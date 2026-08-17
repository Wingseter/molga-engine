#include "doctest.h"

#include "Core/SceneSerializer.h"
#include "Core/World.h"
#include "ECS/BuiltinComponents.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/GameObject.h"
#include "UI/UISystem.h"

#include <memory>

namespace {
std::shared_ptr<GameObject> MakeCanvas(World& world) {
    auto object = std::make_shared<GameObject>("Canvas");
    object->AddComponent<UICanvas>();
    auto* rect = object->AddComponent<RectTransform>();
    rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetSizeDelta({0.0f, 0.0f});
    world.Add(object);
    return object;
}

std::shared_ptr<GameObject> MakeButton(World& world, GameObject* parent,
                                       const char* name, int order) {
    auto object = std::make_shared<GameObject>(name);
    auto* rect = object->AddComponent<RectTransform>();
    rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetSizeDelta({200.0f, 80.0f});
    auto* button = object->AddComponent<UIButton>();
    button->SetSortingOrder(order);
    object->SetParent(parent);
    world.Add(object);
    return object;
}
} // namespace

TEST_CASE("UICanvas scale-with-screen-size uses width-height match") {
    UICanvas canvas;
    CHECK(canvas.GetReferenceResolution() == Vector2(800.0f, 600.0f));
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(0.5f));
    CHECK(canvas.ScaleFactor({800.0f, 600.0f}) == doctest::Approx(1.0f));
    CHECK(canvas.ScaleFactor({1600.0f, 1200.0f}) == doctest::Approx(2.0f));
    CHECK(canvas.ScaleFactor({1600.0f, 600.0f}) == doctest::Approx(std::sqrt(2.0f)));
}

TEST_CASE("RectTransform resolves anchors, pivot, nesting, and viewport scale") {
    World world;
    auto canvas = MakeCanvas(world);

    auto panel = std::make_shared<GameObject>("Panel");
    auto* panelRect = panel->AddComponent<RectTransform>();
    panelRect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    panelRect->SetPivot({0.5f, 0.5f});
    panelRect->SetAnchoredPosition({10.0f, -20.0f});
    panelRect->SetSizeDelta({200.0f, 100.0f});
    panel->SetParent(canvas.get());
    world.Add(panel);

    AABB panelAtReference = panelRect->GetScreenRect({800.0f, 600.0f});
    CHECK(panelAtReference.x == doctest::Approx(310.0f));
    CHECK(panelAtReference.y == doctest::Approx(230.0f));
    CHECK(panelAtReference.width == doctest::Approx(200.0f));
    CHECK(panelAtReference.height == doctest::Approx(100.0f));

    auto child = std::make_shared<GameObject>("Child");
    auto* childRect = child->AddComponent<RectTransform>();
    childRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    childRect->SetPivot({0.5f, 0.5f});
    childRect->SetSizeDelta({-20.0f, -10.0f});
    child->SetParent(panel.get());
    world.Add(child);

    AABB nested = childRect->GetScreenRect({800.0f, 600.0f});
    CHECK(nested.x == doctest::Approx(320.0f));
    CHECK(nested.y == doctest::Approx(235.0f));
    CHECK(nested.width == doctest::Approx(180.0f));
    CHECK(nested.height == doctest::Approx(90.0f));

    AABB scaled = panelRect->GetScreenRect({1600.0f, 1200.0f});
    CHECK(scaled.x == doctest::Approx(620.0f));
    CHECK(scaled.y == doctest::Approx(460.0f));
    CHECK(scaled.width == doctest::Approx(400.0f));
    CHECK(scaled.height == doctest::Approx(200.0f));
}

TEST_CASE("UIButton gives pointer capture to only the topmost draw-order target") {
    World world;
    auto canvas = MakeCanvas(world);
    auto low = MakeButton(world, canvas.get(), "Low", 1);
    auto high = MakeButton(world, canvas.get(), "High", 2);

    int lowClicks = 0;
    int highClicks = 0;
    low->GetComponent<UIButton>()->SetOnClick([&] { ++lowClicks; });
    high->GetComponent<UIButton>()->SetOnClick([&] { ++highClicks; });

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    CHECK_FALSE(low->GetComponent<UIButton>()->IsPressed());
    CHECK(high->GetComponent<UIButton>()->IsPressed());

    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, false, false, true, true});
    CHECK(lowClicks == 0);
    CHECK(highClicks == 1);
    CHECK(high->GetComponent<UIButton>()->WasClickedThisFrame());

    // Click edge is cleared on the following input frame.
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, false, false, false, true});
    CHECK_FALSE(high->GetComponent<UIButton>()->WasClickedThisFrame());
}

