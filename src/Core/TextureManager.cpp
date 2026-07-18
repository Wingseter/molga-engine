#include "TextureManager.h"
#include "../Rendering/Texture.h"
#include "PathService.h"
#ifdef MOLGA_EDITOR
#include "../Editor/Project.h"
#endif
#include "Core/Profiling/ProfilerService.h"
#include "Core/Profiling/ScopedTimer.h"
#include "Common/Log.h"
#include "Core/AssetDatabase.h"
#include "Core/TextureImportSettings.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

TextureManager& TextureManager::Get() {
    static TextureManager instance;
    return instance;
}

Texture* TextureManager::Load(const std::string& path, const char* caller) {
    molga::TextureImportSettings settings = molga::TextureImportSettings::LegacyDefaults();
    std::string guid = molga::AssetDatabase::Get().GuidForAbsolutePath(path);
    if (guid.empty()) guid = molga::AssetDatabase::Get().GuidForSource(path);
    if (const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid)) {
        settings = molga::DeserializeTextureImportSettings(record->settings, true);
    }
    return LoadWithSettings(path, settings, caller);
}

std::string TextureManager::CacheKey(const std::string& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    if (!error) {
        fs::path canonical = fs::weakly_canonical(absolute, error);
        if (!error) return canonical.generic_string();
    }
    return fs::path(path).lexically_normal().generic_string();
}

Texture* TextureManager::LoadWithSettings(const std::string& path,
                                          const molga::TextureImportSettings& settings,
                                          const char* caller) {
    if (path.empty()) {
        return nullptr;
    }

    // Resolve path (could be relative to project or runtime exe)
    std::string absolutePath = path;
    if (!fs::path(path).is_absolute()) {
#ifdef MOLGA_EDITOR
        if (Project::Get().IsOpen()) {
            absolutePath = Project::Get().GetAbsolutePath(path);
        } else {
            absolutePath = PathService::Get().ResolveAsset(path);
        }
#else
        absolutePath = PathService::Get().ResolveAsset(path);
#endif
    }

    const std::string key = CacheKey(absolutePath);
    auto it = textures.find(key);
    if (it != textures.end()) return it->second.get();

    // Check if file exists
    if (!fs::exists(absolutePath)) {
        std::cerr << "[TextureManager] File not found: " << absolutePath << std::endl;
        return nullptr;
    }

    long long t0 = molga::NowNanos();

    // Load texture
    try {
        auto texture = std::make_unique<Texture>(absolutePath.c_str(), settings);
        if (!texture->IsValid()) return nullptr;
        Texture* ptr = texture.get();
        textures[key] = std::move(texture);

        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ProfilerService::Get().AssetLoadCounter()++;

        constexpr double kSlowLoadMs = 8.0;
        if (ms > kSlowLoadMs) {
            Log::Warn("AssetLoad",
                "Slow texture load by [" + std::string(caller) + "]: " + path + " (" + std::to_string(ms) + " ms)");
        } else {
            std::cout << "[TextureManager] Loaded texture by [" << caller << "]: " << path << " (" << ms << " ms)" << std::endl;
        }
        return ptr;
    } catch (const std::exception& e) {
        std::cerr << "[TextureManager] Failed to load texture: " << path << " - " << e.what() << std::endl;
        return nullptr;
    }
}

bool TextureManager::Reload(const std::string& path,
                            const molga::TextureImportSettings& settings,
                            std::string* errorOut) {
    const std::string key = CacheKey(path);
    auto found = textures.find(key);
    if (found == textures.end()) {
        if (errorOut) errorOut->clear();
        return true;
    }
    std::string absolutePath = path;
    if (!fs::path(path).is_absolute()) absolutePath = PathService::Get().ResolveAsset(path);
    return found->second->Reload(absolutePath.c_str(), settings, errorOut);
}

Texture* TextureManager::Get(const std::string& path) {
    auto it = textures.find(CacheKey(path));
    if (it != textures.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool TextureManager::IsLoaded(const std::string& path) const {
    return textures.find(CacheKey(path)) != textures.end();
}

void TextureManager::Unload(const std::string& path) {
    auto it = textures.find(CacheKey(path));
    if (it != textures.end()) {
        textures.erase(it);
        std::cout << "[TextureManager] Unloaded texture: " << path << std::endl;
    }
}

void TextureManager::Clear() {
    textures.clear();
    std::cout << "[TextureManager] Cleared all textures" << std::endl;
}
