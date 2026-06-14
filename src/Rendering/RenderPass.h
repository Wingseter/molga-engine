#pragma once

#include "Rendering/Renderer.h"

namespace molga {

// 프레임 단위 렌더 패스 경계를 소유하는 RAII 가드.
// 스코프 진입 시 Begin, 탈출 시 End를 호출한다.
class RenderPass {
public:
    RenderPass(Renderer& renderer, Shader* shader, Camera2D* camera = nullptr)
        : renderer_(renderer) {
        renderer_.Begin(shader, camera);
    }
    ~RenderPass() { renderer_.End(); }

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

private:
    Renderer& renderer_;
};

} // namespace molga
