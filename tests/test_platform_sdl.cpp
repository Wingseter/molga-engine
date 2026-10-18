#include "Core/Bootstrap.h"
#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "Platform/NativeInputEvent.h"
#include "Rendering/TextRenderer.h"
#include "Systems/Input.h"
#include "Text/TextDiagnostic.h"
#include "doctest.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace {

void Push(const SDL_Event& event) {
    SDL_Event copy = event;
    REQUIRE(SDL_PushEvent(&copy));
}

} // namespace

TEST_CASE("SDL host translates window input and close events") {
    WindowConfig config;
    config.title = "Molga SDL platform contract";
    config.width = 320;
    config.height = 180;
    config.visible = false;

    auto host = EngineInit(config);
    REQUIRE(host);
    const molga::WindowId windowId = host->WindowId();
    REQUIRE(windowId != 0);

    const molga::WindowMetrics metrics = host->Metrics();
    CHECK(metrics.logicalWidth == 320);
    CHECK(metrics.logicalHeight == 180);
    CHECK(metrics.pixelWidth >= metrics.logicalWidth);
    CHECK(metrics.pixelHeight >= metrics.logicalHeight);
    CHECK(metrics.scaleX > 0.0f);
    CHECK(metrics.scaleY > 0.0f);

    int observedEvents = 0;
    host->SetNativeEventObserver([&observedEvents](const void*) {
        ++observedEvents;
    });

    SDL_Event keyDown{};
    keyDown.type = SDL_EVENT_KEY_DOWN;
    keyDown.key.windowID = windowId;
    keyDown.key.scancode = SDL_SCANCODE_W;
    Push(keyDown);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = windowId;
    wheel.wheel.x = 1.25f;
    wheel.wheel.y = -2.0f;
    Push(wheel);

    host->PollEvents();
    (void)host->TakeNativeInputBatch();
    Input::Update();
    CHECK(observedEvents >= 2);
    CHECK(Input::GetKeyDown(Input::KeyCode::W));
    CHECK(Input::GetScrollX() == doctest::Approx(1.25f));
    CHECK(Input::GetScrollY() == doctest::Approx(-2.0f));
    CHECK_FALSE(host->ShouldClose());

    SDL_Event flippedWheel{};
    flippedWheel.type = SDL_EVENT_MOUSE_WHEEL;
    flippedWheel.wheel.windowID = windowId;
    flippedWheel.wheel.x = -3.0f;
    flippedWheel.wheel.y = 4.0f;
    flippedWheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    Push(flippedWheel);

    host->PollEvents();
    (void)host->TakeNativeInputBatch();
    Input::Update();
    CHECK(Input::GetScrollX() == doctest::Approx(3.0f));
    CHECK(Input::GetScrollY() == doctest::Approx(-4.0f));

    SDL_Event detachedClose{};
    detachedClose.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    detachedClose.window.windowID = windowId + 1000;
    Push(detachedClose);
    host->PollEvents();
    (void)host->TakeNativeInputBatch();
    Input::Update();
    CHECK(Input::GetKey(Input::KeyCode::W));
    CHECK_FALSE(host->ShouldClose());

    SDL_Event focusLost{};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focusLost.window.windowID = windowId;
    Push(focusLost);
    host->PollEvents();
    (void)host->TakeNativeInputBatch();
    Input::Update();
    CHECK(Input::GetKeyUp(Input::KeyCode::W));
    CHECK_FALSE(Input::GetKey(Input::KeyCode::W));

    SDL_Event mainClose{};
    mainClose.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    mainClose.window.windowID = windowId;
    Push(mainClose);
    host->PollEvents();
    (void)host->TakeNativeInputBatch();
    CHECK(host->ShouldClose());

    // Task 11.2 Step 7f: 종료는 상태를 돌려준다. Complete일 때만 host가
    // 놓인다 — 그 두 사실을 함께 못 박아야 "언제나 reset한다"는 구현과
    // 구별된다.
    molga::text::VectorTextDiagnosticSink shutdownSink;
    CHECK(EngineShutdown(host, shutdownSink) == EngineShutdownStatus::Complete);
    CHECK_FALSE(host);
    CHECK(shutdownSink.Diagnostics().empty());
}

