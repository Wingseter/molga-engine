#include "Rendering/PostProcessPipeline.h"

#include "Rendering/Renderer.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"

#include <algorithm>
#include <array>

namespace molga {
namespace {

constexpr RenderTargetSpecification kHdrSceneSpec{
    RenderTargetColorFormat::RGBA16F, true, TextureFilter::Nearest};
constexpr RenderTargetSpecification kHdrNearestSpec{
    RenderTargetColorFormat::RGBA16F, false, TextureFilter::Nearest};
constexpr RenderTargetSpecification kHdrLinearSpec{
    RenderTargetColorFormat::RGBA16F, false, TextureFilter::Linear};

struct alignas(16) BloomDownConstants {
    float texelSize[2]{};
    float threshold = 0.0f;
    float softKnee = 0.0f;
    std::uint32_t prefilter = 0;
    float padding[3]{};
};

struct alignas(16) BloomUpConstants {
    float lowTexelSize[2]{};
    float scatter = 0.0f;
    float padding = 0.0f;
};

struct alignas(16) BloomCompositeConstants {
    float intensity = 0.0f;
    float padding[3]{};
};

struct alignas(16) ColorAdjustConstants {
    float tint[4]{};
    float exposureEV = 0.0f;
    float contrast = 0.0f;
    float saturation = 1.0f;
    float padding = 0.0f;
};

struct alignas(16) VignetteConstants {
    float color[4]{};
    float intensity = 0.0f;
    float smoothness = 0.0f;
    float aspect = 1.0f;
    float padding = 0.0f;
};

static_assert(sizeof(BloomDownConstants) == 32U);
static_assert(sizeof(BloomUpConstants) == 16U);
static_assert(sizeof(BloomCompositeConstants) == 16U);
static_assert(sizeof(ColorAdjustConstants) == 32U);
static_assert(sizeof(VignetteConstants) == 32U);

DrawTextureBinding FragmentTexture(std::uint32_t slot, TextureView view,
                                   SamplerHandle sampler) {
    return {DrawTextureBinding::Stage::Fragment, slot, view, sampler};
}

} // namespace

PostProcessPipeline::PostProcessPipeline()
    : sceneTarget_(kHdrSceneSpec), pingA_(kHdrNearestSpec),
      pingB_(kHdrNearestSpec) {}

bool PostProcessPipeline::PrepareShaders(const PostProcessProfile2D& profile,
                                         std::string* errorOut) {
    ShaderManager& manager = ShaderManager::Get();
    resolveShader_ = manager.Get("postfx_resolve");
    if (!resolveShader_) {
        if (errorOut) *errorOut = "post-process resolve shader is unavailable";
        return false;
    }
    bool needsBloom = false;
    bool needsColorAdjust = false;
    bool needsVignette = false;
    for (const PostProcessEffect2D& effect : profile.effects) {
        if (!effect.IsActive()) continue;
        needsBloom |= effect.type == PostProcessEffectType2D::Bloom;
        needsColorAdjust |= effect.type == PostProcessEffectType2D::ColorAdjust;
        needsVignette |= effect.type == PostProcessEffectType2D::Vignette;
    }
    if (needsBloom) {
        bloomDownShader_ = manager.Get("postfx_bloom_down");
        bloomUpShader_ = manager.Get("postfx_bloom_up");
        bloomCompositeShader_ = manager.Get("postfx_bloom_composite");
        if (!bloomDownShader_ || !bloomUpShader_ || !bloomCompositeShader_) {
            if (errorOut) *errorOut = "bloom shader bundle entries are unavailable";
            return false;
        }
    }
    if (needsColorAdjust) {
        colorAdjustShader_ = manager.Get("postfx_color_adjust");
        if (!colorAdjustShader_) {
            if (errorOut) *errorOut = "color-adjust shader is unavailable";
            return false;
        }
    }
    if (needsVignette) {
        vignetteShader_ = manager.Get("postfx_vignette");
        if (!vignetteShader_) {
            if (errorOut) *errorOut = "vignette shader is unavailable";
            return false;
        }
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool PostProcessPipeline::PrepareTargets(PixelSize size, bool needsBloom,
                                         std::string* errorOut) {
    std::string error;
    if (!sceneTarget_.Resize(size.width, size.height, kHdrSceneSpec, &error) ||
        !pingA_.Resize(size.width, size.height, kHdrNearestSpec, &error) ||
        !pingB_.Resize(size.width, size.height, kHdrNearestSpec, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }

    std::vector<PixelSize> mipSizes;
    if (needsBloom) {
        int width = size.width;
        int height = size.height;
        while ((width > 1 || height > 1) && mipSizes.size() < 6U) {
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
            mipSizes.push_back({width, height});
        }
        if (mipSizes.empty()) mipSizes.push_back({1, 1});
    }
    while (bloomDown_.size() < mipSizes.size()) {
        bloomDown_.push_back(std::make_unique<RenderTarget>(kHdrLinearSpec));
        bloomUp_.push_back(std::make_unique<RenderTarget>(kHdrLinearSpec));
    }
    while (bloomDown_.size() > mipSizes.size()) {
        bloomDown_.pop_back();
        bloomUp_.pop_back();
    }
    for (std::size_t index = 0; index < mipSizes.size(); ++index) {
        if (!bloomDown_[index]->Resize(mipSizes[index].width,
                                       mipSizes[index].height,
                                       kHdrLinearSpec, &error) ||
            !bloomUp_[index]->Resize(mipSizes[index].width,
                                     mipSizes[index].height,
                                     kHdrLinearSpec, &error)) {
            if (errorOut) *errorOut = error;
            return false;
        }
    }
    preparedSize_ = size;
    if (errorOut) errorOut->clear();
    return true;
}

bool PostProcessPipeline::Prepare(PixelSize size,
                                  const PostProcessProfile2D& profile,
                                  std::string* errorOut) {
    if (!size.IsValid() || !profile.HasActiveEffects()) {
        if (errorOut) *errorOut = "post-process requires a valid size and active effect";
        return false;
    }
    const bool needsBloom = std::any_of(
        profile.effects.begin(), profile.effects.end(),
        [](const PostProcessEffect2D& effect) {
            return effect.IsActive() &&
                   effect.type == PostProcessEffectType2D::Bloom;
        });
    if (!PrepareShaders(profile, errorOut) ||
        !PrepareTargets(size, needsBloom, errorOut)) {
        return false;
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool PostProcessPipeline::DrawToTarget(
    Renderer& renderer, Shader& shader,
    const std::vector<DrawTextureBinding>& textures, const void* constants,
    std::size_t constantsSize, RenderTarget& destination, std::string& error) {
    if (!renderer.BeginTarget(destination, {0, 0, 0, 1}, LoadAction::Clear,
                              &error)) {
        return false;
    }
    const bool drew = renderer.SubmitFullscreen(
        shader, textures, constants, constantsSize, BlendState::Opaque, &error);
    const bool ended = renderer.EndTarget(&error);
    return drew && ended;
}

bool PostProcessPipeline::DrawToAttachment(
    Renderer& renderer, Shader& shader,
    const std::vector<DrawTextureBinding>& textures, const void* constants,
    std::size_t constantsSize,
    const ColorAttachmentDescriptor& destination,
    TextureFormat destinationFormat, PixelRect destinationRect,
    std::string& error) {
    const PixelRectU32 viewport{
        static_cast<std::uint32_t>(destinationRect.x),
        static_cast<std::uint32_t>(destinationRect.y),
        static_cast<std::uint32_t>(destinationRect.width),
        static_cast<std::uint32_t>(destinationRect.height)};
    const bool began = destination.swapchain
        ? renderer.BeginSwapchainPass(viewport, destination.loadAction,
                                      destination.clearColor, &error)
        : renderer.BeginTextureTarget(destination.view, viewport,
                                      destinationFormat,
                                      destination.clearColor,
                                      destination.loadAction, &error);
    if (!began) return false;
    const bool drew = renderer.SubmitFullscreen(
        shader, textures, constants, constantsSize, BlendState::Opaque, &error);
    const bool ended = renderer.EndTarget(&error);
    return drew && ended;
}

int PostProcessPipeline::ExecuteBloom(Renderer& renderer,
                                      SampledTexture source,
                                      RenderTarget& destination,
                                      const BloomSettings2D& settings,
                                      std::string& error) {
    int passes = 0;
    SampledTexture current = source;
    for (std::size_t level = 0; level < bloomDown_.size(); ++level) {
        RenderTarget& target = *bloomDown_[level];
        BloomDownConstants constants;
        constants.texelSize[0] = 1.0f / static_cast<float>(
            level == 0 ? preparedSize_.width : bloomDown_[level - 1]->Width());
        constants.texelSize[1] = 1.0f / static_cast<float>(
            level == 0 ? preparedSize_.height : bloomDown_[level - 1]->Height());
        constants.threshold = settings.threshold;
        constants.softKnee = settings.softKnee;
        constants.prefilter = level == 0 ? 1U : 0U;
        if (!DrawToTarget(renderer, *bloomDownShader_,
                          {FragmentTexture(0, current.view, current.sampler)},
                          &constants, sizeof(constants), target, error)) {
            return -1;
        }
        current = {target.ColorView(), target.Sampler()};
        ++passes;
    }

    SampledTexture bloom = current;
    for (std::size_t level = bloomDown_.size(); level-- > 1U;) {
        RenderTarget& high = *bloomDown_[level - 1U];
        RenderTarget& target = *bloomUp_[level - 1U];
        BloomUpConstants constants;
        constants.lowTexelSize[0] = 1.0f /
            static_cast<float>(std::max(1, bloomDown_[level]->Width()));
        constants.lowTexelSize[1] = 1.0f /
            static_cast<float>(std::max(1, bloomDown_[level]->Height()));
        constants.scatter = settings.scatter;
        if (!DrawToTarget(renderer, *bloomUpShader_,
                          {FragmentTexture(0, high.ColorView(), high.Sampler()),
                           FragmentTexture(1, bloom.view, bloom.sampler)},
                          &constants, sizeof(constants), target, error)) {
            return -1;
        }
        bloom = {target.ColorView(), target.Sampler()};
        ++passes;
    }

    BloomCompositeConstants composite;
    composite.intensity = settings.intensity;
    if (!DrawToTarget(renderer, *bloomCompositeShader_,
                      {FragmentTexture(0, source.view, source.sampler),
                       FragmentTexture(1, bloom.view, bloom.sampler)},
                      &composite, sizeof(composite), destination, error)) {
        return -1;
    }
    return passes + 1;
}

int PostProcessPipeline::ExecuteColorAdjust(
    Renderer& renderer, SampledTexture source, RenderTarget& destination,
    const ColorAdjustSettings2D& settings, std::string& error) {
    ColorAdjustConstants constants;
    constants.tint[0] = settings.tint[0];
    constants.tint[1] = settings.tint[1];
    constants.tint[2] = settings.tint[2];
    constants.tint[3] = 1.0f;
    constants.exposureEV = settings.exposureEV;
    constants.contrast = settings.contrast;
    constants.saturation = settings.saturation;
    return DrawToTarget(renderer, *colorAdjustShader_,
                        {FragmentTexture(0, source.view, source.sampler)},
                        &constants, sizeof(constants), destination, error)
        ? 1 : -1;
}

int PostProcessPipeline::ExecuteVignette(
    Renderer& renderer, SampledTexture source, RenderTarget& destination,
    const VignetteSettings2D& settings, std::string& error) {
    VignetteConstants constants;
    constants.color[0] = settings.color[0];
    constants.color[1] = settings.color[1];
    constants.color[2] = settings.color[2];
    constants.color[3] = 1.0f;
    constants.intensity = settings.intensity;
    constants.smoothness = settings.smoothness;
    constants.aspect = static_cast<float>(preparedSize_.width) /
                       static_cast<float>(preparedSize_.height);
    return DrawToTarget(renderer, *vignetteShader_,
                        {FragmentTexture(0, source.view, source.sampler)},
                        &constants, sizeof(constants), destination, error)
        ? 1 : -1;
}

PostProcessExecutionResult PostProcessPipeline::Execute(
    const PostProcessProfile2D& profile, Renderer& renderer,
    const ColorAttachmentDescriptor& destination,
    TextureFormat destinationFormat, PixelSize destinationSize,
    PixelRect destinationRect) {
    PostProcessExecutionResult result;
    if (preparedSize_ != PixelSize{sceneTarget_.Width(), sceneTarget_.Height()} ||
        !preparedSize_.IsValid() || !destinationSize.IsValid() ||
        destinationRect.width <= 0 || destinationRect.height <= 0) {
        result.error = "post-process pipeline is not prepared";
        return result;
    }

    SampledTexture current{sceneTarget_.ColorView(), sceneTarget_.Sampler()};
    bool useA = true;
    auto nextTarget = [&]() -> RenderTarget& {
        RenderTarget& target = useA ? pingA_ : pingB_;
        useA = !useA;
        return target;
    };
    std::string error;
    for (const PostProcessEffect2D& effect : profile.effects) {
        if (!effect.IsActive()) continue;
        RenderTarget& target = nextTarget();
        int count = -1;
        switch (effect.type) {
            case PostProcessEffectType2D::Bloom:
                count = ExecuteBloom(
                    renderer, current, target,
                    std::get<BloomSettings2D>(effect.settings), error);
                break;
            case PostProcessEffectType2D::ColorAdjust:
                count = ExecuteColorAdjust(
                    renderer, current, target,
                    std::get<ColorAdjustSettings2D>(effect.settings), error);
                break;
            case PostProcessEffectType2D::Vignette:
                count = ExecuteVignette(
                    renderer, current, target,
                    std::get<VignetteSettings2D>(effect.settings), error);
                break;
        }
        if (count < 0) { result.error = error; return result; }
        result.passes += count;
        current = {target.ColorView(), target.Sampler()};
    }
    if (!DrawToAttachment(renderer, *resolveShader_,
                          {FragmentTexture(0, current.view, current.sampler)},
                          nullptr, 0, destination, destinationFormat,
                          destinationRect, error)) {
        result.error = error;
        return result;
    }
    ++result.passes;
    result.success = true;
    result.postProcessed = profile.HasActiveEffects();
    return result;
}

} // namespace molga
