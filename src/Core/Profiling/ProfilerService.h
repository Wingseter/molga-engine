#pragma once

#include "Core/Profiling/FrameProfile.h"
#include <cstddef>
#include <vector>

namespace molga {

// 고정 용량 in-process ring buffer. 가장 오래된 프레임을 덮어쓴다.
// 비활성 시 PushFrame은 즉시 반환한다(near-zero overhead).
class ProfilerService {
public:
    explicit ProfilerService(size_t capacity = 240);

    // 프로세스 전역 인스턴스(메인 루프·서브시스템이 공유).
    static ProfilerService& Get();

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const  { return enabled_; }

    void PushFrame(FrameProfile frame);
    void Clear();

    size_t Size() const     { return count_; }
    size_t Capacity() const { return buffer_.size(); }

    // At(0) = 가장 오래된 보존 프레임, At(Size()-1) = 최신. 범위 밖이면 nullptr.
    const FrameProfile* At(size_t i) const;
    const FrameProfile* Latest() const;
    const FrameProfile* SlowestFrame() const;

    // 메인 루프가 프레임마다 비우고 채우는 누적기. 서브시스템 스코프가 여기에 쌓는다.
    FrameAccumulator& Frame() { return frame_; }

    // 현재 누적기를 FrameProfile로 굳혀 ring buffer에 넣고 누적기를 리셋한다.
    void EndFrame(unsigned long long frameIndex, float dt,
                   const FrameCounters& counters, const RenderStats& render);

    // Core-safe asset load counter. Core subsystems (e.g. TextureManager) increment this.
    int& AssetLoadCounter() { return assetLoadCounter_; }

private:
    std::vector<FrameProfile> buffer_;
    size_t head_ = 0;    // 다음에 쓸 슬롯
    size_t count_ = 0;   // 보존 중인 프레임 수
    bool   enabled_ = true;
    FrameAccumulator frame_;
    int assetLoadCounter_ = 0;
};

} // namespace molga
