#include "Core/BuildManifest.h"
#include <filesystem>

namespace fs = std::filesystem;

std::vector<std::string> BuildManifest::FindMissing() const {
    std::vector<std::string> missing;
    for (const auto& f : requiredFiles) {
        std::error_code ec;
        if (!fs::exists(f, ec)) {
            missing.push_back(f);
        }
    }
    return missing;
}

bool BuildManifest::Validate(std::string& errorOut) const {
    auto missing = FindMissing();
    if (missing.empty()) return true;
    errorOut = "Build aborted; missing required files:";
    for (const auto& m : missing) {
        errorOut += "\n  - " + m;
    }
    return false;
}
