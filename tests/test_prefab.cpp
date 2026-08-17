#include "Core/World.h"
#include "Core/PrefabRegistry.h"
#include "Core/PrefabUtil.h"
#include "Core/PathService.h"
#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/PrefabInstance.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/TilemapRenderer.h"
#include "doctest.h"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>

struct TempAssetRootFixture {
    std::filesystem::path tempDir = "/tmp/molga_test_prefab_assets";
    std::filesystem::path oldAssetRoot;

    TempAssetRootFixture() {
        oldAssetRoot = PathService::Get().AssetRoot();
        std::filesystem::create_directories(tempDir);
        PathService::Get().SetAssetRoot(tempDir);
        PrefabRegistry::Get().ScanAssets();
    }

    ~TempAssetRootFixture() {
        PathService::Get().SetAssetRoot(oldAssetRoot);
        std::filesystem::remove_all(tempDir);
    }
};

TEST_CASE("Prefab System: Save, Scan, and Instantiate") {
    TempAssetRootFixture fixture;

    // 1. Create a template object to turn into a prefab
    auto templateObj = std::make_shared<GameObject>("EnemyTemplate");
    auto* t = templateObj->AddComponent<Transform>(10.0f, 15.0f);
    t->SetRotation(45.0f);

    // Serialize it
    nlohmann::json subtree = SceneSerializer::SerializeSubtree(templateObj.get());

    // Generate GUID
    std::string guid = PrefabRegistry::GenerateGUID();
    
    // Save prefab
    bool saved = PrefabRegistry::Get().SavePrefab(guid, "enemy.prefab", subtree);
    REQUIRE(saved);

    // Verify registry mapping
    CHECK(PrefabRegistry::Get().HasPrefab(guid));
    CHECK(PrefabRegistry::Get().GetPrefabPath(guid) == "enemy.prefab");
    CHECK(PrefabRegistry::Get().GetPrefabGuid("enemy.prefab") == guid);

    // 2. Instantiate prefab
    std::vector<std::shared_ptr<GameObject>> createdObjects;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* instance = PrefabRegistry::Get().Instantiate(guid, createdObjects, idRemap);
    
    REQUIRE(instance != nullptr);
    CHECK(instance->GetName() == "EnemyTemplate");
    CHECK(instance->GetID() != templateObj->GetID()); // Fresh ID
    
    auto* instanceTransform = instance->GetComponent<Transform>();
    REQUIRE(instanceTransform != nullptr);
    CHECK(instanceTransform->GetX() == doctest::Approx(10.0f));
    CHECK(instanceTransform->GetY() == doctest::Approx(15.0f));
    CHECK(instanceTransform->GetRotation() == doctest::Approx(45.0f));
}

