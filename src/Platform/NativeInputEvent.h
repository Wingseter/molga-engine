#pragma once

#include "Platform/NativeKeyModifiers.h"
#include "Platform/Window.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace molga::platform {

// 네이티브 이벤트 종류. UIInputEventKind와 1:1이다.
enum class NativeInputEventKind : std::uint8_t {
    Other,
    WindowFocus,
    PointerMotion,
    PointerButton,
    Scroll,
    Key,
    GamepadButton,
    GamepadAxis,
    TextEditing,
    TextCommit,
};

// SDL이 준 이벤트 하나를 **값으로** 옮긴 기록. SDL 타입도, 살아 있는 장치 상태를
// 다시 읽는 접근자도 들지 않는다 — 한 배치는 정확히 그 순간의 값을 담는다.
//
// Task 13.1 Step 4b의 모양이다. `text`(NativeTextEvent)와
// `textInputOwnerTransition`은 그 형식이 13.1에서 생기므로 아직 없다.
struct NativeInputEvent {
    // 프로세스 전역 단조 할당기가 낸 0이 아닌 값. 순서의 근거는 이 값이다.
    std::uint64_t sequence = 0;
    molga::WindowId windowId = 0;
    // SDL event.common.timestamp. 감사용 복사본이고 순서에는 쓰지 않는다.
    std::uint64_t timestampNanoseconds = 0;
    NativeInputEventKind kind = NativeInputEventKind::Other;
    // SDL_EventType. 감사용.
    std::uint32_t nativeType = 0;
    // 창 논리 좌표(점). 포인터 종류와 휠은 항상 든다.
    float x = 0.0f;
    float y = 0.0f;
    // 휠. SDL의 뒤집힘 규칙은 호스트가 정확히 한 번 적용한다.
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    // 정규화된 부호 있는 게임패드 축 값.
    float axisValue = 0.0f;
    // 버튼 번호 / 키 코드 / 게임패드 버튼·축 이름 (Input::* 열거형 값).
    std::uint32_t control = 0;
    NativeKeyModifiers keyModifiers;
    // 눌림, 또는 창 활성.
    bool active = false;
};

// 한 창의 배치 시작 씨앗. 배치의 **어떤** 이벤트보다 앞선 값이다.
// 포인터는 창 물리 픽셀(점 x 픽셀 밀도의 floor)이며 int32로 검사해 넣는다.
struct NativeSurfaceBatchStart {
    molga::WindowId windowId = 0;
    bool nativeWindowFocused = false;
    std::int32_t pointerWindowPixelX = 0;
    std::int32_t pointerWindowPixelY = 0;
    bool pointerWindowPixelValid = false;

    bool operator==(const NativeSurfaceBatchStart& other) const noexcept {
        return windowId == other.windowId &&
               nativeWindowFocused == other.nativeWindowFocused &&
               pointerWindowPixelX == other.pointerWindowPixelX &&
               pointerWindowPixelY == other.pointerWindowPixelY &&
               pointerWindowPixelValid == other.pointerWindowPixelValid;
    }
    bool operator!=(const NativeSurfaceBatchStart& other) const noexcept {
        return !(*this == other);
    }
};

// take-once 배치. 호스트가 폴링마다 하나 만들고, 소비자가 정확히 한 번 가져간다.
struct NativeInputBatch {
    std::vector<NativeInputEvent> events;
    std::vector<NativeSurfaceBatchStart> surfaceStarts;

    bool empty() const noexcept { return events.empty() && surfaceStarts.empty(); }
    // 창 하나의 씨앗. 없거나 중복이면 nullptr — 어느 쪽이 맞는지 모르므로 고르지 않는다.
    const NativeSurfaceBatchStart* FindSurface(molga::WindowId windowId) const noexcept;
};

// 프로세스 전역 시퀀스 할당기. 0을 내지 않고, 감기지 않고, 호스트/창을 가로질러
// 엄격 증가한다. 소진(UINT64_MAX)이면 nullopt이고 값은 UINT64_MAX에 멈춘다.
std::optional<std::uint64_t> AcquireNativeInputSequence() noexcept;

// 시험용. 생성자가 전역 값을 정하고 소멸자가 이전 값을 되돌린다.
class ScopedNativeInputSequenceForTesting {
public:
    explicit ScopedNativeInputSequenceForTesting(std::uint64_t value) noexcept;
    ~ScopedNativeInputSequenceForTesting();
    ScopedNativeInputSequenceForTesting(const ScopedNativeInputSequenceForTesting&) = delete;
    ScopedNativeInputSequenceForTesting& operator=(const ScopedNativeInputSequenceForTesting&) = delete;

private:
    std::uint64_t previous_ = 0;
};

// 시험용. 마지막으로 낸 값을 읽는다.
std::uint64_t PeekNativeInputSequenceForTesting() noexcept;

}  // namespace molga::platform
