#pragma once

#include <vector>
#include <functional>
#include <array>
#include <algorithm>
#include "../Common/linmath.h"
#include "Rendering/BlendMode.h"

class Renderer;
class Shader;
class Texture;

namespace molga {

struct SortKey {
    int cameraPass = 0;
    int sortingLayer = 0;
    int sortingOrder = 0;
    float depthOrYSort = 0.0f;
    uint64_t submissionIndex = 0;

    bool operator<(const SortKey& other) const {
        if (cameraPass != other.cameraPass) return cameraPass < other.cameraPass;
        if (sortingLayer != other.sortingLayer) return sortingLayer < other.sortingLayer;
        if (sortingOrder != other.sortingOrder) return sortingOrder < other.sortingOrder;
        if (depthOrYSort != other.depthOrYSort) return depthOrYSort < other.depthOrYSort;
        return submissionIndex < other.submissionIndex;
    }

    bool operator==(const SortKey& other) const {
        return cameraPass == other.cameraPass &&
               sortingLayer == other.sortingLayer &&
               sortingOrder == other.sortingOrder &&
               depthOrYSort == other.depthOrYSort &&
               submissionIndex == other.submissionIndex;
    }
};

struct BatchKey {
    Shader* shader = nullptr;
    Texture* texture = nullptr;
    BlendMode blendMode = BlendMode::Alpha;
    bool isBatchable = true;

    bool operator<(const BatchKey& other) const {
        if (isBatchable != other.isBatchable) return isBatchable < other.isBatchable;
        if (shader != other.shader) return shader < other.shader;
        if (texture != other.texture) return texture < other.texture;
        return blendMode < other.blendMode;
    }

    bool operator==(const BatchKey& other) const {
        return isBatchable == other.isBatchable &&
               shader == other.shader &&
               texture == other.texture &&
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
    
    std::function<void(Renderer*)> fallbackRender;
};

class RenderQueue {
public:
    RenderQueue() = default;
    
    void Submit(const RenderCommand& cmd) {
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
    }
    
    const std::vector<RenderCommand>& GetCommands() const { return commands_; }
    
private:
    std::vector<RenderCommand> commands_;
    uint64_t nextSubmissionIndex_ = 0;
};

} // namespace molga