TEST_CASE("Prefab System: Stripped Serialization and Deserialization with Overrides") {
    TempAssetRootFixture fixture;

    // 1. Create and save a prefab
    auto templateObj = std::make_shared<GameObject>("EnemyTemplate");
    auto* t = templateObj->AddComponent<Transform>(10.0f, 15.0f);
    t->SetRotation(45.0f);
    nlohmann::json subtree = SceneSerializer::SerializeSubtree(templateObj.get());
    std::string guid = "test-enemy-guid";
    REQUIRE(PrefabRegistry::Get().SavePrefab(guid, "enemy.prefab", subtree));

    // 2. Load it as a prefab instance in the world
    World world;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* rootInstance = PrefabRegistry::Get().Instantiate(guid, world.Objects(), idRemap);
    REQUIRE(rootInstance != nullptr);

    // Attach PrefabInstance component
    auto* pi = rootInstance->AddComponent<PrefabInstance>();
    pi->SetPrefabGuid(guid);
    pi->SetIdRemap(idRemap);

    // Apply an override (change position)
    auto* it = rootInstance->GetComponent<Transform>();
    it->SetPosition(99.0f, 99.0f); // Changed from template's (10, 15)

    // Verify diff generation
    nlohmann::json generatedMods = PrefabUtil::GenerateModifications(rootInstance, PrefabRegistry::Get().GetPrefabJson(guid), idRemap);
    REQUIRE(generatedMods.size() == 1); // "position" array key
    CHECK(generatedMods[0]["key"] == "position");
    CHECK(generatedMods[0]["value"][0] == 99.0f);
    CHECK(generatedMods[0]["value"][1] == 99.0f);
    pi->SetModifications(generatedMods);

    // 3. Serialize scene (stripped)
    nlohmann::json sceneJson = SceneSerializer::SerializeScene(world.Objects(), "Test Prefab Scene");
    
    // Check that it's serialized as a stripped block
    REQUIRE(sceneJson.contains("gameObjects"));
    auto gameObjects = sceneJson["gameObjects"];
    REQUIRE(gameObjects.size() == 1); // Only the root stripped entry, children skipped!
    
    auto strippedEntry = gameObjects[0];
    REQUIRE(strippedEntry.contains("prefabInstance"));
    CHECK(strippedEntry["prefabInstance"]["guid"] == guid);
    CHECK(strippedEntry["prefabInstance"]["rootId"] == rootInstance->GetID());
    CHECK(strippedEntry["prefabInstance"]["modifications"].size() == 1);

    // 4. Deserialize scene
    std::vector<std::shared_ptr<GameObject>> loadedObjects;
    bool loaded = SceneSerializer::DeserializeScene(sceneJson, loadedObjects);
    REQUIRE(loaded);
    REQUIRE(loadedObjects.size() == 1); // Instantiated root

    GameObject* loadedInstance = loadedObjects[0].get();
    CHECK(loadedInstance->GetName() == "EnemyTemplate");
    CHECK(loadedInstance->GetID() == rootInstance->GetID()); // Restored ID

    // Verify overrides applied
    auto* loadedTransform = loadedInstance->GetComponent<Transform>();
    REQUIRE(loadedTransform != nullptr);
    CHECK(loadedTransform->GetX() == doctest::Approx(99.0f)); // Overridden value preserved
    CHECK(loadedTransform->GetY() == doctest::Approx(99.0f));
    CHECK(loadedTransform->GetRotation() == doctest::Approx(45.0f)); // Non-overridden value from prefab template
}

TEST_CASE("Prefab System: missing renderer lighting defaults do not become overrides") {
    auto instance = std::make_shared<GameObject>("Legacy Renderers");
    auto* sprite = instance->AddComponent<SpriteRenderer>();
    auto* tilemap = instance->AddComponent<TilemapRenderer>();

    nlohmann::json spriteTemplate;
    spriteTemplate["type"] = "SpriteRenderer";
    spriteTemplate["enabled"] = true;
    sprite->Serialize(spriteTemplate);
    spriteTemplate.erase("lightingMode");
    spriteTemplate.erase("normalMapGuid");
    spriteTemplate.erase("normalStrength");

    nlohmann::json tilemapTemplate;
    tilemapTemplate["type"] = "TilemapRenderer";
    tilemapTemplate["enabled"] = true;
    tilemap->Serialize(tilemapTemplate);
    tilemapTemplate.erase("lightingMode");

    constexpr unsigned int localId = 17u;
    const nlohmann::json prefab = {
        {"gameObjects", nlohmann::json::array({{
            {"id", localId},
            {"name", instance->GetName()},
            {"tag", instance->GetTag()},
            {"layer", instance->GetLayer()},
            {"active", instance->IsActive()},
            {"components", nlohmann::json::array(
                {spriteTemplate, tilemapTemplate})}
        }})}
    };
    const std::unordered_map<unsigned int, unsigned int> remap = {
        {localId, instance->GetID()}};

    CHECK(PrefabUtil::GenerateModifications(
        instance.get(), prefab, remap).empty());

    sprite->SetLightingMode(SpriteLightingMode2D::Lit);
    tilemap->SetLightingMode(SpriteLightingMode2D::Lit);
    const nlohmann::json changed =
        PrefabUtil::GenerateModifications(instance.get(), prefab, remap);
    CHECK(std::count_if(changed.begin(), changed.end(),
        [](const nlohmann::json& modification) {
            return modification.value("key", "") == "lightingMode";
        }) == 2);
}

