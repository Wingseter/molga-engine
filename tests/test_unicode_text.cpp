#include "Common/Fixed26_6.h"
#include "Rendering/Utf8.h"
#include "Text/TextDiagnostic.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"
#include "UnicodeTextAnalyzerTestAccess.h"

#include "doctest.h"

#include <unicode/uscript.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

TEST_CASE("Fixed26_6 rejects invalid input and rounds exact ties away from zero") {
    using molga::Fixed26_6;
    CHECK_FALSE(Fixed26_6::FromFloat(std::numeric_limits<float>::infinity()));
    CHECK(Fixed26_6::FromFloat(-0.0f)->Raw() == 0);
    CHECK(Fixed26_6::FromFloat(1.0f / 128.0f)->Raw() == 1);
    CHECK(Fixed26_6::FromFloat(-1.0f / 128.0f)->Raw() == -1);
    CHECK_FALSE(Fixed26_6::CheckedAdd(
        Fixed26_6::FromRaw(INT32_MAX), Fixed26_6::FromRaw(1)));
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(64), 1, 0));
}

namespace {

// Dereferencing a disengaged std::optional with -> is undefined behaviour, not
// a failure: in a plain Debug build the stale slot very often still holds the
// raw the assertion expects, so `...->Raw() == V` can pass against an
// implementation stubbed to report failure unconditionally. Every
// expected-value assertion below therefore goes through RawOr, which maps a
// failed call onto a sentinel no real raw can equal, so the check fails closed.
constexpr std::int64_t kNoRaw = std::numeric_limits<std::int64_t>::min();

std::int64_t RawOr(const std::optional<molga::Fixed26_6>& value) {
    return value ? static_cast<std::int64_t>(value->Raw()) : kNoRaw;
}

// Fixed26_6 deletes the floating-point overloads of FromRaw and CheckedMulDiv
// because a double argument would otherwise be truncated silently (the project
// builds without -Wconversion), producing a wrong-but-plausible raw. A deleted
// overload leaves no runtime trace and no other translation unit instantiates
// these, so dropping the guards would compile and pass every test. Pin them
// here instead: naming a deleted function is an error in the immediate context,
// which makes it SFINAE-detectable. Both polarities are asserted so the
// detector cannot pass vacuously.
template <class...>
using VoidT = void;

template <class T, class = void>
struct FromRawCallable : std::false_type {};
template <class T>
struct FromRawCallable<
    T, VoidT<decltype(molga::Fixed26_6::FromRaw(std::declval<T>()))>>
    : std::true_type {};

static_assert(!FromRawCallable<float>::value,
              "FromRaw(float) must be rejected");
static_assert(!FromRawCallable<double>::value,
              "FromRaw(double) must be rejected");
static_assert(FromRawCallable<int>::value, "FromRaw(int) must stay callable");
static_assert(FromRawCallable<std::int64_t>::value,
              "FromRaw(std::int64_t) must stay callable");

template <class TNumerator, class TDenominator, class = void>
struct MulDivCallable : std::false_type {};
template <class TNumerator, class TDenominator>
struct MulDivCallable<
    TNumerator, TDenominator,
    VoidT<decltype(molga::Fixed26_6::CheckedMulDiv(
        molga::Fixed26_6::FromRaw(0), std::declval<TNumerator>(),
        std::declval<TDenominator>()))>> : std::true_type {};

static_assert(!MulDivCallable<double, int>::value,
              "CheckedMulDiv with a floating-point numerator must be rejected");
static_assert(!MulDivCallable<int, double>::value,
              "CheckedMulDiv with a floating-point denominator must be "
              "rejected");
static_assert(!MulDivCallable<float, float>::value,
              "CheckedMulDiv with floating-point ratios must be rejected");
static_assert(MulDivCallable<int, int>::value,
              "CheckedMulDiv with integer ratios must stay callable");
static_assert(MulDivCallable<std::int64_t, std::int64_t>::value,
              "CheckedMulDiv with 64-bit integer ratios must stay callable");

} // namespace

// Characterization coverage for the paths the first test case above never
// exercises: ToFloat, CheckedAdd's success path, CheckedSub, and the
// sign-fold/overflow/round-nearest interior of CheckedMulDiv. Every assertion
// here was proven against the implementation (including under ASan+UBSan)
// before being written down.
TEST_CASE("Fixed26_6 exercises ToFloat, CheckedAdd, CheckedSub, and "
          "CheckedMulDiv's sign/overflow/round paths") {
    using molga::Fixed26_6;

    // ToFloat: exact division by Scale, and raw 0 must produce +0.0f, not -0.0f.
    CHECK(Fixed26_6::FromRaw(64).ToFloat() == 1.0f);
    CHECK(Fixed26_6::FromRaw(-64).ToFloat() == -1.0f);
    CHECK(Fixed26_6::FromRaw(1).ToFloat() == 1.0f / 64.0f);
    CHECK_FALSE(std::signbit(Fixed26_6::FromRaw(0).ToFloat()));

    // CheckedAdd needs a success witness of its own: every other CheckedAdd
    // assertion in this file is a CHECK_FALSE, so a body stubbed to
    // `return std::nullopt;` would satisfy all of them. The second case also
    // pins the negative boundary that must NOT be rejected.
    CHECK(RawOr(Fixed26_6::CheckedAdd(
              Fixed26_6::FromRaw(INT32_MAX), Fixed26_6::FromRaw(-1))) ==
          INT32_MAX - 1);
    CHECK(RawOr(Fixed26_6::CheckedAdd(
              Fixed26_6::FromRaw(INT32_MIN), Fixed26_6::FromRaw(0))) ==
          INT32_MIN);

    // CheckedSub both directions, plus CheckedAdd's mirror overflow case.
    CHECK_FALSE(Fixed26_6::CheckedSub(
        Fixed26_6::FromRaw(INT32_MIN), Fixed26_6::FromRaw(1)));
    CHECK_FALSE(Fixed26_6::CheckedSub(
        Fixed26_6::FromRaw(INT32_MAX), Fixed26_6::FromRaw(-1)));
    CHECK_FALSE(Fixed26_6::CheckedSub(
        Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(INT32_MIN)));
    CHECK(RawOr(Fixed26_6::CheckedSub(
              Fixed26_6::FromRaw(INT32_MAX), Fixed26_6::FromRaw(INT32_MAX))) ==
          0);
    CHECK(RawOr(Fixed26_6::CheckedSub(
              Fixed26_6::FromRaw(INT32_MIN), Fixed26_6::FromRaw(INT32_MIN))) ==
          0);
    CHECK_FALSE(Fixed26_6::CheckedAdd(
        Fixed26_6::FromRaw(INT32_MIN), Fixed26_6::FromRaw(-1)));

    // All eight sign combinations of (value, numerator, denominator): the sign
    // fold is a three-way XOR, and flipping any single toggle changes the
    // result, so a broken fold cannot hide behind an even number of negatives.
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(64), 1, 2)) == 32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(-64), 1, 2)) == -32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(64), -1, 2)) == -32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(64), 1, -2)) == -32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(-64), -1, 2)) == 32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(-64), 1, -2)) == 32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(64), -1, -2)) == 32);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(-64), -1, -2)) == -32);

    // Rounding: exact ties away from zero in both signs, a non-tie a
    // banker's-rounding implementation would get wrong (2.5 -> 3, not 2), and
    // a below-tie case that must round toward zero.
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(1), 1, 2)) == 1);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(-1), 1, 2)) == -1);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(5), 1, 2)) == 3);
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(2), 1, 5)) == 0);
    // A second tie at a different parity. 2.5 -> 3 above already rules out
    // round-half-to-even; 1.5 -> 2 here rules out its mirror image,
    // round-half-to-odd, which would have produced 1. Only "away from zero"
    // satisfies both witnesses at once.
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(3), 1, 2)) == 2);

    // The negative magnitude range is one wider than the positive range
    // (|INT32_MIN| == INT32_MAX + 1): the identical magnitude must succeed
    // negative and fail positive.
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(-INT32_MAX),
                                         2147483648LL, 2147483647LL)) ==
          INT32_MIN);
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(INT32_MAX), 2147483648LL, 2147483647LL));

    // INT32_MIN is the one raw whose magnitude has no positive counterpart, so
    // the unsigned magnitude path must carry it through the identity ratio
    // intact and must reject the sign flip that would land on +2^31.
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(INT32_MIN), 1, 1)) == INT32_MIN);
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(INT32_MIN), -1, 1));

    // A 64-bit intermediate-product overflow is rejected, but a zero value
    // never overflows regardless of how large the other operand is.
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(INT32_MAX), INT64_MAX, 1));
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(0), INT64_MAX, 1)) == 0);

    // The guard is on the intermediate product, not on the mathematical result:
    // INT32_MAX * 2^34 / 2^34 is exactly INT32_MAX, yet 2^31 * 2^34 exceeds the
    // 64-bit accumulator, so the call must fail rather than silently wrap. (The
    // same ratio at 2^33 still fits and succeeds, so this is the real edge.)
    CHECK(RawOr(Fixed26_6::CheckedMulDiv(
              Fixed26_6::FromRaw(INT32_MAX), 1LL << 33, 1LL << 33)) ==
          INT32_MAX);
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(INT32_MAX), 1LL << 34, 1LL << 34));

    // INT64_MIN has no positive counterpart to negate; both operand positions
    // must be rejected rather than reaching undefined behaviour.
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(64), INT64_MIN, 1));
    CHECK_FALSE(Fixed26_6::CheckedMulDiv(Fixed26_6::FromRaw(64), 1, INT64_MIN));

    // FromFloat's range guard runs on the double-widened intermediate, not the
    // float input: +2^25 scales past INT32_MAX but -2^25 scales exactly onto
    // INT32_MIN.
    CHECK_FALSE(Fixed26_6::FromFloat(std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(Fixed26_6::FromFloat(33554432.0f));
    CHECK(RawOr(Fixed26_6::FromFloat(-33554432.0f)) == INT32_MIN);
}

// FixedPoint/FixedSize/FixedRect only ever forward to Fixed26_6's comparison,
// but "forward every member, each to its own counterpart" is exactly the kind
// of boilerplate a cross-field typo (a.x == b.y) or a dropped conjunct hides
// in. Every base value below gives each member a distinct raw, so a swapped or
// duplicated field changes the answer somewhere; each case is then asserted
// through both operator== and operator!= so an inverted operator!= is caught
// too.
TEST_CASE("Fixed26_6, FixedPoint, FixedSize and FixedRect compare every member "
          "to its own counterpart") {
    using molga::Fixed26_6;
    using molga::FixedPoint;
    using molga::FixedRect;
    using molga::FixedSize;

    SUBCASE("Fixed26_6") {
        // Each aggregate spells operator!= as !(a == b) in terms of its OWN
        // operator==, so none of the subcases below ever instantiates
        // Fixed26_6::operator!= and inverting it would go unnoticed. This is
        // the type whose exact-raw equality every later measurement, hit-test
        // and snapshot comparison rests on, so pin both operators directly.
        CHECK(Fixed26_6::FromRaw(1) == Fixed26_6::FromRaw(1));
        CHECK_FALSE(Fixed26_6::FromRaw(1) == Fixed26_6::FromRaw(2));
        CHECK(Fixed26_6::FromRaw(1) != Fixed26_6::FromRaw(2));
        CHECK_FALSE(Fixed26_6::FromRaw(1) != Fixed26_6::FromRaw(1));
    }

    SUBCASE("default member initialisers") {
        // Every member starts at raw 0, so the differing-member cases below
        // really do differ only where they claim to.
        CHECK(FixedPoint{}.x.Raw() == 0);
        CHECK(FixedPoint{}.y.Raw() == 0);
        CHECK(FixedSize{}.width.Raw() == 0);
        CHECK(FixedSize{}.height.Raw() == 0);
        CHECK(FixedRect{}.x.Raw() == 0);
        CHECK(FixedRect{}.y.Raw() == 0);
        CHECK(FixedRect{}.width.Raw() == 0);
        CHECK(FixedRect{}.height.Raw() == 0);
    }

    SUBCASE("FixedPoint") {
        const FixedPoint base{Fixed26_6::FromRaw(1), Fixed26_6::FromRaw(2)};

        // Distinct members, so equality here already fails if either member is
        // compared against the wrong counterpart.
        CHECK(base == FixedPoint{Fixed26_6::FromRaw(1), Fixed26_6::FromRaw(2)});
        CHECK_FALSE(base !=
                    FixedPoint{Fixed26_6::FromRaw(1), Fixed26_6::FromRaw(2)});

        const FixedPoint differsInX{Fixed26_6::FromRaw(99), Fixed26_6::FromRaw(2)};
        CHECK(base != differsInX);
        CHECK_FALSE(base == differsInX);

        const FixedPoint differsInY{Fixed26_6::FromRaw(1), Fixed26_6::FromRaw(99)};
        CHECK(base != differsInY);
        CHECK_FALSE(base == differsInY);
    }

    SUBCASE("FixedSize") {
        const FixedSize base{Fixed26_6::FromRaw(3), Fixed26_6::FromRaw(4)};

        CHECK(base == FixedSize{Fixed26_6::FromRaw(3), Fixed26_6::FromRaw(4)});
        CHECK_FALSE(base !=
                    FixedSize{Fixed26_6::FromRaw(3), Fixed26_6::FromRaw(4)});

        const FixedSize differsInWidth{
            Fixed26_6::FromRaw(99), Fixed26_6::FromRaw(4)};
        CHECK(base != differsInWidth);
        CHECK_FALSE(base == differsInWidth);

        const FixedSize differsInHeight{
            Fixed26_6::FromRaw(3), Fixed26_6::FromRaw(99)};
        CHECK(base != differsInHeight);
        CHECK_FALSE(base == differsInHeight);
    }

    SUBCASE("FixedRect") {
        const FixedRect base{Fixed26_6::FromRaw(5), Fixed26_6::FromRaw(6),
                             Fixed26_6::FromRaw(7), Fixed26_6::FromRaw(8)};

        CHECK(base == FixedRect{Fixed26_6::FromRaw(5), Fixed26_6::FromRaw(6),
                                Fixed26_6::FromRaw(7), Fixed26_6::FromRaw(8)});
        CHECK_FALSE(base !=
                    FixedRect{Fixed26_6::FromRaw(5), Fixed26_6::FromRaw(6),
                              Fixed26_6::FromRaw(7), Fixed26_6::FromRaw(8)});

        const FixedRect differsInX{Fixed26_6::FromRaw(99), Fixed26_6::FromRaw(6),
                                   Fixed26_6::FromRaw(7), Fixed26_6::FromRaw(8)};
        CHECK(base != differsInX);
        CHECK_FALSE(base == differsInX);

        const FixedRect differsInY{Fixed26_6::FromRaw(5), Fixed26_6::FromRaw(99),
                                   Fixed26_6::FromRaw(7), Fixed26_6::FromRaw(8)};
        CHECK(base != differsInY);
        CHECK_FALSE(base == differsInY);

        const FixedRect differsInWidth{
            Fixed26_6::FromRaw(5), Fixed26_6::FromRaw(6),
            Fixed26_6::FromRaw(99), Fixed26_6::FromRaw(8)};
        CHECK(base != differsInWidth);
        CHECK_FALSE(base == differsInWidth);

        const FixedRect differsInHeight{
            Fixed26_6::FromRaw(5), Fixed26_6::FromRaw(6),
            Fixed26_6::FromRaw(7), Fixed26_6::FromRaw(99)};
        CHECK(base != differsInHeight);
        CHECK_FALSE(base == differsInHeight);
    }
}

// ── Task 3.2: byte-preserving Unicode text buffer ────────────────────────────

TEST_CASE("invalid UTF-8 keeps the original maximal-subpart byte range") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build(
        std::string("A\xF0\x28\x8C\x28Z", 6), sink);
    REQUIRE(buffer.has_value());
    CHECK(buffer->OriginalUtf8().size() == 6);
    CHECK(buffer->Scalars().at(1).value == U'\uFFFD');
    CHECK(buffer->Scalars().at(1).sourceBytes.begin == 1);
    CHECK(buffer->Scalars().at(1).sourceBytes.end == 2);
    const auto mapped =
        buffer->SourceBytesForUtf16(buffer->Scalars().at(1).utf16Units);
    REQUIRE(mapped);
    CHECK(mapped->begin == 1);
    CHECK(buffer->HadDecodeErrors());
}

