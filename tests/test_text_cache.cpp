#include "Assets/FontAsset.h"
#include "Common/Fixed26_6.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextRuntimeDependencies.h"
#include "Text/TextShapingService.h"
#include "Text/UnicodeAnalysis.h"
#include "doctest.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace text = molga::text;
using molga::Fixed26_6;
using molga::FixedPoint;
using molga::FixedSize;
using molga::FontArtifactStorage;
using molga::FontSlant;
using text::CaretAffinity;
using text::GraphemeRange;
using text::SourceByteRange;
using text::TextClusterPolicy;
using text::TextParagraphCacheKeyHash;
using text::TextShapeCacheKeyHash;

// ── One-field mutation entries ───────────────────────────────────────────────
// 이 프로그램에서 반복해 살아남은 결함은 "정답과 오답이 일치하는 자리에서 잰
// 단언"이었다. 캐시 키에서 그 모양은 정확히 하나다: fixture 두 개가 두 개 이상의
// 필드에서 다르면, 키에서 아무 필드나 하나 빼도 아무 단언도 움직이지 않는다.
// 그래서 아래 표들은 언제나 필드 하나만 바꾸고, 바꾼 필드의 이름을 함께 들고
// 다닌다. 이름이 없으면 실패 메시지가 "50개 중 하나가 적중했다"까지만 말한다.
template <class TKey>
struct Mutated {
    std::string fieldName;
    TKey        key;
    // 계획서가 verbatim으로 고정한 루프는 이 값을 그대로 조회 함수에 넘긴다.
    // 이름을 함께 나르면서도 그 블록이 컴파일되게 하는 것이 이 변환의 전부다.
    operator const TKey&() const noexcept { return key; }
};

using MutatedShapeKey   = Mutated<text::TextShapeCacheKey>;
using MutatedRequestKey = Mutated<text::TextLayoutRequestIndexKey>;
using MutatedFinalKey   = Mutated<text::TextParagraphCacheKey>;

// ── Fixture values ───────────────────────────────────────────────────────────
// 값은 전부 서로 다르다. 두 후보 face나 두 feature가 같은 내용이면 "순서를
// 바꿨다"는 변이가 no-op이 되어 순서 계약이 시험되지 않는다.
constexpr const char* kFamilyGuid   = "11111111111111111111111111111111";
constexpr const char* kFallbackGuid = "22222222222222222222222222222222";
constexpr const char* kLatinGuid    = "44444444444444444444444444444444";
constexpr const char* kArabicGuid   = "66666666666666666666666666666666";
constexpr const char* kIcuRevision  = "78.1";
constexpr const char* kHbRevision   = "14.3.1";

// 캐시 계층이 내놓는 이식 가능한 파생을 쓴다. std::hash<std::string>은 표준
// 라이브러리 버전마다 값이 달라 키를 툴체인에 묶는다.
std::uint64_t FixtureBytesHash(const std::string& bytes) {
    return text::CacheBytesHash(bytes);
}

// 항목 하나의 owned-byte가 실제로 몇인지는 구현 세부다. 예산이 그 값을 재고
// 있다는 사실만 관찰하면 되므로, 항목이 처음으로 살아남는 2의 거듭제곱 예산을
// 찾아 쓴다. 하드코딩한 숫자는 추정식이 조금만 바뀌어도 조용히 무의미해진다.
constexpr std::uint64_t kBudgetSearchCeiling = 1ULL << 24;

std::string DependencyContractSha() {
    return text::TextRuntimeDependencies::Get().DependencyContractSha256();
}

text::ShapeFeature MakeFeature(std::uint32_t tag, std::uint32_t value,
                               std::uint32_t begin, std::uint32_t end) {
    text::ShapeFeature feature;
    feature.tag         = tag;
    feature.value       = value;
    feature.sourceBytes = SourceByteRange{begin, end};
    return feature;
}

text::ShapeStyle MakeShapeStyle() {
    text::ShapeStyle style;
    style.fontSize        = Fixed26_6::FromRaw(18 * 64);
    style.language        = "fa";
    style.orderedFeatures = {MakeFeature(0x6C696761u, 1, 0, 8),
                             MakeFeature(0x6B65726Eu, 0, 2, 5)};
    style.clusterPolicy   = TextClusterPolicy::MonotoneCharacters;
    return style;
}

text::ParagraphStyle MakeStyle() {
    text::ParagraphStyle style;
    style.fontFamilyGuid              = kFamilyGuid;
    style.fontRequest.weight          = 500;
    style.fontRequest.stretchPercent  = 110;
    style.fontRequest.slant           = FontSlant::Italic;
    style.shape                       = MakeShapeStyle();
    style.analysis.locale             = "fa-IR";
    style.analysis.baseDirection      = text::BaseDirection::RightToLeft;
    style.wrap                        = text::TextWrapMode::Word;
    style.overflow                    = text::TextOverflowMode::Ellipsis;
    style.maxLines                    = 3;
    style.lineSpacing                 = Fixed26_6::FromRaw(80);
    style.horizontal                  = text::TextHorizontalAlignment::Center;
    style.vertical                    = text::TextVerticalAlignment::Middle;
    style.ellipsisUtf8                = u8"…";
    return style;
}

text::FamilyNodeRequestIdentity MakeNode(std::string guid, bool exists,
                                         std::uint64_t generation,
                                         std::vector<std::string> edges) {
    text::FamilyNodeRequestIdentity node;
    node.familyGuid             = std::move(guid);
    node.exists                 = exists;
    node.contentGeneration      = generation;
    node.authoredFallbackGuids  = std::move(edges);
    return node;
}

text::FaceRequestIdentity MakeFaceIdentity(std::string guid, std::uint32_t faceIndex,
                                           std::uint32_t authoredFaceIndex,
                                           FontArtifactStorage storage,
                                           const char* relativePath) {
    text::FaceRequestIdentity face;
    face.fontGuid                    = guid;
    face.fontRevision                = guid + ":" + std::to_string(faceIndex);
    face.sourceSha256                = "a1" + guid;
    face.artifactSha256              = "a1" + guid;
    face.artifactLocator.storage     = storage;
    face.artifactLocator.relativePath = relativePath;
    face.artifactByteSize            = 4096 + faceIndex;
    face.contentGeneration           = 7 + faceIndex;
    face.faceIndex                   = faceIndex;
    face.authoredFaceIndex           = authoredFaceIndex;
    return face;
}

text::ResolvedFamilyRequestIdentity MakeClosure() {
    text::ResolvedFamilyRequestIdentity closure;
    closure.requestedGuid = kFamilyGuid;
    closure.depthFirstFamilyNodes = {
        MakeNode(kFamilyGuid, true, 11, {kFallbackGuid, kLatinGuid}),
        MakeNode(kFallbackGuid, false, 0, {})};
    closure.orderedCandidates = {
        MakeFaceIdentity(kArabicGuid, 0, 1, FontArtifactStorage::ProjectLibrary,
                         "Library/Imported/Fonts/arabic.sfnt"),
        MakeFaceIdentity(kLatinGuid, 1, 0, FontArtifactStorage::PackagedResource,
                         "Resources/Fonts/latin.sfnt")};
    return closure;
}

text::SelectedFaceShapeIdentity MakeSelectedFace(std::string guid,
                                                 std::uint32_t faceIndex,
                                                 FontArtifactStorage storage,
                                                 const char* relativePath) {
    text::SelectedFaceShapeIdentity face;
    face.fontGuid                     = guid;
    face.fontRevision                 = guid + ":" + std::to_string(faceIndex);
    face.sourceSha256                 = "b2" + guid;
    face.artifactSha256               = "b2" + guid;
    face.artifactLocator.storage      = storage;
    face.artifactLocator.relativePath = relativePath;
    face.artifactByteSize             = 8192 + faceIndex;
    face.contentGeneration            = 21 + faceIndex;
    face.faceIndex                    = faceIndex;
    return face;
}

text::ShapeInputSourceSpan MakeSpan(SourceByteRange input, SourceByteRange original,
                                    GraphemeRange graphemes, bool synthetic) {
    text::ShapeInputSourceSpan span;
    span.shapeInputBytes     = input;
    span.originalSourceBytes = original;
    span.originalGraphemes   = graphemes;
    span.synthetic           = synthetic;
    return span;
}

text::TextLayoutRequestIndexKey MakeRequestKey(std::string utf8) {
    text::TextLayoutRequestIndexKey key;
    key.decodePolicyVersion            = 1;
    key.familyResolutionPolicyVersion  = 1;
    key.fallbackPolicyVersion          = 1;
    key.originalUtf8                   = std::move(utf8);
    key.originalBytesHash              = FixtureBytesHash(key.originalUtf8);
    key.style                          = MakeStyle();
    key.constraints.width              = Fixed26_6::FromRaw(300 * 64);
    key.constraints.height             = Fixed26_6::FromRaw(120 * 64);
    key.visualRevision                 = 42;
    key.familyClosure                  = MakeClosure();
    key.harfbuzzRevision               = kHbRevision;
    key.icuRevision                    = kIcuRevision;
    key.icuDataSha256                  = text::kPackagedIcuDataSha256;
    key.requestedGraphemeRulePolicyIdentity =
        text::RequestedGraphemeRulePolicyIdentity(key.icuRevision,
                                                  key.icuDataSha256);
    key.requestedLineBreakRulePolicyIdentity =
        text::RequestedLineBreakRulePolicyIdentity(key.icuRevision,
                                                   key.icuDataSha256);
    key.dependencyContractSha256       = DependencyContractSha();
    return key;
}

