#include "Common/Fixed26_6.h"

#include <cmath>
#include <limits>

namespace molga {
namespace {

// 두 32bit 부호값 곱셈 방향 정보를 하나의 bool로 접어서 이후 계산을 부호 없는
// 64bit 산술로만 하게 만든다. int32_t의 최소값을 부호만 뒤집어 다루면(단항 -)
// 오버플로 UB가 나므로, 크기(magnitude)는 항상 이 함수를 거쳐 64bit로 넓힌 뒤
// 뽑아낸다.
std::uint64_t MagnitudeOf(std::int32_t value) noexcept {
    const std::int64_t widened = static_cast<std::int64_t>(value);
    return static_cast<std::uint64_t>(widened < 0 ? -widened : widened);
}

} // namespace

std::optional<Fixed26_6> Fixed26_6::FromFloat(float value) {
    if (!std::isfinite(value)) return std::nullopt;

    // -0.0f는 이후 raw 0과 동일하게 취급하도록 곱셈 전에 부호를 정규화한다.
    // (곱/반올림을 거쳐도 결과는 같지만, 의도를 명시적으로 남긴다.)
    if (value == 0.0f) value = 0.0f;

    // float 하나를 64배 해도 double은 정확히 표현하므로, 반올림 이전까지는
    // 정밀도 손실이 없는 "더 넓은 유한 표현"이 된다.
    const double scaled = static_cast<double>(value) * static_cast<double>(Scale);
    const double rounded = std::round(scaled); // 0에서 먼 쪽으로 반올림(정확히 .5인 tie 포함).

    constexpr double kMin =
        static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr double kMax =
        static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (rounded < kMin || rounded > kMax) return std::nullopt;

    return Fixed26_6::FromRaw(static_cast<std::int32_t>(rounded));
}

float Fixed26_6::ToFloat() const {
    if (raw_ == 0) return 0.0f; // 표준 양의 0을 반환한다.
    return static_cast<float>(raw_) / static_cast<float>(Scale);
}

std::optional<Fixed26_6> Fixed26_6::CheckedAdd(Fixed26_6 a, Fixed26_6 b) noexcept {
    const std::int64_t sum =
        static_cast<std::int64_t>(a.raw_) + static_cast<std::int64_t>(b.raw_);
    if (sum < std::numeric_limits<std::int32_t>::min() ||
        sum > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return Fixed26_6::FromRaw(static_cast<std::int32_t>(sum));
}

std::optional<Fixed26_6> Fixed26_6::CheckedSub(Fixed26_6 a, Fixed26_6 b) noexcept {
    const std::int64_t difference =
        static_cast<std::int64_t>(a.raw_) - static_cast<std::int64_t>(b.raw_);
    if (difference < std::numeric_limits<std::int32_t>::min() ||
        difference > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return Fixed26_6::FromRaw(static_cast<std::int32_t>(difference));
}

std::optional<Fixed26_6> Fixed26_6::CheckedMulDiv(
    Fixed26_6 value, std::int64_t numerator, std::int64_t denominator) noexcept {
    if (denominator == 0) return std::nullopt;

    // INT64_MIN은 부호를 뒤집을 양의 짝이 없다(단항 -가 오버플로 UB). 실제 논리
    // 단위 연산에서 이 크기의 분자/분모는 나올 일이 없으므로, 뒤의 부호 정규화가
    // 안전해지도록 여기서 바로 거부한다.
    constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();
    if (numerator == kInt64Min || denominator == kInt64Min) return std::nullopt;

    // 세 값의 부호를 하나로 접고, 이후로는 전부 부호 없는 크기로만 계산한다.
    bool negative = value.raw_ < 0;
    if (numerator < 0) { negative = !negative; numerator = -numerator; }
    if (denominator < 0) { negative = !negative; denominator = -denominator; }

    const std::uint64_t absValue = MagnitudeOf(value.raw_);
    const std::uint64_t absNumerator = static_cast<std::uint64_t>(numerator);
    const std::uint64_t absDenominator = static_cast<std::uint64_t>(denominator);

    // absValue * absNumerator가 64bit를 넘치는지, 곱하기 전에 나눗셈으로 검사한다.
    if (absValue != 0 &&
        absNumerator > std::numeric_limits<std::uint64_t>::max() / absValue) {
        return std::nullopt;
    }
    const std::uint64_t product = absValue * absNumerator;

    const std::uint64_t quotient = product / absDenominator;
    const std::uint64_t remainder = product % absDenominator;
    // remainder < absDenominator <= INT64_MAX이므로 remainder * 2는 64bit를
    // 넘치지 않는다. 0에서 먼 쪽으로 반올림: 나머지가 절반 이상이면 올림.
    std::uint64_t rounded = quotient;
    if (remainder * 2 >= absDenominator) rounded += 1;

    constexpr std::uint64_t kMaxMagnitude =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    constexpr std::uint64_t kMinMagnitude = kMaxMagnitude + 1U; // |INT32_MIN|
    const std::uint64_t allowedMagnitude = negative ? kMinMagnitude : kMaxMagnitude;
    if (rounded > allowedMagnitude) return std::nullopt;

    if (!negative) return Fixed26_6::FromRaw(static_cast<std::int32_t>(rounded));
    if (rounded == kMinMagnitude) {
        return Fixed26_6::FromRaw(std::numeric_limits<std::int32_t>::min());
    }
    return Fixed26_6::FromRaw(-static_cast<std::int32_t>(rounded));
}

} // namespace molga
