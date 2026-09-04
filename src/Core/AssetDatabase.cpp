#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Assets/FontArtifactStore.h"
#include "Common/Log.h"
#include "Core/Guid.h"
#include "Core/Importers/PrefabImporter.h"
#include "Core/Importers/TextureImporter.h"
#include "Core/Importers/AudioImporter.h"
#include "Core/Importers/FontImporter.h"
#include "Core/Importers/ImporterRegistry.h"
#include "Core/PersistentStorage.h"
#include "Core/TextureImportSettings.h"
#include "Core/PathService.h"
#include "Core/TextureManager.h"
#include "Rendering/TextRenderer.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <utility>

namespace molga {

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

namespace {

// TextSeverity의 안정 문자열. 숫자 enum을 저장하면 값 순서를 바꾸는 순간
// 과거 카탈로그의 심각도가 조용히 다른 값으로 재해석된다.
const char* StableTextSeverity(molga::text::TextSeverity severity) {
    switch (severity) {
        case molga::text::TextSeverity::Info:    return "Info";
        case molga::text::TextSeverity::Warning: return "Warning";
        case molga::text::TextSeverity::Error:   return "Error";
        case molga::text::TextSeverity::Blocker: return "Blocker";
    }
    return "Error";
}

std::optional<molga::text::TextSeverity> ParseStableTextSeverity(
    const std::string& value) {
    if (value == "Info")    return molga::text::TextSeverity::Info;
    if (value == "Warning") return molga::text::TextSeverity::Warning;
    if (value == "Error")   return molga::text::TextSeverity::Error;
    if (value == "Blocker") return molga::text::TextSeverity::Blocker;
    return std::nullopt;
}

nlohmann::json DiagnosticToJson(const molga::text::TextDiagnostic& diagnostic) {
    return nlohmann::json{
        {"code", molga::text::StableTextDiagnosticCode(diagnostic.code)},
        {"severity", StableTextSeverity(diagnostic.severity)},
        {"subsystem", diagnostic.subsystem},
        {"message", diagnostic.message},
        {"remediation", diagnostic.remediation},
        {"assetGuid", diagnostic.assetGuid},
        {"sceneObjectId", diagnostic.sceneObjectId},
        {"componentType", diagnostic.componentType},
        {"sourceByteBegin", diagnostic.sourceByteRange.begin},
        {"sourceByteEnd", diagnostic.sourceByteRange.end}};
}

bool ReadJsonString(const nlohmann::json& parent, const char* key,
                    std::string& out) {
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_string()) return false;
    out = found->get<std::string>();
    return true;
}

bool ReadJsonUnsigned32(const nlohmann::json& parent, const char* key,
                        std::uint32_t& out) {
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_number_unsigned()) return false;
    const std::uint64_t value = found->get<std::uint64_t>();
    if (value > 0xFFFFFFFFULL) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

// Step 3b: 알 수 없는/숫자/별칭 코드는 그 진단만 조용히 버리는 것이 아니라
// record 전체를 거절한다. 진단 하나가 사라지면 나중 패키지 검증이 "깨끗한
// 캐시 적중"으로 오해하기 때문이다.
bool DiagnosticFromJson(const nlohmann::json& value,
                        molga::text::TextDiagnostic& out,
                        std::string& errorOut) {
    if (!value.is_object()) {
        errorOut = "import diagnostic is not an object";
        return false;
    }
    std::string code;
    if (!ReadJsonString(value, "code", code)) {
        errorOut = "unknown diagnostic code: the code field is missing or "
                   "is not a stable code string";
        return false;
    }
    const auto parsedCode = molga::text::ParseStableTextDiagnosticCode(code);
    if (!parsedCode) {
        errorOut = "unknown diagnostic code: " + code;
        return false;
    }
    std::string severity;
    if (!ReadJsonString(value, "severity", severity)) {
        errorOut = "import diagnostic severity is missing";
        return false;
    }
    const auto parsedSeverity = ParseStableTextSeverity(severity);
    if (!parsedSeverity) {
        errorOut = "unknown diagnostic severity: " + severity;
        return false;
    }
    out.code = *parsedCode;
    out.severity = *parsedSeverity;
    if (!ReadJsonString(value, "subsystem", out.subsystem) ||
        !ReadJsonString(value, "message", out.message) ||
        !ReadJsonString(value, "remediation", out.remediation) ||
        !ReadJsonString(value, "assetGuid", out.assetGuid) ||
        !ReadJsonString(value, "componentType", out.componentType)) {
        errorOut = "import diagnostic is missing a required text field";
        return false;
    }
    if (!ReadJsonUnsigned32(value, "sceneObjectId", out.sceneObjectId) ||
        !ReadJsonUnsigned32(value, "sourceByteBegin",
                            out.sourceByteRange.begin) ||
        !ReadJsonUnsigned32(value, "sourceByteEnd", out.sourceByteRange.end)) {
        errorOut = "import diagnostic is missing a required numeric field";
        return false;
    }
    return true;
}

constexpr const char* kFontArtifactKeys[] = {
    "sourceSha256", "artifactStorage", "artifactRelativePath",
    "artifactSha256", "artifactByteSize"};

// Step 4b: 폰트 locator는 다섯 필드가 전부 있거나 전부 없어야 한다. 일부만
// 살아남은 locator는 "검증된 불변 바이트"라는 계약 자체를 깨뜨린다.
bool FontArtifactFromJson(const nlohmann::json& record,
                          molga::AssetCatalogMode mode,
                          std::optional<molga::VerifiedFontArtifact>& out,
                          std::string& errorOut) {
    std::size_t present = 0;
    for (const char* key : kFontArtifactKeys) {
        if (record.find(key) != record.end()) ++present;
    }
    if (present == 0U) {
        out.reset();
        return true;
    }
    if (present != std::size(kFontArtifactKeys)) {
        errorOut = "font artifact record is missing part of its locator";
        return false;
    }

    molga::VerifiedFontArtifact artifact;
    std::string storage;
    std::string relativePath;
    if (!ReadJsonString(record, "sourceSha256", artifact.sourceSha256) ||
        !ReadJsonString(record, "artifactSha256", artifact.artifactSha256) ||
        !ReadJsonString(record, "artifactStorage", storage) ||
        !ReadJsonString(record, "artifactRelativePath", relativePath)) {
        errorOut = "font artifact locator field is not a string";
        return false;
    }
    const auto byteSize = record.find("artifactByteSize");
    if (!byteSize->is_number_unsigned() ||
        byteSize->get<std::uint64_t>() == 0U) {
        errorOut = "font artifact byte size must be a positive integer";
        return false;
    }
    artifact.byteSize = byteSize->get<std::uint64_t>();

    if (!molga::IsLowercaseSha256(artifact.sourceSha256) ||
        !molga::IsLowercaseSha256(artifact.artifactSha256)) {
        errorOut = "font artifact SHA-256 is not lowercase hexadecimal";
        return false;
    }
    if (artifact.sourceSha256 != artifact.artifactSha256) {
        // 산출물은 원본의 파생물이 아니라 같은 바이트의 사본이므로, 두 SHA가
        // 다르면 어느 쪽도 권한으로 삼을 수 없다.
        errorOut = "font artifact and source SHA-256 must be identical";
        return false;
    }
    const auto parsedStorage = molga::ParseStableFontArtifactStorage(storage);
    if (!parsedStorage) {
        errorOut = "unknown font artifact storage: " + storage;
        return false;
    }
    const molga::FontArtifactStorage required =
        mode == molga::AssetCatalogMode::Project
            ? molga::FontArtifactStorage::ProjectLibrary
            : molga::FontArtifactStorage::PackagedResource;
    if (*parsedStorage != required) {
        errorOut = "font artifact storage " + storage +
                   " is not accepted by this catalog mode";
        return false;
    }

    std::string normalized;
    if (!molga::NormalizeFontArtifactRelativePath(relativePath, normalized)) {
        errorOut = "font artifact path is not a safe relative path: " +
                   relativePath;
        return false;
    }
    if (*parsedStorage == molga::FontArtifactStorage::ProjectLibrary) {
        if (normalized !=
            molga::FontArtifactRelativePath(artifact.artifactSha256)) {
            errorOut = "project font artifact path is not its own content "
                       "address: " + normalized;
            return false;
        }
    } else if (normalized.rfind("Assets/", 0) != 0U) {
        errorOut = "packaged font artifact path is outside Assets/: " +
                   normalized;
        return false;
    }
    artifact.locator.storage = *parsedStorage;
    artifact.locator.relativePath = std::filesystem::path(normalized);
    out = artifact;
    return true;
}

} // namespace

