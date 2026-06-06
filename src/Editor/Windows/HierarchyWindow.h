#pragma once

#include "EditorWindow.h"
#include <vector>
#include <memory>

class GameObject;

class HierarchyWindow : public EditorWindow {
public:
    HierarchyWindow();

    void OnGUI() override;

    // Scene objects to display
    void SetGameObjects(std::vector<std::shared_ptr<GameObject>>* objects) { gameObjects = objects; }

    // Selection
    GameObject* GetSelectedObject() const { return selectedObject; }
    void SetSelectedObject(GameObject* obj) { selectedObject = obj; }

    // Callback when selection changes
    using SelectionCallback = void(*)(GameObject*);
    void SetSelectionCallback(SelectionCallback callback) { onSelectionChanged = callback; }

private:
    void DrawGameObjectNode(GameObject* obj);
    void CreateEmptyGameObject();
    void DeleteSelectedObject();
    void DuplicateSelectedObject();

    std::vector<std::shared_ptr<GameObject>>* gameObjects = nullptr;
    GameObject* selectedObject = nullptr;
    SelectionCallback onSelectionChanged = nullptr;

    // Rename state
    bool isRenaming = false;
    GameObject* renamingObject = nullptr;
    char renameBuffer[256] = "";
};
