#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Collider2D.h"
#include "ECS/Components/BoxCollider2D.h"
#include "doctest.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

// ── GameObject basics ────────────────────────────────────────────────────────

TEST_CASE("GameObject: creation") {
    auto obj = std::make_shared<GameObject>("Player");
    CHECK(obj->GetName() == "Player");
    CHECK(obj->IsActive());
    CHECK(obj->GetID() > 0);
}

TEST_CASE("GameObject: name") {
    auto obj = std::make_shared<GameObject>();
    CHECK(obj->GetName() == "GameObject");
    obj->SetName("Enemy");
    CHECK(obj->GetName() == "Enemy");
}

TEST_CASE("GameObject: active") {
    auto obj = std::make_shared<GameObject>();
    CHECK(obj->IsActive());
    obj->SetActive(false);
    CHECK(!obj->IsActive());
}

TEST_CASE("GameObject: unique IDs") {
    auto a = std::make_shared<GameObject>();
    auto b = std::make_shared<GameObject>();
    CHECK(a->GetID() != b->GetID());
}

// ── Component management ─────────────────────────────────────────────────────

TEST_CASE("ECS: add and get component") {
    auto obj = std::make_shared<GameObject>("Test");
    Transform* t = obj->AddComponent<Transform>(10.0f, 20.0f);
    REQUIRE(t != nullptr);
    CHECK(t->GetX() == doctest::Approx(10.0f));
    CHECK(t->GetY() == doctest::Approx(20.0f));

    Transform* got = obj->GetComponent<Transform>();
    CHECK(got == t);
}

TEST_CASE("ECS: has component") {
    auto obj = std::make_shared<GameObject>();
    CHECK(!obj->HasComponent<Transform>());
    obj->AddComponent<Transform>();
    CHECK(obj->HasComponent<Transform>());
}

TEST_CASE("ECS: remove component") {
    auto obj = std::make_shared<GameObject>();
    obj->AddComponent<Transform>();
    CHECK(obj->HasComponent<Transform>());

    obj->RemoveComponent<Transform>();
    CHECK(!obj->HasComponent<Transform>());
}

TEST_CASE("ECS: multiple components") {
    auto obj = std::make_shared<GameObject>();
    obj->AddComponent<Transform>(5.0f, 5.0f);
    obj->AddComponent<BoxCollider2D>(32.0f, 32.0f);

    CHECK(obj->HasComponent<Transform>());
    CHECK(obj->HasComponent<BoxCollider2D>());

    auto components = obj->GetComponents();
    CHECK(components.size() == 2);
}

// ── Transform ────────────────────────────────────────────────────────────────

TEST_CASE("Transform: position") {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    CHECK(t->GetX() == doctest::Approx(0.0f));
    CHECK(t->GetY() == doctest::Approx(0.0f));

    t->SetPosition(100.0f, 200.0f);
    CHECK(t->GetX() == doctest::Approx(100.0f));
    CHECK(t->GetY() == doctest::Approx(200.0f));

    t->Translate(10.0f, -5.0f);
    CHECK(t->GetX() == doctest::Approx(110.0f));
    CHECK(t->GetY() == doctest::Approx(195.0f));
}

TEST_CASE("Transform: rotation") {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    CHECK(t->GetRotation() == doctest::Approx(0.0f));

    t->SetRotation(45.0f);
    CHECK(t->GetRotation() == doctest::Approx(45.0f));
}

TEST_CASE("Transform: scale") {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    CHECK(t->GetScale().x == doctest::Approx(1.0f));
    CHECK(t->GetScale().y == doctest::Approx(1.0f));

    t->SetScale(2.0f, 3.0f);
    CHECK(t->GetScale().x == doctest::Approx(2.0f));
    CHECK(t->GetScale().y == doctest::Approx(3.0f));

    t->SetScale(0.5f);
    CHECK(t->GetScale().x == doctest::Approx(0.5f));
    CHECK(t->GetScale().y == doctest::Approx(0.5f));
}