nlohmann::json AssetRecordToJson(const AssetRecord& record) {
    nlohmann::json out;
    out["guid"] = record.guid;
    out["sourcePath"] = record.sourcePath;
    out["importer"] = record.importer;
    out["importerVersion"] = record.importerVersion;
    out["artifactPath"] = record.artifactPath;
    out["hash"] = record.hash;
    out["width"] = record.textureWidth;
    out["height"] = record.textureHeight;
    out["settings"] = record.settings;
    out["dependencies"] = record.dependencies;
    out["metadata"] = record.metadata;
    out["importFailed"] = record.importFailed;
    out["importError"] = record.importError;
    out["generated"] = record.generated;
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const molga::text::TextDiagnostic& diagnostic :
         record.importDiagnostics) {
        diagnostics.push_back(DiagnosticToJson(diagnostic));
    }
    out["importDiagnostics"] = std::move(diagnostics);
    if (record.fontArtifact) {
        out["sourceSha256"] = record.fontArtifact->sourceSha256;
        out["artifactStorage"] =
            StableFontArtifactStorage(record.fontArtifact->locator.storage);
        out["artifactRelativePath"] =
            record.fontArtifact->locator.relativePath.generic_string();
        out["artifactSha256"] = record.fontArtifact->artifactSha256;
        out["artifactByteSize"] = record.fontArtifact->byteSize;
    }
    return out;
}

