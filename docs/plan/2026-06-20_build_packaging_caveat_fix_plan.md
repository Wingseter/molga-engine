# Build Packaging Caveat Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Before code changes, use `superpowers:test-driven-development`; before claiming completion, use `superpowers:verification-before-completion`.

**Goal:** Fix the three verification caveats found after the build settings and packaging implementation: recoverable package finalization, safe direct build profile syncing, and complete `SceneOperations` path APIs.

**Architecture:** Keep package replacement logic in `molga_core` as a small, testable filesystem utility, then make `GameBuilder` consume it after `PackageLayout::Validate`. Make `BuildManager` explicitly track which project profile its UI buffers came from so direct Build Game cannot save default buffer values over the project profile. Finish the path-based scene operations declared in `SceneOperations.h` and cover them with editor-source unit tests.

**Tech Stack:** C++17, `<filesystem>`, doctest, CMake/CTest, existing `molga_core`, existing editor source files

---

## Context

Verification passed for Debug, Release, ASan, UBSan, and smoke tests, but three caveats remain:

- `src/Editor/GameBuilder.cpp` removes the existing final output before renaming `.staging` into place. If final rename fails after deletion, the previous package is lost.
- `BuildManager::Build()` saves UI buffer values before ensuring they were loaded from the current project. A menu-triggered direct build can save default `"MyGame"` and `"build/export"` values over a valid project profile.
- `SceneOperations.h` declares `SaveSceneAsPath()` and `OpenScenePath()`, but `SceneOperations.cpp` does not define them. Future calls will fail at link time.

## File Structure

Package finalization:

- Create: `src/Core/PackageFinalizer.h`
- Create: `src/Core/PackageFinalizer.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_package_finalizer.cpp`
- Modify: `tests/CMakeLists.txt`

Build profile syncing:

- Modify: `src/Editor/BuildManager.h`
- Modify: `src/Editor/BuildManager.cpp`
- Create: `tests/test_build_manager.cpp`
- Modify: `tests/CMakeLists.txt`

Scene path operations:

- Modify: `src/Editor/SceneOperations.cpp`
- Create: `tests/test_scene_operations.cpp`
- Modify: `tests/CMakeLists.txt`

Verification:

- Run focused unit tests, full Debug/Release suites, sanitizer suites, and whitespace checks.

---

### Task 1: Recoverable Package Finalization

**Files:**

- Create: `src/Core/PackageFinalizer.h`
- Create: `src/Core/PackageFinalizer.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_package_finalizer.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for package finalization**

Create `tests/test_package_finalizer.cpp`:

```cpp
#include "Core/PackageFinalizer.h"
#include "SmokeTestSupport.h"
#include "doctest.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("FinalizeStagedPackage replaces final output after staging is ready") {
    test_support::TempDirectory temp{"package-finalizer-replace"};
    const fs::path finalOutput = temp.Path() / "Game";
    const fs::path stagingOutput = temp.Path() / "Game.staging";
    const fs::path backupOutput = temp.Path() / "Game.previous";

    test_support::WriteText(finalOutput / "old.txt", "old build");
    test_support::WriteText(stagingOutput / "new.txt", "new build");

    const auto result = PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);

    REQUIRE(result.ok);
    CHECK(fs::exists(finalOutput / "new.txt"));
    CHECK_FALSE(fs::exists(finalOutput / "old.txt"));
    CHECK_FALSE(fs::exists(stagingOutput));
    CHECK_FALSE(fs::exists(backupOutput));
}

