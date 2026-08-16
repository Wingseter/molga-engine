#include "Rendering/RenderTarget.h"

#include <limits>

namespace molga {

RenderTarget::RenderTarget(RenderTargetSpecification specification)
    : specification_(specification) {}

RenderTarget::~RenderTarget() { Release(); }

void RenderTarget::Release() {
    if (GraphicsDevice* device = GraphicsDevice::Current()) {
        device->DestroySampler(sampler_);
        device->DestroyTexture(depthStencilTexture_);
        device->DestroyTexture(colorTexture_);
    } else {
        sampler_ = {};
        depthStencilTexture_ = {};
        colorTexture_ = {};
    }
    width_ = 0;
    height_ = 0;
}

bool RenderTarget::Init(int width, int height, std::string* errorOut) {
    return Resize(width, height, specification_, errorOut);
}

bool RenderTarget::Init(int width, int height,
                        RenderTargetSpecification specification,
                        std::string* errorOut) {
    return Resize(width, height, specification, errorOut);
}

bool RenderTarget::Resize(int width, int height, std::string* errorOut) {
    return Resize(width, height, specification_, errorOut);
}

bool RenderTarget::Resize(int width, int height,
                          RenderTargetSpecification specification,
                          std::string* errorOut) {
    if (width <= 0 || height <= 0 ||
        width > static_cast<int>(std::numeric_limits<std::uint16_t>::max()) ||
        height > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
        if (errorOut) *errorOut = "render target dimensions are invalid";
        return false;
    }
    if (IsValid() && width_ == width && height_ == height &&
        specification_ == specification) {
        if (errorOut) errorOut->clear();
        return true;
    }
    GraphicsDevice* device = GraphicsDevice::Current();
    if (!device) {
        if (errorOut) *errorOut = "render target requires an active graphics device";
        return false;
    }

    TextureDescriptor colorDescriptor;
    colorDescriptor.width = static_cast<std::uint32_t>(width);
    colorDescriptor.height = static_cast<std::uint32_t>(height);
    colorDescriptor.format = specification.colorFormat == RenderTargetColorFormat::RGBA16F
                                 ? TextureFormat::RGBA16F
                                 : TextureFormat::SRGBA8;
    colorDescriptor.usage = GpuTextureUsage::Sampler |
                            GpuTextureUsage::ColorTarget;
    colorDescriptor.debugName = "RenderTarget.Color";

    SamplerDescriptor samplerDescriptor;
    samplerDescriptor.minFilter = specification.filter;
    samplerDescriptor.magFilter = specification.filter;
    samplerDescriptor.addressU = TextureAddressMode::ClampToEdge;
    samplerDescriptor.addressV = TextureAddressMode::ClampToEdge;
    samplerDescriptor.debugName = "RenderTarget.Sampler";

    std::string error;
    TextureHandle newColor = device->CreateTexture(colorDescriptor, error);
    if (!newColor) {
        if (errorOut) *errorOut = error;
        return false;
    }
    SamplerHandle newSampler = device->CreateSampler(samplerDescriptor, error);
    if (!newSampler) {
        device->DestroyTexture(newColor);
        if (errorOut) *errorOut = error;
        return false;
    }
    TextureHandle newDepth;
    if (specification.depthStencil) {
        TextureDescriptor depthDescriptor;
        depthDescriptor.width = static_cast<std::uint32_t>(width);
        depthDescriptor.height = static_cast<std::uint32_t>(height);
        depthDescriptor.format = device->Info().depthStencilFormat;
        depthDescriptor.usage = GpuTextureUsage::DepthStencilTarget;
        depthDescriptor.debugName = "RenderTarget.DepthStencil";
        newDepth = device->CreateTexture(depthDescriptor, error);
        if (!newDepth) {
            device->DestroySampler(newSampler);
            device->DestroyTexture(newColor);
            if (errorOut) *errorOut = error;
            return false;
        }
    }

    device->DestroySampler(sampler_);
    device->DestroyTexture(depthStencilTexture_);
    device->DestroyTexture(colorTexture_);
    colorTexture_ = newColor;
    depthStencilTexture_ = newDepth;
    sampler_ = newSampler;
    width_ = width;
    height_ = height;
    specification_ = specification;
    if (errorOut) errorOut->clear();
    return true;
}

bool RenderTarget::IsValid() const {
    GraphicsDevice* device = GraphicsDevice::Current();
    if (!device || width_ <= 0 || height_ <= 0 ||
        !device->IsAlive(colorTexture_) || !device->IsAlive(sampler_)) {
        return false;
    }
    return !specification_.depthStencil || device->IsAlive(depthStencilTexture_);
}

} // namespace molga
