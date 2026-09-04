#pragma once

#include "Common/Fixed26_6.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga::text {

// 하나의 OpenType feature와 그것이 적용되는 원본 byte 구간.
//
// 구간은 저작 원본 UTF-8 offset이다. HarfBuzz의 hb_feature_t는 start/end를
// cluster 값으로 읽고, 이 셰이퍼는 cluster를 언제나 원본 byte 시작으로 다시
// 쓰므로 두 좌표계가 같은 것을 가리킨다. 겹치는 구간과 순서는 저작된 그대로
// 보존한다: 정규화하면 "뒤에 적힌 것이 이긴다"는 저작 규칙이 사라진다.
struct ShapeFeature {
    std::uint32_t tag = 0;
    std::uint32_t value = 0;
    SourceByteRange sourceBytes;
};

// cluster 정책의 공개 표현. HarfBuzz의 매크로/타입은 여기 들어오지 않는다 —
// 이 enum이 캐시 키와 헤더가 볼 수 있는 전부이고, 매핑은
// TextShapingService.cpp 안에서만 이루어진다.
enum class TextClusterPolicy : std::uint8_t {
    MonotoneCharacters = 1
};

struct ShapeStyle {
    Fixed26_6 fontSize = Fixed26_6::FromRaw(16 * 64);
    std::string language = "und";
    std::vector<ShapeFeature> orderedFeatures;
    TextClusterPolicy clusterPolicy =
        TextClusterPolicy::MonotoneCharacters;
};

// 이 셰이핑 요청이 진짜 텍스트의 시작/끝에 닿아 있는가. HarfBuzz의 BOT/EOT는
// 추측 대상이 아니다: 문단 중간을 BOT로 셰이핑하면 아랍어 첫 글자가 어두형으로
// 나오고, 그 오류는 진단 없이 글자 모양으로만 드러난다.
struct ShapeBoundaryFlags { bool beginningOfText = false; bool endOfText = false; };

// 셰이핑된 glyph 하나. 자기 face 자원의 지분을 들고 있으므로, 이 값이 살아
// 있는 동안 hot reload가 밑에서 바이트를 갈아 끼울 수 없다.
//
// fontSize는 style에서 그대로 복사한 값이다. 레이아웃과 렌더링은 advance,
// bitmap 경계, affine scale 중 어느 것에서도 크기를 역산하지 않는다: 그 셋은
// 전부 폰트마다 다른 근사이고, 같은 요청이 폰트에 따라 다른 크기로 배치된다.
//
// missing=true인 기록은 face가 하나도 없다는 뜻이다. 그때 faceResource는
// nullptr이고 advanceX는 정확히 1 em이다.
struct ShapedGlyph {
    std::string fontGuid;
    std::string fontRevision;
    std::uint32_t faceIndex = 0;
    std::uint32_t glyphId = 0;
    FontFaceResourcePtr faceResource;
    Fixed26_6 fontSize = Fixed26_6::FromRaw(0);
    Fixed26_6 advanceX = Fixed26_6::FromRaw(0);
    Fixed26_6 advanceY = Fixed26_6::FromRaw(0);
    Fixed26_6 offsetX = Fixed26_6::FromRaw(0);
    Fixed26_6 offsetY = Fixed26_6::FromRaw(0);
    SourceByteRange sourceBytes;
    GraphemeRange graphemes;
    std::uint8_t bidiLevel = 0;
    std::uint32_t logicalRunId = 0;
    // hb_glyph_info_get_glyph_flags가 낸 값 그대로다. 이 헤더는 HarfBuzz 타입을
    // 담지 않으므로 해석은 소비자 쪽에서 <hb.h>를 포함해 한다.
    //
    // 이 중 HB_GLYPH_FLAG_UNSAFE_TO_BREAK는 힌트가 아니라 금지다. 그 비트가 선
    // glyph 앞에서 문단 glyph 배열을 자르면 그 자리의 결합/커닝이 조용히
    // 사라지므로, 레이아웃은 그 경계를 넘어 줄을 나눌 수 없다(Milestone 7).
    std::uint32_t harfbuzzGlyphFlags = 0;
    std::vector<Fixed26_6> adjustedGdefCaretOffsets;
    bool missing = false;
};