TEST_CASE("platform quit request reaches the SDL host") {
    WindowConfig config;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);
    CHECK_FALSE(host->ShouldClose());

    molga::RequestApplicationQuit();
    host->PollEvents();
    (void)host->TakeNativeInputBatch();
    CHECK(host->ShouldClose());

    molga::text::VectorTextDiagnosticSink shutdownSink;
    CHECK(EngineShutdown(host, shutdownSink) == EngineShutdownStatus::Complete);
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 11.2 close-out: Complete는 "부술 것이 없었다"가 아니라 "부쉈다"이다.
//
// GPU 소비자를 등록하지 않은 host는 예전에 WaitIdle, atlas 해제, 텍스트 서비스
// 파괴, DestroyRetiredBindings, DestroyDeviceResources를 통째로 건너뛰고도
// 장치를 부수고 Complete를 돌려주었다. 프로덕션에서 그 갈래로 오는 여섯
// 경로(main.cpp의 둘, runtime_main.cpp의 넷) 가운데 하나 —
// runtime_main.cpp의 `TextRenderer::Get().Init()` 실패 — 는 프로세스 텍스트
// renderer가 **이미 만들어진 뒤**다. 그 상태로 돌아가면 진입점의 ICU guard가
// 살아 있는 텍스트 renderer 위에서 u_cleanup을 돌린다.
//
// 이 케이스가 재는 것은 표식이 아니라 효과다: 그 인스턴스가 실제로 사라졌는가.
// "Complete를 돌려주었다"만 재는 단언은 고장난 구현과 고쳐진 구현을 구별하지
// 못한다 — 고장난 구현이 언제나 Complete를 돌려주기 때문이다.
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("engine shutdown tears down an unregistered process text renderer") {
    WindowConfig config;
    config.title = "Molga unregistered GPU consumer shutdown";
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);

    // runtime_main.cpp:928의 모양. Get()이 프로세스 인스턴스를 만들었고,
    // host에는 아무것도 등록되지 않았다.
    TextRenderer& text = TextRenderer::Get();
    REQUIRE(TextRenderer::ProcessInstanceOrNull() == &text);

    molga::text::VectorTextDiagnosticSink shutdownSink;
    CHECK(EngineShutdown(host, shutdownSink) == EngineShutdownStatus::Complete);
    CHECK_FALSE(host);
    // 그리고 그 Complete는 이 인스턴스가 실제로 파괴되었다는 뜻이다. 이
    // 한 줄이 없으면 "건너뛰고 Complete"와 "부수고 Complete"가 같은 시험을
    // 통과한다.
    CHECK(TextRenderer::ProcessInstanceOrNull() == nullptr);
    CHECK(shutdownSink.Diagnostics().empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// 그 방어의 나머지 절반. ~EngineHost의 우회로 감지기는 renderer/textRenderer
// **등록**에 걸려 있었고, 우회로에서는 그 둘이 언제나 널이다 — 방어가 자기가
// 막으려던 경우에만 눈을 감았다. 이 host가 GPU 상태를 소유했다는 사실은 등록이
// 아니라 장치가 말한다: 완료된 teardown은 언제나 장치를 부수고 가므로, 살아
// 있는 장치를 든 채 죽은 host가 곧 우회로다.
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("a host destroyed without a completed teardown is not silent") {
    auto ring = std::make_shared<Log::RingBufferSink>(32);
    Log::AddSink(ring);
    {
        WindowConfig config;
        config.title = "Molga bypassed teardown";
        config.visible = false;
        auto host = EngineInit(config);
        REQUIRE(host);
        // EngineShutdown을 부르지 않고 놓는다. 계약 위반이고, 그 사실이
        // 로그에 남지 않으면 상태 계약은 지켜지든 말든 아무 차이가 없다.
    }
    std::size_t bypassErrors = 0;
    for (const Log::LogMessage& message : ring->Snapshot()) {
        if (message.severity == Log::Severity::Error &&
            message.category == "EngineShutdown") {
            ++bypassErrors;
        }
    }
    Log::ClearSinks();
    CHECK(bypassErrors == 1U);
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 12.3 Step 1: SDL 이벤트 하나가 값으로 옮겨지는 규칙.
//
// 이 세 케이스는 SDL 창도 EngineInit도 쓰지 않는다 — 번역은 SDL_Event 구조체에서
// 값으로 가는 순수 함수다. 그래서 화면이 없는 환경에서도 돈다.
// ═══════════════════════════════════════════════════════════════════════════
namespace {

using molga::platform::NativeInputEvent;
using molga::platform::NativeInputEventKind;

std::optional<NativeInputEvent> Translate(const SDL_Event& event,
                                          std::uint64_t sequence = 9,
                                          std::uint32_t gamepadId = 0,
                                          molga::WindowId gamepadWindow = 0) {
    molga::detail::NativeTranslateContext context;
    context.gamepadInstanceId = gamepadId;
    context.gamepadWindowId = gamepadWindow;
    return molga::detail::TranslateNativeInputEvent(&event, sequence, context);
}

SDL_Event KeyEvent(Uint32 type, SDL_Keymod mod) {
    SDL_Event event{};
    event.type = type;
    event.key.windowID = 7;
    event.key.scancode = SDL_SCANCODE_W;
    event.key.mod = mod;
    return event;
}

} // namespace

TEST_CASE("native translation copies SDL key modifiers from the event") {
    struct Row {
        SDL_Keymod mod;
        bool shift, control, alt, gui;
    };
    const Row rows[] = {
        {SDL_KMOD_NONE, false, false, false, false},
        {SDL_KMOD_CTRL, false, true, false, false},
        {SDL_KMOD_GUI, false, false, false, true},
        {static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_SHIFT),
         true, true, false, true},
    };
    for (const Row& row : rows) {
        const auto down = Translate(KeyEvent(SDL_EVENT_KEY_DOWN, row.mod));
        REQUIRE(down.has_value());
        CHECK(down->kind == NativeInputEventKind::Key);
        CHECK(down->control == static_cast<std::uint32_t>(SDL_SCANCODE_W));
        CHECK(down->active);
        CHECK(down->windowId == 7);
        CHECK(down->keyModifiers.shift == row.shift);
        CHECK(down->keyModifiers.control == row.control);
        CHECK_FALSE(down->keyModifiers.alt);
        CHECK(row.alt == false);
        CHECK(down->keyModifiers.gui == row.gui);
    }
    // 같은 비트가 key-up에서도 그대로 복사되고 active만 거짓이다.
    const auto up = Translate(KeyEvent(SDL_EVENT_KEY_UP, rows[3].mod));
    REQUIRE(up.has_value());
    CHECK(up->kind == NativeInputEventKind::Key);
    CHECK_FALSE(up->active);
    CHECK(up->keyModifiers.shift);
    CHECK(up->keyModifiers.control);
    CHECK_FALSE(up->keyModifiers.alt);
    CHECK(up->keyModifiers.gui);

    // alt는 별도 비트다: ALT만 켠 행이 다른 셋을 켜지 않는다.
    const auto alt = Translate(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_KMOD_ALT));
    REQUIRE(alt.has_value());
    CHECK(alt->keyModifiers.alt);
    CHECK_FALSE(alt->keyModifiers.shift);
    CHECK_FALSE(alt->keyModifiers.control);
    CHECK_FALSE(alt->keyModifiers.gui);
}

