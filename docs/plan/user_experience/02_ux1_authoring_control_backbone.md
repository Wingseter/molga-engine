# UX-1: Authoring Control Backbone (저작 제어 백본)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 모든 단계는 `- [ ]` 체크박스로 추적하고 TDD 순서(실패 테스트 → 실패 확인 → 최소 구현 → 통과 확인 → 커밋)를 지킨다. **이 문서는 UX 패스의 토대(foundational) milestone이다.** UX-2(Console/Task), UX-3(Asset Identity), UX-6(Advanced Production)이 여기서 정의하는 `SelectionService`와 command/dirty 계약 위에 쌓인다. 그러므로 여기서 만든 타입·이벤트·command 의미를 임의로 바꾸면 후속 문서가 깨진다.

**Goal:** Scene View가 1급 저작 표면이 되도록 한다. 사용자가 Scene View에서 Sprite를 직접 클릭해 선택하고, 2D Gizmo로 이동·회전·크기 조절하며, Inspector 숫자 편집과 Gizmo 편집이 **같은 command 의미**를 공유하고, 모든 변형이 Undo/Redo와 dirty 상태를 일관되게 갱신하게 한다. 선택은 더 이상 `HierarchyWindow`가 소유하지 않고 중앙 `SelectionService`가 ID로 소유하며, Play/Stop 후에도 편집 월드 오브젝트가 살아 있으면 선택이 유지된다.

**Architecture:** 선택 상태를 패널이 아니라 에디터 서비스가 소유한다. `SelectionService`는 선택된 GameObject **ID 집합**과 primary ID, 선택 변경 이벤트, 선택 소스(`SelectionSource`), 잠글 수 있는 Inspector 대상을 보관한다 — raw pointer가 아닌 ID를 들고 있으므로 World 교체(Play/Stop) 후에도 동일 ID를 다시 해석할 수 있다. Scene View 픽킹은 패널 안의 ad-hoc 코드가 아니라 테스트 가능한 순수 함수 경계(`viewport 좌표 → world 좌표 → 후보 → 정렬된 hit → SelectionService`)로 분리한다. 2D `TransformGizmo`는 tool(select/move/rotate/scale)·space(local/world)·snap(off/grid/increment) 상태와 drag 수명(begin 스냅샷 → live preview → commit **단일** command)을 관리하고, 실제 변형은 `TransformCommand` 하나로만 기록한다. Inspector의 Transform 편집도 같은 `TransformCommand`로 라우팅한다.

계층 경계(`phase-1-3_roadmap.md` §2.1)를 지킨다:

- Runtime(`molga_core`)은 ImGui나 Editor singleton을 include하지 않는다. → 픽킹 수학과 gizmo 기하는 ImGui 의존 없는 순수 코드로 `molga_core`에 두어 doctest로 직접 검증한다.
- Component가 Panel을 직접 호출하지 않는다. → `Transform::OnInspectorGUI`는 값을 직접 mutate하지 않고 편집 의도만 노출하거나, 라우팅을 위해 Editor command 헬퍼를 호출한다.
- 자산 참조를 절대 경로로 저장하지 않는다. → 이 milestone은 새 자산 경로를 도입하지 않는다.
- Panel이 World를 직접 재생·교체하지 않고 SceneDocument를 통한다. → 선택/명령은 World/SceneDocument를 통해서만 오브젝트를 찾는다.
- 사용자가 수행한 편집이 CommandHistory를 우회하지 않는다. → Gizmo/Inspector 변형은 전부 `TransformCommand`로만 들어간다.

**Tech Stack:** C++17, doctest, ImGui, OpenGL

**닫는 결함:** 갭 분석 §1 "Scene View, Selection, and Transform Tools"(중앙 픽킹/gizmo/선택 모델 부재)와 §2 "Undo/Redo and Dirty State"의 transform 관련 결함(`Transform::OnInspectorGUI()`가 position/rotation/scale을 command 없이 직접 변경). *다중 선택 UI, component add/remove/reset command, 단축키 바인딩 패널, 마키(marquee) 선택은 이 milestone의 비목표다(UX-6 및 후속). 단, `SelectionService`는 다중 선택을 패널 API 재변경 없이 받아들일 수 있게 설계한다.*

---

## 현재 상태 (검증된 사실)

- **선택은 `HierarchyWindow`가 소유한다.** `selectedObject`는 raw `GameObject*` 멤버(`src/Editor/Windows/HierarchyWindow.h:35`)이고, getter/setter는 인라인(`HierarchyWindow.h:19-20`)이며 단일 선택 콜백(`onSelectionChanged`, `HierarchyWindow.h:36`)만 있다. 클릭 선택은 `ImGui::IsItemClicked()` → `selectedObject = obj`(`HierarchyWindow.cpp:160-163`).
- **`Editor`는 선택을 Hierarchy에서 되읽어온다.** `Editor::GetSelectedObject()`는 `windowManager.GetAs<HierarchyWindow>(...)->GetSelectedObject()`를 그대로 반환(`src/Editor/Editor.cpp:309-312`). `Editor::SetSelectedObject()`는 Hierarchy와 Inspector 양쪽에 push(`Editor.cpp:314-323`). 중앙 모델·다중 선택·잠금 Inspector·선택 소스가 전부 없다.
- **Inspector 대상은 raw pointer.** `InspectorWindow::SetTarget/GetTarget`(`src/Editor/Windows/InspectorWindow.h:15-16`)는 `GameObject* target`(`InspectorWindow.h:21`) 하나만 들고 있고 잠금 개념이 없다.
- **Scene View에 픽킹·gizmo·tool 상태가 없다.** `SceneViewWindow.h:21-94`에는 픽킹/gizmo/tool/snap 멤버나 메서드가 선언되어 있지 않다. 좌클릭 선택 경로가 아예 없고, 우클릭 컨텍스트 생성과 MMB 패닝/휠 줌만 처리한다(`SceneViewWindow.cpp:177-181`, `:333-400`).
- **viewport→world 변환이 이미 존재하지만 패널 내부에만 있다.** `SceneViewWindow::ScreenToWorld`(`SceneViewWindow.cpp:486-501`)는 `outX = camX + panelSize.x*0.5 + (sx - panelSize.x*0.5)/zoom` 공식을 쓴다. 단, 같은 파일의 휠 줌(`:349-359`)과 카메라 오버레이(`:158-162`)는 또 다른 중심 계산을 쓰고 있어 변환 공식이 한 곳에 모여 있지 않다 — 픽킹을 추가하려면 이 변환을 테스트 가능한 한 함수로 통일해야 한다.
- **`Transform::OnInspectorGUI()`가 값을 직접 변경한다.** `DragFloat2("Position", ...)` → `SetPosition(...)`, `DragFloat("Rotation", ...)` → `SetRotation(...)`, `DragFloat2("Scale", ...)` → `SetScale(...)` 가 command 없이 즉시 mutate(`src/ECS/Components/Transform.cpp:86-103`). dirty도 갱신하지 않는다. `Transform`의 접근자는 `GetPosition/SetPosition`, `GetRotation/SetRotation`, `GetScale/SetScale`(`src/ECS/Components/Transform.h:15-32`), 회전은 degree.
- **Command 인프라는 존재하나 transform용 command가 없다.** 헤더 온리 `CommandHistory`(`src/Editor/Commands/CommandHistory.h:10-43`: `Execute/Undo/Redo/CanUndo/CanRedo/Clear`)와 `ICommand`(`src/Editor/Commands/EditorCommand.h:8-14`: `Execute()/Undo()/Name()`)가 있다. 구체 command는 `ObjectCommands.{h,cpp}`의 Create/Delete/Rename/Reparent/Duplicate뿐(`src/Editor/Commands/ObjectCommands.h:11-78`)이고 `TransformCommand`는 없다.
- **Undo/Redo 메뉴는 이미 배선되어 있다.** `Editor.cpp:170-175`가 `commandHistory.CanUndo()/Undo()` 등을 호출. dirty 표시(`*unsaved`)도 메뉴 바에 있다(`Editor.cpp:237-239`). `Editor`는 `commandHistory` 멤버(`Editor.h:86`)와 `MarkSceneModified()/FindObjectById()/ShareObjectById()` 헬퍼(`Editor.h:45-49`)를 갖는다.
- **Play/Stop은 World를 교체하고 선택·CommandHistory를 강제로 비운다.** `main.cpp:150-163`의 콜백은 Edit→Play에서 `SetSelectedObject(nullptr)` + `commandHistory.Clear()` 후 `SceneDocument::EnterPlay()`(playWorld = `editWorld_.Clone()`)로 ActiveWorld를 교체하고, Stop에서도 똑같이 선택을 비운 뒤 EditWorld로 되돌린다. **즉 현재는 선택이 Play/Stop을 절대 넘기지 못한다.**
- **Clone은 GameObject ID를 보존한다.** `SceneDocument::EnterPlay`는 `editWorld_.Clone()`을 쓰고(`src/Editor/SceneDocument.h:18-21`), `World::FindById`(`src/Core/World.h:23`)와 `test_world.cpp`의 "World::Clone is an independent deep copy"/"FindById locates and rejects"가 clone 후에도 동일 ID로 오브젝트를 찾을 수 있음을 보장한다. → **ID 기반 선택이면 Play/Stop을 넘길 수 있다.**
- **픽킹 hit-test에 필요한 크기 정보는 SpriteRenderer에 있다.** `GetWidth()/GetHeight()/GetSize()`와 `GetSortingOrder()`(`src/ECS/Components/SpriteRenderer.h:36-48`, 기본 32×32 `:67-68`). 정렬 렌더는 `sortingOrder` 오름차순(`SceneViewWindow.cpp:289-290`) — 픽킹은 그 역순(앞 = 큰 order)으로 hit해야 한다.
- **테스트는 기본적으로 `molga_core`만 링크한다.** `tests/CMakeLists.txt`의 `molga_add_test(name src)`는 `molga_core doctest_main molga_warnings`만 링크(`tests/CMakeLists.txt:9-14`). 에디터 소스를 쓰는 테스트는 `target_sources`로 명시적으로 추가해야 한다(예: `test_build_manager`). → **픽킹/gizmo 수학과 `SelectionService`는 `molga_core`(ENGINE_SOURCES)에 두어 ImGui/Editor 없이 단위 테스트한다.** command 라우팅은 `EDITOR_SOURCES`에 둔다.

