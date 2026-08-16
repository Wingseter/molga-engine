#include "ECS/Component.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Rendering/CameraOutputLayout.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/WorldRenderTraversal.h"
#include "Systems/Input.h"
#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

struct CameraFixture {
    std::shared_ptr<GameObject> object;
    Camera* camera = nullptr;
};

CameraFixture MakeCamera(const char* name, CameraOutputRole role, int depth,
                         CameraViewport viewport = {}) {
    CameraFixture result;
    result.object = std::make_shared<GameObject>(name);
    result.object->AddComponent<Transform>();
    result.camera = result.object->AddComponent<Camera>();
    result.camera->SetOutputRole(role);
    result.camera->SetDepth(depth);
    if (!result.camera->SetViewport(viewport)) {
        throw std::runtime_error("invalid camera fixture viewport");
    }
    return result;
}

const molga::CameraOutputEntry* FindEntry(
    const molga::CameraOutputLayout& layout, const Camera* camera) {
    const auto found = std::find_if(layout.Entries().begin(), layout.Entries().end(),
        [camera](const molga::CameraOutputEntry& entry) {
            return entry.camera == camera;
        });
    return found == layout.Entries().end() ? nullptr : &*found;
}

class CountingRenderComponent final : public Component {
public:
    COMPONENT_TYPE(CountingRenderComponent)

    void CollectRender(molga::RenderQueue&) override { ++collections; }
    int collections = 0;
};

} // namespace

TEST_CASE("CameraOutputLayout selects one stable Primary and composites by depth") {
    auto primaryFirst = MakeCamera("Primary First", CameraOutputRole::Primary, 10);
    auto primaryTied = MakeCamera("Primary Tied", CameraOutputRole::Primary, 10);
    auto secondaryLow = MakeCamera("Secondary Low", CameraOutputRole::Secondary, 2);
    auto secondaryTiedFirst = MakeCamera(
        "Secondary Tied First", CameraOutputRole::Secondary, 20);
    auto secondaryTiedLater = MakeCamera(
        "Secondary Tied Later", CameraOutputRole::Secondary, 20);
    auto inactive = MakeCamera("Inactive", CameraOutputRole::Secondary, 100);
    inactive.object->SetActive(false);
    auto disabled = MakeCamera("Disabled", CameraOutputRole::Secondary, 100);
    disabled.camera->SetEnabled(false);

    const std::vector<std::shared_ptr<GameObject>> objects{
        primaryFirst.object, primaryTied.object, secondaryLow.object,
        secondaryTiedFirst.object, secondaryTiedLater.object,
        inactive.object, disabled.object};
    const molga::CameraOutputLayout layout =
        molga::CameraOutputLayout::Build(objects, {320, 180});

    CHECK(layout.PrimaryCamera() == primaryFirst.camera);
    REQUIRE(layout.Entries().size() == 4);
    CHECK(layout.Entries()[0].camera == secondaryLow.camera);
    CHECK(layout.Entries()[1].camera == primaryFirst.camera);
    CHECK(layout.Entries()[2].camera == secondaryTiedFirst.camera);
    CHECK(layout.Entries()[3].camera == secondaryTiedLater.camera);
    CHECK(FindEntry(layout, primaryTied.camera) == nullptr);

    const auto topmost = layout.LogicalToTopmost(1.0f, 1.0f);
    REQUIRE(topmost);
    CHECK(topmost->cameraInstanceId == secondaryTiedLater.camera->GetInstanceID());
}

TEST_CASE("CameraOutputLayout preserves Primary while enforcing the eight-camera cap") {
    auto primary = MakeCamera("Primary", CameraOutputRole::Primary, -100);
    std::vector<std::shared_ptr<GameObject>> objects{primary.object};
    std::vector<Camera*> secondary;
    for (int depth = 0; depth < 9; ++depth) {
        auto camera = MakeCamera("Secondary", CameraOutputRole::Secondary, depth);
        secondary.push_back(camera.camera);
        objects.push_back(std::move(camera.object));
    }

    const molga::CameraOutputLayout withPrimary =
        molga::CameraOutputLayout::Build(objects, {64, 64});
    REQUIRE(withPrimary.Entries().size() == molga::kMaxCameraOutputs);
    CHECK(withPrimary.PrimaryCamera() == primary.camera);
    CHECK(FindEntry(withPrimary, primary.camera) != nullptr);
    CHECK(FindEntry(withPrimary, secondary[0]) == nullptr);
    CHECK(FindEntry(withPrimary, secondary[1]) == nullptr);
    for (int depth = 2; depth < 9; ++depth) {
        CHECK(FindEntry(withPrimary, secondary[depth]) != nullptr);
    }

    primary.camera->SetOutputRole(CameraOutputRole::Disabled);
    const molga::CameraOutputLayout secondaryOnly =
        molga::CameraOutputLayout::Build(objects, {64, 64});
    REQUIRE(secondaryOnly.Entries().size() == molga::kMaxCameraOutputs);
    CHECK(secondaryOnly.PrimaryCamera() == nullptr);
    CHECK(FindEntry(secondaryOnly, secondary[0]) == nullptr);
    for (int depth = 1; depth < 9; ++depth) {
        CHECK(FindEntry(secondaryOnly, secondary[depth]) != nullptr);
    }
}

