#pragma once

#include <cstdint>

class Time {
public:
    static void Init();
    static void Update();

    static float GetDeltaTime() { return deltaTime; }
    static float GetTime() { return currentTime; }
    static float GetFPS() { return fps; }
    static int GetFrameCount() { return frameCount; }

    // Update가 기록한 마지막 프레임 간격(나노초, steady_clock). Init 직후와 첫
    // Update 전에는 0이다. deltaTime과 달리 0.25초로 자르지 않은 원본 표본이며,
    // 프레임 소유자가 한 프레임에 한 번 읽어 UI tick 시계에 넘긴다.
    static std::uint64_t GetElapsedNanoseconds() { return elapsedNanoseconds; }

    // ── Fixed timestep ──
    static float GetFixedDeltaTime() { return fixedDeltaTime; }
    static void SetFixedDeltaTime(float dt) { fixedDeltaTime = dt; }

    // Accumulator management — caller-driven, not auto-accumulated.
    // Call AccumulateFixedTime() with the simulation dt (may differ from raw dt
    // due to timeScale or pause).
    static void AccumulateFixedTime(float simDt) { accumulator += simDt; }
    static bool HasPendingFixedStep() { return accumulator >= fixedDeltaTime; }
    static void ConsumeFixedStep() { accumulator -= fixedDeltaTime; }
    static void ResetFixedAccumulator() { accumulator = 0.0f; }

    // Interpolation alpha for rendering between physics steps
    static float GetFixedAlpha() {
        return fixedDeltaTime > 0.0f ? accumulator / fixedDeltaTime : 0.0f;
    }

private:
    static float deltaTime;
    static float lastTime;
    static std::uint64_t lastNanoseconds;
    static std::uint64_t elapsedNanoseconds;
    static float currentTime;
    static float fps;
    static int frameCount;

    static float fpsUpdateInterval;
    static float fpsAccumulator;
    static int fpsFrameCount;

    static float fixedDeltaTime;   // default 0.02s (50Hz)
    static float accumulator;
};