---

## 파일 구조

**Create (Runtime/Core, `molga_core`에 등록 → ImGui 비의존, doctest 직접 검증):**
- `src/Editor/Selection/SelectionService.h`
- `src/Editor/Selection/SelectionService.cpp`
- `src/Editor/ViewportMath.h` (헤더 온리: viewport↔world 변환, AABB hit)
- `src/Editor/ScenePicker.h` (헤더 온리: 정렬된 hit 후보 계산)
- `src/Editor/Gizmos/TransformGizmoMath.h` (헤더 온리: 핸들 hit, snap, drag delta 적용)

> 위 다섯은 ImGui/Editor singleton을 include하지 않는다. `SelectionService.cpp`만 .cpp가 필요하고(이벤트 구독자 벡터 관리), 나머지는 헤더 온리.

**Create (Editor, `EDITOR_SOURCES`에 등록 → Editor singleton/World 접근):**
- `src/Editor/Commands/TransformCommand.h`
- `src/Editor/Commands/TransformCommand.cpp`
- `src/Editor/Gizmos/TransformGizmo.h`
- `src/Editor/Gizmos/TransformGizmo.cpp` (ImGui 드로잉 + drag 수명 → TransformCommand commit)

**Create (Tests):**
- `tests/test_selection_service.cpp`
- `tests/test_viewport_math.cpp`
- `tests/test_scene_picker.cpp`
- `tests/test_transform_gizmo_math.cpp`
- `tests/test_transform_command.cpp` (Editor 소스 링크 — 아래 CMake 참고)

**Modify:**
- `src/Editor/Editor.h` — `SelectionService& GetSelection()` 접근자 + `SelectionService selection_` 멤버, `GetSelectedObject/SetSelectedObject`를 SelectionService 위임으로 변경.
- `src/Editor/Editor.cpp` — 위임 구현, Inspector/Hierarchy 동기화를 selection 이벤트 구독으로 이동.
- `src/Editor/Windows/SceneViewWindow.h` — tool/gizmo/픽킹 멤버 + 좌클릭 픽킹 핸들러 선언.
- `src/Editor/Windows/SceneViewWindow.cpp` — 좌클릭 픽킹 → `ScenePicker` → `SelectionService`, 선택 outline 드로우, `TransformGizmo` 통합. `ScreenToWorld`를 `ViewportMath`로 위임.
- `src/Editor/Windows/HierarchyWindow.h` — `selectedObject` raw 멤버 제거, `Editor::GetSelection()`을 읽도록 변경(선택 소유권 이전).
- `src/Editor/Windows/HierarchyWindow.cpp` — 클릭 시 `Editor::Get().GetSelection().Select(id, SelectionSource::Hierarchy)` 호출.
- `src/Editor/Windows/InspectorWindow.h` / `.cpp` — `target`을 selection primary에서 해석하고 잠금(lock) 토글 지원.
- `src/ECS/Components/Transform.cpp` — `OnInspectorGUI`가 직접 mutate 대신 Editor의 transform-edit 라우팅 헬퍼를 호출(아래 Task C).
- `src/main.cpp` — Play/Stop 콜백에서 선택을 비우지 않고 **primary 선택 ID를 보존**해 World 교체 후 재해석(`SelectionService::Rebind`).
- `CMakeLists.txt` — `ENGINE_SOURCES`에 `SelectionService.cpp`; `EDITOR_SOURCES`에 `TransformCommand.cpp`, `TransformGizmo.cpp`.
- `tests/CMakeLists.txt` — 5개 테스트 등록(transform_command는 Editor 소스 추가).

---

## Task A. SelectionService (ID 기반 중앙 선택, TDD)

> 패널/raw pointer가 아니라 ID 집합을 소유한다. 단일 선택부터 시작하되 API는 다중 선택을 받을 수 있게 둔다. ImGui 비의존이라 `molga_core`에서 단위 테스트한다.

**Files:**
- Create: `src/Editor/Selection/SelectionService.h`, `src/Editor/Selection/SelectionService.cpp`
- Create: `tests/test_selection_service.cpp`
- Modify: `CMakeLists.txt`(ENGINE_SOURCES), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성** — `tests/test_selection_service.cpp`

```cpp
#include "Editor/Selection/SelectionService.h"
#include "doctest.h"
#include <vector>

using molga::SelectionService;
using molga::SelectionSource;

TEST_CASE("single selection sets primary and reports membership") {
    SelectionService sel;
    CHECK_FALSE(sel.HasSelection());
    sel.Select(42, SelectionSource::SceneView);
    CHECK(sel.HasSelection());
    CHECK(sel.PrimaryId() == 42u);
    CHECK(sel.IsSelected(42));
    CHECK(sel.LastSource() == SelectionSource::SceneView);
    CHECK(sel.SelectedIds().size() == 1);
}

TEST_CASE("selecting another id replaces the previous single selection") {
    SelectionService sel;
    sel.Select(1, SelectionSource::Hierarchy);
    sel.Select(2, SelectionSource::SceneView);
    CHECK(sel.PrimaryId() == 2u);
    CHECK_FALSE(sel.IsSelected(1));
    CHECK(sel.SelectedIds().size() == 1);
}

TEST_CASE("Clear empties the selection and primary") {
    SelectionService sel;
    sel.Select(7, SelectionSource::Hierarchy);
    sel.Clear(SelectionSource::Code);
    CHECK_FALSE(sel.HasSelection());
    CHECK(sel.PrimaryId() == 0u);          // 0 = 선택 없음
}

TEST_CASE("change listeners fire on select and clear with the source") {
    SelectionService sel;
    int calls = 0;
    SelectionSource seen = SelectionSource::Code;
    sel.AddListener([&](const SelectionService& s, SelectionSource src) {
        ++calls;
        seen = src;
        (void)s;
    });
    sel.Select(3, SelectionSource::SceneView);
    CHECK(calls == 1);
    CHECK(seen == SelectionSource::SceneView);
    sel.Clear(SelectionSource::Hierarchy);
    CHECK(calls == 2);
    CHECK(seen == SelectionSource::Hierarchy);
}

TEST_CASE("selecting the same id again does not re-notify") {
    SelectionService sel;
    int calls = 0;
    sel.AddListener([&](const SelectionService&, SelectionSource) { ++calls; });
    sel.Select(5, SelectionSource::Hierarchy);
    sel.Select(5, SelectionSource::Hierarchy);   // no-op
    CHECK(calls == 1);
}

TEST_CASE("Rebind keeps selection only for ids that still exist") {
    SelectionService sel;
    sel.Select(10, SelectionSource::SceneView);
    sel.Rebind([](unsigned int id) { return id == 10; });   // 10은 살아있음
    CHECK(sel.IsSelected(10));
    sel.Rebind([](unsigned int) { return false; });          // 모두 사라짐
    CHECK_FALSE(sel.HasSelection());
    CHECK(sel.PrimaryId() == 0u);
}

TEST_CASE("locked inspector target overrides primary until unlocked") {
    SelectionService sel;
    sel.Select(1, SelectionSource::Hierarchy);
    sel.LockInspector(1);
    sel.Select(2, SelectionSource::SceneView);   // 선택은 2로 이동
    CHECK(sel.PrimaryId() == 2u);
    CHECK(sel.InspectorTargetId() == 1u);        // 잠금된 1을 계속 본다
    sel.UnlockInspector();
    CHECK(sel.InspectorTargetId() == 2u);        // 다시 primary를 따라간다
}
```

