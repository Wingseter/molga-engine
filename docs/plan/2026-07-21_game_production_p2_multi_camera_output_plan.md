# 게임 제작 P2 후속 구현 — 멀티 카메라 출력

> 작성일: 2026-07-21
>
> 상태: **완료 (2026-07-21)**
>
> 기준 문서: `docs/plan/2026-07-12_game_production_gap_analysis.md`
>
> 시작 기준선: Post-processing MVP 완료 시점의 전체 CTest **76/76**
>
> 완료 규칙: Debug/Release/ASan/UBSan 전체 build/CTest, editor/runtime smoke,
> 패키지 E2E와 `git diff --check`가 모두 통과한 뒤에만 갭 분석의 멀티 카메라
> 항목을 완료 처리한다.

## 1. 목표와 호환성 계약

정규화 viewport, depth 합성, 독립 layer mask와 PostFX를 사용하는 출력 카메라를
최대 8개까지 지원한다. 모든 카메라가 끝난 뒤 전역 UI를 논리 출력 전체에 한 번
렌더하며, 완성된 논리 이미지는 기존 IntegerFit/nearest 경로로 한 번 표시한다.

`CameraOutputRole`은 `Disabled`, `Primary`, `Secondary`를 제공한다. `Primary`는
호환용 대표 카메라이며 최고 depth 한 대만 선택되고, `Secondary`는 Primary 없이도
출력된다. 기존 `IsMain`/`SetMain`은 각각 Primary 판정과 Primary/Disabled 전환으로
유지하므로 구 단일 Main Camera 장면의 화면 계약은 바뀌지 않는다.

## 2. Camera 데이터와 마이그레이션

- Camera는 `outputRole`, 좌상단 원점 정규화 `viewport`, 32비트 `cullingMask`를
  직렬화한다. 기본값은 Disabled, 전체 화면, 모든 layer다.
- viewport는 유한한 0–1 내부 좌표와 양수 크기만 허용한다. 런타임의 잘못된
  setter 입력은 기존 값을 보존하고, 손상된 직렬화 값은 전체 화면으로 복구한다.
- 새 저장에는 `isMain`을 기록하지 않는다. `outputRole`이 없는 구 scene/prefab은
  `isMain=true`를 Primary로, 나머지를 Disabled로 읽는다.
- prefab의 구 `Camera.isMain` override는 `Camera.outputRole` override로 정규화한다.
  현대 키와 구 키가 함께 있으면 현대 키가 우선하며, diff도 정규화된 Camera
  기본값을 기준으로 생성한다.

## 3. 선택·배치·입력 계약

GL과 독립적인 `CameraOutputLayout`이 프레임 경계의 출력 참가자, 합성 순서,
논리 픽셀 viewport와 camera view snapshot을 만든다.

- 최고 depth Primary 한 대를 항상 보존한다. 동률이면 scene order상 첫 카메라다.
- 남은 자리는 depth 내림차순, 동률 scene order 순으로 Secondary를 채운다.
  Primary가 없으면 Secondary를 최대 8대 선택한다.
- 실제 합성 배열은 depth 오름차순, 동률 scene order 순이다. 따라서 높은 depth와
  같은 depth의 나중 카메라가 위에 놓인다.
- 각 정규화 경계는 `floor(edge * logicalSize)`로 변환한다. 맞닿은 경계에는 틈이나
  중첩이 없고, 한 축이라도 1픽셀 미만인 viewport는 그 프레임에 렌더하지 않는다.
- 논리 포인터는 합성 역순으로 검사해 최상단 카메라의 viewport-local/world 좌표로
  변환한다. 카메라 instance를 명시한 변환도 제공한다. zoom, rotation,
  pixel-perfect snap은 snapshot 수학에 포함된다.

기존 `Input::GetMouseX/Y`는 전역 논리 좌표로 유지한다. 카메라 상대 입력은
`HasCameraPointer`, `GetPointerCameraObjectId`, `GetCameraPointerX/Y`,
`GetWorldPointerX/Y`로 별도 노출하며 focus 상실과 scene 교체 시 무효화한다.

## 4. 렌더 파이프라인

한 프레임의 출력 순서는 다음과 같다.

1. 전체 논리 target을 불투명 검정으로 한 번 지운다.
2. 선택된 카메라마다 top-left `PixelRect`를 GL viewport/scissor로 변환한다.
3. 해당 rect의 color/depth/stencil을 Camera background로 지우고, viewport 픽셀
   크기로 투영을 준비한 뒤 culling mask와 일치하는 world component만 렌더한다.
   잘못된 GameObject layer는 호환상 layer 0으로 취급한다.
4. 활성 PostFX가 있으면 Camera instance ID별 독립 HDR pipeline에서 실행하고
   destination의 해당 rect에 resolve한다. 준비·shader·실행 실패는 해당 카메라만
   direct path로 다시 렌더하며 다른 카메라에는 전파하지 않는다.
