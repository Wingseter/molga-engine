#include "BuildManager.h"
#include "EditorConstants.h"
#include "EditorTheme.h"
#include "GameBuilder.h"
#include "Editor.h"
#include "Windows/ConsoleWindow.h"
#include "Project.h"
#include "../Common/Log.h"
#include "../Core/SceneSerializer.h"
#include "../ECS/GameObject.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

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

void BuildManager::RenderBuildWindow(const std::string& currentScenePath) {
    if (!showBuildWindow) {
        wasShowing = false;
        return;
    }

    if (!wasShowing) {
        wasShowing = EnsureProfileLoaded();
    }

    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(EditorConstants::WIN_BUILD_SETTINGS, &showBuildWindow)) {
        ImGui::Text("Game Build Settings");
        ImGui::Separator();

        ImGui::InputText("Game Name", buildGameName, sizeof(buildGameName));
        ImGui::InputText("Output Path", buildOutputPath, sizeof(buildOutputPath));

        ImGui::Separator();
        ImGui::Text("Window Settings");
        ImGui::InputInt("Width", &buildWidth);
        ImGui::InputInt("Height", &buildHeight);
        ImGui::Checkbox("Fullscreen", &buildFullscreen);

        ImGui::Separator();

        if (!currentScenePath.empty()) {
            ImGui::Text("Main Scene: %s", currentScenePath.c_str());
        } else {
            ImGui::TextColored(EditorTheme::WARNING_TEXT,
                               "Warning: No scene saved!");
            ImGui::Text("Save your scene first (File > Save Scene)");
        }

        if (Project::Get().IsOpen()) {
            const BuildProfile& profile = Project::Get().GetBuildProfile();
            ImGui::Text("Startup Scene: %s", profile.startupScene.c_str());
            ImGui::Text("Scenes in Build: %d", static_cast<int>(profile.scenes.size()));
            bool canUseCurrent = !currentScenePath.empty();
            if (!canUseCurrent) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Use Current Scene as Startup")) {
                BuildProfile& profileMutable = Project::Get().GetBuildProfile();
                profileMutable.startupScene = Project::Get().GetRelativePath(currentScenePath);
                if (std::find(profileMutable.scenes.begin(), profileMutable.scenes.end(), profileMutable.startupScene) == profileMutable.scenes.end()) {
                    profileMutable.scenes.push_back(profileMutable.startupScene);
                }
                Project::Get().SaveBuildProfile();
            }
            if (!canUseCurrent) {
                ImGui::EndDisabled();
            }
        }

        ImGui::Separator();

        GameBuilder& builder = GameBuilder::Get();
        if (isBuilding) {
            ImGui::ProgressBar(builder.GetProgress(), ImVec2(-1, 0));
            ImGui::Text("%s", builder.GetCurrentStep().c_str());
        }

        ImGui::Spacing();
        if (ImGui::Button("Build Game", ImVec2(120, 30))) {
            Build(currentScenePath, nullptr);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 30))) {
            showBuildWindow = false;
        }

        if (!builder.GetLastError().empty()) {
            ImGui::Separator();
            ImGui::TextColored(EditorTheme::ERROR_TEXT, "Error: %s",
                               builder.GetLastError().c_str());
        }
    }
    ImGui::End();
}

void BuildManager::Build(const std::string& scenePath,
                         const std::vector<std::shared_ptr<GameObject>>* objects) {
    std::string mainScene = scenePath;
    if (mainScene.empty()) {
        mainScene = EditorConstants::DEFAULT_SCENE_FILE;
    }

    if (objects) {
        SceneSerializer::SaveScene(mainScene, *objects);
    }

    if (auto* console = Editor::Get().GetWindowManager().GetAs<ConsoleWindow>(EditorConstants::WIN_CONSOLE)) {
        if (console->IsClearOnBuild()) {
            console->RequestClear();
        }
    }

    if (!EnsureProfileLoaded()) {
        Log::Error("Editor", "Cannot build because no project build profile is loaded.");
        return;
    }

    // Save UI fields to project build profile
    if (!SaveToProjectProfile()) {
        return;
    }

    BuildProfile& profile = Project::Get().GetBuildProfile();
    BuildSettings settings;
    settings.profile = profile;
    settings.projectRoot = Project::Get().GetPath();

    isBuilding = true;

    auto& tasks = Editor::Get().GetTaskService();
    molga::TaskId tid = tasks.Begin("Build Game", molga::TaskCategory::Build);
    tasks.Update(tid, 0.0f, "Starting build...");

    GameBuilder& builder = GameBuilder::Get();
    bool ok = builder.Build(settings);

    if (ok) {
        tasks.Update(tid, 0.2f, "Copying assets...");
        tasks.Update(tid, 0.4f, "Copying shaders...");
        tasks.Update(tid, 0.5f, "Copying scenes...");
        tasks.Update(tid, 0.7f, "Generating game configuration...");
        tasks.Update(tid, 0.9f, "Copying executable...");
        tasks.Update(tid, 1.0f, "Build successful!");
        tasks.Finish(tid, molga::TaskState::Succeeded);
        Log::Info("Editor", "Build successful!");
        Log::Info("Editor", "Output: " + settings.profile.outputPath + "/" + settings.profile.gameName);
    } else {
        tasks.Update(tid, builder.GetProgress(), "Step failed: " + builder.GetCurrentStep() + " - " + builder.GetLastError());
        tasks.Finish(tid, molga::TaskState::Failed);
        Log::Error("Editor", "Build failed: " + builder.GetLastError());
    }

    isBuilding = false;
}
