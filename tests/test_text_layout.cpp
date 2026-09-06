#include "Assets/FontArtifactStore.h"
#include "Assets/FontAsset.h"
#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "FontCollectionTestSupport.h"
#include "Rendering/FontFace.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextRuntimeDependencies.h"
#include "Text/TextShapingService.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"
#include "TextQualificationAssetTree.h"
#include "doctest.h"

#include <hb.h>
#include <unicode/uvernum.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace text = molga::text;
using molga::Fixed26_6;
using text::AnalysisItem;
using text::GraphemeRange;
using text::ShapedGlyph;
using text::ShapedRun;
using text::SourceByteRange;
using text::TextLayoutRequest;
using text::TextOverflowMode;
using text::TextValidationFact;
using text::TextWrapMode;
using text::UnicodeAnalysis;
using text::UnicodeTextBuffer;

// ── Fixture-tree GUIDs ──────────────────────────────────────────────────────
// Task 4.2가 커밋한 자격 트리의 고정 GUID. 이름이 아니라 이 상수들이 계약이다.
constexpr const char* kPrimaryFamily = "11111111111111111111111111111111";

// 자격 트리에 없는 family. resolver는 이 GUID에 대해 exists=false 노드 하나와
// 후보 0개를 돌려주므로, "face가 하나도 없는 문단"의 유일한 실제 경로다.
constexpr const char* kMissingFamily = "missing-family";

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), path.string());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// doctest는 DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING 없이 빌드되므로 const
// char*를 포인터 주소로 찍는다. 실패 메시지가 읽히려면 string으로 감싸야 한다.
std::string Label(const char* value) { return std::string(value); }

// ── The committed qualification corpus, imported once per process ───────────
// test_text_shaping과 같은 이유로 프로세스에 하나만 만든다: 여섯 폰트(합쳐
// 5.7MB)를 케이스마다 다시 해시하고 다시 발행할 이유가 없다. 실패는 REQUIRE가
// 아니라 문자열로 남긴다 — 정적 초기화 중의 REQUIRE는 어떤 케이스에도 속하지
// 않는다.
class LayoutCorpus {
public:
    LayoutCorpus()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          repository_(database_),
          resolver_(database_, repository_) {
        std::string bindError;
        if (!database_.BindFontArtifactStore(store_, &bindError)) {
            error_ = "BindFontArtifactStore: " + bindError;
            return;
        }
        database_.ScanProject(tree_.AssetsRoot());
        if (database_.Find(std::string(kPrimaryFamily)) == nullptr) {
            error_ = "primary family record is missing after ScanProject";
        }
    }

    const std::string& Error() const noexcept { return error_; }
    text::FontFamilyResolver& Resolver() { return resolver_; }
    const molga::AssetDatabase& Database() const noexcept { return database_; }

private:
    QualificationAssetTreeFixture tree_;
    molga::AssetDatabase database_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
    text::FontRepository repository_;
    text::FontFamilyResolver resolver_;
    std::string error_;
};

LayoutCorpus& Corpus() {
    static LayoutCorpus corpus;
    return corpus;
}

// ── The layout observation window ───────────────────────────────────────────
// 계획서의 verbatim 블록이 `f.shaper.X()`로 부르는 것들이다. 이름과 달리
// TextShapingService가 아니라 레이아웃 서비스가 남긴 관찰을 읽는다: 문단
// 측정과 최종 줄 재셰이핑을 구분할 수 있는 것은 그 둘을 나눠 부르는 쪽,
// 곧 레이아웃 서비스뿐이기 때문이다.
struct LayoutShapeObserver {
    static const text::detail::LayoutObservations& Log() {
        return text::detail::CurrentLayoutObservations();
    }
    std::size_t ParagraphShapeCount() const { return Log().paragraphShapePasses; }
    std::size_t FinalLineShapeCount() const {
        return Log().acceptedLineShapes.size();
    }
    std::size_t FinalLineItemShapeCount() const {
        return Log().finalLineItemShapeCalls;
    }
    std::size_t SeparateEllipsisShapeCount() const {
        std::size_t count = 0;
        for (const auto& candidate : Log().ellipsisCandidates) {
            if (!text::detail::EllipsisCandidateWasShapedWhole(candidate)) ++count;
        }
        return count;
    }
    std::size_t FullRetainedPlusEllipsisShapeCount() const {
        std::size_t count = 0;
        for (const auto& candidate : Log().ellipsisCandidates) {
            if (text::detail::EllipsisCandidateWasShapedWhole(candidate)) ++count;
        }
        return count;
    }
    // 관찰이 하나도 없으면 false다. "모든 후보가 진짜 BOT/EOT를 썼다"가
    // "후보가 하나도 없다"로 조용히 참이 되면, 줄임 경로를 통째로 지운 회귀가
    // 이 단언을 통과한다.
    bool EveryEllipsisCandidateUsedFinalBotEot() const {
        const auto& candidates = Log().ellipsisCandidates;
        if (candidates.empty()) return false;
        for (const auto& candidate : candidates) {
            if (candidate.beginningOfText != candidate.lineBeginningOfText ||
                candidate.endOfText != candidate.lineEndOfText) {
                return false;
            }
        }
        return true;
    }
};

// ── Fixture specifications ──────────────────────────────────────────────────
// 이름 하나가 텍스트/로케일/줄바꿈 정책/글자 크기/폭을 전부 고정한다. 케이스가
// 자기 자리에서 문자열을 적으면 두 케이스가 같은 이름으로 다른 것을 재게 된다.
struct LayoutFixtureSpec {
    const char* name;
    const char* utf8;
    const char* locale;
    TextWrapMode wrap;
    TextOverflowMode overflow;
    std::int32_t fontSizeRaw;
    std::int32_t widthRaw;  // 0 == 제약 없음
    std::uint32_t maxLines;
};

const LayoutFixtureSpec& FixtureSpec(const std::string& name) {
    static const std::vector<LayoutFixtureSpec> kSpecs{
        // 두 방향과 세 embedding level(0/1/2)이 한 문단에 들어 있고, 'fi'
        // 합자가 grapheme 경계 하나를 glyph 내부로 삼켜 그 경계를 후보에서
        // 지운다. 단일 줄도 아니고 LTR 전용도 아니다.
        {"final-line-fi", u8"fi שם 12", "und", TextWrapMode::Grapheme,
         TextOverflowMode::Overflow, 6 * 64, 0, 0},
        // 공백이 없어 UAX#14 기회가 문단 양 끝에만 있는 하나의 토큰.
        {"overlong-word", u8"Wwwwwwwwwwww", "en", TextWrapMode::Word,
         TextOverflowMode::Overflow, 8 * 64, 9 * 64, 0},
        {"empty-lines", u8"", "und", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        {"no-face-metrics", u8"", "und", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        // 줄임표가 붙을 자리에서 아랍어 문맥이 실제로 바뀌는 문단.
        {"arabic-ellipsis-context", u8"سلام "
                                    u8"عليكم",
         "ar", TextWrapMode::Word, TextOverflowMode::Ellipsis, 8 * 64, 22 * 64,
         1},
        {"recoverable-cache", u8"A", "und", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        // Step 1h가 고정한 아랍어. 이어쓰기 때문에 grapheme 경계 대부분이
        // HarfBuzz의 unsafe-to-break가 된다.
        {"unsafe-arabic-boundary", u8"سلامك", "ar",
         TextWrapMode::Grapheme, TextOverflowMode::Overflow, 8 * 64, 0, 0},
        // 데바나가리. 아랍어/히브리어의 재배열은 run을 통째로 뒤집는 것이라
        // cluster 안쪽 순서는 건드리지 않지만, 여기서는 뒤에 오는 pre-base
        // 매트라가 자기 앞 자음보다 먼저 그려진다 — glyph의 시각 자리와 원본
        // 문자 순서가 LTR run 안에서 어긋나는 유일한 종류다.
        {"devanagari-reorder", u8"हिन्दी भाषा", "hi",
         TextWrapMode::Grapheme, TextOverflowMode::Overflow, 16 * 64, 2400, 0},
    };
    for (const LayoutFixtureSpec& spec : kSpecs) {
        if (name == spec.name) return spec;
    }
    REQUIRE_MESSAGE(false, ("no layout fixture named " + name));
    return kSpecs.front();
}

// ── Canonical serialization ─────────────────────────────────────────────────
// 모든 필드를 안정된 순서로 담는다. code/message만 비교하는 직렬화는 severity나
// 범위가 바뀌는 회귀를 통과시킨다.
nlohmann::ordered_json CanonicalGlyphJson(const text::PositionedGlyph& glyph) {
    nlohmann::ordered_json record = nlohmann::ordered_json::object();
    record["fontGuid"] = glyph.glyph.fontGuid;
    record["fontRevision"] = glyph.glyph.fontRevision;
    record["faceIndex"] = glyph.glyph.faceIndex;
    record["glyphId"] = glyph.glyph.glyphId;
    record["missing"] = glyph.glyph.missing;
    record["hasFaceResource"] = glyph.glyph.faceResource != nullptr;
    record["fontSize"] = glyph.glyph.fontSize.Raw();
    record["advanceX"] = glyph.glyph.advanceX.Raw();
    record["advanceY"] = glyph.glyph.advanceY.Raw();
    record["offsetX"] = glyph.glyph.offsetX.Raw();
    record["offsetY"] = glyph.glyph.offsetY.Raw();
    record["sourceBegin"] = glyph.glyph.sourceBytes.begin;
    record["sourceEnd"] = glyph.glyph.sourceBytes.end;
    record["graphemeBegin"] = glyph.glyph.graphemes.begin;
    record["graphemeEnd"] = glyph.glyph.graphemes.end;
    record["bidiLevel"] = glyph.glyph.bidiLevel;
    record["logicalRunId"] = glyph.glyph.logicalRunId;
    record["harfbuzzGlyphFlags"] = glyph.glyph.harfbuzzGlyphFlags;
    record["originX"] = glyph.origin.x.Raw();
    record["originY"] = glyph.origin.y.Raw();
    nlohmann::ordered_json carets = nlohmann::ordered_json::array();
    for (const text::GlyphInteriorCaret& caret : glyph.interiorCarets) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["boundary"] = caret.logicalGraphemeBoundary;
        entry["x"] = caret.position.x.Raw();
        entry["y"] = caret.position.y.Raw();
        entry["fromAdjustedGdef"] = caret.fromAdjustedGdef;
        carets.push_back(std::move(entry));
    }
    record["interiorCarets"] = std::move(carets);
    return record;
}

nlohmann::ordered_json CanonicalLayoutJson(const text::TextLayout& layout) {
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["clipped"] = layout.clipped;
    root["ellipsized"] = layout.ellipsized;
    root["intrinsicWidth"] = layout.intrinsicSize.width.Raw();
    root["intrinsicHeight"] = layout.intrinsicSize.height.Raw();
    nlohmann::ordered_json lines = nlohmann::ordered_json::array();
    for (const text::TextLine& line : layout.lines) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        // Step 15: 정확한 범위와 수직 값이 glyph/run 배열보다 앞에 온다.
        entry["sourceBegin"] = line.sourceBytes.begin;
        entry["sourceEnd"] = line.sourceBytes.end;
        entry["graphemeBegin"] = line.graphemes.begin;
        entry["graphemeEnd"] = line.graphemes.end;
        entry["baseline"] = line.baseline.Raw();
        entry["advance"] = line.advance.Raw();
        entry["ascent"] = line.ascent.Raw();
        entry["descent"] = line.descent.Raw();
        entry["lineGap"] = line.lineGap.Raw();
        entry["top"] = line.top.Raw();
        entry["bottom"] = line.bottom.Raw();
        nlohmann::ordered_json runs = nlohmann::ordered_json::array();
        for (const text::VisualRun& run : line.visualRuns) {
            nlohmann::ordered_json runJson = nlohmann::ordered_json::object();
            runJson["logicalRunId"] = run.logicalRunId;
            runJson["bidiLevel"] = run.bidiLevel;
            nlohmann::ordered_json glyphs = nlohmann::ordered_json::array();
            for (const text::PositionedGlyph& glyph : run.glyphs) {
                glyphs.push_back(CanonicalGlyphJson(glyph));
            }
            runJson["glyphs"] = std::move(glyphs);
            runs.push_back(std::move(runJson));
        }
        entry["runs"] = std::move(runs);
        lines.push_back(std::move(entry));
    }
    root["lines"] = std::move(lines);
    return root;
}

std::string CanonicalValidationFacts(const std::vector<TextValidationFact>& facts) {
    std::ostringstream out;
    for (const TextValidationFact& fact : facts) {
        out << text::StableTextDiagnosticCode(fact.code) << '\x1F'
            << static_cast<int>(fact.severity) << '\x1F' << fact.subsystem
            << '\x1F' << fact.message << '\x1F' << fact.remediation << '\x1F'
            << fact.sourceBytes.begin << '\x1F' << fact.sourceBytes.end << '\x1F'
            << fact.graphemes.begin << '\x1F' << fact.graphemes.end << '\x1F'
            << (fact.recoverableAtRuntime ? 1 : 0) << '\x1F'
            << (fact.blocksAuthoredPackage ? 1 : 0) << '\x1E';
    }
    return out.str();
}

std::string CanonicalDiagnosticRecords(
    const std::vector<text::TextDiagnostic>& diagnostics) {
    std::ostringstream out;
    for (const text::TextDiagnostic& diagnostic : diagnostics) {
        out << text::StableTextDiagnosticCode(diagnostic.code) << '\x1F'
            << static_cast<int>(diagnostic.severity) << '\x1F'
            << diagnostic.subsystem << '\x1F' << diagnostic.message << '\x1F'
            << diagnostic.remediation << '\x1F' << diagnostic.assetGuid << '\x1F'
            << diagnostic.sceneObjectId << '\x1F' << diagnostic.componentType
            << '\x1F' << diagnostic.sourceByteRange.begin << '\x1F'
            << diagnostic.sourceByteRange.end << '\x1E';
    }
    return out.str();
}

bool HasBlockingAuthoredPackageFact(const std::vector<TextValidationFact>& facts) {
    for (const TextValidationFact& fact : facts) {
        if (fact.blocksAuthoredPackage) return true;
    }
    return false;
}

// glyph가 하나도 없으면 곧바로 실패한다. 첫 glyph를 묻는 케이스가 "줄이 비어
// 있다"로 조용히 통과하면 배치를 통째로 잃은 회귀가 살아남는다.
const ShapedGlyph& FirstGlyph(const text::TextLayout& layout) {
    for (const text::TextLine& line : layout.lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            if (!run.glyphs.empty()) return run.glyphs.front().glyph;
        }
    }
    REQUIRE_MESSAGE(false, "the layout has no positioned glyph");
    static const ShapedGlyph kNever{};
    return kNever;
}

