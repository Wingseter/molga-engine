#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Core/TextureImportSettings.h"

class Texture;

class TextureManager {
public:
    static TextureManager& Get();

    // Load texture (cached)
    Texture* Load(const std::string& path, const char* caller = "Unknown");
    Texture* LoadWithSettings(const std::string& path,
                              const molga::TextureImportSettings& settings,
                              const char* caller = "Unknown");
    bool Reload(const std::string& path, const molga::TextureImportSettings& settings,
                std::string* errorOut = nullptr);

    // Get already loaded texture
    Texture* Get(const std::string& path);

    // Check if texture is loaded
    bool IsLoaded(const std::string& path) const;

    // Unload specific texture
    void Unload(const std::string& path);

    // Unload all textures
    void Clear();

    // Get texture count
    size_t GetCount() const { return textures.size(); }

private:
    TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    static std::string CacheKey(const std::string& path);
};
