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
#include "Text/TextHitTesting.h"
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
#include <array>
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
using text::CaretAffinity;
using text::CaretPosition;
using text::CaretStop;
using text::GraphemeRange;
using text::ShapedGlyph;
using text::ShapedRun;
using text::SourceByteRange;
using text::TextHitTesting;
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
        // Task 7.3. 히브리어 뒤의 숫자는 RTL 문단 안에서 level 2가 되므로,
        // grapheme 경계 4는 논리적으로 한 자리이면서 시각적으로 두 자리다:
        // 숫자 run의 왼쪽 끝과 히브리 run의 왼쪽 끝이 서로 다른 x에 있다.
        // 방향이 바뀌지 않는 경계에서는 두 자리가 겹쳐 하나로 접히므로,
        // affinity가 뜻을 갖는 곳은 정확히 이런 경계뿐이다.
        {"hebrew-number-boundary", u8"שלום123", "he", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        // 정확한 중점 하나만을 위한 최소 문단. glyph가 하나뿐이라 caret 자리가
        // 정확히 둘이고, 그 둘 사이의 중점은 나머지 없이 떨어진다.
        // 18em인 이유는 하나다: 이 크기에서 두 픽스처의 advance가 모두 짝수라
        // 중점이 나머지 없이 떨어진다. 16em에서는 히브리 alef의 advance가
        // 홀수라 "정확한 중점"이 1/64 치우친 점이 되어 버린다.
        {"ltr-midpoint", u8"A", "en", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 18 * 64, 0, 0},
        // 같은 모양의 RTL 짝. 물리 규칙(동점은 큰 x로)이 같아도 논리 경계는
        // 반대로 나와야 한다 — LTR 전용 픽스처로는 그 차이를 볼 수 없다.
        {"rtl-midpoint", u8"א", "he", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 18 * 64, 0, 0},
        // grapheme 셋을 삼킨 glyph 하나. NotoSans는 이 합자에 GDEF caret을
        // 싣고 있으므로 저장된 자리만으로 내부 caret 둘이 서야 한다.
        {"latin-ffi-ligature", u8"ffi", "en", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        // 논리적으로 이어진 선택 하나가 시각적으로는 둘로 갈라지는 문단.
        // 선택은 히브리 run의 뒤쪽 넷과 뒤따르는 라틴 셋을 덮고, 선택되지 않은
        // 히브리 첫 글자가 그 둘 사이에 시각적으로 끼어든다.
        {"mixed-bidi-selection", u8"aאבגדהbcd", "und", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        // 합자 뒤에 결합 문자가 붙은 문단. HarfBuzz는 MONOTONE_CHARACTERS에서
        // 표식의 cluster를 합자 cluster에 합치지 않으므로, 표식의 grapheme
        // 범위가 합자 범위의 "같지는 않은 부분집합"으로 도착한다. 순수 LTR
        // 단일 run에서 가짜 affinity 쌍이 생길 수 있는 유일한 길이다.
        // U+0335는 보이지 않는 문자라 이스케이프로 적는다.
        {"latin-ligature-mark", u8"ffi\u0335", "en", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
        // RTL run 안의 합자. 라틴 합자로는 방향에 따른 모서리 뒤바뀜을 잴 수
        // 없다 — lam-alef는 GDEF caret을 싣고 있어 내부 자리까지 함께 잰다.
        {"arabic-ligature", u8"أهلا", "ar", TextWrapMode::NoWrap,
         TextOverflowMode::Overflow, 16 * 64, 0, 0},
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

// 이 파일 뒤쪽에서 정의된다. 픽스처가 합자 안쪽의 점을 만들 때 쓰는데,
// 픽스처가 먼저 선언되어야 verbatim 블록이 부르는 이름들이 맞물린다.
const text::PositionedGlyph& FirstMultiGraphemeGlyph(
    const text::TextLayout& layout);

// ── Caret / hit-test observation helpers (Task 7.3) ─────────────────────────
// 하나의 논리 경계가 갖는 시각 자리 전부. BiDi 경계가 아니면 하나, 방향이
// 바뀌는 경계에서는 둘이다.
std::vector<CaretStop> StopsAtBoundary(const text::TextLayout& layout,
                                       std::uint32_t boundary) {
    std::vector<CaretStop> stops;
    for (const CaretStop& stop : layout.caretStops) {
        if (stop.logicalGraphemeBoundary == boundary) stops.push_back(stop);
    }
    return stops;
}

// 요구된 (경계, affinity)의 자리를 정확히 하나 찾는다. 없거나 여럿이면 곧바로
// 실패한다: 기대값을 만드는 쪽이 조용히 다른 자리를 집으면 아래 비교가
// 무의미해진다.
Fixed26_6 CaretStopX(const text::TextLayout& layout, std::uint32_t boundary,
                     CaretAffinity affinity) {
    std::vector<Fixed26_6> found;
    for (const CaretStop& stop : layout.caretStops) {
        if (stop.logicalGraphemeBoundary != boundary) continue;
        if (stop.affinity != affinity) continue;
        found.push_back(stop.position.x);
    }
    REQUIRE(found.size() == 1U);
    return found.front();
}

// 두 caret 자리 사이의 정확한 중점. 이 픽스처들이 glyph 하나짜리인 이유가
// 이것이다 — 자리가 둘뿐이라 "정확한 중점"이 유일하게 정해진다.
molga::FixedPoint ExactMidpoint(const text::TextLayout& layout) {
    REQUIRE(layout.lines.size() == 1U);
    REQUIRE(layout.caretStops.size() == 2U);
    const std::int32_t low = layout.caretStops[0].position.x.Raw();
    const std::int32_t high = layout.caretStops[1].position.x.Raw();
    REQUIRE(low < high);
    // 나머지가 있으면 그 점은 중점이 아니라 1/64만큼 한쪽으로 치우친 점이고,
    // 그러면 이 케이스는 동점 규칙이 아니라 반올림 방향을 재게 된다.
    REQUIRE((high - low) % 2 == 0);
    molga::FixedPoint point;
    point.x = Fixed26_6::FromRaw(low + (high - low) / 2);
    point.y = layout.lines.front().baseline;
    return point;
}

// 내부 caret이 하나도 없으면 참이 아니다. 합자를 통째로 잃은 회귀가 "모두
// GDEF에서 왔다"로 조용히 통과하면 이 단언은 아무것도 뜻하지 않는다.
bool AllInteriorCaretsCameFromAdjustedGdef(const text::TextLayout& layout) {
    std::size_t seen = 0;
    for (const text::TextLine& line : layout.lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            for (const text::PositionedGlyph& glyph : run.glyphs) {
                for (const text::GlyphInteriorCaret& caret :
                     glyph.interiorCarets) {
                    ++seen;
                    if (!caret.fromAdjustedGdef) return false;
                }
            }
        }
    }
    return seen > 0;
}

std::vector<std::uint32_t> CaretBoundarySequence(const text::TextLayout& layout) {
    std::vector<std::uint32_t> boundaries;
    boundaries.reserve(layout.caretStops.size());
    for (const CaretStop& stop : layout.caretStops) {
        boundaries.push_back(stop.logicalGraphemeBoundary);
    }
    return boundaries;
}

bool CaretXPositionsAreStrictlyIncreasing(const text::TextLayout& layout) {
    // 자리는 줄 단위로 묶여 있고 x는 줄 안에서만 단조롭다. 여러 줄 배치에서는
    // 이 술어가 옳고 그름을 말하지 못하므로 아예 참이 될 수 없게 못 박는다 —
    // 이름만 보고 줄바꿈된 문단에 들이대면 이유 없는 빨강이 나온다.
    if (layout.lines.size() != 1U) return false;
    if (layout.caretStops.size() < 2U) return false;
    for (std::size_t index = 1; index < layout.caretStops.size(); ++index) {
        if (layout.caretStops[index].position.x.Raw() <=
            layout.caretStops[index - 1U].position.x.Raw()) {
            return false;
        }
    }
    return true;
}

// 모든 자리의 y는 자기 줄의 baseline이다. caret은 줄 위에 서므로 한 줄 안에서
// 높이가 갈릴 이유가 없고, 이 값은 Milestone 12-14의 IME/caret 소비자가 읽는
// 공개 출력이다 — 아무도 읽지 않는 필드는 조용히 0이 되어도 드러나지 않는다.
bool CaretYPositionsAreLineBaselines(const text::TextLayout& layout) {
    if (layout.caretStops.empty()) return false;
    for (const CaretStop& stop : layout.caretStops) {
        if (stop.lineIndex >= layout.lines.size()) return false;
        if (stop.position.y.Raw() !=
            layout.lines[stop.lineIndex].baseline.Raw()) {
            return false;
        }
    }
    return true;
}

// 하나의 (경계, affinity) 짝에는 시각 자리가 많아야 하나다. 이것이 성립해야
// CaretRects의 반환값이 "caret 하나"이지 "후보 자루"가 아니게 된다 — 두 자리가
// 같은 짝을 달고 나오면 소비자는 어느 쪽을 그려야 하는지 물어볼 곳이 없다.
bool EveryCaretPositionResolvesToOneStop(const text::TextLayout& layout) {
    if (layout.caretStops.empty()) return false;
    for (std::size_t a = 0; a < layout.caretStops.size(); ++a) {
        for (std::size_t b = a + 1U; b < layout.caretStops.size(); ++b) {
            if (layout.caretStops[a].logicalGraphemeBoundary !=
                layout.caretStops[b].logicalGraphemeBoundary) {
                continue;
            }
            if (layout.caretStops[a].affinity != layout.caretStops[b].affinity) {
                continue;
            }
            return false;
        }
    }
    return true;
}

// caret은 grapheme 경계에만 선다. 배치 결과만으로 확인할 수 있는 형태는 두
// 가지다: 모든 자리가 자기 줄의 grapheme 범위 안이고, 한 줄이 갖는 서로 다른
// 경계 값의 수가 grapheme 수 + 1을 넘지 않는다. 뒤쪽이 실제 관찰이다 —
// scalar/UTF-16 단위마다 caret을 내는 구현은 결합 문자나 ZWJ가 있는 grapheme
// 하나에서 곧바로 그 상한을 넘긴다(그래서 데바나가리 케이스가 따로 있다).
bool NoCaretInsideUtf16ScalarOrGrapheme(const text::TextLayout& layout) {
    if (layout.caretStops.empty()) return false;
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        const text::TextLine& line = layout.lines[index];
        std::vector<std::uint32_t> distinct;
        for (const CaretStop& stop : layout.caretStops) {
            if (stop.lineIndex != index) continue;
            if (stop.logicalGraphemeBoundary < line.graphemes.begin) return false;
            if (stop.logicalGraphemeBoundary > line.graphemes.end) return false;
            distinct.push_back(stop.logicalGraphemeBoundary);
        }
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()),
                       distinct.end());
        if (distinct.size() >
            static_cast<std::size_t>(line.graphemes.end - line.graphemes.begin) +
                1U) {
            return false;
        }
    }
    return true;
}

