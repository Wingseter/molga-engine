#include "Editor/Project.h"
#include "SmokeTestSupport.h"
#include "doctest.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("Project creates and reloads persistent build profile") {
    Project::Get().Close();
    test_support::TempDirectory temp{"project-build-profile"};

    REQUIRE(Project::Get().Create(temp.Path().string(), "ProfileGame"));
    const fs::path projectRoot = Project::Get().GetPath();
    const fs::path profilePath = Project::Get().GetBuildProfilePath();

    CHECK(fs::exists(profilePath));
    CHECK(Project::Get().GetBuildProfile().gameName == "ProfileGame");

    BuildProfile& profile = Project::Get().GetBuildProfile();
    profile.gameName = "RenamedGame";
    profile.window.width = 1024;
    profile.window.height = 576;
    REQUIRE(Project::Get().SaveBuildProfile());

    Project::Get().Close();
    REQUIRE(Project::Get().Open(projectRoot.string()));

    CHECK(Project::Get().GetBuildProfile().gameName == "RenamedGame");
    CHECK(Project::Get().GetBuildProfile().window.width == 1024);
    CHECK(Project::Get().GetBuildProfile().window.height == 576);

    Project::Get().Close();
}