TEST_CASE("Transform: type name") {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    CHECK(t->GetTypeName() == "Transform");
}

// ── Parent-child hierarchy ───────────────────────────────────────────────────

TEST_CASE("Hierarchy: parent-child connection") {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child = std::make_shared<GameObject>("Child");

    parent->AddChild(child.get());

    CHECK(child->GetParent() == parent.get());
    REQUIRE(parent->GetChildren().size() == 1);
    CHECK(parent->GetChildren()[0] == child.get());
}

TEST_CASE("Hierarchy: remove child") {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child = std::make_shared<GameObject>("Child");

    parent->AddChild(child.get());
    parent->RemoveChild(child.get());

    CHECK(child->GetParent() == nullptr);
    CHECK(parent->GetChildren().empty());
}

TEST_CASE("Hierarchy: world transform propagation") {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child = std::make_shared<GameObject>("Child");

    Transform* pt = parent->AddComponent<Transform>(100.0f, 200.0f);
    pt->SetScale(2.0f, 2.0f);

    Transform* ct = child->AddComponent<Transform>(10.0f, 20.0f);

    parent->AddChild(child.get());

    // World position = parent_pos + child_local * parent_scale (no rotation)
    Vector2 worldPos = ct->GetWorldPosition();
    CHECK(worldPos.x == doctest::Approx(120.0f));  // 100 + 10*2
    CHECK(worldPos.y == doctest::Approx(240.0f));  // 200 + 20*2

    // World scale = parent_scale * child_scale
    Vector2 worldScale = ct->GetWorldScale();
    CHECK(worldScale.x == doctest::Approx(2.0f));
    CHECK(worldScale.y == doctest::Approx(2.0f));
}

// ── Component lifecycle callbacks ────────────────────────────────────────────

class LifecycleTestComponent : public Component {
public:
    COMPONENT_TYPE(LifecycleTestComponent)
    int enableCount = 0;
    int disableCount = 0;
    void OnEnable() override { enableCount++; }
    void OnDisable() override { disableCount++; }
};

TEST_CASE("ECS: component lifecycle callbacks") {
    auto obj = std::make_shared<GameObject>();
    auto* comp = obj->AddComponent<LifecycleTestComponent>();
    CHECK(comp->IsEnabled());
    CHECK(comp->enableCount == 0);  // Not called on initial creation
    CHECK(comp->disableCount == 0);

    // Disable → OnDisable called
    comp->SetEnabled(false);
    CHECK(!comp->IsEnabled());
    CHECK(comp->disableCount == 1);

    // Enable → OnEnable called
    comp->SetEnabled(true);
    CHECK(comp->IsEnabled());
    CHECK(comp->enableCount == 1);

    // Duplicate call → no extra callback
    comp->SetEnabled(true);
    CHECK(comp->enableCount == 1);
    comp->SetEnabled(false);
    CHECK(comp->disableCount == 2);
    comp->SetEnabled(false);
    CHECK(comp->disableCount == 2);
}

// ── OnDestroy / NotifyDestroy ────────────────────────────────────────────────

struct DestroyCtx { bool called = false; };
class DestroyComp : public Component {
public:
    COMPONENT_TYPE(DestroyComp)
    DestroyCtx* ctx = nullptr;
    void OnDestroy() override { ctx->called = true; }
};

TEST_CASE("ECS: OnDestroy called") {
    DestroyCtx ctx;
    {
        auto obj = std::make_shared<GameObject>("destroy_test");
        auto* comp = obj->AddComponent<DestroyComp>();
        comp->ctx = &ctx;
    }
    CHECK(ctx.called);
}

struct IdempotentCtx { int count = 0; };
class CountComp : public Component {
public:
    COMPONENT_TYPE(CountComp)
    IdempotentCtx* ctx = nullptr;
    void OnDestroy() override { ctx->count++; }
};

