#include "Editor/Properties/EditorPropertyDescriptor.h"
#include "Core/AssetDatabase.h"
#include "Core/ProjectSettings.h"
#include "ECS/Component.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/SpriteRenderer.h"
#include "Scripting/Script.h"
#include "doctest.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace {

class DescriptorFixtureComponent : public Component {
public:
    COMPONENT_TYPE(DescriptorFixtureComponent)

    bool visible = true;
    int order = 0;
    float opacity = 1.0f;
    Vector2 offset{0.0f, 0.0f};
    Color tint = Color::White();
    std::string textureGuid;

    void Serialize(nlohmann::json& json) const override {
        json["visible"] = visible;
        json["order"] = order;
        json["opacity"] = opacity;
        json["offset"] = {offset.x, offset.y};
        json["tint"] = {tint.r, tint.g, tint.b, tint.a};
        json["textureGuid"] = textureGuid;
        json["structural"] = nlohmann::json::array({
            nlohmann::json{{"cell", 1}}, nlohmann::json{{"cell", 2}}});
    }

    void Deserialize(const nlohmann::json& json) override {
        visible = json.value("visible", visible);
        order = json.value("order", order);
        opacity = json.value("opacity", opacity);
        if (json.contains("offset")) {
            offset = {json["offset"][0].get<float>(), json["offset"][1].get<float>()};
        }
        if (json.contains("tint")) {
            tint = {json["tint"][0].get<float>(), json["tint"][1].get<float>(),
                    json["tint"][2].get<float>(), json["tint"][3].get<float>()};
        }
        textureGuid = json.value("textureGuid", textureGuid);
    }
};

class DescriptorFixtureScript : public Script {
public:
    SCRIPT_CLASS(DescriptorFixtureScript)

    float speed = 1.0f;
    Vector2 direction{2.0f, 3.0f};
    Color tint{0.1f, 0.2f, 0.3f, 0.4f};
    PrefabRef prefab;
    int movement = 0;

    void RegisterFields(ScriptFieldRegistry& registry) override {
        registry.Float("speed", &speed)
            .Vec2("direction", &direction)
            .Color("tint", &tint)
            .Prefab("prefab", &prefab)
            .Enum("movement", &movement, {"Idle", "Walk", "Run"});
    }
};

class ReplacingDescriptorComponent : public Component {
public:
    COMPONENT_TYPE(ReplacingDescriptorComponent)

    int value = 0;
    mutable bool replacePeerOnSerialize = false;
    bool replacePeerOnDeserialize = false;
    GameObject* peer = nullptr;

    void Serialize(nlohmann::json& json) const override {
        json["value"] = value;
        if (!replacePeerOnSerialize || !peer) return;
        replacePeerOnSerialize = false;
        peer->RemoveComponent<ReplacingDescriptorComponent>();
        auto* replacement = peer->AddComponent<ReplacingDescriptorComponent>();
        replacement->value = 66;
    }

    void Deserialize(const nlohmann::json& json) override {
        value = json.value("value", value);
        if (!replacePeerOnDeserialize || !peer) return;
        replacePeerOnDeserialize = false;
        peer->RemoveComponent<ReplacingDescriptorComponent>();
        auto* replacement = peer->AddComponent<ReplacingDescriptorComponent>();
        replacement->value = 77;
    }
};

const molga::EditorPropertyDescriptor& Find(
    const std::vector<molga::EditorPropertyDescriptor>& descriptors,
    const char* key) {
    auto it = std::find_if(descriptors.begin(), descriptors.end(),
        [key](const auto& descriptor) { return descriptor.key == key; });
    REQUIRE(it != descriptors.end());
    return *it;
}

bool Has(const std::vector<molga::EditorPropertyDescriptor>& descriptors,
         const char* key) {
    return std::any_of(descriptors.begin(), descriptors.end(),
        [key](const auto& descriptor) { return descriptor.key == key; });
}

struct SortingLayersGuard {
    std::vector<std::string> saved = ProjectSettings::Get().sortingLayers;
    ~SortingLayersGuard() { ProjectSettings::Get().sortingLayers = std::move(saved); }
};

} // namespace

