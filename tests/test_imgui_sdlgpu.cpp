#include "Core/Bootstrap.h"
#include "Editor/ImGuiLayer.h"
#include "Editor/ImGuiTextureBridge.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/Renderer.h"
#include "doctest.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <string>

namespace {

struct ImGuiShutdownGuard {
    ~ImGuiShutdownGuard() { ImGuiLayer::Shutdown(); }
};

bool DrawEditorSmokeFrame(EngineHost& host, Renderer& renderer,
                          const std::array<molga::RenderTarget*, 3>& previews,
                          bool drawDetached, std::string& error) {
    host.PollEvents();
    molga::BeginFrameResult acquired = host.BeginFrame();
    if (acquired.status != molga::FrameAcquireStatus::Acquired) {
        error = acquired.error.empty() ? "swapchain unavailable"
                                       : acquired.error;
        return false;
    }
    if (!renderer.BeginFrame(std::move(acquired.frame), &error)) return false;

    static constexpr std::array<molga::Color4f, 3> colors{{
        {0.8f, 0.1f, 0.1f, 1.0f},
        {0.1f, 0.8f, 0.1f, 1.0f},
        {0.1f, 0.1f, 0.8f, 1.0f},
    }};
    for (std::size_t index = 0; index < previews.size(); ++index) {
        if (!renderer.BeginTarget(*previews[index], colors[index],
                                  molga::LoadAction::Clear, &error) ||
            !renderer.EndTarget(&error)) {
            return false;
        }
    }

    renderer.Clear(0.08f, 0.08f, 0.10f, 1.0f);
    ImGuiLayer::BeginFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigViewportsNoAutoMerge = true;
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({300.0f, 190.0f}, ImGuiCond_Always);
    ImGui::Begin("Editor Preview Surfaces", nullptr,
                 ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);
    static constexpr std::array<const char*, 3> labels{{
        "Game View", "Scene View", "Animation Preview"}};
    for (std::size_t index = 0; index < previews.size(); ++index) {
        ImGui::TextUnformatted(labels[index]);
        const ImTextureID texture = ImGuiTextureBridge::From(
            previews[index]->ColorView().texture);
        if (texture == ImTextureID_Invalid) {
            ImGui::End();
            ImGui::EndFrame();
            error = "preview texture bridge returned an invalid texture";
            return false;
        }
        ImGui::SameLine();
        ImGui::Image(texture, {48.0f, 32.0f});
    }
    ImGui::End();

    if (drawDetached) {
        ImGui::SetNextWindowPos(
            {mainViewport->Pos.x + mainViewport->Size.x + 32.0f,
             mainViewport->Pos.y + 24.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({180.0f, 120.0f}, ImGuiCond_Always);
        ImGui::Begin("Detached Scene View", nullptr,
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("SDL_GPU detached viewport");
        ImGui::Image(ImGuiTextureBridge::From(
                         previews[1]->ColorView().texture),
                     {96.0f, 64.0f});
        ImGui::End();
    }

    return ImGuiLayer::EndFrame(renderer, &error);
}

} // namespace

TEST_CASE("Dear ImGui SDL_GPU smoke covers preview textures and viewports") {
    WindowConfig config;
    config.title = "Molga Dear ImGui SDL_GPU smoke";
    config.width = 320;
    config.height = 240;
    config.visible = true;
    config.resizable = true;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderTarget gameView;
    molga::RenderTarget sceneView;
    molga::RenderTarget animationPreview;
    REQUIRE(gameView.Init(96, 64, &error));
    REQUIRE(sceneView.Init(96, 64, &error));
    REQUIRE(animationPreview.Init(96, 64, &error));
    const std::array<molga::RenderTarget*, 3> previews{
        &gameView, &sceneView, &animationPreview};

    ImGuiLayer::Init(*host);
    ImGuiShutdownGuard imguiGuard;
    REQUIRE(ImGui::GetCurrentContext() != nullptr);
    ImGuiIO& io = ImGui::GetIO();
    CHECK((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0);
    CHECK((io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0);
    CHECK((io.BackendFlags & ImGuiBackendFlags_RendererHasViewports) != 0);
    CHECK((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);
    for (molga::RenderTarget* preview : previews) {
        CHECK(ImGuiTextureBridge::From(preview->ColorView().texture) !=
              ImTextureID_Invalid);
    }

    REQUIRE(DrawEditorSmokeFrame(*host, renderer, previews, true, error));
    REQUIRE(DrawEditorSmokeFrame(*host, renderer, previews, true, error));
    REQUIRE(ImGui::GetPlatformIO().Viewports.Size >= 2);
    bool hasDetachedPlatformWindow = false;
    for (int index = 1; index < ImGui::GetPlatformIO().Viewports.Size;
         ++index) {
        const ImGuiViewport* viewport =
            ImGui::GetPlatformIO().Viewports[index];
        hasDetachedPlatformWindow |= viewport->PlatformHandle != nullptr &&
                                     viewport->RendererUserData != nullptr;
    }
    CHECK(hasDetachedPlatformWindow);

    REQUIRE(io.Fonts->TexData != nullptr);
    CHECK(io.Fonts->TexData->Status == ImTextureStatus_OK);
    CHECK(io.Fonts->TexData->GetTexID() != ImTextureID_Invalid);
    const int fontsBeforeUpdate = io.Fonts->Fonts.Size;
    REQUIRE(io.Fonts->AddFontDefault() != nullptr);
    CHECK(io.Fonts->Fonts.Size == fontsBeforeUpdate + 1);

    SDL_Window* mainWindow = SDL_GetWindowFromID(host->WindowId());
    REQUIRE(mainWindow != nullptr);
    REQUIRE(SDL_SetWindowSize(mainWindow, 400, 300));
    REQUIRE(DrawEditorSmokeFrame(*host, renderer, previews, true, error));
    const molga::WindowMetrics resized = host->Metrics();
    CHECK(resized.logicalWidth == 400);
    CHECK(resized.logicalHeight == 300);
    CHECK(resized.pixelWidth >= resized.logicalWidth);
    CHECK(resized.pixelHeight >= resized.logicalHeight);
    CHECK(resized.scaleX >= 1.0f);
    CHECK(resized.scaleY >= 1.0f);
    CHECK(io.DisplayFramebufferScale.x >= 1.0f);
    CHECK(io.DisplayFramebufferScale.y >= 1.0f);
    REQUIRE(io.Fonts->TexData != nullptr);
    CHECK(io.Fonts->TexData->Status == ImTextureStatus_OK);
    CHECK(io.Fonts->TexData->GetTexID() != ImTextureID_Invalid);

    REQUIRE(DrawEditorSmokeFrame(*host, renderer, previews, false, error));
    REQUIRE(DrawEditorSmokeFrame(*host, renderer, previews, false, error));
    REQUIRE(DrawEditorSmokeFrame(*host, renderer, previews, false, error));
    CHECK(ImGui::GetPlatformIO().Viewports.Size == 1);
    const molga::FrameTelemetry& telemetry = renderer.LastFrameTelemetry();
    CHECK(telemetry.renderPasses >= 4U);
    const ImDrawData* drawData = ImGui::GetDrawData();
    REQUIRE(drawData != nullptr);
    CHECK(drawData->TotalVtxCount > 0);
    CHECK(drawData->TotalIdxCount > 0);

    host->RequestClose();
    CHECK(host->ShouldClose());
}
