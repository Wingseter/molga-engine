#include "pch.h"
#include "MolgaEngine.h"
#define GLFW_EXPOSE_NATIVE_WIN32

#include <GL/glfw3.h>
#include <GL/glfw3native.h> // Win32 ������ �ڵ��� ��� ���� �ʿ�

// ������ �ڵ��� �����ϴ� ���� ����
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

    // GLFW �������� Win32 ������ �ڵ��� ����
    g_hwnd = glfwGetWin32Window(window);

    return true;
}

// GetMolgaEngineWindowHandle �Լ� �߰�
extern "C" MOLGAENGINE_API HWND GetMolgaEngineWindowHandle() {
    return g_hwnd;
}