TEST_CASE("ECS: NotifyDestroy idempotent") {
    IdempotentCtx ctx;
    auto obj = std::make_shared<GameObject>("idempotent_test");
    auto* comp = obj->AddComponent<CountComp>();
    comp->ctx = &ctx;
    obj->NotifyDestroy();
    obj->NotifyDestroy();  // second call should be ignored
    obj.reset();           // destructor also ignored
    CHECK(ctx.count == 1);
}

struct DisableCtx { int disableCount = 0; };
class TrackComp : public Component {
public:
    COMPONENT_TYPE(TrackComp)
    DisableCtx* ctx = nullptr;
    void OnDisable() override { ctx->disableCount++; }
    void OnDestroy() override {}
};

TEST_CASE("ECS: destroy disabled no double disable") {
    DisableCtx ctx;
    auto obj = std::make_shared<GameObject>("disable_test");
    auto* comp = obj->AddComponent<TrackComp>();
    comp->ctx = &ctx;
    comp->SetEnabled(false);  // OnDisable called once
    CHECK(ctx.disableCount == 1);
    obj.reset();  // already disabled → OnDisable NOT called again
    CHECK(ctx.disableCount == 1);
}

struct RemCtx { int disableCount = 0; int detachCount = 0; };
class RemComp : public Component {
public:
    COMPONENT_TYPE(RemComp)
    RemCtx* ctx = nullptr;
    void OnDisable() override { ctx->disableCount++; }
    void OnDetach() override { ctx->detachCount++; }
};

TEST_CASE("ECS: remove component calls on disable") {
    RemCtx ctx;
    auto obj = std::make_shared<GameObject>("remove_test");
    auto* comp = obj->AddComponent<RemComp>();
    comp->ctx = &ctx;
    CHECK(comp->IsEnabled());
    obj->RemoveComponent<RemComp>();
    CHECK(ctx.disableCount == 1);
    CHECK(ctx.detachCount == 1);
}

namespace {

struct ReentrantRemovalCtx {
    int disableCount = 0;
    int detachCount = 0;
    int destructorCount = 0;
};

class ReentrantRemovalComp : public Component {
public:
    COMPONENT_TYPE(ReentrantRemovalComp)
    ReentrantRemovalCtx* ctx = nullptr;

    ~ReentrantRemovalComp() override {
        if (ctx) ++ctx->destructorCount;
    }

    void OnDisable() override {
        ++ctx->disableCount;
        // The outer removal must already have detached this identity from the
        // component map, so this nested removal is a harmless no-op.
        gameObject->RemoveComponentById(GetRuntimeTypeID());
    }

    void OnDetach() override {
        ++ctx->detachCount;
        gameObject->RemoveComponent<ReentrantRemovalComp>();
    }
};

struct ThrowingRemovalCtx {
    int disableCount = 0;
    int detachCount = 0;
};

class ThrowingRemovalComp : public Component {
public:
    COMPONENT_TYPE(ThrowingRemovalComp)
    ThrowingRemovalCtx* ctx = nullptr;

    void OnDisable() override {
        ++ctx->disableCount;
        throw std::runtime_error("disable failure");
    }

    void OnDetach() override {
        ++ctx->detachCount;
        throw std::runtime_error("detach failure");
    }
};

struct RemovalDuringDestroyCtx {
    int disableCount = 0;
    int destroyCount = 0;
    int detachCount = 0;
};

class RemovalDuringDestroyComp : public Component {
public:
    COMPONENT_TYPE(RemovalDuringDestroyComp)
    RemovalDuringDestroyCtx* ctx = nullptr;
    bool removeFromDisable = false;
    bool removeFromDestroy = false;

    void OnDisable() override {
        ++ctx->disableCount;
        if (removeFromDisable) {
            gameObject->RemoveComponent<RemovalDuringDestroyComp>();
        }
    }

    void OnDestroy() override {
        ++ctx->destroyCount;
        if (removeFromDestroy) {
            gameObject->RemoveComponentById(GetRuntimeTypeID());
        }
    }

    void OnDetach() override { ++ctx->detachCount; }
};

} // namespace

