#pragma once

#include "Rendering/ShaderBundle.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

// Immutable engine-facing view of one validated shader bundle entry. Native
// shader objects are short-lived inputs to cached GraphicsDevice pipelines.
class Shader {
public:
    const std::string& Name() const { return entry_.name; }
    std::uint64_t Revision() const { return entry_.revision; }
    std::uint32_t VertexStride() const { return entry_.vertexStride; }
    std::uint32_t ParameterBlockSize() const { return entry_.parameterBlockSize; }
    const std::vector<molga::ShaderParameterRecord>& Parameters() const {
        return entry_.parameters;
    }
    const molga::ShaderBundleEntry& BundleEntry() const { return entry_; }
    const std::filesystem::path& BundleRoot() const { return bundleRoot_; }
    bool IsValid() const {
        return !entry_.name.empty() && entry_.revision != 0U &&
               !bundleRoot_.empty();
    }

private:
    Shader(molga::ShaderBundleEntry entry, std::filesystem::path bundleRoot)
        : entry_(std::move(entry)), bundleRoot_(std::move(bundleRoot)) {}
    void Replace(molga::ShaderBundleEntry entry,
                 const std::filesystem::path& bundleRoot) {
        entry_ = std::move(entry);
        bundleRoot_ = bundleRoot;
    }

    molga::ShaderBundleEntry entry_;
    std::filesystem::path bundleRoot_;

    friend class ShaderManager;
};
