#include "Editor/Editor.h"
#include "Editor/Commands/CommandHistory.h"
#include "Editor/Commands/SceneSnapshots.h"
#include "Editor/Commands/PropertyCommands.h"
#include "Editor/Commands/ComponentCommands.h"
#include "Editor/Commands/ObjectCommands.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Component.h"
#include "doctest.h"
#include <memory>
#include <vector>
#include <algorithm>

// --- Mock Editor Implementation ---

static std::vector<std::shared_ptr<GameObject>>* s_gameObjects = nullptr;
static GameObject* s_selectedObject = nullptr;
static bool s_sceneModified = false;

namespace {

class SnapshotSelfRemovingComponent : public Component {
public:
    COMPONENT_TYPE(SnapshotSelfRemovingComponent)

    void OnDisable() override {
        if (GameObject* owner = GetGameObject()) {
            owner->RemoveComponent<SnapshotSelfRemovingComponent>();
        }
    }

    void ResolveAssets() override { ++resolveCount; }

    static inline int resolveCount = 0;
};

class SnapshotCaptureVictim : public Component {
public:
    COMPONENT_TYPE(SnapshotCaptureVictim)
};

class SnapshotCaptureMutator : public Component {
public:
    COMPONENT_TYPE(SnapshotCaptureMutator)

    void Serialize(nlohmann::json&) const override {
        if (GameObject* owner = GetGameObject()) {
            owner->RemoveComponent<SnapshotCaptureVictim>();
        }
    }
};

class SnapshotRemovalVictim : public Component {
public:
    COMPONENT_TYPE(SnapshotRemovalVictim)
};

class SnapshotRemovalMutator : public Component {
public:
    COMPONENT_TYPE(SnapshotRemovalMutator)

    void OnDisable() override {
        if (GameObject* owner = GetGameObject()) {
            owner->RemoveComponent<SnapshotRemovalVictim>();
        }
    }
};

class SnapshotOwnerRemovingComponent : public Component {
public:
    COMPONENT_TYPE(SnapshotOwnerRemovingComponent)

    void OnDisable() override {
        if (GameObject* owner = GetGameObject()) {
            Editor::Get().RemoveObjectsByIds({owner->GetID()});
        }
    }

    void ResolveAssets() override { ++resolveCount; }

    static inline int resolveCount = 0;
};

} // namespace

// Mock implementations of EditorState transition callbacks
#include "Editor/EditorState.h"
EditorState& EditorState::Get() {
    static EditorState state;
    return state;
}
void EditorState::SetMode(EditorMode mode) { currentMode = mode; }
void EditorState::Play() { currentMode = EditorMode::Play; }
void EditorState::Pause() { currentMode = EditorMode::Pause; }
void EditorState::Stop() { currentMode = EditorMode::Edit; }

// Mock implementations of UIRegistry & others to avoid linking errors
#include "Editor/UIRegistry.h"
const UIRegistry::ComponentTypeInfo& UIRegistry::GetComponentInfo(const std::string& typeName) {
    (void)typeName;
    static UIRegistry::ComponentTypeInfo info{"Icons::ArrowsAlt"};
    return info;
}

// Mock FontManager Icons namespace members
namespace Icons {
    const char* Cube = "Cube";
    const char* ArrowsAlt = "ArrowsAlt";
    const char* Image = "Image";
    const char* Sitemap = "Sitemap";
    const char* Lightbulb = "Lightbulb";
    const char* Square = "Square";
    const char* Music = "Music";
    const char* VolumeUp = "VolumeUp";
    const char* Camera = "Camera";
    const char* ListUl = "ListUl";
    const char* Code = "Code";
    const char* FileCode = "FileCode";
    const char* Cubes = "Cubes";
    const char* Plus = "Plus";
    const char* Trash = "Trash";
}

Editor& Editor::Get() {
    static Editor instance;
    return instance;
}

GameObject* Editor::FindObjectById(unsigned int id) const {
    if (!s_gameObjects) return nullptr;
    for (auto& o : *s_gameObjects) {
        if (o && o->GetID() == id) return o.get();
    }
    return nullptr;
}

std::shared_ptr<GameObject> Editor::ShareObjectById(unsigned int id) const {
    if (!s_gameObjects) return nullptr;
    for (auto& o : *s_gameObjects) {
        if (o && o->GetID() == id) return o;
    }
    return nullptr;
}

std::shared_ptr<GameObject> Editor::AddExistingObject(std::shared_ptr<GameObject> obj) {
    if (s_gameObjects && obj) {
        s_gameObjects->push_back(obj);
    }
    return obj;
}

