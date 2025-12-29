#include "Time.h"
#include <GLFW/glfw3.h>

float Time::deltaTime = 0.0f;
float Time::lastTime = 0.0f;
float Time::currentTime = 0.0f;
float Time::fps = 0.0f;
int Time::frameCount = 0;

float Time::fpsUpdateInterval = 0.5f;
float Time::fpsAccumulator = 0.0f;
int Time::fpsFrameCount = 0;

void Time::Init() {
    lastTime = static_cast<float>(glfwGetTime());
    currentTime = lastTime;
    deltaTime = 0.0f;
    fps = 0.0f;
    frameCount = 0;
    fpsAccumulator = 0.0f;
    fpsFrameCount = 0;
}

void Time::Update() {
    currentTime = static_cast<float>(glfwGetTime());
    deltaTime = currentTime - lastTime;
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
