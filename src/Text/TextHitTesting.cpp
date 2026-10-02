#include "Text/TextHitTesting.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace molga::text {

namespace {

// 이 파일에는 HarfBuzz도 폰트 헤더도 없고, faceResource를 역참조하지도
// 않는다(Task 7.3 Step 5). 나눗셈도 하지 않는다: 합자 안쪽의 자리는 배치가
// 이미 저장해 둔 것을 읽을 뿐이며, 여기서 비례 분할로 다시 만들어 내면
// 저장된 자리와 조용히 어긋난 두 번째 진실이 생긴다.
//
// Rendering/FontFace.h는 TextLayoutTypes.h -> TextShapingService.h ->
// FontRepository.h를 타고 이 번역 단위에 들어온다. ShapedGlyph가 face 자원
// 지분을 들고 있으므로 TextLayout을 참조로 받는 한 피할 수 없고, HarfBuzz
// 타입은 그 경로 어디에도 없다 — Step 5를 전처리 결과와 기계적으로 견주는
// 사람이 이 include를 위반으로 읽지 않도록 적어 둔다.

// 이 y가 닿는 줄. 줄은 위에서 아래로 정렬되어 있으므로 bottom을 처음 넘어서는
// 줄이 답이고, 첫 줄 위쪽은 첫 줄, 마지막 줄 아래쪽은 마지막 줄로 고정된다 —
// 클릭이 문단 밖으로 나갔다고 caret이 사라지면 드래그 선택이 끊긴다.
std::size_t LineForY(const TextLayout& layout, Fixed26_6 y) {
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        if (y.Raw() < layout.lines[index].bottom.Raw()) return index;
    }
    return layout.lines.empty() ? 0U : layout.lines.size() - 1U;
}

// caretStops는 줄 단위로 묶여 있고 줄 안에서는 시각 순서다(TextLayoutService의
// BuildCaretStops). 그 줄의 구간을 찾는다.
bool StopRangeForLine(const TextLayout& layout, std::size_t lineIndex,
                      std::size_t& begin, std::size_t& end) {
    bool found = false;
    for (std::size_t index = 0; index < layout.caretStops.size(); ++index) {
        if (layout.caretStops[index].lineIndex != lineIndex) {
            // 자리는 줄 순서로 쌓인다. 그 줄을 이미 지났으면 더 볼 것이 없다 —
            // 드래그 선택은 이 질의를 프레임마다 부른다.
            if (found) break;
            continue;
        }
        if (!found) {
            begin = index;
            found = true;
        }
        end = index + 1U;
    }
    return found;
}

// glyph 하나 안에서 논리 경계가 놓인 정확한 x. RTL glyph에서는 논리 시작이
// 오른쪽 모서리이고 논리 끝이 왼쪽 모서리다.
//
// 바깥 모서리는 glyph 원점이 아니라 pen 자리에서 잰다. 원점은 pen에 GPOS의 x
// offset을 더한 그리기 좌표이므로, 그것으로 모서리를 삼으면 이웃한 두 glyph의
// 선택 사각형 사이에 offset 차만큼의 틈이나 겹침이 생긴다. pen은 배치가
// origin.x = pen + offsetX로 놓은 값이라 그대로 되짚을 수 있고,
// TextLayoutService의 BuildCaretStops도 바깥 모서리를 같은 규칙으로 잡는다.
//
// 안쪽 경계는 그 규칙을 따르지 않는다. 그 자리는 Task 7.2가 glyph 그리기
// 원점에 붙여 저장해 둔 값이고, 여기서는 그것을 그대로 읽는다(Step 5). 두
// 규약은 glyph의 offsetX가 0인 곳에서 같은 자리이고 커밋된 corpus의 내부
// caret을 가진 glyph는 전부 offsetX가 0이지만, 합자에 가로 GPOS 보정을 거는
// 폰트가 들어오면 아래의 min/max가 pen 기준 모서리와 원점 기준 안쪽 자리를
// 한 사각형에 섞게 된다 — 하나로 합치는 일은 7.2의 저장 공식과 그것을 고정한
// 케이스를 함께 고치는 개정이다.
bool GlyphBoundaryX(const PositionedGlyph& glyph, bool rightToLeft,
                    std::uint32_t boundary, Fixed26_6& out) {
    const auto leading =
        Fixed26_6::CheckedSub(glyph.origin.x, glyph.glyph.offsetX);
    if (!leading) return false;
    const auto trailing =
        Fixed26_6::CheckedAdd(*leading, glyph.glyph.advanceX);
    if (!trailing) return false;
    if (boundary == glyph.glyph.graphemes.begin) {
        out = rightToLeft ? *trailing : *leading;
        return true;
    }
    if (boundary == glyph.glyph.graphemes.end) {
        out = rightToLeft ? *leading : *trailing;
        return true;
    }
    for (const GlyphInteriorCaret& caret : glyph.interiorCarets) {
        if (caret.logicalGraphemeBoundary != boundary) continue;
        out = caret.position.x;
        return true;
    }
    // 저장된 내부 caret이 없는 경계를 여기서 만들어 내지 않는다(Step 5).
    // 배치의 불변식이 깨진 것이고, 늦은 대체는 그 사실을 사각형 하나로 덮는다.
    //
    // assert가 아니라 실패로 닫는다. assert는 NDEBUG에서 사라지므로 정책이
    // 릴리스에서는 아예 강제되지 않고, 디버그에서는 프로세스를 죽여 이 갈래를
    // 관찰하는 케이스를 쓸 수 없게 만든다. 두 구성 모두에서 같은 답 — 사각형
    // 없음 — 을 내는 쪽이 관찰 가능하면서도 자리를 지어내지 않는다.
    return false;
}

}  // namespace

