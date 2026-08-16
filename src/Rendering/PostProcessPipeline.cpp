#include "Rendering/PostProcessPipeline.h"

#include "Core/PathService.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <filesystem>

namespace molga {
namespace {

constexpr FramebufferSpecification kHdrSceneSpec{
    FramebufferColorFormat::RGBA16F, true, FramebufferTextureFilter::Nearest};
constexpr FramebufferSpecification kHdrNearestSpec{
    FramebufferColorFormat::RGBA16F, false, FramebufferTextureFilter::Nearest};
constexpr FramebufferSpecification kHdrLinearSpec{
    FramebufferColorFormat::RGBA16F, false, FramebufferTextureFilter::Linear};

void SetError(std::string* output, const std::string& message) {
    if (output) *output = message;
}

std::filesystem::path ShaderRoot() {
    const std::filesystem::path deployed =
        PathService::Get().EngineResource("Shaders");
    if (std::filesystem::is_directory(deployed)) return deployed;
#ifdef MOLGA_SHADER_SOURCE_DIR
    const std::filesystem::path source(MOLGA_SHADER_SOURCE_DIR);
    if (std::filesystem::is_directory(source)) return source;
#endif
    return deployed;
}

Shader* LoadPostShader(const char* name, const char* fragment) {
    ShaderManager& manager = ShaderManager::Get();
    Shader* shader = manager.Get(name);
    if (!shader) {
        const std::filesystem::path root = ShaderRoot();
        shader = manager.Load(name,
            (root / "postfx_fullscreen.vert").string(),
            (root / fragment).string());
    }
    // Invalid programs remain cached until the editor's existing Reload
    // Shaders action retries them. Recompiling the same broken built-in every
    // frame would bypass the output renderer's warn-once contract.
    return shader && shader->IsValid() ? shader : nullptr;
}

class ScopedFullscreenGlState {
public:
    ScopedFullscreenGlState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_.data());
        glGetIntegerv(GL_SCISSOR_BOX, scissor_.data());
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0_);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1_);
        glActiveTexture(static_cast<GLenum>(activeTexture_));
        scissorEnabled_ = glIsEnabled(GL_SCISSOR_TEST);
        srgbEnabled_ = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        blendEnabled_ = glIsEnabled(GL_BLEND);
        depthEnabled_ = glIsEnabled(GL_DEPTH_TEST);
        cullEnabled_ = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb_);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha_);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_.data());
    }

    ~ScopedFullscreenGlState() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                          static_cast<GLuint>(drawFramebuffer_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
                          static_cast<GLuint>(readFramebuffer_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glScissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
        SetEnabled(GL_SCISSOR_TEST, scissorEnabled_);
        SetEnabled(GL_FRAMEBUFFER_SRGB, srgbEnabled_);
        SetEnabled(GL_BLEND, blendEnabled_);
        SetEnabled(GL_DEPTH_TEST, depthEnabled_);
        SetEnabled(GL_CULL_FACE, cullEnabled_);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb_),
                            static_cast<GLenum>(blendDstRgb_),
                            static_cast<GLenum>(blendSrcAlpha_),
                            static_cast<GLenum>(blendDstAlpha_));
        glDepthMask(depthMask_);
        glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vao_));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer_));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0_));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture1_));
        glActiveTexture(static_cast<GLenum>(activeTexture_));
    }

private:
    static void SetEnabled(GLenum capability, GLboolean enabled) {
        if (enabled) glEnable(capability);
        else glDisable(capability);
    }

    GLint drawFramebuffer_ = 0;
    GLint readFramebuffer_ = 0;
    std::array<GLint, 4> viewport_{};
    std::array<GLint, 4> scissor_{};
    GLint program_ = 0;
    GLint vao_ = 0;
    GLint arrayBuffer_ = 0;
    GLint activeTexture_ = GL_TEXTURE0;
    GLint texture0_ = 0;
    GLint texture1_ = 0;
    GLint blendSrcRgb_ = GL_ONE;
    GLint blendDstRgb_ = GL_ZERO;
    GLint blendSrcAlpha_ = GL_ONE;
    GLint blendDstAlpha_ = GL_ZERO;
    GLboolean scissorEnabled_ = GL_FALSE;
    GLboolean srgbEnabled_ = GL_FALSE;
    GLboolean blendEnabled_ = GL_FALSE;
    GLboolean depthEnabled_ = GL_FALSE;
    GLboolean cullEnabled_ = GL_FALSE;
    GLboolean depthMask_ = GL_TRUE;
    std::array<GLboolean, 4> colorMask_{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
};

