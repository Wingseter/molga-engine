#include "Text/TextShapingService.h"

#include "Text/TextRuntimeDependencies.h"

#include <hb-icu.h>
#include <hb-ot.h>
#include <hb.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace molga::text {
namespace {

constexpr const char* kSubsystem = "text-shaping";

// 시도된 HarfBuzz 객체 생성 횟수. 호출 직전에 올린다(UnicodeAnalysis의 ICU
// 계수기와 같은 이유다): 사후에 세면 "만들려다 실패했다"가 0으로 보인다.
std::uint64_t g_harfBuzzObjectCreations = 0;

// 호출 하나의 관찰. ShapeAnalysisItem이 시작할 때마다 비운다 — 관찰은 기록이
// 아니라 seam이고, 프로세스 수명 동안 쌓이면 셰이핑 호출 수만큼 커진다.
detail::ShapingObservations g_observations;

void CountHarfBuzzObject() noexcept { ++g_harfBuzzObjectCreations; }

template <class T>
void RecordBounded(std::vector<T>& out, T value) {
    if (out.size() >= detail::kMaxShapingObservations) return;
    out.push_back(std::move(value));
}

// 상한을 넘긴 뒤에는 조용히 세기만 한다. 호출자가 진단 개수로 없는 glyph 수를
// 세지 못하도록, 권한 있는 기록은 언제나 ShapedGlyph::missing 쪽이다.
struct DiagnosticBudget {
    std::size_t remaining = kMaxShapingDiagnosticsPerItem;
};

void Emit(TextDiagnosticSink& sink, TextDiagnosticCode code,
          std::string message, std::string remediation, SourceByteRange range) {
    TextDiagnostic diagnostic;
    diagnostic.code            = code;
    diagnostic.severity        = TextSeverity::Error;
    diagnostic.subsystem       = kSubsystem;
    diagnostic.message         = std::move(message);
    diagnostic.remediation     = std::move(remediation);
    diagnostic.sourceByteRange = range;
    sink.Report(std::move(diagnostic));
}

// grapheme 수에 비례해 날 수 있는 진단만 예산을 쓴다. 지금은 MissingGlyph
// 하나뿐이다.
void ReportBounded(TextDiagnosticSink& sink, DiagnosticBudget& budget,
                   TextDiagnosticCode code, std::string message,
                   std::string remediation, SourceByteRange range) {
    if (budget.remaining == 0) return;
    --budget.remaining;
    Emit(sink, code, std::move(message), std::move(remediation), range);
}

// 종결 진단은 예산 밖이다. 이 진단들은 곧바로 nullopt로 이어지므로 한 번의
// 호출에 많아야 하나이고, 예산을 쓰게 두면 앞선 여덟 개의 MissingGlyph가
// "실패했는데 왜 실패했는지 말하는 진단이 하나도 없다"를 만들어 낸다. 실패에
// 이유가 붙어 있다는 성질이 상한의 목적보다 앞선다.
void ReportTerminal(TextDiagnosticSink& sink, TextDiagnosticCode code,
                    std::string message, std::string remediation,
                    SourceByteRange range) {
    Emit(sink, code, std::move(message), std::move(remediation), range);
}

void ReportLayoutInvalid(TextDiagnosticSink& sink, std::string message,
                         SourceByteRange range) {
    ReportTerminal(sink, TextDiagnosticCode::LayoutInvalid, std::move(message),
                   "This is an internal shaping invariant, not authored "
                   "content. Report the text and the font family that produced "
                   "it.",
                   range);
}

// ── Step 5: the project cluster policy, mapped in exactly one place ─────────
// hb_buffer_cluster_level_t와 HB_BUFFER_CLUSTER_LEVEL_*는 이 파일 밖으로
// 나가지 않는다. 공개 캐시 키가 HarfBuzz 상수를 담으면 그 상수의 의미가 바뀌는
// 날 캐시가 조용히 다른 셰이핑을 같은 것으로 취급한다.
std::optional<hb_buffer_cluster_level_t> ToHarfBuzzClusterLevel(
    TextClusterPolicy policy) {
    switch (policy) {
        case TextClusterPolicy::MonotoneCharacters:
            return HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS;
    }
    return std::nullopt;
}

// ── RAII handles ───────────────────────────────────────────────────────────

class ScopedHarfBuzzBuffer {
public:
    ScopedHarfBuzzBuffer() {
        CountHarfBuzzObject();
        buffer_ = hb_buffer_create();
    }
    ~ScopedHarfBuzzBuffer() {
        if (buffer_ != nullptr) hb_buffer_destroy(buffer_);
    }
    ScopedHarfBuzzBuffer(const ScopedHarfBuzzBuffer&)            = delete;
    ScopedHarfBuzzBuffer& operator=(const ScopedHarfBuzzBuffer&) = delete;
    hb_buffer_t* Get() const noexcept { return buffer_; }

private:
    hb_buffer_t* buffer_ = nullptr;
};

// 하나의 불변 face 자원 위에 열린 HarfBuzz 핸들 묶음.
//
// 자원 지분을 핸들 옆에 함께 붙들고 있는 것이 이 클래스의 존재 이유다.
// hb_blob은 바이트를 복사하지 않고 가리키기만 하므로, 지분을 놓으면 hot
// reload가 그 아래에서 바이트를 해제할 수 있다. 경로도 GUID도 다시 열지
// 않는다: 여는 것은 언제나 이미 검증된 이 바이트다.
class ResourceFont {
public:
    ResourceFont() = default;
    ~ResourceFont() { Reset(); }
    ResourceFont(const ResourceFont&)            = delete;
    ResourceFont& operator=(const ResourceFont&) = delete;

    bool Open(const FontFaceResourcePtr& resource, Fixed26_6 fontSize) {
        Reset();
        if (resource == nullptr || resource->bytes == nullptr) return false;
        const std::vector<std::uint8_t>& bytes = *resource->bytes;
        if (bytes.empty() ||
            bytes.size() > std::numeric_limits<unsigned int>::max()) {
            return false;
        }
        resource_ = resource;
        CountHarfBuzzObject();
        blob_ = hb_blob_create(reinterpret_cast<const char*>(bytes.data()),
                               static_cast<unsigned int>(bytes.size()),
                               HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        if (blob_ == nullptr) return false;
        CountHarfBuzzObject();
        face_ = hb_face_create(blob_, resource->faceIndex);
        if (face_ == nullptr) return false;
        CountHarfBuzzObject();
        font_ = hb_font_create(face_);
        if (font_ == nullptr) return false;
        hb_ot_font_set_funcs(font_);
        hb_font_set_scale(font_, fontSize.Raw(), fontSize.Raw());
        return true;
    }

    hb_font_t* Font() const noexcept { return font_; }

private:
    void Reset() noexcept {
        if (font_ != nullptr) hb_font_destroy(font_);
        if (face_ != nullptr) hb_face_destroy(face_);
        if (blob_ != nullptr) hb_blob_destroy(blob_);
        font_ = nullptr;
        face_ = nullptr;
        blob_ = nullptr;
        resource_.reset();
    }

    FontFaceResourcePtr resource_;
    hb_blob_t*          blob_ = nullptr;
    hb_face_t*          face_ = nullptr;
    hb_font_t*          font_ = nullptr;
};

// ── Step 7a: cmap format 14 (Unicode Variation Sequences) ──────────────────
// 변이 선택자 판정은 호스트 폰트도 휴리스틱도 보지 않는다. 선택된 face의 불변
// 바이트 안에 있는 format 14 기록만이 "이 face가 그 쌍을 직접 이름 붙였는가"를
// 답한다. 여기서 읽는 것은 default UVS 범위와 non-default UVS 매핑 둘 다이고,
// 어느 쪽이든 그 쌍을 이름 붙였다는 사실만 쓴다(글리프 ID는 HarfBuzz가 셰이핑
// 중에 스스로 찾는다).

std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes,
                      std::uint64_t at) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at)]) << 8) |
        static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at + 1U)]));
}