TEST_CASE("native translation flips the wheel exactly once and keeps signed values") {
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.common.timestamp = 123456789;
    wheel.wheel.windowID = 11;
    wheel.wheel.x = 1.25f;
    wheel.wheel.y = -2.0f;
    wheel.wheel.mouse_x = 40.5f;
    wheel.wheel.mouse_y = 22.25f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;

    const auto normal = Translate(wheel, 77);
    REQUIRE(normal.has_value());
    CHECK(normal->kind == NativeInputEventKind::Scroll);
    CHECK(normal->sequence == 77);
    CHECK(normal->windowId == 11);
    CHECK(normal->nativeType == static_cast<std::uint32_t>(SDL_EVENT_MOUSE_WHEEL));
    CHECK(normal->timestampNanoseconds == 123456789);
    CHECK(normal->deltaX == 1.25f);
    CHECK(normal->deltaY == -2.0f);
    // 휠 이벤트가 자기 커서 위치를 든다(조회가 아니라 복사).
    CHECK(normal->x == 40.5f);
    CHECK(normal->y == 22.25f);

    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    const auto flipped = Translate(wheel);
    REQUIRE(flipped.has_value());
    CHECK(flipped->deltaX == -1.25f);
    CHECK(flipped->deltaY == 2.0f);

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = 12;
    motion.motion.x = 3.5f;
    motion.motion.y = 4.5f;
    const auto moved = Translate(motion);
    REQUIRE(moved.has_value());
    CHECK(moved->kind == NativeInputEventKind::PointerMotion);
    CHECK(moved->windowId == 12);
    CHECK(moved->x == 3.5f);
    CHECK(moved->y == 4.5f);

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.windowID = 12;
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.x = 8.0f;
    button.button.y = 9.0f;
    const auto released = Translate(button);
    REQUIRE(released.has_value());
    CHECK(released->kind == NativeInputEventKind::PointerButton);
    CHECK(released->control == static_cast<std::uint32_t>(Input::MouseButton::Right));
    CHECK_FALSE(released->active);
    CHECK(released->x == 8.0f);
    button.button.button = 99;
    CHECK_FALSE(Translate(button).has_value());

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focus.window.windowID = 12;
    const auto lost = Translate(focus);
    REQUIRE(lost.has_value());
    CHECK(lost->kind == NativeInputEventKind::WindowFocus);
    CHECK_FALSE(lost->active);
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    CHECK(Translate(focus)->active);

    // 게임패드: 호스트가 연 장치만, 호스트가 도장 찍은 창에 붙는다.
    SDL_Event axis{};
    axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    axis.gaxis.which = 5;
    axis.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHTY;
    axis.gaxis.value = -24576;
    const auto stick = Translate(axis, 3, /*gamepadId=*/5, /*window=*/21);
    REQUIRE(stick.has_value());
    CHECK(stick->kind == NativeInputEventKind::GamepadAxis);
    CHECK(stick->control == static_cast<std::uint32_t>(Input::GamepadAxis::RightY));
    CHECK(stick->axisValue == doctest::Approx(-0.75f));
    CHECK(stick->windowId == 21);
    CHECK_FALSE(Translate(axis, 3, /*gamepadId=*/6, 21).has_value());

    SDL_Event pad{};
    pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    pad.gbutton.which = 5;
    pad.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    const auto pressed = Translate(pad, 4, 5, 21);
    REQUIRE(pressed.has_value());
    CHECK(pressed->kind == NativeInputEventKind::GamepadButton);
    CHECK(pressed->control == static_cast<std::uint32_t>(Input::GamepadButton::South));
    CHECK(pressed->active);
    CHECK(pressed->windowId == 21);
    // 키보드 포커스 창이 없으면 0이다(이후 단계가 외부 이벤트로 감사만 한다).
    CHECK(Translate(pad, 4, 5, 0)->windowId == 0);
    pad.gbutton.button = 200;
    CHECK_FALSE(Translate(pad, 4, 5, 21).has_value());

    // UI가 보지 않는 종류는 값이 되지 않는다.
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    CHECK_FALSE(Translate(text).has_value());
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    CHECK_FALSE(Translate(quit).has_value());
}

