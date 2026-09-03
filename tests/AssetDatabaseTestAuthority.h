#pragma once

#include "Assets/FontArtifactStore.h"
#include "Core/AssetDatabase.h"
#include "SmokeTestSupport.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace test_support {

// One project artifact authority per test process.
//
// A font artifact store binds exactly once per AssetDatabase and is never
// rebound, which is precisely what makes the singleton database usable from
// several test cases: they all need the same store, and each needs its own
// Assets subtree so one case's records cannot leak into the next. Deriving a
// project root by stripping "Assets" off a scan path, or from the working
// directory, is exactly the ambiguity this class exists to remove.
class AssetDatabaseTestAuthority {
public:
    static AssetDatabaseTestAuthority& Get();
    const std::filesystem::path& ProjectRoot() const noexcept;
    std::filesystem::path AssetsCaseRoot(std::string_view caseName);
    bool Bind(molga::AssetDatabase&, std::string* errorOut = nullptr);
private:
    AssetDatabaseTestAuthority();
    TempDirectory temp_;
    std::filesystem::path projectRoot_;
    std::uint64_t nextCase_ = 1;
};

inline AssetDatabaseTestAuthority::AssetDatabaseTestAuthority()
    : temp_("asset-database-authority") {
    projectRoot_ = temp_.Path() / "Project";
    std::filesystem::create_directories(projectRoot_ / "Assets");
}

inline AssetDatabaseTestAuthority& AssetDatabaseTestAuthority::Get() {
    static AssetDatabaseTestAuthority authority;
    return authority;
}

inline const std::filesystem::path&
AssetDatabaseTestAuthority::ProjectRoot() const noexcept {
    return projectRoot_;
}

inline std::filesystem::path AssetDatabaseTestAuthority::AssetsCaseRoot(
    std::string_view caseName) {
    if (caseName.empty()) {
        throw std::invalid_argument("case label must not be empty");
    }
    // Anything outside [A-Za-z0-9_-] becomes '-', so "." and ".." cannot
    // survive as path components and the result is always exactly one
    // component below Assets/.
    std::string sanitized;
    sanitized.reserve(caseName.size());
    for (const char character : caseName) {
        const unsigned char byte = static_cast<unsigned char>(character);
        const bool safe = std::isalnum(byte) != 0 || character == '_' ||
                          character == '-';
        sanitized.push_back(safe ? character : '-');
    }
    if (sanitized.find_first_not_of('-') == std::string::npos) {
        throw std::invalid_argument("case label has no usable characters");
    }
    if (nextCase_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("case ordinal would wrap");
    }
    const std::uint64_t ordinal = nextCase_++;

    const std::filesystem::path assetsRoot = projectRoot_ / "Assets";
    const std::filesystem::path child =
        assetsRoot / (std::to_string(ordinal) + "-" + sanitized);
    if (child.parent_path() != assetsRoot) {
        throw std::runtime_error("case root escaped the project Assets root");
    }
    if (std::filesystem::exists(child)) {
        throw std::runtime_error("case root already exists: " + child.string());
    }
    std::filesystem::create_directories(child);
    return child;
}

inline bool AssetDatabaseTestAuthority::Bind(molga::AssetDatabase& database,
                                             std::string* errorOut) {
    if (const molga::FontArtifactStore* bound = database.FontArtifacts()) {
        if (bound->IsProjectAuthorityFor(projectRoot_)) {
            if (errorOut) errorOut->clear();
            return true;
        }
        if (errorOut) {
            *errorOut = "a font artifact store for a different project root is "
                        "already bound";
        }
        return false;
    }
    return database.BindFontArtifactStore(
        std::make_shared<const molga::FontArtifactStore>(
            molga::FontArtifactStore::ForProject(projectRoot_)),
        errorOut);
}

}  // namespace test_support
