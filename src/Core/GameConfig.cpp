#include "GameConfig.h"
#include "Core/ProjectSettings.h"
#include "Systems/Input.h"
#include <fstream>
#include <iostream>

namespace {

bool ParseGameConfigJson(const nlohmann::json& j, GameConfig& config) {
    if (!j.is_object()) return false;
    const int storedSchemaVersion = j.value("schemaVersion", 1);
    if (storedSchemaVersion != GameConfig::CurrentSchemaVersion) {
        return false;
    }
    config.schemaVersion = GameConfig::CurrentSchemaVersion;
    config.outputScaleMode = molga::GameOutputScaleMode::Native;
    config.resizable = true;
    if (j.contains("gameName")) config.gameName = j["gameName"].get<std::string>();
    if (j.contains("companyName")) config.companyName = j["companyName"].get<std::string>();
    if (j.contains("mainScene")) config.mainScene = j["mainScene"].get<std::string>();
    if (j.contains("windowWidth")) config.windowWidth = j["windowWidth"].get<int>();
    if (j.contains("windowHeight")) config.windowHeight = j["windowHeight"].get<int>();
    if (j.contains("fullscreen")) config.fullscreen = j["fullscreen"].get<bool>();
    if (j.contains("resizable")) config.resizable = j["resizable"].get<bool>();
    if (storedSchemaVersion >= 2 && j.contains("outputScaleMode")) {
        if (!j["outputScaleMode"].is_string() ||
            !molga::TryParseGameOutputScaleMode(
                j["outputScaleMode"].get<std::string>(),
                config.outputScaleMode)) {
            return false;
        }
    }
    if (j.contains("projectSettings")) {
        ProjectSettings::Get().Deserialize(j["projectSettings"]);
    }
    if (!j.contains("inputActions") ||
        !Input::DeserializeActions(j["inputActions"])) return false;

    config.scenes.clear();
    if (j.contains("scenes")) {
        if (!j["scenes"].is_array()) return false;
        for (const auto& scene : j["scenes"]) {
            if (!scene.is_string()) return false;
            config.scenes.push_back(scene.get<std::string>());
        }
    }

    config.startupSceneId = j.value("startupSceneId", std::string{});
    config.sceneCatalog.clear();
    const bool hasSceneCatalog = j.contains("sceneCatalog");
    if (hasSceneCatalog) {
        if (!j["sceneCatalog"].is_array()) return false;
        for (const auto& item : j["sceneCatalog"]) {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
                !item.contains("packagePath") || !item["packagePath"].is_string()) {
                return false;
            }
            const std::string id = item.value("id", std::string{});
            const std::string packagePath = item.value("packagePath", std::string{});
            if (id.empty() || packagePath.empty()) return false;
            config.sceneCatalog.push_back({id, packagePath});
        }
    }

    // Backward-compatible catalog for packages that only contain mainScene and
    // the legacy array of package-path strings.
    if (!hasSceneCatalog) {
        if (config.scenes.empty()) config.scenes.push_back(config.mainScene);
        bool containsMainScene = false;
        for (const auto& scene : config.scenes) {
            config.sceneCatalog.push_back({scene, scene});
            containsMainScene = containsMainScene || scene == config.mainScene;
        }
        if (!containsMainScene) {
            config.sceneCatalog.push_back({config.mainScene, config.mainScene});
        }
    } else if (config.sceneCatalog.empty()) {
        return false;
    }
    if (config.startupSceneId.empty()) {
        for (const auto& entry : config.sceneCatalog) {
            if (entry.packagePath == config.mainScene) {
                config.startupSceneId = entry.id;
                break;
            }
        }
        if (config.startupSceneId.empty()) config.startupSceneId = config.mainScene;
    }

    if (j.contains("scripts")) {
        const auto& s = j["scripts"];
        config.scripts.enabled = s.value("enabled", false);
        config.scripts.library = s.value("library", std::string{});
        config.scripts.apiVersion = s.value("apiVersion", 0);
        config.scripts.buildHash = s.value("buildHash", std::string{});
    } else {
        config.scripts = ScriptManifest{};
    }
    return true;
}

} // namespace

bool LoadGameConfig(const std::string& path, GameConfig& config) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open game config: " << path << std::endl;
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        return ParseGameConfigJson(j, config);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing game config: " << e.what() << std::endl;
        return false;
    }
}

bool LoadGameConfigFromString(const std::string& jsonStr, GameConfig& config) {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        return ParseGameConfigJson(j, config);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing game config from string: " << e.what() << std::endl;
        return false;
    }
}
