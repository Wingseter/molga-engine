#include "Editor/Editor.h"
#include "Editor/Commands/CommandHistory.h"
#include "Editor/Commands/SceneSnapshots.h"
#include "Editor/Commands/PropertyCommands.h"
#include "Editor/Commands/ComponentCommands.h"
#include "Editor/Commands/ObjectCommands.h"
#include "Editor/Properties/EditorPropertyDescriptor.h"
#include "Core/AssetDatabase.h"
#include "Core/PathService.h"
#include "Core/PrefabRegistry.h"
#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/PrefabInstance.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Animator2D.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/PointLight2D.h"
#include "ECS/Components/ShadowOccluder2D.h"
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
#include <filesystem>
#include <fstream>

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

class LifecycleCountingComponent : public Component {
public:
    COMPONENT_TYPE(LifecycleCountingComponent)

    void OnEnable() override { ++enableCount; }
    void OnDisable() override { ++disableCount; }

    int enableCount = 0;
    int disableCount = 0;
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

std::shared_ptr<GameObject> Editor::InsertExistingObjectAt(
    std::shared_ptr<GameObject> obj, std::size_t index) {
    if (s_gameObjects && obj) {
        index = std::min(index, s_gameObjects->size());
        s_gameObjects->insert(
            s_gameObjects->begin() + static_cast<std::ptrdiff_t>(index), obj);
    }
    return obj;
}

bool Editor::TryGetObjectIndex(unsigned int id, std::size_t& index) const {
    if (!s_gameObjects) return false;
    for (std::size_t i = 0; i < s_gameObjects->size(); ++i) {
        if ((*s_gameObjects)[i] && (*s_gameObjects)[i]->GetID() == id) {
            index = i;
            return true;
        }
    }
    return false;
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

TEST_CASE("BatchComponentSnapshotCommand changes every target in one undo step") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    auto first = std::make_shared<GameObject>("First");
    auto second = std::make_shared<GameObject>("Second");
    auto* firstRenderer = first->AddComponent<SpriteRenderer>();
    auto* secondRenderer = second->AddComponent<SpriteRenderer>();
    firstRenderer->SetSize(10.f, 11.f);
    secondRenderer->SetSize(20.f, 21.f);
    gameObjects = {first, second};

    const nlohmann::json firstBefore = molga::CaptureComponentSnapshot(firstRenderer);
    const nlohmann::json secondBefore = molga::CaptureComponentSnapshot(secondRenderer);
    firstRenderer->SetSize(100.f, 11.f);
    secondRenderer->SetSize(100.f, 21.f);
    const nlohmann::json firstAfter = molga::CaptureComponentSnapshot(firstRenderer);
    const nlohmann::json secondAfter = molga::CaptureComponentSnapshot(secondRenderer);
    molga::RestoreComponentSnapshot(first.get(), firstBefore);
    molga::RestoreComponentSnapshot(second.get(), secondBefore);

    history.Execute(std::make_unique<molga::BatchComponentSnapshotCommand>(
        std::vector<molga::ComponentSnapshotChange>{
            {first->GetID(), "SpriteRenderer", firstBefore, firstAfter},
            {second->GetID(), "SpriteRenderer", secondBefore, secondAfter},
        }));
    CHECK(firstRenderer->GetWidth() == doctest::Approx(100.f));
    CHECK(secondRenderer->GetWidth() == doctest::Approx(100.f));
    history.Undo();
    CHECK(firstRenderer->GetWidth() == doctest::Approx(10.f));
    CHECK(secondRenderer->GetWidth() == doctest::Approx(20.f));
    CHECK_FALSE(history.CanUndo());

    // Deleting one target between undo and redo must not invalidate the rest.
    gameObjects.erase(gameObjects.begin() + 1);
    CHECK_NOTHROW(history.Redo());
    CHECK(firstRenderer->GetWidth() == doctest::Approx(100.f));
    s_gameObjects = nullptr;
}

TEST_CASE("PointLight2D mixed multi-edit is one undo step and refreshes prefab overrides") {
    namespace fs = std::filesystem;
    const fs::path oldAssetRoot = PathService::Get().AssetRoot();
    const fs::path assetRoot = fs::temp_directory_path() /
        "molga_point_light_prefab_override_assets";
    std::error_code error;
    fs::remove_all(assetRoot, error);
    fs::create_directories(assetRoot, error);
    REQUIRE_FALSE(error);
    PathService::Get().SetAssetRoot(assetRoot);
    PrefabRegistry::Get().ScanAssets();

    auto first = std::make_shared<GameObject>("First light");
    first->AddComponent<Transform>();
    PointLight2D* firstLight = first->AddComponent<PointLight2D>();
    const unsigned int prefabSourceId = first->GetID();
    const std::string prefabGuid = "edededededededededededededededed";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        prefabGuid, "point-light.prefab",
        SceneSerializer::SerializeSubtree(first.get())));

    auto second = std::make_shared<GameObject>("Second light");
    second->AddComponent<Transform>();
    PointLight2D* secondLight = second->AddComponent<PointLight2D>();
    REQUIRE(secondLight->SetIntensity(2.0f));

    PrefabInstance* firstInstance = first->AddComponent<PrefabInstance>();
    firstInstance->SetPrefabGuid(prefabGuid);
    firstInstance->SetIdRemap({{prefabSourceId, first->GetID()}});
    PrefabInstance* secondInstance = second->AddComponent<PrefabInstance>();
    secondInstance->SetPrefabGuid(prefabGuid);
    secondInstance->SetIdRemap({{prefabSourceId, second->GetID()}});

    std::vector<std::shared_ptr<GameObject>> gameObjects{first, second};
    s_gameObjects = &gameObjects;
    const nlohmann::json firstBefore =
        molga::CaptureComponentSnapshot(firstLight);
    const nlohmann::json secondBefore =
        molga::CaptureComponentSnapshot(secondLight);
    const std::vector<molga::ComponentSnapshotBaseline> baselines{
        {first->GetID(), firstLight->GetRuntimeTypeID(),
         firstLight->GetInstanceID(), "PointLight2D", firstBefore},
        {second->GetID(), secondLight->GetRuntimeTypeID(),
         secondLight->GetInstanceID(), "PointLight2D", secondBefore},
    };
    const std::vector<molga::EditorComponentIdentity> identities{
        molga::CaptureEditorComponentIdentity(*firstLight),
        molga::CaptureEditorComponentIdentity(*secondLight),
    };
    const molga::EditorComponentResolver resolve =
        [](const molga::EditorComponentIdentity& identity) -> Component* {
            GameObject* object = molga::FindGameObjectById(identity.objectId);
            if (!object) return nullptr;
            for (Component* component : object->GetComponents()) {
                if (component &&
                    component->GetRuntimeTypeID() == identity.runtimeTypeId &&
                    component->GetInstanceID() == identity.instanceId &&
                    component->GetTypeName() == identity.componentType) {
                    return component;
                }
            }
            return nullptr;
        };

    const auto descriptors =
        molga::CommonEditorProperties(identities, resolve);
    const auto intensityProperty = std::find_if(
        descriptors.begin(), descriptors.end(), [](const auto& descriptor) {
            return descriptor.key == "intensity";
        });
    REQUIRE(intensityProperty != descriptors.end());
    CHECK(intensityProperty->type == molga::EditorPropertyType::Float);
    CHECK(molga::HasMixedEditorPropertyValue(
        *intensityProperty, identities, resolve));
    CHECK(molga::ApplyEditorPropertyValue(
        *intensityProperty, identities, resolve, 4.25) == 2u);

    auto changes = molga::CaptureAppliedComponentChanges(baselines);
    REQUIRE(changes.size() == 2u);
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();
    history.Execute(std::make_unique<molga::BatchComponentSnapshotCommand>(
        std::move(changes), true));
    CHECK(firstLight->GetIntensity() == doctest::Approx(4.25f));
    CHECK(secondLight->GetIntensity() == doctest::Approx(4.25f));
    CHECK_FALSE(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());

    history.Undo();
    CHECK(firstLight->GetIntensity() == doctest::Approx(1.0f));
    CHECK(secondLight->GetIntensity() == doctest::Approx(2.0f));
    CHECK(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());
    CHECK_FALSE(history.CanUndo());

    history.Redo();
    CHECK(firstLight->GetIntensity() == doctest::Approx(4.25f));
    CHECK(secondLight->GetIntensity() == doctest::Approx(4.25f));
    CHECK_FALSE(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());

    history.Clear();
    s_gameObjects = nullptr;
    PathService::Get().SetAssetRoot(oldAssetRoot);
    fs::remove_all(assetRoot, error);
}