TEST_CASE("descriptor hierarchy exposes scalar and per-axis values but not structures") {
    DescriptorFixtureComponent component;
    const auto descriptors = molga::DescribeEditorProperties(component);
    CHECK(Find(descriptors, "visible").type == molga::EditorPropertyType::Bool);
    CHECK(Find(descriptors, "order").type == molga::EditorPropertyType::Integer);
    CHECK(Find(descriptors, "opacity").type == molga::EditorPropertyType::Float);
    CHECK(Find(descriptors, "offset.0").channel == 0);
    CHECK(Find(descriptors, "offset.1").channel == 1);
    CHECK(Find(descriptors, "tint.3").channel == 3);
    CHECK(Find(descriptors, "textureGuid").type == molga::EditorPropertyType::AssetGuid);
    CHECK(std::none_of(descriptors.begin(), descriptors.end(), [](const auto& descriptor) {
        return descriptor.key.find("structural") != std::string::npos;
    }));
}

TEST_CASE("common descriptor mixed state and axis apply preserve other axes") {
    DescriptorFixtureComponent first;
    DescriptorFixtureComponent second;
    first.offset = {1.0f, 10.0f};
    second.offset = {2.0f, 20.0f};
    std::vector<Component*> components{&first, &second};
    const auto descriptors = molga::CommonEditorProperties(components);
    const auto& x = Find(descriptors, "offset.0");
    CHECK(molga::HasMixedEditorPropertyValue(x, components));

    CHECK(molga::ApplyEditorPropertyValue(x, components, 8.0) == 2u);
    CHECK(first.offset.x == doctest::Approx(8.0f));
    CHECK(second.offset.x == doctest::Approx(8.0f));
    CHECK(first.offset.y == doctest::Approx(10.0f));
    CHECK(second.offset.y == doctest::Approx(20.0f));
    CHECK_FALSE(molga::HasMixedEditorPropertyValue(x, components));
}

TEST_CASE("float mixed comparison uses descriptor epsilon") {
    DescriptorFixtureComponent first;
    DescriptorFixtureComponent second;
    first.opacity = 1.0f;
    second.opacity = 1.0f + 5.0e-6f;
    std::vector<Component*> components{&first, &second};
    const auto descriptors = molga::CommonEditorProperties(components);
    CHECK_FALSE(molga::HasMixedEditorPropertyValue(
        Find(descriptors, "opacity"), components));
}

TEST_CASE("stable multi property apply skips a target replaced by an earlier callback") {
    auto firstObject = std::make_shared<GameObject>("First replacer");
    auto secondObject = std::make_shared<GameObject>("Second victim");
    auto* first = firstObject->AddComponent<ReplacingDescriptorComponent>();
    auto* second = secondObject->AddComponent<ReplacingDescriptorComponent>();
    first->value = 1;
    second->value = 2;

    std::vector<molga::EditorComponentIdentity> identities{
        molga::CaptureEditorComponentIdentity(*first),
        molga::CaptureEditorComponentIdentity(*second),
    };
    const std::vector<std::shared_ptr<GameObject>> objects{
        firstObject, secondObject};
    const molga::EditorComponentResolver resolve =
        [&objects](const molga::EditorComponentIdentity& identity) -> Component* {
            for (const auto& object : objects) {
                if (!object || object->GetID() != identity.objectId) continue;
                for (Component* component : object->GetComponents()) {
                    if (component &&
                        component->GetRuntimeTypeID() == identity.runtimeTypeId &&
                        component->GetInstanceID() == identity.instanceId &&
                        component->GetTypeName() == identity.componentType) {
                        return component;
                    }
                }
            }
            return nullptr;
        };

    const auto descriptors = molga::CommonEditorProperties(identities, resolve);
    const auto& value = Find(descriptors, "value");
    const std::uint64_t displayVictimInstanceId = second->GetInstanceID();
    first->peer = secondObject.get();
    first->replacePeerOnSerialize = true;
    CHECK(molga::HasMixedEditorPropertyValue(value, identities, resolve));
    CHECK(resolve(identities[1]) == nullptr);
    auto* displayReplacement =
        secondObject->GetComponent<ReplacingDescriptorComponent>();
    REQUIRE(displayReplacement != nullptr);
    CHECK(displayReplacement->GetInstanceID() != displayVictimInstanceId);
    CHECK(displayReplacement->value == 66);

    identities[1] = molga::CaptureEditorComponentIdentity(*displayReplacement);
    const std::uint64_t replacedInstanceId = displayReplacement->GetInstanceID();
    first->replacePeerOnDeserialize = true;

    std::size_t changed = 0;
    CHECK_NOTHROW(changed = molga::ApplyEditorPropertyValue(
        value, identities, resolve, std::int64_t{9}));
    CHECK(changed == 1u);
    CHECK(first->value == 9);
    auto* replacement =
        secondObject->GetComponent<ReplacingDescriptorComponent>();
    REQUIRE(replacement != nullptr);
    CHECK(replacement->GetInstanceID() != replacedInstanceId);
    CHECK(replacement->value == 77);
    CHECK(resolve(identities[1]) == nullptr);
}