std::uint32_t ReadU24(const std::vector<std::uint8_t>& bytes,
                      std::uint64_t at) {
    return (static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at)])
            << 16) |
           (static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at + 1U)])
            << 8) |
           static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at + 2U)]);
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes,
                      std::uint64_t at) {
    return (static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at)])
            << 24) |
           (static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at + 1U)])
            << 16) |
           (static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at + 2U)])
            << 8) |
           static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(at + 3U)]);
}

bool Fits(std::uint64_t offset, std::uint64_t length, std::uint64_t size) {
    return offset <= size && length <= size - offset;
}

// collection('ttcf')이면 faceIndex번째 face의 표 디렉터리를, 단일 face 파일이면
// 언제나 0을 돌려준다. 인덱스를 짐작하지 않는다: 범위를 벗어나면 실패다.
std::optional<std::uint64_t> TableDirectoryOffset(
    const std::vector<std::uint8_t>& bytes, std::uint32_t faceIndex) {
    if (!Fits(0, 12U, bytes.size())) return std::nullopt;
    if (ReadU32(bytes, 0) == 0x74746366U /* 'ttcf' */) {
        const std::uint32_t faces = ReadU32(bytes, 8U);
        if (faceIndex >= faces) return std::nullopt;
        const std::uint64_t at = 12U + static_cast<std::uint64_t>(faceIndex) * 4U;
        if (!Fits(at, 4U, bytes.size())) return std::nullopt;
        return static_cast<std::uint64_t>(ReadU32(bytes, at));
    }
    if (faceIndex != 0U) return std::nullopt;
    return std::uint64_t{0};
}

struct TableSpan {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

std::optional<TableSpan> FindTable(const std::vector<std::uint8_t>& bytes,
                                   std::uint64_t directory, std::uint32_t tag) {
    if (!Fits(directory, 12U, bytes.size())) return std::nullopt;
    const std::uint16_t tables = ReadU16(bytes, directory + 4U);
    if (!Fits(directory + 12U, static_cast<std::uint64_t>(tables) * 16U,
              bytes.size())) {
        return std::nullopt;
    }
    for (std::uint16_t index = 0; index < tables; ++index) {
        const std::uint64_t record =
            directory + 12U + static_cast<std::uint64_t>(index) * 16U;
        if (ReadU32(bytes, record) != tag) continue;
        TableSpan span;
        span.offset = ReadU32(bytes, record + 8U);
        span.length = ReadU32(bytes, record + 12U);
        if (!Fits(span.offset, span.length, bytes.size())) return std::nullopt;
        return span;
    }
    return std::nullopt;
}

struct UvsSelectorRecord {
    char32_t                                    selector = 0;
    std::vector<std::pair<char32_t, char32_t>>  defaultRanges;
    std::vector<char32_t>                       nonDefaultValues;
};

class VariationSelectorTable {
public:
    void Parse(const std::vector<std::uint8_t>& bytes, std::uint32_t faceIndex);
    bool Names(char32_t base, char32_t selector) const;

private:
    std::vector<UvsSelectorRecord> selectors_;
};

void VariationSelectorTable::Parse(const std::vector<std::uint8_t>& bytes,
                                   std::uint32_t faceIndex) {
    selectors_.clear();
    const auto directory = TableDirectoryOffset(bytes, faceIndex);
    if (!directory) return;
    const auto cmap = FindTable(bytes, *directory, 0x636D6170U /* 'cmap' */);
    if (!cmap || cmap->length < 4U) return;
    const std::uint16_t subtables = ReadU16(bytes, cmap->offset + 2U);
    if (!Fits(cmap->offset + 4U, static_cast<std::uint64_t>(subtables) * 8U,
              bytes.size())) {
        return;
    }
    for (std::uint16_t index = 0; index < subtables; ++index) {
        const std::uint64_t record =
            cmap->offset + 4U + static_cast<std::uint64_t>(index) * 8U;
        // UVS는 platform 0 / encoding 5로만 저작된다.
        if (ReadU16(bytes, record) != 0U || ReadU16(bytes, record + 2U) != 5U) {
            continue;
        }
        const std::uint64_t offset = ReadU32(bytes, record + 4U);
        if (offset >= cmap->length) continue;
        const std::uint64_t subtable = cmap->offset + offset;
        if (!Fits(subtable, 10U, bytes.size())) continue;
        if (ReadU16(bytes, subtable) != 14U) continue;
        const std::uint32_t records = ReadU32(bytes, subtable + 6U);
        if (!Fits(subtable + 10U, static_cast<std::uint64_t>(records) * 11U,
                  bytes.size())) {
            continue;
        }
        for (std::uint32_t entry = 0; entry < records; ++entry) {
            const std::uint64_t at =
                subtable + 10U + static_cast<std::uint64_t>(entry) * 11U;
            UvsSelectorRecord parsed;
            parsed.selector = static_cast<char32_t>(ReadU24(bytes, at));
            const std::uint64_t defaultOffset = ReadU32(bytes, at + 3U);
            const std::uint64_t nonDefaultOffset = ReadU32(bytes, at + 7U);
            if (defaultOffset != 0U &&
                Fits(subtable + defaultOffset, 4U, bytes.size())) {
                const std::uint64_t base = subtable + defaultOffset;
                const std::uint32_t ranges = ReadU32(bytes, base);
                if (Fits(base + 4U, static_cast<std::uint64_t>(ranges) * 4U,
                         bytes.size())) {
                    for (std::uint32_t range = 0; range < ranges; ++range) {
                        const std::uint64_t item =
                            base + 4U + static_cast<std::uint64_t>(range) * 4U;
                        const std::uint32_t start = ReadU24(bytes, item);
                        const std::uint32_t extra =
                            bytes[static_cast<std::size_t>(item + 3U)];
                        parsed.defaultRanges.emplace_back(
                            static_cast<char32_t>(start),
                            static_cast<char32_t>(start + extra));
                    }
                }
            }
            if (nonDefaultOffset != 0U &&
                Fits(subtable + nonDefaultOffset, 4U, bytes.size())) {
                const std::uint64_t base = subtable + nonDefaultOffset;
                const std::uint32_t mappings = ReadU32(bytes, base);
                if (Fits(base + 4U, static_cast<std::uint64_t>(mappings) * 5U,
                         bytes.size())) {
                    for (std::uint32_t mapping = 0; mapping < mappings;
                         ++mapping) {
                        const std::uint64_t item =
                            base + 4U + static_cast<std::uint64_t>(mapping) * 5U;
                        parsed.nonDefaultValues.push_back(
                            static_cast<char32_t>(ReadU24(bytes, item)));
                    }
                }
            }
            selectors_.push_back(std::move(parsed));
        }
    }
}

bool VariationSelectorTable::Names(char32_t base, char32_t selector) const {
    for (const UvsSelectorRecord& record : selectors_) {
        if (record.selector != selector) continue;
        for (const std::pair<char32_t, char32_t>& range : record.defaultRanges) {
            if (base >= range.first && base <= range.second) return true;
        }
        for (const char32_t value : record.nonDefaultValues) {
            if (value == base) return true;
        }
    }
    return false;
}

// ── Coverage and scalar classification ─────────────────────────────────────

bool CoverageContains(const std::vector<std::pair<char32_t, char32_t>>& ranges,
                      char32_t value) {
    for (const std::pair<char32_t, char32_t>& range : ranges) {
        if (value >= range.first && value <= range.second) return true;
    }
    return false;
}

// join control, ZWJ/ZWNJ, 그 밖의 default ignorable은 독립된 cmap glyph를
// 요구하지 않는다. 요구하면 ZWJ 하나가 들어간 이름 하나 때문에 문단 전체가
// fallback으로 넘어간다.
bool IsDefaultIgnorable(char32_t value) {
    return u_hasBinaryProperty(static_cast<UChar32>(value),
                               UCHAR_DEFAULT_IGNORABLE_CODE_POINT) != 0;
}

bool IsVariationSelector(char32_t value) {
    return u_hasBinaryProperty(static_cast<UChar32>(value),
                               UCHAR_VARIATION_SELECTOR) != 0;
}

// ── Per-candidate lazy state ───────────────────────────────────────────────
// probe 결과는 대상 grapheme이 아니라 (후보, item)에만 의존하므로 후보마다 한
// 번만 셰이핑한다. grapheme마다 다시 셰이핑하면 같은 출력을 grapheme 수만큼
// 다시 만들 뿐이고, 선택 결과는 한 글자도 달라지지 않는다.
struct CandidateState {
    const ResolvedFace*          face = nullptr;
    ResourceFont                 font;
    bool                         fontReady   = false;
    bool                         fontFailed  = false;
    bool                         uvsParsed   = false;
    VariationSelectorTable       uvs;
    bool                         probed      = false;
    bool                         probeFailed = false;
    std::vector<SourceByteRange> probeNotdefRanges;
};

struct GraphemeSpanKey {
    const FontFaceResource* resource = nullptr;
    std::uint32_t           faceIndex = 0;
    std::string             fontRevision;
    std::uint8_t            bidiLevel = 0;
    std::int32_t            scriptCode = 0;
    bool                    rightToLeft = false;

