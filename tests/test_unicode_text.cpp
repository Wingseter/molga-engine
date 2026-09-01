#include "Common/Fixed26_6.h"

#include "doctest.h"

#include <cstdint>
#include <limits>

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
