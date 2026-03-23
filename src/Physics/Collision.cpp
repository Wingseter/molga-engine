#include "Collision.h"
#include <cmath>
#include <algorithm>

bool Collision::CheckAABB(const AABB& a, const AABB& b) {
    return a.Left() < b.Right() &&
           a.Right() > b.Left() &&
           a.Top() < b.Bottom() &&
           a.Bottom() > b.Top();
}

CollisionResult Collision::CheckAABBWithResult(const AABB& a, const AABB& b) {
    CollisionResult result = {false, 0, 0, 0, 0};

    float overlapLeft = a.Right() - b.Left();
    float overlapRight = b.Right() - a.Left();
    float overlapTop = a.Bottom() - b.Top();
    float overlapBottom = b.Bottom() - a.Top();

    if (overlapLeft <= 0 || overlapRight <= 0 ||
        overlapTop <= 0 || overlapBottom <= 0) {
        return result;
    }

    result.collided = true;

    // Find minimum overlap
    float minOverlapX = (overlapLeft < overlapRight) ? overlapLeft : -overlapRight;
    float minOverlapY = (overlapTop < overlapBottom) ? overlapTop : -overlapBottom;

    if (std::abs(minOverlapX) < std::abs(minOverlapY)) {
        result.overlapX = minOverlapX;
        result.overlapY = 0;
        result.normalX = (minOverlapX > 0) ? 1.0f : -1.0f;
        result.normalY = 0;
    } else {
        result.overlapX = 0;
        result.overlapY = minOverlapY;
        result.normalX = 0;
        result.normalY = (minOverlapY > 0) ? 1.0f : -1.0f;
    }

    return result;
}

bool Collision::CheckCircle(const Circle& a, const Circle& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float distSq = dx * dx + dy * dy;
    float radiusSum = a.radius + b.radius;
    return distSq < radiusSum * radiusSum;
}

CollisionResult Collision::CheckCircleWithResult(const Circle& a, const Circle& b) {
    CollisionResult result = {false, 0, 0, 0, 0};

    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float distSq = dx * dx + dy * dy;
    float radiusSum = a.radius + b.radius;

    if (distSq >= radiusSum * radiusSum) {
        return result;
    }

    float dist = std::sqrt(distSq);
    result.collided = true;

    if (dist > 0.0001f) {
        result.normalX = dx / dist;
        result.normalY = dy / dist;
        float overlap = radiusSum - dist;
        result.overlapX = result.normalX * overlap;
        result.overlapY = result.normalY * overlap;
    } else {
        // Circles at same position
        result.normalX = 1.0f;
        result.normalY = 0.0f;
        result.overlapX = radiusSum;
        result.overlapY = 0.0f;
    }

    return result;
}

bool Collision::CheckAABBCircle(const AABB& box, const Circle& circle) {
    float closestX = std::max(box.Left(), std::min(circle.x, box.Right()));
    float closestY = std::max(box.Top(), std::min(circle.y, box.Bottom()));

    float dx = circle.x - closestX;
    float dy = circle.y - closestY;

    return (dx * dx + dy * dy) < (circle.radius * circle.radius);
}

CollisionResult Collision::CheckAABBCircleWithResult(const AABB& box, const Circle& circle) {
    CollisionResult result = {false, 0, 0, 0, 0};

    float closestX = std::max(box.Left(), std::min(circle.x, box.Right()));
    float closestY = std::max(box.Top(), std::min(circle.y, box.Bottom()));

    float dx = circle.x - closestX;
    float dy = circle.y - closestY;
    float distSq = dx * dx + dy * dy;

    if (distSq >= circle.radius * circle.radius) {
        return result;
    }

    result.collided = true;
    float dist = std::sqrt(distSq);

    if (dist > 0.0001f) {
        result.normalX = dx / dist;
        result.normalY = dy / dist;
        float overlap = circle.radius - dist;
        result.overlapX = result.normalX * overlap;
        result.overlapY = result.normalY * overlap;
    } else {
        // Circle center inside box
        float overlapLeft = circle.x - box.Left();
        float overlapRight = box.Right() - circle.x;
        float overlapTop = circle.y - box.Top();
        float overlapBottom = box.Bottom() - circle.y;

        float minX = std::min(overlapLeft, overlapRight);
        float minY = std::min(overlapTop, overlapBottom);

        if (minX < minY) {
            result.normalX = (overlapLeft < overlapRight) ? -1.0f : 1.0f;
            result.normalY = 0;
            result.overlapX = minX + circle.radius;
            result.overlapY = 0;
        } else {
            result.normalX = 0;
            result.normalY = (overlapTop < overlapBottom) ? -1.0f : 1.0f;
            result.overlapX = 0;
            result.overlapY = minY + circle.radius;
        }
    }

    return result;
}

bool Collision::PointInAABB(float px, float py, const AABB& box) {
    return px >= box.Left() && px <= box.Right() &&
           py >= box.Top() && py <= box.Bottom();
}

bool Collision::PointInCircle(float px, float py, const Circle& circle) {
    float dx = px - circle.x;
    float dy = py - circle.y;
    return (dx * dx + dy * dy) <= (circle.radius * circle.radius);
}
