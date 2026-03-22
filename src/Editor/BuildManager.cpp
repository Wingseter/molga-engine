#include "BuildManager.h"
#include "EditorConstants.h"
#include "EditorTheme.h"
#include "GameBuilder.h"
#include "../Common/Log.h"
#include "../Core/SceneSerializer.h"
#include "../ECS/GameObject.h"
#include <imgui.h>

void BuildManager::RenderBuildWindow(const std::string& currentScenePath) {
    if (!showBuildWindow) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
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

    BuildSettings settings;
    settings.gameName = buildGameName;
    settings.outputPath = buildOutputPath;
    settings.mainScene = mainScene;
    settings.windowWidth = buildWidth;
    settings.windowHeight = buildHeight;
    settings.fullscreen = buildFullscreen;

    isBuilding = true;

    GameBuilder& builder = GameBuilder::Get();
    if (builder.Build(settings)) {
        Log::Info("Editor", "Build successful!");
        Log::Info("Editor", "Output: " + settings.outputPath + "/" + settings.gameName);
    } else {
        Log::Error("Editor", "Build failed: " + builder.GetLastError());
    }

    isBuilding = false;
}
