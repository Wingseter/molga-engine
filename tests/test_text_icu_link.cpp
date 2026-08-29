#include "doctest.h"
#include <cstdint>
#include <unicode/ubrk.h>
#include <unicode/uversion.h>

extern "C" const std::uint8_t icudt78_dat[];

TEST_CASE("pinned static ICU symbols are reachable directly") {
    UVersionInfo version{};
    u_getVersion(version);
    CHECK(version[0] == 78);
    CHECK(version[1] == 3);
    auto* i18nSymbol = &ubrk_open;
    CHECK(i18nSymbol != nullptr);
    CHECK(icudt78_dat[2] == 0xda);
    CHECK(icudt78_dat[3] == 0x27);
}
