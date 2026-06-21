#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Shader.h"

class ShaderManager {
public:
    static ShaderManager& Get() {
        static ShaderManager instance;
        return instance;
    }

    Shader* Load(const std::string& name, const std::string& vertPath, const std::string& fragPath);
    Shader* Get(const std::string& name);
    void ReloadAll();
    void Shutdown();

private:
    ShaderManager() = default;
    ~ShaderManager() = default;
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;
};