TEST_CASE("supplementary scalar maps through two UTF-16 units") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build(u8"A😀Z", sink);
    REQUIRE(buffer);
    CHECK(buffer->Scalars().at(1).utf16Units.end -
          buffer->Scalars().at(1).utf16Units.begin == 2);
}

TEST_CASE("range mapping accepts exact empty end and rejects partial scalars") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build(u8"A😀Z", sink);
    REQUIRE(buffer);
    const auto utf16End = static_cast<std::uint32_t>(buffer->SanitizedUtf16().size());
    const auto byteEnd = static_cast<std::uint32_t>(buffer->OriginalUtf8().size());
    const auto endRange = buffer->SourceBytesForUtf16({utf16End, utf16End});
    REQUIRE(endRange);
    CHECK(endRange->begin == byteEnd);
    CHECK(endRange->end == byteEnd);
    const auto reverseEnd = buffer->Utf16ForSourceBytes({byteEnd, byteEnd});
    REQUIRE(reverseEnd);
    CHECK(reverseEnd->begin == utf16End);
    CHECK(reverseEnd->end == utf16End);
    CHECK_FALSE(buffer->SourceBytesForUtf16({3, 2}));
    CHECK_FALSE(buffer->SourceBytesForUtf16({2, 2})); // inside 😀 surrogate pair
    CHECK_FALSE(buffer->SourceBytesForUtf16({0, utf16End + 1}));
    CHECK_FALSE(buffer->Utf16ForSourceBytes({5, 4}));
    CHECK_FALSE(buffer->Utf16ForSourceBytes({2, 2})); // inside 😀 UTF-8 bytes
    CHECK_FALSE(buffer->Utf16ForSourceBytes({1, 2})); // ends inside 😀 bytes
    CHECK_FALSE(buffer->Utf16ForSourceBytes({byteEnd + 1, byteEnd + 1}));
}

namespace {

using molga::text::SourceByteRange;

// One decode expectation: the exact input bytes, the scalars they must produce
// in order, and the original byte range each of those scalars must keep.
struct DecodeCase {
    std::string bytes;
    std::vector<char32_t> scalars;
    std::vector<SourceByteRange> sourceRanges;
};

std::vector<char32_t> ScalarValues(
    const molga::text::UnicodeTextBuffer& buffer) {
    std::vector<char32_t> values;
    for (const auto& scalar : buffer.Scalars()) values.push_back(scalar.value);
    return values;
}

std::vector<SourceByteRange> ScalarSourceRanges(
    const molga::text::UnicodeTextBuffer& buffer) {
    std::vector<SourceByteRange> ranges;
    for (const auto& scalar : buffer.Scalars()) {
        ranges.push_back(scalar.sourceBytes);
    }
    return ranges;
}

// Both mapping directions must reproduce exactly the range the scalar already
// carries — no widening, no clamping. Spelled with == only, because neither
// Utf16Range nor SourceByteRange declares an operator!= for std::optional's
// mixed comparison to reach.
bool EveryScalarRoundTripsBothMappings(
    const molga::text::UnicodeTextBuffer& buffer) {
    for (const auto& scalar : buffer.Scalars()) {
        if (!(buffer.SourceBytesForUtf16(scalar.utf16Units) ==
              scalar.sourceBytes)) {
            return false;
        }
        if (!(buffer.Utf16ForSourceBytes(scalar.sourceBytes) ==
              scalar.utf16Units)) {
            return false;
        }
    }
    return true;
}

std::vector<SourceByteRange> InvalidDiagnosticRanges(
    const molga::text::VectorTextDiagnosticSink& sink) {
    std::vector<SourceByteRange> ranges;
    for (const auto& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == molga::text::TextDiagnosticCode::Utf8Invalid) {
            ranges.push_back(diagnostic.sourceByteRange);
        }
    }
    return ranges;
}

} // namespace

TEST_CASE("valid UTF-8 table round-trips scalar UTF-16 and source bytes") {
    const std::vector<DecodeCase> cases{
        {"A", {U'A'}, {{0, 1}}},
        {u8"é", {U'é'}, {{0, 2}}},
        {u8"😀", {U'😀'}, {{0, 4}}},
    };
    for (const auto& c : cases) {
        molga::text::VectorTextDiagnosticSink sink;
        const auto buffer = molga::text::UnicodeTextBuffer::Build(c.bytes, sink);
        REQUIRE(buffer);
        CHECK_FALSE(buffer->HadDecodeErrors());
        CHECK(ScalarValues(*buffer) == c.scalars);
        CHECK(ScalarSourceRanges(*buffer) == c.sourceRanges);
        CHECK(EveryScalarRoundTripsBothMappings(*buffer));
    }
}

