# Task 0-2: 단일 World/SceneDocument 모델 + Play 스냅샷/Stop 복원

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 선행: [task-0-5a](task-0-5a_test_framework.md), [task-0-1](task-0-1_renderer_contract.md), [task-0-4](task-0-4_hierarchy_and_commands.md). 체크박스로 추적.

**Goal:** 편집 씬, Play 씬, 런타임 씬을 하나의 데이터 모델(`World`)로 통일한다. Play 진입 시 현재 씬을 직렬화로 복제한 `PlayWorld`를 실행하고, Stop 시 폐기해 편집 상태를 그대로 복원한다. 하드코딩 `MenuScene/GameScene`는 엔진 실행 흐름에서 제거한다.

**Architecture:** `World`(molga_core)가 오브젝트 벡터와 명시적 업데이트 순서(Start→Fixed→Update→Late)와 직렬화 기반 `Clone()`을 소유한다. 에디터는 `SceneDocument`(헤더 온리)가 `editWorld` + `playWorld`를 들고 `EnterPlay/ExitPlay`를 제공한다. 에디터·런타임 둘 다 동일한 `World::LoadFromFile`/`SceneSerializer`를 쓴다.

**Tech Stack:** C++17, nlohmann/json, doctest

**닫는 결함:** 갭 분석 P0-2 (`docs/plan/2026-06-06_project_gap_analysis.md` §3 P0-2, §4.1)

---

## 현재 상태 (검증된 사실)

- **세 개의 분리된 모델:** ① 편집 `std::vector<std::shared_ptr<GameObject>> editorObjects`(`main.cpp:60`), ② 하드코딩 `SceneManager` + `MenuScene/GameScene`(`Core/Scene.{h,cpp}`, `Scenes/`), ③ 런타임 `gameObjects`(`runtime_main.cpp:84`).
- main.cpp는 프로젝트를 연 뒤에도 샘플 Player/Enemy/Ground를 강제 생성(`main.cpp:108-136`)하고 `MenuScene/GameScene`를 등록(`:138-141`)한다. Play 모드에서 `editorObjects`를 업데이트하지만 렌더는 `SceneManager::Render`로 샘플 씬을 표시(`:194-211`).
- `EditorState::Play/Stop`은 씬 저장·복원을 TODO로 둠(`EditorState.cpp:30,46`). EditorState는 `currentMode`/`timeScale`만 가짐(`EditorState.h`).
- `SceneSerializer`는 `SaveScene/LoadScene`(파일 전용)과 `SerializeGameObject/DeserializeGameObject`(문자열)만 제공. **메모리 단위 전체-씬 직렬화 없음**(`SceneSerializer.h:9-24`). `LoadScene`은 시작에 `objects.clear()`(`SceneSerializer.cpp:86`).
- `GameObject`: `Update`(전 컴포넌트), `FixedUpdateScripts`, `LateUpdateScripts`(**호출처 0**), `Script::Start`/`MarkStarted`(**호출처 0**). 즉 어떤 진입점도 스크립트 `Start()`/LateUpdate를 돌리지 않는다.
- `Time` 고정 스텝 API: `AccumulateFixedTime/HasPendingFixedStep/ConsumeFixedStep/ResetFixedAccumulator`, `fixedDeltaTime = 0.02`(50Hz).
- `Transform`: `Vector2 GetPosition()`, `float GetX()/GetY()`, `Serialize/Deserialize` 구현됨(`Transform.h`).

---

## 파일 구조

**Files:**
- Modify: `src/Core/SceneSerializer.h` (`SerializeScene`/`DeserializeScene` 메모리 API)
- Modify: `src/Core/SceneSerializer.cpp` (SaveScene/LoadScene를 위 API로 재구성)
- Modify: `src/ECS/GameObject.h` / `src/ECS/GameObject.cpp` (`StartScripts()` 추가)
- Create: `src/Core/World.h` / `src/Core/World.cpp`
- Create: `src/Editor/SceneDocument.h` (헤더 온리)
- Create: `tests/test_world.cpp`
- Modify: `src/main.cpp` (editorObjects/SceneManager 제거 → SceneDocument, 단일 렌더 경로)
- Modify: `src/Editor/EditorState.h` / `EditorState.cpp` (Play/Stop 전이 콜백)
- Modify: `src/runtime_main.cpp` (World + 공용 로딩)
- Modify: `CMakeLists.txt` (`World.cpp` 추가, `Scenes/*` 제거)
- Move: `src/Scenes/` → `examples/scenes_sample/` (빌드에서 제외)
- Modify: `tests/CMakeLists.txt` (test_world 등록)

