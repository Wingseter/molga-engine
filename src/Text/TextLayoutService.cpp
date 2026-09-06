#include "Text/TextLayoutService.h"

#include "Assets/FontAsset.h"
#include "Text/TextRuntimeDependencies.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"

#include <hb.h>
#include <unicode/ubidi.h>
#include <unicode/uvernum.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace molga::text {

namespace {

constexpr const char* kSubsystem = "text.layout";

// ── Process-wide observation seams ──────────────────────────────────────────
detail::LayoutObservations g_observations;
std::uint64_t              g_layoutIcuObjectCreations = 0;

// 호출 직전에 올린다. 사후에 세면 "만들려다 실패해서 흔적이 없다"는 가장
// 위험한 경우가 0으로 보인다(UnicodeAnalysis.h의 계수기와 같은 규칙).
void CountIcuObject() noexcept { ++g_layoutIcuObjectCreations; }

template <class T>
void RecordBounded(std::vector<T>& out, T value) {
    if (out.size() >= detail::kMaxLayoutObservations) return;
    out.push_back(std::move(value));
}

void ResetObservations() {
    // 용량은 남기고 내용만 비운다. 대입하면 호출마다 세 벡터의 버퍼를 해제하고
    // 다시 잡게 된다.
    g_observations.paragraphShapePasses = 0;
    g_observations.acceptedLineShapes.clear();
    g_observations.finalLineItemShapeCalls = 0;
    g_observations.backtrackSteps = 0;
    g_observations.discardedUnsafeBoundaries.clear();
    g_observations.ellipsisRemovalAttempts = 0;
    g_observations.ellipsisCandidates.clear();
    g_observations.requestKey.reset();
    g_observations.finalKey.reset();
}

// ── Checked 26.6 helpers ────────────────────────────────────────────────────
// 전부 fail-closed다. 포화시키면 배치가 조용히 잘못된 자리를 내고, 그 오류는
// 진단 없이 픽셀로만 드러난다.
bool AddChecked(Fixed26_6 a, Fixed26_6 b, Fixed26_6& out) {
    const auto sum = Fixed26_6::CheckedAdd(a, b);
    if (!sum) return false;
    out = *sum;
    return true;
}

bool SubChecked(Fixed26_6 a, Fixed26_6 b, Fixed26_6& out) {
    const auto difference = Fixed26_6::CheckedSub(a, b);
    if (!difference) return false;
    out = *difference;
    return true;
}

bool MulDivChecked(Fixed26_6 value, std::int64_t numerator,
                   std::int64_t denominator, Fixed26_6& out) {
    const auto scaled = Fixed26_6::CheckedMulDiv(value, numerator, denominator);
    if (!scaled) return false;
    out = *scaled;
    return true;
}

// ── Diagnostics ─────────────────────────────────────────────────────────────
void Emit(TextDiagnosticSink& sink, TextDiagnosticCode code,
          TextSeverity severity, std::string message, std::string remediation,
          const TextDiagnosticContext& context, SourceByteRange range) {
    TextDiagnostic diagnostic;
    diagnostic.code            = code;
    diagnostic.severity        = severity;
    diagnostic.subsystem       = kSubsystem;
    diagnostic.message         = std::move(message);
    diagnostic.remediation     = std::move(remediation);
    diagnostic.assetGuid       = context.assetGuid;
    diagnostic.sceneObjectId   = context.sceneObjectId;
    diagnostic.componentType   = context.componentType;
    diagnostic.sourceByteRange = range;
    sink.Report(std::move(diagnostic));
}

// 종결 진단은 kMaxLayoutDiagnosticsPerParagraph 밖이다. 곧바로 nullopt로
// 이어지므로 호출당 많아야 하나이고, 예산에 밀려 사라지면 "실패했는데 이유를
// 말하는 진단이 없다"가 된다(TextShapingService.cpp와 같은 규칙).
void ReportTerminal(TextDiagnosticSink& sink, TextDiagnosticCode code,
                    std::string message, std::string remediation,
                    const TextDiagnosticContext& context,
                    SourceByteRange range) {
    Emit(sink, code, TextSeverity::Error, std::move(message),
         std::move(remediation), context, range);
}

// ── Validation facts ────────────────────────────────────────────────────────
bool SameFact(const TextValidationFact& a, const TextValidationFact& b) {
    // Step 5b가 정한 안정 tuple 그대로다. 이 목록이 구조체보다 짧아지면 서로
    // 다른 두 사실이 하나로 접혀 조용히 사라진다.
    //
    // 값싸고 잘 갈라지는 항을 앞에 둔다. 한 문단이 사실을 무더기로 내는 유일한
    // 입력(깨진 UTF-8)에서는 code/severity/message/remediation이 전부 같으므로,
    // 문자열을 먼저 비교하면 비교 하나가 200바이트짜리 memcmp 두 번이 된다.
    //
    // 아래 FactBucketHash는 이 목록의 부분집합만 접는다. 그래서 hash가 같은
    // 짝만 여기까지 오고, hash에 든 항은 그 짝에서는 이미 같다 — 이 함수의
    // 권위는 warm 경로의 ContainsFact가 hash 없이 그대로 쓰는 데서 온다.
    // 어느 항을 빼려면 두 곳에서 함께 빼야 하고, 그러면 서로 다른 두 사실이
    // 하나로 접혀 조용히 사라진다.
    return a.sourceBytes == b.sourceBytes && a.graphemes == b.graphemes &&
           a.code == b.code && a.severity == b.severity &&
           a.recoverableAtRuntime == b.recoverableAtRuntime &&
           a.blocksAuthoredPackage == b.blocksAuthoredPackage &&
           a.subsystem == b.subsystem && a.message == b.message &&
           a.remediation == b.remediation;
}

bool ContainsFact(const std::vector<TextValidationFact>& facts,
                  const TextValidationFact& fact) {
    for (const TextValidationFact& existing : facts) {
        if (SameFact(existing, fact)) return true;
    }
    return false;
}

// 안정 tuple의 스칼라 절반만 접은 값. 문자열은 일부러 빼 두었다: 사실이 무더기로
// 나오는 입력에서는 문자열이 전부 같고 범위만 다르므로, 범위만 접어도 bucket이
// 갈라지고 문자열 비교는 SameFact 안에서 충돌한 짝에만 일어난다.
std::uint64_t FactBucketHash(const TextValidationFact& fact) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto fold = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFFULL;
            hash *= 1099511628211ULL;
        }
    };
    fold(static_cast<std::uint64_t>(fact.code));
    fold(static_cast<std::uint64_t>(fact.severity));
    fold(fact.sourceBytes.begin);
    fold(fact.sourceBytes.end);
    fold(fact.graphemes.begin);
    fold(fact.graphemes.end);
    fold(static_cast<std::uint64_t>(fact.recoverableAtRuntime ? 1 : 0));
    fold(static_cast<std::uint64_t>(fact.blocksAuthoredPackage ? 1 : 0));
    return hash;
}

// 삽입 순서를 지키면서 안정 tuple로 중복을 지우는 집합.
//
// 목록 전체를 훑는 중복 검사는 쓸 수 없다. UnicodeTextBuffer는 잘못된 최대
// 부분열마다 진단을 하나씩 내고 상한을 두지 않으므로(그 헤더가 적어 둔 대로
// 상한은 문맥을 아는 호출자인 여기의 몫이다), UTF-8로 잘못 읽힌 1MB Latin-1
// 파일은 사실 백만 개가 된다. 그때 선형 검사는 문단 하나에 수십 분이다.
class FactSet {
public:
    void Append(TextValidationFact fact) {
        const std::uint64_t bucket = FactBucketHash(fact);
        std::vector<std::uint32_t>& slot = index_[bucket];
        for (const std::uint32_t existing : slot) {
            if (SameFact(facts_[existing], fact)) return;
        }
        slot.push_back(static_cast<std::uint32_t>(facts_.size()));
        facts_.push_back(std::move(fact));
    }

    void Assign(const FactSet& other) {
        facts_ = other.facts_;
        index_ = other.index_;
    }

    const std::vector<TextValidationFact>& Facts() const noexcept {
        return facts_;
    }

private:
    std::vector<TextValidationFact>                          facts_;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> index_;
};

// 배치를 성공시키는 실패들. 런타임에서는 복구되지만(치환 문자, 절차적 두부
// glyph) 저작 패키지에서는 배포를 막아야 한다.
bool BlocksAuthoredPackage(TextDiagnosticCode code) noexcept {
    switch (code) {
        case TextDiagnosticCode::Utf8Invalid:
        case TextDiagnosticCode::FontInvalid:
        case TextDiagnosticCode::FontFamilyInvalid:
        case TextDiagnosticCode::MissingGlyph:
            return true;
        default:
            return false;
    }
}

TextValidationFact FactFromDiagnostic(const TextDiagnostic& diagnostic,
                                      GraphemeRange graphemes) {
    TextValidationFact fact;
    fact.code                  = diagnostic.code;
    fact.severity              = diagnostic.severity;
    fact.subsystem             = diagnostic.subsystem;
    fact.message               = diagnostic.message;
    fact.remediation           = diagnostic.remediation;
    fact.sourceBytes           = diagnostic.sourceByteRange;
    fact.graphemes             = graphemes;
    fact.recoverableAtRuntime  = true;
    fact.blocksAuthoredPackage = BlocksAuthoredPackage(diagnostic.code);
    return fact;
}

// ── Hard line separators ────────────────────────────────────────────────────
// UAX#14의 강제 줄바꿈 문자들. ICU의 규칙 상태를 캐지 않고 grapheme 내용으로
// 직접 판정한다: LineBreakBoundaries()는 강제 기회와 임의 기회를 구분하지
// 않는데, 그 구분이 "구분자 grapheme 자체는 어느 표시 줄에도 속하지 않는다"는
// Step 12c 규칙의 유일한 근거다.
//
// 이 목록은 Bidi_Class B(U+000A, U+000D, U+001C..U+001E, U+0085, U+2029)를
// 전부 포함해야 한다. 표시 문단이 UBA 문단을 쪼개는 것은 괜찮지만
// (U+2028이 정확히 그런 문자다) 걸쳐 있으면 안 된다: 걸치면
// ubidi_setLine이 문단 경계를 넘었다고 거절한다.
//
// 보이지 않는 문자는 \u 이스케이프로 적는다. 원문 그대로 두면 편집기·diff·
// 정규화 도구가 지울 수 있고, U+2028이 조용히 사라지면 "문단을 나누지 않는
// 문단 구분자"가 된다.
bool IsHardSeparatorScalar(char32_t value) noexcept {
    return value == U'\n' || value == U'\v' || value == U'\f' ||
           value == U'\r' || value == U'\u001C' || value == U'\u001D' ||
           value == U'\u001E' || value == U'\u0085' ||
           value == U'\u2028' || value == U'\u2029';
}

// ── Analysis item clipping ──────────────────────────────────────────────────
// 잘라낸 item은 반드시 grapheme 경계에서 시작하고 끝난다. TextShapingService가
// 그 성질을 스스로 검사하고 어긋나면 LayoutInvalid로 거절하므로, 여기서 어긋난
// 값을 만들면 실패는 조용하지 않다.
std::optional<AnalysisItem> ClipItem(const AnalysisItem& item,
                                     const UnicodeTextBuffer& buffer,
                                     const std::vector<std::uint32_t>& boundaries,
                                     std::uint32_t graphemeBegin,
                                     std::uint32_t graphemeEnd) {
    AnalysisItem clipped = item;
    clipped.graphemes    = GraphemeRange{graphemeBegin, graphemeEnd};
    clipped.sourceBytes =
        SourceByteRange{boundaries[graphemeBegin], boundaries[graphemeEnd]};
    clipped.paragraphStart = item.paragraphStart &&
                             clipped.sourceBytes.begin == item.sourceBytes.begin;
    clipped.paragraphEnd =
        item.paragraphEnd && clipped.sourceBytes.end == item.sourceBytes.end;
    // 실패하면 typed 실패로 올린다(UnicodeTextBuffer.h의 호출자 의무). 여기서
    // 조용히 넘기면 잘라낸 item이 자기 것보다 넓은 부모의 UTF-16 구간을 그대로
    // 달고 내려가고, 그 값을 읽는 쪽은 한 줄짜리 구간이라고 믿는다.
    const auto units = buffer.Utf16ForSourceBytes(clipped.sourceBytes);
    if (!units) return std::nullopt;
    clipped.utf16Units = *units;
    return clipped;
}

// 원본 byte 구간이 닿는 grapheme 구간. 경계표는 오름차순이라 이분 탐색이다.
GraphemeRange GraphemesForBytes(const std::vector<std::uint32_t>& boundaries,
                                SourceByteRange bytes) {
    const auto firstAfterBegin =
        std::upper_bound(boundaries.begin(), boundaries.end(), bytes.begin);
    const auto begin =
        static_cast<std::uint32_t>((firstAfterBegin - boundaries.begin()) - 1);
    if (bytes.end <= bytes.begin) return GraphemeRange{begin, begin};
    const auto firstAfterLast =
        std::upper_bound(boundaries.begin(), boundaries.end(), bytes.end - 1U);
    const auto last =
        static_cast<std::uint32_t>((firstAfterLast - boundaries.begin()) - 1);
    return GraphemeRange{begin, last + 1U};
}

