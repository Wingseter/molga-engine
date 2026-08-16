#include "doctest.h"

#include "Core/PackageLayout.h"
#include "Core/SmokeReport.h"
#include "SmokeTestSupport.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("build package requires executable config scene assets and shaders") {
    test_support::TempDirectory temp{"build-smoke"};
    const fs::path root = temp.Path();

    fs::create_directories(root / "Scenes");
    fs::create_directories(root / "Assets");
    test_support::WriteText(root / "SmokeGame", "runtime");
    test_support::WriteText(root / "game.json", "{}");
    test_support::WriteText(root / "Scenes/main.json", "{}");

    std::string error;
    CHECK_FALSE(PackageLayout::Validate(root, "SmokeGame", error));
    CHECK(error.find("Shaders") != std::string::npos);

    test_support::WriteText(root / "Shaders/sprite.vert", "shader");

    // asset_catalog.json and placeholder resource are now required
    CHECK_FALSE(PackageLayout::Validate(root, "SmokeGame", error));
    test_support::WriteText(root / "asset_catalog.json", "{\"schemaVersion\":1,\"records\":[]}");
    fs::create_directories(root / "Resources");
    test_support::WriteText(root / "Resources/missing_texture.png", "placeholder");
    CHECK(PackageLayout::Validate(root, "SmokeGame", error));
}

TEST_CASE("smoke report round trips through json") {
    test_support::TempDirectory temp{"smoke-report"};
    const fs::path path = temp.Path() / "report.json";

    SmokeReport written;
    written.executable = "molga_runtime";
    written.status = "ok";
    written.scenePath = "Scenes/main.json";
    written.objectCount = 1;
    written.frames = 3;
    written.assetsResolved = true;
    written.assetCatalogLoaded = true;
    written.assetCatalogRecords = 2;
    written.spriteAssetsResolved = 1;
    written.spriteAssetsMissing = 0;
    written.sceneTransitions = 2;
    written.uiDrivenSceneTransitions = 2;
    written.fontAssetsResolved = true;
    written.fontAssetsResolvedCount = 1;
    written.fontAssetsMissing = 0;
    written.koreanTitlePreserved = true;
    written.koreanFontGlyphsPresent = true;
    written.koreanGlyphAtlasReady = true;
    written.koreanGlyphQuads = 8;
    written.uiComponentsLoaded = 5;
    written.platformerPlayersLoaded = 1;
    written.physicsBodiesLoaded = 2;
    written.physicsShapesLoaded = 2;
    written.rotatedTerrainVerified = true;
    written.physicsContactObserved = true;
    written.restitutionResponseObserved = true;
    written.frictionResponseObserved = true;
    written.saveRoundtrip = true;
    written.scriptDrivenPrefsSaved = true;
    written.scriptDrivenSlotSaved = true;
    written.scriptDrivenPersistence = true;
    written.postProcessed = true;
    written.postProcessFallback = false;
    written.postProcessPasses = 7;
    written.postProcessProfileGuid = "dddddddddddddddddddddddddddddddd";
    written.selectedCameraCount = 2;
    written.renderedCameraCount = 2;
    written.postProcessedCameraCount = 1;
    written.postProcessFallbackCameraCount = 0;
    written.lightingAppliedCameraCount = 1;
    written.lightingFallbackCameraCount = 0;
    written.shadowFallbackCameraCount = 0;
    written.selectedLightCount = 1;
    written.shadowedLightCount = 1;
    written.shadowCasterDrawCount = 1;
    written.lightingPasses = 1;
    written.shadowPasses = 1;
    written.drawCalls = 5;
    written.batches = 2;
    written.textureBinds = 4;
    written.shaderSwitches = 1;
    written.outputCameraPasses = 2;
    written.submittedSprites = 10;
    written.submittedCommands = 12;
    written.batchFlushes = 3;
    written.batchBreaks = 1;
    written.maxSpritesPerBatch = 8;
    written.verticesUploadedBytes = 128;
    written.queueSortNanos = 5000;
    REQUIRE(written.Save(path));

    SmokeReport loaded;
    REQUIRE(SmokeReport::Load(path, loaded));
    CHECK(loaded.status == "ok");
    CHECK(loaded.objectCount == 1);
    CHECK(loaded.frames == 3);
    CHECK(loaded.assetsResolved);
    CHECK(loaded.assetCatalogLoaded);
    CHECK(loaded.assetCatalogRecords == 2);
    CHECK(loaded.spriteAssetsResolved == 1);
    CHECK(loaded.spriteAssetsMissing == 0);
    CHECK(loaded.sceneTransitions == 2);
    CHECK(loaded.uiDrivenSceneTransitions == 2);
    CHECK(loaded.fontAssetsResolved);
    CHECK(loaded.fontAssetsResolvedCount == 1);
    CHECK(loaded.fontAssetsMissing == 0);
    CHECK(loaded.koreanTitlePreserved);
    CHECK(loaded.koreanFontGlyphsPresent);
    CHECK(loaded.koreanGlyphAtlasReady);
    CHECK(loaded.koreanGlyphQuads == 8);
    CHECK(loaded.uiComponentsLoaded == 5);
    CHECK(loaded.platformerPlayersLoaded == 1);
    CHECK(loaded.physicsBodiesLoaded == 2);
    CHECK(loaded.physicsShapesLoaded == 2);
    CHECK(loaded.rotatedTerrainVerified);
    CHECK(loaded.physicsContactObserved);
    CHECK(loaded.restitutionResponseObserved);
    CHECK(loaded.frictionResponseObserved);
    CHECK(loaded.saveRoundtrip);
    CHECK(loaded.scriptDrivenPrefsSaved);
    CHECK(loaded.scriptDrivenSlotSaved);
    CHECK(loaded.scriptDrivenPersistence);
    CHECK(loaded.postProcessed);
    CHECK_FALSE(loaded.postProcessFallback);
    CHECK(loaded.postProcessPasses == 7);
    CHECK(loaded.postProcessProfileGuid ==
          "dddddddddddddddddddddddddddddddd");
    CHECK(loaded.selectedCameraCount == 2);
    CHECK(loaded.renderedCameraCount == 2);
    CHECK(loaded.postProcessedCameraCount == 1);
    CHECK(loaded.postProcessFallbackCameraCount == 0);
    CHECK(loaded.lightingAppliedCameraCount == 1);
    CHECK(loaded.lightingFallbackCameraCount == 0);
    CHECK(loaded.shadowFallbackCameraCount == 0);
    CHECK(loaded.selectedLightCount == 1);
    CHECK(loaded.shadowedLightCount == 1);
    CHECK(loaded.shadowCasterDrawCount == 1);
    CHECK(loaded.lightingPasses == 1);
    CHECK(loaded.shadowPasses == 1);
    CHECK(loaded.drawCalls == 5);
    CHECK(loaded.batches == 2);
    CHECK(loaded.textureBinds == 4);
    CHECK(loaded.shaderSwitches == 1);
    CHECK(loaded.outputCameraPasses == 2);
    CHECK(loaded.submittedSprites == 10);
    CHECK(loaded.submittedCommands == 12);
    CHECK(loaded.batchFlushes == 3);
    CHECK(loaded.batchBreaks == 1);
    CHECK(loaded.maxSpritesPerBatch == 8);
    CHECK(loaded.verticesUploadedBytes == 128);
    CHECK(loaded.queueSortNanos == 5000);
}