bool AnyLineEndsAtGrapheme(const text::TextLayout& layout,
                           std::uint32_t graphemeBoundary) {
    for (const text::TextLine& line : layout.lines) {
        if (line.graphemes.end == graphemeBoundary) return true;
    }
    return false;
}

// ── The layout fixture ──────────────────────────────────────────────────────
class LayoutFixture {
public:
    text::VectorTextDiagnosticSink sink;
    LayoutShapeObserver shaper;
    text::TextShapingService shapingService;
    text::TextLayoutCache cache{text::TextLayoutCacheLimits::Production()};
    text::TextLayoutService service;

    explicit LayoutFixture(const LayoutFixtureSpec& spec)
        : service(Corpus().Resolver(), shapingService, cache), spec_(spec) {
        REQUIRE_MESSAGE(Corpus().Error().empty(), Corpus().Error());
    }

    const LayoutFixtureSpec& Spec() const noexcept { return spec_; }

    TextLayoutRequest MakeRequest(std::string utf8) const {
        TextLayoutRequest request;
        request.utf8 = std::move(utf8);
        request.style.fontFamilyGuid = kPrimaryFamily;
        request.style.fontRequest = {400, 100, molga::FontSlant::Upright};
        request.style.shape.fontSize = Fixed26_6::FromRaw(spec_.fontSizeRaw);
        request.style.shape.language = spec_.locale;
        request.style.analysis.locale = spec_.locale;
        request.style.analysis.baseDirection = text::BaseDirection::Auto;
        request.style.wrap = spec_.wrap;
        request.style.overflow = spec_.overflow;
        request.style.maxLines = spec_.maxLines;
        if (spec_.widthRaw != 0) {
            request.constraints.width = Fixed26_6::FromRaw(spec_.widthRaw);
        }
        request.diagnosticContext.assetGuid = "layout-fixture-asset";
        request.diagnosticContext.sceneObjectId = 7;
        request.diagnosticContext.componentType = "UILabel";
        return request;
    }

    TextLayoutRequest RequestForBytes(std::string bytes) const {
        return MakeRequest(std::move(bytes));
    }

    TextLayoutRequest RequestForMissingFamily(std::string utf8) const {
        TextLayoutRequest request = MakeRequest(std::move(utf8));
        request.style.fontFamilyGuid = kMissingFamily;
        return request;
    }

    std::optional<std::shared_ptr<const text::TextLayout>> LayoutAtWidth(
        Fixed26_6 width) {
        TextLayoutRequest request = MakeRequest(spec_.utf8);
        request.constraints.width = width;
        return Remember(service.Layout(request, sink));
    }

    std::shared_ptr<const text::TextLayout> Layout(TextWrapMode wrap,
                                                   TextOverflowMode overflow) {
        TextLayoutRequest request = MakeRequest(spec_.utf8);
        request.style.wrap = wrap;
        request.style.overflow = overflow;
        return Required(service.Layout(request, sink));
    }

    std::shared_ptr<const text::TextLayout> LayoutText(std::string utf8) {
        return Required(service.Layout(MakeRequest(std::move(utf8)), sink));
    }

    std::shared_ptr<const text::TextLayout> LayoutTextWithFamily(
        std::string utf8, std::string familyGuid, Fixed26_6 em) {
        TextLayoutRequest request = MakeRequest(std::move(utf8));
        request.style.fontFamilyGuid = std::move(familyGuid);
        request.style.shape.fontSize = em;
        return Required(service.Layout(request, sink));
    }

    std::optional<std::shared_ptr<const text::TextLayout>> LayoutEllipsized() {
        return Remember(service.Layout(MakeRequest(spec_.utf8), sink));
    }

    // ── Observations over the last completed Layout call ────────────────────
    std::size_t EllipsisRemovalAttemptCount() const {
        return text::detail::CurrentLayoutObservations().ellipsisRemovalAttempts;
    }

    std::size_t BacktrackStepCount() const {
        return text::detail::CurrentLayoutObservations().backtrackSteps;
    }

    bool WasBreakCandidateDiscarded(std::uint32_t graphemeBoundary) const {
        const auto& discarded =
            text::detail::CurrentLayoutObservations().discardedUnsafeBoundaries;
        return std::find(discarded.begin(), discarded.end(), graphemeBoundary) !=
               discarded.end();
    }

    bool EveryAcceptedLineWasFinalReshaped() const {
        const auto& accepted =
            text::detail::CurrentLayoutObservations().acceptedLineShapes;
        if (lastLayout_ == nullptr) return false;
        if (accepted.size() != lastLayout_->lines.size()) return false;
        for (std::size_t index = 0; index < accepted.size(); ++index) {
            if (!(accepted[index] == lastLayout_->lines[index].sourceBytes)) {
                return false;
            }
        }
        return true;
    }

    // ── The paragraph shaping the fixture performs for itself ───────────────
    // 프로덕션 후보 집합을 바꾸지 않고 관찰만 한다: 자기 버퍼/분석/셰이퍼로
    // 문단을 한 번 셰이핑하고 HarfBuzz가 실제로 낸 flag만 읽는다.
    std::optional<std::uint32_t> FirstParagraphUnsafeGraphemeBoundary() {
        PrepareReference();
        // RTL run의 glyph는 시각 순서로 나오므로 배열 순서로 "첫 번째"를 고르면
        // 논리적으로 마지막 경계가 잡힌다. 논리 순서의 최솟값을 고른다.
        std::optional<std::uint32_t> first;
        for (const ShapedGlyph& glyph : referenceGlyphs_) {
            const bool unsafe =
                (glyph.harfbuzzGlyphFlags &
                 static_cast<std::uint32_t>(HB_GLYPH_FLAG_UNSAFE_TO_BREAK)) != 0U;
            if (!unsafe) continue;
            const std::optional<std::uint32_t> boundary =
                GraphemeBoundaryAtByte(glyph.sourceBytes.begin);
            if (!boundary || *boundary == 0U) continue;
            if (!first || *boundary < *first) first = boundary;
        }
        return first;
    }

    // 그 경계와 그 앞의 합법 경계 사이에 오직 하나 놓이는 폭. 두 값이 붙어
    // 있으면 "사이"가 존재하지 않으므로 곧바로 실패한다.
    Fixed26_6 WidthThatWouldOtherwiseChoose(std::uint32_t graphemeBoundary) {
        PrepareReference();
        const std::int32_t at = CumulativeAdvanceRawTo(graphemeBoundary);
        const std::int32_t before =
            CumulativeAdvanceRawTo(PrecedingLegalBoundary(graphemeBoundary));
        REQUIRE(at > before + 1);
        return Fixed26_6::FromRaw(before + (at - before) / 2);
    }

    // 문단 셰이핑을 그대로 잘라 붙였을 때 나오는 glyph 배열. 최종 줄 재셰이핑이
    // 문단 조각과 정말 다른지를 재는 데만 쓴다.
    std::vector<ShapedGlyph> ParagraphSliceForBytes(SourceByteRange bytes) {
        PrepareReference();
        std::vector<ShapedGlyph> sliced;
        for (const ShapedGlyph& glyph : referenceGlyphs_) {
            if (glyph.sourceBytes.begin >= bytes.begin &&
                glyph.sourceBytes.end <= bytes.end) {
                sliced.push_back(glyph);
            }
        }
        return sliced;
    }

    nlohmann::ordered_json CanonicalJson(const text::TextLayout& layout) const {
        return CanonicalLayoutJson(layout);
    }

    nlohmann::ordered_json ExpectedCanonicalJson() const {
        return ExpectedCanonicalJson(spec_.name);
    }

    nlohmann::ordered_json ExpectedCanonicalJson(const std::string& key) const {
        static const nlohmann::ordered_json kExpected =
            nlohmann::ordered_json::parse(ReadFile(MOLGA_TEXT_EXPECTED_LAYOUT));
        const auto found = kExpected.find(key);
        REQUIRE_MESSAGE(found != kExpected.end(),
                        ("expected/layout.json has no entry named " + key));
        return *found;
    }

    // ── Runtime dependency counters ─────────────────────────────────────────
    void ResetIcuAndHarfBuzzCounters() {
        text::detail::ResetIcuObjectCreationCount();
        text::detail::ResetLayoutIcuObjectCreationCount();
        text::detail::ResetHarfBuzzObjectCreationCount();
    }
    std::uint64_t IcuCallCount() const {
        return text::detail::IcuObjectCreationCount() +
               text::detail::LayoutIcuObjectCreationCount();
    }
    std::uint64_t HarfBuzzCallCount() const {
        return text::detail::HarfBuzzObjectCreationCount();
    }

private:
    std::optional<std::shared_ptr<const text::TextLayout>> Remember(
        std::optional<std::shared_ptr<const text::TextLayout>> result) {
        if (result) lastLayout_ = *result;
        return result;
    }

    // 실패 닫힌 헬퍼. optional을 확인 없이 역참조하면 성공이 nullopt로 바뀌는
    // 회귀가 UB가 되고, 그 UB는 기대값과 우연히 같은 값을 읽어 통과할 수 있다.
    std::shared_ptr<const text::TextLayout> Required(
        std::optional<std::shared_ptr<const text::TextLayout>> result) {
        REQUIRE(result.has_value());
        REQUIRE(*result != nullptr);
        lastLayout_ = *result;
        return *result;
    }

    void PrepareReference() {
        if (referencePrepared_) return;
        referencePrepared_ = true;
        text::VectorTextDiagnosticSink referenceSink;
        auto buffer = UnicodeTextBuffer::Build(spec_.utf8, referenceSink);
        REQUIRE(buffer);
        buffer_ = std::move(buffer);
        auto analysis = text::UnicodeTextAnalyzer::Analyze(
            *buffer_, {spec_.locale, text::BaseDirection::Auto}, referenceSink);
        REQUIRE(analysis);
        analysis_ = std::move(analysis);
        auto family = Corpus().Resolver().BuildCandidates(
            kPrimaryFamily, {400, 100, molga::FontSlant::Upright}, referenceSink);
        REQUIRE(family);

        text::ShapeStyle style;
        style.fontSize = Fixed26_6::FromRaw(spec_.fontSizeRaw);
        style.language = spec_.locale;
        text::TextShapingService reference;
        const std::size_t textBytes = std::string(spec_.utf8).size();
        for (const AnalysisItem& item : analysis_->Items()) {
            const bool first = item.sourceBytes.begin == 0U;
            const bool last = item.sourceBytes.end == textBytes;
            auto shaped = reference.ShapeAnalysisItem(
                *buffer_, *analysis_, item, *family, style, {first, last},
                referenceSink);
            REQUIRE(shaped);
            for (const ShapedRun& run : *shaped) {
                for (const ShapedGlyph& glyph : run.glyphs) {
                    referenceGlyphs_.push_back(glyph);
                }
            }
        }
        REQUIRE_FALSE(referenceGlyphs_.empty());
    }

    std::optional<std::uint32_t> GraphemeBoundaryAtByte(std::uint32_t byte) const {
        const std::vector<std::uint32_t>& boundaries =
            analysis_->GraphemeBoundaries();
        for (std::size_t index = 0; index < boundaries.size(); ++index) {
            if (boundaries[index] == byte) {
                return static_cast<std::uint32_t>(index);
            }
        }
        return std::nullopt;
    }

    std::uint32_t PrecedingLegalBoundary(std::uint32_t graphemeBoundary) const {
        // 이 픽스처의 텍스트에서 앞선 합법 경계는 언제나 줄 시작(0)이다. 그
        // 사실을 조용히 가정하지 않고 여기서 못 박는다.
        REQUIRE(graphemeBoundary >= 1U);
        return 0U;
    }

    std::int32_t CumulativeAdvanceRawTo(std::uint32_t graphemeBoundary) const {
        const std::vector<std::uint32_t>& boundaries =
            analysis_->GraphemeBoundaries();
        REQUIRE(graphemeBoundary < boundaries.size());
        const std::uint32_t limit = boundaries[graphemeBoundary];
        std::int32_t total = 0;
        for (const ShapedGlyph& glyph : referenceGlyphs_) {
            if (glyph.sourceBytes.end <= limit) total += glyph.advanceX.Raw();
        }
        return total;
    }

    const LayoutFixtureSpec& spec_;
    std::shared_ptr<const text::TextLayout> lastLayout_;
    std::optional<UnicodeTextBuffer> buffer_;
    std::optional<UnicodeAnalysis> analysis_;
    std::vector<ShapedGlyph> referenceGlyphs_;
    bool referencePrepared_ = false;
};

LayoutFixture LoadLayoutFixture(const std::string& name) {
    return LayoutFixture(FixtureSpec(name));
}

}  // namespace

// ── Step 1: every accepted line is shaped in its real line context ──────────
TEST_CASE("every accepted line is shaped in its real line context") {
    LayoutFixture f = LoadLayoutFixture("final-line-fi");
    const auto layout = f.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
    REQUIRE(layout);
    CHECK(f.shaper.ParagraphShapeCount() == 1);
    CHECK(f.shaper.FinalLineShapeCount() == (*layout)->lines.size());
    CHECK(f.CanonicalJson(**layout) == f.ExpectedCanonicalJson());
}

