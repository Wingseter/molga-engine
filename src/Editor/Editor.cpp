#include "Editor.h"
#include "EditorConstants.h"
#include "EditorTheme.h"
#include "Project.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/GameObject.h"
#include "../Scripting/ScriptCompiler.h"
#include "../Scripting/ScriptManager.h"
#include "../MolgaTime.h"
#include "EditorState.h"
#include "VSCodeIntegration.h"
#include "Windows/HierarchyWindow.h"
#include "Windows/InspectorWindow.h"
#include "Windows/ProjectBrowserWindow.h"
#include "Windows/ScriptWindow.h"
#include "Windows/SceneViewWindow.h"
#include "Windows/StatsWindow.h"
#include "../Common/Log.h"
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>

Editor &Editor::Get() {
  static Editor instance;
  return instance;
}

void Editor::Init() {
  // Register all windows via WindowManager
  windowManager.Register(EditorConstants::WIN_HIERARCHY, std::make_unique<HierarchyWindow>());
  windowManager.Register(EditorConstants::WIN_INSPECTOR, std::make_unique<InspectorWindow>());
  windowManager.Register(EditorConstants::WIN_PROJECT_BROWSER, std::make_unique<ProjectBrowserWindow>());
  windowManager.Register(EditorConstants::WIN_SCRIPTS, std::make_unique<ScriptWindow>());
  windowManager.Register(EditorConstants::WIN_SCENE, std::make_unique<SceneViewWindow>());
  windowManager.Register(EditorConstants::WIN_STATS, std::make_unique<StatsWindow>());

  // Connect hierarchy selection to inspector
  auto* hierarchy = windowManager.GetAs<HierarchyWindow>(EditorConstants::WIN_HIERARCHY);
  auto* inspector = windowManager.GetAs<InspectorWindow>(EditorConstants::WIN_INSPECTOR);
  if (hierarchy && inspector) {
    hierarchy->SetSelectionCallback(
        [](GameObject *obj) {
          auto* insp = Editor::Get().windowManager.GetAs<InspectorWindow>(EditorConstants::WIN_INSPECTOR);
          if (insp) insp->SetTarget(obj);
        });
  }

  // Set engine path for ScriptCompiler and VSCodeIntegration
  std::string enginePath =
      std::filesystem::current_path().parent_path().string();
  ScriptCompiler::Get().SetEnginePath(enginePath);
  VSCodeIntegration::Get().SetEnginePath(enginePath);
}

void Editor::Shutdown() {
  windowManager.ShutdownAll();
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
  dockspaceId = ImGui::GetID(EditorConstants::DOCKSPACE_ID);
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
  ImGui::DockBuilderDockWindow(EditorConstants::WIN_HIERARCHY, dockLeft);
  ImGui::DockBuilderDockWindow(EditorConstants::WIN_INSPECTOR, dockRight);
  ImGui::DockBuilderDockWindow(EditorConstants::WIN_SCENE, dockMain);
  ImGui::DockBuilderDockWindow(EditorConstants::WIN_PROJECT_BROWSER, dockBottom);
  ImGui::DockBuilderDockWindow(EditorConstants::WIN_SCRIPTS, dockBottom);
  ImGui::DockBuilderDockWindow(EditorConstants::WIN_STATS, dockBottom);

  ImGui::DockBuilderFinish(dockId);
}

void Editor::RenderGUI() {
  // Begin full-screen dockspace
  BeginDockSpace();

  // Menu bar inside dockspace
  RenderMenuBar();

  // End dockspace container
  EndDockSpace();

  // Render all registered windows via WindowManager
  windowManager.RenderAll();

  // Build window is a separate dialog, not a dockable window
  buildMgr.RenderBuildWindow(sceneOps.GetCurrentPath());
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
        Log::Warn("Editor", "Exit is not yet implemented");
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
        Log::Warn("Editor", "Undo is not yet implemented");
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
        Log::Warn("Editor", "Redo is not yet implemented");
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
      windowManager.RenderWindowMenu();
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
        buildMgr.ShowWindow();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Build Game", "Ctrl+B")) {
        buildMgr.Build(sceneOps.GetCurrentPath(), gameObjects);
      }
      ImGui::EndMenu();
    }

    // Spacer to push play controls to center
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
    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::PLAY_BUTTON);
    if (ImGui::Button(" > Play ")) {
      editorState.Play();
    }
    ImGui::PopStyleColor();
  } else if (editorState.IsPlayMode()) {
    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::PAUSE_BUTTON);
    if (ImGui::Button(" || Pause ")) {
      editorState.Pause();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::STOP_BUTTON);
    if (ImGui::Button(" [] Stop ")) {
      editorState.Stop();
    }
    ImGui::PopStyleColor();
  } else if (editorState.IsPaused()) {
    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::PLAY_BUTTON);
    if (ImGui::Button(" > Resume ")) {
      editorState.Play();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::STOP_BUTTON);
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
    ImGui::TextColored(EditorTheme::STATE_EDIT, "Edit");
  } else if (editorState.IsPlayMode()) {
    ImGui::TextColored(EditorTheme::STATE_PLAYING, "Playing");
  } else {
    ImGui::TextColored(EditorTheme::STATE_PAUSED, "Paused");
  }
}