- [ ] **Step 2: 등록 + 실패 확인** — `tests/CMakeLists.txt`에 추가

```cmake
molga_add_test(test_selection_service test_selection_service.cpp)
```

Run:
```bash
cmake --build --preset debug --target test_selection_service -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: SelectionService 헤더 작성** — `src/Editor/Selection/SelectionService.h`

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace molga {

// 선택 변경을 일으킨 주체. 패널이 자기 자신 이벤트를 무시할 때 사용.
enum class SelectionSource { Code, SceneView, Hierarchy, Inspector };

// 에디터 전역 선택 모델. raw pointer가 아니라 GameObject ID(0 = 없음)를 소유한다.
// 단일 선택으로 시작하되 SelectedIds()는 다중 선택을 받을 수 있는 형태로 노출한다.
class SelectionService {
public:
    using Listener = std::function<void(const SelectionService&, SelectionSource)>;

    // 단일 선택으로 교체. id == 0 이면 Clear와 동일.
    void Select(unsigned int id, SelectionSource source);
    void Clear(SelectionSource source);

    bool HasSelection() const { return !ids_.empty(); }
    unsigned int PrimaryId() const { return primary_; }
    bool IsSelected(unsigned int id) const;
    const std::vector<unsigned int>& SelectedIds() const { return ids_; }
    SelectionSource LastSource() const { return lastSource_; }

    // World 교체(Play/Stop) 후 호출: alive(id)가 false인 선택을 떨군다.
    void Rebind(const std::function<bool(unsigned int)>& alive);

    // 잠긴 Inspector 대상. 잠금 동안 InspectorTargetId는 primary 대신 잠금 id를 반환.
    void LockInspector(unsigned int id);
    void UnlockInspector();
    unsigned int InspectorTargetId() const { return locked_ ? lockedId_ : primary_; }
    bool IsInspectorLocked() const { return locked_; }

    void AddListener(Listener l) { listeners_.push_back(std::move(l)); }

private:
    void Notify(SelectionSource source);

    std::vector<unsigned int> ids_;          // 선택된 ID들(현재는 0..1개)
    unsigned int primary_ = 0;               // 0 = 없음
    SelectionSource lastSource_ = SelectionSource::Code;
    bool locked_ = false;
    unsigned int lockedId_ = 0;
    std::vector<Listener> listeners_;
};

} // namespace molga
```

- [ ] **Step 4: SelectionService 구현** — `src/Editor/Selection/SelectionService.cpp`

```cpp
#include "Editor/Selection/SelectionService.h"
#include <algorithm>

namespace molga {

void SelectionService::Select(unsigned int id, SelectionSource source) {
    if (id == 0) { Clear(source); return; }
    if (ids_.size() == 1 && ids_[0] == id && primary_ == id) {
        return;  // 같은 단일 선택 — 재알림 없음
    }
    ids_.assign(1, id);
    primary_ = id;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::Clear(SelectionSource source) {
    if (ids_.empty() && primary_ == 0) return;
    ids_.clear();
    primary_ = 0;
    lastSource_ = source;
    Notify(source);
}

bool SelectionService::IsSelected(unsigned int id) const {
    return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}

void SelectionService::Rebind(const std::function<bool(unsigned int)>& alive) {
    ids_.erase(std::remove_if(ids_.begin(), ids_.end(),
                              [&](unsigned int id) { return !alive(id); }),
               ids_.end());
    if (primary_ != 0 && !alive(primary_)) {
        primary_ = ids_.empty() ? 0u : ids_.front();
    }
    if (locked_ && !alive(lockedId_)) UnlockInspector();
}

void SelectionService::LockInspector(unsigned int id) {
    locked_ = (id != 0);
    lockedId_ = id;
}

void SelectionService::UnlockInspector() {
    locked_ = false;
    lockedId_ = 0;
}

void SelectionService::Notify(SelectionSource source) {
    for (auto& l : listeners_) l(*this, source);
}

} // namespace molga
```

- [ ] **Step 5: ENGINE_SOURCES 등록 + 테스트 통과** — `CMakeLists.txt`의 `set(ENGINE_SOURCES ...)`에 추가

```cmake
    src/Editor/Selection/SelectionService.cpp
```

Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_selection_service -j4
ctest --preset debug -R test_selection_service --output-on-failure
```
Expected: PASS, `7 | 7 passed`.

- [ ] **Step 6: 커밋**

```bash
git add src/Editor/Selection/SelectionService.h src/Editor/Selection/SelectionService.cpp \
        tests/test_selection_service.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(editor): id-based SelectionService with rebind + lockable inspector target"
```

---

## Task B. Scene View 픽킹 (ViewportMath + ScenePicker, TDD)

> 픽킹을 패널 밖의 두 순수 경계로 나눈다: (1) `ViewportMath` — viewport 좌표↔world 좌표 변환과 AABB hit, (2) `ScenePicker` — sortingOrder 역순으로 후보를 정렬해 hit 리스트를 만든다. 둘 다 ImGui/GameObject 의존 없이 숫자만 다뤄 doctest로 직접 검증한다. `SceneViewWindow`의 좌클릭 핸들러가 이 둘을 호출해 `SelectionService::Select`로 연결한다.

**Files:**
- Create: `src/Editor/ViewportMath.h`, `src/Editor/ScenePicker.h`
- Create: `tests/test_viewport_math.cpp`, `tests/test_scene_picker.cpp`
- Modify: `src/Editor/Windows/SceneViewWindow.{h,cpp}`, `tests/CMakeLists.txt`

- [ ] **Step 1: ViewportMath 실패 테스트** — `tests/test_viewport_math.cpp`

```cpp
#include "Editor/ViewportMath.h"
#include "doctest.h"

using molga::ViewportCamera;
using molga::ScreenToWorld;
using molga::WorldToScreen;
using molga::PointInAabb;

TEST_CASE("ScreenToWorld matches the existing SceneView convention") {
    // SceneViewWindow.cpp:486-501 과 동일한 규약:
    // outX = camX + w*0.5 + (sx - w*0.5)/zoom
    ViewportCamera cam{ /*camX*/ 100.f, /*camY*/ 50.f, /*zoom*/ 1.f };
    float wx, wy;
    ScreenToWorld(cam, 800.f, 600.f, 400.f, 300.f, wx, wy);
    CHECK(wx == doctest::Approx(100.f + 400.f + (400.f - 400.f) / 1.f));
    CHECK(wy == doctest::Approx(50.f  + 300.f + (300.f - 300.f) / 1.f));
}

TEST_CASE("ScreenToWorld and WorldToScreen are inverse at zoom != 1") {
    ViewportCamera cam{ -20.f, 30.f, 2.f };
    float wx, wy; ScreenToWorld(cam, 640.f, 480.f, 120.f, 90.f, wx, wy);
    float sx, sy; WorldToScreen(cam, 640.f, 480.f, wx, wy, sx, sy);
    CHECK(sx == doctest::Approx(120.f));
    CHECK(sy == doctest::Approx(90.f));
}

TEST_CASE("PointInAabb respects center+halfsize bounds") {
    CHECK(PointInAabb(10.f, 10.f, /*cx*/10.f, /*cy*/10.f, /*hw*/16.f, /*hh*/16.f));
    CHECK(PointInAabb(25.f, 10.f, 10.f, 10.f, 16.f, 16.f));   // 경계 안 (26까지)
    CHECK_FALSE(PointInAabb(27.f, 10.f, 10.f, 10.f, 16.f, 16.f));
}
```

- [ ] **Step 2: ScenePicker 실패 테스트** — `tests/test_scene_picker.cpp`

```cpp
#include "Editor/ScenePicker.h"
#include "doctest.h"
#include <vector>

using molga::PickCandidate;
using molga::PickAt;

TEST_CASE("PickAt returns the front-most (highest sortingOrder) hit first") {
    std::vector<PickCandidate> c = {
        { /*id*/1, /*cx*/0.f, 0.f, /*hw*/16.f, /*hh*/16.f, /*order*/0 },
        { /*id*/2, 0.f, 0.f, 16.f, 16.f, /*order*/5 },   // 더 앞
    };
    auto hits = PickAt(c, 0.f, 0.f);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 2u);   // 큰 order = 앞 = 먼저
    CHECK(hits[1] == 1u);
}

TEST_CASE("PickAt skips candidates whose bounds exclude the point") {
    std::vector<PickCandidate> c = {
        { 1, 0.f, 0.f, 16.f, 16.f, 0 },
        { 2, 100.f, 0.f, 16.f, 16.f, 0 },
    };
    auto hits = PickAt(c, 5.f, 5.f);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == 1u);
}

TEST_CASE("PickAt on empty space returns no hits") {
    std::vector<PickCandidate> c = { { 1, 0.f, 0.f, 16.f, 16.f, 0 } };
    CHECK(PickAt(c, 1000.f, 1000.f).empty());
}

