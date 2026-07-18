#include "Editor/EditorPreferences.h"
#include "Editor/GameViewLayout.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Editor/SceneDocument.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Systems/Input.h"
#include "doctest.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace molga;

TEST_CASE("OutputPresentationLayout preserves Native output and integer scales") {
    const auto native = OutputPresentationLayout::Calculate(
        GameOutputScaleMode::Native, {320, 180}, {853, 479});
    REQUIRE(native.IsValid());
    CHECK((native.logicalSize == PixelSize{853, 479}));
    CHECK(native.scale == 1);
    CHECK(native.contentRect.x == 0);
    CHECK(native.contentRect.y == 0);
    CHECK(native.contentRect.width == 853);
    CHECK(native.contentRect.height == 479);
    CHECK_FALSE(native.cropped);

    for (const auto expected : {1, 2, 3}) {
        const auto integer = OutputPresentationLayout::IntegerFit(
            {320, 180}, {320 * expected, 180 * expected});
        REQUIRE(integer.IsValid());
        CHECK((integer.logicalSize == PixelSize{320, 180}));
        CHECK(integer.scale == expected);
        CHECK(integer.contentRect.width == 320 * expected);
        CHECK(integer.contentRect.height == 180 * expected);
        CHECK_FALSE(integer.cropped);
    }
}

TEST_CASE("OutputPresentationLayout centers bars with exclusive boundaries") {
    const auto layout = OutputPresentationLayout::IntegerFit(
        {320, 180}, {1000, 700});
    REQUIRE(layout.IsValid());
    CHECK(layout.scale == 3);
    CHECK(layout.contentRect.x == 20);
    CHECK(layout.contentRect.y == 80);
    CHECK(layout.contentRect.width == 960);
    CHECK(layout.contentRect.height == 540);
    CHECK_FALSE(layout.FramebufferToLogical(19.999f, 80.0f));
    CHECK_FALSE(layout.FramebufferToLogical(20.0f, 79.999f));
    const auto first = layout.FramebufferToLogical(20.0f, 80.0f);
    REQUIRE(first);
    CHECK(first->x == 0);
    CHECK(first->y == 0);
    const auto last = layout.FramebufferToLogical(979.999f, 619.999f);
    REQUIRE(last);
    CHECK(last->x == 319);
    CHECK(last->y == 179);
    CHECK_FALSE(layout.FramebufferToLogical(980.0f, 619.0f));
    CHECK_FALSE(layout.FramebufferToLogical(979.0f, 620.0f));

    const auto odd = OutputPresentationLayout::IntegerFit({3, 2}, {10, 7});
    CHECK(odd.scale == 3);
    CHECK(odd.contentRect.x == 0);
    CHECK(odd.contentRect.y == 0);
    CHECK(odd.contentRect.width == 9);
    CHECK(odd.contentRect.height == 6);
}

TEST_CASE("OutputPresentationLayout keeps 1x and maps a centered crop") {
    const auto crop = OutputPresentationLayout::IntegerFit({5, 4}, {4, 3});
    REQUIRE(crop.IsValid());
    CHECK(crop.scale == 1);
    CHECK(crop.cropped);
    CHECK(crop.contentRect.x == -1);
    CHECK(crop.contentRect.y == -1);
    const auto firstVisible = crop.FramebufferToLogical(0.0f, 0.0f);
    REQUIRE(firstVisible);
    CHECK(firstVisible->x == 1);
    CHECK(firstVisible->y == 1);
    const auto lastVisible = crop.FramebufferToLogical(3.999f, 2.999f);
    REQUIRE(lastVisible);
    CHECK(lastVisible->x == 4);
    CHECK(lastVisible->y == 3);
    CHECK_FALSE(crop.FramebufferToLogical(4.0f, 2.0f));
    CHECK_FALSE(crop.FramebufferToLogical(3.0f, 3.0f));

    CHECK_FALSE(OutputPresentationLayout::IntegerFit({320, 180}, {0, 0})
                    .IsValid());
    CHECK_FALSE(OutputPresentationLayout::Native({0, 720}).IsValid());
}

TEST_CASE("GameViewLayout Fit preserves aspect ratio and letterboxes") {
    const GameViewLayout layout = GameViewLayout::Calculate(
        {1920, 1080}, {1000.0f, 700.0f}, {1.0f, 1.0f},
        GameViewDisplayMode::Fit);

    CHECK(layout.imageRect.width == doctest::Approx(1000.0f));
    CHECK(layout.imageRect.height == doctest::Approx(562.5f));
    CHECK(layout.imageRect.x == doctest::Approx(0.0f));
    CHECK(layout.imageRect.y == doctest::Approx(68.75f));
    CHECK(layout.physicalPixelsPerTexel == doctest::Approx(1000.0f / 1920.0f));

    const auto center = layout.ScreenToGamePixel({510.0f, 301.25f}, {10.0f, 20.0f});
    REQUIRE(center.has_value());
    CHECK(center->x == 960);
    CHECK(center->y == 540);
}