TEST_CASE("UIButton release outside its captured rect does not click") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    int clicks = 0;
    buttonObject->GetComponent<UIButton>()->SetOnClick([&] { ++clicks; });

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{10.0f, 10.0f}, false, false, true, true});
    CHECK(clicks == 0);
    CHECK_FALSE(buttonObject->GetComponent<UIButton>()->WasClickedThisFrame());
}

TEST_CASE("UIButton invalid pointer immediately releases capture") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    auto* button = buttonObject->GetComponent<UIButton>();
    int clicks = 0;
    button->SetOnClick([&] { ++clicks; });

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f},
        {{400.0f, 300.0f}, true, true, false, true});
    REQUIRE(button->IsPressed());

    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{}, false, false, false, false});
    CHECK_FALSE(button->IsPressed());
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f},
        {{400.0f, 300.0f}, false, false, true, true});
    CHECK(clicks == 0);
}

TEST_CASE("UI ignores disabled canvases and inactive ancestors") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    auto* button = buttonObject->GetComponent<UIButton>();

    canvas->GetComponent<UICanvas>()->SetEnabled(false);
    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    CHECK_FALSE(button->IsPressed());
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == nullptr);

    canvas->GetComponent<UICanvas>()->SetEnabled(true);
    canvas->SetActive(false);
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    CHECK_FALSE(button->IsPressed());
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == nullptr);
}

TEST_CASE("UI hit testing selects only visible UI components") {
    World world;
    auto canvas = MakeCanvas(world);
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == nullptr);

    auto image = std::make_shared<GameObject>("Image");
    auto* rect = image->AddComponent<RectTransform>();
    rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    rect->SetSizeDelta({100.0f, 100.0f});
    image->AddComponent<UIImage>();
    image->SetParent(canvas.get());
    world.Add(image);
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == image.get());
}

TEST_CASE("UIButton disabling clears transient pointer state") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    auto* button = buttonObject->GetComponent<UIButton>();
    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    REQUIRE(button->IsPressed());
    button->SetInteractable(false);
    CHECK_FALSE(button->IsPressed());
    CHECK_FALSE(button->IsHovered());
    CHECK_FALSE(button->WasClickedThisFrame());
}

TEST_CASE("UIButton callback may clear its world safely") {
    World world;
    auto canvas = MakeCanvas(world);
    auto high = MakeButton(world, canvas.get(), "High", 2);
    auto low = MakeButton(world, canvas.get(), "Low", 1);
    bool clicked = false;
    high->GetComponent<UIButton>()->SetOnClick([&] {
        clicked = true;
        world.Clear();
    });
    // Leave the World as the sole owner so Clear() destroys the callback target.
    canvas.reset();
    high.reset();
    low.reset();

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, false, false, true, true});
    CHECK(clicked);
    CHECK(world.Objects().empty());
}

TEST_CASE("UI components serialize through the scene component contract") {
    RegisterBuiltinComponents();
    auto canvas = std::make_shared<GameObject>("Canvas");
    canvas->AddComponent<UICanvas>()->SetReferenceResolution({1920.0f, 1080.0f});
    auto* rect = canvas->AddComponent<RectTransform>();
    rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    auto* image = canvas->AddComponent<UIImage>();
    image->SetTextureGuid("0123456789abcdef0123456789abcdef");
    image->SetTint({0.1f, 0.2f, 0.3f, 0.4f});
    auto* label = canvas->AddComponent<UILabel>();
    label->SetText("한글 타이틀");
    label->SetFontGuid("fedcba9876543210fedcba9876543210");
    auto* button = canvas->AddComponent<UIButton>();
    button->SetInteractable(false);

    std::vector<std::shared_ptr<GameObject>> source{canvas};
    const auto json = SceneSerializer::SerializeScene(source, "UI");
    std::vector<std::shared_ptr<GameObject>> restored;
    REQUIRE(SceneSerializer::DeserializeScene(json, restored));
    REQUIRE(restored.size() == 1);
    CHECK(restored[0]->GetComponent<UICanvas>()->GetReferenceResolution() ==
          Vector2(1920.0f, 1080.0f));
    CHECK(restored[0]->GetComponent<UIImage>()->GetTextureGuid() ==
          "0123456789abcdef0123456789abcdef");
    CHECK(restored[0]->GetComponent<UILabel>()->GetText() == "한글 타이틀");
    CHECK_FALSE(restored[0]->GetComponent<UIButton>()->IsInteractable());
}
