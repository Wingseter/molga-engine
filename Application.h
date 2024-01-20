//
// Created by 권용훈 on 1/20/24.
//

#ifndef MOLGA_ENGINE_APPLICATION_H
#define MOLGA_ENGINE_APPLICATION_H
#include "GLFW/glfw3.h"
#include "Window.h"

class Application
{
public:

private:
    Window* window;
public:
    Application();
    ~Application();

    bool Initalize();
    bool Run();
    bool Terminate();

    static void glfwErrorCallback(int error, const char* description);
private:


};


#endif //MOLGA_ENGINE_APPLICATION_H