TEST_CASE("ties on sortingOrder keep stable input order at front") {
    std::vector<PickCandidate> c = {
        { 1, 0.f, 0.f, 16.f, 16.f, 3 },
        { 2, 0.f, 0.f, 16.f, 16.f, 3 },
    };
    auto hits = PickAt(c, 0.f, 0.f);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 2u);   // 동률이면 입력 뒤쪽(나중에 그려진 것)이 앞
}
```

- [ ] **Step 3: 등록 + 실패 확인** — `tests/CMakeLists.txt`

```cmake
molga_add_test(test_viewport_math  test_viewport_math.cpp)
molga_add_test(test_scene_picker   test_scene_picker.cpp)
```

Run:
```bash
cmake --build --preset debug --target test_viewport_math test_scene_picker -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 4: ViewportMath 작성** — `src/Editor/ViewportMath.h`

```cpp
#pragma once

namespace molga {

// SceneView 에디터 카메라의 최소 상태(좌상단 기준 camX/camY + 균일 zoom).
struct ViewportCamera {
    float camX;
    float camY;
    float zoom;
};

// 패널 내 스크린 픽셀 → world. SceneViewWindow.cpp:486-501 규약을 그대로 옮긴다.
inline void ScreenToWorld(const ViewportCamera& cam, float vpW, float vpH,
                          float sx, float sy, float& outX, float& outY) {
    outX = cam.camX + vpW * 0.5f + (sx - vpW * 0.5f) / cam.zoom;
    outY = cam.camY + vpH * 0.5f + (sy - vpH * 0.5f) / cam.zoom;
}

// world → 패널 내 스크린 픽셀 (ScreenToWorld의 역).
inline void WorldToScreen(const ViewportCamera& cam, float vpW, float vpH,
                          float wx, float wy, float& outSx, float& outSy) {
    outSx = (wx - cam.camX - vpW * 0.5f) * cam.zoom + vpW * 0.5f;
    outSy = (wy - cam.camY - vpH * 0.5f) * cam.zoom + vpH * 0.5f;
}

// 점(px,py)이 중심(cx,cy)·반치수(hw,hh) AABB 안에 있는가(경계 포함).
inline bool PointInAabb(float px, float py, float cx, float cy, float hw, float hh) {
    return px >= cx - hw && px <= cx + hw && py >= cy - hh && py <= cy + hh;
}

} // namespace molga
```

- [ ] **Step 5: ScenePicker 작성** — `src/Editor/ScenePicker.h`

```cpp
#pragma once

#include "Editor/ViewportMath.h"
#include <algorithm>
#include <vector>

namespace molga {

// 픽킹 후보 1개: GameObject ID + world AABB(center, half-size) + sortingOrder.
struct PickCandidate {
    unsigned int id;
    float cx, cy;     // world center
    float hw, hh;     // half width/height
    int order;        // sortingOrder (큰 값 = 앞)
};

// world 점 (wx,wy)에 맞는 후보를 앞에서 뒤 순서(order 내림차순, 동률은 입력 뒤가 앞)로 반환.
inline std::vector<unsigned int> PickAt(const std::vector<PickCandidate>& candidates,
                                        float wx, float wy) {
    std::vector<const PickCandidate*> hits;
    for (const auto& c : candidates) {
        if (PointInAabb(wx, wy, c.cx, c.cy, c.hw, c.hh)) hits.push_back(&c);
    }
    // 안정 정렬: order 내림차순. 동률이면 입력에서 나중에 온(뒤에 그려진) 것이 앞.
    std::stable_sort(hits.begin(), hits.end(),
        [](const PickCandidate* a, const PickCandidate* b) { return a->order > b->order; });
    std::vector<unsigned int> out;
    out.reserve(hits.size());
    for (auto* h : hits) out.push_back(h->id);
    return out;
}

} // namespace molga
```

> 동률 처리: 입력은 sortingOrder 오름차순으로 그려지므로(`SceneViewWindow.cpp:289-290`) 같은 order에서는 입력 뒤쪽이 위에 그려진다. `stable_sort`는 동률의 입력 순서를 보존하므로, 입력을 **역순으로 넣거나** 후보 생성 시 뒤쪽을 먼저 평가해야 "나중에 그려진 것이 먼저"가 된다. SceneView 통합(Step 6)에서 후보를 draw 순서와 동일하게 push한 뒤 `stable_sort` 전에 `std::reverse`로 뒤집어 동률에서 앞면 우선을 만든다.

- [ ] **Step 6: SceneViewWindow에 좌클릭 픽킹 연결**

`src/Editor/Windows/SceneViewWindow.h` private에 추가:
```cpp
    // 현재 에디터 카메라 상태를 ViewportMath 구조로 변환
    molga::ViewportCamera ViewportCam() const;
    // 좌클릭 픽킹: 패널 좌표 클릭 → 후보 수집 → SelectionService
    void HandlePick(ImVec2 panelPos, ImVec2 panelSize);
```
include 추가: `#include "Editor/ViewportMath.h"`, `#include "Editor/ScenePicker.h"`.

`src/Editor/Windows/SceneViewWindow.cpp`:
- `ScreenToWorld`(현재 `:486-501`)를 `ViewportMath::ScreenToWorld` 위임으로 교체(중복 공식 제거):
```cpp
void SceneViewWindow::ScreenToWorld(ImVec2 panelPos, ImVec2 panelSize, ImVec2 screen,
                                    float& outX, float& outY) const {
    molga::ScreenToWorld(ViewportCam(), panelSize.x, panelSize.y,
                         screen.x - panelPos.x, screen.y - panelPos.y, outX, outY);
}

molga::ViewportCamera SceneViewWindow::ViewportCam() const {
    return { editorCamera_->GetX(), editorCamera_->GetY(), editorCamera_->GetZoom() };
}
```
- 좌클릭 픽킹 핸들러 추가(후보는 `SpriteRenderer`의 world center/half-size로 구성; draw 순서대로 push 후 reverse):
```cpp
void SceneViewWindow::HandlePick(ImVec2 panelPos, ImVec2 panelSize) {
    if (!gameObjects_) return;
    ImVec2 m = ImGui::GetMousePos();
    float wx, wy;
    molga::ScreenToWorld(ViewportCam(), panelSize.x, panelSize.y,
                         m.x - panelPos.x, m.y - panelPos.y, wx, wy);

    std::vector<molga::PickCandidate> cands;
    for (auto& obj : *gameObjects_) {
        if (!obj || !obj->IsActive()) continue;
        auto* tr = obj->GetComponent<Transform>();
        auto* sr = obj->GetComponent<SpriteRenderer>();
        if (!tr || !sr) continue;
        Vector2 wp = tr->GetWorldPosition();
        cands.push_back({ obj->GetID(), wp.x, wp.y,
                          sr->GetWidth() * 0.5f, sr->GetHeight() * 0.5f,
                          sr->GetSortingOrder() });
    }
    std::reverse(cands.begin(), cands.end());   // 동률에서 뒤에 그려진(위) 면 우선
    auto hits = molga::PickAt(cands, wx, wy);

    auto& sel = Editor::Get().GetSelection();
    if (hits.empty()) sel.Clear(molga::SelectionSource::SceneView);
    else              sel.Select(hits.front(), molga::SelectionSource::SceneView);
}
```
- `OnGUI`의 입력 처리부(현재 `HandleInput` 호출 `:174` 직후, 우클릭 컨텍스트 트리거 `:177` 전)에 좌클릭 픽킹 호출 추가:
```cpp
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        HandlePick(panelPos, ImVec2(vpW, vpH));
    }
```
> Task D에서 Gizmo가 들어오면, 이 좌클릭 픽킹은 "gizmo 핸들을 잡지 않았을 때"만 실행되도록 가드한다(아래 Task D Step 5).

- [ ] **Step 7: 빌드 + 단위 테스트 통과**

Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_viewport_math test_scene_picker molga_engine -j4
ctest --preset debug -R "test_viewport_math|test_scene_picker" --output-on-failure
```
Expected: 두 테스트 PASS, `molga_engine` 빌드 성공.

- [ ] **Step 8: 커밋**

```bash
git add src/Editor/ViewportMath.h src/Editor/ScenePicker.h \
        src/Editor/Windows/SceneViewWindow.h src/Editor/Windows/SceneViewWindow.cpp \
        tests/test_viewport_math.cpp tests/test_scene_picker.cpp tests/CMakeLists.txt
