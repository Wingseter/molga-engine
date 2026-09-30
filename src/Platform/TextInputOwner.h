#pragma once

#include "UI/UIRuntimeIdentity.h"

#include <cstdint>
#include <optional>

namespace molga::platform {

// 한 창의 텍스트 입력을 지금 누가 쥐고 있는가. 닫힌 열거다.
enum class TextInputOwnerKind : std::uint8_t {
    None,
    RuntimeUITextInput,
    EditorImGui,
};

// 소유자 **값**. 중재기 상태를 가리키는 포인터도, 세대로 표를 뒤지는 키도
// 아니다 — 값이라야 이벤트가 복사되어 나간 뒤에도 그때의 소유자를 그대로
// 이름한다.
struct TextInputOwner {
    TextInputOwnerKind kind = TextInputOwnerKind::None;
    std::optional<molga::ui::UIRuntimeTargetIdentity> runtimeTarget;

    // 잘 만들어진 값인가. RuntimeUITextInput은 완전한 런타임 식별자가 **있을
    // 때만**, EditorImGui는 **없을 때만** 유효하다. 이 규칙이 없으면 런타임
    // 대상이 비어 있는 런타임 소유자가 만들어질 수 있고, 그 값은 어떤
    // 컴포넌트도 이름하지 않으면서 소유권만 차지한다.
    bool WellFormed() const noexcept {
        switch (kind) {
            case TextInputOwnerKind::None:
                return false;
            case TextInputOwnerKind::RuntimeUITextInput:
                return runtimeTarget.has_value() &&
                       static_cast<bool>(*runtimeTarget);
            case TextInputOwnerKind::EditorImGui:
                return !runtimeTarget.has_value();
        }
        return false;
    }

    // 모든 필드를 비교한다. 종류만 보는 비교는 서로 다른 두 입력창을 같은
    // 소유자로 읽는다.
    bool operator==(const TextInputOwner& other) const noexcept {
        return kind == other.kind && runtimeTarget == other.runtimeTarget;
    }
    bool operator!=(const TextInputOwner& other) const noexcept {
        return !(*this == other);
    }
};

// 호스트가 ingest 시각에 텍스트/편집 이벤트에 찍는 불변 도장.
//
// generation은 프로세스 전역 비순환 단조 할당기가 낸 0이 아닌 값이다. 0은
// "도장 없음"이고, 그래서 operator bool이 그것을 거른다. 소유권이 바뀌어도
// 이미 찍힌 도장은 바뀌지 않는다 — 그것이 "이미 ingest된 옛 소유자의 텍스트는
// 자기가 도장 받은 완전한 런타임 식별자에만, 한 번만 영향을 준다"의 전부다.
struct TextInputOwnerStamp {
    TextInputOwnerKind kind = TextInputOwnerKind::None;
    std::optional<molga::ui::UIRuntimeTargetIdentity> runtimeTarget;
    std::uint64_t generation = 0;

    explicit operator bool() const noexcept {
        return kind != TextInputOwnerKind::None && generation != 0;
    }

    // TextInputOwner::WellFormed와 같은 규칙에 0이 아닌 세대를 더한 것.
    bool WellFormed() const noexcept {
        if (generation == 0) return false;
        return TextInputOwner{kind, runtimeTarget}.WellFormed();
    }

    // 이 도장이 UI 런타임 대상을 이름하는가. EditorImGui/None 도장은 여기서
    // 언제나 거짓이다 — 그 이벤트는 ImGui 관찰자의 것이고 UI 대상이 없다.
    bool NamesRuntimeTarget() const noexcept {
        return WellFormed() && kind == TextInputOwnerKind::RuntimeUITextInput;
    }

    bool operator==(const TextInputOwnerStamp& other) const noexcept {
        return kind == other.kind && runtimeTarget == other.runtimeTarget &&
               generation == other.generation;
    }
    bool operator!=(const TextInputOwnerStamp& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace molga::platform
