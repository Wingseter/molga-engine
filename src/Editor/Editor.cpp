#include "Editor.h"
#include "../Core/GameBuilder.h"
#include "../Core/Project.h"
#include "../Core/SceneSerializer.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/GameObject.h"
#include "../Scripting/ScriptCompiler.h"
#include "../Scripting/ScriptManager.h"
#include "../Time.h"
#include "EditorState.h"
#include "VSCodeIntegration.h"
#include "Windows/HierarchyWindow.h"
#include "Windows/InspectorWindow.h"
#include "Windows/ProjectBrowserWindow.h"
#include "Windows/ScriptWindow.h"
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>

Editor &Editor::Get() {
  static Editor instance;
  return instance;
}

void Editor::Init() {
  hierarchyWindow = std::make_unique<HierarchyWindow>();
  inspectorWindow = std::make_unique<InspectorWindow>();
  projectBrowserWindow = std::make_unique<ProjectBrowserWindow>();
  scriptWindow = std::make_unique<ScriptWindow>();

  // Connect hierarchy selection to inspector
  hierarchyWindow->SetSelectionCallback(
      [](GameObject *obj) { Editor::Get().inspectorWindow->SetTarget(obj); });

  // Set engine path for ScriptCompiler and VSCodeIntegration
  std::string enginePath =
      std::filesystem::current_path().parent_path().string();
  ScriptCompiler::Get().SetEnginePath(enginePath);
  VSCodeIntegration::Get().SetEnginePath(enginePath);
}

void Editor::Shutdown() {
  hierarchyWindow.reset();
  inspectorWindow.reset();
  projectBrowserWindow.reset();
  scriptWindow.reset();
}

void Editor::Update(float dt) {
  // Editor-specific updates can go here
}

void Editor::BeginDockSpace() {
  static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

  windowFlags |= ImGuiWindowFlags_NoTitleBar
               | ImGuiWindowFlags_NoCollapse
               | ImGuiWindowFlags_NoResize
               | ImGuiWindowFlags_NoMove
               | ImGuiWindowFlags_NoBringToFrontOnFocus
               | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("DockSpace", nullptr, windowFlags);
  ImGui::PopStyleVar(3);

  // DockSpace
  dockspaceId = ImGui::GetID("MolgaDockSpace");
  ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

  // Setup default layout on first run
  if (firstTimeLayout) {
    firstTimeLayout = false;
    SetupDefaultLayout(dockspaceId);
  }
}

void Editor::EndDockSpace() {
  ImGui::End();
}

void Editor::SetupDefaultLayout(ImGuiID dockId) {
  ImGui::DockBuilderRemoveNode(dockId);
  ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->Size);

  // Split the dockspace
  ImGuiID dockMain = dockId;
  ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.2f, nullptr, &dockMain);
  ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain);
  ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);

  // Dock windows
  ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
  ImGui::DockBuilderDockWindow("Inspector", dockRight);
  ImGui::DockBuilderDockWindow("Scene", dockMain);
  ImGui::DockBuilderDockWindow("Project Browser", dockBottom);
  ImGui::DockBuilderDockWindow("Scripts", dockBottom);
  ImGui::DockBuilderDockWindow("Stats", dockBottom);

  ImGui::DockBuilderFinish(dockId);
}

void Editor::RenderGUI() {
  // Begin full-screen dockspace
  BeginDockSpace();

  // Menu bar inside dockspace
  RenderMenuBar();

  // End dockspace container
  EndDockSpace();

  // Render individual windows (now dockable)
  if (showHierarchy && hierarchyWindow) {
    hierarchyWindow->OnGUI();
  }

  if (showInspector && inspectorWindow) {
    inspectorWindow->OnGUI();
  }

  if (showProjectBrowser && projectBrowserWindow) {
    projectBrowserWindow->OnGUI();
  }

  if (showScriptWindow && scriptWindow) {
    scriptWindow->OnGUI();
  }

  if (showSceneView) {
    RenderSceneView();
  }

  if (showStats) {
    RenderStatsWindow();
  }

  if (showBuildWindow) {
    RenderBuildWindow();
  }
}