text::TextShapeCacheKey MakeShapeKey(const text::TextLayoutRequestIndexKey& request) {
    text::TextShapeCacheKey key;
    key.decodePolicyVersion   = request.decodePolicyVersion;
    key.originalUtf8          = request.originalUtf8;
    key.originalBytesHash     = request.originalBytesHash;
    key.shapeInputUtf8        = request.originalUtf8;
    key.shapeInputBytesHash   = request.originalBytesHash;
    key.shapeInputMapping     = {
        MakeSpan(SourceByteRange{0, 8}, SourceByteRange{0, 8},
                 GraphemeRange{0, 4}, false),
        MakeSpan(SourceByteRange{8, 11}, SourceByteRange{11, 13},
                 GraphemeRange{5, 6}, true)};
    key.paragraphBytes        = SourceByteRange{0, 13};
    key.runBytes              = SourceByteRange{0, 8};
    key.fallbackGraphGeneration = 9;
    key.selectedFaces         = {
        MakeSelectedFace(kArabicGuid, 0, FontArtifactStorage::ProjectLibrary,
                         "Library/Imported/Fonts/arabic.sfnt"),
        MakeSelectedFace(kLatinGuid, 1, FontArtifactStorage::PackagedResource,
                         "Resources/Fonts/latin.sfnt")};
    key.fontSize              = request.style.shape.fontSize;
    key.variationKey          = 0;
    key.embeddingLevel        = 1;
    key.direction             = 1;
    key.scriptCode            = 5;
    key.language              = request.style.shape.language;
    key.componentLocale       = request.style.analysis.locale;
    key.resolvedGraphemeLocale  = "fa";
    key.resolvedLineBreakLocale = "fa_IR";
    key.graphemeRuleIdentity    = "1111111111111111111111111111111111111111111111111111111111111111";
    key.lineBreakRuleIdentity   = "2222222222222222222222222222222222222222222222222222222222222222";
    key.analysisGeneration      = 7;
    key.boundaries.beginningOfText = true;
    key.boundaries.endOfText       = false;
    key.harfbuzzBufferFlags     = 5;
    key.clusterPolicy           = TextClusterPolicy::MonotoneCharacters;
    key.orderedFeatures         = request.style.shape.orderedFeatures;
    key.harfbuzzRevision        = request.harfbuzzRevision;
    key.icuRevision             = request.icuRevision;
    key.icuDataSha256           = request.icuDataSha256;
    key.dependencyContractSha256 = request.dependencyContractSha256;
    return key;
}

text::TextParagraphCacheKey MakeFinalKey(const text::TextLayoutRequestIndexKey& request) {
    text::TextShapeCacheKey firstLine  = MakeShapeKey(request);
    text::TextShapeCacheKey secondLine = MakeShapeKey(request);
    secondLine.runBytes                = SourceByteRange{8, 13};
    secondLine.boundaries.beginningOfText = false;
    secondLine.boundaries.endOfText       = true;

    text::TextParagraphCacheKey key;
    key.finalLineShapeKeys  = {firstLine, secondLine};
    key.constraints         = request.constraints;
    key.wrap                = request.style.wrap;
    key.overflow            = request.style.overflow;
    key.overlongTokenPolicy = "break-grapheme";
    key.ellipsisUtf8        = request.style.ellipsisUtf8;
    key.ellipsisStyle       = request.style.shape;
    key.maxLines            = request.style.maxLines;
    key.lineSpacing         = request.style.lineSpacing;
    key.horizontal          = request.style.horizontal;
    key.vertical            = request.style.vertical;
    key.visualRevision      = request.visualRevision;
    return key;
}

// ── Immutable cache payloads ─────────────────────────────────────────────────
text::ShapedGlyph MakeGlyph(std::uint32_t glyphId, std::uint32_t begin,
                            std::uint32_t end) {
    text::ShapedGlyph glyph;
    glyph.fontGuid    = kArabicGuid;
    glyph.fontRevision = std::string(kArabicGuid) + ":0";
    glyph.faceIndex   = 0;
    glyph.glyphId     = glyphId;
    glyph.fontSize    = Fixed26_6::FromRaw(18 * 64);
    glyph.advanceX    = Fixed26_6::FromRaw(9 * 64);
    glyph.sourceBytes = SourceByteRange{begin, end};
    glyph.graphemes   = GraphemeRange{begin, end};
    glyph.bidiLevel   = 1;
    glyph.logicalRunId = 0;
    // Task 5.2가 남긴 UNSAFE_TO_BREAK 비트가 실려 다니는 자리다. 값 자체가
    // 캐시된 결과의 일부이므로 canonical 직렬화가 이것도 본다.
    glyph.harfbuzzGlyphFlags = 1;
    return glyph;
}

text::TextValidationFact MakeFact() {
    text::TextValidationFact fact;
    fact.code                 = text::TextDiagnosticCode::MissingGlyph;
    fact.severity             = text::TextSeverity::Warning;
    fact.subsystem            = "text.layout";
    fact.message              = "no candidate face covers U+0640";
    fact.remediation          = "add an Arabic fallback family";
    fact.sourceBytes          = SourceByteRange{4, 6};
    fact.graphemes            = GraphemeRange{2, 3};
    fact.recoverableAtRuntime = true;
    fact.blocksAuthoredPackage = false;
    return fact;
}

text::TextLayout MakeFixtureLayout() {
    text::PositionedGlyph first;
    first.glyph  = MakeGlyph(11, 0, 2);
    first.origin = FixedPoint{Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(0)};
    text::GlyphInteriorCaret interior;
    interior.logicalGraphemeBoundary = 1;
    interior.position = FixedPoint{Fixed26_6::FromRaw(32), Fixed26_6::FromRaw(0)};
    interior.fromAdjustedGdef = true;
    first.interiorCarets = {interior};

    text::PositionedGlyph second;
    second.glyph  = MakeGlyph(12, 2, 4);
    second.origin = FixedPoint{Fixed26_6::FromRaw(9 * 64), Fixed26_6::FromRaw(0)};

    text::VisualRun run;
    run.logicalRunId = 0;
    run.bidiLevel    = 1;
    run.glyphs       = {first, second};

    text::TextLine line;
    line.sourceBytes = SourceByteRange{0, 4};
    line.graphemes   = GraphemeRange{0, 2};
    line.baseline    = Fixed26_6::FromRaw(14 * 64);
    line.advance     = Fixed26_6::FromRaw(18 * 64);
    line.ascent      = Fixed26_6::FromRaw(14 * 64);
    line.descent     = Fixed26_6::FromRaw(4 * 64);
    line.lineGap     = Fixed26_6::FromRaw(64);
    line.top         = Fixed26_6::FromRaw(0);
    line.bottom      = Fixed26_6::FromRaw(18 * 64);
    line.visualRuns  = {run};

    text::CaretStop stop;
    stop.logicalGraphemeBoundary = 0;
    stop.affinity  = CaretAffinity::Downstream;
    stop.position  = FixedPoint{Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(14 * 64)};
    stop.lineIndex = 0;

    text::TextLayout layout;
    layout.lines         = {line};
    layout.caretStops    = {stop};
    layout.intrinsicSize = FixedSize{Fixed26_6::FromRaw(18 * 64),
                                     Fixed26_6::FromRaw(18 * 64)};
    layout.clipped       = false;
    layout.ellipsized    = true;
    layout.validationFacts = {MakeFact()};
    return layout;
}

text::TextLayout MakeOtherLayout() {
    text::TextLayout layout = MakeFixtureLayout();
    layout.ellipsized       = false;
    layout.intrinsicSize    = FixedSize{Fixed26_6::FromRaw(36 * 64),
                                        Fixed26_6::FromRaw(18 * 64)};
    return layout;
}

text::TextLayout MakeLayoutWithoutFacts() {
    text::TextLayout layout = MakeFixtureLayout();
    layout.validationFacts.clear();
    return layout;
}

text::CachedShapeResult MakeShapeResultValue() {
    text::ShapedRun run;
    run.bidiLevel = 1;
    run.glyphs    = {MakeGlyph(11, 0, 2), MakeGlyph(12, 2, 4)};
    text::CachedShapeResult result;
    result.runs            = {run};
    result.validationFacts = {MakeFact()};
    return result;
}

// TextLayout 전체를 결정적인 문자열로 편다. 축출이 "캐시 소유권만" 버리고
// 바깥 공유 소유자를 건드리지 않았다는 주장은 이 문자열로만 관찰된다 —
// 포인터 비교는 내용이 조용히 바뀌어도 통과한다.
std::string CanonicalizeLayout(const text::TextLayout& layout) {
    std::ostringstream out;
    out << "layout|lines=" << layout.lines.size();
    for (const auto& line : layout.lines) {
        out << "|line(" << line.sourceBytes.begin << ',' << line.sourceBytes.end
            << ';' << line.graphemes.begin << ',' << line.graphemes.end << ';'
            << line.baseline.Raw() << ',' << line.advance.Raw() << ','
            << line.ascent.Raw() << ',' << line.descent.Raw() << ','
            << line.lineGap.Raw() << ',' << line.top.Raw() << ','
            << line.bottom.Raw() << ')';
        for (const auto& run : line.visualRuns) {
            out << "|run(" << run.logicalRunId << ','
                << static_cast<unsigned>(run.bidiLevel) << ')';
            for (const auto& positioned : run.glyphs) {
                const text::ShapedGlyph& glyph = positioned.glyph;
                out << "|glyph(" << glyph.fontGuid << ',' << glyph.fontRevision
                    << ',' << glyph.faceIndex << ',' << glyph.glyphId << ','
                    << glyph.fontSize.Raw() << ',' << glyph.advanceX.Raw() << ','
                    << glyph.advanceY.Raw() << ',' << glyph.offsetX.Raw() << ','
                    << glyph.offsetY.Raw() << ',' << glyph.sourceBytes.begin << ','
                    << glyph.sourceBytes.end << ',' << glyph.graphemes.begin << ','
                    << glyph.graphemes.end << ','
                    << static_cast<unsigned>(glyph.bidiLevel) << ','
                    << glyph.logicalRunId << ',' << glyph.harfbuzzGlyphFlags << ','
                    << (glyph.missing ? 1 : 0) << ',' << positioned.origin.x.Raw()
                    << ',' << positioned.origin.y.Raw() << ')';
                for (const auto& caret : positioned.interiorCarets) {
                    out << "|interior(" << caret.logicalGraphemeBoundary << ','
                        << caret.position.x.Raw() << ',' << caret.position.y.Raw()
                        << ',' << (caret.fromAdjustedGdef ? 1 : 0) << ')';
                }
            }
        }
    }
    for (const auto& stop : layout.caretStops) {
        out << "|caret(" << stop.logicalGraphemeBoundary << ','
            << static_cast<unsigned>(stop.affinity) << ','
            << stop.position.x.Raw() << ',' << stop.position.y.Raw() << ','
            << stop.lineIndex << ')';
    }
    out << "|size(" << layout.intrinsicSize.width.Raw() << ','
        << layout.intrinsicSize.height.Raw() << ')' << "|clipped="
        << (layout.clipped ? 1 : 0) << "|ellipsized=" << (layout.ellipsized ? 1 : 0);
    for (const auto& fact : layout.validationFacts) {
        out << "|fact(" << static_cast<unsigned>(fact.code) << ','
            << static_cast<unsigned>(fact.severity) << ',' << fact.subsystem << ','
            << fact.message << ',' << fact.remediation << ','
            << fact.sourceBytes.begin << ',' << fact.sourceBytes.end << ','
            << fact.graphemes.begin << ',' << fact.graphemes.end << ','
            << (fact.recoverableAtRuntime ? 1 : 0) << ','
            << (fact.blocksAuthoredPackage ? 1 : 0) << ')';
    }
    return out.str();
}

