#include "Rendering/GameOutputRenderer.h"

#include "Common/Log.h"
#include "ECS/Components/Camera.h"
#include "ECS/GameObject.h"
#include "Rendering/Camera2D.h"
#include "Rendering/LightingFrame2D.h"
#include "Rendering/LightingPipeline2D.h"
#include "Rendering/PostProcessProfileResolver.h"
#include "Rendering/RenderPass.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/WorldRenderTraversal.h"
#include "UI/UISystem.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace molga {
namespace {

constexpr RenderTargetSpecification kCameraTargetSpec{
    RenderTargetColorFormat::SRGBA8, true, TextureFilter::Nearest};

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

bool DrawPreparedWorldLayer(PreparedWorldLayer2D& prepared,
                            RenderTarget& target, Renderer& renderer,
                            Shader* spriteShader, std::string& error) {
    if (!prepared.camera || !target.IsValid()) return false;
    const Color clear = prepared.clearColor;
    if (!renderer.BeginTarget(
            target, {clear.r, clear.g, clear.b, clear.a}, LoadAction::Clear,
            &error)) {
        return false;
    }
    LightingRenderContext2D lightingContext;
    const LightingRenderContext2D* lightingPointer = nullptr;
    if (prepared.lightingApplied && prepared.lightingPipeline) {
        lightingContext = prepared.lightingPipeline->ContextForTarget(
            {target.Width(), target.Height()},
            {0, 0, target.Width(), target.Height()});
        if (lightingContext.IsUsable()) {
            lightingPointer = &lightingContext;
            ++prepared.lightingPasses;
        }
    }
    {
        RenderPass pass(renderer, spriteShader, prepared.camera);
        RenderSystem2D::Get().Render(prepared.queue, &renderer,
                                     prepared.camera, lightingPointer);
    }
    return renderer.EndTarget(&error);
}

bool RenderUiLayer(const std::vector<std::shared_ptr<GameObject>>& objects,
                   PixelSize logicalSize, RenderTarget& target,
                   Renderer& renderer, Shader* spriteShader,
                   std::string& error) {
    RenderQueue queue;
    UISystem::Get().CollectRender(
        objects,
        {static_cast<float>(logicalSize.width),
         static_cast<float>(logicalSize.height)},
        queue);
    if (queue.GetCommands().empty()) return true;
    if (!renderer.BeginTarget(target, {0, 0, 0, 1}, LoadAction::Load, &error)) {
        return false;
    }
    Camera2D uiCamera(static_cast<float>(logicalSize.width),
                      static_cast<float>(logicalSize.height));
    {
        RenderPass pass(renderer, spriteShader, &uiCamera);
        RenderSystem2D::Get().Render(queue, &renderer, &uiCamera);
    }
    return renderer.EndTarget(&error);
}

ColorAttachmentDescriptor TargetAttachment(RenderTarget* target) {
    ColorAttachmentDescriptor descriptor;
    descriptor.swapchain = target == nullptr;
    if (target) descriptor.view = target->ColorView();
    descriptor.loadAction = LoadAction::Clear;
    descriptor.storeAction = StoreAction::Store;
    descriptor.clearColor = {0, 0, 0, 1};
    return descriptor;
}

bool ClipPresentationBlit(const OutputPresentationLayout& presentation,
                          PixelRectU32& source, PixelRectU32& destination) {
    const PixelRect& content = presentation.contentRect;
    const PixelSize framebuffer = presentation.framebufferSize;
    if (!presentation.IsValid() || content.width <= 0 || content.height <= 0 ||
        framebuffer.width <= 0 || framebuffer.height <= 0) {
        return false;
    }

    const int left = std::max(0, content.x);
    const int top = std::max(0, content.y);
    const int right = std::min(framebuffer.width, content.x + content.width);
    const int bottom = std::min(framebuffer.height, content.y + content.height);
    if (right <= left || bottom <= top) return false;

    // IntegerFit can only crop at 1x: fitScale is zero and is clamped to one.
    // Keeping the calculation in terms of the recorded scale also makes the
    // top-left source selection explicit and preserves exact texel mapping.
    const int scale = presentation.scale;
    const int sourceX = (left - content.x) / scale;
    const int sourceY = (top - content.y) / scale;
    const int sourceWidth = (right - left) / scale;
    const int sourceHeight = (bottom - top) / scale;
    if (sourceX < 0 || sourceY < 0 || sourceWidth <= 0 || sourceHeight <= 0 ||
        sourceX + sourceWidth > presentation.logicalSize.width ||
        sourceY + sourceHeight > presentation.logicalSize.height) {
        return false;
    }

    source = {static_cast<std::uint32_t>(sourceX),
              static_cast<std::uint32_t>(sourceY),
              static_cast<std::uint32_t>(sourceWidth),
              static_cast<std::uint32_t>(sourceHeight)};
    destination = {static_cast<std::uint32_t>(left),
                   static_cast<std::uint32_t>(top),
                   static_cast<std::uint32_t>(right - left),
                   static_cast<std::uint32_t>(bottom - top)};
    return true;
}

} // namespace