// ── Shaped pieces ───────────────────────────────────────────────────────────
// 하나의 셰이핑 호출이 낸 glyph 묶음과, 그 호출을 정체성으로 되짚는 데 필요한
// 전부. 최종 캐시 키가 이 값들에서 만들어지므로 셰이핑 시점에 채운다.
struct LinePiece {
    SourceByteRange                     bytes;
    std::uint8_t                        level        = 0;
    std::uint32_t                       logicalRunId = 0;
    std::int32_t                        scriptCode   = 0;
    ShapeBoundaryFlags                  boundaries;
    std::string                         shapeInputUtf8;
    std::vector<ShapeInputSourceSpan>   shapeInputMapping;
    std::vector<ShapedGlyph>            glyphs;
    // 이 piece를 낸 셰이핑이 실제로 읽은 분석의 정체성이다. 문단 분석과 같지
    // 않을 수 있다: 줄임표 후보는 자기만의 임시 분석으로 셰이핑되고, 그
    // analysisGeneration은 프로세스 안에서 재사용되지 않는 다른 값이다.
    UnicodeAnalysisIdentity             analysisIdentity;
};

struct ShapedLine {
    std::vector<LinePiece> pieces;
    Fixed26_6              advance = Fixed26_6::FromRaw(0);
    // 이 셰이핑 호출이 실제로 건네받은 byte 구간. ShapeRange 안에서만 채워지고
    // 다른 어떤 경로도 이 값을 쓰지 않는다 — 그래서 "확정된 줄마다 최종
    // 셰이핑이 한 번 있었다"가 확정된 줄의 범위를 베낀 항등식이 아니게 된다.
    SourceByteRange        handledBytes;
    bool                   shaped = false;
};

// 확정된 논리 줄 하나. glyph 배열은 언제나 이 줄을 다시 셰이핑한 결과다.
struct LineDraft {
    SourceByteRange        bytes;
    GraphemeRange          graphemes;
    SourceByteRange        paragraphBytes;
    SourceByteRange        shapedRange;
    ShapeBoundaryFlags     boundaries;
    std::vector<VisualRun> visualRuns;
    std::vector<LinePiece> pieces;
    Fixed26_6              advance  = Fixed26_6::FromRaw(0);
    Fixed26_6              ascent   = Fixed26_6::FromRaw(0);
    Fixed26_6              descent  = Fixed26_6::FromRaw(0);
    Fixed26_6              lineGap  = Fixed26_6::FromRaw(0);
    Fixed26_6              baseline = Fixed26_6::FromRaw(0);
    Fixed26_6              top      = Fixed26_6::FromRaw(0);
    Fixed26_6              bottom   = Fixed26_6::FromRaw(0);
    bool                   overWidth = false;
};

UBiDiLevel ParagraphLevelFor(BaseDirection direction) noexcept {
    switch (direction) {
        case BaseDirection::LeftToRight: return 0;
        case BaseDirection::RightToLeft: return 1;
        case BaseDirection::Auto:        break;
    }
    return UBIDI_DEFAULT_LTR;
}

// 모든 종료 경로에서 닫는다. ubidi_setLine이 실패해도 두 객체는 남으므로
// 조기 반환마다 손으로 닫으면 한 갈래가 반드시 새어 나간다.
class ScopedUBiDi {
public:
    ScopedUBiDi() {
        CountIcuObject();
        handle_ = ubidi_open();
    }
    ~ScopedUBiDi() {
        if (handle_ != nullptr) ubidi_close(handle_);
    }
    ScopedUBiDi(const ScopedUBiDi&)            = delete;
    ScopedUBiDi& operator=(const ScopedUBiDi&) = delete;
    UBiDi* Get() const noexcept { return handle_; }

private:
    UBiDi* handle_ = nullptr;
};

// ── The paragraph layout run ────────────────────────────────────────────────
class LayoutRun {
public:
    LayoutRun(FontFamilyResolver& resolver, TextShapingService& shaper,
              TextLayoutCache& cache, const TextLayoutRequest& request,
              TextDiagnosticSink& sink)
        : resolver_(resolver),
          shaper_(shaper),
          cache_(cache),
          request_(request),
          sink_(sink) {}

    std::optional<std::shared_ptr<const TextLayout>> Run();

private:
    bool ResolveFamilyClosure();
    TextLayoutRequestIndexKey BuildRequestKey() const;
    bool BuildAnalysis();
    bool MeasureParagraphs();
    bool BuildLines();
    bool BuildParagraphLines(GraphemeRange paragraph);
    std::vector<std::uint32_t> LegalCandidates(GraphemeRange paragraph);
    void BuildBreakTables();
    bool IsSafeBoundary(std::uint32_t grapheme) const;
    Fixed26_6 EstimateAdvance(std::uint32_t graphemeBegin,
                              std::uint32_t graphemeEnd) const;
    bool ShapeRange(const UnicodeTextBuffer& buffer,
                    const UnicodeAnalysis& analysis, SourceByteRange bytes,
                    GraphemeRange graphemes, bool beginningOfText,
                    bool endOfText, bool countAsFinalLine, ShapedLine& out);
    UBiDi* EnsureDocumentBidi();
    bool OrderVisually(const UnicodeTextBuffer& buffer, UBiDi* paragraphBidi,
                       SourceByteRange lineBytes,
                       const std::vector<LinePiece>& pieces,
                       std::vector<VisualRun>& out);
    bool ApplyTruncationAndOverflow();
    bool EllipsizeLine(LineDraft& line);
    bool ComputeVerticalFields();
    bool LineMetrics(const LineDraft& line, Fixed26_6& ascent,
                     Fixed26_6& descent, Fixed26_6& lineGap) const;
    bool ProceduralMetrics(Fixed26_6& ascent, Fixed26_6& descent,
                           Fixed26_6& lineGap) const;
    bool PositionLines(TextLayout& layout);
    bool BuildInteriorCarets(PositionedGlyph& glyph, bool rightToLeft) const;
    bool BuildCaretStops(const TextLine& line, std::uint32_t lineIndex,
                         Fixed26_6 lineOrigin,
                         std::vector<CaretStop>& out) const;
    TextShapeCacheKey BuildShapeKey(const LineDraft& line,
                                    const LinePiece& piece) const;
    TextParagraphCacheKey BuildFinalKey() const;
    void ReportFacts(const std::vector<TextValidationFact>& facts);
    void ReportFactSequence(const std::vector<TextValidationFact>& first,
                            const std::vector<TextValidationFact>* second);
    void ReportWarmFacts(const std::vector<TextValidationFact>& stored);
    void Fail(std::string message, SourceByteRange range) const;

    const std::vector<std::uint32_t>& Boundaries() const {
        return analysis_->GraphemeBoundaries();
    }
    Fixed26_6 FontSize() const noexcept { return request_.style.shape.fontSize; }

    FontFamilyResolver&      resolver_;
    TextShapingService&      shaper_;
    TextLayoutCache&         cache_;
    const TextLayoutRequest& request_;
    TextDiagnosticSink&      sink_;

    // 이 객체가 살아 있는 동안은 Shutdown이 진행하지 않으므로, 아래의 UBiDi들과
    // 이 가동이 부르는 분석/셰이핑은 u_cleanup 이후를 볼 수 없다. 가장 먼저
    // 선언하면 마지막에 파괴되므로, ScopedUBiDi가 ubidi_close를 부를 때까지도
    // lease가 살아 있다(UnicodeAnalysis/TextShapingService와 같은 규약).
    std::optional<TextRuntimeClientHandle> lease_;

    ResolvedFamily                   family_;
    FactSet                          resolveFacts_;
    FactSet                          facts_;
    std::optional<UnicodeTextBuffer> buffer_;
    std::optional<UnicodeAnalysis>   analysis_;
    // 문서 전체를 단 한 번 ubidi_setPara한 객체. 줄마다 문단 pass를 다시
    // 돌리지 않는 이유는 비용보다 정확성이다: 표시 문단은 UBA 문단을 쪼갬 수
    // 있고(U+2028), 그 조각에 대해 setPara를 다시 돌리면 문단 level을 잘린
    // 조각에서 다시 유도하게 된다. ICU는 다중 문단 텍스트를 스스로 나누므로,
    // 문서 전체를 넘기면 UBA가 정한 그대로의 문단 level을 쓴다.
    //
    // line UBiDi는 부모보다 먼저 파괴되어야 하므로(ubidi_setLine 계약) 언제나
    // OrderVisually 안의 지역 변수다.
    std::optional<ScopedUBiDi>       documentBidi_;
    std::vector<GraphemeRange>       paragraphs_;
    // 문단 측정용 셰이핑 결과. 후보 자리를 재고 unsafe 경계를 걸러내는 데만
    // 쓰이며 확정된 줄의 glyph로는 절대 흘러가지 않는다.
    std::vector<ShapedGlyph>         measured_;
    // 아래 두 표는 측정이 끝난 뒤 한 번만 만든다. 후보마다 measured_를 다시
    // 훑으면 문단 길이의 세제곱이 된다: 후보 수 x 후보당 탐색 x 줄 수.
    std::vector<std::uint8_t>        unsafeBoundary_;
    std::vector<std::int64_t>        cumulativeAdvance_;
    std::vector<LineDraft>           lines_;
    bool                             clipped_    = false;
    bool                             ellipsized_ = false;
    bool                             truncated_  = false;
};

void LayoutRun::Fail(std::string message, SourceByteRange range) const {
    ReportTerminal(sink_, TextDiagnosticCode::LayoutInvalid, std::move(message),
                   "This is an internal layout invariant, not authored "
                   "content. Report the text, the constraints and the font "
                   "family that produced it.",
                   request_.diagnosticContext, range);
}

// ── Step 4: the pre-analysis family closure ─────────────────────────────────
bool LayoutRun::ResolveFamilyClosure() {
    VectorTextDiagnosticSink resolveSink;
    auto resolved = resolver_.BuildCandidates(
        request_.style.fontFamilyGuid, request_.style.fontRequest, resolveSink);
    if (!resolved) {
        // nullopt는 "family가 없다"가 아니라 "FontArtifactStore가 묶이지
        // 않았다"는 뜻이다(Task 5.1의 계약). 절차적 두부로 넘어갈 수 있는
        // 상태가 아니므로 종결 실패다.
        ReportTerminal(sink_, TextDiagnosticCode::DependencyInvalid,
                       "font family resolution is unavailable; no verified "
                       "font artifact store is bound to this project",
                       "Bind a validated ProjectLibrary or packaged resource "
                       "artifact store before laying out text.",
                       request_.diagnosticContext, SourceByteRange{});
        return false;
    }
    family_ = std::move(*resolved);
    for (const TextDiagnostic& diagnostic : resolveSink.Diagnostics()) {
        // 해석은 분석보다 앞에 있으므로 grapheme 좌표가 아직 없다. warm 경로도
        // 같은 자리에서 같은 값을 내므로 두 경로의 사실이 정확히 겹친다.
        resolveFacts_.Append(FactFromDiagnostic(diagnostic,
                                                GraphemeRange{0, 0}));
    }
    return true;
}

// ── Step 4a: the early request-index key ────────────────────────────────────
TextLayoutRequestIndexKey LayoutRun::BuildRequestKey() const {
    TextLayoutRequestIndexKey key;
    key.originalUtf8      = request_.utf8;
    key.originalBytesHash = CacheBytesHash(key.originalUtf8);
    key.style             = request_.style;
    key.constraints       = request_.constraints;
    key.visualRevision    = request_.visualRevision;

    key.familyClosure.requestedGuid = family_.requestedGuid;
    for (const ResolvedFamilyNode& node : family_.depthFirstFamilyNodes) {
        FamilyNodeRequestIdentity identity;
        identity.familyGuid            = node.familyGuid;
        identity.exists                = node.exists;
        identity.contentGeneration     = node.contentGeneration;
        identity.authoredFallbackGuids = node.authoredFallbackGuids;
        key.familyClosure.depthFirstFamilyNodes.push_back(std::move(identity));
    }
    for (const ResolvedFace& face : family_.candidates) {
        FaceRequestIdentity identity;
        identity.fontGuid          = face.fontGuid;
        identity.fontRevision      = face.fontRevision;
        identity.faceIndex         = face.faceIndex;
        identity.authoredFaceIndex = face.authoredFaceIndex;
        if (face.resource != nullptr) {
            identity.sourceSha256      = face.resource->sourceSha256;
            identity.artifactSha256    = face.resource->artifactSha256;
            identity.artifactLocator   = face.resource->artifactLocator;
            identity.contentGeneration = face.resource->contentGeneration;
            if (face.resource->asset != nullptr) {
                identity.artifactByteSize =
                    face.resource->asset->artifactByteSize;
            }
        }
        key.familyClosure.orderedCandidates.push_back(std::move(identity));
    }

    key.harfbuzzRevision = hb_version_string();
    key.icuRevision      = U_ICU_VERSION;
    key.icuDataSha256    = kPackagedIcuDataSha256;
    // Task 7.1이 남긴 배선점. ICU revision은 unicode/uvernum.h에서만 나오고
    // 캐시 계층은 ICU 헤더를 담을 수 없으므로, 실제 계약 값을 넣는 일은 ICU
    // 헤더가 허용되는 이 계층의 몫이다.
    key.requestedGraphemeRulePolicyIdentity =
        RequestedGraphemeRulePolicyIdentity(key.icuRevision, key.icuDataSha256);
    key.requestedLineBreakRulePolicyIdentity =
        RequestedLineBreakRulePolicyIdentity(key.icuRevision, key.icuDataSha256);
    key.dependencyContractSha256 =
        TextRuntimeDependencies::Get().DependencyContractSha256();
    return key;
}

