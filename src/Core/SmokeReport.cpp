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
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}
