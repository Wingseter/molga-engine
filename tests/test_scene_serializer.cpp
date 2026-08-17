#include "Core/SceneSerializer.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/BuiltinComponents.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/Camera.h"
#include "Scripting/Script.h"
#include "Scripting/ScriptManager.h"
#include "doctest.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

class DeserializeLifecycleProbeScript final : public Script {
public:
    SCRIPT_CLASS(DeserializeLifecycleProbeScript)

    static inline int awakeCalls = 0;
    static inline int enableCalls = 0;
    static inline int startCalls = 0;
    static inline int disableCalls = 0;
    static inline bool throwOnDisable = false;

    static void Reset(bool shouldThrowOnDisable) {
        awakeCalls = 0;
        enableCalls = 0;
        startCalls = 0;
        disableCalls = 0;
        throwOnDisable = shouldThrowOnDisable;
    }

    void Awake() override { ++awakeCalls; }
    void OnEnable() override { ++enableCalls; }
    void Start() override { ++startCalls; }
    void OnDisable() override {
        ++disableCalls;
        if (throwOnDisable) {
            throw std::runtime_error("OnDisable must not run while deserializing");
        }
    }
};

void RegisterDeserializeLifecycleProbe() {
    ScriptManager::Get().RegisterDynamic(
        "DeserializeLifecycleProbeScript",
        []() -> std::unique_ptr<Script> {
            return std::make_unique<DeserializeLifecycleProbeScript>();
        });
}

void CheckNoDeserializeLifecycleCalls() {
    CHECK(DeserializeLifecycleProbeScript::awakeCalls == 0);
    CHECK(DeserializeLifecycleProbeScript::enableCalls == 0);
    CHECK(DeserializeLifecycleProbeScript::startCalls == 0);
    CHECK(DeserializeLifecycleProbeScript::disableCalls == 0);
}

} // namespace

// ── Single GameObject round-trip ─────────────────────────────────────────────

TEST_CASE("SceneSerializer: serialize and deserialize single GameObject") {
    // Create a GameObject with components
    auto original = std::make_shared<GameObject>("TestObject");
    Transform* t = original->AddComponent<Transform>(100.0f, 200.0f);
    t->SetRotation(45.0f);
    t->SetScale(2.0f, 3.0f);

    original->SetTag("Player");
    original->SetLayer(4);

    BoxCollider2D* bc = original->AddComponent<BoxCollider2D>(64.0f, 32.0f);
    bc->SetOffset(5.0f, 10.0f);
    bc->SetTrigger(true);

    // Serialize
    std::string json = SceneSerializer::SerializeGameObject(original.get());
    CHECK(!json.empty());
    CHECK(json != "{}");

    // Deserialize
    auto restored = SceneSerializer::DeserializeGameObject(json);
    REQUIRE(restored != nullptr);
    CHECK(restored->GetName() == "TestObject");
    CHECK(restored->GetTag() == "Player");
    CHECK(restored->GetLayer() == 4);
    CHECK(restored->IsActive());

    // Verify Transform
    Transform* rt = restored->GetComponent<Transform>();
    REQUIRE(rt != nullptr);
    CHECK(rt->GetX() == doctest::Approx(100.0f));
    CHECK(rt->GetY() == doctest::Approx(200.0f));
    CHECK(rt->GetRotation() == doctest::Approx(45.0f));
    CHECK(rt->GetScale().x == doctest::Approx(2.0f));
    CHECK(rt->GetScale().y == doctest::Approx(3.0f));

    // Verify BoxCollider2D
    BoxCollider2D* rbc = restored->GetComponent<BoxCollider2D>();
    REQUIRE(rbc != nullptr);
    CHECK(rbc->GetSize().x == doctest::Approx(64.0f));
    CHECK(rbc->GetSize().y == doctest::Approx(32.0f));
    CHECK(rbc->GetOffset().x == doctest::Approx(5.0f));
    CHECK(rbc->GetOffset().y == doctest::Approx(10.0f));
    CHECK(rbc->IsTrigger() == true);
}