TEST_CASE("ShadowOccluder2D handle preview commits the whole polygon in one undo step") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    auto object = std::make_shared<GameObject>("Polygon occluder");
    object->AddComponent<Transform>();
    ShadowOccluder2D* occluder = object->AddComponent<ShadowOccluder2D>();
    const std::vector<Vector2> beforeVertices{
        {-10.0f, -10.0f}, {10.0f, -10.0f}, {0.0f, 10.0f}};
    const std::vector<Vector2> afterVertices{
        {-14.0f, -8.0f}, {10.0f, -10.0f}, {0.0f, 10.0f}};
    REQUIRE(occluder->SetPolygon(beforeVertices));
    gameObjects.push_back(object);
    s_gameObjects = &gameObjects;

    const nlohmann::json before =
        molga::CaptureComponentSnapshot(occluder);
    REQUIRE(occluder->SetPolygon(afterVertices));
    const nlohmann::json after =
        molga::CaptureComponentSnapshot(occluder);

    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();
    history.Execute(std::make_unique<molga::BatchComponentSnapshotCommand>(
        std::vector<molga::ComponentSnapshotChange>{
            {object->GetID(), "ShadowOccluder2D", before, after}},
        true));
    CHECK(occluder->GetVertices() == afterVertices);

    history.Undo();
    CHECK(occluder->GetVertices() == beforeVertices);
    CHECK_FALSE(history.CanUndo());

    history.Redo();
    CHECK(occluder->GetVertices() == afterVertices);

    history.Clear();
    s_gameObjects = nullptr;
}

