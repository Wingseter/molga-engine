#ifndef MOLGA_TIME_H
#define MOLGA_TIME_H

class Time {
public:
    static void Init();
    static void Update();

    static float GetDeltaTime() { return deltaTime; }
    static float GetTime() { return currentTime; }
    static float GetFPS() { return fps; }
    static int GetFrameCount() { return frameCount; }

private:
    static float deltaTime;
    static float lastTime;
    static float currentTime;
    static float fps;
    static int frameCount;

    static float fpsUpdateInterval;
    static float fpsAccumulator;
    static int fpsFrameCount;
};

#endif // MOLGA_TIME_H