TEST_CASE("FinalizeStagedPackage keeps final output when staging is missing") {
    test_support::TempDirectory temp{"package-finalizer-missing-staging"};
    const fs::path finalOutput = temp.Path() / "Game";
    const fs::path stagingOutput = temp.Path() / "Game.staging";

    test_support::WriteText(finalOutput / "old.txt", "old build");

    const auto result = PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("staging") != std::string::npos);
    CHECK(fs::exists(finalOutput / "old.txt"));
}
```

- [ ] **Step 2: Register the failing test**

Modify `tests/CMakeLists.txt` near the existing build/package tests:

```cmake
molga_add_test(test_package_finalizer test_package_finalizer.cpp)
target_include_directories(test_package_finalizer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```

- [ ] **Step 3: Run the test and confirm it fails for the right reason**

Run:

```bash
cmake --build --preset debug --target test_package_finalizer -j4
```

Expected: build fails because `Core/PackageFinalizer.h` does not exist.

- [ ] **Step 4: Add the finalizer interface**

Create `src/Core/PackageFinalizer.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace PackageFinalizer {

struct Result {
    bool ok = false;
    std::string error;

    explicit operator bool() const { return ok; }
};

Result FinalizeStagedPackage(const std::filesystem::path& stagingOutput,
                             const std::filesystem::path& finalOutput);

}  // namespace PackageFinalizer
```

- [ ] **Step 5: Implement the finalizer with a backup-and-restore path**

Create `src/Core/PackageFinalizer.cpp`:

```cpp
#include "PackageFinalizer.h"

#include <system_error>

namespace PackageFinalizer {
namespace fs = std::filesystem;

namespace {

Result Fail(const std::string& error) {
    return Result{false, error};
}

std::string QuotePath(const fs::path& path) {
    return "'" + path.string() + "'";
}

}  // namespace

Result FinalizeStagedPackage(const fs::path& stagingOutput,
                             const fs::path& finalOutput) {
    if (stagingOutput.empty()) {
        return Fail("staging output path is empty");
    }
    if (finalOutput.empty()) {
        return Fail("final output path is empty");
    }

    std::error_code ec;
    const bool stagingExists = fs::exists(stagingOutput, ec);
    if (ec) {
        return Fail("could not inspect staging output " + QuotePath(stagingOutput) +
                    ": " + ec.message());
    }
    const bool stagingIsDirectory = fs::is_directory(stagingOutput, ec);
    if (ec) {
        return Fail("could not inspect staging output " + QuotePath(stagingOutput) +
                    ": " + ec.message());
    }
    if (!stagingExists || !stagingIsDirectory) {
        return Fail("staging output does not exist or is not a directory: " +
                    QuotePath(stagingOutput));
    }

    const fs::path backupOutput(finalOutput.string() + ".previous");

    fs::remove_all(backupOutput, ec);
    if (ec) {
        return Fail("could not remove stale backup " + QuotePath(backupOutput) +
                    ": " + ec.message());
    }

    const bool hadFinal = fs::exists(finalOutput, ec);
    if (ec) {
        return Fail("could not inspect final output " + QuotePath(finalOutput) +
                    ": " + ec.message());
    }

    if (hadFinal) {
        fs::rename(finalOutput, backupOutput, ec);
        if (ec) {
            return Fail("could not move current output " + QuotePath(finalOutput) +
                        " to backup " + QuotePath(backupOutput) + ": " +
                        ec.message());
        }
    }

    fs::rename(stagingOutput, finalOutput, ec);
    if (ec) {
        const std::string renameError = ec.message();

        std::error_code cleanupEc;
        if (fs::exists(finalOutput, cleanupEc)) {
            fs::remove_all(finalOutput, cleanupEc);
        }

        std::error_code restoreEc;
        if (hadFinal && fs::exists(backupOutput, restoreEc)) {
            fs::rename(backupOutput, finalOutput, restoreEc);
        }

        if (restoreEc) {
            return Fail("could not move staged output into place: " + renameError +
                        "; previous output restore failed: " +
                        restoreEc.message());
        }

        return Fail("could not move staged output into place; previous output "
                    "was restored: " +
                    renameError);
    }

    if (hadFinal) {
        fs::remove_all(backupOutput, ec);
        if (ec) {
            return Fail("built package is in place, but backup cleanup failed for " +
                        QuotePath(backupOutput) + ": " + ec.message());
        }
    }

    return Result{true, ""};
}

}  // namespace PackageFinalizer
```

- [ ] **Step 6: Add the finalizer to `molga_core`**

Modify the `ENGINE_SOURCES` list in `CMakeLists.txt` by adding:

```cmake
    src/Core/PackageFinalizer.cpp
```

Place it next to `src/Core/PackageLayout.cpp`.

- [ ] **Step 7: Use the finalizer in `GameBuilder`**

Modify the includes in `src/Editor/GameBuilder.cpp`:

```cpp
#include "../Core/PackageFinalizer.h"
```

Replace the current final output removal and rename block:

```cpp
    // Atomic swap: staging -> final
    try {
        if (fs::exists(finalOutput)) {
            fs::remove_all(finalOutput);
        }
        fs::rename(stagingOutput, finalOutput);
    } catch (const std::exception& e) {
        lastError = "Failed to finalize output: " + std::string(e.what());
        cleanupStaging();
        return false;
    }
```

with:

```cpp
    const auto finalizeResult =
        PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);
    if (!finalizeResult) {
        lastError = "Failed to finalize output: " + finalizeResult.error;
        cleanupStaging();
        return false;
    }
