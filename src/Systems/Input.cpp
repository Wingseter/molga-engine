#include "Input.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace {

constexpr int kMaxKeys = 512;
constexpr int kMaxMouseButtons = 8;
constexpr int kMaxGamepadButtons = 26;
constexpr int kMaxGamepadAxes = 6;

struct PendingScroll {
    float x = 0.0f;
    float y = 0.0f;
};

struct WindowInputState {
    std::array<bool, kMaxKeys> keys{};
    std::array<bool, kMaxMouseButtons> mouseButtons{};
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool focused = true;
};

molga::WindowId g_mainWindowId = 0;
std::unordered_map<molga::WindowId, WindowInputState> g_windows;
std::unordered_map<molga::WindowId, PendingScroll> g_pendingScroll;

std::array<bool, kMaxKeys> g_currentKeys{};
std::array<bool, kMaxKeys> g_previousKeys{};
std::array<bool, kMaxMouseButtons> g_currentMouseButtons{};
std::array<bool, kMaxMouseButtons> g_previousMouseButtons{};
std::array<bool, kMaxGamepadButtons> g_currentGamepadButtons{};
std::array<bool, kMaxGamepadButtons> g_previousGamepadButtons{};
std::array<float, kMaxGamepadAxes> g_currentGamepadAxes{};

std::vector<Input::Action> g_actions;
float g_mouseX = 0.0f;
float g_mouseY = 0.0f;
float g_lastMouseX = 0.0f;
float g_lastMouseY = 0.0f;
float g_mouseDeltaX = 0.0f;
float g_mouseDeltaY = 0.0f;
bool g_cameraPointerValid = false;
unsigned int g_pointerCameraObjectId = 0;
float g_cameraPointerX = 0.0f;
float g_cameraPointerY = 0.0f;
float g_worldPointerX = 0.0f;
float g_worldPointerY = 0.0f;
float g_scrollX = 0.0f;
float g_scrollY = 0.0f;

template<typename Enum>
std::size_t Index(Enum value) {
    return static_cast<std::size_t>(value);
}

const std::vector<std::pair<const char*, Input::KeyCode>>& KeyNames() {
    using K = Input::KeyCode;
    static const std::vector<std::pair<const char*, K>> names = {
        {"A", K::A}, {"B", K::B}, {"C", K::C}, {"D", K::D},
        {"E", K::E}, {"F", K::F}, {"G", K::G}, {"H", K::H},
        {"I", K::I}, {"J", K::J}, {"K", K::K}, {"L", K::L},
        {"M", K::M}, {"N", K::N}, {"O", K::O}, {"P", K::P},
        {"Q", K::Q}, {"R", K::R}, {"S", K::S}, {"T", K::T},
        {"U", K::U}, {"V", K::V}, {"W", K::W}, {"X", K::X},
        {"Y", K::Y}, {"Z", K::Z},
        {"1", K::Num1}, {"2", K::Num2}, {"3", K::Num3},
        {"4", K::Num4}, {"5", K::Num5}, {"6", K::Num6},
        {"7", K::Num7}, {"8", K::Num8}, {"9", K::Num9},
        {"0", K::Num0},
        {"Enter", K::Enter}, {"Escape", K::Escape},
        {"Backspace", K::Backspace}, {"Tab", K::Tab}, {"Space", K::Space},
        {"Minus", K::Minus}, {"Equal", K::Equal},
        {"LeftBracket", K::LeftBracket}, {"RightBracket", K::RightBracket},
        {"Backslash", K::Backslash}, {"Semicolon", K::Semicolon},
        {"Apostrophe", K::Apostrophe}, {"Grave", K::Grave},
        {"Comma", K::Comma}, {"Period", K::Period}, {"Slash", K::Slash},
        {"CapsLock", K::CapsLock},
        {"F1", K::F1}, {"F2", K::F2}, {"F3", K::F3}, {"F4", K::F4},
        {"F5", K::F5}, {"F6", K::F6}, {"F7", K::F7}, {"F8", K::F8},
        {"F9", K::F9}, {"F10", K::F10}, {"F11", K::F11}, {"F12", K::F12},
        {"F13", K::F13}, {"F14", K::F14}, {"F15", K::F15},
        {"F16", K::F16}, {"F17", K::F17}, {"F18", K::F18},
        {"F19", K::F19}, {"F20", K::F20}, {"F21", K::F21},
        {"F22", K::F22}, {"F23", K::F23}, {"F24", K::F24},
        {"PrintScreen", K::PrintScreen}, {"ScrollLock", K::ScrollLock},
        {"Pause", K::Pause}, {"Insert", K::Insert}, {"Home", K::Home},
        {"PageUp", K::PageUp}, {"Delete", K::Delete}, {"End", K::End},
        {"PageDown", K::PageDown}, {"Right", K::Right}, {"Left", K::Left},
        {"Down", K::Down}, {"Up", K::Up}, {"NumLock", K::NumLock},
        {"KeypadDivide", K::KeypadDivide}, {"KeypadMultiply", K::KeypadMultiply},
        {"KeypadMinus", K::KeypadMinus}, {"KeypadPlus", K::KeypadPlus},
        {"KeypadEnter", K::KeypadEnter}, {"Keypad1", K::Keypad1},
        {"Keypad2", K::Keypad2}, {"Keypad3", K::Keypad3},
        {"Keypad4", K::Keypad4}, {"Keypad5", K::Keypad5},
        {"Keypad6", K::Keypad6}, {"Keypad7", K::Keypad7},
        {"Keypad8", K::Keypad8}, {"Keypad9", K::Keypad9},
        {"Keypad0", K::Keypad0}, {"KeypadPeriod", K::KeypadPeriod},
        {"KeypadEqual", K::KeypadEqual}, {"Menu", K::Menu},
        {"LeftControl", K::LeftControl}, {"LeftShift", K::LeftShift},
        {"LeftAlt", K::LeftAlt}, {"LeftSuper", K::LeftSuper},
        {"RightControl", K::RightControl}, {"RightShift", K::RightShift},
        {"RightAlt", K::RightAlt}, {"RightSuper", K::RightSuper}
    };
    return names;
}

