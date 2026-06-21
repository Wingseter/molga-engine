#include "Material.h"
#include "Renderer.h"
#include "Shader.h"
#include "ShaderManager.h"
#include "Texture.h"
#include "../Core/TextureManager.h"
#include "../Core/PathService.h"
#include "Core/AssetDatabase.h"
#include <glad/glad.h>
#include <iostream>

using json = nlohmann::json;

void Material::Apply(Renderer* renderer) {
    Shader* shader = ShaderManager::Get().Get(shaderName);
    if (!shader) {
        shader = ShaderManager::Get().Get("default");
    }
    if (!shader) return;

    renderer->SetShader(shader);

    // Set 'uColor' uniform
    shader->SetVec4("uColor", tint.r, tint.g, tint.b, tint.a);

    // Bind mainTexture to slot 0 if not null
    if (mainTexture) {
        shader->SetBool("useTexture", true);
        shader->SetInt("uTexture", 0);
        mainTexture->Bind(0);
    }

    // Apply GL blending
    switch (blendMode) {
        case BlendMode::Opaque:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Multiply:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
    }

    // Upload custom properties
    int textureSlot = 1;
    for (const auto& [name, prop] : properties) {
        if (prop.type == MaterialProperty::Type::Float) {
            shader->SetFloat(name.c_str(), prop.floatVal);
        } else if (prop.type == MaterialProperty::Type::Vec4) {
            shader->SetVec4(name.c_str(), prop.vec4Val.x, prop.vec4Val.y, prop.vec4Val.z, prop.vec4Val.w);
        } else if (prop.type == MaterialProperty::Type::Texture) {
            if (prop.texture) {
                prop.texture->Bind(textureSlot);
                shader->SetInt(name.c_str(), textureSlot);
                textureSlot++;
            } else {
                shader->SetInt(name.c_str(), 0);
            }
        }
    }
}

void Material::ResolveAssets() {
    if (!mainTexture) {
        std::filesystem::path src;
        if (!mainTextureGuid.empty()) {
            src = molga::AssetDatabase::Get().AbsoluteSourcePath(mainTextureGuid);
        }
        if (src.empty() && !mainTexturePath.empty()) {
            src = PathService::Get().ResolveAsset(mainTexturePath);  // guid 미해석 시 폴백
        }
        if (!src.empty()) {
            mainTexture = TextureManager::Get().Load(src.string());
        }
        if (!mainTexture) {
            mainTexture = TextureManager::Get().Load(
                molga::AssetDatabase::MissingTexturePath().string());
        }
    }
    for (auto& [name, prop] : properties) {
        if (prop.type == MaterialProperty::Type::Texture && !prop.texture) {
            if (!prop.texturePath.empty()) {
                std::string abs = PathService::Get().ResolveAsset(prop.texturePath);
                prop.texture = TextureManager::Get().Load(abs);
            }
        }
    }
}

void Material::Serialize(nlohmann::json& j) const {
    j["shaderName"] = shaderName;
    j["tint"] = { tint.r, tint.g, tint.b, tint.a };
    j["mainTexturePath"] = mainTexturePath;
    j["mainTextureGuid"] = mainTextureGuid;   // 권위값. mainTexturePath는 하위 호환용으로 함께 보존.
    j["blendMode"] = static_cast<int>(blendMode);

    json propsJson = json::object();
    for (const auto& [name, prop] : properties) {
        json p;
        p["type"] = static_cast<int>(prop.type);
        if (prop.type == MaterialProperty::Type::Float) {
            p["floatVal"] = prop.floatVal;
        } else if (prop.type == MaterialProperty::Type::Vec4) {
            p["vec4Val"] = { prop.vec4Val.x, prop.vec4Val.y, prop.vec4Val.z, prop.vec4Val.w };
        } else if (prop.type == MaterialProperty::Type::Texture) {
            p["texturePath"] = prop.texturePath;
        }
        propsJson[name] = p;
    }
    j["properties"] = propsJson;
}

void Material::Deserialize(const nlohmann::json& j) {
    if (j.contains("shaderName")) shaderName = j["shaderName"];
    if (j.contains("tint") && j["tint"].is_array()) {
        tint = Color(j["tint"][0], j["tint"][1], j["tint"][2], j["tint"][3]);
    }
    if (j.contains("mainTextureGuid") && j["mainTextureGuid"].is_string()) {
        mainTextureGuid = j["mainTextureGuid"].get<std::string>();
    }
    if (j.contains("mainTexturePath")) {
        mainTexturePath = j["mainTexturePath"].get<std::string>();
    }
    // 구버전 마이그레이션: guid가 없고 path만 있으면 path를 guid로 승격(메모리에서만).
    if (mainTextureGuid.empty() && !mainTexturePath.empty()) {
        std::string g = molga::AssetDatabase::Get().GuidForSource(mainTexturePath);
        if (!g.empty()) mainTextureGuid = g;
    }
    if (j.contains("blendMode")) {
        blendMode = static_cast<BlendMode>(j["blendMode"].get<int>());
    }

    properties.clear();
    if (j.contains("properties") && j["properties"].is_object()) {
        for (auto it = j["properties"].begin(); it != j["properties"].end(); ++it) {
            std::string name = it.key();
            const json& p = it.value();
            MaterialProperty prop;
            if (p.contains("type")) {
                prop.type = static_cast<MaterialProperty::Type>(p["type"].get<int>());
            }
            if (prop.type == MaterialProperty::Type::Float && p.contains("floatVal")) {
                prop.floatVal = p["floatVal"].get<float>();
            } else if (prop.type == MaterialProperty::Type::Vec4 && p.contains("vec4Val") && p["vec4Val"].is_array()) {
                prop.vec4Val = Vector4(p["vec4Val"][0], p["vec4Val"][1], p["vec4Val"][2], p["vec4Val"][3]);
            } else if (prop.type == MaterialProperty::Type::Texture && p.contains("texturePath")) {
                prop.texturePath = p["texturePath"].get<std::string>();
            }
            properties[name] = prop;
        }
    }
}
