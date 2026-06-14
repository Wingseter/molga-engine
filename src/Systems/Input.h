#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class Input {
public:
    enum class DeviceType {
        Keyboard,
        Mouse,
        GamepadButton,
        GamepadAxis
    };

    struct Binding {
        DeviceType device = DeviceType::Keyboard;
        int code = 0;
        float multiplier = 1.0f;
    };

    struct Action {
        std::string name;
        bool isAxis = false;
        std::vector<Binding> bindings;

        // Action state tracking
        bool currentState = false;
        bool previousState = false;
        float currentValue = 0.0f;
        float previousValue = 0.0f;
    };

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

    // Action Map
    static bool GetAction(const std::string& name);
    static bool GetActionDown(const std::string& name);
    static bool GetActionUp(const std::string& name);
    static float GetAxis(const std::string& name);
    static void LoadActions(const std::string& filepath);
    static void DeserializeActions(const nlohmann::json& jsonArray);
    static void SaveActions(const std::string& filepath);
    static void InitializeDefaultActions();
    static std::vector<Action>& GetActions();

    // Testing Helpers
    static void SetKeyForTesting(int key, bool pressed);
    static void SetMouseButtonForTesting(int button, bool pressed);
    static void SetGamepadButtonForTesting(int button, bool pressed);
    static void SetGamepadAxisForTesting(int axis, float value);

private:
    static GLFWwindow* window;

    static constexpr int MAX_KEYS = 512;
    static constexpr int MAX_MOUSE_BUTTONS = 8;
    static constexpr int MAX_GAMEPAD_BUTTONS = 15;
    static constexpr int MAX_GAMEPAD_AXES = 6;

    static bool currentKeys[MAX_KEYS];
    static bool previousKeys[MAX_KEYS];

    static bool currentMouseButtons[MAX_MOUSE_BUTTONS];
    static bool previousMouseButtons[MAX_MOUSE_BUTTONS];

    static bool currentGamepadButtons[MAX_GAMEPAD_BUTTONS];
    static bool previousGamepadButtons[MAX_GAMEPAD_BUTTONS];
    static float currentGamepadAxes[MAX_GAMEPAD_AXES];

    static std::vector<Action> actions;

    static float mouseX, mouseY;
    static float lastMouseX, lastMouseY;
    static float mouseDeltaX, mouseDeltaY;

    static float scrollX, scrollY;

    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

