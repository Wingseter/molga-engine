# UX-6: Advanced Production UX (고급 제작 UX)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 이 문서는 UX 로드맵의 **마지막 milestone**이며, **UX-1(SelectionService/transform·inspector commands), UX-2(structured Log + Console), UX-3(asset identity/GUID)이 모두 안정된 뒤에** 시작한다. 그 토대가 없으면 다중 선택·override·command palette가 같은 selection/undo/asset 결함을 또 반복한다(§10 비목표). 체크박스(`- [ ]`)로 추적한다.

**Goal:** Inspector를 "필드를 직접 mutate 하는 패널"에서 "command·adapter·tracker가 받쳐주는 상용 엔진형 authoring 표면"으로 끌어올린다. 컴포넌트 필드 편집/되돌리기/복사/붙여넣기/reset/remove, 다중 선택의 shared/mixed-value 편집, lock 가능한 Inspector, property 단위 prefab override 편집·되돌리기, inspector 검색/필터, 그리고 이를 메뉴·툴바와 동일한 command로 호출하는 Command Palette·단축키·preference·layout backbone을 제공한다.

**Architecture:** §6/§8의 분리 계약을 그대로 따른다 — `ComponentEditor`는 **그리기만** 하고, `PropertyAdapter`는 직렬화 가능한 필드를 **읽고/쓰며**, `EditorCommand`(`ComponentAdd/Remove/Reset/PasteCommand`, `SetPropertyCommand`)가 값 변경을 **commit** 한다. Inspector는 context menu와 공통 컴포넌트 연산을 소유하고, `PrefabOverrideTracker`가 바뀐 값을 표시한다. 사용자가 수행하는 모든 편집은 `CommandHistory`를 통과한다(roadmap §2.1 금지 규칙: "사용자 편집이 CommandHistory를 우회하지 않는다"). 프로젝트 데이터(`ProjectSettings`)와 머신/유저 데이터(`EditorPreferences`)는 별도 파일·별도 타입으로 유지한다. 단축키(`EditorShortcutRegistry`)·Command Palette는 메뉴/툴바와 **동일한 command 객체**를 실행한다.

**Tech Stack:** C++17, doctest, ImGui

**닫는 결함:** §6(Inspector/Authoring UX: 컴포넌트 UI가 mutation을 소유 → undo/dirty 불일치, context menu·mixed-value·검색·lock·property override 부재), §2(ComponentAdd/Remove/Reset/PasteCommand·ProjectSettingsEditCommand·BuildProfileEditCommand 부재), §8(EditorPreferences vs ProjectSettings 미분리, 단축키가 메뉴 label에 그침, layout 영구 저장/복구 불명확, CommandPalette 부재). *애니메이션/타일맵 authoring 패널은 milestone "Includes"에 있으나 **가장 큰 항목**이므로 backbone 뒤로 gated stretch(Task H)로 미룬다(§10 비목표: selection/undo/console/asset identity가 안정되기 전 advanced animation editor 금지).*

---

## 현재 상태 (검증된 사실)

- **컴포넌트는 Inspector에서 직접 mutate 된다.** `Transform::OnInspectorGUI()`는 `ImGui::DragFloat2` 결과를 곧바로 `SetPosition/SetRotation/SetScale`로 적용한다 — command·dirty·undo를 전부 우회한다(`src/ECS/Components/Transform.cpp:86-103`). 다른 컴포넌트도 같은 패턴(`OnInspectorGUI` override, `Component::OnInspectorGUI`는 기본 no-op, `src/ECS/Component.h:57`).
- **Add Component는 직접 호출이다.** `InspectorWindow::OnGUI`의 "AddComponentPopup"이 `target->AddComponent<SpriteRenderer>()` 등을 바로 부른다(`src/Editor/Windows/InspectorWindow.cpp:193-265`). 스크립트는 `ScriptManager::Get().CreateScript(...)` 후 `target->AddComponentRaw(script.release())`(`:253-257`).
- **컴포넌트 remove/reset/copy/paste/move up·down은 존재하지 않는다.** Inspector에 context menu가 없고, 컴포넌트 헤더는 enable 체크박스만 가진다(`InspectorWindow::DrawComponent`, `src/Editor/Windows/InspectorWindow.cpp:363-415`). `GameObject`는 `RemoveComponent<T>()`(템플릿, 타입명 기반 아님)와 `AddComponentRaw`만 제공(`src/ECS/GameObject.h:112-127`).
- **컴포넌트는 JSON으로 직렬화된다.** `Component::Serialize/Deserialize`(가상, 기본 no-op: `src/ECS/Component.cpp:4-10`)를 각 컴포넌트가 override(예: `Transform::Serialize`는 `position/rotation/scale` 기록, `src/ECS/Components/Transform.cpp:68-84`). → copy/paste/reset의 토대가 이미 있다.
- **타입명으로 컴포넌트를 생성할 수 있다.** `ComponentFactory::Get().Create(typeName, gameObject)`와 `HasType(typeName)`이 있고, `REGISTER_COMPONENT` 매크로로 모든 컴포넌트가 자동 등록된다(`src/ECS/ComponentFactory.h:32-54`). `Component::GetTypeName()`이 런타임 타입명을 준다(`src/ECS/Component.h:49`).
- **선택은 단일 객체이고 HierarchyWindow가 소유한다.** `Editor::GetSelectedObject()`는 `hierarchy->GetSelectedObject()`를 읽고, `Editor::SetSelectedObject(obj)`는 hierarchy와 `inspector->SetTarget(obj)`를 갱신한다(`src/Editor/Editor.cpp:309-321`). **다중 선택 모델·SelectionService는 없다**(UX-1에서 도입 예정). `InspectorWindow`는 `GameObject* target` 하나만 가진다(`src/Editor/Windows/InspectorWindow.h:15-21`).
- **prefab override는 보이지만 property 단위 편집/되돌리기가 없다.** Inspector는 `PrefabInstance::GetModifications()`(JSON array)를 읽어 헤더에서 `BulletText("%s: %s", component, key)`로 나열하고, 컴포넌트 헤더에 `*` 표시만 한다(`src/Editor/Windows/InspectorWindow.cpp:154-167, 368-396`). Apply/Revert/Unpack은 **객체 전체** command다(`molga::ApplyPrefabCommand/RevertPrefabCommand/UnpackPrefabCommand`, `src/Editor/Commands/PrefabCommands.h:40-77`). modification entry 모양은 `{target, component, key, value}`(`src/Core/PrefabUtil.cpp:21, 116-144`).
- **inspector 검색/필터·lock target은 없다.** `InspectorWindow`에 검색 버퍼·lock 플래그가 없다(`src/Editor/Windows/InspectorWindow.h`).
- **단축키는 menu label 뿐이다.** `"Ctrl+Z"` 등은 `ImGui::MenuItem`의 표시용 문자열일 뿐, 실제 키 처리(`ImGui::IsKeyPressed`)는 Escape·F·이름변경 같은 국소적 곳에만 있다(`src/Editor/Editor.cpp:170-173`, `src/Editor/Windows/SceneViewWindow.cpp:338`). 중앙 단축키 바인딩 레지스트리 **전무**.
- **layout 저장/복구는 "reset"만 있다.** "Reset Layout"이 `firstTimeLayout = true`로 기본 dock 레이아웃을 다시 만든다(`src/Editor/Editor.cpp:209-211`, `SetupDefaultLayout` `:107-127`). 영구 save/restore 서비스·복구(corrupt fallback)는 없다.
- **ProjectSettings는 프로젝트 데이터다.** tags/layerNames/collisionMatrix/sortingLayers를 가지며 `ProjectSettings/project_settings.json`에 저장된다(`src/Core/ProjectSettings.h:8-42`, `src/Editor/Project.cpp:52-54, 93-95`). **EditorPreferences(머신/유저 데이터: snap/외부 에디터/console 정책 등)는 타입조차 없다.**
- **CommandHistory·EditorCommand 인프라는 갖춰져 있다.** `molga::CommandHistory`(헤더 온리 undo/redo 스택, `Execute/Undo/Redo/CanUndo/CanRedo`, `src/Editor/Commands/CommandHistory.h`), `molga::ICommand`(`Execute/Undo/Name`, `src/Editor/Commands/EditorCommand.h`), `Editor::GetCommandHistory()`(`src/Editor/Editor.h:41`). UX-6은 이 위에 컴포넌트/property command를 쌓는다.
- **빌드 구조:** 에디터 전용 cpp는 `CMakeLists.txt`의 `EDITOR_SOURCES`(`:144-164`, `InspectorWindow.cpp`·`ProjectSettingsWindow.cpp`·`Commands/*.cpp` 포함), `MOLGA_EDITOR` 정의는 `molga_engine`에만 붙는다(`:173`). 단위 테스트는 `tests/CMakeLists.txt`의 `molga_add_test(name src)`로 등록하며 기본적으로 `molga_core`+`doctest_main`에 링크된다(헤더 온리/`molga_core` 심볼만 쓰는 테스트가 이상적). imgui가 필요한 테스트는 `target_link_libraries(... imgui)`를 추가한다(예: `test_build_manager`).