// 26.6 raw 넷. float로 견주면 1/64 어긋난 사각형이 같다고 나온다.
using CanonicalRect = std::array<std::int32_t, 4>;

std::vector<CanonicalRect> CanonicalFixedRects(
    const std::vector<molga::FixedRect>& rects) {
    std::vector<CanonicalRect> canonical;
    canonical.reserve(rects.size());
    for (const molga::FixedRect& rect : rects) {
        canonical.push_back(CanonicalRect{rect.x.Raw(), rect.y.Raw(),
                                          rect.width.Raw(), rect.height.Raw()});
    }
    return canonical;
}

// 위에서 아래로, 한 줄 안에서는 왼쪽에서 오른쪽으로, 겹침 없이.
bool RectsAreInStableVisualOrder(const std::vector<molga::FixedRect>& rects) {
    if (rects.empty()) return false;
    for (const molga::FixedRect& rect : rects) {
        if (rect.width.Raw() <= 0 || rect.height.Raw() <= 0) return false;
    }
    for (std::size_t index = 1; index < rects.size(); ++index) {
        const molga::FixedRect& previous = rects[index - 1U];
        const molga::FixedRect& next = rects[index];
        if (next.y.Raw() > previous.y.Raw()) continue;
        if (next.y.Raw() < previous.y.Raw()) return false;
        if (next.x.Raw() < previous.x.Raw() + previous.width.Raw()) return false;
    }
    return true;
}

// caret 자리 하나를 통째로 담는 안정된 표현. 자리를 x만으로 견주면 affinity나
// 줄 번호가 바뀌는 회귀가 그대로 통과한다.
using CanonicalStop = std::array<std::int32_t, 5>;

std::vector<CanonicalStop> CanonicalCaretStops(const text::TextLayout& layout) {
    std::vector<CanonicalStop> canonical;
    canonical.reserve(layout.caretStops.size());
    for (const CaretStop& stop : layout.caretStops) {
        canonical.push_back(CanonicalStop{
            static_cast<std::int32_t>(stop.logicalGraphemeBoundary),
            static_cast<std::int32_t>(stop.affinity), stop.position.x.Raw(),
            stop.position.y.Raw(), static_cast<std::int32_t>(stop.lineIndex)});
    }
    return canonical;
}

// ── The layout fixture ──────────────────────────────────────────────────────
class LayoutFixture {
public:
    // Task 7.3의 verbatim 블록 하나가 픽스처를 `const auto`로 잡은 채
    // Layout()/ExpectedSelectionRects()를 부른다. 배치 자체는 관찰 상태를
    // 남기므로 논리적으로 const가 아니고, 그 사실을 감추는 const_cast 대신
    // 변이하는 멤버만 mutable로 적어 둔다.
    mutable text::VectorTextDiagnosticSink sink;
    LayoutShapeObserver shaper;
    text::TextShapingService shapingService;
    text::TextLayoutCache cache{text::TextLayoutCacheLimits::Production()};
    mutable text::TextLayoutService service;

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

