#include "BoxCollider2D.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

REGISTER_COMPONENT(BoxCollider2D)
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

AABB BoxCollider2D::GetWorldBounds() const {
    Vector2 worldPosition = Vector2::Zero();
    Vector2 worldScale = Vector2::One();
    float worldRotation = 0.0f;
    if (gameObject) {
        if (Transform* transform = gameObject->GetComponent<Transform>()) {
            worldPosition = transform->GetWorldPosition();
            worldScale = transform->GetWorldScale();
            worldRotation = transform->GetWorldRotation();
        }
    }

    const float x0 = offset.x * worldScale.x;
    const float x1 = (offset.x + size.x) * worldScale.x;
    const float y0 = offset.y * worldScale.y;
    const float y1 = (offset.y + size.y) * worldScale.y;
    const float radians = worldRotation * 3.14159265f / 180.0f;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    const Vector2 corners[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const Vector2& corner : corners) {
        const Vector2 rotated(corner.x * cosA - corner.y * sinA,
                              corner.x * sinA + corner.y * cosA);
        const Vector2 point = worldPosition + rotated;
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    return AABB(minX, minY, maxX - minX, maxY - minY);
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

    float frictionValue = friction;
    if (ImGui::DragFloat("Friction", &frictionValue, 0.01f, 0.0f, 10.0f)) {
        SetFriction(frictionValue);
    }
    float restitutionValue = restitution;
    if (ImGui::DragFloat("Restitution", &restitutionValue, 0.01f, 0.0f, 10.0f)) {
        SetRestitution(restitutionValue);
    }
#endif
}