TEST_CASE("invalid UTF-8 table preserves exact maximal-subpart ranges") {
    const std::vector<DecodeCase> cases{
        {std::string("\xC0\xAF", 2), {U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}}},
        {std::string("\xF0\x9F\x92", 3), {U'\uFFFD'}, {{0, 3}}},
        {std::string("\x80", 1), {U'\uFFFD'}, {{0, 1}}},
    };
    for (const auto& c : cases) {
        molga::text::VectorTextDiagnosticSink sink;
        const auto buffer = molga::text::UnicodeTextBuffer::Build(c.bytes, sink);
        REQUIRE(buffer);
        CHECK(buffer->HadDecodeErrors());
        CHECK(ScalarValues(*buffer) == c.scalars);
        CHECK(ScalarSourceRanges(*buffer) == c.sourceRanges);
        CHECK(EveryScalarRoundTripsBothMappings(*buffer));
        CHECK(InvalidDiagnosticRanges(sink) == c.sourceRanges);
    }
}

// The tables above never look at scalarIndex, at the sanitized UTF-16 content
// itself, or at the original bytes beyond their count: ScalarValues and
// ScalarSourceRanges project only value and sourceBytes, and the boundary case
// reads SanitizedUtf16() only for its size. An implementation that left every
// scalarIndex at 0, that emitted the right NUMBER of UTF-16 units with wrong
// values, or that overwrote the ill-formed bytes inside originalUtf8_ would
// satisfy all of them. Overwriting the source bytes is the exact failure this
// type exists to prevent, because the preserved range is what HarfBuzz
// clusters, caret/selection, editor diagnostics and package-time scans index.
// Every assertion below was first checked against a deliberately broken build.
TEST_CASE("the buffer keeps the original bytes, scalar indices and UTF-16 "
          "content") {
    const std::string source("A\xF0\x9F\x98\x80\xC0Z", 7);
    molga::text::VectorTextDiagnosticSink sink;
    const auto buffer = molga::text::UnicodeTextBuffer::Build(source, sink);
    REQUIRE(buffer);

    // The authored bytes come back byte for byte, the ill-formed 0xC0 included.
    CHECK(buffer->OriginalUtf8() == source);

    // The sanitized view replaces only that byte, and encodes U+1F600 as a real
    // surrogate pair rather than as two units of the right count.
    const std::u16string expected{0x0041, 0xD83D, 0xDE00, 0xFFFD, 0x005A};
    CHECK(buffer->SanitizedUtf16() == expected);

    // scalarIndex is the scalar's own position, and both range families tile
    // their space with no gap and no overlap.
    REQUIRE(buffer->Scalars().size() == 4);
    std::uint32_t nextByte = 0;
    std::uint32_t nextUnit = 0;
    for (std::uint32_t index = 0; index < buffer->Scalars().size(); ++index) {
        const auto& scalar = buffer->Scalars()[index];
        CHECK(scalar.scalarIndex == index);
        CHECK(scalar.sourceBytes.begin == nextByte);
        CHECK(scalar.utf16Units.begin == nextUnit);
        CHECK(scalar.sourceBytes.end > scalar.sourceBytes.begin);
        CHECK(scalar.utf16Units.end > scalar.utf16Units.begin);
        nextByte = scalar.sourceBytes.end;
        nextUnit = scalar.utf16Units.end;
    }
    CHECK(nextByte == source.size());
    CHECK(nextUnit == expected.size());

    // Exactly one diagnostic, carrying the one ill-formed byte range and enough
    // context for a human to act on it.
    REQUIRE(sink.Diagnostics().size() == 1);
    const auto& reported = sink.Diagnostics().front();
    CHECK(reported.code == molga::text::TextDiagnosticCode::Utf8Invalid);
    // Error, not Blocker: the same bytes must block an authored asset and keep
    // running for a script-built string, and only the caller knows which.
    CHECK(reported.severity == molga::text::TextSeverity::Error);
    CHECK(reported.sourceByteRange.begin == 5);
    CHECK(reported.sourceByteRange.end == 6);
    CHECK_FALSE(reported.message.empty());
    CHECK_FALSE(reported.remediation.empty());
}

// Two-sided witnesses the tables cannot give. A fully valid string must report
// NOTHING — the valid table only checks HadDecodeErrors(), so a spurious extra
// diagnostic would pass it — and the empty buffer must still answer the range
// every caller asks for first: the empty range at the end of the text.
TEST_CASE("valid text reports nothing and the empty buffer still maps its end") {
    molga::text::VectorTextDiagnosticSink cleanSink;
    const auto clean = molga::text::UnicodeTextBuffer::Build(u8"A😀Z", cleanSink);
    REQUIRE(clean);
    CHECK_FALSE(clean->HadDecodeErrors());
    CHECK(cleanSink.Diagnostics().empty());

    molga::text::VectorTextDiagnosticSink emptySink;
    const auto empty =
        molga::text::UnicodeTextBuffer::Build(std::string(), emptySink);
    REQUIRE(empty);
    CHECK(empty->OriginalUtf8().empty());
    CHECK(empty->SanitizedUtf16().empty());
    CHECK(empty->Scalars().empty());
    CHECK_FALSE(empty->HadDecodeErrors());
    CHECK(emptySink.Diagnostics().empty());
    const auto forward = empty->SourceBytesForUtf16({0, 0});
    REQUIRE(forward);
    CHECK(forward->begin == 0);
    CHECK(forward->end == 0);
    const auto backward = empty->Utf16ForSourceBytes({0, 0});
    REQUIRE(backward);
    CHECK(backward->begin == 0);
    CHECK(backward->end == 0);
    CHECK_FALSE(empty->SourceBytesForUtf16({0, 1}));
    CHECK_FALSE(empty->Utf16ForSourceBytes({0, 1}));
}

// Utf16Range, ScalarRange and GraphemeRange each carry their own operator==,
// and only Utf16Range's is reached above — through the optional comparisons in
// EveryScalarRoundTripsBothMappings, and only on values that are equal. A
// cross-field typo (a.begin == b.end) or an inverted result in the other two
// would reach Task 3.3's scalar and grapheme ranges unnoticed, so pin both
// polarities of all three here. Each base gives its two members different
// values, so comparing a member against the wrong counterpart changes the
// answer. None of these types declares an operator!=, so CHECK_FALSE on == is
// the opposite polarity.
TEST_CASE("Unicode range records compare each member to its own counterpart") {
    using molga::text::DecodedScalar;
    using molga::text::GraphemeRange;
    using molga::text::ScalarRange;
    using molga::text::Utf16Range;

    const Utf16Range utf16{1, 2};
    const Utf16Range utf16Same{1, 2};
    const Utf16Range utf16DiffersInBegin{9, 2};
    const Utf16Range utf16DiffersInEnd{1, 9};
    CHECK(utf16 == utf16Same);
    CHECK_FALSE(utf16 == utf16DiffersInBegin);
    CHECK_FALSE(utf16 == utf16DiffersInEnd);

    const ScalarRange scalar{3, 4};
    const ScalarRange scalarSame{3, 4};
    const ScalarRange scalarDiffersInBegin{9, 4};
    const ScalarRange scalarDiffersInEnd{3, 9};
    CHECK(scalar == scalarSame);
    CHECK_FALSE(scalar == scalarDiffersInBegin);
    CHECK_FALSE(scalar == scalarDiffersInEnd);

    const GraphemeRange grapheme{5, 6};
    const GraphemeRange graphemeSame{5, 6};
    const GraphemeRange graphemeDiffersInBegin{9, 6};
    const GraphemeRange graphemeDiffersInEnd{5, 9};
    CHECK(grapheme == graphemeSame);
    CHECK_FALSE(grapheme == graphemeDiffersInBegin);
    CHECK_FALSE(grapheme == graphemeDiffersInEnd);

    // Default member initialisers. Every range starts as the empty range at 0
    // and a default DecodedScalar carries no scalar, so a record a decoder
    // forgot to fill cannot be mistaken for a real one at offset 0.
    const Utf16Range emptyUtf16{0, 0};
    const ScalarRange emptyScalar{0, 0};
    const GraphemeRange emptyGrapheme{0, 0};
    CHECK(Utf16Range{} == emptyUtf16);
    CHECK(ScalarRange{} == emptyScalar);
    CHECK(GraphemeRange{} == emptyGrapheme);

    const DecodedScalar defaulted;
    CHECK(defaulted.value == U'\0');
    CHECK(defaulted.scalarIndex == 0);
    CHECK(defaulted.sourceBytes == SourceByteRange{});
    CHECK(defaulted.utf16Units == emptyUtf16);
}

namespace {

using molga::text::Utf16Range;

// True only when every surrogate in the sanitized view belongs to a well-formed
// pair. A UTF-8-encoded surrogate (CESU-8/WTF-8) or a code point above
// U+10FFFF that slipped past validation shows up here as a lone surrogate,
// which is then handed to ICU break iteration in Task 3.3 and to HarfBuzz,
// where it misbehaves silently rather than failing.
bool Utf16IsWellFormed(const std::u16string& units) {
    for (std::size_t index = 0; index < units.size(); ++index) {
        const char16_t unit = units[index];
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (index + 1 >= units.size()) return false;
            const char16_t low = units[index + 1];
            if (low < 0xDC00 || low > 0xDFFF) return false;
            ++index;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return false;
        }
    }
    return true;
}

} // namespace

