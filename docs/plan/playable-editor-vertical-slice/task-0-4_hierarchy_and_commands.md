# Task 0-4: 안전한 Hierarchy 소유권 + Command/Undo 편집

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 선행: [task-0-5a](task-0-5a_test_framework.md)(doctest). **task-0-2보다 먼저** 끝내야 한다 — World 복제가 ASan-clean 해지려면 계층 수명이 먼저 안전해야 한다. 체크박스로 추적.

**Goal:** 부모/자식 raw pointer의 dangling·cycle 위험을 제거하고, subtree 삭제 정책을 명확히 한다. 생성/삭제/이름 변경/재부모화를 Undo 가능한 Command로 처리하고, 모든 편집이 dirty 상태를 갱신하게 한다.

**Architecture:** 소유권은 그대로 `std::vector<std::shared_ptr<GameObject>>`(부모/자식은 그 위의 raw pointer 오버레이)를 유지하되, (1) `~GameObject`가 자식의 `parent`를 정리하고, (2) `SetParent`가 cycle을 거부하며, (3) `CollectSubtree`로 subtree 단위 삭제를 지원한다. 편집은 헤더 온리 `CommandHistory`(undo/redo 스택)와 구체 Command들을 통해 수행하고, 각 Command가 dirty를 갱신한다.

**Tech Stack:** C++17, doctest, ImGui

**닫는 결함:** 갭 분석 P0-4(계층 안전), P0-7(dirty 추적 토대). *미저장 확인 모달과 단축키 처리는 Phase 1로 미룬다(갭 분석 Phase 1 항목과 일치).*

---

## 현재 상태 (검증된 사실)

- `GameObject`의 `parent`는 `GameObject*`, `children`은 `std::vector<GameObject*>` — **둘 다 raw pointer**. (`src/ECS/GameObject.h:122-123`)
- `~GameObject`(`src/ECS/GameObject.cpp:13-23`)는 자기 `parent`에서는 떨어져 나오지만(`parent->RemoveChild(this)`), 자식들에 대해선 `children.clear()`만 한다 → **삭제된 부모를 가리키는 dangling `parent` 포인터** 발생. Hierarchy는 매 프레임 `GetParent()`를 읽는다(`HierarchyWindow.cpp:39`).
- `SetParent`(`src/ECS/GameObject.cpp:25-40`)는 `if (parent == newParent) return;` 외에 **cycle/self/ancestor 검사가 없고**, `parent->children.push_back(this)`로 직접 넣어 AddChild의 중복 가드를 우회한다.
- 소유 컨테이너는 `main.cpp:60`의 `std::vector<std::shared_ptr<GameObject>> editorObjects`. `Editor`와 `HierarchyWindow`는 이를 raw pointer로 공유한다. 생성된 오브젝트(자식 포함)는 모두 이 평면 벡터에 들어간다.
- `HierarchyWindow::DeleteSelectedObject`(`:163-179`)는 **선택 오브젝트의 shared_ptr 하나만** 벡터에서 지운다 — 자식 미처리, `MarkModified` 미호출.
- dirty 플래그는 `SceneOperations::sceneModified`(`SceneOperations.h:18-24`) 하나뿐이고, 유일한 호출처는 `Editor::CreateGameObject`(`Editor.cpp:308-309`). 삭제/복제/이름 변경/Inspector 편집은 dirty를 안 바꾼다.
- Command/Undo/Redo 인프라 **전무**. Undo/Redo 메뉴는 "not yet implemented" 로그만 출력(`Editor.cpp:166-171`).
- ID는 `static unsigned int GameObject::nextID = 1` 후위 증가(`GameObject.cpp:7,10`). `GetID()`(`GameObject.h:21`).

---

## 파일 구조

