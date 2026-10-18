#include "Editor/Windows/GameViewWindow.h"

#include "Common/Log.h"
#include "Editor/Editor.h"
#include "Editor/EditorConstants.h"
#include "Editor/ImGuiTextureBridge.h"
#include "Editor/Project.h"
#include "Core/World.h"
#include "Core/MolgaTime.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/Renderer.h"
#include "Systems/Input.h"
#include "UI/UISurfaceCoordinateMapping.h"
#include "UI/UISystem.h"

#include <algorithm>
#include <cstdint>
#include <imgui_internal.h>

namespace {
molga::WindowId ViewportWindowId(const ImGuiViewport* viewport) {
    return viewport && viewport->PlatformHandle
        ? static_cast<molga::WindowId>(
              reinterpret_cast<std::intptr_t>(viewport->PlatformHandle))
        : 0;
}
} // namespace

GameViewWindow::GameViewWindow()
    : EditorWindow(EditorConstants::WIN_GAME),
      preferencePath_(molga::EditorPreferences::DefaultPath()) {
    std::string warning;
    preferences_.Load(preferencePath_, &warning);
    if (!warning.empty()) Log::Warn("EditorPreferences", warning);
}

GameViewWindow::~GameViewWindow() {
    // 소멸 시점에는 world_가 이미 죽었을 수 있다(play 월드는 ExitPlay가 먼저 부순다).
    // 그래서 월드를 건드리는 ResetPlayInput 대신 스크립트 쪽 입력과 소유권만 놓는다.
    inputFocused_ = false;
    imageValid_ = false;
    Input::DiscardPendingScroll(inputSourceWindow_);
    Input::ReleaseAll();
    ReleaseRetainedUISnapshots();
}

void GameViewWindow::SetFrameNativeInput(molga::platform::NativeInputBatch&& batch) {
    pendingBatch_ = std::move(batch);
}

void GameViewWindow::ReleaseRetainedUISnapshots() {
    renderSnapshot_.reset();
    pendingBatch_ = {};
}

void GameViewWindow::SetSceneResources(
    Renderer* renderer,
    Shader* spriteShader,
    World* world,
    TextRenderer* textRenderer,
    molga::text::TextDiagnosticSink* textDiagnostics) {
    renderer_ = renderer;
    spriteShader_ = spriteShader;
    world_ = world;
    textRenderer_ = textRenderer;
    textDiagnostics_ = textDiagnostics;
}

molga::PixelSize GameViewWindow::RequestedOutputSize() const {
    molga::PixelSize build{800, 600};
    if (Project::Get().IsOpen()) {
        const auto& window = Project::Get().GetBuildProfile().window;
        build = {window.width, window.height};
    }
    return molga::ResolveResolutionPreset(
        preferences_.gameView.selectedPreset, build,
        preferences_.gameView.customResolution);
}

molga::PixelSize GameViewWindow::RequestedLogicalSize() const {
    if (Project::Get().IsOpen()) {
        const auto& window = Project::Get().GetBuildProfile().window;
        return {window.width, window.height};
    }
    return {800, 600};
}

molga::GameOutputScaleMode GameViewWindow::RequestedScaleMode() const {
    return Project::Get().IsOpen()
        ? Project::Get().GetBuildProfile().window.outputScaleMode
        : molga::GameOutputScaleMode::Native;
}

void GameViewWindow::SavePreferences() {
    molga::EditorPreferences latest;
    std::string ignored;
    latest.Load(preferencePath_, &ignored);
    latest.gameView = preferences_.gameView;
    std::string error;
    if (!latest.SaveAtomic(preferencePath_, &error) && !error.empty()) {
        Log::Warn("EditorPreferences", error);
    }
    preferences_.sceneView = latest.sceneView;
}

