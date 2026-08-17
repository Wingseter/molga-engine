#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/CircleCollider2D.h"
#include "Scripting/Script.h"
#include "Physics/PhysicsWorld.h"
#include "Core/EventBus.h"
#include "Core/Events/PhysicsEvents.h"
#include "Core/ProjectSettings.h"
#include "doctest.h"
#include <memory>
#include <iostream>
#include <cmath>

// Mock script to verify physics callbacks
class MockPhysicsScript : public Script {
public:
    SCRIPT_CLASS(MockPhysicsScript)

    int collisionEnter = 0;
    int collisionStay = 0;
    int collisionExit = 0;
    
    int triggerEnter = 0;
    int triggerStay = 0;
    int triggerExit = 0;

    GameObject* lastOther = nullptr;

    void OnCollisionEnter(GameObject* other) override {
        collisionEnter++;
        lastOther = other;
    }
    void OnCollisionStay(GameObject* other) override {
        collisionStay++;
        lastOther = other;
    }
    void OnCollisionExit(GameObject* other) override {
        collisionExit++;
        lastOther = other;
    }

    void OnTriggerEnter(GameObject* other) override {
        triggerEnter++;
        lastOther = other;
    }
    void OnTriggerStay(GameObject* other) override {
        triggerStay++;
        lastOther = other;
    }
    void OnTriggerExit(GameObject* other) override {
        triggerExit++;
        lastOther = other;
    }

    void Reset() {
        collisionEnter = 0;
        collisionStay = 0;
        collisionExit = 0;
        triggerEnter = 0;
        triggerStay = 0;
        triggerExit = 0;
        lastOther = nullptr;
    }
};

namespace {

int gReplacementTriggerEnter = 0;
int gReplacementTriggerStay = 0;
int gMutationTriggerEnter = 0;

class CallbackReplacementScript : public Script {
public:
    SCRIPT_CLASS(CallbackReplacementScript)

    void OnTriggerEnter(GameObject*) override { ++gReplacementTriggerEnter; }
    void OnTriggerStay(GameObject*) override { ++gReplacementTriggerStay; }
};

class CallbackMutationScript : public Script {
public:
    SCRIPT_CLASS(CallbackMutationScript)

    void OnTriggerEnter(GameObject*) override {
        ++gMutationTriggerEnter;
        GameObject* owner = gameObject;
        owner->RemoveComponent<CallbackReplacementScript>();
        owner->AddComponentRaw(new CallbackReplacementScript());
        // This intentionally destroys the currently executing script. The
        // physics dispatcher must not dereference it after the callback.
        owner->RemoveComponent<CallbackMutationScript>();
    }
};

int gDestroyOtherTriggerEnter = 0;
int gRemovedObjectTriggerEnter = 0;
int gRemovedObjectDestructed = 0;

class RemovedObjectPhysicsScript : public Script {
public:
    SCRIPT_CLASS(RemovedObjectPhysicsScript)

    ~RemovedObjectPhysicsScript() override { ++gRemovedObjectDestructed; }
    void OnTriggerEnter(GameObject*) override { ++gRemovedObjectTriggerEnter; }
};

class DestroyOtherPhysicsScript : public Script {
public:
    SCRIPT_CLASS(DestroyOtherPhysicsScript)

    void OnTriggerEnter(GameObject* other) override {
        ++gDestroyOtherTriggerEnter;
        World* world = GetWorld();
        if (!world) return;
        world->Destroy(other);
        // Exercise immediate queue flushing from inside a callback. Dispatch
        // still has a later callback planned for `other`.
        world->FlushDeferred(0.0f);
    }
};

} // namespace

TEST_CASE("Physics: Gravity Integration") {
    World world;
    auto go = std::make_shared<GameObject>("DynamicBody");
    auto* transform = go->AddComponent<Transform>();
    transform->SetPosition(0.0f, 0.0f);
    
    auto* rb = go->AddComponent<Rigidbody2D>();
    rb->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    rb->SetGravityScale(1.0f);
    rb->SetMass(1.0f);
    
    world.Add(go);
    
    // Simulate one step
    float dt = 0.1f;
    world.FixedStep(dt);
    
    // Gravity = 981 pixels/s^2, mass = 1.0f, dt = 0.1s
    // Velocity expected: 0 + 981 * 1 * 0.1 = 98.1 pixels/s
    // Box2D integrates each of the configured four substeps semi-implicitly.
    // Position = g * (dt / n)^2 * (1 + ... + n) = 6.13125 pixels.
    CHECK(rb->GetVelocity().y == doctest::Approx(98.1f));
    CHECK(transform->GetY() == doctest::Approx(6.13125f));
}