TEST_CASE("non-active asset and popup changes request an immediate transaction") {
    using molga::EditorPropertyType;
    CHECK(molga::ShouldCommitEditorPropertyImmediately(
        EditorPropertyType::AssetGuid, true, false, false, false));
    CHECK(molga::ShouldCommitEditorPropertyImmediately(
        EditorPropertyType::Enum, true, false, false, false));
    CHECK(molga::ShouldCommitEditorPropertyImmediately(
        EditorPropertyType::Bool, true, false, false, false));
    CHECK_FALSE(molga::ShouldCommitEditorPropertyImmediately(
        EditorPropertyType::AssetGuid, false, false, false, false));
    CHECK_FALSE(molga::ShouldCommitEditorPropertyImmediately(
        EditorPropertyType::AssetGuid, true, true, true, true));
    CHECK_FALSE(molga::ShouldCommitEditorPropertyImmediately(
        EditorPropertyType::Float, true, false, true, false));
}

TEST_CASE("registered Script fields use the same typed per-channel descriptors") {
    DescriptorFixtureScript first;
    DescriptorFixtureScript second;
    first.direction = {1.0f, 2.0f};
    second.direction = {3.0f, 4.0f};
    std::vector<Component*> scripts{&first, &second};
    const auto descriptors = molga::CommonEditorProperties(scripts);
    const auto& directionY = Find(descriptors, "fields.direction.1");
    const auto& tintA = Find(descriptors, "fields.tint.3");
    const auto& prefab = Find(descriptors, "fields.prefab");
    CHECK(directionY.type == molga::EditorPropertyType::Float);
    CHECK(tintA.channel == 3);
    CHECK(prefab.type == molga::EditorPropertyType::AssetGuid);
    CHECK(prefab.assetType == "PrefabImporter");

    CHECK(molga::ApplyEditorPropertyValue(directionY, scripts, 9.0) == 2u);
    CHECK(first.direction.x == doctest::Approx(1.0f));
    CHECK(second.direction.x == doctest::Approx(3.0f));
    CHECK(first.direction.y == doctest::Approx(9.0f));
    CHECK(second.direction.y == doctest::Approx(9.0f));

    first.tint = {0.1f, 0.2f, 0.3f, 0.4f};
    second.tint = {0.9f, 0.8f, 0.7f, 0.6f};
    CHECK(molga::ApplyEditorPropertyValue(tintA, scripts, 1.0) == 2u);
    CHECK(first.tint.r == doctest::Approx(0.1f));
    CHECK(second.tint.r == doctest::Approx(0.9f));
    CHECK(first.tint.a == doctest::Approx(1.0f));
    CHECK(second.tint.a == doctest::Approx(1.0f));
    CHECK(tintA.label == "Tint A");
}

TEST_CASE("enum descriptors carry labels and authored values") {
    DescriptorFixtureScript first;
    DescriptorFixtureScript second;
    std::vector<Component*> scripts{&first, &second};
    const auto scriptDescriptors = molga::CommonEditorProperties(scripts);
    const auto& movement = Find(scriptDescriptors, "fields.movement");
    CHECK(movement.type == molga::EditorPropertyType::Enum);
    CHECK((movement.enumLabels ==
           std::vector<std::string>{"Idle", "Walk", "Run"}));
    REQUIRE(movement.enumValues.size() == 3);
    CHECK(molga::ApplyEditorPropertyValue(
              movement, scripts, std::int64_t{2}) == 2u);
    CHECK(first.movement == 2);
    CHECK(second.movement == 2);
    CHECK(molga::ApplyEditorPropertyValue(
              movement, scripts, std::int64_t{9}) == 0u);
    CHECK(first.movement == 2);

    Rigidbody2D body;
    const auto bodyDescriptors = molga::DescribeEditorProperties(body);
    const auto& bodyType = Find(bodyDescriptors, "bodyType");
    CHECK(bodyType.type == molga::EditorPropertyType::Enum);
    CHECK((bodyType.enumLabels ==
           std::vector<std::string>{"Static", "Kinematic", "Dynamic"}));

    SpriteRenderer sprite;
    const auto spriteDescriptors = molga::DescribeEditorProperties(sprite);
    const auto& sizeMode = Find(spriteDescriptors, "sizeMode");
    const auto& materialTexture = Find(
        spriteDescriptors, "material.mainTextureGuid");
    const auto& materialBlend = Find(spriteDescriptors, "material.blendMode");
    CHECK(sizeMode.type == molga::EditorPropertyType::Enum);
    REQUIRE(sizeMode.enumValues.size() == 2);
    CHECK(std::get<std::string>(sizeMode.enumValues[1]) == "Native");
    CHECK(materialTexture.type == molga::EditorPropertyType::AssetGuid);
    CHECK(materialTexture.assetType == "TextureImporter");
    CHECK(materialBlend.type == molga::EditorPropertyType::Enum);
    CHECK(materialBlend.enumLabels.size() == 4);
}

