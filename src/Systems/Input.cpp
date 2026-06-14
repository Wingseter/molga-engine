#include "Input.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>
#include <nlohmann/json.hpp>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

GLFWwindow* Input::window = nullptr;

bool Input::currentKeys[MAX_KEYS] = {false};
bool Input::previousKeys[MAX_KEYS] = {false};

bool Input::currentMouseButtons[MAX_MOUSE_BUTTONS] = {false};
bool Input::previousMouseButtons[MAX_MOUSE_BUTTONS] = {false};

bool Input::currentGamepadButtons[MAX_GAMEPAD_BUTTONS] = {false};
bool Input::previousGamepadButtons[MAX_GAMEPAD_BUTTONS] = {false};
float Input::currentGamepadAxes[MAX_GAMEPAD_AXES] = {0.0f};

std::vector<Input::Action> Input::actions;

float Input::mouseX = 0.0f;
float Input::mouseY = 0.0f;
float Input::lastMouseX = 0.0f;
float Input::lastMouseY = 0.0f;
float Input::mouseDeltaX = 0.0f;
float Input::mouseDeltaY = 0.0f;

float Input::scrollX = 0.0f;
float Input::scrollY = 0.0f;

static std::string DeviceTypeToString(Input::DeviceType type) {
    switch (type) {
        case Input::DeviceType::Keyboard: return "Keyboard";
        case Input::DeviceType::Mouse: return "Mouse";
        case Input::DeviceType::GamepadButton: return "GamepadButton";
        case Input::DeviceType::GamepadAxis: return "GamepadAxis";
    }
    return "Keyboard";
}

static Input::DeviceType StringToDeviceType(const std::string& str) {
    if (str == "Keyboard") return Input::DeviceType::Keyboard;
    if (str == "Mouse") return Input::DeviceType::Mouse;
    if (str == "GamepadButton") return Input::DeviceType::GamepadButton;
    if (str == "GamepadAxis") return Input::DeviceType::GamepadAxis;
    return Input::DeviceType::Keyboard;
}

void Input::Init(GLFWwindow* win) {
    window = win;

    std::memset(currentKeys, false, sizeof(currentKeys));
    std::memset(previousKeys, false, sizeof(previousKeys));
    std::memset(currentMouseButtons, false, sizeof(currentMouseButtons));
    std::memset(previousMouseButtons, false, sizeof(previousMouseButtons));
    std::memset(currentGamepadButtons, false, sizeof(currentGamepadButtons));
    std::memset(previousGamepadButtons, false, sizeof(previousGamepadButtons));
    std::memset(currentGamepadAxes, 0, sizeof(currentGamepadAxes));

    if (window) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        mouseX = lastMouseX = static_cast<float>(mx);
        mouseY = lastMouseY = static_cast<float>(my);

        glfwSetScrollCallback(window, ScrollCallback);
    }
}

void Input::Update() {
    // Store previous states
    std::memcpy(previousKeys, currentKeys, sizeof(currentKeys));
    std::memcpy(previousMouseButtons, currentMouseButtons, sizeof(currentMouseButtons));
    std::memcpy(previousGamepadButtons, currentGamepadButtons, sizeof(currentGamepadButtons));

    if (window) {
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
    } else {
        // In unit tests, window is nullptr. We preserve currentKeys/currentMouseButtons so tests can set them manually.
        mouseDeltaX = 0.0f;
        mouseDeltaY = 0.0f;
    }

    // Poll Gamepad
    GLFWgamepadstate gamepadState;
    if (window && glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepadState)) {
        for (int i = 0; i < MAX_GAMEPAD_BUTTONS; i++) {
            currentGamepadButtons[i] = gamepadState.buttons[i] == GLFW_PRESS;
        }
        for (int i = 0; i < MAX_GAMEPAD_AXES; i++) {
            currentGamepadAxes[i] = gamepadState.axes[i];
        }
    } else if (window) {
        // Only clear if we actually have a window but no gamepad.
        std::memset(currentGamepadButtons, false, sizeof(currentGamepadButtons));
        std::memset(currentGamepadAxes, 0, sizeof(currentGamepadAxes));
    }

#ifdef MOLGA_EDITOR
    if (ImGui::GetCurrentContext()) {
        auto& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            std::memset(currentKeys, false, sizeof(currentKeys));
        }
        if (io.WantCaptureMouse) {
            std::memset(currentMouseButtons, false, sizeof(currentMouseButtons));
        }
    }
