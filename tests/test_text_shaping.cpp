#include "Assets/FontArtifactStore.h"
#include "Assets/FontAsset.h"
#include "Common/Fixed26_6.h"
#include "Common/Sha256.h"
#include "Core/AssetDatabase.h"
#include "FontCollectionTestSupport.h"
#include "Rendering/FontFace.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextShapingService.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"
#include "TextQualificationAssetTree.h"
#include "doctest.h"

#include <hb.h>
#include <hb-icu.h>
#include <hb-ot.h>
#include <unicode/uscript.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
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
using text::ResolvedFace;
using text::ResolvedFamily;
using text::ShapeBoundaryFlags;
using text::ShapedGlyph;
using text::ShapedRun;
using text::ShapeFeature;
using text::SourceByteRange;
using text::TextDiagnosticCode;
using text::UnicodeAnalysis;
using text::UnicodeTextBuffer;

// ── Fixture-tree GUIDs ──────────────────────────────────────────────────────
// Task 4.2가 커밋한 자격 트리의 고정 GUID. 이름이 아니라 이 상수들이 계약이다.
constexpr const char* kPrimaryFamily = "11111111111111111111111111111111";
constexpr const char* kLatinFont = "44444444444444444444444444444444";
constexpr const char* kDevanagariFont = "12121212121212121212121212121212";
constexpr const char* kArabicFont = "66666666666666666666666666666666";
constexpr const char* kKoreanFont = "77777777777777777777777777777777";

// 마지막 셰이핑 호출이 쓴 버퍼. Step 1e는 AllClustersMapToOriginalBytes를 인자
// 하나로 부르도록 계획서가 verbatim으로 지정하는데, 그 검사에는 원본 scalar
// 표가 필요하다. ShapingFixture가 셰이핑 직후 여기에 자기 버퍼를 남기므로, 그
// 한 인자짜리 형태는 언제나 "바로 앞 셰이핑 호출의 출력"에 대한 질문이다.
const UnicodeTextBuffer* g_lastShapedBuffer = nullptr;

// doctest는 DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING 없이 빌드되므로 const
// char*를 포인터 주소로 찍는다. 실패 메시지가 읽히려면 string으로 감싸야 한다.
std::string Label(const char* text) { return std::string(text); }

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::string();
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// ── Step 1: the explicit one-face/script reference runs ─────────────────────
// 이 표에는 family도 fallback 선택자도 없다. 각 행은 정확히 한 face 파일과 그
// face를 여는 데 필요한 값만 담는다.

struct ExplicitReferenceRun {
    std::filesystem::path facePath;
    std::string fontGuid;
    std::uint32_t faceIndex;
    std::string utf8;
    hb_direction_t direction;
    hb_script_t script;
    std::string language;
    molga::Fixed26_6 fontSize;
    molga::text::ShapeBoundaryFlags boundaries;
    std::vector<molga::text::ShapeFeature> features;
};
std::array<ExplicitReferenceRun, 3> ExplicitPinnedRuns() {
    const std::filesystem::path root = MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT;
    return {{
        {root / "fonts/NotoSans-Regular.ttf",
         "44444444444444444444444444444444", 0, u8"ffi",
         HB_DIRECTION_LTR, HB_SCRIPT_LATIN, "en",
         molga::Fixed26_6::FromRaw(16 * 64), {true, true},
         {{HB_TAG('l','i','g','a'), 1, {0, 3}}}},
        {root / "fonts/NotoSansArabic-Regular.ttf",
         "66666666666666666666666666666666", 0, u8"سلام",
         HB_DIRECTION_RTL, HB_SCRIPT_ARABIC, "ar",
         molga::Fixed26_6::FromRaw(16 * 64), {true, true}, {}},
        {root / "fonts/NotoSansDevanagari-Regular.ttf",
         "12121212121212121212121212121212", 0, u8"क्षि",
         HB_DIRECTION_LTR, HB_SCRIPT_DEVANAGARI, "hi",
         molga::Fixed26_6::FromRaw(16 * 64), {true, true}, {}},
    }};
}

// ── Step 1b/1c: the direct one-face HarfBuzz handle path ────────────────────
// 이 경로는 오직 run.facePath만 읽는다. FontFamilyResolver도, coverage
// preflight도, 다른 face도 보지 않는다. 프로덕션 셰이퍼가 무엇을 하든 이쪽은
// 고정된 HarfBuzz 참조로 남아야 하므로, 선택 로직을 여기에 복제하지 않는다.

struct DirectHarfBuzzHandles {
    std::vector<char> bytes;
    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;
    hb_buffer_t* buffer = nullptr;

    DirectHarfBuzzHandles() = default;
    DirectHarfBuzzHandles(const DirectHarfBuzzHandles&) = delete;
    DirectHarfBuzzHandles& operator=(const DirectHarfBuzzHandles&) = delete;
    // Step 1c: 모든 종료 경로에서 buffer, font, face, blob 순서로 파괴한다.
    ~DirectHarfBuzzHandles() {
        if (buffer != nullptr) hb_buffer_destroy(buffer);
        if (font != nullptr) hb_font_destroy(font);
        if (face != nullptr) hb_face_destroy(face);
        if (blob != nullptr) hb_blob_destroy(blob);
    }
};

std::vector<char> ReadAllBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    REQUIRE_MESSAGE(input.good(), path.string());
    const std::streamoff size = input.tellg();
    REQUIRE_MESSAGE(size > 0, path.string());
    input.seekg(0);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE_MESSAGE(input.good(), path.string());
    return bytes;
}