---

## Task A. SceneSerializer 메모리 단위 직렬화 (리팩터)

> 동작은 동일하되, 파일 I/O와 직렬화 로직을 분리해 메모리 복제를 가능하게 한다. 기존 `test_scene_serializer`가 계속 통과하면 회귀 없음.

- [ ] **Step 1: SceneSerializer.h에 메모리 API 추가**

`src/Core/SceneSerializer.h` 상단에 추가:
```cpp
#include <nlohmann/json.hpp>
```
클래스 안 `SaveScene` 선언 위에 추가:
```cpp
    // 메모리 직렬화 (스냅샷/복제용)
    static nlohmann::json SerializeScene(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        const std::string& sceneName);
    static bool DeserializeScene(
        const nlohmann::json& doc,
        std::vector<std::shared_ptr<GameObject>>& objects);
```

- [ ] **Step 2: SceneSerializer.cpp 재구성**

`src/Core/SceneSerializer.cpp`에서 `SaveScene` 본문(현재 `:16-66`)을 다음으로 교체:
```cpp
nlohmann::json SceneSerializer::SerializeScene(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& sceneName) {
    json sceneJson;
    sceneJson["version"] = "1.0";
    sceneJson["name"] = sceneName;

    json objectsArray = json::array();
    for (const auto& obj : objects) {
        if (!obj) continue;
        json objJson;
        objJson["name"] = obj->GetName();
        objJson["id"] = obj->GetID();
        objJson["active"] = obj->IsActive();
        objJson["parentId"] = obj->GetParent()
            ? static_cast<int>(obj->GetParent()->GetID()) : -1;

        json componentsArray = json::array();
        for (auto* comp : obj->GetComponents()) {
            if (!comp) continue;
            json compJson;
            compJson["type"] = comp->GetTypeName();
            compJson["enabled"] = comp->IsEnabled();
            comp->Serialize(compJson);
            componentsArray.push_back(compJson);
        }
        objJson["components"] = componentsArray;
        objectsArray.push_back(objJson);
    }
    sceneJson["gameObjects"] = objectsArray;
    return sceneJson;
}

bool SceneSerializer::SaveScene(const std::string& filepath,
                                 const std::vector<std::shared_ptr<GameObject>>& objects) {
    json sceneJson = SerializeScene(objects, "Untitled Scene");
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open file for writing: " << filepath << std::endl;
        return false;
    }
    file << sceneJson.dump(2);
    file.close();
    std::cout << "[SceneSerializer] Scene saved to: " << filepath << std::endl;
    return true;
}
```
`LoadScene` 본문(현재 `:68-160`)을 다음으로 교체:
```cpp
bool SceneSerializer::DeserializeScene(
    const nlohmann::json& sceneJson,
    std::vector<std::shared_ptr<GameObject>>& objects) {
    objects.clear();

    if (!sceneJson.contains("gameObjects")) {
        std::cerr << "[SceneSerializer] No gameObjects in scene document" << std::endl;
        return false;
    }

    auto& factory = ComponentFactory::Get();
    struct LoadedObject { std::shared_ptr<GameObject> obj; int parentId; };
    std::vector<LoadedObject> loaded;

    for (const auto& objJson : sceneJson["gameObjects"]) {
        std::string name = objJson.value("name", "GameObject");
        bool active = objJson.value("active", true);
        int parentId = objJson.value("parentId", -1);

        auto obj = std::make_shared<GameObject>(name);
        if (objJson.contains("id")) {
            obj->SetID(objJson["id"].get<unsigned int>());
        }
        obj->SetActive(active);

        if (objJson.contains("components")) {
            for (const auto& compJson : objJson["components"]) {
                std::string type = compJson.value("type", "");
                Component* comp = factory.Create(type, obj.get());
                if (!comp) {
                    auto script = ScriptManager::Get().CreateScript(type);
                    if (script) comp = obj->AddComponentRaw(script.release());
                }
                if (comp) {
                    comp->Deserialize(compJson);
                    if (compJson.contains("enabled"))
                        comp->SetEnabled(compJson["enabled"].get<bool>());
                } else {
                    std::cerr << "[SceneSerializer] Unknown component type: " << type << std::endl;
                }
            }
        }
        loaded.push_back({obj, parentId});
        objects.push_back(obj);
    }

    std::unordered_map<unsigned int, GameObject*> idMap;
    for (auto& [obj, _] : loaded) idMap[obj->GetID()] = obj.get();
    for (auto& [obj, parentId] : loaded) {
        if (parentId >= 0) {
            auto it = idMap.find(static_cast<unsigned int>(parentId));
            if (it != idMap.end()) obj->SetParent(it->second);
        }
    }
    return true;
}

bool SceneSerializer::LoadScene(const std::string& filepath,
                                 std::vector<std::shared_ptr<GameObject>>& objects) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open file: " << filepath << std::endl;
        return false;
    }
    json sceneJson;
    try {
        file >> sceneJson;
    } catch (const json::parse_error& e) {
        std::cerr << "[SceneSerializer] JSON parse error: " << e.what() << std::endl;
        return false;
    }
    file.close();

    bool ok = DeserializeScene(sceneJson, objects);
    if (ok) {
        std::cout << "[SceneSerializer] Scene loaded from: " << filepath
                  << " (" << objects.size() << " objects)" << std::endl;
    }
    return ok;
}
```
> `<unordered_map>` 헤더가 필요하면 상단 include에 추가한다(기존 코드가 이미 사용 중일 수 있음 — 컴파일 에러 시 추가).