void GameViewWindow::DrawToolbar() {
    auto& game = preferences_.gameView;
    if (ImGui::BeginCombo("##GameResolution",
                          molga::ResolutionPresetLabel(game.selectedPreset))) {
        for (molga::ResolutionPreset preset : {
                 molga::ResolutionPreset::BuildResolution,
                 molga::ResolutionPreset::R320x180,
                 molga::ResolutionPreset::R640x360,
                 molga::ResolutionPreset::R1280x720,
                 molga::ResolutionPreset::R1920x1080,
                 molga::ResolutionPreset::Custom}) {
            const bool selected = game.selectedPreset == preset;
            if (ImGui::Selectable(molga::ResolutionPresetLabel(preset), selected)) {
                game.selectedPreset = preset;
                SavePreferences();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (game.selectedPreset == molga::ResolutionPreset::Custom) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        int width = game.customResolution.width;
        if (ImGui::InputInt("##GameWidth", &width, 0, 0)) {
            game.customResolution.width = std::clamp(width, 1, 65536);
            SavePreferences();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        int height = game.customResolution.height;
        if (ImGui::InputInt("##GameHeight", &height, 0, 0)) {
            game.customResolution.height = std::clamp(height, 1, 65536);
            SavePreferences();
        }
    }

    ImGui::SameLine();
    const bool fit = game.displayMode == molga::GameViewDisplayMode::Fit;
    if (fit) ImGui::BeginDisabled();
    if (ImGui::Button("Fit")) {
        game.displayMode = molga::GameViewDisplayMode::Fit;
        SavePreferences();
    }
    if (fit) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!fit) ImGui::BeginDisabled();
    if (ImGui::Button("100%")) {
        game.displayMode = molga::GameViewDisplayMode::PixelPerfect100;
        SavePreferences();
    }
    if (!fit) ImGui::EndDisabled();

    if (inputFocused_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Input Focus");
    }
}

void GameViewWindow::OnGUI() {
    if (!isOpen) {
        LoseInputFocus();
        renderSnapshot_.reset();
        return;
    }

    const bool visible = ImGui::Begin(title.c_str(), &isOpen,
                                      ImGuiWindowFlags_NoCollapse);
    if (!isOpen) LoseInputFocus();
    if (!visible) {
        LoseInputFocus();
        renderSnapshot_.reset();
        ImGui::End();
        return;
    }
    DrawToolbar();
    ImGui::Separator();

    const molga::PixelSize requested = RequestedOutputSize();
    if (requested != lastAttemptedOutput_) {
        lastAttemptedOutput_ = requested;
        if (!requested.IsValid()) {
            outputError_ = "Invalid output resolution";
        } else if (outputTarget_.Resize(requested.width, requested.height)) {
            activeOutput_ = requested;
            outputError_.clear();
        } else {
            outputError_ = "Resolution rejected by this GPU; showing last valid output";
        }
    }

    logicalOutput_ = RequestedLogicalSize();
    if (outputTarget_.IsValid() && renderer_ && spriteShader_ && world_ &&
        textRenderer_ && textDiagnostics_) {
        // Task 12.3 Step 6: UI 층은 ProcessPlayInput가 이번 프레임에 게시한 N+1 하나로
        // 그려진다. 입력이 없는 프레임(편집 모드, 일시정지, 포커스 없는 패널의 입력
        // 생략 등)은 같은 표면으로 직접 짓는다. 가져가는 즉시 멤버를 비운다 — 프레임을
        // 넘겨 쥐면 장치 종료를 막는 수명 토큰을 든다.
        molga::ui::UISnapshotPtr uiSnapshot = std::move(renderSnapshot_);
        renderSnapshot_.reset();
        const auto uiViewport = molga::GameOutputRenderer::UiLogicalViewport(
            RequestedScaleMode(), logicalOutput_, activeOutput_);
        const molga::WindowId uiWindow = ViewportWindowId(ImGui::GetWindowViewport());
        // 해상도가 이 프레임에 바뀌었거나 표면/세대가 달라졌다면 입력 단계의 스냅샷은
        // 이 출력과 맞지 않는다: 버리고 다시 짓는다(맞지 않는 채로 그리면 거절 진단이
        // 매 프레임 난다).
        if (uiSnapshot &&
            (!uiViewport || uiSnapshot->logicalViewport != *uiViewport ||
             uiSnapshot->surfaceWindowId != uiWindow ||
             uiSnapshot->worldGeneration != world_->Generation())) {
            uiSnapshot.reset();
        }
        // 창 0은 UI 표면이 아니다: UI 층 없이 그린다(프레임마다 진단을 내지 않는다).
        if (!uiSnapshot && uiViewport && uiWindow != 0) {
            uiSnapshot = UISystem::Get().BuildLayout(
                *world_, uiWindow, *uiViewport, *textDiagnostics_);
        }
        molga::GameOutputRequest request{activeOutput_, logicalOutput_,
                                         RequestedScaleMode(), &outputTarget_};
        request.uiSnapshot = uiSnapshot.get();
        const molga::GameOutputResult result = outputRenderer_.Render(
            *world_, request, *renderer_, spriteShader_, *textRenderer_,
            *textDiagnostics_);
        presentation_ = result.presentation;
        hasOutputCamera_ = result.cameraLayout.HasRenderableCamera();
        auto& renderStats = Editor::Get().RenderStats();
        renderStats.outputCameraPasses += static_cast<int>(std::count_if(
            result.cameraResults.begin(), result.cameraResults.end(),
            [](const molga::CameraOutputResult& camera) {
                return camera.rendered;
            }));
        renderStats.postProcessPasses += result.postProcessPasses;
        renderStats.lightingPasses += result.lightingPasses;
        renderStats.shadowPasses += result.shadowPasses;
        renderStats.selectedLightCount += result.selectedLightCount;
        renderStats.shadowedLightCount += result.shadowedLightCount;
        renderStats.shadowCasterDrawCount += result.shadowCasterDrawCount;
        if (result.allocationFailed) {
            outputError_ =
                "Logical output framebuffer allocation failed; showing last output";
        } else if (lastAttemptedOutput_ == activeOutput_) {
            outputError_.clear();
        }
    } else {
        presentation_ = molga::OutputPresentationLayout::Calculate(
            RequestedScaleMode(), logicalOutput_, activeOutput_);
        hasOutputCamera_ = false;
    }

    ImGui::BeginChild("##GameOutputArea", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGuiViewport* platformViewport = ImGui::GetWindowViewport();
    float dpi = platformViewport ? platformViewport->DpiScale : 0.0f;
    if (dpi <= 0.0f) {
        dpi = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
    }
    molga::GameViewPoint framebufferScale{dpi, dpi};
    const molga::WindowId platformWindow = ViewportWindowId(platformViewport);
    molga::WindowMetrics platformMetrics;
    if (molga::QueryWindowMetrics(platformWindow, platformMetrics)) {
        // 창 점 -> 창 물리 픽셀 배율의 출처는 하나다: 호스트의 포인터 씨앗과 같은
        // SDL 픽셀 밀도(없을 때만 픽셀/논리 크기). 렌더 배치(이 레이아웃)와 입력
        // 매핑이 같은 값을 쓰므로 둘이 한 픽셀씩 어긋나지 않는다.
        if (const auto scale = molga::ResolveWindowPixelsPerPoint(platformMetrics)) {
            framebufferScale.x = static_cast<float>(scale->x);
            framebufferScale.y = static_cast<float>(scale->y);
        }
    }
    layout_ = molga::GameViewLayout::Calculate(
        activeOutput_, {available.x, available.y}, framebufferScale,
        preferences_.gameView.displayMode);

    const ImVec2 contentStart = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(contentStart.x + layout_.imageRect.x,
                               contentStart.y + layout_.imageRect.y));
    imageScreenOrigin_ = ImGui::GetCursorScreenPos();
    imageValid_ = outputTarget_.IsValid() && layout_.imageRect.width > 0.0f &&
                  layout_.imageRect.height > 0.0f;
    platformViewportId_ = platformViewport ? platformViewport->ID : 0;
    if (platformWindow != inputSourceWindow_) {
        Input::DiscardPendingScroll(inputSourceWindow_);
        inputSourceWindow_ = platformWindow;
        Input::DiscardPendingScroll(inputSourceWindow_);
    }
    if (!inputFocused_) Input::DiscardPendingScroll(inputSourceWindow_);

    if (imageValid_) {
        ImGui::Image(ImGuiTextureBridge::From(outputTarget_.ColorView().texture),
                     ImVec2(layout_.imageRect.width, layout_.imageRect.height),
                     ImVec2(0, 0), ImVec2(1, 1));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            Input::DiscardPendingScroll(inputSourceWindow_);
            inputFocused_ = true;
            ImGui::SetWindowFocus();
        }
    } else {
        ImGui::Dummy(ImVec2(std::max(available.x, 1.0f),
                           std::max(available.y, 1.0f)));
    }
    // 자식 창의 보이는 영역(스크롤바 제외). 입력 단계가 다음 프레임에 쓴다.
    {
        const ImRect clip = ImGui::GetCurrentWindow()->ClipRect;
        childClipValid_ = true;
        childClipMin_ = clip.Min;
        childClipMax_ = clip.Max;
    }

    if (inputFocused_ &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        LoseInputFocus();
    }

    const char* overlay = nullptr;
    if (!outputError_.empty()) overlay = outputError_.c_str();
    else if (!hasOutputCamera_) overlay = "No active Output Camera";
    if (overlay) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 textSize = ImGui::CalcTextSize(overlay);
        const ImVec2 position(
            imageScreenOrigin_.x + std::max(8.0f,
                (layout_.imageRect.width - textSize.x) * 0.5f),
            imageScreenOrigin_.y + std::max(8.0f,
                (layout_.imageRect.height - textSize.y) * 0.5f));
        draw->AddRectFilled(ImVec2(position.x - 6.0f, position.y - 4.0f),
                            ImVec2(position.x + textSize.x + 6.0f,
                                   position.y + textSize.y + 4.0f),
                            IM_COL32(15, 15, 18, 210), 4.0f);
        draw->AddText(position, IM_COL32(230, 230, 235, 255), overlay);
    }
    ImGui::EndChild();
    ImGui::End();
}

