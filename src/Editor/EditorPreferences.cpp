#include "Editor/EditorPreferences.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace molga {

namespace fs = std::filesystem;

namespace {

void SetMessage(std::string* output, const std::string& message) {
    if (output) *output = message;
}

bool IsSaneResolution(PixelSize size) {
    // The graphics device performs authoritative limit validation. This protects
    // preference parser from pathological allocation requests without assuming
    // a particular GPU driver's texture-size limit.
    return size.width > 0 && size.height > 0 &&
           size.width <= 65536 && size.height <= 65536;
}

} // namespace

EditorPreferences EditorPreferences::Defaults() {
    return EditorPreferences{};
}

fs::path EditorPreferences::DefaultPath() {
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        return fs::path(local) / "Molga" / "Editor" / "editor_preferences.json";
    }
    if (const char* roaming = std::getenv("APPDATA")) {
        return fs::path(roaming) / "Molga" / "Editor" / "editor_preferences.json";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / "Library" / "Application Support" / "Molga" /
               "Editor" / "editor_preferences.json";
    }
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME")) {
        return fs::path(config) / "molga" / "editor" / "editor_preferences.json";
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".config" / "molga" / "editor" /
               "editor_preferences.json";
    }
#endif
    return fs::temp_directory_path() / "molga" / "editor_preferences.json";
}

bool EditorPreferences::Load(const fs::path& path, std::string* warningOut) {
    *this = Defaults();
    SetMessage(warningOut, {});

    std::ifstream input(path);
    if (!input.is_open()) {
        // First launch is normal; defaults are already installed.
        return false;
    }

    try {
        nlohmann::json root;
        input >> root;
        const int schemaVersion = root.value("schemaVersion", 0);
        if (!root.is_object() ||
            (schemaVersion != 1 && schemaVersion != 2 &&
             schemaVersion != kSchemaVersion) ||
            !root.contains("gameView") || !root["gameView"].is_object()) {
            SetMessage(warningOut,
                       "Unsupported or incomplete editor preference file; using defaults.");
            return false;
        }

        const auto& game = root["gameView"];
        const auto preset = ResolutionPresetFromKey(
            game.value("preset", std::string{}).c_str());
        const std::string mode = game.value("displayMode", std::string{});
        PixelSize custom{
            game.value("customWidth", 0),
            game.value("customHeight", 0)};
        if (!preset || (mode != "fit" && mode != "100") ||
            !IsSaneResolution(custom)) {
            SetMessage(warningOut,
                       "Invalid Game View preferences; using defaults.");
            return false;
        }

        gameView.selectedPreset = *preset;
        gameView.displayMode = mode == "100"
            ? GameViewDisplayMode::PixelPerfect100
            : GameViewDisplayMode::Fit;
        gameView.customResolution = custom;
        // Schema v1 had no Scene View section. Its migration contract is an
        // explicit OFF so opening an old editor never changes scene colors.
        if (schemaVersion == 1) {
            sceneView.fxEnabled = false;
        } else {
            if (!root.contains("sceneView") || !root["sceneView"].is_object() ||
                !root["sceneView"].contains("fxEnabled") ||
                !root["sceneView"]["fxEnabled"].is_boolean()) {
                *this = Defaults();
                SetMessage(warningOut,
                           "Invalid Scene View preferences; using defaults.");
                return false;
            }
            sceneView.fxEnabled = root["sceneView"]["fxEnabled"].get<bool>();
        }
        // Lighting preview was added in schema v3. Existing preference files
        // migrate to the documented default of ON.
        if (schemaVersion < 3) {
            sceneView.litEnabled = true;
        } else {
            if (!root["sceneView"].contains("litEnabled") ||
                !root["sceneView"]["litEnabled"].is_boolean()) {
                *this = Defaults();
                SetMessage(warningOut,
                           "Invalid Scene View preferences; using defaults.");
                return false;
            }
            sceneView.litEnabled = root["sceneView"]["litEnabled"].get<bool>();
        }
        return true;
    } catch (const std::exception& error) {
        *this = Defaults();
        SetMessage(warningOut,
                   std::string("Could not read editor preferences; using defaults: ") +
                       error.what());
        return false;
    }
}

bool EditorPreferences::SaveAtomic(const fs::path& path,
                                   std::string* errorOut) const {
    SetMessage(errorOut, {});
    if (!IsSaneResolution(gameView.customResolution)) {
        SetMessage(errorOut, "Custom Game View resolution is invalid.");
        return false;
    }

    std::error_code directoryError;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), directoryError);
        if (directoryError) {
            SetMessage(errorOut, "Could not create the editor preference directory: " +
                                     directoryError.message());
            return false;
        }
    }

    nlohmann::json root;
    root["schemaVersion"] = kSchemaVersion;
    root["gameView"] = {
        {"preset", ResolutionPresetKey(gameView.selectedPreset)},
        {"displayMode", gameView.displayMode == GameViewDisplayMode::Fit
                            ? "fit" : "100"},
        {"customWidth", gameView.customResolution.width},
        {"customHeight", gameView.customResolution.height}};
    root["sceneView"] = {
        {"fxEnabled", sceneView.fxEnabled},
        {"litEnabled", sceneView.litEnabled}};

    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            SetMessage(errorOut, "Could not open the temporary preference file.");
            return false;
        }
        output << root.dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code ignored;
            fs::remove(temporary, ignored);
            SetMessage(errorOut, "Could not write the temporary preference file.");
            return false;
        }
    }

#if defined(_WIN32)
    const bool replaced = MoveFileExW(
        temporary.wstring().c_str(), path.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool replaced = std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
    if (!replaced) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        SetMessage(errorOut, "Could not atomically replace the editor preference file.");
        return false;
    }
    return true;
}

} // namespace molga