TEST_CASE("adopted batch preview does not replay enabled lifecycle callbacks") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();

    auto first = std::make_shared<GameObject>("First lifecycle");
    auto second = std::make_shared<GameObject>("Second lifecycle");
    auto* firstComponent = first->AddComponent<LifecycleCountingComponent>();
    auto* secondComponent = second->AddComponent<LifecycleCountingComponent>();
    gameObjects = {first, second};

    const nlohmann::json firstBefore =
        molga::CaptureComponentSnapshot(firstComponent);
    const nlohmann::json secondBefore =
        molga::CaptureComponentSnapshot(secondComponent);

    // This is the Inspector's live preview. Each target receives one callback.
    firstComponent->SetEnabled(false);
    secondComponent->SetEnabled(false);
    const nlohmann::json firstAfter =
        molga::CaptureComponentSnapshot(firstComponent);
    const nlohmann::json secondAfter =
        molga::CaptureComponentSnapshot(secondComponent);

    history.Execute(std::make_unique<molga::BatchComponentSnapshotCommand>(
        std::vector<molga::ComponentSnapshotChange>{
            {first->GetID(), "LifecycleCountingComponent", firstBefore, firstAfter},
            {second->GetID(), "LifecycleCountingComponent", secondBefore, secondAfter},
        }, true));
    CHECK(firstComponent->disableCount == 1);
    CHECK(secondComponent->disableCount == 1);
    CHECK_FALSE(firstComponent->IsEnabled());
    CHECK_FALSE(secondComponent->IsEnabled());

    history.Undo();
    CHECK(firstComponent->enableCount == 1);
    CHECK(secondComponent->enableCount == 1);
    CHECK(firstComponent->IsEnabled());
    CHECK(secondComponent->IsEnabled());

    history.Redo();
    CHECK(firstComponent->disableCount == 2);
    CHECK(secondComponent->disableCount == 2);
    CHECK_FALSE(firstComponent->IsEnabled());
    CHECK_FALSE(secondComponent->IsEnabled());
    s_gameObjects = nullptr;
}

TEST_CASE("single component snapshots refresh nearest prefab overrides") {
    namespace fs = std::filesystem;
    const fs::path oldAssetRoot = PathService::Get().AssetRoot();
    const fs::path assetRoot = fs::temp_directory_path() /
        "molga_single_prefab_override_assets";
    std::error_code error;
    fs::remove_all(assetRoot, error);
    fs::create_directories(assetRoot, error);
    REQUIRE_FALSE(error);
    PathService::Get().SetAssetRoot(assetRoot);
    PrefabRegistry::Get().ScanAssets();

    auto object = std::make_shared<GameObject>("Single prefab");
    auto* renderer = object->AddComponent<SpriteRenderer>();
    renderer->SetSize(20.f, 30.f);
    const std::string guid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        guid, "single.prefab", SceneSerializer::SerializeSubtree(object.get())));

    auto* instance = object->AddComponent<PrefabInstance>();
    instance->SetPrefabGuid(guid);
    instance->SetIdRemap({{object->GetID(), object->GetID()}});
    std::vector<std::shared_ptr<GameObject>> gameObjects{object};
    s_gameObjects = &gameObjects;

    const nlohmann::json before = molga::CaptureComponentSnapshot(renderer);
    renderer->SetSize(80.f, 90.f);
    const nlohmann::json after = molga::CaptureComponentSnapshot(renderer);
    molga::RestoreComponentSnapshot(object.get(), before);
    molga::ComponentSnapshotCommand command(
        object->GetID(), "SpriteRenderer", before, after);

    command.Execute();
    CHECK_FALSE(instance->GetModifications().empty());
    command.Undo();
    CHECK(instance->GetModifications().empty());

    s_gameObjects = nullptr;
    PathService::Get().SetAssetRoot(oldAssetRoot);
    fs::remove_all(assetRoot, error);
}

