#include "Core/Profiling/ProfilerService.h"
#include "doctest.h"

using molga::ProfilerService;
using molga::FrameProfile;

static FrameProfile MakeFrame(unsigned long long idx, float dt) {
    FrameProfile f;
    f.frameIndex = idx;
    f.dt = dt;
    return f;
}

TEST_CASE("a fresh ring buffer is empty") {
    ProfilerService svc(/*capacity=*/4);
    CHECK(svc.Size() == 0);
    CHECK(svc.Capacity() == 4);
    CHECK(svc.Latest() == nullptr);
}

TEST_CASE("PushFrame stores frames up to capacity") {
    ProfilerService svc(4);
    svc.PushFrame(MakeFrame(0, 0.016f));
    svc.PushFrame(MakeFrame(1, 0.017f));
    CHECK(svc.Size() == 2);
    REQUIRE(svc.Latest() != nullptr);
    CHECK(svc.Latest()->frameIndex == 1);
    // At(0) = 가장 오래된, At(Size-1) = 최신
    CHECK(svc.At(0)->frameIndex == 0);
    CHECK(svc.At(1)->frameIndex == 1);
}

TEST_CASE("PushFrame overwrites oldest once capacity is exceeded") {
    ProfilerService svc(3);
    for (unsigned long long i = 0; i < 5; ++i)
        svc.PushFrame(MakeFrame(i, 0.016f));
    CHECK(svc.Size() == 3);
    // 가장 오래된 2개(0,1)는 밀려나고 2,3,4가 남는다.
    CHECK(svc.At(0)->frameIndex == 2);
    CHECK(svc.At(1)->frameIndex == 3);
    CHECK(svc.At(2)->frameIndex == 4);
    CHECK(svc.Latest()->frameIndex == 4);
}

TEST_CASE("the slowest retained frame can be located") {
    ProfilerService svc(4);
    svc.PushFrame(MakeFrame(0, 0.010f));
    svc.PushFrame(MakeFrame(1, 0.040f));  // 느린 프레임
    svc.PushFrame(MakeFrame(2, 0.012f));
    const FrameProfile* slow = svc.SlowestFrame();
    REQUIRE(slow != nullptr);
    CHECK(slow->frameIndex == 1);
}

TEST_CASE("disabled service ignores pushed frames (near-zero overhead path)") {
    ProfilerService svc(4);
    svc.SetEnabled(false);
    svc.PushFrame(MakeFrame(0, 0.016f));
    CHECK(svc.Size() == 0);
}
