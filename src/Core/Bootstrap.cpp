#include "Bootstrap.h"

#include <SDL3/SDL.h>

#include "Common/Log.h"
#include "Common/StdoutSink.h"
#include "MolgaTime.h"
#include "Systems/Audio.h"
#include "Systems/Input.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

struct EngineHost::Impl {
    SDL_Window* window = nullptr;
    std::unique_ptr<molga::GraphicsDevice> graphics;
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID gamepadId = 0;
    bool closeRequested = false;
    NativeEventObserver eventObserver;
};

namespace {

Input::MouseButton ToMouseButton(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return Input::MouseButton::Left;
        case SDL_BUTTON_RIGHT: return Input::MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return Input::MouseButton::Middle;
        case SDL_BUTTON_X1: return Input::MouseButton::X1;
        case SDL_BUTTON_X2: return Input::MouseButton::X2;
        default: return Input::MouseButton::Unknown;
    }
}

Input::GamepadButton ToGamepadButton(Uint8 button) {
    if (button >= static_cast<Uint8>(SDL_GAMEPAD_BUTTON_COUNT)) {
        return Input::GamepadButton::Unknown;
    }
    return static_cast<Input::GamepadButton>(button);
}

Input::GamepadAxis ToGamepadAxis(Uint8 axis) {
    if (axis >= static_cast<Uint8>(SDL_GAMEPAD_AXIS_COUNT)) {
        return Input::GamepadAxis::Unknown;
    }
    return static_cast<Input::GamepadAxis>(axis);
}

float NormalizeGamepadAxis(Input::GamepadAxis axis, Sint16 value) {
    if (axis == Input::GamepadAxis::LeftTrigger ||
        axis == Input::GamepadAxis::RightTrigger) {
        return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
    }
    const float denominator = value < 0 ? 32768.0f : 32767.0f;
    return std::clamp(static_cast<float>(value) / denominator, -1.0f, 1.0f);
}

void CloseGamepad(EngineHost::Impl& impl) {
    if (!impl.gamepad) return;
    SDL_CloseGamepad(impl.gamepad);
    impl.gamepad = nullptr;
    impl.gamepadId = 0;
    Input::ClearGamepad();
}

void OpenFirstGamepad(EngineHost::Impl& impl) {
    if (impl.gamepad) return;
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (!gamepads) return;
    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* opened = SDL_OpenGamepad(gamepads[i]);
        if (!opened) continue;
        impl.gamepad = opened;
        impl.gamepadId = gamepads[i];
        break;
    }
    SDL_free(gamepads);
}

} // namespace

EngineHost::EngineHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

EngineHost::~EngineHost() {
    if (!impl_) return;
    CloseGamepad(*impl_);
    Audio::Shutdown();
    Log::ClearSinks();
    impl_->graphics.reset();
    if (impl_->window) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    SDL_Quit();
}