- [ ] **Step 3: 회귀 없음 확인 + 커밋**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug -R test_scene_serializer --output-on-failure
```
Expected: `test_scene_serializer` PASS(기존 라운드트립 유지).
```bash
git add src/Core/SceneSerializer.h src/Core/SceneSerializer.cpp
git commit -m "refactor(core): split SceneSerializer into in-memory Serialize/DeserializeScene"
```

---

## Task B. GameObject::StartScripts + World (TDD)

- [ ] **Step 1: GameObject에 StartScripts 추가**

`src/ECS/GameObject.h`의 `void LateUpdateScripts(float dt);` 아래에 추가:
```cpp
    // 아직 시작 안 한 스크립트의 Start()를 1회 호출
    void StartScripts();
```
`src/ECS/GameObject.cpp`의 `LateUpdateScripts` 정의 아래에 추가:
```cpp
void GameObject::StartScripts() {
    if (!active) return;
    for (auto& [id, comp] : componentMap) {
        if (!comp->IsEnabled()) continue;
        if (auto* s = dynamic_cast<Script*>(comp.get())) {
            if (!s->HasStarted()) {
                s->Start();
                s->MarkStarted();
            }
        }
    }
}
```
> `Script.h`는 `FixedUpdateScripts`가 이미 `dynamic_cast<Script*>`를 쓰므로 포함되어 있다.

- [ ] **Step 2: 실패하는 World 테스트 작성**

Create `tests/test_world.cpp`:
```cpp
#include "Core/World.h"
#include "Editor/SceneDocument.h"
#include "ECS/Components/Transform.h"
#include "doctest.h"
#include <memory>

TEST_CASE("World::Clone is an independent deep copy") {
    World w;
    auto go = std::make_shared<GameObject>("Player");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(10.0f, 20.0f);
    w.Add(go);

    auto clone = w.Clone();
    REQUIRE(clone->Objects().size() == 1);
    auto* ct = clone->Objects()[0]->GetComponent<Transform>();
    REQUIRE(ct != nullptr);
    CHECK(ct->GetX() == doctest::Approx(10.0f));
    CHECK(clone->Objects()[0]->GetName() == "Player");

    ct->SetPosition(99.0f, 99.0f);            // mutate clone
    CHECK(t->GetX() == doctest::Approx(10.0f)); // original untouched
}

TEST_CASE("World::FindById locates and rejects") {
    World w;
    auto a = std::make_shared<GameObject>("A");
    w.Add(a);
    CHECK(w.FindById(a->GetID()) == a.get());
    CHECK(w.FindById(0) == nullptr);
}