5. 모든 카메라가 끝나면 전역 ECS UI를 mask/PostFX 밖에서 전체 논리 출력에 한 번
   렌더한다.
6. IntegerFit은 기존 검은 bars/crop과 `GL_NEAREST` 단일 blit을 그대로 사용한다.

카메라가 없으면 기존 fallback 배경과 UI를 출력한다. 투명 camera stacking이나
clear mode는 제공하지 않으며, 각 카메라 clear는 아래 합성을 rect 전체에서 교체한다.
렌더 뒤에는 framebuffer, viewport, scissor, sRGB, blend/depth/stencil/cull 및
관련 write/clear 상태를 복구한다.

`GameOutputResult::cameraResults`는 카메라 object/instance ID, 역할, depth, viewport,
렌더 여부, PostFX 적용·fallback·pass 수를 합성 순서로 기록한다. 기존
`mainCamera`는 선택된 Primary만 가리키고 PostFX 필드는 카메라별 결과의 합계다.
논리 출력 FBO 실패만 `allocationFailed`이며 카메라 PostFX 실패는 별도 fallback으로
보고한다.

## 5. 에디터와 관측성

- Camera Inspector는 Output Role, Viewport X/Y/Width/Height, Full/Left/Right/Top/
  Bottom/PIP preset과 LayerMask popup을 제공한다.
- LayerMask는 All/Nothing과 ProjectSettings의 32개 layer 이름을 표시한다.
  단일·멀티 편집 모두 component snapshot batch Undo와 prefab override 경로를 쓴다.
- 다중 Primary, 선택 가능한 카메라 8대 초과, 논리 해상도에서 사라지는 viewport,
  missing/invalid PostFX profile을 경고한다.
- Game View와 packaged runtime은 같은 `GameOutputRenderer` 합성을 사용한다.
- Scene View는 editor camera 렌더와 선택된 Primary의 기존 FX preview를 유지한다.
  실제 분할 합성 대신 출력 카메라의 world frustum과 normalized viewport inset을
  gizmo로 표시한다.
- `RenderStats`/Profiler는 출력 camera pass와 합산 PostFX pass를 기록한다.
  runtime smoke report는 선택·렌더·PostFX·fallback 카메라 수를 기록한다.

## 6. 검증 범위

- Unit: role/viewport/mask 직렬화, 구 scene/prefab/override 마이그레이션, Primary와
  Secondary-only 선택, depth 동률과 8대 제한, 홀수 해상도 floor 경계, layer mask,
  topmost/명시적 pointer 변환, zoom/rotation/pixel-perfect, 입력 초기화, Inspector
  descriptor와 multi Undo/prefab override.
- GL: 좌우 split, PIP depth/scene 순서, rect clear와 바깥 검정, 카메라별 mask,
  독립 PostFX와 pipeline cache 정리, 카메라 국소 fallback, UI 분리, IntegerFit
  nearest/bars/crop, Native/IntegerFit resize, 1×1/홀수 크기와 GL 상태 복구.
- Smoke/E2E: stage2의 Primary/Secondary가 서로 다른 viewport/layer mask를 사용하고
  Primary에만 PostFX를 적용한다. 패키지 runtime report에서 선택 2, 렌더 2,
  PostFX 1, fallback 0을 확인하면서 기존 UI scene transition, 입력, 저장과 asset
  package 계약을 함께 회귀 검증한다.

## 7. 비목표

RenderTexture와 사용자 camera target, 투명 camera stack/clear mode, 카메라 귀속 UI와
world-space Canvas, 자동 split-screen 재배치, 카메라 전환·blend·shake, 카메라별
presentation 배율, 다중 창, 8대 초과 출력, Scene View의 실제 멀티 카메라 합성은
이번 범위에 포함하지 않는다.

## 8. 완료 검증

- Debug/Release/ASan/UBSan 네 preset에서 전체 build와 CTest **77/77**이 각각
  통과했다. ASan/UBSan 진단은 없었다.
- Debug GL 회귀는 `test_framebuffer_gl`의 **13 test cases / 375 assertions**를
  통과했다. 좌우 split, PIP depth, viewport clear, 독립 PostFX, 국소 fallback,
  destination rect와 GL 상태 복구를 포함한다.
- 네 preset 모두 `editor_smoke`, `runtime_smoke`, 패키지 `smoke_end_to_end`를
  통과했다.
- 패키지 runtime report는 `status=ok`, 선택 2, 렌더 2, PostFX 1, fallback 0,
  최종 frame의 output camera pass 2를 기록했다.
- `git diff --check`를 통과했으며 기준 갭 분석의 멀티 카메라 항목을 완료로
  갱신했다.