// The invalid table above reaches only the "this byte cannot lead a sequence"
// path: \xC0, a stray \x80, and a truncated \xF0\x9F\x92 that runs off the end.
// Nothing in it exercises a per-lead second-byte window, so widening ED's
// window to 0x80..0xBF, F0's low bound to 0x80, or F4's high bound to 0xBF
// would make UTF-8-encoded surrogates, overlong forms, and code points above
// U+10FFFF decode as VALID scalars: HadDecodeErrors() false, no
// TEXT_UTF8_INVALID, and an unpaired surrogate in SanitizedUtf16(). Every row
// below was confirmed to fail against exactly that widening.
TEST_CASE("overlong, surrogate and out-of-range sequences never form a scalar") {
    const std::vector<DecodeCase> cases{
        // Overlong: a code point spelled in more bytes than it needs. The
        // classic hazard is the overlong NUL, which a byte-level filter reads
        // as harmless while a decoder that validates by value alone accepts it.
        {std::string("\xC1\xBF", 2), {U'\uFFFD', U'\uFFFD'}, {{0, 1}, {1, 2}}},
        {std::string("\xE0\x80\x80", 3),
         {U'\uFFFD', U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}, {2, 3}}},
        {std::string("\xF0\x80\x80\x80", 4),
         {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}, {2, 3}, {3, 4}}},
        // UTF-8-encoded surrogates, both ends of D800..DFFF.
        {std::string("\xED\xA0\x80", 3),
         {U'\uFFFD', U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}, {2, 3}}},
        {std::string("\xED\xBF\xBF", 3),
         {U'\uFFFD', U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}, {2, 3}}},
        // Above U+10FFFF: once through F4's second-byte window, once through a
        // lead byte that can never start a sequence at all.
        {std::string("\xF4\x90\x80\x80", 4),
         {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}, {2, 3}, {3, 4}}},
        {std::string("\xF5\x88\x80\x80", 4),
         {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'},
         {{0, 1}, {1, 2}, {2, 3}, {3, 4}}},
        {std::string("\xFE\xFF", 2), {U'\uFFFD', U'\uFFFD'}, {{0, 1}, {1, 2}}},
    };
    for (const auto& c : cases) {
        molga::text::VectorTextDiagnosticSink sink;
        const auto buffer = molga::text::UnicodeTextBuffer::Build(c.bytes, sink);
        REQUIRE(buffer);
        CHECK(buffer->HadDecodeErrors());
        CHECK(ScalarValues(*buffer) == c.scalars);
        CHECK(ScalarSourceRanges(*buffer) == c.sourceRanges);
        CHECK(InvalidDiagnosticRanges(sink) == c.sourceRanges);
        CHECK(EveryScalarRoundTripsBothMappings(*buffer));
        CHECK(Utf16IsWellFormed(buffer->SanitizedUtf16()));
    }
}

// The valid table stops at U+1F600, far from every class boundary, and reads
// SanitizedUtf16() only for its size. So `value < 0x10000` could become `<=`
// and U+10000 — the FIRST supplementary scalar — would be emitted as the
// single unit (char16_t)0x10000 == 0x0000: one unit instead of two, shifting
// every UTF-16 offset after it for the rest of the buffer. The rows below sit
// on the accepted side of each window that the case above probes from outside.
TEST_CASE("valid UTF-8 boundary scalars decode and encode exactly") {
    struct BoundaryCase {
        std::string bytes;
        char32_t scalar;
        std::u16string units;
    };
    const std::vector<BoundaryCase> cases{
        {std::string("\x7F", 1), char32_t{0x007F}, std::u16string{0x007F}},
        {std::string("\xC2\x80", 2), char32_t{0x0080}, std::u16string{0x0080}},
        {std::string("\xDF\xBF", 2), char32_t{0x07FF}, std::u16string{0x07FF}},
        {std::string("\xE0\xA0\x80", 3), char32_t{0x0800},
         std::u16string{0x0800}},
        // The two scalars either side of the surrogate block.
        {std::string("\xED\x9F\xBF", 3), char32_t{0xD7FF},
         std::u16string{0xD7FF}},
        {std::string("\xEE\x80\x80", 3), char32_t{0xE000},
         std::u16string{0xE000}},
        {std::string("\xEF\xBF\xBF", 3), char32_t{0xFFFF},
         std::u16string{0xFFFF}},
        // The two scalars either side of the BMP boundary, and the last scalar
        // Unicode has.
        {std::string("\xF0\x90\x80\x80", 4), char32_t{0x010000},
         std::u16string{0xD800, 0xDC00}},
        {std::string("\xF4\x8F\xBF\xBF", 4), char32_t{0x10FFFF},
         std::u16string{0xDBFF, 0xDFFF}},
    };
    for (const auto& c : cases) {
        molga::text::VectorTextDiagnosticSink sink;
        const auto buffer = molga::text::UnicodeTextBuffer::Build(c.bytes, sink);
        REQUIRE(buffer);
        CHECK_FALSE(buffer->HadDecodeErrors());
        CHECK(sink.Diagnostics().empty());
        REQUIRE(buffer->Scalars().size() == 1);
        CHECK(buffer->Scalars()[0].value == c.scalar);
        CHECK(buffer->Scalars()[0].sourceBytes ==
              SourceByteRange{0, static_cast<std::uint32_t>(c.bytes.size())});
        CHECK(buffer->SanitizedUtf16() == c.units);
        CHECK(Utf16IsWellFormed(buffer->SanitizedUtf16()));
        CHECK(EveryScalarRoundTripsBothMappings(*buffer));
    }
}

// Step 1's own input decodes into six scalars that tile all six bytes, but that
// case asserts only Scalars().at(1). An implementation that advanced the cursor
// by the LEAD BYTE's declared length instead of by the maximal subpart it
// actually consumed would jump from byte 1 straight to byte 5, produce three
// scalars, and leave bytes 2..5 covered by no scalar at all while
// OriginalUtf8() still reports six. That is the silent shift this type exists
// to prevent: every HarfBuzz cluster and caret offset past byte 1 would name
// the wrong byte. The other tiling case cannot see it, because its only
// multi-byte lead is well formed.
TEST_CASE("a broken sequence yields only its maximal subpart and resynchronises") {
    const std::string source("A\xF0\x28\x8C\x28Z", 6);
    molga::text::VectorTextDiagnosticSink sink;
    const auto buffer = molga::text::UnicodeTextBuffer::Build(source, sink);
    REQUIRE(buffer);
    CHECK(buffer->OriginalUtf8() == source);

    // \xF0 promises three continuation bytes and gets '(' instead, so the
    // subpart is the lead alone and '(' is re-examined as a fresh lead.
    const std::vector<char32_t> expectedScalars{
        U'A', U'\uFFFD', U'(', U'\uFFFD', U'(', U'Z'};
    const std::vector<SourceByteRange> expectedRanges{
        {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}};
    CHECK(ScalarValues(*buffer) == expectedScalars);
    CHECK(ScalarSourceRanges(*buffer) == expectedRanges);

    // No authored byte is left without a scalar, and none is claimed twice.
    std::uint32_t nextByte = 0;
    for (const auto& scalar : buffer->Scalars()) {
        CHECK(scalar.sourceBytes.begin == nextByte);
        nextByte = scalar.sourceBytes.end;
    }
    CHECK(nextByte == source.size());

    // Only the two bytes that actually broke a sequence are reported; the two
    // '(' that ended them are ordinary text.
    const std::vector<SourceByteRange> expectedDiagnostics{{1, 2}, {3, 4}};
    CHECK(InvalidDiagnosticRanges(sink) == expectedDiagnostics);
    CHECK(EveryScalarRoundTripsBothMappings(*buffer));
}

// Two guards that the boundary case above covers only by accident.
//
// Its two reversed-range checks pass for the wrong reason: on A😀Z the second
// endpoint of {3,2} and of {5,4} is itself inside the surrogate pair / inside
// 😀's bytes, so both already fail the partial-scalar rule. Delete the
// `begin > end` guard and they still pass, while SourceBytesForUtf16({3,1})
// starts returning {5,1} — a well-typed, half-open-inverted range that nothing
// downstream can tell from a real one, because both endpoints ARE exact scalar
// boundaries.
//
// The second half covers the other unwitnessed guard. An offset strictly inside
// the LAST scalar drives the binary search to scalars.end(); every string in
// the suite avoids that by ending in ASCII, so the bounds check is dead code
// and a missing one reads a past-the-end scalar.
TEST_CASE("reversed ranges and offsets inside the final scalar are rejected") {
    molga::text::VectorTextDiagnosticSink sink;
    const auto buffer = molga::text::UnicodeTextBuffer::Build(u8"A😀Z", sink);
    REQUIRE(buffer);

    // Both endpoints are exact boundaries here; only their order is wrong.
    CHECK_FALSE(buffer->SourceBytesForUtf16({3, 1}));
    CHECK_FALSE(buffer->Utf16ForSourceBytes({5, 1}));
    // The same two endpoints in the right order still map, so the rejections
    // above are about the order and not about the endpoints.
    CHECK(buffer->SourceBytesForUtf16({1, 3}) == SourceByteRange{1, 5});
    CHECK(buffer->Utf16ForSourceBytes({1, 5}) == Utf16Range{1, 3});

    molga::text::VectorTextDiagnosticSink trailingSink;
    const auto trailing =
        molga::text::UnicodeTextBuffer::Build(u8"A😀", trailingSink);
    REQUIRE(trailing);
    REQUIRE(trailing->OriginalUtf8().size() == 5);
    // Byte offsets 2, 3 and 4 all fall inside the trailing supplementary
    // scalar, so no scalar begins at or after any of them.
    CHECK_FALSE(trailing->Utf16ForSourceBytes({2, 2}));
    CHECK_FALSE(trailing->Utf16ForSourceBytes({3, 4}));
    CHECK_FALSE(trailing->Utf16ForSourceBytes({0, 3}));
    // The exact end-of-text boundary is still accepted, so the rejections above
    // are about the partial scalar and not about querying near the end.
    CHECK(trailing->Utf16ForSourceBytes({0, 5}) == Utf16Range{0, 3});
}

// ── Task 3.3: ICU grapheme, line-break, script and BiDi analysis ─────────────

