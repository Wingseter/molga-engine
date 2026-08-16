#include "Core/TextureImportSettings.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/PointLight2D.h"
#include "ECS/Components/ShadowOccluder2D.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Rendering/LightingFrame2D.h"
#include "doctest.h"

#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>

namespace {

float SignedAreaTwice(const std::vector<Vector2>& vertices) {
    float area = 0.0f;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const Vector2& current = vertices[index];
        const Vector2& next = vertices[(index + 1U) % vertices.size()];
        area += current.x * next.y - current.y * next.x;
    }
    return area;
}

} // namespace

TEST_CASE("PointLight2D defaults ranges finite validation and serialization") {
    PointLight2D light;
    CHECK(light.GetColor() == Color::White());
    CHECK(light.GetIntensity() == doctest::Approx(1.0f));
    CHECK(light.GetRadius() == doctest::Approx(128.0f));
    CHECK(light.GetHeight() == doctest::Approx(32.0f));
    CHECK(light.GetFalloff() == doctest::Approx(2.0f));
    CHECK(light.GetAffectMask() == 0xFFFFFFFFu);
    CHECK_FALSE(light.CastsShadows());
    CHECK(light.GetPriority() == 0);

    REQUIRE(light.SetIntensity(100.0f));
    REQUIRE(light.SetRadius(0.0f));
    REQUIRE(light.SetFalloff(99.0f));
    CHECK(light.GetIntensity() == doctest::Approx(32.0f));
    CHECK(light.GetRadius() == doctest::Approx(0.01f));
    CHECK(light.GetFalloff() == doctest::Approx(8.0f));

    const float intensity = light.GetIntensity();
    CHECK_FALSE(light.SetIntensity(std::numeric_limits<float>::infinity()));
    CHECK(light.GetIntensity() == intensity);
    CHECK_FALSE(light.SetColor({
        std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, 1.0f}));
    CHECK(light.GetColor() == Color::White());

    light.SetAffectMask(0x80000001u);
    light.SetCastsShadows(true);
    light.SetPriority(-7);
    nlohmann::json serialized;
    light.Serialize(serialized);

    PointLight2D restored;
    restored.Deserialize(serialized);
    CHECK(restored.GetIntensity() == doctest::Approx(32.0f));
    CHECK(restored.GetRadius() == doctest::Approx(0.01f));
    CHECK(restored.GetFalloff() == doctest::Approx(8.0f));
    CHECK(restored.GetAffectMask() == 0x80000001u);
    CHECK(restored.CastsShadows());
    CHECK(restored.GetPriority() == -7);
}

TEST_CASE("PointLight2D uses Transform world position without radius scaling") {
    auto object = std::make_shared<GameObject>("Light");
    auto* transform = object->AddComponent<Transform>();
    transform->SetPosition(12.0f, -4.0f);
    transform->SetScale(-3.0f, 7.0f);
    transform->SetRotation(45.0f);
    auto* light = object->AddComponent<PointLight2D>();
    REQUIRE(light->SetRadius(25.0f));

    CHECK(light->GetWorldPosition() == Vector2{12.0f, -4.0f});
    CHECK(light->GetRadius() == doctest::Approx(25.0f));
}

TEST_CASE("ShadowOccluder2D normalizes convex polygons and rejects invalid setters") {
    ShadowOccluder2D occluder;
    CHECK(occluder.GetShape() == ShadowOccluderShape2D::Box);
    CHECK(occluder.GetOffset() == Vector2::Zero());
    CHECK(occluder.GetSize() == Vector2{100.0f, 100.0f});
    CHECK(occluder.IsShapeValid());

    const std::vector<Vector2> clockwise = {
        {-10.0f, -10.0f}, {-10.0f, 10.0f},
        {10.0f, 10.0f}, {10.0f, -10.0f}};
    REQUIRE(occluder.SetPolygon(clockwise));
    CHECK(occluder.GetShape() == ShadowOccluderShape2D::Polygon);
    CHECK(SignedAreaTwice(occluder.GetVertices()) > 0.0f);
    const std::vector<Vector2> retained = occluder.GetVertices();

    CHECK_FALSE(occluder.SetPolygon({
        {0.0f, 0.0f}, {10.0f, 0.0f}, {5.0f, 2.0f}, {10.0f, 10.0f},
        {0.0f, 10.0f}}));
    CHECK(occluder.GetVertices() == retained);
    CHECK_FALSE(occluder.SetPolygon({
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {std::numeric_limits<float>::infinity(), 1.0f}}));
    CHECK(occluder.GetVertices() == retained);

    // A pentagram traversal keeps a same-sign turn at every consecutive
    // vertex, but is self-intersecting and therefore not strictly convex.
    const std::vector<Vector2> pentagram = {
        {0.0f, -3.0f}, {1.763f, 2.427f}, {-2.853f, -0.927f},
        {2.853f, -0.927f}, {-1.763f, 2.427f}};
    CHECK_FALSE(occluder.SetPolygon(pentagram));
    CHECK(occluder.GetVertices() == retained);
    std::vector<Vector2> runtimePentagram = pentagram;
    CHECK_FALSE(molga::NormalizeConvexPolygon2D(runtimePentagram));
}