---

## 파일 구조

**Create (core/editor 로직 — 단위 테스트 대상):**
- `src/Editor/Inspector/PropertyAdapter.h` — 직렬화 가능 필드 read/write 추상화(헤더 온리 가능 부분 분리)
- `src/Editor/Inspector/PropertyAdapter.cpp`
- `src/Editor/Inspector/PrefabOverrideTracker.h` — modifications(JSON) 위 property override 조회/추가/제거
- `src/Editor/Inspector/PrefabOverrideTracker.cpp`
- `src/Editor/Inspector/MultiEditModel.h` — 여러 객체의 shared/mixed-value 계산
- `src/Editor/Inspector/MultiEditModel.cpp`
- `src/Editor/Commands/ComponentCommands.h` — `ComponentAdd/Remove/Reset/PasteCommand`, `SetPropertyCommand`, `RevertOverrideCommand`
- `src/Editor/Commands/ComponentCommands.cpp`
- `src/Editor/Preferences/EditorPreferences.h` — 머신/유저 데이터(snap/외부 에디터/console/script-compile 정책)
- `src/Editor/Preferences/EditorPreferences.cpp`
- `src/Editor/Preferences/EditorShortcutRegistry.h` — 단축키 → command id 바인딩, 메뉴/툴바와 공유
- `src/Editor/Preferences/EditorShortcutRegistry.cpp`
- `src/Editor/Preferences/EditorLayoutService.h` — imgui.ini layout 영구 save/restore + 손상 복구
- `src/Editor/Preferences/EditorLayoutService.cpp`
- `src/Editor/CommandPalette.h` — 등록된 editor command를 검색·실행
- `src/Editor/CommandPalette.cpp`
- `src/Editor/Inspector/ComponentEditor.h` — **그리기 전용** 컴포넌트 에디터(데이터 mutation 금지)
- `src/Editor/Inspector/ComponentEditor.cpp`

**Modify:**
- `src/Editor/Windows/InspectorWindow.h` — `std::vector<GameObject*> targets`, lock 플래그, 검색 버퍼, clipboard 보유
- `src/Editor/Windows/InspectorWindow.cpp` — context menu·검색·lock·multi-edit·property override UI를 command 경로로
- `src/ECS/GameObject.h` / `.cpp` — `RemoveComponentByName(typeName)`, `GetComponentByName(typeName)`, `MoveComponent(from,to)` (타입명/순서 기반 연산)
- `src/Editor/Editor.h` / `.cpp` — `EditorPreferences`/`EditorShortcutRegistry`/`EditorLayoutService`/`CommandPalette` 멤버·접근자, 단축키 처리 루프, Command Palette 토글, layout save on shutdown
- `src/Editor/EditorConstants.h` — `WIN_COMMAND_PALETTE` 등 필요한 상수(선택)
- `CMakeLists.txt` — 위 신규 `.cpp`를 `EDITOR_SOURCES`에
- `tests/CMakeLists.txt` — 아래 테스트 등록

**Create (테스트):**
- `tests/test_property_adapter.cpp`
- `tests/test_component_commands.cpp`
- `tests/test_multi_edit_model.cpp`
- `tests/test_prefab_override_tracker.cpp`
- `tests/test_editor_preferences.cpp`
- `tests/test_shortcut_registry.cpp`
- `tests/test_command_palette.cpp`

> 테스트 전략: `PropertyAdapter`·`PrefabOverrideTracker`·`MultiEditModel`·`EditorPreferences`·`EditorShortcutRegistry`·`CommandPalette`는 ImGui·`Editor` singleton에 의존하지 않게 설계해 `molga_core`만으로 테스트한다. `ComponentCommands`는 `GameObject`/`ComponentFactory`만 쓰는 부분(Add/Remove/Reset/Paste를 **GameObject에 직접 적용**하는 free 함수 + thin command 래퍼)을 분리해 테스트하고, `Editor` 통합(선택/dirty)은 thin wrapper로 둔다.

---

## Task A. PropertyAdapter + ComponentEditor 분리 (TDD)

> 핵심 계약(§6): **그리기(ComponentEditor)와 데이터 read/write(PropertyAdapter)와 commit(EditorCommand)을 분리한다.** 이번 task는 직렬화 가능 필드를 JSON으로 추상화하는 `PropertyAdapter`를 만들고(테스트 가능), ComponentEditor는 이후 task에서 이 adapter+command를 쓰도록 한다.

