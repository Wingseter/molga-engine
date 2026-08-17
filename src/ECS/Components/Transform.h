#pragma once

#include "../Component.h"
#include "../../Common/Types.h"

class Transform : public Component {
public:
    COMPONENT_TYPE(Transform)

    Transform() = default;
    Transform(float x, float y) : position(x, y) {}
    Transform(const Vector2& pos) : position(pos) {}

    // Position
    Vector2 GetPosition() const { return position; }
    void SetPosition(const Vector2& pos) { position = pos; }
    void SetPosition(float x, float y) { position.x = x; position.y = y; }

    float GetX() const { return position.x; }
    float GetY() const { return position.y; }
    void SetX(float x) { position.x = x; }
    void SetY(float y) { position.y = y; }

    // Rotation (in degrees)
    float GetRotation() const { return rotation; }
    void SetRotation(float degrees) { rotation = degrees; }

    // Scale
    Vector2 GetScale() const { return scale; }
    void SetScale(const Vector2& s) { scale = s; }
    void SetScale(float x, float y) { scale.x = x; scale.y = y; }
    void SetScale(float uniform) { scale.x = scale.y = uniform; }

    // Movement helpers
    void Translate(const Vector2& delta) { position += delta; }
    void Translate(float dx, float dy) { position.x += dx; position.y += dy; }

    // Get world position (considering parent transforms)
    Vector2 GetWorldPosition() const;
    void SetWorldPosition(const Vector2& worldPosition);

    // Get world rotation
    float GetWorldRotation() const;
    void SetWorldRotation(float worldDegrees);

    // Get world scale
    Vector2 GetWorldScale() const;
    // Atomically converts a desired world scale to local space. Returns false
    // without changing this Transform when a parent world-scale axis is too
    // close to zero to invert safely.
    bool TrySetWorldScale(const Vector2& worldScale, float epsilon = 1.0e-6f);

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Vector2 position;
    float rotation = 0.0f;  // degrees
    Vector2 scale = Vector2::One();
};