TEST_CASE("ShadowOccluder2D transforms nonuniform negative scale and repairs damage") {
    auto object = std::make_shared<GameObject>("Occluder");
    auto* transform = object->AddComponent<Transform>();
    transform->SetPosition(7.0f, 11.0f);
    transform->SetScale(-2.0f, 0.5f);
    transform->SetRotation(90.0f);
    auto* occluder = object->AddComponent<ShadowOccluder2D>();
    REQUIRE(occluder->SetPolygon({
        {-2.0f, -1.0f}, {2.0f, -1.0f}, {0.0f, 3.0f}}));

    const std::vector<Vector2> world = occluder->GetWorldVertices();
    REQUIRE(world.size() == 3);
    CHECK(SignedAreaTwice(world) > 0.0f);
    for (const Vector2& vertex : world) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
    }

    occluder->Deserialize({
        {"shape", "Polygon"},
        {"offset", {"damaged", 0.0f}},
        {"size", {-1.0f, 0.0f}},
        {"vertices", {{-2.0f, -1.0f}, {2.0f, -1.0f}, {0.0f, 3.0f}}}});
    CHECK(occluder->IsShapeValid());
    CHECK(occluder->GetShape() == ShadowOccluderShape2D::Polygon);
    CHECK(occluder->GetVertices().size() == 3);

    occluder->Deserialize({
        {"shape", "Polygon"},
        {"vertices", {{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.0f}}}});
    CHECK_FALSE(occluder->IsShapeValid());
    CHECK(occluder->GetWorldVertices().empty());

    occluder->ResetToDefaultBox();
    CHECK(occluder->IsShapeValid());
    CHECK(occluder->GetShape() == ShadowOccluderShape2D::Box);
    CHECK(occluder->GetSize() == Vector2{100.0f, 100.0f});
}

