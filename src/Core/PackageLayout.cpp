#include "Core/PackageLayout.h"

#include <array>

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

    errorOut.clear();
    return true;
}
