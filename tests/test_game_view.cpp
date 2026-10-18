#include "Editor/EditorPreferences.h"
#include "Editor/GameViewLayout.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Editor/SceneDocument.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIFixedTickClock.h"
#include "UI/UISurfaceCoordinateMapping.h"
#include "doctest.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>

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
    saved.sceneView.fxEnabled = true;
    saved.sceneView.litEnabled = false;
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
    CHECK(loaded.sceneView.fxEnabled);
    CHECK_FALSE(loaded.sceneView.litEnabled);

    {
        std::ofstream legacy(path, std::ios::trunc);
        legacy << nlohmann::json{
            {"schemaVersion", 1},
            {"gameView", {
                {"preset", "custom"}, {"displayMode", "100"},
                {"customWidth", 777}, {"customHeight", 333}}}
        }.dump(2);
    }
    REQUIRE(loaded.Load(path, &warning));
    CHECK_FALSE(loaded.sceneView.fxEnabled);
    CHECK(loaded.sceneView.litEnabled);

    {
        std::ofstream previous(path, std::ios::trunc);
        previous << nlohmann::json{
            {"schemaVersion", 2},
            {"gameView", {
                {"preset", "custom"}, {"displayMode", "100"},
                {"customWidth", 777}, {"customHeight", 333}}},
            {"sceneView", {{"fxEnabled", true}}}
        }.dump(2);
    }
    REQUIRE(loaded.Load(path, &warning));
    CHECK(loaded.sceneView.fxEnabled);
    CHECK(loaded.sceneView.litEnabled);

    {
        std::ofstream corrupt(path, std::ios::trunc);
        corrupt << "{ definitely not json";
    }
    CHECK_FALSE(loaded.Load(path, &warning));
    CHECK_FALSE(warning.empty());
    CHECK(loaded.gameView.selectedPreset == ResolutionPreset::BuildResolution);
    CHECK(loaded.gameView.displayMode == GameViewDisplayMode::Fit);
    CHECK((loaded.gameView.customResolution == PixelSize{1280, 720}));
    CHECK(loaded.sceneView.litEnabled);

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