TEST_CASE("batch component snapshots refresh prefab overrides through persisted reload") {
    namespace fs = std::filesystem;
    const fs::path oldAssetRoot = PathService::Get().AssetRoot();
    const fs::path assetRoot = fs::temp_directory_path() /
        "molga_batch_prefab_override_assets";
    std::error_code error;
    fs::remove_all(assetRoot, error);
    fs::create_directories(assetRoot, error);
    REQUIRE_FALSE(error);
    PathService::Get().SetAssetRoot(assetRoot);

    const fs::path controllerPath = assetRoot / "dropped.animator";
    {
        std::ofstream controller(controllerPath);
        controller << nlohmann::json{
            {"schemaVersion", 1},
            {"parameters", nlohmann::json::array()},
            {"states", nlohmann::json::array({
                nlohmann::json{{"id", "idle-state"}, {"name", "Idle"},
                               {"clipGuid", "cccccccccccccccccccccccccccccccc"},
                               {"speed", 1.0f}}})},
            {"defaultStateId", "idle-state"},
            {"transitions", nlohmann::json::array()},
        }.dump(2);
    }
    auto& assetDatabase = molga::AssetDatabase::Get();
    assetDatabase.Clear();
    assetDatabase.ScanProject(assetRoot);
    const std::string controllerGuid =
        assetDatabase.GuidForAbsolutePath(controllerPath);
    REQUIRE_FALSE(controllerGuid.empty());
    const molga::AssetRecord* controllerRecord =
        assetDatabase.Find(controllerGuid);
    REQUIRE(controllerRecord != nullptr);
    CHECK(controllerRecord->importer == "AnimatorControllerImporter");
    CHECK_FALSE(controllerRecord->importFailed);
    PrefabRegistry::Get().ScanAssets();

    auto prefabSource = std::make_shared<GameObject>("Batch prefab");
    prefabSource->AddComponent<Transform>(10.0f, 20.0f);
    prefabSource->AddComponent<Animator2D>();
    const std::string guid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        guid, "batch.prefab", SceneSerializer::SerializeSubtree(prefabSource.get())));

    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    std::unordered_map<unsigned int, unsigned int> firstRemap;
    std::unordered_map<unsigned int, unsigned int> secondRemap;
    GameObject* first = PrefabRegistry::Get().Instantiate(guid, gameObjects, firstRemap);
    GameObject* second = PrefabRegistry::Get().Instantiate(guid, gameObjects, secondRemap);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    auto* firstInstance = first->AddComponent<PrefabInstance>();
    auto* secondInstance = second->AddComponent<PrefabInstance>();
    firstInstance->SetPrefabGuid(guid);
    secondInstance->SetPrefabGuid(guid);
    firstInstance->SetIdRemap(firstRemap);
    secondInstance->SetIdRemap(secondRemap);

    auto* firstAnimator = first->GetComponent<Animator2D>();
    auto* secondAnimator = second->GetComponent<Animator2D>();
    REQUIRE(firstAnimator != nullptr);
    REQUIRE(secondAnimator != nullptr);
    const nlohmann::json firstBefore =
        molga::CaptureComponentSnapshot(firstAnimator);
    const nlohmann::json secondBefore =
        molga::CaptureComponentSnapshot(secondAnimator);
    const std::vector<molga::ComponentSnapshotBaseline> baselines{
        {first->GetID(), firstAnimator->GetRuntimeTypeID(),
         firstAnimator->GetInstanceID(), "Animator2D", firstBefore},
        {second->GetID(), secondAnimator->GetRuntimeTypeID(),
         secondAnimator->GetInstanceID(), "Animator2D", secondBefore},
    };

    std::vector<molga::EditorComponentIdentity> identities{
        molga::CaptureEditorComponentIdentity(*firstAnimator),
        molga::CaptureEditorComponentIdentity(*secondAnimator),
    };
    const molga::EditorComponentResolver resolve =
        [](const molga::EditorComponentIdentity& identity) -> Component* {
            GameObject* object = molga::FindGameObjectById(identity.objectId);
            if (!object) return nullptr;
            for (Component* component : object->GetComponents()) {
                if (component &&
                    component->GetRuntimeTypeID() == identity.runtimeTypeId &&
                    component->GetInstanceID() == identity.instanceId &&
                    component->GetTypeName() == identity.componentType) {
                    return component;
                }
            }
            return nullptr;
        };
    const auto descriptors =
        molga::CommonEditorProperties(identities, resolve);
    const auto controllerProperty = std::find_if(
        descriptors.begin(), descriptors.end(), [](const auto& descriptor) {
            return descriptor.key == "controllerGuid";
        });
    REQUIRE(controllerProperty != descriptors.end());
    CHECK(controllerProperty->type == molga::EditorPropertyType::AssetGuid);
    CHECK(controllerProperty->assetType == "AnimatorControllerImporter");
    CHECK(molga::ShouldCommitEditorPropertyImmediately(
        controllerProperty->type, true, false, false, false));
    CHECK(molga::ApplyEditorPropertyValue(
        *controllerProperty, identities, resolve, controllerGuid) == 2u);
    CHECK(firstAnimator->GetControllerGuid() == controllerGuid);
    CHECK(secondAnimator->GetControllerGuid() == controllerGuid);

    auto changes = molga::CaptureAppliedComponentChanges(baselines);
    REQUIRE(changes.size() == 2);

    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();
    s_sceneModified = false;
    history.Execute(std::make_unique<molga::BatchComponentSnapshotCommand>(
        std::move(changes), true));
    CHECK(history.IsDirty());
    CHECK(s_sceneModified);
    CHECK_FALSE(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());

    history.Undo();
    CHECK(firstAnimator->GetControllerGuid().empty());
    CHECK(secondAnimator->GetControllerGuid().empty());
    CHECK(firstInstance->GetModifications().empty());
    CHECK(secondInstance->GetModifications().empty());
    history.Redo();
    CHECK(firstAnimator->GetControllerGuid() == controllerGuid);
    CHECK(secondAnimator->GetControllerGuid() == controllerGuid);
    CHECK_FALSE(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());

    const nlohmann::json saved =
        SceneSerializer::SerializeScene(gameObjects, "Batch prefab overrides");
    std::vector<std::shared_ptr<GameObject>> loaded;
    REQUIRE(SceneSerializer::DeserializeScene(saved, loaded));
    REQUIRE(loaded.size() == 2);
    std::size_t loadedWithController = 0;
    for (const auto& object : loaded) {
        REQUIRE(object != nullptr);
        const Animator2D* animator = object->GetComponent<Animator2D>();
        REQUIRE(animator != nullptr);
        if (animator->GetControllerGuid() == controllerGuid) {
            ++loadedWithController;
        }
    }
    CHECK(loadedWithController == 2u);

    history.Clear();
    s_gameObjects = nullptr;
    assetDatabase.Clear();
    PathService::Get().SetAssetRoot(oldAssetRoot);
    fs::remove_all(assetRoot, error);
}