TEST_CASE("GameViewLayout accounts for HiDPI in Fit and 100 percent modes") {
    const GameViewLayout fit = GameViewLayout::Calculate(
        {640, 360}, {320.0f, 200.0f}, {2.0f, 2.0f},
        GameViewDisplayMode::Fit);
    CHECK(fit.physicalPixelsPerTexel == doctest::Approx(1.0f));
    CHECK(fit.imageRect.width == doctest::Approx(320.0f));
    CHECK(fit.imageRect.height == doctest::Approx(180.0f));
    CHECK(fit.imageRect.y == doctest::Approx(10.0f));

    auto last = fit.ScreenToGamePixel({319.75f, 179.75f}, {0.0f, 0.0f});
    REQUIRE(last.has_value());
    CHECK(last->x == 639);
    CHECK(last->y == 359);

    const GameViewLayout actual = GameViewLayout::Calculate(
        {640, 360}, {200.0f, 100.0f}, {2.0f, 2.0f},
        GameViewDisplayMode::PixelPerfect100);
    CHECK(actual.physicalPixelsPerTexel == doctest::Approx(1.0f));
    CHECK(actual.imageRect.width == doctest::Approx(320.0f));
    CHECK(actual.imageRect.height == doctest::Approx(180.0f));
    CHECK(actual.imageRect.x == doctest::Approx(0.0f));
    CHECK(actual.imageRect.y == doctest::Approx(0.0f));

    const GameViewLayout nonUniform = GameViewLayout::Calculate(
        {600, 300}, {300.0f, 200.0f}, {2.0f, 1.5f},
        GameViewDisplayMode::PixelPerfect100);
    CHECK(nonUniform.imageRect.width == doctest::Approx(300.0f));
    CHECK(nonUniform.imageRect.height == doctest::Approx(200.0f));
    const auto nonUniformLast = nonUniform.ScreenToGamePixel(
        {299.9f, 199.9f}, {0.0f, 0.0f});
    REQUIRE(nonUniformLast);
    CHECK(nonUniformLast->x == 599);
    CHECK(nonUniformLast->y == 299);

    const GameViewLayout oddPhysicalRemainder = GameViewLayout::Calculate(
        {3, 2}, {2.0f, 1.5f}, {2.0f, 2.0f},
        GameViewDisplayMode::PixelPerfect100);
    CHECK(oddPhysicalRemainder.imageRect.width == doctest::Approx(1.5f));
    CHECK(oddPhysicalRemainder.imageRect.height == doctest::Approx(1.0f));
    CHECK(oddPhysicalRemainder.imageRect.x == doctest::Approx(0.0f));
    CHECK(oddPhysicalRemainder.imageRect.y == doctest::Approx(0.0f));
    CHECK(oddPhysicalRemainder.imageRect.x * 2.0f ==
          doctest::Approx(std::floor(oddPhysicalRemainder.imageRect.x * 2.0f)));
    CHECK(oddPhysicalRemainder.imageRect.y * 2.0f ==
          doctest::Approx(std::floor(oddPhysicalRemainder.imageRect.y * 2.0f)));
}

TEST_CASE("GameViewLayout rejects points outside the output image") {
    const GameViewLayout layout = GameViewLayout::Calculate(
        {320, 180}, {640.0f, 360.0f}, {1.0f, 1.0f},
        GameViewDisplayMode::Fit);
    CHECK_FALSE(layout.ScreenToGamePixel({9.9f, 20.0f}, {10.0f, 20.0f}));
    CHECK_FALSE(layout.ScreenToGamePixel({650.0f, 20.0f}, {10.0f, 20.0f}));
    CHECK_FALSE(layout.ScreenToGamePixel({10.0f, 380.0f}, {10.0f, 20.0f}));
    const auto first = layout.ScreenToGamePixel({10.0f, 20.0f}, {10.0f, 20.0f});
    REQUIRE(first);
    CHECK(first->x == 0);
    CHECK(first->y == 0);
}

