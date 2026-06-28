#include "GameConfig.h"
#include "Core/ProjectSettings.h"
#include "Systems/Input.h"
#include <fstream>
#include <iostream>

bool LoadGameConfig(const std::string& path, GameConfig& config) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open game config: " << path << std::endl;
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("gameName")) config.gameName = j["gameName"];
        if (j.contains("mainScene")) config.mainScene = j["mainScene"];
        if (j.contains("windowWidth")) config.windowWidth = j["windowWidth"];
        if (j.contains("windowHeight")) config.windowHeight = j["windowHeight"];
        if (j.contains("fullscreen")) config.fullscreen = j["fullscreen"];
        if (j.contains("projectSettings")) {
            ProjectSettings::Get().Deserialize(j["projectSettings"]);
        }
        if (j.contains("inputActions")) {
            Input::DeserializeActions(j["inputActions"]);
        }

        if (j.contains("scripts")) {
            auto& s = j["scripts"];
            if (s.contains("enabled")) config.scripts.enabled = s["enabled"];
            if (s.contains("library")) config.scripts.library = s["library"];
            if (s.contains("apiVersion")) config.scripts.apiVersion = s["apiVersion"];
            if (s.contains("buildHash")) config.scripts.buildHash = s["buildHash"];
        } else {
            config.scripts.enabled = false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing game config: " << e.what() << std::endl;
        return false;
    }
}

bool LoadGameConfigFromString(const std::string& jsonStr, GameConfig& config) {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        if (j.contains("gameName")) config.gameName = j["gameName"];
        if (j.contains("mainScene")) config.mainScene = j["mainScene"];
        if (j.contains("windowWidth")) config.windowWidth = j["windowWidth"];
        if (j.contains("windowHeight")) config.windowHeight = j["windowHeight"];
        if (j.contains("fullscreen")) config.fullscreen = j["fullscreen"];
        if (j.contains("projectSettings")) {
            ProjectSettings::Get().Deserialize(j["projectSettings"]);
        }
        if (j.contains("inputActions")) {
            Input::DeserializeActions(j["inputActions"]);
        }

        if (j.contains("scripts")) {
            auto& s = j["scripts"];
            if (s.contains("enabled")) config.scripts.enabled = s["enabled"];
            if (s.contains("library")) config.scripts.library = s["library"];
            if (s.contains("apiVersion")) config.scripts.apiVersion = s["apiVersion"];
            if (s.contains("buildHash")) config.scripts.buildHash = s["buildHash"];
        } else {
            config.scripts.enabled = false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing game config from string: " << e.what() << std::endl;
        return false;
    }
}
