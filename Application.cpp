//
// Created by 권용훈 on 1/20/24.
//

#include "Application.h"
#include <iostream>
#include "Application.h"

Application::Application() : window(nullptr) {}

Application::~Application() {
    Terminate();
}

bool Application::Initalize() {
    glfwSetErrorCallback(Application::glfwErrorCallback);

    if (!glfwInit()) {
        return false;
    }

    window = new Window(800, 600, false, "MolgaEngine");
    if (!window->IsInitialized()) {
        glfwTerminate();
        return false;
    }

    return true;
}

bool Application::Terminate() {
    delete window;
    glfwTerminate();
    return true;
}

void Application::glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

bool Application::Run() {
    while (!glfwWindowShouldClose((window->GetWindow())))
    {

    }
    return false;
}