git commit -m "feat(editor): scene view picking via ViewportMath + ScenePicker into SelectionService"
```

---

## Task C. TransformCommand + Inspector 라우팅 (TDD)

> Gizmo와 Inspector가 공유할 단일 변형 command. `TransformCommand`는 (목표 GameObject ID, before/after Transform 값)을 보관하고 `Execute/Undo`에서 `World::FindById`로 대상을 찾아 값을 쓴 뒤 dirty를 갱신한다. 값 보관 구조 `TransformState`는 `molga_core`에 두어 단위 테스트하고, command 자체는 Editor singleton을 쓰므로 `EDITOR_SOURCES`에 둔다. 테스트는 Editor 대신 `World*`를 주입받는 생성자로 검증한다(에디터 GL/ImGui 없이).

**Files:**
- Create: `src/Editor/Commands/TransformCommand.h`, `src/Editor/Commands/TransformCommand.cpp`
- Create: `tests/test_transform_command.cpp`
- Modify: `src/Editor/Editor.h/.cpp`(transform-edit 라우팅 헬퍼), `src/ECS/Components/Transform.cpp`, `CMakeLists.txt`(EDITOR_SOURCES), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패 테스트 작성** — `tests/test_transform_command.cpp` (World 주입 생성자 사용)

```cpp
#include "Editor/Commands/TransformCommand.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "doctest.h"
#include <memory>

using molga::TransformCommand;
using molga::TransformState;

namespace {
GameObject* SpawnAt(World& w, const char* name, float x, float y) {
    auto go = std::make_shared<GameObject>(name);
    go->AddComponent<Transform>()->SetPosition(x, y);
    return w.Add(go);
}
}

TEST_CASE("TransformCommand applies after-state and undo restores before-state") {
    World w;
    GameObject* go = SpawnAt(w, "Obj", 0.f, 0.f);
    TransformState before{ {0.f, 0.f}, 0.f, {1.f, 1.f} };
    TransformState after { {10.f, 5.f}, 90.f, {2.f, 2.f} };

    TransformCommand cmd(&w, go->GetID(), before, after);
    cmd.Execute();
    auto* tr = go->GetComponent<Transform>();
    CHECK(tr->GetPosition().x == doctest::Approx(10.f));
    CHECK(tr->GetRotation()   == doctest::Approx(90.f));
    CHECK(tr->GetScale().y    == doctest::Approx(2.f));

    cmd.Undo();
    CHECK(tr->GetPosition().x == doctest::Approx(0.f));
    CHECK(tr->GetRotation()   == doctest::Approx(0.f));
    CHECK(tr->GetScale().y    == doctest::Approx(1.f));
}

TEST_CASE("TransformCommand on a missing id is a safe no-op") {
    World w;
    TransformState s{ {0.f, 0.f}, 0.f, {1.f, 1.f} };
    TransformCommand cmd(&w, 9999u, s, s);
    cmd.Execute();   // 대상 없음 — 크래시 없이 통과
    cmd.Undo();
    CHECK(cmd.Name() == "Transform");
}