// ── Step 5: cold Unicode paragraph analysis ─────────────────────────────────
bool LayoutRun::BuildAnalysis() {
    VectorTextDiagnosticSink decodeSink;
    auto buffer = UnicodeTextBuffer::Build(request_.utf8, decodeSink);
    if (!buffer) {
        Fail("the authored text is too large to address with 32-bit source "
             "offsets",
             SourceByteRange{});
        return false;
    }
    buffer_ = std::move(buffer);

    VectorTextDiagnosticSink analysisSink;
    auto analysis = UnicodeTextAnalyzer::Analyze(*buffer_,
                                                 request_.style.analysis,
                                                 analysisSink);
    if (!analysis) {
        // 분석 실패의 사유는 분석기만 안다. 다시 쓰지 않고 호출자의 문맥만
        // 입혀 그대로 올려 보낸다.
        for (const TextDiagnostic& diagnostic : analysisSink.Diagnostics()) {
            TextDiagnostic forwarded = diagnostic;
            forwarded.assetGuid      = request_.diagnosticContext.assetGuid;
            forwarded.sceneObjectId  = request_.diagnosticContext.sceneObjectId;
            forwarded.componentType  = request_.diagnosticContext.componentType;
            sink_.Report(std::move(forwarded));
        }
        return false;
    }
    analysis_ = std::move(analysis);

    facts_.Assign(resolveFacts_);
    // 치환된 구간 하나가 불변 사실 하나가 된다. U+FFFD 개수로는 저작자가 직접
    // 쓴 문자와 우리가 치환한 문자를 구분할 수 없으므로(UnicodeTextBuffer.h),
    // 진단 range가 유일한 근거다.
    for (const TextDiagnostic& diagnostic : decodeSink.Diagnostics()) {
        facts_.Append(FactFromDiagnostic(
            diagnostic,
            GraphemesForBytes(Boundaries(), diagnostic.sourceByteRange)));
    }
    return true;
}

// ── Step 5b: shape each display paragraph once, to measure ──────────────────
bool LayoutRun::MeasureParagraphs() {
    const std::vector<std::uint32_t>& boundaries = Boundaries();
    const std::vector<DecodedScalar>& scalars    = buffer_->Scalars();
    const auto graphemeCount =
        static_cast<std::uint32_t>(boundaries.size() - 1U);

    // 표시 문단으로 나눈다. 구분자 grapheme 자체는 어느 문단에도 들어가지
    // 않는다(Step 12c). "A\n"은 {0,1}과 그 뒤의 빈 {2,2}가 된다.
    paragraphs_.clear();
    std::uint32_t graphemeStart = 0;
    // scalar 표는 byte 순서이므로 grapheme마다 0부터 훑지 않고 커서를 민다.
    std::size_t scalarCursor = 0;
    for (std::uint32_t grapheme = 0; grapheme < graphemeCount; ++grapheme) {
        const SourceByteRange bytes{boundaries[grapheme],
                                    boundaries[grapheme + 1U]};
        while (scalarCursor < scalars.size() &&
               scalars[scalarCursor].sourceBytes.begin < bytes.begin) {
            ++scalarCursor;
        }
        bool separator = bytes.end > bytes.begin;
        for (std::size_t index = scalarCursor;
             index < scalars.size() &&
             scalars[index].sourceBytes.begin < bytes.end;
             ++index) {
            if (!IsHardSeparatorScalar(scalars[index].value)) separator = false;
        }
        if (!separator) continue;
        paragraphs_.push_back(GraphemeRange{graphemeStart, grapheme});
        graphemeStart = grapheme + 1U;
    }
    paragraphs_.push_back(GraphemeRange{graphemeStart, graphemeCount});

    for (const GraphemeRange& paragraph : paragraphs_) {
        if (paragraph.end <= paragraph.begin) continue;
        const SourceByteRange bytes{boundaries[paragraph.begin],
                                    boundaries[paragraph.end]};
        ShapedLine shaped;
        if (!ShapeRange(*buffer_, *analysis_, bytes, paragraph, true, true,
                        false, shaped)) {
            return false;
        }
        ++g_observations.paragraphShapePasses;
        for (const LinePiece& piece : shaped.pieces) {
            for (const ShapedGlyph& glyph : piece.glyphs) {
                measured_.push_back(glyph);
            }
        }
    }

    BuildBreakTables();

    // 없는 glyph와 fallback을 grapheme 단위 분류로 접는다. 셰이퍼의 진단
    // 스트림은 item마다 상한이 있어 완전하지 않으므로, 권한 있는 기록인
    // ShapedGlyph 쪽에서 유도한다.
    enum class Coverage : std::uint8_t { Primary, Fallback, Missing };
    std::vector<Coverage> coverage(graphemeCount, Coverage::Primary);
    const bool        haveCandidate = !family_.candidates.empty();
    const std::string primaryGuid =
        haveCandidate ? family_.candidates.front().fontGuid : std::string();
    const std::uint32_t primaryFaceIndex =
        haveCandidate ? family_.candidates.front().faceIndex : 0U;
    for (const ShapedGlyph& glyph : measured_) {
        Coverage value = Coverage::Primary;
        if (glyph.missing) {
            value = Coverage::Missing;
        } else if (glyph.fontGuid != primaryGuid ||
                   glyph.faceIndex != primaryFaceIndex) {
            value = Coverage::Fallback;
        }
        if (value == Coverage::Primary) continue;
        for (std::uint32_t grapheme = glyph.graphemes.begin;
             grapheme < glyph.graphemes.end && grapheme < graphemeCount;
             ++grapheme) {
            coverage[grapheme] = value;
        }
    }
    for (std::uint32_t grapheme = 0; grapheme < graphemeCount;) {
        if (coverage[grapheme] == Coverage::Primary) {
            ++grapheme;
            continue;
        }
        const Coverage value = coverage[grapheme];
        std::uint32_t  end   = grapheme;
        while (end < graphemeCount && coverage[end] == value) ++end;
        TextValidationFact fact;
        fact.subsystem            = kSubsystem;
        fact.sourceBytes          = SourceByteRange{boundaries[grapheme],
                                           boundaries[end]};
        fact.graphemes            = GraphemeRange{grapheme, end};
        fact.recoverableAtRuntime = true;
        if (value == Coverage::Missing) {
            fact.code     = TextDiagnosticCode::MissingGlyph;
            fact.severity = TextSeverity::Error;
            fact.message  = "no font in the resolved family can render this "
                            "text";
            fact.remediation = "Add a fallback family whose fonts cover this "
                               "script, or author the text with characters the "
                               "family covers.";
            fact.blocksAuthoredPackage = true;
        } else {
            // 성공한 fallback은 결함이 아니라 기록이다. severity가 Info이고
            // blocksAuthoredPackage가 false인 유일한 사실이며, 그래서 그 두
            // 필드가 상수가 아니라는 증인이기도 하다. 코드는
            // FontFamilyInvalid를 쓴다: 닫힌 enum에 fallback 전용 코드가 없고
            // (값 추가는 설계 개정 사안이다), 이 사실이 말하는 대상은 저작된
            // family의 coverage이기 때문이다.
            fact.code        = TextDiagnosticCode::FontFamilyInvalid;
            fact.severity    = TextSeverity::Info;
            fact.message     = "the primary authored face does not cover this "
                               "text; a fallback face was selected";
            fact.remediation = "No action is required. Author this script into "
                               "the primary family if the fallback face is not "
                               "intended.";
            fact.blocksAuthoredPackage = false;
        }
        facts_.Append(std::move(fact));
        grapheme = end;
    }
    return true;
}

// ── Step 7: shape one exact range as its own final line ─────────────────────
bool LayoutRun::ShapeRange(const UnicodeTextBuffer& buffer,
                           const UnicodeAnalysis& analysis,
                           SourceByteRange bytes, GraphemeRange graphemes,
                           bool beginningOfText, bool endOfText,
                           bool countAsFinalLine, ShapedLine& out) {
    out.pieces.clear();
    out.advance      = Fixed26_6::FromRaw(0);
    out.handledBytes = SourceByteRange{};
    out.shaped       = false;
    if (graphemes.end <= graphemes.begin) return true;
    // 이 두 줄이 acceptedLineShapes의 유일한 출처다. 확정된 줄의 범위를
    // 베끼지 않고 셰이핑이 진짜로 건네받은 것을 적어야, 문단 배열을 잘라
    // 쓰는 구현이 같은 관찰을 낼 수 없다.
    out.handledBytes = bytes;
    out.shaped       = true;

    const std::vector<std::uint32_t>& boundaries = analysis.GraphemeBoundaries();
    for (const AnalysisItem& item : analysis.Items()) {
        const std::uint32_t begin =
            std::max(item.graphemes.begin, graphemes.begin);
        const std::uint32_t end = std::min(item.graphemes.end, graphemes.end);
        if (end <= begin) continue;
        const std::optional<AnalysisItem> clippedItem =
            ClipItem(item, buffer, boundaries, begin, end);
        if (!clippedItem) {
            Fail("a line range does not map to whole UTF-16 units", bytes);
            return false;
        }
        const AnalysisItem& clipped = *clippedItem;
        const bool first = clipped.sourceBytes.begin == bytes.begin;
        const bool last  = clipped.sourceBytes.end == bytes.end;
        const ShapeBoundaryFlags flags{beginningOfText && first,
                                       endOfText && last};

        VectorTextDiagnosticSink shapeSink;
        auto shaped =
            shaper_.ShapeAnalysisItem(buffer, analysis, clipped, family_,
                                      request_.style.shape, flags, shapeSink);
        if (countAsFinalLine) ++g_observations.finalLineItemShapeCalls;
        if (!shaped) {
            // 셰이퍼의 종결 진단만 그대로 올려 보낸다. MissingGlyph는 상한이
            // 있는 스트림 쪽 사본이고, 권한 있는 기록은 이미 사실로 옮겼다.
            for (const TextDiagnostic& diagnostic : shapeSink.Diagnostics()) {
                if (diagnostic.code == TextDiagnosticCode::MissingGlyph) continue;
                TextDiagnostic forwarded = diagnostic;
                forwarded.assetGuid     = request_.diagnosticContext.assetGuid;
                forwarded.sceneObjectId = request_.diagnosticContext.sceneObjectId;
                forwarded.componentType = request_.diagnosticContext.componentType;
                sink_.Report(std::move(forwarded));
            }
            return false;
        }
        for (ShapedRun& run : *shaped) {
            if (run.glyphs.empty()) continue;
            LinePiece piece;
            piece.level        = run.bidiLevel;
            piece.logicalRunId = run.glyphs.front().logicalRunId;
            piece.scriptCode   = clipped.scriptCode;
            piece.boundaries   = flags;
            std::uint32_t low  = run.glyphs.front().sourceBytes.begin;
            std::uint32_t high = run.glyphs.front().sourceBytes.end;
            for (const ShapedGlyph& glyph : run.glyphs) {
                low  = std::min(low, glyph.sourceBytes.begin);
                high = std::max(high, glyph.sourceBytes.end);
                if (!AddChecked(out.advance, glyph.advanceX, out.advance)) {
                    Fail("a line advance overflows the checked 26.6 range",
                         bytes);
                    return false;
                }
            }
            piece.bytes = SourceByteRange{low, high};
            piece.shapeInputUtf8 =
                buffer.OriginalUtf8().substr(low, high - low);
            ShapeInputSourceSpan span;
            span.shapeInputBytes     = SourceByteRange{0, high - low};
            span.originalSourceBytes = piece.bytes;
            span.originalGraphemes =
                GraphemesForBytes(boundaries, piece.bytes);
            span.synthetic = false;
            piece.shapeInputMapping.push_back(span);
            // Step 5a: 이 piece를 낸 셰이핑이 실제로 읽은 분석의 정체성.
            // 문단 분석을 무조건 쓰면 줄임표 후보의 piece가 자기를 만들지
            // 않은 분석의 generation을 주장하게 된다.
            piece.analysisIdentity = analysis.Identity();
            piece.glyphs = std::move(run.glyphs);
            out.pieces.push_back(std::move(piece));
        }
    }
    return true;
}

