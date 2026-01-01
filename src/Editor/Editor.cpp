#include "Editor.h"
#include "EditorState.h"
#include "Windows/HierarchyWindow.h"
#include "Windows/InspectorWindow.h"
#include "Windows/ProjectBrowserWindow.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../Core/SceneSerializer.h"
#include "../Core/GameBuilder.h"
#include "../Core/Project.h"
#include "../Time.h"
#include <imgui.h>
#include <iostream>
#include <cstring>

Editor& Editor::Get() {
    static Editor instance;
    return instance;
}

void Editor::Init() {
    hierarchyWindow = std::make_unique<HierarchyWindow>();
    inspectorWindow = std::make_unique<InspectorWindow>();
    projectBrowserWindow = std::make_unique<ProjectBrowserWindow>();

    // Connect hierarchy selection to inspector
    hierarchyWindow->SetSelectionCallback([](GameObject* obj) {
        Editor::Get().inspectorWindow->SetTarget(obj);
    });
}

void Editor::Shutdown() {
    hierarchyWindow.reset();
    inspectorWindow.reset();
    projectBrowserWindow.reset();
}

void Editor::Update(float dt) {
    // Editor-specific updates can go here
}

void Editor::RenderGUI() {
    RenderMenuBar();

    // Windows
    if (showHierarchy && hierarchyWindow) {
        hierarchyWindow->OnGUI();
    }

    if (showInspector && inspectorWindow) {
        inspectorWindow->OnGUI();
    }

    // Project browser
    if (showProjectBrowser && projectBrowserWindow) {
        projectBrowserWindow->OnGUI();
    }

    // Stats window
    if (showStats) {
        ImGui::SetNextWindowPos(ImVec2(10, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
        ImGui::Begin("Stats", &showStats);
        ImGui::Text("FPS: %.1f", Time::GetFPS());
        ImGui::Text("Delta Time: %.3f ms", Time::GetDeltaTime() * 1000.0f);
        ImGui::Text("Frame: %d", Time::GetFrameCount());
        ImGui::End();
    }

    // Build window
    if (showBuildWindow) {
        RenderBuildWindow();
    }
}

void Editor::RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                NewScene();
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                OpenScene();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                SaveScene();
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                SaveSceneAs();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // Request quit
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Create Empty")) {
                CreateGameObject("GameObject");
            }
            if (ImGui::BeginMenu("2D Object")) {
                if (ImGui::MenuItem("Sprite")) {
                    auto obj = CreateGameObject("Sprite");
                    if (obj) {
                        obj->AddComponent<SpriteRenderer>();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &showInspector);
            ImGui::MenuItem("Project", nullptr, &showProjectBrowser);
            ImGui::MenuItem("Stats", nullptr, &showStats);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build Settings...")) {
                showBuildWindow = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build Game", "Ctrl+B")) {
                BuildGame();
            }
            ImGui::EndMenu();
        }

        // Play controls
        RenderPlayControls();

        ImGui::EndMainMenuBar();
    }
}

void Editor::RenderPlayControls() {
    ImGui::Separator();

    EditorState& editorState = EditorState::Get();

    if (editorState.IsEditMode()) {
        if (ImGui::Button("Play")) {
            editorState.Play();
        }
    } else if (editorState.IsPlayMode()) {
        if (ImGui::Button("Pause")) {
            editorState.Pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            editorState.Stop();
        }
    } else if (editorState.IsPaused()) {
        if (ImGui::Button("Resume")) {
            editorState.Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            editorState.Stop();
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (editorState.IsEditMode()) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Edit");
    } else if (editorState.IsPlayMode()) {
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Playing");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Paused");
    }
}

void Editor::SetGameObjects(std::vector<std::shared_ptr<GameObject>>* objects) {
    gameObjects = objects;
    if (hierarchyWindow) {
        hierarchyWindow->SetGameObjects(objects);
    }
}

GameObject* Editor::GetSelectedObject() const {
    return hierarchyWindow ? hierarchyWindow->GetSelectedObject() : nullptr;
}

void Editor::SetSelectedObject(GameObject* obj) {
    if (hierarchyWindow) {
        hierarchyWindow->SetSelectedObject(obj);
    }
    if (inspectorWindow) {
        inspectorWindow->SetTarget(obj);
    }
}

