#include "Core/MolgaTime.h"
#include "doctest.h"

// ── Fixed accumulator ────────────────────────────────────────────────────────

TEST_CASE("fixed accumulator yields 2 steps for 0.05s at 50Hz") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();

    Time::AccumulateFixedTime(0.05f);  // 0.05 / 0.02 = 2.5 → 2 steps
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    CHECK(steps == 2);
}

TEST_CASE("ResetFixedAccumulator clears backlog") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(1.0f);   // large backlog
    Time::ResetFixedAccumulator();
    CHECK_FALSE(Time::HasPendingFixedStep());
}

TEST_CASE("fixed alpha is fraction of a step") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(0.01f);
    CHECK(Time::GetFixedAlpha() == doctest::Approx(0.5f));  // 0.01 / 0.02
}

TEST_CASE("0.1s produces 5 fixed steps") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(0.1f);
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    CHECK(steps == 5);  // 0.1 / 0.02
}
