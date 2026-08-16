# Molga Engine - 프로젝트 현황

## 현재 권위와 지원 범위

Molga Engine은 C++17 기반의 2D 게임 엔진이며, SDL3 호스트 위에서 editor와
standalone runtime을 제공한다. 현재 렌더러 전환 권위 문서는
[`2026-08-16_sdlgpu_metal_production_renderer.md`](plan/2026-08-16_sdlgpu_metal_production_renderer.md)다.
창·입력·게임패드·SDL 수명주기의 선행 기준은
[`2026-08-16_sdl3_platform_migration.md`](plan/2026-08-16_sdl3_platform_migration.md)다.

검증된 생산 경계는 **SDL_GPU Metal on macOS 26.5.1 arm64**로 제한한다.
`CMAKE_OSX_DEPLOYMENT_TARGET=11.0`은 빌드 호환 목표일 뿐 macOS 11 실행 지원
증거가 아니다. Linux Vulkan과 Windows D3D12는 configure/build/unit/capability
CI 범위이며 전체 pixel/package/editor E2E는 후속 qualification의
`NOT REQUIRED` 항목이다.

## 기술 스택

| 항목 | 기술 |
|---|---|
| 언어 | C++17 |
| 빌드/테스트 | CMake 3.27+, CTest, Debug/Release/ASan/UBSan presets |
| 플랫폼 | SDL 3.4.14 static |
| 생산 그래픽스 | SDL_GPU/Metal 단일 backend |
| 셰이더 공급망 | HLSL descriptor → pinned SDL_shadercross host tool → MSL/SPIR-V/DXIL bundle |
| 에디터 UI | Dear ImGui Docking + `imgui_impl_sdl3` + `imgui_impl_sdlgpu3` |
| 오디오 | miniaudio |
| 데이터/이미지 | nlohmann/json, stb_image |
| 선택 통합 | Marrow runtime via explicit `MOLGA_MARROW_DIR` |

OpenGL, GLAD, GLSL, OpenGL ImGui backend와 런타임 shader compilation은 active
code/build/package 경로에 포함하지 않는다. 자동 backend fallback이나 프로젝트별
graphics backend 선택도 제공하지 않는다.

## 소스 구조

```text
src/
├── main.cpp                    # editor 진입점
├── runtime_main.cpp            # packaged runtime 진입점
├── Common/                     # 타입, 로깅, SHA-256, 수학 유틸리티
├── Core/                       # SDL host, scene, package/config/smoke report
├── Rendering/
│   ├── GraphicsDevice.*        # non-virtual facade+pImpl SDL_GPU RHI
│   ├── RenderTarget.*          # 명시적 attachment와 texture view
│   ├── Renderer.*              # frame streaming vertex/index draw
│   ├── ShaderBundle.*          # manifest/artifact/hash 검증
│   ├── Shader.*                # immutable bundle entry와 revision/layout
│   ├── Texture.*               # generation texture+sampler handles
│   ├── GameOutputRenderer.*    # camera/output/fallback orchestration
│   └── Lighting/PostProcess/...# 2D lighting, shadow, ordered postfx
├── Editor/
│   ├── ImGuiLayer.*            # SDL3 + SDL_GPU backend
│   ├── ImGuiTextureBridge.*    # editor preview 전용 native bridge
│   └── Windows/                # Game/Scene/Animation 등 editor windows
├── ECS/Components/             # sprite, tilemap, particle, Marrow components
├── Systems/                    # input, particle, audio
├── Physics/                    # collision/Box2D integration
├── Scripting/                  # native Script API v3와 package loader
├── UI/                         # world 이후 합성되는 runtime UI
├── Shaders/                    # HLSL source + schema-v1 descriptor
└── Tools/ShaderCompiler.cpp    # host-only molga_shaderc

external/
├── SDL/                        # pinned recursive submodule
├── imgui/                      # pinned docking submodule
└── SDL_shadercross/            # pinned recursive, offline tool 전용

tests/                          # 83 CTest: unit/platform/gpu/pixel/smoke/e2e
docs/                           # 현재 권위, 계획과 이력
```

## 실행 구조

```text
CMakeLists.txt
├── molga_core (STATIC)         # backend-neutral engine API + SDL_GPU implementation
├── molga_shaderc (HOST TOOL)   # HLSL bundle compiler; runtime에는 링크하지 않음
├── molga_engine (EXE)          # editor + ImGui SDL_GPU renderer
├── molga_runtime (EXE)         # standalone packaged player
└── tests/                      # unit, GPU readback, editor/package smoke
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

- sprite, Korean text atlas, tilemap chunk, particles, runtime UI, grid, picking과
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

### package와 native Script ABI

- `game.json` schema v4는 `graphics.api=sdlgpu`, `driver=metal`,
  `shaderFormat=msl`, 축약 manifest 경로와 SHA-256을 기록한다.
- macOS package에는 MSL artifact와 축약 manifest만 포함한다. HLSL, SPIR-V,
  DXIL, shader compiler와 GLSL은 포함하지 않는다.
- custom shader 누락, 중복, case collision, compile/hash/manifest 오류는 package
  생성을 중단한다.
- native Script API는 v3이며 이전 native script package는 새 헤더로 재빌드해야 한다.

## 테스트와 qualification

- 현재 suite는 83개 CTest로 구성된다.
- RHI unit은 stale handle, usage, pass order/nesting, deterministic pipeline key,
  upload alignment/overflow, failed resize last-good와 shader reload atomicity를 검사한다.
- SDL_GPU readback fixture는 top-left RGBA pixel을 channel tolerance로 비교하고
  sprite/UV/blend, grid, Korean atlas, tilemap/particle, split/PIP,
  UI-after-world, postfx/HDR, normal map, hard shadow와 pinned Marrow fixture를
  포함한다.
- platform/capability tests는 SDL event, focus, window/HiDPI와 native GPU driver
  capability를 검사한다.
- `smoke_end_to_end`는 editor build → package → 실제 runtime 실행 → schema/hash,
  render/pass counters, physics probe와 final pixel report를 검증한다.
- macOS Debug/Release는 전체 suite, ASan/UBSan은 GUI package E2E 제외 전체 suite가
  승격 게이트다. validation error는 0이어야 한다.

성능 report는 120-frame warm-up 후 600 frame의 CPU p50/p95, draw/batch/pass,
upload bytes, resident/peak memory를 기록하지만 현재 단계의 차단 기준은 아니다.

## 주요 구현 기능

- ECS component factory, hierarchy와 JSON scene serialization
- Camera2D, sprite/spritesheet/animation, text, tilemap, particle와 runtime UI
- 2D lighting, normal map, hard shadow, post-processing, split/PIP camera output
- Box2D 기반 physics와 collision/trigger callback
- miniaudio 기반 effect/music playback
- native script compile/hot reload와 Script API v3 package validation
- docked editor, scene hierarchy/inspector/project browser, Game/Scene/Animation previews
- standalone package builder와 deterministic smoke report

## 현재 브랜치와 기준선

- branch: `finetune`
- SDL3 platform 선행 commit: `dd2cc4b`
- SDL_GPU/Metal Stage 2: 구현 및 macOS 로컬 qualification 완료. 이 문서를
  포함하는 commit이 Stage 2 기준선이다.
