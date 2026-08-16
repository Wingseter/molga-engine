#pragma once

#include "Rendering/GraphicsDevice.h"
#include "Rendering/LightingFrame2D.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"

#include <array>
#include <string>

class Camera2D;
class Renderer;

namespace molga {

struct LightingPipelinePrepareResult2D {
    bool ready = false;
    bool shadowFallback = false;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int shadowPasses = 0;
    std::string error;
};

// Immutable bindings for one world draw. The GPU handles remain private to the
// renderer layer and carry generations, so stale shadow resources fail closed.
struct LightingRenderContext2D {
    const LightingFrame2D* frame = nullptr;
    TextureHandle shadowTextureArray;
    SamplerHandle shadowSampler;
    std::array<bool, kMaxShadowLights2D> shadowLayerAvailable{};
    PixelSize targetSize{};
    PixelRect viewport{};

    bool IsUsable() const noexcept {
        return frame && frame->IsUsable() && viewport.width > 0 &&
               viewport.height > 0;
    }
};

class LightingPipeline2D {
public:
    LightingPipeline2D() = default;
    ~LightingPipeline2D();

    LightingPipeline2D(const LightingPipeline2D&) = delete;
    LightingPipeline2D& operator=(const LightingPipeline2D&) = delete;

    // Shadow failures stay camera-local: unavailable layers are marked false
    // and the corresponding selected lights continue unshadowed.
    bool Prepare(const LightingFrame2D& frame, Camera2D& camera,
                 Renderer& renderer, LightingPipelinePrepareResult2D& result);

    LightingRenderContext2D ContextForTarget(
        PixelSize targetSize, PixelRect topLeftViewport) const;

    PixelSize PreparedSize() const { return preparedSize_; }
    TextureHandle ShadowTextureArray() const { return shadowTextureArray_; }

private:
    bool EnsureResources(PixelSize size, std::string& error);
    void Release();

    LightingFrame2D frame_{};
    PixelSize preparedSize_{};
    TextureHandle shadowTextureArray_;
    SamplerHandle shadowSampler_;
    std::array<bool, kMaxShadowLights2D> shadowLayerAvailable_{};
};

} // namespace molga