**Files:**
- Modify: `src/ECS/GameObject.h` (`SetParent`→bool, `IsAncestorOf`/`CollectSubtree` 추가)
- Modify: `src/ECS/GameObject.cpp` (destructor 안전화, cycle 거부, subtree 수집)
- Create: `tests/test_hierarchy.cpp`
- Create: `src/Editor/Commands/EditorCommand.h` (헤더 온리 ICommand)
- Create: `src/Editor/Commands/CommandHistory.h` (헤더 온리 undo/redo 스택)
- Create: `tests/test_command_history.cpp`
- Create: `src/Editor/Commands/ObjectCommands.h`
- Create: `src/Editor/Commands/ObjectCommands.cpp`
- Modify: `src/Editor/Editor.h` (CommandHistory 멤버 + 헬퍼 메서드)
- Modify: `src/Editor/Editor.cpp` (헬퍼 구현, Undo/Redo 메뉴, dirty 표시)
- Modify: `src/Editor/Windows/HierarchyWindow.cpp` (생성/삭제/이름 변경을 Command로)
- Modify: `CMakeLists.txt` (`ObjectCommands.cpp`를 EDITOR_SOURCES에)
- Modify: `tests/CMakeLists.txt` (test_hierarchy, test_command_history 등록)

---

## Task A. GameObject 계층 안전화 (TDD)

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_hierarchy.cpp`:
```cpp
#include "ECS/GameObject.h"
#include "doctest.h"
#include <memory>
#include <vector>

TEST_CASE("destroying a parent leaves children with a null parent (no dangling)") {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child  = std::make_shared<GameObject>("Child");
    parent->AddChild(child.get());
    REQUIRE(child->GetParent() == parent.get());

    parent.reset();                          // destroy the parent object
    CHECK(child->GetParent() == nullptr);    // must NOT dangle
}

TEST_CASE("SetParent rejects self-parenting") {
    auto a = std::make_shared<GameObject>("A");
    CHECK_FALSE(a->SetParent(a.get()));
    CHECK(a->GetParent() == nullptr);
}

TEST_CASE("SetParent rejects cycles") {
    auto a = std::make_shared<GameObject>("A");
    auto b = std::make_shared<GameObject>("B");
    a->AddChild(b.get());                    // a -> b
    CHECK_FALSE(a->SetParent(b.get()));      // b -> a would create a cycle
    CHECK(a->GetParent() == nullptr);
    CHECK(b->GetParent() == a.get());
}

TEST_CASE("CollectSubtree returns self then all descendants") {
    auto a = std::make_shared<GameObject>("A");
    auto b = std::make_shared<GameObject>("B");
    auto c = std::make_shared<GameObject>("C");
    a->AddChild(b.get());
    b->AddChild(c.get());

    std::vector<GameObject*> out;
    a->CollectSubtree(out);
    CHECK(out.size() == 3);
    CHECK(out[0] == a.get());                // parent before children
}

TEST_CASE("reparenting moves a child without duplicating it") {
    auto p1 = std::make_shared<GameObject>("P1");
    auto p2 = std::make_shared<GameObject>("P2");
    auto c  = std::make_shared<GameObject>("C");
    CHECK(c->SetParent(p1.get()));
    CHECK(p1->GetChildren().size() == 1);
    CHECK(c->SetParent(p2.get()));
    CHECK(p1->GetChildren().size() == 0);
    CHECK(p2->GetChildren().size() == 1);
}
```

- [ ] **Step 2: tests/CMakeLists.txt에 등록**

```cmake
molga_add_test(test_hierarchy test_hierarchy.cpp)
```

- [ ] **Step 3: 컴파일/실행 실패 확인**

Run:
```bash
cmake --build --preset debug --target test_hierarchy -j4
```
Expected: FAIL — `SetParent`가 `void`라 `CHECK_FALSE(a->SetParent(...))` 컴파일 에러, `CollectSubtree` 미정의.

- [ ] **Step 4: GameObject.h 선언 수정**

`src/ECS/GameObject.h`에서:
```cpp
    void SetParent(GameObject* newParent);
    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);
