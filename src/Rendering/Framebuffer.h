#pragma once

enum class FramebufferColorFormat {
    SRGBA8,
    RGBA16F,
};

enum class FramebufferTextureFilter {
    Nearest,
    Linear,
};

struct FramebufferSpecification {
    FramebufferColorFormat colorFormat = FramebufferColorFormat::SRGBA8;
    bool depthStencil = true;
    FramebufferTextureFilter filter = FramebufferTextureFilter::Linear;

    bool operator==(const FramebufferSpecification& other) const {
        return colorFormat == other.colorFormat &&
               depthStencil == other.depthStencil && filter == other.filter;
    }
    bool operator!=(const FramebufferSpecification& other) const {
        return !(*this == other);
    }
};

// 오프스크린 렌더링을 위한 OpenGL Framebuffer Object (FBO).
// 컬러 어태치먼트(텍스처)를 하나 유지하며 동적으로 리사이즈할 수 있다.
// ImGui::Image()에 ColorTexture()를 전달해 패널에 출력한다.
class Framebuffer {
public:
    explicit Framebuffer(FramebufferSpecification specification = {});
    ~Framebuffer();

    // 비복사
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // FBO 초기화 (w×h 크기의 컬러 텍스처 생성)
    bool Init(int width, int height);
    bool Init(int width, int height, FramebufferSpecification specification);

    // 현재 크기와 다를 때 FBO·텍스처를 재생성
    // Returns false without disturbing the last valid allocation when the
    // requested size is invalid, exceeds GL limits, or allocation fails.
    bool Resize(int width, int height);
    bool Resize(int width, int height, FramebufferSpecification specification);

    // 이 FBO를 현재 렌더 타깃으로 바인드
    void Bind();

    // 기본 프레임버퍼(0)로 되돌림
    void Unbind();

    // 컬러 어태치먼트 텍스처 ID
    unsigned int ColorTexture() const { return colorTexture_; }
    // Low-level presentation paths may bind this as a read framebuffer. Callers
    // must preserve the binding state around direct use.
    unsigned int Id() const { return fbo_; }

    int Width()  const { return width_; }
    int Height() const { return height_; }

    bool IsValid() const { return fbo_ != 0; }
    const FramebufferSpecification& Specification() const { return specification_; }

private:
    void Cleanup();

    unsigned int fbo_          = 0;
    unsigned int colorTexture_ = 0;
    unsigned int rbo_          = 0;  // 깊이/스텐실 렌더버퍼 (선택적)
    int    width_        = 0;
    int    height_       = 0;
    FramebufferSpecification specification_{};

    struct SavedBindingState {
        int drawFramebuffer = 0;
        int readFramebuffer = 0;
        int viewport[4] = {0, 0, 0, 0};
        int scissorBox[4] = {0, 0, 0, 0};
        unsigned char scissorEnabled = 0;
        unsigned char framebufferSrgbEnabled = 0;
        bool valid = false;
    } saved_;
};

// Guarantees restoration when rendering throws (non-Script engine components
// intentionally remain fail-loud).
class ScopedFramebufferBinding {
public:
    explicit ScopedFramebufferBinding(Framebuffer& framebuffer)
        : framebuffer_(&framebuffer) {
        framebuffer_->Bind();
    }
    ~ScopedFramebufferBinding() { framebuffer_->Unbind(); }

    ScopedFramebufferBinding(const ScopedFramebufferBinding&) = delete;
    ScopedFramebufferBinding& operator=(const ScopedFramebufferBinding&) = delete;

private:
    Framebuffer* framebuffer_;
};
