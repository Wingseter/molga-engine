#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct SmokeReport {
    std::string executable;
    std::string status;
    std::string scenePath;
    std::string message;
    std::size_t objectCount = 0;
    int frames = 0;
    bool assetsResolved = false;

    bool Save(const std::filesystem::path& path) const;
    static bool Load(const std::filesystem::path& path, SmokeReport& out);
};