```
을 다음으로 교체:
```cpp
    // 성공 시 true. self/cycle을 만들면 거부하고 false를 반환한다.
    bool SetParent(GameObject* newParent);
    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);

    // node가 이 오브젝트의 자손이면 true (cycle 방지에 사용).
    bool IsAncestorOf(const GameObject* node) const;
    // 이 오브젝트와 모든 자손을 DFS 순서(부모 먼저)로 out에 추가.
    void CollectSubtree(std::vector<GameObject*>& out);
```

- [ ] **Step 5: GameObject.cpp 구현 수정**

`src/ECS/GameObject.cpp` 상단 include에 없으면 추가:
```cpp
#include "Common/Log.h"
#include <algorithm>
```
destructor(현재 `:13-23`)를 다음으로 교체:
```cpp
GameObject::~GameObject() {
    NotifyDestroy();  // destroyed 플래그가 중복 호출을 막는다
    for (auto& [id, comp] : componentMap) {
        comp->OnDetach();
    }
    componentMap.clear();

    if (parent) {
        parent->RemoveChild(this);
    }
    // 살아남는 자식들이 해제될 우리를 가리키지 않도록 parent를 끊는다.
    for (auto* child : children) {
        if (child) child->parent = nullptr;
    }
    children.clear();
}
```
`SetParent`(현재 `:25-40`)를 다음으로 교체:
```cpp
bool GameObject::SetParent(GameObject* newParent) {
    if (newParent == parent) return true;          // 이미 그 부모
    if (newParent == this) {
        Log::Warn("GameObject", "Ignoring attempt to parent '" + name + "' to itself");
        return false;
    }
    if (newParent && IsAncestorOf(newParent)) {    // cycle을 만들게 됨
        Log::Warn("GameObject", "Ignoring reparent of '" + name + "' that would create a cycle");
        return false;
    }

    if (parent) {
        parent->RemoveChild(this);                 // this->parent를 nullptr로 만든다
    }
    parent = newParent;
    if (parent) {
        auto& siblings = parent->children;
        if (std::find(siblings.begin(), siblings.end(), this) == siblings.end()) {
            siblings.push_back(this);
        }
    }
    return true;
}

bool GameObject::IsAncestorOf(const GameObject* node) const {
    for (const GameObject* p = (node ? node->parent : nullptr); p; p = p->parent) {
        if (p == this) return true;
    }
    return false;
}

