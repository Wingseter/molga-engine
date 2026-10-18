# Molga Engine - 프로젝트 현황

> 갱신일: **2026-10-04 (Asia/Seoul)**
> 확인 기준: branch `system_manage`, HEAD `dab2f78` (2026-10-02, PR #26 병합).
> 이 문서는 진행 상태와 재개 지점의 요약이다. 승인된 설계와 실행 하위 계획을 대체하지 않는다.

## 현재 위치와 즉시 재개 지점

현재 개발의 중심은 SDL_GPU/Metal 렌더러 전환 이후의 **UI/Text Production Backbone**이다.
다국어 텍스트와 UI 레이아웃·렌더링 기반, `Task 12.1`의 입력 계획 및 포인터 소유권
보완까지 코드에 반영돼 있다. 포커스·캡처 상태 관리와 실제 프레임 입력 경로의 전환은
아직 남아 있다.

**다음 순서: Task 11.3 Step 5e 스크롤 수렴성 amendment → Task 12.2 → Task 12.3
→ IME·텍스트 편집 → 에디터 저작·마이그레이션 → 패키징·최종 qualification.**

스크롤 수렴성 수정은 2026-10-01에 방향이 승인됐지만 코드에는 아직 반영되지 않았다.
`Task 12.2` 전에 별도 커밋으로 닫도록 지정된 선행 작업이므로 건너뛰지 않는다.
**현재 HEAD의 전체 테스트 통과는 재확인되지 않았다.** 구현 상태와 검증 상태는 아래에서
별도로 기록한다.

## 현재 권위와 지원 범위

Molga Engine은 C++17 기반의 2D 게임 엔진이며, SDL3 호스트 위에서 editor와
standalone runtime을 제공한다.

현재 UI/Text 작업의 권위 문서는 다음과 같다.

- 승인 설계: [`2026-08-20-ui-text-production-backbone-design.md`](plans/2026-08-20-ui-text-production-backbone-design.md)
- 마스터 계약·의존 순서·최종 승인 기준: [`2026-08-20-ui-text-production-backbone.md`](superpowers/plans/2026-08-20-ui-text-production-backbone.md)
- 실행 단계: [`01`](superpowers/plans/2026-08-20-ui-text/01-dependencies-unicode.md) → [`02`](superpowers/plans/2026-08-20-ui-text/02-font-shaping-layout.md) → [`03`](superpowers/plans/2026-08-20-ui-text/03-ui-layout-rendering.md) → [`04`](superpowers/plans/2026-08-20-ui-text/04-ui-interaction-ime.md) → [`05`](superpowers/plans/2026-08-20-ui-text/05-editor-authoring-migration.md) → [`06`](superpowers/plans/2026-08-20-ui-text/06-macos-package-policy.md) → [`07`](superpowers/plans/2026-08-20-ui-text/07-parity-qualification.md)

렌더러 전환의 선행 권위는
[`2026-08-16_sdlgpu_metal_production_renderer.md`](plan/2026-08-16_sdlgpu_metal_production_renderer.md)이며,
창·입력·게임패드·SDL 수명주기의 선행 기준은
[`2026-08-16_sdl3_platform_migration.md`](plan/2026-08-16_sdl3_platform_migration.md)다.

기존 렌더러 qualification의 기록상 생산 경계는 **SDL_GPU Metal on macOS 26.5.1 arm64**다.
이는 현재 UI/Text 전체의 qualification 완료를 뜻하지 않는다.
`CMAKE_OSX_DEPLOYMENT_TARGET=11.0`은 빌드 호환 목표일 뿐 macOS 11 실행 지원 증거가 아니다.
Linux Vulkan과 Windows D3D12의 기존 범위는 configure/build/unit/capability CI이며,
전체 pixel/package/editor E2E는 후속 qualification의 `NOT REQUIRED` 항목이다.
UI/Text의 color/variable-font rendering, native macOS accessibility bridge,
signing/notarization, Intel/Universal 2, Linux, Windows, older-macOS qualification은
현재 완료 범위로 주장하지 않는다.

## UI/Text 진행 상태

아래의 **반영됨**은 커밋·코드·하위 계획의 구현 기록을 뜻한다. 이번 점검에서 해당
단계의 모든 승인 기준을 새로 통과했다는 뜻은 아니다.

| 실행 단계 | 범위 | 2026-10-04 상태 |
|---|---|---|
| 01 · Milestone 1–3 | 고정 의존성, ICU 수명주기, Unicode 분석, 26.6 고정소수점 | 선행 구현과 테스트 존재 |
| 02 · Milestone 4–8 | 폰트·패밀리, shaping, atlas, 문단 배치·BiDi hit testing, 공통 텍스트 소비자 전환 | 반영됨. 하위 계획 종료 기록 존재 |
| 03 · Milestone 9–11 | UI identity·스키마, 레이아웃 스냅샷, 클리핑, 스크롤 상태 | 반영됨. Step 5e 수렴성 amendment는 미반영 |
| 04 · Task 12.1 | 순서 있는 UI 이벤트, 스냅샷 N 기준 타깃 계획·결과 합산 | `f9ac229`로 반영됨 |
| 12.1 포인터 소유권 amendment | 장식 UI의 조상 라우팅, 클릭 차단, self-or-ancestor 스크롤 연결 | `5c2fc17`로 반영됨 |
| 04 · Task 12.2 | identity 기반 포커스·캡처·탐색·단일 이벤트 스크롤 | 미구현 |
| 04 · Task 12.3 | `UISystem::ProcessFrame`과 실제 입력 경로 통합 | 미구현 |
| 04 · Milestone 13–14 | IME 소유권, native event/ImGui 연결, 텍스트 편집·표시, 내부 접근성 트리 | 후속 작업 |
| 05 · Milestone 15 | UI/Text 에디터 저작과 명시적 legacy migration | 후속 작업 |
| 06 · Milestone 16–17 | macOS resource-root·font/dependency 검증·패키징 정책 | 후속 작업 |
| 07 · Milestone 18 | editor/runtime/copied app 일치, 성능·최종 qualification | 후속 작업 |

### 최근 반영 사항

공통 텍스트 소비자 전환은 `a0f8ddb`, 하위 계획 02 종료 기록은 `42b5025`에 있다.
`Task 12.1`은 스냅샷 N에 입력 타깃을 고정하고, 결과 합산 시 sequence·stage·타깃을
검증하는 기반을 추가했다. 컴포넌트 교체를 놓치던 레이아웃 캐시 identity와
canvas-root 활성 상태 관련 선행 결함도 이 작업의 구현 기록에서 닫혔다.

포인터 소유권 amendment는 장식용 라벨·이미지 위의 press/hover/release를 가장 가까운
상호작용 가능한 소유자로 라우팅한다. 소유자가 없거나 비상호작용/미게시 소유자에
막히면 뒤의 UI로 통과시키지 않는다. release가 스냅샷 N에서 캡처한 동일 identity로
라우팅될 때 클릭으로 판정하며, 스크롤 대상 목록은 자신의 scroll view 또는 조상부터
안쪽→바깥쪽 순서로 고정한다.

### 실제 입력 경로는 아직 legacy

`UIInputRouter::PlanNext`는 존재하지만 Game View와 standalone runtime은 여전히
`UISystem::ProcessInput`을 호출한다. `UISystem`에는 기존 `capturedOwner_`와
`capturedObjectId_`가 남아 있으며 `UIFocusSystem`과 `UISystem::ProcessFrame`은 없다.
따라서 새 라우터의 구현을 실제 실행 경로 전환 완료로 해석하지 않는다.

근거: [`UISystem.h`](../src/UI/UISystem.h),
[`GameViewWindow.cpp`](../src/Editor/Windows/GameViewWindow.cpp),
[`runtime_main.cpp`](../src/runtime_main.cpp).

## 다음 작업과 이월 사항

### 1. Task 11.3 Step 5e — 승인된 스크롤 수렴성 amendment

권위: [하위 계획 03](superpowers/plans/2026-08-20-ui-text/03-ui-layout-rendering.md)의
`Resolution of the Step 5e decision (2026-10-01)`.
`dac2b7e`는 **결정 문서 커밋**이며, 구현 완료 커밋이 아니다.
대상은 `src/UI/UIScrollSystem.cpp`와 `tests/test_ui_scroll.cpp`다.

현재 `StepAxis`는 감쇠에도 `MulQ6NearestAway`를 사용하고 `decayStep`과 `returnStep`을
`[0,64]`로 제한한다. 예를 들어 retained=48이면 속도가
`96 → 72 → 54 → … → 3 → 2 → 2 …`에 머물 수 있고, 작은 overscroll의 복귀량도
0으로 반올림될 수 있다.

| 승인된 변경 | 의도 |
|---|---|
| 속도 감쇠에 한해 `MulQ6TowardZero` 사용 | 반올림으로 생기는 0이 아닌 고정점 제거 |
| 양수 deceleration rate의 decayStep을 최소 1 raw로 제한 | 작은 양수 감속률도 실제 감쇠하도록 보장 |
| 양수 elasticity rate의 returnStep을 최소 1 raw로 제한 | 작은 양수 복귀율도 실제 복귀하도록 보장 |
| overscroll 보정량의 크기를 최소 1 raw, 최대 overscroll 크기로 제한 | 복귀 정체를 없애되 합법 경계를 넘어가지 않도록 제한 |

재개 시 닫아야 할 완료 조건은 다음과 같다. 아래 체크박스는 승인 여부가 아니라
**구현·검증 완료 여부**를 나타내며, 현재는 모두 미완료다.

- [ ] 승인된 네 가지 수식 변경을 구현하고 기존 hand fixture와 settle clause를 보존한다.
- [ ] 승인된 rate·초기 상태 격자에서 유한 tick 안에 속도 0 및 합법 offset에 도달하는 수렴성 테스트를 추가·통과시킨다.
- [ ] 네 가지 변경을 각각 제거하면 해당 테스트가 실패하는지 독립적으로 mutation-check한다.
- [ ] `test_ui_scroll`의 완료 결과를 확보하고, `Task 12.2` 전에 별도 구현 커밋으로 닫는다.

승인 근거는 `03-ui-layout-rendering.md:3875–3919`, 미반영 코드의 근거는
`src/UI/UIScrollSystem.cpp:610–670`이다. 두 범위는 이번 문서 보완에서도 직접 대조했다.

별도로 `test_ui_scroll`은 제공된 2026-10-04 선행 점검 기록에서 명령 시간제한 안에 완료되지 않았다.
지연 위치를 확인해야 하며, 수렴성 결함이 이 실행 지연의 원인이라고 단정하지 않는다.

### 2. Task 12.2 — 포커스·캡처·탐색·단일 이벤트 스크롤

권위: [하위 계획 04](superpowers/plans/2026-08-20-ui-text/04-ui-interaction-ime.md)의
`Task 12.2`. `UIFocusSystem.{h,cpp}`와 `test_ui_focus.cpp`를 추가하고,
`UIInputRouter`, `UIScrollSystem`, `UIButton`을 확장한다.

상태는 `{windowId,worldGeneration}`별로 분리하고 타깃은 전체 identity
`{worldGeneration,objectId,componentRuntimeTypeId,componentInstanceId}`로 관리한다.
컴포넌트 교체, 월드 해제, native focus loss, pointer departure를 구분하며,
명시적/자동 navigation과 같은 입력 묶음 안의 포커스 projection을 구현한다.
같은 입력 묶음에서 탐색으로 포커스가 바뀌더라도, 수신 당시 소유자가 기록된 텍스트
이벤트를 새 포커스 대상으로 재전달하지 않는다. 수신 시점의 소유권을 보존한다.

특히 다음 이월 의무를 포함한다.

- `R7`: 현재 `UIButton::ApplyPointerState`가 live `interactable_`로 클릭을 다시
  필터링하므로 snapshot-N 라우팅과 콜백 실행의 판단 주체가 갈린다. hover/pressed/click
  상태를 router-owned table로 옮기며 이 불일치와 capture 상실 뒤 잔여 pressed 상태를 닫는다.
- 실제로 보이는 상태를 바꾸기 전에 runtime invalidation을 획득한다. no-op은 세대를
  올리지 않고, exhaustion은 이전 상태를 보존한다. 기존 getter 호환성은 유지한다.
- Scroll handler는 한 `PlannedUIEvent`만 처리한다. signed wheel/drag/axis 값을 보존하고
  고정된 self-or-ancestor 목록을 안쪽부터 따라가 실제로 소비한 타깃과 ordinal을 보고한다.

### 3. Task 12.3 — 실제 프레임 처리 통합

`UISystem::ProcessFrame`이 ordered-event vector의 유일한 처리 루프가 되도록 하고,
Game View와 standalone runtime을 새 경로에 연결한다. 콜백이 RenderSnapshot N+1을
게시해도 같은 입력 묶음의 타깃 판단과 release arbitration은 끝까지 N을 사용한다.

`R6`의 N/N+1 회귀 테스트, `R11`의 planning-state 창·월드 검증을 포함한다.
이미 구현된 `UIEventDispatchAccumulator`의 명시적 diagnostic sink와 non-fatal
merge rejection 계약을 사용하고 재구현하지 않는다. 하위 계획의 오래된 생성자 예시와
stored-plan vector를 금지하는 모순된 검색 검증도 실제 계약에 맞춰 확인한다.

### 4. 후속 단계의 주의점

`Task 13.2`에는 우클릭·중클릭도 현재 router에서 클릭/캡처로 취급될 수 있는 `R10`이
남아 있다. primary-only는 기존 기록의 권고안이지 이 현황 갱신으로 새로 승인한 정책이 아니다.
`UIInputEvent::control`의 key domain도 12.2/13.2/14.1이 일치시켜야 한다.

`Task 14.1`은 `UISelectable` 없는 bare `UITextInput`이 포인터 포커스를 얻지 못하는
문제를 담당한다. `Task 15.3`의 ScrollView authoring preset에는 빈 viewport 영역에도
hit record를 게시하는 background image가 필요하다.

이후 IME 소유권 조정과 grapheme-safe 편집, 에디터 저작·undoable migration,
검증 실패 시 패키지 생성/시작을 막는 정책, editor/runtime/copied app parity를 순서대로
닫는다. 기존 패키징 기능이 있다는 사실을 새 UI/Text 패키징 정책의 완료로 간주하지 않는다.

## 기술 스택

| 항목 | 기술 |
|---|---|
| 언어 | C++17 |
| 빌드/테스트 | CMake 3.27+, CTest/doctest, Debug/Release/ASan/UBSan presets |
| 플랫폼 | SDL 3.4.14 static |
| 생산 그래픽스 | SDL_GPU/Metal 단일 backend |
| 셰이더 공급망 | HLSL descriptor → pinned SDL_shadercross host tool → MSL/SPIR-V/DXIL bundle |
| 에디터 UI | Dear ImGui Docking + `imgui_impl_sdl3` + `imgui_impl_sdlgpu3` |
| 텍스트 의존성 | pinned HarfBuzz 14.3.1, ICU4C 78.3 + `icudt78l.dat`, text-owned stb_truetype snapshot |
| 텍스트/UI 기반 | shared shaping/layout, glyph-ID atlas, validated signed 26.6 layout, immutable snapshots |
| 오디오 | miniaudio |
| 데이터/이미지 | nlohmann/json, stb_image |
| 선택 통합 | Marrow runtime via explicit `MOLGA_MARROW_DIR` |

OpenGL, GLAD, GLSL, OpenGL ImGui backend와 런타임 shader compilation은 active
code/build/package 경로에 포함하지 않는다. 자동 backend fallback이나 프로젝트별
graphics backend 선택도 제공하지 않는다.

텍스트 의존성은 vendored static 구성을 사용하며 normal configure/build에서 다운로드하지
않는다. 시스템 폰트 탐색과 CoreText fallback은 추가하지 않는다. authored 문자열은
byte-exact로 보존하고 Unicode 분석은 ICU, shaping은 HarfBuzz를 사용한다.

## 소스 구조

```text
src/
├── main.cpp                    # editor 진입점
├── runtime_main.cpp            # packaged runtime 진입점
├── Common/                     # 타입, 로깅, SHA-256, Fixed26_6
├── Core/                       # SDL host, scene, package/config/smoke report
├── Assets/                     # font artifact store, font/font-family assets
├── Text/                       # Unicode, dependency lifetime, shaping/layout/cache
├── Rendering/
│   ├── GraphicsDevice.*        # non-virtual facade+pImpl SDL_GPU RHI
│   ├── RenderTarget.*          # 명시적 attachment와 texture view
│   ├── Renderer.*              # frame streaming vertex/index draw
│   ├── ShaderBundle.*          # manifest/artifact/hash 검증
│   ├── Shader.*                # immutable bundle entry와 revision/layout
│   ├── Texture.*               # generation texture+sampler handles
│   ├── TextRenderer.*          # 공통 텍스트 서비스와 렌더 수집
│   ├── FontAtlas.*             # glyph-ID atlas
│   ├── GpuRetirementQueue.*    # GPU fence 기반 자원 회수
│   ├── GameOutputRenderer.*    # camera/output/fallback orchestration
│   └── Lighting/PostProcess/...# 2D lighting, shadow, ordered postfx
├── Editor/
│   ├── ImGuiLayer.*            # SDL3 + SDL_GPU backend
│   ├── ImGuiTextureBridge.*    # editor preview 전용 native bridge
│   └── Windows/                # Game/Scene/Animation 등 editor windows
├── ECS/Components/             # world/UI authored components, Marrow
├── Systems/                    # input, particle, audio
├── Physics/                    # collision/Box2D integration
├── Scripting/                  # native Script API v3와 package loader
├── UI/                         # identity, layout snapshots, render collector, scroll/router
├── Shaders/                    # HLSL source + schema-v1 descriptor
└── Tools/ShaderCompiler.cpp    # host-only molga_shaderc

external/
├── SDL/                        # pinned recursive submodule
├── imgui/                      # pinned docking submodule
├── SDL_shadercross/            # pinned recursive, offline tool 전용
├── harfbuzz/                   # pinned text dependency
├── icu/                        # pinned Unicode dependency
└── text/                       # text-owned rasterizer snapshot

resources/text/                # locked ICU data와 dependency contract
resources/licenses/            # text dependency notices/licenses
tests/                         # Debug에 등록된 CTest 109개 (2026-10-04)
docs/                          # 현재 권위, 계획과 이력
```

## 실행 구조와 선행 구현

```text
CMakeLists.txt
├── molga_core (STATIC)         # backend-neutral engine API + SDL_GPU + Text/UI
├── molga_shaderc (HOST TOOL)   # HLSL bundle compiler; runtime에는 링크하지 않음
├── molga_engine (EXE)          # editor + ImGui SDL_GPU renderer
├── molga_runtime (EXE)         # standalone packaged player
└── tests/                      # unit, text/UI, GPU readback, editor/package smoke
```

### SDL host와 RHI

- `Bootstrap`이 SDL init, window, SDL_GPU device, engine subsystem 순으로 생성하고
  종료 시 GPU idle, resource, device, window, SDL 순으로 해제한다.
- `GraphicsDevice` public API에는 generation handle과 descriptor만 노출한다.
  `SDL_GPU*` native pointer는 editor 내부 `ImGuiTextureBridge` 밖으로 유출하지 않는다.
- `BeginFrame(WindowId)`는 `Acquired`, `Unavailable`, `Fatal`을 구분한다. minimized
  swapchain은 `Unavailable`, device loss는 보고 후 종료하며 backend 전환을 시도하지 않는다.
- move-only `FrameContext`가 upload, pass nesting, viewport/scissor, binding, draw와
  단일 submit/present를 소유한다. upload copy pass는 모든 render pass보다 앞선다.
- public 좌표, texture origin과 GPU readback은 top-left RGBA다.

### 2D 렌더 경로

- sprite, text atlas, tilemap chunk, particles, runtime UI, grid, picking과
  Marrow skeletal geometry가 동일한 SDL_GPU command stream을 사용한다.
- `Renderer`와 `SpriteBatcher`는 frame streaming vertex/index buffer와 stable
  shader/texture/material ID 기반 draw packet을 사용한다.
- camera별 shadow → world/HDR → lighting → ordered postfx ping-pong → global UI →
  presentation → ImGui 순으로 encode한다.
- postfx 실패는 해당 camera direct resolve, lighting 실패는 해당 camera unlit,
  shadow 실패는 해당 light unshadowed로 한정한다.
- pipeline cache key는 shader revision, vertex layout, blend/depth/raster,
  target format과 sample count를 포함한다.

### 셰이더와 material

- engine shader와 프로젝트 `Assets/Shaders/*.shader.json`은 schema v1 descriptor와
  HLSL source를 사용한다.
- `molga_shaderc`가 generated binding include, 16-byte aligned parameter layout,
  MSL 1.2/SPIR-V/DXIL, reflection JSON과 SHA-256 manifest를 만든다.
- 수동 HLSL `register`와 런타임 HLSL/GLSL compilation은 거부한다.
- editor의 Reload Shaders는 staging bundle을 전부 검증한 후 원자 교체하며,
  실패하면 last-good shader/pipeline을 유지한다.
- Material의 `shaderName`, blend와 Float/Vec4/Texture property schema는 유지한다.
  기존 custom GLSL은 자동 변환하지 않으며 HLSL descriptor로 수동 이식해야 한다.

### 에디터와 출력

- ImGui main draw data는 `ImGui_ImplSDLGPU3_PrepareDrawData` 이후 main swapchain
  render pass에 기록한다. detached viewport는 공식 SDL_GPU backend가 관리한다.
- Game View, Scene View와 Animation preview는 `ImGuiTextureBridge`만 사용한다.
- GameOutput request/result, per-camera fallback과 telemetry 계약은 유지한다.
- resize와 hot reload는 새 allocation 검증 후 교체하며 실패 시 last-good
  resource를 보존한다.

### 기존 package와 native Script ABI

- `game.json` schema v4는 `graphics.api=sdlgpu`, `driver=metal`,
  `shaderFormat=msl`, 축약 manifest 경로와 SHA-256을 기록한다.
- macOS package의 shader bundle에는 MSL artifact와 축약 manifest만 포함한다.
  HLSL, SPIR-V, DXIL, shader compiler와 GLSL은 포함하지 않는다.
- custom shader 누락, 중복, case collision, compile/hash/manifest 오류는 package
  생성을 중단한다.
- native Script API는 v3이며 이전 native script package는 새 헤더로 재빌드해야 한다.

이는 선행 패키징 기능의 설명이다. `Game.app/Contents/Resources`를 유일한 release
resource root로 삼고 font/dependency/notice/coverage를 검증하는 새 UI/Text 정책은
하위 계획 06–07의 후속 구현·qualification 대상이다.

### 기타 선행 기능

ECS component factory, hierarchy, JSON scene serialization, Camera2D,
sprite/spritesheet/animation, tilemap, particles, Box2D physics와 collision/trigger
callback, miniaudio effect/music, native script compile/hot reload,
docked editor의 hierarchy/inspector/project browser, standalone package builder와
smoke report가 존재한다. 이 기능 목록은 이번 갱신에서 전부 재검증한 목록이 아니다.

## 검증 현황과 완료 기준

### 2026-10-04 선행 코드 점검 기록 — 이번 문서 보완의 실행 결과와 구분

아래 build/test 결과는 사용자 제공 점검 보고와 기존 현황 문서에 기록된 같은 HEAD의
선행 실행 결과를 이관한 것이다. 이번 문서 보완에서 빌드·개별 테스트를 재실행해 얻은
결과가 아니다. 직접 다시 확인한 범위는 다음 소절에 별도로 기록한다.

| 검증 | 관찰 결과 | 해석 |
|---|---|---|
| `ctest --preset debug -N` | 109개 등록, 종료 코드 0 | 목록 확인일 뿐 통과 증거 아님 |
| `cmake --build --preset debug -j 4` | 약 66.7초 후 SIGTERM, 전체 빌드 미완료 | 성공으로 기록하지 않음. 컴파일 오류가 원인이라고도 단정하지 않음 |
| `cmake --build --preset debug --target test_ui_input test_ui_layout test_ui_scroll -j 4` | 세 타깃 빌드 성공, 종료 코드 0 | 해당 타깃 빌드만 확인 |
| UI 3종 선택 CTest 실행 | `test_ui_layout` 12.07초 통과 뒤 scroll 실행 중 55초 명령 제한 종료 | 선택 suite 전체 통과 아님 |
| `ctest --preset debug -R '^test_ui_input$' --output-on-failure` | 16.25초, 1/1 통과 | input 단독 통과 |
| `ctest --preset debug -R '^test_ui_scroll$' --output-on-failure` | 65초 명령 제한으로 SIGTERM, 완료 결과 없음 | 지연 원인 미확인. assertion 실패나 수렴성 결함과 동일시하지 않음 |
| 전체 Debug / Release / ASan / UBSan | 선행 점검의 전체 통과 결과 없음 | 현재 HEAD의 최종 qualification 미확인 |

### 이번 문서 보완에서 직접 재확인한 범위

| 확인 | 결과 | 범위의 한계 |
|---|---|---|
| 현재 workspace와 `git log -1` | `/Users/kwon/Workspace/C/molga-engine`, branch `system_manage`, HEAD `dab2f78` (2026-10-02, PR #26 병합) | 기존 현황의 기준점과 일치 |
| 승인 문서와 `UIScrollSystem::StepAxis` 대조 | 네 가지 수렴성 수정은 승인됐으나 현재 코드는 여전히 이전 반올림·최솟값 규칙 사용 | 구현 미반영을 재확인했으며 테스트 지연의 원인까지 확인한 것은 아님 |
| `ctest --preset debug -N` | 종료 코드 0, 등록된 테스트 109개 | 테스트 목록만 확인했으며 테스트 본체는 실행하지 않음 |

이번 요청에서는 진행 현황 문서만 보완했다. 엔진 빌드, input/layout/scroll 테스트 실행,
전체 Debug·Release·ASan·UBSan 재검증은 수행하지 않았다. 기존 통과 기록을 이번
문서 보완의 새 통과 증거로 승격하지 않는다.

### 과거 기록 — 현재 HEAD의 새 통과 증거와 구분

[하위 계획 04의 Task 12.1 구현 기록](superpowers/plans/2026-08-20-ui-text/04-ui-interaction-ime.md)은
2026-10-01 Debug 109/109 통과와 `test_ui_input`/`test_ui_layout`의 ASan·UBSan 통과를
기록한다. [하위 계획 03의 Task 11.3 구현 기록](superpowers/plans/2026-08-20-ui-text/03-ui-layout-rendering.md)은
2026-09-09 108/108 통과와 scroll/layout sanitizer 결과를 기록한다.
이전 문서의 83개는 렌더러 단계의 과거 기준이며 현재 등록 수가 아니다.

### 테스트 범위와 승격 게이트

RHI unit은 stale handle, usage, pass order/nesting, pipeline key, upload
alignment/overflow, failed resize last-good와 shader reload atomicity를 다룬다.
GPU readback fixture는 top-left RGBA와 channel tolerance를 사용하고 sprite/UV/blend,
grid, Korean text, tilemap/particle, split/PIP, UI-after-world, postfx/HDR, normal map,
hard shadow, pinned Marrow fixture를 포함한다. platform/capability tests는 SDL event,
focus, window/HiDPI와 GPU capability를 다룬다.
`smoke_end_to_end`는 editor build → package → runtime 실행과 schema/hash,
render/pass counters, physics probe, final pixel report를 검증한다.
텍스트·UI 테스트는 의존성·폰트·Unicode·shaping/layout·atlas·identity·snapshot·clip·scroll·input으로 확장돼 있다.

선행 렌더러 승격 게이트는 macOS Debug/Release 전체 suite, ASan/UBSan의 GUI package E2E
제외 suite와 validation error 0이었다. UI/Text 전체 완료 여부는 마스터 및 하위 계획 07의
최신 acceptance matrix와 fresh Debug/Release/ASan/UBSan 증거로 판단한다.
기존 build directory, 체크박스, 이전 통과 수만으로 완료 처리하지 않는다.

기존 렌더러 성능 report의 120-frame warm-up/600-frame 측정은 당시 비차단 지표였다.
이 정책을 현재 UI/Text에 그대로 적용하지 않는다. UI/Text의 승인된 성능 게이트와
editor/runtime/copied app parity 증거는 하위 계획 07의 최종 qualification 대상이다.

## 최근 기준 커밋과 작업 트리

| 커밋 | 날짜 | 의미 |
|---|---|---|
| `dab2f78` | 2026-10-02 | PR #26 병합, 이번 현황 확인 HEAD |
| `5c2fc17` | 2026-10-02 | 비상호작용 UI의 nearest-interactable-owner 라우팅 구현 |
| `dac2b7e` | 2026-10-01 | Step 5e 수렴성 수정 결정 문서화 — 구현은 미반영 |
| `f9ac229` | 2026-10-01 | Task 12.1 ordered events와 snapshot-N target planning |
| `e740457` | 2026-09-09 | Task 11.3 identity-keyed scroll state |
| `428aa69` | 2026-09-09 | render queue/GPU pass의 snapshot scissor 적용 |

SDL3 platform의 선행 commit은 `dd2cc4b`다. SDL_GPU/Metal Stage 2의 구현·로컬
qualification은 기존 기준선의 이력이며 현재 UI/Text 작업의 완료를 의미하지 않는다.

사용자 제공 선행 코드 점검에서는 `.serena/project.yml`만 미커밋 상태였으나,
**이번 문서 보완 요청을 시작할 때는 `.serena/project.yml`과 `docs/now_going.md`
양쪽에 기존 미커밋 변경이 있었다.** 두 시점의 작업 트리 상태를 혼동하지 않는다.

기존 현황 문서의 변경 내용을 유지하면서 재개 조건과 검증 기록의 출처를 보완했다.
이번 요청의 수정 대상은 `docs/now_going.md`뿐이며 `.serena/project.yml`은 수정하지
않는다. 엔진 코드 수정, 새로운 설계 결정, commit/push는 수행하지 않는다.