**Files:**
- Create: `src/Editor/Inspector/PropertyAdapter.h`, `.cpp`
- Create: `tests/test_property_adapter.cpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_property_adapter.cpp`:
```cpp
#include "Editor/Inspector/PropertyAdapter.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "doctest.h"
#include <memory>

using molga::PropertyAdapter;

TEST_CASE("Snapshot captures a component's serialized fields") {
    auto go = std::make_shared<GameObject>("A");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(3.0f, 4.0f);

    nlohmann::json snap = PropertyAdapter::Snapshot(t);
    CHECK(snap["position"][0] == 3.0f);
    CHECK(snap["position"][1] == 4.0f);
}

TEST_CASE("Apply writes serialized fields back into a component") {
    auto go = std::make_shared<GameObject>("A");
    auto* t = go->AddComponent<Transform>();
    nlohmann::json data = { {"position", {7.0f, 8.0f}}, {"rotation", 0.0f}, {"scale", {1.0f, 1.0f}} };

    PropertyAdapter::Apply(t, data);
    CHECK(t->GetPosition().x == 7.0f);
    CHECK(t->GetPosition().y == 8.0f);
}

TEST_CASE("Apply of a single key changes only that key") {
    auto go = std::make_shared<GameObject>("A");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(1.0f, 2.0f);
    t->SetRotation(45.0f);

    PropertyAdapter::ApplyKey(t, "rotation", 90.0f);
    CHECK(t->GetRotation() == 90.0f);
    CHECK(t->GetPosition().x == 1.0f);   // 다른 필드 보존
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_property_adapter test_property_adapter.cpp)
target_sources(test_property_adapter PRIVATE ${CMAKE_SOURCE_DIR}/src/Editor/Inspector/PropertyAdapter.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_property_adapter -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: 최소 구현**

Create `src/Editor/Inspector/PropertyAdapter.h`:
```cpp
#pragma once
#include <nlohmann/json.hpp>
#include <string>

class Component;

namespace molga {

// 직렬화 가능한 컴포넌트 필드를 JSON으로 read/write 한다.
// ImGui·Editor singleton에 의존하지 않는다(데이터 계층).
class PropertyAdapter {
public:
    // 컴포넌트의 모든 직렬화 필드를 JSON으로 캡처(Component::Serialize 사용).
    static nlohmann::json Snapshot(const Component* component);

    // JSON 전체를 컴포넌트에 적용(Component::Deserialize 사용).
    static void Apply(Component* component, const nlohmann::json& data);

    // 단일 key만 변경(나머지 필드는 현재 값 보존).
    static void ApplyKey(Component* component, const std::string& key,
                         const nlohmann::json& value);
};

} // namespace molga
```

Create `src/Editor/Inspector/PropertyAdapter.cpp`:
```cpp
#include "Editor/Inspector/PropertyAdapter.h"
#include "ECS/Component.h"

namespace molga {

nlohmann::json PropertyAdapter::Snapshot(const Component* component) {
    nlohmann::json j;
    if (component) component->Serialize(j);
    return j;
}

void PropertyAdapter::Apply(Component* component, const nlohmann::json& data) {
    if (component) component->Deserialize(data);
}

void PropertyAdapter::ApplyKey(Component* component, const std::string& key,
                               const nlohmann::json& value) {
    if (!component) return;
    nlohmann::json merged = Snapshot(component);   // 현재 값
    merged[key] = value;                            // 한 key만 교체
    component->Deserialize(merged);                 // 나머지 보존
}

} // namespace molga
```

- [ ] **Step 4: 통과 확인 + EDITOR_SOURCES 등록**

`CMakeLists.txt`의 `EDITOR_SOURCES`에 추가:
```cmake
    src/Editor/Inspector/PropertyAdapter.cpp
```
Run:
```bash
cmake --build --preset debug --target test_property_adapter -j4
ctest --preset debug -R test_property_adapter --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: ComponentEditor(그리기 전용) 골격 추가**

Create `src/Editor/Inspector/ComponentEditor.h`:
```cpp
#pragma once
class Component;
class InspectorWindow;
namespace molga {
// 그리기 전용. 값 변경은 절대 직접 mutate 하지 않고 InspectorWindow가
// 소유한 command 경로(SetPropertyCommand 등)로 위임한다.
class ComponentEditor {
public:
    // 한 컴포넌트의 필드 UI를 그리고, 편집이 commit 되면 owner를 통해 command를 낸다.
    static void Draw(InspectorWindow& owner, Component* component);
};
} // namespace molga
```
`.cpp`는 우선 기존 `Component::OnInspectorGUI()` 호출을 위임받는 thin 구현으로 두고(회귀 방지), Task B/E에서 command·override-aware 경로로 채운다.

- [ ] **Step 6: 커밋**
```bash
git add src/Editor/Inspector/PropertyAdapter.* src/Editor/Inspector/ComponentEditor.* \
        tests/test_property_adapter.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(inspector): PropertyAdapter data layer + ComponentEditor draw boundary (UX-6 A)"
```

---

## Task B. ComponentAdd/Remove/Reset/Paste command + context menu (TDD)

> §2/§6: 컴포넌트 추가/삭제/reset/copy/paste/move를 모두 command로 만들고, Inspector header context menu에서 호출한다. command는 `ComponentFactory`(타입명 생성)와 `Component::Serialize/Deserialize`(reset/paste 데이터)를 쓴다. **단위 테스트는 GameObject에 직접 적용하는 free 함수를 검증**하고, command는 그 위 thin 래퍼다.

**Files:**
- Modify: `src/ECS/GameObject.h`, `.cpp` (`RemoveComponentByName`, `GetComponentByName`, `MoveComponent`)
- Create: `src/Editor/Commands/ComponentCommands.h`, `.cpp`
- Create: `tests/test_component_commands.cpp`
- Modify: `src/Editor/Windows/InspectorWindow.cpp` (context menu, Add Component → command)
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: GameObject에 타입명/순서 기반 연산 추가 (선언)**

`src/ECS/GameObject.h`의 `GetComponents()` 근처에 추가:
```cpp
    // 타입명으로 컴포넌트를 찾고/제거하고, 표시 순서를 바꾼다(에디터 연산 지원).
    Component* GetComponentByName(const std::string& typeName) const;
    bool RemoveComponentByName(const std::string& typeName);   // 제거 시 true
    bool MoveComponent(const std::string& typeName, int direction); // -1=up, +1=down
```

- [ ] **Step 2: 실패하는 테스트 작성**

Create `tests/test_component_commands.cpp`:
```cpp
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "Editor/Commands/ComponentCommands.h"
#include "doctest.h"
#include <memory>

using namespace molga;

TEST_CASE("AddComponentByName adds a component of the named type") {
    auto go = std::make_shared<GameObject>("A");
    CHECK(ComponentOps::AddByName(go.get(), "SpriteRenderer") != nullptr);
    CHECK(go->GetComponentByName("SpriteRenderer") != nullptr);
}

TEST_CASE("RemoveComponentByName removes only the named component") {
    auto go = std::make_shared<GameObject>("A");
    go->AddComponent<Transform>();
    go->AddComponent<SpriteRenderer>();
    CHECK(go->RemoveComponentByName("SpriteRenderer"));
    CHECK(go->GetComponentByName("SpriteRenderer") == nullptr);
    CHECK(go->GetComponentByName("Transform") != nullptr);
}

TEST_CASE("Reset restores a component's default-serialized state") {
    auto go = std::make_shared<GameObject>("A");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(9.0f, 9.0f);
    ComponentOps::Reset(go.get(), "Transform");
    CHECK(go->GetComponent<Transform>()->GetPosition().x == 0.0f);  // 기본값
}

TEST_CASE("Copy then Paste transfers serialized fields to another object") {
    auto src = std::make_shared<GameObject>("Src");
    auto* st = src->AddComponent<Transform>();
    st->SetPosition(2.0f, 5.0f);
    nlohmann::json clip = ComponentOps::Copy(src.get(), "Transform");

    auto dst = std::make_shared<GameObject>("Dst");
    dst->AddComponent<Transform>();
    ComponentOps::Paste(dst.get(), "Transform", clip);
    CHECK(dst->GetComponent<Transform>()->GetPosition().y == 5.0f);
}

TEST_CASE("MoveComponent reorders components deterministically") {
    auto go = std::make_shared<GameObject>("A");
    go->AddComponent<Transform>();
    go->AddComponent<SpriteRenderer>();
    CHECK(go->MoveComponent("SpriteRenderer", -1));   // up
    CHECK(go->GetComponents().front()->GetTypeName() == "SpriteRenderer");
}
```

