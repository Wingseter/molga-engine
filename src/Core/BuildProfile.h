#pragma once

#include "Rendering/OutputPresentationLayout.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct BuildWindowSettings {
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    bool resizable = true;
    molga::GameOutputScaleMode outputScaleMode =
        molga::GameOutputScaleMode::Native;
};

struct BuildProfile {
    static constexpr int CurrentSchemaVersion = 2;

    int schemaVersion = CurrentSchemaVersion;
    std::string gameName = "MyGame";
    std::string productVersion = "0.1.0";
    std::string companyName = "Molga";
    std::string outputPath = "Builds/MyGame";
    std::string startupScene = "Scenes/main.json";
    std::vector<std::string> scenes = {"Scenes/main.json"};
    BuildWindowSettings window;
    bool developmentBuild = false;
    bool showConsole = false;
    std::string target = "host";

    static BuildProfile Defaults(const std::string& projectName);

    bool Validate(std::string& errorOut) const;
    nlohmann::json Serialize() const;
    bool Deserialize(const nlohmann::json& j);
};
