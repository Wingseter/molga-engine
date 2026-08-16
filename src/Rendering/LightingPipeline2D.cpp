#include "Rendering/LightingPipeline2D.h"

#include "Common/linmath.h"
#include "Rendering/Camera2D.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <string>

namespace molga {
namespace {

void DiscardGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool ConsumeGlErrors() {
    bool hadError = false;
    while (glGetError() != GL_NO_ERROR) {
        hadError = true;
    }
    return hadError;
}

class ScopedShadowGlState {
public:
    ScopedShadowGlState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_.data());
        glGetIntegerv(GL_SCISSOR_BOX, scissor_.data());
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer_);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
        for (int unit = 0; unit < 3; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2D_[unit]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &textureArray_[unit]);
        }

        scissorEnabled_ = glIsEnabled(GL_SCISSOR_TEST);
        srgbEnabled_ = glIsEnabled(GL_FRAMEBUFFER_SRGB);
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
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_.data());
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilMaskFront_);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &stencilMaskBack_);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor_.data());
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth_);
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clearStencil_);
    }

    ~ScopedShadowGlState() {
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
        SetEnabled(GL_STENCIL_TEST, stencilEnabled_);
        SetEnabled(GL_CULL_FACE, cullEnabled_);
        glBlendFuncSeparate(
            static_cast<GLenum>(blendSrcRgb_), static_cast<GLenum>(blendDstRgb_),
            static_cast<GLenum>(blendSrcAlpha_), static_cast<GLenum>(blendDstAlpha_));
        glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb_),
                                static_cast<GLenum>(blendEquationAlpha_));
        glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        glDepthMask(depthMask_);
        glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(stencilMaskFront_));
        glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(stencilMaskBack_));
        glClearColor(clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]);
        glClearDepth(clearDepth_);
        glClearStencil(clearStencil_);

        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vertexArray_));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer_));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLuint>(elementBuffer_));
        for (int unit = 0; unit < 3; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2D_[unit]));
            glBindTexture(GL_TEXTURE_2D_ARRAY,
                          static_cast<GLuint>(textureArray_[unit]));
        }
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
    GLint vertexArray_ = 0;
    GLint arrayBuffer_ = 0;
    GLint elementBuffer_ = 0;
    GLint activeTexture_ = GL_TEXTURE0;
    std::array<GLint, 3> texture2D_{};
    std::array<GLint, 3> textureArray_{};
    GLboolean scissorEnabled_ = GL_FALSE;
    GLboolean srgbEnabled_ = GL_FALSE;
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
    std::array<GLboolean, 4> colorMask_{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depthMask_ = GL_TRUE;
    GLint stencilMaskFront_ = -1;
    GLint stencilMaskBack_ = -1;
    std::array<GLfloat, 4> clearColor_{};
    GLdouble clearDepth_ = 1.0;
    GLint clearStencil_ = 0;
};

} // namespace

LightingPipeline2D::~LightingPipeline2D() {
    Release();
}