TEST_CASE("ECS: component removal is safe when lifecycle callbacks remove themselves") {
    ReentrantRemovalCtx ctx;
    auto obj = std::make_shared<GameObject>("reentrant_remove");
    auto* component = obj->AddComponent<ReentrantRemovalComp>();
    component->ctx = &ctx;

    CHECK_NOTHROW(obj->RemoveComponent<ReentrantRemovalComp>());
    CHECK_FALSE(obj->HasComponent<ReentrantRemovalComp>());
    CHECK(ctx.disableCount == 1);
    CHECK(ctx.detachCount == 1);
    CHECK(ctx.destructorCount == 1);
}

TEST_CASE("ECS: component removal attempts detach after a throwing disable callback") {
    ThrowingRemovalCtx ctx;
    auto obj = std::make_shared<GameObject>("throwing_remove");
    auto* component = obj->AddComponent<ThrowingRemovalComp>();
    component->ctx = &ctx;

    CHECK_NOTHROW(obj->RemoveComponentById(component->GetRuntimeTypeID()));
    CHECK_FALSE(obj->HasComponent<ThrowingRemovalComp>());
    CHECK(ctx.disableCount == 1);
    CHECK(ctx.detachCount == 1);
}

TEST_CASE("ECS: removing a component during destruction preserves exactly-once callbacks") {
    SUBCASE("remove from OnDisable") {
        RemovalDuringDestroyCtx ctx;
        auto obj = std::make_shared<GameObject>("remove_during_destroy_disable");
        auto* component = obj->AddComponent<RemovalDuringDestroyComp>();
        component->ctx = &ctx;
        component->removeFromDisable = true;

        CHECK_NOTHROW(obj->NotifyDestroy());
        CHECK_FALSE(obj->HasComponent<RemovalDuringDestroyComp>());
        CHECK(ctx.disableCount == 1);
        CHECK(ctx.destroyCount == 1);
        CHECK(ctx.detachCount == 1);
    }

    SUBCASE("remove from OnDestroy") {
        RemovalDuringDestroyCtx ctx;
        auto obj = std::make_shared<GameObject>("remove_during_destroy_callback");
        auto* component = obj->AddComponent<RemovalDuringDestroyComp>();
        component->ctx = &ctx;
        component->removeFromDestroy = true;

        CHECK_NOTHROW(obj->NotifyDestroy());
        CHECK_FALSE(obj->HasComponent<RemovalDuringDestroyComp>());
        CHECK(ctx.disableCount == 1);
        CHECK(ctx.destroyCount == 1);
        CHECK(ctx.detachCount == 1);
    }
}

// ── BoxCollider2D ────────────────────────────────────────────────────────────

TEST_CASE("BoxCollider2D: world AABB calculation") {
    auto obj = std::make_shared<GameObject>();
    obj->AddComponent<Transform>(50.0f, 60.0f);
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>(20.0f, 30.0f);

    AABB aabb = bc->GetWorldAABB();
    CHECK(aabb.x == doctest::Approx(50.0f));
    CHECK(aabb.y == doctest::Approx(60.0f));
    CHECK(aabb.width == doctest::Approx(20.0f));
    CHECK(aabb.height == doctest::Approx(30.0f));
}

TEST_CASE("BoxCollider2D: collision check") {
    auto obj1 = std::make_shared<GameObject>();
    obj1->AddComponent<Transform>(0.0f, 0.0f);
    BoxCollider2D* bc1 = obj1->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    auto obj2 = std::make_shared<GameObject>();
    obj2->AddComponent<Transform>(5.0f, 5.0f);
    BoxCollider2D* bc2 = obj2->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    CHECK(bc1->CheckCollision(bc2));

    auto obj3 = std::make_shared<GameObject>();
    obj3->AddComponent<Transform>(100.0f, 100.0f);
    BoxCollider2D* bc3 = obj3->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    CHECK(!bc1->CheckCollision(bc3));
}

