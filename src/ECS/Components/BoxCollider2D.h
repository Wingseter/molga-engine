#pragma once

#include "Collider2D.h"
#include "Transform.h"
#include "../../Physics/Collision.h"

class BoxCollider2D : public Collider2D {
public:
    COMPONENT_TYPE(BoxCollider2D)

    BoxCollider2D() = default;
    BoxCollider2D(float width, float height) : size(width, height) {}

    // ── Collider2D interface ──
    ShapeType GetShapeType() const override { return ShapeType::Box; }
    AABB GetWorldBounds() const override;

    // Size
    void SetSize(float w, float h) { size.x = w; size.y = h; }
    void SetSize(const Vector2& s) { size = s; }
    Vector2 GetSize() const { return size; }

    // Legacy API (delegates to GetWorldBounds)
    AABB GetWorldAABB() const;

    // Collision checks
    bool CheckCollision(const BoxCollider2D* other) const;
    CollisionResult CheckCollisionWithResult(const BoxCollider2D* other) const;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Vector2 size = Vector2(32.0f, 32.0f);
};