    // 이 픽스처 이름이 고정한 문단을 그대로 배치한다.
    std::shared_ptr<const text::TextLayout> Layout() const {
        return Required(service.Layout(MakeRequest(spec_.utf8), sink));
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

    // hit test는 셰이퍼를 다시 부르지 않는다. 계수기를 0으로 되돌린 뒤
    // caret/선택 API를 전부 부르는 것이 그 주장의 관찰이다.
    //
    // Task 8.2 Step 8a.1: face 자원 쪽 계수기는 함께 사라졌다. 그것이 세던
    // FontFace::Advance/Kerning이 더 이상 존재하지 않으므로, "hit test가
    // advance/kerning을 다시 묻지 않는다"는 이제 컴파일러가 지킨다.
    void ResetFontAndShaperCounters() {
        text::detail::ResetHarfBuzzObjectCreationCount();
    }

    // ── Task 7.3 fixture geometry ───────────────────────────────────────────
    // 합자 '안쪽'의 점. 두 내부 caret 사이를 고르는 이유는 하나다: glyph 앞
    // 모서리를 고르면 경계 0이 나와 "0보다 크다"가 조용히 통과한다.
    molga::FixedPoint PointInsideLigature() const {
        REQUIRE(lastLayout_ != nullptr);
        const text::PositionedGlyph& ligature =
            FirstMultiGraphemeGlyph(*lastLayout_);
        REQUIRE(ligature.interiorCarets.size() >= 2U);
        const std::int32_t left = ligature.interiorCarets[0].position.x.Raw();
        const std::int32_t right = ligature.interiorCarets[1].position.x.Raw();
        REQUIRE(left > ligature.origin.x.Raw());
        REQUIRE(left < right);
        molga::FixedPoint point;
        point.x = Fixed26_6::FromRaw(left + (right - left) / 2);
        point.y = ligature.origin.y;
        return point;
    }

    // 기대되는 선택 사각형을 SelectionRects가 아니라 caret 정지 자리에서
    // 만든다. 같은 코드로 두 번 계산한 값끼리 견주면 그 비교는 언제나 참이다.
    //
    // 이 픽스처에서 히브리 run의 선택된 부분은 경계 6(run의 왼쪽 끝)에서
    // 경계 2(오른쪽 끝)까지이고, 뒤따르는 라틴 run은 경계 6에서 9까지다.
    // 같은 경계 6이 서로 다른 두 x를 갖는다는 것이 이 픽스처의 전부다.
    std::vector<CanonicalRect> ExpectedSelectionRects() const {
        REQUIRE(Label(spec_.name) == Label("mixed-bidi-selection"));
        REQUIRE(lastLayout_ != nullptr);
        REQUIRE(lastLayout_->lines.size() == 1U);
        const text::TextLine& line = lastLayout_->lines.front();
        const std::int32_t hebrewLeft =
            CaretStopX(*lastLayout_, 6, CaretAffinity::Upstream).Raw();
        const std::int32_t hebrewRight =
            CaretStopX(*lastLayout_, 2, CaretAffinity::Downstream).Raw();
        const std::int32_t latinLeft =
            CaretStopX(*lastLayout_, 6, CaretAffinity::Downstream).Raw();
        const std::int32_t latinRight =
            CaretStopX(*lastLayout_, 9, CaretAffinity::Upstream).Raw();
        // 선택되지 않은 히브리 첫 글자가 두 사각형 사이에 실제로 끼어 있다.
        // 이 셋이 무너지면 아래 기대값은 두 사각형이 붙어 있는 배치도 통과시킨다.
        REQUIRE(hebrewLeft < hebrewRight);
        REQUIRE(hebrewRight < latinLeft);
        REQUIRE(latinLeft < latinRight);
        const std::int32_t top = line.top.Raw();
        const std::int32_t height = line.bottom.Raw() - line.top.Raw();
        REQUIRE(height > 0);
        return {CanonicalRect{hebrewLeft, top, hebrewRight - hebrewLeft, height},
                CanonicalRect{latinLeft, top, latinRight - latinLeft, height}};
    }

    // 선택되지 않은 채 두 사각형 사이에 놓인 glyph의 advance. 두 사각형이
    // 정말 떨어져 있는지를 SelectionRects와 무관하게 재는 값이다.
    std::int32_t UnselectedGapRaw(std::uint32_t grapheme) const {
        REQUIRE(lastLayout_ != nullptr);
        for (const text::TextLine& line : lastLayout_->lines) {
            for (const text::VisualRun& run : line.visualRuns) {
                for (const text::PositionedGlyph& glyph : run.glyphs) {
                    if (glyph.glyph.graphemes.begin != grapheme) continue;
                    if (glyph.glyph.graphemes.end != grapheme + 1U) continue;
                    return glyph.glyph.advanceX.Raw();
                }
            }
        }
        REQUIRE_MESSAGE(false, "no single-grapheme glyph at that boundary");
        return 0;
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
        std::optional<std::shared_ptr<const text::TextLayout>> result) const {
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
    mutable std::shared_ptr<const text::TextLayout> lastLayout_;
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

    // Task 8.2 Step 8a.1: 여기 있던 "stb measurement 호출 0" 단언은 그 API가
    // 지워지면서 컴파일러의 것이 되었다. 남은 주장은 차가운 경로와 따뜻한
    // 경로가 같은 저작 정수에서 같은 지표를 낸다는 것이다.
    const auto cold = layoutOnce();
    REQUIRE(cold->lines.size() == 1U);
    CHECK(cold->lines[0].ascent.Raw() == expectedAscent->Raw());
    CHECK(cold->lines[0].descent.Raw() == expectedDescent->Raw());
    CHECK(cold->lines[0].lineGap.Raw() == expectedLineGap->Raw());
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

// ── Task 7.3 Step 1: BiDi-affinity caret, selection and hit testing ─────────
// 방향이 바뀌지 않는 경계에서는 논리 이웃과 시각 이웃이 같은 glyph라, 어떤
// affinity 논리도 없는 구현이 그대로 통과한다. affinity가 뜻을 갖는 곳은 정확히
// 방향 경계뿐이므로 관찰도 그곳에서만 이루어져야 한다.
TEST_CASE("BiDi boundary exposes two affinity-specific visual stops") {
    const auto layout = LoadLayoutFixture("hebrew-number-boundary").Layout();
    const auto stops = StopsAtBoundary(*layout, 4);
    REQUIRE(stops.size() == 2);
    CHECK(stops[0].affinity != stops[1].affinity);
    CHECK(stops[0].position.x != stops[1].position.x);
}

// 위 케이스는 경계 4가 정말 방향 경계인지를 묻지 않는다. 그 사실이 무너지면
// "자리가 둘"은 우연이 되므로 픽스처 자체를 여기서 못 박는다.
TEST_CASE("the hebrew-number fixture really changes direction at boundary 4") {
    const auto layout = LoadLayoutFixture("hebrew-number-boundary").Layout();
    REQUIRE(layout->lines.size() == 1U);
    std::uint8_t before = 0;
    std::uint8_t after = 0;
    bool sawBefore = false;
    bool sawAfter = false;
    for (const text::VisualRun& run : layout->lines[0].visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.graphemes.end == 4U) {
                before = run.bidiLevel;
                sawBefore = true;
            }
            if (glyph.glyph.graphemes.begin == 4U) {
                after = run.bidiLevel;
                sawAfter = true;
            }
        }
    }
    REQUIRE(sawBefore);
    REQUIRE(sawAfter);
    CHECK(before == 1U);
    CHECK(after == 2U);
    CHECK((before % 2U) != (after % 2U));

    // 그리고 그 두 자리는 서로의 시각 이웃이 아니다: 히브리 run의 왼쪽 끝과
    // 숫자 run의 왼쪽 끝 사이에는 숫자 run 전체가 놓인다.
    const auto stops = StopsAtBoundary(*layout, 4);
    REQUIRE(stops.size() == 2U);
    const std::int32_t span = stops[1].position.x.Raw() - stops[0].position.x.Raw();
    CHECK(span > 0);
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));

    // 그리고 방향이 바뀌지 않는 경계는 자리를 정확히 하나 갖는다. 이 대조가
    // 없으면 위 케이스의 "자리가 둘"은 모든 경계에서 참이 되어 아무것도 말하지
    // 않는다 — 논리 이웃과 시각 이웃이 같은 glyph인 자리에 affinity 둘을
    // 적어 두는 구현이 그대로 통과한다.
    CHECK(StopsAtBoundary(*layout, 2).size() == 1U);  // 히브리 run 안쪽
    CHECK(StopsAtBoundary(*layout, 5).size() == 1U);  // 숫자 run 안쪽
    CHECK(StopsAtBoundary(*layout, 0).size() == 1U);  // 문단 시작
    CHECK(StopsAtBoundary(*layout, 7).size() == 1U);  // 문단 끝
    // 그리고 경계 4가 자리를 둘 갖는 유일한 경계다. 위의 넷만으로는 cell의 앞
    // 모서리를 pen이 아니라 그리기 원점에서 재는 회귀를 놓친다: 이 픽스처에서
    // offsetX가 0이 아닌 glyph는 경계 3에 닿으므로, 그 회귀는 방향이 바뀌지도
    // 않은 경계 3에 두 번째 자리를 심어 놓고 위 넷을 그대로 통과한다.
    for (std::uint32_t boundary = 0; boundary <= 7U; ++boundary) {
        const std::size_t expected = (boundary == 4U) ? 2U : 1U;
        CHECK_MESSAGE(StopsAtBoundary(*layout, boundary).size() == expected,
                      boundary);
    }
    CHECK(CaretYPositionsAreLineBaselines(*layout));

    // hit test는 경계만이 아니라 affinity도 돌려주어야 한다. 같은 논리 경계
    // 4가 서로 멀리 떨어진 두 자리에서 잡히고, 어느 쪽에서 잡혔는지는
    // affinity로만 구분된다 — 언제나 Downstream을 돌려주는 구현은 여기서 걸린다.
    const auto at = [&layout, &line = layout->lines[0]](std::int32_t x) {
        molga::FixedPoint point;
        point.x = Fixed26_6::FromRaw(x);
        point.y = line.baseline;
        return TextHitTesting::HitTest(*layout, point);
    };
    const std::int32_t downstreamX =
        CaretStopX(*layout, 4, CaretAffinity::Downstream).Raw();
    const std::int32_t upstreamX =
        CaretStopX(*layout, 4, CaretAffinity::Upstream).Raw();
    REQUIRE(upstreamX > downstreamX + 2);
    const CaretPosition down = at(downstreamX);
    CHECK(down.boundary == 4U);
    CHECK(down.affinity == CaretAffinity::Downstream);
    // 상류 자리 바로 왼쪽. 앞 구간의 중점을 지났으므로 그 자리로 붙는다.
    const CaretPosition up = at(upstreamX - 1);
    CHECK(up.boundary == 4U);
    CHECK(up.affinity == CaretAffinity::Upstream);
}

// ── Step 1a: exact visual midpoints ─────────────────────────────────────────
// glyph 한가운데의 점은 leading 모서리와 trailing 모서리를 구분하지 못한다.
// 동점 규칙(물리적으로 더 큰 x)은 하나인데 논리 경계는 방향에 따라 반대로
// 나와야 하므로, 두 방향을 함께 보아야 규칙이 실제로 관찰된다.
TEST_CASE("visual midpoint ties move in the visual run direction") {
    const auto ltr = LoadLayoutFixture("ltr-midpoint").Layout();
    const auto rtl = LoadLayoutFixture("rtl-midpoint").Layout();
    CHECK(TextHitTesting::HitTest(*ltr, ExactMidpoint(*ltr)).boundary == 1);
    CHECK(TextHitTesting::HitTest(*rtl, ExactMidpoint(*rtl)).boundary == 0);
}

// 두 픽스처가 정말 반대 방향인지, 그리고 중점 바로 옆의 두 점이 서로 다른
// 경계로 갈라지는지. 동점 케이스만 보면 "언제나 1을 돌려준다"와 "언제나 큰
// x를 고른다"가 LTR에서 같은 답을 낸다.
TEST_CASE("the midpoint fixtures are opposite directions and split at the tie") {
    const auto ltr = LoadLayoutFixture("ltr-midpoint").Layout();
    const auto rtl = LoadLayoutFixture("rtl-midpoint").Layout();
    REQUIRE(ltr->lines.size() == 1U);
    REQUIRE(rtl->lines.size() == 1U);
    REQUIRE(ltr->lines[0].visualRuns.size() == 1U);
    REQUIRE(rtl->lines[0].visualRuns.size() == 1U);
    CHECK(ltr->lines[0].visualRuns[0].bidiLevel % 2U == 0U);
    CHECK(rtl->lines[0].visualRuns[0].bidiLevel % 2U == 1U);

    const auto probe = [](const text::TextLayout& layout, std::int32_t delta) {
        molga::FixedPoint point = ExactMidpoint(layout);
        point.x = Fixed26_6::FromRaw(point.x.Raw() + delta);
        return TextHitTesting::HitTest(layout, point).boundary;
    };
    CHECK(probe(*ltr, -1) == 0U);
    CHECK(probe(*ltr, 1) == 1U);
    CHECK(probe(*rtl, -1) == 1U);
    CHECK(probe(*rtl, 1) == 0U);

    // 양 끝 바깥은 가장 가까운 자리로 고정된다.
    molga::FixedPoint farLeft = ExactMidpoint(*ltr);
    farLeft.x = Fixed26_6::FromRaw(-1000);
    molga::FixedPoint farRight = ExactMidpoint(*ltr);
    farRight.x = Fixed26_6::FromRaw(1000 * 64);
    CHECK(TextHitTesting::HitTest(*ltr, farLeft).boundary == 0U);
    CHECK(TextHitTesting::HitTest(*ltr, farRight).boundary == 1U);
    CHECK(TextHitTesting::HitTest(*rtl, farLeft).boundary == 1U);
    CHECK(TextHitTesting::HitTest(*rtl, farRight).boundary == 0U);
}

// ── Step 1b: ligature carets ────────────────────────────────────────────────
TEST_CASE("ligature carets remain on every grapheme boundary") {
    auto fixture = LoadLayoutFixture("latin-ffi-ligature");
    const auto layout = fixture.Layout();
    CHECK(AllInteriorCaretsCameFromAdjustedGdef(*layout));
    fixture.ResetFontAndShaperCounters();
    CHECK(CaretBoundarySequence(*layout) ==
          std::vector<std::uint32_t>{0, 1, 2, 3});
    CHECK(CaretXPositionsAreStrictlyIncreasing(*layout));
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));
    CHECK(TextHitTesting::HitTest(*layout, fixture.PointInsideLigature()).boundary > 0);
    CHECK(fixture.HarfBuzzCallCount() == 0);
}

