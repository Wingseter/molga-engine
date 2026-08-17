#include "Core/SmokeReport.h"

#include <fstream>
#include <nlohmann/json.hpp>

bool SmokeReport::Save(const std::filesystem::path& path) const {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream output(path);
    if (!output) {
        return false;
    }

    output << nlohmann::json{
        {"executable", executable},
        {"status", status},
        {"scenePath", scenePath},
        {"message", message},
        {"objectCount", objectCount},
        {"frames", frames},
        {"graphicsApi", graphicsApi},
        {"graphicsDriver", graphicsDriver},
        {"osVersion", osVersion},
        {"architecture", architecture},
        {"swapchainFormat", swapchainFormat},
        {"outputTextureFormat", outputTextureFormat},
        {"shaderArtifactFormat", shaderArtifactFormat},
        {"shaderManifestSha256", shaderManifestSha256},
        {"gpuCopyPasses", gpuCopyPasses},
        {"gpuRenderPasses", gpuRenderPasses},
        {"gpuDrawCalls", gpuDrawCalls},
        {"gpuUploadBytes", gpuUploadBytes},
        {"gpuValidationEnabled", gpuValidationEnabled},
        {"gpuValidationErrors", gpuValidationErrors},
        {"finalPixelProbeValid", finalPixelProbeValid},
        {"finalPixelR", finalPixelR},
        {"finalPixelG", finalPixelG},
        {"finalPixelB", finalPixelB},
        {"finalPixelA", finalPixelA},
        {"benchmarkWarmupFrames", benchmarkWarmupFrames},
        {"benchmarkMeasuredFrames", benchmarkMeasuredFrames},
        {"benchmarkCpuP50Ms", benchmarkCpuP50Ms},
        {"benchmarkCpuP95Ms", benchmarkCpuP95Ms},
        {"benchmarkDrawCalls", benchmarkDrawCalls},
        {"benchmarkBatches", benchmarkBatches},
        {"benchmarkRenderPasses", benchmarkRenderPasses},
        {"benchmarkUploadBytes", benchmarkUploadBytes},
        {"residentMemoryBytes", residentMemoryBytes},
        {"peakMemoryBytes", peakMemoryBytes},
        {"assetsResolved", assetsResolved},
        {"assetCatalogLoaded", assetCatalogLoaded},
        {"assetCatalogRecords", assetCatalogRecords},
        {"spriteAssetsResolved", spriteAssetsResolved},
        {"spriteAssetsMissing", spriteAssetsMissing},
        {"sceneTransitions", sceneTransitions},
        {"uiDrivenSceneTransitions", uiDrivenSceneTransitions},
        {"fontAssetsResolved", fontAssetsResolved},
        {"fontAssetsResolvedCount", fontAssetsResolvedCount},
        {"fontAssetsMissing", fontAssetsMissing},
        {"koreanTitlePreserved", koreanTitlePreserved},
        {"koreanFontGlyphsPresent", koreanFontGlyphsPresent},
        {"koreanGlyphAtlasReady", koreanGlyphAtlasReady},
        {"koreanGlyphQuads", koreanGlyphQuads},
        {"uiComponentsLoaded", uiComponentsLoaded},
        {"platformerPlayersLoaded", platformerPlayersLoaded},
        {"physicsBodiesLoaded", physicsBodiesLoaded},
        {"physicsShapesLoaded", physicsShapesLoaded},
        {"rotatedTerrainVerified", rotatedTerrainVerified},
        {"physicsContactObserved", physicsContactObserved},
        {"restitutionResponseObserved", restitutionResponseObserved},
        {"frictionResponseObserved", frictionResponseObserved},
        {"saveRoundtrip", saveRoundtrip},
        {"scriptDrivenPrefsSaved", scriptDrivenPrefsSaved},
        {"scriptDrivenSlotSaved", scriptDrivenSlotSaved},
        {"scriptDrivenPersistence", scriptDrivenPersistence},
        {"postProcessed", postProcessed},
        {"postProcessFallback", postProcessFallback},
        {"postProcessPasses", postProcessPasses},
        {"postProcessProfileGuid", postProcessProfileGuid},
        {"selectedCameraCount", selectedCameraCount},
        {"renderedCameraCount", renderedCameraCount},
        {"postProcessedCameraCount", postProcessedCameraCount},
        {"postProcessFallbackCameraCount", postProcessFallbackCameraCount},
        {"lightingAppliedCameraCount", lightingAppliedCameraCount},
        {"lightingFallbackCameraCount", lightingFallbackCameraCount},
        {"shadowFallbackCameraCount", shadowFallbackCameraCount},
        {"selectedLightCount", selectedLightCount},
        {"shadowedLightCount", shadowedLightCount},
        {"shadowCasterDrawCount", shadowCasterDrawCount},
        {"lightingPasses", lightingPasses},
        {"shadowPasses", shadowPasses},
        {"drawCalls", drawCalls},
        {"batches", batches},
        {"textureBinds", textureBinds},
        {"shaderSwitches", shaderSwitches},
        {"outputCameraPasses", outputCameraPasses},
        {"submittedSprites", submittedSprites},
        {"submittedCommands", submittedCommands},
        {"batchFlushes", batchFlushes},
        {"batchBreaks", batchBreaks},
        {"maxSpritesPerBatch", maxSpritesPerBatch},
        {"verticesUploadedBytes", verticesUploadedBytes},
        {"queueSortNanos", queueSortNanos},
    }.dump(2);
    return output.good();
}

