#include "Editor.h"
#include "EditorConstants.h"
#include "EditorTheme.h"
#include "Project.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../Rendering/ShaderManager.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/GameObject.h"
#include "../Scripting/ScriptCompiler.h"
#include "../Scripting/ScriptManager.h"
#include "../Core/MolgaTime.h"
#include "EditorState.h"
#include "Commands/ObjectCommands.h"
#include "Editor/Commands/TransformCommand.h"
#include "Editor/Commands/ComponentCommands.h"
#include "VSCodeIntegration.h"
#include "Windows/HierarchyWindow.h"
#include "Windows/InspectorWindow.h"
#include "Windows/ProjectBrowserWindow.h"
#include "Windows/ScriptWindow.h"
#include "Windows/SceneViewWindow.h"
#include "Windows/StatsWindow.h"
#include "Windows/ProfilerWindow.h"
#include "Windows/ProjectSettingsWindow.h"
#include "Windows/ConsoleWindow.h"
#include "../Common/Log.h"
#include "../Core/PathService.h"
#include <cstring>
#include <sstream>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include "Scripting/Script.h"
#include "Platform/Platform.h"
#include "Platform/Process.h"

namespace {
class EditorLibraryPort : public molga::ILibraryPort {
public:
    explicit EditorLibraryPort(Editor* editor) : editor_(editor) {}

    bool Validate(const std::string& path, std::string& error) override {
        CleanValidated();
        void* handle = nullptr;
        if (ScriptManager::Get().ValidateLibrary(path, handle, error)) {
            validatedHandle_ = handle;
            validatedPath_ = path;
            return true;
        }
        return false;
    }

    void Swap(const std::string& path) override {
        void* handleToUse = nullptr;
        if (validatedPath_ == path && validatedHandle_) {
            handleToUse = validatedHandle_;
            validatedHandle_ = nullptr;
            validatedPath_.clear();
        } else {
            CleanValidated();
            std::string err;
            if (!ScriptManager::Get().ValidateLibrary(path, handleToUse, err)) {
                Log::Error("Editor", "Failed to validate library during swap: " + err);
                return;
            }
        }

        // 1. Gather all dynamic Script instances currently attached to game objects in the scene.
        struct ScriptSnapshot {
            unsigned int gameObjectId;
            std::string scriptName;
            nlohmann::json fields;
        };
        std::vector<ScriptSnapshot> snapshots;

        auto* gameObjects = editor_->GetGameObjects();
        if (gameObjects) {
            for (auto& obj : *gameObjects) {
                if (!obj) continue;
                for (auto* comp : obj->GetComponents()) {
                    if (auto* script = dynamic_cast<Script*>(comp)) {
                        std::string scriptName = script->GetScriptName();
                        if (ScriptManager::Get().IsDynamicScript(scriptName)) {
                            snapshots.push_back({
                                obj->GetID(),
                                scriptName,
                                script->SnapshotFields()
                            });
                        }
                    }
                }
            }
        }

        // 2. Remove those dynamic scripts from their GameObjects to prevent stale pointers/vtables.
        if (gameObjects) {
            for (auto& obj : *gameObjects) {
                if (!obj) continue;
                std::vector<size_t> dynamicTypesToRemove;
                for (auto* comp : obj->GetComponents()) {
                    if (auto* script = dynamic_cast<Script*>(comp)) {
                        std::string scriptName = script->GetScriptName();
                        if (ScriptManager::Get().IsDynamicScript(scriptName)) {
                            dynamicTypesToRemove.push_back(script->GetRuntimeTypeID());
                        }
                    }
                }
                for (size_t typeId : dynamicTypesToRemove) {
                    obj->RemoveComponentById(typeId);
                }
            }
        }

        // 3. Swap the active library handle in ScriptManager.
        ScriptManager::Get().SwapToValidatedLibrary(handleToUse, path);

        // 4. Recreate scripts and restore their fields.
        if (gameObjects) {
            for (const auto& snap : snapshots) {
                GameObject* obj = editor_->FindObjectById(snap.gameObjectId);
                if (!obj) continue;

                auto newScript = ScriptManager::Get().CreateScript(snap.scriptName);
                if (newScript) {
                    newScript->RestoreFields(snap.fields);
                    obj->AddComponentRaw(newScript.release());
                } else {
                    Log::Error("Editor", "Failed to recreate script '" + snap.scriptName + "' after reload");
                }
            }
        }

        Log::Info("Editor", "Successfully swapped and reloaded library: " + path);
    }

    std::string Active() const override {
        return ScriptManager::Get().ActiveLibraryPath();
    }

    ~EditorLibraryPort() override {
        CleanValidated();
    }

private:
    void CleanValidated() {
        if (validatedHandle_) {
            Platform::CloseDynamicLibrary(validatedHandle_);
            validatedHandle_ = nullptr;
        }
        validatedPath_.clear();
    }