TEST_CASE("SceneSerializer: legacy Camera isMain loads and saves canonical output fields") {
    RegisterBuiltinComponents();
    const nlohmann::json legacyScene{
        {"version", "1.0"},
        {"name", "Legacy Camera"},
        {"gameObjects", nlohmann::json::array({
            {
                {"name", "Legacy Main"}, {"id", 7001u},
                {"tag", "Untagged"}, {"layer", 0}, {"active", true},
                {"parentId", -1},
                {"components", nlohmann::json::array({
                    {
                        {"type", "Camera"}, {"enabled", true},
                        {"isMain", true}, {"depth", 4},
                        {"postProcessEnabled", true},
                        {"postProcessProfileGuid", "legacy-profile"},
                    },
                })},
            },
        })},
    };

    std::vector<std::shared_ptr<GameObject>> loaded;
    REQUIRE(SceneSerializer::DeserializeScene(legacyScene, loaded));
    REQUIRE(loaded.size() == 1u);
    Camera* camera = loaded.front()->GetComponent<Camera>();
    REQUIRE(camera != nullptr);
    CHECK(camera->GetOutputRole() == CameraOutputRole::Primary);
    CHECK(camera->GetViewport() == CameraViewport{});
    CHECK(camera->GetCullingMask() == 0xFFFFFFFFu);
    CHECK(camera->IsPostProcessEnabled());
    CHECK(camera->GetPostProcessProfileGuid() == "legacy-profile");

    const nlohmann::json saved =
        SceneSerializer::SerializeScene(loaded, "Canonical Camera");
    REQUIRE(saved["gameObjects"].size() == 1u);
    const auto& components = saved["gameObjects"][0]["components"];
    const auto found = std::find_if(
        components.begin(), components.end(), [](const nlohmann::json& component) {
            return component.value("type", "") == "Camera";
    });
    REQUIRE(found != components.end());
    CHECK((*found)["outputRole"] == "Primary");
    const nlohmann::json fullViewport{
        {"x", 0.0f}, {"y", 0.0f}, {"width", 1.0f}, {"height", 1.0f}};
    CHECK((*found)["viewport"] == fullViewport);
    CHECK((*found)["cullingMask"].get<std::uint32_t>() == 0xFFFFFFFFu);
    CHECK_FALSE(found->contains("isMain"));
    CHECK((*found)["postProcessEnabled"] == true);
    CHECK((*found)["postProcessProfileGuid"] == "legacy-profile");
}

// ── Scene save/load round-trip ───────────────────────────────────────────────

TEST_CASE("SceneSerializer: scene save and load") {
    // Create test scene
    std::vector<std::shared_ptr<GameObject>> originalScene;

    auto obj1 = std::make_shared<GameObject>("Player");
    obj1->SetTag("Player");
    obj1->SetLayer(1);
    obj1->AddComponent<Transform>(10.0f, 20.0f);
    obj1->AddComponent<BoxCollider2D>(32.0f, 32.0f);
    originalScene.push_back(obj1);

    auto obj2 = std::make_shared<GameObject>("Enemy");
    obj2->SetTag("Enemy");
    obj2->SetLayer(2);
    obj2->SetActive(false);
    Transform* t2 = obj2->AddComponent<Transform>(50.0f, 60.0f);
    t2->SetRotation(90.0f);
    originalScene.push_back(obj2);

    // Save to temp file
    const fs::path tmpPath =
        fs::temp_directory_path() / "molga_test_scene.json";
    bool saved = SceneSerializer::SaveScene(tmpPath.string(), originalScene);
    CHECK(saved);

    // Load back
    std::vector<std::shared_ptr<GameObject>> loadedScene;
    bool loaded = SceneSerializer::LoadScene(tmpPath.string(), loadedScene);
    CHECK(loaded);
    REQUIRE(loadedScene.size() == 2);

    // Verify first object
    CHECK(loadedScene[0]->GetName() == "Player");
    CHECK(loadedScene[0]->GetTag() == "Player");
    CHECK(loadedScene[0]->GetLayer() == 1);
    CHECK(loadedScene[0]->IsActive());
    Transform* lt1 = loadedScene[0]->GetComponent<Transform>();
    REQUIRE(lt1 != nullptr);
    CHECK(lt1->GetX() == doctest::Approx(10.0f));
    CHECK(lt1->GetY() == doctest::Approx(20.0f));
    BoxCollider2D* lbc = loadedScene[0]->GetComponent<BoxCollider2D>();
    CHECK(lbc != nullptr);

    // Verify second object
    CHECK(loadedScene[1]->GetName() == "Enemy");
    CHECK(loadedScene[1]->GetTag() == "Enemy");
    CHECK(loadedScene[1]->GetLayer() == 2);
    CHECK(!loadedScene[1]->IsActive());
    Transform* lt2 = loadedScene[1]->GetComponent<Transform>();
    REQUIRE(lt2 != nullptr);
    CHECK(lt2->GetRotation() == doctest::Approx(90.0f));

    // Cleanup
    fs::remove(tmpPath);
}

