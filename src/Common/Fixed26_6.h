#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

namespace molga {

// 텍스트/UI 계층이 공유하는 26.6 signed fixed-point 논리 단위. 1.0f는 raw 64와
// 같고, 이후 모든 측정/배치/hit-test/스냅샷 비교는 float가 아니라 이 raw 값을
// 정확히(exact) 비교한다. 그래서 변환/사칙연산 각각이 스스로 실패를 보고해야
// 하며(포화/랩어라운드 금지), 반올림 규칙(짝수/양수 편향이 아닌 "0에서 먼 쪽")도
// 여기 한 곳에서만 정의한다.
class Fixed26_6 {
public:
    static constexpr std::int32_t Scale = 64;

    // 유한하지 않은 값(NaN/Inf)과 int32_t raw 범위를 벗어나는 값은 거부한다.
    // -0.0f는 부호를 정규화해 raw 0을 만든다.
    static std::optional<Fixed26_6> FromFloat(float value);

    static constexpr Fixed26_6 FromRaw(std::int32_t value) {
        return Fixed26_6(value);
    }

    // raw는 논리 단위의 정수 표현이므로 부동소수를 raw로 넘기는 것은 항상 실수다.
    // 프로젝트가 -Wconversion 없이 빌드되기 때문에 double 변수는 경고 없이 잘려
    // 들어간다(FromRaw(3.7) -> FromRaw(3)). 컴파일 자체를 막는다.
    // 정수(int/long/int64_t 등) 호출은 그대로 위 오버로드로 간다.
    // 차단 기준은 "추론된 인자 타입이 부동소수인가" 하나뿐이다. operator double()을
    // 가진 클래스 타입은 추론 단계에서 클래스로 보이므로 위 오버로드로 들어가 여전히
    // 조용히 잘리고, int64_t -> int32_t 축소도 의도적으로 그대로 남는다. 정수
    // 화이트리스트로 뒤집으면 operator int() 타입까지 막히므로, 이 블랙리스트가
    // 이 코드베이스의 절충안이다.
    template <class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
    static constexpr Fixed26_6 FromRaw(T) = delete;

    constexpr std::int32_t Raw() const { return raw_; }

    // raw / 64.0f. float는 유효숫자가 24bit뿐이라 |raw| <= 2^24 (16777216)에서만
    // 무손실이고, 그 범위에서만 FromFloat(x.ToFloat()) == x가 보장된다. 그 밖의
    // raw는 ToFloat에서 반올림될 수 있어 왕복이 원래 값으로 돌아온다고 보장할 수
    // 없다(2^25나 INT32_MIN처럼 float로 정확히 표현되는 큰 raw는 우연히 돌아온다).
    float ToFloat() const;

    static std::optional<Fixed26_6> CheckedAdd(Fixed26_6, Fixed26_6) noexcept;
    static std::optional<Fixed26_6> CheckedSub(Fixed26_6, Fixed26_6) noexcept;

    // (raw * numerator) / denominator를 0에서 먼 쪽으로 반올림한 뒤 int32_t 범위를
    // 검사한다. denominator가 0이거나 중간 곱이 64bit를 넘치면 실패를 보고한다.
    // 중간 곱은 부호 없는 64bit 하나에 담기므로, 결과가 수학적으로 int32_t에
    // 들어가더라도 |raw| * |numerator| < 2^64를 넘으면 계산을 거부한다. 즉 실제
    // 사용 가능한 정의역은 |raw| * |numerator| < 2^64이다.
    // (numerator/denominator는 INT64_MIN도 거부한다.)
    static std::optional<Fixed26_6> CheckedMulDiv(
        Fixed26_6, std::int64_t numerator, std::int64_t denominator) noexcept;

