#include "CircleCollider2D.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

REGISTER_COMPONENT(CircleCollider2D)
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

AABB CircleCollider2D::GetWorldBounds() const {
    AABB aabb;
    float r = radius;
    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();
            float scale = std::max(std::abs(worldScale.x), std::abs(worldScale.y));
            r *= scale;
            aabb.x = worldPos.x + offset.x * worldScale.x - r;
            aabb.y = worldPos.y + offset.y * worldScale.y - r;
        }
    } else {
        aabb.x = offset.x - r;
        aabb.y = offset.y - r;
    }
    aabb.width = r * 2.0f;
    aabb.height = r * 2.0f;
    return NormalizeBounds(aabb);
}

Circle CircleCollider2D::GetWorldCircle() const {
    Circle c;
    c.radius = radius;
    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();
            c.x = worldPos.x + offset.x * worldScale.x;
            c.y = worldPos.y + offset.y * worldScale.y;
            c.radius *= std::max(std::abs(worldScale.x), std::abs(worldScale.y));
        }
    } else {
        c.x = offset.x;
        c.y = offset.y;
    }
    return c;
}

bool CircleCollider2D::CheckCollision(const CircleCollider2D* other) const {
    if (!other) return false;
    Circle a = GetWorldCircle();
    Circle b = other->GetWorldCircle();
    return Collision::CheckCircle(a, b);
}

CollisionResult CircleCollider2D::CheckCollisionWithResult(const CircleCollider2D* other) const {
    CollisionResult result;
    if (!other) return result;
    Circle a = GetWorldCircle();
    Circle b = other->GetWorldCircle();
    return Collision::CheckCircleWithResult(a, b);
}

void CircleCollider2D::Serialize(nlohmann::json& j) const {
    SerializeBase(j);
    j["radius"] = radius;
}

void CircleCollider2D::Deserialize(const nlohmann::json& j) {
    DeserializeBase(j);
    if (j.contains("radius")) {
        radius = j["radius"];
    }
}

void CircleCollider2D::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    float r = radius;
    if (ImGui::DragFloat("Radius", &r, 0.5f, 0.0f, 10000.0f)) {
        SetRadius(r);
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