// ── Step 6: legal break candidates ──────────────────────────────────────────
// 측정 결과를 grapheme 경계마다 한 번에 접어 둔다. 두 표는 문단 셰이핑 직후에만
// 만들어지고 그 뒤로는 읽기 전용이다.
void LayoutRun::BuildBreakTables() {
    const std::vector<std::uint32_t>& boundaries = Boundaries();
    const auto boundaryCount = static_cast<std::uint32_t>(boundaries.size());
    unsafeBoundary_.assign(boundaryCount, 0U);
    cumulativeAdvance_.assign(boundaryCount, 0);
    for (const ShapedGlyph& glyph : measured_) {
        // 경계가 glyph 안쪽이면 그 자리는 cluster 경계가 아니다. 합자가 삼킨
        // grapheme 경계가 정확히 이 모양이다.
        for (std::uint32_t grapheme = glyph.graphemes.begin + 1U;
             grapheme < glyph.graphemes.end && grapheme < boundaryCount;
             ++grapheme) {
            unsafeBoundary_[grapheme] = 1U;
        }
        // HarfBuzz가 "이 cluster 앞에서 자르면 양쪽을 다시 셰이핑해야 한다"고
        // 표시한 자리. 힌트가 아니라 금지다(TextShapingService.h).
        if ((glyph.harfbuzzGlyphFlags &
             static_cast<std::uint32_t>(HB_GLYPH_FLAG_UNSAFE_TO_BREAK)) != 0U &&
            glyph.graphemes.begin < boundaryCount) {
            unsafeBoundary_[glyph.graphemes.begin] = 1U;
        }
        // advance는 glyph이 끝나는 경계에 얹는다. 합법 후보는 어떤 glyph도
        // 가로지르지 않으므로, 두 합법 경계 사이의 누계 차는 그 구간에 온전히
        // 들어 있는 glyph들의 합과 정확히 같다.
        if (glyph.graphemes.end < boundaryCount) {
            cumulativeAdvance_[glyph.graphemes.end] += glyph.advanceX.Raw();
        }
    }
    for (std::uint32_t index = 1U; index < boundaryCount; ++index) {
        cumulativeAdvance_[index] += cumulativeAdvance_[index - 1U];
    }
}

bool LayoutRun::IsSafeBoundary(std::uint32_t grapheme) const {
    if (grapheme >= unsafeBoundary_.size()) return true;
    return unsafeBoundary_[grapheme] == 0U;
}

std::vector<std::uint32_t> LayoutRun::LegalCandidates(GraphemeRange paragraph) {
    const std::vector<std::uint32_t>& boundaries = Boundaries();
    const std::vector<std::uint32_t>& lineBreaks =
        analysis_->LineBreakBoundaries();
    std::vector<std::uint32_t> candidates;
    candidates.push_back(paragraph.begin);
    for (std::uint32_t grapheme = paragraph.begin + 1U; grapheme < paragraph.end;
         ++grapheme) {
        bool legal = false;
        switch (request_.style.wrap) {
            case TextWrapMode::NoWrap:
                legal = false;
                break;
            case TextWrapMode::Word:
                legal = std::binary_search(lineBreaks.begin(), lineBreaks.end(),
                                           boundaries[grapheme]);
                break;
            case TextWrapMode::Grapheme:
                legal = true;
                break;
        }
        if (!legal) continue;
        if (!IsSafeBoundary(grapheme)) {
            RecordBounded(g_observations.discardedUnsafeBoundaries, grapheme);
            continue;
        }
        candidates.push_back(grapheme);
    }
    if (paragraph.end > paragraph.begin) candidates.push_back(paragraph.end);
    return candidates;
}

Fixed26_6 LayoutRun::EstimateAdvance(std::uint32_t graphemeBegin,
                                     std::uint32_t graphemeEnd) const {
    const std::int64_t total = cumulativeAdvance_[graphemeEnd] -
                               cumulativeAdvance_[graphemeBegin];
    // 추정치일 뿐이다. 권한 있는 값은 언제나 이 줄을 다시 셰이핑한 결과이며,
    // Step 8이 그 값으로 자리를 정한다. 넘치면 "무한히 넓다"로 보아 되짚기에
    // 맡긴다.
    if (total > 0x7FFFFFFF) return Fixed26_6::FromRaw(0x7FFFFFFF);
    if (total < -0x7FFFFFFF) return Fixed26_6::FromRaw(-0x7FFFFFFF);
    return Fixed26_6::FromRaw(static_cast<std::int32_t>(total));
}

// ── Steps 7-8 and 12c: one paragraph's accepted lines ───────────────────────
bool LayoutRun::BuildParagraphLines(GraphemeRange paragraph) {
    const std::vector<std::uint32_t>& boundaries = Boundaries();
    const SourceByteRange paragraphBytes{boundaries[paragraph.begin],
                                         boundaries[paragraph.end]};

    if (paragraph.end <= paragraph.begin) {
        // Step 12c: 빈 문단은 metric을 갖는 {n,n} 줄 하나가 된다. grapheme
        // 경계를 지어내지 않으므로 범위는 길이 0이다.
        LineDraft line;
        line.graphemes      = paragraph;
        line.bytes          = paragraphBytes;
        line.paragraphBytes = paragraphBytes;
        // 길이 0의 줄에는 셰이핑할 것이 없다. 관찰이 비어 있는 것과 구분되도록
        // 자기 자리의 길이 0 구간을 남긴다 — 이 갈래는 ShapeRange를 부르지
        // 않으므로 handledBytes를 가질 수 없다.
        line.shapedRange    = paragraphBytes;
        line.boundaries     = ShapeBoundaryFlags{true, true};
        lines_.push_back(std::move(line));
        return true;
    }

    const std::vector<std::uint32_t> candidates = LegalCandidates(paragraph);
    std::size_t                      current    = 0;
    while (current + 1U < candidates.size()) {
        const std::uint32_t start = candidates[current];
        std::size_t proposal = candidates.size() - 1U;
        if (request_.constraints.width) {
            const std::int32_t limit = request_.constraints.width->Raw();
            for (std::size_t index = current + 1U; index < candidates.size();
                 ++index) {
                if (EstimateAdvance(start, candidates[index]).Raw() > limit) {
                    proposal = index - 1U;
                    break;
                }
            }
            // 문단 측정은 조언일 뿐이므로 줄의 끝을 정하게 두지 않는다. 측정이
            // 허락하는 것보다 한 후보 더 나아간 자리를 제안하고, 아래 Step 8의
            // 되짚기가 권한 있는 값으로 자리를 정한다. 그래서 측정이 지나치게
            // 낙관적이면 되짚기가 끝까지 되돌리고, 지나치게 비관적이면 한
            // 후보만큼은 되찾는다 — 어느 쪽이든 측정 오차가 배치를 틀리게
            // 만들지 못한다.
            if (proposal + 1U < candidates.size()) ++proposal;
            if (proposal <= current) proposal = current + 1U;
        }

        ShapedLine shaped;
        for (;;) {
            const std::uint32_t end = candidates[proposal];
            const SourceByteRange bytes{boundaries[start], boundaries[end]};
            if (!ShapeRange(*buffer_, *analysis_, bytes,
                            GraphemeRange{start, end},
                            start == paragraph.begin, end == paragraph.end, true,
                            shaped)) {
                return false;
            }
            if (!request_.constraints.width) break;
            if (shaped.advance.Raw() <= request_.constraints.width->Raw()) break;
            // 앞선 합법 후보가 남아 있지 않으면 되짚을 곳이 없다. 그때는
            // 길이를 지어내지 않고 Step 9의 과장 정책에 넘긴다.
            if (proposal <= current + 1U) break;
            --proposal;
            ++g_observations.backtrackSteps;
        }

        const std::uint32_t end = candidates[proposal];
        LineDraft line;
        line.graphemes      = GraphemeRange{start, end};
        line.bytes          = SourceByteRange{boundaries[start], boundaries[end]};
        line.paragraphBytes = paragraphBytes;
        // 확정된 줄의 범위가 아니라 최종 셰이핑이 실제로 건네받은 구간이다.
        line.shapedRange    = shaped.handledBytes;
        line.boundaries =
            ShapeBoundaryFlags{start == paragraph.begin, end == paragraph.end};
        line.advance   = shaped.advance;
        line.overWidth = request_.constraints.width &&
                         shaped.advance.Raw() >
                             request_.constraints.width->Raw();
        line.pieces = std::move(shaped.pieces);
        UBiDi* documentBidi = EnsureDocumentBidi();
        if (documentBidi == nullptr) return false;
        if (!OrderVisually(*buffer_, documentBidi, line.bytes, line.pieces,
                           line.visualRuns)) {
            return false;
        }
        lines_.push_back(std::move(line));
        current = proposal;
    }
    return true;
}

bool LayoutRun::BuildLines() {
    for (const GraphemeRange& paragraph : paragraphs_) {
        if (!BuildParagraphLines(paragraph)) return false;
    }
    if (lines_.empty()) {
        Fail("paragraph layout produced no logical line", SourceByteRange{});
        return false;
    }
    return true;
}

// ── Step 11: line BiDi visual order ─────────────────────────────────────────
// 문서 전체를 한 번만 setPara한다. ICU는 Bidi_Class B에서 스스로 문단을
// 나누므로, 이것이 UBA가 정한 문단 level을 그대로 쓰는 유일한 방법이다 —
// 표시 문단 조각마다 setPara를 다시 돌리면 U+2028처럼 UBA 문단을 나누지
// 않는 줄바꿈에서 문단 방향이 재유도된다. 줄마다 문단 pass를 돌리지 않으니
// 긴 문단의 배치가 줄 수 x 문단 길이가 되지도 않는다.
UBiDi* LayoutRun::EnsureDocumentBidi() {
    if (documentBidi_) return documentBidi_->Get();
    documentBidi_.emplace();
    if (documentBidi_->Get() == nullptr) {
        Fail("ICU refused to allocate a BiDi object for the paragraph",
             SourceByteRange{});
        return nullptr;
    }
    UErrorCode status = U_ZERO_ERROR;
    ubidi_setPara(documentBidi_->Get(), buffer_->SanitizedUtf16().data(),
                  static_cast<std::int32_t>(buffer_->SanitizedUtf16().size()),
                  ParagraphLevelFor(request_.style.analysis.baseDirection),
                  nullptr, &status);
    if (U_FAILURE(status)) {
        Fail("ICU refused the paragraph BiDi pass", SourceByteRange{});
        documentBidi_.reset();
        return nullptr;
    }
    return documentBidi_->Get();
}

bool LayoutRun::OrderVisually(const UnicodeTextBuffer& buffer,
                              UBiDi* paragraphBidi, SourceByteRange lineBytes,
                              const std::vector<LinePiece>& pieces,
                              std::vector<VisualRun>& out) {
    out.clear();
    if (pieces.empty()) return true;

    const auto lineUnits = buffer.Utf16ForSourceBytes(lineBytes);
    if (!lineUnits) {
        Fail("a line range does not map to whole UTF-16 units", lineBytes);
        return false;
    }

    // 부모보다 먼저 파괴되어야 하므로(ubidi_setLine 계약) 언제나 지역 변수다.
    ScopedUBiDi line;
    if (line.Get() == nullptr) {
        Fail("ICU refused to allocate a BiDi object for a line", lineBytes);
        return false;
    }
    UErrorCode status = U_ZERO_ERROR;
    // 줄이 UBA 문단 경계를 넘으면 ICU가 U_ILLEGAL_ARGUMENT_ERROR로 거절한다.
    // 표시 구분자 집합이 Bidi_Class B를 전부 포함하므로 실제로는 일어나지
    // 않지만, 그 불변식이 깨지면 조용한 오배치가 아니라 진단이 된다.
    ubidi_setLine(paragraphBidi, static_cast<std::int32_t>(lineUnits->begin),
                  static_cast<std::int32_t>(lineUnits->end), line.Get(),
                  &status);
    if (U_FAILURE(status)) {
        Fail("ICU refused the line BiDi pass", lineBytes);
        return false;
    }
    const std::int32_t runCount = ubidi_countRuns(line.Get(), &status);
    if (U_FAILURE(status) || runCount <= 0) {
        Fail("ICU produced no BiDi run for a non-empty line", lineBytes);
        return false;
    }

    std::vector<bool> used(pieces.size(), false);
    for (std::int32_t visual = 0; visual < runCount; ++visual) {
        std::int32_t logicalStart = 0;
        std::int32_t length       = 0;
        const UBiDiDirection direction =
            ubidi_getVisualRun(line.Get(), visual, &logicalStart, &length);
        const Utf16Range runUnits{
            lineUnits->begin + static_cast<std::uint32_t>(logicalStart),
            lineUnits->begin + static_cast<std::uint32_t>(logicalStart + length)};
        const auto runBytes = buffer.SourceBytesForUtf16(runUnits);
        if (!runBytes) {
            Fail("an ICU BiDi run does not map to whole UTF-16 units", lineBytes);
            return false;
        }
        // piece는 자기 첫 byte가 들어 있는 run에 속한다. 포함 관계로 고르지
        // 않는 이유는 L1 때문이다: 줄 끝의 공백은 문단 level로 되돌아가므로,
        // 그 공백을 품은 piece가 두 run에 걸칠 수 있다. 그 경우 보이지 않는
        // 공백 하나의 시각 위치만 근사가 된다.
        std::vector<std::size_t> selected;
        for (std::size_t index = 0; index < pieces.size(); ++index) {
            if (used[index]) continue;
            if (pieces[index].bytes.begin < runBytes->begin) continue;
            if (pieces[index].bytes.begin >= runBytes->end) continue;
            selected.push_back(index);
        }
        if (direction == UBIDI_RTL) {
            std::reverse(selected.begin(), selected.end());
        }
        for (const std::size_t index : selected) {
            used[index] = true;
            VisualRun run;
            run.logicalRunId = pieces[index].logicalRunId;
            run.bidiLevel    = pieces[index].level;
            run.glyphs.reserve(pieces[index].glyphs.size());
            for (const ShapedGlyph& glyph : pieces[index].glyphs) {
                PositionedGlyph positioned;
                positioned.glyph = glyph;
                run.glyphs.push_back(std::move(positioned));
            }
            out.push_back(std::move(run));
        }
    }
    // 방어적 불변식이다. 위의 run 선택이 옳은 한 도달할 수 없고, 어떤 픽스처도
    // 여기에 닿지 않는다 — 그래서 값은 "지금 무언가를 잡는다"가 아니라 "선택
    // 규칙이 언젠가 틀어졌을 때 glyph가 조용히 사라지는 대신 typed 실패가
    // 된다"이다.
    for (std::size_t index = 0; index < used.size(); ++index) {
        if (used[index]) continue;
        Fail("a shaped piece was not covered by any ICU BiDi run", lineBytes);
        return false;
    }
    return true;
}