void EngineHost::PollEvents() {
    if (!impl_) return;
    Input::BeginFrame();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (impl_->eventObserver) impl_->eventObserver(&event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                impl_->closeRequested = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(impl_->window)) {
                    impl_->closeRequested = true;
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                Input::ProcessWindowFocus(event.window.windowID, true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                Input::ProcessWindowFocus(event.window.windowID, false);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                if (event.window.windowID == SDL_GetWindowID(impl_->window)) {
                    impl_->graphics->ResizeViewport(
                        event.window.data1, event.window.data2);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                Input::ProcessKeyEvent(
                    event.key.windowID,
                    static_cast<Input::KeyCode>(event.key.scancode),
                    event.type == SDL_EVENT_KEY_DOWN);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                Input::ProcessPointerMotion(event.motion.windowID,
                                            event.motion.x, event.motion.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const Input::MouseButton button = ToMouseButton(event.button.button);
                if (button != Input::MouseButton::Unknown) {
                    Input::ProcessMouseButtonEvent(
                        event.button.windowID, button,
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                }
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    Input::ProcessScrollEvent(event.wheel.windowID,
                                              -event.wheel.x, -event.wheel.y);
                } else {
                    Input::ProcessScrollEvent(event.wheel.windowID,
                                              event.wheel.x, event.wheel.y);
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                OpenFirstGamepad(*impl_);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (impl_->gamepad && event.gdevice.which == impl_->gamepadId) {
                    CloseGamepad(*impl_);
                    OpenFirstGamepad(*impl_);
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (event.gbutton.which == impl_->gamepadId) {
                    const Input::GamepadButton button =
                        ToGamepadButton(event.gbutton.button);
                    if (button != Input::GamepadButton::Unknown) {
                        Input::ProcessGamepadButtonEvent(
                            button, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (event.gaxis.which == impl_->gamepadId) {
                    const Input::GamepadAxis axis = ToGamepadAxis(event.gaxis.axis);
                    if (axis != Input::GamepadAxis::Unknown) {
                        Input::ProcessGamepadAxisEvent(
                            axis, NormalizeGamepadAxis(axis, event.gaxis.value));
                    }
                }
                break;
            default:
                break;
        }
    }
}

void EngineHost::SwapBuffers() {
    if (impl_ && impl_->graphics) impl_->graphics->Present();
}

bool EngineHost::MakeContextCurrent() {
    return impl_ && impl_->graphics && impl_->graphics->MakeCurrent();
}

bool EngineHost::ShouldClose() const {
    return !impl_ || impl_->closeRequested;
}

void EngineHost::RequestClose() {
    if (impl_) impl_->closeRequested = true;
}

void EngineHost::SetTitle(const std::string& title) {
    if (impl_ && impl_->window) SDL_SetWindowTitle(impl_->window, title.c_str());
}

molga::WindowId EngineHost::WindowId() const {
    return impl_ && impl_->window ? SDL_GetWindowID(impl_->window) : 0;
}

molga::WindowMetrics EngineHost::Metrics() const {
    molga::WindowMetrics metrics;
    molga::QueryWindowMetrics(WindowId(), metrics);
    return metrics;
}

molga::WindowPointerState EngineHost::Pointer() const {
    molga::WindowPointerState pointer;
    molga::QueryWindowPointer(WindowId(), pointer);
    return pointer;
}

const molga::GraphicsDeviceInfo& EngineHost::GraphicsInfo() const {
    static const molga::GraphicsDeviceInfo unavailable{};
    return impl_ && impl_->graphics ? impl_->graphics->Info() : unavailable;
}

bool EngineHost::RenderCapabilityFrame(float r, float g, float b, float a) {
    return impl_ && impl_->graphics &&
           impl_->graphics->RenderCapabilityFrame(r, g, b, a);
}

void EngineHost::SetNativeEventObserver(NativeEventObserver observer) {
    if (impl_) impl_->eventObserver = std::move(observer);
}

void* EngineHost::NativeWindowHandle() const {
    return impl_ ? impl_->window : nullptr;
}

void* EngineHost::NativeGLContextHandle() const {
    return impl_ && impl_->graphics ? impl_->graphics->NativeContext() : nullptr;
}

void* EngineHost::NativeGraphicsDeviceHandle() const {
    return impl_ && impl_->graphics ? impl_->graphics->NativeDevice() : nullptr;
}

std::unique_ptr<EngineHost> EngineInit(const WindowConfig& config) {
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    if (config.graphicsBackend == molga::GraphicsBackend::OpenGL33) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                            SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.graphicsBackend == molga::GraphicsBackend::OpenGL33) {
        flags |= SDL_WINDOW_OPENGL;
    }
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (!config.visible) flags |= SDL_WINDOW_HIDDEN;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    auto impl = std::make_unique<EngineHost::Impl>();
    impl->window = SDL_CreateWindow(config.title.c_str(), config.width,
                                    config.height, flags);
    if (!impl->window) {
        std::cerr << "Failed to create SDL3 window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return nullptr;
    }

    std::string graphicsError;
    impl->graphics = molga::CreateGraphicsDevice(
        config.graphicsBackend, impl->window, config.graphicsValidation,
        graphicsError);
    if (!impl->graphics) {
        std::cerr << "Failed to initialize graphics device: "
                  << graphicsError << std::endl;
        SDL_DestroyWindow(impl->window);
        SDL_Quit();
        return nullptr;
    }

    Log::AddSink(std::make_shared<Log::StdoutSink>());
    Time::Init();
    Input::Init(SDL_GetWindowID(impl->window));
    Audio::Init();
    OpenFirstGamepad(*impl);

    return std::unique_ptr<EngineHost>(new EngineHost(std::move(impl)));
}

void EngineShutdown(std::unique_ptr<EngineHost>& host) {
    host.reset();
}

namespace molga {

bool QueryWindowMetrics(WindowId windowId, WindowMetrics& metrics) {
    metrics = {};
    SDL_Window* window = SDL_GetWindowFromID(windowId);
    if (!window) return false;
    if (!SDL_GetWindowSize(window, &metrics.logicalWidth, &metrics.logicalHeight) ||
        !SDL_GetWindowSizeInPixels(window, &metrics.pixelWidth,
                                   &metrics.pixelHeight)) {
        return false;
    }
    metrics.scaleX = metrics.logicalWidth > 0
        ? static_cast<float>(metrics.pixelWidth) /
              static_cast<float>(metrics.logicalWidth)
        : 1.0f;
    metrics.scaleY = metrics.logicalHeight > 0
        ? static_cast<float>(metrics.pixelHeight) /
              static_cast<float>(metrics.logicalHeight)
        : 1.0f;
    metrics.focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    return true;
}

bool QueryWindowPointer(WindowId windowId, WindowPointerState& pointer) {
    pointer = {};
    SDL_Window* window = SDL_GetWindowFromID(windowId);
    if (!window || (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) == 0) {
        return false;
    }

    float globalX = 0.0f;
    float globalY = 0.0f;
    const SDL_MouseButtonFlags buttons =
        SDL_GetGlobalMouseState(&globalX, &globalY);
    int windowX = 0;
    int windowY = 0;
    if (!SDL_GetWindowPosition(window, &windowX, &windowY)) return false;
    pointer.x = globalX - static_cast<float>(windowX);
    pointer.y = globalY - static_cast<float>(windowY);
    pointer.leftDown = (buttons & SDL_BUTTON_LMASK) != 0;
    pointer.valid = true;
    return true;
}

void RequestApplicationQuit() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

} // namespace molga
