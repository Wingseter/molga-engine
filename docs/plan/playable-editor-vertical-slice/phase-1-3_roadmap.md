# Phase 1-3 구현 로드맵: Unity형 2D 게임 엔진으로 확장

> **For agentic workers:** 각 Milestone 구현 전에 `superpowers:brainstorming`과 `superpowers:writing-plans`를 사용해 이 문서에 지정된 상세 계획서를 먼저 작성한다. 구현 시에는 `superpowers:test-driven-development`, 완료 선언 전에는 `superpowers:verification-before-completion`을 사용한다.

**Goal:** Phase 0의 Playable Editor Vertical Slice를 회귀시키지 않으면서, Molga Engine을 실제 2D 게임 제작에 사용할 수 있는 에디터·런타임·배포 도구로 단계적으로 확장한다.

**Architecture Direction:** 에디터는 Core/Runtime 위의 도구 계층으로 유지한다. Asset은 GUID 기반 데이터베이스를 통해 참조하고, Scene/Prefab은 안정된 schema와 migration 경로를 갖는다. 렌더링·물리·오디오는 Runtime service로 분리하며, 에디터 UI는 service를 직접 소유하지 않고 문서·명령·선택 상태를 통해 조작한다.

**로드맵 범위:** Phase 1 Editor Authoring, Phase 2 Gameplay Production, Phase 3 Distribution & Reliability

---

## 1. Phase 0 완료 게이트

Phase 1 작업은 아래 항목이 모두 만족된 후 시작한다.

- [ ] Renderer 계약과 RenderPass 상태 복구 테스트가 있다.
- [ ] Hierarchy가 실제 Scene Object를 편집하며 CommandHistory를 사용한다.
- [ ] SceneDocument가 Edit World와 Play World를 분리한다.
- [ ] PathService와 GameBuilder가 실행 위치와 무관한 패키지를 만든다.
- [ ] Debug/Release/Sanitizer 테스트가 CI에서 실행된다.
- [ ] `smoke_end_to_end`가 프로젝트 → 저장 → 빌드 → 독립 런타임을 검증한다.
- [ ] Phase 0 문서의 완료 기준과 실제 코드가 다르면 문서를 먼저 현실에 맞게 갱신한다.

Phase 0 완료 이전에 AssetDatabase, Physics, Prefab을 병렬로 도입하면 기존 Scene schema와 수명 주기가 반복해서 깨진다. Phase 0 게이트는 일정상의 권고가 아니라 Phase 1 진입 조건이다.

---

## 2. 공통 설계 원칙

### 2.1 계층 경계

```text
Platform
  └─ Window / FileDialog / DynamicLibrary / Process

Core
  └─ UUID / Event / Log / Serialization / PathService

Runtime
  ├─ World / GameObject / Component / Scene
  ├─ Rendering / Physics / Audio / Input / Animation
  └─ AssetDatabase runtime view

Editor
  ├─ SceneDocument / Selection / CommandHistory
  ├─ Panels / Gizmos / Importers
  └─ BuildPipeline / ProjectSettings
```

금지 규칙:

- Runtime 코드가 ImGui 또는 Editor singleton을 include하지 않는다.
- Component가 에디터 Panel을 직접 호출하지 않는다.
- Asset 참조를 절대 경로로 저장하지 않는다.
- Panel이 World를 직접 재생·교체하지 않고 SceneDocument를 통한다.
- 사용자가 수행한 편집 작업이 CommandHistory를 우회하지 않는다.

### 2.2 데이터 호환성

모든 지속 데이터에는 schema version을 기록한다.

```json
{
  "schemaVersion": 1,
  "engineVersion": "0.1.0"
}
```

Scene, Prefab, Material, AnimationClip, ProjectSettings는 다음 규칙을 따른다.

- 현재 version과 직전 두 version의 로드를 지원한다.
- migration은 입력 파일을 즉시 덮어쓰지 않고 메모리에서 수행한다.
- 저장 시 현재 version으로 기록한다.
- 알 수 없는 Component와 필드는 경고 후 보존 가능한 형태로 유지한다.
- schema 변경 PR에는 이전 fixture를 로드하는 테스트가 반드시 포함된다.

### 2.3 기능 완료 정의

각 Milestone은 아래 네 종류의 완료 증거를 갖는다.

