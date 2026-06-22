#include "Core/Profiling/ProfilerService.h"

namespace molga {

ProfilerService::ProfilerService(size_t capacity)
    : buffer_(capacity == 0 ? 1 : capacity) {}

ProfilerService& ProfilerService::Get() {
    static ProfilerService instance(240);  // 약 4초(60fps) 분량
    return instance;
}

void ProfilerService::PushFrame(FrameProfile frame) {
    if (!enabled_) return;
    buffer_[head_] = std::move(frame);
    head_ = (head_ + 1) % buffer_.size();
    if (count_ < buffer_.size()) ++count_;
}

void ProfilerService::Clear() {
    head_ = 0;
    count_ = 0;
}

const FrameProfile* ProfilerService::At(size_t i) const {
    if (i >= count_) return nullptr;
    // 가장 오래된 슬롯 = head_ - count_ (모듈러)
    size_t start = (head_ + buffer_.size() - count_) % buffer_.size();
    return &buffer_[(start + i) % buffer_.size()];
}

const FrameProfile* ProfilerService::Latest() const {
    if (count_ == 0) return nullptr;
    return At(count_ - 1);
}

const FrameProfile* ProfilerService::SlowestFrame() const {
    const FrameProfile* slow = nullptr;
    for (size_t i = 0; i < count_; ++i) {
        const FrameProfile* f = At(i);
        if (!slow || f->dt > slow->dt) slow = f;
    }
    return slow;
}

void ProfilerService::EndFrame(unsigned long long frameIndex, float dt,
                               const FrameCounters& counters,
                               const RenderStats& render) {
    if (!enabled_) {
        frame_.Reset();
        assetLoadCounter_ = 0;
        return;
    }
    FrameProfile fp;
    fp.frameIndex = frameIndex;
    fp.dt = dt;
    fp.scopes = frame_.Records();
    fp.counters = counters;
    // Core에서 누적한 asset loads 추가
    fp.counters.assetLoads += assetLoadCounter_;
    assetLoadCounter_ = 0; // reset
    
    fp.render = render;
    for (size_t i = 0; i < static_cast<size_t>(ProfileCategory::Count); ++i)
        fp.categoryNanos[i] = frame_.CategoryNanos(static_cast<ProfileCategory>(i));
    PushFrame(std::move(fp));
    frame_.Reset();
}

} // namespace molga
