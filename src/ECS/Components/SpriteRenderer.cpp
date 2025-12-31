#include "SpriteRenderer.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../../Renderer.h"
#include "../../Shader.h"
#include "../../Camera2D.h"
#include "../../Sprite.h"
#include <nlohmann/json.hpp>
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