1. Core/Runtime의 doctest 단위 테스트
2. 에디터 서비스 또는 문서 수준 통합 테스트
3. `smoke_end_to_end`에 추가된 최소 사용자 흐름
4. 사람이 확인할 수 있는 짧은 수동 검증 절차

UI가 보인다는 이유만으로 Milestone을 완료 처리하지 않는다.

---

## 3. 의존성 지도

```text
Phase 0 Vertical Slice
  │
  ├─ 1-1 Scene View ── 1-2 Selection/Gizmo/Undo ── 1-3 Authoring UX ─┐
  └─ 1-4 Console/Diagnostics ── 1-5 Script Workflow ────────────────┤
                                                                    ▼
                                                            Phase 1 Gate
                                                                    │
                                                             2-1 AssetDatabase
                                                                    │
      ┌─────────────────────────────────────────────────────────────┘
      ├─ 2-2 Input + Tags/Layers ── 2-3 Physics
      ├─ 2-4 Rendering Pipeline
      ├─ 2-5 Audio
      ├─ 2-6 Animation
      ├─ 2-7 Prefab
      └─ 2-8 Tilemap
            │
            └─ 3-1 Build Profiles/Packaging
               ├─ 3-2 Profiler/Diagnostics
               ├─ 3-3 Multi-scene/Streaming
               └─ 3-4 Release/Documentation
```

병렬 실행이 가능한 묶음:

- `1-1` 완료 후 `1-4`와 `1-2`는 병렬 가능하다.
- `2-1` 완료 후 `2-2`, `2-4`, `2-5`는 기술적으로 병렬 가능하다.
- 기본 제품 순서는 `2-1 AssetDatabase → 2-2 Input/Tags/Layers → 2-3 Physics → 2-4 Rendering Pipeline/batching`으로 유지한다.
- `2-1`과 `1-2` 완료 후 `2-7`을 시작한다.
- `2-1`, `2-4`, `1-2` 완료 후 `2-8`을 시작한다.

---

# Phase 1: Editor Authoring MVP

**Phase Goal:** Scene을 코드나 JSON 직접 편집 없이 구성하고, 변경을 안전하게 되돌리며, 문제를 에디터 안에서 진단할 수 있다.

**Phase Exit Scenario:**

1. Project Browser에서 Texture를 Scene View로 드래그한다.
2. 생성된 Sprite를 Scene View에서 선택하고 Gizmo로 이동한다.
3. Inspector에서 값을 변경하고 Undo/Redo한다.
4. Scene을 저장하고 Play한다.
5. Game View와 Console에서 결과와 로그를 확인한다.
6. Script 오류를 수정하고 재컴파일한 뒤 Play를 다시 실행한다.

---

## Milestone 1-1: Scene View와 Editor Camera

**상세 계획서 경로:** `docs/plan/phase-1/task-1-1_scene_view.md`

**목표:** 에디터 안에서 편집용 카메라로 Scene을 탐색하고 GameObject를 선택할 수 있는 Scene View를 제공한다.

**핵심 구현:**

- `EditorCamera`의 pan, zoom, viewport resize
- Offscreen framebuffer와 color/depth attachment
- Scene View Panel에 framebuffer 출력
- viewport 좌표와 world 좌표 변환
- Object picking용 안정된 ID pass 또는 CPU bounds hit test
- 선택 윤곽선과 Grid 표시
- Scene View focus 상태에서만 camera 입력 처리

**예상 파일:**

```text
src/Editor/EditorCamera.h
src/Editor/EditorCamera.cpp
src/Editor/Panels/SceneViewPanel.h
src/Editor/Panels/SceneViewPanel.cpp
src/Rendering/Framebuffer.h
src/Rendering/Framebuffer.cpp
src/Rendering/SelectionPass.h
src/Rendering/SelectionPass.cpp
tests/test_editor_camera.cpp
tests/test_scene_picking.cpp
```

**설계 결정:**

- Scene View camera는 Scene의 GameObject가 아니다.
- Game View의 runtime camera와 Scene View camera를 공유하지 않는다.
- 렌더 Target resize는 viewport 크기가 실제로 바뀔 때만 수행한다.
- 첫 구현의 picking은 Sprite bounds 기반 CPU hit test를 허용한다. 단, sorting order와 역순 hit test를 지켜야 한다.

**완료 기준:**

