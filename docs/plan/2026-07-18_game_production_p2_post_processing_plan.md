# 게임 제작 P2 후속 구현 계획 — Post-processing MVP

> 작성일: 2026-07-18
>
> 상태: **완료 (2026-07-18)**
>
> 기준 문서: `docs/plan/2026-07-12_game_production_gap_analysis.md`
>
> 완료 규칙: 아래 데이터·런타임·에디터 통합과 Debug/Release/ASan/UBSan
> 전체 build/CTest, editor/runtime smoke, 패키지 E2E, `git diff --check`가
> 모두 통과한 뒤에만 갭 분석의 `포스트 프로세싱` 항목을 완료 처리한다.

## 1. 목표와 호환성 계약

Camera가 GUID로 참조하는 opt-in `.postfx` 프로필을 추가한다. 유효하고 활성인
프로필이 있을 때만 world를 논리 해상도 `RGBA16F` target에 렌더하고, 프로필의
배열 순서대로 Bloom, Color Adjust, Vignette를 적용한 뒤 linear clamp와 sRGB
resolve를 수행한다.

UI, Scene View selection outline, gizmo는 후처리 뒤에 렌더한다. IntegerFit은 완성된
논리 이미지를 기존 `GL_NEAREST` 경로로 표시한다. 구 scene/prefab은
`postProcessEnabled=false`, 빈 profile GUID가 기본이므로 화면과 출력 비용이 바뀌지
않는다. 빈 배열, 모두 disabled, 수학적으로 중립인 프로필도 direct path를 사용한다.

## 2. 프로필·Camera·에셋 계약

- `.postfx` schema v1은 순서를 보존하는 `effects` 배열을 사용한다. 지원 타입은
  `Bloom`, `ColorAdjust`, `Vignette`이며 프로필마다 타입별 최대 하나다.
- Bloom 범위는 threshold 0–16, softKnee/scatter 0–1, intensity 0–10이다.
  Color Adjust는 exposure -8–8 EV, contrast -1–1, saturation 0–2, tint 0–4다.
  Vignette는 intensity 0–1, smoothness 0.01–1, color 0–1이다.
- 잘못된 schema/type, 중복 효과, 비유한 값과 범위 밖 값은 import를 실패시킨다.
  알 수 없는 부가 키는 round-trip에서 보존하지만 v1 실행에는 사용하지 않는다.
- `PostProcessProfileImporter`가 schema/effect 수/active effect 수를 catalog metadata에
  기록한다. resolver는 `AssetDatabase` content hash로 last-good을 캐시하고 성공한
  reimport만 교체한다. 편집 중 transient override가 디스크 cache보다 우선한다.
- 삭제·미해결 GUID와 type mismatch는 Camera의 원본 GUID를 보존한 채 런타임에서
  bypass한다. 빌드 dependency validation은 missing, type mismatch, import failure를
  거부한다.
- Camera는 `postProcessEnabled`와 `postProcessProfileGuid`를 평면 직렬화하며 실행
  중 enable/profile 교체 API만 제공한다. Camera별 파라미터 override는 제공하지 않는다.

## 3. 렌더 파이프라인

`GameOutputRenderer`의 한 프레임은 다음 순서를 따른다.

1. 활성 Main Camera 중 depth가 가장 높은 Camera와 유효 프로필을 해석한다.
2. 활성 효과에 필요한 shader, HDR scene/ping-pong, adaptive Bloom target을 world
   렌더 전에 준비한다.
3. Camera background와 world를 논리 해상도 nearest `RGBA16F` scene target에 그린다.
4. 효과 배열 순서대로 fullscreen pass를 실행한다.
5. RGB/alpha를 0–1로 clamp하고 현재 sRGB destination에 resolve한다.
6. ECS UI를 destination 위에 최종 pass로 그린다.
7. IntegerFit은 기존 black bars/crop과 nearest 정수배 blit을 수행한다.

`FramebufferSpecification`은 기본 `SRGBA8 + depth-stencil + Linear` 계약을 유지하면서
`RGBA16F`, optional depth-stencil, Nearest/Linear를 선택할 수 있다. 단순 효과와 resolve의
source는 nearest이고 Bloom mip만 linear다.

Bloom은 최대 5단계 adaptive dual-filter chain이다. 첫 2×2 downsample은 nearest HDR
source의 모든 texel을 덮어 단일 bright impulse도 보존한다. Rec.709 linear luminance에
threshold/soft knee를 적용하고, scatter additive upsample 뒤 intensity composite를
수행한다. Color Adjust는 exposure → contrast → saturation → tint 순서이며 Vignette는
화면 aspect를 반영한 radial RGB mask다. 모든 효과는 alpha를 보존한다. LUT, ACES나
그 밖의 tone mapping은 사용하지 않는다.

