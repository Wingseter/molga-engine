#pragma once

#include "../Common/Types.h"
#include <cstddef>
#include <memory>
#include <vector>

class GameObject;
class World;
struct RaycastHit2D;

// One persistent Box2D world belongs to each ECS World. ECS components remain
// the public/serialized state; this class incrementally mirrors them into
// backend bodies and shapes before stepping or querying.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Step(World& world, float fixedDt);
    void Reset();

    RaycastHit2D Raycast(World& world, const Vector2& origin, const Vector2& direction,
                         float maxDistance, int layerMask);
    std::vector<GameObject*> OverlapCircleAll(World& world, const Vector2& center,
                                              float radius, int layerMask);
    std::vector<GameObject*> OverlapBoxAll(World& world, const Vector2& center,
                                           const Vector2& halfExtents, int layerMask);
    GameObject* OverlapPoint(World& world, const Vector2& point, int layerMask);

    // Stable diagnostics used by regression tests and the profiler.
    std::size_t BodyCount() const;
    std::size_t ShapeCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void Synchronize(World& world);
};
