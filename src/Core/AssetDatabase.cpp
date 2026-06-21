#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Common/Log.h"
#include <algorithm>

namespace molga {

AssetDatabase& AssetDatabase::Get() {
    static AssetDatabase instance;
    return instance;
}

std::string AssetDatabase::NormalizeRel(const std::filesystem::path& rel) {
    std::string s = rel.generic_string();  // 슬래시 정규화
    return s;
}

std::string AssetDatabase::ImporterForExtension(const std::string& ext, int& versionOut) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") { versionOut = 1; return "TextureImporter"; }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")  { versionOut = 1; return "AudioImporter"; }
    if (ext == ".prefab")                                  { versionOut = 1; return "PrefabImporter"; }
    versionOut = 1;
    return "GenericImporter";
}

void AssetDatabase::IndexOne(const std::filesystem::path& absPath) {
    std::string ext = absPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".meta") return;  // sidecar 자체는 애셋이 아니다

    int version = 1;
    std::string importer = ImporterForExtension(ext, version);
    
    // .prefab 이면 임시로 guid 추출 기능을 추후 PrefabImporter 구현 시 보완하도록, 
    // 여기서는 기본 생성/로드 위주로 진행하고, 임베디드 guid가 있는지 체크하는 얇은 마이그레이션만 둡니다.
    AssetMeta meta = AssetMeta::CreateOrLoad(absPath, importer, version);

    AssetRecord rec;
    rec.guid = meta.guid;
    rec.sourcePath = NormalizeRel(std::filesystem::relative(absPath, assetRoot_));
    rec.importer = meta.importer;
    rec.importerVersion = meta.importerVersion;

    sourceToGuid_[rec.sourcePath] = rec.guid;
    byGuid_[rec.guid] = std::move(rec);
}

void AssetDatabase::ScanProject(const std::filesystem::path& assetRoot) {
    assetRoot_ = assetRoot;
    byGuid_.clear();
    sourceToGuid_.clear();
    if (assetRoot_.empty() || !std::filesystem::exists(assetRoot_)) return;

    try {
        for (const auto& e : std::filesystem::recursive_directory_iterator(assetRoot_)) {
            if (e.is_regular_file()) {
                if (e.path().filename() == ".trash") continue; // .trash 디렉터리 건너뛰기
                // 경로 내에 .trash 가 포함되어 있으면 패스
                auto rel = std::filesystem::relative(e.path(), assetRoot_);
                bool inTrash = false;
                for (const auto& p : rel) {
                    if (p == ".trash") {
                        inTrash = true;
                        break;
                    }
                }
                if (inTrash) continue;

                IndexOne(e.path());
            }
        }
    } catch (const std::exception& ex) {
        Log::Error("AssetDatabase", std::string("scan failed: ") + ex.what());
    }
}

const AssetRecord* AssetDatabase::Find(const std::string& guid) const {
    auto it = byGuid_.find(guid);
    return it == byGuid_.end() ? nullptr : &it->second;
}

std::string AssetDatabase::GuidForSource(const std::string& relativeSourcePath) const {
    auto it = sourceToGuid_.find(NormalizeRel(relativeSourcePath));
    return it == sourceToGuid_.end() ? std::string() : it->second;
}

std::filesystem::path AssetDatabase::AbsoluteSourcePath(const std::string& guid) const {
    const AssetRecord* rec = Find(guid);
    if (!rec) return {};
    return assetRoot_ / rec->sourcePath;
}

void AssetDatabase::Reimport(const std::string& guid) {
    const AssetRecord* rec = Find(guid);
    if (!rec) return;
    IndexOne(assetRoot_ / rec->sourcePath);
}

void AssetDatabase::OnSourceAdded(const std::filesystem::path& rel) {
    IndexOne(assetRoot_ / rel);
}

void AssetDatabase::OnSourceRemoved(const std::filesystem::path& rel) {
    std::string key = NormalizeRel(rel);
    auto it = sourceToGuid_.find(key);
    if (it == sourceToGuid_.end()) return;
    byGuid_.erase(it->second);
    sourceToGuid_.erase(it);
}

void AssetDatabase::OnSourceRenamed(const std::filesystem::path& oldRel,
                                    const std::filesystem::path& newRel) {
    std::string oldKey = NormalizeRel(oldRel);
    auto it = sourceToGuid_.find(oldKey);
    if (it == sourceToGuid_.end()) { OnSourceAdded(newRel); return; }
    std::string guid = it->second;
    sourceToGuid_.erase(it);
    std::string newKey = NormalizeRel(newRel);
    sourceToGuid_[newKey] = guid;
    auto recIt = byGuid_.find(guid);
    if (recIt != byGuid_.end()) recIt->second.sourcePath = newKey;
}

} // namespace molga