fullscreen 실행은 draw/read framebuffer, viewport, scissor, sRGB, program, VAO/VBO,
texture unit, blend/depth/cull, blend function과 write mask를 복구한다. 준비 또는 shader
실패 시 world/UI를 direct path로 계속 표시하고 `postProcessFallback=true`를 반환하며,
출력 크기와 원인 조합별 한 번만 경고한다. IntegerFit 논리 FBO 실패와 이 상태는 별개다.

## 4. 에디터 통합

- Project Browser는 Post Process filter/icon과 Create 메뉴를 제공한다.
- Asset Inspector는 effect 추가/제거, enable, 위/아래 순서 변경과 모든 파라미터를
  편집한다. 로컬 사본은 같은 GUID의 Camera에 transient preview로 즉시 반영한다.
- Save는 검증 후 `AssetContentCommand` 하나로 커밋한다. Revert, asset Undo/Redo,
  다른 asset/object 선택은 transient override를 해제하고 디스크 상태를 다시 읽는다.
- Camera Inspector의 profile은 `PostProcessProfileImporter` 전용 typed AssetGuid다.
  single/multi edit는 기존 batch snapshot Undo와 prefab override를 사용하고, missing
  기존 GUID는 경고와 함께 보존한다.
- Game View와 packaged runtime은 항상 Main Camera 설정을 따른다.
- Scene View toolbar의 `FX`는 기본 OFF이며 EditorPreferences schema v2에 저장한다.
  v1은 OFF로 마이그레이션한다. 활성화하면 Main Camera 프로필을 editor camera와 Scene
  View 해상도로 실행하고 background/grid/world만 처리한다. ECS UI, outline, gizmo는
  후처리 밖에 둔다.

## 5. 공개 결과와 관측성

- `GameOutputResult`는 `postProcessed`, `postProcessFallback`,
  `postProcessPasses`를 제공한다.
- `RenderStats`와 Profiler가 post-process pass 수를 기록한다.
- runtime smoke report는 선택 profile GUID, 실행 여부, fallback, pass 수를 기록한다.
- 내장 fullscreen vertex와 Bloom/Color/Vignette/resolve fragment shader는 기존 shader
  resource 복사와 Reload Shaders 경로를 사용하고 패키지 필수 산출물로 검증한다.

## 6. 검증 범위

- Unit: schema round-trip/순서/default/extension 보존, duplicate/unknown/range/NaN 거부,
  importer 등록과 metadata, hash refresh, transient/last-good/missing, Camera 구 데이터,
  typed AssetGuid multi Undo와 prefab override, dependency validation, preference v1→v2.
- GL: framebuffer format/filter/depth, neutral bypass, Color Adjust known color, Vignette
  center/corner/alpha, 단일 HDR impulse Bloom/threshold/soft-knee/reorder, UI 분리,
  IntegerFit nearest/bars, Native/IntegerFit resize, 1×1, allocation/shader fallback,
  fullscreen GL state 복구.
- Smoke/E2E: `.postfx` fixture를 참조하는 Main Camera, catalog 해석, profile과 내장
  shader 패키징, 실제 runtime pass 실행, 기존 UI scene transition·입력·저장 계약.

## 7. 비목표

UI 후처리, 사용자 fullscreen shader, LUT/ACES, Chromatic Aberration, Film Grain,
Screen Shake, effect 타입 중복, Camera별 값 override, Volume/영역 blending, 2D lighting,
shadow, multi-camera 합성은 이번 범위에 포함하지 않는다. Bloom은 의도적으로 픽셀을
흐리지만 최종 presentation의 IntegerFit nearest 계약은 유지한다.

## 8. 완료 검증

- Debug, Release, ASan, UBSan preset의 전체 build가 성공했다.
- 각 preset의 전체 CTest가 **76/76** 통과했다. 이 게이트에는 post-process unit/GL
  회귀, editor/runtime smoke와 패키지 `smoke_end_to_end`가 포함된다.
- ASan/UBSan 실행에서 sanitizer 오류가 보고되지 않았다.
- `.postfx` fixture와 Main Camera 참조, 내장 shader 패키징, 실제 runtime frame의
  profile 해석·pass 실행을 E2E report로 확인했다.
- `git diff --check`가 통과했다.

따라서 데이터·런타임·에디터·패키지 계약과 완료 게이트를 모두 충족했으며, 기준 갭
분석의 `포스트 프로세싱` 항목을 MVP 완료로 갱신한다.
