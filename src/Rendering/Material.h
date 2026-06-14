#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "../Common/Types.h"

class Texture;
class Renderer;
class Shader;

enum class BlendMode {
    Opaque,
    Alpha,
    Additive,
    Multiply
};

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
    Texture* mainTexture = nullptr;
    BlendMode blendMode = BlendMode::Alpha;
    std::unordered_map<std::string, MaterialProperty> properties;

    void Apply(Renderer* renderer);
    void ResolveAssets();

    void Serialize(nlohmann::json& j) const;
    void Deserialize(const nlohmann::json& j);
};
