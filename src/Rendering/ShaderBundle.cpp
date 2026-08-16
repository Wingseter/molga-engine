#include "Rendering/ShaderBundle.h"

#include "Common/Sha256.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace molga {
namespace {

using Json = nlohmann::json;

bool IsSha256(const std::string& value) {
    return value.size() == 64U && std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character) ||
                   (character >= static_cast<unsigned char>('a') &&
                    character <= static_cast<unsigned char>('f'));
        });
}

std::string FoldCase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool IsSafeRelativePath(const std::string& stored) {
    if (stored.empty()) return false;
    const std::filesystem::path path(stored);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path.lexically_normal()) {
        if (component == "..") return false;
    }
    return true;
}

bool ReadStage(const Json& json, ShaderStageRecord& output,
               const std::string& expectedStage, bool mslOnly,
               std::string& errorOut) {
    if (!json.is_object() || json.value("stage", "") != expectedStage ||
        !json.contains("entryPoint") || !json["entryPoint"].is_string() ||
        !json.contains("resources") || !json["resources"].is_object() ||
        !json.contains("artifacts") || !json["artifacts"].is_object()) {
        errorOut = "invalid " + expectedStage + " stage record";
        return false;
    }
    output.stage = expectedStage;
    output.sourceEntryPoint = json["entryPoint"].get<std::string>();
    const Json& resources = json["resources"];
    for (const char* field : {"samplers", "storageTextures", "storageBuffers",
                              "uniformBuffers"}) {
        if (!resources.contains(field) || !resources[field].is_number_unsigned()) {
            errorOut = "invalid resource count '" + std::string(field) + "'";
            return false;
        }
    }
    output.resources.samplers = resources["samplers"].get<std::uint32_t>();
    output.resources.storageTextures =
        resources["storageTextures"].get<std::uint32_t>();
    output.resources.storageBuffers =
        resources["storageBuffers"].get<std::uint32_t>();
    output.resources.uniformBuffers =
        resources["uniformBuffers"].get<std::uint32_t>();

    for (auto iterator = json["artifacts"].begin();
         iterator != json["artifacts"].end(); ++iterator) {
        const Json& artifact = iterator.value();
        if (!artifact.is_object() ||
            !artifact.contains("path") || !artifact["path"].is_string() ||
            !artifact.contains("sha256") || !artifact["sha256"].is_string() ||
            !artifact.contains("entryPoint") ||
            !artifact["entryPoint"].is_string()) {
            errorOut = "invalid artifact record for " + iterator.key();
            return false;
        }
        ShaderArtifactRecord record;
        record.path = artifact["path"].get<std::string>();
        record.sha256 = artifact["sha256"].get<std::string>();
        record.entryPoint = artifact["entryPoint"].get<std::string>();
        if (!IsSafeRelativePath(record.path) || !IsSha256(record.sha256) ||
            record.entryPoint.empty()) {
            errorOut = "unsafe or incomplete artifact record for " + iterator.key();
            return false;
        }
        output.artifacts.emplace(iterator.key(), std::move(record));
    }
    const std::vector<const char*> required = mslOnly
        ? std::vector<const char*>{"msl"}
        : std::vector<const char*>{"spirv", "msl", "dxil"};
    for (const char* format : required) {
        if (output.artifacts.find(format) == output.artifacts.end()) {
            errorOut = "missing " + std::string(format) + " artifact";
            return false;
        }
    }
    return true;
}

} // namespace

const char* ShaderArtifactFormatName(ShaderArtifactFormat format) {
    switch (format) {
        case ShaderArtifactFormat::Spirv: return "spirv";
        case ShaderArtifactFormat::Msl: return "msl";
        case ShaderArtifactFormat::Dxil: return "dxil";
    }
    return "unknown";
}

