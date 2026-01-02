#include "SpriteRenderer.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../../Renderer.h"
#include "../../Shader.h"
#include "../../Camera2D.h"
#include "../../Sprite.h"
#include "../../Texture.h"
#include "../../Core/TextureManager.h"
#include "../../Core/Project.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

void SpriteRenderer::RenderSprite(Renderer* renderer, Shader* shader, Camera2D* camera) {
    if (!gameObject || !enabled) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    // Create a temporary sprite for rendering
    Sprite sprite;

    Vector2 worldPos = transform->GetWorldPosition();
    Vector2 worldScale = transform->GetWorldScale();
    float worldRot = transform->GetWorldRotation();

    sprite.SetPosition(worldPos.x, worldPos.y);
    sprite.SetSize(width * worldScale.x, height * worldScale.y);
    sprite.SetRotation(worldRot);
    sprite.SetColor(color.r, color.g, color.b, color.a);

    if (texture) {
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

    renderer->Begin(shader, camera);
    renderer->DrawSprite(&sprite);
    renderer->End();
}

void SpriteRenderer::Serialize(nlohmann::json& j) const {
    j["color"] = { color.r, color.g, color.b, color.a };
    j["size"] = { width, height };
    j["flipX"] = flipX;
    j["flipY"] = flipY;
    j["sortingOrder"] = sortingOrder;
    j["texturePath"] = texturePath;
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
#endif
}
