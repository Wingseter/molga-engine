#include "BoxCollider2D.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include <nlohmann/json.hpp>

REGISTER_COMPONENT(BoxCollider2D)
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

AABB BoxCollider2D::GetWorldBounds() const {
    AABB aabb;
    aabb.width = size.x;
    aabb.height = size.y;

    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();

            aabb.x = worldPos.x + offset.x * worldScale.x;
            aabb.y = worldPos.y + offset.y * worldScale.y;
            aabb.width *= worldScale.x;
            aabb.height *= worldScale.y;
        }
    }

    return NormalizeBounds(aabb);
}

AABB BoxCollider2D::GetWorldAABB() const {
    return GetWorldBounds();
}

bool BoxCollider2D::CheckCollision(const BoxCollider2D* other) const {
    if (!other) return false;

    AABB a = GetWorldAABB();
    AABB b = other->GetWorldAABB();

    return Collision::CheckAABB(a, b);
}

CollisionResult BoxCollider2D::CheckCollisionWithResult(const BoxCollider2D* other) const {
    CollisionResult result;
    if (!other) return result;

    AABB a = GetWorldAABB();
    AABB b = other->GetWorldAABB();

    return Collision::CheckAABBWithResult(a, b);
}

void BoxCollider2D::Serialize(nlohmann::json& j) const {
    SerializeBase(j);
    j["size"] = { size.x, size.y };
}

void BoxCollider2D::Deserialize(const nlohmann::json& j) {
    DeserializeBase(j);
    if (j.contains("size") && j["size"].is_array()) {
        SetSize(j["size"][0], j["size"][1]);
    }
}

void BoxCollider2D::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    float sizeArr[2] = { size.x, size.y };
    if (ImGui::DragFloat2("Size", sizeArr, 0.5f)) {
        SetSize(sizeArr[0], sizeArr[1]);
    }

    float offsetArr[2] = { offset.x, offset.y };
    if (ImGui::DragFloat2("Offset", offsetArr, 0.5f)) {
        SetOffset(offsetArr[0], offsetArr[1]);
    }

    bool trigger = isTrigger;
    if (ImGui::Checkbox("Is Trigger", &trigger)) {
        SetTrigger(trigger);
    }
#endif
}
