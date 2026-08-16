# 게임 제작 P2 후속 구현 — 2D 라이팅·노멀맵·하드 그림자 MVP

> 작성일: 2026-07-25
>
> 상태: **완료**
>
> 기준 문서: `docs/plan/2026-07-12_game_production_gap_analysis.md`
>
> 시작 기준선: 멀티 카메라 출력 완료 시점의 전체 CTest **77/77**
>
> 완료 규칙: Debug/Release/ASan/UBSan 전체 build/CTest, focused GL,
> editor/runtime smoke, 패키지 E2E와 `git diff --check`가 모두 통과한 뒤에만
> 갭 분석의 `2D 라이팅/노멀맵/그림자` 항목을 완료 처리한다.

## 1. 목표와 호환성 계약

출력 Camera별 opt-in 2D 조명을 추가한다. MVP는 Ambient, `PointLight2D`, Sprite
normal map, flat-normal Tilemap, 전용 convex `ShadowOccluder2D`와 hard shadow를
지원한다. 렌더 순서는 `Shadow Masks → 단일 World Pass → Camera PostFX →
Global UI → IntegerFit`으로 고정한다.

기존 Camera는 lighting OFF, Sprite/Tilemap은 Unlit가 기본이다. 조명이 꺼졌거나
Lit receiver가 없는 Camera는 기존 shader와 direct path만 사용하고 lighting
allocation/pass를 만들지 않는다. 따라서 구 scene/prefab의 픽셀 결과와 현재의
멀티 카메라, PostFX, 전역 UI, nearest IntegerFit 계약을 유지한다.

고정 예산은 Camera당 Point Light 8개, shadow light 4개, light당 occluder 64개,
polygon당 vertex 8개다. 초과 항목은 결정적인 순서로 제외하고 warn-once로 남긴다.

## 2. 공개 데이터와 에셋 계약

- Camera는 `lightingEnabled=false`, `ambientColor=White`,
  `ambientIntensity=0.2`를 직렬화하고 각 출력 Camera가 독립적으로 설정한다.
- `PointLight2D` 기본값은 white, intensity 1, radius 128, height 32, falloff 2,
  모든 layer affect mask, shadow OFF, priority 0이다. 유효 범위는 intensity 0–32,
  radius 0.01–1,000,000, falloff 0.1–8이며 위치만 Transform world position을
  따른다.
- 감쇠는
  `pow(clamp(1 - distance / radius, 0, 1), falloff) * intensity`로 고정한다.
  normal 방향은 light의 world 위치와 world-unit `height`로 만든 pseudo-3D
  방향이다.
- `SpriteLightingMode2D { Unlit, Lit }`를 Sprite/Tilemap에 추가한다. Sprite는
  Lit일 때 `normalMapGuid`와 0–2 범위 `normalStrength`를 사용하고 Tilemap MVP는
  flat normal만 사용한다.
- Normal texture는 effective diffuse와 동일 크기·atlas layout이어야 하며 현재
  Sprite UV를 공유한다. Animator frame 교체도 같은 atlas 영역을 자동 사용한다.
  normal RGB는 `[-1,1]`, +X right, +Y up, +Z outward로 decode한다.
- Texture import의 `TextureUsage { Color, NormalMap }`에서 누락 값은 Color다.
  NormalMap은 linear GPU format을 강제하고 sRGB 편집을 잠근다.
  `normalMapGuid`는 정상 import된 NormalMap usage의 TextureImporter만 받는다.
  package dependency 검증은 missing, wrong importer, import failure, wrong usage를
  build 오류로 처리한다.

구 scene/prefab은 Camera lighting OFF와 Sprite/Tilemap Unlit로 canonicalize한다.
Camera/SpriteRenderer/TilemapRenderer snapshot도 diff 전에 기본 키를 정규화하여
새 키가 prefab override로 오인되지 않게 한다.

## 3. 차폐 형상 계약

