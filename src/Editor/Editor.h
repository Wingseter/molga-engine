#ifndef MOLGA_EDITOR_H
#define MOLGA_EDITOR_H

#include <memory>
#include <vector>
#include <imgui.h>

class GameObject;
class HierarchyWindow;
class InspectorWindow;
class ProjectBrowserWindow;
class ScriptWindow;
class Renderer;
class Shader;
class Camera2D;
struct BuildSettings;

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

    // Scene file operations
    void NewScene();
    void SaveScene();
    void SaveSceneAs();
    void OpenScene();

    const std::string& GetCurrentScenePath() const { return currentScenePath; }

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
    void RenderBuildWindow();
    void RenderScriptingMenu();
    void RenderSceneView();
    void RenderStatsWindow();
    void BuildGame();

    std::unique_ptr<HierarchyWindow> hierarchyWindow;
    std::unique_ptr<InspectorWindow> inspectorWindow;
    std::unique_ptr<ProjectBrowserWindow> projectBrowserWindow;
    std::unique_ptr<ScriptWindow> scriptWindow;

    std::vector<std::shared_ptr<GameObject>>* gameObjects = nullptr;

    bool showStats = true;
    bool showHierarchy = true;
    bool showInspector = true;
    bool showProjectBrowser = true;
    bool showScriptWindow = true;
    bool showBuildWindow = false;
    bool showSceneView = true;

    std::string currentScenePath;
    bool sceneModified = false;

    // DockSpace
    bool firstTimeLayout = true;
    ImGuiID dockspaceId = 0;

    // Build settings
    char buildGameName[128] = "MyGame";
    char buildOutputPath[256] = "build/export";
    int buildWidth = 800;
    int buildHeight = 600;
    bool buildFullscreen = false;
    bool isBuilding = false;
};

#endif // MOLGA_EDITOR_H
