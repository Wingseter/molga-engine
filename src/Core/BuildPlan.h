#pragma once

#include "Core/BuildProfile.h"
#include <string>
#include <vector>

struct SceneEntry {
    std::string sourceProfilePath;
    std::string sourceAbsolutePath;
    // Stable public identifier used by scripts. Always a normalized,
    // project-relative BuildProfile path.
    std::string sceneId;
    std::string packagePath;
};

struct BuildPlan {
    std::string executableName;
    std::vector<SceneEntry> sceneEntries;
    std::string startupSceneId;
    std::string startupScenePackagePath;
    std::vector<std::string> requiredDirectories;
    std::string optionalScriptLibrary;
    std::string assetCatalogPath;
};

class BuildPlanBuilder {
public:
    static bool Build(
        const BuildProfile& profile,
        const std::string& projectRoot,
        const std::string& target,
        const std::string& userScriptLibPath,
        BuildPlan& planOut,
        std::string& errorOut);
};