TEST_CASE("world sorting descriptors follow project layer order and active sort mode") {
    SortingLayersGuard restore;
    auto& settings = ProjectSettings::Get();
    settings.sortingLayers = {"Background", "Default", "Foreground"};

    SpriteRenderer sprite;
    sprite.SetSortingLayer("Foreground");
    auto descriptors = molga::DescribeEditorProperties(sprite);
    const auto& layer = Find(descriptors, "sortingLayer");
    const auto& mode = Find(descriptors, "sortMode");
    CHECK(layer.type == molga::EditorPropertyType::Enum);
    CHECK((layer.enumLabels ==
           std::vector<std::string>{"Background", "Default", "Foreground"}));
    REQUIRE(layer.enumValues.size() == 3);
    CHECK(std::get<std::string>(layer.enumValues[0]) == "Background");
    CHECK(std::get<std::string>(layer.enumValues[2]) == "Foreground");
    CHECK(mode.type == molga::EditorPropertyType::Enum);
    CHECK((mode.enumLabels == std::vector<std::string>{"Fixed", "YAxis"}));
    CHECK(std::get<std::string>(mode.enumValues[0]) == "Fixed");
    CHECK(std::get<std::string>(mode.enumValues[1]) == "YAxis");
    CHECK_FALSE(Has(descriptors, "ySortOffset"));

    settings.sortingLayers = {"Foreground", "Default", "Background"};
    descriptors = molga::DescribeEditorProperties(sprite);
    CHECK((Find(descriptors, "sortingLayer").enumLabels ==
           std::vector<std::string>{"Foreground", "Default", "Background"}));

    sprite.SetSortMode(molga::SortMode2D::YAxis);
    descriptors = molga::DescribeEditorProperties(sprite);
    CHECK(Has(descriptors, "ySortOffset"));
}

TEST_CASE("missing sorting layer remains an exact selectable authored value") {
    SortingLayersGuard restore;
    ProjectSettings::Get().sortingLayers = {"Default", "Foreground"};

    SpriteRenderer sprite;
    const std::string missing = "Deleted Layer / exact";
    sprite.SetSortingLayer(missing);
    const auto descriptors = molga::DescribeEditorProperties(sprite);
    const auto& layer = Find(descriptors, "sortingLayer");
    REQUIRE(layer.type == molga::EditorPropertyType::Enum);
    const auto missingOption = std::find(
        layer.enumValues.begin(), layer.enumValues.end(),
        molga::EditorPropertyValue{missing});
    REQUIRE(missingOption != layer.enumValues.end());
    const std::size_t index = static_cast<std::size_t>(
        std::distance(layer.enumValues.begin(), missingOption));
    REQUIRE(index < layer.enumLabels.size());
    CHECK(layer.enumLabels[index].find("Missing") != std::string::npos);
    CHECK(std::get<std::string>(layer.getter(sprite)) == missing);
    CHECK(molga::IsEditorPropertyValueValid(layer, missing));

    nlohmann::json serialized;
    sprite.Serialize(serialized);
    CHECK(serialized["sortingLayer"] == missing);
}