    bool operator==(const GraphemeSpanKey& other) const {
        return resource == other.resource && faceIndex == other.faceIndex &&
               fontRevision == other.fontRevision &&
               bidiLevel == other.bidiLevel && scriptCode == other.scriptCode &&
               rightToLeft == other.rightToLeft;
    }
};

struct GraphemeSelection {
    std::uint32_t   graphemeIndex = 0;
    SourceByteRange bytes;
    std::uint32_t   scalarBegin = 0;  // item 내부 색인
    std::uint32_t   scalarEnd   = 0;
    int             candidate   = -1;  // -1이면 어느 face도 맞지 않았다
    GraphemeSpanKey key;
};

struct SelectedSpan {
    std::uint32_t   graphemeBegin = 0;
    std::uint32_t   graphemeEnd   = 0;
    std::uint32_t   scalarBegin   = 0;
    std::uint32_t   scalarEnd     = 0;
    SourceByteRange bytes;
    int             candidate = -1;
};

// hb_position_t가 26.6 raw와 같은 폭이 아니면 아래 FromRaw 변환이 조용히
// 잘린다. 검사된 단위를 쓰는 이유가 사라지므로 컴파일에서 막는다.
static_assert(sizeof(hb_position_t) == sizeof(std::int32_t),
              "hb_position_t must be the same width as a 26.6 raw value");

}  // namespace

namespace detail {

std::uint64_t HarfBuzzObjectCreationCount() noexcept {
    return g_harfBuzzObjectCreations;
}

void ResetHarfBuzzObjectCreationCount() noexcept {
    g_harfBuzzObjectCreations = 0;
}

const ShapingObservations& Observations() noexcept { return g_observations; }

}  // namespace detail

namespace {

// 하나의 셰이핑 호출이 쓰는 모든 상태. 자유 함수 대여섯 개에 같은 인자
// 열두 개를 계속 넘기는 대신 한 곳에 모은다.
class ItemShaper {
public:
    ItemShaper(const UnicodeTextBuffer& buffer, const UnicodeAnalysis& analysis,
               const AnalysisItem& item, const ResolvedFamily& family,
               const ShapeStyle& style, ShapeBoundaryFlags boundaries,
               TextDiagnosticSink& sink)
        : buffer_(buffer),
          analysis_(analysis),
          item_(item),
          family_(family),
          style_(style),
          boundaries_(boundaries),
          sink_(sink) {}

    std::optional<std::vector<ShapedRun>> Run();

private:
    bool PrepareItem();
    bool PrepareBuffer();
    CandidateState* Candidate(std::size_t index);
    bool CandidateCoversGrapheme(CandidateState& candidate,
                                 const GraphemeSelection& grapheme);
    bool CandidateProbeAccepts(CandidateState& candidate,
                               const GraphemeSelection& grapheme);
    bool ShapeIntoBuffer(hb_font_t* font, std::uint32_t scalarOffset,
                         std::uint32_t scalarCount, bool beginningOfText,
                         bool endOfText);
    std::vector<SourceByteRange> ClusterExtents(unsigned int count,
                                                const hb_glyph_info_t* infos,
                                                std::uint32_t spanEndByte,
                                                bool* validOut) const;
    GraphemeRange GraphemesForBytes(SourceByteRange bytes) const;
    bool EmitSpan(const SelectedSpan& span, bool first, bool last,
                  std::vector<ShapedRun>& out);
    void EmitMissingSpan(const SelectedSpan& span, std::vector<ShapedRun>& out);
    void ReportMissing(SourceByteRange bytes);

    const UnicodeTextBuffer& buffer_;
    const UnicodeAnalysis&   analysis_;
    const AnalysisItem&      item_;
    const ResolvedFamily&    family_;
    const ShapeStyle&        style_;
    ShapeBoundaryFlags       boundaries_;
    TextDiagnosticSink&      sink_;

