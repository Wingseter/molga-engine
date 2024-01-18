#pragma once
#include <GLFW/glfw3.h>

class Window {
public:
    Window(int width, int height, bool fullScreen, const char* title);
    ~Window();

    bool IsInitialized() const;

private:
    bool Initialize();

    int width;
    int height;
    bool fullScreen;
    const char* title;
    GLFWwindow* window;
};