- [ ] Scene View resize 시 stretch나 framebuffer leak이 없다.
- [ ] pan/zoom 후 마우스 위치의 world 좌표가 테스트와 일치한다.
- [ ] 겹친 Sprite 클릭 시 가장 앞의 Sprite가 선택된다.
- [ ] Play/Stop 후 Scene View camera 위치가 유지된다.
- [ ] ASan에서 반복 resize와 Panel 열기/닫기가 통과한다.

---

## Milestone 1-2: Selection, Transform Gizmo, Undo/Redo UX

**상세 계획서 경로:** `docs/plan/phase-1/task-1-2_gizmo_and_undo.md`

**선행:** `1-1`, `0-4`

**목표:** Scene View에서 직접 Object를 선택·이동·회전·크기 조절하고 모든 변경을 되돌릴 수 있게 한다.

**핵심 구현:**

- 중앙 `SelectionService`와 선택 변경 이벤트
- Translate/Rotate/Scale Gizmo
- local/world 축 모드
- grid snap과 사용자 설정 snap 간격
- drag 시작 전 값과 drag 종료 후 값을 하나의 Command로 기록
- `Cmd+Z`, `Cmd+Shift+Z` 또는 플랫폼별 표준 shortcut
- Hierarchy, Scene View, Inspector의 선택 상태 동기화

**예상 파일:**

```text
src/Editor/SelectionService.h
src/Editor/SelectionService.cpp
src/Editor/Gizmos/TransformGizmo.h
src/Editor/Gizmos/TransformGizmo.cpp
src/Editor/Commands/TransformCommand.h
src/Editor/Commands/TransformCommand.cpp
src/Editor/EditorShortcuts.h
src/Editor/EditorShortcuts.cpp
tests/test_selection_service.cpp
tests/test_transform_command.cpp
```

**완료 기준:**

- [ ] drag 한 번이 Undo stack 항목 하나가 된다.
- [ ] drag 중 매 프레임 Command를 생성하지 않는다.
- [ ] 부모가 있는 Object의 local/world 변환이 정확하다.
- [ ] Inspector 숫자 변경과 Gizmo 변경이 같은 Undo 규칙을 따른다.
- [ ] 삭제된 Object를 Undo했을 때 Selection이 유효한 Object를 가리킨다.

---

## Milestone 1-3: Authoring UX 완성

**상세 계획서 경로:** `docs/plan/phase-1/task-1-3_authoring_ux.md`

**선행:** `1-1`, `1-2`

**목표:** 저장, 파일 작업, Inspector, Project Browser를 실제 제작에 사용할 수 있는 수준으로 마무리한다.

**핵심 구현:**

- New/Open/Save/Save As Scene
- dirty Scene 닫기·프로젝트 전환·종료 확인
- Project Browser 생성/이름 변경/삭제/이동
- Inspector Component 추가/삭제/Reset/복사/붙여넣기
- 다중 선택의 공통 속성 편집
- 표준 keyboard shortcut과 command palette 또는 menu 연결
- 드래그 앤 드롭으로 Texture에서 Sprite 생성
- Editor layout 저장과 초기화

**예상 파일:**

```text
src/Platform/FileDialog.h
src/Platform/FileDialog.cpp
src/Editor/DirtySceneGuard.h
src/Editor/DirtySceneGuard.cpp
src/Editor/Panels/ProjectBrowserPanel.cpp
src/Editor/Panels/InspectorPanel.cpp
src/Editor/EditorLayout.cpp
tests/test_dirty_scene_guard.cpp
tests/test_project_browser_operations.cpp
tests/test_component_commands.cpp
```

**완료 기준:**

- [ ] 저장하지 않은 Scene을 잃는 종료 경로가 없다.
- [ ] Project Browser의 모든 변경이 실제 파일 시스템과 일치한다.
- [ ] Component 추가·삭제·Reset이 Undo/Redo 된다.
- [ ] Texture drag로 생성한 Sprite가 저장·재로드·빌드 런타임에서 보인다.
- [ ] 기본 layout과 사용자 layout이 손상 시 복구된다.

---

## Milestone 1-4: Console과 진단 기반

**상세 계획서 경로:** `docs/plan/phase-1/task-1-4_console_and_logging.md`

**목표:** 엔진, 에디터, 사용자 Script의 로그와 오류를 에디터 Console에서 탐색할 수 있게 한다.

**핵심 구현:**

- thread-safe structured logger
- severity, category, timestamp, source location
- Console Panel 필터, 검색, collapse, clear
- 로그 항목 double click 시 source location 열기
- assertion과 fatal error의 일관된 출력
- 최근 로그 제한과 메모리 상한
- 파일 로그와 CI 로그

