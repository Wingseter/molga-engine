#ifndef MOLGA_EDITOR_H
#define MOLGA_EDITOR_H

#include <memory>
#include <vector>
#include <imgui.h>
#include "WindowManager.h"
#include "SceneOperations.h"
#include "BuildManager.h"

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

    // Scene file operations (delegate to SceneOperations)
    void NewScene();
    void SaveScene();
    void SaveSceneAs();
    void OpenScene();

    const std::string& GetCurrentScenePath() const { return sceneOps.GetCurrentPath(); }

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

    // DockSpace
    bool firstTimeLayout = true;
    ImGuiID dockspaceId = 0;
};

#endif // MOLGA_EDITOR_H
