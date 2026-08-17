#pragma once

#include "Rendering/Shader.h"
#include "Rendering/ShaderBundle.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

class ShaderManager {
public:
    static ShaderManager& Get() {
        static ShaderManager instance;
        return instance;
    }

    // Loads and verifies a complete candidate before publishing any entry.
    // Existing Shader object addresses remain stable across successful reloads.
    bool Initialize(const std::filesystem::path& bundleRoot,
                    std::string* errorOut = nullptr,
                    bool verifyAllArtifacts = true);
    bool InitializeDefault(std::string* errorOut = nullptr);
    Shader* Load(const std::string& name);
    Shader* Get(const std::string& name);
    const Shader* Get(const std::string& name) const;
    bool ReloadAll(std::string* errorOut = nullptr);
    void Shutdown();

    const std::filesystem::path& BundleRoot() const { return bundleRoot_; }
    const std::string& ManifestSha256() const { return manifestSha256_; }

private:
    ShaderManager() = default;
    ~ShaderManager() = default;
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders_;
    std::filesystem::path bundleRoot_;
    std::string manifestSha256_;
};
