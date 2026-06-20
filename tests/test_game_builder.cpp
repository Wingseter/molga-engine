#include "Core/BuildManifest.h"
#include "Core/PackageLayout.h"
#include "Core/PathConstants.h"
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

TEST_CASE("Package constants use runtime package casing") {
    CHECK(std::string(Paths::Build::ASSETS) == "Assets");
    CHECK(std::string(Paths::Build::SCENES) == "Scenes");
    CHECK(std::string(Paths::Build::SHADERS) == "Shaders");
}

TEST_CASE("PackageLayout executable name is platform aware") {
#if defined(_WIN32)
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game.exe");
#else
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game");
#endif
}