**예상 파일:**

```text
src/Core/Log.h
src/Core/Log.cpp
src/Editor/Panels/ConsolePanel.h
src/Editor/Panels/ConsolePanel.cpp
src/Platform/OpenExternal.h
src/Platform/OpenExternal.cpp
tests/test_log_sink.cpp
tests/test_console_model.cpp
```

**완료 기준:**

- [ ] worker thread 로그가 유실되거나 UI container를 직접 변경하지 않는다.
- [ ] 10만 개 로그 입력에도 설정한 메모리 상한을 넘지 않는다.
- [ ] 빌드·Script compile·runtime load 실패가 Console에 원인과 함께 나타난다.
- [ ] headless smoke 실패 로그가 CI stdout과 리포트에 남는다.

---

## Milestone 1-5: Script 개발 흐름

**상세 계획서 경로:** `docs/plan/phase-1/task-1-5_script_workflow.md`

**선행:** `1-3`, `1-4`

**목표:** Script 생성, 컴파일, 오류 수정, 재로드를 에디터를 종료하지 않고 수행한다.

**핵심 구현:**

- Script template과 생성 UI
- compiler process 비동기 실행
- stdout/stderr 구조화와 Console 연결
- 성공한 빌드만 동적 라이브러리 교체
- Component field 상태 보존 후 Script reload
- reload 실패 시 마지막 정상 assembly 유지
- Play 중 compile 정책 명시

**위험 통제:**

- compiler process를 UI thread에서 기다리지 않는다.
- 동적 라이브러리 unload 전에 Script instance를 모두 파괴한다.
- 함수 포인터와 RTTI 객체를 unload 이후 보관하지 않는다.
- Windows 파일 lock과 macOS/Linux dylib/so 교체 차이를 Platform 계층에서 처리한다.

**완료 기준:**

- [ ] compile 중 에디터 UI가 멈추지 않는다.
- [ ] compile error가 파일과 줄 번호를 포함해 Console에 표시된다.
- [ ] reload 후 직렬화된 Script field 값이 보존된다.
- [ ] 실패한 compile이 현재 실행 가능한 Script를 파괴하지 않는다.
- [ ] Script reload 반복 테스트가 ASan에서 통과한다.

---

## Phase 1 통합 게이트

`smoke_end_to_end`에 다음 흐름을 추가한다.

```text
fixture Texture import
→ Sprite Scene 저장
→ Scene reload
→ Play 3 frames
→ Stop
→ build
→ packaged runtime 3 frames
```

수동 검증 시에는 Scene View drag/drop, Gizmo, Undo/Redo, dirty 확인, Console source navigation을 확인한다.

Phase 1 종료 조건:

- [ ] JSON 직접 편집 없이 Sprite Scene을 만들 수 있다.
- [ ] 사용자 편집 작업이 Undo/Redo와 dirty 상태를 일관되게 갱신한다.
- [ ] Play/Stop 후 편집 상태가 보존된다.
- [ ] 오류 원인을 Console에서 추적할 수 있다.
- [ ] 에디터를 종료하지 않고 Script 수정 흐름을 완료할 수 있다.

---

# Phase 2: Gameplay Production MVP

**Phase Goal:** 외부 파일과 코드를 조합해 충돌, 입력, 오디오, 애니메이션, 재사용 Object를 가진 완전한 2D 게임을 제작할 수 있다.

**Phase Exit Scenario:** Prefab 기반 Player와 Tilemap Level을 만들고, 입력으로 이동하며, Physics 충돌·Animation·Audio가 동작하는 게임을 빌드한다.

---

## Milestone 2-1: GUID 기반 AssetDatabase

**상세 계획서 경로:** `docs/plan/phase-2/task-2-1_asset_database.md`

**목표:** 파일 경로 변경에 강하고 import 상태를 추적할 수 있는 Asset 참조 기반을 만든다.

**핵심 구현:**

- UUID/GUID 타입과 안정된 문자열 형식
- 원본 Asset 옆 `.meta` 파일
- Asset scanner와 incremental refresh
- importer interface와 importer version
- source Asset, imported artifact, runtime Asset 구분
- GUID ↔ source path ↔ artifact path index
- rename/move/delete 충돌 처리
- Project Browser와 BuildPipeline 연동

**최소 meta 형식:**

