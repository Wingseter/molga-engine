#include "Rendering/GameOutputRenderer.h"

#include "Common/Log.h"
#include "ECS/Components/Camera.h"
#include "ECS/GameObject.h"
#include "Rendering/Camera2D.h"
#include "Rendering/RenderPass.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/PostProcessProfileResolver.h"
#include "Rendering/LightingFrame2D.h"
#include "Rendering/LightingPipeline2D.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/WorldRenderTraversal.h"
#include "UI/UISystem.h"

#include <glad/glad.h>

#include <array>
#include <algorithm>
#include <string>
#include <unordered_set>

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
        blendEnabled_ = glIsEnabled(GL_BLEND);
        depthEnabled_ = glIsEnabled(GL_DEPTH_TEST);
        stencilEnabled_ = glIsEnabled(GL_STENCIL_TEST);
        cullEnabled_ = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb_);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha_);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb_);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha_);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_.data());
        glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilMaskFront_);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &stencilMaskBack_);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor_.data());
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth_);
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clearStencil_);
    }

    ~ScopedOutputGlState() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                          static_cast<GLuint>(drawFramebuffer_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
                          static_cast<GLuint>(readFramebuffer_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glScissor(scissorBox_[0], scissorBox_[1],
                  scissorBox_[2], scissorBox_[3]);
        SetEnabled(GL_SCISSOR_TEST, scissorEnabled_);
        SetEnabled(GL_FRAMEBUFFER_SRGB, framebufferSrgbEnabled_);
        SetEnabled(GL_BLEND, blendEnabled_);
        SetEnabled(GL_DEPTH_TEST, depthEnabled_);
        SetEnabled(GL_STENCIL_TEST, stencilEnabled_);
        SetEnabled(GL_CULL_FACE, cullEnabled_);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb_),
                            static_cast<GLenum>(blendDstRgb_),
                            static_cast<GLenum>(blendSrcAlpha_),
                            static_cast<GLenum>(blendDstAlpha_));
        glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb_),
                                static_cast<GLenum>(blendEquationAlpha_));
        glDepthMask(depthMask_);
        glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(stencilMaskFront_));
        glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(stencilMaskBack_));
        glClearColor(clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]);
        glClearDepth(clearDepth_);
        glClearStencil(clearStencil_);
    }

    GLuint DrawFramebuffer() const {
        return static_cast<GLuint>(drawFramebuffer_);
    }

private:
    static void SetEnabled(GLenum capability, GLboolean enabled) {
        if (enabled) glEnable(capability);
        else glDisable(capability);
    }

    GLint drawFramebuffer_ = 0;
    GLint readFramebuffer_ = 0;
    GLint viewport_[4] = {0, 0, 0, 0};
    GLint scissorBox_[4] = {0, 0, 0, 0};
    GLboolean scissorEnabled_ = GL_FALSE;
    GLboolean framebufferSrgbEnabled_ = GL_FALSE;
    GLboolean blendEnabled_ = GL_FALSE;
    GLboolean depthEnabled_ = GL_FALSE;
    GLboolean stencilEnabled_ = GL_FALSE;
    GLboolean cullEnabled_ = GL_FALSE;
    GLint blendSrcRgb_ = GL_ONE;
    GLint blendDstRgb_ = GL_ZERO;
    GLint blendSrcAlpha_ = GL_ONE;
    GLint blendDstAlpha_ = GL_ZERO;
    GLint blendEquationRgb_ = GL_FUNC_ADD;
    GLint blendEquationAlpha_ = GL_FUNC_ADD;
    GLboolean depthMask_ = GL_TRUE;
    std::array<GLboolean, 4> colorMask_{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint stencilMaskFront_ = -1;
    GLint stencilMaskBack_ = -1;
    std::array<GLfloat, 4> clearColor_{};
    GLdouble clearDepth_ = 1.0;
    GLint clearStencil_ = 0;
};

} // namespace

