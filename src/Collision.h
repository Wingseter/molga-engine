#ifndef MOLGA_COLLISION_H
#define MOLGA_COLLISION_H

struct AABB {
    float x, y;
    float width, height;

    float Left() const { return x; }
    float Right() const { return x + width; }
    float Top() const { return y; }
    float Bottom() const { return y + height; }

    float CenterX() const { return x + width * 0.5f; }
    float CenterY() const { return y + height * 0.5f; }
};

struct Circle {
    float x, y;
    float radius;
};

struct CollisionResult {
    bool collided;
    float overlapX;
    float overlapY;
    float normalX;
    float normalY;
};

class Collision {
public:
    // AABB vs AABB
    static bool CheckAABB(const AABB& a, const AABB& b);
    static CollisionResult CheckAABBWithResult(const AABB& a, const AABB& b);

    // Circle vs Circle
    static bool CheckCircle(const Circle& a, const Circle& b);
    static CollisionResult CheckCircleWithResult(const Circle& a, const Circle& b);

    // AABB vs Circle
    static bool CheckAABBCircle(const AABB& box, const Circle& circle);
    static CollisionResult CheckAABBCircleWithResult(const AABB& box, const Circle& circle);

    // Point tests
    static bool PointInAABB(float px, float py, const AABB& box);
    static bool PointInCircle(float px, float py, const Circle& circle);
};

#endif // MOLGA_COLLISION_H