struct TextCacheFixture {
    explicit TextCacheFixture(
        text::TextLayoutCacheLimits limits = text::TextLayoutCacheLimits::Production())
        : cache(limits),
          shapeResult_(std::make_shared<const text::CachedShapeResult>(
              MakeShapeResultValue())),
          layout_(std::make_shared<const text::TextLayout>(MakeFixtureLayout())),
          otherLayout_(std::make_shared<const text::TextLayout>(MakeOtherLayout())),
          layoutWithoutFacts_(
              std::make_shared<const text::TextLayout>(MakeLayoutWithoutFacts())) {}

    text::TextLayoutCache cache;

    std::shared_ptr<const text::CachedShapeResult> ShapeResult() const {
        return shapeResult_;
    }
    std::shared_ptr<const text::TextLayout> Layout() const { return layout_; }
    std::shared_ptr<const text::TextLayout> OtherLayout() const {
        return otherLayout_;
    }
    std::shared_ptr<const text::TextLayout> LayoutWithNoValidationFacts() const {
        return layoutWithoutFacts_;
    }

    text::TextLayoutRequestIndexKey RequestKeyFor(std::string utf8) const {
        return MakeRequestKey(std::move(utf8));
    }
    // 두 키가 같은 bucket에 떨어지도록 hash 필드만 강제로 같게 만든다. hash가
    // bucket 선택 이상의 일을 하고 있으면 이 두 키는 서로를 가린다.
    text::TextLayoutRequestIndexKey RequestKeyForBytesWithForcedHash(
        std::string bytes, std::uint64_t forcedHash) const {
        text::TextLayoutRequestIndexKey key = MakeRequestKey(std::move(bytes));
        key.originalBytesHash = forcedHash;
        return key;
    }
    text::TextShapeCacheKey ShapeKeyFor(
        const text::TextLayoutRequestIndexKey& request) const {
        return MakeShapeKey(request);
    }
    // 원본 문단은 그대로 두고 합성된 shape 입력만 바꾼다. 두 키가 오직
    // shapeInputUtf8 하나에서만 다르므로, 그 필드가 비교에서 빠지면 곧바로
    // 잘못된 적중이 된다.
    text::TextShapeCacheKey ShapeKeyWithForcedInputHash(
        std::string shapeInput, std::uint64_t forcedHash) const {
        text::TextShapeCacheKey key = MakeShapeKey(MakeRequestKey(u8"سلام ffi"));
        key.shapeInputUtf8      = std::move(shapeInput);
        key.shapeInputBytesHash = forcedHash;
        return key;
    }
    // 같은 자리를 원본 바이트 쪽에도 만든다. shapeInputUtf8과 originalUtf8은
    // 서로 다른 비교 항이므로, 한쪽만 시험하면 다른 쪽의 바이트 비교가 통째로
    // 빠져도 아무 단언도 움직이지 않는다.
    text::TextShapeCacheKey ShapeKeyWithForcedOriginalHash(
        std::string original, std::uint64_t forcedHash) const {
        text::TextShapeCacheKey key = MakeShapeKey(MakeRequestKey(u8"سلام ffi"));
        key.originalUtf8      = std::move(original);
        key.originalBytesHash = forcedHash;
        return key;
    }
    text::TextParagraphCacheKey FinalKeyFor(
        const text::TextLayoutRequestIndexKey& request) const {
        return MakeFinalKey(request);
    }

    std::string CanonicalLayout(const text::TextLayout& layout) const {
        return CanonicalizeLayout(layout);
    }
    std::string ExpectedFirstCanonicalLayout() const {
        return CanonicalizeLayout(MakeFixtureLayout());
    }

    void ResetAnalysisAndShapeCounters() const {
        text::detail::ResetIcuObjectCreationCount();
        text::detail::ResetHarfBuzzObjectCreationCount();
    }
    std::uint64_t IcuCallCount() const {
        return text::detail::IcuObjectCreationCount();
    }
    std::uint64_t HarfBuzzCallCount() const {
        return text::detail::HarfBuzzObjectCreationCount();
    }

private:
    std::shared_ptr<const text::CachedShapeResult> shapeResult_;
    std::shared_ptr<const text::TextLayout>        layout_;
    std::shared_ptr<const text::TextLayout>        otherLayout_;
    std::shared_ptr<const text::TextLayout>        layoutWithoutFacts_;
};

// 항목 하나가 처음으로 살아남는 예산. 0이면 천장까지 살아남지 못했다는 뜻이고,
// 호출부는 그것을 실패로 다룬다.
std::uint64_t MinimumBudgetForOneShape(
    const text::TextShapeCacheKey& key,
    const std::shared_ptr<const text::CachedShapeResult>& value) {
    for (std::uint64_t budget = 64; budget <= kBudgetSearchCeiling; budget *= 2) {
        text::TextLayoutCache probe({4, 4, budget});
        probe.StoreShape(key, value);
        if (probe.FindShape(key) != nullptr) return budget;
    }
    return 0;
}

std::uint64_t MinimumBudgetForOneParagraph(
    const text::TextLayoutRequestIndexKey& request,
    const std::shared_ptr<const text::TextLayout>& layout) {
    const auto finalKey = MakeFinalKey(request);
    for (std::uint64_t budget = 64; budget <= kBudgetSearchCeiling; budget *= 2) {
        text::TextLayoutCache probe({4, 4, budget});
        probe.Store(request, finalKey, layout);
        if (probe.FindByRequest(request)) return budget;
    }
    return 0;
}

// ── Step 1d: the one-field mutation tables ───────────────────────────────────
std::vector<MutatedShapeKey> MutateEachShapeIdentityField(
    const text::TextShapeCacheKey& base) {
    std::vector<MutatedShapeKey> out;
    auto add = [&](std::string name, void (*apply)(text::TextShapeCacheKey&)) {
        text::TextShapeCacheKey key = base;
        apply(key);
        out.push_back(MutatedShapeKey{std::move(name), std::move(key)});
    };

    add("decodePolicyVersion",
        [](text::TextShapeCacheKey& k) { k.decodePolicyVersion += 1; });
    add("originalUtf8",
        [](text::TextShapeCacheKey& k) { k.originalUtf8 += "!"; });
    add("originalBytesHash",
        [](text::TextShapeCacheKey& k) { k.originalBytesHash += 1; });
    add("shapeInputUtf8",
        [](text::TextShapeCacheKey& k) { k.shapeInputUtf8 += "!"; });
    add("shapeInputBytesHash",
        [](text::TextShapeCacheKey& k) { k.shapeInputBytesHash += 1; });
    add("shapeInputMapping.shapeInputBytes", [](text::TextShapeCacheKey& k) {
        k.shapeInputMapping[0].shapeInputBytes.end += 1;
    });
    add("shapeInputMapping.originalSourceBytes", [](text::TextShapeCacheKey& k) {
        k.shapeInputMapping[1].originalSourceBytes.begin += 1;
    });
    add("shapeInputMapping.originalGraphemes", [](text::TextShapeCacheKey& k) {
        k.shapeInputMapping[1].originalGraphemes.end += 1;
    });
    add("shapeInputMapping.synthetic", [](text::TextShapeCacheKey& k) {
        k.shapeInputMapping[1].synthetic = false;
    });
    add("shapeInputMapping.size", [](text::TextShapeCacheKey& k) {
        k.shapeInputMapping.pop_back();
    });
    add("shapeInputMapping.order", [](text::TextShapeCacheKey& k) {
        std::swap(k.shapeInputMapping[0], k.shapeInputMapping[1]);
    });
    add("paragraphBytes",
        [](text::TextShapeCacheKey& k) { k.paragraphBytes.end += 1; });
    add("runBytes", [](text::TextShapeCacheKey& k) { k.runBytes.begin += 1; });
    add("fallbackGraphGeneration",
        [](text::TextShapeCacheKey& k) { k.fallbackGraphGeneration += 1; });
    add("selectedFaces.fontGuid", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].fontGuid += "0";
    });
    add("selectedFaces.fontRevision", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].fontRevision += "0";
    });
    add("selectedFaces.sourceSha256", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].sourceSha256 += "0";
    });
    add("selectedFaces.artifactSha256", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].artifactSha256 += "0";
    });
    add("selectedFaces.artifactLocator.storage", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].artifactLocator.storage =
            FontArtifactStorage::PackagedResource;
    });
    add("selectedFaces.artifactLocator.relativePath",
        [](text::TextShapeCacheKey& k) {
            k.selectedFaces[0].artifactLocator.relativePath = "other.sfnt";
        });
    add("selectedFaces.artifactByteSize", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].artifactByteSize += 1;
    });
    add("selectedFaces.contentGeneration", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].contentGeneration += 1;
    });
    add("selectedFaces.faceIndex", [](text::TextShapeCacheKey& k) {
        k.selectedFaces[0].faceIndex += 1;
    });
    add("selectedFaces.size",
        [](text::TextShapeCacheKey& k) { k.selectedFaces.pop_back(); });
    add("selectedFaces.order", [](text::TextShapeCacheKey& k) {
        std::swap(k.selectedFaces[0], k.selectedFaces[1]);
    });
    add("fontSize", [](text::TextShapeCacheKey& k) {
        k.fontSize = Fixed26_6::FromRaw(k.fontSize.Raw() + 1);
    });
    add("variationKey", [](text::TextShapeCacheKey& k) { k.variationKey += 1; });
    add("embeddingLevel",
        [](text::TextShapeCacheKey& k) { k.embeddingLevel += 1; });
    add("direction", [](text::TextShapeCacheKey& k) { k.direction += 1; });
    add("scriptCode", [](text::TextShapeCacheKey& k) { k.scriptCode += 1; });
    add("language", [](text::TextShapeCacheKey& k) { k.language += "r"; });
    add("componentLocale",
        [](text::TextShapeCacheKey& k) { k.componentLocale = "fa-AF"; });
    add("resolvedGraphemeLocale",
        [](text::TextShapeCacheKey& k) { k.resolvedGraphemeLocale = "und"; });
    add("resolvedLineBreakLocale",
        [](text::TextShapeCacheKey& k) { k.resolvedLineBreakLocale = "und"; });
    add("graphemeRuleIdentity",
        [](text::TextShapeCacheKey& k) { k.graphemeRuleIdentity += "0"; });
    add("lineBreakRuleIdentity",
        [](text::TextShapeCacheKey& k) { k.lineBreakRuleIdentity += "0"; });
    add("analysisGeneration",
        [](text::TextShapeCacheKey& k) { k.analysisGeneration += 1; });
    add("boundaries.beginningOfText", [](text::TextShapeCacheKey& k) {
        k.boundaries.beginningOfText = false;
    });
    add("boundaries.endOfText",
        [](text::TextShapeCacheKey& k) { k.boundaries.endOfText = true; });
    add("harfbuzzBufferFlags",
        [](text::TextShapeCacheKey& k) { k.harfbuzzBufferFlags += 1; });
    add("clusterPolicy", [](text::TextShapeCacheKey& k) {
        k.clusterPolicy = static_cast<TextClusterPolicy>(99);
    });
    add("orderedFeatures.tag",
        [](text::TextShapeCacheKey& k) { k.orderedFeatures[0].tag += 1; });
    add("orderedFeatures.value",
        [](text::TextShapeCacheKey& k) { k.orderedFeatures[0].value += 1; });
    add("orderedFeatures.sourceBytes", [](text::TextShapeCacheKey& k) {
        k.orderedFeatures[0].sourceBytes.end += 1;
    });
    add("orderedFeatures.size",
        [](text::TextShapeCacheKey& k) { k.orderedFeatures.pop_back(); });
    add("orderedFeatures.order", [](text::TextShapeCacheKey& k) {
        std::swap(k.orderedFeatures[0], k.orderedFeatures[1]);
    });
    add("harfbuzzRevision",
        [](text::TextShapeCacheKey& k) { k.harfbuzzRevision = "14.3.0"; });
    add("icuRevision",
        [](text::TextShapeCacheKey& k) { k.icuRevision = "77.1"; });
    add("icuDataSha256",
        [](text::TextShapeCacheKey& k) { k.icuDataSha256 += "0"; });
    add("dependencyContractSha256",
        [](text::TextShapeCacheKey& k) { k.dependencyContractSha256 += "0"; });
    return out;
}

