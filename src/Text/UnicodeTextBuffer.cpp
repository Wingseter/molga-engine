#include "Text/UnicodeTextBuffer.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <utility>

namespace molga::text {
namespace {

constexpr const char* kSubsystem = "text-unicode";
constexpr char32_t kReplacementCharacter = U'\uFFFD';

// UTF-8 lead byte 분류. 두 번째 byte의 허용 범위를 lead마다 따로 들고 있는 이유는
// overlong(C0/C1, E0 80.., F0 80..), surrogate(ED A0..), 범위 초과(F4 90.., F5..FF)를
// "일단 3~4 byte를 모아 디코드한 뒤 값으로 거르기"가 아니라 애초에 sequence로
// 인정하지 않기 위해서다. 값으로 거르면 잘못된 3~4 byte가 통째로 하나의 U+FFFD가
// 되어 원본 maximal subpart 경계가 뭉개지고, 그 뭉개진 range가 그대로 진단과
// cluster 매핑에 흘러간다.
struct LeadClass {
    int length = 0; // 0이면 lead가 될 수 없는 byte(연속 byte이거나 금지된 lead).
    unsigned char secondLow = 0x80;
    unsigned char secondHigh = 0xBF;
    char32_t initialValue = 0;
};

LeadClass ClassifyLead(unsigned char lead) noexcept {
    if (lead <= 0x7F) {
        return {1, 0x80, 0xBF, static_cast<char32_t>(lead)};
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return {2, 0x80, 0xBF, static_cast<char32_t>(lead & 0x1FU)};
    }
    if (lead == 0xE0) { // E0 80..9F는 overlong.
        return {3, 0xA0, 0xBF, static_cast<char32_t>(lead & 0x0FU)};
    }
    if (lead >= 0xE1 && lead <= 0xEC) {
        return {3, 0x80, 0xBF, static_cast<char32_t>(lead & 0x0FU)};
    }
    if (lead == 0xED) { // ED A0..BF는 surrogate.
        return {3, 0x80, 0x9F, static_cast<char32_t>(lead & 0x0FU)};
    }
    if (lead >= 0xEE && lead <= 0xEF) {
        return {3, 0x80, 0xBF, static_cast<char32_t>(lead & 0x0FU)};
    }
    if (lead == 0xF0) { // F0 80..8F는 overlong.
        return {4, 0x90, 0xBF, static_cast<char32_t>(lead & 0x07U)};
    }
    if (lead >= 0xF1 && lead <= 0xF3) {
        return {4, 0x80, 0xBF, static_cast<char32_t>(lead & 0x07U)};
    }
    if (lead == 0xF4) { // F4 90..BF는 U+10FFFF 초과.
        return {4, 0x80, 0x8F, static_cast<char32_t>(lead & 0x07U)};
    }
    return {0, 0x80, 0xBF, 0};
}

void AppendUtf16(char32_t value, std::u16string& out) {
    if (value < 0x10000U) {
        out.push_back(static_cast<char16_t>(value));
        return;
    }
    const char32_t offset = value - 0x10000U;
    out.push_back(static_cast<char16_t>(0xD800U + (offset >> 10)));
    out.push_back(static_cast<char16_t>(0xDC00U + (offset & 0x3FFU)));
}

void ReportUtf8Invalid(TextDiagnosticSink& sink, SourceByteRange range) {
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::Utf8Invalid;
    // Blocker가 아니라 Error다. 같은 ill-formed byte가 저작 asset에서는 차단이고
    // script가 만든 dynamic string에서는 계속 실행이어야 하는데(설계 실패 행렬),
    // 이 buffer는 둘을 구분할 정보를 갖고 있지 않다. 사실만 보고하고 승격은
    // 문맥을 아는 호출자 정책에 맡긴다.
    diagnostic.severity = TextSeverity::Error;
    diagnostic.subsystem = kSubsystem;
    diagnostic.message = "Ill-formed UTF-8 at original bytes [" +
        std::to_string(range.begin) + ", " + std::to_string(range.end) +
        ") displayed as U+FFFD";
    diagnostic.remediation =
        "Re-save the source text as valid UTF-8. The original bytes are kept "
        "unchanged, so the reported range indexes the authored file directly.";
    diagnostic.sourceByteRange = range;
    sink.Report(std::move(diagnostic));
}

// UTF-16 offset -> 원본 byte offset. 정확한 scalar 경계만 통과시킨다.
std::optional<std::uint32_t> ByteOffsetAtUtf16Boundary(
    const std::vector<SourceByteRange>& utf16SourceRanges,
    std::size_t originalByteCount, std::uint32_t unit) {
    const std::size_t unitCount = utf16SourceRanges.size();
    if (unit == unitCount) return static_cast<std::uint32_t>(originalByteCount);
    if (unit > unitCount) return std::nullopt;
    // surrogate pair의 뒤쪽 unit은 앞 unit과 같은 scalar에 속한다. 모든 scalar가
    // 최소 1 byte를 소비하므로 서로 다른 scalar의 begin이 같아지는 일은 없고,
    // 따라서 이 비교는 정확히 "pair 내부"만 걸러낸다.
    if (unit > 0 && utf16SourceRanges[unit].begin ==
                        utf16SourceRanges[unit - 1].begin) {
        return std::nullopt;
    }
    return utf16SourceRanges[unit].begin;
}

// 원본 byte offset -> UTF-16 offset. 위와 대칭이며, multibyte scalar 내부를
// 가리키는 offset은 넓히지 않고 거절한다.
std::optional<std::uint32_t> Utf16OffsetAtByteBoundary(
    const std::vector<DecodedScalar>& scalars, std::size_t originalByteCount,
    std::size_t utf16UnitCount, std::uint32_t offset) {
    if (offset == originalByteCount) {
        return static_cast<std::uint32_t>(utf16UnitCount);
    }
    if (offset > originalByteCount) return std::nullopt;
    // 여기 도달했다면 offset < originalByteCount이므로 scalars는 비어 있지 않고,
    // 이 offset을 반드시 어떤 scalar가 품고 있다(scalar들이 원본을 빈틈없이 덮는다).
    // 그래서 "offset보다 뒤에서 시작하는 첫 scalar"의 하나 앞이 곧 그 scalar이고,
    // scalars[0].begin == 0 <= offset이므로 뒤로 물러날 자리는 항상 존재한다.
    //
    // lower_bound로 쓰면 offset이 마지막 scalar 내부일 때만 end()가 나오는데, 그
    // 분기는 문자열이 ASCII로 끝나기만 하면 도달하지 않는다. 즉 경계 검사를
    // 빠뜨려도 sanitizer 없이는 드러나지 않는 past-the-end 읽기가 된다. 검사를
    // 테스트로 지키는 대신 검사가 필요 없는 형태로 쓴다.
    const auto after = std::upper_bound(
        scalars.begin(), scalars.end(), offset,
        [](std::uint32_t target, const DecodedScalar& scalar) {
            return target < scalar.sourceBytes.begin;
        });
    const DecodedScalar& containing = *std::prev(after);
    // begin이 정확히 일치할 때만 경계다. 내부를 가리키면 넓히지 않고 거절한다.
    if (containing.sourceBytes.begin != offset) return std::nullopt;
    return containing.utf16Units.begin;
}

} // namespace

std::optional<UnicodeTextBuffer> UnicodeTextBuffer::Build(
    std::string originalUtf8, TextDiagnosticSink& sink) {
    // 공개 offset이 전부 uint32_t이므로 담을 수 없는 입력은 계산을 시작조차 하지
    // 않는다. scalar 수와 UTF-16 unit 수는 각각 byte 수를 넘을 수 없으므로
    // (모든 scalar는 최소 1 byte, 2 unit짜리 scalar는 4 byte) 이 한 번의 검사가
    // 세 카운트를 모두 덮는다.
    if (originalUtf8.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    UnicodeTextBuffer buffer;
    buffer.originalUtf8_ = std::move(originalUtf8);
    // 아래 루프는 이 문자열을 읽기만 한다. 치환은 sanitizedUtf16_에서만 일어난다.
    const std::string& bytes = buffer.originalUtf8_;

    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        const LeadClass lead =
            ClassifyLead(static_cast<unsigned char>(bytes[cursor]));

        // consumed는 성공 시 sequence 길이, 실패 시 Unicode maximal subpart 길이가
        // 된다. 즉 깨진 sequence의 "유효한 접두부"만 삼키고, 그것을 깨뜨린 byte는
        // 다음 반복이 새 lead로 다시 판정한다.
        std::size_t consumed = 1;
        char32_t value = lead.initialValue;
        bool wellFormed = lead.length > 0;
        while (wellFormed && consumed < static_cast<std::size_t>(lead.length)) {
            const std::size_t position = cursor + consumed;
            if (position >= bytes.size()) {
                wellFormed = false; // 입력 끝에서 잘린 sequence.
                break;
            }
            const auto continuation = static_cast<unsigned char>(bytes[position]);
            // 두 번째 byte만 lead별 창을 쓰고, 세 번째부터는 일반 continuation
            // 범위다. 삼항의 공통 타입이 int로 승격되므로 명시적으로 좁힌다
            // (MSVC /W4의 C4244 소음 방지).
            const unsigned char low =
                consumed == 1 ? lead.secondLow : static_cast<unsigned char>(0x80);
            const unsigned char high =
                consumed == 1 ? lead.secondHigh : static_cast<unsigned char>(0xBF);
            if (continuation < low || continuation > high) {
                wellFormed = false;
                break;
            }
            value = (value << 6) | static_cast<char32_t>(continuation & 0x3FU);
            ++consumed;
        }
        if (!wellFormed) value = kReplacementCharacter;

        DecodedScalar scalar;
        scalar.value = value;
        scalar.scalarIndex = static_cast<std::uint32_t>(buffer.scalars_.size());
        scalar.sourceBytes.begin = static_cast<std::uint32_t>(cursor);
        scalar.sourceBytes.end = static_cast<std::uint32_t>(cursor + consumed);
        scalar.utf16Units.begin =
            static_cast<std::uint32_t>(buffer.sanitizedUtf16_.size());
        AppendUtf16(value, buffer.sanitizedUtf16_);
        scalar.utf16Units.end =
            static_cast<std::uint32_t>(buffer.sanitizedUtf16_.size());
        buffer.utf16SourceRanges_.insert(
            buffer.utf16SourceRanges_.end(),
            scalar.utf16Units.end - scalar.utf16Units.begin,
            scalar.sourceBytes);
        buffer.scalars_.push_back(scalar);

        if (!wellFormed) {
            buffer.hadDecodeErrors_ = true;
            // maximal subpart 하나당 정확히 하나의 진단. 여기서 상한을 두지 않는
            // 것은 각 진단이 서로 다른 원본 byte range를 가리켜야 하기 때문이다.
            //
            // 주의: 그 범위를 LoggerTextDiagnosticSink가 줄여줄 것이라고 기대하면
            // 안 된다. TextDiagnosticRateLimitKey가 sourceByteRange를 key에
            // 넣는데 subpart마다 range가 다르므로 key가 절대 충돌하지 않고, 따라서
            // 억제되는 진단이 하나도 없다. UTF-8로 잘못 읽힌 1 MB Latin-1 파일은
            // 진단 1M개가 된다(흔한 저작 실수다). 상한은 문맥을 아는 호출자
            // (editor import, package-time scan)가 두어야 한다.
            ReportUtf8Invalid(sink, scalar.sourceBytes);
        }
        cursor += consumed;
    }

    return buffer;
}

const std::string& UnicodeTextBuffer::OriginalUtf8() const noexcept {
    return originalUtf8_;
}

const std::u16string& UnicodeTextBuffer::SanitizedUtf16() const noexcept {
    return sanitizedUtf16_;
}

const std::vector<DecodedScalar>& UnicodeTextBuffer::Scalars() const noexcept {
    return scalars_;
}

std::optional<SourceByteRange> UnicodeTextBuffer::SourceBytesForUtf16(
    Utf16Range range) const {
    if (range.begin > range.end) return std::nullopt;
    const auto begin = ByteOffsetAtUtf16Boundary(
        utf16SourceRanges_, originalUtf8_.size(), range.begin);
    if (!begin) return std::nullopt;
    const auto end = ByteOffsetAtUtf16Boundary(
        utf16SourceRanges_, originalUtf8_.size(), range.end);
    if (!end) return std::nullopt;
    return SourceByteRange{*begin, *end};
}

std::optional<Utf16Range> UnicodeTextBuffer::Utf16ForSourceBytes(
    SourceByteRange range) const {
    if (range.begin > range.end) return std::nullopt;
    const auto begin = Utf16OffsetAtByteBoundary(
        scalars_, originalUtf8_.size(), sanitizedUtf16_.size(), range.begin);
    if (!begin) return std::nullopt;
    const auto end = Utf16OffsetAtByteBoundary(
        scalars_, originalUtf8_.size(), sanitizedUtf16_.size(), range.end);
    if (!end) return std::nullopt;
    return Utf16Range{*begin, *end};
}

bool UnicodeTextBuffer::HadDecodeErrors() const noexcept {
    return hadDecodeErrors_;
}

} // namespace molga::text
