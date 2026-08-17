#include "Core/BuildProfile.h"

#include <algorithm>
#include <filesystem>
#include <utility>

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
    if (schemaVersion != CurrentSchemaVersion) {
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
        {"resizable", window.resizable},
        {"outputScaleMode",
         molga::GameOutputScaleModeName(window.outputScaleMode)}
    };
    j["developmentBuild"] = developmentBuild;
    j["showConsole"] = showConsole;
    j["target"] = target;
    return j;
}

bool BuildProfile::Deserialize(const nlohmann::json& j) {
    try {
        if (!j.is_object()) return false;
        const int storedSchemaVersion = j.value("schemaVersion", 1);
        if (storedSchemaVersion < 1 ||
            storedSchemaVersion > CurrentSchemaVersion) {
            return false;
        }

        BuildProfile parsed = *this;
        parsed.schemaVersion = CurrentSchemaVersion;
        parsed.gameName = j.value("gameName", parsed.gameName);
        parsed.productVersion = j.value("productVersion", parsed.productVersion);
        parsed.companyName = j.value("companyName", parsed.companyName);
        parsed.outputPath = j.value("outputPath", parsed.outputPath);
        parsed.startupScene = j.value("startupScene", parsed.startupScene);
        parsed.target = j.value("target", parsed.target);
        parsed.developmentBuild =
            j.value("developmentBuild", parsed.developmentBuild);
        parsed.showConsole = j.value("showConsole", parsed.showConsole);

        if (storedSchemaVersion == 1) {
            parsed.window.resizable = true;
            parsed.window.outputScaleMode = molga::GameOutputScaleMode::Native;
        }

        if (j.contains("scenes") && j["scenes"].is_array()) {
            parsed.scenes.clear();
            for (const auto& scene : j["scenes"]) {
                if (scene.is_string()) {
                    parsed.scenes.push_back(scene.get<std::string>());
                }
            }
        }

        if (j.contains("window") && j["window"].is_object()) {
            const auto& w = j["window"];
            parsed.window.width = w.value("width", parsed.window.width);
            parsed.window.height = w.value("height", parsed.window.height);
            parsed.window.fullscreen =
                w.value("fullscreen", parsed.window.fullscreen);
            parsed.window.resizable =
                w.value("resizable", parsed.window.resizable);
            if (storedSchemaVersion >= 2 && w.contains("outputScaleMode")) {
                if (!w["outputScaleMode"].is_string() ||
                    !molga::TryParseGameOutputScaleMode(
                        w["outputScaleMode"].get<std::string>(),
                        parsed.window.outputScaleMode)) {
                    return false;
                }
            }
        }
        *this = std::move(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