namespace {

using molga::text::AnalysisItem;
using molga::text::BaseDirection;
using molga::text::TextAnalysisOptions;
using molga::text::UnicodeAnalysis;

// Step 1j. The only door into the analyzer these cases use. It goes through the
// public buffer and the public analyzer, never ICU, and requires both optionals
// before dereferencing either, so no case below can read a stale payload out of
// a disengaged optional and pass against a stubbed failure.
UnicodeAnalysis AnalyzeFixture(std::string utf8,
                               TextAnalysisOptions options = {}) {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build(std::move(utf8), sink);
    REQUIRE(buffer);
    auto analysis =
        molga::text::UnicodeTextAnalyzer::Analyze(*buffer, options, sink);
    REQUIRE(analysis);
    return std::move(*analysis);
}

bool HasDiagnostic(const molga::text::VectorTextDiagnosticSink& sink,
                   molga::text::TextDiagnosticCode code) {
    for (const auto& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

// Step 1j. Every boundary vector this file reads is required to be sorted, so a
// linear scan would hide an implementation that emits boundaries out of order.
bool ContainsBoundary(const std::vector<std::uint32_t>& boundaries,
                      std::uint32_t offset) {
    return std::binary_search(boundaries.begin(), boundaries.end(), offset);
}

bool AllLineBreaksAreGraphemeAligned(const UnicodeAnalysis& analysis) {
    for (const std::uint32_t boundary : analysis.LineBreakBoundaries()) {
        if (!ContainsBoundary(analysis.GraphemeBoundaries(), boundary)) {
            return false;
        }
    }
    return true;
}

// Step 1i. The single item covering one grapheme, or nullptr. Written as an
// exhaustive scan with an overlap check rather than "first match wins" because
// an item set that double-covers one grapheme and skips the next still yields a
// perfectly plausible-looking level vector.
const AnalysisItem* CoveringItem(const UnicodeAnalysis& analysis,
                                 std::uint32_t begin, std::uint32_t end) {
    const AnalysisItem* covering = nullptr;
    for (const auto& item : analysis.Items()) {
        if (begin < item.sourceBytes.begin || end > item.sourceBytes.end) {
            continue;
        }
        if (covering != nullptr) return nullptr;  // overlap
        covering = &item;
    }
    return covering;
}

// Step 1i. One slot per adjacent grapheme-boundary pair, each filled from the
// single covering item. A gap or an overlap leaves the sentinel in place and
// fails the case; 0xFF can never collide with a real level, whose ceiling is
// UBIDI_MAX_EXPLICIT_LEVEL + 1 == 126.
std::vector<std::uint8_t> EmbeddingLevelsByGrapheme(
    const UnicodeAnalysis& analysis) {
    constexpr std::uint8_t kUnfilled = 0xFF;
    const auto& boundaries = analysis.GraphemeBoundaries();
    REQUIRE(boundaries.size() >= 1);
    std::vector<std::uint8_t> levels(boundaries.size() - 1, kUnfilled);
    for (std::size_t slot = 0; slot < levels.size(); ++slot) {
        const AnalysisItem* covering =
            CoveringItem(analysis, boundaries[slot], boundaries[slot + 1]);
        REQUIRE(covering != nullptr);
        levels[slot] = covering->embeddingLevel;
    }
    return levels;
}

// Step 1i. The covering item's resolved script, so the case asserts what the
// analyzer actually decided rather than re-deriving it from the scalar.
std::int32_t ScriptAtGrapheme(const UnicodeAnalysis& analysis,
                              std::size_t graphemeIndex) {
    const auto& boundaries = analysis.GraphemeBoundaries();
    REQUIRE(graphemeIndex + 1 < boundaries.size());
    const AnalysisItem* covering = CoveringItem(
        analysis, boundaries[graphemeIndex], boundaries[graphemeIndex + 1]);
    REQUIRE(covering != nullptr);
    return covering->scriptCode;
}

// Step 1i. Flagged items in logical order, adjacent duplicates removed only.
// Not a set: two paragraphs that both claim to start at the same byte is a
// defect this must be able to show, and sorting or de-duplicating globally
// would erase it.
std::vector<std::uint32_t> ParagraphStartBytes(const UnicodeAnalysis& analysis) {
    std::vector<std::uint32_t> starts;
    for (const auto& item : analysis.Items()) {
        if (!item.paragraphStart) continue;
        if (!starts.empty() && starts.back() == item.sourceBytes.begin) continue;
        starts.push_back(item.sourceBytes.begin);
    }
    return starts;
}

std::vector<std::uint32_t> ParagraphEndBytes(const UnicodeAnalysis& analysis) {
    std::vector<std::uint32_t> ends;
    for (const auto& item : analysis.Items()) {
        if (!item.paragraphEnd) continue;
        if (!ends.empty() && ends.back() == item.sourceBytes.end) continue;
        ends.push_back(item.sourceBytes.end);
    }
    return ends;
}

// The whole point of the analysis layer: every item boundary is a real authored
// UTF-8 offset, reachable in both mapping directions, and the items tile the
// source with no gap and no overlap. An analyzer that reported ICU's UTF-16
// offsets directly would pass every level and script assertion in this file and
// fail only here.
bool AllItemBoundariesMapToOriginalBytes(
    const molga::text::UnicodeTextBuffer& buffer,
    const UnicodeAnalysis& analysis) {
    const auto& graphemes = analysis.GraphemeBoundaries();
    std::uint32_t nextByte = 0;
    std::uint32_t nextUnit = 0;
    std::uint32_t nextGrapheme = 0;
    for (const auto& item : analysis.Items()) {
        if (item.sourceBytes.begin != nextByte) return false;
        if (item.sourceBytes.end <= item.sourceBytes.begin) return false;
        if (item.utf16Units.begin != nextUnit) return false;
        if (item.graphemes.begin != nextGrapheme) return false;
        // Both directions, so a widened or clamped range cannot pass.
        if (!(buffer.Utf16ForSourceBytes(item.sourceBytes) == item.utf16Units)) {
            return false;
        }
        if (!(buffer.SourceBytesForUtf16(item.utf16Units) == item.sourceBytes)) {
            return false;
        }
        // The grapheme range must name the same span in the boundary vector.
        if (item.graphemes.end >= graphemes.size()) return false;
        if (graphemes[item.graphemes.begin] != item.sourceBytes.begin) {
            return false;
        }
        if (graphemes[item.graphemes.end] != item.sourceBytes.end) return false;
        nextByte = item.sourceBytes.end;
        nextUnit = item.utf16Units.end;
        nextGrapheme = item.graphemes.end;
    }
    return nextByte == buffer.OriginalUtf8().size() &&
           nextUnit == buffer.SanitizedUtf16().size() &&
           static_cast<std::size_t>(nextGrapheme) + 1 == graphemes.size();
}

} // namespace

TEST_CASE("analysis preserves grapheme boundaries and exact BiDi levels") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build(u8"abc שלום 123!", sink);
    REQUIRE(buffer);
    auto analysis = molga::text::UnicodeTextAnalyzer::Analyze(
        *buffer, {"und", molga::text::BaseDirection::Auto}, sink);
    REQUIRE(analysis);
    CHECK(analysis->GraphemeBoundaries().front() == 0);
    CHECK(analysis->GraphemeBoundaries().back() == buffer->OriginalUtf8().size());
    CHECK(EmbeddingLevelsByGrapheme(*analysis) ==
          std::vector<std::uint8_t>{0,0,0,0,1,1,1,1,1,2,2,2,0});
    CHECK(AllItemBoundariesMapToOriginalBytes(*buffer, *analysis));
}

TEST_CASE("combining ZWJ and variation sequences remain one grapheme") {
    const auto combining = AnalyzeFixture(u8"x\u0301");
    const auto zwj = AnalyzeFixture(u8"👩\u200D🚀");
    const auto variation = AnalyzeFixture(u8"❤️");
    const auto variationSupplement = AnalyzeFixture(u8"！\uFE00");
    CHECK(combining.GraphemeBoundaries().size() - 1 == 1);
    CHECK(zwj.GraphemeBoundaries().size() - 1 == 1);
    CHECK(variation.GraphemeBoundaries().size() - 1 == 1);
    CHECK(variationSupplement.GraphemeBoundaries().size() - 1 == 1);
}

TEST_CASE("CRLF is one grapheme and one explicit paragraph separator") {
    const auto analysis = AnalyzeFixture("A\r\nB\n");
    CHECK(analysis.GraphemeBoundaries() ==
          std::vector<std::uint32_t>{0, 1, 3, 4, 5});
    CHECK(ContainsBoundary(analysis.LineBreakBoundaries(), 3));
    CHECK(ContainsBoundary(analysis.LineBreakBoundaries(), 5));
    CHECK_FALSE(ContainsBoundary(analysis.LineBreakBoundaries(), 2));
    CHECK(ParagraphStartBytes(analysis) ==
          std::vector<std::uint32_t>{0, 3});
    CHECK(ParagraphEndBytes(analysis) ==
          std::vector<std::uint32_t>{3, 5});
}

TEST_CASE("Latin Arabic common and inherited scalars resolve by context") {
    const auto analysis = AnalyzeFixture(u8"A·x\u0301 سـ");
    CHECK(ScriptAtGrapheme(analysis, 0) == USCRIPT_LATIN);
    CHECK(ScriptAtGrapheme(analysis, 1) == USCRIPT_LATIN);
    CHECK(ScriptAtGrapheme(analysis, 2) == USCRIPT_LATIN);
    CHECK(ScriptAtGrapheme(analysis, 4) == USCRIPT_ARABIC);
    CHECK(ScriptAtGrapheme(analysis, 5) == USCRIPT_ARABIC);
}

TEST_CASE("ICU supplies Thai opportunities and CJK punctuation prohibitions") {
    const auto thai = AnalyzeFixture(
        u8"ภาษาไทย", {"th", molga::text::BaseDirection::Auto});
    CHECK(ContainsBoundary(thai.LineBreakBoundaries(), 12));
    CHECK(ContainsBoundary(thai.LineBreakBoundaries(), 21));
    const auto cjk = AnalyzeFixture(
        u8"漢字（、。）", {"ja", molga::text::BaseDirection::Auto});
    CHECK(ContainsBoundary(cjk.LineBreakBoundaries(), 3));
    CHECK_FALSE(ContainsBoundary(cjk.LineBreakBoundaries(), 9));
    CHECK_FALSE(ContainsBoundary(cjk.LineBreakBoundaries(), 12));
    CHECK_FALSE(ContainsBoundary(cjk.LineBreakBoundaries(), 15));
    CHECK(ContainsBoundary(cjk.LineBreakBoundaries(), 18));
    CHECK(AllLineBreaksAreGraphemeAligned(thai));
    CHECK(AllLineBreaksAreGraphemeAligned(cjk));
}

TEST_CASE("invalid ICU locale fails without ad hoc analysis") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
    REQUIRE(buffer);
    CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
        *buffer, {"en--US", molga::text::BaseDirection::Auto}, sink));
    CHECK(HasDiagnostic(sink,
          molga::text::TextDiagnosticCode::LayoutInvalid));
}

