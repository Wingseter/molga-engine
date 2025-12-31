#ifndef MOLGA_COLLISION_H
#define MOLGA_COLLISION_H

#include "Common/Types.h"

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
