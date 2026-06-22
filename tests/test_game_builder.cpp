#include "Core/BuildManifest.h"
#include "Core/PackageLayout.h"
#include "Core/PathConstants.h"
#include "Core/PathService.h"
#include "Editor/Profiling/ProfilerReportSink.h"
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
    CHECK(std::string(Paths::Build::SHADERS) == "Shaders");
}

TEST_CASE("PackageLayout executable name is platform aware") {
#if defined(_WIN32)
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game.exe");
#else
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game");
#endif
}