#endif

    // Reset scroll (scroll is event-based, so reset after each frame)
    scrollX = 0.0f;
    scrollY = 0.0f;

    // Update Action Map States
    for (auto& action : actions) {
        action.previousState = action.currentState;
        action.previousValue = action.currentValue;

        action.currentState = false;
        action.currentValue = 0.0f;

        for (const auto& binding : action.bindings) {
            float val = 0.0f;
            bool btn = false;

            if (binding.device == DeviceType::Keyboard) {
                if (Input::GetKey(binding.code)) {
                    val = 1.0f * binding.multiplier;
                    btn = true;
                }
            } else if (binding.device == DeviceType::Mouse) {
                if (Input::GetMouseButton(binding.code)) {
                    val = 1.0f * binding.multiplier;
                    btn = true;
                }
            } else if (binding.device == DeviceType::GamepadButton) {
                if (binding.code >= 0 && binding.code < MAX_GAMEPAD_BUTTONS) {
                    if (currentGamepadButtons[binding.code]) {
                        val = 1.0f * binding.multiplier;
                        btn = true;
                    }
                }
            } else if (binding.device == DeviceType::GamepadAxis) {
                if (binding.code >= 0 && binding.code < MAX_GAMEPAD_AXES) {
                    float rawVal = currentGamepadAxes[binding.code];
                    // apply a small deadzone (e.g. 0.15f) for GamepadAxis
                    if (std::abs(rawVal) > 0.15f) {
                        val = rawVal * binding.multiplier;
                        if (std::abs(rawVal) > 0.5f) {
                            btn = true;
                        }
                    }
                }
            }

            action.currentValue += val;
            if (btn) {
                action.currentState = true;
            }
        }

        // Clamp axis values
        if (action.isAxis) {
            if (action.currentValue > 1.0f) action.currentValue = 1.0f;
            if (action.currentValue < -1.0f) action.currentValue = -1.0f;
        }
    }
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

// Action Map
bool Input::GetAction(const std::string& name) {
    for (const auto& action : actions) {
        if (action.name == name) {
            return action.currentState;
        }
    }
    return false;
}

bool Input::GetActionDown(const std::string& name) {
    for (const auto& action : actions) {
        if (action.name == name) {
            return action.currentState && !action.previousState;
        }
    }
    return false;
}

bool Input::GetActionUp(const std::string& name) {
    for (const auto& action : actions) {
        if (action.name == name) {
            return !action.currentState && action.previousState;
        }
    }
    return false;
}

float Input::GetAxis(const std::string& name) {
    for (const auto& action : actions) {
        if (action.name == name) {
            return action.currentValue;
        }
    }
    return 0.0f;
}

void Input::LoadActions(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return;
    }

    nlohmann::json jsonArray;
    try {
        file >> jsonArray;
        DeserializeActions(jsonArray);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse input actions: " << e.what() << std::endl;
    }
}

void Input::DeserializeActions(const nlohmann::json& jsonArray) {
    if (jsonArray.is_array()) {
        actions.clear();
        for (const auto& actionJson : jsonArray) {
            Action action;
            action.name = actionJson.value("name", "");
            action.isAxis = actionJson.value("isAxis", false);

            if (actionJson.contains("bindings") && actionJson["bindings"].is_array()) {
                for (const auto& bindingJson : actionJson["bindings"]) {
                    Binding binding;
                    std::string devStr = bindingJson.value("device", "Keyboard");
                    binding.device = StringToDeviceType(devStr);
                    binding.code = bindingJson.value("code", 0);
                    binding.multiplier = bindingJson.value("multiplier", 1.0f);
                    action.bindings.push_back(binding);
                }
            }
            actions.push_back(action);
        }
    }
}

void Input::SaveActions(const std::string& filepath) {
    nlohmann::json jsonArray = nlohmann::json::array();
    for (const auto& action : actions) {
        nlohmann::json actionJson;
        actionJson["name"] = action.name;
        actionJson["isAxis"] = action.isAxis;

        nlohmann::json bindingsJson = nlohmann::json::array();
        for (const auto& binding : action.bindings) {
            nlohmann::json bindingJson;
            bindingJson["device"] = DeviceTypeToString(binding.device);
            bindingJson["code"] = binding.code;
            bindingJson["multiplier"] = binding.multiplier;
            bindingsJson.push_back(bindingJson);
        }
        actionJson["bindings"] = bindingsJson;
        jsonArray.push_back(actionJson);
    }

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << jsonArray.dump(4);
    }
}

