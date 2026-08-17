#pragma once

#include "../Core/BuildProfile.h"
#include "../Core/BuildPlan.h"
#include <string>
#include <vector>

struct BuildSettings {
    BuildProfile profile;
    std::string projectRoot;
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
    bool GenerateGameConfig(const BuildSettings& settings, const BuildPlan& plan, const std::string& outputPath);
    bool CopyExecutable(const std::string& outputPath, const std::string& gameName);
    bool CopyScenes(const BuildPlan& plan, const std::string& outputPath);
    bool CopyUserScripts(const std::string& outputPath, std::string& outLibraryPath);
    bool EmitAssetCatalog(const std::string& outputPath);
    bool CopyPlaceholderResource(const std::string& outputPath);

    std::string lastError;
    float progress = 0.0f;
    std::string currentStep;
};
