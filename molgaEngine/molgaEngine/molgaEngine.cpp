#include "pch.h"
#include "MolgaEngine.h"
#define GLFW_EXPOSE_NATIVE_WIN32

#include <GL/glfw3.h>
#include <GL/glfw3native.h> // Win32 윈도우 핸들을 얻기 위해 필요

// 윈도우 핸들을 저장하는 전역 변수
static HWND g_hwnd = nullptr;

extern "C" MOLGAENGINE_API bool InitializeWindow(int width, int height, const char* title) {
    if (!glfwInit()) {
        return false;
    }

    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);

    // GLFW 윈도우의 Win32 윈도우 핸들을 얻음
    g_hwnd = glfwGetWin32Window(window);

    return true;
}

// GetMolgaEngineWindowHandle 함수 추가
extern "C" MOLGAENGINE_API HWND GetMolgaEngineWindowHandle() {
    return g_hwnd;
}