std::vector<MutatedRequestKey> MutateEachRequestIdentityField(
    const text::TextLayoutRequestIndexKey& base) {
    std::vector<MutatedRequestKey> out;
    auto add = [&](std::string name,
                   void (*apply)(text::TextLayoutRequestIndexKey&)) {
        text::TextLayoutRequestIndexKey key = base;
        apply(key);
        out.push_back(MutatedRequestKey{std::move(name), std::move(key)});
    };

    add("decodePolicyVersion", [](text::TextLayoutRequestIndexKey& k) {
        k.decodePolicyVersion += 1;
    });
    add("familyResolutionPolicyVersion", [](text::TextLayoutRequestIndexKey& k) {
        k.familyResolutionPolicyVersion += 1;
    });
    add("fallbackPolicyVersion", [](text::TextLayoutRequestIndexKey& k) {
        k.fallbackPolicyVersion += 1;
    });
    add("originalUtf8",
        [](text::TextLayoutRequestIndexKey& k) { k.originalUtf8 += "!"; });
    add("originalBytesHash",
        [](text::TextLayoutRequestIndexKey& k) { k.originalBytesHash += 1; });
    add("style.fontFamilyGuid", [](text::TextLayoutRequestIndexKey& k) {
        k.style.fontFamilyGuid = kFallbackGuid;
    });
    add("style.fontRequest.weight", [](text::TextLayoutRequestIndexKey& k) {
        k.style.fontRequest.weight = 700;
    });
    add("style.fontRequest.stretchPercent",
        [](text::TextLayoutRequestIndexKey& k) {
            k.style.fontRequest.stretchPercent = 100;
        });
    add("style.fontRequest.slant", [](text::TextLayoutRequestIndexKey& k) {
        k.style.fontRequest.slant = FontSlant::Oblique;
    });
    add("style.shape.fontSize", [](text::TextLayoutRequestIndexKey& k) {
        k.style.shape.fontSize = Fixed26_6::FromRaw(k.style.shape.fontSize.Raw() + 1);
    });
    add("style.shape.language",
        [](text::TextLayoutRequestIndexKey& k) { k.style.shape.language = "ar"; });
    add("style.shape.orderedFeatures.tag",
        [](text::TextLayoutRequestIndexKey& k) {
            k.style.shape.orderedFeatures[0].tag += 1;
        });
    add("style.shape.orderedFeatures.value",
        [](text::TextLayoutRequestIndexKey& k) {
            k.style.shape.orderedFeatures[0].value += 1;
        });
    add("style.shape.orderedFeatures.sourceBytes",
        [](text::TextLayoutRequestIndexKey& k) {
            k.style.shape.orderedFeatures[0].sourceBytes.begin += 1;
        });
    add("style.shape.orderedFeatures.size",
        [](text::TextLayoutRequestIndexKey& k) {
            k.style.shape.orderedFeatures.pop_back();
        });
    add("style.shape.orderedFeatures.order",
        [](text::TextLayoutRequestIndexKey& k) {
            std::swap(k.style.shape.orderedFeatures[0],
                      k.style.shape.orderedFeatures[1]);
        });
    add("style.shape.clusterPolicy", [](text::TextLayoutRequestIndexKey& k) {
        k.style.shape.clusterPolicy = static_cast<TextClusterPolicy>(99);
    });
    add("style.analysis.locale", [](text::TextLayoutRequestIndexKey& k) {
        k.style.analysis.locale = "fa-AF";
    });
    add("style.analysis.baseDirection", [](text::TextLayoutRequestIndexKey& k) {
        k.style.analysis.baseDirection = text::BaseDirection::LeftToRight;
    });
    add("style.wrap", [](text::TextLayoutRequestIndexKey& k) {
        k.style.wrap = text::TextWrapMode::Grapheme;
    });
    add("style.overflow", [](text::TextLayoutRequestIndexKey& k) {
        k.style.overflow = text::TextOverflowMode::Clip;
    });
    add("style.maxLines",
        [](text::TextLayoutRequestIndexKey& k) { k.style.maxLines += 1; });
    add("style.lineSpacing", [](text::TextLayoutRequestIndexKey& k) {
        k.style.lineSpacing = Fixed26_6::FromRaw(k.style.lineSpacing.Raw() + 1);
    });
    add("style.horizontal", [](text::TextLayoutRequestIndexKey& k) {
        k.style.horizontal = text::TextHorizontalAlignment::Right;
    });
    add("style.vertical", [](text::TextLayoutRequestIndexKey& k) {
        k.style.vertical = text::TextVerticalAlignment::Bottom;
    });
    add("style.ellipsisUtf8", [](text::TextLayoutRequestIndexKey& k) {
        k.style.ellipsisUtf8 = "...";
    });
    add("constraints.width.value", [](text::TextLayoutRequestIndexKey& k) {
        k.constraints.width = Fixed26_6::FromRaw(k.constraints.width->Raw() + 1);
    });
    add("constraints.width.engaged", [](text::TextLayoutRequestIndexKey& k) {
        k.constraints.width.reset();
    });
    add("constraints.height.value", [](text::TextLayoutRequestIndexKey& k) {
        k.constraints.height = Fixed26_6::FromRaw(k.constraints.height->Raw() + 1);
    });
    add("constraints.height.engaged", [](text::TextLayoutRequestIndexKey& k) {
        k.constraints.height.reset();
    });
    add("visualRevision",
        [](text::TextLayoutRequestIndexKey& k) { k.visualRevision += 1; });
    add("familyClosure.requestedGuid", [](text::TextLayoutRequestIndexKey& k) {
        k.familyClosure.requestedGuid = kFallbackGuid;
    });
    add("familyClosure.node.familyGuid", [](text::TextLayoutRequestIndexKey& k) {
        k.familyClosure.depthFirstFamilyNodes[1].familyGuid = kLatinGuid;
    });
    add("familyClosure.node.exists", [](text::TextLayoutRequestIndexKey& k) {
        k.familyClosure.depthFirstFamilyNodes[1].exists = true;
    });
    add("familyClosure.node.contentGeneration",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.depthFirstFamilyNodes[0].contentGeneration += 1;
        });
    add("familyClosure.node.authoredFallbackGuids",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.depthFirstFamilyNodes[0].authoredFallbackGuids[0] +=
                "0";
        });
    add("familyClosure.node.authoredFallbackGuids.order",
        [](text::TextLayoutRequestIndexKey& k) {
            auto& edges = k.familyClosure.depthFirstFamilyNodes[0]
                              .authoredFallbackGuids;
            std::swap(edges[0], edges[1]);
        });
    add("familyClosure.node.authoredFallbackGuids.size",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.depthFirstFamilyNodes[0]
                .authoredFallbackGuids.pop_back();
        });
    add("familyClosure.nodes.size", [](text::TextLayoutRequestIndexKey& k) {
        k.familyClosure.depthFirstFamilyNodes.pop_back();
    });
    add("familyClosure.nodes.order", [](text::TextLayoutRequestIndexKey& k) {
        std::swap(k.familyClosure.depthFirstFamilyNodes[0],
                  k.familyClosure.depthFirstFamilyNodes[1]);
    });
    add("familyClosure.candidate.fontGuid",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].fontGuid += "0";
        });
    add("familyClosure.candidate.fontRevision",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].fontRevision += "0";
        });
    add("familyClosure.candidate.sourceSha256",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].sourceSha256 += "0";
        });
    add("familyClosure.candidate.artifactSha256",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].artifactSha256 += "0";
        });
    add("familyClosure.candidate.artifactLocator.storage",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].artifactLocator.storage =
                FontArtifactStorage::PackagedResource;
        });
    add("familyClosure.candidate.artifactLocator.relativePath",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].artifactLocator.relativePath =
                "other.sfnt";
        });
    add("familyClosure.candidate.artifactByteSize",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].artifactByteSize += 1;
        });
    add("familyClosure.candidate.contentGeneration",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].contentGeneration += 1;
        });
    add("familyClosure.candidate.faceIndex",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].faceIndex += 1;
        });
    add("familyClosure.candidate.authoredFaceIndex",
        [](text::TextLayoutRequestIndexKey& k) {
            k.familyClosure.orderedCandidates[0].authoredFaceIndex += 1;
        });
    add("familyClosure.candidates.size", [](text::TextLayoutRequestIndexKey& k) {
        k.familyClosure.orderedCandidates.pop_back();
    });
    add("familyClosure.candidates.order", [](text::TextLayoutRequestIndexKey& k) {
        std::swap(k.familyClosure.orderedCandidates[0],
                  k.familyClosure.orderedCandidates[1]);
    });
    add("harfbuzzRevision", [](text::TextLayoutRequestIndexKey& k) {
        k.harfbuzzRevision = "14.3.0";
    });
    add("icuRevision",
        [](text::TextLayoutRequestIndexKey& k) { k.icuRevision = "77.1"; });
    add("icuDataSha256",
        [](text::TextLayoutRequestIndexKey& k) { k.icuDataSha256 += "0"; });
    add("requestedGraphemeRulePolicyIdentity",
        [](text::TextLayoutRequestIndexKey& k) {
            k.requestedGraphemeRulePolicyIdentity += "0";
        });
    add("requestedLineBreakRulePolicyIdentity",
        [](text::TextLayoutRequestIndexKey& k) {
            k.requestedLineBreakRulePolicyIdentity += "0";
        });
    add("dependencyContractSha256", [](text::TextLayoutRequestIndexKey& k) {
        k.dependencyContractSha256 += "0";
    });
    return out;
}

