#ifndef MOLGA_INPUT_H
#define MOLGA_INPUT_H

#include <GLFW/glfw3.h>

class Input {
public:
    static void Init(GLFWwindow* window);
    static void Update();

    // Keyboard
    static bool GetKey(int key);
    static bool GetKeyDown(int key);
    static bool GetKeyUp(int key);

    // Mouse buttons
    static bool GetMouseButton(int button);
    static bool GetMouseButtonDown(int button);
    static bool GetMouseButtonUp(int button);

    // Mouse position
    static float GetMouseX();
    static float GetMouseY();
    static float GetMouseDeltaX();
    static float GetMouseDeltaY();

    // Mouse scroll
    static float GetScrollX();
    static float GetScrollY();

private:
    static GLFWwindow* window;

    static constexpr int MAX_KEYS = 512;
    static constexpr int MAX_MOUSE_BUTTONS = 8;

    static bool currentKeys[MAX_KEYS];
    static bool previousKeys[MAX_KEYS];

    static bool currentMouseButtons[MAX_MOUSE_BUTTONS];
    static bool previousMouseButtons[MAX_MOUSE_BUTTONS];

    static float mouseX, mouseY;
    static float lastMouseX, lastMouseY;
    static float mouseDeltaX, mouseDeltaY;

    static float scrollX, scrollY;

    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

#endif // MOLGA_INPUT_H