// 하나의 선택된 span이 낸 glyph들. 논리 순서로 나오며, 시각 순서로 재배열되지
// 않는다. span 안의 glyph 순서는 HarfBuzz가 낸 그대로다(RTL span은 HarfBuzz
// 규약대로 시각 순서로 나온다).
struct ShapedRun {
    std::vector<ShapedGlyph> glyphs;
    std::uint8_t bidiLevel = 0;
};

// 한 번의 ShapeAnalysisItem이 이 셰이퍼 자신의 이름으로 낼 수 있는 진단의
// 상한. 없는 glyph마다 진단을 내면 지원되지 않는 script로 적힌 라벨 하나가
// 프레임마다 로그를 grapheme 수만큼 채운다.
//
// 상한을 넘겨도 셰이핑은 그대로 진행되고, 정보도 잃지 않는다: 권한 있는 기록은
// 진단 스트림이 아니라 돌려주는 ShapedGlyph들의 missing=true이고 그쪽에는
// 상한이 없다. 패키지 검증은 그쪽을 읽는다.
//
// 상한 밖에 있는 것이 둘 있다. 하나는 종결 진단(LayoutInvalid,
// DependencyInvalid)이다. 그것들은 곧바로 nullopt로 이어져 호출당 많아야
// 하나이고, 예산에 밀려 사라지면 "실패했는데 이유를 말하는 진단이 없다"가 된다.
// 다른 하나는 문단 전체의 누계다: 이 값은 ShapeAnalysisItem 한 번의 상한이므로,
// item을 돌며 셰이핑하는 소비자의 상한은 여전히 그 호출부의 몫이다
// (FontFamilyResolver.h의 kMaxFamilyDiagnosticsPerResolve와 같은 규칙이다).
inline constexpr std::size_t kMaxShapingDiagnosticsPerItem = 8;

// 하나의 AnalysisItem을 grapheme 단위로 원자적인 face 선택과 문맥 셰이핑으로
// 바꾼다.
//
// 실패 정책은 fail-closed다. 준비되지 않은 런타임(DependencyInvalid)과 깨진
// 입력/불변식(LayoutInvalid)은 진단 하나와 nullopt이며, ASCII 대체 셰이핑도
// 호스트 폰트 조회도 없다. face가 하나도 맞지 않는 것은 실패가 아니라
// missing=true 기록과 MissingGlyph 진단으로 성공한다.
//
// 단일 thread 전용이다. FontRepository/FontFamilyResolver와 같은 이유이고,
// 아래 detail의 관찰 상태도 프로세스 전역이라 두 thread가 동시에 셰이핑하면
// 서로의 관찰을 지운다.
//
// 비용은 호출당 상수가 지배한다. 이 서비스는 상태가 없어서 ShapeAnalysisItem
// 한 번마다 후보 face의 hb_blob/hb_face/hb_font를 새로 만들고 shape plan을 차게
// 시작한다. 재 본 값으로 debug 빌드에서 item 하나당 약 0.2ms이므로, 문단을
// 프레임마다 셰이핑하는 소비자(Milestone 7의 최종 줄 재셰이핑, Task 8.2의 UI
// 경로)는 face 캐시를 앞에 두어야 한다. 여기 인터페이스를 바꿀 필요는 없다 —
// (FontFaceResource*, 크기) -> hb_font_t 캐시는 private 멤버로 들어갈 수 있다.
class TextShapingService {
public:
    std::optional<std::vector<ShapedRun>> ShapeAnalysisItem(
        const UnicodeTextBuffer&, const UnicodeAnalysis&,
        const AnalysisItem&, const ResolvedFamily&, const ShapeStyle&,
        ShapeBoundaryFlags, TextDiagnosticSink&);
};