    Editor* editor_;
    void* validatedHandle_ = nullptr;
    std::string validatedPath_;
};
}

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
  windowManager.Register(EditorConstants::WIN_PROFILER, std::make_unique<ProfilerWindow>());
  windowManager.Register(EditorConstants::WIN_PROJECT_SETTINGS, std::make_unique<ProjectSettingsWindow>());

  auto console = std::make_unique<ConsoleWindow>();
  Log::AddSink(console->Sink());
  console->SetOpenFileHandler([](const std::string& path, int line) {
      VSCodeIntegration::Get().OpenFileInVSCode(path, line);
  });
  windowManager.Register(EditorConstants::WIN_CONSOLE, std::move(console));

  // Register Selection listener to sync Inspector target
  selection_.AddListener([this](const molga::SelectionService& s, molga::SelectionSource) {
      if (auto* insp = windowManager.GetAs<InspectorWindow>(EditorConstants::WIN_INSPECTOR)) {
          insp->SetTarget(FindObjectById(s.InspectorTargetId()));
      }
  });

  // Find engine source root from executable dir
  std::filesystem::path current = PathService::Get().ExecutableDir();
  std::filesystem::path enginePath = current;
  for (int i = 0; i < 5; ++i) {
      if (std::filesystem::exists(current / "CMakeLists.txt") && std::filesystem::exists(current / "src")) {
          enginePath = current;
          break;
      }
      if (current.has_parent_path()) {
          current = current.parent_path();
      } else {
          break;
      }
  }

  // Set engine path for ScriptCompiler and VSCodeIntegration
  ScriptCompiler::Get().SetEnginePath(enginePath.string());
  VSCodeIntegration::Get().SetEnginePath(enginePath.string());

  libraryPort_ = std::make_unique<EditorLibraryPort>(this);
  reloadService_ = std::make_unique<molga::ScriptReloadService>(libraryPort_.get());
}