TEST_CASE("Prefab System: legacy Camera data and isMain overrides normalize canonically") {
    TempAssetRootFixture fixture;
    const std::string guid = "legacy-camera-prefab-guid";
    const nlohmann::json legacyPrefab{
        {"guid", guid}, {"version", "1.0"},
        {"gameObjects", nlohmann::json::array({
            {
                {"name", "Legacy Camera"}, {"id", 1u},
                {"tag", "Untagged"}, {"layer", 0}, {"active", true},
                {"parentId", -1},
                {"components", nlohmann::json::array({
                    {{"type", "Camera"}, {"enabled", true}, {"isMain", false}},
                })},
            },
        })},
    };
    {
        std::ofstream file(fixture.tempDir / "legacy-camera.prefab");
        REQUIRE(file.is_open());
        file << legacyPrefab.dump(2);
    }
    PrefabRegistry::Get().ScanAssets();
    REQUIRE(PrefabRegistry::Get().HasPrefab(guid));

    // Missing modern Camera defaults in a legacy template are semantically
    // equal to a pristine runtime component and must not become overrides.
    std::vector<std::shared_ptr<GameObject>> pristineObjects;
    std::unordered_map<unsigned int, unsigned int> pristineRemap;
    GameObject* pristine = PrefabRegistry::Get().Instantiate(
        guid, pristineObjects, pristineRemap);
    REQUIRE(pristine != nullptr);
    Camera* pristineCamera = pristine->GetComponent<Camera>();
    REQUIRE(pristineCamera != nullptr);
    CHECK(pristineCamera->GetOutputRole() == CameraOutputRole::Disabled);
    CHECK(PrefabUtil::GenerateModifications(
        pristine, PrefabRegistry::Get().GetPrefabJson(guid), pristineRemap).empty());

    const nlohmann::json legacyOverride = nlohmann::json::array({
        {{"target", 1u}, {"component", "Camera"},
         {"key", "isMain"}, {"value", true}},
    });
    const nlohmann::json scene{
        {"version", "1.0"}, {"name", "Legacy Camera Instance"},
        {"gameObjects", nlohmann::json::array({
            {{"prefabInstance", {
                {"guid", guid}, {"rootId", 9001u}, {"parentId", -1},
                {"modifications", legacyOverride},
            }}},
        })},
    };

    std::vector<std::shared_ptr<GameObject>> loaded;
    REQUIRE(SceneSerializer::DeserializeScene(scene, loaded));
    REQUIRE(loaded.size() == 1u);
    Camera* camera = loaded.front()->GetComponent<Camera>();
    REQUIRE(camera != nullptr);
    CHECK(camera->GetOutputRole() == CameraOutputRole::Primary);
    PrefabInstance* instance = loaded.front()->GetComponent<PrefabInstance>();
    REQUIRE(instance != nullptr);
    REQUIRE(instance->GetModifications().size() == 1u);
    CHECK(instance->GetModifications()[0]["key"] == "outputRole");
    CHECK(instance->GetModifications()[0]["value"] == "Primary");

    const nlohmann::json saved = SceneSerializer::SerializeScene(
        loaded, "Canonical Camera Instance");
    const auto& savedMods =
        saved["gameObjects"][0]["prefabInstance"]["modifications"];
    REQUIRE(savedMods.size() == 1u);
    CHECK(savedMods[0]["key"] == "outputRole");
    CHECK(savedMods[0]["value"] == "Primary");

    // When both eras are present, the modern role is authoritative regardless
    // of modification order and the legacy mirror is removed.
    const nlohmann::json conflicting = nlohmann::json::array({
        {{"target", 1u}, {"component", "Camera"},
         {"key", "isMain"}, {"value", true}},
        {{"target", 1u}, {"component", "Camera"},
         {"key", "outputRole"}, {"value", "Secondary"}},
    });
    const nlohmann::json normalized =
        PrefabUtil::NormalizeModifications(conflicting);
    REQUIRE(normalized.size() == 1u);
    CHECK(normalized[0]["key"] == "outputRole");
    CHECK(normalized[0]["value"] == "Secondary");
}

