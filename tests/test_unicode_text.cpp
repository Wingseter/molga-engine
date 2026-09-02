#include "Common/Fixed26_6.h"

#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

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
