# Molga Engine - 프로젝트 현황

> 갱신일: **2026-10-18**
> 확인 기준: branch `claude/brave-carson-cv9y49`, 코드 HEAD `b9c0444`(Task 12.2 Step E) 위에 이 문서 커밋(이 커밋)을 얹은 상태. 선행 기준은 `c72ac61`(Step 5e amendment 구현)과 `dcd0deb`(GCC 13 수정, PR #27 병합 `c7096dd` 위).
> 검증 환경: **Linux GCC 13 Debug**. macOS는 이번에 다시 실행하지 않았다.
> 이 문서는 진행 상태와 재개 지점의 요약이다. 승인된 설계와 실행 하위 계획을 대체하지 않는다.

## 현재 위치와 즉시 재개 지점

현재 개발의 중심은 SDL_GPU/Metal 렌더러 전환 이후의 **UI/Text Production Backbone**이다.
다국어 텍스트와 UI 레이아웃·렌더링 기반, `Task 12.1`의 입력 계획 및 포인터 소유권
보완, `Task 12.2`의 identity 기반 포커스·캡처·탐색·단일 이벤트 스크롤까지 코드에
반영돼 있다. 실제 프레임 입력 경로(`UISystem::ProcessFrame`)의 전환은 아직 남아 있다.

**다음 순서: Task 12.3 → IME·텍스트 편집 → 에디터 저작·마이그레이션
→ 패키징·최종 qualification.**

Task 12.2는 2026-10-18에 구현·검증을 마쳤다(Linux GCC 13 Debug, 커밋 `7f57c71`…`b9c0444`).
단위 suite는 101개 중 98개 통과, 실패 3개는 기존 결함(아래 검증 현황)이다. **Release·ASan·UBSan과
macOS 재실행은 하지 않았다.** 구현 상태와 검증 상태는 아래에서 별도로 기록한다.
Task 11.3 Step 5e 스크롤 수렴성 amendment는 `c72ac61`에 반영돼 있다.

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

| 실행 단계 | 범위 | 상태 (2026-10-04 기준, 03행과 12.2행은 2026-10-18 갱신) |
|---|---|---|
| 01 · Milestone 1–3 | 고정 의존성, ICU 수명주기, Unicode 분석, 26.6 고정소수점 | 선행 구현과 테스트 존재 |
| 02 · Milestone 4–8 | 폰트·패밀리, shaping, atlas, 문단 배치·BiDi hit testing, 공통 텍스트 소비자 전환 | 반영됨. 하위 계획 종료 기록 존재 |
| 03 · Milestone 9–11 | UI identity·스키마, 레이아웃 스냅샷, 클리핑, 스크롤 상태 | 반영됨. Step 5e 수렴성 amendment는 2026-10-18에 `c72ac61`로 반영 |
| 04 · Task 12.1 | 순서 있는 UI 이벤트, 스냅샷 N 기준 타깃 계획·결과 합산 | `f9ac229`로 반영됨 |
| 12.1 포인터 소유권 amendment | 장식 UI의 조상 라우팅, 클릭 차단, self-or-ancestor 스크롤 연결 | `5c2fc17`로 반영됨 |
| 04 · Task 12.2 | identity 기반 포커스·캡처·탐색·단일 이벤트 스크롤 | 반영됨(2026-10-18). `7f57c71` `1717f46` `7aaf24d` `17263ee` `58f0b8c` `f1a5667` `b9c0444`. 실제 프레임 경로에는 아직 연결되지 않음(12.3) |
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

Task 12.2(2026-10-18)는 버튼의 hover/pressed/clicked 상태를 `{windowId,worldGeneration}`·전체
identity로 키를 잡는 프로세스 전역 표(`UIInteractionStateTable`)로 옮기고, 라우터를 클릭 판정의
유일한 권위로 만들었다(R7 종결). `UIFocusSystem`은 포커스·포인터 캡처·정리(교체/삭제/비활성/
상호작용 불가, 포인터 이탈, native focus loss, 월드 해제)와 스냅샷 N만 읽는 명시적/자동 방향
탐색을 담당하고, `UIScrollSystem::HandleEvent`는 한 계획의 고정된 self-or-ancestor 사슬을
안쪽부터 걸어 처음 소비한 대상과 ordinal을 보고한다. 보이는 상태가 실제로 바뀔 때만
`Advance(SemanticDirty)`를 한 번 획득하고, 소진되면 이전 상태를 유지한 채 Blocker를 한 번 보고한다.
상세·편차·mutation 기록은
[하위 계획 04](superpowers/plans/2026-08-20-ui-text/04-ui-interaction-ime.md)의
`Implementation record — Task 12.2 (2026-10-18)`에 있다.

Step 5e amendment(2026-10-18)는 속도 감쇠를 0 방향 절단(`MulQ6TowardZero`)으로 바꾸고,
양수 감속률·복귀율의 최소 1 raw 바닥과 overscroll 보정의 `[1, |overscroll|]` 제한을
추가해 탄성/감속 재귀가 유한 tick 안에 정지하게 했다. settle clause와 이미 못 박힌
값은 바뀌지 않았다. 상세와 mutation 기록은
[하위 계획 03](superpowers/plans/2026-08-20-ui-text/03-ui-layout-rendering.md)의
`Implementation record — Step 5e amendment (2026-10-18)`에 있다.

### 실제 입력 경로는 아직 legacy

`UIInputRouter::PlanNext`·`HandleEvent`, `UIFocusSystem`, `UIScrollSystem::HandleEvent`와 새
상호작용 상태 표(`UIInteractionStateTable`)는 존재하지만 Game View와 standalone runtime은 여전히
`UISystem::ProcessInput`을 호출한다. `UISystem`에는 기존 `capturedOwner_`와 `capturedObjectId_`가
남아 있고 `UISystem::ProcessFrame`은 없다. 레거시 경로는 `UIButton::ApplyPointerState` 어댑터를 통해
표의 예약 표면 `{windowId = 0, worldGeneration}`에 쓸 뿐이며, 새 라우터·포커스 시스템을 프레임마다
부르는 코드는 아직 없다(테스트만 부른다). 따라서 새 시스템의 구현을 실제 실행 경로 전환 완료로
해석하지 않는다.

근거: [`UISystem.h`](../src/UI/UISystem.h),
[`GameViewWindow.cpp`](../src/Editor/Windows/GameViewWindow.cpp),
[`runtime_main.cpp`](../src/runtime_main.cpp).

## 다음 작업과 이월 사항

### 1. Task 11.3 Step 5e — 승인된 스크롤 수렴성 amendment (완료, 2026-10-18, `c72ac61`)

권위: [하위 계획 03](superpowers/plans/2026-08-20-ui-text/03-ui-layout-rendering.md)의
`Resolution of the Step 5e decision (2026-10-01)`와 같은 문서의
`Implementation record — Step 5e amendment (2026-10-18)`.
`dac2b7e`는 결정 문서 커밋이고, 구현은 `c72ac61`이다.
대상은 `src/UI/UIScrollSystem.{h,cpp}`와 `tests/test_ui_scroll.cpp`다.

| 승인된 변경 | 구현 |
|---|---|
| 속도 감쇠에 한해 `MulQ6TowardZero` 사용 | 반영. 0이 아닌 고정점 제거 |
| 양수 deceleration rate의 decayStep을 최소 1 raw로 제한 | 반영. 감속률이 양수일 때만 바닥 1 |
| 양수 elasticity rate의 returnStep을 최소 1 raw로 제한 | 반영 |
| overscroll 보정량을 같은 부호로 최소 1 raw, 최대 overscroll 크기로 제한 | 반영 |

- [x] 승인된 네 가지 수식 변경을 구현하고 기존 hand fixture와 settle clause를 보존했다.
- [x] rate·초기 상태 격자(780 상태)가 1024 tick 안에 속도 0 및 합법 offset에 도달하는 수렴성 테스트를 추가·통과시켰다. 감속률 0은 승인 본문이 `retained = 64`를 유지하므로 격자에서 제외했다.
- [x] 변경을 각각 제거하는 mutation(M1a, M1b, M2, M2b, M3, M4, M4s, M5)이 모두 테스트를 실패시킨다.
- [x] `test_ui_scroll`이 완료까지 실행됐다: 51 cases / 70071 assertions 통과, ctest 54.6초(Linux GCC 13 Debug).

이전 점검에서 `test_ui_scroll`이 끝나지 않았던 것은 무한 루프나 ctest 시간제한이 아니라
에이전트 셸 명령의 55~65초 제한 때문이었다. 이 suite는 fixture마다 실제 텍스트 런타임을
세우므로 원래 50초대가 걸린다. `tests/CMakeLists.txt`의 `TIMEOUT 600`이 적용되므로
`ctest --preset debug`의 `--timeout 60`은 이 테스트에 걸리지 않는다.

### 2. Task 12.2 — 포커스·캡처·탐색·단일 이벤트 스크롤 (완료, 2026-10-18)

권위: [하위 계획 04](superpowers/plans/2026-08-20-ui-text/04-ui-interaction-ime.md)의
`Task 12.2`와 그 구현 기록. 계획의 단일 커밋 대신 7개 코드 커밋으로 반영됐다.

| 커밋 | 내용 |
|---|---|
| `7f57c71` | Step A: `UIInteractionStateTable`, `UIButton` getter가 표를 읽음, 레거시 `ApplyPointerState` 어댑터, `UISystem::OnWorldReleased` 회수 연결 |
| `1717f46` | Step B: 라우터가 hover·press·capture·click의 유일한 권위, `capturedPointerFromSnapshotN` |
| `7aaf24d` | 수정: 회수 경로 테스트, 레거시 hover 세대 이중 증가 제거, 소진 Blocker 1회, stale release 정리 |
| `17263ee` | Step C: `UIFocusSystem` 포커스·캡처·정리, `tests/test_ui_focus.cpp` |
| `58f0b8c` | Step D: 명시적/자동 방향 탐색, `UINavigationCandidateLess` |
| `f1a5667` | 수정: `Revalidate`의 다른 월드 스냅샷 방어, `ui.focus` 진단 사실당 1회 |
| `b9c0444` | Step E: `UIScrollSystem::HandleEvent`, `ScrollAxisOf` |

- [x] 컴포넌트 교체·삭제·비활성·상호작용 불가·월드 해제·pointer departure·native focus loss가 정확한 전체 identity로 구분된다.
- [x] hover/pressed/clicked 상태가 router-owned 표로 이동했고 R7(두 클릭 권위, capture 상실 뒤 잔여 pressed)이 닫혔다.
- [x] 보이는 상태 변경은 세대를 정확히 한 번 올리고 no-op은 올리지 않으며, 소진 시 fail closed(Blocker 1회).
- [x] 스크롤 핸들러는 한 계획의 signed wheel/drag/axis 값을 보존하고 고정된 사슬을 안쪽부터 따라가 소비한 대상과 ordinal을 보고한다.
- [ ] 실제 프레임 입력 경로 연결과 레거시 제거는 Task 12.3의 몫이다.

사용자 결정(2026-10-18, 하위 계획 04 구현 기록에도 같은 날짜로 기록):

- **Q1** `UIInputEvent::control` 도메인은 엔진 enum이다: `Key` = `Input::KeyCode`,
  `GamepadButton` = `Input::GamepadButton`, `GamepadAxis` = `Input::GamepadAxis`.
  `PointerButton`은 Task 13.2에서 `Input::MouseButton`을 쓴다. 매핑은
  `UINavigationDirectionOf()`와 `ScrollAxisOf()` 두 함수에 격리했다.
- **Q2** 게임패드 스크롤은 **오른쪽 스틱만**(`RightX` 가로, `RightY` 세로). `LeftX`/`LeftY`/두 트리거는 무동작이며 테스트가 확인한다.
- **Q3** 드래그 스크롤은 상호작용 가능한 요소의 **캡처된 press**에서만 시작한다. 비상호작용 내용의 드래그 캡처는 15.3으로 미룬다. 캡처된 identity로 라우팅되는 release는 드래그 뒤에도 클릭한다(12.2에는 클릭 취소가 없다).
- **Q4** (a) 게임패드 스틱은 포커스를 옮기지 않는다 — 화살표 키와 D-pad만. (b) 포커스 요소 활성화(Enter/Space/게임패드 South → Click)는 12.2 밖이고 Task 13.2가 R10과 함께 맡는다. (c) 세대 소진은 fail closed(Press/Click/Focus 미전달, 이전 상태 유지, Blocker 1회). (d) 자동 탐색은 모드 `None` selectable을 목적지에서 제외하고, 모드 `None` 출발점은 목적지가 없다(포커스 유지, 진단 없음).
- 사용자 질문이 아닌 설계 §8.2 귀결: 사슬의 낡은 항목은 `ReferenceInvalid` 한 번과 함께 건너뛰고 바깥쪽으로 계속한다.

### 3. Task 12.3 — 실제 프레임 처리 통합 (다음 작업)

`UISystem::ProcessFrame`이 ordered-event vector의 유일한 처리 루프가 되도록 하고,
Game View와 standalone runtime을 새 경로에 연결한다. 콜백이 RenderSnapshot N+1을
게시해도 같은 입력 묶음의 타깃 판단과 release arbitration은 끝까지 N을 사용한다.

12.2에서 이월되는 의무:

- `UIPlanningState`를 `UIInteractionStateTable::Surface`에서 세운다. 라우터가 쓰는 hover와
  `PlanNext`가 투사하는 hover가 다른 경우(잡힌 이동/놓기가 대상 밖일 때 라우터는 "포인터 아래
  대상 또는 없음", `PlanNext`는 잡힌 대상 유지; 잡히지 않은 놓기는 `PlanNext`만 hover 갱신)를 맞춘다.
- 프레임 시작과 hierarchy/component를 바꿀 수 있는 콜백 뒤마다 `UIFocusSystem::Revalidate`를 부른다.
  `ClearFrameTransients`(clickedThisFrame 프레임 초기화)는 12.2에 없다: 라우터 경로의 click 비트는
  그 버튼의 항목이 다시 쓰일 때까지 남고 `WasClickedThisFrame()`은 표면 전체를 OR 한다.
- `UILayoutSystem.cpp`의 `button->CurrentColor()`(any-surface OR)를
  `ButtonState({surfaceWindowId, world.Generation()}, …)`로 바꾸고, 레거시 표면 키 `{0, gen}`,
  `UIButton::ApplyPointerState`, `UISystem::{capturedOwner_, capturedObjectId_, ResetPointerCapture}`를 지운다.
  레거시 표면과 죽은 identity는 `Revalidate`(라우터 표면만 순회)/월드 해제 전까지 표에 남는다.
- `Revalidate`는 N이 더는 게시하지 않는 기록(예: 비활성 `UICanvas` 서브트리)의 포커스를 유지한다 —
  key 계획이 대상 없이 끝날 뿐 무해하다. 자동 탐색은 clip/mask로 가려진 기록(빈 `logicalClip`)을 고를
  수 있다 — 권위 문서가 침묵한다.

`R6`의 N/N+1 회귀 테스트, `R11`의 planning-state 창·월드 검증을 포함한다.
이미 구현된 `UIEventDispatchAccumulator`의 명시적 diagnostic sink와 non-fatal
merge rejection 계약을 사용하고 재구현하지 않는다. 하위 계획의 오래된 생성자 예시와
stored-plan vector를 금지하는 모순된 검색 검증도 실제 계약에 맞춰 확인한다.

### 4. 후속 단계의 주의점

`Task 13.2`에는 우클릭·중클릭도 현재 router에서 클릭/캡처로 취급될 수 있는 `R10`이
남아 있다. primary-only는 기존 기록의 권고안이지 이 현황 갱신으로 새로 승인한 정책이 아니다.
13.2는 또 `PointerButton.control`을 `Input::MouseButton`으로 채우고, 포커스 요소의 활성화
(Enter/Space/게임패드 South → Click, 사용자 결정 Q4b)를 R10과 함께 맡는다 — 하위 계획 04의
Task 13.2 "Inherited obligations"에 기록했다. `UIInputEvent::control`의 도메인(Key/GamepadButton/
GamepadAxis = 엔진 enum)은 12.2에서 확정돼 13.2/14.1이 그대로 따른다.

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
tests/                         # Debug에 등록된 CTest 110개 (2026-10-18, `test_ui_focus` 추가)
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

### 2026-10-18 Task 12.2 검증 — Linux GCC 13 Debug (macOS 재실행 없음)

환경은 Linux, GCC 13, Debug(`build/debug`, Ninja)다. 이 컨테이너에는 GPU 호스트가 없다
(`Failed to initialize SDL3: No available video device`). 아래는 이번 작업에서 실제로 실행한 결과다.

| 검증 | 관찰 결과 |
|---|---|
| `cmake --build --preset debug -j4` | 116/116, 종료 코드 0 |
| Step 8 게이트 `ctest --test-dir build/debug -R '^(test_ui_focus\|test_ui_input\|test_ui_scroll\|test_scene_serializer)$'` | 4/4 통과: `test_ui_focus` 5.2초(17 cases), `test_ui_input` 15.7초(73 cases, 이전 52), `test_ui_scroll` 54.6초(52 cases, 이전 51), `test_scene_serializer` 0.01초 |
| `test_ui` · `test_ui_layout` · `test_ui_components` · `test_scene_runtime` | 모두 통과(7.3초 · 7.8초 · 0.02초 · 0.02초). `test_ui`는 30 cases(이전 29) |
| `ctest --test-dir build/debug -L unit -LE "smoke\|pixel\|e2e"` | 101개 중 **98개 통과, 3개 실패**, 325초 |
| mutation 스폿 체크 | `f1a5667` 3종, Step E 5종 모두 지정한 테스트를 실패시킴. 하위 계획 04의 구현 기록 참조 |
| `rg` 게이트 | `orderedEvents`는 `UIFrameInput` 필드와 주석뿐, `Drain(Text\|NativeText)\|PendingTextEvents\|vector<.*PlannedUIEvent>`는 `src/UI src/Platform src/Core`에서 0건, `ApplyPointerState`는 `UIButton.*`·`UISystem.cpp`(레거시)·주석뿐 |

단위 suite 실패 3개는 이번 변경과 무관한 기존 실패다.

- `test_ui_render_clip`, `test_gpu_retirement` — GPU 호스트가 필요하다.
- `test_text` — **단독 실행(ctest)에서도 실패한다**: `test_text.cpp:1312`
  (`an authored family wins over a legacy single-face pick`). `-tc`로 그 케이스 하나만 고르면 통과한다.
  `c72ac61`에서 이미 존재한 결함이며 원인은 미확인이다. 별도로 추적한다.

**실행하지 않은 것:** Release, ASan, UBSan, macOS.

### 2026-10-18 Step 5e amendment 검증 — Linux GCC 13 Debug (macOS 재실행 없음)

환경은 Linux, GCC 13, Debug(`build/debug`, Ninja)다. 이 컨테이너에는 display/Vulkan이 없다.
아래 2026-10-04 소절들은 당시 기록이며 이번 결과로 갱신하지 않았다.

| 검증 | 관찰 결과 |
|---|---|
| `ctest --test-dir build/debug -R '^test_ui_scroll$'` | 1/1 통과, 54.6초. doctest 51 cases / 70071 assertions, 실패 0 (mutation 복원 뒤 재실행 포함) |
| `ctest --test-dir build/debug -R '^(test_ui_layout\|test_ui_input)$'` | 2/2 통과, 합계 18.4초 (`test_ui_input` 10.5초) |
| Step 5e mutation 8종 | 모두 실패로 관찰됨. 표는 하위 계획 03의 구현 기록 |
| 느린 케이스 상위 (`-d`) | 2.90초, 2.81초, 2.07초, 2.06초. 새 수렴성 케이스 1.25초 |

오케스트레이터가 같은 Linux 환경에서 잰 기준선: 단위 suite
(`ctest -L unit -LE "smoke|pixel|e2e"`) **97/100 통과**. 실패 3개는 다음과 같다.

- `test_ui_render_clip`, `test_gpu_retirement` — GPU 호스트가 필요하다(이 컨테이너에는 display/Vulkan이 없다).
- `test_text` — `test_text.cpp:1312` assertion으로 실패한다. 이 기록 당시에는 "단독 실행은 통과"로
  적었으나 틀렸다: `test_text` 바이너리 전체는 단독으로 돌려도 실패하고, `-tc`로 그 케이스 하나만
  고를 때만 통과한다(Task 12.2 검증에서 재확인). `c72ac61`에 이미 존재한 결함이며 원인은 미확인,
  별도로 추적한다.

이번 변경과 별개로 GCC 13의 `-Wchanges-meaning` 때문에 `test_ui_input`/`test_ui_scroll`
fixture 접근자의 `World`를 한정한 수정 커밋 `dcd0deb`가 선행한다.
전체 Debug·Release·ASan·UBSan 통과와 macOS 결과는 이번에 확인하지 않았다.

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
| (이 커밋) | 2026-10-18 | Task 12.2 구현 기록(하위 계획 04)과 이 현황 문서 갱신 |
| `b9c0444` | 2026-10-18 | Task 12.2 Step E: `UIScrollSystem::HandleEvent` 한 이벤트 스크롤 |
| `f1a5667` | 2026-10-18 | 수정: `Revalidate`의 다른 월드 스냅샷 방어, `ui.focus` 진단 사실당 1회 |
| `58f0b8c` | 2026-10-18 | Task 12.2 Step D: 명시적/자동 방향 탐색 |
| `17263ee` | 2026-10-18 | Task 12.2 Step C: `UIFocusSystem` 포커스·캡처 상태 |
| `7aaf24d` | 2026-10-18 | 수정: 회수 경로·레거시 hover 세대·소진 Blocker 1회 |
| `1717f46` | 2026-10-18 | Task 12.2 Step B: 라우터가 hover/press/click의 유일한 권위 |
| `7f57c71` | 2026-10-18 | Task 12.2 Step A: 버튼 포인터 상태를 표면 키 표로 이동 |
| `c72ac61` | 2026-10-18 | Task 11.3 Step 5e 수렴성 amendment 구현·테스트·구현 기록 |
| `dcd0deb` | 2026-10-18 | GCC 13 `-Wchanges-meaning` 대응: UI test fixture 접근자의 `World` 한정 |
| `c7096dd` | 2026-10-18 | PR #27 병합 |
| `dab2f78` | 2026-10-02 | PR #26 병합, 2026-10-04 현황 확인 HEAD |
| `5c2fc17` | 2026-10-02 | 비상호작용 UI의 nearest-interactable-owner 라우팅 구현 |
| `dac2b7e` | 2026-10-01 | Step 5e 수렴성 수정 결정 문서화 — 구현은 2026-10-18 `c72ac61` |
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
