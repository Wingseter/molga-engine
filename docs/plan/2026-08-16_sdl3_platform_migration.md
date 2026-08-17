# SDL3 플랫폼 전환 기준 (2026-08-16)

## 결론

Molga Engine의 창·이벤트·입력·게임패드·ImGui 플랫폼 계층은 GLFW에서
SDL3로 전환한다. 현재 생산 렌더러는 **SDL3 + OpenGL 3.3 Core + GLAD**이며,
기존 조명·그림자·후처리·멀티 카메라 출력 계약을 그대로 유지한다.

SDL_GPU 경로는 native device, swapchain, 셰이더 파이프라인, 텍스처 업로드,
샘플러 바인딩과 draw 제출까지 플랫폼 게이트로 검증한다. 이 경로는 Metal,
Vulkan, D3D12 기반 렌더러를 만들 수 있는 토대이지만, 현재 Molga의 전체
렌더 패스를 SDL_GPU로 실행하지는 않는다. 따라서 현재 상태를 "Vulkan
렌더러 지원"으로 표현하지 않는다.

## 고정된 의존성

| 의존성 | 저장 방식 | 고정 revision | 용도 |
|---|---|---|---|
| SDL | git submodule, static link | `147a8ee32dbf9ac02f3794964490687b6bbda1bc` (`release-3.4.14`) | 창, 이벤트, 입력, 게임패드, OpenGL context, SDL_GPU |
| Dear ImGui | git submodule | `b48d1afbe8ee8b238e2961dc363a949dd7304e23` (Docking) | SDL3 플랫폼 + OpenGL3 renderer backend |
| GLAD | 기존 vendored source | repository revision | 생산 OpenGL 함수 로딩 |

SDL은 정적으로 링크한다. 현재 GameBuilder가 런타임 실행 파일만 복사하는
패키지 계약에 별도 SDL dylib/DLL/so 배포 단계를 추가하지 않아도 된다.

## 구현 경계

### 플랫폼 수명주기

- `EngineInit`은 native 포인터 대신 RAII `EngineHost`를 반환한다.
- `EngineHost`가 SDL window, graphics context/device, gamepad와 이벤트 observer를
  소유한다.
- 종료 순서는 외부 렌더 리소스 해제 후 graphics context/device, window,
  SDL 순서다.
- 이벤트는 프레임 시작에 한 번 펌프하고 ImGui와 엔진 입력 변환기에 같은
  `SDL_Event`를 전달한다.
- 메인 창 close만 애플리케이션 종료로 해석하며 detached ImGui viewport close는
  해당 viewport backend가 처리한다.
- 논리 크기와 픽셀 크기를 분리해 high-DPI scale을 계산한다.

### 입력 계약

- 공개 `Input` 헤더와 스크립트에서 GLFW 타입·상수를 제거했다.
- 키보드는 engine-owned `KeyCode`, 마우스와 게임패드는 의미 기반 enum을 쓴다.
- SDL scancode/button/axis 이벤트를 이 타입으로 중앙 변환한다.
- wheel은 `WindowId`별로 누적·1회 소비하며 Game View의 detached viewport도
  native pointer 없이 구분한다.
- 게임패드는 first-connected 정책으로 열고 제거 시 상태를 해제한 뒤 다음
  장치를 찾는다. trigger는 `[0, 1]`, stick은 `[-1, 1]`로 정규화한다.

### 지속 데이터와 스크립트 ABI

입력 액션은 숫자 GLFW code 대신 상징 이름을 저장하는 schema v2다.

```json
{
  "schemaVersion": 2,
  "actions": [
    {
      "name": "Jump",
      "isAxis": false,
      "bindings": [
        { "device": "Keyboard", "control": "Space", "multiplier": 1.0 }
      ]
    }
  ]
}
```

런타임은 숫자 기반 legacy 문서를 묵시적으로 해석하거나 다시 쓰지 않는다.
프로젝트 소유자가 아래 도구로 명시적으로 변환해야 한다.

