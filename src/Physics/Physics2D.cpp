#include "Physics2D.h"
#include "PhysicsWorld.h"
#include "../Core/World.h"

RaycastHit2D Physics2D::Raycast(World& world, const Vector2& origin,
                                const Vector2& direction, float maxDistance,
                                int layerMask) {
    return world.GetPhysicsWorld()->Raycast(world, origin, direction, maxDistance, layerMask);
}

std::vector<GameObject*> Physics2D::OverlapCircleAll(World& world, const Vector2& center,
                                                     float radius, int layerMask) {
    return world.GetPhysicsWorld()->OverlapCircleAll(world, center, radius, layerMask);
}

std::vector<GameObject*> Physics2D::OverlapBoxAll(World& world, const Vector2& center,
                                                  const Vector2& halfExtents,
                                                  int layerMask) {
    return world.GetPhysicsWorld()->OverlapBoxAll(world, center, halfExtents, layerMask);
}

GameObject* Physics2D::OverlapPoint(World& world, const Vector2& point, int layerMask) {
    return world.GetPhysicsWorld()->OverlapPoint(world, point, layerMask);
}