TEST_CASE("SceneDocument restores edit world after Play/Stop") {
    SceneDocument doc;
    auto go = std::make_shared<GameObject>("Mover");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(5.0f, 5.0f);
    doc.EditWorld().Add(go);

    doc.EnterPlay();
    REQUIRE(doc.IsPlaying());
    auto* pt = doc.ActiveWorld().Objects()[0]->GetComponent<Transform>();
    REQUIRE(pt != nullptr);
    pt->SetPosition(123.0f, 456.0f);          // gameplay mutates the play copy
    CHECK(pt->GetX() == doctest::Approx(123.0f));

    doc.ExitPlay();
    CHECK_FALSE(doc.IsPlaying());
    CHECK(t->GetX() == doctest::Approx(5.0f)); // edit world unchanged
}
```

- [ ] **Step 3: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_world test_world.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_world -j4
```
Expected: FAIL — `World.h`/`SceneDocument.h` 없음.

- [ ] **Step 4: World.h 작성**

Create `src/Core/World.h`:
```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>

class GameObject;

// 편집/플레이/런타임이 공유하는 단일 씬 데이터 모델.
class World {
public:
    World() = default;

    GameObject* Add(std::shared_ptr<GameObject> obj);
    GameObject* FindById(unsigned int id) const;
    void Clear();

    std::vector<std::shared_ptr<GameObject>>& Objects() { return objects_; }
    const std::vector<std::shared_ptr<GameObject>>& Objects() const { return objects_; }

    const std::string& Name() const { return name_; }
    void SetName(const std::string& n) { name_ = n; }

    // 명시적 업데이트 순서
    void StartPending();             // 미시작 스크립트 Start()
    void FixedStep(float fixedDt);   // 스크립트 FixedUpdate
    void Update(float dt);           // 전 컴포넌트 Update
    void LateUpdate(float dt);       // 스크립트 LateUpdate

    // 직렬화 기반 독립 복제
    std::unique_ptr<World> Clone() const;

    // 공용 로드/세이브
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

private:
    std::vector<std::shared_ptr<GameObject>> objects_;
    std::string name_ = "Untitled";
};
```

- [ ] **Step 5: World.cpp 작성**

Create `src/Core/World.cpp`:
```cpp
#include "Core/World.h"
#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"

GameObject* World::Add(std::shared_ptr<GameObject> obj) {
    if (!obj) return nullptr;
    GameObject* raw = obj.get();
    objects_.push_back(std::move(obj));
    return raw;
}

GameObject* World::FindById(unsigned int id) const {
    for (const auto& o : objects_) {
        if (o && o->GetID() == id) return o.get();
    }
    return nullptr;
}

void World::Clear() { objects_.clear(); }

void World::StartPending() {
    for (auto& o : objects_) if (o) o->StartScripts();
}
void World::FixedStep(float fixedDt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->FixedUpdateScripts(fixedDt);
}
void World::Update(float dt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->Update(dt);
}
void World::LateUpdate(float dt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->LateUpdateScripts(dt);
}

std::unique_ptr<World> World::Clone() const {
    auto copy = std::make_unique<World>();
    nlohmann::json doc = SceneSerializer::SerializeScene(objects_, name_);
    SceneSerializer::DeserializeScene(doc, copy->objects_);
    copy->name_ = name_;
    return copy;
}

bool World::LoadFromFile(const std::string& path) {
    return SceneSerializer::LoadScene(path, objects_);
}
bool World::SaveToFile(const std::string& path) const {
    return SceneSerializer::SaveScene(path, objects_);
}
```

- [ ] **Step 6: SceneDocument.h 작성 (헤더 온리)**

Create `src/Editor/SceneDocument.h`:
```cpp
#pragma once

#include "Core/World.h"
#include <memory>
#include <string>

// 에디터가 편집 중인 씬 1개를 소유한다.
// editWorld는 권위 있는 사본, playWorld는 Play 중에만 존재하는 휘발 사본.
class SceneDocument {
public:
    World& EditWorld() { return editWorld_; }
    const World& EditWorld() const { return editWorld_; }

    World& ActiveWorld() { return playWorld_ ? *playWorld_ : editWorld_; }
    bool IsPlaying() const { return playWorld_ != nullptr; }

    void EnterPlay() {
        playWorld_ = editWorld_.Clone();
        playWorld_->StartPending();
    }
    void ExitPlay() { playWorld_.reset(); }

    const std::string& Path() const { return path_; }
    void SetPath(std::string p) { path_ = std::move(p); }

private:
    World editWorld_;
    std::unique_ptr<World> playWorld_;
    std::string path_;
};
```

