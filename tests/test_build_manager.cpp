#include "Editor/BuildManager.h"
#include "Editor/Project.h"
#include "SmokeTestSupport.h"
#include "doctest.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("BuildManager direct build loads project profile before saving UI fields") {
    Project::Get().Close();
    test_support::TempDirectory temp{"build-manager-direct"};

    REQUIRE(Project::Get().Create(temp.Path().string(), "DirectBuildGame"));

    BuildProfile& profile = Project::Get().GetBuildProfile();
    profile.gameName = "ConfiguredGame";
    profile.outputPath = "build/custom";
    profile.window.width = 1366;
    profile.window.height = 768;
    profile.window.fullscreen = true;
    REQUIRE(Project::Get().SaveBuildProfile());

    BuildManager manager;
    const fs::path scenePath = fs::path(Project::Get().GetScenesPath()) / "main.json";

    manager.Build(scenePath.string(), nullptr);

    const BuildProfile& afterBuild = Project::Get().GetBuildProfile();
    CHECK(afterBuild.gameName == "ConfiguredGame");
    CHECK(afterBuild.outputPath == "build/custom");
    CHECK(afterBuild.window.width == 1366);
    CHECK(afterBuild.window.height == 768);
    CHECK(afterBuild.window.fullscreen);

    Project::Get().Close();
}

#include "Editor/Editor.h"

Editor& Editor::Get() {
    static Editor instance;
    return instance;
}
