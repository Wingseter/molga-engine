#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Common/Log.h"
#include "Core/Guid.h"
#include "Core/Importers/PrefabImporter.h"
#include "Core/Importers/TextureImporter.h"
#include "Core/Importers/AudioImporter.h"
#include "Core/Importers/FontImporter.h"
#include "Core/PathService.h"
#include "Rendering/TextRenderer.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace molga {

static ImportResult RunImporterImpl(const std::string& importer, const std::string& abs) {
    if (importer == "TextureImporter") return TextureImporter().Import(abs);
    if (importer == "AudioImporter")   return AudioImporter().Import(abs);
    if (importer == "PrefabImporter")  return PrefabImporter().Import(abs);
    if (importer == "FontImporter")    return FontImporter().Import(abs);
    ImportResult ok; ok.success = true; return ok;  // GenericImporter
}

static std::string ComputeFileHash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::uint64_t hash = 1469598103934665603ULL;
    char buffer[4096];
    while (file) {
        file.read(buffer, sizeof(buffer));
        const std::streamsize count = file.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

AssetDatabase& AssetDatabase::Get() {
    static AssetDatabase instance;
    return instance;
}

std::string AssetDatabase::NormalizeRel(const std::filesystem::path& rel) {
    std::string s = rel.generic_string();  // 슬래시 정규화
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string AssetDatabase::ImporterForExtension(const std::string& ext, int& versionOut) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") { versionOut = 1; return "TextureImporter"; }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")  { versionOut = 1; return "AudioImporter"; }
    if (ext == ".prefab")                                  { versionOut = 1; return "PrefabImporter"; }
    if (ext == ".ttf" || ext == ".otf")                    { versionOut = 1; return "FontImporter"; }
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

    // Migrate sidecars created before a built-in importer existed (notably
    // .ttf/.otf files that used to be GenericImporter).
    if (importer != "GenericImporter" &&
        (meta.importer != importer || meta.importerVersion != version)) {
        meta.importer = importer;
        meta.importerVersion = version;
        AssetMeta::Write(absPath, meta);
    }

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
    std::filesystem::path rel;
    if (assetRoot_.filename() == "Assets") {
        rel = std::filesystem::relative(absPath, assetRoot_.parent_path());
    } else {
        rel = "Assets" / std::filesystem::relative(absPath, assetRoot_);
    }
    rec.sourcePath = NormalizeRel(rel);
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
    if (this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateAllFonts();
    }
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
    if (relativeSourcePath.empty()) return "";
    
    // Normalize separators
    std::filesystem::path p(relativeSourcePath);
    std::string normalized = NormalizeRel(p);
    
    // 1. Direct match (e.g., "Assets/Textures/player.png")
    auto it = sourceToGuid_.find(normalized);
    if (it != sourceToGuid_.end()) {
        return it->second;
    }
    
    // 2. Prepend "Assets/" if not present (e.g., "Textures/player.png" -> "Assets/Textures/player.png")
    if (normalized.rfind("Assets/", 0) != 0) {
        std::string withAssets = "Assets/" + normalized;
        it = sourceToGuid_.find(withAssets);
        if (it != sourceToGuid_.end()) {
            return it->second;
        }
    }
    
    // 3. Fallback: match by filename or suffix for legacy scenes
    std::string filename = p.filename().generic_string();
    for (const auto& [relPath, guid] : sourceToGuid_) {
        if (relPath == "Assets/" + filename || 
            (relPath.size() > filename.size() && 
             relPath.compare(relPath.size() - filename.size() - 1, filename.size() + 1, "/" + filename) == 0)) {
            return guid;
        }
    }
    
    return "";
}

std::string AssetDatabase::GuidForAbsolutePath(const std::filesystem::path& absolutePath) const {
    if (assetRoot_.empty() || absolutePath.empty()) return "";
    
    std::filesystem::path rootDir = assetRoot_;
    if (assetRoot_.filename() == "Assets") {
        rootDir = assetRoot_.parent_path();
    }
    
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(absolutePath, rootDir, ec);
    if (!ec && rel.string().find("..") == std::string::npos) {
        return GuidForSource(rel.generic_string());
    }
    
    return GuidForSource(absolutePath.filename().generic_string());
}

static std::string GetCanonicalPathStatic(const std::filesystem::path& absPath, const std::filesystem::path& assetRoot) {
    if (assetRoot.empty()) return "";
    std::filesystem::path rel;
    if (assetRoot.filename() == "Assets") {
        rel = std::filesystem::relative(absPath, assetRoot.parent_path());
    } else {
        rel = "Assets" / std::filesystem::relative(absPath, assetRoot);
    }
    return AssetDatabase::NormalizeRel(rel);
}

std::filesystem::path AssetDatabase::AbsoluteSourcePath(const std::string& guid) const {
    const AssetRecord* rec = Find(guid);
    if (!rec) return {};
    
    std::filesystem::path rootDir = assetRoot_;
    if (assetRoot_.filename() == "Assets") {
        rootDir = assetRoot_.parent_path();
    }
    return rootDir / rec->sourcePath;
}

