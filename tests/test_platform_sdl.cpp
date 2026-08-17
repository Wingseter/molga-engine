#include "Core/Bootstrap.h"
#include "Systems/Input.h"
#include "doctest.h"

#include <SDL3/SDL.h>

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

    EngineShutdown(host);
    CHECK_FALSE(host);
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
}
