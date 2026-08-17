#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Camera.h"
#include "Core/World.h"
#include "Editor/Properties/EditorPropertyDescriptor.h"
#include "doctest.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

namespace {

bool HasDescriptor(const std::vector<molga::EditorPropertyDescriptor>& descriptors,
                   const char* key) {
    return std::any_of(descriptors.begin(), descriptors.end(),
        [key](const auto& descriptor) { return descriptor.key == key; });
}

const molga::EditorPropertyDescriptor* FindDescriptor(
    const std::vector<molga::EditorPropertyDescriptor>& descriptors,
    const char* key) {
    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
        [key](const auto& descriptor) { return descriptor.key == key; });
    return found == descriptors.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("Camera: Properties and defaults") {
    Camera cam;
    CHECK(cam.GetOrthoSize() == doctest::Approx(300.0f));
    CHECK(cam.IsMain() == false);
    CHECK(cam.GetOutputRole() == CameraOutputRole::Disabled);
    CHECK(cam.GetViewport() == CameraViewport{});
    CHECK(cam.GetCullingMask() == 0xFFFFFFFFu);
    CHECK_FALSE(cam.IsLightingEnabled());
    CHECK(cam.GetAmbientColor() == Color::White());
    CHECK(cam.GetAmbientIntensity() == doctest::Approx(0.2f));
    CHECK(cam.GetDepth() == 0);
    CHECK(cam.GetSmoothing() == doctest::Approx(5.0f));
    CHECK(cam.GetFollowTarget() == nullptr);
    CHECK(cam.GetTargetId() == 0);
    CHECK_FALSE(cam.IsPixelPerfect());
    CHECK(cam.GetPixelZoom() == 1);
    
    Color col = cam.GetBackgroundColor();
    CHECK(col.r == doctest::Approx(0.12f));
    CHECK(col.g == doctest::Approx(0.12f));
    CHECK(col.b == doctest::Approx(0.15f));
    CHECK(col.a == doctest::Approx(1.0f));
}

TEST_CASE("Camera: Sync with Owner Transform (No target)") {
    auto world = std::make_unique<World>();
    auto obj = std::make_shared<GameObject>("CameraObj");
    Transform* t = obj->AddComponent<Transform>(100.0f, 200.0f);
    Camera* cam = obj->AddComponent<Camera>();
    
    world->Add(obj);
    world->ResolveAssets();

    // Call update to sync position
    cam->Update(0.1f);
    
    CHECK(cam->GetCamera2D()->GetX() == doctest::Approx(100.0f));
    CHECK(cam->GetCamera2D()->GetY() == doctest::Approx(200.0f));
}

TEST_CASE("Camera: Target tracking and interpolation") {
    auto world = std::make_unique<World>();
    
    auto camObj = std::make_shared<GameObject>("CameraObj");
    Transform* camT = camObj->AddComponent<Transform>(0.0f, 0.0f);
    Camera* cam = camObj->AddComponent<Camera>();
    cam->SetSmoothing(5.0f);

    auto targetObj = std::make_shared<GameObject>("TargetObj");
    Transform* targetT = targetObj->AddComponent<Transform>(100.0f, 200.0f);
    
    world->Add(camObj);
    world->Add(targetObj);
    
    // Set target using ID and resolve
    cam->SetTargetId(targetObj->GetID());
    world->ResolveAssets();
    
    CHECK(cam->GetFollowTarget() == targetObj.get());
    
    // Smooth interpolation test
    // Target position is (100, 200). Current camera2D position is (0, 0).
    // Lerp formula: next = current + (targetPos - current) * smoothing * dt;
    // For dt = 0.1s, next = 0 + (100 - 0) * 5.0 * 0.1 = 50.0.
    // For y: next_y = 0 + (200 - 0) * 5.0 * 0.1 = 100.0.
    cam->Update(0.1f);
    
    CHECK(cam->GetCamera2D()->GetX() == doctest::Approx(50.0f));
    CHECK(cam->GetCamera2D()->GetY() == doctest::Approx(100.0f));
    
    // The owner's transform should also be in sync
    CHECK(camT->GetX() == doctest::Approx(50.0f));
    CHECK(camT->GetY() == doctest::Approx(100.0f));
}

TEST_CASE("Camera: Serialization and Deserialization Roundtrip") {
    Camera original;
    original.SetOrthoSize(450.0f);
    original.SetBackgroundColor(Color(0.5f, 0.6f, 0.7f, 0.8f));
    original.SetDepth(2);
    original.SetMain(true);
    REQUIRE(original.SetViewport({0.1f, 0.2f, 0.3f, 0.4f}));
    original.SetCullingMask(0x80000005u);
    original.SetLightingEnabled(true);
    REQUIRE(original.SetAmbientColor(Color(0.2f, 0.3f, 0.4f, 0.5f)));
    REQUIRE(original.SetAmbientIntensity(1.25f));
    original.SetTargetId(42);
    original.SetSmoothing(10.0f);
    original.SetPixelPerfect(true);
    original.SetPixelZoom(3);
    
    nlohmann::json j;
    original.Serialize(j);
    
    Camera copy;
    copy.Deserialize(j);
    
    CHECK(copy.GetOrthoSize() == doctest::Approx(450.0f));
    CHECK(copy.GetBackgroundColor().r == doctest::Approx(0.5f));
    CHECK(copy.GetBackgroundColor().g == doctest::Approx(0.6f));
    CHECK(copy.GetBackgroundColor().b == doctest::Approx(0.7f));
    CHECK(copy.GetBackgroundColor().a == doctest::Approx(0.8f));
    CHECK(copy.GetDepth() == 2);
    CHECK(copy.IsMain() == true);
    CHECK(copy.GetOutputRole() == CameraOutputRole::Primary);
    CHECK(copy.GetViewport().x == doctest::Approx(0.1f));
    CHECK(copy.GetViewport().y == doctest::Approx(0.2f));
    CHECK(copy.GetViewport().width == doctest::Approx(0.3f));
    CHECK(copy.GetViewport().height == doctest::Approx(0.4f));
    CHECK(copy.GetCullingMask() == 0x80000005u);
    CHECK(copy.IsLightingEnabled());
    CHECK(copy.GetAmbientColor() == Color(0.2f, 0.3f, 0.4f, 0.5f));
    CHECK(copy.GetAmbientIntensity() == doctest::Approx(1.25f));
    CHECK(copy.GetTargetId() == 42);
    CHECK(copy.GetSmoothing() == doctest::Approx(10.0f));
    CHECK(copy.IsPixelPerfect());
    CHECK(copy.GetPixelZoom() == 3);
    CHECK(j["pixelPerfect"] == true);
    CHECK(j["pixelZoom"] == 3);
    CHECK(j["outputRole"] == "Primary");
    CHECK(j["viewport"]["x"].get<float>() == doctest::Approx(0.1f));
    CHECK(j["cullingMask"].get<std::uint32_t>() == 0x80000005u);
    CHECK(j["lightingEnabled"] == true);
    CHECK(j["ambientIntensity"].get<float>() == doctest::Approx(1.25f));
    CHECK_FALSE(j.contains("isMain"));
}

TEST_CASE("Camera: legacy lighting defaults canonicalize and invalid ambient edits retain state") {
    const nlohmann::json canonical =
        Camera::CanonicalizeSerializedData(nlohmann::json::object());
    CHECK(canonical["lightingEnabled"] == false);
    CHECK(canonical["ambientColor"] ==
          nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}));
    CHECK(canonical["ambientIntensity"].get<float>() ==
          doctest::Approx(0.2f));

    Camera camera;
    REQUIRE(camera.SetAmbientIntensity(2.0f));
    CHECK_FALSE(camera.SetAmbientIntensity(-1.0f));
    CHECK_FALSE(camera.SetAmbientIntensity(
        std::numeric_limits<float>::infinity()));
    CHECK(camera.GetAmbientIntensity() == doctest::Approx(2.0f));
    CHECK_FALSE(camera.SetAmbientColor({
        1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f}));
    CHECK(camera.GetAmbientColor() == Color::White());
}