void AssetDatabase::Reimport(const std::string& guid) {
    const AssetRecord* rec = Find(guid);
    if (!rec) return;
    const std::filesystem::path source = AbsoluteSourcePath(guid);
    const bool isFont = rec->importer == "FontImporter";
    IndexOne(source);
    if (isFont && this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateFont(guid);
    }
}

void AssetDatabase::OnSourceAdded(const std::filesystem::path& rel) {
    IndexOne(assetRoot_ / rel);
}

void AssetDatabase::OnSourceRemoved(const std::filesystem::path& rel) {
    std::filesystem::path absPath = assetRoot_ / rel;
    std::string key = GetCanonicalPathStatic(absPath, assetRoot_);
    auto it = sourceToGuid_.find(key);
    if (it == sourceToGuid_.end()) return;
    const std::string guid = it->second;
    const auto record = byGuid_.find(guid);
    const bool isFont = record != byGuid_.end() && record->second.importer == "FontImporter";
    byGuid_.erase(it->second);
    sourceToGuid_.erase(it);
    if (isFont && this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateFont(guid);
    }
}

void AssetDatabase::OnSourceRenamed(const std::filesystem::path& oldRel,
                                    const std::filesystem::path& newRel) {
    std::filesystem::path oldAbs = assetRoot_ / oldRel;
    std::filesystem::path newAbs = assetRoot_ / newRel;
    std::string oldKey = GetCanonicalPathStatic(oldAbs, assetRoot_);
    
    auto it = sourceToGuid_.find(oldKey);
    if (it == sourceToGuid_.end()) { OnSourceAdded(newRel); return; }
    std::string guid = it->second;
    sourceToGuid_.erase(it);
    
    std::string newKey = GetCanonicalPathStatic(newAbs, assetRoot_);
    sourceToGuid_[newKey] = guid;
    auto recIt = byGuid_.find(guid);
    if (recIt != byGuid_.end()) {
        const bool isFont = recIt->second.importer == "FontImporter";
        recIt->second.sourcePath = newKey;
        if (isFont && this == &AssetDatabase::Get()) {
            TextRenderer::Get().InvalidateFont(guid);
        }
    }
}

bool AssetDatabase::SaveCatalog(const std::filesystem::path& path) const {
    try {
        nlohmann::json j;
        j["schemaVersion"] = 1;
        j["assetRootMode"] = "packageRoot";
        
        std::filesystem::path rootDir = assetRoot_;
        if (assetRoot_.filename() == "Assets") {
            rootDir = assetRoot_.parent_path();
        }

        nlohmann::json recordsJson = nlohmann::json::array();
        for (const auto& [guid, rec] : byGuid_) {
            nlohmann::json r;
            r["guid"] = rec.guid;
            r["sourcePath"] = rec.sourcePath;
            r["importer"] = rec.importer;
            r["importerVersion"] = rec.importerVersion;
            r["artifactPath"] = rec.artifactPath;
            r["hash"] = ComputeFileHash(rootDir / rec.sourcePath);
            r["width"] = rec.textureWidth;
            r["height"] = rec.textureHeight;
            recordsJson.push_back(r);
        }
        j["records"] = recordsJson;
        
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool AssetDatabase::LoadCatalog(const std::filesystem::path& path, const std::filesystem::path& packageRoot) {
    Clear();
    assetRoot_ = packageRoot;
    
    if (!std::filesystem::exists(path)) {
        return false;
    }
    
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        nlohmann::json j;
        file >> j;
        
        if (j.contains("records") && j["records"].is_array()) {
            for (const auto& r : j["records"]) {
                AssetRecord rec;
                rec.guid = r.value("guid", "");
                rec.sourcePath = r.value("sourcePath", "");
                rec.importer = r.value("importer", "");
                rec.importerVersion = r.value("importerVersion", 1);
                rec.artifactPath = r.value("artifactPath", "");
                rec.hash = r.value("hash", "");
                rec.textureWidth = r.value("width", 0);
                rec.textureHeight = r.value("height", 0);
                
                sourceToGuid_[rec.sourcePath] = rec.guid;
                byGuid_[rec.guid] = std::move(rec);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

void AssetDatabase::Clear() {
    if (this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateAllFonts();
    }
    byGuid_.clear();
    sourceToGuid_.clear();
}

std::filesystem::path AssetDatabase::MissingTexturePath() {
    auto runtimePath = PathService::Get().AssetRoot() / "Resources/missing_texture.png";
    if (std::filesystem::exists(runtimePath)) {
        return runtimePath;
    }
    return PathService::Get().EngineResource("Editor/missing_texture.png");
}

ImportResult AssetDatabase::RunImporter(const std::string& importer, const std::string& abs) {
    return RunImporterImpl(importer, abs);
}

} // namespace molga