TEST_CASE("Capture reads current transform into a TransformState") {
    World w;
    GameObject* go = SpawnAt(w, "Obj", 3.f, 4.f);
    go->GetComponent<Transform>()->SetRotation(45.f);
    TransformState s = TransformCommand::Capture(go->GetComponent<Transform>());
    CHECK(s.position.x == doctest::Approx(3.f));
    CHECK(s.rotation   == doctest::Approx(45.f));
}
```

- [ ] **Step 2: 등록(Editor 소스 링크) + 실패 확인** — `tests/CMakeLists.txt`

```cmake
molga_add_test(test_transform_command test_transform_command.cpp)
target_sources(test_transform_command PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/TransformCommand.cpp
)
target_include_directories(test_transform_command PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```

Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_transform_command -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: TransformCommand 작성** — `src/Editor/Commands/TransformCommand.h`

```cpp
#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "Common/Types.h"
#include <string>

class World;
class Transform;

namespace molga {

// 한 Transform의 스냅샷(position/rotation(deg)/scale).
struct TransformState {
    Vector2 position;
    float   rotation;
    Vector2 scale;
};

// 단일 GameObject의 transform 변경을 before/after 스냅샷으로 기록하는 command.
// World*를 주입받아(테스트) 또는 nullptr이면 Editor의 활성 World를 사용(에디터).
class TransformCommand : public ICommand {
public:
    TransformCommand(World* world, unsigned int targetId,
                     const TransformState& before, const TransformState& after);

    void Execute() override;   // after 적용
    void Undo() override;      // before 복원
    std::string Name() const override { return "Transform"; }

    static TransformState Capture(const Transform* tr);

private:
    void ApplyTo(const TransformState& s);
    Transform* Resolve() const;

    World* world_;             // nullptr = Editor::Get().ActiveWorld 사용
    unsigned int targetId_;
    TransformState before_;
    TransformState after_;
};

} // namespace molga
```

`src/Editor/Commands/TransformCommand.cpp`:
```cpp
#include "Editor/Commands/TransformCommand.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"

// Editor 의존은 약하게: world_ == nullptr일 때만 사용한다.
#include "Editor/Editor.h"

namespace molga {

TransformCommand::TransformCommand(World* world, unsigned int targetId,
                                   const TransformState& before, const TransformState& after)
    : world_(world), targetId_(targetId), before_(before), after_(after) {}

TransformState TransformCommand::Capture(const Transform* tr) {
    return { tr->GetPosition(), tr->GetRotation(), tr->GetScale() };
}

Transform* TransformCommand::Resolve() const {
    GameObject* go = nullptr;
    if (world_) {
        go = world_->FindById(targetId_);
    } else {
        go = Editor::Get().FindObjectById(targetId_);
    }
    return go ? go->GetComponent<Transform>() : nullptr;
}

void TransformCommand::ApplyTo(const TransformState& s) {
    if (Transform* tr = Resolve()) {
        tr->SetPosition(s.position);
        tr->SetRotation(s.rotation);
        tr->SetScale(s.scale);
        if (!world_) Editor::Get().MarkSceneModified();
    }
}

void TransformCommand::Execute() { ApplyTo(after_); }
void TransformCommand::Undo()    { ApplyTo(before_); }

} // namespace molga
```

- [ ] **Step 4: EDITOR_SOURCES 등록 + 테스트 통과**

`CMakeLists.txt`의 `set(EDITOR_SOURCES ...)`에 추가:
```cmake
    src/Editor/Commands/TransformCommand.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_transform_command molga_engine -j4
ctest --preset debug -R test_transform_command --output-on-failure
```
Expected: PASS, `3 | 3 passed`, `molga_engine` 빌드 성공.

- [ ] **Step 5: Editor에 transform-edit 라우팅 헬퍼 추가**

`src/Editor/Editor.h` public에 추가:
```cpp
    molga::SelectionService& GetSelection();
    World& ActiveWorld();   // gameObjects가 가리키는 World (선택 ID 해석용)
    // Inspector/Gizmo 공통 진입점: target의 transform을 after로 바꾸는 command를 push.
    void SubmitTransformEdit(unsigned int targetId,
                             const molga::TransformState& before,
                             const molga::TransformState& after);
```
`src/Editor/Editor.cpp`에 구현 추가:
```cpp
#include "Editor/Commands/TransformCommand.h"

void Editor::SubmitTransformEdit(unsigned int targetId,
                                 const molga::TransformState& before,
                                 const molga::TransformState& after) {
    commandHistory.Execute(
        std::make_unique<molga::TransformCommand>(nullptr, targetId, before, after));
}
```
> `ActiveWorld()`는 `gameObjects` 포인터가 가리키는 vector를 소유한 World가 필요하다. main이 `SetGameObjects(&world.Objects())`로 주입하므로, Editor가 현재 World 포인터를 함께 보관하도록 `SetGameObjects`에 World* 인자 오버로드를 추가하거나, 선택 해석에는 `FindObjectById`(이미 `gameObjects` 벡터를 순회, `Editor.cpp:449` 인근)를 그대로 쓴다. 본 milestone은 후자(기존 `FindObjectById`)를 사용해 World* 보관 변경을 피한다.

- [ ] **Step 6: Inspector Transform 편집을 command로 라우팅**

`src/ECS/Components/Transform.cpp`의 `OnInspectorGUI`(현재 `:86-103`)를 교체. Component가 Panel/Editor를 직접 부르지 않도록(계층 경계), Inspector가 Transform 편집을 감지해 command를 만들도록 **InspectorWindow로 옮긴다**:

`src/ECS/Components/Transform.cpp`의 `OnInspectorGUI`(현재 `:86-103`)를 교체한다. 핵심 규칙: 각 ImGui 위젯의 `IsItemActivated()`/`IsItemDeactivatedAfterEdit()`는 **그 위젯을 그린 직후 같은 줄에서** 검사해야 한다(둘은 "마지막으로 제출된 item"을 가리킨다). drag 동안에는 값을 직접 바꿔 live preview를 주되, command는 위젯이 비활성화되는 순간 before→after로 **한 번만** push한다.

owner ID는 `GetGameObject()->GetID()`로 얻는다(`Component::GetGameObject()`, `src/ECS/Component.h:68`). before 스냅샷은 위젯이 활성화되는 순간 잡는다.

`#ifdef MOLGA_EDITOR` 블록 안에서만 `#include "Editor/Editor.h"`와 `#include "Editor/Commands/TransformCommand.h"`를 include해, Runtime 빌드(`MOLGA_EDITOR` 미정의)가 Editor를 include하지 않도록 한다(계층 경계 준수).

```cpp
void Transform::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    static molga::TransformState before;   // 활성화 순간의 스냅샷
    unsigned int ownerId = gameObject ? gameObject->GetID() : 0u;

    float pos[2] = { position.x, position.y };
    if (ImGui::DragFloat2("Position", pos, 0.5f)) { position.x = pos[0]; position.y = pos[1]; }
    if (ImGui::IsItemActivated())           before = molga::TransformCommand::Capture(this);
    if (ImGui::IsItemDeactivatedAfterEdit())
        Editor::Get().SubmitTransformEdit(ownerId, before, molga::TransformCommand::Capture(this));

    float rot = rotation;
    if (ImGui::DragFloat("Rotation", &rot, 0.5f)) { rotation = rot; }
    if (ImGui::IsItemActivated())           before = molga::TransformCommand::Capture(this);
    if (ImGui::IsItemDeactivatedAfterEdit())
        Editor::Get().SubmitTransformEdit(ownerId, before, molga::TransformCommand::Capture(this));

    float scaleArr[2] = { scale.x, scale.y };
    if (ImGui::DragFloat2("Scale", scaleArr, 0.01f)) { scale.x = scaleArr[0]; scale.y = scaleArr[1]; }
    if (ImGui::IsItemActivated())           before = molga::TransformCommand::Capture(this);
    if (ImGui::IsItemDeactivatedAfterEdit())
        Editor::Get().SubmitTransformEdit(ownerId, before, molga::TransformCommand::Capture(this));
#endif
}
```

> drag/입력 동안 값은 미리보기로 직접 바뀌되, **단일 command는 위젯이 비활성화되는 `IsItemDeactivatedAfterEdit` 시점에 before→after 한 번만 push**된다. 이로써 "Inspector 숫자 편집 = 1 undo step"이 Gizmo와 동일해진다. `before`는 위젯이 활성화되는 첫 프레임에 잡히고, 그 사이 live preview로 바뀐 현재 값이 commit 시 after가 된다.

- [ ] **Step 7: 빌드 + 전체 테스트**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 빌드 성공, 모든 테스트 PASS.

수동 검증: 오브젝트 선택 → Inspector에서 Position을 드래그로 바꾸고 손을 떼면 1번의 Undo로 원위치되는지, 메뉴 바 `*unsaved`가 뜨는지 확인.

- [ ] **Step 8: 커밋**

```bash
git add src/Editor/Commands/TransformCommand.h src/Editor/Commands/TransformCommand.cpp \
        src/Editor/Editor.h src/Editor/Editor.cpp src/ECS/Components/Transform.cpp \
        tests/test_transform_command.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(editor): TransformCommand + inspector transform edits routed as one undo step"
```

---

## Task D. TransformGizmo + Snap (TDD)

> 2D gizmo: tool(select/move/rotate/scale), space(local/world), snap(off/grid/increment). drag 수명 = begin에서 `TransformState` 스냅샷 → drag 동안 live preview(Transform 직접 미리보기) → release에서 before→after 한 번을 `SubmitTransformEdit`로 commit. 핸들 hit·snap·delta 적용 같은 **순수 수학**은 `TransformGizmoMath.h`(molga_core)로 분리해 doctest로 검증하고, ImGui 드로잉/마우스 수명만 `TransformGizmo`(EDITOR_SOURCES)에 둔다.

**Files:**
- Create: `src/Editor/Gizmos/TransformGizmoMath.h`
- Create: `src/Editor/Gizmos/TransformGizmo.h`, `src/Editor/Gizmos/TransformGizmo.cpp`
- Create: `tests/test_transform_gizmo_math.cpp`
- Modify: `src/Editor/Windows/SceneViewWindow.{h,cpp}`, `CMakeLists.txt`(EDITOR_SOURCES), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패 테스트 작성** — `tests/test_transform_gizmo_math.cpp`

```cpp
#include "Editor/Gizmos/TransformGizmoMath.h"
#include "doctest.h"

using molga::GizmoTool;
using molga::GizmoAxis;
using molga::SnapMode;
using molga::SnapValue;
using molga::PickAxis;
using molga::ApplyMoveDelta;

TEST_CASE("SnapValue off returns the raw value") {
    CHECK(SnapValue(7.3f, SnapMode::Off, 1.0f) == doctest::Approx(7.3f));
}

TEST_CASE("SnapValue grid rounds to nearest multiple of step") {
    CHECK(SnapValue(7.3f,  SnapMode::Grid, 5.f) == doctest::Approx(5.f));
    CHECK(SnapValue(8.0f,  SnapMode::Grid, 5.f) == doctest::Approx(10.f));
    CHECK(SnapValue(-2.4f, SnapMode::Grid, 5.f) == doctest::Approx(0.f));
}

TEST_CASE("PickAxis returns X handle when the cursor is along +X near origin") {
    // 핸들 길이 50, 두께 6, 카메라 zoom 1
    GizmoAxis a = PickAxis(/*originX*/0.f, /*originY*/0.f,
                           /*mouseX*/30.f, /*mouseY*/0.f,
                           /*handleLen*/50.f, /*thickness*/6.f);
    CHECK(a == GizmoAxis::X);
}

TEST_CASE("PickAxis returns None when far from both handles") {
    CHECK(PickAxis(0.f, 0.f, 200.f, 200.f, 50.f, 6.f) == GizmoAxis::None);
}

TEST_CASE("ApplyMoveDelta on X axis moves only X, grid-snapped") {
    float nx, ny;
    ApplyMoveDelta(GizmoAxis::X, /*startX*/0.f, /*startY*/0.f,
                   /*dragWorldDX*/7.f, /*dragWorldDY*/9.f,
                   SnapMode::Grid, /*step*/5.f, nx, ny);
    CHECK(nx == doctest::Approx(5.f));   // 7 → grid 5
    CHECK(ny == doctest::Approx(0.f));   // Y 고정
}

TEST_CASE("ApplyMoveDelta on Both axis moves freely with no snap") {
    float nx, ny;
    ApplyMoveDelta(GizmoAxis::Both, 1.f, 2.f, 3.f, 4.f, SnapMode::Off, 5.f, nx, ny);
    CHECK(nx == doctest::Approx(4.f));
    CHECK(ny == doctest::Approx(6.f));
}
```

- [ ] **Step 2: 등록 + 실패 확인** — `tests/CMakeLists.txt`

```cmake
molga_add_test(test_transform_gizmo_math test_transform_gizmo_math.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_transform_gizmo_math -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: TransformGizmoMath 작성** — `src/Editor/Gizmos/TransformGizmoMath.h`

```cpp
#pragma once

#include <cmath>

namespace molga {

enum class GizmoTool { Select, Move, Rotate, Scale };
enum class GizmoSpace { World, Local };
enum class GizmoAxis { None, X, Y, Both };
enum class SnapMode { Off, Grid, Increment };

// value를 snap 규칙에 따라 보정. Off는 원값, Grid/Increment는 step 배수로 반올림.
inline float SnapValue(float value, SnapMode mode, float step) {
    if (mode == SnapMode::Off || step <= 0.f) return value;
    return std::round(value / step) * step;
}

// gizmo 원점(스크린/world 동일 좌표계)에서 마우스가 어느 축 핸들 위인지.
// 중앙 정사각형(두께 영역)이면 Both, +X/+Y 막대 근처면 X/Y, 아니면 None.
inline GizmoAxis PickAxis(float originX, float originY, float mouseX, float mouseY,
                          float handleLen, float thickness) {
    float dx = mouseX - originX;
    float dy = mouseY - originY;
    if (std::fabs(dx) <= thickness && std::fabs(dy) <= thickness) return GizmoAxis::Both;
    if (dx >= 0.f && dx <= handleLen && std::fabs(dy) <= thickness) return GizmoAxis::X;
    if (dy >= 0.f && dy <= handleLen && std::fabs(dx) <= thickness) return GizmoAxis::Y;
    return GizmoAxis::None;
}

// 잡은 축에 따라 world 드래그 델타를 시작 위치에 적용해 새 위치(nx,ny)를 만든다(snap 포함).
inline void ApplyMoveDelta(GizmoAxis axis, float startX, float startY,
                           float dragWorldDX, float dragWorldDY,
                           SnapMode mode, float step, float& nx, float& ny) {
    float tx = startX + ((axis == GizmoAxis::Y) ? 0.f : dragWorldDX);
    float ty = startY + ((axis == GizmoAxis::X) ? 0.f : dragWorldDY);
    nx = SnapValue(tx, mode, step);
    ny = SnapValue(ty, mode, step);
}

} // namespace molga
```

- [ ] **Step 4: 빌드 + 단위 테스트 통과**

Run:
```bash
cmake --build --preset debug --target test_transform_gizmo_math -j4
ctest --preset debug -R test_transform_gizmo_math --output-on-failure
```
Expected: PASS, `6 | 6 passed`.

- [ ] **Step 5: TransformGizmo(드로잉 + drag 수명) 작성 + SceneView 통합**

`src/Editor/Gizmos/TransformGizmo.h`:
```cpp
#pragma once

#include "Editor/Gizmos/TransformGizmoMath.h"
#include "Editor/Commands/TransformCommand.h"
#include "Editor/ViewportMath.h"
#include <imgui.h>

class GameObject;

namespace molga {

// 선택된 단일 GameObject 위에 2D gizmo를 그리고 drag로 transform을 편집한다.
// drag 한 번 = TransformCommand 한 개(begin 스냅샷 → live preview → release commit).
class TransformGizmo {
public:
    void SetTool(GizmoTool t)   { tool_ = t; }
    GizmoTool Tool() const      { return tool_; }
    void SetSpace(GizmoSpace s) { space_ = s; }
    void SetSnap(SnapMode m, float step) { snapMode_ = m; snapStep_ = step; }

    bool IsDragging() const { return dragging_; }

    // target(없으면 무시)에 대해 gizmo를 ImGui drawlist로 그리고 입력을 처리한다.
    // 반환값: 이 프레임에 gizmo가 마우스를 소비했는가(좌클릭 픽킹 억제용).
    bool Draw(GameObject* target, const ViewportCamera& cam, ImVec2 panelPos, ImVec2 panelSize);

private:
    GizmoTool  tool_   = GizmoTool::Move;
    GizmoSpace space_  = GizmoSpace::World;
    SnapMode   snapMode_ = SnapMode::Off;
    float      snapStep_ = 32.f;

    bool dragging_ = false;
    GizmoAxis grabbedAxis_ = GizmoAxis::None;
    TransformState dragStart_;   // begin 스냅샷
    float startWorldX_ = 0.f, startWorldY_ = 0.f;  // drag 시작 시 target world pos
};

} // namespace molga
```

`src/Editor/Gizmos/TransformGizmo.cpp`(핵심 수명만; 수학은 GizmoMath 호출):
```cpp
#include "Editor/Gizmos/TransformGizmo.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"

namespace molga {

bool TransformGizmo::Draw(GameObject* target, const ViewportCamera& cam,
                          ImVec2 panelPos, ImVec2 panelSize) {
    if (!target || tool_ == GizmoTool::Select) { dragging_ = false; return false; }
    auto* tr = target->GetComponent<Transform>();
    if (!tr) return false;

    Vector2 wp = tr->GetWorldPosition();
    float ox, oy;
    WorldToScreen(cam, panelSize.x, panelSize.y, wp.x, wp.y, ox, oy);
    ox += panelPos.x; oy += panelPos.y;

    const float handleLen = 60.f, thickness = 7.f;
    // (drawlist로 +X(빨강)/+Y(초록) 막대 + 중앙 사각형 그리기 — 생략표기 없이 실제 구현)

    ImVec2 m = ImGui::GetMousePos();
    GizmoAxis hover = PickAxis(ox, oy, m.x, m.y, handleLen, thickness);

    if (!dragging_ && hover != GizmoAxis::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_ = true;
        grabbedAxis_ = hover;
        dragStart_ = TransformCommand::Capture(tr);
        startWorldX_ = tr->GetPosition().x;
        startWorldY_ = tr->GetPosition().y;
    }

    if (dragging_) {
        // 마우스의 world 위치(시작 대비 델타)를 계산해 live preview로 직접 적용
        ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float dWorldX = d.x / cam.zoom, dWorldY = d.y / cam.zoom;
        float nx, ny;
        ApplyMoveDelta(grabbedAxis_, startWorldX_, startWorldY_,
                       dWorldX, dWorldY, snapMode_, snapStep_, nx, ny);
        tr->SetPosition(nx, ny);               // live preview (command 아직 없음)

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            TransformState after = TransformCommand::Capture(tr);
            dragging_ = false;
            grabbedAxis_ = GizmoAxis::None;
            // 단일 command로 commit (before = dragStart_)
            Editor::Get().SubmitTransformEdit(target->GetID(), dragStart_, after);
        }
        return true;   // 마우스 소비 → 좌클릭 픽킹 억제
    }
    return hover != GizmoAxis::None;
}

} // namespace molga
```

`SceneViewWindow` 통합:
- `src/Editor/Windows/SceneViewWindow.h`에 `#include "Editor/Gizmos/TransformGizmo.h"` + 멤버 `molga::TransformGizmo gizmo_;` 추가.
- `OnGUI`에서 좌클릭 픽킹 호출(Task B Step 6) **앞에** gizmo를 그린다:
```cpp
    GameObject* primaryTarget =
        Editor::Get().FindObjectById(Editor::Get().GetSelection().PrimaryId());
    bool gizmoUsed = gizmo_.Draw(primaryTarget, ViewportCam(),
                                 panelPos, ImVec2(vpW, vpH));
    if (!gizmoUsed && ImGui::IsItemHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        HandlePick(panelPos, ImVec2(vpW, vpH));
    }
```
- Scene View 상단 오버레이/툴바에 tool(Q/W/E/R select/move/rotate/scale 단축키) + snap 토글 UI를 추가해 `gizmo_.SetTool/SetSnap`을 호출한다(rotate/scale 핸들 수학은 후속 — 이 milestone은 move 축을 완성하고 rotate/scale은 tool 전환·핸들 hit까지 둔다).