void BeginFullscreenTarget(GLuint framebuffer, PixelSize size, GLuint vao,
                           Shader& shader) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, size.width, size.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    shader.Use();
    glBindVertexArray(vao);
}

void BindTexture(GLenum unit, GLuint texture) {
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

} // namespace

PostProcessPipeline::PostProcessPipeline()
    : sceneTarget_(kHdrSceneSpec),
      pingA_(kHdrNearestSpec),
      pingB_(kHdrNearestSpec) {}

PostProcessPipeline::~PostProcessPipeline() {
    if (fullscreenVbo_) glDeleteBuffers(1, &fullscreenVbo_);
    if (fullscreenVao_) glDeleteVertexArrays(1, &fullscreenVao_);
}

bool PostProcessPipeline::PrepareShaders(const PostProcessProfile2D& profile,
                                         std::string* errorOut) {
    bool needsBloom = false;
    bool needsColorAdjust = false;
    bool needsVignette = false;
    for (const PostProcessEffect2D& effect : profile.effects) {
        if (!effect.IsActive()) continue;
        switch (effect.type) {
            case PostProcessEffectType2D::Bloom: needsBloom = true; break;
            case PostProcessEffectType2D::ColorAdjust:
                needsColorAdjust = true;
                break;
            case PostProcessEffectType2D::Vignette: needsVignette = true; break;
        }
    }
    if (needsBloom) {
        bloomDownShader_ = LoadPostShader(
            "postfx.bloom.down", "postfx_bloom_down.frag");
        bloomUpShader_ = LoadPostShader(
            "postfx.bloom.up", "postfx_bloom_up.frag");
        bloomCompositeShader_ = LoadPostShader(
            "postfx.bloom.composite", "postfx_bloom_composite.frag");
    }
    if (needsColorAdjust) {
        colorAdjustShader_ = LoadPostShader(
            "postfx.color_adjust", "postfx_color_adjust.frag");
    }
    if (needsVignette) {
        vignetteShader_ = LoadPostShader(
            "postfx.vignette", "postfx_vignette.frag");
    }
    resolveShader_ = LoadPostShader("postfx.resolve", "postfx_resolve.frag");
    if ((needsBloom && (!bloomDownShader_ || !bloomUpShader_ ||
                        !bloomCompositeShader_)) ||
        (needsColorAdjust && !colorAdjustShader_) ||
        (needsVignette && !vignetteShader_) || !resolveShader_) {
        SetError(errorOut, "a required built-in post-process shader failed to load");
        return false;
    }
    return true;
}

bool PostProcessPipeline::PrepareQuad(std::string* errorOut) {
    if (fullscreenVao_ && fullscreenVbo_) return true;
    constexpr float vertices[] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f};
    glGenVertexArrays(1, &fullscreenVao_);
    glGenBuffers(1, &fullscreenVbo_);
    if (!fullscreenVao_ || !fullscreenVbo_) {
        SetError(errorOut, "could not allocate the post-process fullscreen quad");
        return false;
    }
    GLint previousVao = 0;
    GLint previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glBindVertexArray(fullscreenVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fullscreenVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
    return true;
}

bool PostProcessPipeline::PrepareTargets(PixelSize size, bool needsBloom,
                                         std::string* errorOut) {
    if (!sceneTarget_.Resize(size.width, size.height, kHdrSceneSpec) ||
        !pingA_.Resize(size.width, size.height, kHdrNearestSpec) ||
        !pingB_.Resize(size.width, size.height, kHdrNearestSpec)) {
        SetError(errorOut, "could not allocate full-resolution HDR post-process targets");
        return false;
    }
    if (!needsBloom) return true;

    std::vector<PixelSize> mipSizes;
    int width = size.width;
    int height = size.height;
    for (int level = 0; level < 5; ++level) {
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
        mipSizes.push_back({width, height});
        if (width == 1 && height == 1) break;
    }
    bloomDown_.resize(mipSizes.size());
    bloomUp_.resize(mipSizes.size());
    for (std::size_t level = 0; level < mipSizes.size(); ++level) {
        if (!bloomDown_[level])
            bloomDown_[level] = std::make_unique<Framebuffer>(kHdrLinearSpec);
        if (!bloomUp_[level])
            bloomUp_[level] = std::make_unique<Framebuffer>(kHdrLinearSpec);
        const PixelSize mip = mipSizes[level];
        if (!bloomDown_[level]->Resize(mip.width, mip.height, kHdrLinearSpec) ||
            !bloomUp_[level]->Resize(mip.width, mip.height, kHdrLinearSpec)) {
            SetError(errorOut, "could not allocate adaptive Bloom mip targets");
            return false;
        }
    }
    return true;
}