TEST_CASE("the native sequence allocator never publishes zero or reuses a value") {
    const auto first = molga::platform::AcquireNativeInputSequence();
    const auto second = molga::platform::AcquireNativeInputSequence();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first != 0);
    CHECK(*second > *first);

    {
        molga::platform::ScopedNativeInputSequenceForTesting guard(
            std::numeric_limits<std::uint64_t>::max());
        CHECK_FALSE(molga::platform::AcquireNativeInputSequence().has_value());
        // 소진 뒤에도 값은 최댓값에 멈춘다: 감기지 않고 다시 쓰이지 않는다.
        CHECK_FALSE(molga::platform::AcquireNativeInputSequence().has_value());
        CHECK(molga::platform::PeekNativeInputSequenceForTesting() ==
              std::numeric_limits<std::uint64_t>::max());
    }
    // 가드가 풀리면 다음 값은 가드 이전 흐름에서 이어진다.
    const auto third = molga::platform::AcquireNativeInputSequence();
    REQUIRE(third.has_value());
    CHECK(*third > *second);
}

TEST_CASE("a native batch finds one surface seed and refuses duplicates") {
    using molga::platform::NativeInputBatch;
    using molga::platform::NativeSurfaceBatchStart;
    NativeInputBatch batch;
    CHECK(batch.empty());
    CHECK(batch.FindSurface(10) == nullptr);

    NativeSurfaceBatchStart a;
    a.windowId = 10;
    a.nativeWindowFocused = true;
    a.pointerWindowPixelX = -3;
    a.pointerWindowPixelY = 8;
    a.pointerWindowPixelValid = true;
    NativeSurfaceBatchStart b;
    b.windowId = 20;
    batch.surfaceStarts = {a, b};
    CHECK_FALSE(batch.empty());
    REQUIRE(batch.FindSurface(10) != nullptr);
    CHECK(*batch.FindSurface(10) == a);
    CHECK_FALSE(*batch.FindSurface(10) == b);
    CHECK(batch.FindSurface(30) == nullptr);

    // 같은 창이 두 번 있으면 어느 쪽이 맞는지 모른다: 고르지 않는다.
    batch.surfaceStarts.push_back(a);
    CHECK(batch.FindSurface(10) == nullptr);
    CHECK(batch.FindSurface(20) != nullptr);

    // 모든 필드가 비교에 들어간다.
    NativeSurfaceBatchStart c = a;
    c.pointerWindowPixelY = 9;
    CHECK_FALSE(c == a);
    c = a;
    c.nativeWindowFocused = false;
    CHECK_FALSE(c == a);
    c = a;
    c.pointerWindowPixelValid = false;
    CHECK_FALSE(c == a);
    c = a;
    c.pointerWindowPixelX = 0;
    CHECK_FALSE(c == a);
    c = a;
    c.windowId = 11;
    CHECK_FALSE(c == a);
}

