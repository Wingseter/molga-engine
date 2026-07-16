#include "Core/BuildProfile.h"

#include <algorithm>
#include <filesystem>

namespace {
bool IsSafeStorageSegment(const std::string& value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return true;
}
}

BuildProfile BuildProfile::Defaults(const std::string& projectName) {
    BuildProfile profile;
    if (!projectName.empty()) {
        profile.gameName = projectName;
        profile.outputPath = "Builds/" + projectName;
    }
    return profile;
}

bool BuildProfile::Validate(std::string& errorOut) const {
    if (schemaVersion != 1) {
        errorOut = "Unsupported build profile schemaVersion: " + std::to_string(schemaVersion);
        return false;
    }
    if (gameName.empty()) {
        errorOut = "Build profile gameName must not be empty.";
        return false;
    }
    if (!IsSafeStorageSegment(gameName)) {
        errorOut = "Build profile gameName contains characters that are unsafe for storage paths.";
        return false;
    }
    if (!IsSafeStorageSegment(companyName)) {
        errorOut = "Build profile companyName contains characters that are unsafe for storage paths.";
        return false;
    }
    if (startupScene.empty()) {
        errorOut = "Build profile startupScene must not be empty.";
        return false;
    }
    if (std::find(scenes.begin(), scenes.end(), startupScene) == scenes.end()) {
        errorOut = "Build profile startupScene must be included in scenes.";
        return false;
    }
    if (window.width <= 0 || window.height <= 0) {
        errorOut = "Build profile window size must be positive.";
        return false;
    }
    if (target != "host") {
        errorOut = "Unsupported build target: " + target;
        return false;
    }
    errorOut.clear();
    return true;
}

nlohmann::json BuildProfile::Serialize() const {
    nlohmann::json j;
    j["schemaVersion"] = schemaVersion;
    j["gameName"] = gameName;
    j["productVersion"] = productVersion;
    j["companyName"] = companyName;
    j["outputPath"] = outputPath;
    j["startupScene"] = startupScene;
    j["scenes"] = scenes;
    j["window"] = {
        {"width", window.width},
        {"height", window.height},
        {"fullscreen", window.fullscreen},
        {"resizable", window.resizable}
    };
    j["developmentBuild"] = developmentBuild;
    j["showConsole"] = showConsole;
    j["target"] = target;
    return j;
}

bool BuildProfile::Deserialize(const nlohmann::json& j) {
    try {
        schemaVersion = j.value("schemaVersion", 1);
        gameName = j.value("gameName", gameName);
        productVersion = j.value("productVersion", productVersion);
        companyName = j.value("companyName", companyName);
        outputPath = j.value("outputPath", outputPath);
        startupScene = j.value("startupScene", startupScene);
        target = j.value("target", target);
        developmentBuild = j.value("developmentBuild", developmentBuild);
        showConsole = j.value("showConsole", showConsole);

        if (j.contains("scenes") && j["scenes"].is_array()) {
            scenes.clear();
            for (const auto& scene : j["scenes"]) {
                if (scene.is_string()) {
                    scenes.push_back(scene.get<std::string>());
                }
            }
        }

        if (j.contains("window") && j["window"].is_object()) {
            const auto& w = j["window"];
            window.width = w.value("width", window.width);
            window.height = w.value("height", window.height);
            window.fullscreen = w.value("fullscreen", window.fullscreen);
            window.resizable = w.value("resizable", window.resizable);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
