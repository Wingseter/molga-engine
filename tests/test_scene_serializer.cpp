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

// ── Shared text component schemas (Task 8.1) ─────────────────────────────────
// 8.2 이전이므로 렌더 경로는 legacy 그대로다. 여기서 검증하는 것은 스키마와,
// 디스크에 이미 있는 scene을 조용히 다시 쓰지 않는다는 보증뿐이다.

#include "ECS/Components/TextRenderer2D.h"
#include "ECS/Components/UILabel.h"

namespace {

// scene 문서 안에서 이름으로 컴포넌트 payload를 찾는다. 실패를 nullptr가 아니라
// 빈 json으로 돌려주면 뒤따르는 CHECK가 통과해 버리므로, 없으면 REQUIRE로 죽는다.
const nlohmann::json& FindComponentJson(const nlohmann::json& sceneJson,
                                        const char* typeName) {
    REQUIRE(sceneJson.contains("gameObjects"));
    for (const auto& objJson : sceneJson["gameObjects"]) {
        if (!objJson.contains("components")) continue;
        for (const auto& compJson : objJson["components"]) {
            if (compJson.value("type", std::string{}) == typeName) {
                return compJson;
            }
        }
    }
    REQUIRE_MESSAGE(false, "component not present in scene document");
    static const nlohmann::json kUnreachable = nlohmann::json::object();
    return kUnreachable;
}

// 8.1 이전 에디터가 실제로 디스크에 쓰던 모양. 새로 저장한 scene을 다시 읽는
// 시험은 migration 경로를 한 번도 밟지 않으므로, 문자 그대로의 옛 문서가 필요하다.
nlohmann::json LegacyTextSceneDocument() {
    return {
        {"version", "1.0"},
        {"name", "LegacyText"},
        {"gameObjects",
         {{{"name", "LegacyText"},
           {"id", 41},
           {"tag", "Untagged"},
           {"layer", 0},
           {"active", true},
           {"parentId", -1},
           {"components",
            {{{"type", "UILabel"},
              {"enabled", true},
              {"text", "제목"},
              {"fontGuid", "font-a"},
              {"fontSizePx", 32.0},
              {"lineSpacing", 1.5},
              {"color", {0.5, 0.25, 0.125, 1.0}},
              // 두 정렬은 일부러 서로 다른 값이다. 같은 서수로 맞추면 저장에서
              // 두 키가 뒤바뀌어도 아래의 문자 그대로의 비교가 통과한다.
              {"horizontalAlignment", 2},
              {"verticalAlignment", 0},
              {"sortingOrder", 7}},
             {{"type", "TextRenderer2D"},
              {"enabled", true},
              {"text", "world"},
              {"color", {1.0, 0.5, 0.25, 1.0}},
              {"scale", 2.0},
              {"alignment", 1},
              {"fontGuid", "font-b"},
              {"fontSizePx", 12.0},
              {"lineSpacing", 1.25},
              {"fontName", "arial"},
              {"sortingLayer", "Default"},
              {"sortingOrder", 9},
              {"sortMode", "Fixed"},
              {"ySortOffset", 0.0}}}}}}}};
}

} // namespace

TEST_CASE("current world text preserves unbounded world contract") {
    TextRenderer2D text;
    text.SetFontFamilyGuid("family-a");
    CHECK(text.WrapMode() == molga::text::TextWrapMode::NoWrap);
    CHECK(text.OverflowMode() == molga::text::TextOverflowMode::Overflow);
    CHECK_FALSE(text.HasAuthoredLayoutBounds());
}

TEST_CASE("world text schema invents no bounds and no Transform duplicates") {
    TextRenderer2D text;
    text.SetFontFamilyGuid("family-a");
    nlohmann::json saved;
    text.Serialize(saved);
    // 저작된 bound가 없다는 계약의 증인은 스키마다. 폭/높이나 wrap/overflow 키가
    // 생기는 순간 unbounded 계약은 말만 남는다. 위치/회전/월드 스케일은
    // 형제 Transform이 권한을 갖는다.
    for (const char* key : {"width", "height", "layoutWidth", "layoutHeight",
                            "bounds", "wrap", "overflow", "maxLines",
                            "position", "rotation", "worldScale"}) {
        CAPTURE(key);
        CHECK_FALSE(saved.contains(key));
    }
    // 반대편 증인: 월드 배치와 무관한 local logical 크기/스케일은 계속 남는다.
    CHECK(saved.contains("fontSizePx"));
    CHECK(saved.contains("scale"));
}