TEST_CASE("Prefab System: Apply and Revert") {
    TempAssetRootFixture fixture;

    // 1. Create and save prefab
    auto templateObj = std::make_shared<GameObject>("EnemyTemplate");
    auto* t = templateObj->AddComponent<Transform>(10.0f, 15.0f);
    nlohmann::json subtree = SceneSerializer::SerializeSubtree(templateObj.get());
    std::string guid = "apply-revert-guid";
    REQUIRE(PrefabRegistry::Get().SavePrefab(guid, "enemy.prefab", subtree));

    // 2. Instantiate and modify
    World world;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* instance = PrefabRegistry::Get().Instantiate(guid, world.Objects(), idRemap);
    REQUIRE(instance != nullptr);

    auto* pi = instance->AddComponent<PrefabInstance>();
    pi->SetPrefabGuid(guid);
    pi->SetIdRemap(idRemap);

    auto* transform = instance->GetComponent<Transform>();
    transform->SetPosition(200.0f, 300.0f); // modification

    // Generate and apply modifications
    pi->SetModifications(PrefabUtil::GenerateModifications(instance, PrefabRegistry::Get().GetPrefabJson(guid), idRemap));

    // 3. Test Revert
    bool reverted = PrefabUtil::RevertPrefab(instance, world.Objects());
    REQUIRE(reverted);
    REQUIRE(world.Objects().size() == 1); // The new clean instance

    GameObject* revertedInstance = world.Objects()[0].get();
    auto* revertedTransform = revertedInstance->GetComponent<Transform>();
    REQUIRE(revertedTransform != nullptr);
    CHECK(revertedTransform->GetX() == doctest::Approx(10.0f)); // Reset to template position
    CHECK(revertedTransform->GetY() == doctest::Approx(15.0f));

    // 4. Test Apply
    // Get fresh instance from the reverted one (it is now the active instance)
    pi = revertedInstance->GetComponent<PrefabInstance>();
    transform = revertedInstance->GetComponent<Transform>();
    
    // Mutate position again
    transform->SetPosition(555.0f, 666.0f);
    pi->SetModifications(PrefabUtil::GenerateModifications(revertedInstance, PrefabRegistry::Get().GetPrefabJson(guid), pi->GetIdRemap()));

    // Apply prefab changes back to template
    bool applied = PrefabUtil::ApplyPrefab(revertedInstance);
    REQUIRE(applied);

    // Verify template JSON updated on disk / registry
    nlohmann::json updatedPrefab = PrefabRegistry::Get().GetPrefabJson(guid);
    REQUIRE(updatedPrefab.contains("gameObjects"));
    auto prefabObjects = updatedPrefab["gameObjects"];
    REQUIRE(prefabObjects.size() == 1);
    
    nlohmann::json prefabTransformJson;
    for (const auto& comp : prefabObjects[0]["components"]) {
        if (comp.value("type", "") == "Transform") {
            prefabTransformJson = comp;
            break;
        }
    }
    REQUIRE(!prefabTransformJson.is_null());
    CHECK(prefabTransformJson["position"][0].get<float>() == doctest::Approx(555.0f));
    CHECK(prefabTransformJson["position"][1].get<float>() == doctest::Approx(666.0f));

    // Modifications on the instance should be cleared after apply
    CHECK(pi->GetModifications().empty());
}

// ── Nested Prefab helpers ─────────────────────────────────────────────────────
// Builds a "Car" object that contains a single nested instance of the given
// child prefab as a child, links it, and returns the car (caller owns it).
static std::shared_ptr<GameObject> MakeCarWithNestedWheel(
    const std::string& wheelGuid,
    std::vector<std::shared_ptr<GameObject>>& wheelObjs,
    GameObject*& outWheelInstance) {
    auto car = std::make_shared<GameObject>("Car");
    car->AddComponent<Transform>(0.0f, 0.0f);

    std::unordered_map<unsigned int, unsigned int> wheelRemap;
    GameObject* wheel = PrefabRegistry::Get().Instantiate(wheelGuid, wheelObjs, wheelRemap);
    REQUIRE(wheel != nullptr);
    auto* wheelPi = wheel->AddComponent<PrefabInstance>();
    wheelPi->SetPrefabGuid(wheelGuid);
    wheelPi->SetIdRemap(wheelRemap);
    wheel->SetParent(car.get());
    outWheelInstance = wheel;
    return car;
}

TEST_CASE("Nested Prefab: child prefab instance is serialized as a stripped entry within the parent subtree") {
    TempAssetRootFixture fixture;

    // Child prefab "Wheel"
    auto wheelTemplate = std::make_shared<GameObject>("Wheel");
    wheelTemplate->AddComponent<Transform>(5.0f, 5.0f);
    std::string wheelGuid = "nested-wheel-guid-1";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        wheelGuid, "wheel.prefab", SceneSerializer::SerializeSubtree(wheelTemplate.get())));

    // Car with a nested Wheel instance
    std::vector<std::shared_ptr<GameObject>> wheelObjs;
    GameObject* wheelInstance = nullptr;
    auto car = MakeCarWithNestedWheel(wheelGuid, wheelObjs, wheelInstance);

    // Serialize the Car subtree
    nlohmann::json subtree = SceneSerializer::SerializeSubtree(car.get());
    REQUIRE(subtree.contains("gameObjects"));
    const auto& gos = subtree["gameObjects"];
    REQUIRE(gos.size() == 2); // Car (full) + Wheel (stripped)

    // The nested wheel must appear as a stripped prefabInstance entry, not a full object.
    bool foundStripped = false;
    for (const auto& go : gos) {
        if (go.contains("prefabInstance")) {
            foundStripped = true;
            CHECK(go["prefabInstance"]["guid"] == wheelGuid);
        }
    }
    CHECK(foundStripped);

    // The Car full entry must NOT carry a PrefabInstance component (templates are not instances).
    for (const auto& go : gos) {
        if (go.contains("components")) {
            for (const auto& c : go["components"]) {
                CHECK(c.value("type", std::string()) != "PrefabInstance");
            }
        }
    }
}

