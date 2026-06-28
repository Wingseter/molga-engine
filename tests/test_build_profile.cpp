#include "Core/BuildProfile.h"
#include "doctest.h"

TEST_CASE("BuildProfile defaults include main scene") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    CHECK(profile.schemaVersion == 1);
    CHECK(profile.gameName == "MyGame");
    CHECK(profile.startupScene == "Scenes/main.json");
    REQUIRE(profile.scenes.size() == 1);
    CHECK(profile.scenes[0] == "Scenes/main.json");
}

TEST_CASE("BuildProfile validation rejects empty game name") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    profile.gameName = "";
    std::string error;
    CHECK_FALSE(profile.Validate(error));
    CHECK(error.find("gameName") != std::string::npos);
}

TEST_CASE("BuildProfile validation requires startup scene in scene list") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    profile.startupScene = "Scenes/level1.json";
    std::string error;
    CHECK_FALSE(profile.Validate(error));
    CHECK(error.find("startupScene") != std::string::npos);
}

TEST_CASE("BuildProfile round trips JSON") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    profile.productVersion = "1.2.3";
    profile.companyName = "Studio";
    profile.window.width = 1280;
    profile.window.height = 720;
    profile.window.fullscreen = true;
    profile.developmentBuild = true;

    nlohmann::json j = profile.Serialize();
    BuildProfile restored;
    REQUIRE(restored.Deserialize(j));

    CHECK(restored.gameName == "MyGame");
    CHECK(restored.productVersion == "1.2.3");
    CHECK(restored.companyName == "Studio");
    CHECK(restored.window.width == 1280);
    CHECK(restored.window.height == 720);
    CHECK(restored.window.fullscreen);
    CHECK(restored.developmentBuild);
}

#include "Core/BuildPlan.h"
#include "Core/PackageLayout.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("BuildPlanBuilder validates and creates correct plan") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    profile.scenes = {"Scenes/main.json", "Scenes/levels/start.json"};
    profile.startupScene = "Scenes/levels/start.json";

    BuildPlan plan;
    std::string error;
    bool success = BuildPlanBuilder::Build(profile, "/my/project/root", "host", "", plan, error);
    REQUIRE(success);
    CHECK(plan.executableName == PackageLayout::ExecutableNameFor("MyGame"));
    REQUIRE(plan.sceneEntries.size() == 2);
    CHECK(plan.sceneEntries[0].sourceProfilePath == "Scenes/main.json");
    CHECK(plan.sceneEntries[0].packagePath == "Scenes/main.json");
    CHECK(plan.sceneEntries[1].sourceProfilePath == "Scenes/levels/start.json");
    CHECK(plan.sceneEntries[1].packagePath == "Scenes/levels/start.json");
    CHECK(plan.startupScenePackagePath == "Scenes/levels/start.json");
}

TEST_CASE("BuildPlanBuilder detects duplicate scene package paths") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    // These two map to the same package path "Scenes/start.json" because of case normalization of the "scenes/" prefix
    profile.scenes = {"Scenes/start.json", "scenes/start.json"};
    profile.startupScene = "Scenes/start.json";

    BuildPlan plan;
    std::string error;
    bool success = BuildPlanBuilder::Build(profile, "/my/project/root", "host", "", plan, error);
    CHECK_FALSE(success);
    CHECK(error.find("Duplicate scene package path detected") != std::string::npos);
}

TEST_CASE("BuildPlanBuilder rejects escaping scene paths") {
    BuildProfile profile = BuildProfile::Defaults("MyGame");
    profile.scenes = {"../outside.json"};
    profile.startupScene = "../outside.json";

    BuildPlan plan;
    std::string error;
    bool success = BuildPlanBuilder::Build(profile, "/my/project/root", "host", "", plan, error);
    CHECK_FALSE(success);
    CHECK(error.find("escapes project root") != std::string::npos);
}

TEST_CASE("PackageLayout validates custom scenes listed in game.json") {
    fs::path tmpDir = fs::temp_directory_path() / "molga_custom_pkg_layout_test";
    fs::create_directories(tmpDir);

    std::string exeName = PackageLayout::ExecutableNameFor("TestCustomGame");
    { std::ofstream(tmpDir / exeName); }
    fs::create_directories(tmpDir / "Assets");
    fs::create_directories(tmpDir / "Shaders");

    // Case 1: Custom mainScene and scenes listed in game.json
    {
        std::ofstream f(tmpDir / "game.json");
        f << R"({
            "mainScene": "Scenes/levels/start.json",
            "scenes": [
                "Scenes/levels/start.json",
                "Scenes/other.json"
            ]
        })";
    }

    std::string error;
    // Should fail because Scenes/levels/start.json does not exist yet
    bool valid = PackageLayout::Validate(tmpDir, "TestCustomGame", error);
    CHECK_FALSE(valid);
    CHECK(error.find("Missing package entry") != std::string::npos);

    // Create the required custom scenes
    fs::create_directories(tmpDir / "Scenes/levels");
    { std::ofstream(tmpDir / "Scenes/levels/start.json"); }
    { std::ofstream(tmpDir / "Scenes/other.json"); }

    // Should now pass
    valid = PackageLayout::Validate(tmpDir, "TestCustomGame", error);
    CHECK(valid);
    CHECK(error.empty());

    fs::remove_all(tmpDir);
}

