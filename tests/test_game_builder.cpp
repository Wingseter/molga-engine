#include "Core/BuildManifest.h"
#include "Core/PathService.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("BuildManifest fails and names a missing required file") {
    BuildManifest m;
    m.requiredFiles = { "/nonexistent/molga_missing_zzz.txt" };
    std::string err;
    CHECK_FALSE(m.Validate(err));
    CHECK(err.find("molga_missing_zzz") != std::string::npos);
}

TEST_CASE("BuildManifest passes when every required file exists") {
    fs::path tmp = fs::temp_directory_path() / "molga_manifest_present.txt";
    { std::ofstream f(tmp); f << "x"; }
    BuildManifest m;
    m.requiredFiles = { tmp.string() };
    std::string err;
    CHECK(m.Validate(err));
    fs::remove(tmp);
}
