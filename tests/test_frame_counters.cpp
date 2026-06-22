#include "Core/Profiling/FrameProfile.h"
#include "doctest.h"

using molga::FrameCounters;

TEST_CASE("counters start at zero and add up") {
    FrameCounters c;
    CHECK(c.drawCalls == 0);
    CHECK(c.sprites == 0);
    c.drawCalls += 3;
    c.sprites   += 12;
    c.assetLoads += 1;
    CHECK(c.drawCalls == 3);
    CHECK(c.sprites == 12);
    CHECK(c.assetLoads == 1);
}

TEST_CASE("Reset clears every counter") {
    FrameCounters c;
    c.drawCalls = 9; c.particles = 5; c.scripts = 7; c.physics = 4;
    c.Reset();
    CHECK(c.drawCalls == 0);
    CHECK(c.particles == 0);
    CHECK(c.scripts == 0);
    CHECK(c.physics == 0);
}
