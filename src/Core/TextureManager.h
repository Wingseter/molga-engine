#ifndef MOLGA_TEXTURE_MANAGER_H
#define MOLGA_TEXTURE_MANAGER_H

#include <string>
#include <unordered_map>
#include <memory>

class Texture;

class TextureManager {
public:
    static TextureManager& Get();

    // Load texture (cached)
    Texture* Load(const std::string& path);

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
};

#endif // MOLGA_TEXTURE_MANAGER_H