TEST_CASE("Camera: output roles preserve the IsMain compatibility surface") {
    Camera camera;
    camera.SetOutputRole(CameraOutputRole::Secondary);
    CHECK(camera.GetOutputRole() == CameraOutputRole::Secondary);
    CHECK_FALSE(camera.IsMain());

    camera.SetMain(true);
    CHECK(camera.GetOutputRole() == CameraOutputRole::Primary);
    CHECK(camera.IsMain());

    camera.SetMain(false);
    CHECK(camera.GetOutputRole() == CameraOutputRole::Disabled);
    CHECK_FALSE(camera.IsMain());

    camera.SetOutputRole(static_cast<CameraOutputRole>(99));
    CHECK(camera.GetOutputRole() == CameraOutputRole::Disabled);
}

TEST_CASE("Camera: viewport rejects invalid runtime edits and repairs serialized damage") {
    Camera camera;
    const CameraViewport authored{0.25f, 0.1f, 0.5f, 0.75f};
    REQUIRE(camera.SetViewport(authored));

    CHECK_FALSE(camera.SetViewport({-0.1f, 0.0f, 0.5f, 0.5f}));
    CHECK(camera.GetViewport() == authored);
    CHECK_FALSE(camera.SetViewport({0.0f, 0.0f, 0.0f, 0.5f}));
    CHECK(camera.GetViewport() == authored);
    CHECK_FALSE(camera.SetViewport({0.75f, 0.0f, 0.5f, 0.5f}));
    CHECK(camera.GetViewport() == authored);
    CHECK_FALSE(camera.SetViewport({
        std::numeric_limits<float>::infinity(), 0.0f, 0.5f, 0.5f}));
    CHECK(camera.GetViewport() == authored);

    camera.SetCullingMask(7u);
    camera.Deserialize({
        {"outputRole", "Secondary"},
        {"viewport", {{"x", 0.2f}, {"y", 0.2f},
                      {"width", 0.9f}, {"height", 0.5f}}},
        {"cullingMask", -1},
    });
    CHECK(camera.GetOutputRole() == CameraOutputRole::Secondary);
    CHECK(camera.GetViewport() == CameraViewport{});
    CHECK(camera.GetCullingMask() == 0xFFFFFFFFu);

    camera.Deserialize({
        {"viewport", {{"x", 0.0f}, {"y", 0.0f}, {"width", 0.5f}}},
    });
    CHECK(camera.GetViewport() == CameraViewport{});
}