// 위 케이스의 마지막 단언은 계수기가 죽어 있어도 통과한다. 같은 프로세스에서
// 그 계수기가 실제로 움직인다는 것을 함께 못 박는다(test_text_shaping의 선례).
TEST_CASE("the hit-test counters are alive, so their zeros mean something") {
    auto fixture = LoadLayoutFixture("latin-ffi-ligature");
    fixture.ResetIcuAndHarfBuzzCounters();
    fixture.ResetFontAndShaperCounters();
    const auto layout = fixture.Layout();
    REQUIRE_FALSE(layout->lines.empty());
    // 차가운 배치는 셰이퍼를 반드시 부른다.
    CHECK(fixture.HarfBuzzCallCount() > 0);

    // Task 8.2 Step 8a.1: 이 자리에 있던 양성 대조는 face 계수기의 것이었고,
    // 그 계수기가 세던 FontFace::Advance/Kerning과 함께 사라졌다. 셰이퍼
    // 계수기 쪽 양성 대조는 바로 위 "차가운 배치는 셰이퍼를 반드시 부른다"가
    // 그대로 맡는다.
}

// 합자 안쪽의 hit test는 "0보다 크다"보다 정확히 답해야 한다. 내부 caret 하나를
// 사이에 두고 1/64씩 떨어진 두 점이 서로 다른 경계로 갈라지는지 본다.
TEST_CASE("hit testing inside a ligature resolves to the nearer interior caret") {
    auto fixture = LoadLayoutFixture("latin-ffi-ligature");
    const auto layout = fixture.Layout();
    const text::PositionedGlyph& ligature = FirstMultiGraphemeGlyph(*layout);
    REQUIRE(ligature.interiorCarets.size() == 2U);
    const std::int32_t first = ligature.interiorCarets[0].position.x.Raw();
    const std::int32_t second = ligature.interiorCarets[1].position.x.Raw();
    REQUIRE(first + 2 < second);

    const auto at = [&layout](std::int32_t x) {
        molga::FixedPoint point;
        point.x = Fixed26_6::FromRaw(x);
        point.y = layout->lines.front().baseline;
        return TextHitTesting::HitTest(*layout, point);
    };
    CHECK(at(first).boundary == 1U);
    CHECK(at(second).boundary == 2U);
    CHECK(at(first + 1).boundary == 1U);
    CHECK(at(second - 1).boundary == 2U);
    // 합자의 leading 모서리는 여전히 경계 0이다: 내부 caret이 앞 모서리를
    // 밀어내면 caret이 글자 앞에 설 수 없게 된다.
    CHECK(at(ligature.origin.x.Raw()).boundary == 0U);
}

// ── Step 1c: the stored fallback caret ──────────────────────────────────────
namespace {

constexpr const char* kNoLigCaretFont = "3a3a3a3a3a3a3a3a3a3a3a3a3a3a3a3a";
constexpr const char* kNoLigCaretFamily = "3b3b3b3b3b3b3b3b3b3b3b3b3b3b3b3b";

// NotoSans에서 GDEF LigCaretList 하나만 떼어 낸 사본. 합자 치환은 GSUB에 있으므로
// 'ffi'는 그대로 만들어지지만 HarfBuzz가 돌려줄 caret 기록이 없어, 최종 합자의
// adjustedGdefCaretOffsets가 빈 채로 배치에 도착한다.
//
// 커밋된 여섯 폰트 중 라틴 합자를 내면서 caret 기록이 없는 폰트는 하나도 없다.
// 크기를 2 raw로 낮춰 GDEF 값을 무너뜨리는 기존 케이스는 "집합이 있으나 쓸 수
// 없다"를 재고, 여기서는 "집합이 아예 없다"를 잰다 — 두 갈래는 서로 다른 코드다.
void AuthorLigCaretStrippedFont(const fs::path& fontsDir,
                                const std::string& fileName,
                                const std::string& baseFontName,
                                const std::string& guid) {
    std::vector<unsigned char> bytes =
        test_support::ReadAllBytes(fontsDir / baseFontName);
    const std::uint16_t tableCount =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[4]) << 8) |
                                   bytes[5]);
    REQUIRE(tableCount > 0U);
    std::size_t gdefRecord = 0;
    std::size_t gdefOffset = 0;
    std::size_t gdefLength = 0;
    for (std::uint16_t index = 0; index < tableCount; ++index) {
        const std::size_t record = 12U + static_cast<std::size_t>(index) * 16U;
        const std::string tag(reinterpret_cast<const char*>(&bytes[record]), 4U);
        if (tag != "GDEF") continue;
        gdefRecord = record;
        gdefOffset = test_support::ReadBigEndianU32(bytes, record + 8U);
        gdefLength = test_support::ReadBigEndianU32(bytes, record + 12U);
    }
    REQUIRE(gdefLength >= 12U);
    // GDEF 헤더의 ligCaretListOffset은 표 시작 + 8이다. 0은 "목록 없음"이다.
    REQUIRE((static_cast<std::uint32_t>(bytes[gdefOffset + 8U]) << 8 |
             bytes[gdefOffset + 9U]) != 0U);
    bytes[gdefOffset + 8U] = 0U;
    bytes[gdefOffset + 9U] = 0U;
    test_support::WriteBigEndianU32(bytes, gdefRecord + 4U,
                                    TableChecksum(bytes, gdefOffset, gdefLength));

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

// PatchedMetricsCorpus와 같은 이유로 자기만의 자격 트리 사본을 갖는다:
// 공유 corpus에 폰트를 하나 더 쓰면 다른 케이스가 읽는 카탈로그가 달라진다.
class LigCaretStrippedCorpus {
public:
    LigCaretStrippedCorpus()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))) {
        AuthorLigCaretStrippedFont(tree_.AssetsRoot() / "fonts",
                                   "no-lig-caret.ttf", "NotoSans-Regular.ttf",
                                   kNoLigCaretFont);
        AuthorSingleFaceFamily(tree_.AssetsRoot() / "families",
                               "no-lig-caret.fontfamily", kNoLigCaretFamily,
                               kNoLigCaretFont);
        std::string bindError;
        REQUIRE_MESSAGE(database_.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database_.ScanProject(tree_.AssetsRoot());
        REQUIRE(database_.Find(std::string(kNoLigCaretFont)) != nullptr);
    }

    molga::AssetDatabase& Database() noexcept { return database_; }

private:
    QualificationAssetTreeFixture tree_;
    molga::AssetDatabase database_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
};

}  // namespace

// 폰트가 caret 자리를 하나도 싣지 않은 합자. 배치가 그 자리를 균등 분할로
// 미리 저장해 두어야 하고, hit/caret/선택 API는 저장된 것만 읽어야 한다 —
// 질의 시점에 다시 유도하는 구현은 셰이퍼나 face를 다시 부르게 된다.
TEST_CASE("a ligature with no GDEF carets stores proportional stops up front") {
    LigCaretStrippedCorpus corpus;
    text::FontRepository repository(corpus.Database());
    text::FontFamilyResolver resolver(corpus.Database(), repository);
    text::TextShapingService shaper;
    text::TextLayoutCache cache(text::TextLayoutCacheLimits::Production());
    text::TextLayoutService service(resolver, shaper, cache);
    text::VectorTextDiagnosticSink sink;

    TextLayoutRequest request;
    request.utf8 = u8"ffi";
    request.style.fontFamilyGuid = kNoLigCaretFamily;
    request.style.shape.fontSize = Fixed26_6::FromRaw(16 * 64);
    request.style.shape.language = "en";
    request.style.analysis.locale = "en";
    const auto result = service.Layout(request, sink);
    REQUIRE(result);
    REQUIRE(*result != nullptr);
    const std::shared_ptr<const text::TextLayout> layout = *result;

    const text::PositionedGlyph& ligature = FirstMultiGraphemeGlyph(*layout);
    REQUIRE(ligature.glyph.graphemes.begin == 0U);
    REQUIRE(ligature.glyph.graphemes.end == 3U);
    // 이 케이스가 재는 갈래가 정말 "빈 집합"인지 먼저 못 박는다.
    REQUIRE(ligature.glyph.adjustedGdefCaretOffsets.empty());
    REQUIRE(ligature.interiorCarets.size() == 2U);
    for (std::size_t index = 0; index < ligature.interiorCarets.size(); ++index) {
        const text::GlyphInteriorCaret& caret = ligature.interiorCarets[index];
        CHECK_FALSE(caret.fromAdjustedGdef);
        const auto expected = Fixed26_6::CheckedMulDiv(
            ligature.glyph.advanceX, static_cast<std::int64_t>(index) + 1, 3);
        REQUIRE(expected);
        CHECK(caret.position.x.Raw() ==
              ligature.origin.x.Raw() + expected->Raw());
        CHECK(caret.position.y.Raw() == ligature.origin.y.Raw());
    }
    CHECK(CaretBoundarySequence(*layout) ==
          std::vector<std::uint32_t>{0, 1, 2, 3});
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));

    // 여기서부터가 Step 1c의 본론이다. 계수기를 전부 0으로 되돌리고 모든
    // hit/caret/선택 API를 부른 뒤, 계수기가 그대로 0이고 저장된 자리가
    // 한 글자도 달라지지 않았음을 요구한다.
    const std::vector<CanonicalStop> before = CanonicalCaretStops(*layout);
    REQUIRE(before.size() == 4U);
    text::detail::ResetIcuObjectCreationCount();
    text::detail::ResetLayoutIcuObjectCreationCount();
    text::detail::ResetHarfBuzzObjectCreationCount();

    molga::FixedPoint probe;
    probe.x = ligature.interiorCarets[0].position.x;
    probe.y = ligature.origin.y;
    const CaretPosition hit = TextHitTesting::HitTest(*layout, probe);
    CHECK(hit.boundary == 1U);
    const auto caretRects = TextHitTesting::CaretRects(
        *layout, CaretPosition{2U, CaretAffinity::Downstream},
        Fixed26_6::FromRaw(64));
    REQUIRE(caretRects.size() == 1U);
    CHECK(caretRects[0].x.Raw() == ligature.interiorCarets[1].position.x.Raw());
    CHECK(caretRects[0].width.Raw() == 64);
    const auto selection =
        TextHitTesting::SelectionRects(*layout, GraphemeRange{1U, 2U});
    REQUIRE(selection.size() == 1U);
    CHECK(selection[0].x.Raw() == ligature.interiorCarets[0].position.x.Raw());
    CHECK(selection[0].width.Raw() ==
          ligature.interiorCarets[1].position.x.Raw() -
              ligature.interiorCarets[0].position.x.Raw());

    CHECK(text::detail::IcuObjectCreationCount() == 0U);
    CHECK(text::detail::LayoutIcuObjectCreationCount() == 0U);
    CHECK(text::detail::HarfBuzzObjectCreationCount() == 0U);
    CHECK(CanonicalCaretStops(*layout) == before);
}

