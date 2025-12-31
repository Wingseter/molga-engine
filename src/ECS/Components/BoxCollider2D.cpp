#include "BoxCollider2D.h"
#include "../GameObject.h"

AABB BoxCollider2D::GetWorldAABB() const {
    AABB aabb;
    aabb.width = size.x;
    aabb.height = size.y;

    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();

            aabb.x = worldPos.x + offset.x * worldScale.x;
            aabb.y = worldPos.y + offset.y * worldScale.y;
            aabb.width *= worldScale.x;
            aabb.height *= worldScale.y;
        }
    }

    return aabb;
}

bool BoxCollider2D::CheckCollision(const BoxCollider2D* other) const {
    if (!other) return false;

    AABB a = GetWorldAABB();
    AABB b = other->GetWorldAABB();

    return Collision::CheckAABB(a, b);
}

CollisionResult BoxCollider2D::CheckCollisionWithResult(const BoxCollider2D* other) const {
    CollisionResult result;
    if (!other) return result;

    AABB a = GetWorldAABB();
    AABB b = other->GetWorldAABB();

    return Collision::CheckAABBWithResult(a, b);
}
