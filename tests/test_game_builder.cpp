#include "Core/BuildManifest.h"
#include "Core/PackageLayout.h"
#include "Core/PathConstants.h"
#include "Core/PathService.h"
#include "Editor/Profiling/ProfilerReportSink.h"
#include "Scripting/ScriptApi.h"
#include "ShaderPackageTestSupport.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

struct CapturingSink : molga::IProfilerReportSink {
    std::vector<std::string> labels;
    void ReportTiming(const std::string& label, double, const std::string&) override {
        labels.push_back(label);
    }
};

TEST_CASE("report sink can be swapped and captures timings") {
    CapturingSink sink;
    molga::SetReportSink(&sink);
    molga::ActiveReportSink().ReportTiming("Build: Total", 12.5, "Game");
    molga::SetReportSink(nullptr);  // 폴백 복귀
    REQUIRE(sink.labels.size() == 1);
    CHECK(sink.labels[0] == "Build: Total");
}

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
    CHECK(std::string(Paths::Build::SHADER_BUNDLE) == "ShaderBundle");
}

TEST_CASE("PackageLayout executable name is platform aware") {
#if defined(_WIN32)
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game.exe");
#else
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game");
#endif
}

TEST_CASE("PackageLayout script manifest validation") {
    fs::path tmpDir = fs::temp_directory_path() / "molga_pkg_layout_test";
    fs::create_directories(tmpDir);

    std::string exeName = PackageLayout::ExecutableNameFor("TestGame");
    
    { std::ofstream(tmpDir / exeName); }
    fs::create_directories(tmpDir / "Scenes");
    { std::ofstream(tmpDir / "Scenes/main.json"); }
    fs::create_directories(tmpDir / "Assets");
    const std::string shaderHash =
        test_support::WriteMinimalMslShaderBundle(tmpDir);
    const auto baseConfig = test_support::MinimalPackageGameConfig(shaderHash);
    { std::ofstream(tmpDir / "game.json") << baseConfig.dump(2); }
    { std::ofstream(tmpDir / "asset_catalog.json") << "{\"schemaVersion\":1,\"records\":[]}"; }
    fs::create_directories(tmpDir / "Resources");
    { std::ofstream(tmpDir / "Resources/missing_texture.png") << "placeholder"; }

    std::string error;
    bool valid = PackageLayout::Validate(tmpDir, "TestGame", error);
    CHECK(valid);
    CHECK(error.empty());

    {
        std::ofstream(tmpDir / "ShaderBundle/artifacts/forbidden.spv")
            << "forbidden";
        CHECK_FALSE(PackageLayout::Validate(tmpDir, "TestGame", error));
        CHECK(error.find("forbidden non-MSL") != std::string::npos);
        fs::remove(tmpDir / "ShaderBundle/artifacts/forbidden.spv");
    }
    {
        std::ofstream(tmpDir / "ShaderBundle/artifacts/test.fragment.msl",
                      std::ios::app) << "tampered";
        CHECK_FALSE(PackageLayout::Validate(tmpDir, "TestGame", error));
        CHECK(error.find("SHA-256 mismatch") != std::string::npos);
        CHECK(test_support::WriteMinimalMslShaderBundle(tmpDir) == shaderHash);
    }
    {
        std::ofstream(tmpDir / "ShaderBundle/manifest.json", std::ios::app)
            << ' ';
        CHECK_FALSE(PackageLayout::Validate(tmpDir, "TestGame", error));
        CHECK(error.find("manifest SHA-256 mismatch") != std::string::npos);
        CHECK(test_support::WriteMinimalMslShaderBundle(tmpDir) == shaderHash);
    }

    {
        std::ofstream f(tmpDir / "game.json");
        auto config = baseConfig;
        config["scripts"] = {
            {"enabled", false},
            {"library", "Scripts/libUserScripts.dylib"},
            {"apiVersion", molga::ScriptApiVersion},
        };
        f << config.dump(2);
    }
    valid = PackageLayout::Validate(tmpDir, "TestGame", error);
    CHECK(valid);
    CHECK(error.empty());

    {
        std::ofstream f(tmpDir / "game.json");
        auto config = baseConfig;
        config["scripts"] = {
            {"enabled", true},
            {"library", "Scripts/libUserScripts.dylib"},
            {"apiVersion", molga::ScriptApiVersion},
        };
        f << config.dump(2);
    }
    valid = PackageLayout::Validate(tmpDir, "TestGame", error);
    CHECK_FALSE(valid);
    CHECK(error.find("missing from package") != std::string::npos);

    fs::create_directories(tmpDir / "Scripts");
    { std::ofstream(tmpDir / "Scripts/libUserScripts.dylib"); }
    valid = PackageLayout::Validate(tmpDir, "TestGame", error);
    CHECK(valid);
    CHECK(error.empty());

    fs::remove_all(tmpDir);
}