void Input::InitializeDefaultActions() {
    actions.clear();

    // Horizontal Action
    {
        Action horizontal;
        horizontal.name = "Horizontal";
        horizontal.isAxis = true;

        Binding dKey;
        dKey.device = DeviceType::Keyboard;
        dKey.code = GLFW_KEY_D;
        dKey.multiplier = 1.0f;
        horizontal.bindings.push_back(dKey);

        Binding aKey;
        aKey.device = DeviceType::Keyboard;
        aKey.code = GLFW_KEY_A;
        aKey.multiplier = -1.0f;
        horizontal.bindings.push_back(aKey);

        Binding gpX;
        gpX.device = DeviceType::GamepadAxis;
        gpX.code = GLFW_GAMEPAD_AXIS_LEFT_X;
        gpX.multiplier = 1.0f;
        horizontal.bindings.push_back(gpX);

        actions.push_back(horizontal);
    }

    // Vertical Action
    {
        Action vertical;
        vertical.name = "Vertical";
        vertical.isAxis = true;

        Binding wKey;
        wKey.device = DeviceType::Keyboard;
        wKey.code = GLFW_KEY_W;
        wKey.multiplier = 1.0f;
        vertical.bindings.push_back(wKey);

        Binding sKey;
        sKey.device = DeviceType::Keyboard;
        sKey.code = GLFW_KEY_S;
        sKey.multiplier = -1.0f;
        vertical.bindings.push_back(sKey);

        Binding gpY;
        gpY.device = DeviceType::GamepadAxis;
        gpY.code = GLFW_GAMEPAD_AXIS_LEFT_Y;
        gpY.multiplier = -1.0f; // GLFW Left Y stick is negative when moving up
        vertical.bindings.push_back(gpY);

        actions.push_back(vertical);
    }

    // Jump Action
    {
        Action jump;
        jump.name = "Jump";
        jump.isAxis = false;

        Binding spaceKey;
        spaceKey.device = DeviceType::Keyboard;
        spaceKey.code = GLFW_KEY_SPACE;
        spaceKey.multiplier = 1.0f;
        jump.bindings.push_back(spaceKey);

        Binding gpA;
        gpA.device = DeviceType::GamepadButton;
        gpA.code = GLFW_GAMEPAD_BUTTON_A;
        gpA.multiplier = 1.0f;
        jump.bindings.push_back(gpA);

        actions.push_back(jump);
    }

    // Fire Action
    {
        Action fire;
        fire.name = "Fire";
        fire.isAxis = false;

        Binding mouseLeft;
        mouseLeft.device = DeviceType::Mouse;
        mouseLeft.code = GLFW_MOUSE_BUTTON_LEFT;
        mouseLeft.multiplier = 1.0f;
        fire.bindings.push_back(mouseLeft);

        Binding gpB;
        gpB.device = DeviceType::GamepadButton;
        gpB.code = GLFW_GAMEPAD_BUTTON_B;
        gpB.multiplier = 1.0f;
        fire.bindings.push_back(gpB);

        actions.push_back(fire);
    }
}

std::vector<Input::Action>& Input::GetActions() {
    return actions;
}

void Input::SetKeyForTesting(int key, bool pressed) {
    if (key >= 0 && key < MAX_KEYS) {
        currentKeys[key] = pressed;
    }
}

void Input::SetMouseButtonForTesting(int button, bool pressed) {
    if (button >= 0 && button < MAX_MOUSE_BUTTONS) {
        currentMouseButtons[button] = pressed;
    }
}

void Input::SetGamepadButtonForTesting(int button, bool pressed) {
    if (button >= 0 && button < MAX_GAMEPAD_BUTTONS) {
        currentGamepadButtons[button] = pressed;
    }
}

void Input::SetGamepadAxisForTesting(int axis, float value) {
    if (axis >= 0 && axis < MAX_GAMEPAD_AXES) {
        currentGamepadAxes[axis] = value;
    }
}

