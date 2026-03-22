#include "SceneOperations.h"
#include "EditorConstants.h"
#include "../Common/Log.h"
#include "../Core/SceneSerializer.h"
#include "../ECS/GameObject.h"

void SceneOperations::NewScene(std::vector<std::shared_ptr<GameObject>>& objects) {
    objects.clear();
    currentScenePath.clear();
    sceneModified = false;
    Log::Info("Editor", "New scene created");
}

bool SceneOperations::SaveScene(const std::vector<std::shared_ptr<GameObject>>& objects) {
    if (currentScenePath.empty()) {
        return SaveSceneAs(objects);
    }

    if (SceneSerializer::SaveScene(currentScenePath, objects)) {
        sceneModified = false;
        return true;
    }
    return false;
}

bool SceneOperations::SaveSceneAs(const std::vector<std::shared_ptr<GameObject>>& objects) {
    // For now, use a default path
    currentScenePath = EditorConstants::DEFAULT_SCENE_FILE;
    if (SceneSerializer::SaveScene(currentScenePath, objects)) {
        sceneModified = false;
        Log::Info("Editor", "Scene saved to: " + currentScenePath);
        return true;
    }
    return false;
}

bool SceneOperations::OpenScene(std::vector<std::shared_ptr<GameObject>>& objects) {
    std::string filepath = EditorConstants::DEFAULT_SCENE_FILE;

    if (SceneSerializer::LoadScene(filepath, objects)) {
        currentScenePath = filepath;
        sceneModified = false;
        return true;
    }
    return false;
}