std::vector<MutatedFinalKey> MutateEachFinalIdentityField(
    const text::TextParagraphCacheKey& base) {
    std::vector<MutatedFinalKey> out;
    auto add = [&](std::string name, void (*apply)(text::TextParagraphCacheKey&)) {
        text::TextParagraphCacheKey key = base;
        apply(key);
        out.push_back(MutatedFinalKey{std::move(name), std::move(key)});
    };

    add("finalLineShapeKeys.content", [](text::TextParagraphCacheKey& k) {
        k.finalLineShapeKeys[0].runBytes.end += 1;
    });
    add("finalLineShapeKeys.boundaries", [](text::TextParagraphCacheKey& k) {
        k.finalLineShapeKeys[1].boundaries.endOfText = false;
    });
    add("finalLineShapeKeys.size",
        [](text::TextParagraphCacheKey& k) { k.finalLineShapeKeys.pop_back(); });
    add("finalLineShapeKeys.order", [](text::TextParagraphCacheKey& k) {
        std::swap(k.finalLineShapeKeys[0], k.finalLineShapeKeys[1]);
    });
    add("constraints.width.value", [](text::TextParagraphCacheKey& k) {
        k.constraints.width = Fixed26_6::FromRaw(k.constraints.width->Raw() + 1);
    });
    add("constraints.width.engaged",
        [](text::TextParagraphCacheKey& k) { k.constraints.width.reset(); });
    add("constraints.height.value", [](text::TextParagraphCacheKey& k) {
        k.constraints.height = Fixed26_6::FromRaw(k.constraints.height->Raw() + 1);
    });
    add("constraints.height.engaged",
        [](text::TextParagraphCacheKey& k) { k.constraints.height.reset(); });
    add("wrap", [](text::TextParagraphCacheKey& k) {
        k.wrap = text::TextWrapMode::NoWrap;
    });
    add("overflow", [](text::TextParagraphCacheKey& k) {
        k.overflow = text::TextOverflowMode::Overflow;
    });
    add("overlongTokenPolicy", [](text::TextParagraphCacheKey& k) {
        k.overlongTokenPolicy = "overflow";
    });
    add("ellipsisUtf8",
        [](text::TextParagraphCacheKey& k) { k.ellipsisUtf8 = "..."; });
    add("ellipsisStyle.fontSize", [](text::TextParagraphCacheKey& k) {
        k.ellipsisStyle.fontSize =
            Fixed26_6::FromRaw(k.ellipsisStyle.fontSize.Raw() + 1);
    });
    add("ellipsisStyle.language",
        [](text::TextParagraphCacheKey& k) { k.ellipsisStyle.language = "ar"; });
    add("ellipsisStyle.orderedFeatures", [](text::TextParagraphCacheKey& k) {
        k.ellipsisStyle.orderedFeatures.pop_back();
    });
    add("ellipsisStyle.clusterPolicy", [](text::TextParagraphCacheKey& k) {
        k.ellipsisStyle.clusterPolicy = static_cast<TextClusterPolicy>(99);
    });
    add("maxLines", [](text::TextParagraphCacheKey& k) { k.maxLines += 1; });
    add("lineSpacing", [](text::TextParagraphCacheKey& k) {
        k.lineSpacing = Fixed26_6::FromRaw(k.lineSpacing.Raw() + 1);
    });
    add("horizontal", [](text::TextParagraphCacheKey& k) {
        k.horizontal = text::TextHorizontalAlignment::Left;
    });
    add("vertical", [](text::TextParagraphCacheKey& k) {
        k.vertical = text::TextVerticalAlignment::Top;
    });
    add("visualRevision",
        [](text::TextParagraphCacheKey& k) { k.visualRevision += 1; });
    return out;
}

// Step 1f가 verbatim으로 요구하는 여섯 항목. 다섯은 불변 UnicodeAnalysis
// Identity()의 전부이고, 여섯째는 프로젝트 cluster 정책이다.
std::vector<MutatedShapeKey> MutateAnalysisIdentityAndClusterPolicy(
    const text::TextShapeCacheKey& base) {
    std::vector<MutatedShapeKey> out;
    auto add = [&](std::string name, void (*apply)(text::TextShapeCacheKey&)) {
        text::TextShapeCacheKey key = base;
        apply(key);
        out.push_back(MutatedShapeKey{std::move(name), std::move(key)});
    };
    add("resolvedGraphemeLocale",
        [](text::TextShapeCacheKey& k) { k.resolvedGraphemeLocale = "th_TH"; });
    add("resolvedLineBreakLocale",
        [](text::TextShapeCacheKey& k) { k.resolvedLineBreakLocale = "th"; });
    add("graphemeRuleIdentity",
        [](text::TextShapeCacheKey& k) { k.graphemeRuleIdentity += "f"; });
    add("lineBreakRuleIdentity",
        [](text::TextShapeCacheKey& k) { k.lineBreakRuleIdentity += "f"; });
    add("analysisGeneration",
        [](text::TextShapeCacheKey& k) { k.analysisGeneration += 1; });
    add("clusterPolicy", [](text::TextShapeCacheKey& k) {
        k.clusterPolicy = static_cast<TextClusterPolicy>(99);
    });
    return out;
}

}  // namespace

// ── Step 1 ───────────────────────────────────────────────────────────────────
TEST_CASE("every shape and paragraph identity field causes a cache miss") {
    TextCacheFixture f;
    const auto request = f.RequestKeyFor(u8"سلام ffi");
    const auto shapeKey = f.ShapeKeyFor(request);
    const auto finalKey = f.FinalKeyFor(request);
    f.cache.StoreShape(shapeKey, f.ShapeResult());
    f.cache.Store(request, finalKey, f.Layout());
    CHECK(f.cache.FindByRequest(request)->layout == f.Layout());
    CHECK(f.cache.FindShape(shapeKey) == f.ShapeResult());
    CHECK(f.cache.FindByFinal(finalKey) == f.Layout());
    for (const auto& mutated : MutateEachShapeIdentityField(shapeKey)) {
        CHECK(f.cache.FindShape(mutated) == nullptr);
    }
    for (const auto& mutated : MutateEachRequestIdentityField(request)) {
        CHECK_FALSE(f.cache.FindByRequest(mutated));
    }
    for (const auto& mutated : MutateEachFinalIdentityField(finalKey)) {
        CHECK(f.cache.FindByFinal(mutated) == nullptr);
    }
}

// Step 1d의 나머지 절반: 실패한 miss 단언이 어떤 필드였는지 말해야 한다. 위
// 블록은 계획서가 고정한 형태라 CAPTURE를 넣을 자리가 없으므로, 같은 표를 이름과
// 함께 한 번 더 돈다. 표가 비어 있으면 위 루프 전체가 조용히 vacuous가 되므로
// 크기도 여기서 못 박는다.
TEST_CASE("a missed identity field names itself and every table is populated") {
    TextCacheFixture f;
    const auto request = f.RequestKeyFor(u8"سلام ffi");
    const auto shapeKey = f.ShapeKeyFor(request);
    const auto finalKey = f.FinalKeyFor(request);
    f.cache.StoreShape(shapeKey, f.ShapeResult());
    f.cache.Store(request, finalKey, f.Layout());

    const auto shapeMutations = MutateEachShapeIdentityField(shapeKey);
    const auto requestMutations = MutateEachRequestIdentityField(request);
    const auto finalMutations = MutateEachFinalIdentityField(finalKey);
    CHECK(shapeMutations.size() == 50);
    CHECK(requestMutations.size() == 58);
    CHECK(finalMutations.size() == 21);

    for (const auto& mutated : shapeMutations) {
        CAPTURE(mutated.fieldName);
        CHECK_FALSE(mutated.key == shapeKey);
        CHECK(f.cache.FindShape(mutated.key) == nullptr);
    }
    for (const auto& mutated : requestMutations) {
        CAPTURE(mutated.fieldName);
        CHECK_FALSE(mutated.key == request);
        CHECK_FALSE(f.cache.FindByRequest(mutated.key));
    }
    for (const auto& mutated : finalMutations) {
        CAPTURE(mutated.fieldName);
        CHECK_FALSE(mutated.key == finalKey);
        CHECK(f.cache.FindByFinal(mutated.key) == nullptr);
    }

    // 50 + 58 + 21번의 miss 뒤에도 원래 키는 그대로 적중해야 한다. 이 단언이
    // 없으면 "무엇이든 놓친다"는 구현이 위 루프를 전부 통과한다.
    CHECK(f.cache.FindShape(shapeKey) == f.ShapeResult());
    CHECK(f.cache.FindByFinal(finalKey) == f.Layout());
    const auto warm = f.cache.FindByRequest(request);
    REQUIRE(warm);
    CHECK(warm->layout == f.Layout());
    CHECK(warm->finalKey == finalKey);
}

// ── Step 1a ──────────────────────────────────────────────────────────────────
TEST_CASE("hash collision never aliases different original bytes") {
    TextCacheFixture f;
    auto a = f.RequestKeyForBytesWithForcedHash("abc", 7);
    auto b = f.RequestKeyForBytesWithForcedHash("abd", 7);
    f.cache.Store(a, f.FinalKeyFor(a), f.Layout());
    CHECK_FALSE(f.cache.FindByRequest(b));
}

// 위 케이스는 CHECK_FALSE 하나뿐이라 "언제나 miss"인 구현도 통과한다. 같은
// 강제 hash 아래에서 같은 바이트는 반드시 적중해야 한다는 반대편을 못 박는다.
TEST_CASE("a forced hash still hits for identical original bytes") {
    TextCacheFixture f;
    auto a = f.RequestKeyForBytesWithForcedHash("abc", 7);
    auto same = f.RequestKeyForBytesWithForcedHash("abc", 7);
    f.cache.Store(a, f.FinalKeyFor(a), f.Layout());
    const auto warm = f.cache.FindByRequest(same);
    REQUIRE(warm);
    CHECK(warm->layout == f.Layout());
}