- [ ] **Step 7: ENGINE_SOURCES에 World.cpp 추가**

`CMakeLists.txt`의 `set(ENGINE_SOURCES ...)`에 추가:
```cmake
    src/Core/World.cpp
```

- [ ] **Step 8: 테스트 통과 + 커밋**

Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_world -j4
ctest --preset debug -R test_world --output-on-failure
```
Expected: PASS, `3 | 3 passed`. (특히 세 번째 케이스가 Play/Stop 복원을 증명한다.)
```bash
git add src/Core/World.h src/Core/World.cpp src/Editor/SceneDocument.h \
        src/ECS/GameObject.h src/ECS/GameObject.cpp tests/test_world.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(core): World single model + SceneDocument play snapshot/restore"
```

---

## Task C. 에디터 메인 루프를 SceneDocument로 전환

> task-0-1이 이미 편집 렌더를 `RenderPass`로 바꿔두었다. 이제 데이터 소스를 `editorObjects`/`SceneManager`에서 `SceneDocument`로 바꾸고, edit/play 렌더 분기를 하나로 합친다. **수정 전 `src/main.cpp`를 처음부터 끝까지 읽어** 현재 구조를 파악한다.

- [ ] **Step 1: 샘플 오브젝트 + SceneManager 등록 제거**

`src/main.cpp`에서:
- 샘플 Player/Enemy/Ground 생성 블록(현재 `:108-136`, `// Create sample GameObjects for editor demo { ... }` 전체)을 **삭제**.
- SceneManager 등록 블록(현재 `:138-141`)
  ```cpp
        SceneManager::AddScene("Menu", std::make_shared<MenuScene>());
        SceneManager::AddScene("Game", std::make_shared<GameScene>());
        SceneManager::ChangeScene("Menu");
  ```
  을 **삭제**.
- 상단의 `#include "Scenes/MenuScene.h"`, `#include "Scenes/GameScene.h"`, `#include "Core/Scene.h"`(SceneManager) include를 **삭제**.

- [ ] **Step 2: editorObjects를 SceneDocument로 교체**

`src/main.cpp` 상단 include에 추가:
```cpp
#include "Editor/SceneDocument.h"
#include "Editor/EditorState.h"
```
`std::vector<std::shared_ptr<GameObject>> editorObjects;`(현재 `:60`)를 다음으로 교체:
```cpp
    SceneDocument sceneDoc;
```
`Editor::Get().SetGameObjects(&editorObjects);`(현재 `:70`)를 다음으로 교체:
```cpp
    Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
```
파일 끝의 `editorObjects.clear();`(현재 `:227`)를 **삭제**(SceneDocument 소멸 시 정리됨).

- [ ] **Step 3: 업데이트 루프를 ActiveWorld 기반으로 교체**

업데이트 블록(현재 `:169-192`, `if (editorState.IsPlayMode()) { ... }`)을 다음으로 교체:
```cpp
            // Play 모드에서만 ActiveWorld(=playWorld)를 시뮬레이션한다.
            if (editorState.IsPlayMode() && sceneDoc.IsPlaying()) {
                float scaledDt = dt * editorState.GetTimeScale();

                Time::AccumulateFixedTime(scaledDt);
                while (Time::HasPendingFixedStep()) {
                    sceneDoc.ActiveWorld().FixedStep(Time::GetFixedDeltaTime());
                    Time::ConsumeFixedStep();
                }
                sceneDoc.ActiveWorld().Update(scaledDt);
                sceneDoc.ActiveWorld().LateUpdate(scaledDt);
            }
```

- [ ] **Step 4: 렌더 분기를 단일 경로로 교체**

렌더 블록(현재 `:194-211`, `if (editorState.IsEditMode()) { ... } else { SceneManager::Render(...); }`)을 다음으로 교체:
```cpp
            // 편집이든 플레이든 ActiveWorld를 동일 경로로 렌더한다.
            renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
            {
                molga::RenderPass pass(*renderer, shader.get(), camera.get());
                for (auto& obj : sceneDoc.ActiveWorld().Objects()) {
                    if (obj && obj->IsActive()) {
                        if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                            sr->RenderSprite(renderer.get());
                        }
                    }
                }
            }
```
> `SceneManager::Update(...)` 호출이 업데이트 블록에 남아 있었다면 그것도 삭제한다. 이제 main.cpp는 `SceneManager`를 전혀 참조하지 않는다.

