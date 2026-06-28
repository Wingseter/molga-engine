#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "../Common/Types.h"
#include "Rendering/BlendMode.h"
#include "Rendering/RenderQueue.h"

class Texture;
class Renderer;
class Shader;

struct MaterialProperty {
    enum class Type { Float, Vec4, Texture };
    Type type = Type::Float;
    float floatVal = 0.0f;
    Vector4 vec4Val;
    std::string texturePath;
    Texture* texture = nullptr;
};

class Material {
public:
    std::string shaderName = "default";
    Color tint = Color::White();
    std::string mainTexturePath;
    std::string mainTextureGuid;
    Texture* mainTexture = nullptr;
    BlendMode blendMode = BlendMode::Alpha;
    std::unordered_map<std::string, MaterialProperty> properties;

    void SetMainTextureGuid(const std::string& g) { mainTextureGuid = g; }
    const std::string& GetMainTextureGuid() const { return mainTextureGuid; }

    Shader* ResolveShader() const;
    molga::BatchKey GetBatchKey() const;
    void ApplyForBatchStart(Renderer* renderer);

    void Apply(Renderer* renderer);
    void ResolveAssets();

    void Serialize(nlohmann::json& j) const;
    void Deserialize(const nlohmann::json& j);
};