TEST_CASE("authored TextRenderer2D persists schema 2 text identity with legacy keys") {
    TextRenderer2D text;
    text.SetFontFamilyGuid("family-a");
    text.SetLocale("ko-KR");
    text.SetBaseDirection(molga::text::BaseDirection::RightToLeft);
    text.SetFontGuid("font-b");
    text.SetFontName("arial");
    text.SetFontSizePx(12.0f);
    text.SetComponentScale(2.0f);
    text.SetSortingOrder(9);
    text.SetText("world");

    nlohmann::json saved;
    text.Serialize(saved);
    CHECK(saved["schemaVersion"] == 2);
    CHECK(saved["fontFamilyGuid"] == "family-a");
    CHECK(saved["locale"] == "ko-KR");
    CHECK(saved["baseDirection"] == "RTL");
    CHECK(saved["fontGuid"] == "font-b");
    CHECK(saved["fontName"] == "arial");

    TextRenderer2D reloaded;
    reloaded.Deserialize(saved);
    CHECK_FALSE(reloaded.LoadedLegacyFontGuid());
    CHECK(reloaded.GetFontFamilyGuid() == "family-a");
    CHECK(reloaded.GetLocale() == "ko-KR");
    CHECK(reloaded.GetBaseDirection() == molga::text::BaseDirection::RightToLeft);
    CHECK(reloaded.GetFontGuid() == "font-b");
    CHECK(reloaded.GetFontName() == "arial");
    CHECK(reloaded.GetFontSizePx() == doctest::Approx(12.0f));
    CHECK(reloaded.GetComponentScale() == doctest::Approx(2.0f));
    CHECK(reloaded.GetSortingOrder() == 9);
}

TEST_CASE("TextRenderer2D defaults preserve the legacy world text contract") {
    const TextRenderer2D text;
    CHECK(text.GetLocale() == "und");
    CHECK(text.GetBaseDirection() == molga::text::BaseDirection::Auto);
    CHECK(text.GetFontFamilyGuid().empty());
    CHECK_FALSE(text.LoadedLegacyFontGuid());
}

TEST_CASE("legacy text scene document loads and saves in its legacy shape") {
    RegisterBuiltinComponents();
    const nlohmann::json legacyScene = LegacyTextSceneDocument();

    std::vector<std::shared_ptr<GameObject>> restored;
    REQUIRE(SceneSerializer::DeserializeScene(legacyScene, restored));
    REQUIRE(restored.size() == 1);

    auto* label = restored[0]->GetComponent<UILabel>();
    REQUIRE(label != nullptr);
    CHECK(label->LoadedLegacyFontGuid());
    CHECK(label->GetFontGuid() == "font-a");
    CHECK(label->GetText() == "제목");
    CHECK(label->GetFontFamilyGuid().empty());
    // legacy 파일에는 없던 필드는 legacy 동작과 같은 기본값으로만 나타난다.
    CHECK(label->GetLocale() == "und");
    CHECK(label->GetWrapMode() == molga::text::TextWrapMode::NoWrap);

    auto* text = restored[0]->GetComponent<TextRenderer2D>();
    REQUIRE(text != nullptr);
    CHECK(text->LoadedLegacyFontGuid());
    CHECK(text->GetFontGuid() == "font-b");
    CHECK(text->GetFontName() == "arial");
    CHECK(text->GetFontFamilyGuid().empty());

    // 저장한 결과가 읽은 payload와 키 하나까지 같아야 migration 전 scene이
    // 조용히 다시 쓰이지 않는다.
    const nlohmann::json resaved = SceneSerializer::SerializeScene(restored, "LegacyText");
    CHECK(FindComponentJson(resaved, "UILabel") ==
          FindComponentJson(legacyScene, "UILabel"));
    CHECK(FindComponentJson(resaved, "TextRenderer2D") ==
          FindComponentJson(legacyScene, "TextRenderer2D"));
}

