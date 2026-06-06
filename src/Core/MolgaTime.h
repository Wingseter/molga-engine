#pragma once

class Time {
public:
    static void Init();
    static void Update();

    static float GetDeltaTime() { return deltaTime; }
    static float GetTime() { return currentTime; }
    static float GetFPS() { return fps; }
    static int GetFrameCount() { return frameCount; }

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
    static float currentTime;
    static float fps;
    static int frameCount;

    static float fpsUpdateInterval;
    static float fpsAccumulator;
    static int fpsFrameCount;

    static float fixedDeltaTime;   // default 0.02s (50Hz)
    static float accumulator;
};