namespace detail {

// UnicodeAnalysis.h의 ICU 계수기와 같은 성격의 seam이고, 같은 이유로 출하되는
// 빌드에 들어 있다. 시험할 가치가 있는 유일한 주장 — "런타임이 준비되기 전에는
// HarfBuzz 객체를 하나도 만들지 않는다" — 은 프로덕션 경로에 대한 것이라,
// 테스트에만 컴파일되는 계수기는 다른 프로그램을 재는 셈이 된다.

// 시도된 HarfBuzz 객체(blob/face/font/buffer/unicode funcs) 생성 횟수. 성공
// 여부와 무관하게 호출 직전에 올린다. 사후에 세면 "준비되지 않았는데도
// 만들려다 실패해서 흔적이 없다"는 가장 위험한 경우가 0으로 보인다.
std::uint64_t HarfBuzzObjectCreationCount() noexcept;
void          ResetHarfBuzzObjectCreationCount() noexcept;

// 후보 probe가 낸 .notdef 하나의 원본 byte 구간. 대상 grapheme과 겹치는
// .notdef만이 face를 거절한다는 규칙은 이 기록으로만 관찰된다.
struct ShapingProbeNotdef {
    SourceByteRange notdefBytes;
};

// 선택이 끝난 뒤 실제로 hb_shape에 넘어간 span 하나. contextBytes는 HarfBuzz가
// 받은 sanitized 문맥 배열의 구간이고 itemBytes는 그 span이 속한 AnalysisItem의
// 구간이다. 둘이 같아야 "선택 뒤 문맥을 다시 셰이핑했다"가 성립한다.
struct ShapingFinalSpanShape {
    GraphemeRange   spanGraphemes;
    SourceByteRange contextBytes;
    SourceByteRange itemBytes;
};

// 변이 선택자 하나에 대한 판정. explicitRecord는 선택된 face의 cmap format 14가
// 그 (base, selector) 쌍을 실제로 이름 붙였다는 뜻이고, false는 기록이 없어
// base의 기본 표현으로 받아들였다는 뜻이다.
struct ShapingVariationDecision {
    char32_t base = 0;
    char32_t selector = 0;
    bool     explicitRecord = false;
};

// 관찰 기록의 상한. 관찰은 기록이 아니라 seam이므로, 긴 문단 하나가 이 벡터를
// 텍스트 길이만큼 키우게 두지 않는다.
inline constexpr std::size_t kMaxShapingObservations = 1024;

// ShapeAnalysisItem은 호출 시작에서 이 기록을 비운다. 여러 item을 셰이핑하는
// 소비자는 호출 사이에 스스로 누적해야 한다.
struct ShapingObservations {
    std::vector<char32_t>                 cmapRequirements;
    std::vector<ShapingProbeNotdef>       probeNotdefs;
    std::vector<ShapingFinalSpanShape>    finalSpanShapes;
    std::vector<ShapingVariationDecision> variationDecisions;
    std::size_t                           selectedSpans = 0;
    // hb_shape 호출 수(probe + 최종)와, 그중 buffer가 실제로
    // hb_icu_get_unicode_funcs()를 들고 있던 횟수. 둘이 같아야 "ICU Unicode
    // funcs로 셰이핑했다"가 성립한다. 이 관찰이 없으면 설치를 통째로 빼도
    // HarfBuzz가 내장 UCD 표로 조용히 같은 결과를 내므로 아무 단언도 움직이지
    // 않는다.
    std::size_t                           shapeCalls = 0;
    std::size_t                           shapeCallsWithIcuUnicodeFuncs = 0;
};
const ShapingObservations& Observations() noexcept;

}  // namespace detail

}  // namespace molga::text