// ── Steps 9-10c: truncation, clipping and the ellipsis candidate ────────────
bool LayoutRun::ApplyTruncationAndOverflow() {
    if (request_.style.maxLines != 0U &&
        lines_.size() > request_.style.maxLines) {
        lines_.resize(request_.style.maxLines);
        truncated_ = true;
    }
    if (request_.constraints.height) {
        const std::int32_t limit = request_.constraints.height->Raw();
        // 첫 줄은 세로 제약을 넘더라도 세지 않고 남긴다. 0줄짜리 배치는 caret이
        // 설 자리조차 없고, 그 뒤의 세로 계산은 빈 목록의 양 끝을 읽는다.
        // 사후에 0을 1로 고치는 대신 1에서 시작하는 이유가 그것이다: 그런
        // 보정은 실제로는 도달할 수 없는 자리에 놓이기 쉽고, 그러면 규칙이
        // 있다는 주장만 남는다.
        std::size_t keep = 1;
        while (keep < lines_.size() && lines_[keep].bottom.Raw() <= limit) {
            ++keep;
        }
        if (keep < lines_.size()) {
            lines_.resize(keep);
            truncated_ = true;
        }
    }

    bool overWidth = false;
    for (const LineDraft& line : lines_) {
        if (line.overWidth) overWidth = true;
    }

    switch (request_.style.overflow) {
        case TextOverflowMode::Overflow:
            // 과장된 줄을 그대로 남긴다. 잘라 낸 내용이 있을 때만 clipped다.
            clipped_ = truncated_;
            break;
        case TextOverflowMode::Clip:
            // 논리 glyph는 그대로 두고 표시만 잘렸다고 적는다.
            clipped_ = truncated_ || overWidth;
            break;
        case TextOverflowMode::Ellipsis:
            if (truncated_ || overWidth) {
                if (!EllipsizeLine(lines_.back())) return false;
                ellipsized_ = true;
            }
            break;
    }
    return true;
}

bool LayoutRun::EllipsizeLine(LineDraft& line) {
    const std::vector<std::uint32_t>& boundaries = Boundaries();
    const std::string&                original   = buffer_->OriginalUtf8();
    const std::uint32_t retainedBegin = line.graphemes.begin;
    const auto ellipsisBytes =
        static_cast<std::uint32_t>(request_.style.ellipsisUtf8.size());
    std::uint32_t retainedEnd = line.graphemes.end;

    // 후보는 자기만의 임시 문단이 되므로, 진짜 문단의 기준 방향을 명시적으로
    // 물려주지 않으면 잘린 글에서 방향을 다시 유도한다. "abcdef <히브리어>"의
    // 마지막 줄을 줄이면 후보의 첫 strong 문자가 히브리어가 되어 문단 level이
    // 0에서 1로 바뀌고, 중립인 줄임표가 줄의 반대쪽 끝에 놓인다.
    UBiDiLevel paragraphLevel =
        ParagraphLevelFor(request_.style.analysis.baseDirection);
    if (paragraphLevel == UBIDI_DEFAULT_LTR ||
        paragraphLevel == UBIDI_DEFAULT_RTL) {
        UBiDi* documentBidi = EnsureDocumentBidi();
        if (documentBidi == nullptr) return false;
        const auto lineUnits = buffer_->Utf16ForSourceBytes(line.bytes);
        if (!lineUnits) {
            Fail("a line range does not map to whole UTF-16 units", line.bytes);
            return false;
        }
        // 잘려서 남은 마지막 줄은 길이 0일 수 있고(빈 문단), 그때 줄 시작은
        // 텍스트 끝과 같다. ICU는 그 색인을 거절하므로 마지막 문자로 당긴다.
        const std::int32_t processed = ubidi_getProcessedLength(documentBidi);
        UErrorCode         status    = U_ZERO_ERROR;
        if (processed <= 0) {
            paragraphLevel = ubidi_getParaLevel(documentBidi);
        } else {
            std::int32_t at = static_cast<std::int32_t>(lineUnits->begin);
            if (at >= processed) at = processed - 1;
            ubidi_getParagraph(documentBidi, at, nullptr, nullptr,
                               &paragraphLevel, &status);
        }
        if (U_FAILURE(status)) {
            Fail("ICU refused to report the paragraph level of an ellipsized "
                 "line",
                 line.bytes);
            return false;
        }
    }
    TextAnalysisOptions candidateOptions = request_.style.analysis;
    candidateOptions.baseDirection = (paragraphLevel & 1U) != 0U
                                         ? BaseDirection::RightToLeft
                                         : BaseDirection::LeftToRight;

    for (;;) {
        ++g_observations.ellipsisRemovalAttempts;
        const std::uint32_t retainedFrom = boundaries[retainedBegin];
        const std::uint32_t retainedTo   = boundaries[retainedEnd];
        const auto retainedBytes = static_cast<std::uint32_t>(retainedTo -
                                                              retainedFrom);
        // Step 10: 남긴 whole-grapheme 접두사 뒤에 줄임표를 붙인 하나의 임시
        // 버퍼. 줄임표만 따로 셰이핑해 이어 붙이면 남긴 글의 마지막 글자가
        // 자기 뒤에 무엇이 오는지 모른 채 모양을 정한다.
        std::string candidateUtf8 =
            original.substr(retainedFrom, retainedBytes);
        candidateUtf8 += request_.style.ellipsisUtf8;

        VectorTextDiagnosticSink candidateSink;
        auto candidateBuffer =
            UnicodeTextBuffer::Build(candidateUtf8, candidateSink);
        if (!candidateBuffer) {
            Fail("the ellipsis candidate is too large to address", line.bytes);
            return false;
        }
        auto candidateAnalysis = UnicodeTextAnalyzer::Analyze(
            *candidateBuffer, candidateOptions, candidateSink);
        if (!candidateAnalysis) {
            Fail("ICU refused to analyze the ellipsis candidate", line.bytes);
            return false;
        }
        const auto candidateGraphemeCount = static_cast<std::uint32_t>(
            candidateAnalysis->GraphemeBoundaries().size() - 1U);
        const SourceByteRange candidateRange{
            0, static_cast<std::uint32_t>(candidateUtf8.size())};

        ShapedLine shaped;
        // Step 10a: 후보 전체를 원래 스타일과 진짜 최종 BOT/EOT로 셰이핑한다.
        // 줄임표가 텍스트의 끝이므로 EOT는 언제나 참이다.
        if (!ShapeRange(*candidateBuffer, *candidateAnalysis, candidateRange,
                        GraphemeRange{0, candidateGraphemeCount},
                        line.boundaries.beginningOfText, true, true, shaped)) {
            return false;
        }

        detail::LayoutEllipsisCandidate record;
        record.candidateBufferBytes =
            static_cast<std::uint32_t>(candidateBuffer->OriginalUtf8().size());
        record.retainedBytes = retainedBytes;
        record.ellipsisBytes = ellipsisBytes;
        for (const LinePiece& piece : shaped.pieces) {
            record.shapedBytes += piece.bytes.end - piece.bytes.begin;
        }
        if (!shaped.pieces.empty()) {
            record.beginningOfText =
                shaped.pieces.front().boundaries.beginningOfText;
            record.endOfText = shaped.pieces.back().boundaries.endOfText;
        }
        record.lineBeginningOfText = line.boundaries.beginningOfText;
        // 줄임표가 붙은 줄은 그 자리에서 텍스트가 끝난다. 뒤에 잘려 나간
        // 내용이 있더라도 셰이핑되는 텍스트의 끝은 여기다.
        record.lineEndOfText = true;
        RecordBounded(g_observations.ellipsisCandidates, record);

        const bool fits = !request_.constraints.width ||
                          shaped.advance.Raw() <=
                              request_.constraints.width->Raw();
        // Step 10c: 맞을 때까지 grapheme을 정확히 하나씩만 뺀다.
        if (!fits && retainedEnd > retainedBegin) {
            --retainedEnd;
            continue;
        }

        // 후보는 자기 버퍼를 갖는 별개의 텍스트이므로 자기 문단 BiDi가
        // 필요하다. 문단 level은 진짜 문단에서 가져온 값을 명시적으로 준다.
        ScopedUBiDi candidateBidi;
        if (candidateBidi.Get() == nullptr) {
            Fail("ICU refused to allocate a BiDi object for the ellipsis "
                 "candidate",
                 line.bytes);
            return false;
        }
        UErrorCode candidateStatus = U_ZERO_ERROR;
        ubidi_setPara(candidateBidi.Get(),
                      candidateBuffer->SanitizedUtf16().data(),
                      static_cast<std::int32_t>(
                          candidateBuffer->SanitizedUtf16().size()),
                      paragraphLevel, nullptr, &candidateStatus);
        if (U_FAILURE(candidateStatus)) {
            Fail("ICU refused the BiDi pass for the ellipsis candidate",
                 line.bytes);
            return false;
        }
        std::vector<VisualRun> ordered;
        if (!OrderVisually(*candidateBuffer, candidateBidi.Get(), candidateRange,
                           shaped.pieces, ordered)) {
            return false;
        }

        // Step 10b: 남긴 cluster는 원본 구간으로, 합성 cluster는 길이 0의
        // 잘림 경계로 되돌린다. 원본 byte를 합성 glyph에 물려주면 caret과
        // hit-test가 화면에 없는 글자를 가리키게 된다.
        bool missingSynthetic = false;
        for (VisualRun& run : ordered) {
            for (PositionedGlyph& glyph : run.glyphs) {
                if (glyph.glyph.sourceBytes.begin < retainedBytes) {
                    const std::uint32_t begin =
                        retainedFrom + glyph.glyph.sourceBytes.begin;
                    const std::uint32_t end =
                        retainedFrom + std::min(glyph.glyph.sourceBytes.end,
                                                retainedBytes);
                    glyph.glyph.sourceBytes = SourceByteRange{begin, end};
                    glyph.glyph.graphemes =
                        GraphemesForBytes(boundaries, glyph.glyph.sourceBytes);
                } else {
                    glyph.glyph.sourceBytes = SourceByteRange{retainedTo,
                                                              retainedTo};
                    glyph.glyph.graphemes = GraphemeRange{retainedEnd,
                                                          retainedEnd};
                    if (glyph.glyph.missing) missingSynthetic = true;
                }
            }
        }
        if (missingSynthetic) {
            TextValidationFact fact;
            fact.code                  = TextDiagnosticCode::MissingGlyph;
            fact.severity              = TextSeverity::Error;
            fact.subsystem             = kSubsystem;
            fact.message               = "no font in the resolved family can "
                                         "render the authored ellipsis token";
            fact.remediation           = "Author an ellipsis the family covers, "
                                         "or add a fallback family that does.";
            fact.sourceBytes           = SourceByteRange{retainedTo, retainedTo};
            fact.graphemes             = GraphemeRange{retainedEnd, retainedEnd};
            fact.recoverableAtRuntime  = true;
            fact.blocksAuthoredPackage = true;
            facts_.Append(std::move(fact));
        }

        // 최종 키가 보는 매핑도 함께 되돌린다. 남긴 구간과 합성 구간을
        // 구분하지 않으면 서로 다른 줄임 지점이 같은 정체성을 갖는다.
        for (LinePiece& piece : shaped.pieces) {
            piece.shapeInputMapping.clear();
            const std::uint32_t low  = piece.bytes.begin;
            const std::uint32_t high = piece.bytes.end;
            if (low < retainedBytes) {
                ShapeInputSourceSpan retained;
                const std::uint32_t retainedHigh = std::min(high, retainedBytes);
                retained.shapeInputBytes =
                    SourceByteRange{0, retainedHigh - low};
                retained.originalSourceBytes =
                    SourceByteRange{retainedFrom + low,
                                    retainedFrom + retainedHigh};
                retained.originalGraphemes =
                    GraphemesForBytes(boundaries, retained.originalSourceBytes);
                retained.synthetic = false;
                piece.shapeInputMapping.push_back(retained);
            }
            if (high > retainedBytes) {
                const std::uint32_t syntheticLow = std::max(low, retainedBytes);
                ShapeInputSourceSpan synthetic;
                synthetic.shapeInputBytes =
                    SourceByteRange{syntheticLow - low, high - low};
                synthetic.originalSourceBytes =
                    SourceByteRange{retainedTo, retainedTo};
                synthetic.originalGraphemes =
                    GraphemeRange{retainedEnd, retainedEnd};
                synthetic.synthetic = true;
                piece.shapeInputMapping.push_back(synthetic);
            }
            piece.bytes = SourceByteRange{retainedFrom + std::min(low,
                                                                  retainedBytes),
                                          retainedFrom + std::min(high,
                                                                  retainedBytes)};
        }

        line.graphemes   = GraphemeRange{retainedBegin, retainedEnd};
        line.bytes       = SourceByteRange{retainedFrom, retainedTo};
        // 후보는 임시 버퍼에서 셰이핑되었으므로 handledBytes는 후보 좌표다.
        // 이 줄이 원본에서 실제로 덮은 구간은 남긴 접두사이며, 셰이핑이
        // 일어났다는 사실 자체는 shaped 플래그가 증인이다.
        if (!shaped.shaped) {
            Fail("the ellipsis candidate produced no shaping call", line.bytes);
            return false;
        }
        line.shapedRange = line.bytes;
        line.pieces      = std::move(shaped.pieces);
        line.visualRuns  = std::move(ordered);
        line.advance     = shaped.advance;
        line.overWidth   = !fits;
        return true;
    }
}