void LightingPipeline2D::Release() {
    if (indexBuffer_) glDeleteBuffers(1, &indexBuffer_);
    if (vertexBuffer_) glDeleteBuffers(1, &vertexBuffer_);
    if (vertexArray_) glDeleteVertexArrays(1, &vertexArray_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    if (shadowTextureArray_) glDeleteTextures(1, &shadowTextureArray_);
    indexBuffer_ = 0;
    vertexBuffer_ = 0;
    vertexArray_ = 0;
    framebuffer_ = 0;
    shadowTextureArray_ = 0;
    preparedSize_ = {};
    shadowLayerAvailable_.fill(false);
}

bool LightingPipeline2D::EnsureResources(PixelSize size, std::string& error) {
    if (!size.IsValid()) {
        error = "invalid shadow-mask size";
        return false;
    }
    if (preparedSize_ == size && shadowTextureArray_ && framebuffer_ &&
        vertexArray_ && vertexBuffer_ && indexBuffer_) {
        return true;
    }

    GLuint candidateTexture = 0;
    GLuint candidateFramebuffer = 0;
    glGenTextures(1, &candidateTexture);
    glGenFramebuffers(1, &candidateFramebuffer);
    if (!candidateTexture || !candidateFramebuffer) {
        if (candidateFramebuffer) glDeleteFramebuffers(1, &candidateFramebuffer);
        if (candidateTexture) glDeleteTextures(1, &candidateTexture);
        error = "could not allocate shadow texture/FBO";
        return false;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, candidateTexture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, size.width, size.height,
                 static_cast<GLsizei>(kMaxShadowLights2D), 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    GLint allocatedWidth = 0;
    GLint allocatedHeight = 0;
    glGetTexLevelParameteriv(
        GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &allocatedWidth);
    glGetTexLevelParameteriv(
        GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &allocatedHeight);
    if (allocatedWidth != size.width || allocatedHeight != size.height) {
        glDeleteFramebuffers(1, &candidateFramebuffer);
        glDeleteTextures(1, &candidateTexture);
        error = "shadow texture allocation failed";
        return false;
    }

    if (shadowTextureArray_) glDeleteTextures(1, &shadowTextureArray_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    shadowTextureArray_ = candidateTexture;
    framebuffer_ = candidateFramebuffer;
    preparedSize_ = size;

    if (!vertexArray_) glGenVertexArrays(1, &vertexArray_);
    if (!vertexBuffer_) glGenBuffers(1, &vertexBuffer_);
    if (!indexBuffer_) glGenBuffers(1, &indexBuffer_);
    if (!vertexArray_ || !vertexBuffer_ || !indexBuffer_) {
        error = "could not allocate shadow geometry buffers";
        Release();
        return false;
    }
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vector2),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    return true;
}

bool LightingPipeline2D::Prepare(
    const LightingFrame2D& frame, Camera2D& camera,
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

    ScopedShadowGlState savedState;
    // Attribute only errors produced by this pass. Entry errors belong to the
    // caller and must not be mistaken for a shadow-layer failure.
    DiscardGlErrors();
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
    if (ConsumeGlErrors()) {
        result.shadowFallback = true;
        result.error = "shadow-mask resource setup failed";
        Release();
        return true;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, preparedSize_.width, preparedSize_.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);

    mat4x4 projection;
    mat4x4 view;
    mat4x4 projectionView;
    camera.GetProjectionMatrix(projection);
    camera.GetViewMatrix(view);
    mat4x4_mul(projectionView, projection, view);
    shadowShader->Use();
    shadowShader->SetMat4("projection",
                          reinterpret_cast<const float*>(projectionView));
    glBindVertexArray(vertexArray_);
    if (ConsumeGlErrors()) {
        result.shadowFallback = true;
        result.error = "shadow-mask draw setup failed";
        return true;
    }

    for (const ShadowMaskLayerFrame2D& layer : frame_.shadowLayers) {
        if (layer.layer < 0 ||
            layer.layer >= static_cast<int>(kMaxShadowLights2D)) {
            result.shadowFallback = true;
            continue;
        }
        glFramebufferTextureLayer(
            GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowTextureArray_, 0,
            layer.layer);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
                GL_FRAMEBUFFER_COMPLETE ||
            ConsumeGlErrors()) {
            result.shadowFallback = true;
            continue;
        }

        glClearColor(layer.fullCover ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        int layerCasterDrawCount = 0;
        if (!layer.fullCover) {
            for (const ShadowCasterGeometry2D& caster : layer.casters) {
                if (caster.fullCover) {
                    glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    break;
                }
                if (!caster.HasTriangles() || caster.vertices.empty()) continue;
                glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(
                        caster.vertices.size() * sizeof(Vector2)),
                    caster.vertices.data(), GL_STREAM_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
                glBufferData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(
                        caster.indices.size() * sizeof(std::uint32_t)),
                    caster.indices.data(), GL_STREAM_DRAW);
                glDrawElements(
                    GL_TRIANGLES, static_cast<GLsizei>(caster.indices.size()),
                    GL_UNSIGNED_INT, nullptr);
                ++layerCasterDrawCount;
            }
        }
        if (ConsumeGlErrors()) {
            result.shadowFallback = true;
            continue;
        }
        result.shadowCasterDrawCount += layerCasterDrawCount;
        shadowLayerAvailable_[static_cast<std::size_t>(layer.layer)] = true;
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
    context.shadowLayerAvailable = shadowLayerAvailable_;
    context.targetSize = targetSize;
    context.viewport = topLeftViewport;
    context.viewportOriginBottomLeft = {
        static_cast<float>(topLeftViewport.x),
        static_cast<float>(
            targetSize.height - (topLeftViewport.y + topLeftViewport.height))};
    return context;
}

void LightingPipeline2D::BindFrameUniforms(
    Shader& shader, const LightingRenderContext2D& context) {
    if (!context.frame) return;
    const LightingFrame2D& frame = *context.frame;
    shader.SetVec4("uAmbientColor", frame.ambientColor.r, frame.ambientColor.g,
                   frame.ambientColor.b, frame.ambientColor.a);
    shader.SetFloat("uAmbientIntensity", frame.ambientIntensity);
    const int lightCount = static_cast<int>(
        std::min<std::size_t>(frame.lights.size(), kMaxPointLights2D));
    shader.SetInt("uLightCount", lightCount);
    shader.SetVec2(
        "uShadowViewportOrigin", context.viewportOriginBottomLeft.x,
        context.viewportOriginBottomLeft.y);
    shader.SetVec2("uShadowViewportSize",
                   static_cast<float>(context.viewport.width),
                   static_cast<float>(context.viewport.height));
    shader.SetInt("uShadowMasks", 2);

    for (int index = 0; index < static_cast<int>(kMaxPointLights2D); ++index) {
        const std::string suffix = "[" + std::to_string(index) + "]";
        if (index >= lightCount) {
            shader.SetInt(("uLightShadowLayer" + suffix).c_str(), -1);
            continue;
        }
        const LightingLightSnapshot2D& light =
            frame.lights[static_cast<std::size_t>(index)];
        shader.SetVec2(("uLightPosition" + suffix).c_str(),
                       light.position.x, light.position.y);
        shader.SetVec4(("uLightColor" + suffix).c_str(),
                       light.color.r, light.color.g, light.color.b, light.color.a);
        shader.SetFloat(("uLightIntensity" + suffix).c_str(), light.intensity);
        shader.SetFloat(("uLightRadius" + suffix).c_str(), light.radius);
        shader.SetFloat(("uLightHeight" + suffix).c_str(), light.height);
        shader.SetFloat(("uLightFalloff" + suffix).c_str(), light.falloff);
        shader.SetUInt(("uLightAffectMask" + suffix).c_str(), light.affectMask);
        int shadowLayer = light.shadowLayer;
        if (shadowLayer < 0 ||
            shadowLayer >= static_cast<int>(kMaxShadowLights2D) ||
            !context.shadowLayerAvailable[
                static_cast<std::size_t>(shadowLayer)]) {
            shadowLayer = -1;
        }
        shader.SetInt(("uLightShadowLayer" + suffix).c_str(), shadowLayer);
    }
}

} // namespace molga