TEST_CASE("Camera output properties and viewport preset batch undo and prefab overrides") {
    namespace fs = std::filesystem;
    const fs::path oldAssetRoot = PathService::Get().AssetRoot();
    const fs::path assetRoot = fs::temp_directory_path() /
        "molga_camera_postfx_prefab_override_assets";
    std::error_code error;
    fs::remove_all(assetRoot, error);
    fs::create_directories(assetRoot, error);
    REQUIRE_FALSE(error);
    PathService::Get().SetAssetRoot(assetRoot);

    const fs::path profilePath = assetRoot / "camera.postfx";
    {
        std::ofstream profile(profilePath);
        profile << nlohmann::json{
            {"schemaVersion", 1}, {"effects", nlohmann::json::array()}
        }.dump(2);
    }
    auto& assetDatabase = molga::AssetDatabase::Get();
    assetDatabase.Clear();
    assetDatabase.ScanProject(assetRoot);
    const std::string profileGuid =
        assetDatabase.GuidForAbsolutePath(profilePath);
    REQUIRE_FALSE(profileGuid.empty());
    const molga::AssetRecord* profileRecord = assetDatabase.Find(profileGuid);
    REQUIRE(profileRecord != nullptr);
    CHECK(profileRecord->importer == "PostProcessProfileImporter");
    CHECK_FALSE(profileRecord->importFailed);

    PrefabRegistry::Get().ScanAssets();
    auto prefabSource = std::make_shared<GameObject>("Post camera prefab");
    prefabSource->AddComponent<Transform>();
    prefabSource->AddComponent<Camera>();
    const std::string prefabGuid = "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        prefabGuid, "post-camera.prefab",
        SceneSerializer::SerializeSubtree(prefabSource.get())));

    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    std::unordered_map<unsigned int, unsigned int> firstRemap;
    std::unordered_map<unsigned int, unsigned int> secondRemap;
    GameObject* first = PrefabRegistry::Get().Instantiate(
        prefabGuid, gameObjects, firstRemap);
    GameObject* second = PrefabRegistry::Get().Instantiate(
        prefabGuid, gameObjects, secondRemap);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    auto* firstInstance = first->AddComponent<PrefabInstance>();
    auto* secondInstance = second->AddComponent<PrefabInstance>();
    firstInstance->SetPrefabGuid(prefabGuid);
    secondInstance->SetPrefabGuid(prefabGuid);
    firstInstance->SetIdRemap(firstRemap);
    secondInstance->SetIdRemap(secondRemap);
    Camera* firstCamera = first->GetComponent<Camera>();
    Camera* secondCamera = second->GetComponent<Camera>();
    REQUIRE(firstCamera != nullptr);
    REQUIRE(secondCamera != nullptr);

    const nlohmann::json firstBefore =
        molga::CaptureComponentSnapshot(firstCamera);
    const nlohmann::json secondBefore =
        molga::CaptureComponentSnapshot(secondCamera);
    const std::vector<molga::ComponentSnapshotBaseline> baselines{
        {first->GetID(), firstCamera->GetRuntimeTypeID(),
         firstCamera->GetInstanceID(), "Camera", firstBefore},
        {second->GetID(), secondCamera->GetRuntimeTypeID(),
         secondCamera->GetInstanceID(), "Camera", secondBefore},
    };
    std::vector<Component*> cameras{firstCamera, secondCamera};
    const auto descriptors = molga::CommonEditorProperties(cameras);
    const auto profileProperty = std::find_if(
        descriptors.begin(), descriptors.end(), [](const auto& descriptor) {
            return descriptor.key == "postProcessProfileGuid";
        });
    REQUIRE(profileProperty != descriptors.end());
    CHECK(profileProperty->type == molga::EditorPropertyType::AssetGuid);
    CHECK(profileProperty->assetType == "PostProcessProfileImporter");
    CHECK(molga::HasMixedEditorPropertyValue(*profileProperty, cameras) == false);
    firstCamera->SetPostProcessProfileGuid(profileGuid);
    CHECK(molga::HasMixedEditorPropertyValue(*profileProperty, cameras));
    firstCamera->SetPostProcessProfileGuid({});
    CHECK(molga::ApplyEditorPropertyValue(
        *profileProperty, cameras, profileGuid) == 2u);
    CHECK(firstCamera->GetPostProcessProfileGuid() == profileGuid);
    CHECK(secondCamera->GetPostProcessProfileGuid() == profileGuid);

    const auto roleProperty = std::find_if(
        descriptors.begin(), descriptors.end(), [](const auto& descriptor) {
            return descriptor.key == "outputRole";
        });
    const auto maskProperty = std::find_if(
        descriptors.begin(), descriptors.end(), [](const auto& descriptor) {
            return descriptor.key == "cullingMask";
        });
    REQUIRE(roleProperty != descriptors.end());
    REQUIRE(maskProperty != descriptors.end());
    CHECK(roleProperty->type == molga::EditorPropertyType::Enum);
    CHECK(maskProperty->type == molga::EditorPropertyType::LayerMask);
    CHECK(molga::ApplyEditorPropertyValue(
              *roleProperty, cameras, std::string{"Secondary"}) == 2u);
    constexpr std::int64_t gameplayMask =
        (std::int64_t{1} << 0) | (std::int64_t{1} << 5);
    CHECK(molga::ApplyEditorPropertyValue(
              *maskProperty, cameras, gameplayMask) == 2u);
    const CameraViewport leftPreset{0.0f, 0.0f, 0.5f, 1.0f};
    CHECK(firstCamera->SetViewport(leftPreset));
    CHECK(secondCamera->SetViewport(leftPreset));

    auto changes = molga::CaptureAppliedComponentChanges(baselines);
    REQUIRE(changes.size() == 2);
    auto& history = Editor::Get().GetCommandHistory();
    history.Clear();
    history.Execute(std::make_unique<molga::BatchComponentSnapshotCommand>(
        std::move(changes), true));
    CHECK_FALSE(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());
    history.Undo();
    CHECK(firstCamera->GetPostProcessProfileGuid().empty());
    CHECK(secondCamera->GetPostProcessProfileGuid().empty());
    CHECK(firstCamera->GetOutputRole() == CameraOutputRole::Disabled);
    CHECK(secondCamera->GetOutputRole() == CameraOutputRole::Disabled);
    CHECK(firstCamera->GetCullingMask() == 0xFFFFFFFFu);
    CHECK(secondCamera->GetCullingMask() == 0xFFFFFFFFu);
    CHECK((firstCamera->GetViewport() == CameraViewport{}));
    CHECK((secondCamera->GetViewport() == CameraViewport{}));
    CHECK(firstInstance->GetModifications().empty());
    CHECK(secondInstance->GetModifications().empty());
    history.Redo();
    CHECK(firstCamera->GetPostProcessProfileGuid() == profileGuid);
    CHECK(secondCamera->GetPostProcessProfileGuid() == profileGuid);
    CHECK(firstCamera->GetOutputRole() == CameraOutputRole::Secondary);
    CHECK(secondCamera->GetOutputRole() == CameraOutputRole::Secondary);
    CHECK(firstCamera->GetCullingMask() == static_cast<std::uint32_t>(gameplayMask));
    CHECK(secondCamera->GetCullingMask() == static_cast<std::uint32_t>(gameplayMask));
    CHECK((firstCamera->GetViewport() == leftPreset));
    CHECK((secondCamera->GetViewport() == leftPreset));
    CHECK_FALSE(firstInstance->GetModifications().empty());
    CHECK_FALSE(secondInstance->GetModifications().empty());

    history.Clear();
    s_gameObjects = nullptr;
    assetDatabase.Clear();
    PathService::Get().SetAssetRoot(oldAssetRoot);
    fs::remove_all(assetRoot, error);
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