// ── Step 12/12a/12b: vertical fields from imported design integers ──────────
bool LayoutRun::ProceduralMetrics(Fixed26_6& ascent, Fixed26_6& descent,
                                  Fixed26_6& lineGap) const {
    if (!MulDivChecked(FontSize(), 3, 4, ascent)) return false;
    if (!MulDivChecked(FontSize(), 1, 4, descent)) return false;
    lineGap = Fixed26_6::FromRaw(0);
    return true;
}

bool LayoutRun::LineMetrics(const LineDraft& line, Fixed26_6& ascent,
                            Fixed26_6& descent, Fixed26_6& lineGap) const {
    ascent  = Fixed26_6::FromRaw(0);
    descent = Fixed26_6::FromRaw(0);
    lineGap = Fixed26_6::FromRaw(0);
    bool                                  any = false;
    std::vector<const FontFaceResource*> seen;
    const auto fold = [&](const FontFaceResource* resource) {
        for (const FontFaceResource* existing : seen) {
            if (existing == resource) return true;
        }
        seen.push_back(resource);
        const auto scaled =
            molga::ScaleFontDesignMetrics(resource->designMetrics, FontSize());
        if (!scaled) return false;
        ascent  = Fixed26_6::FromRaw(std::max(ascent.Raw(), scaled->ascent.Raw()));
        descent =
            Fixed26_6::FromRaw(std::max(descent.Raw(), scaled->descent.Raw()));
        lineGap =
            Fixed26_6::FromRaw(std::max(lineGap.Raw(), scaled->lineGap.Raw()));
        any = true;
        return true;
    };

    for (const VisualRun& run : line.visualRuns) {
        for (const PositionedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.faceResource == nullptr) continue;
            if (!fold(glyph.glyph.faceResource.get())) {
                Fail("an imported design metric does not scale into the "
                     "checked 26.6 range",
                     line.bytes);
                return false;
            }
        }
    }
    if (!any) {
        // 빈 줄이거나 face가 하나도 붙지 않은 줄. 해석된 family의 첫 번째
        // 순서 후보를 쓴다 — 후보가 아예 없는 family만 Step 12b로 내려간다.
        if (!family_.candidates.empty() &&
            family_.candidates.front().resource != nullptr) {
            if (!fold(family_.candidates.front().resource.get())) {
                Fail("an imported design metric does not scale into the "
                     "checked 26.6 range",
                     line.bytes);
                return false;
            }
        }
    }
    if (any) return true;
    if (!ProceduralMetrics(ascent, descent, lineGap)) {
        Fail("procedural em metrics do not scale into the checked 26.6 range",
             line.bytes);
        return false;
    }
    return true;
}

bool LayoutRun::ComputeVerticalFields() {
    Fixed26_6 baseline = Fixed26_6::FromRaw(0);
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        LineDraft& line = lines_[index];
        if (!LineMetrics(line, line.ascent, line.descent, line.lineGap)) {
            return false;
        }
        if (index == 0) baseline = line.ascent;
        line.baseline = baseline;
        if (!SubChecked(baseline, line.ascent, line.top) ||
            !AddChecked(baseline, line.descent, line.bottom)) {
            Fail("a line's vertical band overflows the checked 26.6 range",
                 line.bytes);
            return false;
        }
        // Step 12: 다음 baseline까지의 걸음은 lineGap을 포함한 뒤에 저작된
        // lineSpacing 정책을 곱한다. 순서를 뒤집으면 lineSpacing이 gap을
        // 건너뛴다.
        Fixed26_6 step = Fixed26_6::FromRaw(0);
        if (!AddChecked(line.ascent, line.descent, step) ||
            !AddChecked(step, line.lineGap, step) ||
            !MulDivChecked(step, request_.style.lineSpacing.Raw(),
                           Fixed26_6::Scale, step) ||
            !AddChecked(baseline, step, baseline)) {
            Fail("a baseline step overflows the checked 26.6 range", line.bytes);
            return false;
        }
    }
    return true;
}

// ── Steps 13-13c: positions, alignment, carets ──────────────────────────────
bool LayoutRun::BuildInteriorCarets(PositionedGlyph& glyph,
                                    bool rightToLeft) const {
    const std::uint32_t graphemes =
        glyph.glyph.graphemes.end - glyph.glyph.graphemes.begin;
    if (graphemes <= 1U) return true;
    const Fixed26_6 advance = glyph.glyph.advanceX;

    // Step 13a: 완전하고 순서가 맞으며 advance 안쪽에 있는 집합만 받는다.
    // 하나라도 어긋나면 부분적으로 섞지 않고 집합 전체를 버린다 — 섞으면
    // "폰트가 준 자리"만 골라 검증할 수 없게 된다.
    const bool useGdef = detail::AdjustedGdefCaretSetIsUsable(
        glyph.glyph.adjustedGdefCaretOffsets, graphemes, advance);

    for (std::uint32_t index = 1U; index < graphemes; ++index) {
        Fixed26_6 along = Fixed26_6::FromRaw(0);
        if (useGdef) {
            along = glyph.glyph.adjustedGdefCaretOffsets[index - 1U];
        } else if (!MulDivChecked(advance, index, graphemes, along)) {
            Fail("a proportional interior caret overflows the checked 26.6 "
                 "range",
                 glyph.glyph.sourceBytes);
            return false;
        }
        GlyphInteriorCaret caret;
        caret.fromAdjustedGdef = useGdef;
        caret.logicalGraphemeBoundary = detail::InteriorCaretLogicalBoundary(
            glyph.glyph.graphemes, index, rightToLeft);
        if (!AddChecked(glyph.origin.x, along, caret.position.x)) {
            Fail("an interior caret position overflows the checked 26.6 range",
                 glyph.glyph.sourceBytes);
            return false;
        }
        caret.position.y = glyph.origin.y;
        glyph.interiorCarets.push_back(caret);
    }
    return true;
}

