#include "doctest.h"

#include "Core/PackageLayout.h"
#include "Core/SmokeReport.h"
#include "SmokeTestSupport.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("build package requires executable config scene assets and shaders") {
    test_support::TempDirectory temp{"build-smoke"};
    const fs::path root = temp.Path();

    fs::create_directories(root / "Scenes");
    fs::create_directories(root / "Assets");
    test_support::WriteText(root / "SmokeGame", "runtime");
    test_support::WriteText(root / "game.json", "{}");
    test_support::WriteText(root / "Scenes/main.json", "{}");

    std::string error;
    CHECK_FALSE(PackageLayout::Validate(root, "SmokeGame", error));
    CHECK(error.find("Shaders") != std::string::npos);

    test_support::WriteText(root / "Shaders/sprite.vert", "shader");
    CHECK(PackageLayout::Validate(root, "SmokeGame", error));
}

TEST_CASE("smoke report round trips through json") {
    test_support::TempDirectory temp{"smoke-report"};
    const fs::path path = temp.Path() / "report.json";

    SmokeReport written;
    written.executable = "molga_runtime";
    written.status = "ok";
    written.scenePath = "Scenes/main.json";
    written.objectCount = 1;
    written.frames = 3;
    written.assetsResolved = true;
    REQUIRE(written.Save(path));

    SmokeReport loaded;
    REQUIRE(SmokeReport::Load(path, loaded));
    CHECK(loaded.status == "ok");
    CHECK(loaded.objectCount == 1);
    CHECK(loaded.frames == 3);
    CHECK(loaded.assetsResolved);
}