void Editor::SetGameObjects(std::vector<std::shared_ptr<GameObject>> *objects) {
  gameObjects = objects;
  auto* hierarchy = windowManager.GetAs<HierarchyWindow>(EditorConstants::WIN_HIERARCHY);
  if (hierarchy) {
    hierarchy->SetGameObjects(objects);
  }
}

GameObject *Editor::GetSelectedObject() const {
  auto* hierarchy = windowManager.GetAs<HierarchyWindow>(EditorConstants::WIN_HIERARCHY);
  return hierarchy ? hierarchy->GetSelectedObject() : nullptr;
}

void Editor::SetSelectedObject(GameObject *obj) {
  auto* hierarchy = windowManager.GetAs<HierarchyWindow>(EditorConstants::WIN_HIERARCHY);
  if (hierarchy) {
    hierarchy->SetSelectedObject(obj);
  }
  auto* inspector = windowManager.GetAs<InspectorWindow>(EditorConstants::WIN_INSPECTOR);
  if (inspector) {
    inspector->SetTarget(obj);
  }
}

std::shared_ptr<GameObject> Editor::CreateGameObject(const std::string &name) {
  if (!gameObjects)
    return nullptr;

  auto obj = std::make_shared<GameObject>(name);
  obj->AddComponent<Transform>();
  gameObjects->push_back(obj);
  sceneOps.MarkModified();

  return obj;
}

void Editor::NewScene() {
  if (!gameObjects) return;

  sceneOps.NewScene(*gameObjects);
  SetSelectedObject(nullptr);
}

void Editor::SaveScene() {
  if (!gameObjects) return;
  sceneOps.SaveScene(*gameObjects);
}

void Editor::SaveSceneAs() {
  if (!gameObjects) return;
  sceneOps.SaveSceneAs(*gameObjects);
}

void Editor::OpenScene() {
  if (!gameObjects) return;

  if (sceneOps.OpenScene(*gameObjects)) {
    SetSelectedObject(nullptr);
    auto* hierarchy = windowManager.GetAs<HierarchyWindow>(EditorConstants::WIN_HIERARCHY);
    if (hierarchy) {
      hierarchy->SetGameObjects(gameObjects);
    }
  }
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
      windowManager.SetVisible(EditorConstants::WIN_SCRIPTS, true);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Compile Scripts", "Ctrl+Shift+B")) {
      ScriptCompiler &compiler = ScriptCompiler::Get();
      compiler.SetProjectPath(project.GetPath());
      if (compiler.Compile()) {
        Log::Info("Editor", "Scripts compiled successfully");
      } else {
        Log::Error("Editor", "Script compilation failed: " + compiler.GetLastError());
      }
      auto* scriptWin = windowManager.GetAs<ScriptWindow>(EditorConstants::WIN_SCRIPTS);
      if (scriptWin) {
        scriptWin->RefreshScriptList();
      }
    }

    if (ImGui::MenuItem("Hot Reload", "Ctrl+R")) {
      ScriptCompiler &compiler = ScriptCompiler::Get();
      std::string libPath = compiler.GetCompiledLibraryPath();
      if (std::filesystem::exists(libPath)) {
        if (ScriptManager::Get().LoadScriptLibrary(libPath)) {
          Log::Info("Editor", "Scripts reloaded successfully");
        } else {
          Log::Error("Editor", "Failed to reload scripts");
        }
      } else {
        Log::Error("Editor", "No compiled library found");
      }
    }

    if (!hasProject) {
      ImGui::EndDisabled();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Script Window")) {
      windowManager.Toggle(EditorConstants::WIN_SCRIPTS);
    }

    ImGui::EndMenu();
  }
}