TEST_CASE("analysis records resolved locales rules and unique generation") {
    const auto first = AnalyzeFixture(u8"ภาษาไทย", {"th-TH", BaseDirection::Auto});
    const auto second = AnalyzeFixture(u8"ภาษาไทย", {"th-TH", BaseDirection::Auto});
    CHECK_FALSE(first.Identity().resolvedGraphemeLocale.empty());
    CHECK_FALSE(first.Identity().resolvedLineBreakLocale.empty());
    CHECK(first.Identity().graphemeRuleIdentity.size() == 64);
    CHECK(first.Identity().lineBreakRuleIdentity.size() == 64);
    CHECK(first.Identity().resolvedGraphemeLocale ==
          second.Identity().resolvedGraphemeLocale);
    CHECK(first.Identity().resolvedLineBreakLocale ==
          second.Identity().resolvedLineBreakLocale);
    CHECK(first.Identity().graphemeRuleIdentity ==
          second.Identity().graphemeRuleIdentity);
    CHECK(first.Identity().lineBreakRuleIdentity ==
          second.Identity().lineBreakRuleIdentity);
    CHECK(first.Identity().analysisGeneration !=
          second.Identity().analysisGeneration);
}

// The two break kinds must not collapse onto one digest. Nothing above would
// notice: both identities are 64 hex characters and both are stable across
// runs, so an implementation that hashed the character rules twice — or that
// dropped the `kind` field from the canonical stream — satisfies every
// assertion in the case above.
TEST_CASE("character and line rule identities are distinct and locale sensitive") {
    const auto root = AnalyzeFixture(u8"ภาษาไทย");
    CHECK(root.Identity().graphemeRuleIdentity !=
          root.Identity().lineBreakRuleIdentity);
    // Japanese tailors the line rules, so a digest built from the requested
    // locale string alone — or from nothing but the ICU version — would make
    // these equal.
    const auto japanese =
        AnalyzeFixture(u8"漢字（、。）", {"ja", BaseDirection::Auto});
    CHECK(japanese.Identity().lineBreakRuleIdentity !=
          root.Identity().lineBreakRuleIdentity);
    CHECK(japanese.Identity().resolvedLineBreakLocale !=
          root.Identity().resolvedLineBreakLocale);
    // The other half of the same claim, and the reason the pair is asserted
    // together: Japanese tailors LINE breaking only, so the character identity
    // must be untouched by it. Assigning one iterator's rules or locale to the
    // other field passes every inequality above and fails exactly here.
    CHECK(japanese.Identity().graphemeRuleIdentity ==
          root.Identity().graphemeRuleIdentity);
    CHECK(japanese.Identity().resolvedGraphemeLocale ==
          root.Identity().resolvedGraphemeLocale);
}

// Step 1h. Serialized against every other case in this executable only by
// doctest's single-threaded runner: the allocator is process-wide, so the
// override has to be undone before the case returns or every later analysis
// becomes a silent nullopt.
TEST_CASE("the analysis generation issues UINT64_MAX once and never wraps") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
    REQUIRE(buffer);
    {
        const auto restore = molga::text_test::UnicodeTextAnalyzerTestAccess::
            SetNextGeneration(UINT64_MAX);

        const auto last =
            molga::text::UnicodeTextAnalyzer::Analyze(*buffer, {}, sink);
        REQUIRE(last);
        CHECK(last->Identity().analysisGeneration == UINT64_MAX);

        molga::text::VectorTextDiagnosticSink exhausted;
        const std::uint64_t before =
            molga::text_test::IcuObjectCreationCountForTest();
        CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
            *buffer, {}, exhausted));
        CHECK(molga::text_test::IcuObjectCreationCountForTest() == before);
        REQUIRE(exhausted.Diagnostics().size() == 1);
        CHECK(exhausted.Diagnostics().front().code ==
              molga::text::TextDiagnosticCode::LayoutInvalid);
    }
    // The RAII restore put a fresh monotonic allocator back, so the exhaustion
    // latch did not leak out of the block above.
    const auto resumed = AnalyzeFixture("A");
    CHECK(resumed.Identity().analysisGeneration != UINT64_MAX);
}

// The two-sided witness for the counter that test_unicode_not_ready reads. That
// executable can only ever observe zero, so `return 0;` would satisfy it; here
// a successful analysis must move the counter and a reset must clear it.
TEST_CASE("the ICU object counter rises with a real analysis and resets") {
    molga::text_test::ResetIcuObjectCreationCountForTest();
    CHECK(molga::text_test::IcuObjectCreationCountForTest() == 0);
    const auto analysis = AnalyzeFixture("A");
    CHECK(analysis.Items().size() == 1);
    // Two break iterators and one paragraph UBiDi at the very least.
    CHECK(molga::text_test::IcuObjectCreationCountForTest() >= 3);
    molga::text_test::ResetIcuObjectCreationCountForTest();
    CHECK(molga::text_test::IcuObjectCreationCountForTest() == 0);
}

// Empty authored text is the first thing a text field holds and the last thing
// a caret query asks about, and none of the cases above reach it: an
// implementation that assumed at least one grapheme would pass all of them and
// read past the end here.
TEST_CASE("empty text analyzes to one boundary, no item and no diagnostic") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("", sink);
    REQUIRE(buffer);
    const auto analysis =
        molga::text::UnicodeTextAnalyzer::Analyze(*buffer, {}, sink);
    REQUIRE(analysis);
    CHECK(analysis->GraphemeBoundaries() == std::vector<std::uint32_t>{0});
    CHECK(analysis->LineBreakBoundaries() == std::vector<std::uint32_t>{0});
    CHECK(analysis->Items().empty());
    CHECK(sink.Diagnostics().empty());
    CHECK(AllItemBoundariesMapToOriginalBytes(*buffer, *analysis));
}

// An explicitly requested base direction must actually reach UBiDi. Every other
// BiDi assertion in this file uses Auto, so an implementation that ignored the
// option and always passed UBIDI_DEFAULT_LTR would pass all of them.
TEST_CASE("the requested base direction changes the resolved levels") {
    const auto autoDirection = AnalyzeFixture(u8"שלום abc");
    const auto forcedLtr =
        AnalyzeFixture(u8"שלום abc", {"und", BaseDirection::LeftToRight});
    const auto forcedRtl =
        AnalyzeFixture(u8"abc שלום", {"und", BaseDirection::RightToLeft});

    // Auto takes the first strong character, which is Hebrew: base level 1, so
    // the trailing Latin run resolves to level 2.
    CHECK(EmbeddingLevelsByGrapheme(autoDirection) ==
          std::vector<std::uint8_t>{1, 1, 1, 1, 1, 2, 2, 2});
    // Forced LTR gives base level 0, so the same Hebrew is 1 and the Latin 0.
    CHECK(EmbeddingLevelsByGrapheme(forcedLtr) ==
          std::vector<std::uint8_t>{1, 1, 1, 1, 0, 0, 0, 0});
    // Forced RTL over Latin-first text: base level 1 puts the Latin at 2.
    CHECK(EmbeddingLevelsByGrapheme(forcedRtl) ==
          std::vector<std::uint8_t>{2, 2, 2, 1, 1, 1, 1, 1});
}

// Items are the unit later milestones hand to HarfBuzz, so a run must not span
// two scripts or two embedding levels even when the graphemes are adjacent.
// Nothing above checks the item count itself.
TEST_CASE("items split at script and embedding level changes with stable runs") {
    const auto analysis = AnalyzeFixture(u8"abשגd");
    REQUIRE(analysis.Items().size() == 3);
    CHECK(analysis.Items()[0].sourceBytes == molga::text::SourceByteRange{0, 2});
    CHECK(analysis.Items()[1].sourceBytes == molga::text::SourceByteRange{2, 6});
    CHECK(analysis.Items()[2].sourceBytes == molga::text::SourceByteRange{6, 7});
    CHECK(analysis.Items()[0].scriptCode == USCRIPT_LATIN);
    CHECK(analysis.Items()[1].scriptCode == USCRIPT_HEBREW);
    CHECK(analysis.Items()[2].scriptCode == USCRIPT_LATIN);
    CHECK(analysis.Items()[0].embeddingLevel == 0);
    CHECK(analysis.Items()[1].embeddingLevel == 1);
    CHECK(analysis.Items()[2].embeddingLevel == 0);
    // The two level-0 Latin items are different logical runs, and the ids are
    // handed out in logical order.
    CHECK(analysis.Items()[0].logicalRunId != analysis.Items()[2].logicalRunId);
    CHECK(analysis.Items()[0].logicalRunId < analysis.Items()[1].logicalRunId);
    CHECK(analysis.Items()[1].logicalRunId < analysis.Items()[2].logicalRunId);
    // One paragraph, so exactly one item starts it and exactly one ends it.
    CHECK(ParagraphStartBytes(analysis) == std::vector<std::uint32_t>{0});
    CHECK(ParagraphEndBytes(analysis) == std::vector<std::uint32_t>{7});
}

// uloc_forLanguageTag reports U_STRING_NOT_TERMINATED_WARNING — a warning, so
// U_FAILURE stays false and parsedLength still equals the whole tag — when the
// canonical locale id is exactly ULOC_FULLNAME_CAPACITY (157) bytes. Gating on
// those two alone accepts the tag and then hands ubrk_open a char array with no
// terminator: ASan reports a stack-buffer-overflow inside strlen, and a plain
// build silently returns a perfectly ordinary-looking analysis for a locale
// nobody can name. Fail-open is the one outcome this subsystem forbids.
//
// The window is exactly one length. 158 already sets U_BUFFER_OVERFLOW_ERROR
// and was refused before, and the 156-byte tag below must still be accepted, so
// the refusal is about the missing terminator and not about long tags.
TEST_CASE("a locale whose canonical form fills ICU's buffer exactly is refused") {
    const std::string overflowing =
        "en-US-abcdaaaz-abcdbaaz-abcdcaaz-abcddaaz-abcdeaaz-abcdfaaz-abcdgaaz"
        "-abcdhaaz-abcdiaaz-abcdjaaz-abcdkaaz-abcdlaaz-abcdmaaz-abcdnaaz"
        "-abcdoaaz-abcdpaaz-wxyzefg";
    REQUIRE(overflowing.size() == 157);

    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
    REQUIRE(buffer);
    CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
        *buffer, {overflowing, BaseDirection::Auto}, sink));
    REQUIRE(sink.Diagnostics().size() == 1);
    CHECK(sink.Diagnostics().front().code ==
          molga::text::TextDiagnosticCode::LayoutInvalid);
    CHECK(sink.Diagnostics().front().severity ==
          molga::text::TextSeverity::Error);

    // One byte shorter, same shape: still analyzed, so the check above is not a
    // blanket length limit that would quietly refuse legitimate long tags.
    const std::string fitting = overflowing.substr(0, overflowing.size() - 1);
    REQUIRE(fitting.size() == 156);
    const auto analyzed = AnalyzeFixture("A", {fitting, BaseDirection::Auto});
    CHECK(analyzed.GraphemeBoundaries() == std::vector<std::uint32_t>{0, 1});
}