TEST_CASE("CameraOutputLayout floors shared viewport edges and skips subpixel views") {
    auto left = MakeCamera("Left", CameraOutputRole::Secondary, 0,
                           {0.0f, 0.0f, 0.5f, 1.0f});
    auto right = MakeCamera("Right", CameraOutputRole::Secondary, 1,
                            {0.5f, 0.0f, 0.5f, 1.0f});
    auto tiny = MakeCamera("Tiny", CameraOutputRole::Secondary, 2,
                           {0.0f, 0.0f, 0.1f, 1.0f});
    const std::vector<std::shared_ptr<GameObject>> objects{
        left.object, right.object, tiny.object};

    const molga::CameraOutputLayout layout =
        molga::CameraOutputLayout::Build(objects, {5, 3});
    const auto* leftEntry = FindEntry(layout, left.camera);
    const auto* rightEntry = FindEntry(layout, right.camera);
    const auto* tinyEntry = FindEntry(layout, tiny.camera);
    REQUIRE(leftEntry);
    REQUIRE(rightEntry);
    CHECK(tinyEntry == nullptr);
    CHECK(leftEntry->viewport.x == 0);
    CHECK(leftEntry->viewport.width == 2);
    CHECK(rightEntry->viewport.x == 2);
    CHECK(rightEntry->viewport.width == 3);
    CHECK(leftEntry->renderable);
    CHECK(rightEntry->renderable);

    const auto leftPointer = layout.LogicalToTopmost(1.999f, 1.0f);
    REQUIRE(leftPointer);
    CHECK(leftPointer->cameraInstanceId == left.camera->GetInstanceID());
    const auto rightPointer = layout.LogicalToTopmost(2.0f, 1.0f);
    REQUIRE(rightPointer);
    CHECK(rightPointer->cameraInstanceId == right.camera->GetInstanceID());
}

TEST_CASE("CameraOutputLayout subpixel Secondary cameras do not consume the cap") {
    std::vector<std::shared_ptr<GameObject>> objects;
    std::vector<Camera*> tinyCameras;
    for (int depth = 100; depth < 108; ++depth) {
        auto camera = MakeCamera("Tiny Secondary", CameraOutputRole::Secondary,
                                 depth, {0.0f, 0.0f, 0.01f, 1.0f});
        tinyCameras.push_back(camera.camera);
        objects.push_back(std::move(camera.object));
    }
    auto visible = MakeCamera("Visible Secondary", CameraOutputRole::Secondary,
                              -100, {0.0f, 0.0f, 1.0f, 1.0f});
    Camera* visibleCamera = visible.camera;
    objects.push_back(std::move(visible.object));

    const molga::CameraOutputLayout layout =
        molga::CameraOutputLayout::Build(objects, {50, 30});
    REQUIRE(layout.Entries().size() == 1u);
    CHECK(layout.Entries()[0].camera == visibleCamera);
    CHECK(layout.Entries()[0].renderable);
    for (Camera* camera : tinyCameras) {
        CHECK(FindEntry(layout, camera) == nullptr);
    }
}

TEST_CASE("CameraOutputLayout maps topmost and explicit camera pointers from snapshots") {
    auto lower = MakeCamera("Lower", CameraOutputRole::Primary, 0);
    lower.camera->SetPixelPerfect(true);
    lower.camera->SetPixelZoom(1);

    auto upper = MakeCamera("Upper PIP", CameraOutputRole::Secondary, 10,
                            {0.25f, 0.25f, 0.5f, 0.5f});
    upper.object->GetComponent<Transform>()->SetPosition(0.26f, 0.26f);
    upper.object->GetComponent<Transform>()->SetRotation(90.0f);
    upper.camera->SetPixelPerfect(true);
    upper.camera->SetPixelZoom(2);

    const std::vector<std::shared_ptr<GameObject>> objects{
        lower.object, upper.object};
    const molga::CameraOutputLayout layout =
        molga::CameraOutputLayout::Build(objects, {100, 80});

    const auto* upperEntry = FindEntry(layout, upper.camera);
    REQUIRE(upperEntry);
    CHECK(upperEntry->view.x == doctest::Approx(0.5f));
    CHECK(upperEntry->view.y == doctest::Approx(0.5f));
    CHECK(upperEntry->view.zoom == doctest::Approx(2.0f));
    CHECK(upperEntry->view.rotation == doctest::Approx(90.0f));

    const auto topmost = layout.LogicalToTopmost(60.0f, 40.0f);
    REQUIRE(topmost);
    CHECK(topmost->cameraObjectId == upper.object->GetID());
    CHECK(topmost->cameraX == doctest::Approx(35.0f));
    CHECK(topmost->cameraY == doctest::Approx(20.0f));
    CHECK(topmost->worldX == doctest::Approx(25.5f));
    CHECK(topmost->worldY == doctest::Approx(15.5f));

    const auto lowerExplicit = layout.LogicalToCamera(
        lower.camera->GetInstanceID(), 60.0f, 40.0f);
    REQUIRE(lowerExplicit);
    CHECK(lowerExplicit->cameraObjectId == lower.object->GetID());
    CHECK(lowerExplicit->cameraX == doctest::Approx(60.0f));
    CHECK(lowerExplicit->cameraY == doctest::Approx(40.0f));
    CHECK(lowerExplicit->worldX == doctest::Approx(60.0f));
    CHECK(lowerExplicit->worldY == doctest::Approx(40.0f));
    CHECK_FALSE(layout.LogicalToCamera(
        upper.camera->GetInstanceID(), 10.0f, 10.0f));
}