bool SmokeReport::Load(const std::filesystem::path& path, SmokeReport& out) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(input);
        out.executable = json.value("executable", "");
        out.status = json.value("status", "");
        out.scenePath = json.value("scenePath", "");
        out.message = json.value("message", "");
        out.objectCount = json.value("objectCount", 0U);
        out.frames = json.value("frames", 0);
        out.graphicsApi = json.value("graphicsApi", "");
        out.graphicsDriver = json.value("graphicsDriver", "");
        out.osVersion = json.value("osVersion", "");
        out.architecture = json.value("architecture", "");
        out.swapchainFormat = json.value("swapchainFormat", "");
        out.outputTextureFormat = json.value("outputTextureFormat", "");
        out.shaderArtifactFormat = json.value("shaderArtifactFormat", "");
        out.shaderManifestSha256 = json.value("shaderManifestSha256", "");
        out.gpuCopyPasses = json.value("gpuCopyPasses", 0U);
        out.gpuRenderPasses = json.value("gpuRenderPasses", 0U);
        out.gpuDrawCalls = json.value("gpuDrawCalls", 0U);
        out.gpuUploadBytes = json.value("gpuUploadBytes", 0ULL);
        out.gpuValidationEnabled = json.value("gpuValidationEnabled", false);
        out.gpuValidationErrors = json.value("gpuValidationErrors", 0U);
        out.finalPixelProbeValid = json.value("finalPixelProbeValid", false);
        out.finalPixelR = json.value("finalPixelR", 0);
        out.finalPixelG = json.value("finalPixelG", 0);
        out.finalPixelB = json.value("finalPixelB", 0);
        out.finalPixelA = json.value("finalPixelA", 0);
        out.benchmarkWarmupFrames = json.value("benchmarkWarmupFrames", 0);
        out.benchmarkMeasuredFrames = json.value("benchmarkMeasuredFrames", 0);
        out.benchmarkCpuP50Ms = json.value("benchmarkCpuP50Ms", 0.0);
        out.benchmarkCpuP95Ms = json.value("benchmarkCpuP95Ms", 0.0);
        out.benchmarkDrawCalls = json.value("benchmarkDrawCalls", 0);
        out.benchmarkBatches = json.value("benchmarkBatches", 0);
        out.benchmarkRenderPasses = json.value("benchmarkRenderPasses", 0U);
        out.benchmarkUploadBytes = json.value("benchmarkUploadBytes", 0ULL);
        out.residentMemoryBytes = json.value("residentMemoryBytes", 0ULL);
        out.peakMemoryBytes = json.value("peakMemoryBytes", 0ULL);
        out.assetsResolved = json.value("assetsResolved", false);
        out.assetCatalogLoaded = json.value("assetCatalogLoaded", false);
        out.assetCatalogRecords = json.value("assetCatalogRecords", 0);
        out.spriteAssetsResolved = json.value("spriteAssetsResolved", 0);
        out.spriteAssetsMissing = json.value("spriteAssetsMissing", 0);
        out.sceneTransitions = json.value("sceneTransitions", 0);
        out.uiDrivenSceneTransitions = json.value("uiDrivenSceneTransitions", 0);
        out.fontAssetsResolved = json.value("fontAssetsResolved", false);
        out.fontAssetsResolvedCount = json.value("fontAssetsResolvedCount", 0);
        out.fontAssetsMissing = json.value("fontAssetsMissing", 0);
        out.koreanTitlePreserved = json.value("koreanTitlePreserved", false);
        out.koreanFontGlyphsPresent = json.value("koreanFontGlyphsPresent", false);
        out.koreanGlyphAtlasReady = json.value("koreanGlyphAtlasReady", false);
        out.koreanGlyphQuads = json.value("koreanGlyphQuads", 0);
        out.uiComponentsLoaded = json.value("uiComponentsLoaded", 0);
        out.platformerPlayersLoaded = json.value("platformerPlayersLoaded", 0);
        out.physicsBodiesLoaded = json.value("physicsBodiesLoaded", 0);
        out.physicsShapesLoaded = json.value("physicsShapesLoaded", 0);
        out.rotatedTerrainVerified = json.value("rotatedTerrainVerified", false);
        out.physicsContactObserved = json.value("physicsContactObserved", false);
        out.restitutionResponseObserved = json.value("restitutionResponseObserved", false);
        out.frictionResponseObserved = json.value("frictionResponseObserved", false);
        out.saveRoundtrip = json.value("saveRoundtrip", false);
        out.scriptDrivenPrefsSaved = json.value("scriptDrivenPrefsSaved", false);
        out.scriptDrivenSlotSaved = json.value("scriptDrivenSlotSaved", false);
        out.scriptDrivenPersistence = json.value("scriptDrivenPersistence", false);
        out.postProcessed = json.value("postProcessed", false);
        out.postProcessFallback = json.value("postProcessFallback", false);
        out.postProcessPasses = json.value("postProcessPasses", 0);
        out.postProcessProfileGuid = json.value("postProcessProfileGuid", "");
        out.selectedCameraCount = json.value("selectedCameraCount", 0);
        out.renderedCameraCount = json.value("renderedCameraCount", 0);
        out.postProcessedCameraCount = json.value("postProcessedCameraCount", 0);
        out.postProcessFallbackCameraCount =
            json.value("postProcessFallbackCameraCount", 0);
        out.lightingAppliedCameraCount =
            json.value("lightingAppliedCameraCount", 0);
        out.lightingFallbackCameraCount =
            json.value("lightingFallbackCameraCount", 0);
        out.shadowFallbackCameraCount =
            json.value("shadowFallbackCameraCount", 0);
        out.selectedLightCount = json.value("selectedLightCount", 0);
        out.shadowedLightCount = json.value("shadowedLightCount", 0);
        out.shadowCasterDrawCount = json.value("shadowCasterDrawCount", 0);
        out.lightingPasses = json.value("lightingPasses", 0);
        out.shadowPasses = json.value("shadowPasses", 0);
        out.drawCalls = json.value("drawCalls", 0);
        out.batches = json.value("batches", 0);
        out.textureBinds = json.value("textureBinds", 0);
        out.shaderSwitches = json.value("shaderSwitches", 0);
        out.outputCameraPasses = json.value("outputCameraPasses", 0);
        out.submittedSprites = json.value("submittedSprites", 0);
        out.submittedCommands = json.value("submittedCommands", 0);
        out.batchFlushes = json.value("batchFlushes", 0);
        out.batchBreaks = json.value("batchBreaks", 0);
        out.maxSpritesPerBatch = json.value("maxSpritesPerBatch", 0);
        out.verticesUploadedBytes = json.value("verticesUploadedBytes", 0ULL);
        out.queueSortNanos = json.value("queueSortNanos", 0LL);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}