std::optional<AssetRecord> AssetRecordFromJson(const nlohmann::json& record,
                                               std::string& errorOut,
                                               AssetCatalogMode mode) {
    errorOut.clear();
    if (!record.is_object()) {
        errorOut = "asset record is not an object";
        return std::nullopt;
    }
    AssetRecord out;
    out.guid = record.value("guid", std::string{});
    out.sourcePath = record.value("sourcePath", std::string{});
    out.importer = record.value("importer", std::string{});
    out.importerVersion = record.value("importerVersion", 1);
    out.artifactPath = record.value("artifactPath", std::string{});
    out.hash = record.value("hash", std::string{});
    out.textureWidth = record.value("width", 0);
    out.textureHeight = record.value("height", 0);
    if (record.contains("settings") && record["settings"].is_object()) {
        out.settings = record["settings"];
    }
    if (record.contains("dependencies") && record["dependencies"].is_array()) {
        out.dependencies = record["dependencies"].get<std::vector<std::string>>();
    }
    if (record.contains("metadata") && record["metadata"].is_object()) {
        out.metadata = record["metadata"];
    }
    out.importFailed = record.value("importFailed", false);
    out.importError = record.value("importError", std::string{});
    out.generated = record.value("generated", false);

    if (!Guid::IsValid(out.guid)) {
        errorOut = "asset record guid is invalid: " + out.guid;
        return std::nullopt;
    }
    if (out.sourcePath.empty()) {
        errorOut = "asset record source path is empty";
        return std::nullopt;
    }

    const auto diagnostics = record.find("importDiagnostics");
    if (diagnostics != record.end()) {
        if (!diagnostics->is_array()) {
            errorOut = "importDiagnostics is not an array";
            return std::nullopt;
        }
        if (diagnostics->size() > kMaxImportDiagnosticsPerRecord) {
            errorOut = "asset record carries more import diagnostics than the "
                       "catalog bound allows";
            return std::nullopt;
        }
        for (const auto& value : *diagnostics) {
            molga::text::TextDiagnostic diagnostic;
            if (!DiagnosticFromJson(value, diagnostic, errorOut)) {
                return std::nullopt;
            }
            out.importDiagnostics.push_back(std::move(diagnostic));
        }
    }
    if (!FontArtifactFromJson(record, mode, out.fontArtifact, errorOut)) {
        return std::nullopt;
    }
    return out;
}

