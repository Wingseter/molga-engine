#pragma once

#include <cstdint>
#include <optional>

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

    constexpr std::int32_t Raw() const { return raw_; }

    float ToFloat() const;

    static std::optional<Fixed26_6> CheckedAdd(Fixed26_6, Fixed26_6) noexcept;
    static std::optional<Fixed26_6> CheckedSub(Fixed26_6, Fixed26_6) noexcept;

    // (raw * numerator) / denominator를 0에서 먼 쪽으로 반올림한 뒤 int32_t 범위를
    // 검사한다. denominator가 0이거나 중간 곱이 64bit를 넘치면 실패를 보고한다.
    static std::optional<Fixed26_6> CheckedMulDiv(
        Fixed26_6, std::int64_t numerator, std::int64_t denominator) noexcept;

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
