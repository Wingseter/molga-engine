#ifndef MOLGA_GAME_BUILDER_H
#define MOLGA_GAME_BUILDER_H

#include <string>
#include <vector>

struct BuildSettings {
    std::string gameName = "MyGame";
    std::string outputPath = "build/export";
    std::string mainScene = "scene.json";
    std::vector<std::string> scenes;
    int windowWidth = 800;
    int windowHeight = 600;
    bool fullscreen = false;
    bool showConsole = false;  // For Windows
};

class GameBuilder {
public:
    static GameBuilder& Get();

    // Build the game
    bool Build(const BuildSettings& settings);

    // Get last error message
    const std::string& GetLastError() const { return lastError; }

    // Build progress (0.0 - 1.0)
    float GetProgress() const { return progress; }
    const std::string& GetCurrentStep() const { return currentStep; }

private:
    GameBuilder() = default;

    bool CreateOutputDirectory(const std::string& path);
    bool CopyAssets(const std::string& outputPath);
    bool CopyShaders(const std::string& outputPath);
    bool GenerateGameConfig(const BuildSettings& settings, const std::string& outputPath);
    bool CopyExecutable(const std::string& outputPath, const std::string& gameName);
    bool CopyScenes(const BuildSettings& settings, const std::string& outputPath);

    std::string lastError;
    float progress = 0.0f;
    std::string currentStep;
};

#endif // MOLGA_GAME_BUILDER_H