std::shared_ptr<GameObject> Editor::CreateGameObject(const std::string& name) {
    if (!gameObjects) return nullptr;

    auto obj = std::make_shared<GameObject>(name);
    obj->AddComponent<Transform>();
    gameObjects->push_back(obj);
    sceneModified = true;

    return obj;
}

void Editor::NewScene() {
    if (!gameObjects) return;

    gameObjects->clear();
    currentScenePath.clear();
    sceneModified = false;

    if (hierarchyWindow) {
        hierarchyWindow->SetSelectedObject(nullptr);
    }
    if (inspectorWindow) {
        inspectorWindow->SetTarget(nullptr);
    }

    std::cout << "[Editor] New scene created" << std::endl;
}

void Editor::SaveScene() {
    if (!gameObjects) return;

    if (currentScenePath.empty()) {
        SaveSceneAs();
        return;
    }

    if (SceneSerializer::SaveScene(currentScenePath, *gameObjects)) {
        sceneModified = false;
    }
}

void Editor::SaveSceneAs() {
    if (!gameObjects) return;

    // Simple file path input (in a real editor, use native file dialog)
    static char pathBuffer[256] = "scene.json";
    static bool showSaveDialog = false;

    showSaveDialog = true;

    // For now, use a default path
    currentScenePath = "scene.json";
    if (SceneSerializer::SaveScene(currentScenePath, *gameObjects)) {
        sceneModified = false;
        std::cout << "[Editor] Scene saved to: " << currentScenePath << std::endl;
    }
}

void Editor::OpenScene() {
    if (!gameObjects) return;

    // For now, use a default path
    std::string filepath = "scene.json";

    if (SceneSerializer::LoadScene(filepath, *gameObjects)) {
        currentScenePath = filepath;
        sceneModified = false;

        if (hierarchyWindow) {
            hierarchyWindow->SetSelectedObject(nullptr);
            hierarchyWindow->SetGameObjects(gameObjects);
        }
        if (inspectorWindow) {
            inspectorWindow->SetTarget(nullptr);
        }
    }
}

void Editor::RenderBuildWindow() {
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build Settings", &showBuildWindow)) {
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

        // Show current scene info
        if (!currentScenePath.empty()) {
            ImGui::Text("Main Scene: %s", currentScenePath.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning: No scene saved!");
            ImGui::Text("Save your scene first (File > Save Scene)");
        }

        ImGui::Separator();

        // Build progress
        GameBuilder& builder = GameBuilder::Get();
        if (isBuilding) {
            ImGui::ProgressBar(builder.GetProgress(), ImVec2(-1, 0));
            ImGui::Text("%s", builder.GetCurrentStep().c_str());
        }

        // Build button
        ImGui::Spacing();
        if (ImGui::Button("Build Game", ImVec2(120, 30))) {
            BuildGame();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 30))) {
            showBuildWindow = false;
        }

        // Show last error if any
        if (!builder.GetLastError().empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", builder.GetLastError().c_str());
        }
    }
    ImGui::End();
}

void Editor::BuildGame() {
    // Save scene first if needed
    if (currentScenePath.empty()) {
        currentScenePath = "scene.json";
    }

    if (gameObjects) {
        SceneSerializer::SaveScene(currentScenePath, *gameObjects);
    }

    // Setup build settings
    BuildSettings settings;
    settings.gameName = buildGameName;
    settings.outputPath = buildOutputPath;
    settings.mainScene = currentScenePath;
    settings.windowWidth = buildWidth;
    settings.windowHeight = buildHeight;
    settings.fullscreen = buildFullscreen;

    isBuilding = true;

    // Build the game
    GameBuilder& builder = GameBuilder::Get();
    if (builder.Build(settings)) {
        std::cout << "[Editor] Build successful!" << std::endl;
        std::cout << "[Editor] Output: " << settings.outputPath << "/" << settings.gameName << std::endl;
    } else {
        std::cerr << "[Editor] Build failed: " << builder.GetLastError() << std::endl;
    }

    isBuilding = false;
}
