#ifndef MOLGA_EDITOR_H
#define MOLGA_EDITOR_H

#include <memory>
#include <vector>

class GameObject;
class HierarchyWindow;
class InspectorWindow;
class ProjectBrowserWindow;
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

    void RenderMenuBar();
    void RenderPlayControls();
    void RenderBuildWindow();
    void BuildGame();

    std::unique_ptr<HierarchyWindow> hierarchyWindow;
    std::unique_ptr<InspectorWindow> inspectorWindow;
    std::unique_ptr<ProjectBrowserWindow> projectBrowserWindow;

    std::vector<std::shared_ptr<GameObject>>* gameObjects = nullptr;

    bool showStats = true;
    bool showHierarchy = true;
    bool showInspector = true;
    bool showProjectBrowser = true;
    bool showBuildWindow = false;

    std::string currentScenePath;
    bool sceneModified = false;

    // Build settings
    char buildGameName[128] = "MyGame";
    char buildOutputPath[256] = "build/export";
    int buildWidth = 800;
    int buildHeight = 600;
    bool buildFullscreen = false;
    bool isBuilding = false;
};

#endif // MOLGA_EDITOR_H