TEST_CASE("newly authored text components save through the scene as schema 2") {
    RegisterBuiltinComponents();
    auto object = std::make_shared<GameObject>("AuthoredText");
    auto* label = object->AddComponent<UILabel>();
    label->SetFontFamilyGuid("family-a");
    label->SetWrapMode(molga::text::TextWrapMode::Word);
    auto* text = object->AddComponent<TextRenderer2D>();
    text->SetFontFamilyGuid("family-b");
    text->SetLocale("ja-JP");

    std::vector<std::shared_ptr<GameObject>> source{object};
    const nlohmann::json sceneJson = SceneSerializer::SerializeScene(source, "AuthoredText");
    CHECK(FindComponentJson(sceneJson, "UILabel").at("schemaVersion") == 2);
    CHECK(FindComponentJson(sceneJson, "TextRenderer2D").at("schemaVersion") == 2);

    std::vector<std::shared_ptr<GameObject>> restored;
    REQUIRE(SceneSerializer::DeserializeScene(sceneJson, restored));
    REQUIRE(restored.size() == 1);
    auto* restoredLabel = restored[0]->GetComponent<UILabel>();
    REQUIRE(restoredLabel != nullptr);
    CHECK_FALSE(restoredLabel->LoadedLegacyFontGuid());
    CHECK(restoredLabel->GetFontFamilyGuid() == "family-a");
    CHECK(restoredLabel->GetWrapMode() == molga::text::TextWrapMode::Word);
    auto* restoredText = restored[0]->GetComponent<TextRenderer2D>();
    REQUIRE(restoredText != nullptr);
    CHECK_FALSE(restoredText->LoadedLegacyFontGuid());
    CHECK(restoredText->GetFontFamilyGuid() == "family-b");
    CHECK(restoredText->GetLocale() == "ja-JP");
}

TEST_CASE("authoring a family migrates TextRenderer2D off the legacy shape") {
    // UILabel 쪽에만 이 증인이 있으면, 표식을 전혀 내려놓지 않는 setter도 통과한다.
    // 그 구현에서는 legacy scene에서 읽은 월드 텍스트에 family를 저작해도
    // 저장이 legacy 가지를 타서 저작한 family가 조용히 사라진다.
    TextRenderer2D text;
    text.Deserialize({{"type", "TextRenderer2D"}, {"fontGuid", "font-b"}, {"fontSizePx", 12.0}});
    REQUIRE(text.LoadedLegacyFontGuid());
    text.SetFontFamilyGuid("family-b");
    CHECK_FALSE(text.LoadedLegacyFontGuid());
    nlohmann::json migrated;
    text.Serialize(migrated);
    CHECK(migrated["schemaVersion"] == 2);
    CHECK(migrated["fontFamilyGuid"] == "family-b");
    CHECK(migrated["fontGuid"] == "font-b");
}

TEST_CASE("a family-less schema 2 TextRenderer2D does not downgrade") {
    // 표식을 payload가 아니라 저작 상태에서 유도하면, 갓 만든 월드 텍스트를
    // 저장한 유효한 schema 2 문서가 다시 읽힐 때 legacy로 뒤집히고 그 다음
    // 저장에서 schemaVersion/fontFamilyGuid/locale/baseDirection이 사라진다.
    TextRenderer2D authored;
    nlohmann::json saved;
    authored.Serialize(saved);
    REQUIRE(saved["schemaVersion"] == 2);
    REQUIRE(saved["fontFamilyGuid"] == "");

    TextRenderer2D reloaded;
    reloaded.Deserialize(saved);
    CHECK_FALSE(reloaded.LoadedLegacyFontGuid());
    nlohmann::json resaved;
    reloaded.Serialize(resaved);
    CHECK(resaved == saved);
}

