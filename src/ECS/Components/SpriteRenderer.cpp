#include "SpriteRenderer.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"

REGISTER_COMPONENT(SpriteRenderer)
#include "../../Rendering/Renderer.h"
#include <glad/glad.h>
#include "../../Rendering/Shader.h"
#include "../../Rendering/Camera2D.h"
#include "../../Rendering/Sprite.h"
#include "../../Rendering/Texture.h"
#include "../../Core/TextureManager.h"
#include "../../Core/PathService.h"
#include "../../Common/Log.h"
#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#endif
#include <nlohmann/json.hpp>
#include <filesystem>
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

void SpriteRenderer::RenderSprite(Renderer* renderer) {
    if (!gameObject || !enabled) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    // Apply material
    material.Apply(renderer);

    // Create a temporary sprite for rendering
    Sprite sprite;

    Vector2 worldPos = transform->GetWorldPosition();
    Vector2 worldScale = transform->GetWorldScale();
    float worldRot = transform->GetWorldRotation();

    sprite.SetPosition(worldPos.x, worldPos.y);
    sprite.SetSize(width * worldScale.x, height * worldScale.y);
    sprite.SetRotation(worldRot);
    sprite.SetColor(color.r * material.tint.r, color.g * material.tint.g, color.b * material.tint.b, color.a * material.tint.a);

    if (material.mainTexture) {
        sprite.SetTexture(material.mainTexture);
    } else if (texture) {
        sprite.SetTexture(texture);
    }

    // Apply flip
    if (flipX) {
        sprite.x += sprite.width;
        sprite.width = -sprite.width;
    }
    if (flipY) {
        sprite.y += sprite.height;
        sprite.height = -sprite.height;
    }

    renderer->DrawSprite(&sprite);

    // Restore standard alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void SpriteRenderer::Serialize(nlohmann::json& j) const {
    j["color"] = { color.r, color.g, color.b, color.a };
    j["size"] = { width, height };
    j["flipX"] = flipX;
    j["flipY"] = flipY;
    j["sortingOrder"] = sortingOrder;
    j["texturePath"] = texturePath;

    nlohmann::json matJson;
    material.Serialize(matJson);
    j["material"] = matJson;
}