void ApplyImportResultToRecord(const ImportResult& result, AssetRecord& record) {
    record.importFailed = !result.success;
    record.artifactPath = result.artifactPath;
    record.dependencies = result.dependencies;
    record.metadata = result.metadata;
    if (result.width > 0)  record.textureWidth  = result.width;
    if (result.height > 0) record.textureHeight = result.height;

    record.importDiagnostics = result.importDiagnostics;
    if (record.importDiagnostics.size() > kMaxImportDiagnosticsPerRecord) {
        record.importDiagnostics.resize(kMaxImportDiagnosticsPerRecord);
    }
    for (molga::text::TextDiagnostic& diagnostic : record.importDiagnostics) {
        // 이미 채워진 GUID는 건드리지 않는다. importer는 자기 자신이 아닌
        // 다른 애셋(예: license 애셋)을 가리키는 진단도 낼 수 있다.
        if (diagnostic.assetGuid.empty()) diagnostic.assetGuid = record.guid;
    }

    record.importError = result.error;
    if (record.importError.empty() && !record.importDiagnostics.empty()) {
        // legacy 필드는 사람이 읽는 호환 요약으로만 남는다. 기계 판독은
        // importDiagnostics를 본다.
        std::string summary;
        for (const molga::text::TextDiagnostic& diagnostic :
             record.importDiagnostics) {
            if (!summary.empty()) summary += "; ";
            summary += molga::text::StableTextDiagnosticCode(diagnostic.code);
            summary += ": ";
            summary += diagnostic.message;
        }
        record.importError = std::move(summary);
    }
}

AssetDatabase& AssetDatabase::Get() {
    static AssetDatabase instance;
    return instance;
}

bool AssetDatabase::BindFontArtifactStore(
    std::shared_ptr<const FontArtifactStore> store, std::string* errorOut) {
    if (!store) {
        if (errorOut) *errorOut = "font artifact store must not be null";
        return false;
    }
    if (fontArtifacts_) {
        // 이미 발행된 record/레이아웃이 이 store의 바이트를 신뢰하고 있으므로
        // 교체는 조용한 정체성 변경이 된다. Clear()도 이 바인딩은 지우지 않는다.
        if (errorOut) {
            *errorOut = "a font artifact store is already bound for this "
                        "asset database";
        }
        return false;
    }
    fontArtifacts_ = std::move(store);
    if (errorOut) errorOut->clear();
    return true;
}

const FontArtifactStore* AssetDatabase::FontArtifacts() const noexcept {
    return fontArtifacts_.get();
}

