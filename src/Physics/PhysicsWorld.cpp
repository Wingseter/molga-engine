#include "PhysicsWorld.h"

#include "Box2DBackend.h"
#include "Physics2D.h"
#include "PhysicsConversions.h"
#include "../Core/EventBus.h"
#include "../Core/Events/PhysicsEvents.h"
#include "../Core/ProjectSettings.h"
#include "../Core/World.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/BoxCollider2D.h"
#include "../ECS/Components/CircleCollider2D.h"
#include "../ECS/Components/Rigidbody2D.h"
#include "../ECS/Components/TilemapRenderer.h"
#include "../ECS/Components/Transform.h"
#include "../Scripting/Script.h"
#include "../Scripting/ScriptInvocationBoundary.h"

#include <box2d/box2d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

constexpr float kMinShapeMeters = 0.005f;
constexpr float kPoseEpsilon = 1.0e-4f;
constexpr float kMaxRayMeters = 90000.0f;

bool Near(float a, float b, float epsilon = kPoseEpsilon) {
    return std::abs(a - b) <= epsilon;
}

bool Near(const Vector2& a, const Vector2& b, float epsilon = kPoseEpsilon) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
}

bool IsFinite(const Vector2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

b2Vec2 ToMeters(const Vector2& pixels, float ppm) {
    return {pixels.x / ppm, pixels.y / ppm};
}

Vector2 ToPixels(const b2Vec2& meters, float ppm) {
    return {meters.x * ppm, meters.y * ppm};
}

b2BodyType ToBackendType(Rigidbody2D::BodyType type) {
    switch (type) {
        case Rigidbody2D::BodyType::Static: return b2_staticBody;
        case Rigidbody2D::BodyType::Kinematic: return b2_kinematicBody;
        case Rigidbody2D::BodyType::Dynamic: return b2_dynamicBody;
    }
    return b2_staticBody;
}

std::uint32_t LayerBit(int layer) {
    return layer >= 0 && layer < 32 ? (std::uint32_t{1} << static_cast<unsigned>(layer)) : 0u;
}

b2Filter FilterForLayer(int layer) {
    b2Filter filter = b2DefaultFilter();
    filter.categoryBits = LayerBit(layer);
    // Keep query visibility independent from the collision matrix. Physical
    // pairs are filtered by CollisionFilterCallback; Box2D queries can then
    // use only the public layer-mask contract.
    filter.maskBits = std::numeric_limits<std::uint32_t>::max();
    return filter;
}

bool SameFilter(const b2Filter& a, const b2Filter& b) {
    return a.categoryBits == b.categoryBits && a.maskBits == b.maskBits &&
           a.groupIndex == b.groupIndex;
}

std::uint64_t CollisionMatrixFingerprint() {
    std::uint64_t hash = 1469598103934665603ull;
    const auto& settings = ProjectSettings::Get();
    for (int a = 0; a < 32; ++a) {
        for (int b = 0; b < 32; ++b) {
            hash ^= settings.IsCollisionEnabled(a, b) ? 1ull : 0ull;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

enum class BackendShapeKind { Box, Circle, TileRun };

struct ShapeKey {
    BackendShapeKind kind = BackendShapeKind::Box;
    int a = 0;
    int b = 0;
    int c = 0;

    bool operator<(const ShapeKey& other) const {
        if (kind != other.kind) return kind < other.kind;
        if (a != other.a) return a < other.a;
        if (b != other.b) return b < other.b;
        return c < other.c;
    }
};

struct BackendBodyState;

struct BackendShapeState {
    BackendBodyState* body = nullptr;
    ShapeKey key;
    b2ShapeId id = b2_nullShapeId;
    bool sensor = false;
    float friction = 0.4f;
    float restitution = 0.0f;
    b2Filter filter = b2DefaultFilter();
    std::array<float, 4> geometry{}; // centre x/y, half extents or radius
};

struct BackendBodyState {
    unsigned int objectId = 0;
    GameObject* owner = nullptr;
    b2BodyId id = b2_nullBodyId;
    b2BodyType type = b2_staticBody;
    std::map<ShapeKey, std::unique_ptr<BackendShapeState>> shapes;

    Vector2 lastWorldPosition = Vector2::Zero();
    float lastWorldRotation = 0.0f;
    Vector2 lastVelocity = Vector2::Zero();
    float lastAngularVelocity = 0.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    float gravityScale = 1.0f;
    float mass = 1.0f;
    bool fixedRotation = false;
    bool massDirty = true;
};

struct ShapePairKey {
    BackendShapeState* a = nullptr;
    BackendShapeState* b = nullptr;

    static ShapePairKey Make(BackendShapeState* first, BackendShapeState* second) {
        if (std::less<BackendShapeState*>{}(second, first)) std::swap(first, second);
        return {first, second};
    }

    bool operator<(const ShapePairKey& other) const {
        if (a != other.a) return std::less<BackendShapeState*>{}(a, other.a);
        return std::less<BackendShapeState*>{}(b, other.b);
    }
};

struct ObjectContactKey {
    unsigned int idA = 0;
    unsigned int idB = 0;
    bool trigger = false;

    static ObjectContactKey Make(unsigned int a, unsigned int b, bool isTrigger) {
        if (a > b) std::swap(a, b);
        return {a, b, isTrigger};
    }

    bool operator<(const ObjectContactKey& other) const {
        if (idA != other.idA) return idA < other.idA;
        if (idB != other.idB) return idB < other.idB;
        return trigger < other.trigger;
    }
};

struct ObjectContactState {
    bool triggerA = false;
    bool triggerB = false;
    Vector2 point = Vector2::Zero();
    Vector2 normal = Vector2::Zero();
    float penetration = 0.0f;
};

int HierarchyDepth(const GameObject* object) {
    int depth = 0;
    for (const GameObject* parent = object ? object->GetParent() : nullptr;
         parent; parent = parent->GetParent()) {
        ++depth;
    }
    return depth;
}

BackendShapeState* ShapeStateFromId(b2ShapeId id) {
    if (B2_IS_NULL(id) || !b2Shape_IsValid(id)) return nullptr;
    return static_cast<BackendShapeState*>(b2Shape_GetUserData(id));
}

bool CollisionFilterCallback(b2ShapeId shapeIdA, b2ShapeId shapeIdB, void*) {
    BackendShapeState* a = ShapeStateFromId(shapeIdA);
    BackendShapeState* b = ShapeStateFromId(shapeIdB);
    if (!a || !b || !a->body || !b->body || !a->body->owner || !b->body->owner) {
        return false;
    }
    return ProjectSettings::Get().IsCollisionEnabled(a->body->owner->GetLayer(),
                                                      b->body->owner->GetLayer());
}

b2DistanceProxy DistanceProxyForShape(b2ShapeId id) {
    switch (b2Shape_GetType(id)) {
        case b2_circleShape: {
            const b2Circle circle = b2Shape_GetCircle(id);
            return b2MakeProxy(&circle.center, 1, circle.radius);
        }
        case b2_polygonShape: {
            const b2Polygon polygon = b2Shape_GetPolygon(id);
            return b2MakeProxy(polygon.vertices, polygon.count, polygon.radius);
        }
        default:
            return {};
    }
}

bool SensorShapesOverlap(const BackendShapeState& a, const BackendShapeState& b) {
    if (!b2Shape_IsValid(a.id) || !b2Shape_IsValid(b.id)) return false;
    const b2DistanceProxy proxyA = DistanceProxyForShape(a.id);
    const b2DistanceProxy proxyB = DistanceProxyForShape(b.id);
    if (proxyA.count == 0 || proxyB.count == 0) return false;

    b2DistanceInput input{};
    input.proxyA = proxyA;
    input.proxyB = proxyB;
    input.transformA = b2Body_GetTransform(a.body->id);
    input.transformB = b2Body_GetTransform(b.body->id);
    input.useRadii = true;
    b2DistanceCache cache = b2_emptyDistanceCache;
    const b2DistanceOutput distance = b2ShapeDistance(&cache, &input, nullptr, 0);
    return distance.distance <= 1.0e-5f;
}

} // namespace

struct PhysicsWorld::Impl {
    b2WorldId worldId = b2_nullWorldId;
    float ppm = 100.0f;
    std::uint64_t collisionMatrixFingerprint = 0;
    std::map<unsigned int, std::unique_ptr<BackendBodyState>> bodies;
    std::set<ShapePairKey> activeShapePairs;
    std::map<ObjectContactKey, ObjectContactState> previousContacts;

    Impl();
    ~Impl();

    void ClearBackend();
    void Synchronize(World& world);
    BackendBodyState& EnsureBody(GameObject& object, Transform* transform,
                                 Rigidbody2D* rigidbody, bool forceStatic);
    void SyncBodyProperties(BackendBodyState& state, Transform* transform,
                            Rigidbody2D* rigidbody, bool forceStatic);
    void SyncBox(BackendBodyState& body, BoxCollider2D& collider, Transform* transform,
                 std::set<ShapeKey>& desiredShapes);
    void SyncCircle(BackendBodyState& body, CircleCollider2D& collider, Transform* transform,
                    std::set<ShapeKey>& desiredShapes);
    void SyncTilemap(BackendBodyState& body, TilemapRenderer& tilemap, Transform* transform,
                     std::set<ShapeKey>& desiredShapes);
    void SyncPolygonShape(BackendBodyState& body, const ShapeKey& key, const b2Polygon& polygon,
                          const std::array<float, 4>& geometry, bool sensor,
                          float friction, float restitution, const b2Filter& filter);
    void SyncCircleShape(BackendBodyState& body, const ShapeKey& key, const b2Circle& circle,
                         const std::array<float, 4>& geometry, bool sensor,
                         float friction, float restitution, const b2Filter& filter);
    void RemoveShape(BackendBodyState& body,
                     std::map<ShapeKey, std::unique_ptr<BackendShapeState>>::iterator it);
    void RemoveShapePairs(BackendShapeState* shape);
    void RemoveBody(std::map<unsigned int, std::unique_ptr<BackendBodyState>>::iterator it);
    void ApplyMass(BackendBodyState& body, Rigidbody2D* rigidbody);
    void ApplyForces(World& world);
    void PublishBodyState(World& world);
    void ProcessBackendEvents();
    std::map<ObjectContactKey, ObjectContactState> AggregateContacts() const;
    void DispatchContacts(World& world,
                          const std::map<ObjectContactKey, ObjectContactState>& current);
};

PhysicsWorld::Impl::Impl() {
    const auto& settings = ProjectSettings::Get();
    ppm = settings.pixelsPerMeter > 0.0f ? settings.pixelsPerMeter : 100.0f;
    collisionMatrixFingerprint = CollisionMatrixFingerprint();
    worldId = Box2DBackend::CreateWorld(settings.gravity.x / ppm, settings.gravity.y / ppm);
    b2World_SetCustomFilterCallback(worldId, CollisionFilterCallback, nullptr);
}

PhysicsWorld::Impl::~Impl() {
    ClearBackend();
    if (B2_IS_NON_NULL(worldId) && b2World_IsValid(worldId)) {
        Box2DBackend::DestroyWorld(worldId);
    }
}

void PhysicsWorld::Impl::ClearBackend() {
    activeShapePairs.clear();
    previousContacts.clear();
    for (auto& [_, body] : bodies) {
        if (body && B2_IS_NON_NULL(body->id) && b2Body_IsValid(body->id)) {
            b2DestroyBody(body->id);
        }
    }
    bodies.clear();
}

BackendBodyState& PhysicsWorld::Impl::EnsureBody(GameObject& object, Transform* transform,
                                                 Rigidbody2D* rigidbody, bool forceStatic) {
    const unsigned int id = object.GetID();
    auto found = bodies.find(id);
    if (found != bodies.end() && found->second->owner != &object) {
        RemoveBody(found);
        found = bodies.end();
    }
    if (found != bodies.end()) return *found->second;

    auto state = std::make_unique<BackendBodyState>();
    state->objectId = id;
    state->owner = &object;
    state->lastWorldPosition = transform ? transform->GetWorldPosition() : Vector2::Zero();
    state->lastWorldRotation = transform ? transform->GetWorldRotation() : 0.0f;
    state->type = forceStatic || !rigidbody ? b2_staticBody : ToBackendType(rigidbody->GetBodyType());
    if (rigidbody) {
        state->lastVelocity = rigidbody->GetVelocity();
        state->lastAngularVelocity = rigidbody->GetAngularVelocity();
        state->linearDamping = rigidbody->GetLinearDamping();
        state->angularDamping = rigidbody->GetAngularDamping();
        state->gravityScale = rigidbody->GetGravityScale();
        state->fixedRotation = rigidbody->IsRotationFrozen();
        state->mass = rigidbody->GetMass();
    }

    b2BodyDef def = b2DefaultBodyDef();
    def.type = state->type;
    def.position = ToMeters(state->lastWorldPosition, ppm);
    def.rotation = b2MakeRot(PhysicsConversions::ToRadians(state->lastWorldRotation));
    def.linearVelocity = ToMeters(state->lastVelocity, ppm);
    def.angularVelocity = PhysicsConversions::ToRadians(state->lastAngularVelocity);
    def.linearDamping = state->linearDamping;
    def.angularDamping = state->angularDamping;
    def.gravityScale = state->gravityScale;
    def.fixedRotation = state->fixedRotation;
    def.userData = state.get();
    state->id = b2CreateBody(worldId, &def);

    BackendBodyState* raw = state.get();
    bodies.emplace(id, std::move(state));
    return *raw;
}

void PhysicsWorld::Impl::SyncBodyProperties(BackendBodyState& state, Transform* transform,
                                            Rigidbody2D* rigidbody, bool forceStatic) {
    const b2BodyType desiredType = forceStatic || !rigidbody
        ? b2_staticBody : ToBackendType(rigidbody->GetBodyType());
    if (desiredType != state.type) {
        b2Body_SetType(state.id, desiredType);
        state.type = desiredType;
        state.massDirty = true;
    }

    const Vector2 worldPosition = transform ? transform->GetWorldPosition() : Vector2::Zero();
    const float worldRotation = transform ? transform->GetWorldRotation() : 0.0f;
    if (!Near(worldPosition, state.lastWorldPosition) || !Near(worldRotation, state.lastWorldRotation)) {
        b2Body_SetTransform(state.id, ToMeters(worldPosition, ppm),
                            b2MakeRot(PhysicsConversions::ToRadians(worldRotation)));
        state.lastWorldPosition = worldPosition;
        state.lastWorldRotation = worldRotation;
    }

    if (!rigidbody) return;

    if (!Near(state.linearDamping, rigidbody->GetLinearDamping())) {
        state.linearDamping = rigidbody->GetLinearDamping();
        b2Body_SetLinearDamping(state.id, state.linearDamping);
    }
    if (!Near(state.angularDamping, rigidbody->GetAngularDamping())) {
        state.angularDamping = rigidbody->GetAngularDamping();
        b2Body_SetAngularDamping(state.id, state.angularDamping);
    }
    if (!Near(state.gravityScale, rigidbody->GetGravityScale())) {
        state.gravityScale = rigidbody->GetGravityScale();
        b2Body_SetGravityScale(state.id, state.gravityScale);
    }
    if (state.fixedRotation != rigidbody->IsRotationFrozen()) {
        state.fixedRotation = rigidbody->IsRotationFrozen();
        b2Body_SetFixedRotation(state.id, state.fixedRotation);
        state.massDirty = true;
    }
    if (!Near(state.mass, rigidbody->GetMass())) {
        state.mass = rigidbody->GetMass();
        state.massDirty = true;
    }

    if (state.type == b2_dynamicBody || state.type == b2_kinematicBody) {
        if (!Near(state.lastVelocity, rigidbody->GetVelocity())) {
            state.lastVelocity = rigidbody->GetVelocity();
            b2Body_SetLinearVelocity(state.id, ToMeters(state.lastVelocity, ppm));
        }
        if (!Near(state.lastAngularVelocity, rigidbody->GetAngularVelocity())) {
            state.lastAngularVelocity = rigidbody->GetAngularVelocity();
            b2Body_SetAngularVelocity(state.id,
                                      PhysicsConversions::ToRadians(state.lastAngularVelocity));
        }
    }
}

void PhysicsWorld::Impl::RemoveShapePairs(BackendShapeState* shape) {
    for (auto it = activeShapePairs.begin(); it != activeShapePairs.end();) {
        if (it->a == shape || it->b == shape) it = activeShapePairs.erase(it);
        else ++it;
    }
}

void PhysicsWorld::Impl::RemoveShape(
    BackendBodyState& body,
    std::map<ShapeKey, std::unique_ptr<BackendShapeState>>::iterator it) {
    RemoveShapePairs(it->second.get());
    if (B2_IS_NON_NULL(it->second->id) && b2Shape_IsValid(it->second->id)) {
        b2DestroyShape(it->second->id);
    }
    body.shapes.erase(it);
    body.massDirty = true;
}

void PhysicsWorld::Impl::RemoveBody(
    std::map<unsigned int, std::unique_ptr<BackendBodyState>>::iterator it) {
    for (auto& [_, shape] : it->second->shapes) RemoveShapePairs(shape.get());
    if (B2_IS_NON_NULL(it->second->id) && b2Body_IsValid(it->second->id)) {
        b2DestroyBody(it->second->id);
    }
    bodies.erase(it);
}

void PhysicsWorld::Impl::SyncPolygonShape(BackendBodyState& body, const ShapeKey& key,
                                          const b2Polygon& polygon,
                                          const std::array<float, 4>& geometry, bool sensor,
                                          float friction, float restitution,
                                          const b2Filter& filter) {
    auto found = body.shapes.find(key);
    if (found != body.shapes.end() && found->second->sensor != sensor) {
        RemoveShape(body, found);
        found = body.shapes.end();
    }

    if (found == body.shapes.end()) {
        auto state = std::make_unique<BackendShapeState>();
        state->body = &body;
        state->key = key;
        state->sensor = sensor;
        state->friction = friction;
        state->restitution = restitution;
        state->filter = filter;
        state->geometry = geometry;
        b2ShapeDef def = b2DefaultShapeDef();
        def.userData = state.get();
        def.density = 1.0f;
        def.friction = friction;
        def.restitution = restitution;
        def.filter = filter;
        def.isSensor = sensor;
        def.enableContactEvents = true;
        def.enableSensorEvents = true;
        state->id = b2CreatePolygonShape(body.id, &def, &polygon);
        body.shapes.emplace(key, std::move(state));
        body.massDirty = true;
        return;
    }

    BackendShapeState& state = *found->second;
    if (state.geometry != geometry) {
        b2Shape_SetPolygon(state.id, &polygon);
        state.geometry = geometry;
        body.massDirty = true;
    }
    if (!Near(state.friction, friction)) {
        state.friction = friction;
        b2Shape_SetFriction(state.id, friction);
    }
    if (!Near(state.restitution, restitution)) {
        state.restitution = restitution;
        b2Shape_SetRestitution(state.id, restitution);
    }
    if (!SameFilter(state.filter, filter)) {
        state.filter = filter;
        b2Shape_SetFilter(state.id, filter);
    }
}

void PhysicsWorld::Impl::SyncCircleShape(BackendBodyState& body, const ShapeKey& key,
                                         const b2Circle& circle,
                                         const std::array<float, 4>& geometry, bool sensor,
                                         float friction, float restitution,
                                         const b2Filter& filter) {
    auto found = body.shapes.find(key);
    if (found != body.shapes.end() && found->second->sensor != sensor) {
        RemoveShape(body, found);
        found = body.shapes.end();
    }

    if (found == body.shapes.end()) {
        auto state = std::make_unique<BackendShapeState>();
        state->body = &body;
        state->key = key;
        state->sensor = sensor;
        state->friction = friction;
        state->restitution = restitution;
        state->filter = filter;
        state->geometry = geometry;
        b2ShapeDef def = b2DefaultShapeDef();
        def.userData = state.get();
        def.density = 1.0f;
        def.friction = friction;
        def.restitution = restitution;
        def.filter = filter;
        def.isSensor = sensor;
        def.enableContactEvents = true;
        def.enableSensorEvents = true;
        state->id = b2CreateCircleShape(body.id, &def, &circle);
        body.shapes.emplace(key, std::move(state));
        body.massDirty = true;
        return;
    }

    BackendShapeState& state = *found->second;
    if (state.geometry != geometry) {
        b2Shape_SetCircle(state.id, &circle);
        state.geometry = geometry;
        body.massDirty = true;
    }
    if (!Near(state.friction, friction)) {
        state.friction = friction;
        b2Shape_SetFriction(state.id, friction);
    }
    if (!Near(state.restitution, restitution)) {
        state.restitution = restitution;
        b2Shape_SetRestitution(state.id, restitution);
    }
    if (!SameFilter(state.filter, filter)) {
        state.filter = filter;
        b2Shape_SetFilter(state.id, filter);
    }
}

void PhysicsWorld::Impl::SyncBox(BackendBodyState& body, BoxCollider2D& collider,
                                 Transform* transform, std::set<ShapeKey>& desiredShapes) {
    const ShapeKey key{BackendShapeKind::Box, 0, 0, 0};
    desiredShapes.insert(key);
    const Vector2 scale = transform ? transform->GetWorldScale() : Vector2::One();
    const Vector2 offset = collider.GetOffset();
    const Vector2 size = collider.GetSize();
    const float x0 = offset.x * scale.x;
    const float x1 = (offset.x + size.x) * scale.x;
    const float y0 = offset.y * scale.y;
    const float y1 = (offset.y + size.y) * scale.y;
    const Vector2 center((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    const float halfX = std::max(std::abs(x1 - x0) * 0.5f / ppm, kMinShapeMeters);
    const float halfY = std::max(std::abs(y1 - y0) * 0.5f / ppm, kMinShapeMeters);
    b2Polygon polygon = b2MakeOffsetBox(halfX, halfY, ToMeters(center, ppm), 0.0f);
    const std::array<float, 4> geometry{center.x / ppm, center.y / ppm, halfX, halfY};
    SyncPolygonShape(body, key, polygon, geometry, collider.IsTrigger(),
                     collider.GetFriction(), collider.GetRestitution(),
                     FilterForLayer(body.owner->GetLayer()));
}

void PhysicsWorld::Impl::SyncCircle(BackendBodyState& body, CircleCollider2D& collider,
                                    Transform* transform, std::set<ShapeKey>& desiredShapes) {
    const ShapeKey key{BackendShapeKind::Circle, 0, 0, 0};
    desiredShapes.insert(key);
    const Vector2 scale = transform ? transform->GetWorldScale() : Vector2::One();
    const Vector2 offset(collider.GetOffset().x * scale.x, collider.GetOffset().y * scale.y);
    const float radius = std::max(std::abs(collider.GetRadius()) *
                                      std::max(std::abs(scale.x), std::abs(scale.y)) / ppm,
                                  kMinShapeMeters);
    b2Circle circle{};
    circle.center = ToMeters(offset, ppm);
    circle.radius = radius;
    const std::array<float, 4> geometry{circle.center.x, circle.center.y, radius, 0.0f};
    SyncCircleShape(body, key, circle, geometry, collider.IsTrigger(),
                    collider.GetFriction(), collider.GetRestitution(),
                    FilterForLayer(body.owner->GetLayer()));
}

void PhysicsWorld::Impl::SyncTilemap(BackendBodyState& body, TilemapRenderer& tilemap,
                                     Transform* transform,
                                     std::set<ShapeKey>& desiredShapes) {
    const Vector2 scale = transform ? transform->GetWorldScale() : Vector2::One();
    const b2Filter filter = FilterForLayer(body.owner->GetLayer());
    for (const TilemapCollisionRun& run : tilemap.GetCollisionRuns()) {
        const int y = run.row;
        const int start = run.start;
        const int end = run.end;
        const int layerRow = run.layerIndex * std::max(1, tilemap.height) + y;
        const ShapeKey key{BackendShapeKind::TileRun, layerRow, start, end};
        desiredShapes.insert(key);

        const float x0 = static_cast<float>(start * tilemap.tileSize) * scale.x;
        const float x1 = static_cast<float>(end * tilemap.tileSize) * scale.x;
        const float y0 = static_cast<float>(y * tilemap.tileSize) * scale.y;
        const float y1 = static_cast<float>((y + 1) * tilemap.tileSize) * scale.y;
        const Vector2 center((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        const float halfX = std::max(std::abs(x1 - x0) * 0.5f / ppm, kMinShapeMeters);
        const float halfY = std::max(std::abs(y1 - y0) * 0.5f / ppm, kMinShapeMeters);
        b2Polygon polygon = b2MakeOffsetBox(halfX, halfY, ToMeters(center, ppm), 0.0f);
        const std::array<float, 4> geometry{center.x / ppm, center.y / ppm, halfX, halfY};
        SyncPolygonShape(body, key, polygon, geometry, false, 0.4f, 0.0f, filter);
    }
}

void PhysicsWorld::Impl::ApplyMass(BackendBodyState& body, Rigidbody2D* rigidbody) {
    if (body.type != b2_dynamicBody || !rigidbody || !body.massDirty) return;
    b2Body_ApplyMassFromShapes(body.id);
    b2MassData data = b2Body_GetMassData(body.id);
    const float targetMass = std::max(rigidbody->GetMass(), 0.001f);
    if (data.mass > 0.000001f) {
        const float scale = targetMass / data.mass;
        data.mass = targetMass;
        data.rotationalInertia *= scale;
    } else {
        data.mass = targetMass;
        data.center = {0.0f, 0.0f};
        data.rotationalInertia = rigidbody->IsRotationFrozen() ? 0.0f : targetMass;
    }
    b2Body_SetMassData(body.id, data);
    body.massDirty = false;
}

void PhysicsWorld::Impl::Synchronize(World& world) {
    const auto& settings = ProjectSettings::Get();
    const float desiredPpm = std::isfinite(settings.pixelsPerMeter) && settings.pixelsPerMeter > 0.0f
        ? settings.pixelsPerMeter : 100.0f;
    if (!Near(ppm, desiredPpm)) {
        ClearBackend();
        ppm = desiredPpm;
    }
    b2World_SetGravity(worldId, ToMeters(settings.gravity, ppm));

    const std::uint64_t desiredMatrixFingerprint = CollisionMatrixFingerprint();
    if (desiredMatrixFingerprint != collisionMatrixFingerprint) {
        // Refilter existing broad-phase proxies so both disabling and enabling a
        // layer pair takes effect without rebuilding bodies or shapes.
        for (auto& [bodyKey, body] : bodies) {
            (void)bodyKey;
            for (auto& [shapeKey, shape] : body->shapes) {
                (void)shapeKey;
                if (!b2Shape_IsValid(shape->id)) continue;
                // Box2D intentionally no-ops when SetFilter receives identical
                // bits. Toggle one mask bit and restore it to invalidate stale
                // pairs while leaving the public filter unchanged for the step.
                b2Filter refresh = shape->filter;
                refresh.maskBits ^= 1u;
                b2Shape_SetFilter(shape->id, refresh);
                b2Shape_SetFilter(shape->id, shape->filter);
            }
        }
        collisionMatrixFingerprint = desiredMatrixFingerprint;
    }

    std::set<unsigned int> desiredBodies;
    for (const auto& objectPtr : world.Objects()) {
        GameObject* object = objectPtr.get();
        if (!object || !object->IsActive()) continue;
        Transform* transform = object->GetComponent<Transform>();
        Rigidbody2D* rigidbody = object->GetComponent<Rigidbody2D>();
        if (rigidbody && !rigidbody->IsEnabled()) rigidbody = nullptr;
        BoxCollider2D* box = object->GetComponent<BoxCollider2D>();
        if (box && !box->IsEnabled()) box = nullptr;
        CircleCollider2D* circle = object->GetComponent<CircleCollider2D>();
        if (circle && !circle->IsEnabled()) circle = nullptr;
        TilemapRenderer* tilemap = object->GetComponent<TilemapRenderer>();
        if (tilemap && !tilemap->IsEnabled()) tilemap = nullptr;
        if (!rigidbody && !box && !circle && !tilemap) continue;

        desiredBodies.insert(object->GetID());
        BackendBodyState& body = EnsureBody(*object, transform, rigidbody, tilemap != nullptr);
        SyncBodyProperties(body, transform, rigidbody, tilemap != nullptr);
        std::set<ShapeKey> desiredShapes;
        if (box) SyncBox(body, *box, transform, desiredShapes);
        if (circle) SyncCircle(body, *circle, transform, desiredShapes);
        if (tilemap) SyncTilemap(body, *tilemap, transform, desiredShapes);

        for (auto it = body.shapes.begin(); it != body.shapes.end();) {
            if (desiredShapes.find(it->first) == desiredShapes.end()) {
                auto remove = it++;
                RemoveShape(body, remove);
            } else {
                ++it;
            }
        }
        ApplyMass(body, rigidbody);
    }

    for (auto it = bodies.begin(); it != bodies.end();) {
        if (desiredBodies.find(it->first) == desiredBodies.end()) {
            auto remove = it++;
            RemoveBody(remove);
        } else {
            ++it;
        }
    }
}

void PhysicsWorld::Impl::ApplyForces(World& world) {
    for (const auto& object : world.Objects()) {
        if (!object || !object->IsActive()) continue;
        Rigidbody2D* rigidbody = object->GetComponent<Rigidbody2D>();
        if (!rigidbody || !rigidbody->IsEnabled()) continue;
        auto bodyIt = bodies.find(object->GetID());
        if (bodyIt != bodies.end() && bodyIt->second->owner == object.get() &&
            bodyIt->second->type == b2_dynamicBody) {
            b2BodyId id = bodyIt->second->id;
            const Vector2 force = rigidbody->GetForceAccumulator();
            const Vector2 impulse = rigidbody->GetImpulseAccumulator();
            if (force.LengthSquared() > 0.0f) {
                b2Body_ApplyForceToCenter(id, ToMeters(force, ppm), true);
            }
            if (impulse.LengthSquared() > 0.0f) {
                b2Body_ApplyLinearImpulseToCenter(id, ToMeters(impulse, ppm), true);
            }
            if (rigidbody->GetTorqueAccumulator() != 0.0f) {
                b2Body_ApplyTorque(id, rigidbody->GetTorqueAccumulator(), true);
            }
            if (rigidbody->GetAngularImpulseAccumulator() != 0.0f) {
                b2Body_ApplyAngularImpulse(id, rigidbody->GetAngularImpulseAccumulator(), true);
            }
        }
        rigidbody->ClearForces();
    }
}

void PhysicsWorld::Impl::PublishBodyState(World& world) {
    std::vector<BackendBodyState*> ordered;
    ordered.reserve(bodies.size());
    for (auto& [_, state] : bodies) ordered.push_back(state.get());
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        return HierarchyDepth(a->owner) < HierarchyDepth(b->owner);
    });

    for (BackendBodyState* state : ordered) {
        GameObject* object = world.FindById(state->objectId);
        if (!object || object != state->owner) continue;
        Rigidbody2D* rigidbody = object->GetComponent<Rigidbody2D>();
        if (rigidbody && !rigidbody->IsEnabled()) rigidbody = nullptr;
        Transform* transform = object->GetComponent<Transform>();
        if (state->type == b2_dynamicBody || state->type == b2_kinematicBody) {
            const Vector2 position = ToPixels(b2Body_GetPosition(state->id), ppm);
            const float rotation = PhysicsConversions::ToDegrees(
                b2Rot_GetAngle(b2Body_GetRotation(state->id)));
            if (transform) {
                transform->SetWorldPosition(position);
                transform->SetWorldRotation(rotation);
            }
            state->lastWorldPosition = position;
            state->lastWorldRotation = rotation;
            if (rigidbody) {
                const Vector2 velocity = ToPixels(b2Body_GetLinearVelocity(state->id), ppm);
                const float angularVelocity = PhysicsConversions::ToDegrees(
                    b2Body_GetAngularVelocity(state->id));
                rigidbody->SetVelocity(velocity);
                rigidbody->SetAngularVelocity(angularVelocity);
                state->lastVelocity = velocity;
                state->lastAngularVelocity = angularVelocity;
            }
        } else if (transform) {
            state->lastWorldPosition = transform->GetWorldPosition();
            state->lastWorldRotation = transform->GetWorldRotation();
        }
    }
}

void PhysicsWorld::Impl::ProcessBackendEvents() {
    const b2ContactEvents contacts = b2World_GetContactEvents(worldId);
    for (int i = 0; i < contacts.beginCount; ++i) {
        BackendShapeState* a = ShapeStateFromId(contacts.beginEvents[i].shapeIdA);
        BackendShapeState* b = ShapeStateFromId(contacts.beginEvents[i].shapeIdB);
        if (a && b) activeShapePairs.insert(ShapePairKey::Make(a, b));
    }
    for (int i = 0; i < contacts.endCount; ++i) {
        BackendShapeState* a = ShapeStateFromId(contacts.endEvents[i].shapeIdA);
        BackendShapeState* b = ShapeStateFromId(contacts.endEvents[i].shapeIdB);
        if (a && b) activeShapePairs.erase(ShapePairKey::Make(a, b));
    }

    const b2SensorEvents sensors = b2World_GetSensorEvents(worldId);
    for (int i = 0; i < sensors.beginCount; ++i) {
        BackendShapeState* a = ShapeStateFromId(sensors.beginEvents[i].sensorShapeId);
        BackendShapeState* b = ShapeStateFromId(sensors.beginEvents[i].visitorShapeId);
        if (a && b) activeShapePairs.insert(ShapePairKey::Make(a, b));
    }
    for (int i = 0; i < sensors.endCount; ++i) {
        BackendShapeState* a = ShapeStateFromId(sensors.endEvents[i].sensorShapeId);
        BackendShapeState* b = ShapeStateFromId(sensors.endEvents[i].visitorShapeId);
        if (a && b) activeShapePairs.erase(ShapePairKey::Make(a, b));
    }

    // Box2D deliberately treats SetTransform as a teleport. Sensor end events may be
    // deferred for a teleported body, while the ECS contract expects the new overlap
    // state in the same fixed step. Validate only sensor pairs geometrically; ordinary
    // contacts continue to follow Box2D's speculative-contact event lifetime.
    for (auto it = activeShapePairs.begin(); it != activeShapePairs.end();) {
        const bool sensorPair = it->a && it->b && (it->a->sensor || it->b->sensor);
        const bool layersCollide = it->a && it->b && it->a->body && it->b->body &&
            it->a->body->owner && it->b->body->owner &&
            ProjectSettings::Get().IsCollisionEnabled(
                it->a->body->owner->GetLayer(), it->b->body->owner->GetLayer());
        if (!layersCollide || (sensorPair && !SensorShapesOverlap(*it->a, *it->b))) {
            it = activeShapePairs.erase(it);
        } else {
            ++it;
        }
    }
}

std::map<ObjectContactKey, ObjectContactState> PhysicsWorld::Impl::AggregateContacts() const {
    std::map<ObjectContactKey, ObjectContactState> result;
    for (const ShapePairKey& pair : activeShapePairs) {
        if (!pair.a || !pair.b || !pair.a->body || !pair.b->body) continue;
        const unsigned int ownerA = pair.a->body->objectId;
        const unsigned int ownerB = pair.b->body->objectId;
        if (ownerA == ownerB) continue;
        const bool trigger = pair.a->sensor || pair.b->sensor;
        const ObjectContactKey key = ObjectContactKey::Make(ownerA, ownerB, trigger);
        ObjectContactState& aggregate = result[key];
        if (pair.a->sensor) {
            if (ownerA == key.idA) aggregate.triggerA = true;
            else aggregate.triggerB = true;
        }
        if (pair.b->sensor) {
            if (ownerB == key.idA) aggregate.triggerA = true;
            else aggregate.triggerB = true;
        }
        if (trigger || !b2Shape_IsValid(pair.a->id)) continue;

        const int capacity = b2Shape_GetContactCapacity(pair.a->id);
        if (capacity <= 0) continue;
        std::vector<b2ContactData> data(static_cast<std::size_t>(capacity));
        const int count = b2Shape_GetContactData(pair.a->id, data.data(), capacity);
        for (int i = 0; i < count; ++i) {
            const bool matches =
                (B2_ID_EQUALS(data[i].shapeIdA, pair.a->id) && B2_ID_EQUALS(data[i].shapeIdB, pair.b->id)) ||
                (B2_ID_EQUALS(data[i].shapeIdA, pair.b->id) && B2_ID_EQUALS(data[i].shapeIdB, pair.a->id));
            if (!matches || data[i].manifold.pointCount <= 0) continue;
            BackendShapeState* manifoldA = ShapeStateFromId(data[i].shapeIdA);
            Vector2 normal(data[i].manifold.normal.x, data[i].manifold.normal.y);
            if (manifoldA && manifoldA->body->objectId != key.idA) normal *= -1.0f;
            aggregate.normal = normal;
            aggregate.point = ToPixels(data[i].manifold.points[0].point, ppm);
            for (int p = 0; p < data[i].manifold.pointCount; ++p) {
                aggregate.penetration = std::max(
                    aggregate.penetration,
                    std::max(0.0f, -data[i].manifold.points[p].separation * ppm));
            }
            break;
        }
    }
    return result;
}

namespace {

enum class ScriptContactPhase { Enter, Stay, Exit };

std::shared_ptr<GameObject> FindSharedObject(World& world, unsigned int id) {
    for (const auto& object : world.Objects()) {
        if (object && object->GetID() == id) return object;
    }
    return {};
}

struct ScriptContactInvocation {
    unsigned int objectId = 0;
    unsigned int otherId = 0;
    std::shared_ptr<GameObject> objectIdentity;
    std::shared_ptr<GameObject> otherIdentity;
    ScriptHandle handle;
    bool trigger = false;
    ScriptContactPhase phase = ScriptContactPhase::Enter;
};

void AppendScriptInvocations(World& world, unsigned int objectId, unsigned int otherId,
                             bool trigger, ScriptContactPhase phase,
                             std::vector<ScriptContactInvocation>& out) {
    std::shared_ptr<GameObject> object = FindSharedObject(world, objectId);
    if (!object || !object->IsActive()) return;
    std::shared_ptr<GameObject> other = FindSharedObject(world, otherId);

    // Capture identities, not raw pointers. A callback is allowed to remove its
    // own component, a later component, or either object. Each invocation is
    // re-resolved immediately before use, and the shared object identities keep
    // an object from being deallocated while an earlier callback still has its
    // `other` argument on the stack.
    for (Component* component : object->GetComponents()) {
        if (!component || !component->IsEnabled() || !dynamic_cast<Script*>(component)) continue;
        out.push_back({objectId, otherId, object, other,
                       ScriptInvocationBoundary::MakeHandle(
                           *static_cast<Script*>(component)),
                       trigger, phase});
    }
}

void InvokeScriptContact(World& world, const ScriptContactInvocation& invocation) {
    std::shared_ptr<GameObject> object = FindSharedObject(world, invocation.objectId);
    if (!object || object.get() != invocation.objectIdentity.get() || !object->IsActive()) return;

    std::shared_ptr<GameObject> other = FindSharedObject(world, invocation.otherId);
    GameObject* otherRaw = other && other.get() == invocation.otherIdentity.get()
        ? other.get() : nullptr;

    ScriptPhase boundaryPhase = ScriptPhase::CollisionEnter;
    if (invocation.trigger) {
        if (invocation.phase == ScriptContactPhase::Enter) boundaryPhase = ScriptPhase::TriggerEnter;
        else if (invocation.phase == ScriptContactPhase::Stay) boundaryPhase = ScriptPhase::TriggerStay;
        else boundaryPhase = ScriptPhase::TriggerExit;
    } else if (invocation.phase == ScriptContactPhase::Stay) {
        boundaryPhase = ScriptPhase::CollisionStay;
    } else if (invocation.phase == ScriptContactPhase::Exit) {
        boundaryPhase = ScriptPhase::CollisionExit;
    }

    ScriptInvocationBoundary::Invoke(
        world, invocation.handle, boundaryPhase,
        [&](Script& script) {
            if (invocation.trigger) {
                if (invocation.phase == ScriptContactPhase::Enter) script.OnTriggerEnter(otherRaw);
                else if (invocation.phase == ScriptContactPhase::Stay) script.OnTriggerStay(otherRaw);
                else script.OnTriggerExit(otherRaw);
            } else {
                if (invocation.phase == ScriptContactPhase::Enter) script.OnCollisionEnter(otherRaw);
                else if (invocation.phase == ScriptContactPhase::Stay) script.OnCollisionStay(otherRaw);
                else script.OnCollisionExit(otherRaw);
            }
        });
}

void QueueTriggerEvents(const ObjectContactKey& key, const ObjectContactState& state, bool entered) {
    if (state.triggerA) {
        TriggerEvent event;
        event.trigger_ID = key.idA;
        event.other_ID = key.idB;
        event.entered = entered;
        EventBus::QueueEvent(event);
    }
    if (state.triggerB) {
        TriggerEvent event;
        event.trigger_ID = key.idB;
        event.other_ID = key.idA;
        event.entered = entered;
        EventBus::QueueEvent(event);
    }
}

} // namespace

void PhysicsWorld::Impl::DispatchContacts(
    World& world, const std::map<ObjectContactKey, ObjectContactState>& current) {
    enum class QueuedEvent { None, TriggerEnter, TriggerExit, CollisionEnter };
    struct ContactDispatchPlan {
        ObjectContactKey key;
        ObjectContactState state;
        std::vector<ScriptContactInvocation> scripts;
        QueuedEvent event = QueuedEvent::None;
    };

    // Build the complete plan before invoking user code. Scripts added by an
    // earlier callback do not unexpectedly receive an event already in flight.
    std::vector<ContactDispatchPlan> plans;
    plans.reserve(current.size() + previousContacts.size());
    for (const auto& [key, state] : current) {
        const bool entering = previousContacts.find(key) == previousContacts.end();
        ContactDispatchPlan plan;
        plan.key = key;
        plan.state = state;
        const ScriptContactPhase phase = entering
            ? ScriptContactPhase::Enter : ScriptContactPhase::Stay;
        AppendScriptInvocations(world, key.idA, key.idB, key.trigger, phase, plan.scripts);
        AppendScriptInvocations(world, key.idB, key.idA, key.trigger, phase, plan.scripts);
        if (entering) {
            plan.event = key.trigger ? QueuedEvent::TriggerEnter : QueuedEvent::CollisionEnter;
        }
        plans.push_back(std::move(plan));
    }

    for (const auto& [key, state] : previousContacts) {
        if (current.find(key) != current.end()) continue;
        ContactDispatchPlan plan;
        plan.key = key;
        plan.state = state;
        AppendScriptInvocations(world, key.idA, key.idB, key.trigger,
                                ScriptContactPhase::Exit, plan.scripts);
        AppendScriptInvocations(world, key.idB, key.idA, key.trigger,
                                ScriptContactPhase::Exit, plan.scripts);
        if (key.trigger) plan.event = QueuedEvent::TriggerExit;
        plans.push_back(std::move(plan));
    }

    for (const ContactDispatchPlan& plan : plans) {
        for (const ScriptContactInvocation& invocation : plan.scripts) {
            InvokeScriptContact(world, invocation);
        }
        if (plan.event == QueuedEvent::TriggerEnter) {
            QueueTriggerEvents(plan.key, plan.state, true);
        } else if (plan.event == QueuedEvent::TriggerExit) {
            QueueTriggerEvents(plan.key, plan.state, false);
        } else if (plan.event == QueuedEvent::CollisionEnter) {
            CollisionEvent event;
            event.objectA_ID = plan.key.idA;
            event.objectB_ID = plan.key.idB;
            event.contactPoint = plan.state.point;
            event.normal = plan.state.normal;
            event.penetration = plan.state.penetration;
            EventBus::QueueEvent(event);
        }
    }
    previousContacts = current;
}

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::Synchronize(World& world) {
    impl_->Synchronize(world);
}

void PhysicsWorld::Reset() {
    impl_->ClearBackend();
}

void PhysicsWorld::Step(World& world, float fixedDt) {
    Synchronize(world);
    impl_->ApplyForces(world);
    if (fixedDt > 0.0f) {
        b2World_Step(impl_->worldId, fixedDt,
                     std::max(1, ProjectSettings::Get().substeps));
        impl_->ProcessBackendEvents();
        impl_->PublishBodyState(world);
    }
    impl_->DispatchContacts(world, impl_->AggregateContacts());
}

std::size_t PhysicsWorld::BodyCount() const {
    return impl_->bodies.size();
}

std::size_t PhysicsWorld::ShapeCount() const {
    std::size_t count = 0;
    for (const auto& [_, body] : impl_->bodies) count += body->shapes.size();
    return count;
}

namespace {

b2QueryFilter MakeQueryFilter(int layerMask) {
    b2QueryFilter filter = b2DefaultQueryFilter();
    filter.categoryBits = std::numeric_limits<std::uint32_t>::max();
    filter.maskBits = static_cast<std::uint32_t>(layerMask);
    return filter;
}

std::vector<GameObject*> OrderedObjects(World& world, const std::set<unsigned int>& ids) {
    std::vector<GameObject*> result;
    for (const auto& object : world.Objects()) {
        if (object && object->IsActive() && ids.find(object->GetID()) != ids.end()) {
            result.push_back(object.get());
        }
    }
    return result;
}

struct OverlapContext {
    std::set<unsigned int> ids;
};

bool CollectOverlap(b2ShapeId shapeId, void* context) {
    BackendShapeState* shape = ShapeStateFromId(shapeId);
    if (shape && shape->body) {
        static_cast<OverlapContext*>(context)->ids.insert(shape->body->objectId);
    }
    return true;
}

struct PointContext {
    b2Vec2 point{};
    std::set<unsigned int> ids;
};

bool CollectPoint(b2ShapeId shapeId, void* context) {
    auto* pointContext = static_cast<PointContext*>(context);
    BackendShapeState* shape = ShapeStateFromId(shapeId);
    if (shape && shape->body && b2Shape_TestPoint(shapeId, pointContext->point)) {
        pointContext->ids.insert(shape->body->objectId);
    }
    return true;
}

struct RayContext {
    float closestFraction = 1.0f;
    unsigned int objectId = std::numeric_limits<unsigned int>::max();
    b2ShapeId shapeId = b2_nullShapeId;
    b2Vec2 point{};
    b2Vec2 normal{};
};

float CollectRay(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal,
                 float fraction, void* context) {
    auto* ray = static_cast<RayContext*>(context);
    BackendShapeState* shape = ShapeStateFromId(shapeId);
    const unsigned int objectId = shape && shape->body
        ? shape->body->objectId : std::numeric_limits<unsigned int>::max();
    if (fraction < ray->closestFraction - 1.0e-6f ||
        (Near(fraction, ray->closestFraction, 1.0e-6f) && objectId < ray->objectId)) {
        ray->closestFraction = fraction;
        ray->objectId = objectId;
        ray->shapeId = shapeId;
        ray->point = point;
        ray->normal = normal;
    }
    return ray->closestFraction;
}

} // namespace

RaycastHit2D PhysicsWorld::Raycast(World& world, const Vector2& origin,
                                   const Vector2& direction, float maxDistance,
                                   int layerMask) {
    Synchronize(world);
    RaycastHit2D result;
    if (!IsFinite(origin) || !IsFinite(direction)) return result;
    const Vector2 unit = direction.Normalized();
    if (unit.LengthSquared() == 0.0f || maxDistance < 0.0f || layerMask == 0) return result;

    if (GameObject* inside = OverlapPoint(world, origin, layerMask)) {
        result.hit = true;
        result.collider = inside;
        result.point = origin;
        result.normal = Vector2::Zero();
        result.distance = 0.0f;
        return result;
    }

    float distanceMeters;
    if (!std::isfinite(maxDistance) || maxDistance == std::numeric_limits<float>::max()) {
        distanceMeters = kMaxRayMeters;
    } else {
        distanceMeters = std::min(maxDistance / impl_->ppm, kMaxRayMeters);
    }
    if (distanceMeters <= 0.0f) return result;

    RayContext context;
    b2World_CastRay(impl_->worldId, ToMeters(origin, impl_->ppm),
                    {unit.x * distanceMeters, unit.y * distanceMeters},
                    MakeQueryFilter(layerMask), CollectRay, &context);
    if (B2_IS_NULL(context.shapeId)) return result;
    BackendShapeState* shape = ShapeStateFromId(context.shapeId);
    if (!shape || !shape->body) return result;
    result.hit = true;
    result.collider = world.FindById(shape->body->objectId);
    result.point = ToPixels(context.point, impl_->ppm);
    result.normal = Vector2(context.normal.x, context.normal.y);
    result.distance = distanceMeters * context.closestFraction * impl_->ppm;
    return result;
}

std::vector<GameObject*> PhysicsWorld::OverlapCircleAll(World& world,
                                                        const Vector2& center,
                                                        float radius, int layerMask) {
    Synchronize(world);
    if (!IsFinite(center) || !std::isfinite(radius) || radius < 0.0f || layerMask == 0) return {};
    b2Circle circle{};
    circle.radius = std::max(radius / impl_->ppm, 0.0f);
    OverlapContext context;
    b2World_OverlapCircle(impl_->worldId, &circle,
                          {ToMeters(center, impl_->ppm), b2MakeRot(0.0f)},
                          MakeQueryFilter(layerMask), CollectOverlap, &context);
    return OrderedObjects(world, context.ids);
}

std::vector<GameObject*> PhysicsWorld::OverlapBoxAll(World& world,
                                                     const Vector2& center,
                                                     const Vector2& halfExtents,
                                                     int layerMask) {
    Synchronize(world);
    if (!IsFinite(center) || !IsFinite(halfExtents) || halfExtents.x < 0.0f ||
        halfExtents.y < 0.0f || layerMask == 0) return {};
    const b2Polygon polygon = b2MakeBox(
        std::max(halfExtents.x / impl_->ppm, 1.0e-6f),
        std::max(halfExtents.y / impl_->ppm, 1.0e-6f));
    OverlapContext context;
    b2World_OverlapPolygon(impl_->worldId, &polygon,
                           {ToMeters(center, impl_->ppm), b2MakeRot(0.0f)},
                           MakeQueryFilter(layerMask), CollectOverlap, &context);
    return OrderedObjects(world, context.ids);
}

GameObject* PhysicsWorld::OverlapPoint(World& world, const Vector2& point, int layerMask) {
    Synchronize(world);
    if (!IsFinite(point) || layerMask == 0) return nullptr;
    PointContext context;
    context.point = ToMeters(point, impl_->ppm);
    const float epsilon = 0.0001f;
    b2AABB aabb;
    aabb.lowerBound = {context.point.x - epsilon, context.point.y - epsilon};
    aabb.upperBound = {context.point.x + epsilon, context.point.y + epsilon};
    b2World_OverlapAABB(impl_->worldId, aabb, MakeQueryFilter(layerMask),
                        CollectPoint, &context);
    const std::vector<GameObject*> ordered = OrderedObjects(world, context.ids);
    return ordered.empty() ? nullptr : ordered.front();
}