TEST_CASE("Nested Prefab: editing the nested template propagates through the parent prefab") {
    TempAssetRootFixture fixture;

    // Child prefab "Wheel" at (5,5)
    auto wheelTemplate = std::make_shared<GameObject>("Wheel");
    wheelTemplate->AddComponent<Transform>(5.0f, 5.0f);
    std::string wheelGuid = "nested-wheel-guid-2";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        wheelGuid, "wheel.prefab", SceneSerializer::SerializeSubtree(wheelTemplate.get())));

    // Car containing a Wheel instance, saved as a prefab
    std::vector<std::shared_ptr<GameObject>> wheelObjs;
    GameObject* wheelInstance = nullptr;
    auto car = MakeCarWithNestedWheel(wheelGuid, wheelObjs, wheelInstance);
    std::string carGuid = "nested-car-guid-2";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        carGuid, "car.prefab", SceneSerializer::SerializeSubtree(car.get())));

    // EDIT the wheel template: move it to (99,99) and re-save under the same GUID
    auto newWheel = std::make_shared<GameObject>("Wheel");
    newWheel->AddComponent<Transform>(99.0f, 99.0f);
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        wheelGuid, "wheel.prefab", SceneSerializer::SerializeSubtree(newWheel.get())));

    // Instantiate the Car prefab; the nested wheel must reflect the NEW template position.
    World world;
    std::unordered_map<unsigned int, unsigned int> carRemap;
    GameObject* carInstance = PrefabRegistry::Get().Instantiate(carGuid, world.Objects(), carRemap);
    REQUIRE(carInstance != nullptr);
    CHECK(carInstance->GetName() == "Car");
    REQUIRE(carInstance->GetChildren().size() == 1);

    GameObject* nestedWheel = carInstance->GetChildren()[0];
    CHECK(nestedWheel->GetName() == "Wheel");

    // The nested link must be preserved.
    auto* nestedPi = nestedWheel->GetComponent<PrefabInstance>();
    REQUIRE(nestedPi != nullptr);
    CHECK(nestedPi->GetPrefabGuid() == wheelGuid);

    // Nested transform resolved from the EDITED template, with fresh IDs.
    auto* wt = nestedWheel->GetComponent<Transform>();
    REQUIRE(wt != nullptr);
    CHECK(wt->GetX() == doctest::Approx(99.0f));
    CHECK(wt->GetY() == doctest::Approx(99.0f));
    CHECK(nestedWheel->GetID() != wheelInstance->GetID());
}

TEST_CASE("Nested Prefab: an override on the nested instance survives the parent prefab round-trip") {
    TempAssetRootFixture fixture;

    // Child prefab "Wheel": pos (5,5), rotation 0
    auto wheelTemplate = std::make_shared<GameObject>("Wheel");
    auto* wtT = wheelTemplate->AddComponent<Transform>(5.0f, 5.0f);
    wtT->SetRotation(0.0f);
    std::string wheelGuid = "nested-wheel-guid-3";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        wheelGuid, "wheel.prefab", SceneSerializer::SerializeSubtree(wheelTemplate.get())));

    // Car with a nested wheel; override the wheel's rotation to 90 inside the car.
    std::vector<std::shared_ptr<GameObject>> wheelObjs;
    GameObject* wheelInstance = nullptr;
    auto car = MakeCarWithNestedWheel(wheelGuid, wheelObjs, wheelInstance);
    wheelInstance->GetComponent<Transform>()->SetRotation(90.0f);

    std::string carGuid = "nested-car-guid-3";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        carGuid, "car.prefab", SceneSerializer::SerializeSubtree(car.get())));

    // Instantiate car; nested wheel must keep the rotation override but inherit position.
    World world;
    std::unordered_map<unsigned int, unsigned int> carRemap;
    GameObject* carInstance = PrefabRegistry::Get().Instantiate(carGuid, world.Objects(), carRemap);
    REQUIRE(carInstance != nullptr);
    REQUIRE(carInstance->GetChildren().size() == 1);

    auto* wt = carInstance->GetChildren()[0]->GetComponent<Transform>();
    REQUIRE(wt != nullptr);
    CHECK(wt->GetRotation() == doctest::Approx(90.0f)); // override preserved
    CHECK(wt->GetX() == doctest::Approx(5.0f));         // non-overridden value from template
}

