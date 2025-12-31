#include "Editor.h"
#include "EditorState.h"
#include "Windows/HierarchyWindow.h"
#include "Windows/InspectorWindow.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../Time.h"
#include <imgui.h>

Editor& Editor::Get() {
    static Editor instance;
    return instance;
}

void Editor::Init() {
    hierarchyWindow = std::make_unique<HierarchyWindow>();
    inspectorWindow = std::make_unique<InspectorWindow>();

    // Connect hierarchy selection to inspector
    hierarchyWindow->SetSelectionCallback([](GameObject* obj) {
        Editor::Get().inspectorWindow->SetTarget(obj);
    });
}

void Editor::Shutdown() {
    hierarchyWindow.reset();
    inspectorWindow.reset();
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
}

void Editor::RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                // TODO: New scene
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                // TODO: Open scene
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                // TODO: Save scene
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
            ImGui::MenuItem("Stats", nullptr, &showStats);
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

    return obj;
}
