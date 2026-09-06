#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "../Common/linmath.h"
#include "Common/Types.h"
#include "Rendering/BlendMode.h"
#include "Rendering/GraphicsDevice.h"

class Renderer;
namespace molga {

struct SortKey {
    int cameraPass = 0;
    int sortingLayer = 0;
    int sortingOrder = 0;
    float depthOrYSort = 0.0f;
    uint64_t submissionIndex = 0;

    static float NormalizedDepth(float value) noexcept {
        return std::isfinite(value) ? value : 0.0f;
    }

    bool operator<(const SortKey& other) const {
        if (cameraPass != other.cameraPass) return cameraPass < other.cameraPass;
        if (sortingLayer != other.sortingLayer) return sortingLayer < other.sortingLayer;
        if (sortingOrder != other.sortingOrder) return sortingOrder < other.sortingOrder;
        const float depth = NormalizedDepth(depthOrYSort);
        const float otherDepth = NormalizedDepth(other.depthOrYSort);
        if (depth != otherDepth) return depth < otherDepth;
        return submissionIndex < other.submissionIndex;
    }

    bool operator==(const SortKey& other) const {
        return cameraPass == other.cameraPass &&
               sortingLayer == other.sortingLayer &&
               sortingOrder == other.sortingOrder &&
               NormalizedDepth(depthOrYSort) ==
                   NormalizedDepth(other.depthOrYSort) &&
               submissionIndex == other.submissionIndex;
    }
};

struct BatchKey {
    struct ExtraTexture {
        bool vertexStage = false;
        std::uint32_t slot = 0;
        TextureHandle texture;
        SamplerHandle sampler;
        std::uint64_t stableId = 0;
    };

    std::string shaderName;
    std::uint64_t shaderRevision = 0;
    TextureHandle texture;
    SamplerHandle textureSampler;
    std::uint64_t textureStableId = 0;
    TextureHandle normalTexture;
    SamplerHandle normalSampler;
    std::uint64_t normalTextureStableId = 0;
    BlendMode blendMode = BlendMode::Alpha;
    bool isBatchable = true;
    bool lit = false;
    std::uint32_t receiverLayer = 0;
    float normalStrength = 1.0f;
    std::uint64_t materialId = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> materialParameters;
    std::shared_ptr<const std::vector<ExtraTexture>> materialTextures;

    bool operator<(const BatchKey& other) const {
        if (isBatchable != other.isBatchable) return isBatchable < other.isBatchable;
        if (lit != other.lit) return lit < other.lit;
        if (shaderName != other.shaderName) return shaderName < other.shaderName;
        if (shaderRevision != other.shaderRevision) return shaderRevision < other.shaderRevision;
        if (textureStableId != other.textureStableId) return textureStableId < other.textureStableId;
        if (normalTextureStableId != other.normalTextureStableId) {
            return normalTextureStableId < other.normalTextureStableId;
        }
        if (receiverLayer != other.receiverLayer) return receiverLayer < other.receiverLayer;
        if (normalStrength != other.normalStrength) return normalStrength < other.normalStrength;
        if (materialId != other.materialId) return materialId < other.materialId;
        return blendMode < other.blendMode;
    }

    bool operator==(const BatchKey& other) const {
        return isBatchable == other.isBatchable &&
               lit == other.lit &&
               shaderName == other.shaderName &&
               shaderRevision == other.shaderRevision &&
               textureStableId == other.textureStableId &&
               normalTextureStableId == other.normalTextureStableId &&
               receiverLayer == other.receiverLayer &&
               normalStrength == other.normalStrength &&
               materialId == other.materialId &&
               blendMode == other.blendMode;
    }

    bool operator!=(const BatchKey& other) const {
        return !(*this == other);
    }
};

struct Vertex2D {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct RenderCommand {
    SortKey sortKey;
    BatchKey batchKey;
    
    bool isBatchableSprite = false;
    std::array<Vertex2D, 4> vertices;
    // Chunk/particle producers publish immutable shared geometry. Rebuilding a
    // dirty cache can replace its shared_ptr without invalidating commands that
    // were already submitted for the current frame.
    std::shared_ptr<const std::vector<Vertex2D>> geometry;
    std::shared_ptr<const std::vector<std::uint32_t>> geometryIndices;
    std::optional<AABB> worldBounds;