template<typename Enum>
bool ParseNamedControl(const std::vector<std::pair<const char*, Enum>>& names,
                       const std::string& text, Enum& result) {
    const auto it = std::find_if(names.begin(), names.end(),
        [&text](const auto& entry) { return text == entry.first; });
    if (it == names.end()) return false;
    result = it->second;
    return true;
}

const std::vector<std::pair<const char*, Input::MouseButton>>& MouseNames() {
    using B = Input::MouseButton;
    static const std::vector<std::pair<const char*, B>> names = {
        {"Left", B::Left}, {"Right", B::Right}, {"Middle", B::Middle},
        {"X1", B::X1}, {"X2", B::X2}
    };
    return names;
}

const std::vector<std::pair<const char*, Input::GamepadButton>>& GamepadButtonNames() {
    using B = Input::GamepadButton;
    static const std::vector<std::pair<const char*, B>> names = {
        {"South", B::South}, {"East", B::East}, {"West", B::West},
        {"North", B::North}, {"Back", B::Back}, {"Guide", B::Guide},
        {"Start", B::Start}, {"LeftStick", B::LeftStick},
        {"RightStick", B::RightStick}, {"LeftShoulder", B::LeftShoulder},
        {"RightShoulder", B::RightShoulder}, {"DpadUp", B::DpadUp},
        {"DpadDown", B::DpadDown}, {"DpadLeft", B::DpadLeft},
        {"DpadRight", B::DpadRight}, {"Misc1", B::Misc1},
        {"RightPaddle1", B::RightPaddle1}, {"LeftPaddle1", B::LeftPaddle1},
        {"RightPaddle2", B::RightPaddle2}, {"LeftPaddle2", B::LeftPaddle2},
        {"Touchpad", B::Touchpad}, {"Misc2", B::Misc2},
        {"Misc3", B::Misc3}, {"Misc4", B::Misc4},
        {"Misc5", B::Misc5}, {"Misc6", B::Misc6}
    };
    return names;
}

const std::vector<std::pair<const char*, Input::GamepadAxis>>& GamepadAxisNames() {
    using A = Input::GamepadAxis;
    static const std::vector<std::pair<const char*, A>> names = {
        {"LeftX", A::LeftX}, {"LeftY", A::LeftY},
        {"RightX", A::RightX}, {"RightY", A::RightY},
        {"LeftTrigger", A::LeftTrigger}, {"RightTrigger", A::RightTrigger}
    };
    return names;
}

std::string DeviceTypeName(Input::DeviceType type) {
    switch (type) {
        case Input::DeviceType::Keyboard: return "Keyboard";
        case Input::DeviceType::Mouse: return "Mouse";
        case Input::DeviceType::GamepadButton: return "GamepadButton";
        case Input::DeviceType::GamepadAxis: return "GamepadAxis";
    }
    return {};
}

