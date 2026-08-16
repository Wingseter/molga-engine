#pragma once

#include "Rendering/LightingFrame2D.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"

#include <array>
#include <string>

class Camera2D;
class Shader;

namespace molga {

struct LightingPipelinePrepareResult2D {
    bool ready = false;
    bool shadowFallback = false;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int shadowPasses = 0;
    std::string error;
};

// Immutable bindings for one world draw. The frame and GL resources are owned
// by the camera-local LightingPipeline2D that produced this context.
struct LightingRenderContext2D {
    const LightingFrame2D* frame = nullptr;
    unsigned int shadowTextureArray = 0;
    std::array<bool, kMaxShadowLights2D> shadowLayerAvailable{};
    PixelSize targetSize{};
    PixelRect viewport{};
    Vector2 viewportOriginBottomLeft{};

    bool IsUsable() const noexcept {
        return frame && frame->IsUsable() &&
               viewport.width > 0 && viewport.height > 0;
    }
};

class LightingPipeline2D {
public:
    LightingPipeline2D() = default;
    ~LightingPipeline2D();

    LightingPipeline2D(const LightingPipeline2D&) = delete;
    LightingPipeline2D& operator=(const LightingPipeline2D&) = delete;

    // A shadow failure is reported in result but does not make Prepare fail:
    // the affected lights remain available as unshadowed lights.
    bool Prepare(const LightingFrame2D& frame, Camera2D& camera,
                 LightingPipelinePrepareResult2D& result);

    LightingRenderContext2D ContextForTarget(
        PixelSize targetSize, PixelRect topLeftViewport) const;

    static void BindFrameUniforms(
        Shader& shader, const LightingRenderContext2D& context);

    PixelSize PreparedSize() const { return preparedSize_; }
    unsigned int ShadowTextureArray() const { return shadowTextureArray_; }

private:
    bool EnsureResources(PixelSize size, std::string& error);
    void Release();

    LightingFrame2D frame_{};
    PixelSize preparedSize_{};
    unsigned int shadowTextureArray_ = 0;
    unsigned int framebuffer_ = 0;
    unsigned int vertexArray_ = 0;
    unsigned int vertexBuffer_ = 0;
    unsigned int indexBuffer_ = 0;
    std::array<bool, kMaxShadowLights2D> shadowLayerAvailable_{};
};

} // namespace molga
