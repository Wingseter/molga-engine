#pragma once

#include <string>

class EngineHost;
class Renderer;

class ImGuiLayer {
public:
    static void Init(EngineHost& host);
    static void Shutdown();

    static void BeginFrame();
    static bool EndFrame(Renderer& renderer, std::string* errorOut = nullptr);

    static void SetDarkTheme();
    static void SetModernTheme();
    static void EnableDocking();

    static bool WantCaptureMouse();
    static bool WantCaptureKeyboard();

private:
    static bool initialized;
    static EngineHost* currentHost;
};
