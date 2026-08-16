#include "Framebuffer.h"
#include "Common/Log.h"
#include <glad/glad.h>
#include <algorithm>

Framebuffer::Framebuffer(FramebufferSpecification specification)
    : specification_(specification) {}

Framebuffer::~Framebuffer() {
    if (saved_.valid) Unbind();
    Cleanup();
}

void Framebuffer::Cleanup() {
    if (colorTexture_) { glDeleteTextures(1, &colorTexture_); colorTexture_ = 0; }
    if (rbo_)          { glDeleteRenderbuffers(1, &rbo_); rbo_ = 0; }
    if (fbo_)          { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    width_ = height_ = 0;
}

bool Framebuffer::Init(int width, int height) {
    return Init(width, height, specification_);
}

bool Framebuffer::Init(int width, int height,
                       FramebufferSpecification specification) {
    if (width <= 0 || height <= 0) return false;
    if (saved_.valid) {
        Log::Error("Framebuffer", "Cannot recreate a framebuffer while it is bound");
        return false;
    }

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    GLint maximum = maxTextureSize;
    if (specification.depthStencil) {
        GLint maxRenderbufferSize = 0;
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
        maximum = std::min(maxTextureSize, maxRenderbufferSize);
    }
    if (maximum <= 0 || width > maximum || height > maximum) {
        Log::Error("Framebuffer", "Rejected framebuffer size " +
            std::to_string(width) + "x" + std::to_string(height) +
            " (device maximum " + std::to_string(maximum) + ")");
        return false;
    }

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousTexture = 0;
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

    GLuint newFbo = 0;
    GLuint newColorTexture = 0;
    GLuint newRbo = 0;
    glGenFramebuffers(1, &newFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, newFbo);

    // 컬러 텍스처
    glGenTextures(1, &newColorTexture);
    glBindTexture(GL_TEXTURE_2D, newColorTexture);
    const GLint internalFormat = specification.colorFormat ==
            FramebufferColorFormat::RGBA16F
        ? GL_RGBA16F : GL_SRGB8_ALPHA8;
    const GLenum dataType = specification.colorFormat ==
            FramebufferColorFormat::RGBA16F
        ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 GL_RGBA, dataType, nullptr);
    const GLint filter = specification.filter == FramebufferTextureFilter::Nearest
        ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           newColorTexture, 0);

    if (specification.depthStencil) {
        glGenRenderbuffers(1, &newRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, newRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, newRbo);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      static_cast<GLuint>(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER,
                      static_cast<GLuint>(previousReadFramebuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Log::Error("Framebuffer", "Framebuffer incomplete: status=" + std::to_string(status));
        if (newColorTexture) glDeleteTextures(1, &newColorTexture);
        if (newRbo) glDeleteRenderbuffers(1, &newRbo);
        if (newFbo) glDeleteFramebuffers(1, &newFbo);
        return false;
    }

    Cleanup();
    fbo_ = newFbo;
    colorTexture_ = newColorTexture;
    rbo_ = newRbo;
    width_  = width;
    height_ = height;
    specification_ = specification;
    return true;
}

bool Framebuffer::Resize(int width, int height) {
    if (width == width_ && height == height_ && IsValid()) return true;
    if (width <= 0 || height <= 0) return false;
    return Init(width, height);
}

bool Framebuffer::Resize(int width, int height,
                         FramebufferSpecification specification) {
    if (width == width_ && height == height_ && IsValid() &&
        specification == specification_) return true;
    if (width <= 0 || height <= 0) return false;
    return Init(width, height, specification);
}

void Framebuffer::Bind() {
    if (!fbo_ || saved_.valid) return;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved_.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved_.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, saved_.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, saved_.scissorBox);
    saved_.scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    saved_.framebufferSrgbEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    saved_.valid = true;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    if (specification_.colorFormat == FramebufferColorFormat::SRGBA8)
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width_, height_);
}

void Framebuffer::Unbind() {
    if (!saved_.valid) return;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      static_cast<GLuint>(saved_.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER,
                      static_cast<GLuint>(saved_.readFramebuffer));
    glViewport(saved_.viewport[0], saved_.viewport[1],
               saved_.viewport[2], saved_.viewport[3]);
    glScissor(saved_.scissorBox[0], saved_.scissorBox[1],
              saved_.scissorBox[2], saved_.scissorBox[3]);
    if (saved_.scissorEnabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    if (saved_.framebufferSrgbEnabled) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);
    saved_.valid = false;
}
