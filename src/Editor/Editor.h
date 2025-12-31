#ifndef MOLGA_EDITOR_H
#define MOLGA_EDITOR_H

#include <memory>
#include <vector>

class GameObject;
class HierarchyWindow;
class InspectorWindow;
class Renderer;
class Shader;
class Camera2D;

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

private:
    Editor() = default;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void RenderMenuBar();
    void RenderPlayControls();

    std::unique_ptr<HierarchyWindow> hierarchyWindow;
    std::unique_ptr<InspectorWindow> inspectorWindow;

    std::vector<std::shared_ptr<GameObject>>* gameObjects = nullptr;

    bool showStats = true;
    bool showHierarchy = true;
    bool showInspector = true;
};

#endif // MOLGA_EDITOR_H