TEST_CASE("World render culling masks normalize invalid layers to zero") {
    std::vector<std::shared_ptr<GameObject>> objects;
    const auto add = [&objects](int layer) {
        auto object = std::make_shared<GameObject>("Renderable");
        object->SetLayer(layer);
        auto* component = object->AddComponent<CountingRenderComponent>();
        objects.push_back(std::move(object));
        return component;
    };
    auto* layerFive = add(5);
    auto* negative = add(-1);
    auto* tooLarge = add(32);
    auto* layerZero = add(0);

    CHECK(molga::NormalizeWorldRenderLayer(5) == 5);
    CHECK(molga::NormalizeWorldRenderLayer(-1) == 0);
    CHECK(molga::NormalizeWorldRenderLayer(32) == 0);
    CHECK(molga::WorldRenderLayerMatchesMask(5, std::uint32_t{1} << 5));
    CHECK_FALSE(molga::WorldRenderLayerMatchesMask(5, std::uint32_t{1}));

    molga::RenderQueue queue;
    molga::CollectWorldRender(objects, queue, std::uint32_t{1} << 5);
    CHECK(layerFive->collections == 1);
    CHECK(negative->collections == 0);
    CHECK(tooLarge->collections == 0);
    CHECK(layerZero->collections == 0);

    molga::CollectWorldRender(objects, queue, std::uint32_t{1});
    CHECK(layerFive->collections == 1);
    CHECK(negative->collections == 1);
    CHECK(tooLarge->collections == 1);
    CHECK(layerZero->collections == 1);

    molga::CollectWorldRender(objects, queue);
    CHECK(layerFive->collections == 2);
    CHECK(negative->collections == 2);
    CHECK(tooLarge->collections == 2);
    CHECK(layerZero->collections == 2);
}

TEST_CASE("Input camera pointer state is independent and resets with snapshots") {
    Input::Init(nullptr);
    InputSnapshot cameraPointer;
    cameraPointer.pointerValid = true;
    cameraPointer.mouseX = 70.0f;
    cameraPointer.mouseY = 30.0f;
    cameraPointer.cameraPointerValid = true;
    cameraPointer.pointerCameraObjectId = 77;
    cameraPointer.cameraPointerX = 20.0f;
    cameraPointer.cameraPointerY = 10.0f;
    cameraPointer.worldPointerX = 120.5f;
    cameraPointer.worldPointerY = -4.25f;
    Input::ApplySnapshot(cameraPointer);

    CHECK(Input::GetMouseX() == doctest::Approx(70.0f));
    CHECK(Input::GetMouseY() == doctest::Approx(30.0f));
    CHECK(Input::HasCameraPointer());
    CHECK(Input::GetPointerCameraObjectId() == 77);
    CHECK(Input::GetCameraPointerX() == doctest::Approx(20.0f));
    CHECK(Input::GetCameraPointerY() == doctest::Approx(10.0f));
    CHECK(Input::GetWorldPointerX() == doctest::Approx(120.5f));
    CHECK(Input::GetWorldPointerY() == doctest::Approx(-4.25f));

    InputSnapshot globalOnly;
    globalOnly.pointerValid = true;
    globalOnly.mouseX = 3.0f;
    globalOnly.mouseY = 4.0f;
    Input::ApplySnapshot(globalOnly);
    CHECK(Input::GetMouseX() == doctest::Approx(3.0f));
    CHECK(Input::GetMouseY() == doctest::Approx(4.0f));
    CHECK_FALSE(Input::HasCameraPointer());
    CHECK(Input::GetPointerCameraObjectId() == 0);
    CHECK(Input::GetCameraPointerX() == doctest::Approx(0.0f));
    CHECK(Input::GetWorldPointerY() == doctest::Approx(0.0f));

    cameraPointer.pointerValid = false;
    Input::ApplySnapshot(cameraPointer);
    CHECK_FALSE(Input::HasCameraPointer());

    cameraPointer.pointerValid = true;
    Input::ApplySnapshot(cameraPointer);
    Input::ReleaseAll();
    CHECK_FALSE(Input::HasCameraPointer());
    CHECK(Input::GetPointerCameraObjectId() == 0);
    CHECK(Input::GetWorldPointerX() == doctest::Approx(0.0f));
}
