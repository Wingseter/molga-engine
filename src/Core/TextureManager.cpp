#include "TextureManager.h"
#include "../Texture.h"
#include "Project.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

TextureManager& TextureManager::Get() {
    static TextureManager instance;
    return instance;
}

Texture* TextureManager::Load(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }

    // Check if already loaded
    auto it = textures.find(path);
    if (it != textures.end()) {
        return it->second.get();
    }

    // Resolve path (could be relative to project)
    std::string absolutePath = path;
    if (!fs::path(path).is_absolute() && Project::Get().IsOpen()) {
        absolutePath = Project::Get().GetAbsolutePath(path);
    }

    // Check if file exists
    if (!fs::exists(absolutePath)) {
        std::cerr << "[TextureManager] File not found: " << absolutePath << std::endl;
        return nullptr;
    }

    // Load texture
    try {
        auto texture = std::make_unique<Texture>(absolutePath.c_str());
        Texture* ptr = texture.get();
        textures[path] = std::move(texture);

        std::cout << "[TextureManager] Loaded texture: " << path << std::endl;
        return ptr;
    } catch (const std::exception& e) {
        std::cerr << "[TextureManager] Failed to load texture: " << path << " - " << e.what() << std::endl;
        return nullptr;
    }
}

Texture* TextureManager::Get(const std::string& path) {
    auto it = textures.find(path);
    if (it != textures.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool TextureManager::IsLoaded(const std::string& path) const {
    return textures.find(path) != textures.end();
}

void TextureManager::Unload(const std::string& path) {
    auto it = textures.find(path);
    if (it != textures.end()) {
        textures.erase(it);
        std::cout << "[TextureManager] Unloaded texture: " << path << std::endl;
    }
}

void TextureManager::Clear() {
    textures.clear();
    std::cout << "[TextureManager] Cleared all textures" << std::endl;
}