std::string AssetDatabase::NormalizeRel(const std::filesystem::path& rel) {
    std::string s = rel.generic_string();  // 슬래시 정규화
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string AssetDatabase::ImporterForExtension(const std::string& ext, int& versionOut) {
    if (const IImporter* importer = ImporterRegistry::Get().FindForExtension(ext)) {
        versionOut = importer->Version();
        return importer->Name();
    }
    versionOut = 1;
    return "GenericImporter";
}

void AssetDatabase::IndexOne(const std::filesystem::path& absPath) {
    std::string ext = absPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".meta") return;  // sidecar 자체는 애셋이 아니다

    const bool hadSidecar = std::filesystem::exists(AssetMeta::MetaPathFor(absPath));
    int version = 1;
    std::string importer = ImporterForExtension(ext, version);
    AssetMeta meta = AssetMeta::CreateOrLoad(absPath, importer, version);

    // Migrate sidecars created before a built-in importer existed (notably
    // .ttf/.otf files that used to be GenericImporter).
    bool metaChanged = false;
    if (importer != "GenericImporter" && meta.importer == "GenericImporter") {
        meta.importer = importer;
        meta.importerVersion = version;
        metaChanged = true;
    } else if (meta.importer == importer && meta.importerVersion < version) {
        // Upgrade old built-in sidecars, but never downgrade a sidecar written
        // by a newer editor or replace an explicitly selected custom importer.
        meta.importerVersion = version;
        metaChanged = true;
    }
    if (meta.importer == "TextureImporter") {
        // Existing sidecars without a colour-space field keep their historical
        // linear appearance; brand-new assets receive the P1 SRGB defaults.
        const TextureImportSettings settings = DeserializeTextureImportSettings(
            meta.settings, hadSidecar);
        const nlohmann::json serialized = SerializeTextureImportSettings(settings);
        if (serialized != meta.settings) {
            meta.settings = serialized;
            metaChanged = true;
        }
    }
    if (meta.importer == "AudioImporter" && meta.settings.empty()) {
        meta.settings = {{"loadMode", "DecodeOnLoad"}};
        metaChanged = true;
    }
    if (metaChanged) {
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
    rec.settings = meta.settings;
    rec.hash = ComputeFileHash(absPath);

    ImportResult res = RunImporter(meta.importer, absPath.string(), meta.settings);

    // Step 7e: 성공한 폰트 import는 카탈로그 record보다 먼저 검증된 불변
    // 산출물을 확보한다. 산출물 확보에 실패하면 record도 발행하지 않고
    // contentGeneration도 올리지 않으므로, 직전 세대가 그대로 권한으로 남는다.
    std::optional<VerifiedFontArtifact> artifact;
    if (res.success && rec.importer == "FontImporter" &&
        res.metadata.contains("font")) {
        molga::text::VectorTextDiagnosticSink sink;
        if (fontArtifacts_) {
            artifact = fontArtifacts_->Publish(
                absPath, res.metadata["font"].value("sourceSha256",
                                                    std::string{}), sink);
        }
        for (const molga::text::TextDiagnostic& diagnostic :
             sink.Diagnostics()) {
            res.importDiagnostics.push_back(diagnostic);
        }
        if (!artifact) {
            res.success = false;
            if (res.error.empty()) {
                res.error = "could not publish the immutable font artifact";
            }
        } else {
            res.metadata["font"]["contentRevision"] = contentGeneration_ + 1U;
        }
    }

    ApplyImportResultToRecord(res, rec);
    rec.fontArtifact = artifact;
    if (artifact) ++contentGeneration_;

    const auto duplicate = byGuid_.find(rec.guid);
    if (duplicate != byGuid_.end() && duplicate->second.sourcePath != rec.sourcePath) {
        rec.importFailed = true;
        rec.importError = "duplicate asset guid also used by " + duplicate->second.sourcePath;
        duplicate->second.importFailed = true;
        duplicate->second.importError = "duplicate asset guid also used by " + rec.sourcePath;
    }
    sourceToGuid_[rec.sourcePath] = rec.guid;
    byGuid_[rec.guid] = std::move(rec);
}

void AssetDatabase::ScanProject(const std::filesystem::path& assetRoot) {
    if (this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateAllFonts();
    }
    assetRoot_ = assetRoot;
    catalogPackageRoot_ = false;
    byGuid_.clear();
    sourceToGuid_.clear();
    // 스캔은 프로젝트 전체를 다시 세우므로 content generation도 이 스캔 안에서만
    // 의미가 있다. 프로세스 수명 동안 계속 올라가면 같은 프로젝트를 두 번
    // 스캔하기만 해도 asset_catalog.json이 달라지고 — GameBuilder는 빌드마다
    // 두 번 스캔한다 — 재현 가능한 빌드가 성립하지 않는다. 정체성은 이
    // 카운터가 아니라 artifactSha256가 진다.
    contentGeneration_ = 0U;
    if (!fontArtifacts_) {
        // 바인딩 없이 스캔하면 폰트 애셋이 어떤 바이트 권한에 게시되어야
        // 하는지 알 수 없다. 조용히 권한 없는 record를 만드느니 스캔 자체를
        // 거절한다.
        Log::Error("AssetDatabase",
                   "scan refused: bind a font artifact store with "
                   "BindFontArtifactStore(FontArtifactStore::ForProject(root)) "
                   "before ScanProject");
        return;
    }
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
    if (!catalogPackageRoot_ && assetRoot_.filename() == "Assets") {
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
    
    if (catalogPackageRoot_) {
        return assetRoot_ / rec->sourcePath;
    }
    if (assetRoot_.filename() == "Assets") {
        return assetRoot_.parent_path() / rec->sourcePath;
    }

    std::filesystem::path relative = rec->sourcePath;
    auto first = relative.begin();
    if (first != relative.end() && *first == "Assets") {
        relative = relative.lexically_relative("Assets");
    }
    return assetRoot_ / relative;
}

void AssetDatabase::Reimport(const std::string& guid) {
    TryReimport(guid, nullptr);
}

bool AssetDatabase::TryReimport(const std::string& guid, std::string* errorOut) {
    const AssetRecord* current = Find(guid);
    if (!current) {
        if (errorOut) *errorOut = "unknown asset guid: " + guid;
        return false;
    }
    const AssetRecord previous = *current;
    const std::filesystem::path source = AbsoluteSourcePath(guid);
    const AssetMeta meta = AssetMeta::CreateOrLoad(
        source, previous.importer, previous.importerVersion);
    ImportResult result = RunImporter(meta.importer, source.string(), meta.settings);
    if (!result.success) {
        AssetRecord& failed = byGuid_[guid];
        failed.importFailed = true;
        failed.importError = result.error;
        // metadata/artifact는 마지막 성공 상태를 유지한다. 실패에서 바뀌는
        // 것은 진단 상태뿐이므로 기계 판독 진단도 함께 갱신한다.
        failed.importDiagnostics = result.importDiagnostics;
        if (failed.importDiagnostics.size() > kMaxImportDiagnosticsPerRecord) {
            failed.importDiagnostics.resize(kMaxImportDiagnosticsPerRecord);
        }
        if (errorOut) *errorOut = result.error;
        // Runtime consumers continue using the last successfully uploaded
        // Texture object. Only the diagnostic state changes on failure.
        return false;
    }

    IndexOne(source);
    AssetRecord* refreshed = const_cast<AssetRecord*>(Find(guid));
    if (!refreshed || refreshed->importFailed) {
        byGuid_[guid] = previous;
        if (errorOut) *errorOut = refreshed ? refreshed->importError
                                            : "reimport changed asset guid";
        return false;
    }

    if (previous.importer == "TextureImporter" && this == &AssetDatabase::Get()) {
        const TextureImportSettings settings =
            DeserializeTextureImportSettings(refreshed->settings, true);
        std::string reloadError;
        if (!TextureManager::Get().Reload(source.string(), settings, &reloadError)) {
            // Reload returns true when the texture was not resident. A false
            // result means a resident last-good texture could not be replaced.
            refreshed->importFailed = true;
            refreshed->importError = reloadError;
            if (errorOut) *errorOut = reloadError;
            return false;
        }
    }
    if (previous.importer == "FontImporter" && this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateFont(guid);
    }
    if (errorOut) errorOut->clear();
    return true;
}

AssetMeta AssetDatabase::MetaForGuid(const std::string& guid) const {
    const AssetRecord* record = Find(guid);
    if (!record) return {};
    const std::filesystem::path source = AbsoluteSourcePath(guid);
    return AssetMeta::CreateOrLoad(source, record->importer, record->importerVersion);
}

bool AssetDatabase::WriteMeta(const std::string& guid, const AssetMeta& meta,
                              bool reimport, std::string* errorOut) {
    const std::filesystem::path source = AbsoluteSourcePath(guid);
    if (source.empty()) {
        if (errorOut) *errorOut = "unknown asset guid: " + guid;
        return false;
    }
    if (meta.guid != guid) {
        if (errorOut) *errorOut = "asset meta guid cannot be changed";
        return false;
    }
    if (!AssetMeta::Write(source, meta)) {
        if (errorOut) *errorOut = "could not atomically write asset meta";
        return false;
    }
    if (reimport) return TryReimport(guid, errorOut);
    if (errorOut) errorOut->clear();
    return true;
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

bool AssetDatabase::SaveCatalog(
    const std::filesystem::path& path,
    const std::string& excludedSourcePrefix) const {
    try {
        nlohmann::json j;
        j["schemaVersion"] = 2;
        j["assetRootMode"] = "packageRoot";
        
        std::vector<const AssetRecord*> ordered;
        ordered.reserve(byGuid_.size());
        for (const auto& [guid, rec] : byGuid_) {
            (void)guid;
            ordered.push_back(&rec);
        }
        std::sort(ordered.begin(), ordered.end(), [](const AssetRecord* lhs,
                                                     const AssetRecord* rhs) {
            return lhs->sourcePath < rhs->sourcePath;
        });

        nlohmann::json recordsJson = nlohmann::json::array();
        for (const AssetRecord* record : ordered) {
            const AssetRecord& rec = *record;
            if (!excludedSourcePrefix.empty() &&
                rec.sourcePath.compare(0, excludedSourcePrefix.size(),
                                       excludedSourcePrefix) == 0) {
                continue;
            }
            // 해시만 저장 시점 기준으로 갱신하고, 나머지 정규 표현은
            // LoadCatalog이 읽는 바로 그 함수를 통과시킨다.
            AssetRecord refreshed = rec;
            refreshed.hash = ComputeFileHash(AbsoluteSourcePath(rec.guid));
            recordsJson.push_back(AssetRecordToJson(refreshed));
        }
        j["records"] = recordsJson;
        return PersistentStorage::AtomicWriteText(path, j.dump(2));
    } catch (...) {
        return false;
    }
}

bool AssetDatabase::LoadCatalog(const std::filesystem::path& catalogPath,
                               const std::filesystem::path& storageRoot,
                               AssetCatalogMode mode,
                               std::string* errorOut) {
    const auto reject = [&](std::string message) {
        Clear();
        assetRoot_ = storageRoot;
        catalogPackageRoot_ = true;
        if (errorOut) *errorOut = std::move(message);
        return false;
    };

    Clear();
    assetRoot_ = storageRoot;
    catalogPackageRoot_ = true;
    if (errorOut) errorOut->clear();

    if (!fontArtifacts_) {
        return reject("no font artifact store is bound for this asset database");
    }
    const FontArtifactStorage required =
        mode == AssetCatalogMode::Project
            ? FontArtifactStorage::ProjectLibrary
            : FontArtifactStorage::PackagedResource;
    if (fontArtifacts_->Storage() != required) {
        // 카탈로그의 신뢰 경계와 바인딩된 바이트 권한이 다르면, 어느 쪽 폰트
        // locator를 신뢰해야 하는지에 대해 두 답이 생긴다.
        return reject("the bound font artifact store does not match the "
                      "requested catalog mode");
    }
    if (!std::filesystem::exists(catalogPath)) {
        return reject("asset catalog does not exist: " + catalogPath.string());
    }

    try {
        std::ifstream file(catalogPath);
        if (!file.is_open()) {
            return reject("could not open the asset catalog: " +
                          catalogPath.string());
        }
        nlohmann::json j;
        file >> j;

        const int schemaVersion = j.value("schemaVersion", 1);
        if (schemaVersion < 1 || schemaVersion > 2 ||
            !j.contains("records") || !j["records"].is_array()) {
            return reject("unsupported asset catalog schema");
        }
        for (const auto& r : j["records"]) {
            std::string recordError;
            auto rec = AssetRecordFromJson(r, recordError, mode);
            if (!rec || sourceToGuid_.count(rec->sourcePath) != 0 ||
                byGuid_.count(rec->guid) != 0) {
                return reject(rec ? "duplicate asset record: " + rec->sourcePath
                                  : recordError);
            }
            sourceToGuid_[rec->sourcePath] = rec->guid;
            byGuid_[rec->guid] = std::move(*rec);
        }
        return true;
    } catch (const std::exception& error) {
        return reject(std::string("could not read the asset catalog: ") +
                      error.what());
    } catch (...) {
        return reject("could not read the asset catalog");
    }
}

void AssetDatabase::Clear() {
    if (this == &AssetDatabase::Get()) {
        TextRenderer::Get().InvalidateAllFonts();
    }
    byGuid_.clear();
    sourceToGuid_.clear();
    catalogPackageRoot_ = false;
}

std::filesystem::path AssetDatabase::MissingTexturePath() {
    auto runtimePath = PathService::Get().AssetRoot() / "Resources/missing_texture.png";
    if (std::filesystem::exists(runtimePath)) {
        return runtimePath;
    }
    return PathService::Get().EngineResource("Editor/missing_texture.png");
}

ImportResult AssetDatabase::RunImporter(const std::string& importer, const std::string& abs,
                                        const nlohmann::json& settings) {
    return ImporterRegistry::Get().Import(importer, abs, settings);
}

} // namespace molga