```yaml
guid: 7fa30de5-5237-4b4f-85f8-9cb15ea2063d
importer: TextureImporter
importerVersion: 1
settings:
  filter: nearest
  wrap: clamp
```

**완료 기준:**

- [ ] Asset 이름 변경과 이동 후 Scene 참조가 유지된다.
- [ ] 같은 GUID 충돌을 감지하고 자동으로 조용히 덮어쓰지 않는다.
- [ ] importer version 변경 시 필요한 Asset만 재import한다.
- [ ] Build는 사용 중인 Asset과 의존 Asset만 패키징할 수 있다.
- [ ] 절대 경로가 Scene, Prefab, Material에 저장되지 않는다.

---

## Milestone 2-2: Input Actions, Tags, Layers

**상세 계획서 경로:** `docs/plan/phase-2/task-2-2_input_tags_layers.md`

**목표:** 하드코딩된 키와 숫자 layer 대신 프로젝트 설정 기반 gameplay 식별 체계를 제공한다.

**핵심 구현:**

- Input action map과 binding
- keyboard, mouse, gamepad
- button/axis/vector2 action
- runtime input snapshot
- ProjectSettings의 Tags/Layers
- GameObject tag와 layer 직렬화
- rendering mask와 physics collision matrix의 공용 layer 정의

**완료 기준:**

- [ ] 게임 코드는 GLFW key code를 직접 참조하지 않고 action을 조회한다.
- [ ] binding 변경이 코드 재컴파일 없이 적용된다.
- [ ] gamepad 연결/해제에 안전하게 대응한다.
- [ ] tag/layer 이름 변경 시 프로젝트 참조가 일관되게 migration된다.

---

## Milestone 2-3: Box2D Physics

**상세 계획서 경로:** `docs/plan/phase-2/task-2-3_physics.md`

**선행:** `2-2`

**목표:** 고정 timestep 기반 2D 충돌과 trigger event를 Component로 제공한다.

**핵심 구현:**

- PhysicsWorld의 생성·파괴 수명
- Rigidbody2D
- BoxCollider2D, CircleCollider2D
- body type, gravity scale, constraints
- collision layer matrix
- collision/trigger enter, stay, exit event
- Transform ↔ Physics 동기화 규칙
- debug draw

**설계 결정:**

- PhysicsWorld는 `World`마다 하나씩 존재한다.
- 동적 body는 Physics가 Transform을 쓰고, 정적·kinematic body는 명시된 규칙으로 동기화한다.
- Component 삭제와 GameObject 삭제는 step 중 즉시 Box2D 객체를 파괴하지 않고 안전한 queue를 사용한다.

**완료 기준:**

- [ ] 고정 입력에서 physics 결과가 허용 오차 내 재현된다.
- [ ] 충돌 event가 한 프레임에 중복 발행되지 않는다.
- [ ] Play/Stop 반복 시 Box2D body와 callback이 남지 않는다.
- [ ] layer collision matrix가 editor와 runtime에서 동일하다.
- [ ] packaged runtime의 physics smoke가 통과한다.

---

## Milestone 2-4: 2D Rendering Pipeline

**상세 계획서 경로:** `docs/plan/phase-2/task-2-4_rendering_pipeline.md`

**선행:** `2-1`

**목표:** 다수 Sprite를 예측 가능한 순서와 충분한 성능으로 렌더링하고 Material 확장을 지원한다.

**핵심 구현:**

- RenderQueue와 명시적 sort key
- sorting layer와 order in layer
- camera culling
- Sprite batch와 dynamic vertex buffer
- Texture atlas 또는 texture slot 정책
- Material/Shader Asset
- transparency 규칙
- Render stats와 debug overlay

**완료 기준:**

- [ ] 같은 입력에서 RenderQueue 순서가 결정적이다.
- [ ] 10,000 Sprite benchmark의 draw call 수가 목표값 이하다.
- [ ] Scene 저장·빌드 후 sorting layer와 Material 참조가 유지된다.
- [ ] batch 유무가 시각 결과와 picking 순서를 바꾸지 않는다.
- [ ] renderer resource 생성과 해제가 ASan/GL debug output에서 깨끗하다.

---

## Milestone 2-5: Audio

**상세 계획서 경로:** `docs/plan/phase-2/task-2-5_audio.md`

**선행:** `2-1`

**목표:** AudioClip Asset과 AudioSource/Listener Component로 2D 게임 오디오를 재생한다.

**핵심 구현:**