`ShadowOccluder2D`는 Box와 Polygon을 지원한다. Box 기본은 local offset 0,
size 100×100의 중앙 정렬 rectangle이다. Polygon은 local-space finite vertex
3–8개, strict convex, non-zero-area만 허용하고 CCW로 정규화한다.

Transform의 회전, 음수 scale과 비균일 scale을 적용한 뒤 world winding을 다시
정규화한다. 잘못된 runtime setter는 기존 형상을 보존한다. 손상된 직렬화 polygon은
해당 occluder만 비활성으로 취급하며 Inspector의 복구 preset으로 기본 Box를 만들 수
있다.

Box와 convex polygon의 silhouette edge는 CPU에서 light 반대 방향으로 extrusion한다.
light가 occluder 내부 또는 epsilon 이내 경계에 있으면 해당 light의 mask 전체를
occluded로 만든다.

## 4. 선택과 렌더 파이프라인

GL과 독립적인 camera-local `LightingFrame2D`가 Camera view, ambient, 선택 light,
world-space occluder와 receiver layer metadata를 snapshot한다. 후보 light는
active/enabled이고 Camera culling mask에 GameObject layer가 포함되며 radius가
Camera view와 교차해야 한다.

후보는 priority 내림차순, 동률 scene order 순으로 8개를 선택한다. 그중
`castsShadows=true`인 앞의 4개만 shadow layer를 받으며 나머지는 shadow 없이
조명한다. 하나의 `affectMask`가 receiver와 occluder GameObject layer를 모두
필터링한다. 잘못된 layer 번호는 layer 0으로 취급한다. Occluder는 affect mask와
radius로 걸러 scene order 앞의 64개만 사용한다.

Camera instance ID별 `LightingPipeline2D`는 viewport 크기의 `GL_R8 /
GL_TEXTURE_2D_ARRAY`를 최대 4 layer로 캐시한다. 각 layer는 `0=lit`,
`1=occluded` hard mask다. RenderCommand/BatchKey에는 Lit 여부, normal texture,
receiver layer, normal strength가 포함되며 모두 같은 command만 batch한다.

내장 `batch_lit` shader는 최대 8개 light uniform과 shadow-mask array를 사용한다.
world position과 UV derivative로 tangent basis를 복원하여 rotation, flip,
음수·비균일 scale을 처리하며 degenerate UV는 flat normal로 fallback한다.
`normalStrength`는 tangent XY에 적용한 뒤 정규화한다.

Lit가 설정된 custom material은 MVP에서 지원하지 않는다. 한 번 경고하고 그
renderer만 Unlit로 출력한다. PostFX가 있으면 Lit world를 기존 HDR scene target에
그린 뒤 실행한다. PostFX fallback 재렌더는 shadow mask를 다시 만들지 않고 동일
`LightingFrame2D`를 사용한다. UI는 모든 Camera lighting/mask/PostFX 밖에서 논리
출력 전체에 한 번 렌더한다.

## 5. 실패 격리와 관측성

- missing/invalid/wrong-size normal texture는 해당 Sprite만 flat normal로 그리고
  deduplicated warning을 남긴다.
- invalid occluder는 해당 항목만 제외한다.
- shadow texture/FBO/shader/layer 실패는 해당 shadow light를 unshadowed로 계속
  그리며 `shadowFallback`으로 기록한다.
- Lit shader나 필수 lighting context 실패는 해당 Camera world만 완전히 Unlit로
  다시 렌더하고 `lightingFallback`으로 기록한다. 그 Camera의 PostFX, 다른 Camera,
  UI와 presentation은 계속 처리한다.
- Camera 결과에는 `lightingApplied`, `lightingFallback`, `shadowFallback`,
  `selectedLightCount`, `shadowedLightCount`, `shadowCasterDrawCount`,
  `lightingPasses`, `shadowPasses`를 기록한다. Game 결과, RenderStats, Profiler와
  SmokeReport는 이를 합산하며 기존 필드를 유지한다. `allocationFailed`는 계속
  논리 출력 target 실패만 뜻한다.

