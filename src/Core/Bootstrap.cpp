#include "Bootstrap.h"

#include <SDL3/SDL.h>

#include "Common/Log.h"
#include "Common/StdoutSink.h"
#include "Core/TextureManager.h"
#include "Rendering/Renderer.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/TextureBindingRegistry.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIRuntimeInvalidation.h"
#include "UI/UISystem.h"
#include "MolgaTime.h"
#include "Systems/Audio.h"
#include "Systems/Input.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

namespace {

// 널이면 아무 일도 하지 않는다. Renderer.h의 종료 단계 hook과 같은 모양이다.
molga::detail::EngineShutdownStageHook g_engineShutdownStageHook = nullptr;

void ReportShutdownBlocker(molga::text::TextDiagnosticSink& sink,
                           std::string message, std::string remediation) {
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = molga::text::TextDiagnosticCode::ReferenceInvalid;
    diagnostic.severity = molga::text::TextSeverity::Error;
    diagnostic.subsystem = "engine.shutdown";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

} // namespace

namespace molga {
namespace detail {

void SetEngineShutdownStageHookForTest(
    EngineShutdownStageHook hook) noexcept {
    g_engineShutdownStageHook = hook;
}

void NotifyEngineShutdownStage(const char* stage) noexcept {
    if (g_engineShutdownStageHook != nullptr) g_engineShutdownStageHook(stage);
}

} // namespace detail
} // namespace molga

struct EngineHost::Impl {
    SDL_Window* window = nullptr;
    std::unique_ptr<molga::GraphicsDevice> graphics;