bool ShaderBundleManifest::Load(const std::filesystem::path& manifestPath,
                                ShaderBundleManifest& output,
                                std::string& errorOut) {
    std::ifstream input(manifestPath);
    if (!input) {
        errorOut = "could not open shader manifest: " + manifestPath.string();
        return false;
    }
    Json root;
    try {
        input >> root;
    } catch (const std::exception& exception) {
        errorOut = "could not parse shader manifest: " +
                   std::string(exception.what());
        return false;
    }
    if (!root.is_object() || root.value("schemaVersion", 0) != 1 ||
        !root.contains("tool") || !root["tool"].is_object() ||
        !root["tool"].contains("revision") ||
        !root["tool"]["revision"].is_string() ||
        !root.contains("entries") || !root["entries"].is_array()) {
        errorOut = "shader manifest does not match schema v1";
        return false;
    }

    ShaderBundleManifest candidate;
    candidate.schemaVersion_ = 1;
    candidate.toolRevision_ = root["tool"]["revision"].get<std::string>();
    candidate.manifestSha256_ = Sha256File(manifestPath, &errorOut);
    if (candidate.manifestSha256_.empty()) return false;
    bool mslOnly = false;
    if (root.contains("artifactFormats")) {
        if (!root["artifactFormats"].is_array() ||
            root["artifactFormats"].empty()) {
            errorOut = "shader artifactFormats must be a non-empty array";
            return false;
        }
        std::set<std::string> formats;
        for (const Json& format : root["artifactFormats"]) {
            if (!format.is_string() ||
                !formats.insert(format.get<std::string>()).second) {
                errorOut = "shader artifactFormats are invalid or duplicated";
                return false;
            }
        }
        if (formats.find("msl") == formats.end() ||
            std::any_of(formats.begin(), formats.end(),
                        [](const std::string& format) {
                            return format != "spirv" && format != "msl" &&
                                   format != "dxil";
                        })) {
            errorOut = "shader artifactFormats contain an unsupported format";
            return false;
        }
        mslOnly = formats.size() == 1U;
    }

    std::set<std::string> names;
    for (const Json& json : root["entries"]) {
        if (!json.is_object() || !json.contains("name") ||
            !json["name"].is_string() || !json.contains("source") ||
            !json["source"].is_string() ||
            !json.contains("sourceSha256") || !json["sourceSha256"].is_string() ||
            !json.contains("descriptorSha256") ||
            !json["descriptorSha256"].is_string() ||
            !json.contains("bindingsSha256") ||
            !json["bindingsSha256"].is_string() ||
            !json.contains("revision") || !json["revision"].is_number_unsigned() ||
            !json.contains("vertex") || !json.contains("fragment")) {
            errorOut = "invalid shader entry in manifest";
            return false;
        }
        ShaderBundleEntry entry;
        entry.name = json["name"].get<std::string>();
        entry.source = json["source"].get<std::string>();
        entry.sourceSha256 = json["sourceSha256"].get<std::string>();
        entry.descriptorSha256 = json["descriptorSha256"].get<std::string>();
        entry.bindingsSha256 = json["bindingsSha256"].get<std::string>();
        entry.revision = json["revision"].get<std::uint64_t>();
        if (entry.name.empty() || !IsSafeRelativePath(entry.source) ||
            !IsSha256(entry.sourceSha256) || !IsSha256(entry.descriptorSha256) ||
            !IsSha256(entry.bindingsSha256) ||
            !names.insert(FoldCase(entry.name)).second) {
            errorOut = "duplicate, case-colliding, or invalid shader entry: " +
                       entry.name;
            return false;
        }
        if (!json.contains("vertexLayout") ||
            !json["vertexLayout"].is_object() ||
            !json["vertexLayout"].contains("stride") ||
            !json["vertexLayout"]["stride"].is_number_unsigned() ||
            !json["vertexLayout"].contains("attributes") ||
            !json["vertexLayout"]["attributes"].is_array()) {
            errorOut = "missing vertex layout for shader: " + entry.name;
            return false;
        }
        entry.vertexStride =
            json["vertexLayout"]["stride"].get<std::uint32_t>();
        std::set<std::uint32_t> locations;
        for (const Json& attribute : json["vertexLayout"]["attributes"]) {
            if (!attribute.is_object() ||
                !attribute.contains("location") ||
                !attribute["location"].is_number_unsigned() ||
                !attribute.contains("format") ||
                !attribute["format"].is_string() ||
                !attribute.contains("offset") ||
                !attribute["offset"].is_number_unsigned()) {
                errorOut = "invalid vertex attribute for shader: " + entry.name;
                return false;
            }
            ShaderVertexAttributeRecord record;
            record.location = attribute["location"].get<std::uint32_t>();
            record.format = attribute["format"].get<std::string>();
            record.offset = attribute["offset"].get<std::uint32_t>();
            if (!locations.insert(record.location).second ||
                record.offset >= entry.vertexStride) {
                errorOut = "duplicate or out-of-range vertex attribute for shader: " +
                           entry.name;
                return false;
            }
            entry.vertexAttributes.push_back(std::move(record));
        }
        if (entry.vertexStride == 0U || entry.vertexAttributes.empty()) {
            errorOut = "empty vertex layout for shader: " + entry.name;
            return false;
        }
        entry.parameterBlockSize = json.value("parameterBlockSize", 0U);
        if ((entry.parameterBlockSize % 16U) != 0U) {
            errorOut = "shader parameter block is not 16-byte aligned: " + entry.name;
            return false;
        }
        if (json.contains("parameters")) {
            if (!json["parameters"].is_array()) {
                errorOut = "shader parameters must be an array: " + entry.name;
                return false;
            }
            std::uint32_t previousEnd = 0;
            for (const Json& parameter : json["parameters"]) {
                ShaderParameterRecord record;
                if (!parameter.is_object() ||
                    !parameter.contains("name") || !parameter["name"].is_string() ||
                    !parameter.contains("type") || !parameter["type"].is_string() ||
                    !parameter.contains("offset") ||
                    !parameter["offset"].is_number_unsigned() ||
                    !parameter.contains("size") ||
                    !parameter["size"].is_number_unsigned()) {
                    errorOut = "invalid shader parameter: " + entry.name;
                    return false;
                }
                record.name = parameter["name"].get<std::string>();
                record.type = parameter["type"].get<std::string>();
                record.offset = parameter["offset"].get<std::uint32_t>();
                record.size = parameter["size"].get<std::uint32_t>();
                if ((record.type != "Float" && record.type != "Vec4") ||
                    (record.offset % 16U) != 0U || record.offset < previousEnd ||
                    (record.size != 4U && record.size != 16U) ||
                    record.offset + record.size > entry.parameterBlockSize) {
                    errorOut = "invalid parameter layout for shader: " + entry.name;
                    return false;
                }
                previousEnd = record.offset + record.size;
                entry.parameters.push_back(std::move(record));
            }
        }
        if (!json.contains("textures") || !json["textures"].is_array()) {
            errorOut = "shader textures must be an array: " + entry.name;
            return false;
        }
        std::set<std::pair<std::string, std::uint32_t>> textureSlots;
        std::set<std::pair<std::string, std::string>> textureNames;
        for (const Json& texture : json["textures"]) {
            if (!texture.is_object() || !texture.contains("name") ||
                !texture["name"].is_string() || !texture.contains("stage") ||
                !texture["stage"].is_string() ||
                !texture.contains("dimension") ||
                !texture["dimension"].is_string() ||
                !texture.contains("slot") ||
                !texture["slot"].is_number_unsigned()) {
                errorOut = "invalid texture binding for shader: " + entry.name;
                return false;
            }
            ShaderTextureBindingRecord record;
            record.name = texture["name"].get<std::string>();
            record.stage = texture["stage"].get<std::string>();
            record.dimension = texture["dimension"].get<std::string>();
            record.slot = texture["slot"].get<std::uint32_t>();
            if ((record.stage != "vertex" && record.stage != "fragment") ||
                (record.dimension != "2D" && record.dimension != "2DArray") ||
                record.name.empty() ||
                !textureSlots.insert({record.stage, record.slot}).second ||
                !textureNames.insert({record.stage, FoldCase(record.name)}).second) {
                errorOut = "duplicate or invalid texture binding for shader: " +
                           entry.name;
                return false;
            }
            entry.textures.push_back(std::move(record));
        }
        if (!ReadStage(json["vertex"], entry.vertex, "vertex", mslOnly,
                       errorOut) ||
            !ReadStage(json["fragment"], entry.fragment, "fragment", mslOnly,
                       errorOut)) {
            errorOut = entry.name + ": " + errorOut;
            return false;
        }
        candidate.entries_.push_back(std::move(entry));
    }
    if (candidate.entries_.empty()) {
        errorOut = "shader manifest contains no entries";
        return false;
    }
    output = std::move(candidate);
    errorOut.clear();
    return true;
}

