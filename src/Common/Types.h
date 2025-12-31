#ifndef MOLGA_COMMON_TYPES_H
#define MOLGA_COMMON_TYPES_H

#include <cmath>

// ============================================================================
// Vector2 - 2D vector for positions, directions, and sizes
// ============================================================================
struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2 operator/(float scalar) const { return Vector2(x / scalar, y / scalar); }
    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
    Vector2& operator-=(const Vector2& other) { x -= other.x; y -= other.y; return *this; }
    Vector2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
    Vector2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }

    bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vector2& other) const { return !(*this == other); }

    float Length() const { return std::sqrt(x * x + y * y); }
    float LengthSquared() const { return x * x + y * y; }
    Vector2 Normalized() const {
        float len = Length();
        return len > 0.0f ? Vector2(x / len, y / len) : Vector2(0, 0);
    }

    float Dot(const Vector2& other) const { return x * other.x + y * other.y; }
    float Cross(const Vector2& other) const { return x * other.y - y * other.x; }

    static Vector2 Zero() { return Vector2(0, 0); }
    static Vector2 One() { return Vector2(1, 1); }
    static Vector2 Up() { return Vector2(0, -1); }
    static Vector2 Down() { return Vector2(0, 1); }
    static Vector2 Left() { return Vector2(-1, 0); }
    static Vector2 Right() { return Vector2(1, 0); }

    static float Distance(const Vector2& a, const Vector2& b) {
        return (b - a).Length();
    }

    static Vector2 Lerp(const Vector2& a, const Vector2& b, float t) {
        return Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }
};

// ============================================================================
// Color - RGBA color representation
// ============================================================================
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    Color() = default;
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }

    static Color White() { return Color(1, 1, 1, 1); }
    static Color Black() { return Color(0, 0, 0, 1); }
    static Color Red() { return Color(1, 0, 0, 1); }
    static Color Green() { return Color(0, 1, 0, 1); }
    static Color Blue() { return Color(0, 0, 1, 1); }
    static Color Yellow() { return Color(1, 1, 0, 1); }
    static Color Cyan() { return Color(0, 1, 1, 1); }
    static Color Magenta() { return Color(1, 0, 1, 1); }
    static Color Transparent() { return Color(0, 0, 0, 0); }

    static Color Lerp(const Color& a, const Color& b, float t) {
        return Color(
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        );
    }
};

// ============================================================================
// AABB - Axis-Aligned Bounding Box for collision detection
// ============================================================================
struct AABB {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    AABB() = default;
    AABB(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}

    float Left() const { return x; }
    float Right() const { return x + width; }
    float Top() const { return y; }
    float Bottom() const { return y + height; }

    float CenterX() const { return x + width * 0.5f; }
    float CenterY() const { return y + height * 0.5f; }
    Vector2 Center() const { return Vector2(CenterX(), CenterY()); }
    Vector2 Size() const { return Vector2(width, height); }

    bool Contains(float px, float py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    bool Contains(const Vector2& point) const {
        return Contains(point.x, point.y);
    }

    bool Intersects(const AABB& other) const {
        return !(Right() < other.Left() || Left() > other.Right() ||
                 Bottom() < other.Top() || Top() > other.Bottom());
    }
};

// ============================================================================
// Circle - Circle shape for collision detection
// ============================================================================
struct Circle {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;

    Circle() = default;
    Circle(float x, float y, float radius) : x(x), y(y), radius(radius) {}

    Vector2 Center() const { return Vector2(x, y); }

    bool Contains(float px, float py) const {
        float dx = px - x;
        float dy = py - y;
        return (dx * dx + dy * dy) <= (radius * radius);
    }

    bool Contains(const Vector2& point) const {
        return Contains(point.x, point.y);
    }

    bool Intersects(const Circle& other) const {
        float dx = other.x - x;
        float dy = other.y - y;
        float distSq = dx * dx + dy * dy;
        float radiusSum = radius + other.radius;
        return distSq <= (radiusSum * radiusSum);
    }
};

// ============================================================================
// CollisionResult - Result of collision detection
// ============================================================================
struct CollisionResult {
    bool collided = false;
    float overlapX = 0.0f;
    float overlapY = 0.0f;
    float normalX = 0.0f;
    float normalY = 0.0f;

    CollisionResult() = default;
    CollisionResult(bool collided, float overlapX, float overlapY, float normalX, float normalY)
        : collided(collided), overlapX(overlapX), overlapY(overlapY),
          normalX(normalX), normalY(normalY) {}

    Vector2 GetOverlap() const { return Vector2(overlapX, overlapY); }
    Vector2 GetNormal() const { return Vector2(normalX, normalY); }
};

// ============================================================================
// Frame - UV coordinates for sprite sheet frames
// ============================================================================
struct Frame {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;

    Frame() = default;
    Frame(float u0, float v0, float u1, float v1) : u0(u0), v0(v0), u1(u1), v1(v1) {}

    float Width() const { return u1 - u0; }
    float Height() const { return v1 - v0; }
};

// ============================================================================
// Rect - Generic rectangle (integer-based, useful for UI)
// ============================================================================
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    Rect() = default;
    Rect(int x, int y, int width, int height)
        : x(x), y(y), width(width), height(height) {}

    int Left() const { return x; }
    int Right() const { return x + width; }
    int Top() const { return y; }
    int Bottom() const { return y + height; }
};

#endif // MOLGA_COMMON_TYPES_H