TEST_CASE("Camera: legacy isMain migrates only when outputRole is absent") {
    Camera legacyMain;
    legacyMain.Deserialize({{"isMain", true}});
    CHECK(legacyMain.GetOutputRole() == CameraOutputRole::Primary);
    CHECK(legacyMain.IsMain());

    Camera legacyDisabled;
    legacyDisabled.Deserialize({{"isMain", false}});
    CHECK(legacyDisabled.GetOutputRole() == CameraOutputRole::Disabled);

    Camera modernWins;
    modernWins.Deserialize({
        {"outputRole", "Secondary"}, {"isMain", true},
    });
    CHECK(modernWins.GetOutputRole() == CameraOutputRole::Secondary);
    CHECK_FALSE(modernWins.IsMain());

    nlohmann::json canonical;
    modernWins.Serialize(canonical);
    CHECK(canonical["outputRole"] == "Secondary");
    CHECK_FALSE(canonical.contains("isMain"));
}

TEST_CASE("Camera: Legacy projection data defaults pixel-perfect fields and clamps zoom") {
    Camera legacy;
    legacy.Deserialize(nlohmann::json{{"orthoSize", 240.0f}});
    CHECK_FALSE(legacy.IsPixelPerfect());
    CHECK(legacy.GetPixelZoom() == 1);

    Camera camera;
    camera.SetPixelZoom(0);
    CHECK(camera.GetPixelZoom() == 1);
    camera.SetPixelZoom(100);
    CHECK(camera.GetPixelZoom() == 64);

    camera.Deserialize(nlohmann::json{
        {"pixelPerfect", true}, {"pixelZoom", -5}});
    CHECK(camera.IsPixelPerfect());
    CHECK(camera.GetPixelZoom() == 1);
    camera.Deserialize(nlohmann::json{
        {"pixelPerfect", true}, {"pixelZoom", 100}});
    CHECK(camera.GetPixelZoom() == 64);
}

TEST_CASE("Camera: Pixel-perfect viewport snaps render state without changing Transform") {
    auto object = std::make_shared<GameObject>("Pixel Camera");
    Transform* transform = object->AddComponent<Transform>(10.2f, -4.2f);
    Camera* camera = object->AddComponent<Camera>();
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(3);

    const Vector2 authored = transform->GetWorldPosition();
    REQUIRE(camera->PrepareForViewport({320, 180}));
    CHECK(camera->GetCamera2D()->GetZoom() == doctest::Approx(3.0f));
    CHECK(camera->GetCamera2D()->GetX() == doctest::Approx(31.0f / 3.0f));
    CHECK(camera->GetCamera2D()->GetY() == doctest::Approx(-13.0f / 3.0f));
    CHECK(camera->GetCamera2D()->GetViewBounds().height == doctest::Approx(60.0f));
    CHECK(transform->GetWorldPosition().x == doctest::Approx(authored.x));
    CHECK(transform->GetWorldPosition().y == doctest::Approx(authored.y));

    camera->SetPixelZoom(64);
    REQUIRE(camera->PrepareForViewport({320, 180}));
    CHECK(camera->GetCamera2D()->GetZoom() == doctest::Approx(64.0f));

    // Disabling the opt-in mode retains the old ortho projection and its
    // Camera2D 0.1..10 zoom clamp.
    camera->SetPixelPerfect(false);
    camera->SetOrthoSize(5.0f);
    REQUIRE(camera->PrepareForViewport({320, 180}));
    CHECK(camera->GetCamera2D()->GetZoom() == doctest::Approx(10.0f));
    CHECK(camera->GetCamera2D()->GetX() == doctest::Approx(authored.x));
    CHECK(camera->GetCamera2D()->GetY() == doctest::Approx(authored.y));
}

