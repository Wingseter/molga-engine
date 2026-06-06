#include "Core/MolgaTime.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool approx(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) < eps;
}

// ── Fixed accumulator ────────────────────────────────────────────────────────

static void test_fixed_accumulator_basic() {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();

    // 0.05s accumulated → 2 steps possible (0.05 / 0.02 = 2.5)
    Time::AccumulateFixedTime(0.05f);
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    assert(steps == 2);
}

static void test_fixed_accumulator_reset() {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(1.0f);  // large backlog
    Time::ResetFixedAccumulator();
    assert(!Time::HasPendingFixedStep());
}

static void test_fixed_alpha() {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(0.01f);
    float alpha = Time::GetFixedAlpha();
    assert(approx(alpha, 0.5f));  // 0.01 / 0.02 = 0.5
}

static void test_fixed_step_count_large() {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(0.1f);
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    assert(steps == 5);  // 0.1 / 0.02 = 5
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_fixed_accumulator_basic();
    test_fixed_accumulator_reset();
    test_fixed_alpha();
    test_fixed_step_count_large();

    std::printf("test_time: all tests passed\n");
    return 0;
}