// ── Step 6: half-open visual hit testing ────────────────────────────────────
CaretPosition TextHitTesting::HitTest(const TextLayout& layout,
                                      FixedPoint point) {
    if (layout.caretStops.empty()) return CaretPosition{};
    std::size_t begin = 0;
    std::size_t end   = 0;
    if (!StopRangeForLine(layout, LineForY(layout, point.y), begin, end)) {
        // 되짚기는 방어적이다. 배치가 낸 줄은 빈 줄이라도 자리를 하나 가지므로
        // (BuildCaretStops) 이 서비스가 만든 배치에서는 닿을 수 없다. 손으로
        // 조립한 TextLayout이 들어오면 첫 자리를 돌려준다: x는 줄 안에서만
        // 단조롭고 줄과 줄 사이에서는 되감기므로, 배열 전체를 하나의 구간
        // 사슬로 훑으면 첫 내리막에서 멈춰 아무 뜻 없는 자리를 고른다.
        const CaretStop& fallback = layout.caretStops.front();
        return CaretPosition{fallback.logicalGraphemeBoundary,
                             fallback.affinity};
    }

    const std::int64_t x = point.x.Raw();
    std::size_t chosen   = begin;
    for (std::size_t index = begin; index + 1U < end; ++index) {
        const std::int64_t low  = layout.caretStops[index].position.x.Raw();
        const std::int64_t high = layout.caretStops[index + 1U].position.x.Raw();
        // 반열림 [low, high). high에 정확히 놓인 점은 이 구간이 아니라 다음
        // 구간의 것이므로, 같은 x에 여러 자리가 겹쳐 있어도 답이 하나로 정해진다.
        if (x >= high) {
            chosen = index + 1U;
            continue;
        }
        if (x <= low) {
            chosen = index;
            break;
        }
        // 정확한 중점은 시각 진행 방향, 곧 더 큰 x 쪽으로 간다. 물리 규칙은
        // 하나지만 그 자리에 있는 논리 경계는 run 방향에 따라 반대로 나온다.
        chosen = (2 * x >= low + high) ? index + 1U : index;
        break;
    }
    const CaretStop& stop = layout.caretStops[chosen];
    return CaretPosition{stop.logicalGraphemeBoundary, stop.affinity};
}

