#include "Input.h"
#include <cstring>

GLFWwindow* Input::window = nullptr;

bool Input::currentKeys[MAX_KEYS] = {false};
bool Input::previousKeys[MAX_KEYS] = {false};

bool Input::currentMouseButtons[MAX_MOUSE_BUTTONS] = {false};
bool Input::previousMouseButtons[MAX_MOUSE_BUTTONS] = {false};

float Input::mouseX = 0.0f;
float Input::mouseY = 0.0f;
float Input::lastMouseX = 0.0f;
float Input::lastMouseY = 0.0f;
float Input::mouseDeltaX = 0.0f;
float Input::mouseDeltaY = 0.0f;

float Input::scrollX = 0.0f;
float Input::scrollY = 0.0f;

void Input::Init(GLFWwindow* win) {
    window = win;

    std::memset(currentKeys, false, sizeof(currentKeys));
    std::memset(previousKeys, false, sizeof(previousKeys));
    std::memset(currentMouseButtons, false, sizeof(currentMouseButtons));
    std::memset(previousMouseButtons, false, sizeof(previousMouseButtons));

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    mouseX = lastMouseX = static_cast<float>(mx);
    mouseY = lastMouseY = static_cast<float>(my);

    glfwSetScrollCallback(window, ScrollCallback);
}

void Input::Update() {
    // Store previous states
    std::memcpy(previousKeys, currentKeys, sizeof(currentKeys));
    std::memcpy(previousMouseButtons, currentMouseButtons, sizeof(currentMouseButtons));

    // Update keyboard state
    for (int i = 0; i < MAX_KEYS; i++) {
        currentKeys[i] = glfwGetKey(window, i) == GLFW_PRESS;
    }

    // Update mouse button state
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) {
        currentMouseButtons[i] = glfwGetMouseButton(window, i) == GLFW_PRESS;
    }

    // Update mouse position
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    mouseX = static_cast<float>(mx);
    mouseY = static_cast<float>(my);

    mouseDeltaX = mouseX - lastMouseX;
    mouseDeltaY = mouseY - lastMouseY;

    lastMouseX = mouseX;
    lastMouseY = mouseY;

    // Reset scroll (scroll is event-based, so reset after each frame)
    scrollX = 0.0f;
    scrollY = 0.0f;
}

void Input::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    scrollX = static_cast<float>(xoffset);
    scrollY = static_cast<float>(yoffset);
}

// Keyboard
bool Input::GetKey(int key) {
    if (key < 0 || key >= MAX_KEYS) return false;
    return currentKeys[key];
}

bool Input::GetKeyDown(int key) {
    if (key < 0 || key >= MAX_KEYS) return false;
    return currentKeys[key] && !previousKeys[key];
}

bool Input::GetKeyUp(int key) {
    if (key < 0 || key >= MAX_KEYS) return false;
    return !currentKeys[key] && previousKeys[key];
}

// Mouse buttons
bool Input::GetMouseButton(int button) {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) return false;
    return currentMouseButtons[button];
}

bool Input::GetMouseButtonDown(int button) {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) return false;
    return currentMouseButtons[button] && !previousMouseButtons[button];
}

bool Input::GetMouseButtonUp(int button) {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) return false;
    return !currentMouseButtons[button] && previousMouseButtons[button];
}

// Mouse position
float Input::GetMouseX() { return mouseX; }
float Input::GetMouseY() { return mouseY; }
float Input::GetMouseDeltaX() { return mouseDeltaX; }
float Input::GetMouseDeltaY() { return mouseDeltaY; }

// Scroll
float Input::GetScrollX() { return scrollX; }
float Input::GetScrollY() { return scrollY; }
