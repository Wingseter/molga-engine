#pragma once

#include "Rendering/RenderQueue.h"
#include "Rendering/SpriteBatcher.h"

class Renderer;
class Camera2D;

namespace molga {

struct LightingRenderContext2D;

class RenderSystem2D {
public:
    static RenderSystem2D& Get() {
        static RenderSystem2D instance;
        return instance;
    }

    void Init();
    void Shutdown();

    void Render(RenderQueue& queue, Renderer* renderer, Camera2D* camera,
                const LightingRenderContext2D* lightingContext = nullptr);

private:
    RenderSystem2D() = default;
    ~RenderSystem2D() = default;

    SpriteBatcher batcher_;
    bool initialized_ = false;
};

} // namespace molga
