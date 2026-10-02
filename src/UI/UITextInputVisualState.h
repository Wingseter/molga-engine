#pragma once

#include "Platform/Window.h"
#include "Text/TextHitTesting.h"
#include "Text/TextLayoutTypes.h"
#include "Text/UnicodeTextBuffer.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstdint>
#include <optional>
#include <string>

namespace molga::ui {

// ── Step 3e: 편집 상태와 배치 사이의 값 전용 경계 ───────────────────────────
// 여기에는 편집 시스템 저장소를 가리키는 포인터가 하나도 없다. 있으면 게시된
// 스냅샷이 살아 있는 편집 상태를 들여다보게 되고, "게시된 뒤에는 불변"이라는
// 계약이 그 자리에서 깨진다 — 같은 프레임의 렌더와 hit-test가 caret 위치를
// 서로 다르게 보는 정확한 경로다. 그래서 제공자는 값 사본 하나를 돌려주고,
// 스냅샷은 그 사본만 담는다.
struct UITextInputVisualState {
    molga::WindowId surfaceWindowId = 0;
    UIRuntimeTargetIdentity input;
    std::string committedUtf8;
    molga::text::CaretPosition caret;
    molga::text::GraphemeRange selection;
    std::string compositionUtf8;
    molga::text::GraphemeRange compositionSelection;
    bool focused = false;
    bool caretVisible = false;
    std::uint64_t editRevision = 0;
    std::uint64_t surfaceRevision = 0;
    bool operator==(const UITextInputVisualState& other) const {
        return surfaceWindowId == other.surfaceWindowId &&
               input == other.input &&
               committedUtf8 == other.committedUtf8 &&
               caret.boundary == other.caret.boundary &&
               caret.affinity == other.caret.affinity &&
               selection == other.selection &&
               compositionUtf8 == other.compositionUtf8 &&
               compositionSelection == other.compositionSelection &&
               focused == other.focused && caretVisible == other.caretVisible &&
               editRevision == other.editRevision &&
               surfaceRevision == other.surfaceRevision;
    }
    bool operator!=(const UITextInputVisualState& other) const {
        return !(*this == other);
    }
};

// 기하 캐시가 보는 입력 편집 상태의 부분집합. selection 끝점/caret/affinity/
// focus/blink/surface revision은 여기 없다 — 그것들은 전체 키에만 있고,
// 그래서 caret이 깜빡이는 프레임은 셰이핑과 배치를 그대로 재사용한다.
// 반대로 보이는 UTF-8과 유효 요청은 여기 있어야 한다: 그 둘이 바뀌면 고유
// 크기와 확정된 줄이 달라지므로, 편집 이전 기하를 재사용하면 화면에 옛 글이
// 남는다.
struct UITextInputGeometryCacheIdentity {
    UIRuntimeTargetIdentity input;
    // 완전한 유효 입력 요청. 현재 보이는 UTF-8과 입력창이 소유한 family/style,
    // 그리고 얼어붙은 뷰포트 제약을 전부 담는다.
    molga::text::TextLayoutRequest effectiveRequest;
    bool operator==(const UITextInputGeometryCacheIdentity&) const;
    bool operator!=(const UITextInputGeometryCacheIdentity& other) const {
        return !(*this == other);
    }
};

// 배치가 런타임 편집 상태를 묻는 유일한 창구.
//
// 조회는 (surfaceWindowId, inputIdentity) 쌍으로 한다. 창을 빼면 분리된 두
// 표면이 같은 입력창을 서로의 caret으로 그리게 된다.
class UITextInputVisualStateProvider {
public:
    virtual ~UITextInputVisualStateProvider() = default;
    virtual std::optional<UITextInputVisualState> GetVisualState(
        molga::WindowId, const UIRuntimeTargetIdentity&) const = 0;
};

// Task 14가 진짜 제공자를 설치하기 전까지 에디터/런타임/테스트가 건네는 값.
// 기본 인자로 숨기지 않는다 — 숨기면 진짜 제공자를 넘기는 것을 한 곳에서
// 빠뜨려도 컴파일이 통과하고, 그 표면만 조용히 caret 없이 그려진다.
class EmptyUITextInputVisualStateProvider final
    : public UITextInputVisualStateProvider {
public:
    static const EmptyUITextInputVisualStateProvider& Instance();
    std::optional<UITextInputVisualState> GetVisualState(
        molga::WindowId, const UIRuntimeTargetIdentity&) const override {
        return std::nullopt;
    }
};

} // namespace molga::ui
