#pragma once

#include "Common/Fixed26_6.h"
#include "Text/FontFamilyResolver.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextShapingService.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace molga::text {

// 한 번의 Layout이 호출자의 sink로 낼 수 있는 진단의 상한.
// kMaxFamilyDiagnosticsPerResolve / kMaxShapingDiagnosticsPerItem /
// kMaxAtlasDiagnosticsPerCollection과 같은 규약이고 같은 이유다: 깨진 byte가
// 하나 있는 긴 문단은 grapheme 수만큼 진단을 낼 수 있고, 그 라벨을 프레임마다
// 배치하는 소비자는 로그를 그 수만큼 채운다.
//
// 상한을 넘겨도 정보는 잃지 않는다. 권한 있는 완전한 기록은 진단 스트림이 아니라
// TextLayout::validationFacts이고 그쪽에는 상한이 없다 — 패키지 검증은 그쪽을
// 읽는다(Step 14b). 상한이 사실 쪽에도 있으면 "진단이 잘린 문단은 검증도
// 통과한다"가 되어 상한이 정책이 되어 버린다.
inline constexpr std::size_t kMaxLayoutDiagnosticsPerParagraph = 8;

// TextParagraphCacheKey::overlongTokenPolicy에 들어갈 수 있는 어휘 전부.
// 캐시 헤더가 적어 둔 계약대로 값은 한 곳에서만 정해진다: 서로 다른 두 정책이
// 같은 철자를 쓰면 서로 다른 배치가 한 캐시 항목을 공유한다.
inline constexpr const char* kOverlongTokenPolicyBreakGrapheme =
    "overlong-break-grapheme";
inline constexpr const char* kOverlongTokenPolicyOverflow = "overlong-overflow";
inline constexpr const char* kOverlongTokenPolicyClip = "overlong-clip";
inline constexpr const char* kOverlongTokenPolicyEllipsis = "overlong-ellipsis";
std::string OverlongTokenPolicy(TextWrapMode, TextOverflowMode);

// 문단 하나를 확정된 불변 배치로 바꾼다.
//
// 계약의 핵심은 하나다: 확정된 줄의 glyph 배열은 언제나 "그 줄을 그 줄의 진짜
// BOT/EOT 문맥으로 다시 셰이핑한 결과"이며 문단 셰이핑 결과의 조각이 아니다.
// 문단 셰이핑은 후보 줄바꿈 자리를 재기 위해서만 돌고, HarfBuzz가
// unsafe-to-break로 표시한 경계는 후보에서 아예 빠진다.
//
// 실패 정책은 fail-closed다. 준비되지 않은 런타임, 깨진 불변식, 셰이핑 실패는
// 진단 하나와 nullopt이며 대체 배치는 없다. 반대로 깨진 UTF-8, 없는 family,
// 없는 glyph는 실패가 아니라 validationFacts를 단 성공이다.
//
// 단일 thread 전용이다. FontRepository/FontFamilyResolver/TextShapingService/
// TextLayoutCache와 같은 계약이고, 아래 관찰 상태도 프로세스 전역이라 두
// thread가 동시에 배치하면 서로의 관찰을 지운다.
class TextLayoutService {
public:
    TextLayoutService(FontFamilyResolver&, TextShapingService&,
                      TextLayoutCache&);
    std::optional<std::shared_ptr<const TextLayout>> Layout(
        const TextLayoutRequest&, TextDiagnosticSink&);

private:
    FontFamilyResolver& resolver_;
    TextShapingService& shaper_;
    TextLayoutCache& cache_;
};

