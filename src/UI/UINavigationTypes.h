#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 포커스가 방향 입력으로 어떻게 옮겨 가는가. 닫힌 열거이고, UISelectable과
// 나중 포커스 라우팅이 같은 값을 본다. 컴포넌트 헤더가 아니라 여기 사는 것은
// 라우팅 쪽이 UISelectable을 include하지 않고도 이 값을 다루기 위해서다.
enum class UINavigationMode : std::uint8_t { None, Auto, Explicit };

inline std::string_view ToCanonicalString(UINavigationMode value) noexcept {
    switch (value) {
        case UINavigationMode::None: return "None";
        case UINavigationMode::Explicit: return "Explicit";
        case UINavigationMode::Auto: break;
    }
    return "Auto";
}

inline std::optional<UINavigationMode> ParseUINavigationMode(
    const std::string& token) {
    if (token == "None") return UINavigationMode::None;
    if (token == "Auto") return UINavigationMode::Auto;
    if (token == "Explicit") return UINavigationMode::Explicit;
    return std::nullopt;
}