```sh
# 변경 가능 여부와 결과만 출력한다.
build/debug/molga_migrate input --project /path/to/project

# timestamp가 붙은 원본 백업을 만든 뒤 schema v2를 원자적으로 반영한다.
build/debug/molga_migrate input --project /path/to/project --apply
```

- 생성 파일을 다시 파싱·검증한 뒤에만 교체한다.
- POSIX는 atomic rename, Windows는 `ReplaceFileW`를 사용한다.
- 알 수 없는 legacy code가 있으면 일부만 변환하지 않고 전체 작업을 실패시킨다.
- package `game.json`은 schema v3, native script API는 v2다. 이전 script package는
  새 헤더로 다시 빌드해야 하며 loader가 버전 불일치를 거부한다.

## 그래픽 경계와 지원 수준

| 경로 | 현재 수준 | 증거/게이트 |
|---|---|---|
| SDL3 + OpenGL 3.3 | 생산 기본값 | GLAD load, 실제 shader/framebuffer 테스트, package E2E |
| SDL_GPU + Metal | 플랫폼 capability | macOS에서 native driver, shader pipeline, texture upload, sampled draw, present |
| SDL_GPU + Vulkan | CI capability 목표 | Linux Mesa/lavapipe + xvfb `sdlgpu` gate |
| SDL_GPU + D3D12 | CI capability 목표 | Windows native `platform` gate |
| Molga renderer on SDL_GPU | 미완료 | `Renderer`, `Texture`, `Framebuffer`, lighting/postfx, ImGui renderer port 필요 |

공개 렌더 헤더에서는 GL 타입과 GLAD include를 제거했다. 그러나 구현 파일의
OpenGL 호출을 단순히 숨긴 것이 renderer port 완료를 뜻하지 않는다. Vulkan
지원 표시는 아래 조건을 모두 만족한 뒤에만 허용한다.

1. Texture/buffer/shader/pipeline/render-target가 backend-owned opaque resource다.
2. sprite batching, font, tilemap, particle, lighting/shadow, post-processing과
   multi-camera presentation이 SDL_GPU 경로에서 동등하게 동작한다.
3. ImGui는 `imgui_impl_sdlgpu3`로 main/detached viewport를 렌더하고 context를
   올바르게 복원한다.
4. HLSL 원본에서 SPIR-V/MSL/DXIL을 재현 가능하게 생성하고 package manifest가
   필요한 artifact를 검증한다.
5. macOS Metal, Linux Vulkan, Windows D3D12의 지정된 E2E가 skip 없이 통과한다.

## 테스트와 CI 게이트

- `unit`: 플랫폼 장치가 필요 없는 계약과 입력 schema/migration
- `migration`: migrator dry-run, 원본 불변, apply, 백업, idempotence
- `platform;opengl`: SDL lifecycle/event/focus/close/HiDPI와 OpenGL host
- `platform;gpu;sdlgpu`: SDL_GPU native device + real swapchain shader draw
- `gpu;opengl`: 실제 GL shader/framebuffer/lighting/postfx 계약
- `smoke;e2e`: 에디터 build → package → 숨김 런타임의 기존 렌더 카운터 계약

CI는 macOS Debug/Release, Linux Debug/Release(xvfb + Mesa), Windows Debug와
macOS ASan/UBSan lane을 정의한다. 플랫폼 또는 GPU 지정 lane에서 context/device
생성 실패는 skip하지 않고 실패다. 아직 이 문서에 실행 결과가 기록되지 않은
OS는 workflow가 존재한다는 이유만으로 지원 완료로 간주하지 않는다.

## 롤백과 제거 기준

GLFW source와 backend는 이 변경에서 제거한다. 롤백은 변경 전 commit으로
되돌리는 revision 경계에서 수행하며, 프로젝트 데이터는 timestamp backup과
schema version으로 분리한다. 새 코드에 GLFW include, native pointer cast 또는
숫자 GLFW binding을 다시 도입하지 않는다.

과거 설계 문서는 당시 판단의 기록으로 유지한다. 현재 지원 주장과 migration
절차는 이 문서와 `docs/now_going.md`를 우선한다.