// "" parses completely (parsedLength 0 == size 0) and canonicalizes to root, so
// the completeness gate alone lets it through. A cleared or default-constructed
// locale field reaching this API means "nobody chose a locale", not "the author
// chose root tailoring" — and this same gate already refuses "en_US", "C" and
// "en-" precisely so that no author silently gets tailoring they never picked.
TEST_CASE("an empty locale tag is refused rather than silently treated as root") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
    REQUIRE(buffer);
    CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
        *buffer, {"", BaseDirection::Auto}, sink));
    REQUIRE(sink.Diagnostics().size() == 1);
    CHECK(sink.Diagnostics().front().code ==
          molga::text::TextDiagnosticCode::LayoutInvalid);
    // The documented default is "und", and it must keep working.
    const auto root = AnalyzeFixture("A", {"und", BaseDirection::Auto});
    CHECK(root.Identity().resolvedLineBreakLocale == "root");
}

// Step 7 runs UBiDi once per explicit paragraph, over that paragraph's slice of
// the UTF-16. Nothing above can see whether the slice offsets are right: the
// only multi-paragraph fixture in this file is "A\r\nB\n", every grapheme of
// which is level 0, so passing the whole text instead of the paragraph, or
// reading levels[] at the wrong base, produces exactly the same answer. Here
// the two paragraphs resolve to different base levels, so either mistake
// changes the second paragraph's levels.
//
// The Latin paragraph is deliberately long enough to push the UTF-16 buffer out
// of libc++'s inline storage, so a slice length taken as the paragraph END
// rather than its LENGTH reads off the end of a heap allocation and ASan sees
// it; in a short string that read stays inside the object and is invisible.
TEST_CASE("each paragraph resolves its own base level and run ids stay unique") {
    // Paragraph 1 is Hebrew and resolves to base level 1; paragraph 2 starts
    // Latin and resolves to base level 0, and is itself mixed, so reading the
    // level array at the wrong base shifts levels WITHIN the paragraph as well
    // as across it.
    const auto analysis = AnalyzeFixture(u8"שלום\nabc שלום");
    CHECK(EmbeddingLevelsByGrapheme(analysis) ==
          std::vector<std::uint8_t>{1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1});
    REQUIRE(analysis.Items().size() == 3);
    CHECK(analysis.Items()[0].sourceBytes == molga::text::SourceByteRange{0, 9});
    CHECK(analysis.Items()[1].sourceBytes ==
          molga::text::SourceByteRange{9, 13});
    CHECK(analysis.Items()[2].sourceBytes ==
          molga::text::SourceByteRange{13, 21});
    // Logical run ids are handed out across the whole analysis, not restarted
    // per paragraph. Two runs sharing an id is invisible in every boundary and
    // level assertion, and silently mis-hits any cache keyed on the run.
    CHECK(analysis.Items()[0].logicalRunId !=
          analysis.Items()[1].logicalRunId);
    CHECK(analysis.Items()[0].logicalRunId < analysis.Items()[1].logicalRunId);
    CHECK(analysis.Items()[1].logicalRunId < analysis.Items()[2].logicalRunId);
    CHECK(ParagraphStartBytes(analysis) == std::vector<std::uint32_t>{0, 9});
    CHECK(ParagraphEndBytes(analysis) == std::vector<std::uint32_t>{9, 21});

    // The requested direction has to reach every paragraph, not just the first.
    const auto forced = AnalyzeFixture(u8"שלום\nabc שלום",
                                       {"und", BaseDirection::RightToLeft});
    CHECK(EmbeddingLevelsByGrapheme(forced) ==
          std::vector<std::uint8_t>{1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1});

    // Each paragraph is handed its LENGTH, not its end offset. Passing the end
    // makes every paragraph but the first overrun into the text that follows,
    // which no two-paragraph fixture can show and which the sanitizer misses as
    // long as the overrun stays inside the string's allocated capacity. The
    // middle paragraph here holds nothing but neutrals, so its base direction
    // is decided entirely by whether the Hebrew paragraph below it is visible
    // to ubidi_setPara: 0 if the slice is right, 1 if it ran on.
    const auto three = AnalyzeFixture(u8"abc\n \nשלום");
    CHECK(EmbeddingLevelsByGrapheme(three) ==
          std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 1, 1, 1, 1});
    CHECK(ParagraphStartBytes(three) == std::vector<std::uint32_t>{0, 4, 6});
    CHECK(ParagraphEndBytes(three) == std::vector<std::uint32_t>{4, 6, 14});
    REQUIRE(three.Items().size() == 3);
    CHECK(three.Items()[0].logicalRunId < three.Items()[1].logicalRunId);
    CHECK(three.Items()[1].logicalRunId < three.Items()[2].logicalRunId);
}

// The existing splitting case uses Latin next to Hebrew, where the script
// change always coincides with an embedding level change, so the script
// conjunct is never the reason for the split. Here both sides are level 0 and
// one logical run, and only the resolved script differs.
TEST_CASE("items split at a script change inside one embedding level") {
    const auto analysis = AnalyzeFixture(u8"abあ");
    REQUIRE(analysis.Items().size() == 2);
    CHECK(analysis.Items()[0].sourceBytes == molga::text::SourceByteRange{0, 2});
    CHECK(analysis.Items()[1].sourceBytes == molga::text::SourceByteRange{2, 5});
    CHECK(analysis.Items()[0].scriptCode == USCRIPT_LATIN);
    CHECK(analysis.Items()[1].scriptCode == USCRIPT_HIRAGANA);
    CHECK(analysis.Items()[0].embeddingLevel ==
          analysis.Items()[1].embeddingLevel);
    CHECK(analysis.Items()[0].logicalRunId == analysis.Items()[1].logicalRunId);
}

// Step 8 requires isolate/control boundaries to split. No fixture anywhere else
// in this file contains one. These five graphemes share a level, a run and a
// resolved script, so the controls are the only thing that can separate them:
// an implementation that ignored them returns one item and passes every other
// assertion in this file. They must be their own items because they are
// direction marks, not content — a shaper handed one inside a text item tries
// to draw a glyph for it.
TEST_CASE("every BiDi isolate control becomes an item of its own") {
    const auto analysis = AnalyzeFixture(u8"a⁦b⁩c");
    REQUIRE(analysis.Items().size() == 5);
    CHECK(analysis.Items()[0].sourceBytes == molga::text::SourceByteRange{0, 1});
    CHECK(analysis.Items()[1].sourceBytes == molga::text::SourceByteRange{1, 4});
    CHECK(analysis.Items()[2].sourceBytes == molga::text::SourceByteRange{4, 5});
    CHECK(analysis.Items()[3].sourceBytes == molga::text::SourceByteRange{5, 8});
    CHECK(analysis.Items()[4].sourceBytes == molga::text::SourceByteRange{8, 9});
    for (const auto& item : analysis.Items()) {
        CHECK(item.embeddingLevel == analysis.Items()[0].embeddingLevel);
        CHECK(item.logicalRunId == analysis.Items()[0].logicalRunId);
    }
}

// Step 6, the three halves the spec case above cannot see. It checks graphemes
// 0, 1, 2, 4 and 5 of u8"A·x́ سـ" and skips grapheme 3 — the space, the
// only scalar there whose preceding and following contexts disagree.
TEST_CASE("context resolution prefers the preceding strong script") {
    // The space sits between Latin and Arabic. Taking the FOLLOWING strong
    // script instead would put it in the Arabic run.
    const auto analysis = AnalyzeFixture(u8"a س");
    CHECK(ScriptAtGrapheme(analysis, 1) == USCRIPT_LATIN);
    REQUIRE(analysis.Items().size() == 2);
    CHECK(analysis.Items()[0].sourceBytes == molga::text::SourceByteRange{0, 2});
}

TEST_CASE("a leading inherited mark takes the following strong script") {
    // U+0301 is Inherited and starts the text, so it has no preceding context.
    // Treating Inherited as a strong script of its own leaves it in a run the
    // shaper cannot pick a font for.
    const auto analysis = AnalyzeFixture(u8"́x");
    CHECK(ScriptAtGrapheme(analysis, 0) == USCRIPT_LATIN);
    CHECK(analysis.Items().size() == 1);
}

TEST_CASE("Script_Extensions keeps the Arabic tatweel in the Arabic run") {
    // U+0640 ARABIC TATWEEL is Script=Common, so plain inheritance from the
    // preceding scalar would make it Latin here and cut the Arabic word in two.
    // Only its Script_Extensions says Arabic.
    const auto analysis = AnalyzeFixture(u8"aـس");
    CHECK(ScriptAtGrapheme(analysis, 1) == USCRIPT_ARABIC);
    REQUIRE(analysis.Items().size() == 2);
    CHECK(analysis.Items()[1].sourceBytes == molga::text::SourceByteRange{1, 5});
}

// Step 4c's whole purpose: the digest must move when the compiled rules move,
// even though every other cache-key field is identical. Nothing else in this
// file can show that. Both requests resolve to the same actual locale (root),
// the same kind ("line"), the same ICU version and the same platform tuple —
// only the compiled bytes differ, and the boundary difference proves the
// difference is real and not a hashing artifact. A digest built from locale,
// kind and version alone, or one that never reads ubrk_getBinaryRules at all,
// makes these two equal and would let a cache serve loose breaks for strict.
TEST_CASE("two line tailorings of one resolved locale get different digests") {
    const auto loose =
        AnalyzeFixture(u8"あぁア。ア", {"en-u-lb-loose", BaseDirection::Auto});
    const auto strict =
        AnalyzeFixture(u8"あぁア。ア", {"en-u-lb-strict", BaseDirection::Auto});
    CHECK(loose.Identity().resolvedLineBreakLocale ==
          strict.Identity().resolvedLineBreakLocale);
    CHECK(loose.Identity().resolvedGraphemeLocale ==
          strict.Identity().resolvedGraphemeLocale);
    // The character rules are untouched by a line tailoring.
    CHECK(loose.Identity().graphemeRuleIdentity ==
          strict.Identity().graphemeRuleIdentity);
    // Loose breaking allows a line before the small kana; strict does not.
    CHECK(ContainsBoundary(loose.LineBreakBoundaries(), 3));
    CHECK_FALSE(ContainsBoundary(strict.LineBreakBoundaries(), 3));
    CHECK(loose.Identity().lineBreakRuleIdentity !=
          strict.Identity().lineBreakRuleIdentity);
}

