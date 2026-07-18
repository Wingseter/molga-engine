# 게임 제작 P2 후속 구현 계획 — 2D 출력 품질과 렌더 정렬

> 작성일: 2026-07-17
>
> 상태: **완료** (2026-07-18)
>
> 기준 문서: `docs/plan/2026-07-12_game_production_gap_analysis.md`
>
> 완료 규칙: 아래 세 축의 구현과 Debug/Release/ASan/UBSan 전체 빌드·CTest,
> 에디터 Play smoke, 패키지 런타임 E2E, `git diff --check`가 모두 통과한 뒤에만
> 갭 분석의 해당 세 항목을 완료 처리한다.

## 1. 범위와 구현 순서

이번 P2 후속 범위는 다음 세 항목으로 고정한다.

1. Sorting Layer / Order in Layer 완전 연결과 월드 렌더러 공통 Y-sort
2. opt-in Pixel Perfect Camera
3. opt-in 고정 논리 출력과 런타임 정수배 표시

구현 순서는 정렬 계약 → Y-sort·피킹 → Camera → 출력·입력 → 통합 검증이다.
기존 프로젝트는 기본적으로 Native 출력과 Ortho Camera 동작을 유지한다.

## 2. Sorting Layer와 Y-sort

- `SortKey`는 `cameraPass → sorting layer → sorting order → Y → submission` 순으로
  비교한다. 낮은 값이 먼저 렌더되고 높은 값이 화면 앞에 놓인다.
- `ProjectSettings.sortingLayers`의 저장 순서를 렌더 순서로 사용한다. 로드 시 빈 이름과
  중복을 제거하고 `Default`를 정확히 하나 보장한다.
- 월드 컴포넌트에는 layer 이름을 저장하고 제출 직전에 현재 프로젝트 목록 index로
  해석한다. 삭제된 이름은 원본을 보존한 채 `Default`로 렌더하며 이름별 한 번만 경고한다.
- 공통 `SortMode2D { Fixed, YAxis }`, `WorldSortSettings2D`, 정렬 키 생성 헬퍼를 사용한다.
  직렬화 필드는 `sortingLayer`, `sortingOrder`, `sortMode`, `ySortOffset`의 평면 구조다.
- Y-sort 값은 Transform world Y와 offset의 합이다. +Y가 아래쪽이므로 큰 Y가 나중에
  그려진다. NaN/Inf는 0으로 정규화한다.
- SpriteRenderer, TextRenderer2D, ParticleSystem emitter, MarrowRenderer는 컴포넌트
  단위 Y-sort를 사용한다. 내부 glyph/particle/skeleton 순서는 유지한다.
- TilemapRenderer는 Sorting Layer와 기존 base/layer offset만 적용하고 셀·행 Y-sort는
  제공하지 않는다. UI는 기존 최종 pass와 Canvas/element order를 유지한다.
- Scene View와 Game Output은 같은 컴포넌트 수집 순서를 사용한다. Scene View의 edit-mode
  particle preview만 동일 컴포넌트 위치에서 대체한다.
- Scene Picker는 Sprite의 resolved layer/order/Y/submission 역순으로 실제 최상단 Sprite를
  선택한다. Inspector Sorting Layer는 ProjectSettings 기반 동적 enum이며 missing 이름을
  경고 항목으로 보존하고 기존 batch Undo/prefab override 경로를 사용한다.

## 3. Pixel Perfect Camera

- Camera에 opt-in `pixelPerfect=false`, `pixelZoom=1`(1–64)을 추가한다.
- 활성화 시 논리 화면 높이는 `logicalHeight / pixelZoom`이고 렌더 위치는
  `round(worldPosition * pixelZoom) / pixelZoom`으로 스냅한다.
- 스냅은 해당 프레임 Camera2D 렌더 상태에만 적용하며 Transform과 follow/smoothing 상태를
  변경하지 않는다.
- 비활성화하면 기존 `height / (2 * orthoSize)` projection과 기존 줌 제한을 그대로 사용한다.
  Inspector는 활성 모드에 따라 Ortho Size 또는 Pixel Zoom만 노출한다.
- 현재 픽셀 기반 sprite/physics world 단위를 유지하며 assets-PPU 변환은 도입하지 않는다.

## 4. 고정 논리 출력과 입력 매핑