    // 아래 HarfBuzz 핸들들보다 먼저 선언되어 있으므로 가장 나중에 파괴된다.
    // 이 lease가 살아 있는 한 Shutdown이 진행될 수 없고, 따라서 hbBuffer_와
    // 후보 face들은 u_cleanup 이후의 ICU를 절대 볼 수 없다. Run() 안의 지역
    // 변수로 두면 핸들이 lease보다 오래 살아남아, 주석이 주장하는 불변식과
    // 코드가 지키는 불변식이 달라진다.
    std::optional<TextRuntimeClientHandle>        lease_;
    DiagnosticBudget                              budget_;
    hb_buffer_cluster_level_t                     clusterLevel_ =
        HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS;
    hb_direction_t                                direction_ = HB_DIRECTION_LTR;
    hb_script_t                                   script_    = HB_SCRIPT_COMMON;
    hb_language_t                                 language_  = nullptr;
    std::vector<hb_feature_t>                     features_;
    std::vector<hb_codepoint_t>                   values_;
    std::vector<std::uint32_t>                    clusters_;
    SourceByteRange                               lastContextBytes_;
    std::uint32_t                                 scalarBegin_ = 0;
    std::vector<GraphemeSelection>                graphemes_;
    std::vector<std::unique_ptr<CandidateState>>  candidates_;
    std::unique_ptr<ScopedHarfBuzzBuffer>         hbBuffer_;
};

// Step 5b/9b: item 전체의 sanitized scalar 배열과 그 scalar들의 원본 byte
// 시작을 만든다. 원본의 ill-formed byte는 절대 HarfBuzz로 넘어가지 않고,
// U+FFFD로 치환된 scalar가 자기 원본 byte 시작을 cluster로 들고 간다.
bool ItemShaper::PrepareItem() {
    const std::vector<std::uint32_t>& boundaries = analysis_.GraphemeBoundaries();
    if (item_.graphemes.end <= item_.graphemes.begin ||
        static_cast<std::size_t>(item_.graphemes.end) + 1U > boundaries.size()) {
        ReportLayoutInvalid(sink_,
                            "an analysis item names graphemes outside the "
                            "analysis it belongs to",
                            item_.sourceBytes);
        return false;
    }
    if (boundaries[item_.graphemes.begin] != item_.sourceBytes.begin ||
        boundaries[item_.graphemes.end] != item_.sourceBytes.end) {
        ReportLayoutInvalid(sink_,
                            "an analysis item's grapheme range and byte range "
                            "do not describe the same span",
                            item_.sourceBytes);
        return false;
    }

    // scalar 표는 byte 순서로 정렬되어 있다. 매 item마다 0부터 훑으면 문단
    // 하나를 셰이핑하는 비용이 item 수 x scalar 수가 된다.
    const std::vector<DecodedScalar>& scalars = buffer_.Scalars();
    const std::size_t                 first   = static_cast<std::size_t>(
        std::lower_bound(scalars.begin(), scalars.end(), item_.sourceBytes.begin,
                         [](const DecodedScalar& scalar, std::uint32_t value) {
                             return scalar.sourceBytes.begin < value;
                         }) -
        scalars.begin());
    const std::size_t last = static_cast<std::size_t>(
        std::lower_bound(scalars.begin(), scalars.end(), item_.sourceBytes.end,
                         [](const DecodedScalar& scalar, std::uint32_t value) {
                             return scalar.sourceBytes.end <= value;
                         }) -
        scalars.begin());
    if (first >= last || first >= scalars.size() ||
        scalars[first].sourceBytes.begin != item_.sourceBytes.begin ||
        scalars[last - 1U].sourceBytes.end != item_.sourceBytes.end) {
        ReportLayoutInvalid(sink_,
                            "an analysis item does not begin and end on a "
                            "decoded scalar boundary",
                            item_.sourceBytes);
        return false;
    }
    scalarBegin_ = static_cast<std::uint32_t>(first);
    values_.clear();
    clusters_.clear();
    values_.reserve(last - first);
    clusters_.reserve(last - first);
    for (std::size_t index = first; index < last; ++index) {
        values_.push_back(static_cast<hb_codepoint_t>(scalars[index].value));
        clusters_.push_back(scalars[index].sourceBytes.begin);
    }

    // grapheme 하나마다 자기 scalar 구간을 미리 붙인다. 경계는 scalar 경계의
    // 부분집합이므로 정확히 일치하는 scalar가 반드시 있다.
    graphemes_.clear();
    for (std::uint32_t grapheme = item_.graphemes.begin;
         grapheme < item_.graphemes.end; ++grapheme) {
        GraphemeSelection selection;
        selection.graphemeIndex = grapheme;
        selection.bytes = SourceByteRange{boundaries[grapheme],
                                          boundaries[grapheme + 1U]};
        // clusters_는 scalar 순서 그대로이므로 오름차순이다. 선형 탐색을 쓰면
        // grapheme마다 표를 다시 훑어 item 길이의 제곱이 된다.
        const auto beginAt = std::lower_bound(clusters_.begin(), clusters_.end(),
                                              selection.bytes.begin);
        const bool haveBegin =
            beginAt != clusters_.end() && *beginAt == selection.bytes.begin;
        selection.scalarBegin =
            static_cast<std::uint32_t>(beginAt - clusters_.begin());
        const auto endAt = std::lower_bound(clusters_.begin(), clusters_.end(),
                                            selection.bytes.end);
        bool haveEnd = endAt != clusters_.end() && *endAt == selection.bytes.end;
        selection.scalarEnd = static_cast<std::uint32_t>(endAt - clusters_.begin());
        if (!haveEnd && selection.bytes.end == item_.sourceBytes.end) {
            selection.scalarEnd = static_cast<std::uint32_t>(clusters_.size());
            haveEnd             = true;
        }
        if (!haveBegin || !haveEnd || selection.scalarEnd <= selection.scalarBegin) {
            ReportLayoutInvalid(sink_,
                                "a grapheme boundary fell inside a decoded "
                                "scalar",
                                selection.bytes);
            return false;
        }
        graphemes_.push_back(selection);
    }
    return !graphemes_.empty();
}

bool ItemShaper::PrepareBuffer() {
    // 방향은 item의 정확한 resolved level에서 온다. UnicodeAnalysis.h가 경고하는
    // 예외 아홉 자(LRE/RLE/LRO/RLO/PDF와 네 isolate)에서는 이 값이 UBA의 보장이
    // 아니라 ICU의 보존 규약이다. 그래도 출력이 달라지지 않는 이유는 그 아홉이
    // 전부 default ignorable이고 각자 혼자 item이 되기 때문이다: HarfBuzz가
    // 숨기는 zero-advance glyph 하나뿐이라 배치할 순서 자체가 없다.
    direction_ = (item_.embeddingLevel & 1U) != 0U ? HB_DIRECTION_RTL
                                                   : HB_DIRECTION_LTR;
    script_ = hb_icu_script_to_script(
        static_cast<UScriptCode>(item_.scriptCode));
    CountHarfBuzzObject();
    language_ = hb_language_from_string(
        style_.language.c_str(), static_cast<int>(style_.language.size()));

    // Step 5c: 저작 순서 그대로, 겹침도 그대로. 정규화하면 "뒤에 적힌 것이
    // 이긴다"는 저작 규칙이 조용히 사라진다.
    features_.clear();
    features_.reserve(style_.orderedFeatures.size());
    for (const ShapeFeature& feature : style_.orderedFeatures) {
        hb_feature_t converted{};
        converted.tag   = feature.tag;
        converted.value = feature.value;
        converted.start = feature.sourceBytes.begin;
        converted.end   = feature.sourceBytes.end;
        features_.push_back(converted);
    }

    hbBuffer_ = std::make_unique<ScopedHarfBuzzBuffer>();
    if (hbBuffer_->Get() == nullptr) {
        ReportLayoutInvalid(sink_,
                            "HarfBuzz could not allocate a shaping buffer",
                            item_.sourceBytes);
        return false;
    }
    CountHarfBuzzObject();
    hb_unicode_funcs_t* unicode = hb_icu_get_unicode_funcs();
    if (unicode == nullptr) {
        ReportLayoutInvalid(sink_,
                            "HarfBuzz could not bind the ICU Unicode "
                            "functions",
                            item_.sourceBytes);
        return false;
    }
    // 이 대입이 사라지면 HarfBuzz는 조용히 자기 내장 UCD 표로 셰이핑한다.
    // 그러면 Unicode 속성 판정이 item을 만든 ICU 분석과 어긋나는데, 출력은
    // 대개 그대로라 어떤 단언도 움직이지 않는다. 그래서 ShapeIntoBuffer가
    // hb_shape 직전에 buffer의 funcs를 실제로 읽어 관찰로 남긴다.
    hb_buffer_set_unicode_funcs(hbBuffer_->Get(), unicode);
    return true;
}

CandidateState* ItemShaper::Candidate(std::size_t index) {
    CandidateState& state = *candidates_[index];
    if (state.fontFailed) return nullptr;
    if (!state.fontReady) {
        // 여는 데 실패한 후보는 아무것도 덮지 않는 후보와 똑같이 넘어간다.
        // 여기서 진단을 내지 않는 이유는 그 사유가 이미 다른 곳에 있기
        // 때문이다: 이 바이트는 FontRepository가 검증하고 발행한 것이고,
        // 실패의 사유는 그쪽의 FontInvalid에 적힌다. 후보가 전부 실패하면
        // 결과는 조용해지는 대신 missing 기록과 MissingGlyph가 된다.
        if (!state.font.Open(state.face->resource, style_.fontSize)) {
            state.fontFailed = true;
            return nullptr;
        }
        state.fontReady = true;
    }
    return &state;
}

// Step 7: grapheme의 필수 scalar를 후보가 덮는가.
bool ItemShaper::CandidateCoversGrapheme(CandidateState& candidate,
                                         const GraphemeSelection& grapheme) {
    if (candidate.face->resource == nullptr ||
        candidate.face->resource->asset == nullptr) {
        return false;
    }
    const std::vector<std::pair<char32_t, char32_t>>& coverage =
        candidate.face->resource->asset->coverage;
    const std::vector<DecodedScalar>& scalars = buffer_.Scalars();
    for (std::uint32_t index = grapheme.scalarBegin; index < grapheme.scalarEnd;
         ++index) {
        const char32_t value = scalars[scalarBegin_ + index].value;
        // 선택자 자신과 default ignorable은 독립된 glyph를 요구하지 않는다.
        if (IsVariationSelector(value) || IsDefaultIgnorable(value)) continue;
        const bool hasSelector =
            index + 1U < grapheme.scalarEnd &&
            IsVariationSelector(scalars[scalarBegin_ + index + 1U].value);
        if (hasSelector) {
            const char32_t selector = scalars[scalarBegin_ + index + 1U].value;
            if (!candidate.uvsParsed) {
                candidate.uvs.Parse(*candidate.face->resource->bytes,
                                    candidate.face->resource->faceIndex);
                candidate.uvsParsed = true;
            }
            // Step 7a: 명시적 UVS 기록을 먼저 본다. 기록이 있으면 base가 평범한
            // cmap에 없어도 그 face는 그 쌍을 그릴 수 있다. 기록이 없으면 그
            // 선택자는 base의 기본 표현을 요청한 것이므로 base의 coverage로
            // 판정한다 — 어느 쪽도 선택자 자신의 glyph를 요구하지 않는다.
            //
            // 이 규칙은 VS15/VS16(text/emoji presentation)에도 그대로 적용된다.
            // 그 둘도 "이 face가 그 쌍을 이름 붙였는가, 아니면 base의 기본
            // 표현인가"로만 판정한다. 두 표현을 실제로 갈라 주는 것은 color
            // table을 가진 face인데, 이 프로덕션 범위는 COLR/CPAL/CBDT/CBLC/
            // sbix/SVG를 전부 거절하므로 남는 답은 언제나 base의 기본 표현
            // 하나뿐이다. 컬러 face가 허용되는 날 이 자리가 갈라져야 한다.
            const bool named = candidate.uvs.Names(value, selector);
            RecordBounded(g_observations.variationDecisions,
                          detail::ShapingVariationDecision{value, selector,
                                                           named});
            if (named) continue;
        }
        if (!CoverageContains(coverage, value)) return false;
    }
    return true;
}

// Step 8: 후보마다 완전한 AnalysisItem을 셰이핑하고, 대상 grapheme과 겹치는
// .notdef가 있을 때에만 그 후보를 거절한다. 주변 문맥의 .notdef는 대상 face를
// 거절하지 않는다 — 거절하면 알 수 없는 문자 하나가 문단 전체의 폰트를 바꾼다.
bool ItemShaper::CandidateProbeAccepts(CandidateState& candidate,
                                       const GraphemeSelection& grapheme) {
    if (!candidate.probed) {
        candidate.probed = true;
        if (!ShapeIntoBuffer(candidate.font.Font(), 0,
                             static_cast<std::uint32_t>(values_.size()),
                             boundaries_.beginningOfText,
                             boundaries_.endOfText)) {
            candidate.probeFailed = true;
        } else {
            unsigned int           count = 0;
            const hb_glyph_info_t* infos =
                hb_buffer_get_glyph_infos(hbBuffer_->Get(), &count);
            bool valid = false;
            const std::vector<SourceByteRange> extents =
                ClusterExtents(count, infos, item_.sourceBytes.end, &valid);
            if (!valid) {
                candidate.probeFailed = true;
            } else {
                for (unsigned int index = 0; index < count; ++index) {
                    if (infos[index].codepoint != 0U) continue;
                    candidate.probeNotdefRanges.push_back(extents[index]);
                    RecordBounded(
                        g_observations.probeNotdefs,
                        detail::ShapingProbeNotdef{extents[index]});
                }
            }
        }
    }
    if (candidate.probeFailed) return false;
    for (const SourceByteRange& notdef : candidate.probeNotdefRanges) {
        if (notdef.begin < grapheme.bytes.end &&
            grapheme.bytes.begin < notdef.end) {
            return false;
        }
    }
    return true;
}

// Step 5b: 완전한 sanitized 배열을 넘기고 span만 buffer 내용으로 만든다.
// HarfBuzz는 배열의 나머지를 pre/post 문맥으로 붙들므로, span을 잘라 따로
// 넘기는 것과 달리 문맥이 유지된다. 그런 다음 아직 셰이핑되지 않은 입력
// info의 cluster를 원본 UTF-8 byte 시작으로 다시 쓴다.
bool ItemShaper::ShapeIntoBuffer(hb_font_t* font, std::uint32_t scalarOffset,
                                 std::uint32_t scalarCount,
                                 bool beginningOfText, bool endOfText) {
    if (font == nullptr) return false;
    hb_buffer_t* buffer = hbBuffer_->Get();
    hb_buffer_clear_contents(buffer);
    hb_buffer_set_direction(buffer, direction_);
    hb_buffer_set_script(buffer, script_);
    hb_buffer_set_language(buffer, language_);
    hb_buffer_set_cluster_level(buffer, clusterLevel_);
    unsigned int flags = HB_BUFFER_FLAG_DEFAULT;
    if (beginningOfText) flags |= HB_BUFFER_FLAG_BOT;
    if (endOfText) flags |= HB_BUFFER_FLAG_EOT;
    hb_buffer_set_flags(buffer, static_cast<hb_buffer_flags_t>(flags));

    // 문맥으로 실제로 넘긴 배열의 원본 byte 구간을 기록한다. 아래 두 값은
    // hb_buffer_add_codepoints에 넘기는 바로 그 포인터와 길이에서만 나온다 —
    // item에서 되유도하면 "span만 잘라 넘겼다"는 회귀가 관찰에 잡히지 않고,
    // 관찰은 문맥이 잘렸는데도 계속 "item 전체를 넘겼다"고 답한다.
    const hb_codepoint_t* text       = values_.data();
    const int             textLength = static_cast<int>(values_.size());
    hb_buffer_add_codepoints(buffer, text, textLength,
                             static_cast<int>(scalarOffset),
                             static_cast<int>(scalarCount));
    const std::size_t contextFirst =
        static_cast<std::size_t>(text - values_.data());
    const std::vector<DecodedScalar>& scalars = buffer_.Scalars();
    lastContextBytes_ = SourceByteRange{
        clusters_[contextFirst],
        scalars[scalarBegin_ + contextFirst +
                static_cast<std::size_t>(textLength) - 1U]
            .sourceBytes.end};
    unsigned int     inputCount = 0;
    hb_glyph_info_t* inputs = hb_buffer_get_glyph_infos(buffer, &inputCount);
    if (inputCount != scalarCount || inputs == nullptr) return false;
    for (unsigned int index = 0; index < inputCount; ++index) {
        inputs[index].cluster = clusters_[scalarOffset + index];
    }
    // hb_shape 직전에 읽는다. hb_buffer_clear_contents는 unicode funcs를
    // 남기지만 hb_buffer_reset은 지우므로, "설치했다"가 아니라 "이 셰이핑이
    // 실제로 그것을 들고 있었다"를 관찰해야 그 차이가 잡힌다.
    ++g_observations.shapeCalls;
    if (hb_buffer_get_unicode_funcs(buffer) == hb_icu_get_unicode_funcs()) {
        ++g_observations.shapeCallsWithIcuUnicodeFuncs;
    }
    hb_shape(font, buffer, features_.data(),
             static_cast<unsigned int>(features_.size()));
    return true;
}

// Step 9d: cluster 하나가 덮는 원본 byte 구간. MONOTONE_CHARACTERS에서
// cluster 값은 단조이므로, 정렬된 서로 다른 cluster들의 다음 값이 곧 끝이다.
std::vector<SourceByteRange> ItemShaper::ClusterExtents(
    unsigned int count, const hb_glyph_info_t* infos,
    std::uint32_t spanEndByte, bool* validOut) const {
    *validOut = false;
    std::vector<SourceByteRange> extents(count);
    std::vector<std::uint32_t>   distinct;
    distinct.reserve(count);
    for (unsigned int index = 0; index < count; ++index) {
        distinct.push_back(infos[index].cluster);
    }
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()),
                   distinct.end());
    for (unsigned int index = 0; index < count; ++index) {
        const std::uint32_t cluster = infos[index].cluster;
        const auto position =
            std::lower_bound(distinct.begin(), distinct.end(), cluster);
        if (position == distinct.end() || *position != cluster) return extents;
        const std::size_t rank =
            static_cast<std::size_t>(position - distinct.begin());
        const std::uint32_t end =
            rank + 1U < distinct.size() ? distinct[rank + 1U] : spanEndByte;
        if (cluster >= end) return extents;
        extents[index] = SourceByteRange{cluster, end};
    }
    *validOut = true;
    return extents;
}