void GameObject::CollectSubtree(std::vector<GameObject*>& out) {
    out.push_back(this);
    for (auto* child : children) {
        if (child) child->CollectSubtree(out);
    }
}
```
`AddChild`(현재 `:42-50`)는 그대로 두되, 마지막 줄 `child->SetParent(this);`은 이제 bool을 반환하지만 결과를 무시해도 무방하다(자기/cycle은 SetParent가 거부).

- [ ] **Step 6: 테스트 통과 + ASan 확인**

Run:
```bash
cmake --build --preset debug --target test_hierarchy -j4
ctest --preset debug -R test_hierarchy --output-on-failure
cmake --preset asan && cmake --build --preset asan --target test_hierarchy -j4
ctest --preset asan -R test_hierarchy --output-on-failure
```
Expected: 둘 다 PASS, ASan 오류 없음.

- [ ] **Step 7: 커밋**

```bash
git add src/ECS/GameObject.h src/ECS/GameObject.cpp tests/test_hierarchy.cpp tests/CMakeLists.txt
git commit -m "fix(ecs): safe destructor + cycle-free SetParent + CollectSubtree (P0-4)"
```

---

## Task B. CommandHistory (헤더 온리, TDD)

> CommandHistory는 헤더 온리로 만들어 `molga_core`만 링크하는 테스트에서도 컴파일된다(에디터 타깃 링크 불필요).

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_command_history.cpp`:
```cpp
#include "Editor/Commands/CommandHistory.h"
#include "doctest.h"
#include <memory>

using molga::CommandHistory;
using molga::ICommand;

namespace {
struct CounterCommand : ICommand {
    int* value;
    int delta;
    CounterCommand(int* v, int d) : value(v), delta(d) {}
    void Execute() override { *value += delta; }
    void Undo() override    { *value -= delta; }
    std::string Name() const override { return "Counter"; }
};
}

TEST_CASE("Execute applies the command and makes it undoable") {
    int v = 0;
    CommandHistory h;
    h.Execute(std::make_unique<CounterCommand>(&v, 5));
    CHECK(v == 5);
    CHECK(h.CanUndo());
    CHECK_FALSE(h.CanRedo());
    h.Undo();
    CHECK(v == 0);
    CHECK(h.CanRedo());
}

TEST_CASE("Redo re-applies an undone command") {
    int v = 0;
    CommandHistory h;
    h.Execute(std::make_unique<CounterCommand>(&v, 3));
    h.Undo();
    h.Redo();
    CHECK(v == 3);
    CHECK_FALSE(h.CanRedo());
}

TEST_CASE("a new command after undo clears the redo stack") {
    int v = 0;
    CommandHistory h;
    h.Execute(std::make_unique<CounterCommand>(&v, 1));
    h.Execute(std::make_unique<CounterCommand>(&v, 1));  // v = 2
    h.Undo();                                            // v = 1
    REQUIRE(h.CanRedo());
    h.Execute(std::make_unique<CounterCommand>(&v, 10)); // v = 11
    CHECK_FALSE(h.CanRedo());
    CHECK(v == 11);
}

TEST_CASE("Undo/Redo on empty stacks are safe no-ops") {
    CommandHistory h;
    h.Undo();
    h.Redo();
    CHECK_FALSE(h.CanUndo());
    CHECK_FALSE(h.CanRedo());
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_command_history test_command_history.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_command_history -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: ICommand 인터페이스 작성**

Create `src/Editor/Commands/EditorCommand.h`:
```cpp
#pragma once

#include <string>

namespace molga {

// 되돌릴 수 있는 단일 편집 동작.
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;          // 적용 / 다시 적용(redo)
    virtual void Undo() = 0;             // 되돌리기
    virtual std::string Name() const = 0;
};

} // namespace molga
```

- [ ] **Step 4: CommandHistory 작성 (헤더 온리)**

Create `src/Editor/Commands/CommandHistory.h`:
```cpp
#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <memory>
#include <vector>

namespace molga {

// Undo/Redo 스택. Execute는 즉시 실행 후 undo 스택에 쌓고 redo 스택을 비운다.
class CommandHistory {
public:
    void Execute(std::unique_ptr<ICommand> cmd) {
        if (!cmd) return;
        cmd->Execute();
        undo_.push_back(std::move(cmd));
        redo_.clear();
    }

    void Undo() {
        if (undo_.empty()) return;
        auto cmd = std::move(undo_.back());
        undo_.pop_back();
        cmd->Undo();
        redo_.push_back(std::move(cmd));
    }

    void Redo() {
        if (redo_.empty()) return;
        auto cmd = std::move(redo_.back());
        redo_.pop_back();
        cmd->Execute();
        undo_.push_back(std::move(cmd));
    }

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    void Clear() { undo_.clear(); redo_.clear(); }

private:
    std::vector<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
};

} // namespace molga
```

- [ ] **Step 5: 테스트 통과 확인 + 커밋**

Run:
```bash
cmake --build --preset debug --target test_command_history -j4
ctest --preset debug -R test_command_history --output-on-failure
```
Expected: PASS, `4 | 4 passed`.
```bash
git add src/Editor/Commands/EditorCommand.h src/Editor/Commands/CommandHistory.h \
        tests/test_command_history.cpp tests/CMakeLists.txt