// 위 케이스는 재셰이핑이 실제로 일어났는지를 계수로만 묻는다. 계수가 옳은
// 자리에서 올라갔는지는 별개이므로, 같은 픽스처에서 (1) 줄이 여럿이고 (2)
// 방향이 섞여 있으며 (3) 최종 줄 item 셰이핑이 정말 호출됐다는 세 가지를
// 따로 못 박는다. 셋 중 하나라도 무너지면 위 케이스는 아무것도 증명하지 못한다.
TEST_CASE("the final-line fixture is multi-line, bidirectional and reshaped") {
    LayoutFixture f = LoadLayoutFixture("final-line-fi");
    const auto layout = f.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
    REQUIRE(layout);
    CHECK((*layout)->lines.size() >= 2U);
    CHECK(f.shaper.FinalLineItemShapeCount() >= (*layout)->lines.size());
    CHECK(f.EveryAcceptedLineWasFinalReshaped());
    // Step 8: 문단 측정이 허락하는 것보다 한 후보 더 나아간 자리를 제안하므로,
    // 줄의 끝을 정하는 것은 언제나 그 자리를 다시 셰이핑한 권한 있는 값이다.
    // 되짚기가 사라지면 각 줄이 한 grapheme씩 길어져 폭 제약을 넘긴다.
    CHECK(f.BacktrackStepCount() >= 1U);
    for (const text::TextLine& line : (*layout)->lines) {
        CHECK(line.advance.Raw() <= 9 * 64);
    }

    std::vector<std::uint8_t> levels;
    for (const text::TextLine& line : (*layout)->lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            levels.push_back(run.bidiLevel);
        }
    }
    REQUIRE_FALSE(levels.empty());
    CHECK(*std::min_element(levels.begin(), levels.end()) == 0U);
    CHECK(*std::max_element(levels.begin(), levels.end()) >= 1U);

    // 마지막 줄은 논리 순서로 level 1 공백 다음에 level 2 숫자가 온다. UBA
    // L2가 그 둘을 뒤집으므로 시각 순서의 첫 run이 원본에서 더 뒤에 있다.
    // golden을 열어 보지 않고도 이 재배열이 계약임을 읽을 수 있어야 한다.
    const text::TextLine& lastLine = (*layout)->lines.back();
    REQUIRE(lastLine.visualRuns.size() == 2U);
    CHECK(lastLine.visualRuns[0].bidiLevel == 2U);
    CHECK(lastLine.visualRuns[1].bidiLevel == 1U);
    REQUIRE_FALSE(lastLine.visualRuns[0].glyphs.empty());
    REQUIRE_FALSE(lastLine.visualRuns[1].glyphs.empty());
    CHECK(lastLine.visualRuns[0].glyphs.front().glyph.sourceBytes.begin >
          lastLine.visualRuns[1].glyphs.front().glyph.sourceBytes.begin);
    CHECK(lastLine.visualRuns[0].glyphs.front().origin.x.Raw() <
          lastLine.visualRuns[1].glyphs.front().origin.x.Raw());

    // 합자가 삼킨 경계(grapheme 1)는 후보에서 빠지고, 그 옆의 안전한 경계
    // (grapheme 2)는 남는다. 둘을 함께 봐야 "언제나 버린다"와 "한 번도 버리지
    // 않는다"가 모두 죽는다.
    CHECK(f.WasBreakCandidateDiscarded(1U));
    CHECK_FALSE(f.WasBreakCandidateDiscarded(2U));

    // 합자가 grapheme 경계 하나를 glyph 안으로 삼켰다는 사실 자체를 확인한다.
    // 삼키지 않았다면 "문단 배열을 자를 수 없다"는 이 픽스처의 전제가 사라진다.
    bool spansAGraphemeBoundary = false;
    for (const text::TextLine& line : (*layout)->lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            for (const text::PositionedGlyph& glyph : run.glyphs) {
                if (glyph.glyph.graphemes.end - glyph.glyph.graphemes.begin > 1U) {
                    spansAGraphemeBoundary = true;
                }
            }
        }
    }
    CHECK(spansAGraphemeBoundary);
}

// ── Step 1a: overlong unbreakable spans ─────────────────────────────────────
TEST_CASE("word wrap never splits an overlong unbreakable span") {
    LayoutFixture f = LoadLayoutFixture("overlong-word");
    CHECK(f.Layout(TextWrapMode::Word, TextOverflowMode::Overflow)->lines.size() == 1);
    CHECK(f.Layout(TextWrapMode::Word, TextOverflowMode::Clip)->clipped);
    CHECK(f.Layout(TextWrapMode::Word, TextOverflowMode::Ellipsis)->ellipsized);
}

// 위 세 CHECK는 전부 "무언가가 참이다" 쪽만 본다. 세 정책이 서로 다른 결과를
// 낸다는 사실이 빠지면 세 갈래를 하나로 합쳐도 통과하므로, 반대편을 함께 못
// 박는다.
TEST_CASE("the three overlong policies differ from each other") {
    LayoutFixture f = LoadLayoutFixture("overlong-word");
    const auto overflow = f.Layout(TextWrapMode::Word, TextOverflowMode::Overflow);
    const auto clip = f.Layout(TextWrapMode::Word, TextOverflowMode::Clip);
    const auto ellipsis = f.Layout(TextWrapMode::Word, TextOverflowMode::Ellipsis);
    CHECK_FALSE(overflow->clipped);
    CHECK_FALSE(overflow->ellipsized);
    CHECK_FALSE(clip->ellipsized);
    CHECK_FALSE(ellipsis->clipped);
    // Clip은 표시만 잘렸다고 적을 뿐 논리 glyph는 그대로 남긴다.
    REQUIRE(clip->lines.size() == 1U);
    REQUIRE(overflow->lines.size() == 1U);
    CHECK(clip->lines[0].graphemes.end == overflow->lines[0].graphemes.end);
    CHECK(clip->lines[0].advance.Raw() == overflow->lines[0].advance.Raw());
    // Overflow는 제약을 넘겨서라도 토큰을 그대로 남긴다.
    REQUIRE(overflow->lines.size() == 1U);
    CHECK(overflow->lines[0].advance.Raw() > 9 * 64);
    // Ellipsis는 줄임표를 넣어 제약 안으로 들어온다.
    REQUIRE(ellipsis->lines.size() == 1U);
    CHECK(ellipsis->lines[0].advance.Raw() <= 9 * 64);
    CHECK(ellipsis->lines[0].graphemes.end < overflow->lines[0].graphemes.end);
    // 이 토큰은 첫 글자 하나조차 줄임표와 함께 들어가지 못하므로, Step 10c는
    // 남길 grapheme이 없어질 때까지 정확히 하나씩 빼고 멈춘다. 멈추지 않으면
    // 케이스가 통과하는 대신 걸린다.
    CHECK(ellipsis->lines[0].graphemes.begin == ellipsis->lines[0].graphemes.end);
}

// ── Step 1b: empty paragraphs and trailing separators ───────────────────────
TEST_CASE("empty paragraphs and trailing newline retain metric-bearing lines") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto empty = f.LayoutText("");
    REQUIRE(empty->lines.size() == 1);
    CHECK(empty->lines[0].sourceBytes.begin == 0);
    CHECK(empty->lines[0].sourceBytes.end == 0);
    CHECK(empty->lines[0].graphemes.begin == 0);
    CHECK(empty->lines[0].graphemes.end == 0);
    CHECK(empty->lines[0].bottom.Raw() > empty->lines[0].top.Raw());
    const auto trailing = f.LayoutText("A\n");
    REQUIRE(trailing->lines.size() == 2);
    CHECK(trailing->lines[0].sourceBytes.begin == 0);
    CHECK(trailing->lines[0].sourceBytes.end == 1);
    CHECK(trailing->lines[0].graphemes.begin == 0);
    CHECK(trailing->lines[0].graphemes.end == 1);
    CHECK(trailing->lines[1].sourceBytes.begin == 2);
    CHECK(trailing->lines[1].sourceBytes.end == 2);
    CHECK(trailing->lines[1].graphemes.begin == 2);
    CHECK(trailing->lines[1].graphemes.end == 2);
    CHECK(trailing->lines[1].advance.Raw() == 0);
    CHECK(trailing->lines[1].bottom.Raw() > trailing->lines[1].top.Raw());
}

// 빈 줄이 metric을 갖는다는 것만으로는 "다음 줄이 그 metric만큼 내려갔다"가
// 증명되지 않는다. 두 줄이 정말 서로 다른 세로 자리에 있고, 문단 전체 높이가
// 두 줄을 담는다는 것을 함께 못 박는다.
TEST_CASE("the trailing empty line occupies its own vertical band") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto trailing = f.LayoutText("A\n");
    REQUIRE(trailing->lines.size() == 2);
    CHECK(trailing->lines[1].top.Raw() >= trailing->lines[0].bottom.Raw());
    CHECK(trailing->lines[1].baseline.Raw() > trailing->lines[0].baseline.Raw());
    CHECK(trailing->intrinsicSize.height.Raw() ==
          trailing->lines[1].bottom.Raw() - trailing->lines[0].top.Raw());
    CHECK(f.CanonicalJson(*trailing) ==
          f.ExpectedCanonicalJson("empty-lines-trailing"));
}

// ── Step 1c: procedural em metrics ──────────────────────────────────────────
TEST_CASE("procedural em metrics cover empty and missing-family lines") {
    LayoutFixture f = LoadLayoutFixture("no-face-metrics");
    const auto em = Fixed26_6::FromRaw(16 * 64);
    const auto empty = f.LayoutTextWithFamily("", "missing-family", em);
    REQUIRE(empty->lines.size() == 1);
    CHECK(empty->lines[0].ascent.Raw() == 12 * 64);
    CHECK(empty->lines[0].descent.Raw() == 4 * 64);
    CHECK(empty->lines[0].lineGap.Raw() == 0);
    const auto trailing =
        f.LayoutTextWithFamily("\n", "missing-family", em);
    REQUIRE(trailing->lines.size() == 2);
    CHECK(trailing->lines[1].sourceBytes.begin == 1);
    CHECK(trailing->lines[1].sourceBytes.end == 1);
    CHECK(trailing->lines[1].graphemes.begin == 1);
    CHECK(trailing->lines[1].graphemes.end == 1);
    CHECK(trailing->lines[1].ascent.Raw() == 12 * 64);
    CHECK(trailing->lines[1].descent.Raw() == 4 * 64);
    CHECK(trailing->lines[1].lineGap.Raw() == 0);
    const auto missing =
        f.LayoutTextWithFamily(u8"👩‍🚀", "missing-family", em);
    REQUIRE(missing);
    const auto& glyph = FirstGlyph(*missing);
    CHECK(glyph.missing);
    CHECK_FALSE(glyph.faceResource);
    CHECK(glyph.advanceX.Raw() == 16 * 64);
}

// 위 케이스의 세 값은 face가 없을 때의 값이다. face가 있을 때는 반드시 달라야
// 하고, 다르지 않으면 Step 12a의 imported metric 경로가 통째로 죽어 있어도
// 아무 단언도 움직이지 않는다.
TEST_CASE("a resolved family does not use the procedural fallback metrics") {
    LayoutFixture f = LoadLayoutFixture("no-face-metrics");
    const auto em = Fixed26_6::FromRaw(16 * 64);
    const auto withFace = f.LayoutTextWithFamily(u8"A", kPrimaryFamily, em);
    REQUIRE(withFace->lines.size() == 1);
    // NotoSans는 ascender 1069/1000 em이므로 3/4 em과 같을 수 없다.
    CHECK(withFace->lines[0].ascent.Raw() != 12 * 64);
    CHECK(withFace->lines[0].descent.Raw() != 4 * 64);
    CHECK(withFace->lines[0].ascent.Raw() > 0);
    CHECK(withFace->lines[0].descent.Raw() > 0);
    // 그리고 빈 줄도 같은 face metric을 쓴다(Step 12a의 empty-line 갈래).
    const auto emptyWithFamily = f.LayoutTextWithFamily("", kPrimaryFamily, em);
    REQUIRE(emptyWithFamily->lines.size() == 1);
    CHECK(emptyWithFamily->lines[0].ascent.Raw() ==
          withFace->lines[0].ascent.Raw());
    CHECK(emptyWithFamily->lines[0].descent.Raw() ==
          withFace->lines[0].descent.Raw());
}

// ── Step 1d: imported design metrics, never rasterizer metrics ──────────────
namespace {

constexpr const char* kPatchedMetricsFont = "2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b";
constexpr const char* kPatchedMetricsFamily =
    "2c2c2c2c2c2c2c2c2c2c2c2c2c2c2c2c";

// head/hhea의 정확한 SFNT 정수만 갈아 끼우고 그 표의 checksum을 다시 쓴다.
// FontImporter가 표 checksum을 검증하므로(Step 6a) 값을 고치고 checksum을 두면
// import 자체가 거절된다.
std::uint32_t TableChecksum(const std::vector<unsigned char>& bytes,
                            std::size_t offset, std::size_t length) {
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < length; index += 4U) {
        std::uint32_t word = 0;
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            const std::size_t at = offset + index + byte;
            const std::uint32_t value =
                at < offset + length ? static_cast<std::uint32_t>(bytes[at]) : 0U;
            word = (word << 8) | value;
        }
        sum += word;
    }
    return sum;
}

void AuthorPatchedMetricsFont(const fs::path& fontsDir,
                              const std::string& fileName,
                              const std::string& baseFontName,
                              const std::string& guid,
                              std::int16_t ascender, std::int16_t descender,
                              std::int16_t lineGap) {
    std::vector<unsigned char> bytes =
        test_support::ReadAllBytes(fontsDir / baseFontName);
    const std::uint16_t tableCount =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[4]) << 8) |
                                   bytes[5]);
    REQUIRE(tableCount > 0U);
    std::size_t hheaRecord = 0;
    std::size_t hheaOffset = 0;
    std::size_t hheaLength = 0;
    for (std::uint16_t index = 0; index < tableCount; ++index) {
        const std::size_t record = 12U + static_cast<std::size_t>(index) * 16U;
        const std::string tag(reinterpret_cast<const char*>(&bytes[record]), 4U);
        if (tag != "hhea") continue;
        hheaRecord = record;
        hheaOffset = test_support::ReadBigEndianU32(bytes, record + 8U);
        hheaLength = test_support::ReadBigEndianU32(bytes, record + 12U);
    }
    REQUIRE(hheaLength >= 10U);
    const auto writeS16 = [&bytes](std::size_t at, std::int16_t value) {
        const auto raw = static_cast<std::uint16_t>(value);
        bytes[at] = static_cast<unsigned char>((raw >> 8) & 0xFFU);
        bytes[at + 1U] = static_cast<unsigned char>(raw & 0xFFU);
    };
    writeS16(hheaOffset + 4U, ascender);
    writeS16(hheaOffset + 6U, descender);
    writeS16(hheaOffset + 8U, lineGap);
    test_support::WriteBigEndianU32(bytes, hheaRecord + 4U,
                                    TableChecksum(bytes, hheaOffset, hheaLength));

    const fs::path source = fontsDir / fileName;
    REQUIRE_MESSAGE(!fs::exists(source), source.string());
    test_support::WriteBytes(source, bytes);

    const fs::path metaPath = molga::AssetMeta::MetaPathFor(source);
    const nlohmann::json meta{
        {"guid", guid},
        {"importer", "FontImporter"},
        {"importerVersion", 2},
        {"settings",
         nlohmann::json{{"faceIndex", 0},
                        {"weight", 400},
                        {"stretchPercent", 100},
                        {"slant", "Upright"},
                        {"redistributableConfirmed", true},
                        {"licenseKind", "OFL-1.1"},
                        {"copyright", "fixture provenance: Noto Fonts ffebf8c1"},
                        {"licenseAssetGuid", test_support::kNotoLicenseGuid}}}};
    fs::create_directories(metaPath.parent_path());
    std::ofstream output(metaPath, std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), metaPath.string());
    output << meta.dump(2);
    output.close();
    REQUIRE_MESSAGE(output.good(), metaPath.string());
}