Shadow pass 뒤에는 framebuffer, viewport/scissor, sRGB, program/VAO/buffer,
active texture와 texture unit 0–2, blend/depth/stencil/cull, write mask와 clear
상태를 복구한다.

## 6. 에디터 통합

- Inspector는 Camera Lighting, PointLight2D, Sprite/Tilemap Lighting,
  ShadowOccluder2D를 편집한다. Color, LayerMask와 NormalMap AssetGuid는 기존
  multi-edit, batch snapshot Undo와 prefab override 경로를 사용한다.
- Polygon 구조 편집은 단일 선택에서만 허용하며 복구, triangle, rectangle preset을
  제공한다.
- Scene View는 editor-user preference인 `Lit` 토글을 제공하고 기본 ON으로 한다.
  대표 Primary의 lighting/ambient/culling 설정을 editor camera view로 preview한다.
- Point radius와 polygon vertex handle은 한 drag를 한 snapshot Undo로 기록한다.
- Game View와 packaged runtime은 계속 공통 `GameOutputRenderer` 결과를 표시한다.

## 7. 검증 범위

- Unit: Camera/Light/Sprite/Tilemap/Occluder 기본값·직렬화·finite 검증과 prefab
  canonicalization, TextureUsage/dependency 검증, priority/scene-order 선택과
  8/4/64 제한, mask/radius culling, convexity/winding/transform와 light 내부·경계
  shadow geometry, Inspector mixed edit/Undo/prefab override/Scene View transaction.
- GL: 기존 Unlit 픽셀 동일성, ambient와 light 수학, flat/authored normal의
  rotation·flip 응답, Box/convex hard shadow와 복수 light/mask/제한, split/PIP별
  독립 조명, `Lighting → PostFX → UI`, Bloom HDR, Camera-local fallback,
  1×1/홀수 viewport, Native/IntegerFit nearest와 전체 GL 상태 복구/cache 정리.
- Smoke/E2E: stage2 Primary에 Lit Sprite, NormalMap, PointLight와 convex occluder를
  추가하고 Secondary는 legacy Unlit로 둔다. 최종 report는 lit Camera 1,
  lighting fallback 0, shadow fallback 0, selected light 1, shadowed light 1,
  caster draw 1과 양수 pass count를 요구하며 기존 scene transition, 물리, 저장,
  전역 입력, PostFX, 멀티 카메라와 package 경로도 함께 회귀한다.

## 8. 비목표

Global/Spot/Area/Freeform Light, soft/1D shadow map, Circle·concave·hole·alpha
silhouette occluder, Tilemap 자동 contour/normal map, Particle/Text/Marrow/custom
shader lighting, GI/baking/emission/volumetric light와 프로젝트별 가변 예산은 이번
MVP에 포함하지 않는다.

## 9. 완료 검증

2026-07-25 최종 게이트를 통과했다.

- Debug/Release/ASan/UBSan 네 preset에서 전체 build와 CTest **78/78**이 각각
  통과했다.
- focused lighting unit/GL/editor 테스트가 light 선택·예산·mask, normal 방향과
  rotation/flip, Box/convex hard shadow, camera-local fallback, Inspector
  multi-edit/Undo/prefab override와 Scene View handle transaction을 검증했다.
- 네 preset의 `editor_smoke`, `runtime_smoke`, `build_smoke`와 패키지
  `smoke_end_to_end`가 모두 통과했다.
- 패키지 smoke report에서 lit Camera 1, lighting fallback 0, shadow fallback 0,
  selected light 1, shadowed light 1, caster draw 1과 양수 lighting/shadow pass를
  확인했다. 기존 scene transition, 물리, 저장, 전역 입력, PostFX와 멀티 카메라
  경로도 함께 통과했다.
- `git diff --check`가 통과했다.