// ── Step 1d: mixed BiDi selection ───────────────────────────────────────────
TEST_CASE("mixed BiDi logical selection emits stable visual rectangles") {
    const auto fixture = LoadLayoutFixture("mixed-bidi-selection");
    const auto layout = fixture.Layout();
    const auto rects = TextHitTesting::SelectionRects(*layout, {2, 9});
    CHECK(rects.size() > 1);
    CHECK(CanonicalFixedRects(rects) == fixture.ExpectedSelectionRects());
    CHECK(RectsAreInStableVisualOrder(rects));
}

// 위 케이스의 `rects.size() > 1`은 사각형을 glyph마다 하나씩 내는 구현으로도
// 통과한다. 그래서 (1) 순수 LTR 선택은 정확히 하나이고 (2) 이 픽스처의 둘
// 사이 간격이 선택되지 않은 히브리 글자 하나의 advance와 정확히 같음을 함께
// 못 박는다 — 그 둘이 있어야 "논리적으로 이어진 선택이 시각적으로 갈라졌다"가
// 관찰된다.
TEST_CASE("a contiguous logical selection splits only where BiDi splits it") {
    const auto fixture = LoadLayoutFixture("mixed-bidi-selection");
    const auto layout = fixture.Layout();
    REQUIRE(layout->lines.size() == 1U);
    REQUIRE(layout->lines[0].graphemes.end == 9U);

    const auto rects = TextHitTesting::SelectionRects(*layout, {2, 9});
    REQUIRE(rects.size() == 2U);
    const std::int32_t gap =
        rects[1].x.Raw() - (rects[0].x.Raw() + rects[0].width.Raw());
    CHECK(gap == fixture.UnselectedGapRaw(1U));
    CHECK(gap > 0);

    // 같은 문단에서 라틴 꼬리만 고르면 사각형은 정확히 하나다. 방향이 갈리지
    // 않는 선택까지 여럿으로 쪼개는 구현은 여기서 걸린다.
    const auto latinOnly = TextHitTesting::SelectionRects(*layout, {6, 9});
    CHECK(latinOnly.size() == 1U);
    CHECK(RectsAreInStableVisualOrder(latinOnly));

    // 그리고 빈 선택과 뒤집힌 선택은 사각형을 내지 않는다.
    CHECK(TextHitTesting::SelectionRects(*layout, {4, 4}).empty());
    CHECK(TextHitTesting::SelectionRects(*layout, {7, 3}).empty());
}

// ── Steps 4/7: caret rectangles resolve affinity, not just the boundary ─────
TEST_CASE("caret rectangles follow the requested affinity at a BiDi boundary") {
    const auto layout = LoadLayoutFixture("hebrew-number-boundary").Layout();
    REQUIRE(layout->lines.size() == 1U);
    const text::TextLine& line = layout->lines[0];
    const auto thickness = Fixed26_6::FromRaw(96);

    const auto upstream = TextHitTesting::CaretRects(
        *layout, CaretPosition{4U, CaretAffinity::Upstream}, thickness);
    const auto downstream = TextHitTesting::CaretRects(
        *layout, CaretPosition{4U, CaretAffinity::Downstream}, thickness);
    REQUIRE(upstream.size() == 1U);
    REQUIRE(downstream.size() == 1U);
    CHECK(upstream[0].x.Raw() != downstream[0].x.Raw());
    CHECK(upstream[0].y.Raw() == line.top.Raw());
    CHECK(upstream[0].height.Raw() == line.bottom.Raw() - line.top.Raw());
    CHECK(upstream[0].width.Raw() == 96);
    CHECK(downstream[0].height.Raw() == upstream[0].height.Raw());
    CHECK(upstream[0].x.Raw() ==
          CaretStopX(*layout, 4, CaretAffinity::Upstream).Raw());
    CHECK(downstream[0].x.Raw() ==
          CaretStopX(*layout, 4, CaretAffinity::Downstream).Raw());

    // 방향이 바뀌지 않는 경계에서는 두 affinity가 한 자리로 접힌다. 접힌 자리를
    // 요구한 affinity로 찾지 못했다고 caret을 잃으면 편집기가 그 자리에서
    // 커서를 그리지 못한다.
    const auto collapsedDown = TextHitTesting::CaretRects(
        *layout, CaretPosition{2U, CaretAffinity::Downstream}, thickness);
    const auto collapsedUp = TextHitTesting::CaretRects(
        *layout, CaretPosition{2U, CaretAffinity::Upstream}, thickness);
    REQUIRE(collapsedDown.size() == 1U);
    REQUIRE(collapsedUp.size() == 1U);
    CHECK(collapsedDown[0].x.Raw() == collapsedUp[0].x.Raw());

    // 두께는 호출자의 것이고 양수여야 한다.
    CHECK(TextHitTesting::CaretRects(
              *layout, CaretPosition{4U, CaretAffinity::Upstream},
              Fixed26_6::FromRaw(0))
              .empty());
    CHECK(TextHitTesting::CaretRects(
              *layout, CaretPosition{4U, CaretAffinity::Upstream},
              Fixed26_6::FromRaw(-64))
              .empty());
    // 존재하지 않는 경계는 사각형이 없다.
    CHECK(TextHitTesting::CaretRects(
              *layout, CaretPosition{4096U, CaretAffinity::Downstream}, thickness)
              .empty());
}

// ── Step 4: every line, including the metric-bearing empty ones ─────────────
// 빈 줄에 caret 자리가 없으면 편집기가 빈 줄에 커서를 놓을 수 없다. 그리고
// 여러 줄 배치에서는 y가 줄을 고른다.
TEST_CASE("every line carries at least one caret stop, empty lines included") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto layout = f.LayoutText(u8"AB\n\nCD");
    REQUIRE(layout->lines.size() == 3U);
    for (std::size_t index = 0; index < layout->lines.size(); ++index) {
        std::size_t stops = 0;
        for (const CaretStop& stop : layout->caretStops) {
            if (stop.lineIndex == index) ++stops;
        }
        CHECK_MESSAGE(stops > 0U, index);
    }
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));

    // 두 번째 줄은 비어 있고, 그 줄의 유일한 자리는 그 줄의 grapheme 시작이다.
    const auto empty = StopsAtBoundary(*layout, layout->lines[1].graphemes.begin);
    REQUIRE_FALSE(empty.empty());

    // y가 줄을 고른다: 마지막 줄 한가운데를 찍으면 마지막 줄의 경계가 나온다.
    molga::FixedPoint point;
    point.x = Fixed26_6::FromRaw(0);
    point.y = layout->lines[2].baseline;
    const CaretPosition last = TextHitTesting::HitTest(*layout, point);
    CHECK(last.boundary >= layout->lines[2].graphemes.begin);
    CHECK(last.boundary <= layout->lines[2].graphemes.end);
    point.y = layout->lines[0].baseline;
    const CaretPosition first = TextHitTesting::HitTest(*layout, point);
    CHECK(first.boundary <= layout->lines[0].graphemes.end);

    // 빈 줄의 유일한 자리는 그 줄의 정렬된 시작 자리다. 0으로 고정하면
    // 오른쪽 정렬 문단의 빈 줄에서 커서가 글이 있는 자리와 어긋난다.
    TextLayoutRequest request = f.MakeRequest(u8"AB\n\nCD");
    request.constraints.width = Fixed26_6::FromRaw(400 * 64);
    request.style.horizontal = text::TextHorizontalAlignment::Right;
    const auto aligned = f.service.Layout(request, f.sink);
    REQUIRE(aligned);
    REQUIRE(*aligned != nullptr);
    REQUIRE((*aligned)->lines.size() == 3U);
    const auto emptyStops =
        StopsAtBoundary(**aligned, (*aligned)->lines[1].graphemes.begin);
    REQUIRE(emptyStops.size() == 1U);
    CHECK(emptyStops[0].lineIndex == 1U);
    // 빈 줄은 advance가 0이므로 정렬 여백이 곧 상자 폭이다.
    CHECK(emptyStops[0].position.x.Raw() == 400 * 64);
}

