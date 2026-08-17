#include "doctest.h"
#include "Scripting/ScriptCompiler.h"
#include "SmokeTestSupport.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

TEST_CASE("ScriptCompiler subfolder script template creation, discovery, and CMake generation") {
    test_support::TempDirectory temp{"script-compiler"};
    const fs::path projectRoot = temp.Path();

    ScriptCompiler& compiler = ScriptCompiler::Get();
    compiler.SetProjectPath(projectRoot.string());

    // 1. Create a script in root Scripts/ directory
    bool ok = compiler.CreateScriptTemplate("PlayerController", "");
    REQUIRE(ok);

    fs::path scriptsRoot = projectRoot / "Scripts";
    CHECK(fs::exists(scriptsRoot / "PlayerController.h"));
    CHECK(fs::exists(scriptsRoot / "PlayerController.cpp"));

    // 2. Create a script in subdirectory Scripts/Enemies
    std::string enemiesDir = (scriptsRoot / "Enemies").string();
    ok = compiler.CreateScriptTemplate("Enemy", enemiesDir);
    REQUIRE(ok);

    CHECK(fs::exists(scriptsRoot / "Enemies" / "Enemy.h"));
    CHECK(fs::exists(scriptsRoot / "Enemies" / "Enemy.cpp"));

    // 3. A casing difference in the logical Scripts root must still map into
    // the configured Scripts tree on every host filesystem.
    std::string lowerEnemiesDir = (projectRoot / "scripts" / "NPCs").string();
    ok = compiler.CreateScriptTemplate("NPC", lowerEnemiesDir);
    REQUIRE(ok);

    CHECK(fs::exists(scriptsRoot / "NPCs" / "NPC.h"));
    CHECK(fs::exists(scriptsRoot / "NPCs" / "NPC.cpp"));

    // 4. Discover all scripts recursively
    auto discovered = compiler.DiscoverScripts();
    // We expect at least PlayerController, Enemy, and NPC
    CHECK(discovered.size() >= 3);

    bool foundPlayer = false;
    bool foundEnemy = false;
    bool foundNPC = false;

    for (const auto& script : discovered) {
        if (script.name == "PlayerController") foundPlayer = true;
        if (script.name == "Enemy") foundEnemy = true;
        if (script.name == "NPC") foundNPC = true;
    }

    CHECK(foundPlayer);
    CHECK(foundEnemy);
    CHECK(foundNPC);

    // 5. Generate CMakeLists.txt and verify relative paths are correct
    ok = compiler.GenerateCMakeLists();
    REQUIRE(ok);

    fs::path cmakePath = scriptsRoot / "CMakeLists.txt";
    REQUIRE(fs::exists(cmakePath));

    std::ifstream cmakeFile(cmakePath);
    std::stringstream cmakeBuf;
    cmakeBuf << cmakeFile.rdbuf();
    std::string cmakeContent = cmakeBuf.str();

    // Verify relative paths in SCRIPT_SOURCES
    CHECK(cmakeContent.find("PlayerController.cpp") != std::string::npos);
    CHECK(cmakeContent.find("Enemies/Enemy.cpp") != std::string::npos);
    CHECK(cmakeContent.find("NPCs/NPC.cpp") != std::string::npos);

    // 6. Generate ScriptExports.cpp and verify relative paths in #include
    fs::path exportsPath = scriptsRoot / "ScriptExports.cpp";
    REQUIRE(fs::exists(exportsPath));

    std::ifstream exportsFile(exportsPath);
    std::stringstream exportsBuf;
    exportsBuf << exportsFile.rdbuf();
    std::string exportsContent = exportsBuf.str();

    CHECK(exportsContent.find("#include \"PlayerController.h\"") != std::string::npos);
    CHECK(exportsContent.find("#include \"Enemies/Enemy.h\"") != std::string::npos);
    CHECK(exportsContent.find("#include \"NPCs/NPC.h\"") != std::string::npos);
}