TEST_CASE("Camera prepares explicit viewport without native-window state") {
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
    Input::Init(0);
    Input::InitializeDefaultActions();

    InputSnapshot captured;
    captured.keys[static_cast<std::size_t>(Input::KeyCode::D)] = true;
    captured.mouseButtons[static_cast<std::size_t>(Input::MouseButton::Left)] = true;
    captured.mouseX = 123.0f;
    captured.mouseY = 45.0f;
    captured.pointerValid = true;
    Input::ApplySnapshot(captured);

    CHECK(Input::GetMouseX() == doctest::Approx(123.0f));
    CHECK(Input::GetMouseY() == doctest::Approx(45.0f));
    CHECK(Input::GetMouseButton(Input::MouseButton::Left));
    CHECK(Input::GetAction("Fire"));
    CHECK(Input::GetAxis("Horizontal") == doctest::Approx(1.0f));

    Input::BeginFrame();
    InputSnapshot outsidePresentation;
    outsidePresentation.keys[static_cast<std::size_t>(Input::KeyCode::D)] = true;
    outsidePresentation.gamepadButtons[
        static_cast<std::size_t>(Input::GamepadButton::South)] = true;
    outsidePresentation.pointerValid = false;
    Input::ApplySnapshot(outsidePresentation);
    CHECK_FALSE(Input::GetMouseButton(Input::MouseButton::Left));
    CHECK(Input::GetMouseButtonUp(Input::MouseButton::Left));
    CHECK(Input::GetAxis("Horizontal") == doctest::Approx(1.0f));
    CHECK(Input::GetAction("Jump"));

    // Focus loss remains stronger than a presentation-bar miss and releases
    // every device. Re-establish the captured frame to verify its one edge.
    Input::BeginFrame();
    Input::ApplySnapshot(captured);

    Input::BeginFrame();
    Input::ReleaseAll();
    CHECK_FALSE(Input::GetMouseButton(Input::MouseButton::Left));
    CHECK(Input::GetMouseButtonUp(Input::MouseButton::Left));
    CHECK(Input::GetActionUp("Fire"));
    CHECK(Input::GetAxis("Horizontal") == doctest::Approx(0.0f));

    Input::BeginFrame();
    Input::ReleaseAll();
    CHECK_FALSE(Input::GetMouseButtonUp(Input::MouseButton::Left));
    CHECK_FALSE(Input::GetActionUp("Fire"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 12.3 Step 2: 표면 좌표 매핑과 배치 -> UIFrameInput.
//
// 같은 매핑 값이 포인터 hit-test(여기)와 이후 IME 사각형(Task 14.3)에 쓰인다.
// 기대 raw 값은 손으로 계산한 것이다 — 26.6 raw = 논리 단위 x 64.
// ═══════════════════════════════════════════════════════════════════════════
namespace {

using molga::ui::UISurfaceCoordinateMapping;

std::size_t CountDiagnostics(const molga::text::VectorTextDiagnosticSink& sink,
                             molga::text::TextDiagnosticCode code,
                             const char* subsystem) {
    std::size_t count = 0;
    for (const auto& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code && diagnostic.subsystem == subsystem) ++count;
    }
    return count;
}

// 기대 raw 점 비교 보조.
void CheckPoint(const std::optional<FixedPoint>& actual, std::int32_t rawX,
                std::int32_t rawY) {
    REQUIRE(actual.has_value());
    CHECK(actual->x.Raw() == rawX);
    CHECK(actual->y.Raw() == rawY);
}

} // namespace

TEST_CASE("UISurfaceCoordinateMapping maps a letterboxed Game View panel to canvas points") {
    // 위 "GameView maps panel through target presentation" 케이스와 같은 수:
    // 패널 ppt=1, framebufferScale {2,1.5}, IntegerFit 320x180 -> 800x600
    // (scale 2, content (80,120,640,360)).
    const GameViewLayout panel = GameViewLayout::Calculate(
        {800, 600}, {400.0f, 400.0f}, {2.0f, 1.5f},
        GameViewDisplayMode::PixelPerfect100);
    const OutputPresentationLayout presentation =
        OutputPresentationLayout::IntegerFit({320, 180}, {800, 600});
    REQUIRE(panel.physicalPixelsPerTexel == doctest::Approx(1.0f));

    // 창 안 이미지 원점 (10,20)점 -> (20,30) 창 픽셀. 출력 원점 = 그 + content
    // 원점 x ppt = (100,150). 논리 픽셀 하나 = scale(2) x ppt(1) = 2 창 픽셀.
    const auto mapping = molga::ui::MakeEmbeddedSurfaceMapping(
        presentation, 10.0, 20.0, 2.0, 1.5, panel.physicalPixelsPerTexel, 7);
    REQUIRE(mapping.has_value());
    CHECK(mapping->outputOriginWindowPixelX == doctest::Approx(100.0));
    CHECK(mapping->outputOriginWindowPixelY == doctest::Approx(150.0));
    CHECK(mapping->windowPixelsPerOutputPixelX == doctest::Approx(2.0));
    CHECK(mapping->windowPixelsPerOutputPixelY == doctest::Approx(2.0));
    CHECK(mapping->canvasToOutput.deviceGeneration == 7);

    CheckPoint(mapping->WindowPixelToCanvas(100.0, 150.0), 0, 0);
    // 레터박스 막대: content 왼쪽/위.
    CHECK_FALSE(mapping->WindowPixelToCanvas(99.99, 150.0).has_value());
    CHECK_FALSE(mapping->WindowPixelToCanvas(100.0, 149.99).has_value());
    // 마지막 안쪽 점 (319.5, 179.5) 논리 -> raw (20448, 11488).
    CheckPoint(mapping->WindowPixelToCanvas(739.0, 509.0), 20448, 11488);
    // 반열린 최대 가장자리 (320,180)은 밖이다. 클램프 없음.
    CHECK_FALSE(mapping->WindowPixelToCanvas(740.0, 510.0).has_value());
    CHECK_FALSE(mapping->WindowPixelToCanvas(740.0, 150.0).has_value());
    // 이미지(패널) 밖은 content 안처럼 보여도 밖이다.
    CHECK_FALSE(mapping->WindowPixelToCanvas(19.9, 300.0).has_value());
    // 비유한 값은 밖이다.
    CHECK_FALSE(mapping->WindowPixelToCanvas(
        std::numeric_limits<double>::quiet_NaN(), 200.0).has_value());
    CHECK_FALSE(mapping->WindowPixelToCanvas(
        200.0, std::numeric_limits<double>::infinity()).has_value());

    // 캔버스 100x100 사각형 (110,40)의 중심 (160,90) = 창 픽셀 (420,330).
    const auto center = mapping->WindowPixelToCanvas(420.0, 330.0);
    CheckPoint(center, 160 * 64, 90 * 64);
    const FixedRect rect{Fixed26_6::FromRaw(110 * 64), Fixed26_6::FromRaw(40 * 64),
                         Fixed26_6::FromRaw(100 * 64), Fixed26_6::FromRaw(100 * 64)};
    CHECK(center->x.Raw() >= rect.x.Raw());
    CHECK(center->x.Raw() < rect.x.Raw() + rect.width.Raw());
    CHECK(center->y.Raw() >= rect.y.Raw());
    CHECK(center->y.Raw() < rect.y.Raw() + rect.height.Raw());
}

TEST_CASE("UISurfaceCoordinateMapping maps standalone 1x, 2x and cropped output") {
    // 1x: 800x600 창에 320x180. 같은 content (80,120), scale 2.
    const OutputPresentationLayout oneX =
        OutputPresentationLayout::IntegerFit({320, 180}, {800, 600});
    const auto standalone = molga::ui::MakeStandaloneSurfaceMapping(oneX, 3);
    REQUIRE(standalone.has_value());
    CheckPoint(standalone->WindowPixelToCanvas(80.0, 120.0), 0, 0);
    CheckPoint(standalone->WindowPixelToCanvas(400.0, 300.0), 160 * 64, 90 * 64);
    CHECK_FALSE(standalone->WindowPixelToCanvas(79.0, 300.0).has_value());
    CHECK_FALSE(standalone->WindowPixelToCanvas(720.0, 300.0).has_value());
    // 프레임버퍼(창) 밖.
    CHECK_FALSE(standalone->WindowPixelToCanvas(-1.0, 300.0).has_value());
    CHECK_FALSE(standalone->WindowPixelToCanvas(400.0, 600.0).has_value());

    // 2x 창: 프레임버퍼 1600x1200 -> scale 5, content (0,150,1600,900).
    const OutputPresentationLayout twoX =
        OutputPresentationLayout::IntegerFit({320, 180}, {1600, 1200});
    REQUIRE(twoX.scale == 5);
    const auto retina = molga::ui::MakeStandaloneSurfaceMapping(twoX, 3);
    REQUIRE(retina.has_value());
    CheckPoint(retina->WindowPixelToCanvas(800.0, 600.0), 160 * 64, 90 * 64);
    CheckPoint(retina->WindowPixelToCanvas(0.0, 150.0), 0, 0);
    CHECK_FALSE(retina->WindowPixelToCanvas(800.0, 149.0).has_value());

    // 잘림: 5x4 논리를 4x3 프레임버퍼에. content (-1,-1,5,4), scale 1.
    const OutputPresentationLayout cropped =
        OutputPresentationLayout::IntegerFit({5, 4}, {4, 3});
    REQUIRE(cropped.cropped);
    REQUIRE(cropped.contentRect.x == -1);
    REQUIRE(cropped.contentRect.y == -1);
    const auto crop = molga::ui::MakeStandaloneSurfaceMapping(cropped, 3);
    REQUIRE(crop.has_value());
    CheckPoint(crop->WindowPixelToCanvas(0.0, 0.0), 1 * 64, 1 * 64);
    CheckPoint(crop->WindowPixelToCanvas(3.0, 2.0), 4 * 64, 3 * 64);
    // 프레임버퍼 밖은 잘린 content 안이어도 밖이다.
    CHECK_FALSE(crop->WindowPixelToCanvas(4.0, 0.0).has_value());
    CHECK_FALSE(crop->WindowPixelToCanvas(-1.0, 0.0).has_value());

    // 잘린 임베디드: 이미지 원점 (10,20)점, 1x, ppt 1.
    const auto embeddedCrop = molga::ui::MakeEmbeddedSurfaceMapping(
        cropped, 10.0, 20.0, 1.0, 1.0, 1.0, 3);
    REQUIRE(embeddedCrop.has_value());
    CheckPoint(embeddedCrop->WindowPixelToCanvas(10.0, 20.0), 1 * 64, 1 * 64);
    CHECK_FALSE(embeddedCrop->WindowPixelToCanvas(9.5, 20.0).has_value());
    CHECK_FALSE(embeddedCrop->WindowPixelToCanvas(14.0, 20.0).has_value());

    // 분리된 창: 원점 (0,0), 2x 밀도, 텍셀 하나 = 2 창 픽셀 -> 논리 픽셀 = 4 창 픽셀.
    const OutputPresentationLayout fit =
        OutputPresentationLayout::IntegerFit({320, 180}, {320, 180});
    const auto detached = molga::ui::MakeEmbeddedSurfaceMapping(
        fit, 0.0, 0.0, 2.0, 2.0, 2.0, 3);
    REQUIRE(detached.has_value());
    CHECK(detached->windowPixelsPerOutputPixelX == doctest::Approx(2.0));
    CheckPoint(detached->WindowPixelToCanvas(320.0, 180.0), 160 * 64, 90 * 64);
    CHECK_FALSE(detached->WindowPixelToCanvas(640.0, 100.0).has_value());

    // 잘못된 입력은 매핑을 만들지 않는다.
    CHECK_FALSE(molga::ui::MakeEmbeddedSurfaceMapping(
        OutputPresentationLayout{}, 0.0, 0.0, 1.0, 1.0, 1.0, 3).has_value());
    CHECK_FALSE(molga::ui::MakeEmbeddedSurfaceMapping(
        fit, 0.0, 0.0, 0.0, 1.0, 1.0, 3).has_value());
    CHECK_FALSE(molga::ui::MakeEmbeddedSurfaceMapping(
        fit, 0.0, 0.0, 1.0, 1.0, 0.0, 3).has_value());
    CHECK_FALSE(molga::ui::MakeEmbeddedSurfaceMapping(
        fit, std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, 1.0, 1.0, 3)
                    .has_value());
    CHECK_FALSE(molga::ui::MakeStandaloneSurfaceMapping(
        OutputPresentationLayout{}, 3).has_value());
}

TEST_CASE("MapNativeInputBatch preserves order, sequence and signed values") {
    using molga::platform::NativeInputBatch;
    using molga::platform::NativeInputEvent;
    using molga::platform::NativeInputEventKind;
    using molga::platform::NativeSurfaceBatchStart;

    const OutputPresentationLayout presentation =
        OutputPresentationLayout::IntegerFit({320, 180}, {800, 600});
    const auto mapping = molga::ui::MakeStandaloneSurfaceMapping(presentation, 3);
    REQUIRE(mapping.has_value());
    constexpr molga::WindowId kSurface = 10;

    auto make = [](std::uint64_t sequence, molga::WindowId window,
                   NativeInputEventKind kind) {
        NativeInputEvent event;
        event.sequence = sequence;
        event.windowId = window;
        event.kind = kind;
        event.nativeType = 1000 + static_cast<std::uint32_t>(sequence);
        return event;
    };
    NativeInputBatch batch;
    {
        NativeInputEvent key = make(40, kSurface, NativeInputEventKind::Key);
        key.control = 40;
        key.keyModifiers.gui = true;
        key.active = true;
        NativeInputEvent scroll = make(41, kSurface, NativeInputEventKind::Scroll);
        scroll.deltaX = -2.5f;
        scroll.deltaY = 4.0f;
        scroll.x = 400.0f;
        scroll.y = 300.0f;
        NativeInputEvent axis = make(42, kSurface, NativeInputEventKind::GamepadAxis);
        axis.control = static_cast<std::uint32_t>(Input::GamepadAxis::RightY);
        axis.axisValue = -0.75f;
        NativeInputEvent inside = make(43, kSurface, NativeInputEventKind::PointerMotion);
        inside.x = 400.0f;
        inside.y = 300.0f;
        NativeInputEvent bar = make(44, kSurface, NativeInputEventKind::PointerMotion);
        bar.x = 10.0f;
        bar.y = 10.0f;
        NativeInputEvent foreign = make(45, 20, NativeInputEventKind::PointerMotion);
        foreign.x = 400.0f;
        foreign.y = 300.0f;
        NativeInputEvent pad = make(46, kSurface, NativeInputEventKind::GamepadButton);
        pad.control = static_cast<std::uint32_t>(Input::GamepadButton::South);
        pad.active = true;
        NativeInputEvent padNoWindow = make(47, 0, NativeInputEventKind::GamepadButton);
        batch.events = {key, scroll, axis, inside, bar, foreign, pad, padNoWindow};
    }
    NativeSurfaceBatchStart seed;
    seed.windowId = kSurface;
    seed.nativeWindowFocused = true;
    seed.pointerWindowPixelX = 400;
    seed.pointerWindowPixelY = 300;
    seed.pointerWindowPixelValid = true;
    NativeSurfaceBatchStart other;
    other.windowId = 20;
    batch.surfaceStarts = {other, seed};

    molga::text::VectorTextDiagnosticSink sink;
    const auto input = molga::ui::MapNativeInputBatch(
        batch, kSurface, *mapping, 1.0, 1.0, sink);
    REQUIRE(input.has_value());
    CHECK(sink.Diagnostics().empty());
    CHECK(input->windowId == kSurface);
    REQUIRE(input->orderedEvents.size() == batch.events.size());
    for (std::size_t i = 0; i < batch.events.size(); ++i) {
        CHECK(input->orderedEvents[i].sequence == batch.events[i].sequence);
        CHECK(input->orderedEvents[i].nativeType == batch.events[i].nativeType);
        CHECK(input->orderedEvents[i].traceOrdinal == 0);
        CHECK(input->orderedEvents[i].windowId == batch.events[i].windowId);
    }
    using molga::ui::UIInputEventKind;
    const auto& events = input->orderedEvents;
    CHECK(events[0].kind == UIInputEventKind::Key);
    CHECK(events[0].keyModifiers.gui);
    CHECK(events[0].active);
    CHECK(events[0].control == 40);
    CHECK(events[1].kind == UIInputEventKind::Scroll);
    CHECK(events[1].logicalDelta.x.Raw() == -160);
    CHECK(events[1].logicalDelta.y.Raw() == 256);
    CHECK(events[1].logicalPointValid);
    CHECK(events[2].kind == UIInputEventKind::GamepadAxis);
    CHECK(events[2].axisValue.Raw() == -48);
    CHECK(events[3].logicalPointValid);
    CHECK(events[3].logicalPoint.x.Raw() == 160 * 64);
    CHECK(events[3].logicalPoint.y.Raw() == 90 * 64);
    // 막대 위: 클램프하지 않고 무효(이탈)다.
    CHECK_FALSE(events[4].logicalPointValid);
    // 다른 창: 이 표면의 좌표계로 풀지 않는다.
    CHECK(events[5].windowId == 20);
    CHECK_FALSE(events[5].logicalPointValid);
    // 게임패드: 호스트가 정한 창 그대로. 매퍼가 재배정하지 않는다.
    CHECK(events[6].windowId == kSurface);
    CHECK(events[7].windowId == 0);
    // 씨앗은 첫 이동과 같은 물리 픽셀이므로 바이트까지 같다.
    CHECK(input->pointerAtBatchStartValid);
    CHECK(input->pointerAtBatchStart == events[3].logicalPoint);
    CHECK(input->nativeWindowFocusedAtBatchStart);

    // 무효 씨앗 포인터: (0,0)을 지어내지 않는다.
    batch.surfaceStarts[1].pointerWindowPixelValid = false;
    const auto noPointer = molga::ui::MapNativeInputBatch(
        batch, kSurface, *mapping, 1.0, 1.0, sink);
    REQUIRE(noPointer.has_value());
    CHECK_FALSE(noPointer->pointerAtBatchStartValid);
    // 씨앗이 막대 위(content 밖)이면 무효다.
    batch.surfaceStarts[1].pointerWindowPixelValid = true;
    batch.surfaceStarts[1].pointerWindowPixelX = 10;
    batch.surfaceStarts[1].pointerWindowPixelY = 10;
    const auto barSeed = molga::ui::MapNativeInputBatch(
        batch, kSurface, *mapping, 1.0, 1.0, sink);
    REQUIRE(barSeed.has_value());
    CHECK_FALSE(barSeed->pointerAtBatchStartValid);
    CHECK(sink.Diagnostics().empty());

    // 씨앗이 없거나 중복이면 배치 전체를 거절하고 한 번만 보고한다.
    {
        molga::text::VectorTextDiagnosticSink missingSink;
        NativeInputBatch missing = batch;
        missing.surfaceStarts = {other};
        CHECK_FALSE(molga::ui::MapNativeInputBatch(
            missing, kSurface, *mapping, 1.0, 1.0, missingSink).has_value());
        CHECK(CountDiagnostics(missingSink,
                               molga::text::TextDiagnosticCode::ReferenceInvalid,
                               "ui.frame") == 1);
    }
    {
        molga::text::VectorTextDiagnosticSink duplicateSink;
        NativeInputBatch duplicate = batch;
        duplicate.surfaceStarts = {seed, other, seed};
        CHECK_FALSE(molga::ui::MapNativeInputBatch(
            duplicate, kSurface, *mapping, 1.0, 1.0, duplicateSink).has_value());
        CHECK(CountDiagnostics(duplicateSink,
                               molga::text::TextDiagnosticCode::ReferenceInvalid,
                               "ui.frame") == 1);
    }
}

TEST_CASE("MapNativeInputBatch multiplies window points by the pixel density exactly once") {
    using molga::platform::NativeInputBatch;
    using molga::platform::NativeInputEvent;
    using molga::platform::NativeInputEventKind;
    using molga::platform::NativeSurfaceBatchStart;

    // 2x 창: 점 (400,300) = 창 픽셀 (800,600) = 논리 (160,90).
    const OutputPresentationLayout twoX =
        OutputPresentationLayout::IntegerFit({320, 180}, {1600, 1200});
    const auto mapping = molga::ui::MakeStandaloneSurfaceMapping(twoX, 3);
    REQUIRE(mapping.has_value());
    NativeInputEvent motion;
    motion.sequence = 5;
    motion.windowId = 10;
    motion.kind = NativeInputEventKind::PointerMotion;
    motion.x = 400.0f;
    motion.y = 300.0f;
    NativeSurfaceBatchStart seed;
    seed.windowId = 10;
    seed.pointerWindowPixelX = 800;   // 호스트가 이미 픽셀로 바꾼 값: 다시 곱하지 않는다.
    seed.pointerWindowPixelY = 600;
    seed.pointerWindowPixelValid = true;
    NativeInputBatch batch;
    batch.events = {motion};
    batch.surfaceStarts = {seed};

    molga::text::VectorTextDiagnosticSink sink;
    const auto input = molga::ui::MapNativeInputBatch(
        batch, 10, *mapping, 2.0, 2.0, sink);
    REQUIRE(input.has_value());
    REQUIRE(input->orderedEvents.size() == 1);
    CHECK(input->orderedEvents[0].logicalPointValid);
    CHECK(input->orderedEvents[0].logicalPoint.x.Raw() == 160 * 64);
    CHECK(input->orderedEvents[0].logicalPoint.y.Raw() == 90 * 64);
    CHECK(input->pointerAtBatchStartValid);
    CHECK(input->pointerAtBatchStart == input->orderedEvents[0].logicalPoint);
    CHECK(sink.Diagnostics().empty());
}

TEST_CASE("MapNativeInputBatch reports non-finite values once and keeps the sequence audit") {
    using molga::platform::NativeInputBatch;
    using molga::platform::NativeInputEvent;
    using molga::platform::NativeInputEventKind;
    using molga::platform::NativeSurfaceBatchStart;

    const OutputPresentationLayout presentation =
        OutputPresentationLayout::IntegerFit({320, 180}, {800, 600});
    const auto mapping = molga::ui::MakeStandaloneSurfaceMapping(presentation, 3);
    REQUIRE(mapping.has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();

    NativeInputEvent scroll;
    scroll.sequence = 1;
    scroll.windowId = 10;
    scroll.kind = NativeInputEventKind::Scroll;
    scroll.deltaX = nan;
    scroll.deltaY = 1.0f;
    scroll.x = 400.0f;
    scroll.y = 300.0f;
    NativeInputEvent motion;
    motion.sequence = 2;
    motion.windowId = 10;
    motion.kind = NativeInputEventKind::PointerMotion;
    motion.x = nan;
    motion.y = 300.0f;
    NativeInputEvent axis;
    axis.sequence = 3;
    axis.windowId = 10;
    axis.kind = NativeInputEventKind::GamepadAxis;
    axis.axisValue = std::numeric_limits<float>::infinity();
    NativeSurfaceBatchStart seed;
    seed.windowId = 10;
    NativeInputBatch batch;
    batch.events = {scroll, motion, axis};
    batch.surfaceStarts = {seed};

    molga::text::VectorTextDiagnosticSink sink;
    const auto input = molga::ui::MapNativeInputBatch(
        batch, 10, *mapping, 1.0, 1.0, sink);
    REQUIRE(input.has_value());
    REQUIRE(input->orderedEvents.size() == 3);
    // 시퀀스 감사는 보존되고 값만 0/무효가 된다.
    CHECK(input->orderedEvents[0].sequence == 1);
    CHECK(input->orderedEvents[0].logicalDelta.x.Raw() == 0);
    CHECK(input->orderedEvents[0].logicalDelta.y.Raw() == 64);
    CHECK(input->orderedEvents[1].sequence == 2);
    CHECK_FALSE(input->orderedEvents[1].logicalPointValid);
    CHECK(input->orderedEvents[2].axisValue.Raw() == 0);
    // 배치당 한 번.
    CHECK(CountDiagnostics(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                           "ui.frame") == 1);
    CHECK(sink.Diagnostics().size() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 12.3 Step 3: 프로덕션 고정 UI tick 시계.
//
// 1/64초 = 15'625'000ns. 경과 시간이 인자로 도착하므로 시계는 벽시계를 읽지
// 않는다. 산수: 7ms는 한 걸음에 못 미쳐 이월(7ms), +9ms = 16ms >= 15.625ms이면
// tick 1(이월 0.375ms), +31.25ms = 31.625ms이면 tick 2,3(이월 0.375ms).
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("UIFixedTickClock emits one 1/64 s tick per complete interval and keeps the carry") {
    using molga::ui::UIDeterministicTick;
    using molga::ui::UIFixedTickClock;
    static_assert(UIFixedTickClock::StepNanoseconds == 15'625'000,
                  "one tick is exactly 1/64 s");

    molga::text::VectorTextDiagnosticSink sink;
    UIFixedTickClock clock;
    UIFixedTickClock twin;
    const std::uint64_t elapsed[] = {7'000'000, 9'000'000, 31'250'000};
    std::vector<UIDeterministicTick> all;
    std::vector<std::vector<UIDeterministicTick>> perCall;
    for (std::uint64_t ns : elapsed) {
        perCall.push_back(clock.AdvanceElapsedNanoseconds(ns, sink));
        const auto again = twin.AdvanceElapsedNanoseconds(ns, sink);
        // 두 인스턴스는 같은 입력에서 같은 값을 낸다.
        CHECK(again == perCall.back());
        all.insert(all.end(), perCall.back().begin(), perCall.back().end());
    }
    CHECK(perCall[0].empty());
    REQUIRE(perCall[1].size() == 1);
    REQUIRE(perCall[2].size() == 2);
    CHECK(perCall[1][0].tickIndex == 1);
    CHECK(perCall[2][0].tickIndex == 2);
    CHECK(perCall[2][1].tickIndex == 3);
    for (const UIDeterministicTick& tick : all) {
        CHECK(tick.deltaSeconds.Raw() == 1);
    }
    REQUIRE(all.size() == 3);
    CHECK(molga::ui::UIDeterministicTickBatchIsValid(all));
    CHECK(molga::ui::DecodeCanonicalTicks(molga::ui::EncodeCanonicalTicks(all)) == all);

    // 이월이 보존된다: 남은 0.375ms + 15.25ms = 15.625ms 정확히 한 걸음.
    const auto exact = clock.AdvanceElapsedNanoseconds(15'250'000, sink);
    REQUIRE(exact.size() == 1);
    CHECK(exact[0].tickIndex == 4);
    // 0은 시간이 흐르지 않은 것이다: tick도 진단도 없다.
    CHECK(clock.AdvanceElapsedNanoseconds(0, sink).empty());
    // 한 번에 많은 걸음: 1초 = 64 tick, 인덱스는 이어진다.
    const auto second = clock.AdvanceElapsedNanoseconds(1'000'000'000, sink);
    REQUIRE(second.size() == 64);
    CHECK(second.front().tickIndex == 5);
    CHECK(second.back().tickIndex == 68);
    CHECK(sink.Diagnostics().empty());
}

TEST_CASE("UIFixedTickClock fails closed before its index can wrap or its carry overflow") {
    using molga::ui::UIFixedTickClock;
    {
        molga::text::VectorTextDiagnosticSink sink;
        UIFixedTickClock clock = UIFixedTickClock::WithNextTickIndexForTesting(
            std::numeric_limits<std::uint64_t>::max());
        // 인덱스를 쓸 수 없으니 tick을 내지 않고 Blocker 한 번.
        CHECK(clock.AdvanceElapsedNanoseconds(
                       UIFixedTickClock::StepNanoseconds, sink).empty());
        REQUIRE(CountDiagnostics(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                                 "ui.frame") == 1);
        CHECK(sink.Diagnostics()[0].severity == molga::text::TextSeverity::Blocker);
        // 두 번째 호출은 조용하다.
        CHECK(clock.AdvanceElapsedNanoseconds(
                       UIFixedTickClock::StepNanoseconds, sink).empty());
        CHECK(sink.Diagnostics().size() == 1);
    }
    {
        // 마지막으로 쓸 수 있는 인덱스(UINT64_MAX-1)까지는 낸다.
        molga::text::VectorTextDiagnosticSink sink;
        UIFixedTickClock clock = UIFixedTickClock::WithNextTickIndexForTesting(
            std::numeric_limits<std::uint64_t>::max() - 1);
        const auto ticks = clock.AdvanceElapsedNanoseconds(
            2 * UIFixedTickClock::StepNanoseconds, sink);
        REQUIRE(ticks.size() == 1);
        CHECK(ticks[0].tickIndex == std::numeric_limits<std::uint64_t>::max() - 1);
        CHECK(CountDiagnostics(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                               "ui.frame") == 1);
    }
    {
        // 이월 + 경과가 넘치면 아무것도 내지 않고 이월은 그대로다.
        molga::text::VectorTextDiagnosticSink sink;
        UIFixedTickClock clock;
        CHECK(clock.AdvanceElapsedNanoseconds(7'000'000, sink).empty());
        CHECK(clock.AdvanceElapsedNanoseconds(
                       std::numeric_limits<std::uint64_t>::max(), sink).empty());
        CHECK(CountDiagnostics(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                               "ui.frame") == 1);
        // 이월 7ms가 살아 있다: 8.625ms를 더하면 한 걸음.
        const auto ticks = clock.AdvanceElapsedNanoseconds(8'625'000, sink);
        REQUIRE(ticks.size() == 1);
        CHECK(ticks[0].tickIndex == 1);
    }
}

TEST_CASE("Time records one monotonic elapsed sample per Update") {
    Time::Init();
    CHECK(Time::GetElapsedNanoseconds() == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    Time::Update();
    const std::uint64_t first = Time::GetElapsedNanoseconds();
    CHECK(first >= 3'000'000);
    CHECK(first < 5'000'000'000ULL);
    // 샘플은 Update가 기록한 한 값이다: 읽기만으로는 변하지 않는다.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(Time::GetElapsedNanoseconds() == first);
    Time::Update();
    CHECK(Time::GetElapsedNanoseconds() >= 2'000'000);
    // Init은 표본을 되돌린다.
    Time::Init();
    CHECK(Time::GetElapsedNanoseconds() == 0);
}
