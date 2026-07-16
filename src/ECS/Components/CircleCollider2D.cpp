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
    const Circle circle = GetWorldCircle();
    return AABB(circle.x - circle.radius, circle.y - circle.radius,
                circle.radius * 2.0f, circle.radius * 2.0f);
}

Circle CircleCollider2D::GetWorldCircle() const {
    Circle c;
    c.radius = std::abs(radius);
    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();
            const Vector2 scaledOffset(offset.x * worldScale.x, offset.y * worldScale.y);
            const float radians = transform->GetWorldRotation() * 3.14159265f / 180.0f;
            const float cosA = std::cos(radians);
            const float sinA = std::sin(radians);
            c.x = worldPos.x + scaledOffset.x * cosA - scaledOffset.y * sinA;
            c.y = worldPos.y + scaledOffset.x * sinA + scaledOffset.y * cosA;
            c.radius = std::abs(c.radius) * std::max(std::abs(worldScale.x), std::abs(worldScale.y));
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
