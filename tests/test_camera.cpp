#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Camera.h"
#include "Core/World.h"
#include "doctest.h"
#include <memory>

TEST_CASE("Camera: Properties and defaults") {
    Camera cam;
    CHECK(cam.GetOrthoSize() == doctest::Approx(300.0f));
    CHECK(cam.IsMain() == false);
    CHECK(cam.GetDepth() == 0);
    CHECK(cam.GetSmoothing() == doctest::Approx(5.0f));
    CHECK(cam.GetFollowTarget() == nullptr);
    CHECK(cam.GetTargetId() == 0);
    
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
    original.SetTargetId(42);
    original.SetSmoothing(10.0f);
    
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
    CHECK(copy.GetTargetId() == 42);
    CHECK(copy.GetSmoothing() == doctest::Approx(10.0f));
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
