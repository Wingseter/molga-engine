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
