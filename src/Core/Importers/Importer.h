#pragma once

#include "Text/TextDiagnostic.h"

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
    // 기계가 읽는 실패 사유. legacy `error`는 사람이 읽는 요약으로 남는다.
    // 애셋 하나가 만드는 진단 수는 반드시 유한해야 한다(카탈로그에 그대로
    // 저장되므로): importer는 첫 실패에서 즉시 반환하고, AssetDatabase는
    // kMaxImportDiagnosticsPerRecord로 잘라 낸다.
    std::vector<molga::text::TextDiagnostic> importDiagnostics;
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
