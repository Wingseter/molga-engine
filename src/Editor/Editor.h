#pragma once

#include <memory>
#include <vector>
#include <imgui.h>
#include "WindowManager.h"
#include "SceneOperations.h"
#include "BuildManager.h"

#include "Editor/Commands/CommandHistory.h"

class GameObject;
class Renderer;
class Shader;
class Camera2D;
class HierarchyWindow;
class InspectorWindow;

class Editor {
public:
    static Editor& Get();

    void Init();
    void Shutdown();

    // Main editor update/render
    void Update(float dt);
    void RenderGUI();

    // Scene management
    void SetGameObjects(std::vector<std::shared_ptr<GameObject>>* objects);

    // Selection
    GameObject* GetSelectedObject() const;
    void SetSelectedObject(GameObject* obj);

    // Create new GameObject
    std::shared_ptr<GameObject> CreateGameObject(const std::string& name = "GameObject");

    molga::CommandHistory& GetCommandHistory() { return commandHistory; }

    // Command가 사용하는 저수준 헬퍼
    std::shared_ptr<GameObject> AddExistingObject(std::shared_ptr<GameObject> obj);
    void RemoveObjectsByIds(const std::vector<unsigned int>& ids);
    GameObject* FindObjectById(unsigned int id) const;
    void MarkSceneModified();
    std::shared_ptr<GameObject> ShareObjectById(unsigned int id) const;

    // Scene file operations (delegate to SceneOperations)
    void NewScene();
    void SaveScene();
    void SaveSceneAs();
    void OpenScene();

    const std::string& GetCurrentScenePath() const { return sceneOps.GetCurrentPath(); }
    void SetCurrentScenePath(const std::string& path) {
        sceneOps.SetCurrentPath(path);
        sceneOps.ClearModified();
    }

    // SceneView에 렌더 리소스 주입
    void SetSceneViewResources(Renderer* renderer, Shader* shader);

private:
    Editor() = default;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    // DockSpace
    void BeginDockSpace();
    void EndDockSpace();
    void SetupDefaultLayout(ImGuiID dockspaceId);

    void RenderMenuBar();
    void RenderPlayControls();
    void RenderScriptingMenu();

    // Subsystems
    WindowManager windowManager;
    SceneOperations sceneOps;
    BuildManager buildMgr;

    std::vector<std::shared_ptr<GameObject>>* gameObjects = nullptr;
    molga::CommandHistory commandHistory;

    // DockSpace
    bool firstTimeLayout = true;
    ImGuiID dockspaceId = 0;
};