- [ ] **Step 6: EDITOR_SOURCES 등록 + 빌드 + 수동 검증**

`CMakeLists.txt`의 `set(EDITOR_SOURCES ...)`에 추가:
```cmake
    src/Editor/Gizmos/TransformGizmo.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 빌드 성공, 전체 테스트 PASS.

수동 검증: Sprite 선택 → Move gizmo의 X 핸들을 드래그 → 떼면 **한 번의** Undo로 원위치. snap을 Grid(32)로 켜고 드래그하면 32 배수에만 멈추는지 확인.

- [ ] **Step 7: 커밋**

```bash
git add src/Editor/Gizmos/TransformGizmoMath.h src/Editor/Gizmos/TransformGizmo.h \
        src/Editor/Gizmos/TransformGizmo.cpp src/Editor/Windows/SceneViewWindow.h \
        src/Editor/Windows/SceneViewWindow.cpp \
        tests/test_transform_gizmo_math.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(editor): 2D transform gizmo with snap; drag commits one TransformCommand"
```

---

## Task E. 선택 Outline

> 선택된 오브젝트의 world AABB를 Scene View 위에 사각형으로 강조한다. 별도 GL pass 없이 ImGui drawlist로 그린다(Phase 1 수준; SelectionPass는 후속). `WorldToScreen`으로 4모서리를 변환한다.

**Files:**
- Modify: `src/Editor/Windows/SceneViewWindow.{h,cpp}`

- [ ] **Step 1: outline 드로잉 추가**

`src/Editor/Windows/SceneViewWindow.cpp`의 `OnGUI`에서 gizmo를 그리기 직전(스프라이트 위, FBO Image 위)에 선택 outline을 그린다:
```cpp
void SceneViewWindow::DrawSelectionOutline(ImVec2 panelPos, ImVec2 panelSize) {
    if (!gameObjects_) return;
    auto& sel = Editor::Get().GetSelection();
    if (!sel.HasSelection()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (unsigned int id : sel.SelectedIds()) {
        GameObject* go = Editor::Get().FindObjectById(id);
        if (!go) continue;
        auto* tr = go->GetComponent<Transform>();
        auto* sr = go->GetComponent<SpriteRenderer>();
        if (!tr || !sr) continue;
        Vector2 wp = tr->GetWorldPosition();
        float hw = sr->GetWidth() * 0.5f, hh = sr->GetHeight() * 0.5f;
        float sx0, sy0, sx1, sy1;
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             wp.x - hw, wp.y - hh, sx0, sy0);
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             wp.x + hw, wp.y + hh, sx1, sy1);
        bool primary = (id == sel.PrimaryId());
        dl->AddRect(ImVec2(panelPos.x + sx0, panelPos.y + sy0),
                    ImVec2(panelPos.x + sx1, panelPos.y + sy1),
                    primary ? IM_COL32(255, 170, 0, 255) : IM_COL32(255, 170, 0, 140),
                    0.f, 0, primary ? 2.f : 1.f);
    }
}
```
헤더에 `void DrawSelectionOutline(ImVec2 panelPos, ImVec2 panelSize);` 선언 + `OnGUI`에서 호출.

- [ ] **Step 2: 빌드 + 수동 검증**

Run:
```bash
cmake --build --preset debug --target molga_engine -j4
```
Expected: 빌드 성공. 수동: Sprite를 Hierarchy/Scene View 어디서 선택해도 같은 주황 outline이 뜨고, primary가 더 굵게 표시되는지 확인.

- [ ] **Step 3: 커밋**

```bash
git add src/Editor/Windows/SceneViewWindow.h src/Editor/Windows/SceneViewWindow.cpp
git commit -m "feat(editor): selection outline in scene view from SelectionService"
```

---

## Task F. 선택 소유권 이전 + Play/Stop 선택 생존

> Hierarchy/Inspector가 더 이상 선택을 따로 소유하지 않고 전부 `SelectionService`를 읽는다. `Editor::Get/SetSelectedObject`를 SelectionService 위임으로 바꾸고, 선택 변경 이벤트로 Inspector target을 동기화한다. Play/Stop 콜백에서 선택을 비우는 대신 primary ID를 보존해 World 교체 후 `Rebind`로 재해석한다.

**Files:**
- Modify: `src/Editor/Editor.h/.cpp`, `src/Editor/Windows/HierarchyWindow.h/.cpp`, `src/Editor/Windows/InspectorWindow.h/.cpp`, `src/main.cpp`

- [ ] **Step 1: Editor를 SelectionService 위임으로 전환**

`src/Editor/Editor.h`에 `#include "Editor/Selection/SelectionService.h"` + private 멤버 `molga::SelectionService selection_;` 추가, public에 `molga::SelectionService& GetSelection() { return selection_; }`.