- [ ] **Step 3: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_component_commands test_component_commands.cpp)
target_sources(test_component_commands PRIVATE ${CMAKE_SOURCE_DIR}/src/Editor/Commands/ComponentCommands.cpp)
```
Run: `cmake --build --preset debug --target test_component_commands -j4` → FAIL.

- [ ] **Step 4: GameObject 구현 (`.cpp`)**

`src/ECS/GameObject.cpp`에 추가(`AddComponentRaw`/`GetComponents` 근처):
```cpp
Component* GameObject::GetComponentByName(const std::string& typeName) const {
    for (auto* c : componentOrder_) {
        if (c && c->GetTypeName() == typeName) return c;
    }
    return nullptr;
}

bool GameObject::RemoveComponentByName(const std::string& typeName) {
    for (auto it = componentMap.begin(); it != componentMap.end(); ++it) {
        if (it->second && it->second->GetTypeName() == typeName) {
            Component* raw = it->second.get();
            if (raw->IsEnabled()) raw->OnDisable();
            raw->OnDetach();
            componentOrder_.erase(
                std::remove(componentOrder_.begin(), componentOrder_.end(), raw),
                componentOrder_.end());
            componentMap.erase(it);
            return true;
        }
    }
    return false;
}

bool GameObject::MoveComponent(const std::string& typeName, int direction) {
    auto it = std::find_if(componentOrder_.begin(), componentOrder_.end(),
        [&](Component* c){ return c && c->GetTypeName() == typeName; });
    if (it == componentOrder_.end()) return false;
    auto idx = std::distance(componentOrder_.begin(), it);
    auto target = idx + direction;
    if (target < 0 || target >= (long)componentOrder_.size()) return false;
    std::swap(componentOrder_[idx], componentOrder_[target]);
    return true;
}
```

- [ ] **Step 5: ComponentCommands 구현**

Create `src/Editor/Commands/ComponentCommands.h`:
```cpp
#pragma once
#include "Editor/Commands/EditorCommand.h"
#include <nlohmann/json.hpp>
#include <string>

class GameObject;
class Component;