bool ParseDeviceType(const std::string& text, Input::DeviceType& type) {
    if (text == "Keyboard") type = Input::DeviceType::Keyboard;
    else if (text == "Mouse") type = Input::DeviceType::Mouse;
    else if (text == "GamepadButton") type = Input::DeviceType::GamepadButton;
    else if (text == "GamepadAxis") type = Input::DeviceType::GamepadAxis;
    else return false;
    return true;
}

void ConsumeScroll(molga::WindowId sourceWindow, bool deliver,
                   InputSnapshot& snapshot) {
    const auto it = g_pendingScroll.find(sourceWindow);
    if (it == g_pendingScroll.end()) return;
    if (deliver) {
        snapshot.scrollX = it->second.x;
        snapshot.scrollY = it->second.y;
    }
    g_pendingScroll.erase(it);
}

bool ResolveKey(const std::string& control, Input::KeyCode& key) {
    return ParseNamedControl(KeyNames(), control, key);
}

bool ResolveMouse(const std::string& control, Input::MouseButton& button) {
    return ParseNamedControl(MouseNames(), control, button);
}

bool ResolveGamepadButton(const std::string& control,
                          Input::GamepadButton& button) {
    return ParseNamedControl(GamepadButtonNames(), control, button);
}

bool ResolveGamepadAxis(const std::string& control, Input::GamepadAxis& axis) {
    return ParseNamedControl(GamepadAxisNames(), control, axis);
}

std::string LegacyKeyboardControl(int code) {
    if (code >= 65 && code <= 90) return std::string(1, static_cast<char>(code));
    if (code >= 49 && code <= 57) return std::string(1, static_cast<char>(code));
    if (code == 48) return "0";
    switch (code) {
        case 32: return "Space"; case 39: return "Apostrophe";
        case 44: return "Comma"; case 45: return "Minus";
        case 46: return "Period"; case 47: return "Slash";
        case 59: return "Semicolon"; case 61: return "Equal";
        case 91: return "LeftBracket"; case 92: return "Backslash";
        case 93: return "RightBracket"; case 96: return "Grave";
        case 256: return "Escape"; case 257: return "Enter";
        case 258: return "Tab"; case 259: return "Backspace";
        case 260: return "Insert"; case 261: return "Delete";
        case 262: return "Right"; case 263: return "Left";
        case 264: return "Down"; case 265: return "Up";
        case 266: return "PageUp"; case 267: return "PageDown";
        case 268: return "Home"; case 269: return "End";
        case 280: return "CapsLock"; case 281: return "ScrollLock";
        case 282: return "NumLock"; case 283: return "PrintScreen";
        case 284: return "Pause"; case 320: return "Keypad0";
        case 321: return "Keypad1"; case 322: return "Keypad2";
        case 323: return "Keypad3"; case 324: return "Keypad4";
        case 325: return "Keypad5"; case 326: return "Keypad6";
        case 327: return "Keypad7"; case 328: return "Keypad8";
        case 329: return "Keypad9"; case 330: return "KeypadPeriod";
        case 331: return "KeypadDivide"; case 332: return "KeypadMultiply";
        case 333: return "KeypadMinus"; case 334: return "KeypadPlus";
        case 335: return "KeypadEnter"; case 336: return "KeypadEqual";
        case 340: return "LeftShift"; case 341: return "LeftControl";
        case 342: return "LeftAlt"; case 343: return "LeftSuper";
        case 344: return "RightShift"; case 345: return "RightControl";
        case 346: return "RightAlt"; case 347: return "RightSuper";
        case 348: return "Menu";
        default:
            if (code >= 290 && code <= 313) {
                return "F" + std::to_string(code - 289);
            }
            return {};
    }
}

std::string LegacyControl(Input::DeviceType device, int code) {
    static const char* mouse[] = {"Left", "Right", "Middle", "X1", "X2"};
    static const char* buttons[] = {
        "South", "East", "West", "North", "LeftShoulder", "RightShoulder",
        "Back", "Start", "Guide", "LeftStick", "RightStick", "DpadUp",
        "DpadRight", "DpadDown", "DpadLeft"
    };
    static const char* axes[] = {
        "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"
    };
    switch (device) {
        case Input::DeviceType::Keyboard: return LegacyKeyboardControl(code);
        case Input::DeviceType::Mouse:
            return code >= 0 && code < 5 ? mouse[code] : std::string{};
        case Input::DeviceType::GamepadButton:
            return code >= 0 && code < 15 ? buttons[code] : std::string{};
        case Input::DeviceType::GamepadAxis:
            return code >= 0 && code < 6 ? axes[code] : std::string{};
    }
    return {};
}

} // namespace

