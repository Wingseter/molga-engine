#include "Text/UnicodeAnalysis.h"

#include "Common/Sha256.h"
#include "Text/TextRuntimeDependencies.h"

#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/uloc.h>
#include <unicode/uscript.h>
#include <unicode/utypes.h>
#include <unicode/uversion.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <mutex>
#include <utility>

namespace molga::text {
namespace {

constexpr const char* kSubsystem = "text-analysis";

// 시도된 ICU 객체 생성 횟수. 성공 여부와 무관하게 호출 직전에 올린다. 사후에
// 세면 "준비되지 않았는데도 ubrk_open을 불렀지만 실패해서 흔적이 없다"는 정확히
// 가장 위험한 경우가 0으로 보인다.
std::uint64_t g_icuObjectCreations = 0;

// process 전역 generation 발급기.
//
// mutex는 동시 분석을 허용하려고 있는 것이 아니다(텍스트 계층은 단일 thread
// 계약이다). generation이 중복 발급되면 캐시가 서로 다른 분석을 같은 것으로
// 취급하는데, 그 실패는 어떤 렌더 결과로도 드러나지 않는다. 그 한 가지를 위해
// 발급 경로만 잠근다.
struct GenerationAllocator {
    std::mutex                       mutex;
    detail::AnalysisGenerationState  state;
};

GenerationAllocator& Generations() {
    static GenerationAllocator allocator;
    return allocator;
}

// 0으로 되돌아가지 않고, 같은 값을 두 번 주지 않는다. UINT64_MAX는 정확히 한 번
// 나가고 그 뒤로는 발급기가 잠긴다. wrap하면 아주 오래 산 process에서 오래된
// 캐시 항목이 새 분석과 같은 generation을 갖게 되는데, 그건 실패가 아니라
// 조용한 오답이다.
std::optional<std::uint64_t> ReserveGeneration() {
    GenerationAllocator&              allocator = Generations();
    const std::lock_guard<std::mutex> lock(allocator.mutex);
    if (allocator.state.exhausted) return std::nullopt;
    const std::uint64_t reserved = allocator.state.next;
    if (reserved == std::numeric_limits<std::uint64_t>::max()) {
        allocator.state.exhausted = true;
    } else {
        allocator.state.next = reserved + 1;
    }
    return reserved;
}

// 진단 상한: 아래 모든 보고 지점은 곧바로 nullopt를 반환한다. 따라서 Analyze
// 한 번이 남기는 진단은 최대 하나이고, grapheme/줄바꿈/run 개수에 비례하는
// 진단은 구조적으로 만들 수 없다(Task 3.2의 Utf8Invalid가 원본 byte range 때문에
// rate limit로도 줄지 않는다는 점이 이미 확인되었다).
void ReportOnce(TextDiagnosticSink& sink, TextDiagnosticCode code,
                std::string message, std::string remediation) {
    TextDiagnostic diagnostic;
    diagnostic.code        = code;
    diagnostic.severity    = TextSeverity::Error;
    diagnostic.subsystem   = kSubsystem;
    diagnostic.message     = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

// 저작 locale이나 패키징된 ICU 데이터로 고칠 수 있는 실패.
void ReportLayoutInvalid(TextDiagnosticSink& sink, std::string message) {
    ReportOnce(sink, TextDiagnosticCode::LayoutInvalid, std::move(message),
               "Unicode analysis has no fallback path: an ASCII or heuristic "
               "substitute would ship as text that renders but breaks lines "
               "wrongly. Fix the locale tag, or repair the packaged ICU data "
               "under Engine/Text.");
}

// 분석기 내부 불변식이 깨진 경우. 위 조치를 그대로 붙이면 운영자는 33MB짜리 ICU
// 데이터를 다시 스테이징하고 아무것도 배우지 못한다. 진단은 무엇을 하라는
// 말인지가 정확할 때만 값이 있다.
void ReportInternalInvariant(TextDiagnosticSink& sink, std::string message) {
    ReportOnce(sink, TextDiagnosticCode::LayoutInvalid, std::move(message),
               "This is an internal consistency failure in the analyzer, not a "
               "problem with the authored text or with the packaged ICU data: "
               "re-staging Engine/Text changes nothing. Restart the process, "
               "and report the message above as a text subsystem defect if it "
               "repeats.");
}

class ScopedBreakIterator {
public:
    explicit ScopedBreakIterator(UBreakIterator* value) noexcept
        : value_(value) {}
    ~ScopedBreakIterator() {
        if (value_ != nullptr) ubrk_close(value_);
    }
    ScopedBreakIterator(const ScopedBreakIterator&)            = delete;
    ScopedBreakIterator& operator=(const ScopedBreakIterator&) = delete;
    UBreakIterator*      Get() const noexcept { return value_; }

private:
    UBreakIterator* value_ = nullptr;
};

class ScopedBidi {
public:
    explicit ScopedBidi(UBiDi* value) noexcept : value_(value) {}
    ~ScopedBidi() {
        if (value_ != nullptr) ubidi_close(value_);
    }
    ScopedBidi(const ScopedBidi&)            = delete;
    ScopedBidi& operator=(const ScopedBidi&) = delete;
    UBiDi*      Get() const noexcept { return value_; }

private:
    UBiDi* value_ = nullptr;
};

// ubrk_first()/ubrk_next()가 내는 UTF-16 offset 전부. 빈 문자열이면 {0}이고,
// 그 밖에는 항상 0으로 시작해 텍스트 길이로 끝난다. hardOut이 주어지면 각
// 경계가 UAX#14의 강제 줄바꿈(명시적 구분자)인지 함께 담는다.
bool CollectBoundaries(UBreakIterator*             iterator,
                       std::vector<std::int32_t>&  out,
                       std::vector<char>*          hardOut) {
    out.clear();
    if (hardOut != nullptr) hardOut->clear();
    std::int32_t offset = ubrk_first(iterator);
    if (offset != 0) return false;
    while (offset != UBRK_DONE) {
        if (!out.empty() && offset <= out.back()) return false;
        out.push_back(offset);
        if (hardOut != nullptr) {
            const std::int32_t rule = ubrk_getRuleStatus(iterator);
            hardOut->push_back(static_cast<char>(
                rule >= UBRK_LINE_HARD && rule < UBRK_LINE_HARD_LIMIT));
        }
        offset = ubrk_next(iterator);
    }
    return !out.empty();
}

// Step 4b: 요청한 locale이 아니라 ICU가 실제로 고른 locale. ICU는 root를 빈
// 문자열로 돌려주는데, 빈 문자열을 그대로 기록하면 "locale을 못 받아왔다"와
// 구분되지 않으므로 root로 정규화한다.
std::optional<std::string> ResolvedActualLocale(UBreakIterator* iterator) {
    UErrorCode  status = U_ZERO_ERROR;
    const char* actual =
        ubrk_getLocaleByType(iterator, ULOC_ACTUAL_LOCALE, &status);
    if (U_FAILURE(status) || actual == nullptr) return std::nullopt;
    const std::string raw(actual);
    if (raw.empty() || raw == "root") return std::string("root");

    char       tag[ULOC_FULLNAME_CAPACITY] = {};
    UErrorCode tagStatus                   = U_ZERO_ERROR;
    const std::int32_t length              = uloc_toLanguageTag(
        raw.c_str(), tag, static_cast<std::int32_t>(sizeof(tag)),
        static_cast<UBool>(false), &tagStatus);
    if (U_FAILURE(tagStatus) || length <= 0 ||
        length >= static_cast<std::int32_t>(sizeof(tag))) {
        return std::nullopt;
    }
    return std::string(tag, static_cast<std::size_t>(length));
}

// Step 4c: 살아 있는 iterator가 실제로 들고 있는 컴파일된 break rule 바이트의
// SHA-256.
//
// 요청한 locale 문자열이나 ICU 버전만으로 규칙을 식별하면, 같은 문자열이 다른
// 데이터 파일에서 다른 경계를 내도 캐시가 조용히 적중한다. 그래서 실제 이진
// 규칙에 kind(character/line), 실제 locale, 그리고 규칙 해석을 좌우하는 플랫폼
// 3요소(ICU major, endianness, charset family)를 함께 묶는다. 이진 바이트 자체는
// UnicodeAnalysis 밖으로 절대 내보내지 않는다.
std::optional<std::string> BreakRuleIdentity(UBreakIterator*    iterator,
                                             const char*        kind,
                                             const std::string& resolvedLocale) {
    UErrorCode         status   = U_ZERO_ERROR;
    const std::int32_t required = ubrk_getBinaryRules(iterator, nullptr, 0,
                                                      &status);
    if (U_FAILURE(status) || required <= 0) return std::nullopt;

    std::vector<std::uint8_t> rules(static_cast<std::size_t>(required));
    status = U_ZERO_ERROR;
    const std::int32_t written =
        ubrk_getBinaryRules(iterator, rules.data(), required, &status);
    if (U_FAILURE(status) || written != required) return std::nullopt;

    std::string canonical("molga-icu-break-rule-v2");
    canonical.push_back('\0');
    canonical.append(kind);
    canonical.push_back('\0');
    canonical.append(resolvedLocale);
    canonical.push_back('\0');
    canonical.append(std::to_string(U_ICU_VERSION_MAJOR_NUM));
    canonical.push_back(':');
    canonical.append(std::to_string(U_IS_BIG_ENDIAN));
    canonical.push_back(':');
    canonical.append(std::to_string(U_CHARSET_FAMILY));
    canonical.push_back('\0');
    canonical.append(reinterpret_cast<const char*>(rules.data()), rules.size());
    return molga::Sha256Bytes(canonical.data(), canonical.size());
}

// UTF-16 경계 목록을 원본 byte offset으로 옮긴다. 부분 scalar를 가리키는
// 경계는 넓히지 않고 실패로 만든다(UnicodeTextBuffer의 계약).
bool MapBoundariesToSourceBytes(const UnicodeTextBuffer&         buffer,
                                const std::vector<std::int32_t>& units,
                                std::vector<std::uint32_t>&      out) {
    out.clear();
    out.reserve(units.size());
    for (const std::int32_t unit : units) {
        const auto position = static_cast<std::uint32_t>(unit);
        const auto mapped = buffer.SourceBytesForUtf16(Utf16Range{position,
                                                                  position});
        if (!mapped) return false;
        if (!out.empty() && mapped->begin <= out.back()) return false;
        out.push_back(mapped->begin);
    }
    return !out.empty() && out.front() == 0 &&
           out.back() == static_cast<std::uint32_t>(buffer.OriginalUtf8().size());
}

// character/line 두 iterator가 하는 일은 정확히 같다: 열고, 경계를 모으고, 원본
// byte로 옮기고, 실제 locale과 컴파일된 규칙 digest를 받는다. 이 여섯 단계를 두
// 번 손으로 적으면 한쪽만 고쳐지는 drift가 생기는데, 그 drift는 경계 값이 아니라
// ICU 호출 계약(버퍼 종료, 상태 검사) 쪽에서 먼저 나타난다.
struct BreakPassLabels {
    const char* kind;              // canonical stream과 진단에 그대로 들어간다
    const char* boundariesPlural;  // "grapheme boundaries" / "line boundaries"
    const char* boundarySingular;  // "a grapheme boundary" / "a line ..."
};

struct BreakPass {
    std::vector<std::int32_t>  units;
    std::vector<std::uint32_t> bytes;
    std::vector<char>          hard;
    std::string                resolvedLocale;
    std::string                ruleIdentity;
};

std::optional<BreakPass> RunBreakPass(UBreakIteratorType       type,
                                      const BreakPassLabels&   labels,
                                      const char*              canonicalLocale,
                                      const UnicodeTextBuffer& buffer,
                                      const std::u16string&    utf16,
                                      std::int32_t             utf16Length,
                                      bool                     collectHard,
                                      TextDiagnosticSink&      sink) {
    ++g_icuObjectCreations;
    UErrorCode                status = U_ZERO_ERROR;
    const ScopedBreakIterator iterator(
        ubrk_open(type, canonicalLocale, utf16.data(), utf16Length, &status));
    if (U_FAILURE(status) || iterator.Get() == nullptr) {
        ReportLayoutInvalid(sink, std::string("ICU could not open a ") +
                                      labels.kind + " break iterator");
        return std::nullopt;
    }

    BreakPass pass;
    if (!CollectBoundaries(iterator.Get(), pass.units,
                           collectHard ? &pass.hard : nullptr)) {
        ReportInternalInvariant(sink, std::string("ICU produced non-monotonic ") +
                                          labels.boundariesPlural);
        return std::nullopt;
    }
    if (!MapBoundariesToSourceBytes(buffer, pass.units, pass.bytes)) {
        // 이건 ICU 실패가 아니라 입력 매핑 실패다. 경계가 정확히 scalar 경계가
        // 아니었다는 뜻이므로 Utf8Invalid로 보고한다. 한 번 보고하고 즉시
        // 중단하므로 경계 수에 비례하는 진단은 나오지 않는다.
        ReportOnce(sink, TextDiagnosticCode::Utf8Invalid,
                   std::string(labels.boundarySingular) +
                       " did not land on an authored UTF-8 scalar boundary",
                   "Re-save the source text as valid UTF-8; the analyzer never "
                   "widens a boundary to cover a partial scalar.");
        return std::nullopt;
    }

    auto resolved = ResolvedActualLocale(iterator.Get());
    if (!resolved) {
        ReportLayoutInvalid(sink, std::string("ICU did not report an actual ") +
                                      labels.kind + " break locale");
        return std::nullopt;
    }
    pass.resolvedLocale = std::move(*resolved);

    auto rules = BreakRuleIdentity(iterator.Get(), labels.kind,
                                   pass.resolvedLocale);
    if (!rules) {
        ReportLayoutInvalid(sink, std::string("ICU did not expose compiled ") +
                                      labels.kind + " break rules");
        return std::nullopt;
    }
    pass.ruleIdentity = std::move(*rules);
    return pass;
}

// Step 6: common/inherited scalar는 주변 문맥으로 해석한다.
//
// 강한 script를 가진 scalar는 그대로 두고, common/inherited가 연속한 구간은 앞
// 쪽 script를 우선 상속한다. 다만 그 scalar의 Script_Extensions가 어느 쪽을
// 실제로 지목하면 그쪽을 먼저 고른다. U+0640 ARABIC TATWEEL이 대표적인데,
// Script는 Common이지만 Script_Extensions에 Arab이 들어 있어 앞뒤 어느 쪽이든
// 아랍 문맥에 붙어야 한다.
//
// 방향은 여기서 전혀 결정하지 않는다. script가 common이라는 이유로 중립 문자를
// LTR로 밀면 RTL 문단 안의 괄호/숫자가 통째로 뒤집힌다.
bool ResolveScripts(const std::vector<DecodedScalar>& scalars,
                    std::vector<std::int32_t>&        out) {
    const std::size_t count = scalars.size();
    out.assign(count, static_cast<std::int32_t>(USCRIPT_COMMON));
    std::vector<UScriptCode> raw(count, USCRIPT_COMMON);
    for (std::size_t index = 0; index < count; ++index) {
        UErrorCode status = U_ZERO_ERROR;
        raw[index] = uscript_getScript(
            static_cast<UChar32>(scalars[index].value), &status);
        if (U_FAILURE(status)) return false;
        out[index] = static_cast<std::int32_t>(raw[index]);
    }

    std::size_t index = 0;
    while (index < count) {
        if (raw[index] > USCRIPT_INHERITED) {
            ++index;
            continue;
        }
        // 이 구간은 최대이므로, 양 끝 바깥의 scalar는 반드시 강한 script다.
        std::size_t limit = index;
        while (limit < count && raw[limit] <= USCRIPT_INHERITED) ++limit;
        const bool        hasBefore = index > 0;
        const bool        hasAfter  = limit < count;
        const UScriptCode before =
            hasBefore ? raw[index - 1] : USCRIPT_INVALID_CODE;
        const UScriptCode after = hasAfter ? raw[limit] : USCRIPT_INVALID_CODE;
        for (std::size_t inner = index; inner < limit; ++inner) {
            const auto code = static_cast<UChar32>(scalars[inner].value);
            if (hasBefore && uscript_hasScript(code, before)) {
                out[inner] = static_cast<std::int32_t>(before);
            } else if (hasAfter && uscript_hasScript(code, after)) {
                out[inner] = static_cast<std::int32_t>(after);
            } else if (hasBefore) {
                out[inner] = static_cast<std::int32_t>(before);
            } else if (hasAfter) {
                out[inner] = static_cast<std::int32_t>(after);
            }
            // 양쪽 모두 없으면 문서 전체가 common/inherited다. raw 그대로 둔다.
        }
        index = limit;
    }
    return true;
}

// UBA의 명시적 서식/격리 문자. item 경계를 여기서 반드시 끊는 이유는, 이들이
// 자기 뒤 텍스트의 방향을 바꾸는 표시이지 그려질 내용이 아니기 때문이다. 같은
// item 안에 묶이면 shaper가 그 표시까지 글리프로 만들려 든다.
bool IsBidiControl(char32_t value) {
    switch (u_charDirection(static_cast<UChar32>(value))) {
        case U_LEFT_TO_RIGHT_EMBEDDING:
        case U_RIGHT_TO_LEFT_EMBEDDING:
        case U_LEFT_TO_RIGHT_OVERRIDE:
        case U_RIGHT_TO_LEFT_OVERRIDE:
        case U_POP_DIRECTIONAL_FORMAT:
        case U_LEFT_TO_RIGHT_ISOLATE:
        case U_RIGHT_TO_LEFT_ISOLATE:
        case U_FIRST_STRONG_ISOLATE:
        case U_POP_DIRECTIONAL_ISOLATE:
            return true;
        default:
            return false;
    }
}

// Bidi_Class B. CRLF은 하나의 grapheme이고 그 마지막 scalar가 LF이므로,
// 문단 끝 판정을 grapheme 단위로 하면 CR과 LF 사이가 문단 경계가 되는 사고를
// 구조적으로 못 만든다.
bool IsParagraphSeparator(char32_t value) {
    return u_charDirection(static_cast<UChar32>(value)) == U_BLOCK_SEPARATOR;
}

UBiDiLevel RequestedParagraphLevel(BaseDirection direction) {
    switch (direction) {
        case BaseDirection::LeftToRight:
            return 0;
        case BaseDirection::RightToLeft:
            return 1;
        case BaseDirection::Auto:
            break;
    }
    return UBIDI_DEFAULT_LTR;
}

struct ParagraphSpan {
    std::uint32_t firstGrapheme = 0;
    std::uint32_t graphemeLimit = 0;
};

}  // namespace

namespace detail {

std::uint64_t IcuObjectCreationCount() noexcept { return g_icuObjectCreations; }

void ResetIcuObjectCreationCount() noexcept { g_icuObjectCreations = 0; }

AnalysisGenerationState ExchangeAnalysisGenerationState(
    AnalysisGenerationState next) noexcept {
    GenerationAllocator&              allocator = Generations();
    const std::lock_guard<std::mutex> lock(allocator.mutex);
    const AnalysisGenerationState     previous = allocator.state;
    allocator.state                            = next;
    return previous;
}

}  // namespace detail

const std::vector<std::uint32_t>& UnicodeAnalysis::GraphemeBoundaries()
    const noexcept {
    return graphemeBoundaries_;
}

const std::vector<std::uint32_t>& UnicodeAnalysis::LineBreakBoundaries()
    const noexcept {
    return lineBreakBoundaries_;
}

const std::vector<AnalysisItem>& UnicodeAnalysis::Items() const noexcept {
    return items_;
}

const UnicodeAnalysisIdentity& UnicodeAnalysis::Identity() const noexcept {
    return identity_;
}

std::optional<UnicodeAnalysis> UnicodeTextAnalyzer::Analyze(
    const UnicodeTextBuffer& buffer, const TextAnalysisOptions& options,
    TextDiagnosticSink& sink) {
    // Step 4. ICU 객체를 하나라도 만들기 전에 준비 상태를 먼저 본다. 준비되지
    // 않은 process에서 ubrk_open을 부르면 실패하는 것이 아니라 데이터 없는
    // fallback으로 "성공"할 수 있고, 그러면 저작자는 틀린 경계를 보게 된다.
    if (!TextRuntimeDependencies::Get().IsReady()) {
        ReportOnce(sink, TextDiagnosticCode::DependencyInvalid,
                   "Unicode analysis requires a ready text runtime; ICU has "
                   "not been initialized in this process",
                   "Create a TextRuntimeLifetimeGuard from a verified "
                   "Engine/Text root before analyzing text, and keep it alive "
                   "for as long as any text service exists.");
        return std::nullopt;
    }
    // 분석이 진행되는 동안 수명을 붙잡는다. Shutdown은 client handle이 전부
    // 사라진 뒤에만 진행되므로, 이 lease가 살아 있는 한 아래 ICU 객체들은
    // u_cleanup 이후를 볼 수 없다.
    const auto lease = TextRuntimeClientHandle::Acquire();
    if (!lease) {
        ReportOnce(sink, TextDiagnosticCode::DependencyInvalid,
                   "Unicode analysis could not acquire a text runtime client "
                   "handle",
                   "Analyze text only while the process text runtime is ready; "
                   "a terminally cleaned runtime never becomes ready again.");
        return std::nullopt;
    }

    // Step 9a. ICU를 만들기 전에 generation을 먼저 예약한다. 뒤에서 예약하면
    // 소진 상태에서도 iterator를 열었다 버리게 된다.
    const auto generation = ReserveGeneration();
    if (!generation) {
        ReportOnce(sink, TextDiagnosticCode::LayoutInvalid,
                   "this process has exhausted its analysis generations and "
                   "cannot produce another analysis",
                   "Restart the process. The allocator never resets and never "
                   "reuses a value, because two different analyses sharing a "
                   "generation would let a cache treat them as the same one.");
        return std::nullopt;
    }

    // 빈 태그는 "root를 원한다"가 아니라 "locale을 채우지 않았다"이다. 빈
    // 문자열은 parsedLength==size를 그냥 통과하므로 여기서 따로 막지 않으면
    // 기본값을 지운 호출자만 조용히 root tailoring을 받는다. 같은 관문이
    // "en_US"나 "C"를 거절하는 이유와 정확히 같다.
    if (options.locale.empty()) {
        ReportLayoutInvalid(sink,
                            "an empty locale tag names no tailoring; use "
                            "\"und\" to ask for root");
        return std::nullopt;
    }

    // Step 9. 저작 locale을 ICU 표준형으로 옮기고, 전체가 소비되었는지 본다.
    // 부분 파싱을 허용하면 "en--US"가 조용히 "en"이 되어 저작자가 고른 적 없는
    // tailoring으로 줄바꿈이 결정된다.
    //
    // 반환된 길이도 함께 본다. 정규형이 버퍼 용량과 정확히 같으면 ICU는
    // U_STRING_NOT_TERMINATED_WARNING만 남기는데, 그것은 WARNING이라
    // U_FAILURE가 false이고 parsedLength도 전체 길이 그대로다. 길이를 보지
    // 않으면 NUL 없는 char 배열이 그대로 ubrk_open으로 넘어가 strlen이 스택
    // 밖을 읽고, 그런데도 분석은 "성공"한다. fail-open은 이 계층이 유일하게
    // 허용하지 않는 결말이다.
    char               canonicalLocale[ULOC_FULLNAME_CAPACITY] = {};
    UErrorCode         status                                  = U_ZERO_ERROR;
    std::int32_t       parsedLength                            = 0;
    const std::int32_t canonicalLength = uloc_forLanguageTag(
        options.locale.c_str(), canonicalLocale,
        static_cast<std::int32_t>(sizeof(canonicalLocale)), &parsedLength,
        &status);
    if (U_FAILURE(status) || parsedLength < 0 ||
        static_cast<std::size_t>(parsedLength) != options.locale.size()) {
        ReportLayoutInvalid(sink, "'" + options.locale +
                                      "' is not a complete BCP-47 locale tag");
        return std::nullopt;
    }
    // 길이 0은 정상이다: ICU의 root locale id는 빈 문자열이고 "und"가 바로
    // 거기로 정규화된다. 거절해야 하는 것은 용량을 꽉 채운 경우뿐이다.
    if (canonicalLength < 0 ||
        canonicalLength >= static_cast<std::int32_t>(sizeof(canonicalLocale))) {
        ReportLayoutInvalid(sink,
                            "'" + options.locale +
                                "' canonicalizes to a locale id ICU cannot "
                                "terminate inside its own name buffer");
        return std::nullopt;
    }

    const std::u16string& utf16 = buffer.SanitizedUtf16();
    if (utf16.size() > static_cast<std::size_t>(
                           std::numeric_limits<std::int32_t>::max())) {
        ReportOnce(sink, TextDiagnosticCode::LayoutInvalid,
                   "text is longer than ICU's 32-bit unit index",
                   "Analyze this text in pieces of fewer than 2^31 UTF-16 "
                   "units; ICU indexes boundaries with a signed 32-bit "
                   "integer and cannot describe a longer run.");
        return std::nullopt;
    }
    const auto utf16Length = static_cast<std::int32_t>(utf16.size());

    // Step 4a. 확장 grapheme 경계.
    auto character = RunBreakPass(UBRK_CHARACTER,
                                  {"character", "grapheme boundaries",
                                   "a grapheme boundary"},
                                  canonicalLocale, buffer, utf16, utf16Length,
                                  false, sink);
    if (!character) return std::nullopt;
    const std::vector<std::int32_t>& graphemeUnits = character->units;
    std::vector<std::uint32_t>&      graphemeBytes = character->bytes;

    // Step 5. 줄바꿈 기회. Thai 사전 분해와 CJK 금칙은 전부 ICU가 낸 것만 쓰고,
    // 여기서 문자별 규칙을 덧붙이지 않는다.
    auto line = RunBreakPass(UBRK_LINE,
                             {"line", "line boundaries",
                              "a line break opportunity"},
                             canonicalLocale, buffer, utf16, utf16Length, true,
                             sink);
    if (!line) return std::nullopt;

    // grapheme 경계에 맞지 않는 기회는 버린다. 클러스터 한가운데를 자르면
    // 결합 문자만 다음 줄로 넘어간다.
    //
    // 두 번째 조건은 Step 5가 요구하는 "명시적 구분자 보존"이다. 현재 ICU
    // 78에서는 한 번도 발동하지 않는다: UAX#14의 강제 줄바꿈은 언제나 BK/CR/
    // LF/NL 뒤에 오고 그 넷은 전부 GCB Control/CR/LF라 반드시 cluster 경계이기
    // 때문이다. 그래도 남겨 두는 이유는, 만약 언젠가 발동한다면 그건 "저작자가
    // 적은 줄바꿈"과 "cluster를 쪼개지 않는다"가 충돌한다는 뜻이고, 그때는
    // 저작 내용 쪽이 이긴다는 것을 여기에 적어 두기 위해서다. 그 경우에만
    // 헤더가 말하는 부분집합 성질이 깨지며, 그 사실은 감춰지지 않아야 한다.
    std::vector<std::uint32_t> lineBytes;
    lineBytes.reserve(line->bytes.size());
    for (std::size_t index = 0; index < line->bytes.size(); ++index) {
        const bool aligned = std::binary_search(
            graphemeBytes.begin(), graphemeBytes.end(), line->bytes[index]);
        if (aligned || line->hard[index] != 0) {
            lineBytes.push_back(line->bytes[index]);
        }
    }

    const std::vector<DecodedScalar>& scalars = buffer.Scalars();
    std::vector<std::int32_t>         scalarScripts;
    if (!ResolveScripts(scalars, scalarScripts)) {
        ReportLayoutInvalid(sink, "ICU could not resolve a scalar script");
        return std::nullopt;
    }

    const auto graphemeCount =
        static_cast<std::uint32_t>(graphemeBytes.size() - 1);

    // grapheme -> 그 안의 첫 scalar. grapheme 경계는 scalar 경계의 부분집합이라
    // 정확히 일치하는 scalar가 반드시 있다. 없으면 두 표가 어긋난 것이므로
    // 추정하지 않고 실패한다.
    std::vector<std::uint32_t> graphemeScalarStart(
        static_cast<std::size_t>(graphemeCount) + 1, 0);
    {
        std::size_t scalar = 0;
        for (std::uint32_t index = 0; index <= graphemeCount; ++index) {
            while (scalar < scalars.size() &&
                   scalars[scalar].sourceBytes.begin < graphemeBytes[index]) {
                ++scalar;
            }
            const bool atEnd = scalar == scalars.size();
            if (!atEnd &&
                scalars[scalar].sourceBytes.begin != graphemeBytes[index]) {
                ReportInternalInvariant(
                    sink, "a grapheme boundary fell inside a decoded scalar");
                return std::nullopt;
            }
            graphemeScalarStart[index] = static_cast<std::uint32_t>(scalar);
        }
    }

    // Step 7 준비: 명시적 문단. 구분자는 자기 앞 문단에 속한다(UBA P1).
    std::vector<ParagraphSpan> paragraphs;
    {
        std::uint32_t start = 0;
        for (std::uint32_t index = 0; index < graphemeCount; ++index) {
            const std::uint32_t lastScalar = graphemeScalarStart[index + 1] - 1;
            if (!IsParagraphSeparator(scalars[lastScalar].value)) continue;
            paragraphs.push_back({start, index + 1});
            start = index + 1;
        }
        if (start < graphemeCount) paragraphs.push_back({start, graphemeCount});
    }

    std::vector<std::uint8_t>  levelByGrapheme(graphemeCount, 0);
    std::vector<std::uint32_t> runByGrapheme(graphemeCount, 0);
    std::vector<std::int32_t>  scriptByGrapheme(graphemeCount, 0);
    std::vector<char>          controlByGrapheme(graphemeCount, 0);
    for (std::uint32_t index = 0; index < graphemeCount; ++index) {
        const std::uint32_t scalar = graphemeScalarStart[index];
        scriptByGrapheme[index]    = scalarScripts[scalar];
        controlByGrapheme[index] =
            static_cast<char>(IsBidiControl(scalars[scalar].value));
    }

    // Step 7. 문단마다 따로 UBiDi를 돌린다. 한 번에 넘기면 ICU가 문단을 스스로
    // 나누기는 하지만 base direction 요청이 문단별로 다시 적용되는지가 호출자
    // 눈에 보이지 않는다. 여기서는 요청한 Auto/LTR/RTL이 문단마다 그대로
    // 적용된다는 것이 코드에 드러나야 한다.
    const UBiDiLevel requestedLevel =
        RequestedParagraphLevel(options.baseDirection);
    std::uint32_t nextRunId = 0;
    if (!paragraphs.empty()) {
        // UBiDi 하나를 문단마다 다시 setPara 한다. 문단 수만큼 할당/해제를
        // 반복할 이유가 없고(장문 문서에서는 그것만으로 수천 번이다), 문단마다
        // 따로 돈다는 사실은 루프 구조로 이미 드러난다.
        ++g_icuObjectCreations;
        const ScopedBidi bidi(ubidi_open());
        if (bidi.Get() == nullptr) {
            ReportInternalInvariant(sink,
                                    "ICU could not open a BiDi paragraph");
            return std::nullopt;
        }
        for (const ParagraphSpan& paragraph : paragraphs) {
            const std::int32_t unitBegin =
                graphemeUnits[paragraph.firstGrapheme];
            const std::int32_t unitEnd = graphemeUnits[paragraph.graphemeLimit];
            // ubidi_setPara는 끝 offset이 아니라 길이를 받는다. 두 값을 따로
            // 이름 붙여 두는 이유가 그것이다.
            const std::int32_t  unitCount     = unitEnd - unitBegin;
            const char16_t*     paragraphText = utf16.data() + unitBegin;

            status = U_ZERO_ERROR;
            ubidi_setPara(bidi.Get(), paragraphText, unitCount, requestedLevel,
                          nullptr, &status);
            if (U_FAILURE(status)) {
                ReportLayoutInvalid(sink, "ICU could not resolve BiDi levels");
                return std::nullopt;
            }
            status                   = U_ZERO_ERROR;
            const UBiDiLevel* levels = ubidi_getLevels(bidi.Get(), &status);
            if (U_FAILURE(status) || levels == nullptr) {
                ReportInternalInvariant(sink, "ICU reported no BiDi levels");
                return std::nullopt;
            }
            // levels는 이 문단 슬라이스 기준이므로 색인도 문단 기준이어야 한다.
            for (std::uint32_t index = paragraph.firstGrapheme;
                 index < paragraph.graphemeLimit; ++index) {
                levelByGrapheme[index] = static_cast<std::uint8_t>(
                    levels[graphemeUnits[index] - unitBegin]);
            }

            // logical run 번호는 전체 분석에서 논리 순서대로 단조 증가한다.
            // 문단마다 0으로 되돌리면 서로 다른 run이 같은 번호를 갖게 되는데,
            // 경계와 level만 보는 어떤 검사도 그것을 볼 수 없고 run id로 키를
            // 만든 캐시는 조용히 다른 run을 돌려준다. 같은 방향 parity라도
            // level이 다르면 다른 run이라는 사실도 여기서 보존된다.
            std::int32_t  runStart = 0;
            std::uint32_t cursor   = paragraph.firstGrapheme;
            while (runStart < unitCount) {
                std::int32_t runLimit = 0;
                UBiDiLevel   runLevel = 0;
                ubidi_getLogicalRun(bidi.Get(), runStart, &runLimit, &runLevel);
                if (runLimit <= runStart) {
                    ReportInternalInvariant(
                        sink, "ICU reported an empty BiDi logical run");
                    return std::nullopt;
                }
                while (cursor < paragraph.graphemeLimit &&
                       graphemeUnits[cursor] - unitBegin < runLimit) {
                    runByGrapheme[cursor] = nextRunId;
                    ++cursor;
                }
                runStart = runLimit;
                ++nextRunId;
            }
            if (cursor != paragraph.graphemeLimit) {
                ReportInternalInvariant(
                    sink, "ICU BiDi runs did not cover the whole paragraph");
                return std::nullopt;
            }
        }
    }

    // Step 8. 문단 / logical run / 정확한 level / 해석된 script / isolate·control
    // 경계에서 자른다. 언어·스타일 경계는 하나의 Analyze가 하나의 locale만
    // 받으므로 이 계층에서는 나타나지 않는다.
    UnicodeAnalysis analysis;
    // item 하나당 grapheme 하나는 script/level/control이 매 cluster마다 바뀌는
    // 문서에서만 나오는 최악의 경우다. 1MB 문서에 그만큼을 미리 잡으면 실제로
    // 쓰이지 않을 수십 MB를 먼저 요청하게 된다. 확실한 하한만 잡고 나머지는
    // vector의 증가에 맡긴다.
    analysis.items_.reserve(paragraphs.size());
    for (const ParagraphSpan& paragraph : paragraphs) {
        const std::size_t firstItem = analysis.items_.size();
        std::uint32_t     itemStart = paragraph.firstGrapheme;
        for (std::uint32_t index = paragraph.firstGrapheme;
             index < paragraph.graphemeLimit; ++index) {
            // 문단의 마지막 grapheme에서는 index + 1이 이 문단 밖이다. 그
            // 사실을 short-circuit 순서에 맡기면, 조건 하나를 재배열하는 것만으로
            // 범위 밖 읽기가 되고 sanitizer 없이는 결과가 그대로 그럴듯하다.
            // 분기를 나눠 구조적으로 불가능하게 만든다.
            const bool last = index + 1 == paragraph.graphemeLimit;
            bool       split = true;
            if (!last) {
                split = controlByGrapheme[index] != 0 ||
                        controlByGrapheme[index + 1] != 0 ||
                        levelByGrapheme[index] != levelByGrapheme[index + 1] ||
                        runByGrapheme[index] != runByGrapheme[index + 1] ||
                        scriptByGrapheme[index] != scriptByGrapheme[index + 1];
            }
            if (!split) continue;

            AnalysisItem item;
            item.graphemes      = GraphemeRange{itemStart, index + 1};
            item.sourceBytes    = SourceByteRange{graphemeBytes[itemStart],
                                               graphemeBytes[index + 1]};
            item.utf16Units     = Utf16Range{
                static_cast<std::uint32_t>(graphemeUnits[itemStart]),
                static_cast<std::uint32_t>(graphemeUnits[index + 1])};
            item.scriptCode     = scriptByGrapheme[index];
            item.embeddingLevel = levelByGrapheme[index];
            item.logicalRunId   = runByGrapheme[index];
            analysis.items_.push_back(item);
            itemStart = index + 1;
        }
        if (analysis.items_.size() > firstItem) {
            analysis.items_[firstItem].paragraphStart = true;
            analysis.items_.back().paragraphEnd       = true;
        }
    }

    // Step 9b. 두 실제 locale과 두 규칙 digest가 모두 유효해진 뒤에야 identity를
    // 만들고, 경계/item과 함께 한 번에 옮긴다. 접근자가 나중에 ICU를 다시 부르는
    // 일은 없다.
    analysis.graphemeBoundaries_               = std::move(graphemeBytes);
    analysis.lineBreakBoundaries_              = std::move(lineBytes);
    analysis.identity_.resolvedGraphemeLocale  = std::move(character->resolvedLocale);
    analysis.identity_.resolvedLineBreakLocale = std::move(line->resolvedLocale);
    analysis.identity_.graphemeRuleIdentity    = std::move(character->ruleIdentity);
    analysis.identity_.lineBreakRuleIdentity   = std::move(line->ruleIdentity);
    analysis.identity_.analysisGeneration      = *generation;
    return analysis;
}

}  // namespace molga::text
