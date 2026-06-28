#include "Core/PackageLayout.h"
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>

bool PackageLayout::Validate(
    const std::filesystem::path& root,
    const std::string& executableName,
    std::string& errorOut) {
    const std::array required{
        root / executableName,
        root / "game.json",
        root / "Scenes/main.json",
        root / "Assets",
        root / "Shaders",
    };

    for (const auto& path : required) {
        if (!std::filesystem::exists(path)) {
            errorOut = "Missing package entry: " + path.string();
            return false;
        }
    }

    // Check game.json for scripts manifest if it exists
    std::filesystem::path configPath = root / "game.json";
    if (std::filesystem::exists(configPath)) {
        try {
            std::ifstream file(configPath);
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;
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
            }
        } catch (...) {
            errorOut = "Failed to parse game.json in package layout validation";
            return false;
        }
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