TEST_CASE("Physics: Collision Detection & Resolution (Box-Box)") {
    World world;
    
    // Object A: Static platform
    auto goA = std::make_shared<GameObject>("Platform");
    auto* transA = goA->AddComponent<Transform>();
    transA->SetPosition(0.0f, 100.0f);
    auto* colA = goA->AddComponent<BoxCollider2D>();
    colA->SetSize(100.0f, 20.0f);
    
    auto* rbA = goA->AddComponent<Rigidbody2D>();
    rbA->SetBodyType(Rigidbody2D::BodyType::Static);
    
    // Object B: Dynamic falling box, positioned overlapping the platform
    auto goB = std::make_shared<GameObject>("Box");
    auto* transB = goB->AddComponent<Transform>();
    transB->SetPosition(0.0f, 90.0f); // Centers are at 100 and 90. Box size is 20, Platform size is 20.
    // Platform bounds: x = [-50, 50] (if size is 100, offset is 0, bounds are [x, x+width]),
    // Wait, BoxCollider2D bounds are calculated as:
    // aabb.x = worldPos.x + offset.x * scale.x; (so top-left is at worldPos.x)
    // BoxCollider2D size is 100x20. So goA bounds are x = [0, 100], y = [100, 120].
    // Let's set sizes and positions carefully.
    // Let's set sizes:
    // colA size = 100, 20. transA pos = 0, 100. Bounds A: x = [0, 100], y = [100, 120]
    // colB size = 20, 20. transB pos = 40, 90. Bounds B: x = [40, 60], y = [90, 110]
    // Y-overlap: [100, 110] (amount = 10.0f).
    // Box B is dynamic and should be pushed UP (negative Y direction, away from platform)
    
    auto* colB = goB->AddComponent<BoxCollider2D>();
    colB->SetSize(20.0f, 20.0f);
    
    auto* rbB = goB->AddComponent<Rigidbody2D>();
    rbB->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    rbB->SetGravityScale(0.0f); // No gravity for this test
    rbB->SetVelocity(Vector2(0.0f, 10.0f)); // Moving downwards into platform
    
    world.Add(goA);
    world.Add(goB);
    
    world.FixedStep(0.1f);
    
    // Box2D corrects deep initial penetration over solver iterations instead of
    // applying the legacy solver's one-frame minimum-translation snap.
    CHECK(transB->GetY() < 91.0f);
    
    // Velocity resolution: B was moving at (0, 10), should bounce/stop on static platform
    CHECK(rbB->GetVelocity().y <= 0.0f);

    for (int i = 0; i < 30; ++i) world.FixedStep(1.0f / 60.0f);
    CHECK(transB->GetY() <= 80.5f);
}

TEST_CASE("Physics: Collision Callbacks Enter/Stay/Exit") {
    World world;
    
    auto goA = std::make_shared<GameObject>("A");
    auto* transA = goA->AddComponent<Transform>();
    transA->SetPosition(0.0f, 0.0f);
    auto* colA = goA->AddComponent<BoxCollider2D>();
    colA->SetSize(20.0f, 20.0f);
    
    auto goB = std::make_shared<GameObject>("B");
    auto* transB = goB->AddComponent<Transform>();
    transB->SetPosition(100.0f, 100.0f); // far away
    auto* colB = goB->AddComponent<BoxCollider2D>();
    colB->SetSize(20.0f, 20.0f);
    
    auto* rbB = goB->AddComponent<Rigidbody2D>();
    rbB->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    rbB->SetGravityScale(0.0f);
    
    auto* script = new MockPhysicsScript();
    goA->AddComponentRaw(script);
    
    world.Add(goA);
    world.Add(goB);
    
    // Frame 1: No collision
    world.FixedStep(0.1f);
    CHECK(script->collisionEnter == 0);
    CHECK(script->collisionStay == 0);
    CHECK(script->collisionExit == 0);
    
    // Frame 2: Move B so they overlap
    transB->SetPosition(10.0f, 10.0f);
    world.FixedStep(0.1f);
    CHECK(script->collisionEnter == 1);
    CHECK(script->collisionStay == 0);
    CHECK(script->collisionExit == 0);
    CHECK(script->lastOther == goB.get());
    
    // Frame 3: Stay overlapping (force overlap again since resolution pushed them apart)
    transB->SetPosition(10.0f, 10.0f);
    world.FixedStep(0.1f);
    CHECK(script->collisionEnter == 1);
    CHECK(script->collisionStay == 1);
    CHECK(script->collisionExit == 0);
    
    // Frame 4: Move B away
    transB->SetPosition(100.0f, 100.0f);
    world.FixedStep(0.1f);
    CHECK(script->collisionEnter == 1);
    CHECK(script->collisionStay == 1);
    CHECK(script->collisionExit == 1);
}