TEST_CASE("mixed world sorting descriptors merge missing options and batch apply") {
    SortingLayersGuard restore;
    ProjectSettings::Get().sortingLayers =
        {"Foreground", "Default", "Background"};

    auto firstObject = std::make_shared<GameObject>("First Sprite");
    auto secondObject = std::make_shared<GameObject>("Second Sprite");
    SpriteRenderer* first = firstObject->AddComponent<SpriteRenderer>();
    SpriteRenderer* second = secondObject->AddComponent<SpriteRenderer>();
    first->SetSortingLayer("Missing First");
    second->SetSortingLayer("Missing Second");
    first->SetSortMode(molga::SortMode2D::Fixed);
    second->SetSortMode(molga::SortMode2D::YAxis);
    second->SetYSortOffset(9.0f);
    const std::vector<std::shared_ptr<GameObject>> objects{
        firstObject, secondObject};
    const std::vector<molga::EditorComponentIdentity> identities{
        molga::CaptureEditorComponentIdentity(*first),
        molga::CaptureEditorComponentIdentity(*second)};
    const molga::EditorComponentResolver resolve =
        [&objects](const molga::EditorComponentIdentity& identity) -> Component* {
            for (const auto& object : objects) {
                if (!object || object->GetID() != identity.objectId) continue;
                for (Component* component : object->GetComponents()) {
                    if (component &&
                        component->GetRuntimeTypeID() == identity.runtimeTypeId &&
                        component->GetInstanceID() == identity.instanceId &&
                        component->GetTypeName() == identity.componentType) {
                        return component;
                    }
                }
            }
            return nullptr;
        };

    auto common = molga::CommonEditorProperties(identities, resolve);
    const auto& layer = Find(common, "sortingLayer");
    CHECK(molga::HasMixedEditorPropertyValue(layer, identities, resolve));
    CHECK(std::find(layer.enumValues.begin(), layer.enumValues.end(),
                    molga::EditorPropertyValue{std::string{"Missing First"}}) !=
          layer.enumValues.end());
    CHECK(std::find(layer.enumValues.begin(), layer.enumValues.end(),
                    molga::EditorPropertyValue{std::string{"Missing Second"}}) !=
          layer.enumValues.end());
    CHECK(molga::ApplyEditorPropertyValue(
              layer, identities, resolve, std::string{"Background"}) == 2u);
    CHECK(first->GetSortingLayer() == "Background");
    CHECK(second->GetSortingLayer() == "Background");
    CHECK(molga::ApplyEditorPropertyValue(
              layer, identities, resolve, std::string{"Not An Option"}) == 0u);

    // Mixed modes retain the common enum but hide the mode-specific offset.
    CHECK(Has(common, "sortMode"));
    CHECK_FALSE(Has(common, "ySortOffset"));
    const auto& mode = Find(common, "sortMode");
    CHECK(molga::ApplyEditorPropertyValue(
              mode, identities, resolve, std::string{"YAxis"}) == 1u);

    common = molga::CommonEditorProperties(identities, resolve);
    const auto& offset = Find(common, "ySortOffset");
    CHECK(molga::ApplyEditorPropertyValue(
              offset, identities, resolve, 2.5) == 2u);
    CHECK(first->GetYSortOffset() == doctest::Approx(2.5f));
    CHECK(second->GetYSortOffset() == doctest::Approx(2.5f));
}

TEST_CASE("typed asset descriptors reject missing failed and wrong importer GUIDs") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        "molga_editor_property_descriptor_assets";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    REQUIRE_FALSE(error);

    const std::string prefabGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    {
        std::ofstream prefab(root / "valid.prefab");
        prefab << nlohmann::json{{"guid", prefabGuid},
                                 {"gameObjects", nlohmann::json::array()}}.dump();
    }
    {
        std::ofstream animator(root / "wrong.animator");
        animator << nlohmann::json{{"schemaVersion", 1},
                                   {"states", nlohmann::json::array()}}.dump();
    }

    auto& database = molga::AssetDatabase::Get();
    database.Clear();
    database.ScanProject(root);
    const std::string animatorGuid = database.GuidForAbsolutePath(root / "wrong.animator");
    REQUIRE_FALSE(animatorGuid.empty());
    REQUIRE(database.Find(prefabGuid) != nullptr);

    DescriptorFixtureScript script;
    std::vector<Component*> scripts{&script};
    const auto descriptors = molga::DescribeEditorProperties(script);
    const auto& prefab = Find(descriptors, "fields.prefab");
    CHECK(prefab.assetType == "PrefabImporter");
    CHECK(molga::ApplyEditorPropertyValue(prefab, scripts, animatorGuid) == 0u);
    CHECK(script.prefab.guid.empty());
    CHECK(molga::ApplyEditorPropertyValue(
              prefab, scripts, std::string{"missing-guid"}) == 0u);
    CHECK(molga::ApplyEditorPropertyValue(prefab, scripts, prefabGuid) == 1u);
    CHECK(script.prefab.guid == prefabGuid);
    CHECK(molga::ApplyEditorPropertyValue(prefab, scripts, std::string{}) == 1u);
    CHECK(script.prefab.guid.empty());

    database.Clear();
    fs::remove_all(root, error);
}
