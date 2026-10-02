#include "doctest.h"
#include <hb.h>
#include <hb-icu.h>
#include <string_view>

TEST_CASE("pinned static HarfBuzz symbols are reachable") {
    CHECK(hb_version_atleast(14, 3, 1));
    CHECK(std::string_view(hb_version_string()).find("14.3.1") == 0);
    CHECK(hb_icu_get_unicode_funcs() != nullptr);
}
