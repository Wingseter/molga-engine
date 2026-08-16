#include "Rendering/SpriteBatcher.h"

#include "Core/Profiling/ProfileScope.h"
#include "Rendering/LightingPipeline2D.h"
#include "Rendering/Renderer.h"

namespace molga {

SpriteBatcher::SpriteBatcher() { vertices_.reserve(MAX_SPRITES * 4U); }

SpriteBatcher::~SpriteBatcher() { Shutdown(); }

void SpriteBatcher::Init() {}

void SpriteBatcher::Shutdown() {
    vertices_.clear();
    renderer_ = nullptr;
    lightingContext_ = nullptr;
    spriteCount_ = 0;
    hasActiveKey_ = false;
}

void SpriteBatcher::Begin(Renderer* renderer,
                          const LightingRenderContext2D* lightingContext) {
    renderer_ = renderer;
    lightingContext_ = lightingContext;
    vertices_.clear();
    spriteCount_ = 0;
    hasActiveKey_ = false;
}

void SpriteBatcher::DrawSprite(const std::array<Vertex2D, 4>& vertices,
                               const BatchKey& key) {
    if (!renderer_) return;
    if (spriteCount_ >= MAX_SPRITES || (hasActiveKey_ && activeKey_ != key)) {
        ++renderer_->Stats().batchBreaks;
        Flush();
    }
    if (!hasActiveKey_) {
        activeKey_ = key;
        hasActiveKey_ = true;
    }
    vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
    ++spriteCount_;
    ++renderer_->Stats().submittedSprites;
}

void SpriteBatcher::DrawGeometry(const std::vector<Vertex2D>& vertices,
                                 const BatchKey& key) {
    if (vertices.size() % 4U != 0U) return;
    for (std::size_t offset = 0; offset < vertices.size(); offset += 4U) {
        DrawSprite({vertices[offset], vertices[offset + 1U],
                    vertices[offset + 2U], vertices[offset + 3U]}, key);
    }
}

void SpriteBatcher::DrawIndexedGeometry(
    const std::vector<Vertex2D>& vertices,
    const std::vector<std::uint32_t>& indices, const BatchKey& key) {
    if (!renderer_ || vertices.empty() || indices.empty()) return;
    Flush();
    std::string error;
    if (renderer_->SubmitGeometry(vertices, indices, key, lightingContext_,
                                  &error)) {
        ++renderer_->Stats().batches;
        ++renderer_->Stats().batchFlushes;
    }
}

void SpriteBatcher::Flush() {
    if (spriteCount_ == 0U || !renderer_ || !hasActiveKey_) return;
    MOLGA_PROFILE_SCOPE("SpriteBatcher.Flush", ProfileCategory::Rendering);
    std::string error;
    if (!renderer_->SubmitBatch(vertices_, activeKey_, lightingContext_, &error)) {
        // A failed packet does not poison later batches; the owning camera can
        // report its local fallback/validation error.
    } else {
        auto& stats = renderer_->Stats();
        ++stats.batches;
        ++stats.batchFlushes;
        stats.maxSpritesPerBatch = std::max(
            stats.maxSpritesPerBatch, static_cast<int>(spriteCount_));
    }
    vertices_.clear();
    spriteCount_ = 0;
    hasActiveKey_ = false;
}

void SpriteBatcher::End() {
    Flush();
    renderer_ = nullptr;
    lightingContext_ = nullptr;
}

} // namespace molga