// ── Step 1b ──────────────────────────────────────────────────────────────────
TEST_CASE("shape-input hash collision never aliases ellipsis bytes") {
    TextCacheFixture f;
    auto a = f.ShapeKeyWithForcedInputHash(u8"سلام…", 11);
    auto b = f.ShapeKeyWithForcedInputHash(u8"سلا…", 11);
    f.cache.StoreShape(a, f.ShapeResult());
    CHECK(f.cache.FindShape(b) == nullptr);
}

TEST_CASE("a forced shape-input hash still hits for identical shape input") {
    TextCacheFixture f;
    auto a = f.ShapeKeyWithForcedInputHash(u8"سلام…", 11);
    auto same = f.ShapeKeyWithForcedInputHash(u8"سلام…", 11);
    f.cache.StoreShape(a, f.ShapeResult());
    CHECK(f.cache.FindShape(same) == f.ShapeResult());
}

// 위의 두 강제 충돌은 길이도 함께 다르다("سلام…" 11바이트, "سلا…" 9바이트).
// 그래서 비교에서 바이트 항을 통째로 지우고 길이 항만 남겨도 둘 다 통과한다 —
// 정답과 오답이 일치하는 자리다. 같은 길이, 다른 바이트를 한 bucket에 넣어야
// 비로소 바이트 비교가 유일한 판정자가 된다.
TEST_CASE("a same-length shape-input collision is decided by bytes, not length") {
    TextCacheFixture f;
    const auto a = f.ShapeKeyWithForcedInputHash("abcd", 11);
    const auto b = f.ShapeKeyWithForcedInputHash("abce", 11);
    REQUIRE(a.shapeInputUtf8.size() == b.shapeInputUtf8.size());
    REQUIRE(TextShapeCacheKeyHash{}(a) == TextShapeCacheKeyHash{}(b));
    CHECK_FALSE(a == b);
    f.cache.StoreShape(a, f.ShapeResult());
    CHECK(f.cache.FindShape(b) == nullptr);
    CHECK(f.cache.FindShape(a) == f.ShapeResult());
}

TEST_CASE("a same-length original collision is decided by bytes, not length") {
    TextCacheFixture f;
    const auto a = f.ShapeKeyWithForcedOriginalHash("abcd", 11);
    const auto b = f.ShapeKeyWithForcedOriginalHash("abce", 11);
    REQUIRE(a.originalUtf8.size() == b.originalUtf8.size());
    REQUIRE(TextShapeCacheKeyHash{}(a) == TextShapeCacheKeyHash{}(b));
    CHECK_FALSE(a == b);
    f.cache.StoreShape(a, f.ShapeResult());
    CHECK(f.cache.FindShape(b) == nullptr);
    CHECK(f.cache.FindShape(a) == f.ShapeResult());
}

// FindByFinal에도 같은 구멍이 있다. 한 필드 변이표는 hash까지 함께 바꾸므로
// 언제나 다른 bucket으로 떨어져 공짜로 miss한다 — bucket 선택 뒤의 전체 비교를
// `if (true)`로 바꿔도 아무 단언이 움직이지 않는다. 최종 키를 강제로 같은
// bucket에 넣어야 그 비교가 관찰된다.
TEST_CASE("a final-key hash collision is decided by the full key, not the bucket") {
    TextCacheFixture f;
    const auto a = f.RequestKeyForBytesWithForcedHash("abc", 7);
    const auto b = f.RequestKeyForBytesWithForcedHash("abd", 7);
    const auto finalA = f.FinalKeyFor(a);
    const auto finalB = f.FinalKeyFor(b);
    REQUIRE(TextParagraphCacheKeyHash{}(finalA) ==
            TextParagraphCacheKeyHash{}(finalB));
    CHECK_FALSE(finalA == finalB);
    f.cache.Store(a, finalA, f.Layout());
    CHECK(f.cache.FindByFinal(finalB) == nullptr);
    CHECK(f.cache.FindByFinal(finalA) == f.Layout());
}

// ── Step 1c ──────────────────────────────────────────────────────────────────
TEST_CASE("request index hits before ICU and HarfBuzz") {
    TextCacheFixture f;
    const auto requestKey = f.RequestKeyFor(u8"سلام ffi");
    const auto finalKey = f.FinalKeyFor(requestKey);
    f.cache.Store(requestKey, finalKey, f.LayoutWithNoValidationFacts());
    f.ResetAnalysisAndShapeCounters();
    REQUIRE(f.cache.FindByRequest(requestKey));
    CHECK(f.IcuCallCount() == 0);
    CHECK(f.HarfBuzzCallCount() == 0);
}

// ── Step 1e ──────────────────────────────────────────────────────────────────
TEST_CASE("bounded eviction drops only cache ownership and zero is disabled") {
    TextCacheFixture one({1, 1, 4096});
    const auto firstRequest = one.RequestKeyFor("first");
    const auto firstLayout = one.Layout();
    one.cache.Store(firstRequest, one.FinalKeyFor(firstRequest), firstLayout);
    const auto secondRequest = one.RequestKeyFor("second");
    one.cache.Store(secondRequest, one.FinalKeyFor(secondRequest), one.OtherLayout());
    CHECK_FALSE(one.cache.FindByRequest(firstRequest));
    CHECK(one.CanonicalLayout(*firstLayout) ==
          one.ExpectedFirstCanonicalLayout());

    TextCacheFixture zero({0, 0, 0});
    zero.cache.Store(firstRequest, zero.FinalKeyFor(firstRequest), firstLayout);
    CHECK_FALSE(zero.cache.FindByRequest(firstRequest));
}

// 위 케이스는 "무엇이든 즉시 축출"과 "canonical이 언제나 빈 문자열"을 둘 다
// 통과시킨다. 두 반대편을 여기서 못 박는다.
TEST_CASE("a one-entry cache keeps the newest entry and canonicalization discriminates") {
    // 여기서 재는 것은 항목 수 상한이므로 바이트 예산은 넉넉히 둔다. 예산이
    // 항목의 키 사본까지 세므로, 4096으로는 무엇이 축출을 일으켰는지 흐려진다.
    TextCacheFixture one({1, 1, 1u << 20});
    const auto firstRequest = one.RequestKeyFor("first");
    one.cache.Store(firstRequest, one.FinalKeyFor(firstRequest), one.Layout());
    const auto secondRequest = one.RequestKeyFor("second");
    const auto secondFinal = one.FinalKeyFor(secondRequest);
    one.cache.Store(secondRequest, secondFinal, one.OtherLayout());

    const auto warm = one.cache.FindByRequest(secondRequest);
    REQUIRE(warm);
    CHECK(warm->layout == one.OtherLayout());
    // 축출된 항목의 final index도 함께 사라져야 한다. 남으면 request index가
    // 없는 레이아웃을 final index가 계속 붙들고 있게 된다.
    CHECK(one.cache.FindByFinal(one.FinalKeyFor(firstRequest)) == nullptr);
    CHECK(one.cache.FindByFinal(secondFinal) == one.OtherLayout());

    CHECK(one.CanonicalLayout(*one.Layout()) !=
          one.CanonicalLayout(*one.OtherLayout()));
    CHECK(one.CanonicalLayout(*one.Layout()) ==
          one.ExpectedFirstCanonicalLayout());
}

TEST_CASE("the owned-byte bound is a real capacity in both directions") {
    const auto probe = MakeRequestKey("byte budget");
    {
        TextCacheFixture roomy({8, 8, 1u << 20});
        roomy.cache.Store(probe, roomy.FinalKeyFor(probe), roomy.Layout());
        CHECK(roomy.cache.FindByRequest(probe));
    }
    {
        TextCacheFixture tight({8, 8, 1});
        tight.cache.Store(probe, tight.FinalKeyFor(probe), tight.Layout());
        CHECK_FALSE(tight.cache.FindByRequest(probe));
    }
    {
        TextCacheFixture roomy({8, 8, 1u << 20});
        const auto shapeKey = roomy.ShapeKeyFor(probe);
        roomy.cache.StoreShape(shapeKey, roomy.ShapeResult());
        CHECK(roomy.cache.FindShape(shapeKey) == roomy.ShapeResult());
    }
    {
        TextCacheFixture tight({8, 8, 1});
        const auto shapeKey = tight.ShapeKeyFor(probe);
        tight.cache.StoreShape(shapeKey, tight.ShapeResult());
        CHECK(tight.cache.FindShape(shapeKey) == nullptr);
    }
}

TEST_CASE("zero shape capacity is disabled, not unlimited") {
    TextCacheFixture zero({0, 4, 1u << 20});
    const auto request = MakeRequestKey("zero shapes");
    const auto shapeKey = zero.ShapeKeyFor(request);
    zero.cache.StoreShape(shapeKey, zero.ShapeResult());
    CHECK(zero.cache.FindShape(shapeKey) == nullptr);
    // 문단 쪽 용량은 그대로 살아 있어야 한다: 0은 그 자원 하나만 끄는 값이다.
    zero.cache.Store(request, zero.FinalKeyFor(request), zero.Layout());
    CHECK(zero.cache.FindByRequest(request));
}

// 문단 쪽 0에는 전용 fixture가 없었다. Step 1e의 {0,0,0}에서는 maxOwnedBytes가
// 0이라 어차피 항목이 나가므로, "0을 무제한으로 읽는" 구현도 그 자리를
// 통과한다 — 정답과 오답이 일치하는 자리다.
TEST_CASE("zero paragraph capacity is disabled, not unlimited") {
    TextCacheFixture zero({4, 0, 1u << 20});
    const auto request = MakeRequestKey("zero paragraphs");
    const auto finalKey = zero.FinalKeyFor(request);
    zero.cache.Store(request, finalKey, zero.Layout());
    CHECK_FALSE(zero.cache.FindByRequest(request));
    CHECK(zero.cache.FindByFinal(finalKey) == nullptr);
    // 반대편: shape 쪽 용량은 그대로 살아 있다.
    const auto shapeKey = zero.ShapeKeyFor(request);
    zero.cache.StoreShape(shapeKey, zero.ShapeResult());
    CHECK(zero.cache.FindShape(shapeKey) == zero.ShapeResult());
}