void AuthorSingleFaceFamily(const fs::path& familiesDir,
                            const std::string& fileName,
                            const std::string& familyGuid,
                            const std::string& fontGuid) {
    const nlohmann::json family{
        {"schemaVersion", 1},
        {"faces", nlohmann::json::array({nlohmann::json{
                      {"fontGuid", fontGuid},
                      {"faceIndex", 0},
                      {"weight", 400},
                      {"stretchPercent", 100},
                      {"slant", "Upright"}}})},
        {"fallbackFamilyGuids", nlohmann::json::array()}};
    const fs::path source = familiesDir / fileName;
    fs::create_directories(familiesDir);
    {
        std::ofstream output(source, std::ios::trunc);
        REQUIRE_MESSAGE(output.good(), source.string());
        output << family.dump(2);
    }
    const fs::path metaPath = molga::AssetMeta::MetaPathFor(source);
    const nlohmann::json meta{{"guid", familyGuid},
                              {"importer", "FontFamilyImporter"},
                              {"importerVersion", 1},
                              {"settings", nlohmann::json::object()}};
    std::ofstream output(metaPath, std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), metaPath.string());
    output << meta.dump(2);
}

// 이 케이스만의 자격 트리 사본. 공유 corpus에 폰트를 하나 더 쓰면 다른 케이스가
// 읽는 카탈로그가 이 케이스 때문에 달라진다(test_text_shaping의 선례).
class PatchedMetricsCorpus {
public:
    PatchedMetricsCorpus()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))) {
        AuthorPatchedMetricsFont(tree_.AssetsRoot() / "fonts",
                                 "patched-metrics.ttf", "NotoSans-Regular.ttf",
                                 kPatchedMetricsFont, 750, -250, 125);
        AuthorSingleFaceFamily(tree_.AssetsRoot() / "families",
                               "patched-metrics.fontfamily", kPatchedMetricsFamily,
                               kPatchedMetricsFont);
        std::string bindError;
        REQUIRE_MESSAGE(database_.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database_.ScanProject(tree_.AssetsRoot());
        const molga::AssetRecord* record =
            database_.Find(std::string(kPatchedMetricsFont));
        REQUIRE(record != nullptr);
    }

    molga::AssetDatabase& Database() noexcept { return database_; }

private:
    QualificationAssetTreeFixture tree_;
    molga::AssetDatabase database_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
};

}  // namespace

TEST_CASE("line metrics come from imported design integers, never the rasterizer") {
    PatchedMetricsCorpus corpus;
    const auto size = Fixed26_6::FromRaw(1056);
    const auto expectedAscent = Fixed26_6::CheckedMulDiv(size, 750, 1000);
    const auto expectedDescent = Fixed26_6::CheckedMulDiv(size, 250, 1000);
    const auto expectedLineGap = Fixed26_6::CheckedMulDiv(size, 125, 1000);
    REQUIRE(expectedAscent);
    REQUIRE(expectedDescent);
    REQUIRE(expectedLineGap);

    const auto layoutOnce = [&corpus, size]() {
        text::FontRepository repository(corpus.Database());
        text::FontFamilyResolver resolver(corpus.Database(), repository);
        text::TextShapingService shaper;
        text::TextLayoutCache cache(text::TextLayoutCacheLimits::Production());
        text::TextLayoutService service(resolver, shaper, cache);
        text::VectorTextDiagnosticSink sink;
        TextLayoutRequest request;
        request.utf8 = u8"Av";
        request.style.fontFamilyGuid = kPatchedMetricsFamily;
        request.style.shape.fontSize = size;
        request.style.shape.language = "en";
        request.style.analysis.locale = "en";
        const auto layout = service.Layout(request, sink);
        REQUIRE(layout);
        REQUIRE(*layout != nullptr);
        return *layout;
    };

    const auto warm = layoutOnce();
    REQUIRE(warm->lines.size() == 1U);
    CHECK(warm->lines[0].ascent.Raw() == expectedAscent->Raw());
    CHECK(warm->lines[0].descent.Raw() == expectedDescent->Raw());
    CHECK(warm->lines[0].lineGap.Raw() == expectedLineGap->Raw());
    // 값이 정말 저작된 정수에서 왔는지 못 박는다. 우연히 같은 숫자를 낸
    // 다른 경로가 있으면 위 세 CHECK는 아무것도 말하지 못한다.
    CHECK(expectedAscent->Raw() == 792);
    CHECK(expectedDescent->Raw() == 264);
    CHECK(expectedLineGap->Raw() == 132);

    molga::detail::ResetLegacyFontFaceMetricCallCount();
    const auto cold = layoutOnce();
    REQUIRE(cold->lines.size() == 1U);
    CHECK(cold->lines[0].ascent.Raw() == expectedAscent->Raw());
    CHECK(cold->lines[0].descent.Raw() == expectedDescent->Raw());
    CHECK(cold->lines[0].lineGap.Raw() == expectedLineGap->Raw());
    CHECK(molga::detail::LegacyFontFaceMetricCallCount() == 0U);
}

// ── Step 1e: the whole-candidate ellipsis ───────────────────────────────────
TEST_CASE("ellipsis reshapes retained text and token as one final candidate") {
    LayoutFixture f = LoadLayoutFixture("arabic-ellipsis-context");
    const auto layout = f.LayoutEllipsized();
    REQUIRE(layout);
    CHECK(f.shaper.SeparateEllipsisShapeCount() == 0);
    CHECK(f.shaper.FullRetainedPlusEllipsisShapeCount() ==
          f.EllipsisRemovalAttemptCount());
    CHECK(f.shaper.EveryEllipsisCandidateUsedFinalBotEot());
}

// 위 케이스의 두 계수가 둘 다 0이면 세 CHECK가 전부 공허하게 통과한다. 실제로
// 줄임이 일어났고 후보가 하나 이상 만들어졌음을 따로 못 박는다.
TEST_CASE("the arabic ellipsis fixture actually ellipsizes") {
    LayoutFixture f = LoadLayoutFixture("arabic-ellipsis-context");
    const auto layout = f.LayoutEllipsized();
    REQUIRE(layout);
    CHECK((*layout)->ellipsized);
    CHECK(f.EllipsisRemovalAttemptCount() >= 1U);
    CHECK(f.shaper.FullRetainedPlusEllipsisShapeCount() >= 1U);
    // 관찰은 Layout 호출마다 비워지므로 아래에서 한 번 더 배치하기 전에
    // 지금 값을 붙들어 둔다.
    const std::size_t attempts = f.EllipsisRemovalAttemptCount();
    // 줄임 전의 같은 줄과 견준다. 남긴 grapheme이 줄지 않았다면 Step 10c의
    // 반복이 한 번도 돌지 않은 것이고, 위 계수 비교는 후보 하나만 보게 된다.
    LayoutFixture untruncated = LoadLayoutFixture("arabic-ellipsis-context");
    TextLayoutRequest wide = untruncated.MakeRequest(f.Spec().utf8);
    wide.style.overflow = TextOverflowMode::Overflow;
    wide.style.maxLines = 0;
    const auto plain = untruncated.service.Layout(wide, untruncated.sink);
    REQUIRE(plain);
    REQUIRE_FALSE((*plain)->lines.empty());
    CHECK((*layout)->lines[0].graphemes.end <
          (*plain)->lines[0].graphemes.end);
    CHECK(attempts ==
          static_cast<std::size_t>((*plain)->lines[0].graphemes.end -
                                   (*layout)->lines[0].graphemes.end) +
              1U);
    REQUIRE((*layout)->lines.size() == 1U);
    CHECK((*layout)->lines[0].advance.Raw() <= 22 * 64);
    // 합성 glyph는 원본의 잘린 자리(길이 0)를 가리켜야 한다. 원본 byte를
    // 그대로 물려주면 caret과 hit-test가 없는 글자를 가리키게 된다.
    const text::TextLine& line = (*layout)->lines[0];
    bool sawSynthetic = false;
    for (const text::VisualRun& run : line.visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.sourceBytes.begin == glyph.glyph.sourceBytes.end) {
                sawSynthetic = true;
                CHECK(glyph.glyph.sourceBytes.begin == line.sourceBytes.end);
                CHECK(glyph.glyph.graphemes.begin == glyph.glyph.graphemes.end);
                CHECK(glyph.glyph.graphemes.begin == line.graphemes.end);
            }
        }
    }
    CHECK(sawSynthetic);
}

// ── Step 1f: cold and warm recoverable results ──────────────────────────────
TEST_CASE("warm recoverable layouts re-emit identical facts without ICU or HB") {
    LayoutFixture f = LoadLayoutFixture("recoverable-cache");
    for (const TextLayoutRequest& request : {
             f.RequestForBytes(std::string("A\xFFZ", 3)),
             f.RequestForMissingFamily(u8"👩‍🚀")}) {
        molga::text::VectorTextDiagnosticSink cold;
        const auto first = f.service.Layout(request, cold);
        REQUIRE(first);
        REQUIRE_FALSE((*first)->validationFacts.empty());
        CHECK(HasBlockingAuthoredPackageFact((*first)->validationFacts));
        f.ResetIcuAndHarfBuzzCounters();
        molga::text::VectorTextDiagnosticSink warm;
        const auto second = f.service.Layout(request, warm);
        REQUIRE(second == first);
        CHECK(f.IcuCallCount() == 0);
        CHECK(f.HarfBuzzCallCount() == 0);
        CHECK(CanonicalValidationFacts((*second)->validationFacts) ==
              CanonicalValidationFacts((*first)->validationFacts));
        CHECK(CanonicalDiagnosticRecords(warm.Diagnostics()) ==
              CanonicalDiagnosticRecords(cold.Diagnostics()));
    }
}

// 위 케이스의 counter 두 개는 "0이다"만 본다. counter 자체가 죽어 있어도 0이
// 나오므로, 같은 counter가 cold 경로에서는 반드시 올라간다는 반대편을 함께
// 못 박는다. 그리고 진단이 정말 호출자의 문맥으로 다시 조립되는지도 본다.
TEST_CASE("cold layout does reach ICU and HarfBuzz, and facts carry context") {
    LayoutFixture f = LoadLayoutFixture("recoverable-cache");
    const TextLayoutRequest request = f.RequestForBytes(std::string("A\xFFZ", 3));
    f.ResetIcuAndHarfBuzzCounters();
    text::VectorTextDiagnosticSink cold;
    const auto layout = f.service.Layout(request, cold);
    REQUIRE(layout);
    CHECK(f.IcuCallCount() > 0);
    CHECK(f.HarfBuzzCallCount() > 0);
    REQUIRE_FALSE(cold.Diagnostics().empty());
    bool sawUtf8Invalid = false;
    for (const text::TextDiagnostic& diagnostic : cold.Diagnostics()) {
        CHECK(diagnostic.assetGuid == Label("layout-fixture-asset"));
        CHECK(diagnostic.sceneObjectId == 7U);
        CHECK(diagnostic.componentType == Label("UILabel"));
        if (diagnostic.code == text::TextDiagnosticCode::Utf8Invalid) {
            sawUtf8Invalid = true;
        }
    }
    CHECK(sawUtf8Invalid);

    // 같은 불변 레이아웃을 다른 컴포넌트가 다시 쓰면 진단은 그 컴포넌트의
    // 이름으로 나와야 한다 — 문맥이 캐시 정체성에 들어가지 않는다는 계약.
    TextLayoutRequest other = request;
    other.diagnosticContext.assetGuid = "second-consumer";
    other.diagnosticContext.sceneObjectId = 99;
    other.diagnosticContext.componentType = "TextRenderer2D";
    text::VectorTextDiagnosticSink second;
    const auto shared = f.service.Layout(other, second);
    REQUIRE(shared);
    CHECK(shared == layout);
    REQUIRE_FALSE(second.Diagnostics().empty());
    for (const text::TextDiagnostic& diagnostic : second.Diagnostics()) {
        CHECK(diagnostic.assetGuid == Label("second-consumer"));
        CHECK(diagnostic.sceneObjectId == 99U);
        CHECK(diagnostic.componentType == Label("TextRenderer2D"));
    }
}

// ── Step 1g: HarfBuzz unsafe boundaries are never accepted ──────────────────
TEST_CASE("layout never accepts a HarfBuzz unsafe boundary") {
    LayoutFixture f = LoadLayoutFixture("unsafe-arabic-boundary");
    const auto unsafe = f.FirstParagraphUnsafeGraphemeBoundary();
    REQUIRE(unsafe);
    const auto layout = f.LayoutAtWidth(
        f.WidthThatWouldOtherwiseChoose(*unsafe));
    REQUIRE(layout);
    CHECK_FALSE(AnyLineEndsAtGrapheme(**layout, *unsafe));
    CHECK(f.WasBreakCandidateDiscarded(*unsafe));
    CHECK(f.EveryAcceptedLineWasFinalReshaped());
}