// 구간이 닿는 grapheme들. 경계표는 오름차순이므로 이분 탐색으로 찾는다:
// glyph마다 전체 grapheme을 훑으면 긴 문단에서 item 길이의 제곱이 된다.
// 호출자가 이미 [begin, end)가 비어 있지 않고 item 안에 있음을 보장한다.
GraphemeRange ItemShaper::GraphemesForBytes(SourceByteRange bytes) const {
    const std::vector<std::uint32_t>& boundaries = analysis_.GraphemeBoundaries();
    const auto firstAfterBegin =
        std::upper_bound(boundaries.begin(), boundaries.end(), bytes.begin);
    const auto firstAfterLast =
        std::upper_bound(boundaries.begin(), boundaries.end(), bytes.end - 1U);
    const auto first = static_cast<std::uint32_t>(
        (firstAfterBegin - boundaries.begin()) - 1);
    const auto last = static_cast<std::uint32_t>(
        (firstAfterLast - boundaries.begin()) - 1);
    return GraphemeRange{first, last + 1U};
}

void ItemShaper::ReportMissing(SourceByteRange bytes) {
    ReportBounded(sink_, budget_, TextDiagnosticCode::MissingGlyph,
           "no font in the resolved family can render this grapheme",
           "Add a fallback family whose fonts cover this script, or author "
           "the text with characters the family covers.",
           bytes);
}

