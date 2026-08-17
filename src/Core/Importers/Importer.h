#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace molga {

struct ImportResult {
    bool success = false;
    std::string error;
    std::string artifactPath;   // 산출물 경로(없으면 비움)
    int width = 0;              // 텍스처용(없으면 0)
    int height = 0;
    std::vector<std::string> dependencies;
    nlohmann::json metadata = nlohmann::json::object();
};

// 소스 애셋을 검증/메타 추출하는 importer.
class IImporter {
public:
    virtual ~IImporter() = default;
    virtual std::string Name() const = 0;
    virtual int Version() const = 0;
    virtual bool CanImport(const std::string& ext) const = 0;
    virtual ImportResult Import(const std::string& absSourcePath) const = 0;
    virtual ImportResult Import(const std::string& absSourcePath,
                                const nlohmann::json& settings) const {
        (void)settings;
        return Import(absSourcePath);
    }
};

} // namespace molga