```

- [ ] **Step 8: Run focused package tests**

Run:

```bash
cmake --build --preset debug --target test_package_finalizer test_game_builder -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset debug -R 'test_package_finalizer|test_game_builder' --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 9: Commit package finalization fix**

Run:

```bash
git add CMakeLists.txt src/Core/PackageFinalizer.h src/Core/PackageFinalizer.cpp src/Editor/GameBuilder.cpp tests/CMakeLists.txt tests/test_package_finalizer.cpp
git commit -m "fix: preserve previous package during finalization"
```

---

### Task 2: Direct Build Profile Sync Guard

**Files:**

- Modify: `src/Editor/BuildManager.h`
- Modify: `src/Editor/BuildManager.cpp`
- Create: `tests/test_build_manager.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write a failing direct-build profile test**

Create `tests/test_build_manager.cpp`:

```cpp
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
```

- [ ] **Step 2: Register the failing test**

Modify `tests/CMakeLists.txt`:

```cmake
molga_add_test(test_build_manager test_build_manager.cpp)
target_sources(test_build_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/BuildManager.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/GameBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
)
target_include_directories(test_build_manager PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_build_manager PRIVATE imgui)
```

- [ ] **Step 3: Run the test and confirm it fails**

Run:

```bash
cmake --build --preset debug --target test_build_manager -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset debug -R test_build_manager --output-on-failure
```

Expected: test fails because the direct build saves default UI buffer values into `Project::Get().GetBuildProfile()`.

- [ ] **Step 4: Track the loaded project profile in `BuildManager`**

Modify `src/Editor/BuildManager.h`:

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>

class GameObject;

class BuildManager {
public:
    void RenderBuildWindow(const std::string& currentScenePath);
    void Build(const std::string& scenePath,
               const std::vector<std::shared_ptr<GameObject>>* objects);

    bool IsShowingWindow() const { return showBuildWindow; }
    void ShowWindow();

    bool LoadFromProjectProfile();
    bool EnsureProfileLoaded();
    bool SaveToProjectProfile();

private:
    char buildGameName[128] = "MyGame";
    char buildOutputPath[256] = "build/export";
    int buildWidth = 800;
    int buildHeight = 600;
    bool buildFullscreen = false;
    bool isBuilding = false;
    bool showBuildWindow = false;
    bool wasShowing = false;
    bool profileLoaded = false;
    std::string loadedProjectPath;
};
```

- [ ] **Step 5: Load the current project before saving build fields**

In `src/Editor/BuildManager.cpp`, replace `ShowWindow()` and `LoadFromProjectProfile()` with:

```cpp
void BuildManager::ShowWindow() {
    showBuildWindow = true;
    wasShowing = EnsureProfileLoaded();
}

bool BuildManager::LoadFromProjectProfile() {
    if (!Project::Get().IsOpen()) {
        profileLoaded = false;
        loadedProjectPath.clear();
        return false;
    }

    const BuildProfile& profile = Project::Get().GetBuildProfile();
    std::snprintf(buildGameName, sizeof(buildGameName), "%s", profile.gameName.c_str());
    std::snprintf(buildOutputPath, sizeof(buildOutputPath), "%s", profile.outputPath.c_str());
    buildWidth = profile.window.width;
    buildHeight = profile.window.height;
    buildFullscreen = profile.window.fullscreen;
    loadedProjectPath = Project::Get().GetPath();
    profileLoaded = true;
    return true;
}

bool BuildManager::EnsureProfileLoaded() {
    if (!Project::Get().IsOpen()) {
        profileLoaded = false;
        loadedProjectPath.clear();
        return false;
    }

    if (!profileLoaded || loadedProjectPath != Project::Get().GetPath()) {
        return LoadFromProjectProfile();
    }

    return true;
}
```

In `RenderBuildWindow()`, replace:

```cpp
    if (!wasShowing) {
        LoadFromProjectProfile();
        wasShowing = true;
    }
```

with:

```cpp
    if (!wasShowing) {
        wasShowing = EnsureProfileLoaded();
    }
```

In `Build()`, insert this before `SaveToProjectProfile()`:

```cpp
    if (!EnsureProfileLoaded()) {
        Log::Error("Editor", "Cannot build because no project build profile is loaded.");
        return;
    }
```

The build flow should then remain:

```cpp
    if (!SaveToProjectProfile()) {
        return;
    }
```