bool PostProcessPipeline::Prepare(PixelSize size,
                                  const PostProcessProfile2D& profile,
                                  std::string* errorOut) {
    SetError(errorOut, {});
    if (!size.IsValid() || !profile.HasActiveEffects()) {
        SetError(errorOut, "post-process preparation requires a valid size and active effect");
        return false;
    }
    bool needsBloom = false;
    for (const PostProcessEffect2D& effect : profile.effects) {
        needsBloom = needsBloom ||
            (effect.type == PostProcessEffectType2D::Bloom && effect.IsActive());
    }
    if (!PrepareShaders(profile, errorOut) || !PrepareQuad(errorOut) ||
        !PrepareTargets(size, needsBloom, errorOut)) return false;
    preparedSize_ = size;
    return true;
}

void PostProcessPipeline::DrawSingleTexture(Shader& shader, unsigned int source,
                                            Framebuffer& destination) {
    BeginFullscreenTarget(destination.Id(),
        {destination.Width(), destination.Height()}, fullscreenVao_, shader);
    BindTexture(GL_TEXTURE0, source);
    shader.SetInt("uSource", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int PostProcessPipeline::ExecuteBloom(unsigned int source,
                                      Framebuffer& destination,
                                      const BloomSettings2D& settings) {
    if (bloomDown_.empty()) return 0;
    GLuint current = source;
    PixelSize sourceSize = preparedSize_;
    int passes = 0;
    for (std::size_t level = 0; level < bloomDown_.size(); ++level) {
        Framebuffer& target = *bloomDown_[level];
        BeginFullscreenTarget(target.Id(), {target.Width(), target.Height()},
                              fullscreenVao_, *bloomDownShader_);
        BindTexture(GL_TEXTURE0, current);
        bloomDownShader_->SetInt("uSource", 0);
        bloomDownShader_->SetVec2("uTexelSize",
            1.0f / static_cast<float>(sourceSize.width),
            1.0f / static_cast<float>(sourceSize.height));
        bloomDownShader_->SetBool("uPrefilter", level == 0);
        bloomDownShader_->SetFloat("uThreshold", settings.threshold);
        bloomDownShader_->SetFloat("uSoftKnee", settings.softKnee);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++passes;
        current = target.ColorTexture();
        sourceSize = {target.Width(), target.Height()};
    }

    GLuint bloom = bloomDown_.back()->ColorTexture();
    PixelSize lowSize{bloomDown_.back()->Width(), bloomDown_.back()->Height()};
    for (std::size_t index = bloomDown_.size(); index > 1; --index) {
        const std::size_t highIndex = index - 2;
        Framebuffer& high = *bloomDown_[highIndex];
        Framebuffer& target = *bloomUp_[highIndex];
        BeginFullscreenTarget(target.Id(), {target.Width(), target.Height()},
                              fullscreenVao_, *bloomUpShader_);
        BindTexture(GL_TEXTURE0, high.ColorTexture());
        BindTexture(GL_TEXTURE1, bloom);
        bloomUpShader_->SetInt("uHigh", 0);
        bloomUpShader_->SetInt("uLow", 1);
        bloomUpShader_->SetVec2("uLowTexelSize",
            1.0f / static_cast<float>(lowSize.width),
            1.0f / static_cast<float>(lowSize.height));
        bloomUpShader_->SetFloat("uScatter", settings.scatter);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++passes;
        bloom = target.ColorTexture();
        lowSize = {target.Width(), target.Height()};
    }

    BeginFullscreenTarget(destination.Id(), preparedSize_, fullscreenVao_,
                          *bloomCompositeShader_);
    BindTexture(GL_TEXTURE0, source);
    BindTexture(GL_TEXTURE1, bloom);
    bloomCompositeShader_->SetInt("uScene", 0);
    bloomCompositeShader_->SetInt("uBloom", 1);
    bloomCompositeShader_->SetFloat("uIntensity", settings.intensity);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    return passes + 1;
}

int PostProcessPipeline::ExecuteColorAdjust(
    unsigned int source, Framebuffer& destination,
    const ColorAdjustSettings2D& settings) {
    BeginFullscreenTarget(destination.Id(), preparedSize_, fullscreenVao_,
                          *colorAdjustShader_);
    BindTexture(GL_TEXTURE0, source);
    colorAdjustShader_->SetInt("uSource", 0);
    colorAdjustShader_->SetFloat("uExposureEV", settings.exposureEV);
    colorAdjustShader_->SetFloat("uContrast", settings.contrast);
    colorAdjustShader_->SetFloat("uSaturation", settings.saturation);
    colorAdjustShader_->SetVec3("uTint", settings.tint[0], settings.tint[1],
                                settings.tint[2]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    return 1;
}

int PostProcessPipeline::ExecuteVignette(unsigned int source,
                                         Framebuffer& destination,
                                         const VignetteSettings2D& settings) {
    BeginFullscreenTarget(destination.Id(), preparedSize_, fullscreenVao_,
                          *vignetteShader_);
    BindTexture(GL_TEXTURE0, source);
    vignetteShader_->SetInt("uSource", 0);
    vignetteShader_->SetFloat("uIntensity", settings.intensity);
    vignetteShader_->SetFloat("uSmoothness", settings.smoothness);
    vignetteShader_->SetFloat("uAspect",
        static_cast<float>(preparedSize_.width) /
        static_cast<float>(preparedSize_.height));
    vignetteShader_->SetVec3("uColor", settings.color[0], settings.color[1],
                             settings.color[2]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    return 1;
}

bool PostProcessPipeline::DrawResolve(unsigned int source,
                                      unsigned int destinationFramebuffer,
                                      PixelSize destinationSize,
                                      PixelRect destinationRect) {
    BeginFullscreenTarget(destinationFramebuffer, destinationSize, fullscreenVao_,
                          *resolveShader_);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }
    // Public output rectangles use top-left coordinates. OpenGL viewports use
    // a bottom-left origin, so conversion belongs only at this resolve edge.
    glViewport(destinationRect.x,
               destinationSize.height -
                   (destinationRect.y + destinationRect.height),
               destinationRect.width, destinationRect.height);
    glEnable(GL_FRAMEBUFFER_SRGB);
    BindTexture(GL_TEXTURE0, source);
    resolveShader_->SetInt("uSource", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

PostProcessExecutionResult PostProcessPipeline::Execute(
    const PostProcessProfile2D& profile, unsigned int destinationFramebuffer,
    PixelSize destinationSize) {
    return Execute(profile, destinationFramebuffer, destinationSize,
                   {0, 0, destinationSize.width, destinationSize.height});
}

PostProcessExecutionResult PostProcessPipeline::Execute(
    const PostProcessProfile2D& profile, unsigned int destinationFramebuffer,
    PixelSize destinationSize, PixelRect destinationRect) {
    PostProcessExecutionResult result;
    const bool validRect = destinationRect.x >= 0 && destinationRect.y >= 0 &&
        destinationRect.width > 0 && destinationRect.height > 0 &&
        destinationRect.width == preparedSize_.width &&
        destinationRect.height == preparedSize_.height &&
        destinationRect.x <= destinationSize.width - destinationRect.width &&
        destinationRect.y <= destinationSize.height - destinationRect.height;
    if (!destinationSize.IsValid() || !validRect || !preparedSize_.IsValid() ||
        !profile.HasActiveEffects() || !sceneTarget_.IsValid() ||
        !pingA_.IsValid() || !pingB_.IsValid() || !resolveShader_) {
        result.error = "post-process pipeline was not prepared for this frame";
        return result;
    }

    ScopedFullscreenGlState savedState;
    GLuint current = sceneTarget_.ColorTexture();
    bool useA = true;
    for (const PostProcessEffect2D& effect : profile.effects) {
        if (!effect.IsActive()) continue;
        Framebuffer& destination = useA ? pingA_ : pingB_;
        int effectPasses = 0;
        switch (effect.type) {
            case PostProcessEffectType2D::Bloom:
                effectPasses = ExecuteBloom(
                    current, destination, std::get<BloomSettings2D>(effect.settings));
                break;
            case PostProcessEffectType2D::ColorAdjust:
                effectPasses = ExecuteColorAdjust(
                    current, destination,
                    std::get<ColorAdjustSettings2D>(effect.settings));
                break;
            case PostProcessEffectType2D::Vignette:
                effectPasses = ExecuteVignette(
                    current, destination, std::get<VignetteSettings2D>(effect.settings));
                break;
        }
        if (effectPasses <= 0) {
            result.error = "an active post-process effect produced no pass";
            return result;
        }
        result.passes += effectPasses;
        current = destination.ColorTexture();
        useA = !useA;
    }
    if (!DrawResolve(current, destinationFramebuffer, destinationSize,
                     destinationRect)) {
        result.error = "post-process destination framebuffer is incomplete";
        return result;
    }
    ++result.passes;
    result.success = true;
    result.postProcessed = true;
    return result;
}

} // namespace molga
