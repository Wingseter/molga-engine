#include "Editor/Windows/GameViewWindow.h"

#include "Common/Log.h"
#include "Editor/Editor.h"
#include "Editor/EditorConstants.h"
#include "Editor/Project.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/Renderer.h"
#include "Systems/Input.h"
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
    ResetPlayInput();
}

void GameViewWindow::SetSceneResources(
    Renderer* renderer,
    Shader* spriteShader,
    std::vector<std::shared_ptr<GameObject>>* objects) {
    renderer_ = renderer;
    spriteShader_ = spriteShader;
    gameObjects_ = objects;
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
        return;
    }

    const bool visible = ImGui::Begin(title.c_str(), &isOpen,
                                      ImGuiWindowFlags_NoCollapse);
    if (!isOpen) LoseInputFocus();
    if (!visible) {
        LoseInputFocus();
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
        } else if (fbo_.Resize(requested.width, requested.height)) {
            activeOutput_ = requested;
            outputError_.clear();
        } else {
            outputError_ = "Resolution rejected by this GPU; showing last valid output";
        }
    }

    logicalOutput_ = RequestedLogicalSize();
    if (fbo_.IsValid() && renderer_ && spriteShader_ && gameObjects_) {
        ScopedFramebufferBinding binding(fbo_);
        const molga::GameOutputResult result = outputRenderer_.Render(
            *gameObjects_,
            {activeOutput_, logicalOutput_, RequestedScaleMode()},
            *renderer_, spriteShader_);
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
        if (platformMetrics.logicalWidth > 0 && platformMetrics.pixelWidth > 0) {
            framebufferScale.x = platformMetrics.scaleX;
        }
        if (platformMetrics.logicalHeight > 0 && platformMetrics.pixelHeight > 0) {
            framebufferScale.y = platformMetrics.scaleY;
        }
    }
    layout_ = molga::GameViewLayout::Calculate(
        activeOutput_, {available.x, available.y}, framebufferScale,
        preferences_.gameView.displayMode);

    const ImVec2 contentStart = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(contentStart.x + layout_.imageRect.x,
                               contentStart.y + layout_.imageRect.y));
    imageScreenOrigin_ = ImGui::GetCursorScreenPos();
    imageValid_ = fbo_.IsValid() && layout_.imageRect.width > 0.0f &&
                  layout_.imageRect.height > 0.0f;
    platformViewportId_ = platformViewport ? platformViewport->ID : 0;
    if (platformWindow != inputSourceWindow_) {
        Input::DiscardPendingScroll(inputSourceWindow_);
        inputSourceWindow_ = platformWindow;
        Input::DiscardPendingScroll(inputSourceWindow_);
    }
    if (!inputFocused_) Input::DiscardPendingScroll(inputSourceWindow_);

    if (imageValid_) {
        ImGui::Image(static_cast<ImTextureID>(fbo_.ColorTexture()),
                     ImVec2(layout_.imageRect.width, layout_.imageRect.height),
                     ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            Input::DiscardPendingScroll(inputSourceWindow_);
            inputFocused_ = true;
            rawMouseWasDown_ = false;
            ImGui::SetWindowFocus();
        }
    } else {
        ImGui::Dummy(ImVec2(std::max(available.x, 1.0f),
                           std::max(available.y, 1.0f)));
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

void GameViewWindow::LoseInputFocus() {
    if (!inputFocused_ && !rawMouseWasDown_) return;
    inputFocused_ = false;
    rawMouseWasDown_ = false;
    Input::DiscardPendingScroll(inputSourceWindow_);
    Input::ReleaseAll();
    UISystem::Get().ResetPointerCapture();
}

void GameViewWindow::ProcessPlayInput() {
    if (!isOpen || !inputFocused_ || !imageValid_ || platformViewportId_ == 0 ||
        !ImGui::GetCurrentContext() || !gameObjects_ || !activeOutput_.IsValid()) {
        if (inputFocused_) LoseInputFocus();
        else {
            Input::ReleaseAll();
            UISystem::Get().ResetPointerCapture();
            rawMouseWasDown_ = false;
        }
        return;
    }

    ImGuiViewport* viewport = ImGui::FindViewportByID(platformViewportId_);
    const molga::WindowId nativeWindow = ViewportWindowId(viewport);
    molga::WindowMetrics nativeMetrics;
    if (!viewport || nativeWindow == 0 ||
        !molga::QueryWindowMetrics(nativeWindow, nativeMetrics) ||
        !nativeMetrics.focused) {
        LoseInputFocus();
        return;
    }
    if (nativeWindow != inputSourceWindow_) {
        Input::DiscardPendingScroll(inputSourceWindow_);
        inputSourceWindow_ = nativeWindow;
        Input::DiscardPendingScroll(inputSourceWindow_);
    }
    molga::WindowPointerState pointer;
    if (!molga::QueryWindowPointer(nativeWindow, pointer)) {
        LoseInputFocus();
        return;
    }
    const molga::GameViewPoint screen{
        pointer.x + viewport->Pos.x,
        pointer.y + viewport->Pos.y};
    const auto targetPixel = layout_.ScreenToGamePixel(
        screen, {imageScreenOrigin_.x, imageScreenOrigin_.y});
    const molga::OutputPresentationLayout inputPresentation =
        molga::OutputPresentationLayout::Calculate(
            RequestedScaleMode(), RequestedLogicalSize(), activeOutput_);
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
            *gameObjects_, inputPresentation.logicalSize);
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

    const bool rawDown = pointer.leftDown;
    const bool pointerValid = logicalPixel.has_value();
    if (!pointerValid) UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        *gameObjects_,
        {static_cast<float>(inputPresentation.logicalSize.width),
         static_cast<float>(inputPresentation.logicalSize.height)},
        {{mappedX, mappedY}, pointerValid && rawDown,
         pointerValid && rawDown && !rawMouseWasDown_,
         pointerValid && !rawDown && rawMouseWasDown_, pointerValid});
    rawMouseWasDown_ = rawDown;
}

void GameViewWindow::ResetPlayInput() {
    inputFocused_ = false;
    imageValid_ = false;
    rawMouseWasDown_ = false;
    Input::DiscardPendingScroll(inputSourceWindow_);
    Input::ReleaseAll();
    UISystem::Get().ResetPointerCapture();
}