// 위 케이스의 CHECK_FALSE는 "어떤 줄도 저기서 끝나지 않는다"만 본다. 줄이
// 하나뿐이거나 배치가 통째로 비어 있어도 참이므로, 이 폭에서 정말 여러 줄이
// 나오고 그 줄들이 안전한 경계에서만 끝난다는 것을 함께 못 박는다.
TEST_CASE("the unsafe fixture still wraps, and only at safe boundaries") {
    LayoutFixture f = LoadLayoutFixture("unsafe-arabic-boundary");
    const auto unsafe = f.FirstParagraphUnsafeGraphemeBoundary();
    REQUIRE(unsafe);
    CHECK(*unsafe == 1U);
    const auto layout =
        f.LayoutAtWidth(f.WidthThatWouldOtherwiseChoose(*unsafe));
    REQUIRE(layout);
    CHECK((*layout)->lines.size() >= 2U);
    // ا 뒤(grapheme 3)는 이어쓰기가 끊기는 자리라 HarfBuzz도 안전하다고
    // 표시한다. 그 경계까지 버려지면 "언제나 버린다"로 위 CHECK가 통과한다.
    CHECK_FALSE(f.WasBreakCandidateDiscarded(3U));
    CHECK(f.WasBreakCandidateDiscarded(2U));
    // 아랍어 "سلامك"에서 ا 뒤(grapheme 3)만 안전하다. 어떤 줄도 그 밖의
    // 안쪽 경계에서 끝날 수 없다.
    for (std::size_t index = 0; index + 1U < (*layout)->lines.size(); ++index) {
        CHECK((*layout)->lines[index].graphemes.end == 3U);
    }
    CHECK((*layout)->lines.back().graphemes.end == 5U);
}

// ── Steps 13a-13c: interior carets ──────────────────────────────────────────
namespace {

const text::PositionedGlyph& FirstMultiGraphemeGlyph(
    const text::TextLayout& layout) {
    for (const text::TextLine& line : layout.lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            for (const text::PositionedGlyph& glyph : run.glyphs) {
                if (glyph.glyph.graphemes.end - glyph.glyph.graphemes.begin > 1U) {
                    return glyph;
                }
            }
        }
    }
    REQUIRE_MESSAGE(false, "the layout has no multi-grapheme glyph");
    static const text::PositionedGlyph kNever{};
    return kNever;
}

}  // namespace

// GDEF 집합이 완전하고 순서가 맞으면 그대로 쓰고, 그렇지 않으면 균등 분할로
// 내려간다. 두 갈래를 한 케이스에서 함께 본다: 한쪽만 보면 나머지 갈래를
// 통째로 지워도 스위트가 통과한다.
TEST_CASE("interior carets come from GDEF when complete and are proportional otherwise") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto em = Fixed26_6::FromRaw(16 * 64);
    const auto normal = f.LayoutTextWithFamily(u8"ffi", kPrimaryFamily, em);
    const text::PositionedGlyph& ligature = FirstMultiGraphemeGlyph(*normal);
    REQUIRE(ligature.glyph.graphemes.begin == 0U);
    REQUIRE(ligature.glyph.graphemes.end == 3U);
    REQUIRE(ligature.glyph.adjustedGdefCaretOffsets.size() == 2U);
    REQUIRE(ligature.interiorCarets.size() == 2U);
    for (std::size_t index = 0; index < ligature.interiorCarets.size(); ++index) {
        const text::GlyphInteriorCaret& caret = ligature.interiorCarets[index];
        CHECK(caret.fromAdjustedGdef);
        // 논리 경계는 LTR run이므로 앞에서 센다.
        CHECK(caret.logicalGraphemeBoundary ==
              ligature.glyph.graphemes.begin + static_cast<std::uint32_t>(index) + 1U);
        // 절대 좌표다: glyph 원점에 GDEF offset을 더한 값.
        CHECK(caret.position.x.Raw() ==
              ligature.origin.x.Raw() +
                  ligature.glyph.adjustedGdefCaretOffsets[index].Raw());
        CHECK(caret.position.y.Raw() == ligature.origin.y.Raw());
        CHECK(caret.position.x.Raw() > ligature.origin.x.Raw());
        CHECK(caret.position.x.Raw() <
              ligature.origin.x.Raw() + ligature.glyph.advanceX.Raw());
    }

    // 같은 face, 같은 합자, 거의 0에 가까운 크기. GDEF 두 값이 같은 raw로
    // 무너져 "엄격히 증가"가 깨지므로 집합 전체가 거절되고 Step 13c가 쓰인다.
    const auto degenerate =
        f.LayoutTextWithFamily(u8"ffi", kPrimaryFamily, Fixed26_6::FromRaw(2));
    const text::PositionedGlyph& tiny = FirstMultiGraphemeGlyph(*degenerate);
    REQUIRE(tiny.glyph.adjustedGdefCaretOffsets.size() == 2U);
    REQUIRE(tiny.glyph.adjustedGdefCaretOffsets[0].Raw() ==
            tiny.glyph.adjustedGdefCaretOffsets[1].Raw());
    REQUIRE(tiny.interiorCarets.size() == 2U);
    for (std::size_t index = 0; index < tiny.interiorCarets.size(); ++index) {
        const text::GlyphInteriorCaret& caret = tiny.interiorCarets[index];
        CHECK_FALSE(caret.fromAdjustedGdef);
        const auto expected = molga::Fixed26_6::CheckedMulDiv(
            tiny.glyph.advanceX, static_cast<std::int64_t>(index) + 1, 3);
        REQUIRE(expected);
        CHECK(caret.position.x.Raw() ==
              tiny.origin.x.Raw() + expected->Raw());
    }
}

// RTL run에서는 시각 진행 순서의 i번째 자리가 뒤에서 센 논리 경계다. 방향을
// 무시하고 언제나 앞에서 세면 아랍어 합자의 caret이 반대쪽 글자를 가리킨다.
TEST_CASE("an RTL ligature maps its interior caret to the trailing logical boundary") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto layout =
        f.LayoutTextWithFamily(u8"لا", kPrimaryFamily, Fixed26_6::FromRaw(16 * 64));
    const text::PositionedGlyph& ligature = FirstMultiGraphemeGlyph(*layout);
    REQUIRE(ligature.glyph.bidiLevel % 2U == 1U);
    REQUIRE(ligature.glyph.graphemes.begin == 0U);
    REQUIRE(ligature.glyph.graphemes.end == 2U);
    REQUIRE(ligature.interiorCarets.size() == 1U);
    CHECK(ligature.interiorCarets[0].logicalGraphemeBoundary ==
          ligature.glyph.graphemes.end - 1U);
}

// ── Step 13: alignment and intrinsic size ───────────────────────────────────
TEST_CASE("horizontal and vertical alignment move the same content") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto measure = [&f](text::TextHorizontalAlignment horizontal,
                              text::TextVerticalAlignment vertical) {
        TextLayoutRequest request = f.MakeRequest(u8"AB");
        request.constraints.width = Fixed26_6::FromRaw(400 * 64);
        request.constraints.height = Fixed26_6::FromRaw(200 * 64);
        request.style.horizontal = horizontal;
        request.style.vertical = vertical;
        const auto layout = f.service.Layout(request, f.sink);
        REQUIRE(layout);
        REQUIRE(*layout != nullptr);
        return *layout;
    };
    using H = text::TextHorizontalAlignment;
    using V = text::TextVerticalAlignment;
    const auto topLeft = measure(H::Left, V::Top);
    const auto topCenter = measure(H::Center, V::Top);
    const auto topRight = measure(H::Right, V::Top);
    const auto middleLeft = measure(H::Left, V::Middle);
    const auto bottomLeft = measure(H::Left, V::Bottom);

    REQUIRE(topLeft->lines.size() == 1U);
    const std::int32_t advance = topLeft->lines[0].advance.Raw();
    const std::int32_t slackX = 400 * 64 - advance;
    REQUIRE(slackX > 0);
    const std::int32_t leftOrigin =
        topLeft->lines[0].visualRuns[0].glyphs[0].origin.x.Raw();
    CHECK(leftOrigin == 0);
    // 절반은 짝수/양수 편향이 아니라 "0에서 먼 쪽"으로 반올림한다
    // (Fixed26_6의 공유 규칙). 양수 slack에서는 (slack + 1) / 2와 같다.
    CHECK(topCenter->lines[0].visualRuns[0].glyphs[0].origin.x.Raw() ==
          (slackX + 1) / 2);
    CHECK(topRight->lines[0].visualRuns[0].glyphs[0].origin.x.Raw() == slackX);

    const std::int32_t contentHeight = topLeft->intrinsicSize.height.Raw();
    const std::int32_t slackY = 200 * 64 - contentHeight;
    REQUIRE(slackY > 0);
    CHECK(topLeft->lines[0].top.Raw() == 0);
    CHECK(middleLeft->lines[0].top.Raw() == (slackY + 1) / 2);
    CHECK(bottomLeft->lines[0].top.Raw() == slackY);
    // 세로 정렬은 baseline과 glyph 원점을 같은 만큼 옮긴다.
    CHECK(bottomLeft->lines[0].baseline.Raw() ==
          topLeft->lines[0].baseline.Raw() + slackY);
    CHECK(bottomLeft->lines[0].visualRuns[0].glyphs[0].origin.y.Raw() ==
          topLeft->lines[0].visualRuns[0].glyphs[0].origin.y.Raw() + slackY);
    // 정렬은 내재 크기를 바꾸지 않는다. 바꾸면 부모 레이아웃이 정렬에 따라
    // 다른 크기를 받는다.
    CHECK(topRight->intrinsicSize.width.Raw() ==
          topLeft->intrinsicSize.width.Raw());
    CHECK(topRight->intrinsicSize.height.Raw() == contentHeight);
    CHECK(topLeft->intrinsicSize.width.Raw() == advance);
}

// ── Step 12: the baseline step includes lineGap, then lineSpacing ───────────
// 자격 트리의 여섯 폰트는 전부 lineGap이 0이라, 그 폰트들로는 "gap을 포함한
// 뒤에 lineSpacing을 곱한다"는 순서가 관찰되지 않는다. Step 1d가 저작한
// {1000,750,-250,125} face만이 이 계약의 유일한 증인이다.
TEST_CASE("the baseline step folds lineGap before the authored line spacing") {
    PatchedMetricsCorpus corpus;
    text::FontRepository repository(corpus.Database());
    text::FontFamilyResolver resolver(corpus.Database(), repository);
    text::TextShapingService shaper;
    text::TextLayoutCache cache(text::TextLayoutCacheLimits::Production());
    text::TextLayoutService service(resolver, shaper, cache);
    text::VectorTextDiagnosticSink sink;

    const auto size = Fixed26_6::FromRaw(1056);
    const auto layoutWithSpacing = [&](std::int32_t spacingRaw) {
        TextLayoutRequest request;
        request.utf8 = u8"A\nB";
        request.style.fontFamilyGuid = kPatchedMetricsFamily;
        request.style.shape.fontSize = size;
        request.style.shape.language = "en";
        request.style.analysis.locale = "en";
        request.style.lineSpacing = Fixed26_6::FromRaw(spacingRaw);
        const auto layout = service.Layout(request, sink);
        REQUIRE(layout);
        REQUIRE(*layout != nullptr);
        return *layout;
    };

    const auto single = layoutWithSpacing(64);
    REQUIRE(single->lines.size() == 2U);
    CHECK(single->lines[0].lineGap.Raw() == 132);
    const std::int32_t stepWithGap = 792 + 264 + 132;
    CHECK(single->lines[1].baseline.Raw() - single->lines[0].baseline.Raw() ==
          stepWithGap);
    // gap을 빼고 계산했다면 이 값과 같아질 수 없다.
    CHECK(single->lines[1].baseline.Raw() - single->lines[0].baseline.Raw() !=
          792 + 264);

    const auto doubled = layoutWithSpacing(128);
    REQUIRE(doubled->lines.size() == 2U);
    CHECK(doubled->lines[1].baseline.Raw() - doubled->lines[0].baseline.Raw() ==
          stepWithGap * 2);
}

// ── Step 9: height and max-line truncation ──────────────────────────────────
TEST_CASE("height and max-line truncation drop lines without inventing a break") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto full = f.LayoutText(u8"A\nB\nC");
    REQUIRE(full->lines.size() == 3U);
    const std::int32_t twoLineHeight = full->lines[1].bottom.Raw();

    TextLayoutRequest limited = f.MakeRequest(u8"A\nB\nC");
    limited.style.maxLines = 2;
    const auto byLines = f.service.Layout(limited, f.sink);
    REQUIRE(byLines);
    REQUIRE((*byLines)->lines.size() == 2U);
    CHECK((*byLines)->clipped);
    // 남은 줄의 범위는 잘리기 전과 정확히 같다: 잘림이 grapheme 경계를
    // 지어내지 않는다.
    CHECK((*byLines)->lines[1].graphemes.end == full->lines[1].graphemes.end);
    CHECK((*byLines)->lines[1].sourceBytes.end == full->lines[1].sourceBytes.end);

    TextLayoutRequest short_ = f.MakeRequest(u8"A\nB\nC");
    short_.constraints.height = Fixed26_6::FromRaw(twoLineHeight);
    const auto byHeight = f.service.Layout(short_, f.sink);
    REQUIRE(byHeight);
    REQUIRE((*byHeight)->lines.size() == 2U);
    CHECK((*byHeight)->clipped);

    // 잘라 낼 것이 없으면 clipped가 아니다. 이 반대편이 없으면 clipped를
    // 언제나 true로 만들어도 위 두 CHECK가 통과한다.
    TextLayoutRequest roomy = f.MakeRequest(u8"A\nB\nC");
    roomy.style.maxLines = 3;
    roomy.constraints.height = Fixed26_6::FromRaw(full->lines[2].bottom.Raw());
    const auto untouched = f.service.Layout(roomy, f.sink);
    REQUIRE(untouched);
    CHECK((*untouched)->lines.size() == 3U);
    CHECK_FALSE((*untouched)->clipped);
}