TEST_CASE("Sprite lighting remains opt-in and canonicalizes normal metadata") {
    SpriteRenderer sprite;
    CHECK(sprite.GetLightingMode() == SpriteLightingMode2D::Unlit);
    CHECK(sprite.GetNormalMapGuid().empty());
    CHECK(sprite.GetNormalStrength() == doctest::Approx(1.0f));
    CHECK_FALSE(sprite.HasUsableNormalTexture());

    sprite.SetLightingMode(SpriteLightingMode2D::Lit);
    sprite.SetNormalMapGuid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    REQUIRE(sprite.SetNormalStrength(3.0f));
    CHECK(sprite.GetNormalStrength() == doctest::Approx(2.0f));

    nlohmann::json serialized;
    sprite.Serialize(serialized);
    CHECK(serialized["lightingMode"] == "Lit");
    CHECK(serialized["normalMapGuid"] ==
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(serialized["normalStrength"].get<float>() ==
          doctest::Approx(2.0f));

    const nlohmann::json legacy =
        SpriteRenderer::CanonicalizeSerializedData(nlohmann::json::object());
    CHECK(legacy["lightingMode"] == "Unlit");
    CHECK(legacy["normalMapGuid"] == "");
    CHECK(legacy["normalStrength"].get<float>() ==
          doctest::Approx(1.0f));
}

TEST_CASE("NormalMap texture usage is linear and legacy usage stays Color") {
    const molga::TextureImportSettings legacy =
        molga::DeserializeTextureImportSettings(nlohmann::json::object(), true);
    CHECK(legacy.usage == molga::TextureUsage::Color);

    molga::TextureImportSettings normal;
    normal.usage = molga::TextureUsage::NormalMap;
    normal.colorSpace = molga::TextureColorSpace::SRGB;
    const nlohmann::json serialized =
        molga::SerializeTextureImportSettings(normal);
    CHECK(serialized["usage"] == "NormalMap");
    CHECK(serialized["colorSpace"] == "LegacyLinear");

    const molga::TextureImportSettings restored =
        molga::DeserializeTextureImportSettings(serialized);
    CHECK(restored.usage == molga::TextureUsage::NormalMap);
    CHECK(restored.colorSpace == molga::TextureColorSpace::LegacyLinear);
}

TEST_CASE("LightingFrame2D selects priority order and enforces 8 4 64 budgets") {
    std::vector<std::shared_ptr<GameObject>> objects;
    auto cameraObject = std::make_shared<GameObject>("Camera");
    cameraObject->AddComponent<Transform>(0.0f, 0.0f);
    Camera* camera = cameraObject->AddComponent<Camera>();
    camera->SetLightingEnabled(true);
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(1);
    camera->SetCullingMask(0xFFFFFFFFu);
    REQUIRE(camera->PrepareForViewport({100, 100}));
    objects.push_back(cameraObject);

    std::vector<std::uint64_t> lightIds;
    for (int index = 0; index < 10; ++index) {
        auto object = std::make_shared<GameObject>("Light");
        object->AddComponent<Transform>(50.0f, 50.0f);
        PointLight2D* light = object->AddComponent<PointLight2D>();
        REQUIRE(light->SetRadius(100.0f));
        light->SetPriority(index < 2 ? 10 : 0);
        light->SetCastsShadows(true);
        light->SetAffectMask(1u << 1u);
        lightIds.push_back(light->GetInstanceID());
        objects.push_back(std::move(object));
    }

    // These are deliberately earlier than the in-mask casters. Filtering must
    // happen before the deterministic 64-caster cap.
    auto wrongLayer = std::make_shared<GameObject>("Wrong layer occluder");
    wrongLayer->SetLayer(2);
    wrongLayer->AddComponent<Transform>(70.0f, 50.0f);
    REQUIRE(wrongLayer->AddComponent<ShadowOccluder2D>()->SetBox(
        Vector2::Zero(), {1.0f, 1.0f}));
    objects.push_back(wrongLayer);

    auto outsideRadius = std::make_shared<GameObject>("Far occluder");
    outsideRadius->SetLayer(1);
    outsideRadius->AddComponent<Transform>(500.0f, 500.0f);
    REQUIRE(outsideRadius->AddComponent<ShadowOccluder2D>()->SetBox(
        Vector2::Zero(), {1.0f, 1.0f}));
    objects.push_back(outsideRadius);

    for (int index = 0; index < 65; ++index) {
        auto object = std::make_shared<GameObject>("Occluder");
        object->SetLayer(1);
        object->AddComponent<Transform>(
            70.0f, 48.0f + static_cast<float>(index % 5));
        REQUIRE(object->AddComponent<ShadowOccluder2D>()->SetBox(
            Vector2::Zero(), {1.0f, 1.0f}));
        objects.push_back(std::move(object));
    }

    const molga::LightingFrame2D frame =
        molga::LightingFrame2D::Build(objects, *camera, {100, 100});
    REQUIRE(frame.IsUsable());
    REQUIRE(frame.lights.size() == molga::kMaxPointLights2D);
    CHECK(frame.discardedLightCount == 2);
    CHECK(frame.lights[0].componentInstanceId == lightIds[0]);
    CHECK(frame.lights[1].componentInstanceId == lightIds[1]);
    for (std::size_t index = 0; index < frame.lights.size(); ++index) {
        CHECK(frame.lights[index].componentInstanceId == lightIds[index]);
        CHECK(frame.lights[index].shadowLayer ==
              (index < molga::kMaxShadowLights2D
                   ? static_cast<int>(index) : -1));
    }
    CHECK(frame.discardedShadowLightCount == 4);
    REQUIRE(frame.shadowLayers.size() == molga::kMaxShadowLights2D);
    for (const molga::ShadowMaskLayerFrame2D& layer : frame.shadowLayers) {
        CHECK(layer.selectedOccluderCount ==
              molga::kMaxShadowOccludersPerLight2D);
        CHECK(layer.discardedOccluderCount == 1);
        REQUIRE(layer.casters.size() ==
                molga::kMaxShadowOccludersPerLight2D);
        CHECK_FALSE(layer.fullCover);
        for (const molga::ShadowCasterGeometry2D& caster : layer.casters) {
            CHECK(caster.HasTriangles());
        }
    }
}

TEST_CASE("LightingFrame2D filters camera layer activity and radius deterministically") {
    std::vector<std::shared_ptr<GameObject>> objects;
    auto cameraObject = std::make_shared<GameObject>("Camera");
    cameraObject->AddComponent<Transform>(0.0f, 0.0f);
    Camera* camera = cameraObject->AddComponent<Camera>();
    camera->SetLightingEnabled(true);
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(1);
    camera->SetCullingMask(1u << 1u);
    REQUIRE(camera->PrepareForViewport({100, 100}));
    objects.push_back(cameraObject);

    const auto addLight = [&](const char* name, int layer,
                              Vector2 position, bool active, bool enabled) {
        auto object = std::make_shared<GameObject>(name);
        object->SetLayer(layer);
        object->SetActive(active);
        object->AddComponent<Transform>(position.x, position.y);
        PointLight2D* light = object->AddComponent<PointLight2D>();
        CHECK(light->SetRadius(2.0f));
        light->SetEnabled(enabled);
        objects.push_back(std::move(object));
        return light;
    };

    PointLight2D* included =
        addLight("Included", 1, {50.0f, 50.0f}, true, true);
    addLight("Wrong camera layer", 0, {50.0f, 50.0f}, true, true);
    addLight("Outside radius", 1, {1000.0f, 1000.0f}, true, true);
    addLight("Inactive", 1, {50.0f, 50.0f}, false, true);
    addLight("Disabled", 1, {50.0f, 50.0f}, true, false);
    PointLight2D* normalizedLayer =
        addLight("Invalid layer normalizes to zero", 99,
                 {50.0f, 50.0f}, true, true);

    const molga::LightingFrame2D layerOne =
        molga::LightingFrame2D::Build(objects, *camera, {100, 100});
    REQUIRE(layerOne.lights.size() == 1);
    CHECK(layerOne.lights[0].source == included);

    camera->SetCullingMask(1u << 0u);
    const molga::LightingFrame2D layerZero =
        molga::LightingFrame2D::Build(objects, *camera, {100, 100});
    REQUIRE(layerZero.lights.size() == 2);
    CHECK(layerZero.lights[1].source == normalizedLayer);
    CHECK(layerZero.lights[1].layer == 0);
}

TEST_CASE("Shadow geometry extrudes convex silhouettes and fully covers boundary lights") {
    molga::LightingOccluderSnapshot2D occluder;
    occluder.componentInstanceId = 7;
    occluder.objectId = 11;
    occluder.sceneOrder = 13;
    occluder.vertices = {
        {-1.0f, -1.0f}, {1.0f, -1.0f},
        {1.0f, 1.0f}, {-1.0f, 1.0f}};

    const molga::ShadowCasterGeometry2D outside =
        molga::BuildShadowCasterGeometry2D(
            occluder, {-3.0f, 0.0f}, 10.0f);
    CHECK_FALSE(outside.fullCover);
    CHECK(outside.HasTriangles());
    CHECK(outside.vertices.size() > occluder.vertices.size());
    CHECK(outside.occluderInstanceId == 7);

    molga::LightingOccluderSnapshot2D chordOccluder;
    chordOccluder.vertices = {
        {1.0f, -1.0f}, {2.0f, -1.0f},
        {2.0f, 1.0f}, {1.0f, 1.0f}};
    const molga::ShadowCasterGeometry2D beyondRadiusChord =
        molga::BuildShadowCasterGeometry2D(
            chordOccluder, {0.0f, 0.0f}, 10.0f);
    REQUIRE(beyondRadiusChord.HasTriangles());
    const float farthestX = std::max_element(
        beyondRadiusChord.vertices.begin(),
        beyondRadiusChord.vertices.end(),
        [](const Vector2& lhs, const Vector2& rhs) {
            return lhs.x < rhs.x;
        })->x;
    CHECK(farthestX >= 10.0f);

    const molga::ShadowCasterGeometry2D inside =
        molga::BuildShadowCasterGeometry2D(
            occluder, {0.0f, 0.0f}, 10.0f);
    CHECK(inside.fullCover);
    CHECK_FALSE(inside.HasTriangles());

    const molga::ShadowCasterGeometry2D boundary =
        molga::BuildShadowCasterGeometry2D(
            occluder, {-1.0f - 0.5f * molga::kShadowGeometryEpsilon2D, 0.0f},
            10.0f);
    CHECK(boundary.fullCover);
    CHECK_FALSE(boundary.HasTriangles());
}
