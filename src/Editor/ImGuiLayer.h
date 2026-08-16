#pragma once

class EngineHost;

class ImGuiLayer {
public:
    static void Init(EngineHost& host);
    static void Shutdown();

    static void BeginFrame();
    static void EndFrame();

    static void SetDarkTheme();
    static void SetModernTheme();
    static void EnableDocking();

    static bool WantCaptureMouse();
    static bool WantCaptureKeyboard();

private:
    static bool initialized;
    static EngineHost* currentHost;
};