// ── Steps 4-5: the line's grapheme-boundary caret stops ─────────────────────
// caret이 설 수 있는 자리는 논리 grapheme 경계뿐이고, 그 자리의 시각 좌표는
// 이 함수가 확정된 줄의 시각 순서에서 읽는다. 자리는 세 곳에서만 온다:
// cell의 논리 시작 모서리, 논리 끝 모서리, 그리고 배치가 이미 저장해 둔
// PositionedGlyph::interiorCarets. 그래서 대리 쌍이나 결합/ZWJ grapheme 안쪽에
// 자리가 생길 수 없다 — 세 출처 어느 것도 grapheme 경계가 아닌 값을 낼 수 없다.
//
// 단위는 glyph 하나가 아니라 "같은 grapheme을 나눠 쓰는 glyph 묶음"(cell)이고,
// 묶는 기준은 grapheme 범위가 같은가가 아니라 겹치는가다. 두 기준은 실제로
// 다른 답을 낸다:
//   - 데바나가리의 pre-base 매트라는 같은 범위를 가진 glyph 둘로 온다. glyph마다
//     자리를 내면 같은 논리 경계가 cell 안쪽의 서로 다른 두 x에 서게 된다.
//   - 합자 뒤에 결합 문자가 붙으면 HarfBuzz는(MONOTONE_CHARACTERS) 그 표식의
//     cluster를 합자 cluster에 합치지 않으므로, 표식의 grapheme 범위는 합자
//     범위의 진부분집합이 된다. 같은가로 묶으면 표식이 자기 cell이 되어 합자의
//     바깥 모서리에 두 번째 자리를 내고, 방향이 바뀌지도 않은 경계가 affinity
//     쌍처럼 보인다 — 그러면 어느 소비자도 진짜 BiDi 자리와 구분할 수 없다.
bool LayoutRun::BuildCaretStops(const TextLine& line, std::uint32_t lineIndex,
                                Fixed26_6 lineOrigin,
                                std::vector<CaretStop>& out) const {
    const std::size_t first = out.size();
    for (const VisualRun& run : line.visualRuns) {
        const bool rightToLeft = (run.bidiLevel & 1U) != 0U;
        std::size_t index = 0;
        while (index < run.glyphs.size()) {
            GraphemeRange cell = run.glyphs[index].glyph.graphemes;
            std::size_t last = index;
            // 빈 범위(줄임표의 합성 glyph)는 어느 쪽으로도 묶지 않는다.
            // 지금 이 서비스가 내는 배치에서는 닿지 않는 줄이다 — 합성 glyph는
            // 언제나 자기 혼자 하나의 시각 run이고, 이 묶기는 run 안에서만
            // 일어난다. 그래도 남기는 이유는 겹침 판정이 빈 범위를 특별히 다루지
            // 않으면 cell 안쪽에 놓인 빈 범위 하나가 "겹친다"로 셈해져 그 cell을
            // 삼켜 버리기 때문이고, 그 조건은 여기 한 줄로만 막을 수 있다.
            if (cell.begin != cell.end) {
                while (last + 1U < run.glyphs.size()) {
                    const GraphemeRange next =
                        run.glyphs[last + 1U].glyph.graphemes;
                    if (next.begin == next.end) break;
                    if (!(next.begin < cell.end && cell.begin < next.end)) break;
                    cell.begin = std::min(cell.begin, next.begin);
                    cell.end   = std::max(cell.end, next.end);
                    ++last;
                }
            }
            const std::size_t begin = index;
            index = last + 1U;
            // 줄임표의 합성 glyph는 원본 grapheme을 하나도 덮지 않는다(Step
            // 10b). 그 자리에 caret을 세우면 화면에 없는 글자를 가리키게 되고,
            // 잘린 지점의 caret은 남긴 마지막 grapheme의 끝 모서리가 이미 낸다.
            if (cell.begin == cell.end) continue;

            const PositionedGlyph& head = run.glyphs[begin];
            const PositionedGlyph& tail = run.glyphs[last];
            // cell의 바깥 모서리는 glyph 원점이 아니라 pen 자리다. 원점은 pen에
            // GPOS의 x offset을 더한 그리기 좌표라, 앞 cell의 "원점 + advance"와
            // 뒤 cell의 원점이 두 offset의 차만큼 어긋난다. 그 어긋남은 방향이
            // 바뀌지 않는 경계에서도 자리를 둘로 갈라 놓아, affinity가 실제로
            // 뜻을 갖는 BiDi 경계와 구분할 수 없게 만든다. pen은 PositionLines가
            // origin.x = pen + offsetX로 놓았으므로 그대로 되짚을 수 있다.
            //
            // 이 규칙은 바깥 모서리에만 해당한다. 아래에서 옮겨 적는 내부
            // caret은 Task 7.2가 glyph 그리기 원점에 붙여 저장해 둔 값이고,
            // 그 값은 여기서 다시 만들지 않는다(Step 5). 두 규약은 glyph의
            // offsetX가 0인 곳에서 정확히 같은 자리를 가리키고, 커밋된 corpus의
            // 내부 caret을 가진 glyph는 전부 offsetX가 0이다. 합자에 가로 GPOS
            // 보정을 거는 폰트가 들어오면 그 합자의 내부 caret이 자기 cell에서
            // offsetX만큼 밀리므로, 두 규약을 하나로 합치는 일은 7.2의 저장
            // 공식과 그것을 고정한 케이스를 함께 고치는 개정이 되어야 한다.
            Fixed26_6 leadingEdge  = Fixed26_6::FromRaw(0);
            Fixed26_6 trailingEdge = Fixed26_6::FromRaw(0);
            if (!SubChecked(head.origin.x, head.glyph.offsetX, leadingEdge) ||
                !SubChecked(tail.origin.x, tail.glyph.offsetX, trailingEdge) ||
                !AddChecked(trailingEdge, tail.glyph.advanceX, trailingEdge)) {
                Fail("a caret stop position overflows the checked 26.6 range",
                     tail.glyph.sourceBytes);
                return false;
            }

            // caret의 y는 언제나 그 줄의 baseline이다. glyph 원점의 y는 GPOS의
            // 세로 보정이 섞인 그리기 좌표라, 그것을 쓰면 한 줄 안의 자리들이
            // 서로 다른 높이를 갖게 되고 소비자가 어느 값을 믿어야 할지 알 수 없다.
            //
            // 논리 시작은 LTR에서 왼쪽 모서리, RTL에서 오른쪽 모서리다.
            CaretStop start;
            start.logicalGraphemeBoundary = cell.begin;
            start.affinity   = CaretAffinity::Downstream;
            start.position.x = rightToLeft ? trailingEdge : leadingEdge;
            start.position.y = line.baseline;
            start.lineIndex  = lineIndex;
            out.push_back(start);

            CaretStop end;
            end.logicalGraphemeBoundary = cell.end;
            end.affinity   = CaretAffinity::Upstream;
            end.position.x = rightToLeft ? leadingEdge : trailingEdge;
            end.position.y = line.baseline;
            end.lineIndex  = lineIndex;
            out.push_back(end);

            // Step 5: 합자 내부의 자리는 오직 저장된 것을 옮겨 적는다. 여기서
            // 다시 계산하면 배치가 낸 자리와 조용히 달라질 수 있고, GDEF에서
            // 온 자리와 균등 분할로 유도한 자리를 구분할 근거도 사라진다.
            const std::size_t interiorFirst = out.size();
            for (std::size_t glyph = begin; glyph <= last; ++glyph) {
                for (const GlyphInteriorCaret& interior :
                     run.glyphs[glyph].interiorCarets) {
                    // 한 cell 안에서 같은 논리 경계는 자리를 하나만 갖는다.
                    // BuildInteriorCarets는 glyph 하나마다 그 glyph의 advance를
                    // 나누므로, 여러 grapheme을 덮는 glyph가 한 cell에 둘 이상
                    // 들어오면 같은 경계가 서로 다른 x에 두 번 실린다. 커밋된
                    // corpus는 그런 배치를 내지 않지만, 나오면 방향이 바뀌지
                    // 않는 경계가 BiDi affinity 쌍과 똑같이 보이게 된다.
                    bool alreadyStored = false;
                    for (std::size_t seen = interiorFirst; seen < out.size();
                         ++seen) {
                        if (out[seen].logicalGraphemeBoundary ==
                            interior.logicalGraphemeBoundary) {
                            alreadyStored = true;
                            break;
                        }
                    }
                    if (alreadyStored) continue;
                    CaretStop stop;
                    stop.logicalGraphemeBoundary =
                        interior.logicalGraphemeBoundary;
                    stop.affinity   = CaretAffinity::Downstream;
                    stop.position.x = interior.position.x;
                    stop.position.y = line.baseline;
                    stop.lineIndex  = lineIndex;
                    out.push_back(stop);
                }
            }
        }
    }

    if (out.size() == first) {
        // 빈 줄에도 자리는 하나 있어야 한다. 없으면 편집기가 빈 줄에 커서를
        // 놓을 수 없고, 그 줄은 metric만 있고 닿을 수 없는 줄이 된다.
        CaretStop stop;
        stop.logicalGraphemeBoundary = line.graphemes.begin;
        stop.affinity   = CaretAffinity::Downstream;
        stop.position.x = lineOrigin;
        stop.position.y = line.baseline;
        stop.lineIndex  = lineIndex;
        out.push_back(stop);
        return true;
    }

    const auto rank = [](CaretAffinity affinity) {
        return affinity == CaretAffinity::Downstream ? 0 : 1;
    };
    const auto firstIt = [&out, first]() {
        return out.begin() + static_cast<std::ptrdiff_t>(first);
    };
    // 접기 전용 순서. 같은 경계의 자리들을 한데 모으고, 그 안에서 x로, 다시
    // affinity로 가른다 — Downstream을 앞에 두는 이유는 접기가 앞의 것을
    // 남기기 때문이다.
    std::stable_sort(firstIt(), out.end(),
                     [&rank](const CaretStop& a, const CaretStop& b) {
                         if (a.logicalGraphemeBoundary !=
                             b.logicalGraphemeBoundary) {
                             return a.logicalGraphemeBoundary <
                                    b.logicalGraphemeBoundary;
                         }
                         if (a.position.x.Raw() != b.position.x.Raw()) {
                             return a.position.x.Raw() < b.position.x.Raw();
                         }
                         return rank(a.affinity) < rank(b.affinity);
                     });
    // 방향이 바뀌지 않는 경계에서는 앞 cell의 끝 모서리와 뒤 cell의 시작
    // 모서리가 같은 자리다. 그 둘을 남겨 두면 caret 하나가 두 번 세어지고,
    // 논리 이웃과 시각 이웃이 같은 자리라는 사실이 affinity 둘로 위장된다.
    // 접히는 조건은 "같은 경계이면서 같은 x"이고, 두 조건이 다 필요하다:
    // 경계가 같아도 x가 다르면 그것이 BiDi 자리 쌍이고, x가 같아도 경계가
    // 다르면 폭 0인 cell의 양쪽 모서리다.
    //
    // std::unique를 쓰지 않는 이유가 여기 있다. unique는 이웃만 견주므로 어느
    // 조건이 실제로 일하는지를 정렬 키가 조용히 정해 버린다. 이미 남긴 자리와
    // 직접 견주면 그 규칙이 정렬 순서와 무관하게 성립한다.
    std::size_t write = first;
    for (std::size_t read = first; read < out.size(); ++read) {
        bool folded = false;
        for (std::size_t kept = write; kept > first;) {
            --kept;
            if (out[kept].logicalGraphemeBoundary !=
                out[read].logicalGraphemeBoundary) {
                break;
            }
            if (out[kept].position.x.Raw() == out[read].position.x.Raw()) {
                folded = true;
                break;
            }
        }
        if (folded) continue;
        out[write] = out[read];
        ++write;
    }
    out.resize(write);

    // 그리고 시각 진행 순서(왼쪽에서 오른쪽)로 다시 정렬한다. hit test의 구간
    // 훑기가 줄 안에서 x가 단조롭다는 것을 전제한다.
    std::stable_sort(firstIt(), out.end(),
                     [&rank](const CaretStop& a, const CaretStop& b) {
                         if (a.position.x.Raw() != b.position.x.Raw()) {
                             return a.position.x.Raw() < b.position.x.Raw();
                         }
                         if (a.logicalGraphemeBoundary !=
                             b.logicalGraphemeBoundary) {
                             return a.logicalGraphemeBoundary <
                                    b.logicalGraphemeBoundary;
                         }
                         return rank(a.affinity) < rank(b.affinity);
                     });
    return true;
}

bool LayoutRun::PositionLines(TextLayout& layout) {
    Fixed26_6 widest = Fixed26_6::FromRaw(0);
    for (const LineDraft& line : lines_) {
        widest = Fixed26_6::FromRaw(std::max(widest.Raw(), line.advance.Raw()));
    }
    const Fixed26_6 boxWidth =
        request_.constraints.width ? *request_.constraints.width : widest;

    Fixed26_6 contentHeight = Fixed26_6::FromRaw(0);
    if (!SubChecked(lines_.back().bottom, lines_.front().top, contentHeight)) {
        Fail("the paragraph height overflows the checked 26.6 range",
             SourceByteRange{});
        return false;
    }
    const Fixed26_6 boxHeight =
        request_.constraints.height ? *request_.constraints.height
                                    : contentHeight;
    Fixed26_6 verticalSlack = Fixed26_6::FromRaw(0);
    if (!SubChecked(boxHeight, contentHeight, verticalSlack)) {
        Fail("the vertical alignment slack overflows the checked 26.6 range",
             SourceByteRange{});
        return false;
    }
    Fixed26_6 verticalOffset = Fixed26_6::FromRaw(0);
    switch (request_.style.vertical) {
        case TextVerticalAlignment::Top:
            break;
        case TextVerticalAlignment::Middle:
            if (!MulDivChecked(verticalSlack, 1, 2, verticalOffset)) {
                Fail("a vertical alignment offset overflows the checked 26.6 "
                     "range",
                     SourceByteRange{});
                return false;
            }
            break;
        case TextVerticalAlignment::Bottom:
            verticalOffset = verticalSlack;
            break;
    }

    layout.lines.reserve(lines_.size());
    for (LineDraft& draft : lines_) {
        Fixed26_6 horizontalSlack = Fixed26_6::FromRaw(0);
        if (!SubChecked(boxWidth, draft.advance, horizontalSlack)) {
            Fail("the horizontal alignment slack overflows the checked 26.6 "
                 "range",
                 draft.bytes);
            return false;
        }
        Fixed26_6 pen = Fixed26_6::FromRaw(0);
        switch (request_.style.horizontal) {
            case TextHorizontalAlignment::Left:
                break;
            case TextHorizontalAlignment::Center:
                if (!MulDivChecked(horizontalSlack, 1, 2, pen)) {
                    Fail("a horizontal alignment offset overflows the checked "
                         "26.6 range",
                         draft.bytes);
                    return false;
                }
                break;
            case TextHorizontalAlignment::Right:
                pen = horizontalSlack;
                break;
        }

        TextLine line;
        line.sourceBytes = draft.bytes;
        line.graphemes   = draft.graphemes;
        line.advance     = draft.advance;
        line.ascent      = draft.ascent;
        line.descent     = draft.descent;
        line.lineGap     = draft.lineGap;
        if (!AddChecked(draft.baseline, verticalOffset, line.baseline) ||
            !AddChecked(draft.top, verticalOffset, line.top) ||
            !AddChecked(draft.bottom, verticalOffset, line.bottom)) {
            Fail("a vertically aligned line overflows the checked 26.6 range",
                 draft.bytes);
            return false;
        }

        // 정렬이 정한 이 줄의 시작 자리. glyph 하나 없는 줄에서도 caret이 설
        // 자리를 알아야 하므로, pen이 움직이기 전에 붙잡아 둔다.
        const Fixed26_6 lineOrigin = pen;

        line.visualRuns = std::move(draft.visualRuns);
        for (VisualRun& run : line.visualRuns) {
            const bool rightToLeft = (run.bidiLevel & 1U) != 0U;
            for (PositionedGlyph& glyph : run.glyphs) {
                if (!AddChecked(pen, glyph.glyph.offsetX, glyph.origin.x) ||
                    !SubChecked(line.baseline, glyph.glyph.offsetY,
                                glyph.origin.y) ||
                    !AddChecked(pen, glyph.glyph.advanceX, pen)) {
                    Fail("a glyph position overflows the checked 26.6 range",
                         glyph.glyph.sourceBytes);
                    return false;
                }
                if (!BuildInteriorCarets(glyph, rightToLeft)) return false;
            }
        }
        if (!BuildCaretStops(line,
                             static_cast<std::uint32_t>(layout.lines.size()),
                             lineOrigin, layout.caretStops)) {
            return false;
        }
        layout.lines.push_back(std::move(line));
    }

    layout.intrinsicSize.width  = widest;
    layout.intrinsicSize.height = contentHeight;
    return true;
}

