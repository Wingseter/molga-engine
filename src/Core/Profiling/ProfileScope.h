#pragma once

#include "Core/Profiling/FrameProfile.h"
#include "Core/Profiling/ScopedTimer.h"
#include <string>

namespace molga {

// RAII CPU 스코프. 생성 시 진입 시각을, 소멸 시 경과를 누적기에 기록한다.
// clock 포인터가 주어지면(테스트) 그 카운터를, 아니면 실시간 단조 시계를 쓴다.
class ProfileScope {
public:
    ProfileScope(FrameAccumulator& acc, std::string name, ProfileCategory cat,
                 const long long* clock = nullptr)
        : acc_(acc), name_(std::move(name)), cat_(cat), clock_(clock) {
        depth_ = acc_.OnEnter();
        start_ = clock_ ? *clock_ : NowNanos();
    }

    ~ProfileScope() {
        long long end = clock_ ? *clock_ : NowNanos();
        acc_.OnExit(std::move(name_), cat_, end - start_, depth_);
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    FrameAccumulator& acc_;
    std::string       name_;
    ProfileCategory   cat_;
    const long long*  clock_;
    long long         start_ = 0;
    int               depth_ = 0;
};

} // namespace molga

#include "Core/Profiling/ProfilerService.h"

#define MOLGA_PROFILE_SCOPE(name, cat) \
    ::molga::ProfileScope molga_scope_##__LINE__(::molga::ProfilerService::Get().Frame(), (name), (cat))