void Editor::RemoveObjectsByIds(const std::vector<unsigned int>& ids) {
    if (!s_gameObjects) return;
    for (unsigned int id : ids) {
        s_gameObjects->erase(
            std::remove_if(s_gameObjects->begin(), s_gameObjects->end(),
                           [id](const auto& o) { return o && o->GetID() == id; }),
            s_gameObjects->end()
        );
    }
}

void Editor::MarkSceneModified() {
    if (EditorState::Get().IsEditMode()) {
        s_sceneModified = true;
    }
}

void Editor::SetSelectedObject(GameObject* obj) {
    s_selectedObject = obj;
}

GameObject* Editor::GetSelectedObject() const {
    return s_selectedObject;
}

// --- Test Cases ---

TEST_CASE("CommandHistory clean marker semantics (Phase 1)") {
    int value = 0;
    struct DummyCmd : molga::ICommand {
        int* val;
        DummyCmd(int* v) : val(v) {}
        void Execute() override { (*val)++; }
        void Undo() override { (*val)--; }
        std::string Name() const override { return "Dummy"; }
    };

    molga::CommandHistory history;
    CHECK_FALSE(history.IsDirty()); // Clean at start

    history.Execute(std::make_unique<DummyCmd>(&value));
    CHECK(history.IsDirty()); // Dirty after execute

    history.Undo();
    CHECK_FALSE(history.IsDirty()); // Clean after undo back to start

    history.Redo();
    CHECK(history.IsDirty()); // Dirty again

    history.MarkClean();
    CHECK_FALSE(history.IsDirty()); // Clean after manual mark clean

    history.Undo();
    CHECK(history.IsDirty()); // Undo from clean state makes it dirty

    history.Redo();
    CHECK_FALSE(history.IsDirty()); // Return to clean makes it clean

    // Redo branch truncation invalidates clean marker if it discards clean state
    history.Undo(); // Now clean state is in redo stack (redo size = 1)
    history.Execute(std::make_unique<DummyCmd>(&value)); // Clears redo branch
    CHECK(history.IsDirty());
    
    // Attempting undo can no longer reach the clean state because it was discarded
    history.Undo();
    CHECK(history.IsDirty());
}

TEST_CASE("GameObjectPropertyCommand undo and dirty state (Phase 3)") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    s_sceneModified = false;
    
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    auto obj = std::make_shared<GameObject>("BeforeName");
    obj->SetTag("BeforeTag");
    obj->SetLayer(1);
    obj->SetActive(true);
    gameObjects.push_back(obj);

    nlohmann::json before = molga::CaptureGameObjectProperties(obj.get());
    
    // Mutate properties
    obj->SetName("AfterName");
    obj->SetTag("AfterTag");
    obj->SetLayer(2);
    obj->SetActive(false);

    nlohmann::json after = molga::CaptureGameObjectProperties(obj.get());

    // Restore before temporarily for clean Execute
    obj->SetName("BeforeName");
    obj->SetTag("BeforeTag");
    obj->SetLayer(1);
    obj->SetActive(true);

    auto cmd = std::make_unique<molga::GameObjectPropertyCommand>(obj->GetID(), before, after);
    history.Execute(std::move(cmd));

    CHECK(obj->GetName() == "AfterName");
    CHECK(obj->GetTag() == "AfterTag");
    CHECK(obj->GetLayer() == 2);
    CHECK_FALSE(obj->IsActive());
    CHECK(s_sceneModified);

    history.Undo();
    CHECK(obj->GetName() == "BeforeName");
    CHECK(obj->GetTag() == "BeforeTag");
    CHECK(obj->GetLayer() == 1);
    CHECK(obj->IsActive());

    history.Redo();
    CHECK(obj->GetName() == "AfterName");
    CHECK_FALSE(obj->IsActive());

    s_gameObjects = nullptr;
}

