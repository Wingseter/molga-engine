#ifndef MOLGA_BOX_COLLIDER_2D_COMPONENT_H
#define MOLGA_BOX_COLLIDER_2D_COMPONENT_H

#include "../Component.h"
#include "Transform.h"
#include "../../Collision.h"

class BoxCollider2D : public Component {
public:
    COMPONENT_TYPE(BoxCollider2D)

    BoxCollider2D() = default;
    BoxCollider2D(float width, float height) : size(width, height) {}

    // Size
    void SetSize(float w, float h) { size.x = w; size.y = h; }
    void SetSize(const Vector2& s) { size = s; }
    Vector2 GetSize() const { return size; }

    // Offset from transform position
    void SetOffset(float x, float y) { offset.x = x; offset.y = y; }
    void SetOffset(const Vector2& o) { offset = o; }
    Vector2 GetOffset() const { return offset; }

    // Is trigger (no physical collision, just detection)
    void SetTrigger(bool trigger) { isTrigger = trigger; }
    bool IsTrigger() const { return isTrigger; }

    // Get world AABB
    AABB GetWorldAABB() const;

    // Check collision with another BoxCollider2D
    bool CheckCollision(const BoxCollider2D* other) const;
    CollisionResult CheckCollisionWithResult(const BoxCollider2D* other) const;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Vector2 size = Vector2(32.0f, 32.0f);
    Vector2 offset = Vector2::Zero();
    bool isTrigger = false;
};

#endif // MOLGA_BOX_COLLIDER_2D_COMPONENT_H
