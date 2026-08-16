#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

enum class ShaderArtifactFormat {
    Spirv,
    Msl,
    Dxil,
};

struct ShaderResourceCounts {
    std::uint32_t samplers = 0;
    std::uint32_t storageTextures = 0;
    std::uint32_t storageBuffers = 0;
    std::uint32_t uniformBuffers = 0;
};

struct ShaderArtifactRecord {
    std::string path;
    std::string sha256;
    std::string entryPoint;
};

struct ShaderStageRecord {
    std::string stage;
    std::string sourceEntryPoint;
    ShaderResourceCounts resources;
    std::unordered_map<std::string, ShaderArtifactRecord> artifacts;
};

struct ShaderParameterRecord {
    std::string name;
    std::string type;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct ShaderVertexAttributeRecord {
    std::uint32_t location = 0;
    std::string format;
    std::uint32_t offset = 0;
};

struct ShaderTextureBindingRecord {
    std::string name;
    std::string stage;
    std::string dimension;
    std::uint32_t slot = 0;
};

struct ShaderBundleEntry {
    std::string name;
    std::string source;
    std::string sourceSha256;
    std::string descriptorSha256;
    std::string bindingsSha256;
    std::uint64_t revision = 0;
    std::uint32_t vertexStride = 0;
    std::vector<ShaderVertexAttributeRecord> vertexAttributes;
    std::uint32_t parameterBlockSize = 0;
    std::vector<ShaderParameterRecord> parameters;
    std::vector<ShaderTextureBindingRecord> textures;
    ShaderStageRecord vertex;
    ShaderStageRecord fragment;
};

class ShaderBundleManifest {
public:
    static bool Load(const std::filesystem::path& manifestPath,
                     ShaderBundleManifest& output,
                     std::string& errorOut);

    bool Validate(const std::filesystem::path& bundleRoot,
                  bool verifyAllArtifacts,
                  std::string& errorOut) const;

    const ShaderBundleEntry* Find(const std::string& name) const;
    const std::vector<ShaderBundleEntry>& Entries() const { return entries_; }
    const std::string& ToolRevision() const { return toolRevision_; }
    const std::string& ManifestSha256() const { return manifestSha256_; }
    int SchemaVersion() const { return schemaVersion_; }

private:
    int schemaVersion_ = 0;
    std::string toolRevision_;
    std::string manifestSha256_;
    std::vector<ShaderBundleEntry> entries_;
};

const char* ShaderArtifactFormatName(ShaderArtifactFormat format);

} // namespace molga