void Input::Init(molga::WindowId mainWindowId) {
    g_mainWindowId = mainWindowId;
    g_windows.clear();
    g_pendingScroll.clear();
    if (mainWindowId != 0) g_windows.emplace(mainWindowId, WindowInputState{});
    g_currentKeys.fill(false);
    g_previousKeys.fill(false);
    g_currentMouseButtons.fill(false);
    g_previousMouseButtons.fill(false);
    ClearGamepad();
    g_previousGamepadButtons.fill(false);
    g_mouseX = g_mouseY = g_lastMouseX = g_lastMouseY = 0.0f;
    g_mouseDeltaX = g_mouseDeltaY = 0.0f;
    g_scrollX = g_scrollY = 0.0f;
    g_cameraPointerValid = false;
    g_pointerCameraObjectId = 0;
}

void Input::BeginFrame() {
    g_previousKeys = g_currentKeys;
    g_previousMouseButtons = g_currentMouseButtons;
    g_previousGamepadButtons = g_currentGamepadButtons;
    for (auto& action : g_actions) {
        action.previousState = action.currentState;
        action.previousValue = action.currentValue;
    }
    g_scrollX = g_scrollY = 0.0f;
}

void Input::Update() {
    if (g_mainWindowId != 0) {
        const auto it = g_windows.find(g_mainWindowId);
        const float x = it != g_windows.end() ? it->second.mouseX : 0.0f;
        const float y = it != g_windows.end() ? it->second.mouseY : 0.0f;
        ApplySnapshot(CaptureSnapshot(g_mainWindowId, x, y, true));
        return;
    }
    InputSnapshot snapshot;
    snapshot.keys = g_currentKeys;
    snapshot.mouseButtons = g_currentMouseButtons;
    snapshot.gamepadButtons = g_currentGamepadButtons;
    snapshot.gamepadAxes = g_currentGamepadAxes;
    snapshot.mouseX = g_mouseX;
    snapshot.mouseY = g_mouseY;
    snapshot.pointerValid = true;
    ConsumeScroll(0, true, snapshot);
    ApplySnapshot(snapshot);
}

InputSnapshot Input::CaptureSnapshot(molga::WindowId sourceWindow,
                                     float mappedMouseX, float mappedMouseY,
                                     bool pointerValid) {
    InputSnapshot snapshot;
    snapshot.mouseX = mappedMouseX;
    snapshot.mouseY = mappedMouseY;
    snapshot.pointerValid = pointerValid;
    ConsumeScroll(sourceWindow, pointerValid, snapshot);
    const auto it = g_windows.find(sourceWindow);
    if (it != g_windows.end() && it->second.focused) {
        snapshot.keys = it->second.keys;
        if (pointerValid) snapshot.mouseButtons = it->second.mouseButtons;
    }
    snapshot.gamepadButtons = g_currentGamepadButtons;
    snapshot.gamepadAxes = g_currentGamepadAxes;
    return snapshot;
}

void Input::DiscardPendingScroll(molga::WindowId sourceWindow) {
    g_pendingScroll.erase(sourceWindow);
}

void Input::ApplySnapshot(const InputSnapshot& snapshot) {
    g_currentKeys = snapshot.keys;
    g_currentMouseButtons = snapshot.mouseButtons;
    g_currentGamepadButtons = snapshot.gamepadButtons;
    g_currentGamepadAxes = snapshot.gamepadAxes;
    g_mouseX = snapshot.mouseX;
    g_mouseY = snapshot.mouseY;
    g_mouseDeltaX = snapshot.pointerValid ? g_mouseX - g_lastMouseX : 0.0f;
    g_mouseDeltaY = snapshot.pointerValid ? g_mouseY - g_lastMouseY : 0.0f;
    g_lastMouseX = g_mouseX;
    g_lastMouseY = g_mouseY;
    g_cameraPointerValid = snapshot.pointerValid && snapshot.cameraPointerValid;
    if (g_cameraPointerValid) {
        g_pointerCameraObjectId = snapshot.pointerCameraObjectId;
        g_cameraPointerX = snapshot.cameraPointerX;
        g_cameraPointerY = snapshot.cameraPointerY;
        g_worldPointerX = snapshot.worldPointerX;
        g_worldPointerY = snapshot.worldPointerY;
    } else {
        g_pointerCameraObjectId = 0;
        g_cameraPointerX = g_cameraPointerY = 0.0f;
        g_worldPointerX = g_worldPointerY = 0.0f;
    }
    g_scrollX = snapshot.scrollX;
    g_scrollY = snapshot.scrollY;
    UpdateActions();
}

void Input::ReleaseAll() {
    InputSnapshot released;
    ApplySnapshot(released);
}

