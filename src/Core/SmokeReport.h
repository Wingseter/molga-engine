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
    int sceneTransitions = 0;
    int uiDrivenSceneTransitions = 0;
    bool fontAssetsResolved = false;
    int fontAssetsResolvedCount = 0;
    int fontAssetsMissing = 0;
    bool koreanTitlePreserved = false;
    bool koreanFontGlyphsPresent = false;
    bool koreanGlyphAtlasReady = false;
    int koreanGlyphQuads = 0;
    int uiComponentsLoaded = 0;
    int platformerPlayersLoaded = 0;
    int physicsBodiesLoaded = 0;
    int physicsShapesLoaded = 0;
    bool rotatedTerrainVerified = false;
    bool physicsContactObserved = false;
    bool restitutionResponseObserved = false;
    bool frictionResponseObserved = false;
    bool saveRoundtrip = false;
    bool scriptDrivenPrefsSaved = false;
    bool scriptDrivenSlotSaved = false;
    bool scriptDrivenPersistence = false;
    bool postProcessed = false;
    bool postProcessFallback = false;
    int postProcessPasses = 0;
    std::string postProcessProfileGuid;
    int selectedCameraCount = 0;
    int renderedCameraCount = 0;
    int postProcessedCameraCount = 0;
    int postProcessFallbackCameraCount = 0;
    int lightingAppliedCameraCount = 0;
    int lightingFallbackCameraCount = 0;
    int shadowFallbackCameraCount = 0;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;

    // Render Stats
    int drawCalls = 0;
    int batches = 0;
    int textureBinds = 0;
    int shaderSwitches = 0;
    int outputCameraPasses = 0;
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