namespace molga {

// GameObject에 직접 적용하는 순수 연산(테스트 가능, Editor singleton 비의존).
struct ComponentOps {
    static Component* AddByName(GameObject* go, const std::string& typeName);
    static nlohmann::json Copy(GameObject* go, const std::string& typeName);
    static void Paste(GameObject* go, const std::string& typeName, const nlohmann::json& data);
    static void Reset(GameObject* go, const std::string& typeName); // 기본값으로
};

// ── 위 연산을 CommandHistory로 감싸는 undoable command ──
class ComponentAddCommand : public ICommand {
public:
    ComponentAddCommand(unsigned int objectId, std::string typeName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Add Component"; }
private:
    unsigned int objectId_; std::string typeName_;
};

class ComponentRemoveCommand : public ICommand {
public:
    ComponentRemoveCommand(unsigned int objectId, std::string typeName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Remove Component"; }
private:
    unsigned int objectId_; std::string typeName_;
    nlohmann::json savedState_;   // undo 복원용
};

class ComponentResetCommand : public ICommand {
public:
    ComponentResetCommand(unsigned int objectId, std::string typeName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Reset Component"; }
private:
    unsigned int objectId_; std::string typeName_;
    nlohmann::json before_;
};

class ComponentPasteCommand : public ICommand {
public:
    ComponentPasteCommand(unsigned int objectId, std::string typeName, nlohmann::json data);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Paste Component Values"; }
private:
    unsigned int objectId_; std::string typeName_;
    nlohmann::json data_, before_;
};

// 단일 property 변경(인스펙터 필드 편집·gizmo와 동일 의미). drag 1회 = command 1개.
class SetPropertyCommand : public ICommand {
public:
    SetPropertyCommand(unsigned int objectId, std::string typeName,
                       std::string key, nlohmann::json newValue);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Set " + key_; }
private:
    unsigned int objectId_; std::string typeName_, key_;
    nlohmann::json newValue_, oldValue_;
};

} // namespace molga
```

Create `src/Editor/Commands/ComponentCommands.cpp` — `ComponentOps`는 `ComponentFactory`/`PropertyAdapter`로 구현하고, command 본문은 `Editor::Get().FindObjectById(objectId_)`로 객체를 찾아 `ComponentOps`를 호출한 뒤 `Editor::Get().MarkSceneModified()`를 부른다(Reset/Remove/Paste/Set은 `before_`/`savedState_`/`oldValue_`에 `PropertyAdapter::Snapshot`을 저장해 undo 복원). 예:
```cpp
#include "Editor/Commands/ComponentCommands.h"
#include "Editor/Inspector/PropertyAdapter.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

namespace molga {

Component* ComponentOps::AddByName(GameObject* go, const std::string& typeName) {
    if (!go || go->GetComponentByName(typeName)) return nullptr;
    return ComponentFactory::Get().Create(typeName, go);
}
nlohmann::json ComponentOps::Copy(GameObject* go, const std::string& typeName) {
    return PropertyAdapter::Snapshot(go ? go->GetComponentByName(typeName) : nullptr);
}
void ComponentOps::Paste(GameObject* go, const std::string& typeName, const nlohmann::json& d) {
    if (Component* c = go ? go->GetComponentByName(typeName) : nullptr) PropertyAdapter::Apply(c, d);
}
void ComponentOps::Reset(GameObject* go, const std::string& typeName) {
    if (!go) return;
    go->RemoveComponentByName(typeName);
    ComponentFactory::Get().Create(typeName, go);   // 기본값 인스턴스
}

void ComponentRemoveCommand::Execute() {
    if (GameObject* go = Editor::Get().FindObjectById(objectId_)) {
        if (Component* c = go->GetComponentByName(typeName_)) savedState_ = PropertyAdapter::Snapshot(c);
        go->RemoveComponentByName(typeName_);
        Editor::Get().MarkSceneModified();
    }
}
void ComponentRemoveCommand::Undo() {
    if (GameObject* go = Editor::Get().FindObjectById(objectId_)) {
        if (Component* c = ComponentOps::AddByName(go, typeName_)) PropertyAdapter::Apply(c, savedState_);
        Editor::Get().MarkSceneModified();
    }
}
// ... Add/Reset/Paste/SetProperty 동일 패턴(생성자·나머지 메서드는 위 헤더에 맞춰 구현)
} // namespace molga
```

- [ ] **Step 6: 통과 + EDITOR_SOURCES 등록**

`CMakeLists.txt` `EDITOR_SOURCES`에 `src/Editor/Commands/ComponentCommands.cpp` 추가.
Run:
```bash
cmake --build --preset debug --target test_component_commands -j4
ctest --preset debug -R test_component_commands --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Inspector에 context menu + Add Component를 command로 연결**

`src/Editor/Windows/InspectorWindow.cpp`:
- `DrawComponent`(`:363-415`)의 헤더 뒤에 `if (ImGui::BeginPopupContextItem())`를 추가해 **Reset / Remove / Copy / Paste / Move Up / Move Down** 메뉴 항목을 두고, 각 항목이 `Editor::Get().GetCommandHistory().Execute(std::make_unique<molga::ComponentResetCommand>(target->GetID(), typeName))` 식으로 command를 낸다(Move는 `ComponentMoveCommand` 또는 `SetPropertyCommand` 대신 전용; clipboard는 InspectorWindow 멤버 `nlohmann::json componentClipboard_`).
- "AddComponentPopup"(`:193-265`)의 `target->AddComponent<T>()` 직접 호출을 `Editor::Get().GetCommandHistory().Execute(std::make_unique<molga::ComponentAddCommand>(target->GetID(), "SpriteRenderer"))` 형태로 교체(타입명 문자열은 각 항목의 `StaticTypeName()`).

- [ ] **Step 8: 빌드·수동 검증·커밋**
```bash
cmake --build --preset debug -j4 && ctest --preset debug --output-on-failure
```
수동: 컴포넌트 추가 → Undo로 사라짐 / 우클릭 Reset·Remove·Copy·Paste·Move가 Undo/Redo 되고 `*unsaved` 갱신.
```bash
git add -A && git commit -m "feat(inspector): component add/remove/reset/copy/paste/move via commands + context menu (UX-6 B)"
```

---

## Task C. 다중 선택 shared/mixed-value 편집 (TDD)

> §6: 다중 선택 시 공통 필드와 mixed value를 표시한다. UX-1의 `SelectionService`가 다중 선택 ID 집합을 제공한다는 전제(없으면 본 task의 InspectorWindow가 `std::vector<GameObject*> targets`를 직접 받는다). 핵심 로직 `MultiEditModel`은 데이터 계층으로 분리해 테스트한다.

**Files:**
- Create: `src/Editor/Inspector/MultiEditModel.h`, `.cpp`
- Create: `tests/test_multi_edit_model.cpp`
- Modify: `src/Editor/Windows/InspectorWindow.h`, `.cpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_multi_edit_model.cpp`:
```cpp
#include "Editor/Inspector/MultiEditModel.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "doctest.h"
#include <memory>

using molga::MultiEditModel;

TEST_CASE("shared component types are the intersection across the selection") {
    auto a = std::make_shared<GameObject>("A"); a->AddComponent<Transform>(); a->AddComponent<SpriteRenderer>();
    auto b = std::make_shared<GameObject>("B"); b->AddComponent<Transform>();
    MultiEditModel m({ a.get(), b.get() });
    auto shared = m.SharedComponentTypes();
    CHECK(shared.size() == 1);
    CHECK(shared[0] == "Transform");                 // SpriteRenderer는 b에 없음
}

TEST_CASE("a field with identical values reports no mixed state") {
    auto a = std::make_shared<GameObject>("A"); a->AddComponent<Transform>()->SetRotation(10.0f);
    auto b = std::make_shared<GameObject>("B"); b->AddComponent<Transform>()->SetRotation(10.0f);
    MultiEditModel m({ a.get(), b.get() });
    CHECK_FALSE(m.IsMixed("Transform", "rotation"));
}

TEST_CASE("a field with differing values reports mixed state") {
    auto a = std::make_shared<GameObject>("A"); a->AddComponent<Transform>()->SetRotation(10.0f);
    auto b = std::make_shared<GameObject>("B"); b->AddComponent<Transform>()->SetRotation(90.0f);
    MultiEditModel m({ a.get(), b.get() });
    CHECK(m.IsMixed("Transform", "rotation"));
}
```

- [ ] **Step 2: 등록·실패 → 구현 → 통과**

`MultiEditModel`은 생성자에서 `std::vector<GameObject*>`를 받고, `SharedComponentTypes()`는 각 객체 `GetComponents()`의 타입명 교집합, `IsMixed(type,key)`는 `PropertyAdapter::Snapshot(GetComponentByName(type))[key]`를 모든 객체에서 비교한다. 헤더 온리 비의존(`PropertyAdapter`만 링크).
`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_multi_edit_model test_multi_edit_model.cpp)
target_sources(test_multi_edit_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Inspector/MultiEditModel.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Inspector/PropertyAdapter.cpp)
```

- [ ] **Step 3: InspectorWindow 다중 타깃 + mixed UI**

`src/Editor/Windows/InspectorWindow.h`: `GameObject* target` 옆에 `std::vector<GameObject*> targets;` 추가(단일은 size 1). `SetTargets(std::vector<GameObject*>)` 추가.
`.cpp`: targets.size()>1이면 `MultiEditModel`로 공통 컴포넌트만 그리고, mixed 필드는 placeholder("—")로 표시. 편집 commit 시 **선택된 모든 객체에 동일 `SetPropertyCommand`/`ComponentPasteCommand`를 batch**로 낸다(한 사용자 액션 → 한 묶음). `Editor::SetSelectedObject`/SelectionService 다중 선택과 연결.

- [ ] **Step 4: 빌드·커밋**
```bash
git add -A && git commit -m "feat(inspector): multi-selection shared fields + mixed-value editing (UX-6 C)"
```

---

## Task D. lock 가능한 Inspector + 검색/필터 (TDD 일부 + 수동)

> §6 완료 기준: Inspector가 한 객체에 lock 되면 다른 곳에서 선택이 바뀌어도 그 객체를 계속 보여준다. 검색/필터는 컴포넌트·필드 이름 매칭.

**Files:**
- Modify: `src/Editor/Windows/InspectorWindow.h`, `.cpp`

- [ ] **Step 1: lock 상태 + 검색 버퍼 추가 (헤더)**

`src/Editor/Windows/InspectorWindow.h`:
```cpp
    void SetTarget(GameObject* obj) { if (!locked_) target = obj; }   // lock 시 무시
    GameObject* GetTarget() const { return target; }
    bool IsLocked() const { return locked_; }
    void SetLocked(bool v) { locked_ = v; }
private:
    bool locked_ = false;
    char searchBuffer_[128] = "";
    nlohmann::json componentClipboard_;   // Task B에서 사용
```

- [ ] **Step 2: lock 토글 버튼 + 검색 입력 UI**

`InspectorWindow::OnGUI` 상단(타이틀 영역)에 `🔒` 토글 버튼(`ImGui::Checkbox`/SmallButton)과 `ImGui::InputTextWithHint("##search", "Search components...", searchBuffer_, ...)`를 추가. `DrawComponent` 진입 시 `searchBuffer_`가 비어있지 않으면 컴포넌트 타입명(소문자 비교)에 substring 매칭만 그린다.

- [ ] **Step 3: Editor 선택 경로가 lock을 존중하도록 확인**

`Editor::SetSelectedObject`(`src/Editor/Editor.cpp:314-321`)가 `inspector->SetTarget(obj)`를 부르는데, 위 `SetTarget`이 lock 시 no-op이므로 lock된 Inspector는 hierarchy 선택 변경에도 타깃을 유지한다. (lock 상태에서 Hierarchy/Scene 선택은 여전히 바뀐다 — Inspector만 고정.)

- [ ] **Step 4: 빌드·수동 검증·커밋**

수동: 객체 A를 인스펙터에 lock → Hierarchy에서 B 선택 → 인스펙터는 여전히 A / 검색창에 "Transform" 입력 시 Transform 섹션만 표시.
```bash
git add -A && git commit -m "feat(inspector): lockable target + component search filter (UX-6 D)"
```

---

## Task E. PrefabOverrideTracker + property 단위 override 되돌리기 (TDD)

> §6 완료 기준: prefab instance override가 **property 단위**로 보이고 **개별 되돌리기** 가능. 현재는 객체 전체 Revert만 있고 override는 BulletText 나열뿐(`InspectorWindow.cpp:154-167`). modification entry는 `{target, component, key, value}`(`src/Core/PrefabUtil.cpp:21`).

**Files:**
- Create: `src/Editor/Inspector/PrefabOverrideTracker.h`, `.cpp`
- Create: `tests/test_prefab_override_tracker.cpp`
- Modify: `src/Editor/Commands/ComponentCommands.h`, `.cpp` (`RevertOverrideCommand`)
- Modify: `src/Editor/Windows/InspectorWindow.cpp` (property별 override 마크 + per-property revert 버튼)
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_prefab_override_tracker.cpp`:
```cpp
#include "Editor/Inspector/PrefabOverrideTracker.h"
#include "doctest.h"
#include <nlohmann/json.hpp>

using molga::PrefabOverrideTracker;

static nlohmann::json sampleMods() {
    return nlohmann::json::array({
        { {"target", 5}, {"component","Transform"}, {"key","position"}, {"value", {1.0f,2.0f}} },
        { {"target", 5}, {"component","Transform"}, {"key","rotation"}, {"value", 45.0f} }
    });
}

TEST_CASE("HasOverride is true only for a tracked component+key") {
    PrefabOverrideTracker t(sampleMods());
    CHECK(t.HasOverride(5, "Transform", "position"));
    CHECK(t.HasOverride(5, "Transform", "rotation"));
    CHECK_FALSE(t.HasOverride(5, "Transform", "scale"));
    CHECK_FALSE(t.HasOverride(5, "SpriteRenderer", "color"));
}

TEST_CASE("RemoveOverride drops exactly one property entry") {
    PrefabOverrideTracker t(sampleMods());
    t.RemoveOverride(5, "Transform", "position");
    CHECK_FALSE(t.HasOverride(5, "Transform", "position"));
    CHECK(t.HasOverride(5, "Transform", "rotation"));   // 다른 override 보존
    CHECK(t.Modifications().size() == 1);
}
```

- [ ] **Step 2: 등록·실패 → 구현 → 통과**

`PrefabOverrideTracker`는 `nlohmann::json` modifications를 보유하고 `HasOverride(target,component,key)`·`RemoveOverride(...)`·`Modifications()`를 제공(순수 데이터). `tests/CMakeLists.txt`:
```cmake
molga_add_test(test_prefab_override_tracker test_prefab_override_tracker.cpp)
target_sources(test_prefab_override_tracker PRIVATE ${CMAKE_SOURCE_DIR}/src/Editor/Inspector/PrefabOverrideTracker.cpp)
```
`CMakeLists.txt` `EDITOR_SOURCES`에 `PrefabOverrideTracker.cpp` 추가.

- [ ] **Step 3: RevertOverrideCommand 추가**

`ComponentCommands.h/.cpp`에 `RevertOverrideCommand(objectId, component, key)` 추가: Execute는 `PrefabInstance::GetModifications()`에서 해당 entry를 찾아 prefab template 값으로 필드 복원 + `PrefabOverrideTracker::RemoveOverride`로 entry 제거, Undo는 entry·값 복원. `PrefabUtil`의 template 적용 로직 재사용.

- [ ] **Step 4: Inspector property별 override 표시 + revert**

`InspectorWindow::DrawComponent`(`:363-415`)에서 컴포넌트 헤더 `*`(`:391`)는 유지하되, 필드별로 `PrefabOverrideTracker::HasOverride(target->GetID(), typeName, key)`면 굵게/색 표시하고 우클릭 context menu에 "Revert <key>" 항목을 두어 `RevertOverrideCommand`를 낸다. 헤더 context menu에는 "Revert All / Apply All"(기존 `RevertPrefabCommand`/`ApplyPrefabCommand` 재사용).

- [ ] **Step 5: 빌드·수동 검증·커밋**

수동: prefab instance의 한 필드만 바꿔 override 생성 → 그 필드만 강조 → 우클릭 "Revert position"으로 그 property만 되돌아오고 다른 override 유지 → Undo로 복원.
```bash
git add -A && git commit -m "feat(prefab): property-level override tracker + per-property revert (UX-6 E)"
```

---

## Task F. EditorPreferences vs ProjectSettings 분리 + ShortcutRegistry + LayoutService (TDD)

> §8: 머신/유저 데이터(`EditorPreferences`)와 프로젝트 데이터(`ProjectSettings`)를 별도 타입·별도 파일로 둔다. 단축키는 메뉴/툴바와 **같은 command**를 부르고, layout은 영구 저장/복구된다.

**Files:**
- Create: `src/Editor/Preferences/EditorPreferences.h`, `.cpp`
- Create: `src/Editor/Preferences/EditorShortcutRegistry.h`, `.cpp`
- Create: `src/Editor/Preferences/EditorLayoutService.h`, `.cpp`
- Create: `tests/test_editor_preferences.cpp`, `tests/test_shortcut_registry.cpp`
- Modify: `src/Editor/Editor.h`, `.cpp` (멤버·접근자·단축키 처리·layout save)
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: EditorPreferences 테스트 (round-trip + ProjectSettings 비혼합)**

Create `tests/test_editor_preferences.cpp`:
```cpp
#include "Editor/Preferences/EditorPreferences.h"
#include "doctest.h"

using molga::EditorPreferences;

TEST_CASE("preferences round-trip through JSON") {
    EditorPreferences p;
    p.snapEnabled = true; p.snapIncrement = 0.25f;
    p.externalEditor = "code"; p.clearConsoleOnPlay = true;
    p.scriptCompileOnPlay = EditorPreferences::CompilePolicy::QueueUntilStop;

    nlohmann::json j = p.Serialize();
    EditorPreferences loaded; loaded.Deserialize(j);
    CHECK(loaded.snapEnabled);
    CHECK(loaded.snapIncrement == doctest::Approx(0.25f));
    CHECK(loaded.externalEditor == "code");
    CHECK(loaded.clearConsoleOnPlay);
    CHECK(loaded.scriptCompileOnPlay == EditorPreferences::CompilePolicy::QueueUntilStop);
}

TEST_CASE("defaults are stable and machine-scoped (no project tags/layers fields)") {
    EditorPreferences p;   // snap/editor/console/compile만 — tags/layers 없음
    CHECK(p.snapIncrement > 0.0f);
}
```

- [ ] **Step 2: EditorPreferences 구현**

`EditorPreferences`는 snap(enabled/increment), `externalEditor`, console 정책(`clearConsoleOnPlay/Build/Recompile`, `errorPause`), `scriptCompileOnPlay`(enum: `Blocked/QueueUntilStop/AllowWithReload`)를 필드로 갖고 `Serialize()/Deserialize()`/`LoadFromFile()/SaveToFile()`를 제공. **저장 경로는 프로젝트 폴더가 아닌 유저 경로**(예: `~/.molga/editor_preferences.json`) — `ProjectSettings`(`ProjectSettings/project_settings.json`)와 명확히 분리. tags/layers/collision 같은 프로젝트 데이터는 절대 넣지 않는다.
`tests/CMakeLists.txt`에 `test_editor_preferences` 등록(`EditorPreferences.cpp` source 추가).

- [ ] **Step 3: ShortcutRegistry 테스트**

Create `tests/test_shortcut_registry.cpp`:
```cpp
#include "Editor/Preferences/EditorShortcutRegistry.h"
#include "doctest.h"
#include <string>

using molga::EditorShortcutRegistry;
using molga::Chord;

TEST_CASE("a bound chord resolves to its command id") {
    EditorShortcutRegistry r;
    r.Bind(Chord::Ctrl('Z'), "edit.undo");
    CHECK(r.Resolve(Chord::Ctrl('Z')) == "edit.undo");
    CHECK(r.Resolve(Chord::Ctrl('Y')).empty());
}

TEST_CASE("rebinding a chord overwrites the previous command") {
    EditorShortcutRegistry r;
    r.Bind(Chord::Ctrl('S'), "scene.save");
    r.Bind(Chord::Ctrl('S'), "scene.saveAs");
    CHECK(r.Resolve(Chord::Ctrl('S')) == "scene.saveAs");
}
```

- [ ] **Step 4: ShortcutRegistry 구현**

`Chord`는 `{key, ctrl, shift, alt}` POD + `Ctrl(char)` 헬퍼, `EditorShortcutRegistry`는 `Bind(chord, commandId)`·`Resolve(chord) -> commandId(string)`·`All()`. **command id 문자열**(`"edit.undo"`, `"scene.save"`, `"inspector.toggleLock"`, `"window.commandPalette"` 등)로 메뉴/툴바/Palette와 공유. `tests/CMakeLists.txt` 등록.

- [ ] **Step 5: EditorLayoutService 구현 (수동 검증 위주)**

`EditorLayoutService`는 ImGui ini layout을 명시 경로(유저 경로)에 `Save()`/`Load()`하고, 파일 손상/없음 시 `SetupDefaultLayout`로 안전 복구. `Editor::Shutdown`에서 `Save()`, `Editor::Init`에서 `Load()`(실패 시 default). 기존 "Reset Layout"(`Editor.cpp:209-211`)은 default 재생성 경로를 그대로 호출하되 `EditorLayoutService::ResetToDefault()`로 위임.

- [ ] **Step 6: Editor에 멤버·단축키 처리 연결**

`src/Editor/Editor.h`에 `EditorPreferences preferences_`, `EditorShortcutRegistry shortcuts_`, `EditorLayoutService layout_` 멤버 + 접근자. `Editor.cpp`의 메인 루프(메뉴 바 렌더 후)에 단축키 처리 루프 추가: 현재 입력 chord를 `shortcuts_.Resolve()`로 commandId로 바꾸고, commandId → 동일한 핸들러(`Undo`/`Redo`/`SaveScene`/`Build`/Command Palette 토글)를 호출한다. 즉 menu label `"Ctrl+Z"`(`Editor.cpp:170`)가 이제 **실제로** 작동한다.

- [ ] **Step 7: 빌드·테스트·수동 검증·커밋**
```bash
cmake --build --preset debug -j4 && ctest --preset debug --output-on-failure
```
수동: Ctrl+Z/Ctrl+S가 실제 동작 / snap·외부 에디터·console 정책을 Preferences에서 바꿔 저장 후 재시작 시 유지 / layout 이동 후 재시작 시 복원, Reset Layout으로 안전 복귀.
```bash
git add -A && git commit -m "feat(editor): EditorPreferences vs ProjectSettings split + ShortcutRegistry + LayoutService (UX-6 F)"
```

---

## Task G. CommandPalette (TDD)

> §8 완료 기준: 등록된 editor command를 검색·실행. 메뉴/툴바/단축키와 **같은 command id**를 공유한다.

**Files:**
- Create: `src/Editor/CommandPalette.h`, `.cpp`
- Create: `tests/test_command_palette.cpp`
- Modify: `src/Editor/Editor.h`, `.cpp` (Palette 멤버, 토글 단축키, 그리기)
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_command_palette.cpp`:
```cpp
#include "Editor/CommandPalette.h"
#include "doctest.h"
#include <string>

using molga::CommandPalette;

TEST_CASE("registered commands can be searched by fuzzy/substring") {
    CommandPalette p;
    int ran = 0;
    p.Register("scene.save", "Save Scene", [&]{ ran = 1; });
    p.Register("edit.undo",  "Undo",       [&]{ ran = 2; });

    auto hits = p.Search("save");
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].id == "scene.save");
}

TEST_CASE("executing a command id runs its callback") {
    CommandPalette p;
    int ran = 0;
    p.Register("edit.redo", "Redo", [&]{ ran = 42; });
    p.Execute("edit.redo");
    CHECK(ran == 42);
}

TEST_CASE("search is case-insensitive and matches the title") {
    CommandPalette p;
    p.Register("window.console", "Toggle Console", []{});
    CHECK(p.Search("CONSOLE").size() == 1);
}
```

- [ ] **Step 2: 등록·실패 → 구현 → 통과**

`CommandPalette`는 `Register(id, title, std::function<void()>)`, `Search(query) -> vector<Entry{id,title}>`(대소문자 무시 substring/fuzzy), `Execute(id)`(콜백 호출). ImGui 비의존(데이터/로직)으로 테스트. `tests/CMakeLists.txt`에 `test_command_palette` 등록, `CMakeLists.txt` `EDITOR_SOURCES`에 `CommandPalette.cpp` 추가.

- [ ] **Step 3: Editor에 Palette 등록·그리기·토글**

`Editor::Init`에서 메뉴/단축키와 동일한 핸들러를 command id로 등록(`scene.save`→`SaveScene()`, `edit.undo`→`commandHistory.Undo()`, `inspector.toggleLock`→`inspector->SetLocked(!...)`, `window.console`→ Console 토글 등). `Editor.cpp`에 Palette 모달 UI(검색 입력 + 결과 리스트 + Enter 실행)를 그리고, `shortcuts_`에 `Ctrl+P`(또는 `Ctrl+Shift+P`)를 `"window.commandPalette"`로 바인딩해 토글. 즉 동일 command를 메뉴·툴바·단축키·Palette 네 경로에서 공유.

- [ ] **Step 4: 빌드·수동 검증·커밋**

수동: Ctrl+P → "save" 검색 → Enter로 Save 실행 / "console" 검색 → Console 토글.
```bash
git add -A && git commit -m "feat(editor): CommandPalette searching & executing registered commands (UX-6 G)"
```

---

## Task H. (Stretch, gated) 애니메이션/타일맵 authoring 패널 scaffolding

> **게이트:** Task A–G(인스펙터 command/adapter/tracker + preferences/shortcut/layout + Command Palette)가 모두 안정되고, UX-1(selection/undo)·UX-2(console)·UX-3(asset identity)이 안정된 **이후에만** 시작한다. §10 비목표: "selection·undo·console·asset identity가 안정되기 전 advanced animation editor를 만들지 않는다." 이 task는 본 milestone에서 **scaffolding(데이터 모델·패널 골격·command 경로 합류)까지만** 다루고, 완전한 타임라인/스테이트 머신/타일 브러시는 로드맵 Milestone 2-6(Animation)·2-8(Tilemap)의 별도 상세 계획서에서 구현한다.

**Files (scaffolding 한정):**
- Create: `src/Editor/Windows/AnimationWindow.h`, `.cpp` (패널 골격: AnimationClip 미리보기 자리, command 경로 사용 명시)
- Create: `src/Editor/Windows/TilemapWindow.h`, `.cpp` (Tile Palette 골격: brush stroke를 **단일 Undo command**로 묶는 계약 명시)
- Modify: `CMakeLists.txt` (`EDITOR_SOURCES`)

- [ ] **Step 1: 골격 패널 등록(빈 도킹 윈도우 + "scaffold only" 안내)**

두 윈도우를 `WindowManager`에 등록하고, 본문에는 "이 패널은 UX-6 backbone 위에 scaffold만 제공한다. 편집은 EditorCommand(예: `TilePaintStrokeCommand`)를 통해 CommandHistory를 통과한다"는 계약을 주석/`ImGui::TextDisabled`로 명시.

- [ ] **Step 2: command 경로 계약만 정의(구현은 2-6/2-8로 위임)**

`TilePaintStrokeCommand`(한 brush drag = command 1개), `AnimationKeyframeEditCommand` 등 **선언만** 두고, 실제 데이터 모델·렌더링은 2-6/2-8 상세 계획서에서 채운다. 본 task에서는 회귀 위험(Edit World 영구 변경, brush마다 command 남발)을 피하는 계약을 문서화한다.

- [ ] **Step 3: 커밋**
```bash
git add -A && git commit -m "feat(editor): animation/tilemap authoring panel scaffolding gated behind UX-6 backbone (UX-6 H, stretch)"
```

---

## 완료 기준

- [ ] 컴포넌트 필드를 편집·되돌리기·복사·붙여넣기·reset·remove·move up/down 할 수 있고 모두 `CommandHistory`를 통과해 `*unsaved`를 갱신한다(Task A/B).
- [ ] 인스펙터 필드 편집과 (UX-1의) gizmo 편집이 **같은 command 의미**(`SetPropertyCommand`, drag 1회 = command 1개)를 따른다(Task A/B).
- [ ] 다중 선택 시 공통 필드와 mixed value("—")가 보이고, 한 편집이 선택 전체에 batch command로 적용된다(Task C).
- [ ] Inspector를 한 객체에 lock 하면 다른 곳에서 선택이 바뀌어도 그 객체를 유지한다(Task D).
- [ ] 검색/필터로 컴포넌트를 추려 볼 수 있다(Task D).
- [ ] prefab instance override가 **property 단위**로 보이고 **개별 되돌리기**가 가능하며 다른 override는 보존된다(Task E).
- [ ] 단축키가 메뉴/툴바와 **동일한 command**를 호출한다(`Ctrl+Z`·`Ctrl+S`가 실제 동작)(Task F).
- [ ] 사용자 layout이 재시작 후 복원되고, 손상 시/Reset 시 default로 안전 복구된다(Task F).
- [ ] snap·외부 에디터·console·script-compile 정책이 `EditorPreferences`로 편집·영구 저장되며 `ProjectSettings`와 별도 파일로 분리된다(Task F).
- [ ] Command Palette가 등록된 editor command를 검색·실행한다(Task G).
- [ ] **Exit scenario:** 사용자가 객체 그룹을 편집하고, 컴포넌트 설정을 복사하고, prefab override를 적용한 뒤, 데이터 손실 없이 일반 씬 편집으로 돌아온다.
- [ ] (stretch) 애니메이션/타일맵 패널은 backbone 뒤 scaffolding만 제공하고 편집은 command 경로를 통과하는 계약이 문서화된다(Task H).

---

## 의존성 / 순서

**선행(반드시 안정):**
- **UX-1** — `SelectionService`/다중 선택, `TransformCommand`/inspector transform command. Task C(다중 선택)·B(필드 command)가 이 selection·command 토대를 가정한다. 없으면 InspectorWindow가 `targets` 벡터를 직접 받는 fallback으로만 동작한다.
- **UX-2** — structured Log + `ConsoleWindow`. Command Palette·EditorPreferences의 console 정책(`clearConsoleOnPlay/Build/Recompile`, `errorPause`)이 Console을 전제로 한다.
- **UX-3** — asset identity/GUID. prefab override·asset 참조 필드 편집이 path 대신 안정 id 위에서 동작해야 override가 잘못된 대상에 적용되지 않는다(roadmap 2-7 위험 통제).

**Task 순서:** A(PropertyAdapter/ComponentEditor 분리) → B(Component Add/Remove/Reset/Paste/Move command + context menu) → C(다중 선택) → D(lock + 검색) → E(PrefabOverrideTracker + per-property revert) → F(EditorPreferences/Shortcut/Layout backbone) → G(CommandPalette). **F·G는 A–E 뒤**(command id를 단축키·Palette가 공유하므로 command가 먼저 존재해야 함).

**Gated stretch:** Task H(애니메이션/타일맵 authoring)는 A–G와 UX-1/2/3가 모두 안정된 뒤에만 scaffolding을 시작하고, 완전한 구현은 로드맵 Milestone 2-6·2-8 상세 계획서로 넘긴다(§10 비목표 준수).
