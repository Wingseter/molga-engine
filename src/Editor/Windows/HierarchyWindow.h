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

private:
    void DrawGameObjectNode(GameObject* obj);
    void CreateEmptyGameObject();
    void CreateSpriteObject();
    void CreateTilemapObject();
    void DeleteSelectedObject();
    void DuplicateSelectedObject();

    std::vector<std::shared_ptr<GameObject>>* gameObjects = nullptr;

    // Rename state
    bool isRenaming = false;
    GameObject* renamingObject = nullptr;
    char renameBuffer[256] = "";
};