TEST_CASE("Physics: Trigger Callbacks") {
    World world;
    
    auto goA = std::make_shared<GameObject>("A");
    auto* transA = goA->AddComponent<Transform>();
    transA->SetPosition(0.0f, 0.0f);
    auto* colA = goA->AddComponent<BoxCollider2D>();
    colA->SetSize(20.0f, 20.0f);
    colA->SetTrigger(true); // TRIGGER
    
    auto goB = std::make_shared<GameObject>("B");
    auto* transB = goB->AddComponent<Transform>();
    transB->SetPosition(10.0f, 10.0f);
    auto* colB = goB->AddComponent<BoxCollider2D>();
    colB->SetSize(20.0f, 20.0f);
    
    auto* rbB = goB->AddComponent<Rigidbody2D>();
    rbB->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    rbB->SetGravityScale(0.0f);
    
    auto* script = new MockPhysicsScript();
    goA->AddComponentRaw(script);
    
    world.Add(goA);
    world.Add(goB);
    
    // Frame 1: Trigger overlap
    world.FixedStep(0.1f);
    CHECK(script->triggerEnter == 1);
    CHECK(script->triggerStay == 0);
    CHECK(script->triggerExit == 0);
    
    // Since colA is a trigger, NO penetration resolution should happen
    // transB should remain at 10.0f
    CHECK(transB->GetX() == doctest::Approx(10.0f));
    CHECK(transB->GetY() == doctest::Approx(10.0f));
    
    // Frame 2: Stay overlap
    world.FixedStep(0.1f);
    CHECK(script->triggerEnter == 1);
    CHECK(script->triggerStay == 1);
    CHECK(script->triggerExit == 0);
    
    // Frame 3: Exit trigger
    transB->SetPosition(100.0f, 100.0f);
    world.FixedStep(0.1f);
    CHECK(script->triggerEnter == 1);
    CHECK(script->triggerStay == 1);
    CHECK(script->triggerExit == 1);
}

TEST_CASE("Physics: callback dispatch survives component removal and same-type replacement") {
    ProjectSettings::Get().SetDefaults();
    gReplacementTriggerEnter = 0;
    gReplacementTriggerStay = 0;
    gMutationTriggerEnter = 0;

    World world;
    auto trigger = std::make_shared<GameObject>("Mutating trigger");
    trigger->AddComponent<Transform>();
    auto* triggerCollider = trigger->AddComponent<BoxCollider2D>();
    triggerCollider->SetSize(20.0f, 20.0f);
    triggerCollider->SetTrigger(true);
    trigger->AddComponentRaw(new CallbackMutationScript());
    trigger->AddComponentRaw(new CallbackReplacementScript());
    world.Add(trigger);

    auto visitor = std::make_shared<GameObject>("Visitor");
    visitor->AddComponent<Transform>()->SetPosition(10.0f, 10.0f);
    visitor->AddComponent<BoxCollider2D>()->SetSize(20.0f, 20.0f);
    auto* visitorBody = visitor->AddComponent<Rigidbody2D>();
    visitorBody->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    visitorBody->SetGravityScale(0.0f);
    world.Add(visitor);

    world.FixedStep(1.0f / 60.0f);
    CHECK(gMutationTriggerEnter == 1);
    CHECK(gReplacementTriggerEnter == 0);
    CHECK_FALSE(trigger->HasComponent<CallbackMutationScript>());
    CHECK(trigger->HasComponent<CallbackReplacementScript>());

    // The replacement was not part of the enter-event snapshot, but is
    // eligible on the following stay event.
    world.FixedStep(1.0f / 60.0f);
    CHECK(gReplacementTriggerEnter == 0);
    CHECK(gReplacementTriggerStay == 1);
}

