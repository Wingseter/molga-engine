#ifndef MOLGA_IMGUI_LAYER_H
#define MOLGA_IMGUI_LAYER_H

struct GLFWwindow;

class ImGuiLayer {
public:
    static void Init(GLFWwindow* window);
    static void Shutdown();

    static void BeginFrame();
    static void EndFrame();

    static void SetDarkTheme();
    static void EnableDocking();

    static bool WantCaptureMouse();
    static bool WantCaptureKeyboard();

private:
    static bool initialized;
};

#endif