// ── Step 12a: checked maxima across the faces a line actually used ──────────
// 한 줄이 두 face를 쓰면 수직 값은 둘의 최댓값이어야 한다. 첫 glyph의 face만
// 보거나 마지막 face만 보면, 문자 하나가 섞인 순간 줄이 잘려 보이거나 쓸데없이
// 커진다. NotoSans(ascender 1069)와 NotoSansArabic(1374)은 그 차이를 낸다.
TEST_CASE("a mixed-face line takes the checked maxima of its faces") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto em = Fixed26_6::FromRaw(16 * 64);
    const auto latin = f.LayoutTextWithFamily(u8"A", kPrimaryFamily, em);
    const auto arabic = f.LayoutTextWithFamily(u8"س", kPrimaryFamily, em);
    const auto mixed = f.LayoutTextWithFamily(u8"Aس", kPrimaryFamily, em);
    REQUIRE(latin->lines.size() == 1U);
    REQUIRE(arabic->lines.size() == 1U);
    REQUIRE(mixed->lines.size() == 1U);
    // 두 face의 값이 실제로 다르다는 전제를 먼저 못 박는다. 같다면 아래
    // 최댓값 단언은 아무것도 구분하지 못한다.
    REQUIRE(arabic->lines[0].ascent.Raw() > latin->lines[0].ascent.Raw());
    REQUIRE(arabic->lines[0].descent.Raw() > latin->lines[0].descent.Raw());
    CHECK(mixed->lines[0].ascent.Raw() == arabic->lines[0].ascent.Raw());
    CHECK(mixed->lines[0].descent.Raw() == arabic->lines[0].descent.Raw());
    // 그리고 그 줄이 정말 두 face를 담고 있어야 한다.
    std::vector<std::string> guids;
    for (const text::VisualRun& run : mixed->lines[0].visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (std::find(guids.begin(), guids.end(), glyph.glyph.fontGuid) ==
                guids.end()) {
                guids.push_back(glyph.glyph.fontGuid);
            }
        }
    }
    CHECK(guids.size() == 2U);
}

// ── Step 5b/14b: a successful fallback is a fact, not a defect ──────────────
// blocksAuthoredPackage와 severity가 상수가 아니라는 유일한 증인이다. 성공한
// fallback만이 Info이고 배포를 막지 않는다 — 그 갈래가 없으면 두 필드를
// 통째로 true/Error로 고정해도 스위트가 통과한다.
TEST_CASE("a successful fallback is an informational, non-blocking fact") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto layout = f.LayoutTextWithFamily(u8"سلام", kPrimaryFamily,
                                               Fixed26_6::FromRaw(16 * 64));
    REQUIRE(layout->validationFacts.size() == 1U);
    const TextValidationFact& fact = layout->validationFacts[0];
    CHECK(fact.code == text::TextDiagnosticCode::FontFamilyInvalid);
    CHECK(fact.severity == text::TextSeverity::Info);
    CHECK(fact.recoverableAtRuntime);
    CHECK_FALSE(fact.blocksAuthoredPackage);
    CHECK_FALSE(HasBlockingAuthoredPackageFact(layout->validationFacts));
    CHECK(fact.sourceBytes.begin == 0U);
    CHECK(fact.sourceBytes.end == 8U);
    CHECK(fact.graphemes.begin == 0U);
    CHECK(fact.graphemes.end == 4U);
    // glyph는 실제로 저작된 primary face가 아니라 fallback family의 face에서
    // 왔다. 그렇지 않다면 위 사실이 거짓이다.
    const ShapedGlyph& glyph = FirstGlyph(*layout);
    CHECK_FALSE(glyph.missing);
    CHECK(glyph.fontGuid != Label("44444444444444444444444444444444"));

    // 같은 family, 같은 크기, 순수 Latin이면 사실이 하나도 없다.
    const auto clean = f.LayoutTextWithFamily(u8"A", kPrimaryFamily,
                                              Fixed26_6::FromRaw(16 * 64));
    CHECK(clean->validationFacts.empty());
}

// ── The diagnostic cap, and the record that has none ────────────────────────
// 진단은 kMaxLayoutDiagnosticsPerParagraph에서 멈추고, 권한 있는 기록은 멈추지
// 않는다. 상한이 기록까지 자르면 패키지 검증이 깨진 byte를 여덟 개까지만 보게
// 되고, 상한이 아예 없으면 라벨 하나가 프레임마다 로그를 채운다.
TEST_CASE("layout diagnostics stop at the cap while validation facts do not") {
    LayoutFixture f = LoadLayoutFixture("recoverable-cache");
    std::string broken;
    for (int index = 0; index < 12; ++index) broken += std::string("A\xFF", 2);
    text::VectorTextDiagnosticSink sink;
    const auto layout = f.service.Layout(f.RequestForBytes(broken), sink);
    REQUIRE(layout);
    CHECK((*layout)->validationFacts.size() == 12U);
    CHECK((*layout)->validationFacts.size() >
          text::kMaxLayoutDiagnosticsPerParagraph);
    CHECK(sink.Diagnostics().size() == text::kMaxLayoutDiagnosticsPerParagraph);
    for (const text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        CHECK(diagnostic.code == text::TextDiagnosticCode::Utf8Invalid);
    }
    // 사실 열두 개는 서로 다른 원본 구간을 가리킨다. 같은 구간으로 접히면
    // 개수만 맞고 어느 byte가 깨졌는지는 잃는다.
    std::vector<std::uint32_t> starts;
    for (const TextValidationFact& fact : (*layout)->validationFacts) {
        starts.push_back(fact.sourceBytes.begin);
    }
    std::sort(starts.begin(), starts.end());
    CHECK(std::unique(starts.begin(), starts.end()) == starts.end());
}

// ── Step 11: pieces inside one BiDi run are reversed, not just reordered ────
// 한 ICU BiDi run 안에 piece가 둘 이상 들어가는 경우만이 run 안쪽 뒤집기를
// 관찰한다. 히브리어와 아랍어는 같은 level 1이지만 script가 달라 서로 다른
// 분석 item이 되므로, 그 둘이 붙어 있는 문단이 정확히 그 상태를 만든다.
TEST_CASE("logical pieces inside one BiDi run are emitted in reverse") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto layout = f.LayoutTextWithFamily(u8"שם سلام", kPrimaryFamily,
                                               Fixed26_6::FromRaw(16 * 64));
    REQUIRE(layout->lines.size() == 1U);
    const text::TextLine& line = layout->lines[0];
    // 세 piece가 전부 같은 level 1이어야 한다. 하나라도 level이 다르면 ICU가
    // run을 나누고, 이 케이스는 run 사이 순서만 재게 된다.
    REQUIRE(line.visualRuns.size() == 3U);
    for (const text::VisualRun& run : line.visualRuns) {
        CHECK(run.bidiLevel == 1U);
        REQUIRE_FALSE(run.glyphs.empty());
    }
    // 논리 순서는 히브리어(byte 0) -> 공백 -> 아랍어. 시각 순서는 그 반대다.
    // run 안의 glyph는 HarfBuzz가 이미 시각 순서로 내므로, run의 논리 자리는
    // 그 glyph들의 최소 byte로 읽는다.
    const auto runStart = [](const text::VisualRun& run) {
        std::uint32_t lowest = run.glyphs.front().glyph.sourceBytes.begin;
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            lowest = std::min(lowest, glyph.glyph.sourceBytes.begin);
        }
        return lowest;
    };
    const std::uint32_t firstVisual = runStart(line.visualRuns[0]);
    const std::uint32_t middleVisual = runStart(line.visualRuns[1]);
    const std::uint32_t lastVisual = runStart(line.visualRuns[2]);
    CHECK(firstVisual > middleVisual);
    CHECK(middleVisual > lastVisual);
    CHECK(lastVisual == 0U);
    // 그리고 왼쪽에서 오른쪽으로 갈수록 원본 offset이 줄어야 한다.
    CHECK(line.visualRuns[0].glyphs.front().origin.x.Raw() <
          line.visualRuns[2].glyphs.front().origin.x.Raw());
}

// Step 13b의 방향 규칙. 자격 폰트에는 grapheme 셋 이상을 덮는 RTL cluster가
// 없고 N=2에서는 두 규칙이 수치로 겹치므로, 규칙을 직접 부른다. N=3에서 두
// 방향이 실제로 다른 답을 낸다는 것이 이 케이스의 전부다.
TEST_CASE("the interior caret boundary rule depends on run direction") {
    const GraphemeRange three{4, 7};
    CHECK(text::detail::InteriorCaretLogicalBoundary(three, 1U, false) == 5U);
    CHECK(text::detail::InteriorCaretLogicalBoundary(three, 2U, false) == 6U);
    CHECK(text::detail::InteriorCaretLogicalBoundary(three, 1U, true) == 6U);
    CHECK(text::detail::InteriorCaretLogicalBoundary(three, 2U, true) == 5U);
    CHECK(text::detail::InteriorCaretLogicalBoundary(three, 1U, false) !=
          text::detail::InteriorCaretLogicalBoundary(three, 1U, true));
    // N=2에서 두 규칙이 겹친다는 사실 자체를 못 박아 둔다. 이것이 위 케이스가
    // 픽스처가 아니라 규칙을 직접 부르는 이유다.
    const GraphemeRange two{4, 6};
    CHECK(text::detail::InteriorCaretLogicalBoundary(two, 1U, false) ==
          text::detail::InteriorCaretLogicalBoundary(two, 1U, true));
}

// ── Why accepting only safe boundaries is legitimate ────────────────────────
// 확정된 줄을 다시 셰이핑한 결과는, 안전한 경계에서만 자르는 한 문단 셰이핑을
// 그 자리에서 잘라 낸 것과 glyph 단위로 같아야 한다. 그것이 HarfBuzz의
// safe-to-break 계약이고, "자르지 말라고 표시된 경계는 후보에서 뺀다"는 Step 6
// 규칙이 정당한 이유다.
//
// 그래서 이 케이스는 unsafe 경계 수용에 대한 두 번째 독립 탐지기다: 어느 줄이든
// HarfBuzz가 금지한 자리에서 끝나는 순간 두 배열이 어긋난다. 값이 같다는 것이
// 출처가 같다는 뜻은 아니다. 출처는 두 관찰이 따로 잡는다:
// FinalLineItemShapeCount는 최종 셰이핑 호출부에서만 올라가고,
// EveryAcceptedLineWasFinalReshaped가 견주는 구간은 ShapeRange가 실제로
// 건네받은 값이라 문단 배열을 잘라 쓰는 구현에는 채울 것이 없다.
TEST_CASE("an accepted line's glyphs agree with the paragraph slice at safe boundaries") {
    LayoutFixture f = LoadLayoutFixture("final-line-fi");
    const auto layout = f.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
    REQUIRE(layout);
    REQUIRE((*layout)->lines.size() >= 2U);
    const auto canonical = [](std::vector<ShapedGlyph> glyphs) {
        std::sort(glyphs.begin(), glyphs.end(),
                  [](const ShapedGlyph& a, const ShapedGlyph& b) {
                      if (a.sourceBytes.begin != b.sourceBytes.begin) {
                          return a.sourceBytes.begin < b.sourceBytes.begin;
                      }
                      return a.glyphId < b.glyphId;
                  });
        std::ostringstream out;
        for (const ShapedGlyph& glyph : glyphs) {
            out << glyph.fontGuid << ':' << glyph.glyphId << ':'
                << glyph.advanceX.Raw() << ':' << glyph.offsetX.Raw() << ':'
                << glyph.sourceBytes.begin << ':' << glyph.sourceBytes.end << ';';
        }
        return out.str();
    };
    for (const text::TextLine& line : (*layout)->lines) {
        std::vector<ShapedGlyph> reshaped;
        for (const text::VisualRun& run : line.visualRuns) {
            for (const text::PositionedGlyph& glyph : run.glyphs) {
                reshaped.push_back(glyph.glyph);
            }
        }
        REQUIRE_FALSE(reshaped.empty());
        const std::vector<ShapedGlyph> sliced =
            f.ParagraphSliceForBytes(line.sourceBytes);
        REQUIRE(sliced.size() == reshaped.size());
        CHECK(canonical(reshaped) == canonical(sliced));
    }
}

// ── Step 1i: registration self-check ────────────────────────────────────────
// 이 실행 파일은 molga_add_text_test가 붙인 세션 하나만 쓴다. 케이스가 자기
// 루트에서 ICU를 다시 올리면 그 순간 다른 프로그램을 재게 되므로, 준비된
// 런타임을 실제로 물려받았다는 사실을 한 번 못 박는다.
TEST_CASE("layout cases run on the shared session runtime") {
    CHECK(text::TextRuntimeDependencies::Get().IsReady());
    CHECK_FALSE(text::TextRuntimeDependencies::Get().WasTerminallyCleaned());
    CHECK_FALSE(
        text::TextRuntimeDependencies::Get().DependencyContractSha256().empty());
}






// ── Step 10/10b: the ellipsis token lands at the truncation point ───────────
// 위의 줄임 케이스들은 "길이 0의 원본 구간을 가리키는 glyph가 하나 있다"만
// 본다. 남긴 글과 줄임표의 순서를 뒤집어도 그 성질은 그대로 참이다 — 그때는
// 진짜 글자 하나가 길이 0으로 잘못 표시되고 줄임표가 남긴 글의 원본 byte를
// 물려받는다. 그래서 (1) 합성 glyph가 정말 줄임표 glyph이고 (2) 확정된 줄
// 전체가 canonical 기대값과 일치한다는 두 가지를 함께 못 박는다.
TEST_CASE("the ellipsized line places the real ellipsis glyph at the cut") {
    LayoutFixture f = LoadLayoutFixture("arabic-ellipsis-context");
    const auto layout = f.LayoutEllipsized();
    REQUIRE(layout);
    REQUIRE((*layout)->lines.size() == 1U);
    const text::TextLine& line = (*layout)->lines[0];

    // 같은 family, 같은 크기로 줄임표만 배치했을 때의 glyph. 이 값이 계약이지
    // 상수 하나가 계약이 아니다.
    const auto solo = f.LayoutTextWithFamily(
        u8"…", kPrimaryFamily, Fixed26_6::FromRaw(f.Spec().fontSizeRaw));
    const ShapedGlyph& ellipsisGlyph = FirstGlyph(*solo);

    std::size_t synthetic = 0;
    for (const text::VisualRun& run : line.visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.sourceBytes.begin != glyph.glyph.sourceBytes.end) {
                // 남긴 glyph는 전부 확정된 줄 안쪽을 가리켜야 한다.
                CHECK(glyph.glyph.sourceBytes.begin >= line.sourceBytes.begin);
                CHECK(glyph.glyph.sourceBytes.end <= line.sourceBytes.end);
                CHECK(glyph.glyph.glyphId != ellipsisGlyph.glyphId);
                continue;
            }
            ++synthetic;
            CHECK(glyph.glyph.glyphId == ellipsisGlyph.glyphId);
            CHECK(glyph.glyph.fontGuid == ellipsisGlyph.fontGuid);
        }
    }
    CHECK(synthetic == 1U);
    CHECK(f.CanonicalJson(**layout) ==
          f.ExpectedCanonicalJson("arabic-ellipsis-context"));
}