// ── Step 14: the audit identity of every final line shape ───────────────────
TextShapeCacheKey LayoutRun::BuildShapeKey(const LineDraft& line,
                                           const LinePiece& piece) const {
    TextShapeCacheKey key;
    key.originalUtf8        = request_.utf8;
    key.originalBytesHash   = CacheBytesHash(key.originalUtf8);
    key.shapeInputUtf8      = piece.shapeInputUtf8;
    key.shapeInputBytesHash = CacheBytesHash(key.shapeInputUtf8);
    key.shapeInputMapping   = piece.shapeInputMapping;
    key.paragraphBytes      = line.paragraphBytes;
    key.runBytes            = piece.bytes;
    key.fallbackGraphGeneration = family_.fallbackGraphGeneration;
    for (const ShapedGlyph& glyph : piece.glyphs) {
        if (glyph.faceResource == nullptr) continue;
        SelectedFaceShapeIdentity face;
        face.fontGuid          = glyph.fontGuid;
        face.fontRevision      = glyph.fontRevision;
        face.sourceSha256      = glyph.faceResource->sourceSha256;
        face.artifactSha256    = glyph.faceResource->artifactSha256;
        face.artifactLocator   = glyph.faceResource->artifactLocator;
        face.contentGeneration = glyph.faceResource->contentGeneration;
        face.faceIndex         = glyph.faceIndex;
        if (glyph.faceResource->asset != nullptr) {
            face.artifactByteSize = glyph.faceResource->asset->artifactByteSize;
        }
        bool known = false;
        for (const SelectedFaceShapeIdentity& existing : key.selectedFaces) {
            if (existing.fontGuid == face.fontGuid &&
                existing.faceIndex == face.faceIndex &&
                existing.artifactSha256 == face.artifactSha256) {
                known = true;
                break;
            }
        }
        if (!known) key.selectedFaces.push_back(std::move(face));
    }
    key.fontSize        = request_.style.shape.fontSize;
    key.embeddingLevel  = piece.level;
    key.direction       = (piece.level & 1U) != 0U
                              ? static_cast<std::int32_t>(HB_DIRECTION_RTL)
                              : static_cast<std::int32_t>(HB_DIRECTION_LTR);
    key.scriptCode      = piece.scriptCode;
    key.language        = request_.style.shape.language;
    key.componentLocale = request_.style.analysis.locale;
    // Step 5a: 다섯 값은 요청 문자열이나 지금의 ICU 상태가 아니라 불변
    // UnicodeAnalysis::Identity()에서 그대로 온다. 문단 분석이 아니라 이
    // piece를 실제로 낸 분석의 것이다 — 줄임표 후보는 자기만의 분석을 쓴다.
    const UnicodeAnalysisIdentity& identity = piece.analysisIdentity;
    key.resolvedGraphemeLocale  = identity.resolvedGraphemeLocale;
    key.resolvedLineBreakLocale = identity.resolvedLineBreakLocale;
    key.graphemeRuleIdentity    = identity.graphemeRuleIdentity;
    key.lineBreakRuleIdentity   = identity.lineBreakRuleIdentity;
    key.analysisGeneration      = identity.analysisGeneration;
    key.boundaries              = piece.boundaries;
    key.harfbuzzBufferFlags =
        (piece.boundaries.beginningOfText
             ? static_cast<std::uint32_t>(HB_BUFFER_FLAG_BOT)
             : 0U) |
        (piece.boundaries.endOfText
             ? static_cast<std::uint32_t>(HB_BUFFER_FLAG_EOT)
             : 0U);
    key.clusterPolicy        = request_.style.shape.clusterPolicy;
    key.orderedFeatures      = request_.style.shape.orderedFeatures;
    key.harfbuzzRevision     = hb_version_string();
    key.icuRevision          = U_ICU_VERSION;
    key.icuDataSha256        = kPackagedIcuDataSha256;
    key.dependencyContractSha256 =
        TextRuntimeDependencies::Get().DependencyContractSha256();
    return key;
}

TextParagraphCacheKey LayoutRun::BuildFinalKey() const {
    TextParagraphCacheKey key;
    for (const LineDraft& line : lines_) {
        for (const LinePiece& piece : line.pieces) {
            key.finalLineShapeKeys.push_back(BuildShapeKey(line, piece));
        }
    }
    key.constraints         = request_.constraints;
    key.wrap                = request_.style.wrap;
    key.overflow            = request_.style.overflow;
    key.overlongTokenPolicy =
        OverlongTokenPolicy(request_.style.wrap, request_.style.overflow);
    key.ellipsisUtf8   = request_.style.ellipsisUtf8;
    key.ellipsisStyle  = request_.style.shape;
    key.maxLines       = request_.style.maxLines;
    key.lineSpacing    = request_.style.lineSpacing;
    key.horizontal     = request_.style.horizontal;
    key.vertical       = request_.style.vertical;
    key.visualRevision = request_.visualRevision;
    return key;
}

// 예산은 배포를 막는 사실부터 쓴다. 저자가 실제로 보는 통로는 진단 하나뿐인데,
// 예산이 순서만 따르면 앞에 놓인 정보성 fallback 여덟 개가 그 뒤의 오류를
// 통째로 가린다. 권한 있는 기록(validationFacts)에는 상한이 없으므로 패키지
// 검증은 어느 쪽이든 전부 본다.
void LayoutRun::ReportFacts(const std::vector<TextValidationFact>& facts) {
    ReportFactSequence(facts, nullptr);
}

// 예산을 두 벌의 사실에 걸쳐 쓴다. second가 null이면 first만 본다.
void LayoutRun::ReportFactSequence(
    const std::vector<TextValidationFact>& first,
    const std::vector<TextValidationFact>* second) {
    std::size_t budget = kMaxLayoutDiagnosticsPerParagraph;
    for (int pass = 0; pass < 2; ++pass) {
        const bool blocking = pass == 0;
        for (int part = 0; part < 2; ++part) {
            const std::vector<TextValidationFact>* facts =
                part == 0 ? &first : second;
            if (facts == nullptr) continue;
            for (const TextValidationFact& fact : *facts) {
                if (budget == 0) return;
                if (fact.blocksAuthoredPackage != blocking) continue;
                --budget;
                sink_.Report(
                    MakeContextualDiagnostic(fact, request_.diagnosticContext));
            }
        }
    }
}

// Step 4b: 저장된 사실과 지금의 해석 사실을 합쳐 한 번씩만 보고한다.
//
// 합집합 벡터를 만들지 않는다. warm 경로의 존재 이유가 싸다는 것인데, 저장된
// 사실을 통째로 복사하면 깨진 문단 하나가 프레임마다 그 복사를 다시 낸다.
// 실제로 새로 나올 사실은 해석 쪽뿐이고 그 수는 resolver의 진단 상한에 묶여
// 있으므로, 그것만 따로 모아 예산을 함께 쓴다.
void LayoutRun::ReportWarmFacts(const std::vector<TextValidationFact>& stored) {
    std::vector<TextValidationFact> extra;
    for (const TextValidationFact& fact : resolveFacts_.Facts()) {
        if (ContainsFact(stored, fact)) continue;
        extra.push_back(fact);
    }
    ReportFactSequence(stored, &extra);
}

std::optional<std::shared_ptr<const TextLayout>> LayoutRun::Run() {
    ResetObservations();
    if (!TextRuntimeDependencies::Get().IsReady()) {
        ReportTerminal(sink_, TextDiagnosticCode::DependencyInvalid,
                       "paragraph layout requires a ready text runtime; ICU and "
                       "HarfBuzz have not been initialized in this process",
                       "Create a TextRuntimeLifetimeGuard from a verified "
                       "Engine/Text root before laying out text, and keep it "
                       "alive for as long as any text service exists.",
                       request_.diagnosticContext, SourceByteRange{});
        return std::nullopt;
    }
    // 이 가동이 끝날 때까지 런타임 수명을 붙잡는다. Shutdown은 client handle이
    // 전부 사라진 뒤에만 진행되므로, 이 lease가 살아 있는 한 아래의 ubidi_*
    // 객체와 이 가동이 부르는 분석/셰이핑은 u_cleanup 이후를 볼 수 없다.
    lease_ = TextRuntimeClientHandle::Acquire();
    if (!lease_) {
        ReportTerminal(sink_, TextDiagnosticCode::DependencyInvalid,
                       "paragraph layout could not acquire a text runtime "
                       "client handle",
                       "Lay out text only while the process text runtime is "
                       "ready; a terminally cleaned runtime never becomes "
                       "ready again.",
                       request_.diagnosticContext, SourceByteRange{});
        return std::nullopt;
    }
    if (!ResolveFamilyClosure()) return std::nullopt;

    // Step 4a: UnicodeTextBuffer도 ICU 객체도 HarfBuzz 객체도 만들기 전에
    // 조회한다. 하나라도 앞서 만들면 warm 경로의 존재 이유가 사라진다.
    auto requestKey =
        std::make_shared<const TextLayoutRequestIndexKey>(BuildRequestKey());
    g_observations.requestKey = requestKey;
    if (const auto hit = cache_.FindByRequest(*requestKey)) {
        ReportWarmFacts(hit->layout->validationFacts);
        return hit->layout;
    }

    if (!BuildAnalysis()) return std::nullopt;
    if (!MeasureParagraphs()) return std::nullopt;
    if (!BuildLines()) return std::nullopt;
    if (!ComputeVerticalFields()) return std::nullopt;
    if (!ApplyTruncationAndOverflow()) return std::nullopt;
    if (!ComputeVerticalFields()) return std::nullopt;

    auto layout = std::make_shared<TextLayout>();
    if (!PositionLines(*layout)) return std::nullopt;
    layout->clipped         = clipped_;
    layout->ellipsized      = ellipsized_;
    layout->validationFacts = facts_.Facts();

    for (const LineDraft& line : lines_) {
        RecordBounded(g_observations.acceptedLineShapes, line.shapedRange);
    }

    // Step 14: 배치를 낸 호출은 전부 저장한다. 치환/두부/fallback을 낸
    // 결과도 예외가 아니다 — 그 사실들이 캐시와 함께 살아 있어야 warm hit이
    // 같은 진단을 다시 낼 수 있다.
    std::shared_ptr<const TextLayout> immutable = layout;
    auto finalKey = std::make_shared<const TextParagraphCacheKey>(BuildFinalKey());
    g_observations.finalKey = finalKey;
    cache_.Store(*requestKey, *finalKey, immutable);
    // Step 14a: 저장한 바로 그 불변 값을 돌려주고, 사실은 지금 문맥으로
    // 한 번씩만 진단이 된다.
    ReportFacts(facts_.Facts());
    return immutable;
}

}  // namespace

// ── Public surface ──────────────────────────────────────────────────────────
std::string OverlongTokenPolicy(TextWrapMode wrap, TextOverflowMode overflow) {
    // grapheme 줄바꿈은 과장된 토큰을 실제로 쪼갤 수 있으므로 다른 정책이다.
    if (wrap == TextWrapMode::Grapheme) return kOverlongTokenPolicyBreakGrapheme;
    switch (overflow) {
        case TextOverflowMode::Overflow: return kOverlongTokenPolicyOverflow;
        case TextOverflowMode::Clip:     return kOverlongTokenPolicyClip;
        case TextOverflowMode::Ellipsis: return kOverlongTokenPolicyEllipsis;
    }
    return kOverlongTokenPolicyOverflow;
}

TextLayoutService::TextLayoutService(FontFamilyResolver& resolver,
                                     TextShapingService& shaper,
                                     TextLayoutCache& cache)
    : resolver_(resolver), shaper_(shaper), cache_(cache) {}

std::optional<std::shared_ptr<const TextLayout>> TextLayoutService::Layout(
    const TextLayoutRequest& request, TextDiagnosticSink& sink) {
    LayoutRun run(resolver_, shaper_, cache_, request, sink);
    return run.Run();
}

namespace detail {

std::uint64_t LayoutIcuObjectCreationCount() noexcept {
    return g_layoutIcuObjectCreations;
}

void ResetLayoutIcuObjectCreationCount() noexcept {
    g_layoutIcuObjectCreations = 0;
}

std::uint32_t InteriorCaretLogicalBoundary(GraphemeRange glyphGraphemes,
                                           std::uint32_t visualIndex,
                                           bool rightToLeft) noexcept {
    // 시각 진행 순서의 index번째 자리는 LTR에서는 앞에서, RTL에서는 뒤에서 센
    // 논리 경계다. 방향을 무시하면 아랍어 합자의 caret이 반대쪽 글자를 가리키고,
    // 화면에는 그럴듯한 자리에 그려지므로 눈으로는 드러나지 않는다.
    return rightToLeft ? glyphGraphemes.end - visualIndex
                       : glyphGraphemes.begin + visualIndex;
}

bool AdjustedGdefCaretSetIsUsable(const std::vector<Fixed26_6>& offsets,
                                  std::uint32_t graphemes,
                                  Fixed26_6 advance) noexcept {
    // 정확히 N-1개여야 한다. ">=N-1"로 늦추면 caret 자리를 더 많이 발행한
    // 폰트에서 앞의 N-1개만 조용히 쓰이고, 나머지는 "이 집합은 이 cluster를
    // 설명하지 않는다"는 증거인데도 버려진다.
    if (graphemes < 2U) return false;
    if (offsets.size() != static_cast<std::size_t>(graphemes - 1U)) return false;
    std::int32_t previous = 0;
    for (const Fixed26_6 offset : offsets) {
        // 엄격히 증가하고, 0과 최종 advance 사이에 엄격히 들어 있어야 한다.
        // advance와 같은 자리는 caret이 아니라 다음 cluster의 시작이다.
        if (offset.Raw() <= previous || offset.Raw() >= advance.Raw()) {
            return false;
        }
        previous = offset.Raw();
    }
    return true;
}

bool EllipsisCandidateWasShapedWhole(
    const LayoutEllipsisCandidate& candidate) noexcept {
    // 남긴 글과 줄임표가 한 버퍼에 함께 있었고, 그 버퍼가 통째로 셰이핑됐고,
    // 줄임표가 실제로 들어 있었을 때만 "후보 하나를 통으로 셰이핑했다"이다.
    return candidate.ellipsisBytes > 0U &&
           candidate.candidateBufferBytes ==
               candidate.retainedBytes + candidate.ellipsisBytes &&
           candidate.shapedBytes == candidate.candidateBufferBytes;
}

const LayoutObservations& CurrentLayoutObservations() noexcept {
    return g_observations;
}

}  // namespace detail

}  // namespace molga::text