// ── Step 7: caret rectangles ────────────────────────────────────────────────
std::vector<FixedRect> TextHitTesting::CaretRects(const TextLayout& layout,
                                                  CaretPosition position,
                                                  Fixed26_6 thickness) {
    std::vector<FixedRect> rects;
    // 두께는 호출자의 것이고 계약은 양수다. 0이나 음수를 그대로 받으면 보이지
    // 않는 caret이나 뒤집힌 사각형이 그려진다.
    if (thickness.Raw() <= 0) return rects;

    // 요구된 affinity를 먼저 정확히 찾고, 없을 때만 경계만으로 되짚는다.
    // 방향이 바뀌지 않는 경계에서는 두 affinity가 한 자리로 접혀 하나만
    // 저장되므로(BuildCaretStops), 되짚기가 없으면 편집기가 그 흔한 자리에서
    // 커서를 잃는다. BiDi 경계에서는 두 자리가 다 있으므로 되짚기까지 가지 않는다.
    for (int pass = 0; pass < 2; ++pass) {
        for (const CaretStop& stop : layout.caretStops) {
            if (stop.logicalGraphemeBoundary != position.boundary) continue;
            if (pass == 0 && stop.affinity != position.affinity) continue;
            if (stop.lineIndex >= layout.lines.size()) continue;
            const TextLine& line = layout.lines[stop.lineIndex];
            const auto height = Fixed26_6::CheckedSub(line.bottom, line.top);
            if (!height) return {};
            // 오른쪽 모서리도 26.6 검사 대상이다. 두께는 호출자가 주는 값이라
            // 자리와 더해 raw 범위를 넘을 수 있고, 그때 포화된 사각형을 내면
            // 화면 끝까지 칠해진 caret이 그려진다.
            if (!Fixed26_6::CheckedAdd(stop.position.x, thickness)) return {};
            FixedRect rect;
            rect.x      = stop.position.x;
            rect.y      = line.top;
            rect.width  = thickness;
            rect.height = *height;
            rects.push_back(rect);
        }
        if (!rects.empty()) break;
    }
    return rects;
}

// ── Step 8: selection rectangles ────────────────────────────────────────────
std::vector<FixedRect> TextHitTesting::SelectionRects(
    const TextLayout& layout, GraphemeRange logicalSelection) {
    std::vector<FixedRect> rects;
    // 반열림 [begin, end)의 이른 탈출일 뿐, 규칙을 강제하는 곳은 아니다.
    // 아래의 glyph별 교집합이 이미 같은 답(사각형 없음)을 내므로 이 줄을
    // 지워도 결과는 같다. 빈 선택과 뒤집힌 선택을 여기서 한 번에 걸러
    // 나머지 코드가 그 경우를 다시 생각하지 않게 하려고 남긴다.
    if (logicalSelection.begin >= logicalSelection.end) return rects;

    for (const TextLine& line : layout.lines) {
        const auto height = Fixed26_6::CheckedSub(line.bottom, line.top);
        if (!height) return {};
        // 사각형 하나는 시각 run 하나의 몫이다. 한 run 안에서 논리 순서는
        // 시각 순서로 단조롭게 대응하므로, 논리 구간 하나는 언제나 그 run
        // 안에서 이어진 한 덩어리가 된다. 갈라지는 것은 run과 run 사이뿐이고,
        // 그것이 BiDi 선택이 여러 사각형이 되는 유일한 이유다.
        for (const VisualRun& run : line.visualRuns) {
            const bool rightToLeft = (run.bidiLevel & 1U) != 0U;
            bool         any  = false;
            std::int64_t low  = 0;
            std::int64_t high = 0;
            for (const PositionedGlyph& glyph : run.glyphs) {
                const std::uint32_t begin = std::max(
                    logicalSelection.begin, glyph.glyph.graphemes.begin);
                const std::uint32_t end =
                    std::min(logicalSelection.end, glyph.glyph.graphemes.end);
                if (begin >= end) continue;
                Fixed26_6 from = Fixed26_6::FromRaw(0);
                Fixed26_6 to   = Fixed26_6::FromRaw(0);
                if (!GlyphBoundaryX(glyph, rightToLeft, begin, from)) return {};
                if (!GlyphBoundaryX(glyph, rightToLeft, end, to)) return {};
                const std::int64_t left  = std::min(from.Raw(), to.Raw());
                const std::int64_t right = std::max(from.Raw(), to.Raw());
                if (!any) {
                    low  = left;
                    high = right;
                    any  = true;
                    continue;
                }
                low  = std::min(low, left);
                high = std::max(high, right);
            }
            if (!any || high <= low) continue;
            const std::int64_t width = high - low;
            // 폭도 26.6 검사 대상이다. 배치가 낸 두 좌표의 차가 raw 범위를
            // 넘으면 포화된 사각형을 내는 대신 아무것도 내지 않는다.
            if (width > INT32_MAX) return {};
            FixedRect rect;
            rect.x      = Fixed26_6::FromRaw(static_cast<std::int32_t>(low));
            rect.y      = line.top;
            rect.width  = Fixed26_6::FromRaw(static_cast<std::int32_t>(width));
            rect.height = *height;
            rects.push_back(rect);
        }
    }
    return rects;
}

}  // namespace molga::text
