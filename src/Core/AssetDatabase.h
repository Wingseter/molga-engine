#pragma once

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
    std::vector<std::string> dependencies;  // 이 애셋이 참조하는 다른 애셋 guid
    bool importFailed = false;     // badge용
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

    // guid를 절대 소스 경로로 해석(런타임/에디터 공용). 없으면 빈 경로.
    std::filesystem::path AbsoluteSourcePath(const std::string& guid) const;

    size_t RecordCount() const { return byGuid_.size(); }
    const std::unordered_map<std::string, AssetRecord>& All() const { return byGuid_; }
    const std::filesystem::path& Root() const { return assetRoot_; }

    // 단일 애셋만 다시 가져온다(importerVersion 변경/외부 수정 대응).
    void Reimport(const std::string& guid);

    // Task E/F가 사용하는 인덱스 변경(파일 시스템 동작 후 호출).
    void OnSourceRenamed(const std::filesystem::path& oldRel, const std::filesystem::path& newRel);
    void OnSourceRemoved(const std::filesystem::path& rel);
    void OnSourceAdded(const std::filesystem::path& rel);

    static std::string NormalizeRel(const std::filesystem::path& rel);

public:
    AssetDatabase() = default;

private:

    void IndexOne(const std::filesystem::path& absPath);
    static std::string ImporterForExtension(const std::string& ext, int& versionOut);

    std::filesystem::path assetRoot_;
    std::unordered_map<std::string, AssetRecord> byGuid_;       // guid -> record
    std::unordered_map<std::string, std::string> sourceToGuid_; // relPath -> guid
};

} // namespace molga