TEST_CASE("Nested Prefab: applying a parent prefab instance preserves the nested link") {
    TempAssetRootFixture fixture;

    // Wheel prefab at (5,5)
    auto wheelTemplate = std::make_shared<GameObject>("Wheel");
    wheelTemplate->AddComponent<Transform>(5.0f, 5.0f);
    std::string wheelGuid = "apply-wheel-guid";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        wheelGuid, "wheel.prefab", SceneSerializer::SerializeSubtree(wheelTemplate.get())));

    // Compose Car with a nested wheel and save it as a prefab.
    std::vector<std::shared_ptr<GameObject>> wheelObjs;
    GameObject* wheelInstance = nullptr;
    auto car = MakeCarWithNestedWheel(wheelGuid, wheelObjs, wheelInstance);
    std::string carGuid = "apply-car-guid";
    REQUIRE(PrefabRegistry::Get().SavePrefab(
        carGuid, "car.prefab", SceneSerializer::SerializeSubtree(car.get())));

    // Instantiate the Car prefab as a live, linked instance.
    World world;
    std::unordered_map<unsigned int, unsigned int> carRemap;
    GameObject* carInstance = PrefabRegistry::Get().Instantiate(carGuid, world.Objects(), carRemap);
    REQUIRE(carInstance != nullptr);
    auto* carPi = carInstance->AddComponent<PrefabInstance>();
    carPi->SetPrefabGuid(carGuid);
    carPi->SetIdRemap(carRemap);
    REQUIRE(carInstance->GetChildren().size() == 1);

    // Move the car root, then apply the change back into the prefab template.
    carInstance->GetComponent<Transform>()->SetPosition(12.0f, 34.0f);
    carPi->SetModifications(PrefabUtil::GenerateModifications(
        carInstance, PrefabRegistry::Get().GetPrefabJson(carGuid), carRemap));
    REQUIRE(PrefabUtil::ApplyPrefab(carInstance));

    // Re-instantiate the applied prefab: the nested wheel link must survive.
    World world2;
    std::unordered_map<unsigned int, unsigned int> carRemap2;
    GameObject* carInstance2 = PrefabRegistry::Get().Instantiate(carGuid, world2.Objects(), carRemap2);
    REQUIRE(carInstance2 != nullptr);
    REQUIRE(carInstance2->GetChildren().size() == 1);

    auto* nestedPi = carInstance2->GetChildren()[0]->GetComponent<PrefabInstance>();
    REQUIRE(nestedPi != nullptr);
    CHECK(nestedPi->GetPrefabGuid() == wheelGuid);
    CHECK(carInstance2->GetComponent<Transform>()->GetX() == doctest::Approx(12.0f)); // applied change
}

TEST_CASE("Nested Prefab: a self-referential prefab fails atomically") {
    TempAssetRootFixture fixture;

    // Hand-craft a prefab whose subtree contains a nested instance of itself.
    std::string guid = "cyclic-guid";
    nlohmann::json root;
    root["name"] = "Cyclic"; root["id"] = 1; root["tag"] = "Untagged";
    root["layer"] = 0; root["active"] = true; root["parentId"] = -1;
    root["components"] = nlohmann::json::array();

    nlohmann::json child;
    child["prefabInstance"] = {
        {"guid", guid}, {"rootId", 2}, {"parentId", 1},
        {"modifications", nlohmann::json::array()}
    };

    nlohmann::json doc;
    doc["version"] = "1.0";
    doc["gameObjects"] = nlohmann::json::array({root, child});
    REQUIRE(PrefabRegistry::Get().SavePrefab(guid, "cyclic.prefab", doc));

    World world;
    std::unordered_map<unsigned int, unsigned int> remap;
    GameObject* inst = PrefabRegistry::Get().Instantiate(guid, world.Objects(), remap);

    // Must terminate (depth-guarded) without leaving a truncated instance.
    CHECK(inst == nullptr);
    CHECK(world.Objects().empty());
}