TEST_CASE("GameView maps panel through target presentation to logical pixels") {
    const GameViewLayout panel = GameViewLayout::Calculate(
        {800, 600}, {400.0f, 400.0f}, {2.0f, 1.5f},
        GameViewDisplayMode::PixelPerfect100);
    const OutputPresentationLayout presentation =
        OutputPresentationLayout::IntegerFit({320, 180}, {800, 600});
    REQUIRE(panel.physicalPixelsPerTexel == doctest::Approx(1.0f));
    REQUIRE(presentation.scale == 2);
    CHECK(presentation.contentRect.x == 80);
    CHECK(presentation.contentRect.y == 120);

    const GameViewPoint origin{10.0f, 20.0f};
    const auto targetFirst = panel.ScreenToGamePixel(
        {origin.x + 80.25f / 2.0f, origin.y + 120.25f / 1.5f}, origin);
    REQUIRE(targetFirst);
    CHECK(targetFirst->x == 80);
    CHECK(targetFirst->y == 120);
    const auto logicalFirst = presentation.FramebufferToLogical(
        {targetFirst->x, targetFirst->y});
    REQUIRE(logicalFirst);
    CHECK(logicalFirst->x == 0);
    CHECK(logicalFirst->y == 0);

    const auto targetBar = panel.ScreenToGamePixel(
        {origin.x + 79.25f / 2.0f, origin.y + 120.25f / 1.5f}, origin);
    REQUIRE(targetBar);
    CHECK_FALSE(presentation.FramebufferToLogical(
        {targetBar->x, targetBar->y}));

    const auto targetLast = panel.ScreenToGamePixel(
        {origin.x + 719.75f / 2.0f, origin.y + 479.75f / 1.5f}, origin);
    REQUIRE(targetLast);
    const auto logicalLast = presentation.FramebufferToLogical(
        {targetLast->x, targetLast->y});
    REQUIRE(logicalLast);
    CHECK(logicalLast->x == 319);
    CHECK(logicalLast->y == 179);
}

TEST_CASE("Resolution presets resolve build and custom dimensions") {
    CHECK((ResolveResolutionPreset(ResolutionPreset::BuildResolution,
                                   {1234, 567}, {77, 55}) == PixelSize{1234, 567}));
    CHECK((ResolveResolutionPreset(ResolutionPreset::R1280x720,
                                   {1, 1}, {2, 2}) == PixelSize{1280, 720}));
    CHECK((ResolveResolutionPreset(ResolutionPreset::Custom,
                                   {1, 1}, {777, 333}) == PixelSize{777, 333}));
}

