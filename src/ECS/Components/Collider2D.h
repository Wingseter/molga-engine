#pragma once

#include "../Component.h"
#include "../../Common/Types.h"

class PhysicsMaterial2D;  // forward declaration (Phase 9)

// Abstract base class for all 2D colliders.
//
// GetWorldBounds() contract:
//   - Returns a world-space AABB that fully encloses the collider
//   - Rotation: NOT currently supported (ignored in AABB computation)
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

    // ── Bounds query ──

    // Returns world-space AABB enclosing this collider.
    virtual AABB GetWorldBounds() const = 0;

    // ── Common serialization helpers ──

    void SerializeBase(nlohmann::json& j) const;
    void DeserializeBase(const nlohmann::json& j);

protected:
    Vector2 offset = Vector2::Zero();
    bool isTrigger = false;

    // Helper: normalize an AABB so width/height are always positive
    static AABB NormalizeBounds(AABB aabb);
};
