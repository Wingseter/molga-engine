#pragma once

struct GLFWwindow;

class ImGuiLayer {
public:
    static void Init(GLFWwindow* window);
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
    static GLFWwindow* currentWindow;
};