// Step 4b canonicalizes ICU's returned locale id to a BCP-47 tag. Every other
// fixture resolves to root or ja, whose raw ICU names are already their tags,
// so the canonicalization never runs. Here the raw name is "zh_Hant".
TEST_CASE("a resolved locale is stored as a BCP-47 tag, not an ICU name") {
    const auto analysis =
        AnalyzeFixture(u8"漢字", {"zh-Hant", BaseDirection::Auto});
    CHECK(analysis.Identity().resolvedLineBreakLocale == "zh-Hant");
    CHECK(analysis.Identity().resolvedGraphemeLocale == "root");
}

// The pinned digests for the root iterators under the packaged ICU 78.3.
//
// The inequalities above cannot see the fields that never vary at runtime: the
// "character"/"line" kind, the resolved locale, and the ICU-major/endianness/
// charset tuple. Dropping any of them from the canonical stream leaves every
// other assertion in this file green. These two values were computed
// independently of UnicodeAnalysis.cpp — the rule bytes were pulled straight
// out of ubrk_getBinaryRules and hashed by a separate script following the
// written specification of the byte stream — so they pin the specified stream
// rather than whatever the implementation happens to build.
//
// A mismatch here is not a test bug. It means one of exactly four things
// changed: the packaged icudt78l.dat (the pinned SHA-256 in the dependency
// contract), the ICU major version, the build's endianness or charset family,
// or the canonical stream itself. All four are cache-invalidating events, which
// is the entire reason this identity exists. Update the constants only after
// confirming which one it was.
TEST_CASE("the break rule digests are pinned to the packaged ICU rules") {
    const auto root = AnalyzeFixture("A");
    CHECK(root.Identity().resolvedGraphemeLocale == "root");
    CHECK(root.Identity().resolvedLineBreakLocale == "root");
    CHECK(root.Identity().graphemeRuleIdentity ==
          "d5ab68404e6918d17aefafcefa28e34c45f71a804ee68eb078af8493ec6abe09");
    CHECK(root.Identity().lineBreakRuleIdentity ==
          "3be3e5d83fbf609d709fec9db8bd572520f3518756dd5c8803715681a3820e8c");
}

// The junction Task 3.2 exists for, which none of the analysis cases above
// touch: a U+FFFD whose original byte range is wider than the one UTF-16 unit
// ICU sees. Every boundary ICU reports has to come back through the buffer's
// map, and an implementation that widened a boundary across a replaced subpart,
// or that reported ICU's unit offsets directly, passes everything else here.
TEST_CASE("ill-formed source bytes keep their exact byte ranges through analysis") {
    // One stray continuation byte: one replacement, one byte wide.
    {
        molga::text::VectorTextDiagnosticSink sink;
        auto buffer =
            molga::text::UnicodeTextBuffer::Build(std::string("A\x80" "B", 3), sink);
        REQUIRE(buffer);
        const auto analysis =
            molga::text::UnicodeTextAnalyzer::Analyze(*buffer, {}, sink);
        REQUIRE(analysis);
        CHECK(analysis->GraphemeBoundaries() ==
              std::vector<std::uint32_t>{0, 1, 2, 3});
        CHECK(AllItemBoundariesMapToOriginalBytes(*buffer, *analysis));
    }
    // A truncated three-byte prefix: one replacement covering TWO source bytes
    // but a single UTF-16 unit, so byte and unit offsets diverge from here on.
    {
        molga::text::VectorTextDiagnosticSink sink;
        auto buffer = molga::text::UnicodeTextBuffer::Build(
            std::string("A\xE2\x82" "B", 4), sink);
        REQUIRE(buffer);
        REQUIRE(buffer->SanitizedUtf16().size() == 3);
        const auto analysis =
            molga::text::UnicodeTextAnalyzer::Analyze(*buffer, {}, sink);
        REQUIRE(analysis);
        CHECK(analysis->GraphemeBoundaries() ==
              std::vector<std::uint32_t>{0, 1, 3, 4});
        CHECK(analysis->LineBreakBoundaries().back() == 4);
        CHECK(AllItemBoundariesMapToOriginalBytes(*buffer, *analysis));
    }
}

// The subset property the header documents, on the one input shape that has an
// explicit separator in it. The Step 1b case that owns this fixture is a
// verbatim plan block and is not touched.
TEST_CASE("explicit separators keep line boundaries grapheme aligned") {
    CHECK(AllLineBreaksAreGraphemeAligned(AnalyzeFixture("A\r\nB\n")));
    CHECK(AllLineBreaksAreGraphemeAligned(AnalyzeFixture(u8"שלום\nabc")));
}

// Step 10 left DecodeNextUtf8 in place with no caller in the tree. It is not
// dead by accident: it is the cursor-advancing decoder a future caller will
// find first, and it deliberately disagrees with the authoritative one on
// ill-formed input. Pin both sides so a later cleanup cannot quietly change
// either, and so the disagreement stays a documented fact rather than a
// discovery.
TEST_CASE("the legacy cursor decoder still disagrees with DecodeUtf8") {
    const std::string illFormed("\xE0\x80\xAF", 3);
    // The authoritative decoder splits at the byte that breaks the sequence:
    // three Unicode maximal subparts, three replacements.
    CHECK(molga::DecodeUtf8(illFormed) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter});
    // The cursor decoder gathers the whole three-byte sequence and rejects it
    // by value: one replacement, cursor past all three.
    std::size_t cursor = 0;
    CHECK(molga::DecodeNextUtf8(illFormed, cursor) ==
          molga::kUnicodeReplacementCharacter);
    CHECK(cursor == 3);
    // On well-formed input the two must agree, or the wrapper would be a
    // rewrite rather than a compatibility shim.
    const std::string korean("\xED\x95\x9C\xEA\xB8\x80", 6);
    std::vector<std::uint32_t> stepwise;
    for (std::size_t at = 0; at < korean.size();) {
        stepwise.push_back(molga::DecodeNextUtf8(korean, at));
    }
    CHECK(stepwise == molga::DecodeUtf8(korean));
    CHECK(stepwise == std::vector<std::uint32_t>{0xD55CU, 0xAE00U});
}


// 이 케이스가 embeddingLevel이 정확히 무엇인지를 고정한다. 아래 벡터는 모두 같은
// 문자열을 ICU ubidi_getLevels에 직접 넣어 문자 단위 level을 읽고 UAX#9와 손으로
// 대조해 얻은 값이다. 네 케이스 전부가 경쟁하는 두 해석 — 방향 parity(0과 2가
// 합쳐진다)와 문단 resolved level(전부 0 아니면 1이 된다) — 아래에서 실패한다.
//
// Milestone 4가 이 필드에서 run 방향을 끌어내므로, 여기서 조용히 좁아지면 중첩된
// 방향이 섞인 텍스트에서만, 실패가 아니라 "그럴듯하게 틀린" 배치로 나타난다.
TEST_CASE("embeddingLevel is the exact resolved level, not parity and not the "
          "paragraph level") {
    // RTL 문단 안의 괄호쌍. BD16/N0에 따라 괄호 자체는 문단 level 1에 남고 안쪽
    // 내용만 2로 올라간다. 괄호를 안쪽 run에 합치면 이 벡터가 무너진다.
    CHECK(EmbeddingLevelsByGrapheme(AnalyzeFixture(
              u8"א (abc 12) ב", {"und", BaseDirection::RightToLeft})) ==
          std::vector<std::uint8_t>{1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 1});

    // 같은 자리에 isolate를 쓴 경우. LRI와 짝 PDI는 UBA X6a가 정한 대로 바깥
    // level(1)을 갖고, 안쪽만 2다. 괄호와 결과가 같다는 것 자체가 요점이다.
    CHECK(EmbeddingLevelsByGrapheme(AnalyzeFixture(
              u8"א ⁦abc 12⁩ ב", {"und", BaseDirection::RightToLeft})) ==
          std::vector<std::uint8_t>{1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 1});

    // parity 해석이 살아남을 수 없는 케이스. LTR 문단 안의 RLE embedding은 0과 2를
    // 한 item 목록에 동시에 내놓는데, 둘 다 LTR이므로 parity였다면 전부 0이고
    // 문단 level이었어도 전부 0이다.
    //
    // 5번째와 12번째 값은 RLE와 PDF 자신의 level이다. UBA X9는 이 둘을 제거하므로
    // "정확한 level"이라는 것이 존재하지 않고, 여기 있는 값은 ICU의 보존 규약
    // — 여는 쪽은 안쪽 level, PDF는 바깥 level — 이다. 헤더가 그렇게 적혀 있고,
    // 이 두 자리가 그 문장을 고정한다.
    CHECK(EmbeddingLevelsByGrapheme(AnalyzeFixture(
              u8"abc ‫def 12‬ ghi", {"und", BaseDirection::LeftToRight})) ==
          std::vector<std::uint8_t>{0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0,
                                    0});

    // RLO override도 같은 규약이다: RLO는 안쪽 1, PDF는 바깥 0.
    CHECK(EmbeddingLevelsByGrapheme(AnalyzeFixture(
              u8"abc ‮def‬ ghi", {"und", BaseDirection::LeftToRight})) ==
          std::vector<std::uint8_t>{0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0});

    // L1까지 적용된 값이라는 증거. RTL 문단 안의 Latin은 2인데, 그 사이의 TAB은
    // segment separator라서 L1이 문단 level 1로 되돌린다. neutral 규칙만
    // 돌렸다면 양쪽이 모두 level 2라 TAB도 2가 됐을 것이다.
    CHECK(EmbeddingLevelsByGrapheme(AnalyzeFixture(
              "abc\tdef", {"und", BaseDirection::RightToLeft})) ==
          std::vector<std::uint8_t>{2, 2, 2, 1, 2, 2, 2});
}