void OpenDirectHandles(const ExplicitReferenceRun& run,
                       DirectHarfBuzzHandles& handles) {
    handles.bytes = ReadAllBytes(run.facePath);
    handles.blob = hb_blob_create(
        handles.bytes.data(), static_cast<unsigned int>(handles.bytes.size()),
        HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    REQUIRE(handles.blob != nullptr);
    handles.face = hb_face_create(handles.blob, run.faceIndex);
    REQUIRE(handles.face != nullptr);
    handles.font = hb_font_create(handles.face);
    REQUIRE(handles.font != nullptr);
    hb_ot_font_set_funcs(handles.font);
    hb_font_set_scale(handles.font, run.fontSize.Raw(), run.fontSize.Raw());
    handles.buffer = hb_buffer_create();
    REQUIRE(handles.buffer != nullptr);
    hb_buffer_set_unicode_funcs(handles.buffer, hb_icu_get_unicode_funcs());
    hb_buffer_set_direction(handles.buffer, run.direction);
    hb_buffer_set_script(handles.buffer, run.script);
    hb_buffer_set_language(
        handles.buffer,
        hb_language_from_string(run.language.c_str(),
                                static_cast<int>(run.language.size())));
    hb_buffer_set_cluster_level(handles.buffer,
                                HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    unsigned int flags = HB_BUFFER_FLAG_DEFAULT;
    if (run.boundaries.beginningOfText) flags |= HB_BUFFER_FLAG_BOT;
    if (run.boundaries.endOfText) flags |= HB_BUFFER_FLAG_EOT;
    hb_buffer_set_flags(handles.buffer, static_cast<hb_buffer_flags_t>(flags));
}

std::vector<hb_feature_t> DirectFeatures(const ExplicitReferenceRun& run) {
    std::vector<hb_feature_t> features;
    features.reserve(run.features.size());
    for (const ShapeFeature& feature : run.features) {
        hb_feature_t converted{};
        converted.tag = feature.tag;
        converted.value = feature.value;
        converted.start = feature.sourceBytes.begin;
        converted.end = feature.sourceBytes.end;
        features.push_back(converted);
    }
    return features;
}

nlohmann::ordered_json DirectHarfBuzzSingleFaceRecord(
    const ExplicitReferenceRun& run) {
    DirectHarfBuzzHandles handles;
    OpenDirectHandles(run, handles);
    const std::string fontSha =
        molga::Sha256Bytes(handles.bytes.data(), handles.bytes.size());
    hb_buffer_add_utf8(handles.buffer, run.utf8.c_str(),
                       static_cast<int>(run.utf8.size()), 0,
                       static_cast<int>(run.utf8.size()));
    const std::vector<hb_feature_t> features = DirectFeatures(run);
    hb_shape(handles.font, handles.buffer, features.data(),
             static_cast<unsigned int>(features.size()));

    unsigned int count = 0;
    const hb_glyph_info_t* infos =
        hb_buffer_get_glyph_infos(handles.buffer, &count);
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(handles.buffer, &count);
    nlohmann::ordered_json record = nlohmann::ordered_json::array();
    for (unsigned int index = 0; index < count; ++index) {
        nlohmann::ordered_json glyph;
        glyph["fontSha"] = fontSha;
        glyph["faceIndex"] = run.faceIndex;
        glyph["glyphId"] = infos[index].codepoint;
        glyph["cluster"] = infos[index].cluster;
        glyph["advanceX"] = positions[index].x_advance;
        glyph["advanceY"] = positions[index].y_advance;
        glyph["offsetX"] = positions[index].x_offset;
        glyph["offsetY"] = positions[index].y_offset;
        record.push_back(std::move(glyph));
    }
    return record;
}

std::vector<std::uint32_t> DirectGlyphFlags(const ExplicitReferenceRun& run) {
    DirectHarfBuzzHandles handles;
    OpenDirectHandles(run, handles);
    hb_buffer_add_utf8(handles.buffer, run.utf8.c_str(),
                       static_cast<int>(run.utf8.size()), 0,
                       static_cast<int>(run.utf8.size()));
    const std::vector<hb_feature_t> features = DirectFeatures(run);
    hb_shape(handles.font, handles.buffer, features.data(),
             static_cast<unsigned int>(features.size()));
    unsigned int count = 0;
    const hb_glyph_info_t* infos =
        hb_buffer_get_glyph_infos(handles.buffer, &count);
    std::vector<std::uint32_t> flags;
    flags.reserve(count);
    for (unsigned int index = 0; index < count; ++index) {
        flags.push_back(static_cast<std::uint32_t>(
            hb_glyph_info_get_glyph_flags(&infos[index])));
    }
    return flags;
}

std::vector<Fixed26_6> DirectLigatureCarets(const ExplicitReferenceRun& run,
                                            std::uint32_t glyphId) {
    DirectHarfBuzzHandles handles;
    OpenDirectHandles(run, handles);
    unsigned int declared = 0;
    const unsigned int total = hb_ot_layout_get_ligature_carets(
        handles.font, run.direction, glyphId, 0, &declared, nullptr);
    std::vector<hb_position_t> raw(total);
    unsigned int filled = total;
    if (total > 0) {
        hb_ot_layout_get_ligature_carets(handles.font, run.direction, glyphId, 0,
                                         &filled, raw.data());
    }
    REQUIRE(filled == total);
    std::vector<Fixed26_6> carets;
    carets.reserve(total);
    for (unsigned int index = 0; index < total; ++index) {
        carets.push_back(Fixed26_6::FromRaw(raw[index]));
    }
    return carets;
}

// ── The committed qualification corpus, imported once per process ───────────
// 이 트리를 케이스마다 다시 복사하면 6개 폰트(합쳐 5.7MB)를 케이스 수만큼 다시
// 해시하고 다시 발행하게 된다. 어떤 케이스도 트리를 쓰지 않으므로 한 번만
// 만든다. 실패는 REQUIRE가 아니라 문자열로 남긴다: 정적 초기화 중의 REQUIRE는
// 어떤 케이스에도 속하지 않고, 첫 케이스만 실패한 뒤 나머지는 깨진 corpus를
// 조용히 쓰게 된다.
class ShapingCorpus {
public:
    ShapingCorpus()
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
    const molga::text::FontFamilyResolver& Resolver() const { return resolver_; }
    molga::text::FontRepository& Repository() { return repository_; }

private:
    QualificationAssetTreeFixture tree_;
    molga::AssetDatabase database_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
    molga::text::FontRepository repository_;
    molga::text::FontFamilyResolver resolver_;
    std::string error_;
};

ShapingCorpus& Corpus() {
    static ShapingCorpus corpus;
    return corpus;
}

// ── A non-zero face index the committed corpus cannot supply ────────────────
// 자격 트리의 여섯 폰트는 전부 단일 face 파일이라, 위 corpus로 셰이핑하는 어떤
// 케이스도 face index가 0이 아닌 상태를 보지 못한다. 그 상태에서는
// `glyph.faceIndex = state.face->faceIndex`를 상수 0으로 바꿔도 스위트 전체가
// 통과한다 — 폰트 collection을 쓰는 프로젝트에서는 그 회귀가 실패가 아니라
// "그럴듯하게 다른 글리프"로 나타나므로, 진단도 없이 atlas(Milestone 6)와
// 레이아웃(Milestone 7)까지 흘러간다.
//
// Task 5.1이 resolver 쪽에서 같은 벽을 만나 만든 collection 합성을 그대로 쓴다
// (FontCollectionTestSupport.h). 이 corpus는 자기 자격 트리 사본을 따로 들고
// 있다: 위 ShapingCorpus는 프로세스 하나에 하나뿐인 공유 상태라, 거기에 폰트를
// 더 쓰고 다시 스캔하면 다른 케이스가 읽는 카탈로그가 이 케이스 때문에
// 달라진다.
constexpr const char* kCollectionFont = "1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a";
constexpr std::uint32_t kAuthoredCollectionFaceIndex = 1U;

class CollectionFaceCorpus {
public:
    molga::text::VectorTextDiagnosticSink sink;

    CollectionFaceCorpus()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          repository_(database_),
          resolver_(database_, repository_) {
        test_support::AuthorTwoFaceCollectionFont(
            tree_.AssetsRoot() / "fonts", "latin-collection.ttf",
            "NotoSans-Regular.ttf", kCollectionFont,
            kAuthoredCollectionFaceIndex);
        std::string bindError;
        REQUIRE_MESSAGE(database_.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database_.ScanProject(tree_.AssetsRoot());
        REQUIRE(database_.Find(std::string(kCollectionFont)) != nullptr);
    }

    // 카탈로그가 저작한 face index로 묶인 후보 하나. 1이라는 값은 이 테스트가
    // 적어 넣는 것이 아니라 위 .meta에서 resolver를 거쳐 나오는 것이고,
    // `resource`도 정말 그 face로 열린 자원이다.
    ResolvedFace AuthoredFace() {
        const auto built = resolver_.BuildLegacySingleFace(
            kCollectionFont, {400, 100, molga::FontSlant::Upright}, sink);
        REQUIRE(built);
        REQUIRE(built->candidates.size() == 1U);
        ResolvedFace face = built->candidates.front();
        REQUIRE(face.resource != nullptr);
        REQUIRE(face.resource->rasterFace != nullptr);
        return face;
    }

private:
    QualificationAssetTreeFixture tree_;
    molga::AssetDatabase database_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
    molga::text::FontRepository repository_;
    molga::text::FontFamilyResolver resolver_;
};

// ── Step 1l: the focused shaping observations ───────────────────────────────
// 프로덕션 wrapper가 남긴 관찰을 item마다 모은다. 서비스는 호출마다 관찰을
// 비우므로(선택 결과에는 영향이 없다), 여러 item을 셰이핑하는 픽스처는 호출
// 사이에 스스로 누적해야 한다.
struct ShapingObservationLog {
    std::vector<char32_t> cmapRequirements;
    std::vector<text::detail::ShapingProbeNotdef> probeNotdefs;
    std::vector<text::detail::ShapingFinalSpanShape> finalSpanShapes;
    std::vector<text::detail::ShapingVariationDecision> variationDecisions;
    std::size_t selectedSpans = 0;
    std::size_t shapeCalls = 0;
    std::size_t shapeCallsWithIcuUnicodeFuncs = 0;

    void Merge(const text::detail::ShapingObservations& observations) {
        cmapRequirements.insert(cmapRequirements.end(),
                                observations.cmapRequirements.begin(),
                                observations.cmapRequirements.end());
        probeNotdefs.insert(probeNotdefs.end(),
                            observations.probeNotdefs.begin(),
                            observations.probeNotdefs.end());
        finalSpanShapes.insert(finalSpanShapes.end(),
                               observations.finalSpanShapes.begin(),
                               observations.finalSpanShapes.end());
        variationDecisions.insert(variationDecisions.end(),
                                  observations.variationDecisions.begin(),
                                  observations.variationDecisions.end());
        selectedSpans += observations.selectedSpans;
        shapeCalls += observations.shapeCalls;
        shapeCallsWithIcuUnicodeFuncs +=
            observations.shapeCallsWithIcuUnicodeFuncs;
    }
};

bool RangesIntersect(SourceByteRange a, SourceByteRange b) {
    return a.begin < b.end && b.begin < a.end;
}

// ── The ready-runtime shaping fixture ───────────────────────────────────────
class ShapingFixture {
public:
    molga::text::VectorTextDiagnosticSink sink;

    ShapingFixture(std::string utf8, std::string locale)
        : originalUtf8_(std::move(utf8)) {
        REQUIRE_MESSAGE(Corpus().Error().empty(), Corpus().Error());
        auto buffer = UnicodeTextBuffer::Build(originalUtf8_, sink);
        REQUIRE(buffer);
        buffer_ = std::move(buffer);
        auto analysis = text::UnicodeTextAnalyzer::Analyze(
            *buffer_, {locale, text::BaseDirection::Auto}, sink);
        REQUIRE(analysis);
        analysis_ = std::move(analysis);
        auto family = Corpus().Resolver().BuildCandidates(
            kPrimaryFamily, {400, 100, molga::FontSlant::Upright}, sink);
        REQUIRE(family);
        family_ = std::move(*family);
        style_.language = std::move(locale);
    }

    const UnicodeTextBuffer& Buffer() const {
        REQUIRE(buffer_.has_value());
        return *buffer_;
    }
    const UnicodeAnalysis& Analysis() const {
        REQUIRE(analysis_.has_value());
        return *analysis_;
    }
    const std::string& OriginalUtf8() const noexcept { return originalUtf8_; }
    const molga::text::ShapeStyle& ShapeStyle() const noexcept {
        return style_;
    }

    // 저작된 후보 순서를 보존한 채 지정한 폰트만 남긴다. 순서를 다시 만들지
    // 않는 것이 이 헬퍼의 계약이다.
    void RestrictCandidatesToFonts(const std::vector<std::string>& fontGuids) {
        std::vector<ResolvedFace> kept;
        for (const ResolvedFace& face : family_.candidates) {
            if (std::find(fontGuids.begin(), fontGuids.end(), face.fontGuid) !=
                fontGuids.end()) {
                kept.push_back(face);
            }
        }
        REQUIRE(kept.size() == fontGuids.size());
        family_.candidates = std::move(kept);
    }
    void RestrictCandidatesToFont(const std::string& fontGuid) {
        RestrictCandidatesToFonts({fontGuid});
    }

    // 후보 목록을 통째로 갈아 끼운다. 커밋된 자격 트리 밖에서 묶인 face를
    // 셰이핑에 넣는 유일한 경로다. 셰이퍼가 ResolvedFamily에서 읽는 것은
    // candidates뿐이므로(TextShapingService.cpp), 나머지 필드를 그대로 두는
    // 것이 관찰을 왜곡하지 않는다.
    void UseCandidates(std::vector<ResolvedFace> candidates) {
        REQUIRE_FALSE(candidates.empty());
        family_.candidates = std::move(candidates);
    }
    void UseResolvedFamilyWithNoCandidates() { family_.candidates.clear(); }

    // 첫 분석 item부터 마지막 item까지 논리 순서대로 셰이핑한다.
    //
    // "첫 item만"이 아닌 이유를 적어 둔다. Step 1e의 u8"Aक्‍षB"는 script가
    // Latin -> Devanagari -> Latin으로 바뀌므로 UnicodeTextAnalyzer가 item을
    // 셋으로 나눈다. 첫 item만 셰이핑하면 그 케이스가 검사하려는 grapheme 1이
    // 출력에 아예 없어져, 단언이 실패하는 대신 아무것도 증명하지 못한다.
    // ShapeAllItems()와 같은 동작이며, 계획서의 두 verbatim 블록이 부르는 두
    // 이름을 모두 남겨 둔다.
    std::optional<std::vector<ShapedRun>> ShapeFirstItem() {
        return ShapeItems();
    }
    std::optional<std::vector<ShapedRun>> ShapeAllItems() { return ShapeItems(); }

    const AnalysisItem& AnalysisItemAtByte(std::uint32_t byteOffset) const {
        for (const AnalysisItem& item : Analysis().Items()) {
            if (byteOffset >= item.sourceBytes.begin &&
                byteOffset < item.sourceBytes.end) {
                return item;
            }
        }
        REQUIRE_MESSAGE(false, ("no analysis item contains byte " +
                                std::to_string(byteOffset)));
        return Analysis().Items().front();
    }

    std::size_t CmapRequirementCount(char32_t scalar) const {
        return static_cast<std::size_t>(
            std::count(log_.cmapRequirements.begin(),
                       log_.cmapRequirements.end(), scalar));
    }

    bool ProbeNotdefIntersectsGrapheme(std::uint32_t graphemeIndex) const {
        const SourceByteRange target = GraphemeBytes(graphemeIndex);
        for (const text::detail::ShapingProbeNotdef& notdef : log_.probeNotdefs) {
            if (RangesIntersect(notdef.notdefBytes, target)) return true;
        }
        return false;
    }

    std::size_t FinalSelectedContextShapeCountForGrapheme(
        std::uint32_t graphemeIndex) const {
        std::size_t count = 0;
        for (const text::detail::ShapingFinalSpanShape& shape :
             log_.finalSpanShapes) {
            if (graphemeIndex >= shape.spanGraphemes.begin &&
                graphemeIndex < shape.spanGraphemes.end) {
                ++count;
            }
        }
        return count;
    }

    bool FinalSelectedContextCoveredCompleteItem(
        std::uint32_t graphemeIndex) const {
        bool covered = false;
        for (const text::detail::ShapingFinalSpanShape& shape :
             log_.finalSpanShapes) {
            if (graphemeIndex < shape.spanGraphemes.begin ||
                graphemeIndex >= shape.spanGraphemes.end) {
                continue;
            }
            if (!(shape.contextBytes == shape.itemBytes)) return false;
            covered = true;
        }
        return covered;
    }

    // Step 7a의 관찰창. explicitRecord=true는 선택된 face의 cmap format 14가
    // 그 쌍을 직접 이름 붙였다는 뜻이고, 그 사실은 이 기록 말고는 어디에서도
    // 드러나지 않는다(base가 평범한 cmap에도 있으면 결과가 같기 때문이다).
    std::size_t ExplicitUvsRecordCount(char32_t base, char32_t selector) const {
        std::size_t count = 0;
        for (const text::detail::ShapingVariationDecision& decision :
             log_.variationDecisions) {
            if (decision.base == base && decision.selector == selector &&
                decision.explicitRecord) {
                ++count;
            }
        }
        return count;
    }
    std::size_t VariationDecisionCount(char32_t base, char32_t selector) const {
        std::size_t count = 0;
        for (const text::detail::ShapingVariationDecision& decision :
             log_.variationDecisions) {
            if (decision.base == base && decision.selector == selector) ++count;
        }
        return count;
    }

    std::size_t FinalSelectedSpanCount() const { return log_.selectedSpans; }
    std::size_t FinalShapeCallsForSelectedSpan() const {
        return log_.finalSpanShapes.size();
    }
    std::size_t ShapeCalls() const { return log_.shapeCalls; }
    std::size_t ShapeCallsWithIcuUnicodeFuncs() const {
        return log_.shapeCallsWithIcuUnicodeFuncs;
    }

    const std::vector<ResolvedFace>& Candidates() const noexcept {
        return family_.candidates;
    }

    // byte offset이 속한 grapheme의 색인. 사람이 센 색인을 고정 상수로 적으면
    // 픽스처 문자열이 조금만 바뀌어도 조용히 다른 grapheme을 시험하게 된다.
    std::uint32_t GraphemeIndexForByte(std::uint32_t byteOffset) const {
        const std::vector<std::uint32_t>& boundaries =
            Analysis().GraphemeBoundaries();
        for (std::size_t index = 0; index + 1U < boundaries.size(); ++index) {
            if (byteOffset >= boundaries[index] &&
                byteOffset < boundaries[index + 1U]) {
                return static_cast<std::uint32_t>(index);
            }
        }
        REQUIRE_MESSAGE(false, ("no grapheme contains byte " +
                                std::to_string(byteOffset)));
        return 0;
    }

    SourceByteRange GraphemeBytes(std::uint32_t graphemeIndex) const {
        const std::vector<std::uint32_t>& boundaries =
            Analysis().GraphemeBoundaries();
        REQUIRE(graphemeIndex + 1U < boundaries.size());
        return SourceByteRange{boundaries[graphemeIndex],
                               boundaries[graphemeIndex + 1U]};
    }

private:
    std::optional<std::vector<ShapedRun>> ShapeItems() {
        std::vector<ShapedRun> runs;
        g_lastShapedBuffer = &Buffer();
        for (const AnalysisItem& item : Analysis().Items()) {
            const bool first = item.sourceBytes.begin == 0U;
            const bool last =
                item.sourceBytes.end == originalUtf8_.size();
            auto shaped = service_.ShapeAnalysisItem(
                *buffer_, *analysis_, item, family_, style_, {first, last},
                sink);
            log_.Merge(text::detail::Observations());
            if (!shaped) return std::nullopt;
            for (ShapedRun& run : *shaped) runs.push_back(std::move(run));
        }
        return runs;
    }

    std::string originalUtf8_;
    std::optional<UnicodeTextBuffer> buffer_;
    std::optional<UnicodeAnalysis> analysis_;
    ResolvedFamily family_;
    molga::text::ShapeStyle style_;
    molga::text::TextShapingService service_;
    ShapingObservationLog log_;
};

ShapingFixture LoadTextFixture(std::string utf8, std::string locale) {
    return ShapingFixture(std::move(utf8), std::move(locale));
}

// ── Observation helpers over shaped output ──────────────────────────────────

std::vector<ShapedGlyph> FlattenGlyphs(const std::vector<ShapedRun>& runs) {
    std::vector<ShapedGlyph> glyphs;
    for (const ShapedRun& run : runs) {
        for (const ShapedGlyph& glyph : run.glyphs) glyphs.push_back(glyph);
    }
    return glyphs;
}

std::vector<ShapedGlyph> GlyphsForGrapheme(const std::vector<ShapedRun>& runs,
                                           std::uint32_t graphemeIndex) {
    std::vector<ShapedGlyph> selected;
    for (const ShapedGlyph& glyph : FlattenGlyphs(runs)) {
        if (graphemeIndex >= glyph.graphemes.begin &&
            graphemeIndex < glyph.graphemes.end) {
            selected.push_back(glyph);
        }
    }
    return selected;
}

// glyph가 하나도 없으면 false다. "한 face만 썼다"가 "아무 glyph도 없다"로
// 조용히 참이 되면, grapheme을 통째로 잃어버린 회귀가 이 단언을 통과한다.
bool AllGlyphsForGraphemeUseOneFace(const std::vector<ShapedRun>& runs,
                                    std::uint32_t graphemeIndex) {
    const std::vector<ShapedGlyph> glyphs = GlyphsForGrapheme(runs, graphemeIndex);
    if (glyphs.empty()) return false;
    for (const ShapedGlyph& glyph : glyphs) {
        if (glyph.faceResource != glyphs.front().faceResource) return false;
        if (glyph.faceIndex != glyphs.front().faceIndex) return false;
        if (glyph.fontGuid != glyphs.front().fontGuid) return false;
    }
    return true;
}

std::string FaceGuidForGrapheme(const std::vector<ShapedRun>& runs,
                                std::uint32_t graphemeIndex) {
    const std::vector<ShapedGlyph> glyphs = GlyphsForGrapheme(runs, graphemeIndex);
    if (glyphs.empty()) return std::string();
    for (const ShapedGlyph& glyph : glyphs) {
        if (glyph.fontGuid != glyphs.front().fontGuid) return std::string();
    }
    return glyphs.front().fontGuid;
}

bool AnyMissingGlyph(const std::vector<ShapedRun>& runs) {
    for (const ShapedGlyph& glyph : FlattenGlyphs(runs)) {
        if (glyph.missing) return true;
    }
    return false;
}

bool HasGlyphSourceRange(const std::vector<ShapedRun>& runs,
                         SourceByteRange range) {
    for (const ShapedGlyph& glyph : FlattenGlyphs(runs)) {
        if (glyph.sourceBytes == range) return true;
    }
    return false;
}

// Step 1l: 출력 cluster를 오직 DecodedScalar::sourceBytes.begin과만 비교한다.
bool AllClustersAreDecodedScalarByteStarts(const std::vector<ShapedRun>& runs,
                                           const UnicodeTextBuffer& buffer) {
    const std::vector<ShapedGlyph> glyphs = FlattenGlyphs(runs);
    if (glyphs.empty()) return false;
    for (const ShapedGlyph& glyph : glyphs) {
        bool found = false;
        for (const text::DecodedScalar& scalar : buffer.Scalars()) {
            if (scalar.sourceBytes.begin == glyph.sourceBytes.begin) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool AllClustersMapToOriginalBytes(const std::vector<ShapedRun>& runs,
                                   const UnicodeTextBuffer& buffer) {
    const std::vector<ShapedGlyph> glyphs = FlattenGlyphs(runs);
    if (glyphs.empty()) return false;
    for (const ShapedGlyph& glyph : glyphs) {
        if (glyph.sourceBytes.begin >= glyph.sourceBytes.end) return false;
        if (glyph.sourceBytes.end > buffer.OriginalUtf8().size()) return false;
    }
    return AllClustersAreDecodedScalarByteStarts(runs, buffer);
}

// Step 1e가 부르는 한 인자짜리 형태. 버퍼는 바로 앞 셰이핑 호출의 것이다.
bool AllClustersMapToOriginalBytes(const std::vector<ShapedRun>& runs) {
    REQUIRE(g_lastShapedBuffer != nullptr);
    return AllClustersMapToOriginalBytes(runs, *g_lastShapedBuffer);
}

// 방출된 원본 byte 구간이 [0, totalBytes)를 빈틈도 겹침도 없이 정확히 덮는가.
// 같은 cluster에서 나온 glyph 여러 개는 같은 구간을 공유하므로 중복은 접는다.
bool SourceByteCoverIsExact(const std::vector<ShapedRun>& runs,
                            std::size_t totalBytes) {
    std::vector<SourceByteRange> ranges;
    for (const ShapedGlyph& glyph : FlattenGlyphs(runs)) {
        ranges.push_back(glyph.sourceBytes);
    }
    if (ranges.empty()) return false;
    std::sort(ranges.begin(), ranges.end(),
              [](SourceByteRange a, SourceByteRange b) {
                  return a.begin != b.begin ? a.begin < b.begin : a.end < b.end;
              });
    ranges.erase(std::unique(ranges.begin(), ranges.end(),
                             [](SourceByteRange a, SourceByteRange b) {
                                 return a == b;
                             }),
                 ranges.end());
    std::uint32_t cursor = 0;
    for (const SourceByteRange& range : ranges) {
        if (range.begin != cursor || range.end <= range.begin) return false;
        cursor = range.end;
    }
    return cursor == static_cast<std::uint32_t>(totalBytes);
}

// UTF-16 unit offset이 cluster로 새어 나온 경우를 고발한다. 그런 offset은 렌더
// 결과로는 드러나지 않고 caret과 진단만 조용히 다른 byte를 가리킨다.
//
// 두 좌표계가 갈라지는 곳에서만 뜻이 있다. 모든 문자가 1 byte / 1 unit인
// 픽스처에서는 byte offset과 unit offset이 같은 값이라 이 검사가 참을 말해도
// 아무것도 증명하지 못하므로, 비BMP 문자가 들어 있는 픽스처에서도 함께 부른다.
bool AnyClusterIsUtf16OnlyOffset(const std::vector<ShapedRun>& runs,
                                 const UnicodeTextBuffer& buffer) {
    for (const ShapedGlyph& glyph : FlattenGlyphs(runs)) {
        const std::uint32_t offset = glyph.sourceBytes.begin;
        bool isScalarStart = false;
        for (const text::DecodedScalar& scalar : buffer.Scalars()) {
            if (scalar.sourceBytes.begin == offset) {
                isScalarStart = true;
                break;
            }
        }
        // 원본 scalar 시작이 아닌 값은 byte 좌표가 아니다. 큰 offset을
        // 건너뛰지 않는 것이 중요하다: 원본 길이를 넘어선 cluster야말로
        // 좌표계가 샜다는 가장 분명한 증거다.
        if (!isScalarStart) return true;
        if (!buffer.Utf16ForSourceBytes(glyph.sourceBytes)) return true;
    }
    return false;
}

std::size_t CountDiagnostics(const molga::text::VectorTextDiagnosticSink& sink,
                             TextDiagnosticCode code) {
    std::size_t count = 0;
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) ++count;
    }
    return count;
}

bool HasDiagnostic(const molga::text::VectorTextDiagnosticSink& sink,
                   TextDiagnosticCode code) {
    return CountDiagnostics(sink, code) > 0;
}

// ── Step 1d: the production side of the reference comparison ────────────────
// 프로덕션 셰이퍼를 "정확히 잠긴 face 하나"만 담은 ResolvedFamily로 부른다.
// 프로덕션 family/fallback 선택은 전혀 부르지 않는다.

std::vector<ShapedRun> ProductionShapedRunsForRun(const ExplicitReferenceRun& run,
                                                  ShapedGlyph* firstGlyphOut) {
    REQUIRE_MESSAGE(Corpus().Error().empty(), Corpus().Error());
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = UnicodeTextBuffer::Build(run.utf8, sink);
    REQUIRE(buffer);
    auto analysis = text::UnicodeTextAnalyzer::Analyze(
        *buffer, {run.language, text::BaseDirection::Auto}, sink);
    REQUIRE(analysis);
    // 참조 표의 direction/script는 분석이 실제로 내는 값이어야 한다. 여기서
    // 갈라지면 아래 비교는 프로덕션이 아니라 표를 시험하게 된다.
    REQUIRE(analysis->Items().size() == 1U);
    const AnalysisItem& item = analysis->Items().front();
    CHECK(hb_icu_script_to_script(static_cast<UScriptCode>(item.scriptCode)) ==
          run.script);
    CHECK(((item.embeddingLevel & 1U) != 0U ? HB_DIRECTION_RTL
                                            : HB_DIRECTION_LTR) ==
          run.direction);

    auto resource = Corpus().Repository().Load(run.fontGuid, run.faceIndex, sink);
    REQUIRE_MESSAGE(resource.has_value(), Label(run.fontGuid.c_str()));
    ResolvedFace locked;
    locked.fontGuid = run.fontGuid;
    locked.fontRevision = (*resource)->artifactSha256 + ":" +
                          std::to_string(run.faceIndex);
    locked.faceIndex = run.faceIndex;
    locked.authoredFaceIndex = 0;
    locked.resource = *resource;
    ResolvedFamily family;
    family.requestedGuid = run.fontGuid;
    family.candidates.push_back(locked);

    molga::text::ShapeStyle style;
    style.fontSize = run.fontSize;
    style.language = run.language;
    style.orderedFeatures = run.features;
    molga::text::TextShapingService service;
    auto shaped = service.ShapeAnalysisItem(*buffer, *analysis, item, family,
                                            style, run.boundaries, sink);
    REQUIRE(shaped);
    CHECK_FALSE(HasDiagnostic(sink, TextDiagnosticCode::MissingGlyph));
    // Step 3a: 크기는 advance나 bitmap 경계에서 역산하지 않고 style에서
    // 그대로 복사된다. 어떤 참조 행도 이 값을 우연히 맞힐 수 없다.
    for (const ShapedGlyph& glyph : FlattenGlyphs(*shaped)) {
        CHECK(glyph.fontSize == run.fontSize);
    }
    if (firstGlyphOut != nullptr) {
        const std::vector<ShapedGlyph> glyphs = FlattenGlyphs(*shaped);
        REQUIRE_FALSE(glyphs.empty());
        *firstGlyphOut = glyphs.front();
    }
    return *shaped;
}

nlohmann::ordered_json GlyphRecordJson(const std::vector<ShapedRun>& runs) {
    nlohmann::ordered_json record = nlohmann::ordered_json::array();
    for (const ShapedGlyph& glyph : FlattenGlyphs(runs)) {
        REQUIRE(glyph.faceResource != nullptr);
        nlohmann::ordered_json entry;
        entry["fontSha"] = glyph.faceResource->artifactSha256;
        entry["faceIndex"] = glyph.faceIndex;
        entry["glyphId"] = glyph.glyphId;
        entry["cluster"] = glyph.sourceBytes.begin;
        entry["advanceX"] = glyph.advanceX.Raw();
        entry["advanceY"] = glyph.advanceY.Raw();
        entry["offsetX"] = glyph.offsetX.Raw();
        entry["offsetY"] = glyph.offsetY.Raw();
        record.push_back(std::move(entry));
    }
    return record;
}

nlohmann::ordered_json ProductionSingleResolvedFaceRecord(
    const ExplicitReferenceRun& run) {
    return GlyphRecordJson(ProductionShapedRunsForRun(run, nullptr));
}

// ── The direct reference for one span inside a larger context array ─────────
// Step 5b/9b가 요구하는 호출 형태 그대로다: 완전한 sanitized 배열과 그 안의
// span offset/count, 그리고 그 span의 진짜 끝에서 나온 BOT/EOT. cluster는 여기서
// 비교하지 않는다 — 프로덕션은 cluster를 원본 byte 시작으로 다시 쓰고 이쪽은
// 배열 색인 그대로이므로, 두 좌표계가 아니라 glyph와 위치를 견준다.
nlohmann::ordered_json DirectHarfBuzzSpanShape(
    const ExplicitReferenceRun& run, const std::vector<hb_codepoint_t>& text,
    unsigned int spanOffset, unsigned int spanCount) {
    DirectHarfBuzzHandles handles;
    OpenDirectHandles(run, handles);
    hb_buffer_add_codepoints(handles.buffer, text.data(),
                             static_cast<int>(text.size()),
                             static_cast<int>(spanOffset),
                             static_cast<int>(spanCount));
    const std::vector<hb_feature_t> features = DirectFeatures(run);
    hb_shape(handles.font, handles.buffer, features.data(),
             static_cast<unsigned int>(features.size()));
    unsigned int count = 0;
    const hb_glyph_info_t* infos =
        hb_buffer_get_glyph_infos(handles.buffer, &count);
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(handles.buffer, &count);
    nlohmann::ordered_json record = nlohmann::ordered_json::array();
    for (unsigned int index = 0; index < count; ++index) {
        nlohmann::ordered_json glyph;
        glyph["glyphId"] = infos[index].codepoint;
        glyph["advanceX"] = positions[index].x_advance;
        glyph["advanceY"] = positions[index].y_advance;
        glyph["offsetX"] = positions[index].x_offset;
        glyph["offsetY"] = positions[index].y_offset;
        record.push_back(std::move(glyph));
    }
    return record;
}

nlohmann::ordered_json ShapeOnlyJson(const std::vector<ShapedGlyph>& glyphs) {
    nlohmann::ordered_json record = nlohmann::ordered_json::array();
    for (const ShapedGlyph& glyph : glyphs) {
        nlohmann::ordered_json entry;
        entry["glyphId"] = glyph.glyphId;
        entry["advanceX"] = glyph.advanceX.Raw();
        entry["advanceY"] = glyph.advanceY.Raw();
        entry["offsetX"] = glyph.offsetX.Raw();
        entry["offsetY"] = glyph.offsetY.Raw();
        record.push_back(std::move(entry));
    }
    return record;
}

// 임포트된 coverage에서 value가 어떤 range의 끝점(begin 또는 end)인가.
// 끝점이 아닌 값만 시험하면 CoverageContains의 비교를 <=에서 <로 바꾸는 회귀가
// 보이지 않는다 — 그 회귀는 정확히 끝점만 tofu로 만든다.
bool CoverageRangeEndpoint(const ResolvedFace& face, char32_t value) {
    REQUIRE(face.resource != nullptr);
    REQUIRE(face.resource->asset != nullptr);
    for (const std::pair<char32_t, char32_t>& range :
         face.resource->asset->coverage) {
        if (range.first == value || range.second == value) return true;
    }
    return false;
}

} // namespace

// ── Step 1d ────────────────────────────────────────────────────────────────
TEST_CASE("production shaper matches pinned HarfBuzz reference records") {
    nlohmann::ordered_json records = nlohmann::ordered_json::array();
    for (const ExplicitReferenceRun& run : ExplicitPinnedRuns()) {
        const auto reference = DirectHarfBuzzSingleFaceRecord(run);
        const auto production = ProductionSingleResolvedFaceRecord(run);
        CHECK(production == reference);
        records.push_back(production);
    }
    // 이 비교는 raw byte 비교다. nlohmann::dump는 끝에 개행을 붙이지 않으므로
    // shaping.json은 ']'로 끝나고 개행이 없다. 파일을 POSIX 관례대로 개행으로
    // 끝나게 "고치는" 편집기/포매터/훅은 이 케이스를 깨뜨리는데, 그 diff는
    // 리뷰에서 보이지 않고 실패 메시지는 JSON 본문에 묻힌다.
    CHECK(records.dump(2) == ReadFile(MOLGA_TEXT_EXPECTED_SHAPING));
}

// Step 1g의 짝. not-ready 프로세스는 계수기를 올릴 수 없으므로 "0이다"라는
// 단언의 성공 증인이 그쪽에는 없다. 계수기가 0을 돌려주도록 굳어 버리면 저쪽
// 케이스는 그대로 통과하고 여기서만 무너진다.
TEST_CASE("a ready shaping call creates HarfBuzz objects the counter sees") {
    molga::text::detail::ResetHarfBuzzObjectCreationCount();
    REQUIRE(molga::text::detail::HarfBuzzObjectCreationCount() == 0U);
    auto fixture = LoadTextFixture(u8"ffi", "en");
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    // "0보다 크다"로는 부족하다. 계수기가 세는 것은 blob, face, font, buffer,
    // language, ICU unicode funcs 여섯이고, 한 face로 한 item을 셰이핑하면 그
    // 여섯이 전부 만들어진다. 하한을 6으로 두지 않으면 무거운 셋(blob/face/
    // font)의 증가를 지워도 not-ready 쪽 "== 0"은 그대로 통과하고, 그 순간
    // 계수기의 주장은 "buffer를 만들지 않았다"로 조용히 줄어든다.
    CHECK(molga::text::detail::HarfBuzzObjectCreationCount() >= 6U);

    // 셰이핑은 전부 ICU Unicode funcs를 단 buffer로 이루어졌다. 이 대입이
    // 빠지면 HarfBuzz는 내장 UCD 표로 셰이핑하는데, 이 corpus에서는 출력이
    // 같아서 다른 어떤 단언도 움직이지 않는다.
    CHECK(fixture.ShapeCalls() > 0U);
    CHECK(fixture.ShapeCallsWithIcuUnicodeFuncs() == fixture.ShapeCalls());
}

// ── Step 1e ────────────────────────────────────────────────────────────────
TEST_CASE("fallback never splits an extended grapheme") {
    auto fixture = LoadTextFixture(u8"Aक्‍षB", "und");
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    CHECK(AllGlyphsForGraphemeUseOneFace(*shaped, 1));
    CHECK(AllClustersMapToOriginalBytes(*shaped));
}

// ── Step 1f ────────────────────────────────────────────────────────────────
TEST_CASE("all-face failure emits one resource-free grapheme record") {
    auto fixture = LoadTextFixture(u8"👩‍🚀", "und");
    fixture.UseResolvedFamilyWithNoCandidates();
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    const auto glyphs = FlattenGlyphs(*shaped);
    REQUIRE(glyphs.size() == 1);
    CHECK(glyphs[0].missing);
    CHECK_FALSE(glyphs[0].faceResource);
    CHECK(glyphs[0].sourceBytes.begin == 0);
    CHECK(glyphs[0].sourceBytes.end == fixture.OriginalUtf8().size());
    CHECK(glyphs[0].graphemes.begin == 0);
    CHECK(glyphs[0].graphemes.end == 1);
    CHECK(glyphs[0].advanceX == fixture.ShapeStyle().fontSize);
    CHECK(HasDiagnostic(fixture.sink,
          molga::text::TextDiagnosticCode::MissingGlyph));
}

// ── Step 1h ────────────────────────────────────────────────────────────────
TEST_CASE("join controls and supported variation selectors do not force fallback") {
    auto join = LoadTextFixture(u8"ن\u200Dن", "ar");
    const auto joined = join.ShapeFirstItem();
    REQUIRE(joined);
    CHECK_FALSE(AnyMissingGlyph(*joined));
    CHECK(join.CmapRequirementCount(U'\u200D') == 0);

    auto variation = LoadTextFixture(u8"！\uFE00", "ja");
    const auto varied = variation.ShapeFirstItem();
    REQUIRE(varied);
    CHECK_FALSE(AnyMissingGlyph(*varied));
    CHECK(AllGlyphsForGraphemeUseOneFace(*varied, 0));
    CHECK(FaceGuidForGrapheme(*varied, 0) ==
          "77777777777777777777777777777777");
    CHECK(variation.CmapRequirementCount(U'\uFE00') == 0);
}

// Step 7a: 변이 선택자 판정은 선택된 face의 cmap format 14를 실제로 읽어서
// 나온다. 두 결과가 모두 관찰되어야 한다 — 기록이 있는 쌍과 없는 쌍은 서로 다른
// 경로로 받아들여지고, 어느 쪽도 선택자 자신의 glyph를 요구하지 않는다. 위
// 케이스만으로는 format 14 파싱을 통째로 지워도 통과한다: base가 평범한 cmap에
// 이미 있어서 결과가 같기 때문이다.
TEST_CASE("variation sequences are decided by the face's own UVS records") {
    auto named = LoadTextFixture(u8"！\uFE00", "ja");
    const auto namedShaped = named.ShapeFirstItem();
    REQUIRE(namedShaped);
    CHECK(FaceGuidForGrapheme(*namedShaped, 0) == std::string(kKoreanFont));
    // NotoSansKR은 (U+FF01, VS1)을 format 14에 직접 이름 붙인다. 앞선 후보들은
    // 같은 쌍을 이름 붙이지 않으므로 판정 자체는 여러 번 일어난다.
    CHECK(named.ExplicitUvsRecordCount(U'\uFF01', U'\uFE00') == 1);
    CHECK(named.VariationDecisionCount(U'\uFF01', U'\uFE00') > 1);

    // 기록이 없는 쌍은 거절이 아니라 base의 기본 표현이다. 거절하면 저작자가
    // 고칠 수 없는 tofu가 나온다.
    auto unnamed = LoadTextFixture(u8"A\uFE00", "und");
    const auto unnamedShaped = unnamed.ShapeFirstItem();
    REQUIRE(unnamedShaped);
    CHECK_FALSE(AnyMissingGlyph(*unnamedShaped));
    CHECK(FaceGuidForGrapheme(*unnamedShaped, 0) == std::string(kLatinFont));
    CHECK(unnamed.VariationDecisionCount(U'A', U'\uFE00') == 1);
    CHECK(unnamed.ExplicitUvsRecordCount(U'A', U'\uFE00') == 0);
    CHECK(unnamed.CmapRequirementCount(U'\uFE00') == 0);
}

// ── Step 1i ────────────────────────────────────────────────────────────────
TEST_CASE("surrounding notdef does not reject target and selection reshapes context") {
    auto fixture = LoadTextFixture(u8"👩 क्षि", "hi");
    fixture.RestrictCandidatesToFont(
        "12121212121212121212121212121212");
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    CHECK(fixture.ProbeNotdefIntersectsGrapheme(0));
    CHECK_FALSE(fixture.ProbeNotdefIntersectsGrapheme(2));
    CHECK(FaceGuidForGrapheme(*shaped, 2) ==
          "12121212121212121212121212121212");
    CHECK(fixture.FinalSelectedContextShapeCountForGrapheme(2) == 1);
    CHECK(fixture.FinalSelectedContextCoveredCompleteItem(2));
}

// Step 9a: 하나의 AnalysisItem 안에서 서로 다른 face를 고른 두 인접 grapheme은
// 절대 합쳐지지 않는다.
//
// 이 케이스가 없으면 span 합침 조건에서 face 정체성 비교를 통째로 빼도 스위트
// 전체가 통과한다. 다른 모든 픽스처는 script가 바뀌는 자리에서
// UnicodeTextAnalyzer가 item을 이미 나눠 주므로, 한 item 안에 face 경계가
// 존재하지 않기 때문이다. 여기서는 emoji와 공백이 Common script라 주위의
// Devanagari를 물려받아 셋이 한 item이 되고, 그 안에서 세 grapheme이 각각
// 없음 / Latin / Devanagari로 갈린다.
TEST_CASE("adjacent graphemes that chose different faces stay in separate spans") {
    auto fixture = LoadTextFixture(u8"👩 क्षि", "hi");
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    REQUIRE(fixture.Analysis().Items().size() == 1U);
    REQUIRE(fixture.Analysis().GraphemeBoundaries().size() == 4U);

    CHECK(fixture.FinalSelectedSpanCount() == 3);
    // 없는 grapheme은 hb_shape를 부르지 않으므로 최종 셰이핑은 두 번뿐이다.
    CHECK(fixture.FinalShapeCallsForSelectedSpan() == 2);
    CHECK(FaceGuidForGrapheme(*shaped, 1) == std::string(kLatinFont));
    CHECK(FaceGuidForGrapheme(*shaped, 2) == std::string(kDevanagariFont));
    CHECK(AllGlyphsForGraphemeUseOneFace(*shaped, 1));
    CHECK(AllGlyphsForGraphemeUseOneFace(*shaped, 2));

    // emoji 하나만 없음이고 나머지 둘은 진짜 glyph다. 합쳐 버리는 회귀는 뒤
    // grapheme을 앞 grapheme의 폰트로 셰이핑해 .notdef를 내면서도 missing
    // 기록은 만들지 않으므로, 위 두 face 단언이 유일한 증인이다.
    for (const ShapedGlyph& glyph : FlattenGlyphs(*shaped)) {
        CHECK(glyph.missing == (glyph.sourceBytes.begin < 4U));
    }
    CHECK(SourceByteCoverIsExact(*shaped, fixture.OriginalUtf8().size()));

    // 이 픽스처는 emoji가 4 byte / 2 UTF-16 unit이라 byte 좌표와 unit 좌표가
    // 실제로 갈라진다. Step 1j의 픽스처는 모든 문자가 1 byte / 1 unit이라 같은
    // 검사가 그곳에서는 두 좌표계를 구분하지 못한다.
    CHECK(AllClustersAreDecodedScalarByteStarts(*shaped, fixture.Buffer()));
    CHECK_FALSE(AnyClusterIsUtf16OnlyOffset(*shaped, fixture.Buffer()));
}

// ── Step 1j ────────────────────────────────────────────────────────────────
TEST_CASE("sanitized replacement glyph clusters remain original byte starts") {
    auto fixture = LoadTextFixture(std::string("A\xF0\x28\x8C\x28Z", 6),
                                   "und");
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    CHECK(AllClustersAreDecodedScalarByteStarts(*shaped, fixture.Buffer()));
    CHECK(HasGlyphSourceRange(*shaped, {1, 2}));
    CHECK_FALSE(AnyClusterIsUtf16OnlyOffset(*shaped, fixture.Buffer()));
}

// ── Step 1k ────────────────────────────────────────────────────────────────
TEST_CASE("every shaped glyph retains its exact analysis level and run") {
    auto fixture = LoadTextFixture(u8"abc שלום 123!", "und");
    const auto shaped = fixture.ShapeAllItems();
    REQUIRE(shaped);
    for (const auto& glyph : FlattenGlyphs(*shaped)) {
        const auto& item = fixture.AnalysisItemAtByte(glyph.sourceBytes.begin);
        CHECK(glyph.bidiLevel == item.embeddingLevel);
        CHECK(glyph.logicalRunId == item.logicalRunId);
    }
}

// Step 3a/6: 하나의 glyph 기록이 style 크기, content address, HarfBuzz 위치와
// 플래그를 전부 들고 나온다. 위 참조 세 행은 offset이 모두 0이라 두 축을
// 맞바꾸는 회귀가 보이지 않으므로, x/y가 서로 다르고 둘 다 0이 아닌 표식
// 하나를 여기서 따로 고정한다.
TEST_CASE("shaped glyphs carry the style size, revision, offsets and HB flags") {
    const ExplicitReferenceRun run{
        fs::path(MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT) /
            "fonts/NotoSansArabic-Regular.ttf",
        kArabicFont,
        0,
        u8"\u0634\u064E",
        HB_DIRECTION_RTL,
        HB_SCRIPT_ARABIC,
        "ar",
        Fixed26_6::FromRaw(16 * 64),
        {true, true},
        {}};
    const std::vector<ShapedRun> produced =
        ProductionShapedRunsForRun(run, nullptr);
    const std::vector<ShapedGlyph> glyphs = FlattenGlyphs(produced);
    REQUIRE(glyphs.size() == 2U);
    CHECK(GlyphRecordJson(produced) == DirectHarfBuzzSingleFaceRecord(run));
    CHECK(glyphs[0].offsetX.Raw() != 0);
    CHECK(glyphs[0].offsetY.Raw() != 0);
    CHECK(glyphs[0].offsetX != glyphs[0].offsetY);

    const std::vector<std::uint32_t> directFlags = DirectGlyphFlags(run);
    REQUIRE(directFlags.size() == glyphs.size());
    bool sawUnsafeToBreak = false;
    bool sawSafeToBreak = false;
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        CHECK(glyphs[index].harfbuzzGlyphFlags == directFlags[index]);
        if ((glyphs[index].harfbuzzGlyphFlags &
             static_cast<std::uint32_t>(HB_GLYPH_FLAG_UNSAFE_TO_BREAK)) != 0U) {
            sawUnsafeToBreak = true;
        } else {
            sawSafeToBreak = true;
        }
        CHECK(glyphs[index].fontSize == run.fontSize);
        CHECK(glyphs[index].faceIndex == 0U);
        CHECK_FALSE(glyphs[index].missing);
        REQUIRE(glyphs[index].faceResource != nullptr);
        // fontRevision은 content address다. 프로세스 지역 세대가 섞이면
        // 셰이핑 캐시가 실행 사이에 절대 적중하지 않는다.
        CHECK(glyphs[index].fontRevision ==
              glyphs[index].faceResource->artifactSha256 + ":0");
        CHECK(glyphs[index].fontGuid == std::string(kArabicFont));
    }
    // 플래그가 상수로 굳어 있으면 둘 중 하나는 관찰되지 않는다.
    CHECK(sawUnsafeToBreak);
    CHECK(sawSafeToBreak);
}

// 위 케이스가 관찰하는 face index는 0뿐이고, 자격 트리의 여섯 폰트가 전부 단일
// face 파일이라 스위트의 다른 어떤 케이스도 그보다 나은 것을 보지 못한다. 그
// 사이에서는 셰이퍼의 `glyph.faceIndex = state.face->faceIndex`를 상수 0으로
// 바꿔도 전부 통과한다. 여기서만 face 둘짜리 collection을 저작해 그 복사를
// 실제로 0 밖에서 관찰한다.
//
// 합성한 두 face는 같은 표 디렉터리를 가리키므로 glyph도 advance도 완전히
// 같다. 즉 셰이핑 출력의 모양으로는 두 face를 구별할 수 없고, 이 케이스가 물을
// 수 있는 것은 오직 "glyph가 들고 나온 face index가 카탈로그가 정한 그 값인가"
// 하나뿐이다. 그래서 아래는 두 가지를 나눠 단언한다. 저작된 값이 정말 1로
// 묶였다는 전제는 REQUIRE로, 셰이퍼가 그 값을 glyph마다 옮겼다는 계약은
// glyph.faceIndex로. 앞의 전제가 없으면 뒤의 단언은 "0이 아닌 것을 보았다"를
// 증명하지 못하고, 뒤의 단언 없이는 셰이퍼가 무엇을 했는지 알 수 없다.
TEST_CASE("every shaped glyph carries the authored non-zero face index") {
    CollectionFaceCorpus collection;
    const ResolvedFace authored = collection.AuthoredFace();
    REQUIRE(authored.faceIndex == kAuthoredCollectionFaceIndex);
    REQUIRE(authored.resource->faceIndex == kAuthoredCollectionFaceIndex);

    auto fixture = LoadTextFixture(u8"ffi", "en");
    fixture.UseCandidates({authored});
    const auto shaped = fixture.ShapeAllItems();
    REQUIRE(shaped);
    const std::vector<ShapedGlyph> glyphs = FlattenGlyphs(*shaped);
    REQUIRE_FALSE(glyphs.empty());
    for (const ShapedGlyph& glyph : glyphs) {
        // 이 한 줄이 계약이다. 셰이퍼가 face index를 상수로 써 넣거나 후보의
        // 값을 잃어버리면 스위트에서 여기만 실패한다.
        CHECK(glyph.faceIndex == kAuthoredCollectionFaceIndex);
        CHECK_FALSE(glyph.missing);
        CHECK(glyph.fontGuid == std::string(kCollectionFont));
        REQUIRE(glyph.faceResource != nullptr);
        // 위 값이 우연히 맞은 것이 아님을 남긴다: glyph가 붙들고 있는 자원도,
        // content address의 ":<faceIndex>" 절반도 같은 face를 가리킨다.
        CHECK(glyph.faceResource->faceIndex == kAuthoredCollectionFaceIndex);
        CHECK(glyph.fontRevision == glyph.faceResource->artifactSha256 + ":1");
    }
    CHECK_FALSE(HasDiagnostic(fixture.sink, TextDiagnosticCode::MissingGlyph));
}

// Step 1k의 전제. 모든 item이 같은 level과 같은 run을 가지면 그 케이스는 두
// 필드를 상수로 써 넣는 구현으로도 통과한다. 이 픽스처가 실제로 여러 level과
// 여러 run을 만든다는 것을 따로 고정한다.
TEST_CASE("the level fixture really produces several levels and logical runs") {
    auto fixture = LoadTextFixture(u8"abc שלום 123!", "und");
    std::set<std::uint32_t> itemLevels;
    std::set<std::uint32_t> itemRuns;
    for (const AnalysisItem& item : fixture.Analysis().Items()) {
        itemLevels.insert(item.embeddingLevel);
        itemRuns.insert(item.logicalRunId);
    }
    CHECK(itemLevels.size() >= 2U);
    CHECK(itemRuns.size() >= 2U);

    const auto shaped = fixture.ShapeAllItems();
    REQUIRE(shaped);
    std::set<std::uint32_t> glyphLevels;
    std::set<std::uint32_t> glyphRuns;
    for (const ShapedGlyph& glyph : FlattenGlyphs(*shaped)) {
        glyphLevels.insert(glyph.bidiLevel);
        glyphRuns.insert(glyph.logicalRunId);
    }
    CHECK(glyphLevels.size() >= 2U);
    CHECK(glyphRuns.size() >= 2U);

    // ShapedRun::bidiLevel은 Milestone 7의 시각 재배열이 읽을 값인데, glyph
    // 쪽 level만 검사하면 run 쪽을 0으로 굳혀도 아무도 모른다. RTL run 하나가
    // level 0으로 나오면 히브리어 줄이 LTR로 배치된다.
    std::set<std::uint32_t> runLevels;
    for (const ShapedRun& run : *shaped) {
        REQUIRE_FALSE(run.glyphs.empty());
        runLevels.insert(run.bidiLevel);
        for (const ShapedGlyph& glyph : run.glyphs) {
            CHECK(glyph.bidiLevel == run.bidiLevel);
        }
    }
    CHECK(runLevels.size() >= 2U);

    // item이 여럿인 픽스처에서 아무 grapheme도 사라지지 않는다. item마다
    // scalar 시작 색인을 다시 세지 못하는 회귀는 뒤 item 전체를 절차적 tofu로
    // 바꾸는데, level/run 단언은 그 tofu에서도 그대로 성립한다.
    CHECK_FALSE(AnyMissingGlyph(*shaped));
    CHECK(SourceByteCoverIsExact(*shaped, fixture.OriginalUtf8().size()));

    // 후보는 저작된 순서 그대로 쓰인다. '!'는 Latin(44), Devanagari(12),
    // Arabic(66), Korean(77) 넷이 모두 덮으므로, 후보 목록을 fontGuid로
    // 정렬하는 회귀는 12를 먼저 만나 이 단언을 깨뜨린다. 그 변이는 이 프로그램의
    // asset 계층과 resolver 계층에서 이미 두 번 살아남았다.
    const std::uint32_t bang = fixture.GraphemeIndexForByte(
        static_cast<std::uint32_t>(fixture.OriginalUtf8().size() - 1U));
    CHECK(FaceGuidForGrapheme(*shaped, bang) == std::string(kLatinFont));
    const std::uint32_t digit = fixture.GraphemeIndexForByte(
        static_cast<std::uint32_t>(fixture.OriginalUtf8().find('1')));
    CHECK(FaceGuidForGrapheme(*shaped, digit) == std::string(kLatinFont));
}

// ── Step 1m: maximal selected spans ────────────────────────────────────────
// 인접한 두 grapheme이 같은 fallback face를 고르면 span 하나, hb_shape 한 번이다.
// 쪼개면 lam-alef 결합이 사라지고, 어떤 진단도 그 사실을 말해 주지 않는다.
TEST_CASE("adjacent graphemes on one fallback face shape as a single span") {
    auto fixture = LoadTextFixture(u8"لا", "ar");
    fixture.RestrictCandidatesToFonts({kLatinFont, kArabicFont});
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    CHECK(fixture.FinalSelectedSpanCount() == 1);
    CHECK(fixture.FinalShapeCallsForSelectedSpan() == 1);
    CHECK_FALSE(AnyMissingGlyph(*shaped));
    CHECK(FaceGuidForGrapheme(*shaped, 0) == std::string(kArabicFont));
    CHECK(FaceGuidForGrapheme(*shaped, 1) == std::string(kArabicFont));

    // 같은 BOT/EOT/스타일로 만든 직접 한 face 참조와 정확히 같아야 한다.
    ExplicitReferenceRun run{fs::path(MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT) /
                                 "fonts/NotoSansArabic-Regular.ttf",
                             kArabicFont,
                             0,
                             u8"لا",
                             HB_DIRECTION_RTL,
                             HB_SCRIPT_ARABIC,
                             "ar",
                             Fixed26_6::FromRaw(16 * 64),
                             {true, true},
                             {}};
    CHECK(GlyphRecordJson(*shaped) == DirectHarfBuzzSingleFaceRecord(run));

    // 방출된 원본 byte/grapheme 범위는 item을 정확히 한 번 덮는다. 중복도
    // 누락도 없다.
    std::vector<SourceByteRange> byteRanges;
    std::vector<GraphemeRange> graphemeRanges;
    for (const ShapedGlyph& glyph : FlattenGlyphs(*shaped)) {
        byteRanges.push_back(glyph.sourceBytes);
        graphemeRanges.push_back(glyph.graphemes);
    }
    std::sort(byteRanges.begin(), byteRanges.end(),
              [](SourceByteRange a, SourceByteRange b) {
                  return a.begin < b.begin;
              });
    std::sort(graphemeRanges.begin(), graphemeRanges.end(),
              [](GraphemeRange a, GraphemeRange b) { return a.begin < b.begin; });
    REQUIRE_FALSE(byteRanges.empty());
    CHECK(byteRanges.front().begin == 0U);
    CHECK(byteRanges.back().end == fixture.OriginalUtf8().size());
    for (std::size_t index = 0; index + 1 < byteRanges.size(); ++index) {
        CHECK(byteRanges[index].end == byteRanges[index + 1].begin);
    }
    CHECK(graphemeRanges.front().begin == 0U);
    CHECK(graphemeRanges.back().end == 2U);
    for (std::size_t index = 0; index + 1 < graphemeRanges.size(); ++index) {
        CHECK(graphemeRanges[index].end == graphemeRanges[index + 1].begin);
    }
}

// ── Step 1n: adjusted GDEF carets ──────────────────────────────────────────
// 저장된 caret은 GDEF에서 온 값 그대로여야 한다. 합성하거나 반올림하면 lam-alef
// 같은 결합 글리프 안의 caret이 조용히 잘못된 자리에 놓인다.
TEST_CASE("ligature glyphs keep the adjusted GDEF carets HarfBuzz returns") {
    const std::array<ExplicitReferenceRun, 3> runs = ExplicitPinnedRuns();

    ShapedGlyph ligature;
    ProductionShapedRunsForRun(runs[0], &ligature);
    const std::vector<Fixed26_6> direct =
        DirectLigatureCarets(runs[0], ligature.glyphId);
    // 양성 증인. 이 face/glyph가 caret을 하나도 갖지 않으면 아래 비교는 빈
    // 벡터 두 개를 견주게 되고, caret 코드를 통째로 지워도 통과한다.
    REQUIRE(direct.size() == 2U);
    REQUIRE(ligature.adjustedGdefCaretOffsets.size() == direct.size());
    for (std::size_t index = 0; index < direct.size(); ++index) {
        CHECK(ligature.adjustedGdefCaretOffsets[index] == direct[index]);
    }

    // GDEF caret이 없는 face/glyph는 합성된 위치가 아니라 빈 벡터를 담는다.
    ShapedGlyph plain;
    ProductionShapedRunsForRun(runs[2], &plain);
    CHECK(DirectLigatureCarets(runs[2], plain.glyphId).empty());
    CHECK(plain.adjustedGdefCaretOffsets.empty());
}

// Step 10 + 진단 상한. 없는 grapheme은 하나씩 자기 절차적 기록이 되고, 진단은
// kMaxShapingDiagnosticsPerItem에서 멈춘다. 상한이 없으면 지원되지 않는
// script로 적힌 라벨 하나가 프레임마다 grapheme 수만큼 로그를 채우고, 상한이
// 기록까지 자르면 패키지 검증이 없는 글자를 여덟 개까지만 보게 된다.
TEST_CASE("many missing graphemes stay one record each and eight diagnostics") {
    auto fixture = LoadTextFixture(u8"👩👩👩👩👩👩👩👩👩👩", "und");
    fixture.UseResolvedFamilyWithNoCandidates();
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    REQUIRE(fixture.Analysis().Items().size() == 1U);
    const std::vector<ShapedGlyph> glyphs = FlattenGlyphs(*shaped);
    // 인접한 없음 grapheme끼리 합쳐지면 열 개가 한 개가 되고, 그 줄의 폭이
    // 열 배로 줄어든다. 어떤 진단도 그 사실을 말해 주지 않는다.
    REQUIRE(glyphs.size() == 10U);
    CHECK(fixture.FinalSelectedSpanCount() == 10);
    for (const ShapedGlyph& glyph : glyphs) {
        CHECK(glyph.missing);
        CHECK(glyph.advanceX == fixture.ShapeStyle().fontSize);
        CHECK_FALSE(glyph.faceResource);
    }
    CHECK(SourceByteCoverIsExact(*shaped, fixture.OriginalUtf8().size()));
    CHECK(CountDiagnostics(fixture.sink, TextDiagnosticCode::MissingGlyph) ==
          molga::text::kMaxShapingDiagnosticsPerItem);
}

// Step 4/9e: 실패는 조용하지 않다. 이 스위트에서 nullopt를 돌려주는 경로는
// not-ready 프로세스의 DependencyInvalid뿐이라, 준비된 런타임에서의 fail-closed
// 정책 — LayoutInvalid 하나와 nullopt — 에는 증인이 없었다. AnalysisItem은 공개
// 집합체이므로 자기가 속하지 않은 분석과 함께 넘길 수 있다.
TEST_CASE("an item whose ranges disagree fails closed with one LayoutInvalid") {
    REQUIRE_MESSAGE(Corpus().Error().empty(), Corpus().Error());
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = UnicodeTextBuffer::Build("ab", sink);
    REQUIRE(buffer);
    auto analysis = text::UnicodeTextAnalyzer::Analyze(
        *buffer, {"und", text::BaseDirection::Auto}, sink);
    REQUIRE(analysis);
    auto family = Corpus().Resolver().BuildCandidates(
        kPrimaryFamily, {400, 100, molga::FontSlant::Upright}, sink);
    REQUIRE(family);

    // grapheme 범위는 한 글자, byte 범위는 두 글자를 말한다. 두 좌표계가 같은
    // 구간을 말하지 않으면 셰이핑은 추정하지 않고 실패한다.
    AnalysisItem broken;
    broken.sourceBytes = {0, 2};
    broken.utf16Units = {0, 2};
    broken.graphemes = {0, 1};
    broken.paragraphStart = true;
    broken.paragraphEnd = true;

    molga::text::ShapeStyle style;
    molga::text::TextShapingService service;
    const auto shaped = service.ShapeAnalysisItem(
        *buffer, *analysis, broken, *family, style, {true, true}, sink);
    CHECK_FALSE(shaped);
    CHECK(CountDiagnostics(sink, TextDiagnosticCode::LayoutInvalid) == 1U);
    CHECK_FALSE(HasDiagnostic(sink, TextDiagnosticCode::MissingGlyph));
}

// Step 7: coverage 판정은 range의 끝점을 포함한다. U+0020은 이 face의 임포트된
// coverage에서 어떤 range의 시작점이라, 포함 비교를 <=에서 <로 바꾸는 회귀는
// 정확히 공백 같은 끝점 문자만 조용히 tofu로 만든다.
TEST_CASE("a codepoint on a coverage range endpoint still shapes to a real glyph") {
    auto fixture = LoadTextFixture(u8"a b", "und");
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    REQUIRE_FALSE(fixture.Candidates().empty());
    CHECK(Label(fixture.Candidates().front().fontGuid.c_str()) ==
          std::string(kLatinFont));
    CHECK(CoverageRangeEndpoint(fixture.Candidates().front(), U' '));
    CHECK_FALSE(AnyMissingGlyph(*shaped));
    CHECK(SourceByteCoverIsExact(*shaped, fixture.OriginalUtf8().size()));
}

// Step 3a/5a: 요청된 크기가 실제로 hb_font_set_scale에 닿는가. 위 참조 세 행과
// 아래 두 즉석 행이 모두 16px이라, 크기를 상수로 굳혀도 "glyph.fontSize ==
// run.fontSize"류의 단언은 전부 통과한다 — 그 단언들은 필드가 복사되었다는
// 사실만 증명한다.
TEST_CASE("the requested size is the size HarfBuzz shapes at") {
    ExplicitReferenceRun larger = ExplicitPinnedRuns()[0];
    larger.fontSize = Fixed26_6::FromRaw(24 * 64);
    const auto production = ProductionSingleResolvedFaceRecord(larger);
    CHECK(production == DirectHarfBuzzSingleFaceRecord(larger));
    // 16px 행과 반드시 달라야 한다. 같으면 위 비교는 두 상수를 견주는 셈이다.
    CHECK_FALSE(production ==
                DirectHarfBuzzSingleFaceRecord(ExplicitPinnedRuns()[0]));
}

// Step 5c: feature는 저작된 순서와 구간 그대로 HarfBuzz에 닿는다.
//
// 커밋된 참조 표의 유일한 feature는 Latin의 liga=1인데, liga는 Latin에서 기본
// 켜짐이라 그 행은 feature를 아예 넘기지 않아도 같은 결과를 낸다 — 이 프로그램에서
// 두 번 살아남은 "픽스처가 연산을 무의미하게 만든다" 형태 그대로다. 그래서
// 결과를 실제로 바꾸는 feature와, 순서가 결과를 가르는 짝을 여기서 고정한다.
TEST_CASE("ordered source-range features reach HarfBuzz in the authored order") {
    const std::array<ExplicitReferenceRun, 3> pinned = ExplicitPinnedRuns();
    ExplicitReferenceRun disabled = pinned[0];
    disabled.features = {{HB_TAG('l', 'i', 'g', 'a'), 0, {0, 3}}};
    const auto without = ProductionSingleResolvedFaceRecord(disabled);
    CHECK(without == DirectHarfBuzzSingleFaceRecord(disabled));
    // liga=0이면 ffi 결합이 일어나지 않아 glyph 셋, 켠 쪽은 결합 하나다.
    CHECK(without.size() == 3U);
    CHECK(ProductionSingleResolvedFaceRecord(pinned[0]).size() == 1U);

    // 같은 구간에 겹쳐 적힌 두 feature는 정규화되지 않는다. 뒤에 적힌 것이
    // 이기므로 두 순서의 결과가 서로 달라야 한다.
    ExplicitReferenceRun offLast = pinned[0];
    offLast.features = {{HB_TAG('l', 'i', 'g', 'a'), 1, {0, 3}},
                        {HB_TAG('l', 'i', 'g', 'a'), 0, {0, 3}}};
    ExplicitReferenceRun onLast = pinned[0];
    onLast.features = {{HB_TAG('l', 'i', 'g', 'a'), 0, {0, 3}},
                       {HB_TAG('l', 'i', 'g', 'a'), 1, {0, 3}}};
    const auto offLastRecord = ProductionSingleResolvedFaceRecord(offLast);
    const auto onLastRecord = ProductionSingleResolvedFaceRecord(onLast);
    CHECK_FALSE(offLastRecord == onLastRecord);
    CHECK(offLastRecord == DirectHarfBuzzSingleFaceRecord(offLast));
    CHECK(onLastRecord == DirectHarfBuzzSingleFaceRecord(onLast));
    CHECK(offLastRecord.size() == 3U);
    CHECK(onLastRecord.size() == 1U);
}

// Step 5b/9b/9a: 최종 span은 item 전체의 sanitized 배열을 문맥으로 받고,
// BOT/EOT는 그 span의 진짜 텍스트 끝에서만 온다.
//
// U+08B5는 Arabic script의 양쪽 결합 글자이지만 이 corpus의 어느 face에도 없다.
// 그래서 하나의 Arabic item 안에서 가운데 grapheme만 없음이 되고, 마지막 ب는
// 자기 span 밖에 있는 앞 문맥 때문에 어말형이 된다. 문맥을 잘라 넘기거나 BOT를
// span마다 켜면 홀로형이 나오는데, 그 차이는 글자 모양으로만 드러나고 어떤
// 진단도 남기지 않는다.
TEST_CASE("a mid-item span keeps its surrounding context and its own BOT/EOT") {
    auto fixture = LoadTextFixture(u8"بࢵب", "ar");
    fixture.RestrictCandidatesToFont(kArabicFont);
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    REQUIRE(fixture.Analysis().Items().size() == 1U);
    REQUIRE(shaped->size() == 3U);
    CHECK(fixture.FinalSelectedSpanCount() == 3);
    REQUIRE((*shaped)[1].glyphs.size() == 1U);
    CHECK((*shaped)[1].glyphs.front().missing);
    CHECK_FALSE((*shaped)[0].glyphs.front().missing);
    CHECK_FALSE((*shaped)[2].glyphs.front().missing);

    ExplicitReferenceRun run{fs::path(MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT) /
                                 "fonts/NotoSansArabic-Regular.ttf",
                             kArabicFont,
                             0,
                             std::string(),
                             HB_DIRECTION_RTL,
                             HB_SCRIPT_ARABIC,
                             "ar",
                             Fixed26_6::FromRaw(16 * 64),
                             {false, true},
                             {}};
    const std::vector<hb_codepoint_t> whole{0x0628U, 0x08B5U, 0x0628U};
    const std::vector<hb_codepoint_t> alone{0x0628U};

    // 마지막 span은 논리적으로 마지막이므로 EOT만 켜진다. 그 상태로 완전한
    // 배열을 문맥으로 받은 결과와 정확히 같아야 한다.
    const auto withContext = DirectHarfBuzzSpanShape(run, whole, 2, 1);
    CHECK(ShapeOnlyJson((*shaped)[2].glyphs) == withContext);
    // 문맥을 잘라 span만 넘긴 결과와는 달라야 한다. 이 한 줄이 Step 9b의 유일한
    // 행동 증인이다: 관찰(FinalSelectedContextCoveredCompleteItem)은 seam이지만
    // 이쪽은 glyph 자체가 달라진다.
    CHECK_FALSE(withContext == DirectHarfBuzzSpanShape(run, alone, 0, 1));

    // 첫 span은 BOT만 켜진다. 그 span도 완전한 배열을 문맥으로 받는다.
    ExplicitReferenceRun firstSpan = run;
    firstSpan.boundaries = {true, false};
    CHECK(ShapeOnlyJson((*shaped)[0].glyphs) ==
          DirectHarfBuzzSpanShape(firstSpan, whole, 0, 1));
}

// Step 5a/9a: BOT가 실제로 HarfBuzz까지 간다.
//
// 고정된 HarfBuzz 14.3.1에서 HB_BUFFER_FLAG_BOT가 셰이핑을 바꾸는 자리는 정확히
// 하나다(hb-ot-shape.cc의 hb_insert_dotted_circle): 앞 문맥이 없고 첫 glyph가
// 결합 표식일 때 dotted circle을 끼워 넣는다. 그래서 결합 표식으로 시작하는
// 텍스트가 이 flag의 유일한 관찰 창이다. HB_BUFFER_FLAG_EOT는 이 버전에서
// 셰이핑에 아무 영향이 없고(hb-buffer-verify.cc만 읽는다), 첫 span이 아닌
// span에서는 문맥이 있으므로 BOT도 영향이 없다 — 그래도 두 값을 span의 진짜
// 끝에서 끌어오는 것은 계약이고, 이 케이스가 그 계약의 관찰 가능한 절반이다.
TEST_CASE("beginning-of-text reaches HarfBuzz for the span that really starts the text") {
    auto fixture = LoadTextFixture(u8"َب", "ar");
    fixture.RestrictCandidatesToFont(kArabicFont);
    const auto shaped = fixture.ShapeFirstItem();
    REQUIRE(shaped);
    REQUIRE(fixture.Analysis().Items().size() == 1U);
    REQUIRE(shaped->size() == 1U);
    CHECK(fixture.FinalSelectedSpanCount() == 1);
    CHECK_FALSE(AnyMissingGlyph(*shaped));

    ExplicitReferenceRun run{fs::path(MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT) /
                                 "fonts/NotoSansArabic-Regular.ttf",
                             kArabicFont,
                             0,
                             std::string(),
                             HB_DIRECTION_RTL,
                             HB_SCRIPT_ARABIC,
                             "ar",
                             Fixed26_6::FromRaw(16 * 64),
                             {true, true},
                             {}};
    const std::vector<hb_codepoint_t> whole{0x064EU, 0x0628U};
    const auto withBot = DirectHarfBuzzSpanShape(run, whole, 0, 2);
    CHECK(ShapeOnlyJson((*shaped)[0].glyphs) == withBot);

    ExplicitReferenceRun withoutBot = run;
    withoutBot.boundaries = {false, true};
    CHECK_FALSE(withBot == DirectHarfBuzzSpanShape(withoutBot, whole, 0, 2));
}

// UnicodeAnalysis.h가 경고하는 아홉 문자(X9가 제거하는 LRE/RLE/LRO/RLO/PDF와 네
// isolate)에 대한 고정. 이 셰이퍼는 방향을 item의 level에서 끌어내는데, 그 아홉의
// level은 UBA의 보장이 아니라 ICU의 보존 규약이다. 그래도 결과가 달라질 수 없는
// 이유 — 각자 혼자 item이고 전부 default ignorable이라 배치할 순서가 없다 — 를
// 주석이 아니라 픽스처로 남긴다.
TEST_CASE("a lone isolate item draws nothing and never reports a missing glyph") {
    auto fixture = LoadTextFixture(u8"a⁦b⁩c", "und");
    const auto shaped = fixture.ShapeAllItems();
    REQUIRE(shaped);
    CHECK_FALSE(AnyMissingGlyph(*shaped));
    CHECK_FALSE(HasDiagnostic(fixture.sink, TextDiagnosticCode::MissingGlyph));

    // 두 isolate는 각자 혼자 item이다.
    const AnalysisItem& isolate = fixture.AnalysisItemAtByte(1);
    CHECK(isolate.sourceBytes.begin == 1U);
    CHECK(isolate.sourceBytes.end == 4U);
    const AnalysisItem& pop = fixture.AnalysisItemAtByte(5);
    CHECK(pop.sourceBytes.begin == 5U);
    CHECK(pop.sourceBytes.end == 8U);

    std::size_t visible = 0;
    for (const ShapedGlyph& glyph : FlattenGlyphs(*shaped)) {
        const bool control = glyph.sourceBytes.begin == 1U ||
                             glyph.sourceBytes.begin == 5U;
        if (control) {
            CHECK(glyph.advanceX.Raw() == 0);
            CHECK(glyph.advanceY.Raw() == 0);
            continue;
        }
        CHECK(glyph.advanceX.Raw() > 0);
        ++visible;
    }
    CHECK(visible == 3U);
}

// ── Step 11: stb_truetype never shapes, kerns, or measures ─────────────────
// Task 8.2 Step 8a.1/8b가 FontFace::Advance/Kerning과 그 호출 계수기를 함께
// 지웠다. 이제 이 주장을 지키는 것은 실행 시 계수기가 아니라 컴파일러다:
// 부를 함수가 없으므로 셰이핑이 그쪽으로 돌아갈 수 없고, 되살리려면 지운
// API를 다시 넣는 diff가 필요하다. 남은 것은 그 밑에 있던 행동 주장이다 —
// 성공 경로와 없는 glyph 경로 둘 다 셰이퍼를 실제로 지난다.
TEST_CASE("shaping produces glyphs on both the present and missing paths") {
    auto fixture = LoadTextFixture(u8"ffi لا क्षि", "und");
    const auto shaped = fixture.ShapeAllItems();
    REQUIRE(shaped);
    REQUIRE_FALSE(FlattenGlyphs(*shaped).empty());

    // 없는 glyph 경로도 함께 본다. 앞으로 절차적 metric이 필요해질 때 가장
    // 먼저 손이 가는 곳이 FontFace이므로, 이 주장이 덮어야 하는 곳은 성공
    // 경로만이 아니다.
    auto missing = LoadTextFixture(u8"👩", "und");
    const auto missingShaped = missing.ShapeAllItems();
    REQUIRE(missingShaped);
    REQUIRE(AnyMissingGlyph(*missingShaped));
}