// 호스트 수준: 창이 필요하므로 macOS(디스플레이가 있는 환경)에서만 돈다.
TEST_CASE("EngineHost hands a native batch over exactly once") {
    WindowConfig config;
    config.title = "Molga native batch";
    config.width = 320;
    config.height = 180;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);
    const molga::WindowId windowId = host->WindowId();

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.windowID = windowId;
    key.key.scancode = SDL_SCANCODE_W;
    key.key.mod = SDL_KMOD_CTRL;
    Push(key);
    SDL_Event ignored{};
    ignored.type = SDL_EVENT_TEXT_INPUT;
    ignored.text.windowID = windowId;
    Push(ignored);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = windowId;
    wheel.wheel.y = 2.0f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    Push(wheel);

    host->PollEvents();
    molga::platform::NativeInputBatch batch = host->TakeNativeInputBatch();
    // 창 시작 이벤트(포커스 등)가 섞일 수 있어 우리가 넣은 것만 걸러 본다.
    // TEXT_INPUT은 값이 되지 않지만 시퀀스는 소비한다.
    std::vector<NativeInputEvent> ours;
    for (const NativeInputEvent& event : batch.events) {
        if (event.kind == NativeInputEventKind::Key ||
            event.kind == NativeInputEventKind::Scroll) {
            ours.push_back(event);
        }
        CHECK(event.sequence != 0);
        CHECK(event.kind != NativeInputEventKind::Other);
    }
    REQUIRE(ours.size() == 2);
    CHECK(ours[0].kind == NativeInputEventKind::Key);
    CHECK(ours[0].keyModifiers.control);
    CHECK(ours[1].kind == NativeInputEventKind::Scroll);
    CHECK(ours[1].deltaY == -2.0f);
    CHECK(ours[1].sequence > ours[0].sequence);
    // 창 하나당 씨앗 하나.
    REQUIRE(batch.FindSurface(windowId) != nullptr);

    // 두 번째 take는 빈 배치다.
    CHECK(host->TakeNativeInputBatch().empty());

    // 가져가지 않은 배치가 있으면 폴링하지 않는다: SDL 큐도 입력 상태도 그대로.
    host->PollEvents();                  // 씨앗이 담긴 새 배치
    SDL_Event second{};
    second.type = SDL_EVENT_KEY_DOWN;
    second.key.windowID = windowId;
    second.key.scancode = SDL_SCANCODE_A;
    Push(second);
    host->PollEvents();                  // 앞 배치를 가져가지 않았다: 거절, 큐 유지
    CHECK(host->TakeNativeInputBatch().FindSurface(windowId) != nullptr);
    host->PollEvents();                  // 가져갔으므로 이제 큐의 키가 들어온다
    bool sawA = false;
    for (const NativeInputEvent& event : host->TakeNativeInputBatch().events) {
        if (event.kind == NativeInputEventKind::Key &&
            event.control == static_cast<std::uint32_t>(SDL_SCANCODE_A)) {
            sawA = true;
        }
    }
    CHECK(sawA);

    molga::text::VectorTextDiagnosticSink shutdownSink;
    CHECK(EngineShutdown(host, shutdownSink) == EngineShutdownStatus::Complete);
}
