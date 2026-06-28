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
        {"assetsResolved", assetsResolved},
        {"drawCalls", drawCalls},
        {"batches", batches},
        {"textureBinds", textureBinds},
        {"shaderSwitches", shaderSwitches},
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
        out.assetsResolved = json.value("assetsResolved", false);
        out.drawCalls = json.value("drawCalls", 0);
        out.batches = json.value("batches", 0);
        out.textureBinds = json.value("textureBinds", 0);
        out.shaderSwitches = json.value("shaderSwitches", 0);
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