// ── Step 4: multi-codepoint graphemes never gain their own stop ─────────────
// NoCaretInsideUtf16ScalarOrGrapheme의 상한이 실제로 물리는 곳. 'ffi'는 세
// grapheme이 전부 코드포인트 하나라 상한이 빡빡하지 않지만, 데바나가리는 한
// grapheme이 코드포인트 넷까지 간다.
TEST_CASE("a Devanagari paragraph stops once per grapheme, not per codepoint") {
    LayoutFixture f = LoadLayoutFixture("devanagari-reorder");
    const auto layout = f.Layout();
    REQUIRE_FALSE(layout->lines.empty());
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));

    // 이 픽스처가 정말 다중 코드포인트 grapheme을 담고 있는지 못 박는다.
    // 담고 있지 않으면 위 단언은 'ffi'와 같은 말이 된다.
    const std::string utf8 = f.Spec().utf8;
    std::size_t scalars = 0;
    for (const char byte : utf8) {
        if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) ++scalars;
    }
    std::uint32_t graphemes = 0;
    for (const text::TextLine& line : layout->lines) {
        graphemes += line.graphemes.end - line.graphemes.begin;
    }
    REQUIRE(graphemes > 0U);
    CHECK(scalars > static_cast<std::size_t>(graphemes));
}

// pre-base 매트라는 자기 자음보다 먼저 그려지므로, 하나의 grapheme을 두 glyph가
// 나눠 그린다. caret 자리를 glyph마다 내면 그 한 경계가 cluster 안쪽의 서로
// 다른 두 x에 서게 되고, 화면에서는 그럴듯해 보이는 자리라 눈으로 드러나지
// 않는다. 아랍어/히브리어로는 이 자리를 만들 수 없다: 그쪽 재배열은 run을
// 통째로 뒤집을 뿐 cluster 안쪽 순서를 건드리지 않는다.
TEST_CASE("a grapheme drawn by several glyphs still has one caret stop") {
    LayoutFixture f = LoadLayoutFixture("devanagari-reorder");
    const auto layout = f.Layout();

    const text::VisualRun* sharedRun = nullptr;
    GraphemeRange shared{};
    std::size_t sharedFirst = 0;
    std::size_t sharedLast = 0;
    for (const text::TextLine& line : layout->lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            for (std::size_t index = 0; index + 1U < run.glyphs.size(); ++index) {
                if (!(run.glyphs[index].glyph.graphemes ==
                      run.glyphs[index + 1U].glyph.graphemes)) {
                    continue;
                }
                if (sharedRun != nullptr) continue;
                sharedRun = &run;
                shared = run.glyphs[index].glyph.graphemes;
                sharedFirst = index;
                sharedLast = index + 1U;
                while (sharedLast + 1U < run.glyphs.size() &&
                       run.glyphs[sharedLast + 1U].glyph.graphemes == shared) {
                    ++sharedLast;
                }
            }
        }
    }
    // 픽스처가 정말 그런 cluster를 담고 있는지 먼저 못 박는다. 담고 있지
    // 않으면 아래 단언은 아무 배치에서나 참이다.
    REQUIRE(sharedRun != nullptr);
    REQUIRE(sharedLast > sharedFirst);
    REQUIRE(shared.end > shared.begin);
    // 그리고 이 픽스처가 닿는 범위를 적어 둔다: 여러 glyph가 나눠 그리는 이
    // cell은 grapheme을 정확히 하나만 덮는다. "glyph도 여럿이고 grapheme도
    // 여럿"인 조합 — 같은 논리 경계가 cell 안쪽의 서로 다른 x에 두 번 실릴 수
    // 있는 유일한 배치 — 은 커밋된 corpus 어디에도 없다. BuildCaretStops의
    // 내부 caret 복사가 그 경우를 걸러 두는 이유가 이것이다.
    REQUIRE(shared.end - shared.begin == 1U);

    CHECK(StopsAtBoundary(*layout, shared.begin).size() == 1U);
    CHECK(StopsAtBoundary(*layout, shared.end).size() == 1U);

    // 그리고 그 하나의 자리는 cluster 전체의 바깥 모서리다. 첫 glyph의 pen
    // 자리이지 두 번째 glyph의 자리가 아니다(이 run은 LTR이다).
    const text::PositionedGlyph& head = sharedRun->glyphs[sharedFirst];
    const text::PositionedGlyph& tail = sharedRun->glyphs[sharedLast];
    REQUIRE(sharedRun->bidiLevel % 2U == 0U);
    const std::int32_t headPen = head.origin.x.Raw() - head.glyph.offsetX.Raw();
    const std::int32_t tailPen = tail.origin.x.Raw() - tail.glyph.offsetX.Raw();
    REQUIRE(tailPen > headPen);
    const auto begin = StopsAtBoundary(*layout, shared.begin);
    const auto end = StopsAtBoundary(*layout, shared.end);
    REQUIRE(begin.size() == 1U);
    REQUIRE(end.size() == 1U);
    CHECK(begin[0].position.x.Raw() == headPen);
    CHECK(end[0].position.x.Raw() == tailPen + tail.glyph.advanceX.Raw());
}

// ── Step 4: the synthetic ellipsis glyph is not a caret site ────────────────
// 줄임표 glyph는 원본 grapheme을 하나도 덮지 않는다. 그 glyph에도 자리를
// 내면 잘린 지점의 논리 경계 하나가 줄임표 양쪽에 두 번 서게 되고, 편집기는
// 화면에 없는 글자 뒤에 커서를 놓게 된다.
TEST_CASE("the ellipsis glyph adds no caret stop at the cut") {
    LayoutFixture f = LoadLayoutFixture("arabic-ellipsis-context");
    const auto layout = f.LayoutEllipsized();
    REQUIRE(layout);
    REQUIRE((*layout)->lines.size() == 1U);
    const text::TextLine& line = (*layout)->lines[0];
    REQUIRE((*layout)->ellipsized);

    const text::PositionedGlyph* synthetic = nullptr;
    for (const text::VisualRun& run : line.visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.graphemes.begin != glyph.glyph.graphemes.end) continue;
            REQUIRE(synthetic == nullptr);
            synthetic = &glyph;
        }
    }
    REQUIRE(synthetic != nullptr);
    REQUIRE(synthetic->glyph.advanceX.Raw() > 0);

    // 잘린 지점의 논리 경계는 자리를 정확히 하나 갖는다.
    const auto stops = StopsAtBoundary(*(*layout), line.graphemes.end);
    REQUIRE(stops.size() == 1U);
    // 그리고 그 자리는 줄임표 칸의 바깥, 남긴 글 쪽 모서리다. 아랍어 run이라
    // 줄임표는 시각적으로 왼쪽 끝에 놓이므로 그 칸의 오른쪽 모서리가 된다.
    const std::int32_t synthLeft =
        synthetic->origin.x.Raw() - synthetic->glyph.offsetX.Raw();
    const std::int32_t synthRight = synthLeft + synthetic->glyph.advanceX.Raw();
    CHECK(stops[0].position.x.Raw() == synthRight);
    CHECK(stops[0].position.x.Raw() > synthLeft);
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*(*layout)));
}

// ── Step 8: selection spans pen cells, not glyph draw origins ───────────────
// glyph 원점은 pen에 GPOS x offset을 더한 그리기 좌표다. 사각형을 원점에서
// 재면 offset이 0이 아닌 글자에서 선택 영역이 그만큼 밀리고, 이웃한 두
// 사각형 사이에 틈이나 겹침이 생긴다. 히브리 문단은 실제로 그런 offset을 갖는다.
TEST_CASE("selection rectangles measure pen cells, not glyph draw origins") {
    const auto layout = LoadLayoutFixture("hebrew-number-boundary").Layout();
    REQUIRE(layout->lines.size() == 1U);
    std::size_t measured = 0;
    std::size_t offsetGlyphs = 0;
    for (const text::VisualRun& run : layout->lines[0].visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            const GraphemeRange span = glyph.glyph.graphemes;
            if (span.end - span.begin != 1U) continue;
            const auto rects = TextHitTesting::SelectionRects(*layout, span);
            REQUIRE(rects.size() == 1U);
            const std::int32_t pen =
                glyph.origin.x.Raw() - glyph.glyph.offsetX.Raw();
            CHECK(rects[0].x.Raw() == pen);
            CHECK(rects[0].width.Raw() == glyph.glyph.advanceX.Raw());
            if (glyph.glyph.offsetX.Raw() != 0) ++offsetGlyphs;
            ++measured;
        }
    }
    REQUIRE(measured == 7U);
    // 이 픽스처에 그리기 원점이 pen과 다른 glyph가 실제로 있어야 위 두 단언이
    // 두 좌표계를 구분한다. 없으면 어느 규약을 써도 같은 숫자가 나온다.
    CHECK(offsetGlyphs > 0U);
}