namespace detail {

// UnicodeAnalysis.h / TextShapingService.h의 계수기와 같은 성격의 seam이고 같은
// 이유로 출하되는 빌드에 들어 있다. 시험할 가치가 있는 주장 — "warm 조회는 ICU
// 객체를 하나도 만들지 않는다", "확정된 줄마다 최종 셰이핑이 한 번 있었다" —
// 은 전부 프로덕션 경로에 대한 것이라, 테스트에만 컴파일되는 계수기는 다른
// 프로그램을 재는 셈이 된다.

// 이 서비스가 직접 만든 ICU 객체 수(줄 BiDi). UnicodeAnalysis의 계수기는
// 자기 파일 안의 호출만 세므로, 두 값을 함께 보아야 "ICU를 하나도 만들지
// 않았다"가 성립한다.
std::uint64_t LayoutIcuObjectCreationCount() noexcept;
void          ResetLayoutIcuObjectCreationCount() noexcept;

// 줄임표 후보 하나를 셰이핑한 기록.
//
// candidateBufferBytes는 실제로 만들어진 임시 버퍼의 전체 길이이고,
// retainedBytes + ellipsisBytes와 같아야 "남긴 글과 줄임표를 한 후보로 묶었다"가
// 성립한다. shapedBytes는 그 버퍼에서 실제로 셰이핑된 구간의 합이라, 후보의
// 일부만 셰이핑하고 나머지를 이어 붙인 구현과 구분된다.
struct LayoutEllipsisCandidate {
    std::uint32_t candidateBufferBytes = 0;
    std::uint32_t retainedBytes        = 0;
    std::uint32_t ellipsisBytes        = 0;
    std::uint32_t shapedBytes          = 0;
    bool          beginningOfText      = false;
    bool          endOfText            = false;
    // 이 후보가 놓인 확정된 줄의 진짜 BOT/EOT. 위 둘과 다르면 후보가 문단
    // 문맥이 아니라 자기만의 문맥으로 셰이핑된 것이다.
    bool          lineBeginningOfText  = false;
    bool          lineEndOfText        = false;
};

bool EllipsisCandidateWasShapedWhole(const LayoutEllipsisCandidate&) noexcept;

// Step 13b의 방향 규칙 하나. 시각 진행 순서에서 visualIndex번째(1부터) caret이
// 어느 논리 grapheme 경계인가를 답한다.
//
// 프로덕션 경로가 이 함수를 부른다. 따로 내놓은 이유는 하나뿐이다: 커밋된 여섯
// 자격 폰트에는 grapheme 셋 이상을 덮는 RTL cluster가 없고, N이 2일 때는 두
// 규칙(begin+i와 end-i)이 수치로 일치한다. 그래서 방향을 통째로 무시하는 회귀가
// 어떤 픽스처로도 관찰되지 않는다. 규칙 자체를 N>=3으로 직접 시험할 수 있게
// 두는 것이 지금 할 수 있는 가장 강한 관찰이다.
std::uint32_t InteriorCaretLogicalBoundary(GraphemeRange glyphGraphemes,
                                           std::uint32_t visualIndex,
                                           bool rightToLeft) noexcept;

// Step 13a의 수용 규칙 하나. 폰트가 준 GDEF caret 집합을 통째로 받을지
// 버릴지를 답한다: grapheme이 N개인 최종 glyph에 대해 정확히 N-1개이고,
// 시각 진행 순서로 엄격히 증가하며, 0과 최종 advance 사이에 엄격히 들어 있는
// 집합만 참이다.
//
// 프로덕션 경로가 이 함수를 부른다. 따로 내놓은 이유는 InteriorCaretLogicalBoundary와
// 같다: 커밋된 여섯 자격 폰트가 내는 집합은 언제나 정확히 N-1개이고 advance
// 안쪽이라, "정확히"를 ">="로 늦추거나 "엄격히 안쪽"을 "advance까지"로 늦추는
// 회귀가 어떤 픽스처로도 관찰되지 않는다.
bool AdjustedGdefCaretSetIsUsable(const std::vector<Fixed26_6>& offsets,
                                  std::uint32_t graphemes,
                                  Fixed26_6 advance) noexcept;

// 관찰 기록의 상한. 관찰은 기록이 아니라 seam이므로, 긴 문단 하나가 이 벡터를
// 텍스트 길이만큼 키우게 두지 않는다(TextShapingService.h와 같은 규칙).
inline constexpr std::size_t kMaxLayoutObservations = 1024;

// Layout은 호출 시작에서 이 기록을 비운다.
struct LayoutObservations {
    // 후보 줄바꿈 자리를 재기 위한 문단 셰이핑 pass 수. cold 배치 한 번에 1,
    // warm 적중에는 0이다.
    std::size_t paragraphShapePasses = 0;
    // 확정된 줄마다 하나씩, 그 줄의 glyph 배열을 만들어 낸 최종 셰이핑이
    // 실제로 건네받은 원본 byte 구간.
    //
    // 값은 오직 최종 셰이핑 호출 안에서만 채워진다(ShapedLine::handledBytes).
    // 확정된 줄의 범위를 그대로 베껴 쓰면 이 관찰은 "줄이 있다"만 말하게 되고,
    // 문단 배열을 잘라 쓰는 구현과 다시 셰이핑하는 구현이 같은 값을 낸다.
    // 셰이핑을 부르지 않고 확정된 줄(길이 0의 빈 줄)만이 예외이며, 그 줄은
    // 셰이핑할 것이 없으므로 자기 자리의 길이 0 구간을 남긴다.
    std::vector<SourceByteRange> acceptedLineShapes;
    // 최종 줄 셰이핑으로 실제 호출된 ShapeAnalysisItem 횟수.
    std::size_t finalLineItemShapeCalls = 0;
    // 되짚어 앞 후보로 물러난 횟수(Step 8).
    std::size_t backtrackSteps = 0;
    // unsafe-to-break 때문에 후보에서 빠진 grapheme 경계들.
    std::vector<std::uint32_t> discardedUnsafeBoundaries;
    // 줄임표 후보를 만든 횟수(Step 10c의 반복 수).
    std::size_t ellipsisRemovalAttempts = 0;
    std::vector<LayoutEllipsisCandidate> ellipsisCandidates;
    // 이번 호출이 실제로 캐시에 건넨 두 키. 두 키의 필드는 전부 정체성이지만
    // 저장과 조회가 같은 함수로 만들어지므로, 프로덕션이 무엇을 넣었는지를
    // 밖에서 볼 방법이 이것뿐이다: 값이 통째로 틀려도 warm 적중은 그대로
    // 일어나고 어떤 배치도 달라지지 않는다.
    //
    // 복사가 아니라 지분이다. 최종 키는 piece마다 문단 원문을 한 벌씩 담으므로
    // (TextShapeCacheKey::originalUtf8), 관찰이 값을 복사하면 배치마다 그
    // 비용을 두 번 내게 된다.
    std::shared_ptr<const TextLayoutRequestIndexKey> requestKey;
    std::shared_ptr<const TextParagraphCacheKey>     finalKey;
};
const LayoutObservations& CurrentLayoutObservations() noexcept;

}  // namespace detail

}  // namespace molga::text
