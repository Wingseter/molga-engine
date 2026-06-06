#pragma once

#include <memory>
#include <string>
#include <vector>

class GameObject;

class BuildManager {
public:
    void RenderBuildWindow(const std::string& currentScenePath);
    void Build(const std::string& scenePath,
               const std::vector<std::shared_ptr<GameObject>>* objects);

    bool IsShowingWindow() const { return showBuildWindow; }
    void ShowWindow() { showBuildWindow = true; }

private:
    char buildGameName[128] = "MyGame";
    char buildOutputPath[256] = "build/export";
    int buildWidth = 800;
    int buildHeight = 600;
    bool buildFullscreen = false;
    bool isBuilding = false;
    bool showBuildWindow = false;
};