- Audio backend service
- AudioClip importer
- AudioSource, AudioListener
- play/stop/pause, loop, volume, pitch
- master/music/sfx mixer group
- Play/Stop과 World 파괴 시 안전한 voice 정리
- headless test에서 사용할 null backend

**완료 기준:**

- [ ] CI는 실제 오디오 장치 없이 null backend로 테스트한다.
- [ ] AudioClip 이동 후 GUID 참조가 유지된다.
- [ ] Play/Stop 반복 후 재생 중인 voice가 남지 않는다.
- [ ] 런타임 종료 시 audio thread와 resource가 정상 종료된다.

---

## Milestone 2-6: Animation

**상세 계획서 경로:** `docs/plan/phase-2/task-2-6_animation.md`

**선행:** `2-1`, `2-4`

**목표:** Sprite animation과 기본 상태 전이를 Asset으로 제작하고 런타임에서 재생한다.

**핵심 구현:**

- AnimationClip Asset
- AnimatorController Asset
- frame, duration, event
- parameter와 transition
- Animator Component
- Animation Panel과 preview
- build dependency 수집

**완료 기준:**

- [ ] 같은 timestep 입력에서 frame 전이가 결정적이다.
- [ ] clip/controller Asset 이동 후 참조가 유지된다.
- [ ] animation event가 지정 시점에 한 번만 발생한다.
- [ ] Scene View preview가 Edit World를 영구 변경하지 않는다.

---

## Milestone 2-7: Prefab

**상세 계획서 경로:** `docs/plan/phase-2/task-2-7_prefab.md`

**선행:** `2-1`, `1-2`, 안정된 Scene schema

**목표:** GameObject 계층을 재사용 Asset으로 저장하고 instance override를 안전하게 관리한다.

**핵심 구현:**

- Prefab Asset과 local object ID
- instantiate와 unpack
- property override 기록
- apply/revert
- nested prefab 정책
- missing prefab과 missing component 복구 표현
- Prefab editing isolation mode

**위험 통제:**

- Prefab은 Scene JSON을 단순 복사한 별도 형식으로 구현하지 않는다.
- override 경로는 배열 index보다 안정된 object/component/property ID를 사용한다.
- nested prefab과 cyclic dependency를 검출한다.

**완료 기준:**

- [ ] Prefab 수정이 override가 없는 instance에 반영된다.
- [ ] instance override는 Prefab 수정 후에도 유지된다.
- [ ] 삭제·이름 변경·component 재배치 후 override가 잘못된 대상에 적용되지 않는다.
- [ ] Prefab instance를 포함한 Scene이 빌드 런타임에서 동일하게 로드된다.

---

## Milestone 2-8: Tilemap 제작 흐름

**상세 계획서 경로:** `docs/plan/phase-2/task-2-8_tilemap.md`

**선행:** `2-1`, `2-4`, `1-2`

**목표:** 대규모 2D Level을 Tile Palette로 빠르게 편집하고 효율적으로 렌더링한다.

**핵심 구현:**

- Tile Asset과 Tile Palette
- Grid/Tilemap Component
- paint, erase, box, fill, picker 도구
- chunk 단위 저장과 렌더링
- TilemapCollider2D 생성
- palette와 tile GUID 참조
- Undo 가능한 brush stroke

**완료 기준:**

- [ ] 한 번의 brush drag가 Undo 항목 하나다.
- [ ] 큰 Tilemap에서 변경된 chunk만 다시 만든다.
- [ ] 빈 Tile과 missing Tile이 구분된다.
- [ ] Tilemap 저장·빌드·런타임 로드가 동일한 결과를 낸다.
- [ ] collider 갱신이 편집 중 프레임을 장시간 막지 않는다.

---

## Phase 2 통합 게이트

새로운 게임 fixture `tests/smoke/platformer_project`를 만든다. fixture는 아래를 포함한다.

- Player Prefab
- Tilemap Level
- Input action map
- Rigidbody2D와 Collider2D
- idle/run Animation
- jump AudioClip
- sorting layer와 Material

자동 검증:

```text
project import
→ prefab instantiate
→ input replay
→ physics fixed steps
→ animation transition
→ audio null backend event
→ build
→ packaged runtime deterministic result report
```

Phase 2 종료 조건:

- [ ] 실제 작은 2D platformer를 에디터에서 제작할 수 있다.
- [ ] Asset 이동과 이름 변경이 Scene/Prefab 참조를 깨뜨리지 않는다.
- [ ] 렌더링·물리·입력 결과가 테스트 가능한 결정적 경계를 갖는다.
- [ ] 빌드 결과에 사용되지 않는 원본 Asset이 무조건 포함되지 않는다.

---

# Phase 3: Distribution & Reliability

**Phase Goal:** 프로젝트 규모가 커져도 진단·배포·업그레이드가 가능한 제작 환경을 만든다.

---

## Milestone 3-1: Build Profiles와 플랫폼 패키징

**상세 계획서 경로:** `docs/plan/phase-3/task-3-1_build_profiles.md`

**목표:** Debug/Development/Release 설정과 플랫폼별 독립 실행 패키지를 재현 가능하게 만든다.

**핵심 구현:**

- BuildProfile Asset
- development logging과 debug overlay 옵션
- Asset dependency manifest와 content hash
- incremental build cache
- Windows/macOS/Linux 패키지 구조
- icon, version, metadata
- macOS app bundle과 code signing hook
- CI release artifact와 checksum

**완료 기준:**

- [ ] 같은 source revision과 profile에서 동일 manifest가 생성된다.
- [ ] Release 패키지에 Editor 전용 코드와 source Asset이 없다.
- [ ] 깨끗한 머신에서 패키지가 실행된다.
- [ ] CI artifact가 smoke를 통과한 빌드에서만 생성된다.

---

## Milestone 3-2: Profiler와 Runtime Diagnostics

**상세 계획서 경로:** `docs/plan/phase-3/task-3-2_profiler_diagnostics.md`

**목표:** CPU, rendering, memory, physics 병목과 runtime 오류를 재현 가능한 자료로 수집한다.

**핵심 구현:**

- frame marker와 scoped CPU timer
- subsystem별 frame time
- draw call, batch, triangle, texture 통계
- Object/Component/resource count
- frame history와 Profiler Panel
- crash log와 마지막 Scene/engine version
- smoke report 확장

**완료 기준:**

- [ ] profiler 비활성 시 측정 가능한 오버헤드 목표를 넘지 않는다.
- [ ] 긴 프레임의 subsystem 원인을 Panel에서 구분할 수 있다.
- [ ] crash report가 프로젝트 경로의 개인 정보와 절대 경로를 불필요하게 노출하지 않는다.
- [ ] CI benchmark가 주요 성능 회귀를 탐지한다.

---

## Milestone 3-3: Multi-scene와 Streaming

**상세 계획서 경로:** `docs/plan/phase-3/task-3-3_multi_scene.md`

**선행:** `2-1`, `2-7`

**목표:** 여러 Scene을 additive하게 로드하고 게임 진행 중 안전하게 전환한다.

**핵심 구현:**

- Scene handle과 Scene lifecycle
- single/additive load
- async load와 activation
- persistent GameObject 정책
- cross-scene reference 정책
- build scene list
- unload 안전성

**완료 기준:**

- [ ] Scene unload 후 Object, physics body, audio voice, renderer resource가 남지 않는다.
- [ ] 비동기 load가 main thread의 플랫폼·GPU 제약을 위반하지 않는다.
- [ ] cross-scene reference가 unload 후 dangling pointer가 되지 않는다.
- [ ] build profile의 Scene 순서와 런타임 전환이 일치한다.

---

## Milestone 3-4: Release 품질, 문서, Sample

**상세 계획서 경로:** `docs/plan/phase-3/task-3-4_release_quality.md`

**목표:** 외부 사용자가 설치부터 첫 게임 빌드까지 재현할 수 있는 공개 품질을 만든다.

**핵심 구현:**

- semantic version과 changelog
- Scene/Asset schema migration matrix
- 사용자 문서와 API 문서
- editor onboarding과 starter templates
- platformer sample과 최소 sample
- issue template와 diagnostic bundle
- release checklist와 rollback 절차

**완료 기준:**

- [ ] 깨끗한 환경에서 문서만 따라 sample을 빌드할 수 있다.
- [ ] 이전 두 release 프로젝트 fixture를 현재 버전에서 열 수 있다.
- [ ] starter template이 CI에서 빌드·실행된다.
- [ ] release artifact, checksum, changelog, migration note가 자동 생성된다.

---

## 4. 상세 계획서 작성 순서

각 Milestone은 구현 직전에 아래 순서로 별도 상세 계획서를 작성한다. 한 번에 Phase 전체 구현 계획을 코드 수준으로 고정하지 않는다. 앞 단계에서 얻은 API와 schema를 다음 단계 계획에 반영해야 하기 때문이다.

