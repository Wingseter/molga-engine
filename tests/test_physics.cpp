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
    // Position expected: 0 + 98.1 * 0.1 = 9.81 pixels
    CHECK(rb->GetVelocity().y == doctest::Approx(98.1f));
    CHECK(transform->GetY() == doctest::Approx(9.81f));
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
    
    // Penetration resolution should push Box B up by 10.0f so it is no longer overlapping
    // Because Platform A is Static (inf mass) and Box B is Dynamic.
    // Original y of transB was 90.0f. It moved by velocity (10.0f * 0.1 = 1.0f) to 91.0f.
    // Overlap at 91.0f: Platform is [100, 120], Box is [91, 111], overlap = 11.0f.
    // Resolution should subtract 11.0f, making y = 80.0f.
    CHECK(transB->GetY() == doctest::Approx(80.0f));
    
    // Velocity resolution: B was moving at (0, 10), should bounce/stop on static platform
    CHECK(rbB->GetVelocity().y <= 0.0f);
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
    
    // Dist centers = 15. Radius sum = 20. Overlap = 5.
    // Since B is Dynamic and A is Static: B should be pushed right (positive X) by 5.
    // transB original x was 25. New x should be 30.
    CHECK(transB->GetX() == doctest::Approx(30.0f));
    CHECK(script->collisionEnter == 1);
}