git commit -m "feat(editor): header-only CommandHistory with undo/redo tests"
```

---

## Task C. 구체 Command + Editor 통합

> 구체 Command는 에디터 상태(객체 벡터/선택/dirty)를 건드리므로 `EDITOR_SOURCES`에 둔다(단위 테스트 대상 아님 — 0-5b smoke test가 커버). `Editor`에 헬퍼 메서드를 추가해 Command가 그것을 호출한다.

- [ ] **Step 1: Editor.h에 CommandHistory + 헬퍼 추가**

`src/Editor/Editor.h` 상단 include에 추가:
```cpp
#include "Editor/Commands/CommandHistory.h"
```
public 영역에 추가(기존 `CreateGameObject`/`SetGameObjects` 근처):
```cpp
    molga::CommandHistory& GetCommandHistory() { return commandHistory; }

    // Command가 사용하는 저수준 헬퍼
    std::shared_ptr<GameObject> AddExistingObject(std::shared_ptr<GameObject> obj);
    void RemoveObjectsByIds(const std::vector<unsigned int>& ids);
    GameObject* FindObjectById(unsigned int id) const;
    void MarkSceneModified();
```
private 멤버에 추가:
```cpp
    molga::CommandHistory commandHistory;
```

- [ ] **Step 2: Editor.cpp에 헬퍼 구현**

`src/Editor/Editor.cpp`의 `Editor::CreateGameObject` 근처(파일 끝 부분)에 추가:
```cpp
std::shared_ptr<GameObject> Editor::AddExistingObject(std::shared_ptr<GameObject> obj) {
    if (!gameObjects || !obj) return nullptr;
    gameObjects->push_back(obj);
    sceneOps.MarkModified();
    return obj;
}

void Editor::RemoveObjectsByIds(const std::vector<unsigned int>& ids) {
    if (!gameObjects) return;
    gameObjects->erase(
        std::remove_if(gameObjects->begin(), gameObjects->end(),
            [&](const std::shared_ptr<GameObject>& o) {
                if (!o) return false;
                return std::find(ids.begin(), ids.end(), o->GetID()) != ids.end();
            }),
        gameObjects->end());
    sceneOps.MarkModified();
}

GameObject* Editor::FindObjectById(unsigned int id) const {
    if (!gameObjects) return nullptr;
    for (auto& o : *gameObjects) {
        if (o && o->GetID() == id) return o.get();
    }
    return nullptr;
}

void Editor::MarkSceneModified() {
    sceneOps.MarkModified();
}
```
`Editor.cpp` 상단 include에 없으면 추가:
```cpp
#include <algorithm>
```

- [ ] **Step 3: ObjectCommands 작성**

Create `src/Editor/Commands/ObjectCommands.h`:
```cpp
#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "ECS/GameObject.h"
#include <memory>
#include <string>
#include <vector>