TEST_CASE("Physics: callback dispatch survives immediate removal of the other object") {
    ProjectSettings::Get().SetDefaults();
    gDestroyOtherTriggerEnter = 0;
    gRemovedObjectTriggerEnter = 0;
    gRemovedObjectDestructed = 0;

    World world;
    auto trigger = std::make_shared<GameObject>("Destroying trigger");
    trigger->AddComponent<Transform>();
    auto* triggerCollider = trigger->AddComponent<BoxCollider2D>();
    triggerCollider->SetSize(20.0f, 20.0f);
    triggerCollider->SetTrigger(true);
    trigger->AddComponentRaw(new DestroyOtherPhysicsScript());
    world.Add(trigger);

    auto visitor = std::make_shared<GameObject>("Removed visitor");
    visitor->AddComponent<Transform>()->SetPosition(10.0f, 10.0f);
    visitor->AddComponent<BoxCollider2D>()->SetSize(20.0f, 20.0f);
    auto* visitorBody = visitor->AddComponent<Rigidbody2D>();
    visitorBody->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    visitorBody->SetGravityScale(0.0f);
    visitor->AddComponentRaw(new RemovedObjectPhysicsScript());
    const unsigned int visitorId = visitor->GetID();
    world.Add(visitor);
    visitor.reset();

    world.FixedStep(1.0f / 60.0f);
    CHECK(gDestroyOtherTriggerEnter == 1);
    CHECK(gRemovedObjectTriggerEnter == 0);
    CHECK(world.FindById(visitorId) == nullptr);
    CHECK(gRemovedObjectDestructed == 1);

    // Removes the stale backend body and exercises the exit plan without the
    // ECS object being present. This is especially useful under ASan.
    world.FixedStep(0.0f);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 1);
}

TEST_CASE("Physics: Rigidbody2D deserialize clamps non-positive mass") {
    // 손상/수동편집 씬의 mass:0 이 PhysicsWorld의 force/mass 에서 NaN을 만들지 않도록
    // Deserialize는 SetMass()를 거쳐 mass > 0 불변식을 유지해야 한다.
    Rigidbody2D rb;
    nlohmann::json j;
    j["bodyType"] = static_cast<int>(Rigidbody2D::BodyType::Dynamic);
    j["mass"] = 0.0f;
    rb.Deserialize(j);
    CHECK(rb.GetMass() > 0.0f);

    // 실제 스텝에서도 위치가 NaN이 되지 않는지 확인
    World world;
    auto go = std::make_shared<GameObject>("Zero Mass");
    auto* transform = go->AddComponent<Transform>();
    transform->SetPosition(0.0f, 0.0f);
    auto* body = go->AddComponent<Rigidbody2D>();
    body->Deserialize(j);
    world.Add(go);
    world.FixedStep(0.1f);
    CHECK(std::isfinite(transform->GetY()));
}

TEST_CASE("Physics: Collision matrix enabled by default for arbitrary layers") {
    // 기본 충돌 매트릭스는 모든 레이어 쌍을 활성화해야 한다.
    // (이름 없는 새 레이어에 오브젝트를 두었을 때 '조용한 무충돌'을 방지)
    ProjectSettings::Get().SetDefaults();
    CHECK(ProjectSettings::Get().IsCollisionEnabled(0, 0));
    CHECK(ProjectSettings::Get().IsCollisionEnabled(6, 9));   // 이름 없는 레이어
    CHECK(ProjectSettings::Get().IsCollisionEnabled(31, 31));
}

TEST_CASE("Physics: Circle-Circle Collision") {
    World world;

    auto goA = std::make_shared<GameObject>("CircleA");
    auto* transA = goA->AddComponent<Transform>();
    transA->SetPosition(10.0f, 10.0f);
    auto* colA = goA->AddComponent<CircleCollider2D>();
    colA->SetRadius(10.0f);

    auto goB = std::make_shared<GameObject>("CircleB");
    auto* transB = goB->AddComponent<Transform>();
    transB->SetPosition(25.0f, 10.0f);
    auto* colB = goB->AddComponent<CircleCollider2D>();
    colB->SetRadius(10.0f);

    auto* rbB = goB->AddComponent<Rigidbody2D>();
    rbB->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    rbB->SetGravityScale(0.0f);

    auto* script = new MockPhysicsScript();
    goA->AddComponentRaw(script);

    world.Add(goA);
    world.Add(goB);

    world.FixedStep(0.1f);
    
    // Box2D moves the dynamic circle out over solver iterations.
    CHECK(transB->GetX() > 25.0f);
    CHECK(script->collisionEnter == 1);

    for (int i = 0; i < 30; ++i) world.FixedStep(1.0f / 60.0f);
    CHECK(transB->GetX() >= 29.5f);
}

