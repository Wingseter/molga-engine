#pragma once

#include "Common/Fixed26_6.h"
#include "Text/TextLayoutTypes.h"

#include <cstdint>
#include <vector>

namespace molga::text {

// CaretPosition은 caret 하나가 가리키는 자리다: 논리 grapheme 경계 하나와,
// 그 경계가 BiDi 경계일 때 어느 시각 자리를 뜻하는지. affinity는 위치에서
// 되짚을 수 없다 — 방향이 바뀌는 경계에서 같은 논리 경계는 서로 멀리 떨어진
// 두 x를 갖고, 어느 쪽인지는 caret 자신이 들고 다녀야 한다
// (TextLayoutTypes.h의 CaretAffinity 참조).
//
// TextHitTesting은 확정된 불변 TextLayout 하나에 대한 caret/선택/hit 질의다.
// 입력은 배치가 이미 낸 것뿐이다: 시각 순서로 놓인 run, glyph 원점과 advance,
// 그리고 배치가 미리 저장해 둔 PositionedGlyph::interiorCarets와
// TextLayout::caretStops. 폰트도 셰이퍼도 ICU도 여기서는 열리지 않는다 —
// 질의 시점에 caret 자리를 다시 유도하면 warm hit으로 돌아온 배치가 프레임마다
// 셰이퍼를 깨우게 되고, 그 자리는 배치가 낸 자리와 조용히 달라질 수 있다.
//
// 모든 좌표는 배치와 같은 26.6 논리 단위이고 raw로 정확히 비교된다.
// CaretRects의 thickness는 호출자의 것이며 양수여야 한다.
//
// ── 소비자가 알아야 하는 규약 네 가지 ────────────────────────────────────────
// 1. affinity의 뜻. Downstream은 "그 경계에서 시작하는 cell의 논리 앞 모서리",
//    Upstream은 "그 경계에서 끝나는 cell의 논리 뒤 모서리"다. 방향이 바뀌지
//    않는 경계에서는 두 자리가 같은 x이므로 배치가 하나로 접어 Downstream만
//    저장한다(TextLayoutService의 BuildCaretStops). 그래서 CaretRects는 요구된
//    affinity로 먼저 찾고 없으면 경계만으로 한 번 더 찾는다: 접힌 자리를
//    Upstream으로 물어도 그 하나를 돌려준다. BiDi 경계에서는 두 자리가 다 있어
//    되짚기까지 가지 않는다. 뒤 마일스톤의 caret 소비자는 이 읽기를 그대로 써야
//    한다 — 다른 규약을 세우면 같은 CaretPosition이 두 곳을 가리키게 된다.
// 1b. CaretRects의 개수. 이 서비스가 낸 배치에서 하나의 (경계, affinity) 짝은
//    시각 자리를 많아야 하나 갖는다(test_text_layout의 "one logical boundary
//    plus affinity resolves to at most one stop"). 그래서 반환값은 사각형 0개
//    또는 1개이고, 벡터라는 형태는 Step 3이 못 박은 서명 때문이지 "후보 자루"를
//    뜻하지 않는다. 손으로 조립한 배치가 같은 짝에 자리를 여럿 달아 보내면
//    caretStops에 담긴 순서 그대로 전부 돌려준다.
// 2. x의 기준. cell의 바깥 모서리는 pen 자리에서 재고, 합자 안쪽의 자리는
//    Task 7.2가 glyph 그리기 원점에 붙여 저장한 값을 그대로 쓴다. 두 규약은
//    glyph의 offsetX가 0인 곳에서 같은 자리이고, 커밋된 corpus의 내부 caret을
//    가진 glyph는 전부 offsetX가 0이다. 합자에 가로 GPOS 보정을 거는 폰트를
//    받으려면 7.2의 저장 공식과 그것을 고정한 케이스를 함께 고쳐야 한다.
// 3. Task 7.2가 넘긴 두 가지 위험이 caret에도 그대로 미친다. 표시 문단 분할이
//    U+000B/U+000C/U+2028과 U+001C-U+001E에서 UBA 문단 분할과 어긋나고,
//    줄임표 후보는 자기 혼자 하나의 BiDi 문단으로 다시 분석되어 진짜 문단
//    base level을 잃는다. 그 경계 근처의 caret 자리와 hit 결과는 그 어긋남을
//    물려받는다.
// 4. TextLayout::caretStops는 tests/fixtures/text/expected/layout.json의
//    정규 스냅샷에 들어 있지 않다. 자리의 회귀는 스냅샷이 아니라
//    test_text_layout의 caret 케이스들이 잡는다.
struct CaretPosition {
    std::uint32_t boundary = 0;
    CaretAffinity affinity = CaretAffinity::Downstream;
};
class TextHitTesting {
public:
    static CaretPosition HitTest(const TextLayout&, FixedPoint);
    static std::vector<FixedRect> CaretRects(
        const TextLayout&, CaretPosition, Fixed26_6 thickness);
    static std::vector<FixedRect> SelectionRects(
        const TextLayout&, GraphemeRange logicalSelection);
};

}  // namespace molga::text