void SpriteRenderer::Deserialize(const nlohmann::json& j) {
    if (j.contains("color") && j["color"].is_array()) {
        SetColor(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
    }
    if (j.contains("size") && j["size"].is_array()) {
        SetSize(j["size"][0], j["size"][1]);
    }
    if (j.contains("flipX")) {
        SetFlipX(j["flipX"]);
    }
    if (j.contains("flipY")) {
        SetFlipY(j["flipY"]);
    }
    if (j.contains("sortingOrder")) {
        SetSortingOrder(j["sortingOrder"]);
    }
    if (j.contains("texturePath")) {
        SetTexturePath(j["texturePath"]);
    }
    if (j.contains("material")) {
        material.Deserialize(j["material"]);
    }
}

void SpriteRenderer::ResolveAssets() {
    if (!texturePath.empty() && !texture) {
        std::string abs = PathService::Get().ResolveAsset(texturePath);
        texture = TextureManager::Get().Load(abs);
        if (!texture) {
            Log::Warn("SpriteRenderer", "Texture not found: " + abs);
        } else if (width == 32.0f && height == 32.0f) {
            width = static_cast<float>(texture->GetWidth());
            height = static_cast<float>(texture->GetHeight());
        }
    }
    material.ResolveAssets();
}

void SpriteRenderer::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    namespace fs = std::filesystem;

    // Texture section
    ImGui::Text("Texture");
    ImGui::Separator();

    // Show current texture path
    char pathBuffer[512];
    strncpy(pathBuffer, texturePath.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';

    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("##texpath", pathBuffer, sizeof(pathBuffer))) {
        texturePath = pathBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        texturePath.clear();
        texture = nullptr;
    }

    // Drop target for texture
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
            const char* droppedPath = static_cast<const char*>(payload->Data);

            // Convert to relative path if in project
            std::string relativePath = droppedPath;
            if (Project::Get().IsOpen()) {
                relativePath = Project::Get().GetRelativePath(droppedPath);
            }

            texturePath = relativePath;

            // Load the texture
            texture = TextureManager::Get().Load(droppedPath);

            // Auto-set size from texture if not set
            if (texture && (width == 32.0f && height == 32.0f)) {
                width = static_cast<float>(texture->GetWidth());
                height = static_cast<float>(texture->GetHeight());
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Load button
    if (!texturePath.empty() && !texture) {
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            std::string absPath = texturePath;
            if (Project::Get().IsOpen() && !fs::path(texturePath).is_absolute()) {
                absPath = Project::Get().GetAbsolutePath(texturePath);
            }
            texture = TextureManager::Get().Load(absPath);
        }
    }

    // Show texture info
    if (texture) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Loaded: %dx%d", texture->GetWidth(), texture->GetHeight());

        // Texture preview
        ImGui::Text("Preview:");
        float previewSize = 64.0f;
        float aspect = static_cast<float>(texture->GetWidth()) / static_cast<float>(texture->GetHeight());
        ImVec2 size = aspect > 1.0f ? ImVec2(previewSize, previewSize / aspect) : ImVec2(previewSize * aspect, previewSize);
        ImGui::Image(static_cast<ImTextureID>(texture->GetID()), size);
    } else if (!texturePath.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.3f, 1.0f), "Not loaded");
    }

    ImGui::Spacing();
    ImGui::Text("Transform");
    ImGui::Separator();

    float size[2] = { width, height };
    if (ImGui::DragFloat2("Size", size, 0.5f)) {
        SetSize(size[0], size[1]);
    }

    float colorArr[4] = { color.r, color.g, color.b, color.a };
    if (ImGui::ColorEdit4("Color", colorArr)) {
        SetColor(colorArr[0], colorArr[1], colorArr[2], colorArr[3]);
    }

    bool fx = flipX;
    bool fy = flipY;
    if (ImGui::Checkbox("Flip X", &fx)) {
        SetFlipX(fx);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Flip Y", &fy)) {
        SetFlipY(fy);
    }

    int order = sortingOrder;
    if (ImGui::InputInt("Sorting Order", &order)) {
        SetSortingOrder(order);
    }

    ImGui::Spacing();
    ImGui::Text("Material");
    ImGui::Separator();

    // Shader Name
    char shaderNameBuffer[128];
    strncpy(shaderNameBuffer, material.shaderName.c_str(), sizeof(shaderNameBuffer) - 1);
    shaderNameBuffer[sizeof(shaderNameBuffer)-1] = '\0';
    if (ImGui::InputText("Shader Name", shaderNameBuffer, sizeof(shaderNameBuffer))) {
        material.shaderName = shaderNameBuffer;
    }

    // Blend Mode
    const char* blendModes[] = { "Opaque", "Alpha", "Additive", "Multiply" };
    int currentBlendMode = static_cast<int>(material.blendMode);
    if (ImGui::Combo("Blend Mode", &currentBlendMode, blendModes, 4)) {
        material.blendMode = static_cast<BlendMode>(currentBlendMode);
    }

    // Tint
    float tintArr[4] = { material.tint.r, material.tint.g, material.tint.b, material.tint.a };
    if (ImGui::ColorEdit4("Tint", tintArr)) {
        material.tint = Color(tintArr[0], tintArr[1], tintArr[2], tintArr[3]);
    }

    // Main Texture Path
    char matTexPathBuffer[512];
    strncpy(matTexPathBuffer, material.mainTexturePath.c_str(), sizeof(matTexPathBuffer) - 1);
    matTexPathBuffer[sizeof(matTexPathBuffer)-1] = '\0';
    if (ImGui::InputText("Material Texture", matTexPathBuffer, sizeof(matTexPathBuffer))) {
        material.mainTexturePath = matTexPathBuffer;
        material.mainTexture = nullptr; // force reload on ResolveAssets
    }
    // Drop target for material texture
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
            const char* droppedPath = static_cast<const char*>(payload->Data);
            std::string relativePath = droppedPath;
            if (Project::Get().IsOpen()) {
                relativePath = Project::Get().GetRelativePath(droppedPath);
            }
            material.mainTexturePath = relativePath;
            material.mainTexture = TextureManager::Get().Load(droppedPath);
        }
        ImGui::EndDragDropTarget();
    }
    
    // Load button for material texture
    if (!material.mainTexturePath.empty() && !material.mainTexture) {
        ImGui::SameLine();
        if (ImGui::Button("Load Mat Tex")) {
            std::string absPath = material.mainTexturePath;
            if (Project::Get().IsOpen() && !fs::path(material.mainTexturePath).is_absolute()) {
                absPath = Project::Get().GetAbsolutePath(material.mainTexturePath);
            }
            material.mainTexture = TextureManager::Get().Load(absPath);
        }
    }

    // Custom properties
    ImGui::Text("Custom Properties");
    ImGui::Indent();
    
    std::string toRemove = "";
    for (auto& [name, prop] : material.properties) {
        ImGui::PushID(name.c_str());
        ImGui::Text("%s:", name.c_str());
        ImGui::SameLine();
        
        // Show type combo or select
        const char* propTypes[] = { "Float", "Vec4", "Texture" };
        int currentType = static_cast<int>(prop.type);
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##type", &currentType, propTypes, 3)) {
            prop.type = static_cast<MaterialProperty::Type>(currentType);
        }
        ImGui::SameLine();

        if (prop.type == MaterialProperty::Type::Float) {
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat("##val", &prop.floatVal, 0.1f);
        } else if (prop.type == MaterialProperty::Type::Vec4) {
            float v[4] = { prop.vec4Val.x, prop.vec4Val.y, prop.vec4Val.z, prop.vec4Val.w };
            ImGui::SetNextItemWidth(200);
            if (ImGui::DragFloat4("##val", v, 0.1f)) {
                prop.vec4Val = Vector4(v[0], v[1], v[2], v[3]);
            }
        } else if (prop.type == MaterialProperty::Type::Texture) {
            char texPathBuf[256];
            strncpy(texPathBuf, prop.texturePath.c_str(), sizeof(texPathBuf) - 1);
            texPathBuf[sizeof(texPathBuf)-1] = '\0';
            ImGui::SetNextItemWidth(150);
            if (ImGui::InputText("##texpath", texPathBuf, sizeof(texPathBuf))) {
                prop.texturePath = texPathBuf;
                prop.texture = nullptr;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    std::string relativePath = droppedPath;
                    if (Project::Get().IsOpen()) {
                        relativePath = Project::Get().GetRelativePath(droppedPath);
                    }
                    prop.texturePath = relativePath;
                    prop.texture = TextureManager::Get().Load(droppedPath);
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            toRemove = name;
        }
        ImGui::PopID();
    }

    if (!toRemove.empty()) {
        material.properties.erase(toRemove);
    }

    // Add new property
    static char newPropName[64] = "";
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##newpropname", newPropName, sizeof(newPropName));
    ImGui::SameLine();
    if (ImGui::Button("Add Property")) {
        std::string nameStr(newPropName);
        if (!nameStr.empty() && material.properties.find(nameStr) == material.properties.end()) {
            material.properties[nameStr] = MaterialProperty{};
            newPropName[0] = '\0';
        }
    }
    ImGui::Unindent();
#endif
}
