#include "Core/PackageLayout.h"
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>

bool PackageLayout::Validate(
    const std::filesystem::path& root,
    const std::string& executableName,
    std::string& errorOut) {

    if (!std::filesystem::exists(root / executableName)) {
        errorOut = "Missing package entry: " + (root / executableName).string();
        return false;
    }

    std::filesystem::path configPath = root / "game.json";
    if (!std::filesystem::exists(configPath)) {
        errorOut = "Missing package entry: " + configPath.string();
        return false;
    }

    for (const auto& required : { "Assets", "Shaders" }) {
        if (!std::filesystem::exists(root / required)) {
            errorOut = "Missing package entry: " + (root / required).string();
            return false;
        }
    }

    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            errorOut = "Could not open game.json in package layout validation";
            return false;
        }
        nlohmann::json j;
        file >> j;

        std::string mainScene = j.value("mainScene", "Scenes/main.json");
        std::filesystem::path mainScenePath = root / mainScene;
        if (!std::filesystem::exists(mainScenePath)) {
            errorOut = "Missing package entry: " + mainScenePath.string();
            return false;
        }

        std::vector<std::string> scenes;
        if (j.contains("scenes") && j["scenes"].is_array()) {
            for (const auto& scene : j["scenes"]) {
                if (scene.is_string()) {
                    scenes.push_back(scene.get<std::string>());
                }
            }
        } else {
            scenes.push_back(mainScene);
        }

        for (const auto& scene : scenes) {
            std::filesystem::path scenePath = root / scene;
            if (!std::filesystem::exists(scenePath)) {
                errorOut = "Missing package entry: " + scenePath.string();
                return false;
            }
        }

        if (j.contains("scripts")) {
            auto& s = j["scripts"];
            bool enabled = s.value("enabled", false);
            std::string library = s.value("library", "");
            if (enabled && !library.empty()) {
                std::filesystem::path libPath = root / library;
                if (!std::filesystem::exists(libPath)) {
                    errorOut = "Script library is enabled in game.json but missing from package: " + libPath.string();
                    return false;
                }
            }
        }
    } catch (...) {
        errorOut = "Failed to parse game.json in package layout validation";
        return false;
    }

    errorOut.clear();
    return true;
}

std::string PackageLayout::ExecutableNameFor(const std::string& gameName) {
#ifdef _WIN32
    return gameName + ".exe";
#else
    return gameName;
#endif
}
