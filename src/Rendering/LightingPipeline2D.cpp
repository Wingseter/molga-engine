#include "Rendering/LightingPipeline2D.h"

#include "Rendering/Camera2D.h"
#include "Rendering/Renderer.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"

#include <algorithm>
#include <cstring>

namespace molga {

namespace {

struct alignas(16) ShadowVertexConstants {
    mat4x4 projection;
};

static_assert((sizeof(ShadowVertexConstants) % 16U) == 0U);

} // namespace

LightingPipeline2D::~LightingPipeline2D() { Release(); }

void LightingPipeline2D::Release() {
    if (GraphicsDevice* device = GraphicsDevice::Current()) {
        device->DestroySampler(shadowSampler_);
        device->DestroyTexture(shadowTextureArray_);
    } else {
        shadowSampler_ = {};
        shadowTextureArray_ = {};
    }
    preparedSize_ = {};
    shadowLayerAvailable_.fill(false);
}

bool LightingPipeline2D::EnsureResources(PixelSize size, std::string& error) {
    if (!size.IsValid()) {
        error = "invalid shadow-mask size";
        return false;
    }
    GraphicsDevice* device = GraphicsDevice::Current();
    if (!device) {
        error = "shadow masks require an active graphics device";
        return false;
    }
    if (preparedSize_ == size && device->IsAlive(shadowTextureArray_) &&
        device->IsAlive(shadowSampler_)) {
        error.clear();
        return true;
    }

    TextureDescriptor textureDescriptor;
    textureDescriptor.width = static_cast<std::uint32_t>(size.width);
    textureDescriptor.height = static_cast<std::uint32_t>(size.height);
    textureDescriptor.layers = static_cast<std::uint32_t>(kMaxShadowLights2D);
    textureDescriptor.format = TextureFormat::RGBA8;
    textureDescriptor.usage = GpuTextureUsage::Sampler |
                              GpuTextureUsage::ColorTarget;
    textureDescriptor.debugName = "Lighting2D.ShadowMasks";
    TextureHandle newTexture = device->CreateTexture(textureDescriptor, error);
    if (!newTexture) return false;

    SamplerDescriptor samplerDescriptor;
    samplerDescriptor.minFilter = TextureFilter::Nearest;
    samplerDescriptor.magFilter = TextureFilter::Nearest;
    samplerDescriptor.addressU = TextureAddressMode::ClampToEdge;
    samplerDescriptor.addressV = TextureAddressMode::ClampToEdge;
    samplerDescriptor.debugName = "Lighting2D.ShadowSampler";
    SamplerHandle newSampler = device->CreateSampler(samplerDescriptor, error);
    if (!newSampler) {
        device->DestroyTexture(newTexture);
        return false;
    }

    device->DestroySampler(shadowSampler_);
    device->DestroyTexture(shadowTextureArray_);
    shadowTextureArray_ = newTexture;
    shadowSampler_ = newSampler;
    preparedSize_ = size;
    error.clear();
    return true;
}

bool LightingPipeline2D::Prepare(
    const LightingFrame2D& frame, Camera2D& camera, Renderer& renderer,
    LightingPipelinePrepareResult2D& result) {
    result = {};
    frame_ = frame;
    shadowLayerAvailable_.fill(false);
    if (!frame_.IsUsable()) {
        result.error = "invalid lighting frame";
        return false;
    }
    result.ready = true;
    if (frame_.shadowLayers.empty()) return true;

    Shader* shadowShader = ShaderManager::Get().Get("shadow_mask_2d");
    if (!shadowShader || !shadowShader->IsValid()) {
        result.shadowFallback = true;
        result.error = "shadow-mask shader is unavailable";
        return true;
    }
    if (!EnsureResources(frame_.camera.viewportSize, result.error)) {
        result.shadowFallback = true;
        return true;
    }

    mat4x4 projection;
    mat4x4 view;
    ShadowVertexConstants constants{};
    camera.GetProjectionMatrix(projection);
    camera.GetViewMatrix(view);
    mat4x4_mul(constants.projection, projection, view);

    for (const ShadowMaskLayerFrame2D& layer : frame_.shadowLayers) {
        if (layer.layer < 0 ||
            layer.layer >= static_cast<int>(kMaxShadowLights2D)) {
            result.shadowFallback = true;
            continue;
        }
        const std::uint32_t layerIndex = static_cast<std::uint32_t>(layer.layer);
        const Color4f clear = layer.fullCover
            ? Color4f{1.0f, 0.0f, 0.0f, 1.0f}
            : Color4f{0.0f, 0.0f, 0.0f, 1.0f};
        std::string error;
        if (!renderer.BeginTextureTarget(
                {shadowTextureArray_, 0, layerIndex},
                {0, 0, static_cast<std::uint32_t>(preparedSize_.width),
                 static_cast<std::uint32_t>(preparedSize_.height)},
                TextureFormat::RGBA8, clear, LoadAction::Clear, &error)) {
            result.shadowFallback = true;
            result.error = error;
            continue;
        }

        int layerCasterDrawCount = 0;
        bool fullCover = layer.fullCover;
        if (!fullCover) {
            for (const ShadowCasterGeometry2D& caster : layer.casters) {
                if (caster.fullCover) {
                    fullCover = true;
                    break;
                }
                if (!caster.HasTriangles() || caster.vertices.empty()) continue;
                DrawPacket packet;
                packet.shader = shadowShader;
                packet.blend = BlendState::Opaque;
                packet.vertexStride = sizeof(Vector2);
                packet.vertices.resize(caster.vertices.size() * sizeof(Vector2));
                std::memcpy(packet.vertices.data(), caster.vertices.data(),
                            packet.vertices.size());
                packet.indices = caster.indices;
                packet.vertexUniforms.resize(sizeof(constants));
                std::memcpy(packet.vertexUniforms.data(), &constants,
                            sizeof(constants));
                if (!renderer.Submit(packet, &error)) {
                    result.shadowFallback = true;
                    result.error = error;
                    break;
                }
                ++layerCasterDrawCount;
            }
        }
        if (!renderer.EndTarget(&error)) {
            result.shadowFallback = true;
            result.error = error;
            continue;
        }
        // A caster that fully covers the light must replace the already
        // recorded clear. The snapshot builder normally raises layer.fullCover;
        // fail closed if a malformed mixed layer reaches this point.
        if (fullCover != layer.fullCover) {
            result.shadowFallback = true;
            result.error = "shadow layer full-cover state was inconsistent";
            continue;
        }
        result.shadowCasterDrawCount += layerCasterDrawCount;
        shadowLayerAvailable_[layerIndex] = true;
        ++result.shadowedLightCount;
        ++result.shadowPasses;
    }
    return true;
}

LightingRenderContext2D LightingPipeline2D::ContextForTarget(
    PixelSize targetSize, PixelRect topLeftViewport) const {
    LightingRenderContext2D context;
    context.frame = &frame_;
    context.shadowTextureArray = shadowTextureArray_;
    context.shadowSampler = shadowSampler_;
    context.shadowLayerAvailable = shadowLayerAvailable_;
    context.targetSize = targetSize;
    context.viewport = topLeftViewport;
    return context;
}

} // namespace molga
