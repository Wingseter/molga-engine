#pragma once

#include <string>
#include <nlohmann/json.hpp>

struct ScriptManifest {
    bool enabled = false;
    std::string library;
    int apiVersion = 0;
    std::string buildHash;
};

struct GameConfig {
    std::string gameName = "Molga Game";
    std::string mainScene = "scenes/main.json";
    int windowWidth = 800;
    int windowHeight = 600;
    bool fullscreen = false;
    ScriptManifest scripts;
};

bool LoadGameConfig(const std::string& path, GameConfig& config);
bool LoadGameConfigFromString(const std::string& jsonStr, GameConfig& config);
