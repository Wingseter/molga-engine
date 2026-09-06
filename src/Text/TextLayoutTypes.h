#pragma once

#include "Common/Fixed26_6.h"
#include "Text/FontFamilyResolver.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextShapingService.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga::text {

// ── 레이아웃 정책 ────────────────────────────────────────────────────────────
// 닫힌 열거다. 값 추가는 캐시 정체성 변경이므로 설계 개정을 거쳐야 한다.
enum class TextWrapMode : std::uint8_t { NoWrap, Word, Grapheme };
enum class TextOverflowMode : std::uint8_t { Overflow, Clip, Ellipsis };
enum class TextHorizontalAlignment : std::uint8_t { Left, Center, Right };
enum class TextVerticalAlignment : std::uint8_t { Top, Middle, Bottom };

// BiDi 경계에서 하나의 논리 grapheme 경계는 시각적으로 두 자리를 갖는다.
// 어느 쪽인지는 caret 자신이 들고 다녀야 하고, 위치에서 되짚을 수 없다.
enum class CaretAffinity : std::uint8_t { Upstream, Downstream };

// 제약 없음은 0이 아니라 nullopt다. 0은 "폭 0으로 접어라"라는 정당한 요청이고,
// 그 둘을 같은 값으로 적으면 제약 없는 문단이 조용히 글자마다 줄바꿈된다.
struct LayoutConstraints {
    std::optional<Fixed26_6> width;
    std::optional<Fixed26_6> height;
};

// 한 문단의 저작된 스타일 전부. 캐시 키가 이 구조를 통째로 복사해 담으므로,
// 여기 추가되는 모든 필드는 자동으로 정체성이 아니라 — TextLayoutCache.cpp의
// 비교/해시에 손으로 추가되어야 정체성이 된다.
struct ParagraphStyle {
    std::string fontFamilyGuid;
    // ── Task 8.2 설계 개정(2026-09-07): 레거시 단일 face ────────────────────
    // schema 1 컴포넌트는 family가 아니라 폰트 하나를 이름으로 지목했다. 그
    // 지목을 fontFamilyGuid에 넣어 흘리면 resolver가 FontImporter 기록을
    // family로 읽으려다 exists=false와 엉뚱한 FontFamilyInvalid를 낸다. 그래서
    // 두 지목은 서로 다른 필드다.
    //
    // 의미도 다르다: 이 경로는 face 하나이고 fallback이 없다(Task 5.1의
    // BuildLegacySingleFace가 그 계약이다). 비어 있지 않고 fontFamilyGuid가
    // 비어 있을 때만 쓰인다 — family를 저작하는 것이 레거시 지목을 대체하는
    // 유일한 행위이므로, 둘 다 있으면 family가 이긴다.
    //
    // 캐시 정체성이다: 아래 TextLayoutCache의 비교/해시/크기 추정 셋 모두에
    // 들어간다. 그러지 않으면 같은 원문의 legacy 배치와 family 배치가 한
    // 항목을 나눠 갖는다.
    std::string legacyFontGuid;
    FontRequest fontRequest;
    ShapeStyle shape;
    TextAnalysisOptions analysis;
    TextWrapMode wrap = TextWrapMode::NoWrap;
    TextOverflowMode overflow = TextOverflowMode::Overflow;
    std::uint32_t maxLines = 0;
    Fixed26_6 lineSpacing = Fixed26_6::FromRaw(64);
    TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
    TextVerticalAlignment vertical = TextVerticalAlignment::Top;
    std::string ellipsisUtf8 = u8"…";
};

// ── 배치된 출력 ──────────────────────────────────────────────────────────────
// glyph 내부의 caret 자리. 하나의 glyph가 여러 grapheme을 담는 합자에서만
// 생긴다. fromAdjustedGdef는 이 자리가 폰트의 GDEF ligature caret 기록에서
// 왔다는 뜻이고, false는 균등 분할로 유도했다는 뜻이다 — 두 출처를 섞으면
// 나중에 "폰트가 준 자리"만 골라 검증할 수 없다.
struct GlyphInteriorCaret {
    std::uint32_t logicalGraphemeBoundary = 0;
    FixedPoint position;
    bool fromAdjustedGdef = false;
};

// 셰이핑된 glyph 하나와 그것이 놓인 자리. glyph는 자기 face 자원의 지분을
// 그대로 들고 있으므로, 이 레이아웃이 살아 있는 동안 hot reload가 밑에서
// 바이트를 갈아 끼울 수 없다.
struct PositionedGlyph {
    ShapedGlyph glyph;
    FixedPoint origin;
    std::vector<GlyphInteriorCaret> interiorCarets;
};

// 시각 순서로 배치된 run 하나. bidiLevel은 AnalysisItem이 낸 정확한 resolved
// level이지 방향 parity가 아니다(UnicodeAnalysis.h 참조).
struct VisualRun {
    std::uint32_t logicalRunId = 0;
    std::uint8_t bidiLevel = 0;
    std::vector<PositionedGlyph> glyphs;
};

