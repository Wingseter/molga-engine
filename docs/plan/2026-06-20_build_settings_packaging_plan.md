# Build Settings and Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Before code changes, use `superpowers:test-driven-development`; before claiming completion, use `superpowers:verification-before-completion`.

**Goal:** Turn Molga Engine's current build window and package copier into a reliable 2D game build pipeline with persistent build profiles, explicit startup scenes, safer output handling, and platform-aware package validation.

**Architecture:** Keep runtime-facing build data in `molga_core`, with editor UI only editing and invoking it. Add a persistent build profile JSON under each project, make `GameBuilder` consume that profile, stage packages atomically, and make runtime startup fail clearly when required package files are missing. Treat this as Phase 3-1 of `docs/plan/playable-editor-vertical-slice/phase-1-3_roadmap.md`, but implement it now because settings/build gaps are blocking real game iteration.

**Tech Stack:** C++17, `<filesystem>`, nlohmann/json, doctest, CMake/CTest, GLFW/OpenGL runtime package smoke tests

---

## Current State

Already in place:

- `molga_core`, `molga_engine`, and `molga_runtime` are separated in `CMakeLists.txt`.
- `PathService`, `BuildManifest`, and `PackageLayout` exist.
- `GameBuilder` copies project `Assets`, engine `Shaders`, selected scene, `game.json`, and runtime executable.
- `game.json` bundles `ProjectSettings` and `Input` actions.
- Debug/Release/ASan/UBSan presets exist in `CMakePresets.json`.
- CI runs macOS Debug/Release plus sanitizer jobs.
- `smoke_end_to_end` builds a fixture project and runs the packaged runtime.

Remaining gaps this plan closes:

- Build settings are transient UI fields, not a project asset.
- Build only has a loose `BuildSettings` struct with no schema/version/profile model.
- Scene save/open is still centered on `Scenes/main.json`.
- Additional scenes are copied only if present; missing listed scenes do not fail the build.
- Package executable naming is inconsistent on non-macOS Unix: validation expects `gameName`, copy creates `gameName.exe`.
- Package output deletes the destination directly instead of building in a staging directory first.
- Runtime continues with defaults if `game.json` is missing, which hides broken packages.
- Package path constants disagree on `assets` vs `Assets`.
- CI does not build Linux/Windows packages, and package tests do not cover platform naming helpers.

## Non-Goals

- No AssetDatabase/GUID import pipeline in this plan.
- No app store packaging, code signing, notarization, installer, or Web/mobile export.
- No new scripting language or script AOT pipeline.
- No broad editor UI redesign.
- No texture/audio compression pipeline.

---

## File Structure

Core package/build model:

- Create: `src/Core/BuildProfile.h`
- Create: `src/Core/BuildProfile.cpp`
- Modify: `src/Core/PackageLayout.h`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `src/Core/PathConstants.h`
- Modify: `src/Core/BuildManifest.h`
- Modify: `src/Core/BuildManifest.cpp`
- Modify: `CMakeLists.txt`

Editor build flow:

- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Editor/BuildManager.h`
- Modify: `src/Editor/BuildManager.cpp`
- Modify: `src/Editor/SceneOperations.h`
- Modify: `src/Editor/SceneOperations.cpp`
- Modify: `src/Editor/Project.h`
- Modify: `src/Editor/Project.cpp`

Runtime startup:

- Modify: `src/runtime_main.cpp`

Tests and smoke:

- Create: `tests/test_build_profile.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_runtime_smoke.cpp`
- Modify: `tests/smoke/create_fixture.cmake`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Modify: `tests/CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`

Project files written by engine:

- Create per project: `ProjectSettings/build_profile.json`

---

## Data Contracts

### Build Profile Schema

`ProjectSettings/build_profile.json`:

```json
{
  "schemaVersion": 1,
  "gameName": "MyGame",
  "productVersion": "0.1.0",
  "companyName": "Molga",
  "outputPath": "Builds/MyGame",
  "startupScene": "Scenes/main.json",
  "scenes": [
    "Scenes/main.json"
  ],
  "window": {
    "width": 800,
    "height": 600,
    "fullscreen": false,
    "resizable": true
  },
  "developmentBuild": false,
  "showConsole": false,
  "target": "host"
}
```

Rules:

- Paths stored in the profile are project-relative unless explicitly absolute.
- `startupScene` must be included in `scenes`.
- Build fails if any listed scene is missing.
- Build fails if `gameName` is empty or contains path separators.
- `target` initially accepts only `host`; the field exists so platform-specific profiles can be added without schema churn.

### Runtime Game Config Schema

Packaged `game.json`:

```json
{
  "schemaVersion": 1,
  "gameName": "MyGame",
  "productVersion": "0.1.0",
  "companyName": "Molga",
  "mainScene": "Scenes/main.json",
  "scenes": [
    "Scenes/main.json"
  ],
  "windowWidth": 800,
  "windowHeight": 600,
  "fullscreen": false,
  "resizable": true,
  "developmentBuild": false,
  "projectSettings": {},
  "inputActions": []
}
```

Rules:

- Packaged folders use `Assets`, `Scenes`, and `Shaders` exactly.
- Runtime must return a non-zero exit code in smoke mode if `game.json`, main scene, `Shaders`, or `Assets` is missing.
- Non-smoke runtime should still show a clear stderr message and exit instead of silently running defaults for a packaged game.

---

## Task 1: Normalize Package Constants and Executable Naming

**Files:**

- Modify: `src/Core/PathConstants.h`
- Modify: `src/Core/PackageLayout.h`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `tests/test_game_builder.cpp`

- [ ] **Step 1: Write failing tests for package constants and executable naming**

Add to `tests/test_game_builder.cpp`:

```cpp
#include "Core/PackageLayout.h"
#include "Core/PathConstants.h"
#include "doctest.h"

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
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
cmake --build --preset debug --target test_game_builder -j4
ctest --preset debug -R test_game_builder --output-on-failure
```

Expected: FAIL because `Paths::Build::ASSETS` is currently `assets`, and `PackageLayout::ExecutableNameFor` does not exist.

- [ ] **Step 3: Update package constants**

Change `src/Core/PathConstants.h`:

```cpp
namespace Build {
    constexpr const char* ASSETS = "Assets";
    constexpr const char* SCENES = "Scenes";
    constexpr const char* SHADERS = "Shaders";
}
```

- [ ] **Step 4: Add executable naming helper**

In `src/Core/PackageLayout.h`, add:

```cpp
static std::string ExecutableNameFor(const std::string& gameName);
```

In `src/Core/PackageLayout.cpp`, implement:

```cpp
std::string PackageLayout::ExecutableNameFor(const std::string& gameName) {
#ifdef _WIN32
    return gameName + ".exe";
#else
    return gameName;
#endif
}
```

- [ ] **Step 5: Use one naming helper in GameBuilder**

In `src/Editor/GameBuilder.cpp`, replace both local executable-name branches with:

```cpp
const std::string execName = PackageLayout::ExecutableNameFor(settings.gameName);
```

and:

```cpp
const std::string execName = PackageLayout::ExecutableNameFor(gameName);
```

- [ ] **Step 6: Verify**

Run:

```bash
cmake --build --preset debug --target test_game_builder molga_engine -j4
ctest --preset debug -R test_game_builder --output-on-failure
```

Expected: PASS.

---

## Task 2: Add Persistent BuildProfile

**Files:**

- Create: `src/Core/BuildProfile.h`
- Create: `src/Core/BuildProfile.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_build_profile.cpp`

- [ ] **Step 1: Write failing BuildProfile tests**

Create `tests/test_build_profile.cpp`:

```cpp
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
```

- [ ] **Step 2: Register and confirm failure**

Add to `tests/CMakeLists.txt`:

```cmake
molga_add_test(test_build_profile test_build_profile.cpp)
```

Run:

```bash
cmake --build --preset debug --target test_build_profile -j4
```

Expected: FAIL because `Core/BuildProfile.h` does not exist.

- [ ] **Step 3: Create BuildProfile.h**

Create `src/Core/BuildProfile.h`:

```cpp
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct BuildWindowSettings {
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    bool resizable = true;
};