- [ ] **Step 6: Run focused BuildManager tests**

Run:

```bash
cmake --build --preset debug --target test_build_manager -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset debug -R test_build_manager --output-on-failure
```

Expected: test passes even though the actual game build can fail later due missing package inputs.

- [ ] **Step 7: Commit BuildManager profile guard**

Run:

```bash
git add src/Editor/BuildManager.h src/Editor/BuildManager.cpp tests/CMakeLists.txt tests/test_build_manager.cpp
git commit -m "fix: load build profile before direct builds"
```

---

### Task 3: Complete SceneOperations Path APIs

**Files:**

- Modify: `src/Editor/SceneOperations.cpp`
- Create: `tests/test_scene_operations.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for explicit scene paths**

Create `tests/test_scene_operations.cpp`:

```cpp
#include "Editor/SceneOperations.h"
#include "Core/SceneSerializer.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "SmokeTestSupport.h"
#include "doctest.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("SceneOperations SaveSceneAsPath saves exact path and updates state") {
    test_support::TempDirectory temp{"scene-operations-save-path"};
    const fs::path scenePath = temp.Path() / "Scenes" / "level_01.json";

    std::vector<std::shared_ptr<GameObject>> objects;
    auto player = std::make_shared<GameObject>("Player");
    player->AddComponent<Transform>(32.0f, 48.0f);
    objects.push_back(player);

    SceneOperations operations;
    operations.MarkModified();

    REQUIRE(operations.SaveSceneAsPath(objects, scenePath.string()));

    CHECK(operations.GetCurrentPath() == scenePath.string());
    CHECK_FALSE(operations.IsModified());
    CHECK(fs::exists(scenePath));

    std::vector<std::shared_ptr<GameObject>> loaded;
    REQUIRE(SceneSerializer::LoadScene(scenePath.string(), loaded));
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0]->GetName() == "Player");
}

TEST_CASE("SceneOperations OpenScenePath loads exact path and updates state") {
    test_support::TempDirectory temp{"scene-operations-open-path"};
    const fs::path scenePath = temp.Path() / "Scenes" / "boss.json";

    std::vector<std::shared_ptr<GameObject>> source;
    source.push_back(std::make_shared<GameObject>("Boss"));
    REQUIRE(SceneSerializer::SaveScene(scenePath.string(), source));

    std::vector<std::shared_ptr<GameObject>> loaded;
    SceneOperations operations;
    operations.MarkModified();

    REQUIRE(operations.OpenScenePath(loaded, scenePath.string()));

    CHECK(operations.GetCurrentPath() == scenePath.string());
    CHECK_FALSE(operations.IsModified());
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0]->GetName() == "Boss");
}
```

- [ ] **Step 2: Register the failing test**

Modify `tests/CMakeLists.txt`:

```cmake
molga_add_test(test_scene_operations test_scene_operations.cpp)
target_sources(test_scene_operations PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/SceneOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
)
target_include_directories(test_scene_operations PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```

- [ ] **Step 3: Run the test and confirm it fails at link time**

Run:

```bash
cmake --build --preset debug --target test_scene_operations -j4
```

Expected: link fails because `SceneOperations::SaveSceneAsPath()` and `SceneOperations::OpenScenePath()` are declared but not defined.

- [ ] **Step 4: Implement explicit path save/open and reuse them from existing methods**

Modify `src/Editor/SceneOperations.cpp`.

Replace `SaveSceneAs()` with:

```cpp
bool SceneOperations::SaveSceneAs(const std::vector<std::shared_ptr<GameObject>>& objects) {
    namespace fs = std::filesystem;

    fs::path filepath = EditorConstants::DEFAULT_SCENE_FILE;
    if (Project::Get().IsOpen()) {
        filepath = fs::path(Project::Get().GetScenesPath()) / "main.json";
    }

    return SaveSceneAsPath(objects, filepath.string());
}
```

Replace `OpenScene()` with:

```cpp
bool SceneOperations::OpenScene(std::vector<std::shared_ptr<GameObject>>& objects) {
    namespace fs = std::filesystem;

    fs::path filepath = EditorConstants::DEFAULT_SCENE_FILE;
    if (Project::Get().IsOpen()) {
        filepath = fs::path(Project::Get().GetScenesPath()) / "main.json";
    }

    return OpenScenePath(objects, filepath.string());
}
```

Add these definitions after `OpenScene()`:

```cpp
bool SceneOperations::SaveSceneAsPath(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& path) {
    if (path.empty()) {
        Log::Error("Editor", "Cannot save scene because the target path is empty.");
        return false;
    }

    namespace fs = std::filesystem;
    const fs::path target(path);
    const fs::path parent = target.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }

    if (SceneSerializer::SaveScene(path, objects)) {
        currentScenePath = path;
        sceneModified = false;
        Log::Info("Editor", "Scene saved to: " + currentScenePath);
        return true;
    }

    return false;
}