TEST_CASE("Camera: Pixel render snapping does not feed follow smoothing") {
    auto cameraObject = std::make_shared<GameObject>("Pixel Follow Camera");
    Transform* cameraTransform =
        cameraObject->AddComponent<Transform>(0.24f, -0.24f);
    Camera* camera = cameraObject->AddComponent<Camera>();
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(2);
    camera->SetSmoothing(1.0f);

    auto targetObject = std::make_shared<GameObject>("Target");
    targetObject->AddComponent<Transform>(100.0f, 50.0f);
    camera->SetFollowTarget(targetObject.get());

    REQUIRE(camera->PrepareForViewport({320, 180}));
    CHECK(camera->GetCamera2D()->GetX() == doctest::Approx(0.0f));
    CHECK(camera->GetCamera2D()->GetY() == doctest::Approx(0.0f));
    CHECK(cameraTransform->GetX() == doctest::Approx(0.24f));
    CHECK(cameraTransform->GetY() == doctest::Approx(-0.24f));

    // Toggling the opt-in mode after rendering must not make the snapped
    // Camera2D position become the next follow interpolation state.
    camera->SetPixelPerfect(false);
    camera->Update(0.1f);
    CHECK(cameraTransform->GetX() == doctest::Approx(10.216f));
    CHECK(cameraTransform->GetY() == doctest::Approx(4.784f));
    CHECK(camera->GetCamera2D()->GetX() == doctest::Approx(10.216f));
    CHECK(camera->GetCamera2D()->GetY() == doctest::Approx(4.784f));
}

TEST_CASE("Camera: Inspector descriptors expose only the active projection control") {
    Camera fixed;
    auto descriptors = molga::DescribeEditorProperties(fixed);
    CHECK(HasDescriptor(descriptors, "pixelPerfect"));
    CHECK(HasDescriptor(descriptors, "orthoSize"));
    CHECK_FALSE(HasDescriptor(descriptors, "pixelZoom"));

    Camera pixel;
    pixel.SetPixelPerfect(true);
    descriptors = molga::DescribeEditorProperties(pixel);
    CHECK(HasDescriptor(descriptors, "pixelPerfect"));
    CHECK_FALSE(HasDescriptor(descriptors, "orthoSize"));
    CHECK(HasDescriptor(descriptors, "pixelZoom"));

    std::vector<Component*> mixed{&pixel, &fixed};
    auto common = molga::CommonEditorProperties(mixed);
    CHECK(HasDescriptor(common, "pixelPerfect"));
    CHECK_FALSE(HasDescriptor(common, "orthoSize"));
    CHECK_FALSE(HasDescriptor(common, "pixelZoom"));

    const auto* toggle = FindDescriptor(common, "pixelPerfect");
    REQUIRE(toggle != nullptr);
    CHECK(molga::ApplyEditorPropertyValue(*toggle, mixed, true) == 1u);
    CHECK(pixel.IsPixelPerfect());
    CHECK(fixed.IsPixelPerfect());
    common = molga::CommonEditorProperties(mixed);
    CHECK(HasDescriptor(common, "pixelZoom"));
    CHECK_FALSE(HasDescriptor(common, "orthoSize"));
}

TEST_CASE("Camera: Main camera lookup and depth priority") {
    auto world = std::make_unique<World>();
    
    auto obj1 = std::make_shared<GameObject>("Cam1");
    auto* cam1 = obj1->AddComponent<Camera>();
    cam1->SetMain(true);
    cam1->SetDepth(10);
    
    auto obj2 = std::make_shared<GameObject>("Cam2");
    auto* cam2 = obj2->AddComponent<Camera>();
    cam2->SetMain(true);
    cam2->SetDepth(20); // Higher depth
    
    auto obj3 = std::make_shared<GameObject>("Cam3");
    auto* cam3 = obj3->AddComponent<Camera>();
    cam3->SetMain(false); // Not main
    cam3->SetDepth(30);
    
    world->Add(obj1);
    world->Add(obj2);
    world->Add(obj3);
    world->ResolveAssets();
    
    // Find main camera manually (mimicking main/runtime_main render loop)
    Camera* mainCam = nullptr;
    for (const auto& obj : world->Objects()) {
        if (obj && obj->IsActive()) {
            if (auto cam = obj->GetComponent<Camera>()) {
                if (cam->IsEnabled() && cam->IsMain()) {
                    if (!mainCam || cam->GetDepth() > mainCam->GetDepth()) {
                        mainCam = cam;
                    }
                }
            }
        }
    }
    
    REQUIRE(mainCam != nullptr);
    CHECK(mainCam == cam2); // Should select the one with highest depth (depth 20)
}