struct BuildProfile {
    int schemaVersion = 1;
    std::string gameName = "MyGame";
    std::string productVersion = "0.1.0";
    std::string companyName = "Molga";
    std::string outputPath = "Builds/MyGame";
    std::string startupScene = "Scenes/main.json";
    std::vector<std::string> scenes = {"Scenes/main.json"};
    BuildWindowSettings window;
    bool developmentBuild = false;
    bool showConsole = false;
    std::string target = "host";

    static BuildProfile Defaults(const std::string& projectName);

    bool Validate(std::string& errorOut) const;
    nlohmann::json Serialize() const;
    bool Deserialize(const nlohmann::json& j);
};
```

- [ ] **Step 4: Create BuildProfile.cpp**

Create `src/Core/BuildProfile.cpp`:

```cpp
#include "Core/BuildProfile.h"

#include <algorithm>
#include <filesystem>

namespace {
bool ContainsPathSeparator(const std::string& value) {
    return value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos;
}
}

BuildProfile BuildProfile::Defaults(const std::string& projectName) {
    BuildProfile profile;
    if (!projectName.empty()) {
        profile.gameName = projectName;
        profile.outputPath = "Builds/" + projectName;
    }
    return profile;
}

bool BuildProfile::Validate(std::string& errorOut) const {
    if (schemaVersion != 1) {
        errorOut = "Unsupported build profile schemaVersion: " + std::to_string(schemaVersion);
        return false;
    }
    if (gameName.empty()) {
        errorOut = "Build profile gameName must not be empty.";
        return false;
    }
    if (ContainsPathSeparator(gameName)) {
        errorOut = "Build profile gameName must not contain path separators.";
        return false;
    }
    if (startupScene.empty()) {
        errorOut = "Build profile startupScene must not be empty.";
        return false;
    }
    if (std::find(scenes.begin(), scenes.end(), startupScene) == scenes.end()) {
        errorOut = "Build profile startupScene must be included in scenes.";
        return false;
    }
    if (window.width <= 0 || window.height <= 0) {
        errorOut = "Build profile window size must be positive.";
        return false;
    }
    if (target != "host") {
        errorOut = "Unsupported build target: " + target;
        return false;
    }
    errorOut.clear();
    return true;
}

nlohmann::json BuildProfile::Serialize() const {
    nlohmann::json j;
    j["schemaVersion"] = schemaVersion;
    j["gameName"] = gameName;
    j["productVersion"] = productVersion;
    j["companyName"] = companyName;
    j["outputPath"] = outputPath;
    j["startupScene"] = startupScene;
    j["scenes"] = scenes;
    j["window"] = {
        {"width", window.width},
        {"height", window.height},
        {"fullscreen", window.fullscreen},
        {"resizable", window.resizable}
    };
    j["developmentBuild"] = developmentBuild;
    j["showConsole"] = showConsole;
    j["target"] = target;
    return j;
}