namespace {

void SetWritableClearState(bool srgbTarget) {
    if (srgbTarget) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMaskSeparate(GL_FRONT_AND_BACK, ~GLuint{0});
    glClearDepth(1.0);
    glClearStencil(0);
}

void SetTopLeftTargetRect(PixelSize targetSize, const PixelRect& rect) {
    const int bottom = targetSize.height - (rect.y + rect.height);
    glViewport(rect.x, bottom, rect.width, rect.height);
    glScissor(rect.x, bottom, rect.width, rect.height);
}

void ClearWholeTarget(PixelSize size, const Color& color, bool srgbTarget) {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, size.width, size.height);
    SetWritableClearState(srgbTarget);
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

struct PreparedWorldLayer2D {
    RenderQueue queue;
    Camera2D* camera = nullptr;
    Color clearColor = Color::Black();
    LightingPipeline2D* lightingPipeline = nullptr;
    bool lightingApplied = false;
    bool lightingFallback = false;
    bool shadowFallback = false;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;
};

bool PrepareWorldLayer(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const CameraOutputEntry& entry, PreparedWorldLayer2D& prepared) {
    if (!entry.camera || !entry.renderable ||
        !entry.camera->PrepareForViewport(
            {entry.viewport.width, entry.viewport.height})) {
        return false;
    }

    prepared.camera = entry.camera->GetCamera2D();
    if (!prepared.camera) return false;
    prepared.clearColor = entry.camera->GetBackgroundColor();
    prepared.queue.SetViewBounds(prepared.camera->GetViewBounds());
    CollectWorldRender(objects, prepared.queue, entry.camera->GetCullingMask());
    return true;
}

bool DrawPreparedWorldLayer(
    PreparedWorldLayer2D& prepared, PixelSize targetSize,
    const PixelRect& targetRect, Renderer& renderer, Shader* spriteShader,
    bool srgbTarget) {
    if (!prepared.camera) return false;
    SetTopLeftTargetRect(targetSize, targetRect);
    glEnable(GL_SCISSOR_TEST);
    SetWritableClearState(srgbTarget);
    const Color clear = prepared.clearColor;
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    LightingRenderContext2D lightingContext;
    const LightingRenderContext2D* lightingContextPointer = nullptr;
    if (prepared.lightingApplied && prepared.lightingPipeline) {
        lightingContext = prepared.lightingPipeline->ContextForTarget(
            targetSize, targetRect);
        if (lightingContext.IsUsable()) {
            lightingContextPointer = &lightingContext;
            ++prepared.lightingPasses;
        }
    }
    {
        RenderPass pass(renderer, spriteShader, prepared.camera);
        RenderSystem2D::Get().Render(
            prepared.queue, &renderer, prepared.camera, lightingContextPointer);
    }
    return true;
}

void RenderUiLayer(const std::vector<std::shared_ptr<GameObject>>& objects,
                   molga::PixelSize logicalSize, Renderer& renderer,
                   Shader* spriteShader) {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, logicalSize.width, logicalSize.height);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    molga::RenderQueue uiQueue;
    UISystem::Get().CollectRender(
        objects,
        {static_cast<float>(logicalSize.width),
         static_cast<float>(logicalSize.height)},
        uiQueue);
    if (uiQueue.GetCommands().empty()) return;
    Camera2D uiCamera(static_cast<float>(logicalSize.width),
                      static_cast<float>(logicalSize.height));
    molga::RenderPass pass(renderer, spriteShader, &uiCamera);
    molga::RenderSystem2D::Get().Render(uiQueue, &renderer, &uiCamera);
}

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

    result.cameraLayout = CameraOutputLayout::Build(objects, logicalSize);
    result.mainCamera = result.cameraLayout.PrimaryCamera();

    GLint destinationFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destinationFramebuffer);
    ClearWholeTarget(logicalSize, Color::Black(), true);
    if (!result.cameraLayout.HasRenderableCamera()) {
        // Preserve the original camera-less menu/error background while empty
        // regions around authored camera viewports remain opaque black. A
        // selected Primary whose viewport collapses below one logical pixel is
        // also camera-less for this frame.
        ClearWholeTarget(logicalSize, Color(0.06f, 0.06f, 0.075f, 1.0f), true);
    }

    std::unordered_set<std::uint64_t> selectedInstances;
    for (const CameraOutputEntry& entry : result.cameraLayout.Entries()) {
        selectedInstances.insert(entry.cameraInstanceId);
    }
    for (auto iterator = postProcessPipelines_.begin();
         iterator != postProcessPipelines_.end();) {
        if (selectedInstances.find(iterator->first) == selectedInstances.end()) {
            iterator = postProcessPipelines_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (auto iterator = lightingPipelines_.begin();
         iterator != lightingPipelines_.end();) {
        if (selectedInstances.find(iterator->first) == selectedInstances.end()) {
            iterator = lightingPipelines_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    std::unordered_set<std::uint64_t> usedPostProcessPipelines;
    std::unordered_set<std::uint64_t> usedLightingPipelines;
    const auto warnPostProcessFallback = [&](const CameraOutputEntry& entry,
                                             const std::string& error) {
        const std::string reason = error.empty()
            ? "post-process pipeline failed" : error;
        const std::string warningKey =
            std::to_string(entry.viewport.width) + "x" +
            std::to_string(entry.viewport.height) + ":" + reason;
        if (postProcessWarnings_.insert(warningKey).second) {
            Log::Warn("PostProcess", reason +
                "; rendering this camera without post-processing.");
        }
    };
    const auto warnLighting = [&](const CameraOutputEntry& entry,
                                  const std::string& key,
                                  const std::string& message) {
        const std::string warningKey =
            std::to_string(entry.cameraInstanceId) + ":" + key;
        if (lightingWarnings_.insert(warningKey).second) {
            Log::Warn("Lighting2D", message);
        }
    };

    for (const CameraOutputEntry& entry : result.cameraLayout.Entries()) {
        CameraOutputResult cameraResult;
        cameraResult.cameraObjectId = entry.cameraObjectId;
        cameraResult.cameraInstanceId = entry.cameraInstanceId;
        cameraResult.outputRole = entry.role;
        cameraResult.depth = entry.depth;
        cameraResult.viewport = entry.viewport;
        if (!entry.renderable || !entry.camera) {
            result.cameraResults.push_back(cameraResult);
            continue;
        }

        PreparedWorldLayer2D preparedWorld;
        if (!PrepareWorldLayer(objects, entry, preparedWorld)) {
            result.cameraResults.push_back(cameraResult);
            continue;
        }

        if (entry.camera->IsLightingEnabled() &&
            preparedWorld.queue.HasLitReceivers()) {
            Shader* litShader = ShaderManager::Get().Get("batch_lit");
            if (!litShader || !litShader->IsValid()) {
                preparedWorld.lightingFallback = true;
                preparedWorld.queue.ForceUnlit();
                warnLighting(
                    entry, "shader",
                    "Lit shader is unavailable; rendering this camera world Unlit.");
            } else {
                const PixelSize cameraSize{
                    entry.viewport.width, entry.viewport.height};
                const LightingFrame2D frame = LightingFrame2D::Build(
                    objects, *entry.camera, cameraSize);
                preparedWorld.selectedLightCount =
                    static_cast<int>(frame.lights.size());
                if (frame.discardedLightCount > 0) {
                    warnLighting(
                        entry, "light-budget",
                        "PointLight2D camera budget exceeded; later lights were "
                        "deterministically excluded.");
                }
                if (frame.discardedShadowLightCount > 0) {
                    warnLighting(
                        entry, "shadow-light-budget",
                        "Shadow-light camera budget exceeded; extra selected "
                        "lights remain unshadowed.");
                }
                const bool occluderBudgetExceeded = std::any_of(
                    frame.shadowLayers.begin(), frame.shadowLayers.end(),
                    [](const ShadowMaskLayerFrame2D& layer) {
                        return layer.discardedOccluderCount > 0;
                    });
                if (occluderBudgetExceeded) {
                    warnLighting(
                        entry, "occluder-budget",
                        "Shadow occluder budget exceeded; later occluders were "
                        "deterministically excluded.");
                }

                usedLightingPipelines.insert(entry.cameraInstanceId);
                auto& ownedLighting =
                    lightingPipelines_[entry.cameraInstanceId];
                if (!ownedLighting) {
                    ownedLighting = std::make_unique<LightingPipeline2D>();
                }
                LightingPipelinePrepareResult2D lightingResult;
                if (!ownedLighting->Prepare(
                        frame, *preparedWorld.camera, lightingResult) ||
                    !lightingResult.ready) {
                    preparedWorld.lightingFallback = true;
                    preparedWorld.queue.ForceUnlit();
                    warnLighting(
                        entry, "context:" + lightingResult.error,
                        (lightingResult.error.empty()
                            ? std::string("Lighting context failed")
                            : lightingResult.error) +
                            "; rendering this camera world Unlit.");
                } else {
                    preparedWorld.lightingPipeline = ownedLighting.get();
                    preparedWorld.lightingApplied = true;
                    preparedWorld.shadowFallback =
                        lightingResult.shadowFallback;
                    preparedWorld.shadowedLightCount =
                        lightingResult.shadowedLightCount;
                    preparedWorld.shadowCasterDrawCount =
                        lightingResult.shadowCasterDrawCount;
                    preparedWorld.shadowPasses =
                        lightingResult.shadowPasses;
                    if (lightingResult.shadowFallback) {
                        warnLighting(
                            entry, "shadow:" + lightingResult.error,
                            (lightingResult.error.empty()
                                ? std::string("Shadow mask failed")
                                : lightingResult.error) +
                                "; affected lights remain unshadowed.");
                    }
                }
            }
        }

        bool handled = false;
        if (entry.camera->IsPostProcessEnabled() &&
            !entry.camera->GetPostProcessProfileGuid().empty()) {
            const PostProcessProfileResolveResult resolved =
                PostProcessProfileResolver::Get().Resolve(
                    entry.camera->GetPostProcessProfileGuid());
            if (!resolved) {
                cameraResult.postProcessFallback = true;
                warnPostProcessFallback(entry, resolved.error);
            } else if (resolved.profile->HasActiveEffects()) {
                usedPostProcessPipelines.insert(entry.cameraInstanceId);
                auto& ownedPipeline = postProcessPipelines_[entry.cameraInstanceId];
                if (!ownedPipeline) {
                    ownedPipeline = std::make_unique<PostProcessPipeline>();
                }
                PostProcessPipeline& pipeline = *ownedPipeline;
                const PixelSize cameraSize{entry.viewport.width,
                                           entry.viewport.height};
                std::string error;
                if (pipeline.Prepare(cameraSize, *resolved.profile, &error)) {
                    bool sceneRendered = false;
                    {
                        ScopedFramebufferBinding sceneBinding(pipeline.SceneTarget());
                        sceneRendered = DrawPreparedWorldLayer(
                            preparedWorld, cameraSize,
                            {0, 0, cameraSize.width, cameraSize.height},
                            renderer, spriteShader, false);
                    }
                    if (sceneRendered) {
                        const PostProcessExecutionResult execution = pipeline.Execute(
                            *resolved.profile,
                            static_cast<GLuint>(destinationFramebuffer),
                            logicalSize, entry.viewport);
                        if (execution.success) {
                            cameraResult.rendered = true;
                            cameraResult.postProcessed = execution.postProcessed;
                            cameraResult.postProcessPasses = execution.passes;
                            handled = true;
                        } else {
                            error = execution.error;
                        }
                    } else {
                        error = "camera could not prepare its post-process scene pass";
                    }
                }
                if (!handled) {
                    cameraResult.postProcessFallback = true;
                    warnPostProcessFallback(entry, error);
                }
            }
        }

        if (!handled) {
            // This also handles neutral profiles. They are an intentional
            // direct-path bypass, not a failure.
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                              static_cast<GLuint>(destinationFramebuffer));
            cameraResult.rendered = DrawPreparedWorldLayer(
                preparedWorld, logicalSize, entry.viewport, renderer,
                spriteShader, true);
        }

        cameraResult.lightingApplied = preparedWorld.lightingApplied;
        cameraResult.lightingFallback = preparedWorld.lightingFallback;
        cameraResult.shadowFallback = preparedWorld.shadowFallback;
        cameraResult.selectedLightCount = preparedWorld.selectedLightCount;
        cameraResult.shadowedLightCount = preparedWorld.shadowedLightCount;
        cameraResult.shadowCasterDrawCount =
            preparedWorld.shadowCasterDrawCount;
        cameraResult.lightingPasses = preparedWorld.lightingPasses;
        cameraResult.shadowPasses = preparedWorld.shadowPasses;
        if (cameraResult.rendered) {
            result.rendered = true;
            ++renderer.Stats().outputCameraPasses;
        }
        result.postProcessed = result.postProcessed || cameraResult.postProcessed;
        result.postProcessFallback =
            result.postProcessFallback || cameraResult.postProcessFallback;
        result.postProcessPasses += cameraResult.postProcessPasses;
        result.lightingApplied =
            result.lightingApplied || cameraResult.lightingApplied;
        result.lightingFallback =
            result.lightingFallback || cameraResult.lightingFallback;
        result.shadowFallback =
            result.shadowFallback || cameraResult.shadowFallback;
        result.selectedLightCount += cameraResult.selectedLightCount;
        result.shadowedLightCount += cameraResult.shadowedLightCount;
        result.shadowCasterDrawCount += cameraResult.shadowCasterDrawCount;
        result.lightingPasses += cameraResult.lightingPasses;
        result.shadowPasses += cameraResult.shadowPasses;
        renderer.Stats().postProcessPasses += cameraResult.postProcessPasses;
        renderer.Stats().lightingPasses += cameraResult.lightingPasses;
        renderer.Stats().shadowPasses += cameraResult.shadowPasses;
        renderer.Stats().selectedLightCount += cameraResult.selectedLightCount;
        renderer.Stats().shadowedLightCount += cameraResult.shadowedLightCount;
        renderer.Stats().shadowCasterDrawCount +=
            cameraResult.shadowCasterDrawCount;
        result.cameraResults.push_back(cameraResult);
    }

    for (auto iterator = postProcessPipelines_.begin();
         iterator != postProcessPipelines_.end();) {
        if (usedPostProcessPipelines.find(iterator->first) ==
            usedPostProcessPipelines.end()) {
            iterator = postProcessPipelines_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (auto iterator = lightingPipelines_.begin();
         iterator != lightingPipelines_.end();) {
        if (usedLightingPipelines.find(iterator->first) ==
            usedLightingPipelines.end()) {
            iterator = lightingPipelines_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    // UI is intentionally outside the HDR chain.
    RenderUiLayer(objects, logicalSize, renderer, spriteShader);
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
    const OutputPresentationLayout presentation = result.presentation;
    result = std::move(logicalResult);
    result.presentation = presentation;

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