TEST_CASE("Physics: P0 settings and component state round-trip") {
    ProjectSettings settings;
    settings.gravity = Vector2(12.0f, 345.0f);
    settings.pixelsPerMeter = 64.0f;
    settings.substeps = 7;

    ProjectSettings loaded;
    loaded.Deserialize(settings.Serialize());
    CHECK(loaded.gravity.x == doctest::Approx(12.0f));
    CHECK(loaded.gravity.y == doctest::Approx(345.0f));
    CHECK(loaded.pixelsPerMeter == doctest::Approx(64.0f));
    CHECK(loaded.substeps == 7);

    BoxCollider2D collider;
    collider.SetFriction(0.75f);
    collider.SetRestitution(0.6f);
    nlohmann::json colliderJson;
    collider.Serialize(colliderJson);
    BoxCollider2D loadedCollider;
    loadedCollider.Deserialize(colliderJson);
    CHECK(loadedCollider.GetFriction() == doctest::Approx(0.75f));
    CHECK(loadedCollider.GetRestitution() == doctest::Approx(0.6f));

    Rigidbody2D body;
    body.SetBodyType(Rigidbody2D::BodyType::Dynamic);
    body.SetAngularVelocity(123.0f);
    body.SetAngularDamping(2.5f);
    nlohmann::json bodyJson;
    body.Serialize(bodyJson);
    Rigidbody2D loadedBody;
    loadedBody.Deserialize(bodyJson);
    CHECK(loadedBody.GetAngularVelocity() == doctest::Approx(123.0f));
    CHECK(loadedBody.GetAngularDamping() == doctest::Approx(2.5f));
}

TEST_CASE("Physics: Transform world setters invert parent transforms") {
    auto parent = std::make_shared<GameObject>("Parent");
    auto* parentTransform = parent->AddComponent<Transform>();
    parentTransform->SetPosition(50.0f, -20.0f);
    parentTransform->SetRotation(35.0f);
    parentTransform->SetScale(2.0f, 3.0f);

    auto child = std::make_shared<GameObject>("Child");
    auto* childTransform = child->AddComponent<Transform>();
    REQUIRE(child->SetParent(parent.get()));

    childTransform->SetWorldPosition(Vector2(123.0f, 77.0f));
    childTransform->SetWorldRotation(-15.0f);
    CHECK(childTransform->GetWorldPosition().x == doctest::Approx(123.0f));
    CHECK(childTransform->GetWorldPosition().y == doctest::Approx(77.0f));
    CHECK(childTransform->GetWorldRotation() == doctest::Approx(-15.0f));
}

TEST_CASE("Physics: backend bodies and shapes are synchronized incrementally") {
    ProjectSettings::Get().SetDefaults();
    World world;
    auto object = std::make_shared<GameObject>("MutableBody");
    object->AddComponent<Transform>();
    auto* body = object->AddComponent<Rigidbody2D>();
    body->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    body->SetGravityScale(0.0f);
    object->AddComponent<BoxCollider2D>();
    auto* circle = object->AddComponent<CircleCollider2D>();
    world.Add(object);

    world.FixedStep(1.0f / 60.0f);
    REQUIRE(world.GetPhysicsWorld() != nullptr);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 1);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 2);

    for (int i = 0; i < 20; ++i) world.FixedStep(1.0f / 60.0f);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 1);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 2);

    circle->SetEnabled(false);
    world.FixedStep(1.0f / 60.0f);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 1);

    object->RemoveComponent<BoxCollider2D>();
    world.FixedStep(1.0f / 60.0f);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 1);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 0);

    object->SetActive(false);
    world.FixedStep(1.0f / 60.0f);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 0);

    object->SetActive(true);
    world.FixedStep(1.0f / 60.0f);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 1);

    object->RemoveComponent<Rigidbody2D>();
    world.FixedStep(1.0f / 60.0f);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 0);
}