TEST_CASE("multi-object hierarchy commands operate on root-most selections with stable redo ids") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    auto& history = Editor::Get().GetCommandHistory();
    auto& selection = Editor::Get().GetSelection();
    history.Clear();

    auto parent = std::make_shared<GameObject>("Parent");
    parent->AddComponent<Transform>();
    auto child = std::make_shared<GameObject>("Child");
    child->AddComponent<Transform>();
    child->SetParent(parent.get());
    gameObjects = {parent, child};
    selection.SelectMany({parent->GetID(), child->GetID()}, child->GetID(),
                         molga::SelectionSource::Hierarchy);

    history.Execute(std::make_unique<molga::DuplicateObjectsCommand>(
        selection.SelectedIds()));
    REQUIRE(gameObjects.size() == 4u); // parent subtree duplicated only once
    REQUIRE(selection.SelectedIds().size() == 1u);
    const unsigned int duplicateRootId = selection.PrimaryId();
    CHECK(duplicateRootId != parent->GetID());
    history.Undo();
    REQUIRE(gameObjects.size() == 2u);
    CHECK(selection.SelectedIds() ==
          std::vector<unsigned int>{parent->GetID(), child->GetID()});
    history.Redo();
    REQUIRE(gameObjects.size() == 4u);
    CHECK(selection.PrimaryId() == duplicateRootId);

    history.Clear();
    selection.SelectMany({parent->GetID(), child->GetID()}, parent->GetID(),
                         molga::SelectionSource::Hierarchy);
    selection.LockInspector();
    history.Execute(std::make_unique<molga::DeleteObjectsCommand>(
        selection.SelectedIds()));
    CHECK(gameObjects.size() == 2u); // duplicated subtree remains
    CHECK_FALSE(selection.HasSelection());
    CHECK_FALSE(selection.IsInspectorLocked());
    history.Undo();
    CHECK(gameObjects.size() == 4u);
    CHECK(selection.IsSelected(parent->GetID()));
    CHECK(selection.IsSelected(child->GetID()));
    CHECK(selection.IsInspectorLocked());
    CHECK(selection.InspectorTargetIds() ==
          std::vector<unsigned int>{parent->GetID(), child->GetID()});

    selection.RestoreState({}, molga::SelectionSource::Code);
    s_gameObjects = nullptr;
}

