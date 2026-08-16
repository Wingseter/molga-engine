#pragma once

#include "Rendering/GraphicsDevice.h"

namespace molga {

enum class RenderTargetColorFormat {
    SRGBA8,
    RGBA16F,
};

struct RenderTargetSpecification {
    RenderTargetColorFormat colorFormat = RenderTargetColorFormat::SRGBA8;
    bool depthStencil = true;
    TextureFilter filter = TextureFilter::Linear;

    bool operator==(const RenderTargetSpecification& other) const {
        return colorFormat == other.colorFormat &&
               depthStencil == other.depthStencil && filter == other.filter;
    }
    bool operator!=(const RenderTargetSpecification& other) const {
        return !(*this == other);
    }
};

// Backend-neutral offscreen target. Resize is a last-good transaction: all new
// attachments are created before any currently valid attachment is released.
class RenderTarget {
public:
    explicit RenderTarget(RenderTargetSpecification specification = {});
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool Init(int width, int height, std::string* errorOut = nullptr);
    bool Init(int width, int height, RenderTargetSpecification specification,
              std::string* errorOut = nullptr);
    bool Resize(int width, int height, std::string* errorOut = nullptr);
    bool Resize(int width, int height, RenderTargetSpecification specification,
                std::string* errorOut = nullptr);

    TextureView ColorView() const { return {colorTexture_, 0, 0}; }
    TextureView DepthStencilView() const { return {depthStencilTexture_, 0, 0}; }
    SamplerHandle Sampler() const { return sampler_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsValid() const;
    const RenderTargetSpecification& Specification() const {
        return specification_;
    }

private:
    void Release();

    TextureHandle colorTexture_;
    TextureHandle depthStencilTexture_;
    SamplerHandle sampler_;
    int width_ = 0;
    int height_ = 0;
    RenderTargetSpecification specification_{};
};

} // namespace molga