- [ ] **Step 5: 빌드 (EditorState 콜백 미연결 — 다음 Task에서)**

Run:
```bash
cmake --build --preset debug --target molga_engine -j4
```
Expected: 성공(또는 `SceneManager`/`MenuScene` 미정의 링크 에러가 남으면 Step 1 include/호출 누락을 점검). Play 버튼은 아직 playWorld를 만들지 않으므로 Play 시 빈 화면 — 다음 Task에서 연결.

---

## Task D. EditorState Play/Stop 전이 콜백

- [ ] **Step 1: EditorState.h에 콜백 추가**

`src/Editor/EditorState.h` 상단에 추가:
```cpp
#include <functional>
```
public에 추가:
```cpp
    using PlayTransition = std::function<void()>;
    void SetPlayCallbacks(PlayTransition onEnterPlay, PlayTransition onExitPlay) {
        onEnterPlay_ = std::move(onEnterPlay);
        onExitPlay_ = std::move(onExitPlay);
    }
```
private 멤버에 추가:
```cpp
    PlayTransition onEnterPlay_;
    PlayTransition onExitPlay_;
```

- [ ] **Step 2: EditorState.cpp에서 콜백 호출**

`src/Editor/EditorState.cpp`의 `Play()`(현재 `:28-37`)에서
```cpp
        // TODO: Save scene state before playing
        Time::ResetFixedAccumulator();
        SetMode(EditorMode::Play);
```
를 다음으로 교체:
```cpp
        Time::ResetFixedAccumulator();
        if (onEnterPlay_) onEnterPlay_();
        SetMode(EditorMode::Play);
```
`Stop()`(현재 `:41-47`)에서
```cpp
        // TODO: Restore scene state after stopping
        SetMode(EditorMode::Edit);
```
를 다음으로 교체:
```cpp
        if (onExitPlay_) onExitPlay_();
        SetMode(EditorMode::Edit);
```

- [ ] **Step 3: main.cpp에서 콜백 등록**

`src/main.cpp`의 `Editor::Get().SetGameObjects(...)` 호출 근처(에디터 초기화 직후)에 추가:
```cpp
    editorState.SetPlayCallbacks(
        [&sceneDoc]() {  // Edit → Play
            Editor::Get().SetSelectedObject(nullptr);
            Editor::Get().GetCommandHistory().Clear();
            sceneDoc.EnterPlay();
            Editor::Get().SetGameObjects(&sceneDoc.ActiveWorld().Objects());
        },
        [&sceneDoc]() {  // Play/Pause → Stop
            Editor::Get().SetSelectedObject(nullptr);
            Editor::Get().GetCommandHistory().Clear();
            Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
            sceneDoc.ExitPlay();
        });
```
> 선택을 먼저 nullptr로 비우는 이유: playWorld 폐기 시 Hierarchy/Inspector의 raw 선택 포인터가 dangling되지 않게 하기 위함이다. Command 히스토리도 Play 경계에서 비운다(경계를 넘는 Undo는 의미가 모호함).

- [ ] **Step 4: 빌드 + 수동 검증**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 빌드/테스트 통과.

수동 검증: 에디터에서 오브젝트 생성 → Play → (스크립트가 있으면) 움직임 → Stop → 편집 위치 그대로. Play 중 Hierarchy는 playWorld를 보여주고, Stop 후 editWorld로 돌아온다.

- [ ] **Step 5: 커밋**

```bash
git add src/main.cpp src/Editor/EditorState.h src/Editor/EditorState.cpp
git commit -m "feat(editor): SceneDocument-driven loop with Play snapshot / Stop restore (P0-2)"
```

---

## Task E. 런타임을 World + 공용 로딩으로 전환

- [ ] **Step 1: runtime_main.cpp를 World 기반으로 수정**

`src/runtime_main.cpp`를 읽고:
- 상단 include에 `#include "Core/World.h"` 추가.
- `std::vector<std::shared_ptr<GameObject>> gameObjects;`(현재 `:84`)와 `SceneSerializer::LoadScene(config.mainScene, gameObjects)`(현재 `:94`) 부분을 다음으로 교체:
  ```cpp
      World world;
      if (!world.LoadFromFile(config.mainScene)) {
          std::cerr << "[Runtime] Failed to load main scene: " << config.mainScene << std::endl;
      }
      world.StartPending();
  ```