void Input::ProcessKeyEvent(molga::WindowId sourceWindow, KeyCode key,
                            bool pressed) {
    const std::size_t index = Index(key);
    if (index >= kMaxKeys) return;
    g_windows[sourceWindow].keys[index] = pressed;
}

void Input::ProcessMouseButtonEvent(molga::WindowId sourceWindow,
                                    MouseButton button, bool pressed) {
    const std::size_t index = Index(button);
    if (index >= kMaxMouseButtons) return;
    g_windows[sourceWindow].mouseButtons[index] = pressed;
}

void Input::ProcessPointerMotion(molga::WindowId sourceWindow,
                                 float x, float y) {
    auto& state = g_windows[sourceWindow];
    state.mouseX = x;
    state.mouseY = y;
}

void Input::ProcessScrollEvent(molga::WindowId sourceWindow,
                               float xoffset, float yoffset) {
    PendingScroll& pending = g_pendingScroll[sourceWindow];
    pending.x += xoffset;
    pending.y += yoffset;
}

void Input::ProcessWindowFocus(molga::WindowId sourceWindow, bool focused) {
    auto& state = g_windows[sourceWindow];
    state.focused = focused;
    if (!focused) {
        state.keys.fill(false);
        state.mouseButtons.fill(false);
        DiscardPendingScroll(sourceWindow);
        if (sourceWindow == g_mainWindowId) ReleaseAll();
    }
}

void Input::ProcessGamepadButtonEvent(GamepadButton button, bool pressed) {
    const std::size_t index = Index(button);
    if (index < g_currentGamepadButtons.size()) {
        g_currentGamepadButtons[index] = pressed;
    }
}

void Input::ProcessGamepadAxisEvent(GamepadAxis axis, float value) {
    const std::size_t index = Index(axis);
    if (index < g_currentGamepadAxes.size()) g_currentGamepadAxes[index] = value;
}

void Input::ClearGamepad() {
    g_currentGamepadButtons.fill(false);
    g_currentGamepadAxes.fill(0.0f);
}

bool Input::GetKey(KeyCode key) {
    const std::size_t index = Index(key);
    return index < g_currentKeys.size() && g_currentKeys[index];
}

bool Input::GetKeyDown(KeyCode key) {
    const std::size_t index = Index(key);
    return index < g_currentKeys.size() && g_currentKeys[index] &&
           !g_previousKeys[index];
}

bool Input::GetKeyUp(KeyCode key) {
    const std::size_t index = Index(key);
    return index < g_currentKeys.size() && !g_currentKeys[index] &&
           g_previousKeys[index];
}

bool Input::GetMouseButton(MouseButton button) {
    const std::size_t index = Index(button);
    return index < g_currentMouseButtons.size() && g_currentMouseButtons[index];
}

bool Input::GetMouseButtonDown(MouseButton button) {
    const std::size_t index = Index(button);
    return index < g_currentMouseButtons.size() && g_currentMouseButtons[index] &&
           !g_previousMouseButtons[index];
}

bool Input::GetMouseButtonUp(MouseButton button) {
    const std::size_t index = Index(button);
    return index < g_currentMouseButtons.size() && !g_currentMouseButtons[index] &&
           g_previousMouseButtons[index];
}

float Input::GetMouseX() { return g_mouseX; }
float Input::GetMouseY() { return g_mouseY; }
float Input::GetMouseDeltaX() { return g_mouseDeltaX; }
float Input::GetMouseDeltaY() { return g_mouseDeltaY; }
bool Input::HasCameraPointer() { return g_cameraPointerValid; }
unsigned int Input::GetPointerCameraObjectId() { return g_pointerCameraObjectId; }
float Input::GetCameraPointerX() { return g_cameraPointerX; }
float Input::GetCameraPointerY() { return g_cameraPointerY; }
float Input::GetWorldPointerX() { return g_worldPointerX; }
float Input::GetWorldPointerY() { return g_worldPointerY; }
float Input::GetScrollX() { return g_scrollX; }
float Input::GetScrollY() { return g_scrollY; }