TEST_CASE("multi delete restores exact world sibling and full selection state") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    auto& history = Editor::Get().GetCommandHistory();
    auto& selection = Editor::Get().GetSelection();
    history.Clear();
    selection.RestoreState({}, molga::SelectionSource::Code);

    auto parent = std::make_shared<GameObject>("Parent");
    auto first = std::make_shared<GameObject>("First");
    auto branch = std::make_shared<GameObject>("Branch");
    auto branchChild = std::make_shared<GameObject>("Branch Child");
    auto last = std::make_shared<GameObject>("Last");
    auto otherRoot = std::make_shared<GameObject>("Other Root");
    first->SetParent(parent.get());
    branch->SetParent(parent.get());
    branchChild->SetParent(branch.get());
    last->SetParent(parent.get());
    gameObjects = {parent, first, branch, branchChild, last, otherRoot};

    const auto worldIds = [&]() {
        std::vector<unsigned int> ids;
        for (const auto& object : gameObjects) ids.push_back(object->GetID());
        return ids;
    };
    const auto childIds = [](const GameObject& object) {
        std::vector<unsigned int> ids;
        for (const GameObject* child : object.GetChildren()) {
            ids.push_back(child->GetID());
        }
        return ids;
    };
    const std::vector<unsigned int> beforeWorld = worldIds();
    const std::vector<unsigned int> beforeSiblings = childIds(*parent);
    const molga::SelectionState beforeSelection{
        {branch->GetID(), branchChild->GetID(), otherRoot->GetID()},
        otherRoot->GetID(), branch->GetID(), true,
        {first->GetID(), otherRoot->GetID()}, first->GetID()};
    selection.RestoreState(beforeSelection, molga::SelectionSource::Hierarchy);

    history.Execute(std::make_unique<molga::DeleteObjectsCommand>(
        selection.SelectedIds()));
    CHECK(worldIds() == std::vector<unsigned int>{
        parent->GetID(), first->GetID(), last->GetID()});
    CHECK(childIds(*parent) == std::vector<unsigned int>{
        first->GetID(), last->GetID()});
    const molga::SelectionState afterSelection = selection.State();
    CHECK(afterSelection.selectedIds.empty());
    CHECK(afterSelection.rangeAnchor == 0u);
    CHECK(afterSelection.inspectorLocked);
    CHECK(afterSelection.inspectorTargetIds ==
          std::vector<unsigned int>{first->GetID()});

    history.Undo();
    CHECK(worldIds() == beforeWorld);
    CHECK(childIds(*parent) == beforeSiblings);
    CHECK(selection.State() == beforeSelection);

    history.Redo();
    CHECK(worldIds() == std::vector<unsigned int>{
        parent->GetID(), first->GetID(), last->GetID()});
    CHECK(childIds(*parent) == std::vector<unsigned int>{
        first->GetID(), last->GetID()});
    CHECK(selection.State() == afterSelection);

    history.Undo();
    selection.RestoreState({}, molga::SelectionSource::Code);
    history.Clear();
    s_gameObjects = nullptr;
}