// 확정된 줄 하나. 이 줄의 glyph들은 언제나 "이 줄을 다시 셰이핑한 결과"이며
// 문단 glyph 배열의 조각이 아니다(Task 7.2).
struct TextLine {
    SourceByteRange sourceBytes;
    GraphemeRange graphemes;
    Fixed26_6 baseline = Fixed26_6::FromRaw(0);
    Fixed26_6 advance = Fixed26_6::FromRaw(0);
    Fixed26_6 ascent = Fixed26_6::FromRaw(0);
    Fixed26_6 descent = Fixed26_6::FromRaw(0);
    Fixed26_6 lineGap = Fixed26_6::FromRaw(0);
    Fixed26_6 top = Fixed26_6::FromRaw(0);
    Fixed26_6 bottom = Fixed26_6::FromRaw(0);
    std::vector<VisualRun> visualRuns;
};

// caret이 설 수 있는 자리 하나. 자리는 언제나 논리 grapheme 경계이고,
// affinity가 BiDi 경계에서 같은 경계의 두 시각 위치를 구분한다.
struct CaretStop {
    std::uint32_t logicalGraphemeBoundary = 0;
    CaretAffinity affinity = CaretAffinity::Downstream;
    FixedPoint position;
    std::uint32_t lineIndex = 0;
};

// 캐시에 함께 담기는 문맥 없는 실패 사실.
//
// 진단이 아니라 사실이다. 진단은 assetGuid/sceneObjectId/componentType을 담아
// 문맥에 묶이는데, 같은 불변 레이아웃은 서로 다른 컴포넌트가 공유할 수 있으므로
// 그 문맥을 캐시에 넣으면 두 번째 소비자가 첫 번째의 이름으로 보고된다. 그래서
// 문맥 밖의 부분만 여기 남기고, 매 cold/warm 호출마다
// MakeContextualDiagnostic으로 다시 조립한다.
//
// 패키지 검증은 진단 스트림이 아니라 이 기록을 읽는다: 스트림은 상한이 있고
// warm hit에서는 애초에 셰이퍼가 돌지도 않는다.
struct TextValidationFact {
    TextDiagnosticCode code = TextDiagnosticCode::Utf8Invalid;
    TextSeverity severity = TextSeverity::Error;
    std::string subsystem;
    std::string message;
    std::string remediation;
    SourceByteRange sourceBytes;
    GraphemeRange graphemes;
    bool recoverableAtRuntime = false;
    bool blocksAuthoredPackage = true;
};

// 한 문단의 확정된 결과. 값 타입이고 mutator가 없으며, 캐시는 이것을
// shared_ptr<const>로만 나눠 준다 — 축출은 지분 하나를 놓을 뿐 내용을 고치지
// 않는다.
struct TextLayout {
    std::vector<TextLine> lines;
    std::vector<CaretStop> caretStops;
    FixedSize intrinsicSize;
    bool clipped = false;
    bool ellipsized = false;
    std::vector<TextValidationFact> validationFacts;
};

// 진단을 사람이 추적할 수 있게 만드는 문맥. 캐시 정체성에서 의도적으로
// 제외된다: 같은 불변 레이아웃을 여러 컴포넌트가 재사용하면서 각자의 이름으로
// 진단을 다시 낼 수 있어야 하기 때문이다.
struct TextDiagnosticContext {
    std::string assetGuid;
    unsigned int sceneObjectId = 0;
    std::string componentType;
};

struct TextLayoutRequest {
    std::string utf8;
    ParagraphStyle style;
    LayoutConstraints constraints;
    std::uint64_t visualRevision = 0;
    TextDiagnosticContext diagnosticContext;
};

// 불변 사실 하나를 지금 이 요청의 문맥으로 다시 진단으로 만든다. cold/warm
// 어느 쪽에서도 같은 사실은 같은 code/severity/message/range를 내고, 문맥만
// 호출자의 것으로 바뀐다.
//
// 이 함수는 스트림 상한을 갖지 않는다. 상한은 호출부의 몫이고, 그 규약은
// kMaxFamilyDiagnosticsPerResolve / kMaxShapingDiagnosticsPerItem과 같다:
// 권한 있는 완전한 기록은 진단이 아니라 TextLayout::validationFacts 쪽이다.
inline TextDiagnostic MakeContextualDiagnostic(
    const TextValidationFact& fact, const TextDiagnosticContext& context) {
    TextDiagnostic diagnostic;
    diagnostic.code = fact.code;
    diagnostic.severity = fact.severity;
    diagnostic.subsystem = fact.subsystem;
    diagnostic.message = fact.message;
    diagnostic.remediation = fact.remediation;
    diagnostic.assetGuid = context.assetGuid;
    diagnostic.sceneObjectId = context.sceneObjectId;
    diagnostic.componentType = context.componentType;
    diagnostic.sourceByteRange = fact.sourceBytes;
    return diagnostic;
}

} // namespace molga::text
