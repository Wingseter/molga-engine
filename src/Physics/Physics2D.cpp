#include "Physics2D.h"
#include "../Core/World.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Collider2D.h"
#include "../ECS/Components/CircleCollider2D.h"
#include "Collision.h"
#include <cmath>
#include <algorithm>

namespace {

// 오브젝트의 첫 활성 콜라이더 (PhysicsWorld와 동일한 dynamic_cast 패턴).
Collider2D* GetCollider(GameObject* obj) {
    for (auto* comp : obj->GetComponents()) {
        if (comp->IsEnabled()) {
            if (auto* col = dynamic_cast<Collider2D*>(comp)) {
                return col;
            }
        }
    }
    return nullptr;
}

bool PassesMask(const GameObject* obj, int mask) {
    int layer = obj->GetLayer();
    if (layer < 0 || layer > 31) return mask != 0;  // 범위 밖이면 전체 마스크에만 포함
    return (mask & (1 << layer)) != 0;
}

// 콜라이더 후보를 순회하며 콜백. (활성/마스크/콜라이더 존재 필터)
template <typename Fn>
void ForEachCollider(World& world, int mask, Fn&& fn) {
    for (auto& obj : world.Objects()) {
        if (!obj || !obj->IsActive()) continue;
        if (!PassesMask(obj.get(), mask)) continue;
        if (Collider2D* col = GetCollider(obj.get())) {
            fn(obj.get(), col);
        }
    }
}

Circle WorldCircleOf(Collider2D* col) {
    return static_cast<CircleCollider2D*>(col)->GetWorldCircle();
}

// ── Ray vs AABB (슬랩 방식). D는 단위 벡터 가정. ──
bool RayAABB(const Vector2& O, const Vector2& D, const AABB& box,
             float maxDist, float& outT, Vector2& outNormal) {
    const float minX = box.Left(),  maxX = box.Right();
    const float minY = box.Top(),   maxY = box.Bottom();
    float tmin = 0.0f;
    float tmax = maxDist;
    Vector2 normal(0.0f, 0.0f);
    constexpr float kEps = 1e-8f;

    // X 슬랩
    if (std::fabs(D.x) < kEps) {
        if (O.x < minX || O.x > maxX) return false;
    } else {
        float inv = 1.0f / D.x;
        float t1 = (minX - O.x) * inv;
        float t2 = (maxX - O.x) * inv;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) { tmin = t1; normal = Vector2(D.x > 0.0f ? -1.0f : 1.0f, 0.0f); }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    // Y 슬랩
    if (std::fabs(D.y) < kEps) {
        if (O.y < minY || O.y > maxY) return false;
    } else {
        float inv = 1.0f / D.y;
        float t1 = (minY - O.y) * inv;
        float t2 = (maxY - O.y) * inv;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) { tmin = t1; normal = Vector2(0.0f, D.y > 0.0f ? -1.0f : 1.0f); }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }

    outT = tmin;
    outNormal = normal;
    return true;
}

// ── Ray vs Circle. D는 단위 벡터 가정. ──
bool RayCircle(const Vector2& O, const Vector2& D, const Circle& c,
               float maxDist, float& outT, Vector2& outNormal) {
    Vector2 center(c.x, c.y);
    Vector2 m = O - center;
    float b = m.Dot(D);
    float cc = m.LengthSquared() - c.radius * c.radius;
    if (cc > 0.0f && b > 0.0f) return false;  // 원 밖에서 멀어지는 방향
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    float t = -b - std::sqrt(disc);
    if (t < 0.0f) t = 0.0f;  // origin이 원 내부
    if (t > maxDist) return false;
    Vector2 point = O + D * t;
    Vector2 n = (point - center);
    outNormal = (c.radius > 0.0f) ? n.Normalized() : Vector2(0.0f, 0.0f);
    outT = t;
    return true;
}

} // namespace

RaycastHit2D Physics2D::Raycast(World& world, const Vector2& origin, const Vector2& direction,
                                float maxDistance, int layerMask) {
    RaycastHit2D best;
    Vector2 D = direction.Normalized();
    if (D.LengthSquared() <= 0.0f) return best;  // 방향이 0이면 미스

    float bestT = maxDistance;
    ForEachCollider(world, layerMask, [&](GameObject* obj, Collider2D* col) {
        float t = 0.0f;
        Vector2 n;
        bool hit = false;
        if (col->GetShapeType() == Collider2D::ShapeType::Circle) {
            hit = RayCircle(origin, D, WorldCircleOf(col), bestT, t, n);
        } else {
            hit = RayAABB(origin, D, col->GetWorldBounds(), bestT, t, n);
        }
        if (hit && t <= bestT) {
            bestT = t;
            best.hit = true;
            best.collider = obj;
            best.distance = t;
            best.point = origin + D * t;
            best.normal = n;
        }
    });
    return best;
}

std::vector<GameObject*> Physics2D::OverlapCircleAll(World& world, const Vector2& center,
                                                     float radius, int layerMask) {
    std::vector<GameObject*> result;
    Circle query(center.x, center.y, radius);
    ForEachCollider(world, layerMask, [&](GameObject* obj, Collider2D* col) {
        bool overlap = (col->GetShapeType() == Collider2D::ShapeType::Circle)
            ? Collision::CheckCircle(WorldCircleOf(col), query)
            : Collision::CheckAABBCircle(col->GetWorldBounds(), query);
        if (overlap) result.push_back(obj);
    });
    return result;
}

std::vector<GameObject*> Physics2D::OverlapBoxAll(World& world, const Vector2& center,
                                                  const Vector2& halfExtents, int layerMask) {
    std::vector<GameObject*> result;
    AABB query(center.x - halfExtents.x, center.y - halfExtents.y,
               halfExtents.x * 2.0f, halfExtents.y * 2.0f);
    ForEachCollider(world, layerMask, [&](GameObject* obj, Collider2D* col) {
        bool overlap = (col->GetShapeType() == Collider2D::ShapeType::Circle)
            ? Collision::CheckAABBCircle(query, WorldCircleOf(col))
            : Collision::CheckAABB(query, col->GetWorldBounds());
        if (overlap) result.push_back(obj);
    });
    return result;
}

GameObject* Physics2D::OverlapPoint(World& world, const Vector2& point, int layerMask) {
    GameObject* found = nullptr;
    ForEachCollider(world, layerMask, [&](GameObject* obj, Collider2D* col) {
        if (found) return;
        bool inside = (col->GetShapeType() == Collider2D::ShapeType::Circle)
            ? Collision::PointInCircle(point.x, point.y, WorldCircleOf(col))
            : Collision::PointInAABB(point.x, point.y, col->GetWorldBounds());
        if (inside) found = obj;
    });
    return found;
}
