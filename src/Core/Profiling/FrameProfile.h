#pragma once

#include "Core/Profiling/ProfilerCategory.h"
#include <array>
#include <string>
#include <vector>

namespace molga {

// 한 프레임에서 닫힌 단일 스코프의 기록.
struct ScopeRecord {
    std::string     name;
    ProfileCategory category = ProfileCategory::Other;
    long long       nanos = 0;   // 이 스코프가 머문 총 시간(자식 포함)
    int             depth = 0;   // 0 = 최상위
};

// 프레임당 수량 카운터(갭 분석 §7: draw calls, sprites, particles, text, tile chunks,
// asset loads, scripts, physics).
struct FrameCounters {
    int drawCalls = 0;
    int sprites = 0;
    int particles = 0;
    int text = 0;
    int tileChunks = 0;
    int assetLoads = 0;
    int scripts = 0;
    int physics = 0;
    void Reset() { *this = FrameCounters{}; }
};

// 렌더러 내부 통계(갭 분석 §7: draw calls, batches, texture binds, shader switches,
// FBO resizes). drawCalls는 FrameCounters와 중복 노출되되 여기서는 렌더러가 직접 채운다.
struct RenderStats {
    int drawCalls = 0;
    int batches = 0;
    int textureBinds = 0;
    int shaderSwitches = 0;
    int fboResizes = 0;
    int outputCameraPasses = 0;
    int postProcessPasses = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;

    int submittedSprites = 0;
    int submittedCommands = 0;
    int batchFlushes = 0;
    int batchBreaks = 0;
    int maxSpritesPerBatch = 0;
    size_t verticesUploadedBytes = 0;
    long long queueSortNanos = 0;

    void Reset() { *this = RenderStats{}; }
};

// 한 프레임의 완성된 프로파일.
struct FrameProfile {
    unsigned long long       frameIndex = 0;
    float                    dt = 0.0f;   // 초
    std::vector<ScopeRecord> scopes;
    FrameCounters            counters;
    RenderStats              render;
    std::array<long long, static_cast<size_t>(ProfileCategory::Count)> categoryNanos{};

    long long CategoryNanos(ProfileCategory c) const {
        return categoryNanos[static_cast<size_t>(c)];
    }
};

// 한 프레임 동안 스코프를 모으는 누적기. ProfileScope가 push/pop한다.
class FrameAccumulator {
public:
    void Reset() {
        records_.clear();
        depth_ = 0;
        catNanos_.fill(0);
    }

    // ProfileScope 진입 시 호출. 현재 깊이를 반환한다.
    int OnEnter() { return depth_++; }

    // ProfileScope 종료 시 호출. 닫힌 스코프를 기록한다.
    void OnExit(std::string name, ProfileCategory cat, long long nanos, int depth) {
        depth_ = depth;  // 진입 시 받았던 깊이로 복원
        catNanos_[static_cast<size_t>(cat)] += nanos;
        records_.push_back({std::move(name), cat, nanos, depth});
    }

    const std::vector<ScopeRecord>& Records() const { return records_; }
    int Depth() const { return depth_; }
    long long CategoryNanos(ProfileCategory c) const {
        return catNanos_[static_cast<size_t>(c)];
    }

private:
    std::vector<ScopeRecord> records_;
    int depth_ = 0;
    std::array<long long, static_cast<size_t>(ProfileCategory::Count)> catNanos_{};
};

} // namespace molga