// ── LRU 순서와 bucket 장부 ───────────────────────────────────────────────────
// 살아 있는 항목이 한 종류에 둘 이상 있고 그 사이에 적중이 끼어야만 관찰되는
// 성질들이다. 항목이 하나뿐이면 "가장 오래된 것을 버린다"와 "가장 최근 것을
// 버린다"가 같은 답을 낸다.
TEST_CASE("evicting one member of a shared bucket keeps the other findable") {
    TextCacheFixture f({2, 4, 1u << 20});
    const auto a = f.ShapeKeyWithForcedInputHash(u8"سلام…", 11);
    const auto b = f.ShapeKeyWithForcedInputHash(u8"سلا…", 11);
    REQUIRE(TextShapeCacheKeyHash{}(a) == TextShapeCacheKeyHash{}(b));
    const auto second =
        std::make_shared<const text::CachedShapeResult>(MakeShapeResultValue());
    f.cache.StoreShape(a, f.ShapeResult());
    f.cache.StoreShape(b, second);
    // a를 다시 만지면 b가 가장 오래된 항목이 된다. 세 번째 저장이 그 b만
    // 밀어내야 하고, bucket에서도 a가 아니라 b가 빠져야 한다.
    REQUIRE(f.cache.FindShape(a) == f.ShapeResult());
    const auto third = f.ShapeKeyFor(f.RequestKeyFor("third bucket"));
    const auto thirdValue =
        std::make_shared<const text::CachedShapeResult>(MakeShapeResultValue());
    f.cache.StoreShape(third, thirdValue);
    CHECK(f.cache.FindShape(b) == nullptr);
    CHECK(f.cache.FindShape(a) == f.ShapeResult());
    CHECK(f.cache.FindShape(third) == thirdValue);
}

TEST_CASE("a request hit refreshes recency so the untouched entry is evicted") {
    TextCacheFixture f({4, 2, 1u << 20});
    const auto first = f.RequestKeyFor("recency first");
    const auto second = f.RequestKeyFor("recency second");
    f.cache.Store(first, f.FinalKeyFor(first), f.Layout());
    f.cache.Store(second, f.FinalKeyFor(second), f.OtherLayout());
    REQUIRE(f.cache.FindByRequest(first));
    const auto third = f.RequestKeyFor("recency third");
    f.cache.Store(third, f.FinalKeyFor(third), f.Layout());
    CHECK(f.cache.FindByRequest(first));
    CHECK_FALSE(f.cache.FindByRequest(second));
    CHECK(f.cache.FindByRequest(third));
}

// ── 소유 바이트 장부 ─────────────────────────────────────────────────────────
// 결과는 언제나 옳으므로 이 장부가 틀려도 시험은 조용하다. 틀린 장부의 대가는
// 적중률이고, 그것을 드러내라고 Telemetry가 있다.
TEST_CASE("the owned-byte budget is repaid when an entry is evicted") {
    TextCacheFixture f({1, 4, 1u << 20});
    const auto probe = MakeRequestKey("byte repayment");
    const std::uint64_t budget =
        MinimumBudgetForOneShape(f.ShapeKeyFor(probe), f.ShapeResult());
    REQUIRE(budget != 0);

    // 항목 수 상한이 1이므로 저장할 때마다 앞의 항목이 나간다. 축출이 예산을
    // 되돌려 놓지 않으면 몇 번 만에 예산이 소진되어 방금 저장한 항목까지
    // 즉시 축출된다.
    text::TextLayoutCache cache({1, 4, budget});
    for (int i = 0; i < 20; ++i) {
        auto key = f.ShapeKeyFor(MakeRequestKey("repayment " + std::to_string(i)));
        cache.StoreShape(key, f.ShapeResult());
        CAPTURE(i);
        REQUIRE(cache.FindShape(key) == f.ShapeResult());
    }
}

TEST_CASE("re-storing the same key does not accumulate owned bytes") {
    TextCacheFixture f({4, 4, 1u << 16});
    const auto request = f.RequestKeyFor("replacement budget");
    const auto finalKey = f.FinalKeyFor(request);
    const auto shapeKey = f.ShapeKeyFor(request);
    f.cache.Store(request, finalKey, f.Layout());
    f.cache.StoreShape(shapeKey, f.ShapeResult());
    REQUIRE(f.cache.FindByRequest(request));
    REQUIRE(f.cache.FindShape(shapeKey) == f.ShapeResult());

    // 같은 두 키를 256번 다시 저장한다. 교체 경로가 옛 몫을 빼지 않으면 예산이
    // 실제 점유량과 무관하게 자라 살아 있는 항목을 축출하기 시작한다.
    for (int i = 0; i < 256; ++i) {
        f.cache.Store(request, finalKey, f.Layout());
        f.cache.StoreShape(shapeKey, f.ShapeResult());
    }
    CHECK(f.cache.Telemetry().evictions == 0);
    CHECK(f.cache.FindByRequest(request));
    CHECK(f.cache.FindShape(shapeKey) == f.ShapeResult());
}

TEST_CASE("the owned-byte budget counts the key copies the cache owns") {
    // payload는 두 경우가 완전히 같고 키의 바이트만 다르다. 예산이 payload만
    // 세고 있으면 두 최소 예산이 같아진다 — 그런데 최종 키는 줄마다 문단
    // 텍스트를 두 벌씩 담으므로, 실제로 캐시가 붙드는 바이트의 대부분이 거기에
    // 있다.
    const auto layout = std::make_shared<const text::TextLayout>(MakeFixtureLayout());
    const std::uint64_t shortBudget =
        MinimumBudgetForOneParagraph(MakeRequestKey("k"), layout);
    const std::uint64_t longBudget =
        MinimumBudgetForOneParagraph(MakeRequestKey(std::string(4096, 'k')), layout);
    REQUIRE(shortBudget != 0);
    REQUIRE(longBudget != 0);
    CHECK(shortBudget < longBudget);
}

// ── 제약의 engaged 여부 ──────────────────────────────────────────────────────
// TextLayoutTypes.h가 "제약 없음은 0이 아니다"라고 적어 두었지만, 그 주장을
// 붙드는 단언이 없었다. 한 필드 변이표의 두 항목은 값이 300*64인 폭을
// 되돌리므로 value_or(0)으로 뭉개도 여전히 서로 다르다.
TEST_CASE("an authored zero constraint is not the same request as no constraint") {
    TextCacheFixture f;
    auto unconstrained = f.RequestKeyFor("fold at zero");
    unconstrained.constraints.width.reset();
    unconstrained.constraints.height.reset();

    auto zeroWidth = unconstrained;
    zeroWidth.constraints.width = Fixed26_6::FromRaw(0);
    auto zeroHeight = unconstrained;
    zeroHeight.constraints.height = Fixed26_6::FromRaw(0);
    CHECK_FALSE(zeroWidth == unconstrained);
    CHECK_FALSE(zeroHeight == unconstrained);

    f.cache.Store(unconstrained, f.FinalKeyFor(unconstrained), f.Layout());
    CHECK_FALSE(f.cache.FindByRequest(zeroWidth));
    CHECK_FALSE(f.cache.FindByRequest(zeroHeight));
    CHECK(f.cache.FindByRequest(unconstrained));
}

// ── Step 1f ──────────────────────────────────────────────────────────────────
TEST_CASE("analysis identity and project cluster policy are cache identity") {
    TextCacheFixture f;
    const auto key = f.ShapeKeyFor(f.RequestKeyFor(u8"ภาษาไทย"));
    f.cache.StoreShape(key, f.ShapeResult());
    for (const auto& changed : MutateAnalysisIdentityAndClusterPolicy(key)) {
        CAPTURE(changed.fieldName);
        CHECK_FALSE(changed.key == key);
        CHECK(TextShapeCacheKeyHash{}(changed.key) !=
              TextShapeCacheKeyHash{}(key));
        CHECK(f.cache.FindShape(changed.key) == nullptr);
    }
}

// TextLayoutCache.cpp의 해시 주석은 "접기가 교환법칙을 따르지 않으므로 같은
// 타입의 두 필드를 맞바꾸면 값이 달라진다"고 주장한다. 위 시험들은 값을 다른
// 값으로 바꿀 뿐 두 필드를 맞바꾸지 않으므로, 그 주장을 붙드는 단언이 없었다.
TEST_CASE("swapping two same-typed identity fields changes the shape hash") {
    const auto key = MakeShapeKey(MakeRequestKey(u8"سلام ffi"));
    text::TextShapeCacheKey swapped = key;
    std::swap(swapped.resolvedGraphemeLocale, swapped.resolvedLineBreakLocale);
    REQUIRE(swapped.resolvedGraphemeLocale != swapped.resolvedLineBreakLocale);
    CHECK_FALSE(swapped == key);
    CHECK(TextShapeCacheKeyHash{}(swapped) != TextShapeCacheKeyHash{}(key));
}

TEST_CASE("the analysis identity table has exactly six one-field entries") {
    TextCacheFixture f;
    const auto key = f.ShapeKeyFor(f.RequestKeyFor(u8"ภาษาไทย"));
    const auto changed = MutateAnalysisIdentityAndClusterPolicy(key);
    REQUIRE(changed.size() == 6);
    f.cache.StoreShape(key, f.ShapeResult());
    CHECK(f.cache.FindShape(key) == f.ShapeResult());
    CHECK(TextShapeCacheKeyHash{}(MakeShapeKey(f.RequestKeyFor(u8"ภาษาไทย"))) ==
          TextShapeCacheKeyHash{}(key));
}

// ── Store semantics ──────────────────────────────────────────────────────────
TEST_CASE("an equal key replaces cache ownership rather than appending") {
    TextCacheFixture f;
    const auto request = f.RequestKeyFor("replace me");
    const auto shapeKey = f.ShapeKeyFor(request);
    f.cache.StoreShape(shapeKey, f.ShapeResult());
    const auto replacement =
        std::make_shared<const text::CachedShapeResult>(MakeShapeResultValue());
    f.cache.StoreShape(shapeKey, replacement);
    CHECK(f.cache.FindShape(shapeKey) == replacement);

    f.cache.Store(request, f.FinalKeyFor(request), f.Layout());
    f.cache.Store(request, f.FinalKeyFor(request), f.OtherLayout());
    const auto warm = f.cache.FindByRequest(request);
    REQUIRE(warm);
    CHECK(warm->layout == f.OtherLayout());
    CHECK(f.cache.Telemetry().shapeStores == 2);
    CHECK(f.cache.Telemetry().finalStores == 2);
}

