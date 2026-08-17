#include "Rendering/ShaderManager.h"
#include "Core/PathService.h"

#include <utility>

#ifndef MOLGA_SHADER_BUNDLE_DIR
#define MOLGA_SHADER_BUNDLE_DIR "ShaderBundle"
#endif

bool ShaderManager::Initialize(const std::filesystem::path& bundleRoot,
                               std::string* errorOut,
                               bool verifyAllArtifacts) {
    std::string error;
    molga::ShaderBundleManifest candidate;
    const std::filesystem::path manifestPath = bundleRoot / "manifest.json";
    if (!molga::ShaderBundleManifest::Load(manifestPath, candidate, error) ||
        !candidate.Validate(bundleRoot, verifyAllArtifacts, error)) {
        if (errorOut) *errorOut = error;
        return false;
    }

    // Allocate every new public object before mutating the last-good set.
    std::unordered_map<std::string, std::unique_ptr<Shader>> additions;
    for (const auto& entry : candidate.Entries()) {
        if (shaders_.find(entry.name) == shaders_.end()) {
            additions.emplace(entry.name,
                std::unique_ptr<Shader>(new Shader(entry, bundleRoot)));
        }
    }

    for (const auto& entry : candidate.Entries()) {
        const auto existing = shaders_.find(entry.name);
        if (existing != shaders_.end()) {
            existing->second->Replace(entry, bundleRoot);
        }
    }
    for (auto& [name, shader] : additions) {
        shaders_.emplace(name, std::move(shader));
    }
    bundleRoot_ = bundleRoot;
    manifestSha256_ = candidate.ManifestSha256();
    if (errorOut) errorOut->clear();
    return true;
}

bool ShaderManager::InitializeDefault(std::string* errorOut) {
    std::filesystem::path bundleRoot =
        PathService::Get().EngineResource("ShaderBundle");
    if (!std::filesystem::is_regular_file(bundleRoot / "manifest.json")) {
        bundleRoot = std::filesystem::path(MOLGA_SHADER_BUNDLE_DIR);
    }
    return Initialize(bundleRoot, errorOut,
#if defined(__APPLE__)
                      false
#else
                      true
#endif
    );
}

Shader* ShaderManager::Load(const std::string& name) { return Get(name); }

Shader* ShaderManager::Get(const std::string& name) {
    const auto found = shaders_.find(name);
    return found == shaders_.end() ? nullptr : found->second.get();
}

const Shader* ShaderManager::Get(const std::string& name) const {
    const auto found = shaders_.find(name);
    return found == shaders_.end() ? nullptr : found->second.get();
}

bool ShaderManager::ReloadAll(std::string* errorOut) {
    if (bundleRoot_.empty()) {
        if (errorOut) *errorOut = "shader bundle is not initialized";
        return false;
    }
    return Initialize(bundleRoot_, errorOut,
#if defined(__APPLE__)
                      false
#else
                      true
#endif
    );
}

void ShaderManager::Shutdown() {
    shaders_.clear();
    bundleRoot_.clear();
    manifestSha256_.clear();
}