TEST_CASE("ComponentSnapshotCommand size/color/enabled (Phase 3)") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    auto obj = std::make_shared<GameObject>("Obj");
    auto* sr = obj->AddComponent<SpriteRenderer>();
    sr->SetSize(32.f, 32.f);
    sr->SetColor(Color::White());
    sr->SetEnabled(true);
    gameObjects.push_back(obj);

    nlohmann::json before = molga::CaptureComponentSnapshot(sr);

    sr->SetSize(64.f, 128.f);
    sr->SetColor(Color::Red());
    sr->SetEnabled(false);

    nlohmann::json after = molga::CaptureComponentSnapshot(sr);

    // Revert before Execute
    molga::RestoreComponentSnapshot(obj.get(), before);

    auto cmd = std::make_unique<molga::ComponentSnapshotCommand>(obj->GetID(), "SpriteRenderer", before, after);
    history.Execute(std::move(cmd));

    CHECK(sr->GetWidth() == 64.f);
    CHECK(sr->GetColor().r == 1.f);
    CHECK_FALSE(sr->IsEnabled());

    history.Undo();
    CHECK(sr->GetWidth() == 32.f);
    CHECK(sr->GetColor().r == 1.f); // White r is 1.0f
    CHECK(sr->IsEnabled());

    history.Redo();
    CHECK(sr->GetWidth() == 64.f);
    CHECK_FALSE(sr->IsEnabled());

    s_gameObjects = nullptr;
}

TEST_CASE("Component snapshot restore survives self-removal from OnDisable") {
    auto object = std::make_shared<GameObject>("Self-removing component");
    auto* component = object->AddComponent<SnapshotSelfRemovingComponent>();
    SnapshotSelfRemovingComponent::resolveCount = 0;

    nlohmann::json snapshot = molga::CaptureComponentSnapshot(component);
    snapshot["enabled"] = false;

    CHECK_NOTHROW(molga::RestoreComponentSnapshot(object.get(), snapshot));
    CHECK(object->GetComponent<SnapshotSelfRemovingComponent>() == nullptr);
    CHECK(SnapshotSelfRemovingComponent::resolveCount == 0);
}

TEST_CASE("Component snapshot plans re-resolve siblings after user callbacks") {
    SUBCASE("capture Serialize removes a later component") {
        auto object = std::make_shared<GameObject>("Capture mutation");
        object->AddComponent<SnapshotCaptureMutator>();
        object->AddComponent<SnapshotCaptureVictim>();

        nlohmann::json snapshot;
        CHECK_NOTHROW(snapshot = molga::CaptureGameObjectComponents(object.get()));
        CHECK_FALSE(object->HasComponent<SnapshotCaptureVictim>());
        REQUIRE(snapshot.is_array());
        REQUIRE(snapshot.size() == 1);
        CHECK(snapshot[0].value("type", "") == "SnapshotCaptureMutator");
    }

    SUBCASE("restore removal callback removes a later component") {
        auto object = std::make_shared<GameObject>("Restore mutation");
        object->AddComponent<SnapshotRemovalMutator>();
        object->AddComponent<SnapshotRemovalVictim>();

        CHECK_NOTHROW(molga::RestoreGameObjectComponents(
            object.get(), nlohmann::json::array()));
        CHECK_FALSE(object->HasComponent<SnapshotRemovalMutator>());
        CHECK_FALSE(object->HasComponent<SnapshotRemovalVictim>());
    }
}

TEST_CASE("Component snapshot restore stops when callback removes editor owner") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    SnapshotOwnerRemovingComponent::resolveCount = 0;

    gameObjects.push_back(std::make_shared<GameObject>("Owner mutation"));
    GameObject* object = gameObjects.front().get();
    auto* component = object->AddComponent<SnapshotOwnerRemovingComponent>();
    nlohmann::json snapshot = molga::CaptureComponentSnapshot(component);
    snapshot["enabled"] = false;

    CHECK_NOTHROW(molga::RestoreComponentSnapshot(object, snapshot));
    CHECK(gameObjects.empty());
    CHECK(SnapshotOwnerRemovingComponent::resolveCount == 0);

    s_gameObjects = nullptr;
}

TEST_CASE("ComponentAddCommand and ComponentRemoveCommand (Phase 3)") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    auto obj = std::make_shared<GameObject>("Obj");
    obj->AddComponent<Transform>();
    gameObjects.push_back(obj);

    // 1. Add component test
    auto addCmd = std::make_unique<molga::ComponentAddCommand>(obj->GetID(), "SpriteRenderer");
    history.Execute(std::move(addCmd));

    CHECK(obj->HasComponent<SpriteRenderer>());
    SpriteRenderer* sr = obj->GetComponent<SpriteRenderer>();
    REQUIRE(sr != nullptr);

    history.Undo();
    CHECK_FALSE(obj->HasComponent<SpriteRenderer>());

    history.Redo();
    CHECK(obj->HasComponent<SpriteRenderer>());

    // 2. Remove component test
    auto removeCmd = std::make_unique<molga::ComponentRemoveCommand>(obj->GetID(), "SpriteRenderer");
    history.Execute(std::move(removeCmd));

    CHECK_FALSE(obj->HasComponent<SpriteRenderer>());

    history.Undo();
    CHECK(obj->HasComponent<SpriteRenderer>());

    history.Redo();
    CHECK_FALSE(obj->HasComponent<SpriteRenderer>());

    s_gameObjects = nullptr;
}

