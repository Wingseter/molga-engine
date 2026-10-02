#pragma once

namespace molga::platform {

// 네이티브 키 이벤트가 복사해 오는 수정자 상태 하나.
//
// 값이다. 살아 있는 키보드 상태를 다시 읽는 대신 이벤트가 자기 것을 들고
// 다니는 이유는 하나다 — 한 배치 안에서 Enter가 도착한 순간의 수정자와, 그
// 배치를 처리하는 순간의 수정자는 다를 수 있다. Milestone A의 제출 규칙
// (multiline Control+Enter / GUI(Command)+Enter)이 그 차이 위에서 갈린다.
//
// SDL 헤더를 끌어오지 않는다. 이 값은 UI/텍스트/에디터가 모두 보므로, SDL을
// 여기서 include하면 SDL 없이 컴파일되어야 할 계층이 SDL에 묶인다.
//
// C++17 타깃이다: defaulted operator<=>도 rewritten comparison도 없으므로
// ==/!= 를 명시적으로 정의한다. 네 필드를 **전부** 비교한다 — 하나라도 빠지면
// Control+Enter와 Command+Enter가 같은 값이 되고, 그 둘은 다른 정책이다.
struct NativeKeyModifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool gui = false;  // SDL KMOD_GUI; macOS의 Command.

    bool operator==(const NativeKeyModifiers& other) const noexcept {
        return shift == other.shift && control == other.control &&
               alt == other.alt && gui == other.gui;
    }
    bool operator!=(const NativeKeyModifiers& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace molga::platform