bool SceneOperations::OpenScenePath(std::vector<std::shared_ptr<GameObject>>& objects,
                                    const std::string& path) {
    if (path.empty()) {
        Log::Error("Editor", "Cannot open scene because the target path is empty.");
        return false;
    }

    if (SceneSerializer::LoadScene(path, objects)) {
        currentScenePath = path;
        sceneModified = false;
        return true;
    }

    return false;
}
```

- [ ] **Step 5: Run focused SceneOperations tests**

Run:

```bash
cmake --build --preset debug --target test_scene_operations -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset debug -R test_scene_operations --output-on-failure
```

Expected: tests pass.

- [ ] **Step 6: Commit SceneOperations API completion**

Run:

```bash
git add src/Editor/SceneOperations.cpp tests/CMakeLists.txt tests/test_scene_operations.cpp
git commit -m "fix: define explicit scene path operations"
```

---

### Task 4: Regression Verification

**Files:**

- No source changes beyond Tasks 1-3.

- [ ] **Step 1: Build focused targets**

Run:

```bash
cmake --build --preset debug --target test_package_finalizer test_build_manager test_scene_operations test_game_builder test_path_service molga_engine molga_runtime -j4
```

Expected: build succeeds.

- [ ] **Step 2: Run focused debug tests**

Run:

```bash
ctest --preset debug -R 'test_package_finalizer|test_build_manager|test_scene_operations|test_game_builder|test_path_service|smoke_end_to_end' --output-on-failure
```

Expected: selected tests pass.

- [ ] **Step 3: Run full debug suite**

Run:

```bash
ctest --preset debug --output-on-failure
```

Expected: all debug tests pass.

- [ ] **Step 4: Run release suite**

Run:

```bash
cmake --build --preset release -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset release --output-on-failure
```

Expected: all release tests pass.

- [ ] **Step 5: Run sanitizer suites**

Run:

```bash
cmake --build --preset asan -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset asan -LE e2e --output-on-failure
```

Expected: ASan non-e2e tests pass.

Run:

```bash
cmake --build --preset ubsan -j4
```

Expected: build succeeds.

Run:

```bash
ctest --preset ubsan -LE e2e --output-on-failure
```

Expected: UBSan non-e2e tests pass.

- [ ] **Step 6: Check patch formatting**

Run:

```bash
git diff --check
```

Expected: no output and exit code 0.

- [ ] **Step 7: Final commit**

If Tasks 1-3 were not committed separately, run:

```bash
git add CMakeLists.txt src/Core/PackageFinalizer.h src/Core/PackageFinalizer.cpp src/Editor/GameBuilder.cpp src/Editor/BuildManager.h src/Editor/BuildManager.cpp src/Editor/SceneOperations.cpp tests/CMakeLists.txt tests/test_package_finalizer.cpp tests/test_build_manager.cpp tests/test_scene_operations.cpp
git commit -m "fix: harden build packaging follow-ups"
```

---

## Success Criteria

- Existing package output is not removed until a validated staging package is ready to move into place.
- If finalization fails before the staged directory replaces the final output, the previous output directory remains present whenever filesystem rollback succeeds.
- Direct Build Game from the editor menu preserves project `build_profile.json` values instead of writing default UI buffer values.
- `SceneOperations::SaveSceneAsPath()` and `SceneOperations::OpenScenePath()` link and work with exact paths.
- Debug, Release, ASan, UBSan, and smoke verification remain green.

## Risks and Notes

- The finalizer relies on staging and final output living in the same parent directory. `GameBuilder` already uses `finalOutput + ".staging"`, so the replacement uses same-volume `std::filesystem::rename`.
- If backup cleanup fails after a successful package replacement, the build should report failure because leftover `.previous` output needs operator attention.
- The BuildManager test links editor sources and `imgui`. If that target becomes too heavy, keep the behavior test but move profile buffer syncing into a smaller helper class.
- The SceneOperations tests intentionally exercise path methods directly so future scene workflow changes cannot leave declarations without definitions.
