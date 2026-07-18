#pragma once

#include "Rendering/Framebuffer.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"

#include <memory>
#include <vector>

class Camera;
class GameObject;
class Renderer;
class Shader;

namespace molga {

struct GameOutputResult {
    Camera* mainCamera = nullptr;
    bool rendered = false;
    bool presented = false;
    bool allocationFailed = false;
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

private:
    static GameOutputResult RenderLogical(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        PixelSize logicalSize,
        Renderer& renderer,
        Shader* spriteShader);

    Framebuffer logicalFramebuffer_;
    PixelSize lastObservedTarget_{};
    bool observedTarget_ = false;
};

} // namespace molga
