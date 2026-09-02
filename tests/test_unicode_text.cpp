#include "Common/Fixed26_6.h"
#include "Text/TextDiagnostic.h"
#include "Text/UnicodeTextBuffer.h"

#include "doctest.h"

#include <cmath>
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
