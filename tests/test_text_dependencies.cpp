#include "doctest.h"
#include <filesystem>
#include "Common/Sha256.h"

TEST_CASE("immutable text inputs match the approved bytes") {
    const std::filesystem::path root = MOLGA_SOURCE_DIR;
    CHECK(std::filesystem::file_size(root / "resources/text/icudt78l.dat") ==
          33107232ULL);
    CHECK(molga::Sha256File(root / "resources/text/icudt78l.dat") ==
          "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b");
    CHECK(molga::Sha256File(root / "external/text/rasterizer/imstb_truetype.h") ==
          "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528");
    CHECK(molga::Sha256File(root / "tests/fixtures/text/fonts/NotoSansKR-Regular.otf") ==
          "69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68");
}