void Input::UpdateActions() {
    for (auto& action : g_actions) {
        action.currentState = false;
        action.currentValue = 0.0f;
        for (const Binding& binding : action.bindings) {
            float value = 0.0f;
            bool pressed = false;
            if (binding.device == DeviceType::Keyboard) {
                KeyCode key = KeyCode::Unknown;
                pressed = ResolveKey(binding.control, key) && GetKey(key);
                value = pressed ? binding.multiplier : 0.0f;
            } else if (binding.device == DeviceType::Mouse) {
                MouseButton button = MouseButton::Unknown;
                pressed = ResolveMouse(binding.control, button) &&
                          GetMouseButton(button);
                value = pressed ? binding.multiplier : 0.0f;
            } else if (binding.device == DeviceType::GamepadButton) {
                GamepadButton button = GamepadButton::Unknown;
                if (ResolveGamepadButton(binding.control, button)) {
                    const std::size_t index = Index(button);
                    pressed = index < g_currentGamepadButtons.size() &&
                              g_currentGamepadButtons[index];
                    value = pressed ? binding.multiplier : 0.0f;
                }
            } else {
                GamepadAxis axis = GamepadAxis::Unknown;
                if (ResolveGamepadAxis(binding.control, axis)) {
                    const std::size_t index = Index(axis);
                    const float raw = index < g_currentGamepadAxes.size()
                        ? g_currentGamepadAxes[index] : 0.0f;
                    if (std::abs(raw) > 0.15f) {
                        value = raw * binding.multiplier;
                        pressed = std::abs(raw) > 0.5f;
                    }
                }
            }
            action.currentValue += value;
            action.currentState = action.currentState || pressed;
        }
        if (action.isAxis) {
            action.currentValue = std::clamp(action.currentValue, -1.0f, 1.0f);
        }
    }
}

bool Input::GetAction(const std::string& name) {
    const auto it = std::find_if(g_actions.begin(), g_actions.end(),
        [&name](const Action& action) { return action.name == name; });
    return it != g_actions.end() && it->currentState;
}

bool Input::GetActionDown(const std::string& name) {
    const auto it = std::find_if(g_actions.begin(), g_actions.end(),
        [&name](const Action& action) { return action.name == name; });
    return it != g_actions.end() && it->currentState && !it->previousState;
}

bool Input::GetActionUp(const std::string& name) {
    const auto it = std::find_if(g_actions.begin(), g_actions.end(),
        [&name](const Action& action) { return action.name == name; });
    return it != g_actions.end() && !it->currentState && it->previousState;
}

float Input::GetAxis(const std::string& name) {
    const auto it = std::find_if(g_actions.begin(), g_actions.end(),
        [&name](const Action& action) { return action.name == name; });
    return it != g_actions.end() ? it->currentValue : 0.0f;
}

bool Input::IsValidControl(DeviceType device, const std::string& control) {
    switch (device) {
        case DeviceType::Keyboard: {
            KeyCode value = KeyCode::Unknown;
            return ResolveKey(control, value);
        }
        case DeviceType::Mouse: {
            MouseButton value = MouseButton::Unknown;
            return ResolveMouse(control, value);
        }
        case DeviceType::GamepadButton: {
            GamepadButton value = GamepadButton::Unknown;
            return ResolveGamepadButton(control, value);
        }
        case DeviceType::GamepadAxis: {
            GamepadAxis value = GamepadAxis::Unknown;
            return ResolveGamepadAxis(control, value);
        }
    }
    return false;
}

std::string Input::DefaultControl(DeviceType device) {
    switch (device) {
        case DeviceType::Keyboard: return "A";
        case DeviceType::Mouse: return "Left";
        case DeviceType::GamepadButton: return "South";
        case DeviceType::GamepadAxis: return "LeftX";
    }
    return {};
}

bool Input::DeserializeActions(const nlohmann::json& document,
                               std::string* errorOut) {
    auto fail = [errorOut](const std::string& error) {
        if (errorOut) *errorOut = error;
        return false;
    };
    if (!document.is_object()) {
        return fail("INPUT_SCHEMA_MIGRATION_REQUIRED: expected input schema v2 object; run molga_migrate input --project <path> --apply");
    }
    if (!document.contains("schemaVersion") ||
        !document["schemaVersion"].is_number_integer() ||
        document["schemaVersion"] != ActionSchemaVersion) {
        return fail("INPUT_SCHEMA_MIGRATION_REQUIRED: unsupported input schema; run molga_migrate input --project <path> --apply");
    }
    if (!document.contains("actions") || !document["actions"].is_array()) {
        return fail("input schema v2 requires an actions array");
    }

    std::vector<Action> parsed;
    for (const auto& item : document["actions"]) {
        if (!item.is_object() || !item.contains("name") ||
            !item["name"].is_string() || item["name"].get<std::string>().empty() ||
            !item.contains("isAxis") || !item["isAxis"].is_boolean() ||
            !item.contains("bindings") || !item["bindings"].is_array()) {
            return fail("invalid input action in schema v2 document");
        }
        Action action;
        action.name = item["name"].get<std::string>();
        action.isAxis = item["isAxis"].get<bool>();
        for (const auto& bindingJson : item["bindings"]) {
            if (!bindingJson.is_object() ||
                !bindingJson.contains("device") ||
                !bindingJson["device"].is_string() ||
                !bindingJson.contains("control") ||
                !bindingJson["control"].is_string()) {
                return fail("invalid input binding in schema v2 document");
            }
            Binding binding;
            if (!ParseDeviceType(bindingJson["device"].get<std::string>(),
                                 binding.device)) {
                return fail("unknown input device type");
            }
            binding.control = bindingJson["control"].get<std::string>();
            if (!IsValidControl(binding.device, binding.control)) {
                return fail("unknown input control '" + binding.control + "'");
            }
            if (bindingJson.contains("multiplier")) {
                if (!bindingJson["multiplier"].is_number()) {
                    return fail("input binding multiplier must be numeric");
                }
                binding.multiplier = bindingJson["multiplier"].get<float>();
            }
            if (!std::isfinite(binding.multiplier)) {
                return fail("input binding multiplier must be finite");
            }
            action.bindings.push_back(std::move(binding));
        }
        parsed.push_back(std::move(action));
    }
    g_actions = std::move(parsed);
    if (errorOut) errorOut->clear();
    return true;
}