// ── Step 4: a combining mark does not fork the cell it belongs to ───────────
// HarfBuzz는 MONOTONE_CHARACTERS에서 합자 뒤 결합 문자의 cluster를 합자
// cluster에 합치지 않는다. 그래서 표식의 grapheme 범위는 합자 범위와 "같지는
// 않은 부분집합"으로 도착하고, cell을 범위가 같은 glyph로만 묶으면 표식이 자기
// cell이 되어 합자의 바깥 모서리에 두 번째 자리를 낸다. 방향이 바뀌지도 않은
// 경계가 BiDi affinity 쌍과 똑같이 보이게 되는 유일한 순수 LTR 경로다.
TEST_CASE("a combining mark after a ligature does not add a second stop") {
    LayoutFixture f = LoadLayoutFixture("latin-ligature-mark");
    const auto layout = f.Layout();
    REQUIRE(layout->lines.size() == 1U);
    REQUIRE(layout->lines[0].visualRuns.size() == 1U);
    const text::VisualRun& run = layout->lines[0].visualRuns[0];
    CHECK(run.bidiLevel % 2U == 0U);

    // 픽스처가 정말 그 배치를 담고 있는지 먼저 못 박는다. 담고 있지 않으면
    // 아래 단언은 'ffi' 케이스와 같은 말이 된다.
    REQUIRE(run.glyphs.size() == 2U);
    const text::PositionedGlyph* ligature = nullptr;
    const text::PositionedGlyph* mark = nullptr;
    for (const text::PositionedGlyph& glyph : run.glyphs) {
        const GraphemeRange span = glyph.glyph.graphemes;
        if (span.end - span.begin > 1U) {
            ligature = &glyph;
        } else {
            mark = &glyph;
        }
    }
    REQUIRE(ligature != nullptr);
    REQUIRE(mark != nullptr);
    REQUIRE(ligature->glyph.graphemes.begin == 0U);
    REQUIRE(ligature->glyph.graphemes.end == 3U);
    REQUIRE_FALSE(mark->glyph.graphemes == ligature->glyph.graphemes);
    REQUIRE(mark->glyph.graphemes.begin >= ligature->glyph.graphemes.begin);
    REQUIRE(mark->glyph.graphemes.end <= ligature->glyph.graphemes.end);

    CHECK(CaretBoundarySequence(*layout) ==
          std::vector<std::uint32_t>{0, 1, 2, 3});
    CHECK(CaretXPositionsAreStrictlyIncreasing(*layout));
    CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));
    CHECK(CaretYPositionsAreLineBaselines(*layout));
    for (std::uint32_t boundary = 0; boundary <= 3U; ++boundary) {
        CHECK_MESSAGE(StopsAtBoundary(*layout, boundary).size() == 1U, boundary);
    }

    // 그리고 합자 오른쪽 끝 근처의 클릭은 합자의 끝 경계로 간다. 표식이 자기
    // cell을 만들면 그 자리가 경계 2를 합자 오른쪽 모서리에 한 번 더 세우므로
    // 같은 점이 경계 2로 떨어진다.
    const std::int32_t pen =
        ligature->origin.x.Raw() - ligature->glyph.offsetX.Raw();
    REQUIRE(ligature->interiorCarets.size() == 2U);
    const std::int32_t lastInterior =
        ligature->interiorCarets.back().position.x.Raw();
    const std::int32_t right = pen + ligature->glyph.advanceX.Raw();
    REQUIRE(lastInterior + 2 < right);
    molga::FixedPoint point;
    point.x = Fixed26_6::FromRaw(lastInterior + (right - lastInterior) / 2 + 1);
    point.y = layout->lines[0].baseline;
    CHECK(TextHitTesting::HitTest(*layout, point).boundary == 3U);
}

// ── Step 8: sub-cluster selection reads the run's direction ─────────────────
// 한 cell 안의 선택은 논리 시작을 LTR에서 왼쪽 모서리로, RTL에서 오른쪽
// 모서리로 잡아야 한다. glyph 전체를 덮는 선택만 재면 min/max가 그 뒤바뀜을
// 삼켜 버리므로, 합자 안쪽을 한 grapheme씩 골라야 방향이 관찰된다.
TEST_CASE("selection inside a ligature follows the visual run direction") {
    auto ltrFixture = LoadLayoutFixture("latin-ffi-ligature");
    const auto ltr = ltrFixture.Layout();
    REQUIRE(ltr->lines.size() == 1U);
    REQUIRE(ltr->lines[0].visualRuns.size() == 1U);
    REQUIRE(ltr->lines[0].visualRuns[0].bidiLevel % 2U == 0U);
    const text::PositionedGlyph& ligature = FirstMultiGraphemeGlyph(*ltr);
    REQUIRE(ligature.glyph.graphemes.begin == 0U);
    REQUIRE(ligature.glyph.graphemes.end == 3U);
    REQUIRE(ligature.interiorCarets.size() == 2U);
    const std::int32_t pen =
        ligature.origin.x.Raw() - ligature.glyph.offsetX.Raw();
    const std::int32_t firstCaret = ligature.interiorCarets[0].position.x.Raw();
    const std::int32_t secondCaret = ligature.interiorCarets[1].position.x.Raw();
    const std::int32_t end = pen + ligature.glyph.advanceX.Raw();
    REQUIRE(pen < firstCaret);
    REQUIRE(firstCaret < secondCaret);
    REQUIRE(secondCaret < end);

    // 논리적으로 앞선 grapheme은 LTR에서 왼쪽 조각이다.
    const auto head = TextHitTesting::SelectionRects(*ltr, GraphemeRange{0U, 1U});
    REQUIRE(head.size() == 1U);
    CHECK(head[0].x.Raw() == pen);
    CHECK(head[0].width.Raw() == firstCaret - pen);
    const auto tail = TextHitTesting::SelectionRects(*ltr, GraphemeRange{2U, 3U});
    REQUIRE(tail.size() == 1U);
    CHECK(tail[0].x.Raw() == secondCaret);
    CHECK(tail[0].width.Raw() == end - secondCaret);

    // 그리고 RTL 짝. 라틴 합자로는 이 뒤바뀜을 잴 수 없다.
    auto rtlFixture = LoadLayoutFixture("arabic-ligature");
    const auto rtl = rtlFixture.Layout();
    REQUIRE(rtl->lines.size() == 1U);
    const text::PositionedGlyph& arabic = FirstMultiGraphemeGlyph(*rtl);
    bool sawRtlRun = false;
    for (const text::VisualRun& run : rtl->lines[0].visualRuns) {
        for (const text::PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.graphemes.end - glyph.glyph.graphemes.begin <= 1U) {
                continue;
            }
            CHECK(run.bidiLevel % 2U == 1U);
            sawRtlRun = true;
        }
    }
    REQUIRE(sawRtlRun);
    const GraphemeRange span = arabic.glyph.graphemes;
    REQUIRE(span.end - span.begin == 2U);
    REQUIRE(arabic.interiorCarets.size() == 1U);
    // grapheme이 둘뿐인 cell에서는 InteriorCaretLogicalBoundary의 LTR 대응
    // (begin+index)과 RTL 대응(end-index)이 같은 경계를 가리킨다. 그래서 이
    // 케이스는 저장된 경계값이 아니라 그 자리가 놓인 쪽을 잰다.
    //
    // 둘이 갈리려면 홀수 level run 안에 grapheme 셋 이상을 덮는 cluster가
    // 있어야 하는데, 커밋된 여섯 폰트로는 그런 배치를 만들 수 없다: 아랍어의
    // lam-alef는 grapheme 둘이고 alef-lam-lam-heh는 합자가 되지 않으며,
    // 라틴 ffi는 HarfBuzz가 RTL 방향에서 ff와 i로 갈라 놓는다. 그래서
    // BuildInteriorCarets에 넘기는 방향 인자 자체는 여기서 끝까지 관찰되지
    // 않고, 대응 함수 쪽은 자기 단위 케이스가 양방향으로 고정한다.
    CHECK(arabic.interiorCarets[0].logicalGraphemeBoundary == span.begin + 1U);
    const std::int32_t arabicPen =
        arabic.origin.x.Raw() - arabic.glyph.offsetX.Raw();
    const std::int32_t middle = arabic.interiorCarets[0].position.x.Raw();
    const std::int32_t arabicEnd = arabicPen + arabic.glyph.advanceX.Raw();
    REQUIRE(arabicPen < middle);
    REQUIRE(middle < arabicEnd);
    const auto logicalFirst = TextHitTesting::SelectionRects(
        *rtl, GraphemeRange{span.begin, span.begin + 1U});
    REQUIRE(logicalFirst.size() == 1U);
    // 논리적으로 앞선 grapheme은 RTL run에서 오른쪽 조각이다.
    CHECK(logicalFirst[0].x.Raw() == middle);
    CHECK(logicalFirst[0].width.Raw() == arabicEnd - middle);
    const auto logicalSecond = TextHitTesting::SelectionRects(
        *rtl, GraphemeRange{span.begin + 1U, span.end});
    REQUIRE(logicalSecond.size() == 1U);
    CHECK(logicalSecond[0].x.Raw() == arabicPen);
    CHECK(logicalSecond[0].width.Raw() == middle - arabicPen);
}