bool BuildProfile::Deserialize(const nlohmann::json& j) {
    try {
        schemaVersion = j.value("schemaVersion", 1);
        gameName = j.value("gameName", gameName);
        productVersion = j.value("productVersion", productVersion);
        companyName = j.value("companyName", companyName);
        outputPath = j.value("outputPath", outputPath);
        startupScene = j.value("startupScene", startupScene);
        target = j.value("target", target);
        developmentBuild = j.value("developmentBuild", developmentBuild);
        showConsole = j.value("showConsole", showConsole);

        if (j.contains("scenes") && j["scenes"].is_array()) {
            scenes.clear();
            for (const auto& scene : j["scenes"]) {
                if (scene.is_string()) {
                    scenes.push_back(scene.get<std::string>());
                }
            }
        }

        if (j.contains("window") && j["window"].is_object()) {
            const auto& w = j["window"];
            window.width = w.value("width", window.width);
            window.height = w.value("height", window.height);
            window.fullscreen = w.value("fullscreen", window.fullscreen);
            window.resizable = w.value("resizable", window.resizable);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
```

- [ ] **Step 5: Add source to build**

Add `src/Core/BuildProfile.cpp` to `ENGINE_SOURCES` in `CMakeLists.txt`.

- [ ] **Step 6: Verify**

Run:

```bash
cmake --build --preset debug --target test_build_profile -j4
ctest --preset debug -R test_build_profile --output-on-failure
```

Expected: PASS.

---

## Task 3: Persist BuildProfile in Project

**Files:**

- Modify: `src/Editor/Project.h`
- Modify: `src/Editor/Project.cpp`
- Create: `tests/test_project_build_profile.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add Project build profile API**

In `src/Editor/Project.h`, add:

```cpp
std::string GetBuildProfilePath() const;
bool LoadBuildProfile();
bool SaveBuildProfile() const;
BuildProfile& GetBuildProfile() { return buildProfile; }
const BuildProfile& GetBuildProfile() const { return buildProfile; }
```

and include:

```cpp
#include "../Core/BuildProfile.h"
```

Add private member:

```cpp
BuildProfile buildProfile;
```

- [ ] **Step 2: Initialize profile on project create/open**

In `Project::Create`, after project settings and input actions are saved:

```cpp
buildProfile = BuildProfile::Defaults(projectName);
SaveBuildProfile();
```

In `Project::Open`, after input actions load:

```cpp
if (!LoadBuildProfile()) {
    buildProfile = BuildProfile::Defaults(projectName);
    SaveBuildProfile();
}
```

In `Project::Close`:

```cpp
buildProfile = BuildProfile::Defaults("");
```

- [ ] **Step 3: Implement load/save helpers**

In `src/Editor/Project.cpp`, add:

```cpp
std::string Project::GetBuildProfilePath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / "ProjectSettings" / "build_profile.json").string();
}

bool Project::LoadBuildProfile() {
    const std::string path = GetBuildProfilePath();
    if (path.empty() || !fs::exists(path)) return false;

    try {
        std::ifstream file(path);
        nlohmann::json j;
        file >> j;
        BuildProfile loaded = BuildProfile::Defaults(projectName);
        if (!loaded.Deserialize(j)) return false;
        std::string error;
        if (!loaded.Validate(error)) {
            Log::Error("Project", "Invalid build profile: " + error);
            return false;
        }
        buildProfile = loaded;
        return true;
    } catch (const std::exception& e) {
        Log::Error("Project", "Failed to load build profile: " + std::string(e.what()));
        return false;
    }
}

bool Project::SaveBuildProfile() const {
    const std::string path = GetBuildProfilePath();
    if (path.empty()) return false;

    try {
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << buildProfile.Serialize().dump(4);
        return true;
    } catch (const std::exception& e) {
        Log::Error("Project", "Failed to save build profile: " + std::string(e.what()));
        return false;
    }
}
```

- [ ] **Step 4: Add project build profile persistence test**

Create `tests/test_project_build_profile.cpp`:

```cpp
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
```

Update `tests/CMakeLists.txt`:

```cmake
molga_add_test(test_project_build_profile test_project_build_profile.cpp)
target_sources(test_project_build_profile PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
)
target_include_directories(test_project_build_profile PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```

- [ ] **Step 5: Verify project profile persistence**

Run:

```bash
cmake --build --preset debug --target test_project_build_profile -j4
ctest --preset debug -R test_project_build_profile --output-on-failure
```

Expected: PASS.

---

## Task 4: Make GameBuilder Consume BuildProfile and Fail on Missing Scenes

**Files:**

- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Editor/BuildManager.cpp`
- Modify: `tests/test_game_builder.cpp`

- [ ] **Step 1: Replace BuildSettings surface with BuildProfile-backed settings**

In `src/Editor/GameBuilder.h`, change:

```cpp
struct BuildSettings {
    BuildProfile profile;
    std::string projectRoot;
};
```

Update every current `BuildSettings` caller in this same task. Do not keep a compatibility helper; the profile is the single build settings source after this task.

- [ ] **Step 2: Resolve project-relative paths in GameBuilder**

Add helper in `src/Editor/GameBuilder.cpp`:

```cpp
static fs::path ResolveProjectPath(const fs::path& projectRoot, const std::string& stored) {
    fs::path p(stored);
    return p.is_absolute() ? p : projectRoot / p;
}
```

- [ ] **Step 3: Validate every scene listed in profile**

In `GameBuilder::Build`, before output directory creation:

```cpp
std::string profileError;
if (!settings.profile.Validate(profileError)) {
    lastError = profileError;
    return false;
}

BuildManifest manifest;
manifest.requiredFiles.push_back(Project::Get().GetAssetsPath());
manifest.requiredFiles.push_back(PathService::Get().EngineResource(Paths::Build::SHADERS).string());
manifest.requiredFiles.push_back((PathService::Get().ExecutableDir() / "molga_runtime").string());
for (const auto& scene : settings.profile.scenes) {
    manifest.requiredFiles.push_back(ResolveProjectPath(settings.projectRoot, scene).string());
}
```

- [ ] **Step 4: Copy startup scene and additional scenes by profile**

Change scene copying:

```cpp
const fs::path scenesDir = fs::path(outputPath) / Paths::Build::SCENES;
fs::create_directories(scenesDir);

for (const auto& sceneRel : settings.profile.scenes) {
    const fs::path src = ResolveProjectPath(settings.projectRoot, sceneRel);
    if (!fs::exists(src)) {
        lastError = "Scene listed in build profile is missing: " + src.string();
        return false;
    }
    const fs::path dest = scenesDir / fs::path(sceneRel).filename();
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
}
```

When generating `game.json`, set:

```cpp
config["mainScene"] = std::string(Paths::Build::SCENES) + "/" +
                      fs::path(settings.profile.startupScene).filename().string();
```

- [ ] **Step 5: Update BuildManager to use Project build profile**

In `BuildManager::Build`, replace ad hoc field transfer with:

```cpp
BuildProfile& profile = Project::Get().GetBuildProfile();
profile.gameName = buildGameName;
profile.outputPath = buildOutputPath;
profile.window.width = buildWidth;
profile.window.height = buildHeight;
profile.window.fullscreen = buildFullscreen;
Project::Get().SaveBuildProfile();

BuildSettings settings;
settings.profile = profile;
settings.projectRoot = Project::Get().GetPath();
```

- [ ] **Step 6: Verify**

Run:

```bash
cmake --build --preset debug --target molga_engine test_game_builder -j4
ctest --preset debug -R test_game_builder --output-on-failure
```

Expected: PASS.

---

## Task 5: Stage Builds Before Replacing Output

**Files:**

- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Core/PathService.h`
- Modify: `src/Core/PathService.cpp`
- Modify: `tests/test_path_service.cpp`

- [ ] **Step 1: Extend output path safety tests**

Add to `tests/test_path_service.cpp`:

```cpp
TEST_CASE("IsSafeOutputPath rejects project root and engine root when provided") {
    std::string why;
    CHECK_FALSE(PathService::IsSafeOutputPath("/tmp/project", why, "/tmp/project", "/tmp/engine"));
    CHECK_FALSE(PathService::IsSafeOutputPath("/tmp/engine", why, "/tmp/project", "/tmp/engine"));
    CHECK(PathService::IsSafeOutputPath("/tmp/project/Builds/Game", why, "/tmp/project", "/tmp/engine"));
}
```

- [ ] **Step 2: Add overload with protected roots**

In `src/Core/PathService.h`:

```cpp
static bool IsSafeOutputPath(
    const std::filesystem::path& path,
    std::string& reason,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& engineRoot);
```

In `PathService.cpp`, reject exact canonical matches for `projectRoot` and `engineRoot`.

Implementation:

```cpp
bool PathService::IsSafeOutputPath(
    const std::filesystem::path& path,
    std::string& reason,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& engineRoot) {
    if (!IsSafeOutputPath(path, reason)) {
        return false;
    }

    std::error_code ec;
    fs::path output = fs::weakly_canonical(path, ec);
    if (ec) {
        output = path.lexically_normal();
    }

    auto rejectsExactRoot = [&](const fs::path& root, const char* label) {
        if (root.empty()) return false;
        std::error_code rootEc;
        fs::path protectedRoot = fs::weakly_canonical(root, rootEc);
        if (rootEc) {
            protectedRoot = root.lexically_normal();
        }
        if (output == protectedRoot) {
            reason = std::string(label) + " root";
            return true;
        }
        return false;
    };

    if (rejectsExactRoot(projectRoot, "project")) return false;
    if (rejectsExactRoot(engineRoot, "engine")) return false;
    return true;
}
```

- [ ] **Step 3: Build into staging directory**

In `GameBuilder::Build`:

```cpp
const fs::path finalOutput = ResolveProjectPath(settings.projectRoot, settings.profile.outputPath);
const fs::path stagingOutput = finalOutput.string() + ".staging";

if (fs::exists(stagingOutput)) {
    fs::remove_all(stagingOutput);
}
fs::create_directories(stagingOutput);
```

Run all copy/generate operations against `stagingOutput`.

- [ ] **Step 4: Replace final output only after package validation**

After `PackageLayout::Validate(stagingOutput, execName, packageError)` succeeds:

```cpp
if (fs::exists(finalOutput)) {
    fs::remove_all(finalOutput);
}
fs::rename(stagingOutput, finalOutput);
```

If any build step fails, clean only `stagingOutput` and leave the previous output untouched.

- [ ] **Step 5: Verify**

Run:

```bash
cmake --build --preset debug --target test_path_service molga_engine -j4
ctest --preset debug -R test_path_service --output-on-failure
```

Expected: PASS.

---

## Task 6: Make Runtime Package Config Mandatory

**Files:**

- Modify: `src/runtime_main.cpp`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Modify: `tests/test_runtime_smoke.cpp`

- [ ] **Step 1: Make config failure explicit**

In `runtime_main.cpp`, replace fallback behavior:

```cpp
if (!LoadGameConfig(configPath, config)) {
    std::cerr << "Packaged game config is required: " << configPath << std::endl;
    if (smoke->enabled) {
        SmokeReport report;
        report.executable = "molga_runtime";
        report.status = "error";
        report.message = "Missing or invalid game.json";
        report.Save(smoke->reportPath);
    }
    return 4;
}
```

- [ ] **Step 2: Validate required package directories**

After loading config:

```cpp
const auto exeDir = PathService::Get().ExecutableDir();
for (const auto& required : { "Assets", "Scenes", "Shaders" }) {
    if (!std::filesystem::exists(exeDir / required)) {
        std::cerr << "Missing package directory: " << (exeDir / required) << std::endl;
        return 4;
    }
}
```

- [ ] **Step 3: Verify smoke still passes**

Run:

```bash
cmake --build --preset debug --target molga_runtime -j4
ctest --preset debug -R smoke_end_to_end --output-on-failure
```

Expected: PASS.

---

## Task 7: Build Settings UI Edits the Persistent Profile

**Files:**

- Modify: `src/Editor/BuildManager.h`
- Modify: `src/Editor/BuildManager.cpp`
- Modify: `src/Editor/Project.cpp`

- [ ] **Step 1: Load UI fields from profile when window opens**

Add `BuildManager::LoadFromProjectProfile()`:

```cpp
void BuildManager::LoadFromProjectProfile() {
    if (!Project::Get().IsOpen()) return;
    const BuildProfile& profile = Project::Get().GetBuildProfile();
    std::snprintf(buildGameName, sizeof(buildGameName), "%s", profile.gameName.c_str());
    std::snprintf(buildOutputPath, sizeof(buildOutputPath), "%s", profile.outputPath.c_str());
    buildWidth = profile.window.width;
    buildHeight = profile.window.height;
    buildFullscreen = profile.window.fullscreen;
}
```

Call it when `ShowWindow()` changes from hidden to visible.

- [ ] **Step 2: Save UI fields back before build**

Add `BuildManager::SaveToProjectProfile()`:

```cpp
bool BuildManager::SaveToProjectProfile() {
    if (!Project::Get().IsOpen()) return false;
    BuildProfile& profile = Project::Get().GetBuildProfile();
    profile.gameName = buildGameName;
    profile.outputPath = buildOutputPath;
    profile.window.width = buildWidth;
    profile.window.height = buildHeight;
    profile.window.fullscreen = buildFullscreen;
    std::string error;
    if (!profile.Validate(error)) {
        Log::Error("Editor", "Invalid build profile: " + error);
        return false;
    }
    return Project::Get().SaveBuildProfile();
}
```

- [ ] **Step 3: Display startup scene and scene list**

In `RenderBuildWindow`, show:

```cpp
const BuildProfile& profile = Project::Get().GetBuildProfile();
ImGui::Text("Startup Scene: %s", profile.startupScene.c_str());
ImGui::Text("Scenes in Build: %d", static_cast<int>(profile.scenes.size()));
```

Add one button:

```cpp
if (ImGui::Button("Use Current Scene as Startup")) {
    BuildProfile& profile = Project::Get().GetBuildProfile();
    profile.startupScene = Project::Get().GetRelativePath(currentScenePath);
    if (std::find(profile.scenes.begin(), profile.scenes.end(), profile.startupScene) == profile.scenes.end()) {
        profile.scenes.push_back(profile.startupScene);
    }
    Project::Get().SaveBuildProfile();
}
```

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build --preset debug --target molga_engine -j4
```

Expected: build succeeds. Manual check: open a project, open Build Settings, change game name/output, build, close/reopen project, and confirm values persist.

---

## Task 8: Decouple Scene Save/Open From Hardcoded main.json

**Files:**

- Modify: `src/Editor/SceneOperations.h`
- Modify: `src/Editor/SceneOperations.cpp`
- Modify: `src/Editor/ProjectBrowserWindow.cpp`
- Modify: `src/Editor/Editor.cpp`

- [ ] **Step 1: Add explicit scene path save/open methods**

In `SceneOperations.h`, add:

```cpp
bool SaveSceneAsPath(const std::vector<std::shared_ptr<GameObject>>& objects,
                     const std::string& path);
bool OpenScenePath(std::vector<std::shared_ptr<GameObject>>& objects,
                   const std::string& path);
```

In `SceneOperations.cpp`, implement:

```cpp
bool SceneOperations::SaveSceneAsPath(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& path) {
    currentScenePath = path;
    if (SceneSerializer::SaveScene(currentScenePath, objects)) {
        sceneModified = false;
        Log::Info("Editor", "Scene saved to: " + currentScenePath);
        return true;
    }
    return false;
}

bool SceneOperations::OpenScenePath(
    std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& path) {
    if (SceneSerializer::LoadScene(path, objects)) {
        currentScenePath = path;
        sceneModified = false;
        return true;
    }
    return false;
}
```

- [ ] **Step 2: Preserve File menu default behavior through path-based API**

Leave existing File > Save Scene behavior unchanged for this task, but route it through `SaveSceneAsPath(..., Project::Get().GetScenesPath()/main.json)` so the default path is explicit and future UI can pass a different path.

- [ ] **Step 3: Open `.json` scene from Project Browser double click**

In `ProjectBrowserWindow::DrawFileGrid`, when a non-directory `.json` under project `Scenes` is double-clicked, call a new `Editor::OpenScenePath(entry.path)`.

Add to `Editor.h`:

```cpp
bool OpenScenePath(const std::string& path);
```

Implement in `Editor.cpp`:

```cpp
bool Editor::OpenScenePath(const std::string& path) {
    if (!gameObjects) return false;
    if (sceneOps.OpenScenePath(*gameObjects, path)) {
        SetSelectedObject(nullptr);
        SetGameObjects(gameObjects);
        return true;
    }
    return false;
}
```

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build --preset debug --target molga_engine -j4
```

Expected: build succeeds. Manual check: create `Scenes/level1.json` in Project Browser, double-click it, and confirm editor current scene changes.

---

## Task 9: Extend Smoke Tests for Build Profiles

**Files:**

- Modify: `tests/smoke/create_fixture.cmake`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Modify: `src/main.cpp`

- [ ] **Step 1: Fixture writes build profile**

In `tests/smoke/create_fixture.cmake`, write:

```cmake
file(WRITE "${FIXTURE_ROOT}/ProjectSettings/build_profile.json" [=[
{
  "schemaVersion": 1,
  "gameName": "SmokeGame",
  "productVersion": "0.1.0",
  "companyName": "Molga",
  "outputPath": "Builds/SmokeGame",
  "startupScene": "Scenes/main.json",
  "scenes": ["Scenes/main.json"],
  "window": {
    "width": 640,
    "height": 360,
    "fullscreen": false,
    "resizable": true
  },
  "developmentBuild": true,
  "showConsole": false,
  "target": "host"
}
]=])
```

- [ ] **Step 2: Smoke build uses project profile by default**

In `RunSmokeBuild` in `src/main.cpp`, remove ad hoc `BuildSettings` scene assignment and use:

```cpp
BuildSettings settings;
settings.profile = Project::Get().GetBuildProfile();
settings.projectRoot = Project::Get().GetPath();
settings.profile.outputPath = options.outputRoot.string();
```

- [ ] **Step 3: Check packaged game config metadata**

In `tests/smoke/run_end_to_end.cmake`, after reading `game.json`, assert:

```cmake
foreach(expected
    [["gameName": "SmokeGame"]]
    [["productVersion": "0.1.0"]]
    [["developmentBuild": true]]
    [["mainScene": "Scenes/main.json"]]
)
    string(FIND "${game_config}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "game.json is missing ${expected}\n${game_config}")
    endif()
endforeach()
```

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build --preset debug --target molga_engine molga_runtime -j4
ctest --preset debug -R smoke_end_to_end --output-on-failure
```

Expected: PASS.

---

## Task 10: CI Matrix for Package Portability

**Files:**

- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add Linux build-test job**

Add a Linux job:

```yaml
  build-test-linux:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        preset: [debug, release]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install GLFW dependencies
        run: sudo apt-get update && sudo apt-get install -y libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
      - name: Configure
        run: cmake --preset ${{ matrix.preset }}
      - name: Build
        run: cmake --build --preset ${{ matrix.preset }} -j$(nproc)
      - name: Run unit tests
        run: ctest --preset ${{ matrix.preset }} -L unit -LE smoke --output-on-failure
```

- [ ] **Step 2: Keep GUI smoke macOS-only initially**

Do not run `smoke_end_to_end` on Linux until a headless OpenGL strategy is chosen. The Linux job still catches executable naming, compile, and unit-level package contract regressions.

- [ ] **Step 3: Verify current local build after workflow edit**

Run:

```bash
cmake --preset debug
cmake --build --preset debug -j4
ctest --preset debug -L unit -LE smoke --output-on-failure
```

Expected: local macOS unit suite passes.

---

## Completion Gate

The plan is complete when all of these pass:

```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
cmake --build --preset release -j4
ctest --preset release --output-on-failure
cmake --build --preset asan -j4
ctest --preset asan -LE e2e --output-on-failure
cmake --build --preset ubsan -j4
ctest --preset ubsan -LE e2e --output-on-failure
```

Manual verification:

- Create/open a project.
- Open Build Settings.
- Change game name, output path, window size, and fullscreen.
- Save/build.
- Reopen project and confirm build settings persisted.
- Create a second scene, open it from Project Browser, set it as startup scene, build.
- Run packaged executable from a different current working directory.
- Delete `game.json` from package and confirm runtime exits with a clear error.

## Follow-Up Plans

This plan intentionally leaves these for separate documents:

- AssetDatabase and GUID-based references.
- Multi-platform packaging details for `.app`, Windows app folders, and Linux archive artifacts.
- Script build separation between editor hot reload and packaged runtime.
- Build report UI with warnings/errors/artifact links.
