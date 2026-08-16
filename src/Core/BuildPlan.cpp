#include "Core/BuildPlan.h"
#include "Core/PackageLayout.h"
#include <filesystem>
#include <unordered_set>
#include <algorithm>

namespace fs = std::filesystem;

bool BuildPlanBuilder::Build(
    const BuildProfile& profile,
    const std::string& projectRoot,
    const std::string& target,
    const std::string& userScriptLibPath,
    BuildPlan& planOut,
    std::string& errorOut)
{
    // 1. Validate target
    if (target != "host") {
        errorOut = "Unsupported build target: " + target;
        return false;
    }

    // 2. Validate profile
    if (!profile.Validate(errorOut)) {
        return false;
    }

    planOut.executableName = PackageLayout::ExecutableNameFor(profile.gameName);

    // 3. Resolve paths, normalize, detect containment and collisions
    std::error_code ec;
    fs::path absoluteRoot = fs::absolute(projectRoot, ec);
    if (ec) {
        errorOut = "Could not resolve project root: " + ec.message();
        return false;
    }
    fs::path canonicalRoot = fs::weakly_canonical(absoluteRoot, ec);
    if (ec) {
        errorOut = "Could not canonicalize project root: " + ec.message();
        return false;
    }
    
    std::unordered_set<std::string> seenPackagePaths;
    planOut.sceneEntries.clear();

    for (const auto& scene : profile.scenes) {
        fs::path p(scene);
        fs::path absPath = fs::absolute(
            p.is_absolute() ? p : fs::path(projectRoot) / p, ec);
        if (ec) {
            errorOut = "Could not resolve scene path '" + scene + "': " + ec.message();
            return false;
        }
        fs::path canonicalPath = fs::weakly_canonical(absPath, ec);
        if (ec) {
            errorOut = "Could not canonicalize scene path '" + scene + "': " + ec.message();
            return false;
        }

        fs::path relPath = fs::relative(canonicalPath, canonicalRoot, ec);
        if (ec || relPath.is_absolute() || relPath.empty()) {
            errorOut = "Scene path escapes project root: " + scene;
            return false;
        }
        bool escapesRoot = false;
        for (const auto& part : relPath) {
            if (part == "..") { escapesRoot = true; break; }
        }
        if (escapesRoot) {
            errorOut = "Scene path escapes project root: " + scene;
            return false;
        }

        std::string relStr = relPath.generic_string();
        std::string packagePath = relStr;
        if (packagePath.size() >= 7 && (packagePath.substr(0, 7) == "Scenes/" || packagePath.substr(0, 7) == "scenes/")) {
            packagePath = "Scenes/" + packagePath.substr(7);
        } else if (packagePath == "Scenes" || packagePath == "scenes") {
            packagePath = "Scenes";
        } else {
            packagePath = "Scenes/" + packagePath;
        }

        if (seenPackagePaths.count(packagePath)) {
            errorOut = "Duplicate scene package path detected: " + packagePath;
            return false;
        }
        seenPackagePaths.insert(packagePath);

        SceneEntry entry;
        entry.sourceProfilePath = scene;
        entry.sourceAbsolutePath = canonicalPath.generic_string();
        entry.sceneId = relStr;
        entry.packagePath = packagePath;
        planOut.sceneEntries.push_back(entry);
    }

    // Find and map startup scene
    std::string startupScenePkg;
    std::string startupSceneId;
    bool foundStartup = false;
    for (const auto& entry : planOut.sceneEntries) {
        if (entry.sourceProfilePath == profile.startupScene) {
            startupScenePkg = entry.packagePath;
            startupSceneId = entry.sceneId;
            foundStartup = true;
            break;
        }
    }
    if (!foundStartup) {
        errorOut = "Build profile startupScene '" + profile.startupScene + "' not found in scenes list.";
        return false;
    }
    planOut.startupScenePackagePath = startupScenePkg;
    planOut.startupSceneId = startupSceneId;

    // 4. Set required directories
    planOut.requiredDirectories = {"Assets", "Scenes", "ShaderBundle"};

    // 5. Script library (optional)
    if (!userScriptLibPath.empty()) {
        planOut.optionalScriptLibrary = userScriptLibPath;
    } else {
        planOut.optionalScriptLibrary.clear();
    }

    // 6. Asset catalog path
    planOut.assetCatalogPath.clear();

    return true;
}