void Editor::RenderSceneView() {
  ImGui::Begin("Scene", &showSceneView);

  // Get available size for the scene view
  ImVec2 viewportSize = ImGui::GetContentRegionAvail();

  // Placeholder for scene rendering
  ImGui::Text("Scene View");
  ImGui::Text("Size: %.0f x %.0f", viewportSize.x, viewportSize.y);

  // Draw a placeholder rectangle
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(pos, ImVec2(pos.x + viewportSize.x, pos.y + viewportSize.y - 40),
                          IM_COL32(30, 30, 50, 255));
  drawList->AddRect(pos, ImVec2(pos.x + viewportSize.x, pos.y + viewportSize.y - 40),
                    IM_COL32(100, 100, 150, 255));

  ImGui::End();
}

void Editor::RenderStatsWindow() {
  ImGui::Begin("Stats", &showStats);
  ImGui::Text("FPS: %.1f", Time::GetFPS());
  ImGui::Text("Delta Time: %.3f ms", Time::GetDeltaTime() * 1000.0f);
  ImGui::Text("Frame: %d", Time::GetFrameCount());
  ImGui::Separator();
  ImGui::Text("Docking: Enabled");
  ImGui::Text("Viewports: Enabled");
  ImGui::End();
}

void Editor::RenderMenuBar() {
  if (ImGui::BeginMenuBar()) {
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
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
      }
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
      ImGui::MenuItem("Scene", nullptr, &showSceneView);
      ImGui::MenuItem("Project", nullptr, &showProjectBrowser);
      ImGui::MenuItem("Scripts", nullptr, &showScriptWindow);
      ImGui::MenuItem("Stats", nullptr, &showStats);
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Layout")) {
        firstTimeLayout = true;
      }
      ImGui::EndMenu();
    }

    // Scripting menu
    RenderScriptingMenu();

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

    // Spacer to push play controls to center
    float menuWidth = ImGui::GetCursorPosX();
    float windowWidth = ImGui::GetWindowWidth();
    float playControlsWidth = 200.0f;
    ImGui::SetCursorPosX((windowWidth - playControlsWidth) * 0.5f);

    // Play controls
    RenderPlayControls();

    ImGui::EndMenuBar();
  }
}

void Editor::RenderPlayControls() {
  EditorState &editorState = EditorState::Get();

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

  if (editorState.IsEditMode()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button(" > Play ")) {
      editorState.Play();
    }
    ImGui::PopStyleColor();
  } else if (editorState.IsPlayMode()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button(" || Pause ")) {
      editorState.Pause();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button(" [] Stop ")) {
      editorState.Stop();
    }
    ImGui::PopStyleColor();
  } else if (editorState.IsPaused()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button(" > Resume ")) {
      editorState.Play();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button(" [] Stop ")) {
      editorState.Stop();
    }
    ImGui::PopStyleColor();
  }

  ImGui::PopStyleVar();

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

void Editor::SetGameObjects(std::vector<std::shared_ptr<GameObject>> *objects) {
  gameObjects = objects;
  if (hierarchyWindow) {
    hierarchyWindow->SetGameObjects(objects);
  }
}

GameObject *Editor::GetSelectedObject() const {
  return hierarchyWindow ? hierarchyWindow->GetSelectedObject() : nullptr;
}

void Editor::SetSelectedObject(GameObject *obj) {
  if (hierarchyWindow) {
    hierarchyWindow->SetSelectedObject(obj);
  }
  if (inspectorWindow) {
    inspectorWindow->SetTarget(obj);
  }
}

std::shared_ptr<GameObject> Editor::CreateGameObject(const std::string &name) {
  if (!gameObjects)
    return nullptr;

  auto obj = std::make_shared<GameObject>(name);
  obj->AddComponent<Transform>();
  gameObjects->push_back(obj);
  sceneModified = true;

  return obj;
}

void Editor::NewScene() {
  if (!gameObjects)
    return;

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
  if (!gameObjects)
    return;

  if (currentScenePath.empty()) {
    SaveSceneAs();
    return;
  }

  if (SceneSerializer::SaveScene(currentScenePath, *gameObjects)) {
    sceneModified = false;
  }
}