`src/Editor/Editor.cpp`:
- `Editor::Init()`에서 selection 변경 리스너 등록 — 선택이 바뀌면 Inspector target을 갱신:
```cpp
    selection_.AddListener([this](const molga::SelectionService& s, molga::SelectionSource) {
        if (auto* insp = windowManager.GetAs<InspectorWindow>(EditorConstants::WIN_INSPECTOR)) {
            insp->SetTarget(FindObjectById(s.InspectorTargetId()));
        }
    });
```
- `GetSelectedObject()`(현재 `:309-312`)를 `return FindObjectById(selection_.PrimaryId());`로 교체.
- `SetSelectedObject(GameObject* obj)`(현재 `:314-323`)를 `selection_.Select(obj ? obj->GetID() : 0u, molga::SelectionSource::Code);`로 교체(리스너가 Inspector를 갱신).

- [ ] **Step 2: Hierarchy/Inspector를 SelectionService 읽기로 전환**

`src/Editor/Windows/HierarchyWindow.h`: `GameObject* selectedObject` 멤버와 인라인 getter/setter를 제거하고, 표시용으로는 `Editor::Get().GetSelection()`을 읽는다. 클릭(`HierarchyWindow.cpp:160-163`, `:176`, `:205`)을 다음으로 교체:
```cpp
    if (ImGui::IsItemClicked()) {
        Editor::Get().GetSelection().Select(obj->GetID(), molga::SelectionSource::Hierarchy);
    }
```
선택 강조 표시는 `Editor::Get().GetSelection().IsSelected(obj->GetID())`로 판정한다. command 후 `selectedObject = Editor::Get().GetSelectedObject();` 패턴(`:223,234,244,251,259`)은 제거하고 `Editor::Get().GetSelection()`을 직접 읽도록 정리한다.

`src/Editor/Windows/InspectorWindow`: `SetTarget`은 그대로 두되(리스너가 호출), 잠금(lock) 체크박스를 추가해 `Editor::Get().GetSelection().LockInspector(target->GetID())`/`UnlockInspector()`를 토글한다.

- [ ] **Step 3: Play/Stop 선택 생존** — `src/main.cpp:150-163` 콜백 교체

```cpp
    editorState.SetPlayCallbacks(
        [&sceneDoc]() {  // Edit → Play
            Editor::Get().GetCommandHistory().Clear();
            sceneDoc.EnterPlay();
            sceneDoc.ActiveWorld().ResolveAssets();
            Editor::Get().SetGameObjects(&sceneDoc.ActiveWorld().Objects());
            // 선택 ID는 보존: play world에 같은 ID가 있으면 유지, 없으면 떨군다.
            World& pw = sceneDoc.ActiveWorld();
            Editor::Get().GetSelection().Rebind(
                [&pw](unsigned int id) { return pw.FindById(id) != nullptr; });
        },
        [&sceneDoc]() {  // Play/Pause → Stop
            Editor::Get().GetCommandHistory().Clear();
            Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
            sceneDoc.ExitPlay();
            World& ew = sceneDoc.EditWorld();
            Editor::Get().GetSelection().Rebind(
                [&ew](unsigned int id) { return ew.FindById(id) != nullptr; });
        });
```
> `SetGameObjects` 후 `Rebind`를 호출하면, 리스너가 Inspector target을 새 World의 동일 ID 오브젝트로 다시 가리킨다. EditWorld 오브젝트가 그대로 살아 있으므로 Stop 후 선택이 복원된다.

- [ ] **Step 4: 빌드 + 전체 테스트 + 수동 검증**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 빌드 성공, 전체 테스트 PASS.

수동 검증: Sprite 선택 → Play → (편집 월드 동일 ID 객체가 play world에 있으므로) 선택/outline 유지 → Stop → 선택 복원. Inspector lock 후 다른 오브젝트를 선택해도 Inspector가 잠근 대상을 계속 보여주는지 확인.

- [ ] **Step 5: 커밋**

```bash
git add src/Editor/Editor.h src/Editor/Editor.cpp \
        src/Editor/Windows/HierarchyWindow.h src/Editor/Windows/HierarchyWindow.cpp \
        src/Editor/Windows/InspectorWindow.h src/Editor/Windows/InspectorWindow.cpp \
        src/main.cpp
git commit -m "refactor(editor): move selection ownership to SelectionService; survive Play/Stop"
```

---

## 완료 기준

- [ ] Scene View에서 Sprite를 클릭하면 선택되고 Hierarchy 강조와 Inspector 대상이 동시에 갱신된다(`SelectionSource` 한 모델에서 읽음).
- [ ] 겹친 Sprite를 클릭하면 가장 앞(sortingOrder 큰) Sprite가 선택된다(`ScenePicker`/`test_scene_picker`).
- [ ] Gizmo 핸들을 드래그하면 Undo 항목이 **프레임당이 아니라 드래그당 1개**다(`TransformGizmo`가 release에서 단일 `TransformCommand` commit).
- [ ] Inspector 숫자 편집과 Gizmo 편집이 동일한 command 의미(`TransformCommand`, before→after 1 step)를 공유한다.
- [ ] 편집 월드 오브젝트가 살아 있으면 Play/Stop 후에도 선택이 유지된다(`SelectionService::Rebind` + ID 보존).
- [ ] 다중 선택을 패널 API 재변경 없이 도입할 수 있다(`SelectedIds()` 집합 API가 이미 다중을 표현).
- [ ] **Exit 시나리오:** 사용자가 Scene View에서 Sprite를 클릭해 선택 → Gizmo로 이동 → Inspector에서 position 편집 → 두 변경을 각각 Undo → 각각 Redo → 저장 시 dirty(`*unsaved`)가 사라진다.
- [ ] 전체 테스트(`test_selection_service`, `test_viewport_math`, `test_scene_picker`, `test_transform_command`, `test_transform_gizmo_math`)가 Debug와 asan에서 통과한다.

---

## 의존성 / 순서

- **이 milestone은 토대다.** 권장 작업 순서: Task A(SelectionService) → Task B(픽킹) → Task C(TransformCommand/Inspector) → Task D(Gizmo) → Task E(outline) → Task F(소유권 이전 + Play/Stop 생존). A는 모든 후속의 전제이고, F는 A~E가 selection을 읽는 형태로 정리된 뒤 마지막에 소유권을 완전히 이전한다.
- **선행:** Phase 0 산출물(`SceneDocument`/`World`/`CommandHistory`/`ObjectCommands`)이 이미 존재함을 전제한다(현재 상태에서 검증됨).
- **후속 문서가 의존하는 계약:**
  - `03_ux2…`(Console/Task): 로그 소스 네비게이션이 `SelectionService`로 오브젝트를 선택할 수 있어야 한다.
  - `04_ux3…`(Asset Identity & safe Project Browser): texture→Sprite 드롭이 생성한 오브젝트를 `SelectionService`로 선택하고, 자산 rename이 selection을 깨지 않아야 한다.
  - `07_ux6…`(Advanced Production): 다중 선택 편집·component copy/paste/reset·prefab isolation이 `SelectionService`의 `SelectedIds()` 집합과 command/dirty 계약 위에서 동작한다.
  - 모든 후속 편집은 **CommandHistory를 우회하지 않는다**는 계약(`phase-1-3_roadmap.md` §2.1)을 이 milestone이 transform 편집에 대해 확립한다.
