#pragma once

#include "Assets/FontAsset.h"
#include "Core/Importers/Importer.h"
#include "Core/AssetMeta.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

class FontArtifactStore;

// 카탈로그를 어떤 신뢰 경계에서 읽는지 명시한다. 예전에는 "packageRoot"라는
// 경로 인자 하나가 프로젝트 저작 카탈로그와 봉인된 패키지 카탈로그를 동시에
// 의미했고, 그래서 어떤 폰트 locator를 신뢰해야 하는지 알 수 없었다.
enum class AssetCatalogMode : std::uint8_t { Project, SealedPackage };

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
    // 카탈로그 재적재 후에도 살아남는 기계 판독 실패 사유.
    std::vector<molga::text::TextDiagnostic> importDiagnostics;
    // 폰트 애셋만 채운다. 검증된 불변 바이트의 권한 있는 위치다.
    std::optional<VerifiedFontArtifact> fontArtifact;
};

// 한 record가 카탈로그에 실을 수 있는 진단 수의 상한. 진단은 그대로 디스크에
// 저장되므로, 상한이 없으면 병든 애셋 하나가 카탈로그를 무한히 키운다.
inline constexpr std::size_t kMaxImportDiagnosticsPerRecord = 8;

// 카탈로그 정규 표현. LoadCatalog/SaveCatalog가 쓰는 바로 그 코드 경로이므로
// 테스트가 여기를 통과시키면 실제 재적재도 같은 규칙을 따른다.
nlohmann::json AssetRecordToJson(const AssetRecord& record);
std::optional<AssetRecord> AssetRecordFromJson(
    const nlohmann::json& record, std::string& errorOut,
    AssetCatalogMode mode = AssetCatalogMode::Project);

// ImportResult를 record에 반영한다. 비어 있는 진단 assetGuid만 record의 guid로
// 채우고, legacy importError는 사람이 읽는 호환 요약으로 유지한다.
void ApplyImportResultToRecord(const ImportResult& result, AssetRecord& record);

// guid <-> record, sourcePath -> guid 양방향 인덱스.
class AssetDatabase {
public:
    static AssetDatabase& Get();   // 싱글톤 접근(테스트는 지역 인스턴스 사용 가능)

    // assetRoot를 재귀 스캔: 소스 애셋마다 .meta를 보장하고 record를 만든다.
    void ScanProject(const std::filesystem::path& assetRoot);

    const AssetRecord* Find(const std::string& guid) const;
    std::string GuidForSource(const std::string& relativeSourcePath) const;
    std::string GuidForAbsolutePath(const std::filesystem::path& absolutePath) const;

    // 폰트 바이트 권한을 딱 한 번 묶는다. ScanProject/LoadCatalog 이전에
    // 반드시 성공해야 하고, null이나 재바인딩은 거절한다. 이미 발행된
    // 레이아웃/리소스가 같은 store를 계속 신뢰할 수 있어야 하기 때문이다.
    bool BindFontArtifactStore(std::shared_ptr<const FontArtifactStore>,
                               std::string* errorOut = nullptr);
    const FontArtifactStore* FontArtifacts() const noexcept;

    // 카탈로그 저장 / 로드 / 비우기 (런타임 및 빌드 용)
    bool SaveCatalog(const std::filesystem::path& path,
                     const std::string& excludedSourcePrefix = {}) const;
    bool LoadCatalog(const std::filesystem::path& catalogPath,
                     const std::filesystem::path& storageRoot,
                     AssetCatalogMode mode,
                     std::string* errorOut = nullptr);
    void Clear();

    // 성공한 폰트 발행마다 증가한다. 산출물이나 카탈로그가 실패하면 이전
    // 세대가 그대로 권한이다.
    std::uint64_t ContentGeneration() const noexcept { return contentGeneration_; }

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
    // receives the storage root containing the serialized Assets/ paths.
    bool catalogPackageRoot_ = false;
    // Clear()가 지우지 않는 유일한 상태. 바인딩은 database 수명 동안 불변이다.
    std::shared_ptr<const FontArtifactStore> fontArtifacts_;
    std::uint64_t contentGeneration_ = 0;
    std::unordered_map<std::string, AssetRecord> byGuid_;       // guid -> record
    std::unordered_map<std::string, std::string> sourceToGuid_; // relPath -> guid
};

} // namespace molga