void GameViewWindow::DepartUIPointer() {
    // world_를 읽지 않는다: 씬 전환 커밋 직후에는 옛 월드가 이미 파괴돼 있다. 마지막으로
    // 프레임을 처리한 표면의 이름(창, 세대)만 쓴다. 이미 해제된 세대의 표는 비어
    // 있으므로 이탈은 아무것도 만들지 않는다.
    if (!textDiagnostics_ || uiSurfaceWindow_ == 0 || uiSurfaceWorldGeneration_ == 0) {
        return;
    }
    UISystem::Get().OnSurfacePointerDeparted(
        uiSurfaceWindow_, uiSurfaceWorldGeneration_, *textDiagnostics_);
}

void GameViewWindow::LoseInputFocus() {
    if (!inputFocused_) return;
    inputFocused_ = false;
    Input::DiscardPendingScroll(inputSourceWindow_);
    Input::ReleaseAll();
    DepartUIPointer();
}

void GameViewWindow::ProcessPlayInput() {
    // 배치는 UI 월드가 있든 없든 이 프레임에 한 번 가져간다. 이전 프레임의 N+1은
    // 소유하지 않는다.
    molga::platform::NativeInputBatch batch = std::move(pendingBatch_);
    pendingBatch_ = {};
    renderSnapshot_.reset();

    if (!isOpen || !imageValid_ || platformViewportId_ == 0 ||
        !ImGui::GetCurrentContext() || !world_ || !activeOutput_.IsValid() ||
        !textRenderer_ || !textDiagnostics_) {
        if (inputFocused_) LoseInputFocus();
        else Input::ReleaseAll();
        return;
    }

    ImGuiViewport* viewport = ImGui::FindViewportByID(platformViewportId_);
    const molga::WindowId nativeWindow = ViewportWindowId(viewport);
    molga::WindowMetrics nativeMetrics;
    if (!viewport || nativeWindow == 0 ||
        !molga::QueryWindowMetrics(nativeWindow, nativeMetrics)) {
        if (inputFocused_) LoseInputFocus();
        else Input::ReleaseAll();
        return;
    }
    if (nativeWindow != inputSourceWindow_) {
        Input::DiscardPendingScroll(inputSourceWindow_);
        inputSourceWindow_ = nativeWindow;
        Input::DiscardPendingScroll(inputSourceWindow_);
    }

    // ── UI 프레임: 배치를 한 번 매핑해 ProcessFrame에 넘긴다 ─────────────────
    // 진입 시점의 포커스로 이벤트를 거른다. 네이티브 포커스가 이 배치 안에서 사라졌다면
    // 배치의 WindowFocus(false)가 UI 포커스 시스템에 닿아야 하므로, 아래 LoseInputFocus는
    // ProcessFrame **뒤**에 부른다.
    const bool focusedOnEntry = inputFocused_;
    const molga::OutputPresentationLayout inputPresentation =
        molga::OutputPresentationLayout::Calculate(
            RequestedScaleMode(), RequestedLogicalSize(), activeOutput_);
    const auto uiViewport = molga::GameOutputRenderer::UiLogicalViewport(
        RequestedScaleMode(), RequestedLogicalSize(), activeOutput_);
    // 점 -> 물리 픽셀 배율은 레이아웃이 쓰는 framebufferScale 하나다(OnGUI가 SDL 밀도로
    // 정했다). 매핑과 배치 매핑이 같은 값을 쓴다.
    const double pixelsPerPointX = layout_.framebufferScale.x;
    const double pixelsPerPointY = layout_.framebufferScale.y;
    const molga::GraphicsDevice* device = molga::GraphicsDevice::Current();
    auto mapping = molga::ui::MakeEmbeddedSurfaceMapping(
        inputPresentation, imageScreenOrigin_.x - viewport->Pos.x,
        imageScreenOrigin_.y - viewport->Pos.y, pixelsPerPointX, pixelsPerPointY,
        layout_.physicalPixelsPerTexel, device ? device->Generation() : 0);
    if (mapping && childClipValid_) {
        mapping->ClipVisibleWindowPixels(
            (childClipMin_.x - viewport->Pos.x) * pixelsPerPointX,
            (childClipMin_.y - viewport->Pos.y) * pixelsPerPointY,
            (childClipMax_.x - viewport->Pos.x) * pixelsPerPointX,
            (childClipMax_.y - viewport->Pos.y) * pixelsPerPointY);
    }
    if (mapping && uiViewport) {
        auto input = molga::ui::MapNativeInputBatch(
            batch, nativeWindow, *mapping, pixelsPerPointX, pixelsPerPointY,
            *textDiagnostics_);
        if (input) {
            if (!focusedOnEntry) molga::ui::DropUnfocusedPanelInput(*input);
            input->logicalViewport = *uiViewport;
            // tick은 ProcessFrame을 부르는 프레임에서만 진행한다: 일시정지·편집 구간의
            // 시간은 UI 시간이 아니다.
            input->uiTicks = tickClock_.AdvanceElapsedNanoseconds(
                Time::GetElapsedNanoseconds(), *textDiagnostics_);
            input->frameIndex = ++frameIndex_;
            uiSurfaceWindow_ = nativeWindow;
            uiSurfaceWorldGeneration_ = world_->Generation();
            molga::ui::UIFrameResult result = UISystem::Get().ProcessFrame(
                *world_, *input, textRenderer_->LayoutService(), *textDiagnostics_);
            renderSnapshot_ = std::move(result.renderSnapshot);
        }
    }

    // 포커스 없는 패널: 스크립트 입력은 풀어 두고 끝낸다(이전 동작).
    if (!focusedOnEntry) {
        Input::ReleaseAll();
        return;
    }
    if (!nativeMetrics.focused) {
        LoseInputFocus();
        renderSnapshot_.reset();  // 이탈로 바뀐 hover/눌림 색을 OnGUI가 새로 짓는다.
        return;
    }
    molga::WindowPointerState pointer;
    if (!molga::QueryWindowPointer(nativeWindow, pointer)) {
        LoseInputFocus();
        renderSnapshot_.reset();
        return;
    }

    // ── 스크립트 입력(UI가 아니다): 기존 스냅샷 경로는 그대로다 ───────────────
    const molga::GameViewPoint screen{
        pointer.x + viewport->Pos.x,
        pointer.y + viewport->Pos.y};
    const auto targetPixel = layout_.ScreenToGamePixel(
        screen, {imageScreenOrigin_.x, imageScreenOrigin_.y});
    const auto logicalPixel = targetPixel
        ? inputPresentation.FramebufferToLogical({targetPixel->x, targetPixel->y})
        : std::nullopt;
    const float mappedX = logicalPixel
        ? static_cast<float>(logicalPixel->x) : 0.0f;
    const float mappedY = logicalPixel
        ? static_cast<float>(logicalPixel->y) : 0.0f;

    InputSnapshot snapshot = Input::CaptureSnapshot(
        nativeWindow, mappedX, mappedY, logicalPixel.has_value());
    const molga::CameraOutputLayout cameraLayout =
        molga::CameraOutputLayout::Build(
            world_->Objects(), inputPresentation.logicalSize);
    if (logicalPixel) {
        const auto cameraPointer = cameraLayout.LogicalToTopmost(*logicalPixel);
        if (cameraPointer) {
            snapshot.cameraPointerValid = true;
            snapshot.pointerCameraObjectId = cameraPointer->cameraObjectId;
            snapshot.cameraPointerX = cameraPointer->cameraX;
            snapshot.cameraPointerY = cameraPointer->cameraY;
            snapshot.worldPointerX = cameraPointer->worldX;
            snapshot.worldPointerY = cameraPointer->worldY;
        }
    }
    Input::ApplySnapshot(snapshot);
}

void GameViewWindow::ResetPlayInput() {
    inputFocused_ = false;
    imageValid_ = false;
    Input::DiscardPendingScroll(inputSourceWindow_);
    Input::ReleaseAll();
    DepartUIPointer();
    ReleaseRetainedUISnapshots();
}
