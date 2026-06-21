#pragma once

#include "Collider2D.h"
#include "Transform.h"
#include "../../Physics/Collision.h"

class CircleCollider2D : public Collider2D {
public:
    COMPONENT_TYPE(CircleCollider2D)

    CircleCollider2D() = default;
    CircleCollider2D(float r) : radius(r) {}

    // ── Collider2D interface ──
    ShapeType GetShapeType() const override { return ShapeType::Circle; }
    AABB GetWorldBounds() const override;

    // Radius
    void SetRadius(float r) { radius = r; }
    float GetRadius() const { return radius; }

    // Helper to get world circle
    Circle GetWorldCircle() const;

    // Collision checks
    bool CheckCollision(const CircleCollider2D* other) const;
    CollisionResult CheckCollisionWithResult(const CircleCollider2D* other) const;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    float radius = 16.0f;
};
