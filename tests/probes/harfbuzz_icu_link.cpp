// Minimal standalone probe: requires the HarfBuzz ICU adapter symbol to
// resolve from the archives passed on the link line, with no doctest,
// molga_core, or imported CMake target involved.
#include <hb-icu.h>

int main() {
    return hb_icu_get_unicode_funcs() != nullptr ? 0 : 1;
}
