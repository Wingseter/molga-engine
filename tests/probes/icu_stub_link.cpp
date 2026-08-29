// Minimal standalone probe: links against an ICU common archive and requires
// the stubdata symbol to resolve. Deliberately avoids doctest, molga_core, and
// the imported CMake targets so it cannot mask a broken dependency seam.
#include <cstdint>

extern "C" const std::uint8_t icudt78_dat[];

int main() {
    return icudt78_dat[0] == 0 && icudt78_dat[1] == 0 ? 1 : 0;
}