TEST_CASE("CreateObjectWithComponentsCommand (Phase 3)") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    auto cmd = std::make_unique<molga::CreateObjectWithComponentsCommand>(
        "MySprite", std::vector<std::string>{"SpriteRenderer"}
    );
    history.Execute(std::move(cmd));

    REQUIRE(gameObjects.size() == 1);
    CHECK(gameObjects[0]->GetName() == "MySprite");
    CHECK(gameObjects[0]->HasComponent<Transform>());
    CHECK(gameObjects[0]->HasComponent<SpriteRenderer>());

    history.Undo();
    CHECK(gameObjects.empty());

    history.Redo();
    REQUIRE(gameObjects.size() == 1);
    CHECK(gameObjects[0]->GetName() == "MySprite");
    CHECK(gameObjects[0]->HasComponent<SpriteRenderer>());

    s_gameObjects = nullptr;
}

TEST_CASE("Create UI button preset is authored through undo and dirty commands") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    s_sceneModified = false;

    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();
    history.Execute(std::make_unique<molga::CreateUIPresetCommand>(
        molga::UIPresetType::Button));

    REQUIRE(gameObjects.size() == 3);
    GameObject* canvas = nullptr;
    GameObject* button = nullptr;
    GameObject* label = nullptr;
    for (const auto& object : gameObjects) {
        if (object->GetComponent<UICanvas>()) canvas = object.get();
        if (object->GetComponent<UIButton>()) button = object.get();
        if (object->GetComponent<UILabel>()) label = object.get();
    }
    REQUIRE(canvas != nullptr);
    REQUIRE(button != nullptr);
    REQUIRE(label != nullptr);
    CHECK(button->GetParent() == canvas);
    CHECK(label->GetParent() == button);
    CHECK(button->GetComponent<RectTransform>() != nullptr);
    CHECK(label->GetComponent<UILabel>()->GetText() == "Button");
    CHECK(s_sceneModified);
    CHECK(history.IsDirty());

    history.Undo();
    CHECK(gameObjects.empty());
    history.Redo();
    REQUIRE(gameObjects.size() == 3);
    CHECK(Editor::Get().GetSelectedObject() != nullptr);

    s_gameObjects = nullptr;
}

TEST_CASE("DuplicateObjectCommand subtree and independence (Phase 6)") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    // Set up a hierarchy: Parent -> Child
    auto parent = std::make_shared<GameObject>("Parent");
    auto* parentSR = parent->AddComponent<SpriteRenderer>();
    parentSR->SetColor(Color::Red());
    gameObjects.push_back(parent);

    auto child = std::make_shared<GameObject>("Child");
    child->SetParent(parent.get());
    gameObjects.push_back(child);

    auto cmd = std::make_unique<molga::DuplicateObjectCommand>(parent.get());
    history.Execute(std::move(cmd));

    // After duplication, we should have the copy of parent and child
    REQUIRE(gameObjects.size() == 4); // Parent, Child, Parent (Copy), Child
    
    // Find the duplicated parent
    GameObject* parentCopy = nullptr;
    for (auto& o : gameObjects) {
        if (o && o->GetName() == "Parent (Copy)") {
            parentCopy = o.get();
            break;
        }
    }
    REQUIRE(parentCopy != nullptr);
    CHECK(parentCopy->HasComponent<SpriteRenderer>());
    CHECK(parentCopy->GetComponent<SpriteRenderer>()->GetColor().r == 1.f);
    CHECK(parentCopy->GetChildren().size() == 1);

    // Verify independent mutations (modifying copy doesn't affect original)
    parentCopy->GetComponent<SpriteRenderer>()->SetColor(Color::White());
    CHECK(parent->GetComponent<SpriteRenderer>()->GetColor().r == 1.f); // Original still red
    CHECK(parent->GetComponent<SpriteRenderer>()->GetColor().g == 0.f);

    // Undo duplication
    history.Undo();
    CHECK(gameObjects.size() == 2); // Only original parent and child remain

    history.Redo();
    CHECK(gameObjects.size() == 4); // Re-duplicated

    s_gameObjects = nullptr;
}
