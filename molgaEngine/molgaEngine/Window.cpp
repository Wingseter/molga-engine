#include "Window.h"

Window::Window(int width, int height, bool fullScreen, const char* title)
    : width(width), height(height), fullScreen(fullScreen), title(title), window(nullptr) {
    Initialize();
}

Window::~Window() {
    if (window) {
        glfwDestroyWindow(window);
    }
}

bool Window::Initialize() {
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    return window != nullptr;
}

bool Window::IsInitialized() const {
    return window != nullptr;
}