void Editor::SaveSceneAs() {
  if (!gameObjects)
    return;

  // For now, use a default path
  currentScenePath = "scene.json";
  if (SceneSerializer::SaveScene(currentScenePath, *gameObjects)) {
    sceneModified = false;
    std::cout << "[Editor] Scene saved to: " << currentScenePath << std::endl;
  }
}

void Editor::OpenScene() {
  if (!gameObjects)
    return;

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

    if (!currentScenePath.empty()) {
      ImGui::Text("Main Scene: %s", currentScenePath.c_str());
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                         "Warning: No scene saved!");
      ImGui::Text("Save your scene first (File > Save Scene)");
    }

    ImGui::Separator();

    GameBuilder &builder = GameBuilder::Get();
    if (isBuilding) {
      ImGui::ProgressBar(builder.GetProgress(), ImVec2(-1, 0));
      ImGui::Text("%s", builder.GetCurrentStep().c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Build Game", ImVec2(120, 30))) {
      BuildGame();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 30))) {
      showBuildWindow = false;
    }

    if (!builder.GetLastError().empty()) {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s",
                         builder.GetLastError().c_str());
    }
  }
  ImGui::End();
}

void Editor::BuildGame() {
  if (currentScenePath.empty()) {
    currentScenePath = "scene.json";
  }

  if (gameObjects) {
    SceneSerializer::SaveScene(currentScenePath, *gameObjects);
  }

  BuildSettings settings;
  settings.gameName = buildGameName;
  settings.outputPath = buildOutputPath;
  settings.mainScene = currentScenePath;
  settings.windowWidth = buildWidth;
  settings.windowHeight = buildHeight;
  settings.fullscreen = buildFullscreen;

  isBuilding = true;

  GameBuilder &builder = GameBuilder::Get();
  if (builder.Build(settings)) {
    std::cout << "[Editor] Build successful!" << std::endl;
    std::cout << "[Editor] Output: " << settings.outputPath << "/"
              << settings.gameName << std::endl;
  } else {
    std::cerr << "[Editor] Build failed: " << builder.GetLastError()
              << std::endl;
  }

  isBuilding = false;
}

void Editor::RenderScriptingMenu() {
  if (ImGui::BeginMenu("Scripting")) {
    Project &project = Project::Get();
    bool hasProject = project.IsOpen();

    if (!hasProject) {
      ImGui::BeginDisabled();
    }

    if (ImGui::MenuItem("Open in VSCode")) {
      VSCodeIntegration::Get().OpenInVSCode(project.GetPath());
    }

    if (ImGui::MenuItem("Create New Script...")) {
      showScriptWindow = true;
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Compile Scripts", "Ctrl+Shift+B")) {
      ScriptCompiler &compiler = ScriptCompiler::Get();
      compiler.SetProjectPath(project.GetPath());
      if (compiler.Compile()) {
        std::cout << "[Editor] Scripts compiled successfully" << std::endl;
      } else {
        std::cerr << "[Editor] Script compilation failed: "
                  << compiler.GetLastError() << std::endl;
      }
      if (scriptWindow) {
        scriptWindow->RefreshScriptList();
      }
    }

    if (ImGui::MenuItem("Hot Reload", "Ctrl+R")) {
      ScriptCompiler &compiler = ScriptCompiler::Get();
      std::string libPath = compiler.GetCompiledLibraryPath();
      if (std::filesystem::exists(libPath)) {
        if (ScriptManager::Get().LoadScriptLibrary(libPath)) {
          std::cout << "[Editor] Scripts reloaded successfully" << std::endl;
        } else {
          std::cerr << "[Editor] Failed to reload scripts" << std::endl;
        }
      } else {
        std::cerr << "[Editor] No compiled library found" << std::endl;
      }
    }

    if (!hasProject) {
      ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::MenuItem("Script Window", nullptr, &showScriptWindow);

    ImGui::EndMenu();
  }
}