TEST_CASE("EditorPreferences round trips and recovers from corruption") {
    namespace fs = std::filesystem;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory = fs::temp_directory_path() /
        ("molga-game-view-preferences-" + std::to_string(nonce));
    const fs::path path = directory / "editor_preferences.json";

    EditorPreferences saved = EditorPreferences::Defaults();
    saved.gameView.selectedPreset = ResolutionPreset::Custom;
    saved.gameView.displayMode = GameViewDisplayMode::PixelPerfect100;
    saved.gameView.customResolution = {777, 333};
    std::string error;
    REQUIRE(saved.SaveAtomic(path, &error));
    CHECK(error.empty());

    EditorPreferences loaded;
    std::string warning;
    REQUIRE(loaded.Load(path, &warning));
    CHECK(warning.empty());
    CHECK(loaded.gameView.selectedPreset == ResolutionPreset::Custom);
    CHECK(loaded.gameView.displayMode == GameViewDisplayMode::PixelPerfect100);
    CHECK((loaded.gameView.customResolution == PixelSize{777, 333}));

    {
        std::ofstream corrupt(path, std::ios::trunc);
        corrupt << "{ definitely not json";
    }
    CHECK_FALSE(loaded.Load(path, &warning));
    CHECK_FALSE(warning.empty());
    CHECK(loaded.gameView.selectedPreset == ResolutionPreset::BuildResolution);
    CHECK(loaded.gameView.displayMode == GameViewDisplayMode::Fit);
    CHECK((loaded.gameView.customResolution == PixelSize{1280, 720}));

    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

TEST_CASE("GameOutputRenderer selects highest-depth main camera with stable ties") {
    std::vector<std::shared_ptr<GameObject>> objects;
    auto first = std::make_shared<GameObject>("First");
    Camera* firstCamera = first->AddComponent<Camera>();
    firstCamera->SetMain(true);
    firstCamera->SetDepth(10);
    objects.push_back(first);

    auto tied = std::make_shared<GameObject>("Tied");
    Camera* tiedCamera = tied->AddComponent<Camera>();
    tiedCamera->SetMain(true);
    tiedCamera->SetDepth(10);
    objects.push_back(tied);

    auto highest = std::make_shared<GameObject>("Highest");
    Camera* highestCamera = highest->AddComponent<Camera>();
    highestCamera->SetMain(true);
    highestCamera->SetDepth(20);
    objects.push_back(highest);

    CHECK(GameOutputRenderer::FindMainCamera(objects) == highestCamera);
    highestCamera->SetEnabled(false);
    CHECK(GameOutputRenderer::FindMainCamera(objects) == firstCamera);
    firstCamera->SetMain(false);
    CHECK(GameOutputRenderer::FindMainCamera(objects) == tiedCamera);
}

TEST_CASE("Game output follows the active edit or disposable play world") {
    SceneDocument document;
    auto editCameraObject = std::make_shared<GameObject>("Edit Main Camera");
    editCameraObject->AddComponent<Transform>();
    Camera* authoredCamera = editCameraObject->AddComponent<Camera>();
    authoredCamera->SetMain(true);
    authoredCamera->SetDepth(5);
    document.EditWorld().Add(editCameraObject);

    CHECK(GameOutputRenderer::FindMainCamera(document.ActiveWorld().Objects()) ==
          authoredCamera);

    REQUIRE(document.EnterPlay());
    Camera* clonedCamera = GameOutputRenderer::FindMainCamera(
        document.ActiveWorld().Objects());
    REQUIRE(clonedCamera != nullptr);
    CHECK(clonedCamera != authoredCamera);

    auto playOnlyObject = std::make_shared<GameObject>("Play-only Main Camera");
    playOnlyObject->AddComponent<Transform>();
    Camera* playOnlyCamera = playOnlyObject->AddComponent<Camera>();
    playOnlyCamera->SetMain(true);
    playOnlyCamera->SetDepth(100);
    document.ActiveWorld().Add(playOnlyObject);
    CHECK(GameOutputRenderer::FindMainCamera(document.ActiveWorld().Objects()) ==
          playOnlyCamera);

    document.ExitPlay();
    CHECK_FALSE(document.IsPlaying());
    CHECK(GameOutputRenderer::FindMainCamera(document.ActiveWorld().Objects()) ==
          authoredCamera);
    CHECK(document.EditWorld().Find("Play-only Main Camera") == nullptr);
}

TEST_CASE("Camera prepares explicit viewport without GLFW state") {
    auto object = std::make_shared<GameObject>("Camera");
    Transform* transform = object->AddComponent<Transform>(25.0f, 50.0f);
    Camera* camera = object->AddComponent<Camera>();
    camera->SetOrthoSize(180.0f);

    REQUIRE(camera->PrepareForViewport({640, 360}));
    CHECK(camera->GetCamera2D()->GetScreenWidth() == doctest::Approx(640.0f));
    CHECK(camera->GetCamera2D()->GetScreenHeight() == doctest::Approx(360.0f));
    CHECK(camera->GetCamera2D()->GetZoom() == doctest::Approx(1.0f));
    CHECK(camera->GetCamera2D()->GetX() == doctest::Approx(transform->GetWorldPosition().x));
    CHECK(camera->GetCamera2D()->GetY() == doctest::Approx(transform->GetWorldPosition().y));
    CHECK_FALSE(camera->PrepareForViewport({0, 360}));
}

TEST_CASE("InputSnapshot maps gameplay state and focus loss releases once") {
    Input::Init(nullptr);
    Input::InitializeDefaultActions();

    InputSnapshot captured;
    captured.keys[GLFW_KEY_D] = true;
    captured.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = true;
    captured.mouseX = 123.0f;
    captured.mouseY = 45.0f;
    captured.pointerValid = true;
    Input::ApplySnapshot(captured);

    CHECK(Input::GetMouseX() == doctest::Approx(123.0f));
    CHECK(Input::GetMouseY() == doctest::Approx(45.0f));
    CHECK(Input::GetMouseButton(GLFW_MOUSE_BUTTON_LEFT));
    CHECK(Input::GetAction("Fire"));
    CHECK(Input::GetAxis("Horizontal") == doctest::Approx(1.0f));

    InputSnapshot outsidePresentation;
    outsidePresentation.keys[GLFW_KEY_D] = true;
    outsidePresentation.gamepadButtons[GLFW_GAMEPAD_BUTTON_A] = true;
    outsidePresentation.pointerValid = false;
    Input::ApplySnapshot(outsidePresentation);
    CHECK_FALSE(Input::GetMouseButton(GLFW_MOUSE_BUTTON_LEFT));
    CHECK(Input::GetMouseButtonUp(GLFW_MOUSE_BUTTON_LEFT));
    CHECK(Input::GetAxis("Horizontal") == doctest::Approx(1.0f));
    CHECK(Input::GetAction("Jump"));

    // Focus loss remains stronger than a presentation-bar miss and releases
    // every device. Re-establish the captured frame to verify its one edge.
    Input::ApplySnapshot(captured);

    Input::ReleaseAll();
    CHECK_FALSE(Input::GetMouseButton(GLFW_MOUSE_BUTTON_LEFT));
    CHECK(Input::GetMouseButtonUp(GLFW_MOUSE_BUTTON_LEFT));
    CHECK(Input::GetActionUp("Fire"));
    CHECK(Input::GetAxis("Horizontal") == doctest::Approx(0.0f));

    Input::ReleaseAll();
    CHECK_FALSE(Input::GetMouseButtonUp(GLFW_MOUSE_BUTTON_LEFT));
    CHECK_FALSE(Input::GetActionUp("Fire"));
}
