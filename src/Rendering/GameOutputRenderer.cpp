#include "Rendering/GameOutputRenderer.h"

#include "Common/Log.h"
#include "ECS/Components/Camera.h"
#include "ECS/GameObject.h"
#include "Rendering/Camera2D.h"
#include "Rendering/RenderPass.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/WorldRenderTraversal.h"
#include "UI/UISystem.h"

#include <glad/glad.h>

namespace molga {

namespace {

class ScopedOutputGlState {
public:
    ScopedOutputGlState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox_);
        scissorEnabled_ = glIsEnabled(GL_SCISSOR_TEST);
        framebufferSrgbEnabled_ = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    }

    ~ScopedOutputGlState() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                          static_cast<GLuint>(drawFramebuffer_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
                          static_cast<GLuint>(readFramebuffer_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glScissor(scissorBox_[0], scissorBox_[1],
                  scissorBox_[2], scissorBox_[3]);
        if (scissorEnabled_) glEnable(GL_SCISSOR_TEST);
        else glDisable(GL_SCISSOR_TEST);
        if (framebufferSrgbEnabled_) glEnable(GL_FRAMEBUFFER_SRGB);
        else glDisable(GL_FRAMEBUFFER_SRGB);
    }

    GLuint DrawFramebuffer() const {
        return static_cast<GLuint>(drawFramebuffer_);
    }

private:
    GLint drawFramebuffer_ = 0;
    GLint readFramebuffer_ = 0;
    GLint viewport_[4] = {0, 0, 0, 0};
    GLint scissorBox_[4] = {0, 0, 0, 0};
    GLboolean scissorEnabled_ = GL_FALSE;
    GLboolean framebufferSrgbEnabled_ = GL_FALSE;
};

} // namespace

Camera* GameOutputRenderer::FindMainCamera(
    const std::vector<std::shared_ptr<GameObject>>& objects) {
    Camera* selected = nullptr;
    for (const auto& object : objects) {
        if (!object || !object->IsActive()) continue;
        Camera* candidate = object->GetComponent<Camera>();
        if (!candidate || !candidate->IsEnabled() || !candidate->IsMain()) continue;
        // Strictly greater preserves scene order when depths tie.
        if (!selected || candidate->GetDepth() > selected->GetDepth()) {
            selected = candidate;
        }
    }
    return selected;
}

GameOutputResult GameOutputRenderer::RenderLogical(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    PixelSize logicalSize,
    Renderer& renderer,
    Shader* spriteShader) {
    GameOutputResult result;
    if (!logicalSize.IsValid() || !spriteShader) return result;

    renderer.SetViewport(logicalSize.width, logicalSize.height);
    result.mainCamera = FindMainCamera(objects);
    Camera2D* camera = nullptr;
    if (result.mainCamera &&
        result.mainCamera->PrepareForViewport(logicalSize)) {
        camera = result.mainCamera->GetCamera2D();
        const Color clear = result.mainCamera->GetBackgroundColor();
        renderer.Clear(clear.r, clear.g, clear.b, clear.a);
    } else {
        renderer.Clear(0.06f, 0.06f, 0.075f, 1.0f);
    }
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (camera) {
        RenderQueue worldQueue;
        worldQueue.SetViewBounds(camera->GetViewBounds());
        CollectWorldRender(objects, worldQueue);
        {
            RenderPass pass(renderer, spriteShader, camera);
            RenderSystem2D::Get().Render(worldQueue, &renderer, camera);
        }
        result.rendered = true;
    }

    // Screen-space UI is a final presentation pass, not a child of the world
    // camera. It remains visible for camera-less menus and when a camera has
    // an invalid viewport configuration.
    RenderQueue uiQueue;
    UISystem::Get().CollectRender(
        objects,
        {static_cast<float>(logicalSize.width),
         static_cast<float>(logicalSize.height)},
        uiQueue);
    if (!uiQueue.GetCommands().empty()) {
        Camera2D uiCamera(static_cast<float>(logicalSize.width),
                          static_cast<float>(logicalSize.height));
        RenderPass pass(renderer, spriteShader, &uiCamera);
        RenderSystem2D::Get().Render(uiQueue, &renderer, &uiCamera);
    }
    return result;
}

GameOutputResult GameOutputRenderer::Render(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const GameOutputRequest& request,
    Renderer& renderer,
    Shader* spriteShader) {
    GameOutputResult result;
    result.presentation = OutputPresentationLayout::Calculate(
        request.scaleMode, request.logicalSize, request.targetSize);
    if (!result.presentation.IsValid() || !spriteShader) return result;

    if (!observedTarget_ || request.targetSize != lastObservedTarget_) {
        observedTarget_ = true;
        lastObservedTarget_ = request.targetSize;
        if (result.presentation.cropped) {
            Log::Warn(
                "GameOutput",
                "Framebuffer " + std::to_string(request.targetSize.width) + "x" +
                    std::to_string(request.targetSize.height) +
                    " is smaller than logical output " +
                    std::to_string(result.presentation.logicalSize.width) + "x" +
                    std::to_string(result.presentation.logicalSize.height) +
                    "; presenting at 1x with a centered crop.");
        }
    }

    ScopedOutputGlState savedState;
    if (request.scaleMode == GameOutputScaleMode::Native) {
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_FRAMEBUFFER_SRGB);
        result = RenderLogical(objects, result.presentation.logicalSize,
                               renderer, spriteShader);
        result.presentation = OutputPresentationLayout::Native(request.targetSize);
        result.presented = true;
        return result;
    }

    const PixelSize logicalSize = result.presentation.logicalSize;
    if (!logicalFramebuffer_.Resize(logicalSize.width, logicalSize.height)) {
        result.allocationFailed = true;
        return result;
    }

    GameOutputResult logicalResult;
    {
        ScopedFramebufferBinding binding(logicalFramebuffer_);
        logicalResult = RenderLogical(objects, logicalSize, renderer, spriteShader);
    }
    result.mainCamera = logicalResult.mainCamera;
    result.rendered = logicalResult.rendered;

    // Return to the caller's target, erase every bar/cropped edge to opaque
    // black, then copy the logical image with nearest filtering. The layout is
    // top-left based, while OpenGL destination coordinates are bottom-left.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, savedState.DrawFramebuffer());
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, request.targetSize.width, request.targetSize.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, logicalFramebuffer_.Id());
    const PixelRect& destination = result.presentation.contentRect;
    const int destinationBottom =
        request.targetSize.height - (destination.y + destination.height);
    const int destinationTop = request.targetSize.height - destination.y;
    glBlitFramebuffer(
        0, 0, logicalSize.width, logicalSize.height,
        destination.x, destinationBottom,
        destination.x + destination.width, destinationTop,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    result.presented = true;
    return result;
}

GameOutputResult GameOutputRenderer::Render(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    PixelSize outputSize,
    Renderer& renderer,
    Shader* spriteShader) {
    GameOutputRenderer rendererPath;
    return rendererPath.Render(
        objects,
        GameOutputRequest{outputSize, outputSize, GameOutputScaleMode::Native},
        renderer, spriteShader);
}

} // namespace molga
