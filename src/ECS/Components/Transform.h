#ifndef MOLGA_TRANSFORM_COMPONENT_H
#define MOLGA_TRANSFORM_COMPONENT_H

#include "../Component.h"
#include <cmath>

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
    Vector2& operator-=(const Vector2& other) { x -= other.x; y -= other.y; return *this; }

    float Length() const { return std::sqrt(x * x + y * y); }
    Vector2 Normalized() const {
        float len = Length();
        return len > 0.0f ? Vector2(x / len, y / len) : Vector2(0, 0);
    }

    static Vector2 Zero() { return Vector2(0, 0); }
    static Vector2 One() { return Vector2(1, 1); }
    static Vector2 Up() { return Vector2(0, -1); }
    static Vector2 Down() { return Vector2(0, 1); }
    static Vector2 Left() { return Vector2(-1, 0); }
    static Vector2 Right() { return Vector2(1, 0); }
};

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

    // Get world rotation
    float GetWorldRotation() const;

    // Get world scale
    Vector2 GetWorldScale() const;

private:
    Vector2 position;
    float rotation = 0.0f;  // degrees
    Vector2 scale = Vector2::One();
};

#endif // MOLGA_TRANSFORM_COMPONENT_H
