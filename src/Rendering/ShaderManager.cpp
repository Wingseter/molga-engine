#include "ShaderManager.h"
#include <iostream>

Shader* ShaderManager::Load(const std::string& name, const std::string& vertPath, const std::string& fragPath) {
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        return it->second.get();
    }
    
    auto shader = std::make_unique<Shader>(vertPath.c_str(), fragPath.c_str());
    Shader* rawPtr = shader.get();
    shaders[name] = std::move(shader);
    return rawPtr;
}

Shader* ShaderManager::Get(const std::string& name) {
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ShaderManager::ReloadAll() {
    for (auto& pair : shaders) {
        if (pair.second) {
            std::cout << "[ShaderManager] Reloading shader: " << pair.first << std::endl;
            if (!pair.second->Reload()) {
                std::cerr << "[ShaderManager] Failed to reload shader: " << pair.first << std::endl;
            }
        }
    }
}

void ShaderManager::Shutdown() {
    shaders.clear();
}