// ── Steps 6-8: every query addresses the line it was asked about ────────────
// 하나짜리 줄만 재면 세 API 모두 "언제나 0번 줄"로 줄여도 통과한다.
TEST_CASE("caret, selection and hit queries address the right line") {
    LayoutFixture f = LoadLayoutFixture("empty-lines");
    const auto layout = f.LayoutText(u8"AB\n\nCD");
    REQUIRE(layout->lines.size() == 3U);
    REQUIRE(layout->lines[2].top.Raw() > layout->lines[0].top.Raw());
    CHECK(CaretYPositionsAreLineBaselines(*layout));

    // 줄바꿈을 넘는 선택은 글자가 있는 줄마다 사각형을 하나씩, y가 커지는
    // 순서로 낸다. 가운데 빈 줄은 glyph가 없어 사각형이 없다.
    const auto rects = TextHitTesting::SelectionRects(
        *layout, GraphemeRange{layout->lines[0].graphemes.begin,
                               layout->lines[2].graphemes.end});
    REQUIRE(rects.size() == 2U);
    CHECK(rects[0].y.Raw() == layout->lines[0].top.Raw());
    CHECK(rects[1].y.Raw() == layout->lines[2].top.Raw());
    CHECK(RectsAreInStableVisualOrder(rects));

    // caret 사각형은 그 경계가 실제로 놓인 줄의 세로 띠를 쓴다.
    const auto caret = TextHitTesting::CaretRects(
        *layout,
        CaretPosition{layout->lines[1].graphemes.begin,
                      CaretAffinity::Downstream},
        Fixed26_6::FromRaw(64));
    REQUIRE(caret.size() == 1U);
    CHECK(caret[0].y.Raw() == layout->lines[1].top.Raw());
    CHECK(caret[0].y.Raw() != layout->lines[0].top.Raw());
    CHECK(caret[0].height.Raw() ==
          layout->lines[1].bottom.Raw() - layout->lines[1].top.Raw());

    // 줄 선택은 세로로도 반열림이다: 정확히 첫 줄의 bottom에 놓인 점은 이미
    // 다음 줄의 것이다.
    molga::FixedPoint point;
    point.x = Fixed26_6::FromRaw(0);
    point.y = layout->lines[0].bottom;
    CHECK(TextHitTesting::HitTest(*layout, point).boundary ==
          layout->lines[1].graphemes.begin);
    point.y = Fixed26_6::FromRaw(layout->lines[0].bottom.Raw() - 1);
    CHECK(TextHitTesting::HitTest(*layout, point).boundary ==
          layout->lines[0].graphemes.begin);

    // 그리고 합자가 첫 줄이 아닐 때, 그 내부 자리는 자기 줄의 번호를 달고
    // 나온다. 0으로 고정하면 그 caret이 첫 줄의 세로 띠에 그려진다.
    const auto wrapped = f.LayoutText(u8"AB\nffi");
    REQUIRE(wrapped->lines.size() == 2U);
    const text::PositionedGlyph& ligature = FirstMultiGraphemeGlyph(*wrapped);
    REQUIRE(ligature.interiorCarets.size() == 2U);
    for (const text::GlyphInteriorCaret& interior : ligature.interiorCarets) {
        const auto stops =
            StopsAtBoundary(*wrapped, interior.logicalGraphemeBoundary);
        REQUIRE(stops.size() == 1U);
        CHECK(stops[0].lineIndex == 1U);
        CHECK(stops[0].position.x.Raw() == interior.position.x.Raw());
        // 내부 자리의 affinity도 저장된 값이다. CaretRects의 경계-전용 되짚기가
        // 이 값을 가려 주므로 공개 API로는 관찰되지 않는다.
        CHECK(stops[0].affinity == CaretAffinity::Downstream);
    }
    CHECK(CaretYPositionsAreLineBaselines(*wrapped));
}

// ── Step 5: missing interior-caret storage is a failure, not a fallback ─────
// 여러 grapheme을 덮는 glyph가 내부 caret을 하나도 들고 있지 않은 배치는 배치
// 쪽 불변식이 깨진 것이다. hit testing은 그 자리를 비례 분할로 지어내는 대신
// 아무 사각형도 내지 않는다. 서비스는 그런 배치를 내지 않으므로 손으로
// 조립해야 이 갈래를 관찰할 수 있다.
TEST_CASE("a multi-grapheme glyph with no stored interior caret yields no rect") {
    text::TextLayout layout;
    text::TextLine line;
    line.graphemes = GraphemeRange{0U, 3U};
    line.top = Fixed26_6::FromRaw(0);
    line.baseline = Fixed26_6::FromRaw(10 * 64);
    line.bottom = Fixed26_6::FromRaw(12 * 64);
    text::VisualRun run;
    run.bidiLevel = 0;
    text::PositionedGlyph glyph;
    glyph.glyph.graphemes = GraphemeRange{0U, 3U};
    glyph.glyph.advanceX = Fixed26_6::FromRaw(300);
    glyph.origin.x = Fixed26_6::FromRaw(0);
    glyph.origin.y = line.baseline;
    run.glyphs.push_back(glyph);
    line.visualRuns.push_back(run);
    layout.lines.push_back(line);

    // 바깥 모서리만 쓰는 선택은 그대로 나온다. 이 케이스가 "언제나 빈 결과"를
    // 재고 있는 것이 아님을 먼저 못 박는다.
    const auto whole =
        TextHitTesting::SelectionRects(layout, GraphemeRange{0U, 3U});
    REQUIRE(whole.size() == 1U);
    CHECK(whole[0].x.Raw() == 0);
    CHECK(whole[0].width.Raw() == 300);
    // 안쪽 경계를 요구하면 저장된 자리가 없으므로 사각형이 없다.
    CHECK(TextHitTesting::SelectionRects(layout, GraphemeRange{1U, 2U}).empty());
    CHECK(TextHitTesting::SelectionRects(layout, GraphemeRange{0U, 2U}).empty());
    CHECK(TextHitTesting::SelectionRects(layout, GraphemeRange{1U, 3U}).empty());
}

// ── Step 6: the defensive line fallback ─────────────────────────────────────
// 자리의 줄 번호가 어느 줄과도 맞지 않으면 배열 전체를 하나의 구간 사슬로
// 훑지 않는다. x는 줄 안에서만 단조롭고 줄과 줄 사이에서는 되감기므로 그 훑기는
// 첫 내리막에서 멈춰 아무 뜻 없는 자리를 고른다.
TEST_CASE("hit testing falls back to the first stop when no stop names the line") {
    text::TextLayout layout;
    text::TextLine line;
    line.top = Fixed26_6::FromRaw(0);
    line.baseline = Fixed26_6::FromRaw(48);
    line.bottom = Fixed26_6::FromRaw(64);
    layout.lines.push_back(line);

    CaretStop first;
    first.logicalGraphemeBoundary = 7U;
    first.affinity = CaretAffinity::Upstream;
    first.position.x = Fixed26_6::FromRaw(500);
    first.position.y = line.baseline;
    first.lineIndex = 9U;  // 어느 줄과도 맞지 않는다
    layout.caretStops.push_back(first);
    CaretStop second = first;
    second.logicalGraphemeBoundary = 8U;
    second.position.x = Fixed26_6::FromRaw(1000);
    layout.caretStops.push_back(second);

    molga::FixedPoint point;
    point.x = Fixed26_6::FromRaw(1000);
    point.y = Fixed26_6::FromRaw(32);
    const CaretPosition fallback = TextHitTesting::HitTest(layout, point);
    CHECK(fallback.boundary == 7U);
    CHECK(fallback.affinity == CaretAffinity::Upstream);

    // 줄 번호가 맞으면 같은 점이 두 번째 자리로 간다. 되짚기가 "언제나 첫
    // 자리"로 굳은 것이 아님을 함께 못 박는다.
    layout.caretStops[0].lineIndex = 0U;
    layout.caretStops[1].lineIndex = 0U;
    CHECK(TextHitTesting::HitTest(layout, point).boundary == 8U);
}

// ── Step 7: one resolved CaretPosition is one caret, never a bag ────────────
// CaretRects의 반환형은 Step 3이 못 박은 서명 때문에 벡터이지만, 배치가 낸
// TextLayout에서 하나의 (경계, affinity) 짝은 시각 자리를 많아야 하나 갖는다.
// 그 성질이 무너지면 편집기는 같은 caret을 두 곳에 그리게 되고, affinity로는
// 어느 쪽인지 물어볼 수 없다. 방향이 섞인 문단, 합자, 결합 문자, 부드러운
// 줄바꿈, 딱딱한 줄바꿈, 줄임표를 모두 걸어 둔다.
TEST_CASE("one logical boundary plus affinity resolves to at most one stop") {
    for (const char* name :
         {"hebrew-number-boundary", "mixed-bidi-selection", "latin-ffi-ligature",
          "latin-ligature-mark", "arabic-ligature", "devanagari-reorder",
          "final-line-fi"}) {
        LayoutFixture fixture = LoadLayoutFixture(name);
        const auto layout = fixture.Layout();
        CHECK_MESSAGE(EveryCaretPositionResolvesToOneStop(*layout), name);
        // 그리고 실제로 물어보면 사각형도 하나다.
        for (const CaretStop& stop : layout->caretStops) {
            const auto rects = TextHitTesting::CaretRects(
                *layout,
                CaretPosition{stop.logicalGraphemeBoundary, stop.affinity},
                Fixed26_6::FromRaw(64));
            CHECK_MESSAGE(rects.size() == 1U, name,
                          stop.logicalGraphemeBoundary);
        }
    }

    // 딱딱한 줄바꿈과 빈 줄, 그리고 줄임표까지.
    LayoutFixture lines = LoadLayoutFixture("empty-lines");
    const auto hardBreaks = lines.LayoutText(u8"AB\n\nCD");
    CHECK(EveryCaretPositionResolvesToOneStop(*hardBreaks));
    LayoutFixture arabic = LoadLayoutFixture("arabic-ellipsis-context");
    const auto ellipsized = arabic.LayoutEllipsized();
    REQUIRE(ellipsized);
    REQUIRE((*ellipsized)->ellipsized);
    CHECK(EveryCaretPositionResolvesToOneStop(**ellipsized));

    // 술어가 죽어 있지 않다는 대조. 같은 짝을 하나 더 넣으면 거짓이 되어야 한다.
    text::TextLayout doubled = *hardBreaks;
    REQUIRE_FALSE(doubled.caretStops.empty());
    doubled.caretStops.push_back(doubled.caretStops.front());
    CHECK_FALSE(EveryCaretPositionResolvesToOneStop(doubled));
}
