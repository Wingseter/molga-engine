# SDL_GPU/Metal 생산 렌더러 전환 기준 (2026-08-16)

## 권위와 선행 조건

이 문서는 Molga Engine 2단계 렌더러 전환의 현재 권위 문서다. 창, 이벤트,
입력, 게임패드와 SDL 수명주기의 선행 기준은
[`2026-08-16_sdl3_platform_migration.md`](2026-08-16_sdl3_platform_migration.md)다.
선행 변경은 commit `dd2cc4b`로 고정되었고 다음 로컬 기준선을 통과했다.

- macOS 26.5.1 (25F80), arm64, Apple clang 21.0.0
- Debug/Release: 81/81
- ASan/UBSan: package GUI E2E를 제외한 80/80
- `git diff --check`: 통과

Linux와 Windows 결과는 CI 정의와 로컬 macOS 결과로 대신하지 않는다.

## 생산 경계

2단계 완료 시 editor, runtime과 macOS package는 SDL_GPU/Metal만 사용한다.
OpenGL, GLAD, GLSL, `imgui_impl_opengl3`, GL context accessor와 자동 backend
fallback은 같은 최종 변경에서 제거한다. 프로젝트별 backend 선택은 제공하지
않는다.

공식 검증 문구는 **SDL_GPU Metal on macOS 26.5.1 arm64**로 제한한다.
`CMAKE_OSX_DEPLOYMENT_TARGET=11.0`은 빌드 호환 목표이며 macOS 11 실행 검증을
뜻하지 않는다. Linux Vulkan과 Windows D3D12는 configure/build/unit/capability
범위만 유지하며 전체 pixel/package/editor E2E는 `NOT REQUIRED`다.

## 고정 의존성과 셰이더 공급망

