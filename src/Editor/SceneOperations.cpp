#include "SceneOperations.h"
#include "EditorConstants.h"
#include "Project.h"
#include "../Common/Log.h"
#include "../Core/SceneSerializer.h"
#include "../ECS/GameObject.h"
#include <filesystem>

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
    namespace fs = std::filesystem;
    if (Project::Get().IsOpen()) {
        fs::path dir = Project::Get().GetScenesPath();
        fs::create_directories(dir);
        currentScenePath = (dir / "main.json").string();
    } else {
        currentScenePath = EditorConstants::DEFAULT_SCENE_FILE;  // 프로젝트 없을 때 폴백
    }

    if (SceneSerializer::SaveScene(currentScenePath, objects)) {
        sceneModified = false;
        Log::Info("Editor", "Scene saved to: " + currentScenePath);
        return true;
    }
    return false;
}

bool SceneOperations::OpenScene(std::vector<std::shared_ptr<GameObject>>& objects) {
    namespace fs = std::filesystem;
    std::string filepath = EditorConstants::DEFAULT_SCENE_FILE;
    if (Project::Get().IsOpen()) {
        filepath = (fs::path(Project::Get().GetScenesPath()) / "main.json").string();
    }

    if (SceneSerializer::LoadScene(filepath, objects)) {
        currentScenePath = filepath;
        sceneModified = false;
        return true;
    }
    return false;
}
