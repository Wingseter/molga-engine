#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct ScriptManifest {
    bool enabled = false;
    std::string library;
    int apiVersion = 0;
    std::string buildHash;
};

struct SceneCatalogEntry {
    std::string id;
    std::string packagePath;
};

struct GameConfig {
    std::string gameName = "Molga Game";
    std::string companyName = "Molga";
    std::string mainScene = "Scenes/main.json";
    std::vector<std::string> scenes;
    std::string startupSceneId;
    std::vector<SceneCatalogEntry> sceneCatalog;
    int windowWidth = 800;
    int windowHeight = 600;
    bool fullscreen = false;
    ScriptManifest scripts;
};

bool LoadGameConfig(const std::string& path, GameConfig& config);
bool LoadGameConfigFromString(const std::string& jsonStr, GameConfig& config);