void Editor::Shutdown() {
  if (auto* console = windowManager.GetAs<ConsoleWindow>(EditorConstants::WIN_CONSOLE)) {
      Log::RemoveSink(console->Sink());
  }
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

void Editor::ProcessPlayUIInput() {
    if (auto* sceneView = windowManager.GetAs<SceneViewWindow>(
            EditorConstants::WIN_SCENE)) {
        sceneView->ProcessPlayUIInput();
    }
}

void Editor::ResetPlayUIInput() {
    if (auto* sceneView = windowManager.GetAs<SceneViewWindow>(
            EditorConstants::WIN_SCENE)) {
        sceneView->ResetPlayUIInput();
    }
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
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, commandHistory.CanUndo())) {
        commandHistory.Undo();
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, commandHistory.CanRedo())) {
        commandHistory.Redo();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Project Settings...")) {
        windowManager.SetVisible(EditorConstants::WIN_PROJECT_SETTINGS, true);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Reload Shaders")) {
        ShaderManager::Get().ReloadAll();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject")) {
      if (ImGui::MenuItem("Create Empty")) {
        auto cmd = std::make_unique<molga::CreateObjectCommand>("New GameObject");
        commandHistory.Execute(std::move(cmd));
      }
      if (ImGui::BeginMenu("2D Object")) {
        if (ImGui::MenuItem("Sprite")) {
          auto cmd = std::make_unique<molga::CreateObjectWithComponentsCommand>("Sprite", std::vector<std::string>{"SpriteRenderer"});
          commandHistory.Execute(std::move(cmd));
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

    if (EditorState::Get().IsEditMode() && (sceneOps.IsModified() || commandHistory.IsDirty())) {
        ImGui::TextDisabled("  *unsaved");
    }

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
  auto* sceneView = windowManager.GetAs<SceneViewWindow>(EditorConstants::WIN_SCENE);
  if (sceneView) {
    sceneView->SetGameObjects(objects);
  }
}

GameObject *Editor::GetSelectedObject() const {
  return FindObjectById(selection_.PrimaryId());
}

void Editor::SetSelectedObject(GameObject *obj) {
  selection_.Select(obj ? obj->GetID() : 0u, molga::SelectionSource::Code);
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
  commandHistory.Clear();
  SetSelectedObject(nullptr);
}

void Editor::SaveScene() {
  if (!gameObjects) return;
  if (sceneOps.SaveScene(*gameObjects)) {
    commandHistory.MarkClean();
  }
}

void Editor::SaveSceneAs() {
  if (!gameObjects) return;
  if (sceneOps.SaveSceneAs(*gameObjects)) {
    commandHistory.MarkClean();
  }
}

void Editor::OpenScene() {
  if (!gameObjects) return;

  if (sceneOps.OpenScene(*gameObjects)) {
    commandHistory.Clear();
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
      compiler.GenerateCMakeLists();

      if (auto* console = windowManager.GetAs<ConsoleWindow>(EditorConstants::WIN_CONSOLE)) {
        if (console->IsClearOnRecompile()) {
          console->RequestClear();
        }
      }

      auto& tasks = GetTaskService();
      molga::TaskId tid = tasks.Begin("Compile Scripts", molga::TaskCategory::ScriptCompile);
      LaunchScriptCompile(tid, compiler.ScriptsDir(), compiler.ConfigureCommand(), compiler.BuildCommand());
    }

    bool isPlaying = EditorState::Get().IsPlayMode();
    if (ImGui::MenuItem("Hot Reload", "Ctrl+R", nullptr, !isPlaying)) {
      ScriptCompiler &compiler = ScriptCompiler::Get();
      std::string libPath = compiler.GetCompiledLibraryPath();
      if (std::filesystem::exists(libPath)) {
        reloadService_->RequestReload(libPath);
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

std::shared_ptr<GameObject> Editor::AddExistingObject(std::shared_ptr<GameObject> obj) {
    if (!gameObjects || !obj) return nullptr;
    gameObjects->push_back(obj);
    sceneOps.MarkModified();
    return obj;
}

void Editor::RemoveObjectsByIds(const std::vector<unsigned int>& ids) {
    if (!gameObjects) return;
    gameObjects->erase(
        std::remove_if(gameObjects->begin(), gameObjects->end(),
            [&](const std::shared_ptr<GameObject>& o) {
                if (!o) return false;
                return std::find(ids.begin(), ids.end(), o->GetID()) != ids.end();
            }),
        gameObjects->end());
    sceneOps.MarkModified();
}

GameObject* Editor::FindObjectById(unsigned int id) const {
    if (!gameObjects) return nullptr;
    for (auto& o : *gameObjects) {
        if (o && o->GetID() == id) return o.get();
    }
    return nullptr;
}

void Editor::MarkSceneModified() {
    if (EditorState::Get().IsEditMode()) {
        sceneOps.MarkModified();
    }
}

std::shared_ptr<GameObject> Editor::ShareObjectById(unsigned int id) const {
    if (!gameObjects) return nullptr;
    for (auto& o : *gameObjects) {
        if (o && o->GetID() == id) return o;
    }
    return nullptr;
}

void Editor::SetSceneViewResources(Renderer* renderer, Shader* shader) {
    auto* sceneView = windowManager.GetAs<SceneViewWindow>(EditorConstants::WIN_SCENE);
    if (sceneView) {
        sceneView->SetSceneResources(renderer, shader, gameObjects);
    }
}

void Editor::SubmitTransformEdit(unsigned int targetId,
                                 const molga::TransformState& before,
                                 const molga::TransformState& after) {
    commandHistory.Execute(
        std::make_unique<molga::TransformCommand>(nullptr, targetId, before, after));
}

void Editor::PumpScriptReload(bool isEditMode) {
    if (reloadService_) {
        reloadService_->PumpPendingReload(isEditMode);
    }
}

void Editor::LaunchScriptCompile(molga::TaskId id,
                                 const std::string& scriptsDir,
                                 const std::string& configureCmd,
                                 const std::string& buildCmd) {
    std::thread worker([this, id, scriptsDir, configureCmd, buildCmd]() {
        taskService.MarkRunning(id);

        molga::SystemProcessRunner runner;

        taskService.AppendLog(id, "=== CMake Configure ===\n");
        auto configRes = runner.Run(configureCmd, scriptsDir,
            [this, id](const std::string& line) {
                taskService.Update(id, 0.25f, line);
            },
            [this, id]() {
                return taskService.IsCancelRequested(id);
            });

        if (configRes.cancelled) {
            taskService.Complete(id, molga::TaskState::Cancelled);
            return;
        }

        if (configRes.exitCode != 0) {
            taskService.AppendLog(id, "\nCMake configuration failed with exit code: " + std::to_string(configRes.exitCode) + "\n");
            taskService.Complete(id, molga::TaskState::Failed);
            return;
        }

        taskService.AppendLog(id, "=== CMake Build ===\n");
        auto buildRes = runner.Run(buildCmd, scriptsDir,
            [this, id](const std::string& line) {
                taskService.Update(id, 0.75f, line);
            },
            [this, id]() {
                return taskService.IsCancelRequested(id);
            });

        if (buildRes.cancelled) {
            taskService.Complete(id, molga::TaskState::Cancelled);
            return;
        }

        if (buildRes.exitCode != 0) {
            taskService.AppendLog(id, "\nBuild failed with exit code: " + std::to_string(buildRes.exitCode) + "\n");
            taskService.Complete(id, molga::TaskState::Failed);
            return;
        }

        taskService.Complete(id, molga::TaskState::Succeeded);
        
        std::string libPath = ScriptCompiler::Get().GetCompiledLibraryPath();
        reloadService_->RequestReload(libPath);
    });
    worker.detach();
}