namespace molga {

// 새 GameObject(+Transform) 생성.
class CreateObjectCommand : public ICommand {
public:
    explicit CreateObjectCommand(std::string name);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Object"; }
    GameObject* created() const { return object_.get(); }
private:
    std::string name_;
    std::shared_ptr<GameObject> object_;  // 생성물을 보관해 redo 시 재사용
    unsigned int id_ = 0;
};

// 선택 subtree 삭제(자손 포함). undo 시 shared_ptr와 부모 링크를 복원.
class DeleteObjectCommand : public ICommand {
public:
    explicit DeleteObjectCommand(GameObject* root);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Delete Object"; }
private:
    struct Saved {
        std::shared_ptr<GameObject> obj;
        unsigned int parentId;  // 0 = 부모 없음
    };
    unsigned int rootId_;
    std::vector<Saved> saved_;  // 제거된 subtree(부모 먼저)
};

// 이름 변경.
class RenameObjectCommand : public ICommand {
public:
    RenameObjectCommand(unsigned int id, std::string newName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Rename Object"; }
private:
    unsigned int id_;
    std::string newName_;
    std::string oldName_;
};

// 재부모화(드래그-드롭은 Phase 1에서 연결; Command는 지금 정의).
class ReparentObjectCommand : public ICommand {
public:
    ReparentObjectCommand(unsigned int childId, unsigned int newParentId);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Reparent Object"; }
private:
    unsigned int childId_;
    unsigned int newParentId_;
    unsigned int oldParentId_ = 0;
};

} // namespace molga
```

Create `src/Editor/Commands/ObjectCommands.cpp`:
```cpp
#include "Editor/Commands/ObjectCommands.h"
#include "Editor/Editor.h"
#include "ECS/Components/Transform.h"

namespace molga {

static unsigned int ParentIdOf(GameObject* o) {
    GameObject* p = o ? o->GetParent() : nullptr;
    return p ? p->GetID() : 0u;
}

// ── CreateObjectCommand ───────────────────────────────────────────────────────
CreateObjectCommand::CreateObjectCommand(std::string name) : name_(std::move(name)) {}

void CreateObjectCommand::Execute() {
    if (!object_) {
        object_ = std::make_shared<GameObject>(name_);
        object_->AddComponent<Transform>();
        id_ = object_->GetID();
    }
    Editor::Get().AddExistingObject(object_);
    Editor::Get().SetSelectedObject(object_.get());
}

void CreateObjectCommand::Undo() {
    if (Editor::Get().GetSelectedObject() == object_.get()) {
        Editor::Get().SetSelectedObject(nullptr);
    }
    Editor::Get().RemoveObjectsByIds({ id_ });
}

// ── DeleteObjectCommand ───────────────────────────────────────────────────────
DeleteObjectCommand::DeleteObjectCommand(GameObject* root)
    : rootId_(root ? root->GetID() : 0u) {}

void DeleteObjectCommand::Execute() {
    saved_.clear();
    GameObject* root = Editor::Get().FindObjectById(rootId_);
    if (!root) return;

    std::vector<GameObject*> subtree;
    root->CollectSubtree(subtree);  // 부모 먼저

    std::vector<unsigned int> ids;
    for (GameObject* o : subtree) {
        // 소유 shared_ptr 확보(제거 후에도 살려둔다)
        GameObject* found = Editor::Get().FindObjectById(o->GetID());
        (void)found;
        ids.push_back(o->GetID());
    }

    // 부모 링크 기록을 위해 shared_ptr를 모은다.
    for (GameObject* o : subtree) {
        unsigned int pid = ParentIdOf(o);
        // shared_ptr는 RemoveObjectsByIds 전에 잡아야 한다.
        // Editor 벡터에서 해당 shared_ptr를 찾는다.
        std::shared_ptr<GameObject> sp;
        // FindObjectById는 raw를 주므로, 소유 포인터는 별도 헬퍼로 잡는다(아래 주석 참고).
        saved_.push_back({ Editor::Get().ShareObjectById(o->GetID()), pid });
        (void)sp;
    }
    // 선택이 지워질 대상이면 해제
    if (Editor::Get().GetSelectedObject() &&
        std::find(ids.begin(), ids.end(), Editor::Get().GetSelectedObject()->GetID()) != ids.end()) {
        Editor::Get().SetSelectedObject(nullptr);
    }
    Editor::Get().RemoveObjectsByIds(ids);
}

void DeleteObjectCommand::Undo() {
    // 부모 먼저 순서이므로 그대로 다시 추가하면 부모가 자식보다 먼저 들어간다.
    for (auto& s : saved_) {
        if (s.obj) Editor::Get().AddExistingObject(s.obj);
    }
    // 부모 링크 복원
    for (auto& s : saved_) {
        if (!s.obj) continue;
        if (s.parentId != 0) {
            if (GameObject* p = Editor::Get().FindObjectById(s.parentId)) {
                s.obj->SetParent(p);
            }
        }
    }
    saved_.clear();
}

// ── RenameObjectCommand ───────────────────────────────────────────────────────
RenameObjectCommand::RenameObjectCommand(unsigned int id, std::string newName)
    : id_(id), newName_(std::move(newName)) {}

void RenameObjectCommand::Execute() {
    if (GameObject* o = Editor::Get().FindObjectById(id_)) {
        oldName_ = o->GetName();
        o->SetName(newName_);
        Editor::Get().MarkSceneModified();
    }
}

void RenameObjectCommand::Undo() {
    if (GameObject* o = Editor::Get().FindObjectById(id_)) {
        o->SetName(oldName_);
        Editor::Get().MarkSceneModified();
    }
}

// ── ReparentObjectCommand ─────────────────────────────────────────────────────
ReparentObjectCommand::ReparentObjectCommand(unsigned int childId, unsigned int newParentId)
    : childId_(childId), newParentId_(newParentId) {}

void ReparentObjectCommand::Execute() {
    GameObject* child = Editor::Get().FindObjectById(childId_);
    if (!child) return;
    oldParentId_ = ParentIdOf(child);
    GameObject* np = newParentId_ ? Editor::Get().FindObjectById(newParentId_) : nullptr;
    child->SetParent(np);
    Editor::Get().MarkSceneModified();
}

void ReparentObjectCommand::Undo() {
    GameObject* child = Editor::Get().FindObjectById(childId_);
    if (!child) return;
    GameObject* op = oldParentId_ ? Editor::Get().FindObjectById(oldParentId_) : nullptr;
    child->SetParent(op);
    Editor::Get().MarkSceneModified();
}

} // namespace molga
```

> 위 `DeleteObjectCommand`는 소유 `shared_ptr`를 잡기 위해 `Editor::ShareObjectById(id)`를 사용한다. 다음 Step에서 이 헬퍼를 Editor에 추가한다.

- [ ] **Step 4: Editor에 ShareObjectById 추가**

`src/Editor/Editor.h` public에 추가:
```cpp
    std::shared_ptr<GameObject> ShareObjectById(unsigned int id) const;
```
`src/Editor/Editor.cpp`에 구현 추가:
```cpp
std::shared_ptr<GameObject> Editor::ShareObjectById(unsigned int id) const {
    if (!gameObjects) return nullptr;
    for (auto& o : *gameObjects) {
        if (o && o->GetID() == id) return o;   // 소유 shared_ptr 복사
    }
    return nullptr;
}
```
이제 `DeleteObjectCommand::Execute`에서 `ids`를 모으는 첫 루프의 임시 코드를 단순화한다. 위 cpp의 `Execute`에서 `ids`/`saved_`를 모으는 두 루프를 다음 한 루프로 교체한다:
```cpp
    std::vector<unsigned int> ids;
    for (GameObject* o : subtree) {
        ids.push_back(o->GetID());
        saved_.push_back({ Editor::Get().ShareObjectById(o->GetID()), ParentIdOf(o) });
    }
```

- [ ] **Step 5: EDITOR_SOURCES에 ObjectCommands.cpp 추가 + 빌드**

`CMakeLists.txt`의 `set(EDITOR_SOURCES ...)`에 추가:
```cmake
    src/Editor/Commands/ObjectCommands.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target molga_engine -j4
```
Expected: 빌드 성공.

- [ ] **Step 6: 커밋**

```bash
git add src/Editor/Commands/ObjectCommands.h src/Editor/Commands/ObjectCommands.cpp \
        src/Editor/Editor.h src/Editor/Editor.cpp CMakeLists.txt
git commit -m "feat(editor): create/delete/rename/reparent commands with dirty tracking"
```

---

## Task D. HierarchyWindow + 메뉴 + dirty 표시 연결

- [ ] **Step 1: HierarchyWindow가 Command를 사용하도록 수정**

`src/Editor/Windows/HierarchyWindow.cpp` 상단 include에 추가:
```cpp
#include "Editor/Editor.h"
#include "Editor/Commands/ObjectCommands.h"
```
`CreateEmptyGameObject()`(현재 `:150-161`)를 다음으로 교체:
```cpp
void HierarchyWindow::CreateEmptyGameObject() {
    auto cmd = std::make_unique<molga::CreateObjectCommand>("New GameObject");
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    // 선택/콜백은 Command가 Editor::SetSelectedObject로 처리한다.
    selectedObject = Editor::Get().GetSelectedObject();
}
```
`DeleteSelectedObject()`(현재 `:163-179`)를 다음으로 교체:
```cpp
void HierarchyWindow::DeleteSelectedObject() {
    if (!selectedObject) return;
    auto cmd = std::make_unique<molga::DeleteObjectCommand>(selectedObject);
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    selectedObject = Editor::Get().GetSelectedObject();  // Command가 nullptr로 비웠음
    if (onSelectionChanged) onSelectionChanged(selectedObject);
}
```
이름 변경 확정 부분(현재 `:94` 부근 `obj->SetName(renameBuffer);`)을 다음으로 교체:
```cpp
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::RenameObjectCommand>(obj->GetID(), std::string(renameBuffer)));
```

> `DuplicateSelectedObject`는 이번 슬라이스에서 구조만 유지하되 dirty는 표시하도록 마지막에 `Editor::Get().MarkSceneModified();` 한 줄을 추가한다(완전한 Duplicate Command는 Phase 1).

- [ ] **Step 2: Undo/Redo 메뉴 연결**

`src/Editor/Editor.cpp`의 Undo/Redo 메뉴(현재 `:166-171`)
```cpp
    if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
        Log::Warn("Editor", "Undo is not yet implemented");
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
        Log::Warn("Editor", "Redo is not yet implemented");
    }
