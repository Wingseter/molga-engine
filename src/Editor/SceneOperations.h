#pragma once

#include <memory>
#include <string>
#include <vector>

class GameObject;

class SceneOperations {
public:
    void NewScene(std::vector<std::shared_ptr<GameObject>>& objects);
    bool SaveScene(const std::vector<std::shared_ptr<GameObject>>& objects);
    bool SaveSceneAs(const std::vector<std::shared_ptr<GameObject>>& objects);
    bool OpenScene(std::vector<std::shared_ptr<GameObject>>& objects);

    // Explicit path-based save/open (no hardcoded fallback)
    bool SaveSceneAsPath(const std::vector<std::shared_ptr<GameObject>>& objects,
                         const std::string& path);
    bool OpenScenePath(std::vector<std::shared_ptr<GameObject>>& objects,
                       const std::string& path);

    const std::string& GetCurrentPath() const { return currentScenePath; }
    void SetCurrentPath(const std::string& path) { currentScenePath = path; }
    bool IsModified() const { return sceneModified; }
    void MarkModified() { sceneModified = true; }
    void ClearModified() { sceneModified = false; }

private:
    std::string currentScenePath;
    bool sceneModified = false;
};