// ── ID preservation ─────────────────────────────────────────────────────────

TEST_CASE("SceneSerializer: ID preservation") {
    auto obj = std::make_shared<GameObject>("IDTest");
    obj->AddComponent<Transform>(1.0f, 2.0f);
    unsigned int originalID = obj->GetID();

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    auto restored = SceneSerializer::DeserializeGameObject(json);

    REQUIRE(restored != nullptr);
    CHECK(restored->GetID() == originalID);
}

// ── enabled serialization ───────────────────────────────────────────────────

TEST_CASE("SceneSerializer: enabled serialization") {
    auto obj = std::make_shared<GameObject>("EnabledTest");
    obj->AddComponent<Transform>(5.0f, 10.0f);
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>(16.0f, 16.0f);
    bc->SetEnabled(false);

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    auto restored = SceneSerializer::DeserializeGameObject(json);

    REQUIRE(restored != nullptr);
    Transform* rt = restored->GetComponent<Transform>();
    REQUIRE(rt != nullptr);
    CHECK(rt->IsEnabled());  // default true

    BoxCollider2D* rbc = restored->GetComponent<BoxCollider2D>();
    REQUIRE(rbc != nullptr);
    CHECK(!rbc->IsEnabled());  // was disabled
}

TEST_CASE("SceneSerializer: disabled Script state loads without lifecycle callbacks") {
    RegisterDeserializeLifecycleProbe();
    DeserializeLifecycleProbeScript::Reset(true);

    const nlohmann::json scene = {
        {"version", "1.0"},
        {"name", "Disabled Script Load"},
        {"gameObjects", nlohmann::json::array({
            {
                {"name", "Probe"},
                {"id", 41001u},
                {"active", true},
                {"parentId", -1},
                {"components", nlohmann::json::array({
                    {
                        {"type", "DeserializeLifecycleProbeScript"},
                        {"enabled", false},
                    },
                })},
            },
        })},
    };

    std::vector<std::shared_ptr<GameObject>> loaded;
    CHECK_NOTHROW(SceneSerializer::DeserializeScene(scene, loaded));
    REQUIRE(loaded.size() == 1);
    auto* script = loaded.front()->GetComponent<DeserializeLifecycleProbeScript>();
    REQUIRE(script != nullptr);
    CHECK_FALSE(script->IsEnabled());
    CheckNoDeserializeLifecycleCalls();
}

TEST_CASE("World::Clone preserves a disabled Script without lifecycle callbacks") {
    RegisterDeserializeLifecycleProbe();
    DeserializeLifecycleProbeScript::Reset(false);

    World source;
    auto object = std::make_shared<GameObject>("Clone Probe");
    auto* sourceScript = static_cast<DeserializeLifecycleProbeScript*>(
        object->AddComponentRaw(new DeserializeLifecycleProbeScript()));
    REQUIRE(sourceScript != nullptr);

    // Explicit standalone SetEnabled keeps its normal lifecycle semantics.
    CHECK_NOTHROW(sourceScript->SetEnabled(false));
    CHECK(DeserializeLifecycleProbeScript::disableCalls == 1);
    source.Add(object);

    DeserializeLifecycleProbeScript::Reset(true);
    std::unique_ptr<World> clone;
    CHECK_NOTHROW(clone = source.Clone());
    REQUIRE(clone != nullptr);
    REQUIRE(clone->Objects().size() == 1);
    auto* clonedScript =
        clone->Objects().front()->GetComponent<DeserializeLifecycleProbeScript>();
    REQUIRE(clonedScript != nullptr);
    CHECK_FALSE(clonedScript->IsEnabled());
    CheckNoDeserializeLifecycleCalls();
}