nlohmann::json Input::SerializeActions() {
    nlohmann::json actions = nlohmann::json::array();
    for (const Action& action : g_actions) {
        nlohmann::json bindings = nlohmann::json::array();
        for (const Binding& binding : action.bindings) {
            bindings.push_back({
                {"device", DeviceTypeName(binding.device)},
                {"control", binding.control},
                {"multiplier", binding.multiplier}
            });
        }
        actions.push_back({
            {"name", action.name},
            {"isAxis", action.isAxis},
            {"bindings", std::move(bindings)}
        });
    }
    return {{"schemaVersion", ActionSchemaVersion}, {"actions", actions}};
}

bool Input::LoadActions(const std::string& filepath, std::string* errorOut) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        if (errorOut) *errorOut = "could not open input actions: " + filepath;
        return false;
    }
    try {
        nlohmann::json document;
        file >> document;
        return DeserializeActions(document, errorOut);
    } catch (const std::exception& error) {
        if (errorOut) *errorOut = std::string("failed to parse input actions: ") + error.what();
        return false;
    }
}

bool Input::SaveActions(const std::string& filepath, std::string* errorOut) {
    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open()) {
        if (errorOut) *errorOut = "could not write input actions: " + filepath;
        return false;
    }
    file << SerializeActions().dump(4) << '\n';
    if (!file.good()) {
        if (errorOut) *errorOut = "failed while writing input actions: " + filepath;
        return false;
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool Input::MigrateLegacyDocument(const nlohmann::json& legacy,
                                  nlohmann::json& migrated,
                                  std::string& errorOut) {
    if (legacy.is_object()) {
        const auto schema = legacy.find("schemaVersion");
        if (schema == legacy.end() || !schema->is_number_integer() ||
            *schema != ActionSchemaVersion) {
            errorOut = "input document must be a legacy array or schema v2 object";
            return false;
        }
        std::vector<Action> saved = g_actions;
        if (!DeserializeActions(legacy, &errorOut)) {
            g_actions = std::move(saved);
            return false;
        }
        g_actions = std::move(saved);
        migrated = legacy;
        errorOut.clear();
        return true;
    }
    if (!legacy.is_array()) {
        errorOut = "legacy input document must be an array";
        return false;
    }
    nlohmann::json actions = nlohmann::json::array();
    for (std::size_t actionIndex = 0; actionIndex < legacy.size(); ++actionIndex) {
        const auto& oldAction = legacy[actionIndex];
        if (!oldAction.is_object() || !oldAction.contains("name") ||
            !oldAction["name"].is_string() ||
            oldAction["name"].get<std::string>().empty() ||
            (oldAction.contains("isAxis") && !oldAction["isAxis"].is_boolean()) ||
            !oldAction.contains("bindings") ||
            !oldAction["bindings"].is_array()) {
            errorOut = "invalid legacy action at index " + std::to_string(actionIndex);
            return false;
        }
        nlohmann::json bindings = nlohmann::json::array();
        for (std::size_t bindingIndex = 0;
             bindingIndex < oldAction["bindings"].size(); ++bindingIndex) {
            const auto& oldBinding = oldAction["bindings"][bindingIndex];
            if (!oldBinding.is_object() || !oldBinding.contains("device") ||
                !oldBinding["device"].is_string() ||
                !oldBinding.contains("code") ||
                !oldBinding["code"].is_number_integer()) {
                errorOut = "invalid legacy binding at action " +
                    std::to_string(actionIndex) + ", binding " +
                    std::to_string(bindingIndex);
                return false;
            }
            DeviceType device = DeviceType::Keyboard;
            const std::string deviceName = oldBinding["device"].get<std::string>();
            if (!ParseDeviceType(deviceName, device)) {
                errorOut = "unknown legacy device '" + deviceName + "'";
                return false;
            }
            int code = 0;
            if (oldBinding["code"].is_number_unsigned()) {
                const auto raw = oldBinding["code"].get<std::uint64_t>();
                if (raw > static_cast<std::uint64_t>(
                              std::numeric_limits<int>::max())) {
                    errorOut = "legacy code is outside the supported integer range";
                    return false;
                }
                code = static_cast<int>(raw);
            } else {
                const auto raw = oldBinding["code"].get<std::int64_t>();
                if (raw < std::numeric_limits<int>::min() ||
                    raw > std::numeric_limits<int>::max()) {
                    errorOut = "legacy code is outside the supported integer range";
                    return false;
                }
                code = static_cast<int>(raw);
            }
            const std::string control = LegacyControl(device, code);
            if (control.empty() || !IsValidControl(device, control)) {
                errorOut = "unsupported legacy code " + std::to_string(code) +
                    " for " + deviceName + " at action " +
                    std::to_string(actionIndex) + ", binding " +
                    std::to_string(bindingIndex);
                return false;
            }
            float multiplier = 1.0f;
            if (oldBinding.contains("multiplier")) {
                if (!oldBinding["multiplier"].is_number()) {
                    errorOut = "non-numeric legacy multiplier at action " +
                        std::to_string(actionIndex) + ", binding " +
                        std::to_string(bindingIndex);
                    return false;
                }
                multiplier = oldBinding["multiplier"].get<float>();
            }
            if (!std::isfinite(multiplier)) {
                errorOut = "non-finite legacy multiplier";
                return false;
            }
            bindings.push_back({
                {"device", deviceName}, {"control", control},
                {"multiplier", multiplier}
            });
        }
        actions.push_back({
            {"name", oldAction["name"].get<std::string>()},
            {"isAxis", oldAction.value("isAxis", false)},
            {"bindings", std::move(bindings)}
        });
    }
    migrated = {{"schemaVersion", ActionSchemaVersion}, {"actions", actions}};
    std::vector<Action> saved = g_actions;
    const bool valid = DeserializeActions(migrated, &errorOut);
    g_actions = std::move(saved);
    return valid;
}

void Input::InitializeDefaultActions() {
    g_actions = {
        {"Horizontal", true,
         {{DeviceType::Keyboard, "D", 1.0f},
          {DeviceType::Keyboard, "A", -1.0f},
          {DeviceType::GamepadAxis, "LeftX", 1.0f}}},
        {"Vertical", true,
         {{DeviceType::Keyboard, "W", 1.0f},
          {DeviceType::Keyboard, "S", -1.0f},
          {DeviceType::GamepadAxis, "LeftY", -1.0f}}},
        {"Jump", false,
         {{DeviceType::Keyboard, "Space", 1.0f},
          {DeviceType::GamepadButton, "South", 1.0f}}},
        {"Fire", false,
         {{DeviceType::Mouse, "Left", 1.0f},
          {DeviceType::GamepadButton, "East", 1.0f}}}
    };
}

std::vector<Input::Action>& Input::GetActions() { return g_actions; }

void Input::SetKeyForTesting(KeyCode key, bool pressed) {
    const std::size_t index = Index(key);
    if (index < g_currentKeys.size()) g_currentKeys[index] = pressed;
}

void Input::SetMouseButtonForTesting(MouseButton button, bool pressed) {
    const std::size_t index = Index(button);
    if (index < g_currentMouseButtons.size()) g_currentMouseButtons[index] = pressed;
}

void Input::SetGamepadButtonForTesting(GamepadButton button, bool pressed) {
    ProcessGamepadButtonEvent(button, pressed);
}

void Input::SetGamepadAxisForTesting(GamepadAxis axis, float value) {
    ProcessGamepadAxisEvent(axis, value);
}

void Input::AddScrollForTesting(molga::WindowId sourceWindow,
                                float xoffset, float yoffset) {
    ProcessScrollEvent(sourceWindow, xoffset, yoffset);
}

InputSnapshot Input::ConsumeScrollForTesting(molga::WindowId sourceWindow,
                                             bool pointerValid) {
    InputSnapshot snapshot;
    snapshot.pointerValid = pointerValid;
    ConsumeScroll(sourceWindow, pointerValid, snapshot);
    return snapshot;
}