    // numerator/denominator는 정수 비율이다. 부동소수를 넘기면 -Wconversion 없이
    // 조용히 잘려(CheckedMulDiv(v, 1.9, 1.0) -> (1, 1)) 반올림 규칙이 통째로
    // 사라지므로, 어느 한쪽이라도 부동소수면 컴파일을 막는다. 두 인자가 모두
    // 정수 타입이면 이 오버로드는 후보에서 빠지고 위 오버로드가 선택된다.
    template <class TNumerator, class TDenominator,
              class = std::enable_if_t<std::is_floating_point_v<TNumerator> ||
                                       std::is_floating_point_v<TDenominator>>>
    static std::optional<Fixed26_6> CheckedMulDiv(
        Fixed26_6, TNumerator, TDenominator) = delete;

    friend constexpr bool operator==(Fixed26_6 a, Fixed26_6 b) noexcept {
        return a.raw_ == b.raw_;
    }
    friend constexpr bool operator!=(Fixed26_6 a, Fixed26_6 b) noexcept {
        return !(a == b);
    }

private:
    explicit constexpr Fixed26_6(std::int32_t value) : raw_(value) {}
    std::int32_t raw_ = 0;
};

// 논리 26.6 값을 물리 픽셀 엣지로 바꾸는 마지막 단계는 min을 floor로, max를
// ceil로 내보내야 한 줄도 잘려 나가지 않는다. C++의 /는 0 방향으로 자르므로
// 음수에서 floor와 다르고(-65 / 64 == -1, floor는 -2), INT64_MIN / -1과
// 0으로 나누기는 UB다. 그래서 두 방향을 이름으로 갈라 놓고 나누기와 나머지를
// 계산하기 "전에" 실패를 보고한다. 포화도 랩어라운드도 하지 않는다.
std::optional<std::int64_t> CheckedFloorDiv(std::int64_t n, std::int64_t d);
std::optional<std::int64_t> CheckedCeilDiv(std::int64_t n, std::int64_t d);

// 인자는 raw 26.6 정수 비율이다. 프로젝트가 -Wconversion 없이 빌드되므로
// 부동소수를 넘기면 경고 없이 잘려(CheckedFloorDiv(1.9, 1.0) -> (1, 1))
// 반올림 방향 자체가 사라진다. FromRaw/CheckedMulDiv와 같은 이유로 막는다.
template <class TNumerator, class TDenominator,
          class = std::enable_if_t<std::is_floating_point_v<TNumerator> ||
                                   std::is_floating_point_v<TDenominator>>>
std::optional<std::int64_t> CheckedFloorDiv(TNumerator, TDenominator) = delete;
template <class TNumerator, class TDenominator,
          class = std::enable_if_t<std::is_floating_point_v<TNumerator> ||
                                   std::is_floating_point_v<TDenominator>>>
std::optional<std::int64_t> CheckedCeilDiv(TNumerator, TDenominator) = delete;

struct FixedPoint {
    Fixed26_6 x = Fixed26_6::FromRaw(0);
    Fixed26_6 y = Fixed26_6::FromRaw(0);

    friend constexpr bool operator==(FixedPoint a, FixedPoint b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
    friend constexpr bool operator!=(FixedPoint a, FixedPoint b) noexcept {
        return !(a == b);
    }
};

struct FixedSize {
    Fixed26_6 width = Fixed26_6::FromRaw(0);
    Fixed26_6 height = Fixed26_6::FromRaw(0);

    friend constexpr bool operator==(FixedSize a, FixedSize b) noexcept {
        return a.width == b.width && a.height == b.height;
    }
    friend constexpr bool operator!=(FixedSize a, FixedSize b) noexcept {
        return !(a == b);
    }
};

struct FixedRect {
    Fixed26_6 x = Fixed26_6::FromRaw(0), y = Fixed26_6::FromRaw(0);
    Fixed26_6 width = Fixed26_6::FromRaw(0), height = Fixed26_6::FromRaw(0);

    friend constexpr bool operator==(FixedRect a, FixedRect b) noexcept {
        return a.x == b.x && a.y == b.y && a.width == b.width &&
               a.height == b.height;
    }
    friend constexpr bool operator!=(FixedRect a, FixedRect b) noexcept {
        return !(a == b);
    }
};

} // namespace molga