TEST_CASE("multi duplicate replays exact placement ids and full selection state") {
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    s_gameObjects = &gameObjects;
    auto& history = Editor::Get().GetCommandHistory();
    auto& selection = Editor::Get().GetSelection();
    history.Clear();
    selection.RestoreState({}, molga::SelectionSource::Code);

    auto parent = std::make_shared<GameObject>("Parent");
    auto first = std::make_shared<GameObject>("First");
    auto branch = std::make_shared<GameObject>("Branch");
    auto branchChild = std::make_shared<GameObject>("Branch Child");
    auto last = std::make_shared<GameObject>("Last");
    auto otherRoot = std::make_shared<GameObject>("Other Root");
    first->SetParent(parent.get());
    branch->SetParent(parent.get());
    branchChild->SetParent(branch.get());
    last->SetParent(parent.get());
    gameObjects = {parent, first, branch, branchChild, last, otherRoot};

    const auto worldIds = [&]() {
        std::vector<unsigned int> ids;
        for (const auto& object : gameObjects) ids.push_back(object->GetID());
        return ids;
    };
    const auto childIds = [](const GameObject& object) {
        std::vector<unsigned int> ids;
        for (const GameObject* child : object.GetChildren()) {
            ids.push_back(child->GetID());
        }
        return ids;
    };
    const std::vector<unsigned int> beforeWorld = worldIds();
    const std::vector<unsigned int> beforeSiblings = childIds(*parent);
    const molga::SelectionState beforeSelection{
        {branch->GetID(), branchChild->GetID(), last->GetID()},
        last->GetID(), branch->GetID(), true,
        {first->GetID(), last->GetID()}, last->GetID()};
    selection.RestoreState(beforeSelection, molga::SelectionSource::Hierarchy);

    history.Execute(std::make_unique<molga::DuplicateObjectsCommand>(
        selection.SelectedIds()));
    REQUIRE(gameObjects.size() == beforeWorld.size() + 3u);
    REQUIRE(selection.SelectedIds().size() == 2u);
    const std::vector<unsigned int> resultWorld = worldIds();
    const std::vector<unsigned int> resultSiblings = childIds(*parent);
    const molga::SelectionState resultSelection = selection.State();
    const std::vector<unsigned int> duplicateRootIds = selection.SelectedIds();
    REQUIRE(duplicateRootIds.size() == 2u);
    GameObject* branchCopy = Editor::Get().FindObjectById(duplicateRootIds[0]);
    REQUIRE(branchCopy != nullptr);
    REQUIRE(branchCopy->GetChildren().size() == 1u);
    const unsigned int branchChildCopyId = branchCopy->GetChildren()[0]->GetID();
    CHECK(resultWorld == std::vector<unsigned int>{
        parent->GetID(), first->GetID(), branch->GetID(), branchChild->GetID(),
        duplicateRootIds[0], branchChildCopyId, last->GetID(),
        duplicateRootIds[1], otherRoot->GetID()});
    CHECK(resultSiblings == std::vector<unsigned int>{
        first->GetID(), branch->GetID(), duplicateRootIds[0], last->GetID(),
        duplicateRootIds[1]});
    CHECK(resultSelection.inspectorLocked);
    CHECK(resultSelection.inspectorTargetIds == beforeSelection.inspectorTargetIds);
    CHECK(resultSelection.rangeAnchor == resultSelection.primaryId);

    history.Undo();
    CHECK(worldIds() == beforeWorld);
    CHECK(childIds(*parent) == beforeSiblings);
    CHECK(selection.State() == beforeSelection);

    history.Redo();
    CHECK(worldIds() == resultWorld);
    CHECK(childIds(*parent) == resultSiblings);
    CHECK(selection.State() == resultSelection);
    CHECK(selection.SelectedIds() == duplicateRootIds);

    history.Undo();
    selection.RestoreState({}, molga::SelectionSource::Code);
    history.Clear();
    s_gameObjects = nullptr;
}