// ── Step 10a: the candidate keeps the real paragraph's base direction ───────
// 줄임표 후보는 자기만의 임시 버퍼로 셰이핑되므로, 문단의 기준 방향을 물려주지
// 않으면 잘린 글에서 방향을 다시 유도한다. 문단이 LTR이고 마지막 줄이 히브리어
// 뿐이면 그 차이가 그대로 보인다: 중립인 줄임표가 문단 level 0을 받아 줄의
// 오른쪽 끝에 놓여야 하는데, 후보를 홀로 분석하면 level 1을 받아 왼쪽 끝으로
// 간다. 어느 쪽이든 glyph 수와 "길이 0인 glyph가 하나 있다"는 성질은 같다.
TEST_CASE("an ellipsized RTL tail keeps the LTR paragraph's base direction") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    TextLayoutRequest request = f.MakeRequest(u8"abcdef שלום שלום");
    request.style.wrap        = TextWrapMode::Word;
    request.style.overflow    = TextOverflowMode::Ellipsis;
    request.constraints.width = Fixed26_6::FromRaw(1600);
    const auto layout = f.service.Layout(request, f.sink);
    REQUIRE(layout);
    CHECK((*layout)->ellipsized);
    REQUIRE((*layout)->lines.size() == 3U);
    const text::TextLine& last = (*layout)->lines.back();

    const text::PositionedGlyph* ellipsis = nullptr;
    const text::PositionedGlyph* retained = nullptr;
    for (const text::VisualRun& run : last.visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.sourceBytes.begin == glyph.glyph.sourceBytes.end) {
                ellipsis = &glyph;
            } else {
                retained = &glyph;
            }
        }
    }
    REQUIRE(ellipsis != nullptr);
    REQUIRE(retained != nullptr);
    // 남긴 글자는 히브리어이므로 level 1이고, 줄임표는 문단 level 0이다.
    CHECK(retained->glyph.bidiLevel == 1U);
    CHECK(ellipsis->glyph.bidiLevel == 0U);
    // 그리고 문단이 LTR이므로 줄임표는 시각적으로 오른쪽 끝이다.
    CHECK(ellipsis->origin.x.Raw() > retained->origin.x.Raw());
}

// ── Step 5b: uncovered text inside a family that DOES resolve ───────────────
// 두 갈래를 한 번에 못 박는다. (1) 해석에 성공한 family 안에서도 덮이지 않은
// 구간은 배포를 막는 사실이 된다 — 없는 family는 resolver가 이미 자기 사실을
// 내므로 그 경로만으로는 이 갈래가 죽어도 아무 단언이 움직이지 않는다.
// (2) 서로 떨어진 두 구간은 두 개의 사실로 남는다 — 안정 tuple에서 범위가
// 빠지면 code/severity/message가 같은 두 사실이 하나로 접혀 사라진다.
TEST_CASE("two uncovered runs in a resolved family are two blocking facts") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto layout = f.LayoutTextWithFamily(
        u8"\U0001F680A\U0001F680", kPrimaryFamily, Fixed26_6::FromRaw(16 * 64));
    std::vector<const TextValidationFact*> missing;
    for (const TextValidationFact& fact : layout->validationFacts) {
        if (fact.code == text::TextDiagnosticCode::MissingGlyph) {
            missing.push_back(&fact);
        }
    }
    REQUIRE(missing.size() == 2U);
    CHECK(missing[0]->sourceBytes.begin == 0U);
    CHECK(missing[0]->sourceBytes.end == 4U);
    CHECK(missing[0]->graphemes.begin == 0U);
    CHECK(missing[0]->graphemes.end == 1U);
    CHECK(missing[1]->sourceBytes.begin == 5U);
    CHECK(missing[1]->sourceBytes.end == 9U);
    CHECK(missing[1]->graphemes.begin == 2U);
    CHECK(missing[1]->graphemes.end == 3U);
    for (const TextValidationFact* fact : missing) {
        CHECK(fact->severity == text::TextSeverity::Error);
        CHECK(fact->blocksAuthoredPackage);
        CHECK(fact->recoverableAtRuntime);
    }
    CHECK(HasBlockingAuthoredPackageFact(layout->validationFacts));
    // 그리고 가운데 'A'는 정상적으로 덮인다. 전부 두부가 되었다면 위 두 사실은
    // 구간을 구분하는 힘이 없다.
    bool sawCovered = false;
    bool sawMissing = false;
    REQUIRE(layout->lines.size() == 1U);
    for (const text::VisualRun& run : layout->lines[0].visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.missing) {
                sawMissing = true;
            } else {
                sawCovered = true;
            }
        }
    }
    CHECK(sawMissing);
    CHECK(sawCovered);
}

// ── Step 5: an ill-formed byte's fact, field by field ───────────────────────
// byte offset과 grapheme index가 서로 다른 텍스트여야 한다. 둘이 같으면
// grapheme 범위를 한 칸 밀어도 값이 우연히 맞는다. 진단 message가 byte 범위를
// 문자열로 담고 있으므로, 범위를 실제로 재는 것은 아래 네 CHECK뿐이다.
TEST_CASE("an ill-formed byte's fact carries every field and exact graphemes") {
    LayoutFixture f = LoadLayoutFixture("recoverable-cache");
    text::VectorTextDiagnosticSink sink;
    // "가" 3 bytes + "A" 1 byte + 깨진 byte 하나. 깨진 자리는 byte 4, grapheme 2다.
    const auto layout = f.service.Layout(
        f.RequestForBytes(std::string("\xEA\xB0\x80" "A\xFF", 5)), sink);
    REQUIRE(layout);
    const TextValidationFact* decoded = nullptr;
    for (const TextValidationFact& fact : (*layout)->validationFacts) {
        if (fact.code == text::TextDiagnosticCode::Utf8Invalid) decoded = &fact;
    }
    REQUIRE(decoded != nullptr);
    CHECK(decoded->severity == text::TextSeverity::Error);
    CHECK_FALSE(decoded->subsystem.empty());
    CHECK_FALSE(decoded->message.empty());
    CHECK_FALSE(decoded->remediation.empty());
    CHECK(decoded->sourceBytes.begin == 4U);
    CHECK(decoded->sourceBytes.end == 5U);
    CHECK(decoded->graphemes.begin == 2U);
    CHECK(decoded->graphemes.end == 3U);
    CHECK(decoded->recoverableAtRuntime);
    CHECK(decoded->blocksAuthoredPackage);
}

// ── Step 14/5a: what production actually put in the final key ───────────────
// 최종 키는 저장만 되고 다시 읽히지 않으므로, 필드가 통째로 비어도 어떤 배치도
// 달라지지 않는다. 그래서 프로덕션이 만든 바로 그 키로 캐시를 되짚고, 그 안의
// 값들을 직접 읽는다.
TEST_CASE("the final paragraph key records what production actually shaped") {
    LayoutFixture f = LoadLayoutFixture("final-line-fi");
    const auto layout = f.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
    REQUIRE(layout);
    const auto& observations = text::detail::CurrentLayoutObservations();
    REQUIRE(observations.finalKey);
    const text::TextParagraphCacheKey& key = *observations.finalKey;

    // piece 하나마다 shape key 하나. VisualRun과 piece는 1:1이다.
    std::size_t runs = 0;
    for (const text::TextLine& line : (*layout)->lines) {
        runs += line.visualRuns.size();
    }
    REQUIRE(runs >= 2U);
    CHECK(key.finalLineShapeKeys.size() == runs);

    // 저장된 그 키로 다시 찾으면 같은 불변 배치가 나오고, 한 필드만 달라지면
    // 나오지 않는다. 뒤쪽이 없으면 FindByFinal이 무엇이든 돌려줘도 통과한다.
    CHECK(f.cache.FindByFinal(key) == *layout);
    text::TextParagraphCacheKey other = key;
    other.visualRevision = key.visualRevision + 1U;
    CHECK(f.cache.FindByFinal(other) == nullptr);
    CHECK(key.overlongTokenPolicy ==
          text::OverlongTokenPolicy(TextWrapMode::Grapheme,
                                    TextOverflowMode::Overflow));

    std::size_t beginningOfText = 0;
    std::size_t endOfText       = 0;
    for (const text::TextShapeCacheKey& shape : key.finalLineShapeKeys) {
        // Step 5a: 요청 locale이 아니라 ICU가 실제로 고른 locale이다. 이
        // 픽스처는 "und"를 요청하고 ICU는 root를 고르므로 둘이 다르다.
        CHECK(shape.componentLocale == Label(f.Spec().locale));
        CHECK(shape.resolvedGraphemeLocale == Label("root"));
        CHECK(shape.resolvedLineBreakLocale == Label("root"));
        CHECK_FALSE(shape.graphemeRuleIdentity.empty());
        CHECK_FALSE(shape.lineBreakRuleIdentity.empty());
        CHECK(shape.analysisGeneration != 0U);
        // 두 hash는 각자의 바이트에서 온다. 한쪽이 다른 쪽의 바이트로 채워지면
        // 두 값이 같아진다 — 이 픽스처에서는 셰이핑 입력이 문단보다 짧다.
        CHECK(shape.shapeInputUtf8.size() < shape.originalUtf8.size());
        CHECK(shape.originalBytesHash ==
              text::CacheBytesHash(shape.originalUtf8));
        CHECK(shape.shapeInputBytesHash ==
              text::CacheBytesHash(shape.shapeInputUtf8));
        CHECK(shape.shapeInputBytesHash != shape.originalBytesHash);
        // BOT/EOT는 이 셰이핑 호출이 텍스트의 끝에 닿는가만 말한다. 문단이 여러
        // 줄이고 첫 줄과 마지막 줄이 여럿의 piece로 나뉘므로, 참인 자리는
        // 정확히 하나씩이다.
        if (shape.boundaries.beginningOfText) ++beginningOfText;
        if (shape.boundaries.endOfText) ++endOfText;
        CHECK(((shape.harfbuzzBufferFlags &
                static_cast<std::uint32_t>(HB_BUFFER_FLAG_BOT)) != 0U) ==
              shape.boundaries.beginningOfText);
        CHECK(((shape.harfbuzzBufferFlags &
                static_cast<std::uint32_t>(HB_BUFFER_FLAG_EOT)) != 0U) ==
              shape.boundaries.endOfText);
    }
    CHECK(beginningOfText == 1U);
    CHECK(endOfText == 1U);
}

// ── Step 9 + Task 7.1 handoff: the request key's contract values ────────────
// 저장과 조회가 같은 함수로 만들어지므로 왕복만으로는 값이 옳은지 알 수 없다.
// Task 7.1의 픽스처가 리터럴 "78.1"을 넣고도 아무 시험이 실패하지 않았던 이유가
// 정확히 그것이다.
TEST_CASE("the request index key carries this process's ICU contract values") {
    LayoutFixture f = LoadLayoutFixture("recoverable-cache");
    const auto layout = f.LayoutText(u8"A");
    REQUIRE(layout != nullptr);
    const auto& observations = text::detail::CurrentLayoutObservations();
    REQUIRE(observations.requestKey);
    const text::TextLayoutRequestIndexKey& key = *observations.requestKey;

    CHECK(key.icuRevision == Label(U_ICU_VERSION));
    CHECK(key.icuDataSha256 == Label(text::kPackagedIcuDataSha256));
    CHECK(key.harfbuzzRevision == Label(hb_version_string()));
    CHECK(key.dependencyContractSha256 ==
          text::TextRuntimeDependencies::Get().DependencyContractSha256());
    // 두 규칙 정책 정체성은 위 두 값에서 나온다. 리터럴을 넣으면 여기서 어긋난다.
    CHECK(key.requestedGraphemeRulePolicyIdentity ==
          text::RequestedGraphemeRulePolicyIdentity(key.icuRevision,
                                                    key.icuDataSha256));
    CHECK(key.requestedLineBreakRulePolicyIdentity ==
          text::RequestedLineBreakRulePolicyIdentity(key.icuRevision,
                                                     key.icuDataSha256));
    CHECK(key.requestedGraphemeRulePolicyIdentity !=
          key.requestedLineBreakRulePolicyIdentity);
    // 그리고 원본 바이트 hash는 실제 바이트에서 온다.
    CHECK(key.originalUtf8 == Label("A"));
    CHECK(key.originalBytesHash == text::CacheBytesHash(key.originalUtf8));
    CHECK(key.originalBytesHash != text::CacheBytesHash(std::string_view("B")));
}

