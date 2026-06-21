#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Common/Log.h"
#include "Core/Guid.h"
#include "Core/Importers/PrefabImporter.h"
#include "Core/Importers/TextureImporter.h"
#include "Core/Importers/AudioImporter.h"
#include "Core/PathService.h"
#include <algorithm>

namespace molga {

static ImportResult RunImporterImpl(const std::string& importer, const std::string& abs) {
    if (importer == "TextureImporter") return TextureImporter().Import(abs);
    if (importer == "AudioImporter")   return AudioImporter().Import(abs);
    if (importer == "PrefabImporter")  return PrefabImporter().Import(abs);
    ImportResult ok; ok.success = true; return ok;  // GenericImporter
}

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
    AssetMeta meta = AssetMeta::CreateOrLoad(absPath, importer, version);

    if (ext == ".prefab") {
        std::string embedded = PrefabImporter::ReadEmbeddedGuid(absPath.string());
        if (Guid::IsValid(embedded)) {
            meta.guid = embedded;  // PrefabRegistry와 동일 guid 공유
            // 변경된 guid를 meta 파일에 다시 쓴다 (persist)
            AssetMeta::Write(absPath, meta);
        }
    }

    AssetRecord rec;
    rec.guid = meta.guid;
    rec.sourcePath = NormalizeRel(std::filesystem::relative(absPath, assetRoot_));
    rec.importer = meta.importer;
    rec.importerVersion = meta.importerVersion;

    ImportResult res = RunImporter(importer, absPath.string());
    rec.importFailed = !res.success;
    rec.artifactPath = res.artifactPath;
    if (res.width > 0)  rec.textureWidth  = res.width;
    if (res.height > 0) rec.textureHeight = res.height;

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

std::filesystem::path AssetDatabase::MissingTexturePath() {
    return PathService::Get().EngineResource("Editor/missing_texture.png");
}

ImportResult AssetDatabase::RunImporter(const std::string& importer, const std::string& abs) {
    return RunImporterImpl(importer, abs);
}

} // namespace molga