```
를 다음으로 교체:
```cpp
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, commandHistory.CanUndo())) {
        commandHistory.Undo();
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, commandHistory.CanRedo())) {
        commandHistory.Redo();
    }
```

- [ ] **Step 3: dirty(`*`) 표시를 메뉴 바에 추가**

`src/Editor/Editor.cpp`의 메인 메뉴 바 렌더(메뉴 항목들을 그린 뒤, `EndMainMenuBar` 직전)에 추가:
```cpp
    if (sceneOps.IsModified()) {
        ImGui::TextDisabled("  *unsaved");
    }
```
> 정식 창 제목 `*` 표시와 미저장 확인 모달은 Phase 1(파일 dialog·단축키)과 함께 완성한다.

- [ ] **Step 4: 빌드 + 수동 검증**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 빌드 성공, 모든 테스트 PASS.

수동 검증(에디터 실행): 오브젝트 생성 → 메뉴 바에 `*unsaved` 표시 → Undo로 사라지는지, Hierarchy에서 삭제 후 Undo로 복원되는지 확인.

- [ ] **Step 5: 커밋**

```bash
git add src/Editor/Windows/HierarchyWindow.cpp src/Editor/Editor.cpp
git commit -m "feat(editor): route hierarchy edits through commands; wire Undo/Redo + dirty marker"
```

---

## 작업 완료 기준

- [ ] 부모 삭제·자식 삭제·계층 전체 삭제·재부모화·cycle 거부 테스트(`test_hierarchy`)가 Debug와 **asan**에서 통과한다.
- [ ] `CommandHistory`의 undo/redo 스택 동작(`test_command_history`)이 통과한다.
- [ ] Hierarchy의 생성/삭제/이름 변경이 Undo/Redo되고, 삭제는 subtree 전체를 안전하게 제거한 뒤 Undo로 복원된다.
- [ ] 생성·삭제·이름 변경·재부모화·복제가 모두 dirty(`*unsaved`)를 갱신한다.
- [ ] Hierarchy 조작 후 ASan 실행에 use-after-free가 없다.

## 다음 작업

[task-0-2_scene_document_and_play_world.md](task-0-2_scene_document_and_play_world.md) — 안전해진 GameObject 위에 `World`/`SceneDocument` 단일 모델을 세우고 Play 스냅샷/복원을 구현한다.