// 위 케이스는 두 번 다 같은 최종 키로 저장하므로 재색인 분기를 한 번도 밟지
// 않는다. 실제 운영에서는 오히려 그쪽이 정상이다: analysisGeneration은
// 프로세스 안에서 재사용되지 않으므로, 같은 요청을 다시 배치하면 최종 줄 키가
// 반드시 달라진다.
TEST_CASE("re-storing a request under a new final key re-indexes the final key") {
    TextCacheFixture f;
    const auto request = f.RequestKeyFor("re-bucket me");
    const auto firstFinal = f.FinalKeyFor(request);
    text::TextParagraphCacheKey secondFinal = firstFinal;
    secondFinal.maxLines += 1;
    REQUIRE_FALSE(firstFinal == secondFinal);

    f.cache.Store(request, firstFinal, f.Layout());
    f.cache.Store(request, secondFinal, f.OtherLayout());
    CHECK(f.cache.FindByFinal(secondFinal) == f.OtherLayout());
    CHECK(f.cache.FindByFinal(firstFinal) == nullptr);
    const auto warm = f.cache.FindByRequest(request);
    REQUIRE(warm);
    CHECK(warm->layout == f.OtherLayout());
    CHECK(warm->finalKey == secondFinal);
}

// 빈 지분을 저장하면 FindByRequest가 engaged optional 안에 nullptr layout을
// 담아 돌려주고, FindShape는 nullptr을 돌려주면서 telemetry에는 적중으로
// 센다 — 호출자가 miss와 구분할 수 없는 상태다.
TEST_CASE("a null payload is refused rather than published as a half entry") {
    TextCacheFixture f;
    const auto request = f.RequestKeyFor("null payload");
    const auto finalKey = f.FinalKeyFor(request);
    const auto shapeKey = f.ShapeKeyFor(request);

    f.cache.StoreShape(shapeKey, nullptr);
    CHECK(f.cache.FindShape(shapeKey) == nullptr);
    CHECK(f.cache.Telemetry().shapeHits == 0);
    CHECK(f.cache.Telemetry().shapeStores == 0);

    f.cache.Store(request, finalKey, nullptr);
    CHECK_FALSE(f.cache.FindByRequest(request));
    CHECK(f.cache.FindByFinal(finalKey) == nullptr);
    CHECK(f.cache.Telemetry().requestHits == 0);
    CHECK(f.cache.Telemetry().finalStores == 0);

    // 반대편: 진짜 지분은 그대로 저장되고 적중한다.
    f.cache.StoreShape(shapeKey, f.ShapeResult());
    f.cache.Store(request, finalKey, f.Layout());
    CHECK(f.cache.FindShape(shapeKey) == f.ShapeResult());
    const auto warm = f.cache.FindByRequest(request);
    REQUIRE(warm);
    CHECK(warm->layout == f.Layout());
}

TEST_CASE("a forced hash collision appends a distinct shape entry") {
    TextCacheFixture f;
    auto a = f.ShapeKeyWithForcedInputHash(u8"سلام…", 11);
    auto b = f.ShapeKeyWithForcedInputHash(u8"سلا…", 11);
    const auto other =
        std::make_shared<const text::CachedShapeResult>(MakeShapeResultValue());
    f.cache.StoreShape(a, f.ShapeResult());
    f.cache.StoreShape(b, other);
    CHECK(f.cache.FindShape(a) == f.ShapeResult());
    CHECK(f.cache.FindShape(b) == other);
}

// ── Step 12: telemetry ───────────────────────────────────────────────────────
TEST_CASE("telemetry counts hits, misses, stores and evictions") {
    TextCacheFixture f({1, 1, 1u << 20});
    CHECK(f.cache.Telemetry().shapeHits == 0);
    CHECK(f.cache.Telemetry().evictions == 0);

    const auto request = f.RequestKeyFor("telemetry");
    const auto shapeKey = f.ShapeKeyFor(request);
    CHECK(f.cache.FindShape(shapeKey) == nullptr);
    CHECK(f.cache.Telemetry().shapeMisses == 1);
    CHECK(f.cache.Telemetry().shapeHits == 0);

    f.cache.StoreShape(shapeKey, f.ShapeResult());
    CHECK(f.cache.Telemetry().shapeStores == 1);
    CHECK(f.cache.FindShape(shapeKey) == f.ShapeResult());
    CHECK(f.cache.Telemetry().shapeHits == 1);
    CHECK(f.cache.Telemetry().shapeMisses == 1);

    const auto finalKey = f.FinalKeyFor(request);
    CHECK_FALSE(f.cache.FindByRequest(request));
    CHECK(f.cache.Telemetry().requestMisses == 1);
    CHECK(f.cache.FindByFinal(finalKey) == nullptr);
    CHECK(f.cache.Telemetry().finalMisses == 1);

    f.cache.Store(request, finalKey, f.Layout());
    CHECK(f.cache.Telemetry().finalStores == 1);
    CHECK(f.cache.FindByRequest(request));
    CHECK(f.cache.Telemetry().requestHits == 1);
    CHECK(f.cache.FindByFinal(finalKey) == f.Layout());
    CHECK(f.cache.Telemetry().finalHits == 1);

    const auto second = f.RequestKeyFor("telemetry two");
    f.cache.Store(second, f.FinalKeyFor(second), f.OtherLayout());
    CHECK(f.cache.Telemetry().evictions == 1);
}

TEST_CASE("production limits are the exact design budget") {
    const auto limits = text::TextLayoutCacheLimits::Production();
    CHECK(limits.maxShapeEntries == 4096);
    CHECK(limits.maxParagraphEntries == 1024);
    CHECK(limits.maxOwnedBytes == 64ULL * 1024ULL * 1024ULL);
}

// ── Step 4: cacheable validation facts ───────────────────────────────────────
TEST_CASE("an immutable fact is re-contextualized into a diagnostic per call") {
    const text::TextValidationFact fact = MakeFact();

    text::TextDiagnosticContext label;
    label.assetGuid     = "aaaa1111";
    label.sceneObjectId = 12;
    label.componentType = "UILabel";
    const text::TextDiagnostic first = text::MakeContextualDiagnostic(fact, label);
    CHECK(first.code == fact.code);
    CHECK(first.severity == fact.severity);
    CHECK(first.subsystem == fact.subsystem);
    CHECK(first.message == fact.message);
    CHECK(first.remediation == fact.remediation);
    CHECK(first.sourceByteRange == fact.sourceBytes);
    CHECK(first.assetGuid == "aaaa1111");
    CHECK(first.sceneObjectId == 12);
    CHECK(first.componentType == "UILabel");

    text::TextDiagnosticContext world;
    world.assetGuid     = "bbbb2222";
    world.sceneObjectId = 99;
    world.componentType = "TextRenderer2D";
    const text::TextDiagnostic second = text::MakeContextualDiagnostic(fact, world);
    CHECK(second.assetGuid == "bbbb2222");
    CHECK(second.sceneObjectId == 99);
    CHECK(second.componentType == "TextRenderer2D");
    // 같은 불변 사실이므로 문맥 밖의 모든 것은 두 호출에서 같아야 한다.
    CHECK(second.message == first.message);
    CHECK(second.sourceByteRange == first.sourceByteRange);
}

// 캐시된 레이아웃은 문맥을 담지 않는다. 같은 warm 결과가 서로 다른 컴포넌트에서
// 서로 다른 진단으로 다시 나오는지가 Step 4의 전부다.
TEST_CASE("a warm layout re-emits its facts under the current diagnostic context") {
    TextCacheFixture f;
    const auto request = f.RequestKeyFor(u8"سلام ffi");
    f.cache.Store(request, f.FinalKeyFor(request), f.Layout());
    const auto warm = f.cache.FindByRequest(request);
    REQUIRE(warm);
    REQUIRE(warm->layout);
    REQUIRE(warm->layout->validationFacts.size() == 1);

    text::TextDiagnosticContext context;
    context.assetGuid     = "cccc3333";
    context.sceneObjectId = 7;
    context.componentType = "UILabel";
    const text::TextDiagnostic diagnostic =
        text::MakeContextualDiagnostic(warm->layout->validationFacts[0], context);
    CHECK(diagnostic.code == text::TextDiagnosticCode::MissingGlyph);
    CHECK(diagnostic.assetGuid == "cccc3333");
    CHECK(diagnostic.sceneObjectId == 7);
}

// ── Step 9: requested rule-policy identities ─────────────────────────────────
TEST_CASE("requested rule policy identities separate the two policy kinds") {
    const std::string icuData = text::kPackagedIcuDataSha256;
    const std::string grapheme =
        text::RequestedGraphemeRulePolicyIdentity(kIcuRevision, icuData);
    const std::string lineBreak =
        text::RequestedLineBreakRulePolicyIdentity(kIcuRevision, icuData);

    CHECK(grapheme.size() == 64);
    CHECK(lineBreak.size() == 64);
    // 정책 종류가 tuple에서 빠지면 두 값이 같아지고, locale이 바뀌지 않은
    // 편집에서 grapheme 캐시와 line-break 캐시가 서로를 가린다.
    CHECK(grapheme != lineBreak);
    CHECK(grapheme ==
          text::RequestedGraphemeRulePolicyIdentity(kIcuRevision, icuData));
    CHECK(grapheme != text::RequestedGraphemeRulePolicyIdentity("77.1", icuData));
    CHECK(grapheme !=
          text::RequestedGraphemeRulePolicyIdentity(kIcuRevision, icuData + "0"));
    CHECK(lineBreak != text::RequestedLineBreakRulePolicyIdentity("77.1", icuData));
    CHECK(lineBreak !=
          text::RequestedLineBreakRulePolicyIdentity(kIcuRevision, icuData + "0"));
    // 네 조각 사이의 구분자가 빠지면 payload가 하나의 붙은 문자열이 되어
    // ("7","81")과 ("78","1")이 같은 정체성을 낸다.
    CHECK(text::RequestedGraphemeRulePolicyIdentity("7", "81") !=
          text::RequestedGraphemeRulePolicyIdentity("78", "1"));
}

// 두 hash 필드의 유일한 생산자. 조회 결과가 이 값에 의존하지 않으므로, 이것이
// 깨져도 시험은 조용하고 캐시만 느려지거나 영원히 빗나간다.
TEST_CASE("the cache byte hash is deterministic and separates near-identical bytes") {
    CHECK(text::CacheBytesHash("abc") == text::CacheBytesHash("abc"));
    CHECK(text::CacheBytesHash("abc") != text::CacheBytesHash("abd"));
    CHECK(text::CacheBytesHash("abc") != text::CacheBytesHash("ab"));
    CHECK(text::CacheBytesHash(std::string_view()) !=
          text::CacheBytesHash(std::string_view("\0", 1)));
    CHECK(text::CacheBytesHash(std::string_view("a\0b", 3)) !=
          text::CacheBytesHash(std::string_view("a\0c", 3)));
}

TEST_CASE("the request key carries the portable dependency contract SHA") {
    const auto request = MakeRequestKey("contract");
    REQUIRE_FALSE(request.dependencyContractSha256.empty());
    CHECK(request.dependencyContractSha256 == DependencyContractSha());
}