    // ── Task 6.3: 이 명령이 읽는 GPU page의 이름과 지분 ─────────────────────
    // 텍스트 명령은 GlyphHandle::pageIdentity와 pageLifetime을 짝으로 복사해
    // 오도록 되어 있다. 이름과 지분이 함께 실리는 이유는 반납이 page 단위이기
    // 때문이다: 한 프레임 안에서 같은 page를 가리키는 명령은 수백 개가 될 수
    // 있지만 되돌려줄 것은 page 하나이고, 그 중복 제거의 키가 정체성이다.
    //
    // 아직 이 두 필드를 채우는 프로덕션 생산자는 없다. TextRenderer::CollectText
    // 는 Task 8.2가 지울 레거시 FontAtlasCache 위에 있어서 GlyphHandle을 아예
    // 만들지 않는다. 그 두 필드를 실제로 싣는 것은 Task 8.2의 소비자 이전이고,
    // 그때까지 여기 실려 오는 값은 테스트가 넣은 것뿐이다.
    //
    // 두 필드 모두 BatchKey/SortKey 밖에 있다. 안에 넣으면 같은 atlas
    // 텍스처를 쓰는 glyph들이 page마다 다른 batch로 쪼개져, 수명 회계가
    // draw call 수를 바꾸게 된다.
    std::uint64_t resourceLifetimeIdentity = 0;
    std::shared_ptr<const void> resourceLifetime;
};

// ── Task 6.3: 수집한 명령을 실제로 제출하는 유일한 루프 ─────────────────────
// 명령이 가리키는 page의 지분은 "그 명령이 실제로 그려질 때"만 프레임으로
// 넘어간다. 컬링되어 그려지지 않는 명령까지 붙들면, fence가 신호할 때까지
// 살아 있어야 할 이유가 없는 page가 예산을 먹는다.
//
// 템플릿인 이유는 하나다. "실제 제출 직전"은 값이 아니라 순서라서, 붙듦과
// 그리기가 한 로그에 순서대로 남는 것 말고는 관찰할 방법이 없는데,
// SpriteBatcher는 GPU 장치 없이 서지 못한다. RenderSystem2D는 진짜
// Renderer/SpriteBatcher로, test_render_queue는 호출을 받아 적는 대역으로
// 같은 코드를 인스턴스화한다 — 루프가 두 벌이면 테스트가 재는 것은 프로덕션이
// 아니라 그 사본이 된다.
template <typename RendererLike>
void RetainCommandResourceLifetime(RendererLike& renderer,
                                   const RenderCommand& command) {
    // 널 토큰은 지분이 없다는 뜻이다(공백 glyph와 포화 tofu가 그 모양이다).
    if (!command.resourceLifetime) return;
    // 여기서 걸러 내는 것은 널 토큰뿐이다. Renderer::RetainUntilFrameComplete
    // 는 정체성이 0이거나 활성 프레임이 없으면 std::logic_error를 던지고, 이
    // 호출은 SpriteBatcher::Begin/End 사이에 있으므로 그 예외는 batcher를
    // 열어 둔 채 RenderSystem2D::Render 밖으로 나간다. 오늘은 닿을 수 없다 —
    // resourceLifetime을 채우는 프로덕션 생산자가 없다. Task 8.2가 소비자를
    // 옮기는 순간 닿을 수 있게 되므로, 그때 프레임 밖 Render 호출자
    // (ParticleSystem/SpriteRenderer/MarrowRenderer의 RenderSprite)를 함께
    // 확인해야 한다.
    renderer.RetainUntilFrameComplete(command.resourceLifetimeIdentity,
                                      command.resourceLifetime);
}

template <typename RendererLike, typename BatcherLike>
void SubmitVisibleCommands(const std::vector<RenderCommand>& commands,
                           const std::optional<AABB>& cameraBounds,
                           RendererLike& renderer, BatcherLike& batcher) {
    for (const RenderCommand& cmd : commands) {
        if (cameraBounds && cmd.worldBounds &&
            !cameraBounds->Intersects(*cmd.worldBounds)) {
            continue;
        }
        renderer.Stats().submittedCommands++;

        if (cmd.geometry) {
            if (cmd.geometryIndices) {
                RetainCommandResourceLifetime(renderer, cmd);
                batcher.DrawIndexedGeometry(*cmd.geometry,
                                            *cmd.geometryIndices,
                                            cmd.batchKey);
                continue;
            }
            if (!cmd.batchKey.isBatchable) batcher.Flush();
            RetainCommandResourceLifetime(renderer, cmd);
            batcher.DrawGeometry(*cmd.geometry, cmd.batchKey);
            if (!cmd.batchKey.isBatchable) batcher.Flush();
        } else if (cmd.isBatchableSprite) {
            if (!cmd.batchKey.isBatchable) batcher.Flush();
            RetainCommandResourceLifetime(renderer, cmd);
            batcher.DrawSprite(cmd.vertices, cmd.batchKey);
            if (!cmd.batchKey.isBatchable) batcher.Flush();
        }
    }
}

class RenderQueue {
public:
    RenderQueue() = default;
    
    void Submit(const RenderCommand& cmd) {
        if (viewBounds_ && cmd.worldBounds && !viewBounds_->Intersects(*cmd.worldBounds)) {
            ++culledCommands_;
            return;
        }
        auto mutableCmd = cmd;
        mutableCmd.sortKey.submissionIndex = nextSubmissionIndex_++;
        commands_.push_back(std::move(mutableCmd));
    }
    
    void Sort() {
        std::sort(commands_.begin(), commands_.end(), [](const RenderCommand& a, const RenderCommand& b) {
            return a.sortKey < b.sortKey;
        });
    }
    
    void Clear() {
        commands_.clear();
        nextSubmissionIndex_ = 0;
        culledCommands_ = 0;
    }
    
    const std::vector<RenderCommand>& GetCommands() const { return commands_; }
    bool HasLitReceivers() const {
        return std::any_of(commands_.begin(), commands_.end(),
            [](const RenderCommand& command) { return command.batchKey.lit; });
    }
    void ForceUnlit() {
        for (auto& command : commands_) {
            command.batchKey.lit = false;
            command.batchKey.normalTexture = {};
            command.batchKey.normalSampler = {};
            command.batchKey.normalTextureStableId = 0;
            command.batchKey.receiverLayer = 0;
            command.batchKey.normalStrength = 1.0f;
        }
    }
    void SetViewBounds(const AABB& bounds) { viewBounds_ = bounds; }
    void ClearViewBounds() { viewBounds_.reset(); }
    const std::optional<AABB>& GetViewBounds() const { return viewBounds_; }
    std::size_t CulledCommandCount() const { return culledCommands_; }
    
private:
    std::vector<RenderCommand> commands_;
    uint64_t nextSubmissionIndex_ = 0;
    std::optional<AABB> viewBounds_;
    std::size_t culledCommands_ = 0;
};

} // namespace molga