bool ShaderBundleManifest::Validate(const std::filesystem::path& bundleRoot,
                                    bool verifyAllArtifacts,
                                    std::string& errorOut) const {
    std::set<std::string> paths;
    for (const ShaderBundleEntry& entry : entries_) {
        for (const ShaderStageRecord* stage : {&entry.vertex, &entry.fragment}) {
            for (const auto& [format, artifact] : stage->artifacts) {
                if (!verifyAllArtifacts && format != "msl") continue;
                if (!paths.insert(FoldCase(artifact.path)).second) {
                    errorOut = "duplicate or case-colliding shader artifact: " +
                               artifact.path;
                    return false;
                }
                const std::filesystem::path fullPath = bundleRoot / artifact.path;
                std::error_code filesystemError;
                if (!std::filesystem::is_regular_file(fullPath, filesystemError)) {
                    errorOut = "missing shader artifact: " + fullPath.string();
                    return false;
                }
                const std::string digest = Sha256File(fullPath, &errorOut);
                if (digest.empty()) return false;
                if (digest != artifact.sha256) {
                    errorOut = "shader artifact SHA-256 mismatch: " +
                               artifact.path;
                    return false;
                }
            }
        }
    }
    errorOut.clear();
    return true;
}

const ShaderBundleEntry* ShaderBundleManifest::Find(
    const std::string& name) const {
    const auto iterator = std::find_if(
        entries_.begin(), entries_.end(), [&](const ShaderBundleEntry& entry) {
            return entry.name == name;
        });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

} // namespace molga