TEST_CASE("a schema 1 TextRenderer2D payload restores the legacy state exactly") {
    // 실행 취소는 기존 컴포넌트에 스냅샷을 다시 Deserialize한다. schema 2 전용
    // 필드가 남으면 표식은 legacy인데 메모리는 저작된 family/locale을 들고 있는,
    // 저장 형식과 다른 텍스트 정체성이 남는다.
    TextRenderer2D text;
    text.SetFontFamilyGuid("family-b");
    text.SetLocale("ja-JP");
    text.SetBaseDirection(molga::text::BaseDirection::RightToLeft);
    REQUIRE_FALSE(text.LoadedLegacyFontGuid());

    text.Deserialize({{"type", "TextRenderer2D"}, {"fontGuid", "font-b"}, {"fontSizePx", 12.0}});
    CHECK(text.LoadedLegacyFontGuid());
    CHECK(text.GetFontFamilyGuid().empty());
    CHECK(text.GetLocale() == "und");
    CHECK(text.GetBaseDirection() == molga::text::BaseDirection::Auto);
}

TEST_CASE("a TextRenderer2D locale read from disk is normalized like an authored one") {
    // 정규화가 setter에만 있으면 손으로 편집한 scene의 빈 태그가 곧장 분석
    // 계층으로 새어 들어간다.
    TextRenderer2D text;
    text.Deserialize({{"type", "TextRenderer2D"}, {"schemaVersion", 2}, {"locale", ""}});
    CHECK(text.GetLocale() == "und");
}

TEST_CASE("both text components spell base direction with the same tokens") {
    // 두 컴포넌트가 하나의 텍스트 서비스를 기술하는 이상 같은 정책이 파일에서
    // 다른 이름으로 보이면 안 된다. 표가 두 벌로 복제되어 있고 공유 정의가
    // 없으므로, 그 계약을 유지하는 기계는 이 시험뿐이다.
    struct DirectionCase {
        molga::text::BaseDirection value;
        const char* token;
    };
    const DirectionCase directions[] = {
        {molga::text::BaseDirection::Auto, "Auto"},
        {molga::text::BaseDirection::LeftToRight, "LTR"},
        {molga::text::BaseDirection::RightToLeft, "RTL"}};
    for (const auto& testCase : directions) {
        CAPTURE(testCase.token);
        TextRenderer2D text;
        text.SetFontFamilyGuid("family-b");
        text.SetBaseDirection(testCase.value);
        nlohmann::json savedText;
        text.Serialize(savedText);
        CHECK(savedText.at("baseDirection") == testCase.token);
        TextRenderer2D reloaded;
        reloaded.Deserialize(savedText);
        CHECK(reloaded.GetBaseDirection() == testCase.value);

        UILabel label;
        label.SetFontFamilyGuid("family-a");
        label.SetBaseDirection(testCase.value);
        nlohmann::json savedLabel;
        label.Serialize(savedLabel);
        CHECK(savedLabel.at("baseDirection") == savedText.at("baseDirection"));
    }
}

TEST_CASE("clearing a TextRenderer2D family is not a migration and an empty locale is und") {
    TextRenderer2D text;
    text.Deserialize({{"type", "TextRenderer2D"}, {"fontGuid", "font-b"}});
    REQUIRE(text.LoadedLegacyFontGuid());
    // family를 비우는 것은 legacy 지목을 대체할 값을 공급하지 않는다.
    text.SetFontFamilyGuid("");
    CHECK(text.LoadedLegacyFontGuid());

    TextRenderer2D authored;
    authored.SetLocale("ja-JP");
    REQUIRE(authored.GetLocale() == "ja-JP");
    authored.SetLocale("");
    CHECK(authored.GetLocale() == "und");
}

TEST_CASE("the legacy bitmap em applies only to a schema 1 world text document") {
    // legacy 문서는 8픽셀 em을 유지해야 migration 전 scene의 글자 크기가 바뀌지
    // 않는다. 반대로 schema 2 문서는 키를 잃어도 그 값을 물려받으면 안 된다.
    TextRenderer2D legacyText;
    legacyText.Deserialize({{"type", "TextRenderer2D"}, {"text", "legacy"}});
    CHECK(legacyText.GetFontSizePx() == doctest::Approx(8.0f));

    TextRenderer2D schema2Text;
    schema2Text.Deserialize(
        {{"type", "TextRenderer2D"}, {"schemaVersion", 2}, {"text", "current"}});
    CHECK(schema2Text.GetFontSizePx() == doctest::Approx(16.0f));
}