Camera* GameOutputRenderer::FindMainCamera(
    const std::vector<std::shared_ptr<GameObject>>& objects) {
    Camera* selected = nullptr;
    for (const auto& object : objects) {
        if (!object || !object->IsActive()) continue;
        Camera* candidate = object->GetComponent<Camera>();
        if (!candidate || !candidate->IsEnabled() || !candidate->IsMain()) continue;
        if (!selected || candidate->GetDepth() > selected->GetDepth()) {
            selected = candidate;
        }
    }
    return selected;
}

GameOutputResult GameOutputRenderer::RenderLogical(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    PixelSize logicalSize, Renderer& renderer, Shader* spriteShader) {
    GameOutputResult result;
    if (!logicalSize.IsValid() || !spriteShader || !renderer.HasFrame()) {
        return result;
    }
    result.cameraLayout = CameraOutputLayout::Build(objects, logicalSize);
    result.mainCamera = result.cameraLayout.PrimaryCamera();

    std::string error;
    const Color4f base = result.cameraLayout.HasRenderableCamera()
        ? Color4f{0, 0, 0, 1}
        : Color4f{0.06f, 0.06f, 0.075f, 1.0f};
    if (!renderer.BeginTarget(logicalFramebuffer_, base, LoadAction::Clear,
                              &error) ||
        !renderer.EndTarget(&error)) {
        result.allocationFailed = true;
        return result;
    }

    std::unordered_set<std::uint64_t> selectedInstances;
    for (const auto& entry : result.cameraLayout.Entries()) {
        selectedInstances.insert(entry.cameraInstanceId);
    }
    const auto prune = [&](auto& cache) {
        for (auto iterator = cache.begin(); iterator != cache.end();) {
            if (selectedInstances.find(iterator->first) ==
                selectedInstances.end()) {
                iterator = cache.erase(iterator);
            } else {
                ++iterator;
            }
        }
    };
    prune(postProcessPipelines_);
    prune(lightingPipelines_);
    prune(cameraTargets_);

    std::unordered_set<std::uint64_t> usedPostProcessPipelines;
    std::unordered_set<std::uint64_t> usedLightingPipelines;
    std::unordered_set<std::uint64_t> usedCameraTargets;

    const auto warnPost = [&](const CameraOutputEntry& entry,
                              const std::string& reason) {
        const std::string key = std::to_string(entry.cameraInstanceId) + ":" +
                                reason;
        if (postProcessWarnings_.insert(key).second) {
            Log::Warn("PostProcess", reason +
                      "; rendering this camera without post-processing.");
        }
    };
    const auto warnLighting = [&](const CameraOutputEntry& entry,
                                  const std::string& reason) {
        const std::string key = std::to_string(entry.cameraInstanceId) + ":" +
                                reason;
        if (lightingWarnings_.insert(key).second) {
            Log::Warn("Lighting2D", reason);
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

        PreparedWorldLayer2D prepared;
        if (!PrepareWorldLayer(objects, entry, prepared)) {
            result.cameraResults.push_back(cameraResult);
            continue;
        }

        if (entry.camera->IsLightingEnabled() &&
            prepared.queue.HasLitReceivers()) {
            Shader* litShader = ShaderManager::Get().Get("batch_lit");
            if (!litShader || !litShader->IsValid()) {
                prepared.lightingFallback = true;
                prepared.queue.ForceUnlit();
                warnLighting(entry, "Lit shader is unavailable; camera is Unlit");
            } else {
                const PixelSize cameraSize{entry.viewport.width,
                                           entry.viewport.height};
                const LightingFrame2D frame = LightingFrame2D::Build(
                    objects, *entry.camera, cameraSize);
                prepared.selectedLightCount =
                    static_cast<int>(frame.lights.size());
                usedLightingPipelines.insert(entry.cameraInstanceId);
                auto& pipeline = lightingPipelines_[entry.cameraInstanceId];
                if (!pipeline) pipeline = std::make_unique<LightingPipeline2D>();
                LightingPipelinePrepareResult2D lightingResult;
                if (!pipeline->Prepare(frame, *prepared.camera, renderer,
                                       lightingResult) ||
                    !lightingResult.ready) {
                    prepared.lightingFallback = true;
                    prepared.queue.ForceUnlit();
                    warnLighting(entry, lightingResult.error.empty()
                        ? "Lighting context failed; camera is Unlit"
                        : lightingResult.error + "; camera is Unlit");
                } else {
                    prepared.lightingPipeline = pipeline.get();
                    prepared.lightingApplied = true;
                    prepared.shadowFallback = lightingResult.shadowFallback;
                    prepared.shadowedLightCount =
                        lightingResult.shadowedLightCount;
                    prepared.shadowCasterDrawCount =
                        lightingResult.shadowCasterDrawCount;
                    prepared.shadowPasses = lightingResult.shadowPasses;
                    if (lightingResult.shadowFallback) {
                        warnLighting(entry, lightingResult.error.empty()
                            ? "Shadow mask failed; affected lights are unshadowed"
                            : lightingResult.error +
                              "; affected lights are unshadowed");
                    }
                }
            }
        }

        const PixelSize cameraSize{entry.viewport.width, entry.viewport.height};
        bool handled = false;
        if (entry.camera->IsPostProcessEnabled() &&
            !entry.camera->GetPostProcessProfileGuid().empty()) {
            const PostProcessProfileResolveResult resolved =
                PostProcessProfileResolver::Get().Resolve(
                    entry.camera->GetPostProcessProfileGuid());
            if (!resolved) {
                cameraResult.postProcessFallback = true;
                warnPost(entry, resolved.error);
            } else if (resolved.profile->HasActiveEffects()) {
                usedPostProcessPipelines.insert(entry.cameraInstanceId);
                auto& pipeline = postProcessPipelines_[entry.cameraInstanceId];
                if (!pipeline) pipeline = std::make_unique<PostProcessPipeline>();
                if (pipeline->Prepare(cameraSize, *resolved.profile, &error) &&
                    DrawPreparedWorldLayer(prepared, pipeline->SceneTarget(),
                                           renderer, spriteShader, error)) {
                    ColorAttachmentDescriptor destination;
                    destination.view = logicalFramebuffer_.ColorView();
                    destination.loadAction = LoadAction::Load;
                    destination.storeAction = StoreAction::Store;
                    const auto execution = pipeline->Execute(
                        *resolved.profile, renderer, destination,
                        TextureFormat::SRGBA8, logicalSize, entry.viewport);
                    if (execution.success) {
                        cameraResult.rendered = true;
                        cameraResult.postProcessed = execution.postProcessed;
                        cameraResult.postProcessPasses = execution.passes;
                        handled = true;
                    } else {
                        error = execution.error;
                    }
                }
                if (!handled) {
                    cameraResult.postProcessFallback = true;
                    warnPost(entry, error.empty() ? "post-process pipeline failed"
                                                  : error);
                }
            }
        }

        if (!handled) {
            usedCameraTargets.insert(entry.cameraInstanceId);
            auto& cameraTarget = cameraTargets_[entry.cameraInstanceId];
            if (!cameraTarget) {
                cameraTarget = std::make_unique<RenderTarget>(kCameraTargetSpec);
            }
            if (cameraTarget->Resize(cameraSize.width, cameraSize.height,
                                     kCameraTargetSpec, &error) &&
                DrawPreparedWorldLayer(prepared, *cameraTarget, renderer,
                                       spriteShader, error)) {
                ColorAttachmentDescriptor destination;
                destination.view = logicalFramebuffer_.ColorView();
                destination.loadAction = LoadAction::Load;
                destination.storeAction = StoreAction::Store;
                handled = renderer.Blit(
                    cameraTarget->ColorView(),
                    {0, 0, static_cast<std::uint32_t>(cameraSize.width),
                     static_cast<std::uint32_t>(cameraSize.height)},
                    destination,
                    {static_cast<std::uint32_t>(entry.viewport.x),
                     static_cast<std::uint32_t>(entry.viewport.y),
                     static_cast<std::uint32_t>(entry.viewport.width),
                     static_cast<std::uint32_t>(entry.viewport.height)},
                    TextureFilter::Nearest, &error);
            }
            cameraResult.rendered = handled;
        }

        cameraResult.lightingApplied = prepared.lightingApplied;
        cameraResult.lightingFallback = prepared.lightingFallback;
        cameraResult.shadowFallback = prepared.shadowFallback;
        cameraResult.selectedLightCount = prepared.selectedLightCount;
        cameraResult.shadowedLightCount = prepared.shadowedLightCount;
        cameraResult.shadowCasterDrawCount = prepared.shadowCasterDrawCount;
        cameraResult.lightingPasses = prepared.lightingPasses;
        cameraResult.shadowPasses = prepared.shadowPasses;
        if (cameraResult.rendered) {
            result.rendered = true;
            ++renderer.Stats().outputCameraPasses;
        }
        result.postProcessed |= cameraResult.postProcessed;
        result.postProcessFallback |= cameraResult.postProcessFallback;
        result.postProcessPasses += cameraResult.postProcessPasses;
        result.lightingApplied |= cameraResult.lightingApplied;
        result.lightingFallback |= cameraResult.lightingFallback;
        result.shadowFallback |= cameraResult.shadowFallback;
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

    if (!RenderUiLayer(objects, logicalSize, logicalFramebuffer_, renderer,
                       spriteShader, error)) {
        Log::Warn("GameOutput", "UI pass failed: " + error);
    }
    const auto pruneUnused = [](auto& cache, const auto& used) {
        for (auto iterator = cache.begin(); iterator != cache.end();) {
            if (used.find(iterator->first) == used.end()) {
                iterator = cache.erase(iterator);
            } else {
                ++iterator;
            }
        }
    };
    pruneUnused(postProcessPipelines_, usedPostProcessPipelines);
    pruneUnused(lightingPipelines_, usedLightingPipelines);
    pruneUnused(cameraTargets_, usedCameraTargets);
    return result;
}

GameOutputResult GameOutputRenderer::Render(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const GameOutputRequest& request, Renderer& renderer,
    Shader* spriteShader) {
    GameOutputResult result;
    result.presentation = OutputPresentationLayout::Calculate(
        request.scaleMode, request.logicalSize, request.targetSize);
    if (!result.presentation.IsValid() || !spriteShader ||
        !renderer.HasFrame()) {
        return result;
    }
    if (request.destination &&
        (request.destination->Width() != request.targetSize.width ||
         request.destination->Height() != request.targetSize.height)) {
        result.allocationFailed = true;
        return result;
    }
    if (!observedTarget_ || request.targetSize != lastObservedTarget_) {
        observedTarget_ = true;
        lastObservedTarget_ = request.targetSize;
        if (result.presentation.cropped) {
            Log::Warn("GameOutput", "Output target is smaller than logical output; "
                      "presenting a centered crop.");
        }
    }

    const PixelSize logicalSize = result.presentation.logicalSize;
    std::string error;
    if (!logicalFramebuffer_.Resize(logicalSize.width, logicalSize.height,
                                    {}, &error)) {
        result.allocationFailed = true;
        return result;
    }
    const OutputPresentationLayout presentation = result.presentation;
    result = RenderLogical(objects, logicalSize, renderer, spriteShader);
    result.presentation = presentation;

    ColorAttachmentDescriptor destination =
        TargetAttachment(request.destination);
    PixelRectU32 sourceRect;
    PixelRectU32 destinationRect;
    if (!ClipPresentationBlit(result.presentation, sourceRect,
                              destinationRect)) {
        return result;
    }
    result.presented = renderer.Blit(
        logicalFramebuffer_.ColorView(),
        sourceRect, destination, destinationRect,
        TextureFilter::Nearest, &error);
    return result;
}

GameOutputResult GameOutputRenderer::Render(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    PixelSize outputSize, Renderer& renderer, Shader* spriteShader) {
    GameOutputRenderer path;
    return path.Render(objects,
        GameOutputRequest{outputSize, outputSize,
                          GameOutputScaleMode::Native, nullptr},
        renderer, spriteShader);
}

} // namespace molga
