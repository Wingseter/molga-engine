#pragma once

#include "Core/GameConfig.h"
#include <string>

class ScriptPackageLoader {
public:
    // Resolves and loads the dynamic library specified in config.scripts.library.
    // Validates existence, required symbols, and API version.
    // If successful, or if scripts are disabled, returns true.
    // If loading or validation fails, writes error to outError, saves smoke report if smokeEnabled is true, and returns false.
    static bool Load(const GameConfig& config, bool smokeEnabled, const std::string& smokeReportPath, std::string& outError);
};
