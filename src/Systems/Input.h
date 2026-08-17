#pragma once

#include "Platform/Window.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct InputSnapshot {
    std::array<bool, 512> keys{};
    std::array<bool, 8> mouseButtons{};
    std::array<bool, 26> gamepadButtons{};
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
    // Physical-key identifiers follow the USB HID/SDL scancode positions, but
    // are engine-owned and never serialized as integers.
    enum class KeyCode : std::uint16_t {
        Unknown = 0,
        A = 4, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,
        Enter = 40, Escape, Backspace, Tab, Space,
        Minus, Equal, LeftBracket, RightBracket, Backslash, NonUsHash,
        Semicolon, Apostrophe, Grave, Comma, Period, Slash, CapsLock,
        F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        PrintScreen = 70, ScrollLock, Pause, Insert, Home, PageUp, Delete,
        End, PageDown, Right, Left, Down, Up, NumLock,
        KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, KeypadEnter,
        Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7,
        Keypad8, Keypad9, Keypad0, KeypadPeriod,
        Application = 101, Power, KeypadEqual,
        F13 = 104, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        Menu = 118,
        LeftControl = 224, LeftShift, LeftAlt, LeftSuper,
        RightControl, RightShift, RightAlt, RightSuper
    };

    enum class MouseButton : std::uint8_t {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,
        Unknown = 0xff
    };

    // Values deliberately match SDL's standard semantic gamepad order.
    enum class GamepadButton : std::uint8_t {
        South = 0,
        East,
        West,
        North,
        Back,
        Guide,
        Start,
        LeftStick,
        RightStick,
        LeftShoulder,
        RightShoulder,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        Misc1,
        RightPaddle1,
        LeftPaddle1,
        RightPaddle2,
        LeftPaddle2,
        Touchpad,
        Misc2,
        Misc3,
        Misc4,
        Misc5,
        Misc6,
        Unknown = 0xff
    };

    enum class GamepadAxis : std::uint8_t {
        LeftX = 0,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger,
        Unknown = 0xff
    };

    enum class DeviceType {
        Keyboard,
        Mouse,
        GamepadButton,
        GamepadAxis
    };

    struct Binding {
        DeviceType device = DeviceType::Keyboard;
        std::string control = "A";
        float multiplier = 1.0f;
    };

    struct Action {
        std::string name;
        bool isAxis = false;
        std::vector<Binding> bindings;
        bool currentState = false;
        bool previousState = false;
        float currentValue = 0.0f;
        float previousValue = 0.0f;
    };

    static constexpr int ActionSchemaVersion = 2;

    static void Init(molga::WindowId mainWindowId);
    static void BeginFrame();
    static void Update();

    static InputSnapshot CaptureSnapshot(molga::WindowId sourceWindow,
                                         float mappedMouseX,
                                         float mappedMouseY,
                                         bool pointerValid);
    static void DiscardPendingScroll(molga::WindowId sourceWindow);
    static void ApplySnapshot(const InputSnapshot& snapshot);
    static void ReleaseAll();

    static void ProcessKeyEvent(molga::WindowId sourceWindow, KeyCode key,
                                bool pressed);
    static void ProcessMouseButtonEvent(molga::WindowId sourceWindow,
                                        MouseButton button, bool pressed);
    static void ProcessPointerMotion(molga::WindowId sourceWindow,
                                     float x, float y);
    static void ProcessScrollEvent(molga::WindowId sourceWindow,
                                   float xoffset, float yoffset);
    static void ProcessWindowFocus(molga::WindowId sourceWindow, bool focused);
    static void ProcessGamepadButtonEvent(GamepadButton button, bool pressed);
    static void ProcessGamepadAxisEvent(GamepadAxis axis, float value);
    static void ClearGamepad();

    static bool GetKey(KeyCode key);
    static bool GetKeyDown(KeyCode key);
    static bool GetKeyUp(KeyCode key);
    static bool GetMouseButton(MouseButton button);
    static bool GetMouseButtonDown(MouseButton button);
    static bool GetMouseButtonUp(MouseButton button);

    static float GetMouseX();
    static float GetMouseY();
    static float GetMouseDeltaX();
    static float GetMouseDeltaY();
    static bool HasCameraPointer();
    static unsigned int GetPointerCameraObjectId();
    static float GetCameraPointerX();
    static float GetCameraPointerY();
    static float GetWorldPointerX();
    static float GetWorldPointerY();
    static float GetScrollX();
    static float GetScrollY();

    static bool GetAction(const std::string& name);
    static bool GetActionDown(const std::string& name);
    static bool GetActionUp(const std::string& name);
    static float GetAxis(const std::string& name);

    static bool LoadActions(const std::string& filepath,
                            std::string* errorOut = nullptr);
    static bool DeserializeActions(const nlohmann::json& document,
                                   std::string* errorOut = nullptr);
    static nlohmann::json SerializeActions();
    static bool SaveActions(const std::string& filepath,
                            std::string* errorOut = nullptr);
    static bool MigrateLegacyDocument(const nlohmann::json& legacy,
                                      nlohmann::json& migrated,
                                      std::string& errorOut);
    static void InitializeDefaultActions();
    static std::vector<Action>& GetActions();
    static bool IsValidControl(DeviceType device, const std::string& control);
    static std::string DefaultControl(DeviceType device);

    static void SetKeyForTesting(KeyCode key, bool pressed);
    static void SetMouseButtonForTesting(MouseButton button, bool pressed);
    static void SetGamepadButtonForTesting(GamepadButton button, bool pressed);
    static void SetGamepadAxisForTesting(GamepadAxis axis, float value);
    static void AddScrollForTesting(molga::WindowId sourceWindow,
                                    float xoffset, float yoffset);
    static InputSnapshot ConsumeScrollForTesting(
        molga::WindowId sourceWindow, bool pointerValid = true);

private:
    static constexpr int MAX_KEYS = 512;
    static constexpr int MAX_MOUSE_BUTTONS = 8;
    static constexpr int MAX_GAMEPAD_BUTTONS = 26;
    static constexpr int MAX_GAMEPAD_AXES = 6;

    static void UpdateActions();
};