TEST_CASE("Physics: angular state is simulated for parented rigidbodies") {
    ProjectSettings::Get().SetDefaults();
    World world;

    auto parent = std::make_shared<GameObject>("Parent");
    auto* parentTransform = parent->AddComponent<Transform>();
    parentTransform->SetPosition(50.0f, 20.0f);
    parentTransform->SetRotation(30.0f);
    parentTransform->SetScale(2.0f, 2.0f);
    world.Add(parent);

    auto child = std::make_shared<GameObject>("ChildBody");
    auto* transform = child->AddComponent<Transform>();
    transform->SetPosition(10.0f, 0.0f);
    auto* collider = child->AddComponent<BoxCollider2D>();
    collider->SetSize(20.0f, 20.0f);
    collider->SetOffset(-10.0f, -10.0f);
    auto* body = child->AddComponent<Rigidbody2D>();
    body->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    body->SetGravityScale(0.0f);
    body->SetAngularVelocity(90.0f);
    world.Add(child);
    REQUIRE(child->SetParent(parent.get()));

    world.FixedStep(0.1f);
    CHECK(transform->GetWorldRotation() > 35.0f);
    CHECK(body->GetAngularVelocity() == doctest::Approx(90.0f).epsilon(0.01));

    body->SetAngularVelocity(0.0f);
    transform->SetWorldPosition(Vector2(300.0f, 200.0f));
    world.FixedStep(1.0f / 60.0f);
    CHECK(transform->GetWorldPosition().x == doctest::Approx(300.0f).epsilon(0.001));
    CHECK(transform->GetWorldPosition().y == doctest::Approx(200.0f).epsilon(0.001));
}

TEST_CASE("Physics: restitution is applied by Box2D materials") {
    ProjectSettings::Get().SetDefaults();
    World world;

    auto platform = std::make_shared<GameObject>("Platform");
    platform->AddComponent<Transform>()->SetPosition(0.0f, 100.0f);
    auto* platformCollider = platform->AddComponent<BoxCollider2D>();
    platformCollider->SetSize(200.0f, 20.0f);
    platformCollider->SetRestitution(1.0f);
    world.Add(platform);

    auto ball = std::make_shared<GameObject>("Ball");
    ball->AddComponent<Transform>()->SetPosition(50.0f, 60.0f);
    auto* ballCollider = ball->AddComponent<CircleCollider2D>();
    ballCollider->SetRadius(10.0f);
    ballCollider->SetRestitution(1.0f);
    auto* body = ball->AddComponent<Rigidbody2D>();
    body->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    body->SetGravityScale(0.0f);
    body->SetVelocity(Vector2(0.0f, 300.0f));
    world.Add(ball);

    bool bouncedUp = false;
    for (int i = 0; i < 60; ++i) {
        world.FixedStep(1.0f / 120.0f);
        if (body->GetVelocity().y < -100.0f) bouncedUp = true;
    }
    CHECK(bouncedUp);
}

TEST_CASE("Physics: multiple backend shape contacts emit one object-pair callback") {
    ProjectSettings::Get().SetDefaults();
    World world;

    auto composite = std::make_shared<GameObject>("Composite");
    composite->AddComponent<Transform>();
    auto* box = composite->AddComponent<BoxCollider2D>();
    box->SetOffset(-20.0f, -20.0f);
    box->SetSize(40.0f, 40.0f);
    composite->AddComponent<CircleCollider2D>()->SetRadius(20.0f);
    auto* script = new MockPhysicsScript();
    composite->AddComponentRaw(script);
    world.Add(composite);

    auto mover = std::make_shared<GameObject>("Mover");
    auto* moverTransform = mover->AddComponent<Transform>();
    moverTransform->SetPosition(10.0f, 0.0f);
    mover->AddComponent<CircleCollider2D>()->SetRadius(5.0f);
    auto* moverBody = mover->AddComponent<Rigidbody2D>();
    moverBody->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    moverBody->SetGravityScale(0.0f);
    world.Add(mover);

    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionEnter == 1);

    moverTransform->SetPosition(10.0f, 0.0f);
    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionStay == 1);

    moverTransform->SetPosition(200.0f, 0.0f);
    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionExit == 1);
}

