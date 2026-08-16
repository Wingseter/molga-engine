#include "MolgaTime.h"
#include <chrono>

namespace {
const auto g_timeOrigin = std::chrono::steady_clock::now();

float MonotonicSeconds() {
    return std::chrono::duration<float>(
        std::chrono::steady_clock::now() - g_timeOrigin).count();
}
} // namespace

float Time::deltaTime = 0.0f;
float Time::lastTime = 0.0f;
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
    deltaTime = 0.0f;
    fps = 0.0f;
    frameCount = 0;
    fpsAccumulator = 0.0f;
    fpsFrameCount = 0;
    accumulator = 0.0f;
}

void Time::Update() {
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
