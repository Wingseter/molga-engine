#include "BoxCollider2D.h"
#include "../GameObject.h"
#include <nlohmann/json.hpp>
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

AABB BoxCollider2D::GetWorldAABB() const {
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

    return aabb;
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
    j["size"] = { size.x, size.y };
    j["offset"] = { offset.x, offset.y };
    j["isTrigger"] = isTrigger;
}

void BoxCollider2D::Deserialize(const nlohmann::json& j) {
    if (j.contains("size") && j["size"].is_array()) {
        SetSize(j["size"][0], j["size"][1]);
    }
    if (j.contains("offset") && j["offset"].is_array()) {
        SetOffset(j["offset"][0], j["offset"][1]);
    }
    if (j.contains("isTrigger")) {
        SetTrigger(j["isTrigger"]);
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
