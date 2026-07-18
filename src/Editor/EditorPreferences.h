#pragma once

#include "Editor/GameViewLayout.h"

#include <filesystem>
#include <string>

namespace molga {

struct GameViewPreferences {
    ResolutionPreset selectedPreset = ResolutionPreset::BuildResolution;
    GameViewDisplayMode displayMode = GameViewDisplayMode::Fit;
    PixelSize customResolution{1280, 720};
};

// User-only editor preferences. This file is deliberately independent of the
// project/scene document, so saving Game View state can never make either dirty.
class EditorPreferences {
public:
    static constexpr int kSchemaVersion = 1;

    static EditorPreferences Defaults();
    static std::filesystem::path DefaultPath();

    // Missing files and malformed files both restore defaults. warningOut is
    // populated for malformed/unsupported data and IO failures.
    bool Load(const std::filesystem::path& path, std::string* warningOut = nullptr);
    bool SaveAtomic(const std::filesystem::path& path,
                    std::string* errorOut = nullptr) const;

    GameViewPreferences gameView;
};

} // namespace molga
