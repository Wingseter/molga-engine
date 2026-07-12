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
    bool assetCatalogLoaded = false;
    int assetCatalogRecords = 0;
    int spriteAssetsResolved = 0;
    int spriteAssetsMissing = 0;

    // Render Stats
    int drawCalls = 0;
    int batches = 0;
    int textureBinds = 0;
    int shaderSwitches = 0;
    int submittedSprites = 0;
    int submittedCommands = 0;
    int batchFlushes = 0;
    int batchBreaks = 0;
    int maxSpritesPerBatch = 0;
    std::size_t verticesUploadedBytes = 0;
    long long queueSortNanos = 0;

    bool Save(const std::filesystem::path& path) const;
    static bool Load(const std::filesystem::path& path, SmokeReport& out);
};
