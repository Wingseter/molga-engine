#pragma once

#include <vector>
#include <array>
#include "Rendering/RenderQueue.h"

class Renderer;

namespace molga {

struct LightingRenderContext2D;

class SpriteBatcher {
public:
    static constexpr size_t MAX_SPRITES = 2048;

    SpriteBatcher();
    ~SpriteBatcher();

    void Init();
    void Shutdown();

    void Begin(Renderer* renderer,
               const LightingRenderContext2D* lightingContext = nullptr);
    void DrawSprite(const std::array<Vertex2D, 4>& vertices, const BatchKey& key);
    void DrawGeometry(const std::vector<Vertex2D>& vertices, const BatchKey& key);
    void DrawIndexedGeometry(const std::vector<Vertex2D>& vertices,
                             const std::vector<std::uint32_t>& indices,
                             const BatchKey& key);
    static size_t RequiredBatchCount(size_t quadCount) {
        return quadCount == 0 ? 0 : (quadCount + MAX_SPRITES - 1) / MAX_SPRITES;
    }
    void Flush();
    void End();

private:
    Renderer* renderer_ = nullptr;
    const LightingRenderContext2D* lightingContext_ = nullptr;
    BatchKey activeKey_;
    bool hasActiveKey_ = false;

    std::vector<Vertex2D> vertices_;
    size_t spriteCount_ = 0;
};

} // namespace molga