// Step 10: 후보가 전부 실패한 grapheme은 실패가 아니라 결정적인 기록이다.
// 크기는 정확히 1 em이고 face 자원은 없다. ASCII 대체도 시스템 폰트 조회도
// 하지 않는다 — 그렇게 하면 데이터가 빠진 패키지가 개발 기계에서만 멀쩡해진다.
void ItemShaper::EmitMissingSpan(const SelectedSpan& span,
                                 std::vector<ShapedRun>& out) {
    ShapedGlyph glyph;
    glyph.fontSize       = style_.fontSize;
    glyph.advanceX       = style_.fontSize;
    glyph.sourceBytes    = span.bytes;
    glyph.graphemes      = GraphemeRange{span.graphemeBegin, span.graphemeEnd};
    glyph.bidiLevel      = item_.embeddingLevel;
    glyph.logicalRunId   = item_.logicalRunId;
    glyph.missing        = true;
    ShapedRun run;
    run.bidiLevel = item_.embeddingLevel;
    run.glyphs.push_back(std::move(glyph));
    out.push_back(std::move(run));
    ReportMissing(span.bytes);
}

bool ItemShaper::EmitSpan(const SelectedSpan& span, bool first, bool last,
                          std::vector<ShapedRun>& out) {
    CandidateState& state = *candidates_[static_cast<std::size_t>(span.candidate)];
    // Step 9a: BOT/EOT는 span의 진짜 텍스트 끝에서만 온다.
    if (!ShapeIntoBuffer(state.font.Font(), span.scalarBegin,
                         span.scalarEnd - span.scalarBegin,
                         first && boundaries_.beginningOfText,
                         last && boundaries_.endOfText)) {
        ReportLayoutInvalid(sink_,
                            "HarfBuzz refused the final shaping call for a "
                            "selected span",
                            span.bytes);
        return false;
    }
    RecordBounded(g_observations.finalSpanShapes,
                  detail::ShapingFinalSpanShape{
                      GraphemeRange{span.graphemeBegin, span.graphemeEnd},
                      lastContextBytes_, item_.sourceBytes});

    hb_buffer_t*               buffer = hbBuffer_->Get();
    unsigned int               count  = 0;
    const hb_glyph_info_t*     infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(buffer, &count);
    if ((count > 0 && (infos == nullptr || positions == nullptr))) {
        ReportLayoutInvalid(sink_,
                            "HarfBuzz produced no glyph records for a shaped "
                            "span",
                            span.bytes);
        return false;
    }
    bool                               valid   = false;
    const std::vector<SourceByteRange> extents =
        ClusterExtents(count, infos, span.bytes.end, &valid);
    if (!valid) {
        ReportLayoutInvalid(sink_,
                            "a shaped cluster is not an original scalar byte "
                            "start",
                            span.bytes);
        return false;
    }

    ShapedRun run;
    run.bidiLevel = item_.embeddingLevel;
    run.glyphs.reserve(count);
    for (unsigned int index = 0; index < count; ++index) {
        // Step 9d: span 밖으로 나간 구간은 추정하지 않고 실패한다.
        if (extents[index].begin < span.bytes.begin ||
            extents[index].end > span.bytes.end) {
            ReportLayoutInvalid(sink_,
                                "a shaped cluster resolved outside its "
                                "selected span",
                                span.bytes);
            return false;
        }
        if (!std::binary_search(clusters_.begin(), clusters_.end(),
                                extents[index].begin)) {
            ReportLayoutInvalid(sink_,
                                "a shaped cluster is not an original scalar "
                                "byte start",
                                span.bytes);
            return false;
        }
        ShapedGlyph glyph;
        glyph.fontGuid     = state.face->fontGuid;
        glyph.fontRevision = state.face->fontRevision;
        glyph.faceIndex    = state.face->faceIndex;
        glyph.glyphId      = infos[index].codepoint;
        glyph.faceResource = state.face->resource;
        glyph.fontSize     = style_.fontSize;
        glyph.advanceX     = Fixed26_6::FromRaw(positions[index].x_advance);
        glyph.advanceY     = Fixed26_6::FromRaw(positions[index].y_advance);
        glyph.offsetX      = Fixed26_6::FromRaw(positions[index].x_offset);
        glyph.offsetY      = Fixed26_6::FromRaw(positions[index].y_offset);
        glyph.sourceBytes  = extents[index];
        glyph.graphemes    = GraphemesForBytes(extents[index]);
        glyph.bidiLevel    = item_.embeddingLevel;
        glyph.logicalRunId = item_.logicalRunId;
        glyph.harfbuzzGlyphFlags = static_cast<std::uint32_t>(
            hb_glyph_info_get_glyph_flags(&infos[index]));
        // Step 6a: caret은 바로 이 span을 셰이핑한 font와 방향으로 묻는다.
        // 두 번 호출 규약의 개수가 어긋나면 합성하지 않고 실패한다.
        unsigned int       declared = 0;
        const unsigned int total    = hb_ot_layout_get_ligature_carets(
            state.font.Font(), direction_, infos[index].codepoint, 0, &declared,
            nullptr);
        if (total > 0) {
            std::vector<hb_position_t> raw(total);
            unsigned int               filled = total;
            hb_ot_layout_get_ligature_carets(state.font.Font(), direction_,
                                             infos[index].codepoint, 0, &filled,
                                             raw.data());
            if (filled != total) {
                ReportLayoutInvalid(sink_,
                                    "HarfBuzz returned an inconsistent GDEF "
                                    "ligature caret count",
                                    span.bytes);
                return false;
            }
            glyph.adjustedGdefCaretOffsets.reserve(total);
            for (unsigned int caret = 0; caret < total; ++caret) {
                glyph.adjustedGdefCaretOffsets.push_back(
                    Fixed26_6::FromRaw(raw[caret]));
            }
        }
        run.glyphs.push_back(std::move(glyph));
    }
    out.push_back(std::move(run));
    return true;
}

