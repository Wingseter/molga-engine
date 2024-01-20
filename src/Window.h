//
// Created by 권용훈 on 1/20/24.
//

#ifndef MOLGA_ENGINE_WINDOW_H
#define MOLGA_ENGINE_WINDOW_H
#include <GLFW/glfw3.h>

class Window {
public:
    Window(int width, int height, bool fullScreen, const char* title);
    ~Window();

    bool IsInitialized() const;
    GLFWwindow* GetWindow() {return window;}

private:
    bool Initialize();

    int width;
    int height;
    bool fullScreen;
    const char* title;
    GLFWwindow* window;
};


#endif //MOLGA_ENGINE_WINDOW_H
