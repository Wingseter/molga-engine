#include "Core/GameConfig.h"
#include "Scripting/ScriptPackageLoader.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/Script.h"
#include "doctest.h"

TEST_CASE("GameConfig parses without script manifest") {
    std::string jsonStr = R"({
        "gameName": "My Awesome Game",
        "mainScene": "scenes/level1.json",
        "windowWidth": 1024,
        "windowHeight": 768,
        "fullscreen": true
    })";

    GameConfig config;
    bool success = LoadGameConfigFromString(jsonStr, config);
    REQUIRE(success);
    CHECK(config.gameName == "My Awesome Game");
    CHECK(config.mainScene == "scenes/level1.json");
    CHECK(config.windowWidth == 1024);
    CHECK(config.windowHeight == 768);
    CHECK(config.fullscreen == true);
    CHECK_FALSE(config.scripts.enabled);
}

TEST_CASE("GameConfig parses with script manifest") {
    std::string jsonStr = R"({
        "gameName": "My Script Game",
        "scripts": {
            "enabled": true,
            "library": "Scripts/libUserScripts.dylib",
            "apiVersion": 1,
            "buildHash": "abcdef123456"
        }
    })";

    GameConfig config;
    bool success = LoadGameConfigFromString(jsonStr, config);
    REQUIRE(success);
    CHECK(config.gameName == "My Script Game");
    CHECK(config.scripts.enabled);
    CHECK(config.scripts.library == "Scripts/libUserScripts.dylib");
    CHECK(config.scripts.apiVersion == 1);
    CHECK(config.scripts.buildHash == "abcdef123456");
}

TEST_CASE("ScriptPackageLoader validation cases") {
    // Force linker to include Script vtable/methods to avoid dynamic loader lookup failure in tests
    {
        struct TestDummyScript : public Script {
            SCRIPT_CLASS(TestDummyScript)
        };
        TestDummyScript ds;
        nlohmann::json j;
        ds.Deserialize(j);
    }
    // 1. Script disabled
    {
        GameConfig config;
        config.scripts.enabled = false;
        config.scripts.library = "NonExistentPath.dylib";
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        CHECK(success);
        CHECK(error.empty());
    }

    // 2. Script enabled, empty path
    {
        GameConfig config;
        config.scripts.enabled = true;
        config.scripts.library = "";
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        CHECK_FALSE(success);
        CHECK(error.find("empty") != std::string::npos);
    }

    // 3. Script enabled, missing file
    {
        GameConfig config;
        config.scripts.enabled = true;
        config.scripts.library = "NonExistentPath.dylib";
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        CHECK_FALSE(success);
        CHECK(error.find("not found") != std::string::npos);
    }

    // 4. Script enabled, missing RegisterScripts
    {
        GameConfig config;
        config.scripts.enabled = true;
        config.scripts.library = DUMMY_MISSING_REGISTER_LIB_PATH;
        config.scripts.apiVersion = 1;
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        CHECK_FALSE(success);
        CHECK(error.find("RegisterScripts symbol not found") != std::string::npos);
    }

    // 5. Script enabled, missing GetScriptApiVersion
    {
        GameConfig config;
        config.scripts.enabled = true;
        config.scripts.library = DUMMY_MISSING_API_PATH;
        config.scripts.apiVersion = 1;
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        CHECK_FALSE(success);
        CHECK(error.find("GetScriptApiVersion symbol not found") != std::string::npos);
    }

    // 6. Script enabled, api version mismatch
    {
        GameConfig config;
        config.scripts.enabled = true;
        config.scripts.library = DUMMY_MISMATCH_API_PATH;
        config.scripts.apiVersion = 1;
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        CHECK_FALSE(success);
        CHECK(error.find("API version mismatch") != std::string::npos);
    }

    // 7. Script enabled, valid load
    {
        GameConfig config;
        config.scripts.enabled = true;
        config.scripts.library = DUMMY_VALID_LIB_PATH;
        config.scripts.apiVersion = 1;
        std::string error;
        bool success = ScriptPackageLoader::Load(config, false, "", error);
        INFO("Error message: " << error);
        CHECK(success);
        CHECK(error.empty());
        
        // Check ScriptManager registry
        auto loaded = ScriptManager::Get().GetLoadedLibraries();
        bool found = false;
        for (const auto& lib : loaded) {
            if (lib == DUMMY_VALID_LIB_PATH) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}