    // ── Task 11.2 Step 7g: 단계는 멱등하고 순서가 고정되어 있다 ─────────────
    // 처음 성공한 GPU drain은 재시도를 건너 유지된다. 실패한 재시도가 이미
    // 증명된 idle을 다시 기다리면, 그 두 번째 기다림은 첫 번째가 증명한 것을
    // 다시 증명하지 못할 수도 있다(장치는 그 사이에 아무 일도 하지 않았다).
    enum class TeardownPhase : std::uint8_t {
        Running,
        Drained,
        InternalOwnersReleased,
        Complete,
    };
    TeardownPhase teardownPhase = TeardownPhase::Running;
    // 이름만 든다. 소유자는 진입점이다.
    Renderer* renderer = nullptr;
    TextRenderer* textRenderer = nullptr;
    std::vector<std::function<void()>> snapshotReleasers;
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
    // ── Task 11.2 Step 7f: 우회로가 조용하지 않게 한다 ──────────────────────
    // 소멸자는 실패할 수 없으므로 막지 못한다. 그러나 GPU 소비자를 등록한
    // host가 TearDownGpu 없이 여기 도달했다면 그것은 계약 위반이고, 그
    // 사실이 로그에 남지 않으면 EngineShutdown의 상태 계약은 지켜지든 말든
    // 아무 차이가 없다.
    //
    // ── Task 11.2 close-out: 감지기를 등록에 매달지 않는다 ─────────────────
    // 예전 조건은 renderer/textRenderer 등록에 걸려 있었다. 그런데 이 감지기가
    // 존재하는 이유인 **우회로**(등록 없이 종료에 도달하는 여섯 진입점)에서는
    // 두 포인터가 모두 널이므로, 방어가 자기가 막으려던 경우에만 눈을 감았다.
    // 이 host가 실제로 GPU 상태를 소유했다는 사실은 등록이 아니라 장치가
    // 말한다: 완료된 teardown은 언제나 그 장치를 부수고 가므로, 살아 있는
    // 장치를 든 채 여기 도달했다는 것은 그 자체가 우회로다.
    const bool ownedLiveGpuState =
        impl_->renderer != nullptr || impl_->textRenderer != nullptr ||
        (impl_->graphics != nullptr && !impl_->graphics->IsDestroyed());
    if (ownedLiveGpuState &&
        impl_->teardownPhase != Impl::TeardownPhase::Complete) {
        Log::Error("EngineShutdown",
                   "the engine host was destroyed without a completed GPU "
                   "teardown; call EngineShutdown(host, sink) and retry until "
                   "it returns Complete instead of letting the host die");
    }
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
                // SDL_GPU swapchain dimensions are acquired per-frame. No
                // mutable global viewport state is carried across resizes.
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

molga::GraphicsDevice& EngineHost::Graphics() { return *impl_->graphics; }

const molga::GraphicsDevice& EngineHost::Graphics() const {
    return *impl_->graphics;
}

molga::BeginFrameResult EngineHost::BeginFrame() {
    if (!impl_ || !impl_->graphics) {
        molga::BeginFrameResult result;
        result.error = "engine host has no graphics device";
        return result;
    }
    return impl_->graphics->BeginFrame(WindowId());
}

bool EngineHost::RenderCapabilityFrame(float r, float g, float b, float a) {
    return impl_ && impl_->graphics &&
           impl_->graphics->RenderCapabilityFrame(r, g, b, a, nullptr);
}

void EngineHost::SetNativeEventObserver(NativeEventObserver observer) {
    if (impl_) impl_->eventObserver = std::move(observer);
}

void* EngineHost::NativeWindowHandle() const {
    return impl_ ? impl_->window : nullptr;
}

std::unique_ptr<EngineHost> EngineInit(const WindowConfig& config) {
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
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

    // ── Task 11.2 Step 7d: 장치 세대가 바뀐 그 자리에서 알린다 ─────────────
    // 배치 시스템은 다음 Build에서도 이 변화를 알아채지만, 그 사이에 남아
    // 있는 옛 장치의 스냅샷이 다른 소비자(Game View의 마지막 UIFrameResult)로
    // 나갈 수 있다. 알림은 장치를 만드는 이 자리에 있어야 한다.
    const std::uint64_t previousDeviceGeneration =
        molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
    std::string graphicsError;
    impl->graphics = molga::CreateGraphicsDevice(
        impl->window, config.graphicsValidation, graphicsError);
    if (impl->graphics) {
        // GraphicsDevice::Create가 이미 취득해 게시한 그 값을 그대로 넘긴다.
        // 여기서 축을 다시 올리면 방금 만든 장치가 그 자리에서 낡은다.
        UISystem::Get().OnDeviceGenerationChanged(previousDeviceGeneration,
                                                  impl->graphics->Generation());
    }
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

void EngineHost::RegisterGpuConsumers(Renderer* renderer,
                                     TextRenderer* textRenderer) {
    if (!impl_) return;
    // 반쪽 등록은 받지 않는다. renderer만 등록되면 종료가 텍스트 단계를
    // 건너뛰고, 텍스트만 등록되면 idle을 증명하지 않은 채 atlas를 부순다.
    // 둘 다 널인 것은 적법하다 — GPU 소비자가 없는 host(플랫폼 전용
    // 픽스처)가 그 모양이다.
    if ((renderer == nullptr) != (textRenderer == nullptr)) {
        Log::Error("EngineShutdown",
                   "RegisterGpuConsumers needs both the renderer and the text "
                   "renderer or neither; a half registration would make the "
                   "shutdown order skip one of its two halves");
        return;
    }
    impl_->renderer = renderer;
    impl_->textRenderer = textRenderer;
}

void EngineHost::RegisterEngineSnapshotReleaser(std::function<void()> releaser) {
    if (!impl_ || !releaser) return;
    impl_->snapshotReleasers.push_back(std::move(releaser));
}

// ── Task 11.2 Step 7g: 단계 기계 ────────────────────────────────────────────
// 이 함수가 정하는 것은 **엔진 쪽 소유자 해제와 외부 소유자 검사의 자리**와
// 장치 파괴 하나뿐이다. drain -> 반납 -> [여기] -> 텍스트 -> 은퇴 바인딩 ->
// renderer 자원 이라는 순서 자체는 ShutdownRendererThenTextGpuResources 한
// 곳에 있고, 이 함수는 그 가운데 자리에 자기 단계를 끼워 넣는다. 순서를 여기
// 다시 적으면 두 벌이 되고, 언젠가 한 벌만 고쳐진다.
EngineShutdownStatus EngineHost::TearDownGpu(
    molga::text::TextDiagnosticSink& sink) {
    if (!impl_) return EngineShutdownStatus::Complete;
    Impl& impl = *impl_;
    if (impl.teardownPhase == Impl::TeardownPhase::Complete) {
        return EngineShutdownStatus::Complete;
    }
    // 장치 세대는 여러 단계가 함께 쓰는 값이다. 단계 사이에 다시 읽으면
    // 마지막 단계가 이미 파괴된 장치에게 물어보게 된다.
    const std::uint64_t deviceGeneration =
        impl.graphics ? impl.graphics->Generation() : 0U;

    // 가운데 자리. 참이면 계속 부수고, 거짓이면 아무것도 부수지 않는다.
    EngineShutdownStatus blocked = EngineShutdownStatus::Complete;
    const auto releaseInternalOwners = [&]() -> bool {
        if (impl.teardownPhase == Impl::TeardownPhase::Running) {
            impl.teardownPhase = Impl::TeardownPhase::Drained;
        }
        if (impl.teardownPhase == Impl::TeardownPhase::Drained) {
            UISystem::Get().ClearFullSnapshotBindingCache(deviceGeneration);
            molga::detail::NotifyEngineShutdownStage(
                "ClearFullSnapshotBindingCache");
            for (auto& releaser : impl.snapshotReleasers) {
                if (releaser) releaser();
            }
            molga::detail::NotifyEngineShutdownStage("ReleaseEngineSnapshots");
            TextureManager::Get().ReleaseBindings(deviceGeneration);
            molga::detail::NotifyEngineShutdownStage(
                "ReleaseTextureManagerBindings");
            impl.teardownPhase = Impl::TeardownPhase::InternalOwnersReleased;
        }
        // ── 계획된 Step 7g에서의 의도적 이탈, 이유와 함께 ───────────────
        // 계획서는 여기서 외부 **텍스처 바인딩** 소유자도 함께 센다. 이
        // 엔진에서는 그렇게 할 수 없다: glyph atlas의 page는 Texture이고,
        // Texture는 업로드할 때마다 바인딩 수명 토큰을 게시한다(Task 11.1의
        // 소유 규칙). 그래서 atlas를 놓기 전에 세면 **엔진 자신의 atlas가
        // 외부 소유자로 잡히고**, 종료는 언제나 여기서 막힌다. 실제로 그렇게
        // 막혔고, 그것이 이 이탈의 근거다.
        //
        // 바인딩 검사는 그 대신 그것이 지키는 파괴 바로 앞으로 옮겼다
        // (ShutdownRendererThenTextGpuResources의 DestroyRetiredBindings).
        // 외부 소유자가 지켜야 하는 것은 그 핸들의 파괴이고, 그 앞의 두
        // 단계(atlas 해제, 텍스트 서비스 파괴)는 그 핸들을 건드리지 않는다.
        //
        // 외부 **glyph page** 토큰은 여기 그대로 남는다. 그 토큰이 지키는
        // 것은 바로 다음 단계인 atlas 해제이기 때문이다.
        if (impl.textRenderer &&
            impl.textRenderer->GlyphAtlas().LiveExternalPagePinCount() != 0U) {
            molga::detail::NotifyEngineShutdownStage(
                "ExternalGlyphPageOwnerBlocked");
            ReportShutdownBlocker(
                sink,
                "an external glyph atlas page token is still held after the "
                "engine released its own owners; nothing was destroyed",
                "release the external glyph page tokens and retry "
                "EngineShutdown through the same host");
            blocked = EngineShutdownStatus::ExternalGpuLifetime;
            return false;
        }
        return true;
    };

    // ── Task 11.2 close-out: 등록되지 않은 GPU 소비자도 부순다 ─────────────
    // "등록이 없다"는 "부술 것이 없다"의 증명이 아니다. 프로세스 텍스트
    // renderer는 TextRenderer::Get()이 만들며 RegisterGpuConsumers와 무관하게
    // 존재한다 — 그리고 여섯 개의 이른 종료 경로(main.cpp의 둘,
    // runtime_main.cpp의 넷)가 정확히 그 상태로 여기 온다. 그중
    // runtime_main.cpp의 TextRenderer::Get().Init() 실패 경로는 인스턴스가
    // **이미 만들어진 뒤**이므로, 이 단계를 건너뛰면 진입점의 ICU guard가
    // 살아 있는 텍스트 renderer 위에서 u_cleanup을 돌린다. Task 8.2의 순서가
    // 막으려던 바로 그 역전이다.
    //
    // 그래서 Complete는 두 경우에만 나온다: 부술 것이 없었거나, 부쉈거나.
    const auto tearDownUnregisteredGpuState = [&]() -> bool {
        TextRenderer* processText = TextRenderer::ProcessInstanceOrNull();
        if (!processText && !impl.graphics) return true;
        // 1. idle 증명. 등록된 renderer가 없으므로 장치에게 직접 묻는다.
        //    (renderer가 있었다면 공유 함수의 drain이 이 자리를 대신한다.)
        if (impl.graphics && !impl.graphics->IsDestroyed()) {
            std::string waitError;
            if (!impl.graphics->WaitIdle(&waitError)) {
                molga::detail::NotifyEngineShutdownStage("WaitIdleFailed");
                ReportShutdownBlocker(
                    sink,
                    "the GPU idle wait failed on a host with no registered GPU "
                    "consumers, so engine shutdown destroyed nothing: " +
                        waitError,
                    "retry EngineShutdown through the same owning host once "
                    "the device can be drained");
                blocked = EngineShutdownStatus::GpuDrainFailed;
                return false;
            }
            molga::detail::NotifyEngineShutdownStage("WaitIdle");
        }
        // 2. 프로세스 텍스트 renderer. 거절하면 아무것도 더 부수지 않는다 —
        //    사유는 ShutdownAfterGpuIdle이 sink로 낸다.
        if (processText) {
            if (!processText->ShutdownAfterGpuIdle(sink)) {
                molga::detail::NotifyEngineShutdownStage(
                    "ExternalGlyphPageOwnerBlocked");
                ReportShutdownBlocker(
                    sink,
                    "text GPU teardown was refused on a host with no "
                    "registered GPU consumers; nothing was destroyed",
                    "release the external glyph page owners and retry "
                    "EngineShutdown through the same host");
                blocked = EngineShutdownStatus::ExternalGpuLifetime;
                return false;
            }
            molga::TextureBindingRegistry::Get().SweepRetiredBindings();
            TextRenderer::DestroyProcessInstance();
        }
        // 3. 은퇴한 바인딩. 살아 있는 외부 토큰이 있으면 아무것도 부수지
        //    않고 거짓이다.
        if (deviceGeneration != 0U) {
            std::string destroyError;
            if (!molga::TextureBindingRegistry::Get().DestroyRetiredBindings(
                    deviceGeneration, destroyError)) {
                molga::detail::NotifyEngineShutdownStage(
                    "ExternalBindingOwnerBlocked");
                ReportShutdownBlocker(
                    sink, destroyError,
                    "release the external binding lifetime owners and retry "
                    "EngineShutdown through the same host");
                blocked = EngineShutdownStatus::ExternalGpuLifetime;
                return false;
            }
            molga::detail::NotifyEngineShutdownStage(
                "DestroyRetiredTextureBindings");
        }
        return true;
    };

    if (impl.renderer && impl.textRenderer) {
        if (!ShutdownRendererThenTextGpuResources(
                *impl.renderer, *impl.textRenderer, deviceGeneration, sink,
                releaseInternalOwners)) {
            if (blocked != EngineShutdownStatus::Complete) return blocked;
            // 공유 함수가 거절했는데 가운데 자리는 통과했다. 남는 원인은
            // 둘뿐이다: drain 실패, 또는 텍스트/바인딩 teardown 거절.
            if (!impl.renderer->HasProvenGpuIdle()) {
                ReportShutdownBlocker(
                    sink,
                    "the GPU idle/fence drain failed, so engine shutdown "
                    "released nothing and destroyed nothing",
                    "retry EngineShutdown through the same owning host once "
                    "the device can be drained; do not return, throw, or "
                    "reset the host while the status is not Complete");
                return EngineShutdownStatus::GpuDrainFailed;
            }
            ReportShutdownBlocker(
                sink,
                "text or texture-binding teardown was refused while an "
                "external GPU lifetime was still held; nothing was destroyed",
                "release the external owner and retry EngineShutdown through "
                "the same host");
            return EngineShutdownStatus::ExternalGpuLifetime;
        }
    } else {
        // GPU 소비자가 **등록되지 않은** host. RegisterGpuConsumers가 반쪽
        // 등록을 거절하므로 여기 오는 host는 언제나 renderer도 텍스트도
        // 등록하지 않았다. 그렇다고 부술 것이 없는 것은 아니다 — 위의
        // tearDownUnregisteredGpuState가 그것을 실제로 확인하고 부순다.
        if (!releaseInternalOwners()) return blocked;
        if (!tearDownUnregisteredGpuState()) return blocked;
    }

    if (impl.graphics) impl.graphics->Destroy();
    molga::detail::NotifyEngineShutdownStage("DestroyGraphicsDevice");
    impl.teardownPhase = Impl::TeardownPhase::Complete;
    return EngineShutdownStatus::Complete;
}

EngineShutdownStatus EngineShutdown(std::unique_ptr<EngineHost>& host,
                                    molga::text::TextDiagnosticSink& sink) {
    if (!host) return EngineShutdownStatus::Complete;
    const EngineShutdownStatus status = host->TearDownGpu(sink);
    // Complete가 아니면 host를 놓지 않는다. 놓으면 이 종료가 막으려던 바로
    // 그 상태가 된다: 아직 살아 있는 명령이나 스냅샷이 가리키는 핸들 위에서
    // 장치와 창이 사라진다.
    if (status != EngineShutdownStatus::Complete) return status;
    host.reset();
    return EngineShutdownStatus::Complete;
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
