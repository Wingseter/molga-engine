#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include <cmath>

// Abstract base class for all 2D colliders.
//
// GetWorldBounds() contract:
//   - Returns a world-space AABB that fully encloses the collider
//   - Rotation: the returned AABB encloses the rotated world-space shape
//   - Negative scale: normalized (width/height always positive)
//   - May be larger than the actual shape (false positives OK for broad phase)
class Collider2D : public Component {
public:
    virtual ~Collider2D() = default;

    // ── Collider shape type ──
    enum class ShapeType { Box, Circle, Polygon, Capsule };
    virtual ShapeType GetShapeType() const = 0;

    // ── Common properties ──

    void SetOffset(const Vector2& o) { offset = o; }
    void SetOffset(float x, float y) { offset = Vector2(x, y); }
    Vector2 GetOffset() const { return offset; }

    void SetTrigger(bool trigger) { isTrigger = trigger; }
    bool IsTrigger() const { return isTrigger; }

    void SetFriction(float value) { friction = std::isfinite(value) && value >= 0.0f ? value : 0.0f; }
    float GetFriction() const { return friction; }

    void SetRestitution(float value) { restitution = std::isfinite(value) && value >= 0.0f ? value : 0.0f; }
    float GetRestitution() const { return restitution; }

    // ── Bounds query ──

    // Returns world-space AABB enclosing this collider.
    virtual AABB GetWorldBounds() const = 0;

    // ── Common serialization helpers ──

    void SerializeBase(nlohmann::json& j) const;
    void DeserializeBase(const nlohmann::json& j);

protected:
    Vector2 offset = Vector2::Zero();
    bool isTrigger = false;
    float friction = 0.4f;
    float restitution = 0.0f;

    // Helper: normalize an AABB so width/height are always positive
    static AABB NormalizeBounds(AABB aabb);
};
