#pragma once

#include "Rendering/Framebuffer.h"
#include "Rendering/CameraOutputLayout.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"
#include "Rendering/PostProcessPipeline.h"
#include "Rendering/LightingPipeline2D.h"

#include <memory>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <unordered_set>

class Camera;
class GameObject;
class Renderer;
class Shader;

namespace molga {

struct CameraOutputResult {
    unsigned int cameraObjectId = 0;
    std::uint64_t cameraInstanceId = 0;
    CameraOutputRole outputRole = CameraOutputRole::Disabled;
    int depth = 0;
    PixelRect viewport{};
    bool rendered = false;
    bool postProcessed = false;
    bool postProcessFallback = false;
    int postProcessPasses = 0;
    bool lightingApplied = false;
    bool lightingFallback = false;
    bool shadowFallback = false;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;
};

using CameraRenderResult = CameraOutputResult;

struct GameOutputResult {
    Camera* mainCamera = nullptr;
    bool rendered = false;
    bool presented = false;
    bool allocationFailed = false;
    bool postProcessed = false;
    bool postProcessFallback = false;
    int postProcessPasses = 0;
    bool lightingApplied = false;
    bool lightingFallback = false;
    bool shadowFallback = false;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;
    std::vector<CameraOutputResult> cameraResults;
    CameraOutputLayout cameraLayout{};
    OutputPresentationLayout presentation{};
};

struct GameOutputRequest {
    PixelSize targetSize{};
    PixelSize logicalSize{};
    GameOutputScaleMode scaleMode = GameOutputScaleMode::Native;
};

// The sole game-output path used by both the standalone player and Game View.
// The caller owns the current render target (backbuffer or FBO).
class GameOutputRenderer {
public:
    static Camera* FindMainCamera(
        const std::vector<std::shared_ptr<GameObject>>& objects);

    GameOutputResult Render(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        const GameOutputRequest& request,
        Renderer& renderer,
        Shader* spriteShader);

    // Compatibility entry point for the original direct Native path.
    static GameOutputResult Render(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        PixelSize outputSize,
        Renderer& renderer,
        Shader* spriteShader);

    PixelSize LogicalFramebufferSize() const {
        return {logicalFramebuffer_.Width(), logicalFramebuffer_.Height()};
    }

    std::size_t CachedPostProcessPipelineCount() const {
        return postProcessPipelines_.size();
    }
    std::size_t CachedLightingPipelineCount() const {
        return lightingPipelines_.size();
    }

private:
    GameOutputResult RenderLogical(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        PixelSize logicalSize,
        Renderer& renderer,
        Shader* spriteShader);

    Framebuffer logicalFramebuffer_;
    std::unordered_map<std::uint64_t, std::unique_ptr<PostProcessPipeline>>
        postProcessPipelines_;
    std::unordered_map<std::uint64_t, std::unique_ptr<LightingPipeline2D>>
        lightingPipelines_;
    PixelSize lastObservedTarget_{};
    bool observedTarget_ = false;
    std::unordered_set<std::string> postProcessWarnings_;
    std::unordered_set<std::string> lightingWarnings_;
};

} // namespace molga