TEST_CASE("BoxCollider2D: type name") {
    auto obj = std::make_shared<GameObject>();
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>();
    CHECK(bc->GetTypeName() == "BoxCollider2D");
}

// ── Collider2D abstraction ───────────────────────────────────────────────────

TEST_CASE("Collider2D: inheritance and abstract options") {
    auto obj = std::make_shared<GameObject>("test");
    auto* box = obj->AddComponent<BoxCollider2D>();

    Collider2D* collider = dynamic_cast<Collider2D*>(box);
    REQUIRE(collider != nullptr);
    CHECK(collider->GetShapeType() == Collider2D::ShapeType::Box);

    collider->SetOffset(5.0f, 10.0f);
    CHECK(collider->GetOffset().x == doctest::Approx(5.0f));
    CHECK(collider->GetOffset().y == doctest::Approx(10.0f));
    collider->SetTrigger(true);
    CHECK(collider->IsTrigger());
}

TEST_CASE("Collider2D: world bounds compatibility") {
    auto obj = std::make_shared<GameObject>("test");
    auto* transform = obj->AddComponent<Transform>();
    transform->SetPosition(100.0f, 200.0f);
    auto* box = obj->AddComponent<BoxCollider2D>(50.0f, 30.0f);

    AABB bounds = box->GetWorldBounds();
    AABB aabb = box->GetWorldAABB();
    CHECK(bounds.x == doctest::Approx(aabb.x));
    CHECK(bounds.y == doctest::Approx(aabb.y));
    CHECK(bounds.width == doctest::Approx(aabb.width));
    CHECK(bounds.height == doctest::Approx(aabb.height));
}

TEST_CASE("Collider2D: negative scale bounds normalization") {
    auto obj = std::make_shared<GameObject>("test");
    auto* transform = obj->AddComponent<Transform>();
    transform->SetPosition(0.0f, 0.0f);
    transform->SetScale(-2.0f, -1.0f);
    auto* box = obj->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    AABB bounds = box->GetWorldBounds();
    CHECK(bounds.width > 0.0f);
    CHECK(bounds.height > 0.0f);
}

#ifdef MOLGA_MARROW_SUPPORT
#include "ECS/Components/MarrowRenderer.h"

TEST_CASE("ECS: MarrowRenderer component basics and loading") {
    auto obj = std::make_shared<GameObject>("MarrowObj");
    auto* marrow = obj->AddComponent<MarrowRenderer>();
    REQUIRE(marrow != nullptr);
    CHECK(marrow->GetTypeName() == "MarrowRenderer");
    CHECK(marrow->GetSortingOrder() == 0);
    
    marrow->SetSortingOrder(15);
    CHECK(marrow->GetSortingOrder() == 15);
    
    marrow->SetColor(Color(0.2f, 0.4f, 0.6f, 0.8f));
    CHECK(marrow->GetColor().r == doctest::Approx(0.2f));
    CHECK(marrow->GetColor().a == doctest::Approx(0.8f));
    
    marrow->SetMix("idle", "run", 0.35f);
    
    // Test serialization/deserialization
    nlohmann::json j;
    marrow->Serialize(j);
    
    auto obj2 = std::make_shared<GameObject>("MarrowObj2");
    auto* marrow2 = obj2->AddComponent<MarrowRenderer>();
    marrow2->Deserialize(j);
    
    CHECK(marrow2->GetSortingOrder() == 15);
    CHECK(marrow2->GetColor().g == doctest::Approx(0.4f));
    CHECK(marrow2->GetColor().a == doctest::Approx(0.8f));
    
    // Test path assignments
    marrow2->SetSkeletonPath("assets/marrow/player_idle.mskl");
    marrow2->SetAtlasPath("assets/marrow/player_idle.matl");
    CHECK(marrow2->GetSkeletonPath() == "assets/marrow/player_idle.mskl");
    CHECK(marrow2->GetAtlasPath() == "assets/marrow/player_idle.matl");
}
#endif
