#include "Framebuffer.h"
#include "Common/Log.h"

Framebuffer::Framebuffer() = default;

Framebuffer::~Framebuffer() {
    Cleanup();
}

void Framebuffer::Cleanup() {
    if (colorTexture_) { glDeleteTextures(1, &colorTexture_); colorTexture_ = 0; }
    if (rbo_)          { glDeleteRenderbuffers(1, &rbo_); rbo_ = 0; }
    if (fbo_)          { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    width_ = height_ = 0;
}

bool Framebuffer::Init(int width, int height) {
    if (width <= 0 || height <= 0) return false;

    Cleanup();

    // FBO
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // 컬러 텍스처
    glGenTextures(1, &colorTexture_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

    // 깊이/스텐실 렌더버퍼 (2D에서도 없으면 일부 드라이버에서 completeness 실패)
    glGenRenderbuffers(1, &rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Log::Error("Framebuffer", "Framebuffer incomplete: status=" + std::to_string(status));
        Cleanup();
        return false;
    }

    width_  = width;
    height_ = height;
    return true;
}

void Framebuffer::Resize(int width, int height) {
    if (width == width_ && height == height_) return;
    if (width <= 0 || height <= 0) return;
    Init(width, height);
}

void Framebuffer::Bind() {
    if (fbo_) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, width_, height_);
    }
}

void Framebuffer::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