- 고정/가변 업데이트 루프(현재 `FixedUpdateScripts`를 직접 도는 부분, `:102-120` 부근)를 다음으로 교체:
  ```cpp
          Time::AccumulateFixedTime(dt);
          while (Time::HasPendingFixedStep()) {
              world.FixedStep(Time::GetFixedDeltaTime());
              Time::ConsumeFixedStep();
          }
          world.Update(dt);
          world.LateUpdate(dt);
  ```
- 렌더 루프(task-0-1에서 `RenderPass`로 바꾼 `for (auto& obj : gameObjects)`)의 `gameObjects`를 `world.Objects()`로 바꾼다:
  ```cpp
          {
              molga::RenderPass pass(*renderer, shader.get(), camera.get());
              for (auto& obj : world.Objects()) {
                  if (obj && obj->IsActive()) {
                      if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                          sr->RenderSprite(renderer.get());
                      }
                  }
              }
          }
  ```

- [ ] **Step 2: 빌드 + 커밋**

Run:
```bash
cmake --build --preset debug --target molga_runtime -j4
```
Expected: 성공.
```bash
git add src/runtime_main.cpp
git commit -m "refactor(runtime): drive the runtime through World + shared scene loading"
```

---

## Task F. 하드코딩 샘플 씬을 빌드에서 제거

- [ ] **Step 1: 샘플 씬을 examples로 이동**

Run:
```bash
mkdir -p examples
git mv src/Scenes examples/scenes_sample
```

- [ ] **Step 2: CMake에서 Scenes 소스 제거**

`CMakeLists.txt`의 `set(EDITOR_SOURCES ...)`(또는 ENGINE_SOURCES)에서 다음 두 줄을 **삭제**:
```cmake
    src/Scenes/GameScene.cpp
    src/Scenes/MenuScene.cpp
```
> `src/Core/Scene.cpp`(SceneManager)는 더 이상 누구도 참조하지 않지만, 컴파일은 되므로 그대로 둔다(후속 정리 대상). 만약 `Scene.cpp`가 `Scenes/`를 include한다면 함께 제거하거나 빈 stub으로 만든다.

- [ ] **Step 3: 전체 빌드 + 테스트 + 커밋**

Run:
```bash
cmake --preset debug
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 빌드 성공, 모든 테스트 PASS.
```bash
git add -A
git commit -m "chore: move sample MenuScene/GameScene out of the engine build"
```

---

## 검증 시나리오 (갭 분석 §8 Task 0-2)

> 아래는 `test_world`의 세 번째 케이스로 자동 검증되며, 에디터에서 수동으로도 확인한다.

1. 편집 씬 Transform 값을 기록한다. → `editWorld`의 오브젝트.
2. Play 중 Script로 값을 변경한다. → `playWorld`(클론)만 바뀐다.
3. Stop 후 원래 값이 복원되는지 확인한다. → `ExitPlay()`로 playWorld 폐기, editWorld 그대로.
4. 같은 씬을 runtime에서 실행해 동일 초기 상태를 확인한다. → runtime이 동일 `World::LoadFromFile` 사용(0-3에서 경로 신뢰성 확보 후 빌드 결과로 최종 확인).

---

## 작업 완료 기준

- [ ] 편집 씬에 추가한 오브젝트가 Play에서 그대로 실행되고, Play 중 변경이 Stop 후 복원된다(`test_world` 통과).
- [ ] 에디터와 런타임이 동일한 `World`/`SceneSerializer` 로딩 코드를 사용한다.
- [ ] main.cpp가 `SceneManager`/`MenuScene`/`GameScene`/샘플 오브젝트를 더 이상 참조하지 않고, 편집·플레이가 단일 렌더 경로를 쓴다.
- [ ] 스크립트 `Start()`와 `LateUpdate`가 실제로 호출된다(`World::StartPending`/`LateUpdate`).

## 다음 작업

[task-0-3_path_service_and_build.md](task-0-3_path_service_and_build.md) — `PathService`로 모든 경로를 절대화하고, 프로젝트 에셋/셰이더/씬을 신뢰성 있게 빌드하며, 런타임 텍스처 resolve(P0-5)를 완성한다.
