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
    
};

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