TEST_CASE("Physics: collision matrix and runtime layer changes reach Box2D") {
    ProjectSettings::Get().SetDefaults();
    ProjectSettings::Get().SetCollisionEnabled(0, 1, false);
    World world;

    auto fixed = std::make_shared<GameObject>("Fixed");
    fixed->SetLayer(0);
    fixed->AddComponent<Transform>();
    fixed->AddComponent<BoxCollider2D>()->SetSize(20.0f, 20.0f);
    auto* script = new MockPhysicsScript();
    fixed->AddComponentRaw(script);
    world.Add(fixed);

    auto moving = std::make_shared<GameObject>("Moving");
    moving->SetLayer(1);
    auto* movingTransform = moving->AddComponent<Transform>();
    movingTransform->SetPosition(10.0f, 10.0f);
    moving->AddComponent<BoxCollider2D>()->SetSize(20.0f, 20.0f);
    auto* body = moving->AddComponent<Rigidbody2D>();
    body->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    body->SetGravityScale(0.0f);
    world.Add(moving);

    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionEnter == 0);
    CHECK(movingTransform->GetX() == doctest::Approx(10.0f));
    CHECK(movingTransform->GetY() == doctest::Approx(10.0f));

    moving->SetLayer(0);
    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionEnter == 1);

    ProjectSettings::Get().SetCollisionEnabled(0, 0, false);
    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionExit == 1);

    ProjectSettings::Get().SetCollisionEnabled(0, 0, true);
    movingTransform->SetPosition(10.0f, 10.0f);
    world.FixedStep(1.0f / 60.0f);
    CHECK(script->collisionEnter == 2);

    ProjectSettings::Get().SetDefaults();
}

TEST_CASE("Physics: trigger EventBus identifies the sensor owner on enter and exit") {
    ProjectSettings::Get().SetDefaults();
    EventBus::Clear();
    World world;

    auto trigger = std::make_shared<GameObject>("Trigger");
    trigger->AddComponent<Transform>();
    auto* triggerCollider = trigger->AddComponent<BoxCollider2D>();
    triggerCollider->SetSize(30.0f, 30.0f);
    triggerCollider->SetTrigger(true);
    world.Add(trigger);

    auto visitor = std::make_shared<GameObject>("Visitor");
    auto* visitorTransform = visitor->AddComponent<Transform>();
    visitorTransform->SetPosition(10.0f, 10.0f);
    visitor->AddComponent<BoxCollider2D>()->SetSize(10.0f, 10.0f);
    auto* visitorBody = visitor->AddComponent<Rigidbody2D>();
    visitorBody->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    visitorBody->SetGravityScale(0.0f);
    world.Add(visitor);

    std::vector<TriggerEvent> events;
    ScopedSubscription subscription(EventBus::Subscribe<TriggerEvent>(
        [&](TriggerEvent& event) { events.push_back(event); }));

    world.FixedStep(1.0f / 60.0f);
    EventBus::ProcessQueue();
    REQUIRE(events.size() == 1);
    CHECK(events[0].trigger_ID == trigger->GetID());
    CHECK(events[0].other_ID == visitor->GetID());
    CHECK(events[0].entered);

    visitorTransform->SetPosition(100.0f, 100.0f);
    world.FixedStep(1.0f / 60.0f);
    EventBus::ProcessQueue();
    REQUIRE(events.size() == 2);
    CHECK(events[1].trigger_ID == trigger->GetID());
    CHECK(events[1].other_ID == visitor->GetID());
    CHECK_FALSE(events[1].entered);
    EventBus::Clear();
}

TEST_CASE("Physics: rotated boxes resolve along their actual slope") {
    ProjectSettings::Get().SetDefaults();
    World world;

    auto slope = std::make_shared<GameObject>("Slope");
    auto* slopeTransform = slope->AddComponent<Transform>();
    slopeTransform->SetPosition(200.0f, 200.0f);
    slopeTransform->SetRotation(30.0f);
    auto* slopeCollider = slope->AddComponent<BoxCollider2D>();
    slopeCollider->SetOffset(-100.0f, -10.0f);
    slopeCollider->SetSize(200.0f, 20.0f);
    world.Add(slope);

    auto ball = std::make_shared<GameObject>("Ball");
    auto* ballTransform = ball->AddComponent<Transform>();
    ballTransform->SetPosition(207.5f, 187.0f);
    ball->AddComponent<CircleCollider2D>()->SetRadius(10.0f);
    auto* ballBody = ball->AddComponent<Rigidbody2D>();
    ballBody->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    ballBody->SetGravityScale(0.0f);
    world.Add(ball);

    world.FixedStep(1.0f / 60.0f);
    CHECK(ballTransform->GetX() > 207.5f);
    CHECK(ballTransform->GetY() < 187.0f);
}
