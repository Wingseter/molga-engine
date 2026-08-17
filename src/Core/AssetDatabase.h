#pragma once

#include "Core/Importers/Importer.h"
#include "Core/AssetMeta.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

// 임포트된 단일 애셋의 식별·임포트 상태.
struct AssetRecord {
    std::string guid;
    std::string sourcePath;        // asset root 기준 상대 경로(슬래시 정규화)
    std::string importer;          // 예: "TextureImporter"
    int importerVersion = 1;
    std::string artifactPath;      // 가져온 산출물 경로(없으면 빈 문자열)
    std::string hash;              // source file content hash at catalog build time
    std::vector<std::string> dependencies;  // 이 애셋이 참조하는 다른 애셋 guid
    nlohmann::json settings = nlohmann::json::object();
    nlohmann::json metadata = nlohmann::json::object();
    bool importFailed = false;     // badge용
    std::string importError;
    bool generated = false;        // 산출물/임시 애셋 표시 badge용
    int textureWidth = 0;
    int textureHeight = 0;
};

// guid <-> record, sourcePath -> guid 양방향 인덱스.
class AssetDatabase {
public:
    static AssetDatabase& Get();   // 싱글톤 접근(테스트는 지역 인스턴스 사용 가능)

    // assetRoot를 재귀 스캔: 소스 애셋마다 .meta를 보장하고 record를 만든다.
    void ScanProject(const std::filesystem::path& assetRoot);

    const AssetRecord* Find(const std::string& guid) const;
    std::string GuidForSource(const std::string& relativeSourcePath) const;
    std::string GuidForAbsolutePath(const std::filesystem::path& absolutePath) const;

    // 카탈로그 저장 / 로드 / 비우기 (런타임 및 빌드 용)
    bool SaveCatalog(const std::filesystem::path& path,
                     const std::string& excludedSourcePrefix = {}) const;
    bool LoadCatalog(const std::filesystem::path& path, const std::filesystem::path& packageRoot);
    void Clear();

    // guid를 절대 소스 경로로 해석(런타임/에디터 공용). 없으면 빈 경로.
    std::filesystem::path AbsoluteSourcePath(const std::string& guid) const;

    size_t RecordCount() const { return byGuid_.size(); }
    const std::unordered_map<std::string, AssetRecord>& All() const { return byGuid_; }
    const std::filesystem::path& Root() const { return assetRoot_; }

    // 단일 애셋만 다시 가져온다(importerVersion 변경/외부 수정 대응).
    void Reimport(const std::string& guid);
    bool TryReimport(const std::string& guid, std::string* errorOut = nullptr);

    AssetMeta MetaForGuid(const std::string& guid) const;
    bool WriteMeta(const std::string& guid, const AssetMeta& meta,
                   bool reimport = true, std::string* errorOut = nullptr);

    // Task E/F가 사용하는 인덱스 변경(파일 시스템 동작 후 호출).
    void OnSourceRenamed(const std::filesystem::path& oldRel, const std::filesystem::path& newRel);
    void OnSourceRemoved(const std::filesystem::path& rel);
    void OnSourceAdded(const std::filesystem::path& rel);

    static std::string NormalizeRel(const std::filesystem::path& rel);

    // guid가 인덱스에 없으면 누락. 호출자는 placeholder를 사용해야 한다.
    bool IsMissing(const std::string& guid) const { return Find(guid) == nullptr; }

    // 누락 텍스처용 placeholder 절대 경로(엔진 리소스). ResolveAssets가 사용.
    static std::filesystem::path MissingTexturePath();

public:
    AssetDatabase() = default;

private:
    static ImportResult RunImporter(const std::string& importer, const std::string& abs,
                                    const nlohmann::json& settings);

    void IndexOne(const std::filesystem::path& absPath);
    static std::string ImporterForExtension(const std::string& ext, int& versionOut);

    std::filesystem::path assetRoot_;
    // ScanProject receives the directory being scanned, while LoadCatalog
    // receives the package root containing the serialized Assets/ paths.
    bool catalogPackageRoot_ = false;
    std::unordered_map<std::string, AssetRecord> byGuid_;       // guid -> record
    std::unordered_map<std::string, std::string> sourceToGuid_; // relPath -> guid
};

} // namespace molga