- Build Profile에 `GameOutputScaleMode { Native, IntegerFit }`을 추가하고 기본은 Native로 한다.
  Build width/height는 IntegerFit 논리 해상도이자 초기 GLFW 창 크기다.
- 순수 계산 타입 `OutputPresentationLayout`은 논리/실제 크기, 정수 scale, 중앙 content rect,
  crop 여부와 framebuffer→논리 pixel 변환을 제공한다. 오른쪽·아래 경계는 exclusive다.
- IntegerFit 배율은
  `max(1, floor(min(framebufferWidth/logicalWidth, framebufferHeight/logicalHeight)))`로 고정한다.
  남는 영역은 검게 채우고 실제 framebuffer가 더 작으면 1× 중앙 crop하며 크기별 한 번 경고한다.
- 공통 `GameOutputRenderer`는 Native에서는 현재 target에 직접 렌더하고 IntegerFit에서는
  논리 FBO에 world/UI를 렌더한 뒤 `GL_NEAREST`로 현재 target에 blit한다. framebuffer,
  viewport, scissor, sRGB 상태는 모든 종료 경로에서 복구한다.
- Runtime target은 실제 physical framebuffer다. Game View target은 선택 preset이며 Build
  Resolution은 1× preview다. Game View 100%는 target texel을 1:1로 표시하고 Fit의 fractional
  panel preview는 유지한다.
- 입력은 window/panel 좌표 → physical target → presentation rect → logical game pixel 순으로
  한 번만 변환한다. Script와 UISystem은 같은 논리 좌표를 받는다. bars 밖과 0×0 framebuffer는
  pointer/capture만 무효화하고 keyboard/gamepad와 simulation은 유지한다.
- 기존 `resizable`을 Build UI, package config parser, `WindowConfig`, GLFW hint까지 연결한다.
  VSync와 실행 중 해상도·전체화면 변경 API는 범위에서 제외한다.

## 5. 스키마, 호환성, 검증

- 공개 계약은 `SortMode2D`, `WorldSortSettings2D`, `OutputPresentationLayout`,
  `GameOutputScaleMode`, `GameOutputRequest`, 확장된 `GameOutputResult`, Camera의
  pixel-perfect getter/setter다.
- Build Profile과 package `game.json`은 schema v2로 저장한다. v1은 Native와 기존 resizable
  기본값으로 마이그레이션한다. 구 scene/prefab의 새 정렬 필드 기본은
  `Default`/`Fixed`/0이며 기존 sortingOrder와 Tilemap/Particle/UI 형식은 유지한다.
- unit은 layer 복구/우선순위/NaN/직렬화/Picker/Inspector, IntegerFit 레이아웃·경계·HiDPI,
  Camera 스냅·회귀, v1→v2 config를 검증한다.
- GL은 nearest 확대, bars/crop, 논리 FBO와 GL 상태 복구 및 Scene/Game 연속 렌더를 검증한다.
- smoke/E2E는 실제 픽셀 정렬, Game View/runtime presentation 일치, bars/crop 입력과 Script/UI
  좌표 일치를 검증한다.

픽셀 정확성 보장은 Camera와 최종 출력 경로에 적용된다. 원본 Sprite까지 선명하려면 Nearest
texture import와 축 정렬된 정수 Transform이 필요하다. 임의 회전·비정수 scale, SortingGroup,
per-tile/per-particle Y-sort, 라이팅·포스트 프로세싱·멀티 카메라는 후속 범위다.

## 6. 완료 검증

- Debug, Release, ASan, UBSan 전체 빌드가 성공했다.
- 각 preset의 CTest가 모두 `75/75` 통과했다. 기존 74개 기준선과 이번 후속에서 추가한
  `test_world_sort`를 함께 재검증했다.
- 실제 GL 검증에서 Sorting Layer/Order/Y의 픽셀 순서, nearest 정수배 확대,
  letterbox/crop, 논리 FBO 크기와 framebuffer/viewport/scissor/sRGB 상태 복구를 확인했다.
- `smoke_end_to_end`, `editor_smoke`, `runtime_smoke`와 패키지 script loader 검증이 네
  preset에서 모두 통과했다. schema v2 game config, `resizable`, IntegerFit 출력 경로를
  포함한다.
- 최종 `git diff --check`가 통과했다.
