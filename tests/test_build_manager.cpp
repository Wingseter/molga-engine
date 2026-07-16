#include "Editor/BuildManager.h"
#include "Editor/EditorState.h"
#include "Editor/GameBuilder.h"
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

TEST_CASE("EditorState remains in Edit mode when entering Play is rejected") {
    EditorState& state = EditorState::Get();
    state.SetMode(EditorMode::Edit);

    int exitCalls = 0;
    state.SetPlayCallbacks(
        []() { return false; },
        [&]() { ++exitCalls; });
    state.Play();

    CHECK(state.IsEditMode());
    CHECK(exitCalls == 0);

    state.SetPlayCallbacks(
        []() { return true; },
        [&]() { ++exitCalls; });
    state.Play();
    CHECK(state.IsPlayMode());
    state.Stop();
    CHECK(state.IsEditMode());
    CHECK(exitCalls == 1);

    state.SetPlayCallbacks({}, {});
}

TEST_CASE("GameBuilder validates font GUIDs referenced only by prefabs") {
    Project::Get().Close();
    test_support::TempDirectory temp{"build-prefab-font-validation"};
    REQUIRE(Project::Get().Create(temp.Path().string(), "PrefabFontGame"));

    const fs::path root = Project::Get().GetPath();
    constexpr const char* prefabGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr const char* missingFontGuid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    test_support::WriteText(root / "Assets/Prefabs/FontOnly.prefab",
        nlohmann::json{
            {"guid", prefabGuid},
            {"version", "1.0"},
            {"gameObjects", nlohmann::json::array({{
                {"name", "Prefab Label"},
                {"id", 1u},
                {"active", true},
                {"parentId", -1},
                {"components", nlohmann::json::array({{
                    {"type", "UILabel"},
                    {"enabled", true},
                    {"text", "프리팹 전용 글꼴"},
                    {"fontGuid", missingFontGuid}
                }})}
            }})}
        }.dump(2));
    test_support::WriteText(root / "Scenes/main.json",
        nlohmann::json{
            {"version", "1.0"},
            {"name", "Prefab Font Scene"},
            {"gameObjects", nlohmann::json::array({{
                {"prefabInstance", {
                    {"guid", prefabGuid},
                    {"rootId", 100u},
                    {"parentId", -1},
                    {"modifications", nlohmann::json::array()}
                }}
            }})}
        }.dump(2));

    BuildProfile& profile = Project::Get().GetBuildProfile();
    profile.startupScene = "Scenes/main.json";
    profile.scenes = {"Scenes/main.json"};
    profile.outputPath = "Builds/PrefabFontGame";

    BuildSettings settings;
    settings.profile = profile;
    settings.projectRoot = root.string();
    CHECK_FALSE(GameBuilder::Get().Build(settings));
    CHECK(GameBuilder::Get().GetLastError().find(missingFontGuid) != std::string::npos);
    CHECK(GameBuilder::Get().GetLastError().find("unknown font GUID") != std::string::npos);

    // The same recursive traversal must reject cycles instead of treating an
    // already-seen active prefab as a successfully validated duplicate.
    test_support::WriteText(root / "Assets/Prefabs/FontOnly.prefab",
        nlohmann::json{
            {"guid", prefabGuid},
            {"version", "1.0"},
            {"gameObjects", nlohmann::json::array({
                {
                    {"name", "Cycle Root"}, {"id", 1u}, {"active", true},
                    {"parentId", -1}, {"components", nlohmann::json::array()}
                },
                {
                    {"prefabInstance", {
                        {"guid", prefabGuid}, {"rootId", 2u}, {"parentId", 1},
                        {"modifications", nlohmann::json::array()}
                    }}
                }
            })}
        }.dump(2));
    CHECK_FALSE(GameBuilder::Get().Build(settings));
    CHECK(GameBuilder::Get().GetLastError().find("Cyclic prefab reference") !=
          std::string::npos);

    Project::Get().Close();
}
