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

    fs::path filepath = EditorConstants::DEFAULT_SCENE_FILE;
    if (Project::Get().IsOpen()) {
        filepath = fs::path(Project::Get().GetScenesPath()) / "main.json";
    }

    return SaveSceneAsPath(objects, filepath.string());
}

bool SceneOperations::OpenScene(std::vector<std::shared_ptr<GameObject>>& objects) {
    namespace fs = std::filesystem;

    fs::path filepath = EditorConstants::DEFAULT_SCENE_FILE;
    if (Project::Get().IsOpen()) {
        filepath = fs::path(Project::Get().GetScenesPath()) / "main.json";
    }

    return OpenScenePath(objects, filepath.string());
}

bool SceneOperations::SaveSceneAsPath(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& path) {
    if (path.empty()) {
        Log::Error("Editor", "Cannot save scene because the target path is empty.");
        return false;
    }

    namespace fs = std::filesystem;
    const fs::path target(path);
    const fs::path parent = target.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }

    if (SceneSerializer::SaveScene(path, objects)) {
        currentScenePath = path;
        sceneModified = false;
        Log::Info("Editor", "Scene saved to: " + currentScenePath);
        return true;
    }

    return false;
}

bool SceneOperations::OpenScenePath(std::vector<std::shared_ptr<GameObject>>& objects,
                                    const std::string& path) {
    if (path.empty()) {
        Log::Error("Editor", "Cannot open scene because the target path is empty.");
        return false;
    }

    if (SceneSerializer::LoadScene(path, objects)) {
        currentScenePath = path;
        sceneModified = false;
        return true;
    }

    return false;
}
