#pragma once

#include <filesystem>
#include <string>

class PackageLayout {
public:
    static bool Validate(
        const std::filesystem::path& root,
        const std::string& executableName,
        std::string& errorOut);

    static std::string ExecutableNameFor(const std::string& gameName);
};