// ── The overlong-token policy vocabulary ────────────────────────────────────
// 캐시 헤더가 이 어휘를 Task 7.2에 맡긴 이유는 하나다: 서로 다른 두 정책이 같은
// 철자를 쓰면 서로 다른 배치가 한 캐시 항목을 공유한다. 그래서 "네 철자가 서로
// 다르다"와 "각 조합이 자기 철자를 낸다"를 함께 본다.
TEST_CASE("every wrap and overflow pair names its own overlong token policy") {
    using text::OverlongTokenPolicy;
    const std::string breakGrapheme = text::kOverlongTokenPolicyBreakGrapheme;
    const std::string overflow      = text::kOverlongTokenPolicyOverflow;
    const std::string clip          = text::kOverlongTokenPolicyClip;
    const std::string ellipsis      = text::kOverlongTokenPolicyEllipsis;
    std::vector<std::string> spellings{breakGrapheme, overflow, clip, ellipsis};
    for (const std::string& spelling : spellings) CHECK_FALSE(spelling.empty());
    std::sort(spellings.begin(), spellings.end());
    CHECK(std::unique(spellings.begin(), spellings.end()) == spellings.end());

    for (const TextWrapMode wrap : {TextWrapMode::NoWrap, TextWrapMode::Word}) {
        CHECK(OverlongTokenPolicy(wrap, TextOverflowMode::Overflow) == overflow);
        CHECK(OverlongTokenPolicy(wrap, TextOverflowMode::Clip) == clip);
        CHECK(OverlongTokenPolicy(wrap, TextOverflowMode::Ellipsis) == ellipsis);
    }
    // grapheme 줄바꿈은 과장된 토큰을 실제로 쪼개므로 세 과장 정책과 다른
    // 정책이고, 그래서 세 overflow 모드를 하나의 철자로 접는다.
    for (const TextOverflowMode mode :
         {TextOverflowMode::Overflow, TextOverflowMode::Clip,
          TextOverflowMode::Ellipsis}) {
        CHECK(OverlongTokenPolicy(TextWrapMode::Grapheme, mode) ==
              breakGrapheme);
    }
}

// ── Step 4: nullopt means "no artifact store", not "no family" ──────────────
// Task 5.1의 계약이다. 이 갈래가 없으면 disengaged optional을 역참조하게 되고,
// 그 UB는 "폰트가 없다"처럼 보이는 절차적 두부로 조용히 위장된다.
TEST_CASE("layout fails closed when no font artifact store is bound") {
    molga::AssetDatabase database;
    text::FontRepository repository(database);
    text::FontFamilyResolver resolver(database, repository);
    text::TextShapingService shaper;
    text::TextLayoutCache cache(text::TextLayoutCacheLimits::Production());
    text::TextLayoutService service(resolver, shaper, cache);
    text::VectorTextDiagnosticSink sink;

    TextLayoutRequest request;
    request.utf8                 = u8"A";
    request.style.fontFamilyGuid = kPrimaryFamily;
    request.style.shape.fontSize = Fixed26_6::FromRaw(16 * 64);
    const auto layout = service.Layout(request, sink);
    CHECK_FALSE(layout.has_value());
    REQUIRE(sink.Diagnostics().size() == 1U);
    CHECK(sink.Diagnostics()[0].code ==
          text::TextDiagnosticCode::DependencyInvalid);
    CHECK(sink.Diagnostics()[0].severity == text::TextSeverity::Error);
}

// ── Step 9: a height smaller than the first line still keeps one line ───────
// 0줄짜리 배치는 caret이 설 자리조차 없고, 그 상태로 계속 가면 빈 벡터의
// back()을 읽게 된다.
TEST_CASE("a height below the first line still keeps exactly one clipped line") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto full = f.LayoutText(u8"A\nB\nC");
    REQUIRE(full->lines.size() == 3U);
    REQUIRE(full->lines[0].bottom.Raw() > 1);

    TextLayoutRequest tiny = f.MakeRequest(u8"A\nB\nC");
    tiny.constraints.height =
        Fixed26_6::FromRaw(full->lines[0].bottom.Raw() - 1);
    const auto clipped = f.service.Layout(tiny, f.sink);
    REQUIRE(clipped);
    REQUIRE((*clipped)->lines.size() == 1U);
    CHECK((*clipped)->clipped);
    CHECK((*clipped)->lines[0].graphemes.end == full->lines[0].graphemes.end);
}

// ── Step 13a: the whole GDEF set is accepted or the whole set is dropped ────
// 자격 폰트가 내는 집합은 언제나 정확히 N-1개이고 advance 안쪽이라, 픽스처로는
// 규칙의 잘못된 쪽을 한 번도 밟지 못한다. 그래서 규칙 자체를 직접 부른다.
TEST_CASE("a usable GDEF caret set is exactly N-1 positions inside the advance") {
    using text::detail::AdjustedGdefCaretSetIsUsable;
    const auto raw = [](std::int32_t value) { return Fixed26_6::FromRaw(value); };
    const Fixed26_6 advance = raw(300);

    CHECK(AdjustedGdefCaretSetIsUsable({raw(100), raw(200)}, 3U, advance));
    CHECK(AdjustedGdefCaretSetIsUsable({raw(150)}, 2U, advance));
    // 개수가 하나 모자라거나 하나 넘치면 집합 전체를 버린다.
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(100)}, 3U, advance));
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(50), raw(100), raw(200)}, 3U,
                                             advance));
    // 순서가 어긋나거나 같은 자리가 두 번 나오면 버린다.
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(200), raw(100)}, 3U, advance));
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(100), raw(100)}, 3U, advance));
    // 0과 advance는 caret 자리가 아니라 cluster의 양 끝이다.
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(0), raw(200)}, 3U, advance));
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(100), raw(300)}, 3U, advance));
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(100), raw(400)}, 3U, advance));
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({raw(-10), raw(100)}, 3U, advance));
    // grapheme 하나짜리 glyph에는 내부 caret이 없다.
    CHECK_FALSE(AdjustedGdefCaretSetIsUsable({}, 1U, advance));
}

// ── Steps 7/11: Indic contextual reordering inside one LTR run ──────────────
// 아랍어/히브리어의 재배열은 run을 통째로 뒤집는 것이고, 그 방향은 별도의
// bidiLevel이 들고 있다. 데바나가리의 pre-base 매트라는 그것과 다르다: LTR run
// 안에서, 뒤에 오는 문자의 glyph가 자기 앞 자음의 glyph보다 먼저 그려진다.
TEST_CASE("a Devanagari cluster draws its pre-base matra before the consonant") {
    LayoutFixture f = LoadLayoutFixture("devanagari-reorder");
    const auto em = Fixed26_6::FromRaw(16 * 64);
    const auto consonant = f.LayoutTextWithFamily(u8"ह", kPrimaryFamily, em);
    const ShapedGlyph& alone = FirstGlyph(*consonant);
    CHECK_FALSE(alone.missing);
    REQUIRE(consonant->lines.size() == 1U);
    REQUIRE(consonant->lines[0].visualRuns.size() == 1U);
    // 자음 하나만 있을 때는 glyph도 하나다: 아래 첫 glyph는 문맥이 만들어 낸
    // 것이지 자음이 언제나 둘로 그려지는 것이 아니다.
    CHECK(consonant->lines[0].visualRuns[0].glyphs.size() == 1U);

    const auto combined = f.LayoutTextWithFamily(u8"हि", kPrimaryFamily, em);
    REQUIRE(combined->lines.size() == 1U);
    REQUIRE(combined->lines[0].visualRuns.size() == 1U);
    const std::vector<text::PositionedGlyph>& glyphs =
        combined->lines[0].visualRuns[0].glyphs;
    REQUIRE(glyphs.size() == 2U);
    // LTR run이므로 시각 순서는 배열 순서다. 자음은 원본에서 먼저지만 두 번째로
    // 그려진다 — 그것이 재배열이다.
    CHECK(glyphs[0].glyph.bidiLevel == 0U);
    CHECK(glyphs[0].glyph.glyphId != alone.glyphId);
    CHECK(glyphs[1].glyph.glyphId == alone.glyphId);
    CHECK(glyphs[0].origin.x.Raw() < glyphs[1].origin.x.Raw());
    // 두 glyph는 한 cluster이므로 같은 원본 구간과 같은 grapheme을 가리킨다.
    CHECK(glyphs[0].glyph.sourceBytes.begin == 0U);
    CHECK(glyphs[0].glyph.sourceBytes.end == 6U);
    CHECK(glyphs[1].glyph.sourceBytes.begin == 0U);
    CHECK(glyphs[1].glyph.sourceBytes.end == 6U);
    CHECK(glyphs[0].glyph.graphemes.end - glyphs[0].glyph.graphemes.begin == 1U);
}

// 같은 문자열을 실제로 줄바꿈해 본다. 재배열이 일어나는 script에서도 확정된
// 줄은 그 줄을 다시 셰이핑한 결과이고, 안전한 경계에서 자른 이상 문단 배열의
// 그 구간과 glyph 개수가 같아야 한다.
TEST_CASE("a wrapped Devanagari paragraph reshapes every accepted line") {
    LayoutFixture f = LoadLayoutFixture("devanagari-reorder");
    const auto layout = f.LayoutAtWidth(Fixed26_6::FromRaw(f.Spec().widthRaw));
    REQUIRE(layout);
    REQUIRE((*layout)->lines.size() >= 2U);
    CHECK(f.EveryAcceptedLineWasFinalReshaped());
    CHECK(f.shaper.ParagraphShapeCount() == 1);
    CHECK(f.shaper.FinalLineItemShapeCount() >= (*layout)->lines.size());
    for (const text::TextLine& line : (*layout)->lines) {
        CHECK(line.advance.Raw() <= f.Spec().widthRaw);
        std::vector<ShapedGlyph> reshaped;
        for (const text::VisualRun& run : line.visualRuns) {
            for (const text::PositionedGlyph& glyph : run.glyphs) {
                CHECK_FALSE(glyph.glyph.missing);
                reshaped.push_back(glyph.glyph);
            }
        }
        REQUIRE_FALSE(reshaped.empty());
        CHECK(f.ParagraphSliceForBytes(line.sourceBytes).size() ==
              reshaped.size());
    }
}

// ── Step 12c: every UAX#14 mandatory break starts a new display line ────────
// 지금까지 모든 픽스처가 '\n' 하나만 쓴다. 나머지 아홉 문자는 하나씩 지워도
// 아무 단언이 움직이지 않는다. U+001C..U+001E는 Bidi_Class B이기도 해서, 여기서
// 빠지면 한 표시 줄이 두 ICU 문단에 걸치고 ubidi_setLine이 거절한다.
TEST_CASE("every UAX#14 mandatory break separates two display lines") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const std::vector<std::string> separators{
        "\n",         "\v",         "\f",         "\r",   "\x1C", "\x1D",
        "\x1E",       u8"\u0085",   u8"\u2028",   u8"\u2029", "\r\n"};
    for (const std::string& separator : separators) {
        const auto layout = f.LayoutText("A" + separator + "B");
        REQUIRE_MESSAGE(layout->lines.size() == 2U,
                        ("separator byte " +
                         std::to_string(static_cast<unsigned>(
                             static_cast<unsigned char>(separator.front())))));
        CHECK(layout->lines[0].sourceBytes.begin == 0U);
        CHECK(layout->lines[0].sourceBytes.end == 1U);
        // 구분자 grapheme 자체는 어느 표시 줄에도 속하지 않는다.
        CHECK(layout->lines[1].sourceBytes.begin ==
              1U + static_cast<std::uint32_t>(separator.size()));
        CHECK(layout->lines[1].sourceBytes.end ==
              2U + static_cast<std::uint32_t>(separator.size()));
    }
    // 그리고 구분자가 아닌 문자는 줄을 나누지 않는다. 이 반대편이 없으면
    // 판정을 언제나 참으로 만들어도 위 단언이 통과한다.
    CHECK(f.LayoutText(u8"A\tB")->lines.size() == 1U);
    CHECK(f.LayoutText(u8"A B")->lines.size() == 1U);
}

// ── Step 8: equality is not "exceeds" ───────────────────────────────────────
// 되짚기는 "권한 있는 줄 advance가 제약을 넘을 때"만 돈다. 비교를 엄격하게
// 만들면 폭에 정확히 들어맞는 줄이 쓸데없이 한 후보 짧아지는데, 그 줄도
// 여전히 제약 안이라 폭 단언은 전부 통과한다.
TEST_CASE("a line whose advance equals the constraint is not shortened") {
    LayoutFixture wide = LoadLayoutFixture("final-line-fi");
    const auto roomy = wide.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
    REQUIRE(roomy);
    REQUIRE((*roomy)->lines.size() >= 2U);
    const std::int32_t exact = (*roomy)->lines[0].advance.Raw();
    REQUIRE(exact > 0);
    REQUIRE(exact < 9 * 64);
    const std::uint32_t end = (*roomy)->lines[0].graphemes.end;
    REQUIRE(end > 1U);

    LayoutFixture tight = LoadLayoutFixture("final-line-fi");
    const auto snug = tight.LayoutAtWidth(Fixed26_6::FromRaw(exact));
    REQUIRE(snug);
    REQUIRE_FALSE((*snug)->lines.empty());
    CHECK((*snug)->lines[0].advance.Raw() == exact);
    CHECK((*snug)->lines[0].graphemes.end == end);
}

// ── The authoritative record has no hidden cap ──────────────────────────────
// UnicodeTextBuffer는 잘못된 최대 부분열마다 진단을 하나씩 내고 상한을 두지
// 않는다 — 그 헤더가 적어 둔 대로 상한은 문맥을 아는 호출자인 배치의 몫이고,
// 배치는 진단에만 상한을 두고 기록에는 두지 않는다(Step 14b). 위의 열두 개짜리
// 케이스는 "8보다 크다"만 말하므로 열여섯쯤에서 조용히 잘리는 두 번째 상한을
// 통과시킨다. 저작 규모의 손상을 통째로 통과시켜 그 자리를 막는다.
TEST_CASE("the validation record keeps one fact per damaged byte at scale") {
    LayoutFixture f = LoadLayoutFixture("recoverable-cache");
    constexpr int kDamaged = 2000;
    std::string broken;
    broken.reserve(2U * static_cast<std::size_t>(kDamaged));
    for (int index = 0; index < kDamaged; ++index) broken += std::string("A\xFF", 2);
    text::VectorTextDiagnosticSink sink;
    const auto layout = f.service.Layout(f.RequestForBytes(broken), sink);
    REQUIRE(layout);
    CHECK((*layout)->validationFacts.size() ==
          static_cast<std::size_t>(kDamaged));
    CHECK(sink.Diagnostics().size() == text::kMaxLayoutDiagnosticsPerParagraph);
    // 그리고 접히지 않았다: 사실 하나가 깨진 byte 하나씩을 정확히 가리킨다.
    std::vector<std::uint32_t> starts;
    starts.reserve((*layout)->validationFacts.size());
    for (const TextValidationFact& fact : (*layout)->validationFacts) {
        starts.push_back(fact.sourceBytes.begin);
    }
    std::sort(starts.begin(), starts.end());
    CHECK(std::unique(starts.begin(), starts.end()) == starts.end());
}