// ── Parent-child serialization ──────────────────────────────────────────────

TEST_CASE("SceneSerializer: parent-child serialization") {
    std::vector<std::shared_ptr<GameObject>> originalScene;

    auto parent = std::make_shared<GameObject>("Parent");
    parent->AddComponent<Transform>(0.0f, 0.0f);
    originalScene.push_back(parent);

    auto child = std::make_shared<GameObject>("Child");
    child->AddComponent<Transform>(10.0f, 20.0f);
    child->SetParent(parent.get());
    originalScene.push_back(child);

    unsigned int parentID = parent->GetID();
    unsigned int childID = child->GetID();

    // Save
    const fs::path tmpPath =
        fs::temp_directory_path() / "molga_test_hierarchy.json";
    bool saved = SceneSerializer::SaveScene(tmpPath.string(), originalScene);
    CHECK(saved);

    // Load
    std::vector<std::shared_ptr<GameObject>> loadedScene;
    bool loaded = SceneSerializer::LoadScene(tmpPath.string(), loadedScene);
    CHECK(loaded);
    REQUIRE(loadedScene.size() == 2);

    // Find parent and child by ID
    GameObject* loadedParent = nullptr;
    GameObject* loadedChild = nullptr;
    for (auto& obj : loadedScene) {
        if (obj->GetID() == parentID) loadedParent = obj.get();
        if (obj->GetID() == childID) loadedChild = obj.get();
    }
    REQUIRE(loadedParent != nullptr);
    REQUIRE(loadedChild != nullptr);

    // Verify hierarchy
    CHECK(loadedChild->GetParent() == loadedParent);
    REQUIRE(loadedParent->GetChildren().size() == 1);
    CHECK(loadedParent->GetChildren()[0] == loadedChild);

    fs::remove(tmpPath);
}

// ── Error handling ───────────────────────────────────────────────────────────

TEST_CASE("SceneSerializer: invalid JSON") {
    auto result = SceneSerializer::DeserializeGameObject("not valid json{{{");
    CHECK(result == nullptr);
}

TEST_CASE("SceneSerializer: null GameObject") {
    std::string json = SceneSerializer::SerializeGameObject(nullptr);
    CHECK(json == "{}");
}

TEST_CASE("SceneSerializer: load nonexistent file") {
    std::vector<std::shared_ptr<GameObject>> objects;
    bool loaded = SceneSerializer::LoadScene("/tmp/nonexistent_test_file_12345.json", objects);
    CHECK(!loaded);
}

#include "Core/ProjectSettings.h"

TEST_CASE("ProjectSettings: serialization round-trip") {
    ProjectSettings settings;
    settings.SetDefaults();

    // Modify settings
    settings.tags.push_back("CustomTag");
    settings.layerNames[8] = "CustomLayer";
    settings.SetCollisionEnabled(0, 8, false);
    settings.sortingLayers.push_back("CustomSortingLayer");

    // Serialize
    nlohmann::json j = settings.Serialize();

    // Deserialize into another settings instance
    ProjectSettings restored;
    restored.Deserialize(j);

    // Verify
    CHECK(restored.tags.size() == 4);
    CHECK(restored.tags.back() == "CustomTag");
    CHECK(restored.GetLayerName(8) == "CustomLayer");
    CHECK_FALSE(restored.IsCollisionEnabled(0, 8));
    CHECK(restored.sortingLayers.size() == 4);
    CHECK(restored.sortingLayers.back() == "CustomSortingLayer");
}

TEST_CASE("ProjectSettings normalizes sorting layers while preserving authored order") {
    ProjectSettings settings;
    settings.Deserialize({
        {"sortingLayers", {"", "Foreground", "Default", "Foreground",
                            "Background", "Default", ""}}
    });
    CHECK(settings.sortingLayers ==
          std::vector<std::string>{"Foreground", "Default", "Background"});

    settings.Deserialize({{"sortingLayers", {"Foreground", "Background"}}});
    CHECK(settings.sortingLayers ==
          std::vector<std::string>{"Default", "Foreground", "Background"});

    settings.Deserialize({{"sortingLayers", nlohmann::json::array()}});
    CHECK(settings.sortingLayers == std::vector<std::string>{"Default"});
}