std::optional<std::vector<ShapedRun>> ItemShaper::Run() {
    // Step 4. blob/face/font/buffer를 하나라도 만들기 전에 준비 상태를 본다.
    if (!TextRuntimeDependencies::Get().IsReady()) {
        ReportTerminal(sink_, TextDiagnosticCode::DependencyInvalid,
               "shaping requires a ready text runtime; ICU and HarfBuzz have "
               "not been initialized in this process",
               "Create a TextRuntimeLifetimeGuard from a verified Engine/Text "
               "root before shaping text, and keep it alive for as long as any "
               "text service exists.",
               item_.sourceBytes);
        return std::nullopt;
    }
    // 셰이핑이 진행되는 동안 수명을 붙잡는다. Shutdown은 client handle이 전부
    // 사라진 뒤에만 진행되므로, 이 lease가 살아 있는 한 아래 HarfBuzz 객체들은
    // u_cleanup 이후의 ICU normalizer를 볼 수 없다. 지역 변수가 아니라 가장
    // 먼저 선언된 멤버인 이유는 그 주장이 Run()이 돌아온 뒤 핸들이 파괴되는
    // 순간까지도 참이어야 하기 때문이다.
    lease_ = TextRuntimeClientHandle::Acquire();
    if (!lease_) {
        ReportTerminal(sink_, TextDiagnosticCode::DependencyInvalid,
               "shaping could not acquire a text runtime client handle",
               "Shape text only while the process text runtime is ready; a "
               "terminally cleaned runtime never becomes ready again.",
               item_.sourceBytes);
        return std::nullopt;
    }
    const auto clusterLevel = ToHarfBuzzClusterLevel(style_.clusterPolicy);
    if (!clusterLevel) {
        ReportLayoutInvalid(sink_,
                            "the requested text cluster policy is not a value "
                            "this build defines",
                            item_.sourceBytes);
        return std::nullopt;
    }
    clusterLevel_ = *clusterLevel;

    if (!PrepareBuffer()) return std::nullopt;
    if (!PrepareItem()) return std::nullopt;

    candidates_.clear();
    candidates_.reserve(family_.candidates.size());
    for (const ResolvedFace& face : family_.candidates) {
        auto state  = std::make_unique<CandidateState>();
        state->face = &face;
        candidates_.push_back(std::move(state));
    }

    // Step 9: 최종 glyph를 하나도 내기 전에 모든 grapheme의 face 선택을 끝낸다.
    // probe 결과는 최종 출력이 아니며 어디에도 복사되지 않는다.
    for (GraphemeSelection& grapheme : graphemes_) {
        const std::vector<DecodedScalar>& scalars = buffer_.Scalars();
        for (std::uint32_t index = grapheme.scalarBegin;
             index < grapheme.scalarEnd; ++index) {
            const char32_t value = scalars[scalarBegin_ + index].value;
            if (IsVariationSelector(value) || IsDefaultIgnorable(value)) continue;
            RecordBounded(g_observations.cmapRequirements, value);
        }
        // 후보는 저작된 순서 그대로 보고 처음 받아들여진 것에서 멈춘다. 이
        // 순서는 정렬하거나 다시 유도하지 않는다.
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            CandidateState& probed = *candidates_[index];
            if (probed.fontFailed) continue;
            // coverage는 순수 카탈로그 데이터라 face를 열지 않아도 답이 나온다.
            // 여기서 떨어질 후보까지 blob/face/font를 먼저 열면, 지원되지 않는
            // script로 적힌 라벨 하나가 item마다 4.6MB짜리 CJK face를 여닫는다.
            if (!CandidateCoversGrapheme(probed, grapheme)) continue;
            CandidateState* state = Candidate(index);
            if (state == nullptr) continue;
            if (!CandidateProbeAccepts(*state, grapheme)) continue;
            grapheme.candidate           = static_cast<int>(index);
            grapheme.key.resource        = state->face->resource.get();
            grapheme.key.faceIndex       = state->face->faceIndex;
            grapheme.key.fontRevision    = state->face->fontRevision;
            grapheme.key.bidiLevel       = item_.embeddingLevel;
            grapheme.key.scriptCode      = item_.scriptCode;
            grapheme.key.rightToLeft     = direction_ == HB_DIRECTION_RTL;
            break;
        }
    }

    // Step 9a: 인접한 grapheme은 선택된 face 정체성과 셰이핑 문맥이 모두 같을
    // 때에만 합친다.
    //
    // 합침 조건이 검사하는 것과 검사하지 않는 것을 나눠 적어 둔다. face 자원
    // 정체성/face index/revision, 정확한 level, script, 방향은 GraphemeSpanKey가
    // 직접 비교한다. 폰트 크기, cluster 정책, 순서 있는 feature, 언어는 한 번의
    // 호출에 하나뿐인 ShapeStyle에서 오므로 하나의 item 안에서 달라질 수 없고,
    // 문단/isolate/control 경계는 UnicodeTextAnalyzer가 이미 item을 나누는
    // 자리이므로 item 안에는 존재하지 않는다. 그래서 실제로 달라질 수 있는 것은
    // 선택된 face뿐이다. 없는 grapheme은 절대 합치지 않고 자기 혼자 절차적
    // 기록이 된다.
    std::vector<SelectedSpan>  spans;
    const GraphemeSelection*   previous = nullptr;
    for (const GraphemeSelection& grapheme : graphemes_) {
        const bool mergeable = !spans.empty() && previous != nullptr &&
                               spans.back().candidate >= 0 &&
                               grapheme.candidate >= 0 &&
                               previous->key == grapheme.key;
        previous = &grapheme;
        if (mergeable) {
            spans.back().graphemeEnd = grapheme.graphemeIndex + 1U;
            spans.back().scalarEnd   = grapheme.scalarEnd;
            spans.back().bytes.end   = grapheme.bytes.end;
            continue;
        }
        SelectedSpan span;
        span.graphemeBegin = grapheme.graphemeIndex;
        span.graphemeEnd   = grapheme.graphemeIndex + 1U;
        span.scalarBegin   = grapheme.scalarBegin;
        span.scalarEnd     = grapheme.scalarEnd;
        span.bytes         = grapheme.bytes;
        span.candidate     = grapheme.candidate;
        spans.push_back(span);
    }
    g_observations.selectedSpans = spans.size();

    // Step 9e: 선택된 span은 item을 빈틈 없이, 겹침 없이 덮어야 한다.
    std::uint32_t cursor = item_.graphemes.begin;
    for (const SelectedSpan& span : spans) {
        if (span.graphemeBegin != cursor || span.graphemeEnd <= span.graphemeBegin) {
            ReportLayoutInvalid(sink_,
                                "selected spans do not cover the analysis item "
                                "exactly once",
                                item_.sourceBytes);
            return std::nullopt;
        }
        cursor = span.graphemeEnd;
    }
    if (cursor != item_.graphemes.end) {
        ReportLayoutInvalid(sink_,
                            "selected spans do not reach the end of the "
                            "analysis item",
                            item_.sourceBytes);
        return std::nullopt;
    }

    std::vector<ShapedRun> runs;
    runs.reserve(spans.size());
    for (std::size_t index = 0; index < spans.size(); ++index) {
        const SelectedSpan& span = spans[index];
        if (span.candidate < 0) {
            EmitMissingSpan(span, runs);
            continue;
        }
        if (!EmitSpan(span, index == 0, index + 1U == spans.size(), runs)) {
            return std::nullopt;
        }
    }

    // Step 9e: 돌려주기 직전에 다시 확인한다. span 하나에 run 하나가 정확히
    // 대응하고, 모든 기록이 자기 span 안에 있다. HarfBuzz 출력 하나가 두 번
    // 복사되는 일은 EmitSpan이 출력 배열을 한 번만 훑기 때문에 구조적으로
    // 불가능하고, 그 사실이 여기서 run/span 대응으로 다시 확인된다.
    if (runs.size() != spans.size()) {
        ReportLayoutInvalid(sink_,
                            "shaped runs and selected spans disagree",
                            item_.sourceBytes);
        return std::nullopt;
    }
    for (std::size_t index = 0; index < spans.size(); ++index) {
        for (const ShapedGlyph& glyph : runs[index].glyphs) {
            if (glyph.sourceBytes.begin < spans[index].bytes.begin ||
                glyph.sourceBytes.end > spans[index].bytes.end) {
                ReportLayoutInvalid(sink_,
                                    "a shaped record does not belong to the "
                                    "span that produced it",
                                    spans[index].bytes);
                return std::nullopt;
            }
        }
    }
    return runs;
}

}  // namespace

std::optional<std::vector<ShapedRun>> TextShapingService::ShapeAnalysisItem(
    const UnicodeTextBuffer& buffer, const UnicodeAnalysis& analysis,
    const AnalysisItem& item, const ResolvedFamily& family,
    const ShapeStyle& style, ShapeBoundaryFlags boundaries,
    TextDiagnosticSink& sink) {
    // 용량은 남기고 내용만 비운다. 대입하면 호출마다 네 벡터의 버퍼를 해제하고
    // 다시 잡게 되는데, 이 함수는 문단마다 item 수만큼 불린다.
    g_observations.cmapRequirements.clear();
    g_observations.probeNotdefs.clear();
    g_observations.finalSpanShapes.clear();
    g_observations.variationDecisions.clear();
    g_observations.selectedSpans                 = 0;
    g_observations.shapeCalls                    = 0;
    g_observations.shapeCallsWithIcuUnicodeFuncs = 0;
    ItemShaper shaper(buffer, analysis, item, family, style, boundaries, sink);
    return shaper.Run();
}

}  // namespace molga::text
