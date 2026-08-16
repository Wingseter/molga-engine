#pragma once

#include <GLFW/glfw3.h>
#include <array>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct InputSnapshot {
    std::array<bool, 512> keys{};
    std::array<bool, 8> mouseButtons{};
    std::array<bool, 15> gamepadButtons{};
    std::array<float, 6> gamepadAxes{};
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    bool pointerValid = false;
    bool cameraPointerValid = false;
    unsigned int pointerCameraObjectId = 0;
    float cameraPointerX = 0.0f;
    float cameraPointerY = 0.0f;
    float worldPointerX = 0.0f;
    float worldPointerY = 0.0f;
};

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

    // Editor Game View captures the detached panel's native GLFW window and
    // maps its pointer to output pixels before gameplay scripts run.
    static InputSnapshot CaptureSnapshot(GLFWwindow* sourceWindow,
                                         float mappedMouseX,
                                         float mappedMouseY,
                                         bool pointerValid);
    // ImGui platform windows are created after Input::Init. Registering their
    // native window installs a chaining scroll callback without disturbing
    // ImGui's own wheel handling.
    static void RegisterScrollSource(GLFWwindow* sourceWindow);
    static void DiscardPendingScroll(GLFWwindow* sourceWindow);
    static void ApplySnapshot(const InputSnapshot& snapshot);
    static void ReleaseAll();

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

    // Camera-relative pointer sampled independently from the legacy logical
    // output coordinates above. A pointer can remain valid for global UI while
    // lying outside every output-camera viewport.
    static bool HasCameraPointer();
    static unsigned int GetPointerCameraObjectId();
    static float GetCameraPointerX();
    static float GetCameraPointerY();
    static float GetWorldPointerX();
    static float GetWorldPointerY();

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
    static void AddScrollForTesting(GLFWwindow* sourceWindow,
                                    float xoffset, float yoffset);
    static InputSnapshot ConsumeScrollForTesting(GLFWwindow* sourceWindow,
                                                 bool pointerValid = true);

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

    static bool cameraPointerValid;
    static unsigned int pointerCameraObjectId;
    static float cameraPointerX, cameraPointerY;
    static float worldPointerX, worldPointerY;

    static float scrollX, scrollY;

    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void UpdateActions();
};
