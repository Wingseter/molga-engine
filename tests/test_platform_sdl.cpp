#include "Core/Bootstrap.h"
#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "Rendering/TextRenderer.h"
#include "Systems/Input.h"
#include "Text/TextDiagnostic.h"
#include "doctest.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <memory>

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
    Input::Update();
    CHECK(Input::GetScrollX() == doctest::Approx(3.0f));
    CHECK(Input::GetScrollY() == doctest::Approx(-4.0f));

    SDL_Event detachedClose{};
    detachedClose.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    detachedClose.window.windowID = windowId + 1000;
    Push(detachedClose);
    host->PollEvents();
    Input::Update();
    CHECK(Input::GetKey(Input::KeyCode::W));
    CHECK_FALSE(host->ShouldClose());

    SDL_Event focusLost{};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focusLost.window.windowID = windowId;
    Push(focusLost);
    host->PollEvents();
    Input::Update();
    CHECK(Input::GetKeyUp(Input::KeyCode::W));
    CHECK_FALSE(Input::GetKey(Input::KeyCode::W));

    SDL_Event mainClose{};
    mainClose.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    mainClose.window.windowID = windowId;
    Push(mainClose);
    host->PollEvents();
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
