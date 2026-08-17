#include "Material.h"
#include "Shader.h"
#include "ShaderManager.h"
#include "Texture.h"
#include "../Core/TextureManager.h"
#include "../Core/PathService.h"
#include "Core/AssetDatabase.h"
#include <cstring>
#include <iostream>

using json = nlohmann::json;

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

Shader* Material::ResolveShader() const {
    Shader* shader = ShaderManager::Get().Get(shaderName);
    if (!shader) {
        shader = ShaderManager::Get().Get("default");
    }
    return shader;
}

molga::BatchKey Material::GetBatchKey() const {
    molga::BatchKey key;
    key.blendMode = blendMode;
    Shader* shader = nullptr;
    const bool builtInBatch = shaderName == "default" || shaderName == "batch";
    if (builtInBatch) shader = ShaderManager::Get().Get("batch");
    else shader = ResolveShader();
    if (!shader) return key;

    key.shaderName = shader->Name();
    key.shaderRevision = shader->Revision();
    key.isBatchable = builtInBatch && properties.empty();
    if (mainTexture && mainTexture->IsValid()) {
        key.texture = mainTexture->Handle();
        key.textureSampler = mainTexture->Sampler();
        key.textureStableId = mainTexture->StableId();
    }

    auto mix = [](std::uint64_t& hash, const void* bytes, std::size_t size) {
        const auto* data = static_cast<const std::uint8_t*>(bytes);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= data[index];
            hash *= 1099511628211ULL;
        }
    };
    std::uint64_t materialHash = 1469598103934665603ULL;
    mix(materialHash, &key.shaderRevision, sizeof(key.shaderRevision));
    mix(materialHash, &blendMode, sizeof(blendMode));

    if (shader->ParameterBlockSize() > 0U) {
        auto block = std::make_shared<std::vector<std::uint8_t>>(
            shader->ParameterBlockSize(), 0U);
        for (const auto& parameter : shader->Parameters()) {
            const auto found = properties.find(parameter.name);
            if (found != properties.end() &&
                parameter.type == "Float" &&
                found->second.type == MaterialProperty::Type::Float) {
                std::memcpy(block->data() + parameter.offset,
                            &found->second.floatVal, sizeof(float));
            } else if (found != properties.end() &&
                       parameter.type == "Vec4" &&
                       found->second.type == MaterialProperty::Type::Vec4) {
                const float values[4]{found->second.vec4Val.x,
                                      found->second.vec4Val.y,
                                      found->second.vec4Val.z,
                                      found->second.vec4Val.w};
                std::memcpy(block->data() + parameter.offset, values,
                            sizeof(values));
            } else if (parameter.type == "Vec4" &&
                       (parameter.name == "uColor" ||
                        parameter.name == "tint")) {
                const float values[4]{tint.r, tint.g, tint.b, tint.a};
                std::memcpy(block->data() + parameter.offset, values,
                            sizeof(values));
            }
        }
        mix(materialHash, block->data(), block->size());
        key.materialParameters = std::move(block);
    }

    if (!shader->BundleEntry().textures.empty() && !builtInBatch) {
        auto bindings = std::make_shared<
            std::vector<molga::BatchKey::ExtraTexture>>();
        bindings->reserve(shader->BundleEntry().textures.size());
        for (const auto& declared : shader->BundleEntry().textures) {
            Texture* texture = nullptr;
            if (declared.name == "uTexture") {
                texture = mainTexture;
            } else {
                const auto found = properties.find(declared.name);
                if (found != properties.end() &&
                    found->second.type == MaterialProperty::Type::Texture) {
                    texture = found->second.texture;
                }
            }
            molga::BatchKey::ExtraTexture binding;
            binding.vertexStage = declared.stage == "vertex";
            binding.slot = declared.slot;
            if (texture && texture->IsValid()) {
                binding.texture = texture->Handle();
                binding.sampler = texture->Sampler();
                binding.stableId = texture->StableId();
            }
            mix(materialHash, &binding.vertexStage,
                sizeof(binding.vertexStage));
            mix(materialHash, &binding.slot, sizeof(binding.slot));
            mix(materialHash, &binding.stableId, sizeof(binding.stableId));
            bindings->push_back(binding);
        }
        key.materialTextures = std::move(bindings);
    }
    key.materialId = materialHash;
    return key;
}
