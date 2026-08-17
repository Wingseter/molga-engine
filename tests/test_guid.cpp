#include "Core/Guid.h"
#include "doctest.h"

using molga::Guid;

TEST_CASE("Generate produces a 32-char lowercase hex string") {
    std::string g = Guid::Generate();
    CHECK(g.size() == 32);
    for (char c : g) CHECK(std::isxdigit(static_cast<unsigned char>(c)));
}

TEST_CASE("two generated guids differ") {
    CHECK(Guid::Generate() != Guid::Generate());
}

TEST_CASE("IsValid accepts 32-hex and rejects others") {
    CHECK(Guid::IsValid("0123456789abcdef0123456789abcdef"));
    CHECK_FALSE(Guid::IsValid(""));
    CHECK_FALSE(Guid::IsValid("xyz"));
    CHECK_FALSE(Guid::IsValid("0123456789ABCDEF0123456789abcdeg")); // g is not hex
}
