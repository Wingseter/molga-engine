#pragma once

#include "../Common/Types.h"
#include <vector>
#include <limits>

class World;
class GameObject;

// Raycast 결과. hit이 false면 나머지 필드는 의미 없음.
struct RaycastHit2D {
    bool hit = false;
    GameObject* collider = nullptr;  // 맞은 콜라이더의 GameObject
    Vector2 point;                   // 충돌 지점 (월드 좌표)
    Vector2 normal;                  // 충돌 표면 법선 (단위 벡터)
    float distance = 0.0f;           // origin으로부터의 거리

    explicit operator bool() const { return hit; }
};

// 씬 공간에 대한 2D 물리 질의(Raycast/Overlap).
// PhysicsWorld와 달리 상태를 시뮬레이션하지 않고, 현재 콜라이더 배치를
// 즉시 질의한다. 박스는 축정렬 AABB(회전 미지원, 엔진 전반 규약과 동일),
// 원은 월드 원으로 처리한다.
//
// layerMask: 비트 i가 1이면 layer i를 포함. 기본값은 모든 레이어.
class Physics2D {
public:
    static constexpr int kAllLayers = ~0;
    static constexpr float kInfinity = std::numeric_limits<float>::max();

    // origin에서 direction 방향으로 maxDistance까지 쏘아 가장 가까운 콜라이더를 반환.
    static RaycastHit2D Raycast(World& world, const Vector2& origin, const Vector2& direction,
                                float maxDistance = kInfinity, int layerMask = kAllLayers);

    // 주어진 원/박스와 겹치는 모든 콜라이더의 GameObject 목록.
    static std::vector<GameObject*> OverlapCircleAll(World& world, const Vector2& center, float radius,
                                                     int layerMask = kAllLayers);
    static std::vector<GameObject*> OverlapBoxAll(World& world, const Vector2& center,
                                                  const Vector2& halfExtents, int layerMask = kAllLayers);

    // 점을 포함하는 첫 콜라이더(없으면 nullptr).
    static GameObject* OverlapPoint(World& world, const Vector2& point, int layerMask = kAllLayers);
};
