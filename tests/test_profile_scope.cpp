#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/FrameProfile.h"
#include "doctest.h"

using molga::ProfileScope;
using molga::ProfileCategory;
using molga::FrameAccumulator;

// 테스트는 단조 시계를 직접 전진시킨다(실시간 의존 제거).
TEST_CASE("a scope accumulates elapsed time under its name and category") {
    FrameAccumulator acc;
    long long clock = 0;
    {
        ProfileScope s(acc, "Update", ProfileCategory::Scripts, &clock);
        clock += 1000;  // 1000ns 경과
    }
    const auto& recs = acc.Records();
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].name == "Update");
    CHECK(recs[0].category == ProfileCategory::Scripts);
    CHECK(recs[0].nanos == 1000);
    CHECK(acc.Depth() == 0);
}

TEST_CASE("nested scopes keep child time and record parent depth") {
    FrameAccumulator acc;
    long long clock = 0;
    {
        ProfileScope outer(acc, "Frame", ProfileCategory::Other, &clock);
        clock += 100;
        {
            ProfileScope inner(acc, "Render", ProfileCategory::Rendering, &clock);
            clock += 50;
        }
        clock += 30;
    }
    const auto& recs = acc.Records();
    REQUIRE(recs.size() == 2);
    // 자식이 먼저 닫히므로 먼저 기록된다.
    CHECK(recs[0].name == "Render");
    CHECK(recs[0].depth == 1);
    CHECK(recs[0].nanos == 50);
    CHECK(recs[1].name == "Frame");
    CHECK(recs[1].depth == 0);
    CHECK(recs[1].nanos == 180);   // 100 + 50 + 30
}

TEST_CASE("category totals sum self-exclusive time per category") {
    FrameAccumulator acc;
    long long clock = 0;
    { ProfileScope a(acc, "S1", ProfileCategory::Scripts, &clock);   clock += 200; }
    { ProfileScope b(acc, "R1", ProfileCategory::Rendering, &clock); clock += 300; }
    { ProfileScope c(acc, "S2", ProfileCategory::Scripts, &clock);   clock += 100; }
    CHECK(acc.CategoryNanos(ProfileCategory::Scripts)   == 300);
    CHECK(acc.CategoryNanos(ProfileCategory::Rendering) == 300);
    CHECK(acc.CategoryNanos(ProfileCategory::Physics)   == 0);
}