| 의존성 | revision | 역할 |
|---|---|---|
| SDL | `147a8ee32dbf9ac02f3794964490687b6bbda1bc` | SDL_GPU device와 platform |
| Dear ImGui | `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | SDL3 + SDL_GPU editor backend |
| SDL_shadercross | `e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba` | offline HLSL 변환과 reflection |
| Marrow | `e4d984535d72b60ad52097a2c067d65c7208b1b2` | 실제 skeletal pixel fixture |

SDL과 SDL_shadercross는 static/vendored로 빌드한다. SDL_shadercross와 DXC는
host tool `molga_shaderc`에만 링크하며 `molga_core`, editor, runtime과 package에
포함하지 않는다. HLSL 원본은 SPIR-V, MSL 1.2, DXIL과 reflection JSON으로
재현 가능하게 변환한다. binding register/set/space는 생성 include만 소유하고
shader source의 수동 register 지정은 거부한다.

bundle manifest는 descriptor/source/tool revision, stage, entry point, reflection
resource count와 source/binding/artifact SHA-256을 기록한다. runtime은 native
driver가 받는 artifact만 읽고 HLSL 또는 GLSL을 컴파일하지 않는다. reload는
staging bundle 전체를 검증한 뒤 directory를 원자 교체하며 실패 시 last-good
shader와 pipeline을 유지한다.

## RHI 계약

`GraphicsDevice`는 non-virtual facade+pImpl이며 public surface에는 generation을
가진 `BufferHandle`, `TextureHandle`, `SamplerHandle`, `PipelineHandle`과 descriptor만
노출한다. SDL native pointer와 숫자 GPU ID는 `ImGuiTextureBridge`를 제외한 일반
렌더 API에서 노출하지 않는다.

- `BeginFrame(WindowId)` 결과는 `Acquired`, `Unavailable`, `Fatal`이다.
- move-only `FrameContext`가 command buffer, pass nesting, draw와 단일
  submit/present를 소유한다.
- pending upload copy pass는 모든 render pass보다 먼저 encode한다.
- minimized swapchain은 `Unavailable`; device loss는 report 후 종료한다.
- 좌표와 readback RGBA 원점은 top-left다.
- resize/reload는 새 리소스 성공 후 handle을 교환하고 실패 시 last-good을
  유지한다.
- 종료 시 GPU idle 후 resource, device, window, SDL 순으로 해제한다.

pipeline cache key에는 shader revision, vertex layout, blend/depth/raster state,
target format과 sample count를 모두 포함한다.

## 패스와 fallback 계약

한 frame은 camera별 shadow layer, world/HDR, lighting, ordered postfx ping-pong,
전역 UI, scale/presentation, ImGui 순으로 encode한다. 기존
`GameOutputRenderer` request/result와 telemetry를 유지한다.

- postfx 실패: 해당 camera direct resolve
- lighting 실패: 해당 camera unlit
- shadow 실패: 해당 light unshadowed

다른 camera와 global UI는 계속 렌더한다. main ImGui draw data는
`ImGui_ImplSDLGPU3_PrepareDrawData` 후 같은 swapchain pass에서 렌더하고 detached
viewport는 공식 backend가 소유한다.

## 패키지와 ABI

`game.json`은 schema v4이며 `graphics.api=sdlgpu`, `driver=metal`,
`shaderFormat=msl`, 축약 manifest 경로와 SHA-256을 기록한다. macOS package에는
MSL artifact와 축약 manifest만 포함하고 HLSL, SPIR-V, DXIL, shader compiler,
GLSL은 포함하지 않는다. custom shader 누락, 중복, case 충돌, compile 실패,
manifest/hash 불일치는 package 생성을 중단한다.

Material의 `shaderName`, blend와 Float/Vec4/Texture property schema는 유지한다.
기존 GLSL custom shader는 자동 변환하지 않는다. native Script API는 v3이며
이전 package는 새 헤더로 명시적으로 다시 빌드해야 한다.

## 완료 게이트

- stale handle, usage, pass order/nesting, deterministic pipeline key, upload
  정렬/overflow, last-good resize, atomic reload와 manifest tamper unit test
- backend-neutral top-left RGBA readback pixel fixture: 일반 색상 channel ±3,
  lighting ±5
- sprite/UV/blend, Korean atlas, tilemap, particle, Marrow, sRGB/HDR,
  split/PIP, UI-after-world, postfx, normal map, hard shadow와 camera-local fallback
- ImGui main/detached viewport, Game/Scene/Animation texture, font atlas update,
  resize/close/HiDPI smoke
- macOS Debug/Release 전체 unit/pixel/editor/package E2E와 validation error 0
- ASan/UBSan은 GUI E2E 제외 나머지 전체
- active source/build/package에서 GLAD, OpenGL 호출/타입, GLSL source와 raw GPU
  ID 0, 그리고 `git diff --check`

성능은 고정 scene 120-frame warm-up 후 600 frame의 CPU p50/p95, draw/batch/pass,
upload bytes와 resident/peak memory를 기록하되 이 단계의 차단 기준으로 쓰지
않는다.

## macOS 로컬 승격 증거 (2026-08-16)

다음 결과는 macOS 26.5.1 (25F80), arm64, Apple clang 21.0.0에서 최종 소스
상태를 새로 build/test한 결과다.

- Debug/Release: 각각 83/83 통과. 두 preset 모두 SDL_GPU pixel, 실제
  `imgui_impl_sdlgpu3` smoke, editor/runtime smoke와 packaged E2E를 포함한다.
- ASan/UBSan: 각각 package GUI E2E만 제외한 82/82 통과. SDL_GPU pixel,
  ImGui main/detached viewport와 pinned Marrow fixture는 제외하지 않았다.
- `test_rendering_sdlgpu`의 15개 case가 RHI stale/order/key/last-good 계약과
  top-left readback, grid, texture/UV/blend, IntegerFit crop/UI, split/PIP,
  ordered postfx/HDR bloom, lighting/hard shadow, normal map, tilemap/particle,
  Korean atlas와 Marrow pixel fixture를 검사한다.
- `test_imgui_sdlgpu`는 Game/Scene/Animation texture bridge, main/detached
  viewport, dynamic font atlas, resize/close와 HiDPI 경로를 실제 GPU pass로
  실행했다.

Release packaged runtime report는 다음 계약을 기록했다.

- API/driver: `sdlgpu` / `metal`; swapchain/output: `BGRA8` / `SRGBA8`
- shader format: `msl`; manifest SHA-256:
  `7f16e1ba2899deb8519acfd2774435654dcd210570c0cf21463d7089733599d5`
- GPU validation: enabled, error count `0`
- final-frame telemetry: copy `1`, render `20`, draw `21`, upload `2656` bytes
- final pixel probe: top-left RGBA `(56, 80, 105, 255)`
- fallback: postfx/lighting/shadow camera count 모두 `0`
- 120-frame warm-up + 600-frame 측정: CPU p50 `8.26475 ms`, p95
  `9.316208 ms`, draw `12600`, batch `3000`, render pass `12000`, upload
  `1593600` bytes, resident `126550016` bytes, peak `145145856` bytes

정적 승격 감사에서 active source의 GL 호출/타입, GLAD, OpenGL ImGui backend와
GLSL source는 0이었다. package shader bundle은 schema v1 manifest와 MSL
artifact 22개만 포함하며 HLSL, SPIR-V, DXIL, GLSL과 `molga_shaderc`를 포함하지
않는다. editor/runtime/package executable은 Metal framework에 링크되고
ShaderCross, DXC 또는 GL undefined symbol을 갖지 않는다. `git diff --check`도
통과했다.

Linux/Windows 실제 CI 결과는 이 로컬 macOS 증거에 포함하지 않는다. workflow는
각 플랫폼의 configure/build/unit/capability 경계를 유지하지만 실행 결과가
생기기 전에는 PASS로 기록하지 않는다.

## 후속 qualification (`NOT REQUIRED`)

- Linux Vulkan 및 Windows D3D12 전체 pixel/package/editor E2E
- Intel/Universal 2와 물리 macOS 11 실행
- device-loss 복구 및 device recreation
- compute와 범용 render graph
