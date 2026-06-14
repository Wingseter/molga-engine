#pragma once

#include <glad/glad.h>

// 오프스크린 렌더링을 위한 OpenGL Framebuffer Object (FBO).
// 컬러 어태치먼트(텍스처)를 하나 유지하며 동적으로 리사이즈할 수 있다.
// ImGui::Image()에 ColorTexture()를 전달해 패널에 출력한다.
class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();

    // 비복사
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // FBO 초기화 (w×h 크기의 컬러 텍스처 생성)
    bool Init(int width, int height);

    // 현재 크기와 다를 때 FBO·텍스처를 재생성
    void Resize(int width, int height);

    // 이 FBO를 현재 렌더 타깃으로 바인드
    void Bind();

    // 기본 프레임버퍼(0)로 되돌림
    void Unbind();

    // 컬러 어태치먼트 텍스처 ID
    GLuint ColorTexture() const { return colorTexture_; }

    int Width()  const { return width_; }
    int Height() const { return height_; }

    bool IsValid() const { return fbo_ != 0; }

private:
    void Cleanup();

    GLuint fbo_          = 0;
    GLuint colorTexture_ = 0;
    GLuint rbo_          = 0;  // 깊이/스텐실 렌더버퍼 (선택적)
    int    width_        = 0;
    int    height_       = 0;
};
