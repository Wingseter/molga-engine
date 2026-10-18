#include "MolgaTime.h"
#include <chrono>

namespace {
const auto g_timeOrigin = std::chrono::steady_clock::now();

std::uint64_t MonotonicNanoseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - g_timeOrigin).count());
}

float MonotonicSeconds() {
    return std::chrono::duration<float>(
        std::chrono::steady_clock::now() - g_timeOrigin).count();
}
} // namespace

float Time::deltaTime = 0.0f;
float Time::lastTime = 0.0f;
std::uint64_t Time::lastNanoseconds = 0;
std::uint64_t Time::elapsedNanoseconds = 0;
bool Time::firstUpdatePending = true;
float Time::currentTime = 0.0f;
float Time::fps = 0.0f;
int Time::frameCount = 0;

float Time::fpsUpdateInterval = 0.5f;
float Time::fpsAccumulator = 0.0f;
int Time::fpsFrameCount = 0;

float Time::fixedDeltaTime = 0.02f;
float Time::accumulator = 0.0f;

void Time::Init() {
    lastTime = MonotonicSeconds();
    currentTime = lastTime;
    lastNanoseconds = MonotonicNanoseconds();
    elapsedNanoseconds = 0;
    firstUpdatePending = true;
    deltaTime = 0.0f;
    fps = 0.0f;
    frameCount = 0;
    fpsAccumulator = 0.0f;
    fpsFrameCount = 0;
    accumulator = 0.0f;
}

void Time::Update() {
    // UI tick 시계가 쓰는 정수 표본은 float 초와 별개로 한 번만 잰다.
    const std::uint64_t nowNanoseconds = MonotonicNanoseconds();
    // Init부터 첫 Update까지는 프로젝트 선택·씬 로드 시간이라 UI 시간이 아니다: 0.
    elapsedNanoseconds = firstUpdatePending ? 0 : nowNanoseconds - lastNanoseconds;
    firstUpdatePending = false;
    lastNanoseconds = nowNanoseconds;
    currentTime = MonotonicSeconds();
    deltaTime = currentTime - lastTime;

    // Clamp to prevent spiral of death
    if (deltaTime > 0.25f) {
        deltaTime = 0.25f;
    }

    lastTime = currentTime;
    frameCount++;

    // FPS calculation
    fpsAccumulator += deltaTime;
    fpsFrameCount++;

    if (fpsAccumulator >= fpsUpdateInterval) {
        fps = static_cast<float>(fpsFrameCount) / fpsAccumulator;
        fpsAccumulator = 0.0f;
        fpsFrameCount = 0;
    }
}