```text
1. docs/plan/phase-1/task-1-1_scene_view.md
2. docs/plan/phase-1/task-1-2_gizmo_and_undo.md
3. docs/plan/phase-1/task-1-4_console_and_logging.md
4. docs/plan/phase-1/task-1-3_authoring_ux.md
5. docs/plan/phase-1/task-1-5_script_workflow.md
6. docs/plan/phase-2/task-2-1_asset_database.md
7. docs/plan/phase-2/task-2-2_input_tags_layers.md
8. docs/plan/phase-2/task-2-3_physics.md
9. docs/plan/phase-2/task-2-4_rendering_pipeline.md
10. docs/plan/phase-2/task-2-5_audio.md
11. docs/plan/phase-2/task-2-6_animation.md
12. docs/plan/phase-2/task-2-7_prefab.md
13. docs/plan/phase-2/task-2-8_tilemap.md
14. docs/plan/phase-3/task-3-1_build_profiles.md
15. docs/plan/phase-3/task-3-2_profiler_diagnostics.md
16. docs/plan/phase-3/task-3-3_multi_scene.md
17. docs/plan/phase-3/task-3-4_release_quality.md
```

각 상세 계획서는 최소한 다음 내용을 포함한다.

- 현재 코드에서 확인한 정확한 결함
- 선행 Milestone과 의존 API
- 생성·수정 파일 목록
- 첫 실패 테스트
- 단계별 구현 코드 또는 정확한 API 계약
- 로컬 검증 명령과 기대 결과
- 기존 smoke fixture에 추가할 회귀 시나리오
- 비목표와 후속 Milestone으로 넘길 범위
- 작고 독립적인 커밋 순서

---

## 5. 우선순위와 범위 통제

### 반드시 먼저 할 것

1. Scene View와 Selection/Gizmo
2. Console과 진단
3. GUID 기반 AssetDatabase
4. Input/Tags/Layers
5. Physics와 Prefab
6. Build Profile과 배포 신뢰성

### 의도적으로 늦출 것

- 3D rendering과 3D physics
- visual scripting
- 네트워크 multiplayer
- package marketplace
- collaborative editing
- mobile platform
- ECS 전면 전환
- custom shader graph

위 기능은 핵심 2D 제작 흐름과 데이터 호환성이 안정된 뒤 별도 제안서로 평가한다. 특히 ECS 전면 전환은 현재 GameObject/Component API를 깨뜨리는 비용이 크므로, profiler로 필요성이 증명되기 전에는 진행하지 않는다.

---

## 6. 지속적으로 추적할 품질 지표

| 지표 | Phase 1 목표 | Phase 2 목표 | Phase 3 목표 |
|---|---:|---:|---:|
| Debug 전체 테스트 | 필수 통과 | 필수 통과 | 필수 통과 |
| Release smoke | 필수 통과 | 필수 통과 | 플랫폼별 필수 통과 |
| ASan/UBSan | 핵심 서비스 | 전체 Core/Runtime | release candidate |
| 에디터 cold start | 측정 시작 | 회귀 방지 | 예산 설정 |
| 10k Sprite draw calls | baseline | batching 목표 설정 | 플랫폼별 유지 |
| Play/Stop 100회 leak | 0 known leak | 0 known leak | CI 장기 테스트 |
| 이전 schema fixture | Phase 0 | 직전 두 version | 직전 두 release |

지표의 숫자 목표는 해당 시스템의 첫 benchmark 결과를 기록한 후 상세 계획서에서 고정한다. 근거 없이 임의의 성능 수치를 완료 기준으로 두지 않는다.

---

## 7. 로드맵 완료 기준

- [ ] 각 Milestone이 구현 직전 별도 상세 계획서로 구체화된다.
- [ ] 새로운 기능은 Core/Runtime/Editor 계층 경계를 위반하지 않는다.
- [ ] 지속 데이터 변경에는 version과 migration 테스트가 있다.
- [ ] Phase별 Exit Scenario가 자동 스모크와 수동 검증 모두에서 통과한다.
- [ ] Phase 0 `smoke_end_to_end`는 모든 후속 Phase에서 계속 통과한다.
- [ ] Phase 3 종료 시 깨끗한 머신에서 sample 프로젝트를 열고 독립 실행 파일로 빌드할 수 있다.
