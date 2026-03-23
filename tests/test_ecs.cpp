#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

static bool approx(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) < eps;
}

// ── GameObject basics ────────────────────────────────────────────────────────

static void test_gameobject_creation() {
    auto obj = std::make_shared<GameObject>("Player");
    assert(obj->GetName() == "Player");
    assert(obj->IsActive());
    assert(obj->GetID() > 0);
}

static void test_gameobject_name() {
    auto obj = std::make_shared<GameObject>();
    assert(obj->GetName() == "GameObject");
    obj->SetName("Enemy");
    assert(obj->GetName() == "Enemy");
}

static void test_gameobject_active() {
    auto obj = std::make_shared<GameObject>();
    assert(obj->IsActive());
    obj->SetActive(false);
    assert(!obj->IsActive());
}

static void test_gameobject_unique_ids() {
    auto a = std::make_shared<GameObject>();
    auto b = std::make_shared<GameObject>();
    assert(a->GetID() != b->GetID());
}

// ── Component management ─────────────────────────────────────────────────────

static void test_add_get_component() {
    auto obj = std::make_shared<GameObject>("Test");
    Transform* t = obj->AddComponent<Transform>(10.0f, 20.0f);
    assert(t != nullptr);
    assert(approx(t->GetX(), 10.0f));
    assert(approx(t->GetY(), 20.0f));

    Transform* got = obj->GetComponent<Transform>();
    assert(got == t);
}

static void test_has_component() {
    auto obj = std::make_shared<GameObject>();
    assert(!obj->HasComponent<Transform>());
    obj->AddComponent<Transform>();
    assert(obj->HasComponent<Transform>());
}

static void test_remove_component() {
    auto obj = std::make_shared<GameObject>();
    obj->AddComponent<Transform>();
    assert(obj->HasComponent<Transform>());

    obj->RemoveComponent<Transform>();
    assert(!obj->HasComponent<Transform>());
}

static void test_multiple_components() {
    auto obj = std::make_shared<GameObject>();
    obj->AddComponent<Transform>(5.0f, 5.0f);
    obj->AddComponent<BoxCollider2D>(32.0f, 32.0f);

    assert(obj->HasComponent<Transform>());
    assert(obj->HasComponent<BoxCollider2D>());

    auto components = obj->GetComponents();
    assert(components.size() == 2);
}

// ── Transform ────────────────────────────────────────────────────────────────

static void test_transform_position() {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    assert(approx(t->GetX(), 0.0f));
    assert(approx(t->GetY(), 0.0f));

    t->SetPosition(100.0f, 200.0f);
    assert(approx(t->GetX(), 100.0f));
    assert(approx(t->GetY(), 200.0f));

    t->Translate(10.0f, -5.0f);
    assert(approx(t->GetX(), 110.0f));
    assert(approx(t->GetY(), 195.0f));
}

static void test_transform_rotation() {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    assert(approx(t->GetRotation(), 0.0f));

    t->SetRotation(45.0f);
    assert(approx(t->GetRotation(), 45.0f));
}

static void test_transform_scale() {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    assert(approx(t->GetScale().x, 1.0f));
    assert(approx(t->GetScale().y, 1.0f));

    t->SetScale(2.0f, 3.0f);
    assert(approx(t->GetScale().x, 2.0f));
    assert(approx(t->GetScale().y, 3.0f));

    t->SetScale(0.5f);
    assert(approx(t->GetScale().x, 0.5f));
    assert(approx(t->GetScale().y, 0.5f));
}

static void test_transform_type_name() {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>();
    assert(t->GetTypeName() == "Transform");
}

// ── Parent-child hierarchy ───────────────────────────────────────────────────

static void test_parent_child() {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child = std::make_shared<GameObject>("Child");

    parent->AddChild(child.get());

    assert(child->GetParent() == parent.get());
    assert(parent->GetChildren().size() == 1);
    assert(parent->GetChildren()[0] == child.get());
}

static void test_remove_child() {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child = std::make_shared<GameObject>("Child");

    parent->AddChild(child.get());
    parent->RemoveChild(child.get());

    assert(child->GetParent() == nullptr);
    assert(parent->GetChildren().empty());
}

static void test_world_transform_with_parent() {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child = std::make_shared<GameObject>("Child");

    Transform* pt = parent->AddComponent<Transform>(100.0f, 200.0f);
    pt->SetScale(2.0f, 2.0f);

    Transform* ct = child->AddComponent<Transform>(10.0f, 20.0f);

    parent->AddChild(child.get());

    // World position = parent_pos + child_local * parent_scale (no rotation)
    Vector2 worldPos = ct->GetWorldPosition();
    assert(approx(worldPos.x, 120.0f));  // 100 + 10*2
    assert(approx(worldPos.y, 240.0f));  // 200 + 20*2

    // World scale = parent_scale * child_scale
    Vector2 worldScale = ct->GetWorldScale();
    assert(approx(worldScale.x, 2.0f));
    assert(approx(worldScale.y, 2.0f));
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

static void test_component_lifecycle_callbacks() {
    auto obj = std::make_shared<GameObject>();
    auto* comp = obj->AddComponent<LifecycleTestComponent>();
    assert(comp->IsEnabled());
    assert(comp->enableCount == 0);  // Not called on initial creation
    assert(comp->disableCount == 0);

    // Disable → OnDisable called
    comp->SetEnabled(false);
    assert(!comp->IsEnabled());
    assert(comp->disableCount == 1);

    // Enable → OnEnable called
    comp->SetEnabled(true);
    assert(comp->IsEnabled());
    assert(comp->enableCount == 1);

    // Duplicate call → no extra callback
    comp->SetEnabled(true);
    assert(comp->enableCount == 1);
    comp->SetEnabled(false);
    assert(comp->disableCount == 2);
    comp->SetEnabled(false);
    assert(comp->disableCount == 2);
}

// ── OnDestroy / NotifyDestroy ────────────────────────────────────────────────

static void test_on_destroy_called() {
    struct Ctx { bool called = false; };
    class DestroyComp : public Component {
    public:
        COMPONENT_TYPE(DestroyComp)
        Ctx* ctx = nullptr;
        void OnDestroy() override { ctx->called = true; }
    };

    Ctx ctx;
    {
        auto obj = std::make_shared<GameObject>("destroy_test");
        auto* comp = obj->AddComponent<DestroyComp>();
        comp->ctx = &ctx;
    }
    assert(ctx.called);
}

static void test_notify_destroy_idempotent() {
    struct Ctx { int count = 0; };
    class CountComp : public Component {
    public:
        COMPONENT_TYPE(CountComp)
        Ctx* ctx = nullptr;
        void OnDestroy() override { ctx->count++; }
    };

    Ctx ctx;
    auto obj = std::make_shared<GameObject>("idempotent_test");
    auto* comp = obj->AddComponent<CountComp>();
    comp->ctx = &ctx;
    obj->NotifyDestroy();
    obj->NotifyDestroy();  // second call should be ignored
    obj.reset();           // destructor also ignored
    assert(ctx.count == 1);
}

static void test_destroy_disabled_no_double_disable() {
    struct Ctx { int disableCount = 0; };
    class TrackComp : public Component {
    public:
        COMPONENT_TYPE(TrackComp)
        Ctx* ctx = nullptr;
        void OnDisable() override { ctx->disableCount++; }
        void OnDestroy() override {}
    };

    Ctx ctx;
    auto obj = std::make_shared<GameObject>("disable_test");
    auto* comp = obj->AddComponent<TrackComp>();
    comp->ctx = &ctx;
    comp->SetEnabled(false);  // OnDisable called once
    assert(ctx.disableCount == 1);
    obj.reset();  // already disabled → OnDisable NOT called again
    assert(ctx.disableCount == 1);
}

static void test_remove_component_calls_on_disable() {
    struct Ctx { int disableCount = 0; int detachCount = 0; };
    class RemComp : public Component {
    public:
        COMPONENT_TYPE(RemComp)
        Ctx* ctx = nullptr;
        void OnDisable() override { ctx->disableCount++; }
        void OnDetach() override { ctx->detachCount++; }
    };

    Ctx ctx;
    auto obj = std::make_shared<GameObject>("remove_test");
    auto* comp = obj->AddComponent<RemComp>();
    comp->ctx = &ctx;
    assert(comp->IsEnabled());
    obj->RemoveComponent<RemComp>();
    assert(ctx.disableCount == 1);
    assert(ctx.detachCount == 1);
}

// ── BoxCollider2D ────────────────────────────────────────────────────────────

static void test_box_collider_world_aabb() {
    auto obj = std::make_shared<GameObject>();
    Transform* t = obj->AddComponent<Transform>(50.0f, 60.0f);
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>(20.0f, 30.0f);

    AABB aabb = bc->GetWorldAABB();
    assert(approx(aabb.x, 50.0f));
    assert(approx(aabb.y, 60.0f));
    assert(approx(aabb.width, 20.0f));
    assert(approx(aabb.height, 30.0f));
}

static void test_box_collider_collision() {
    auto obj1 = std::make_shared<GameObject>();
    obj1->AddComponent<Transform>(0.0f, 0.0f);
    BoxCollider2D* bc1 = obj1->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    auto obj2 = std::make_shared<GameObject>();
    obj2->AddComponent<Transform>(5.0f, 5.0f);
    BoxCollider2D* bc2 = obj2->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    assert(bc1->CheckCollision(bc2));

    auto obj3 = std::make_shared<GameObject>();
    obj3->AddComponent<Transform>(100.0f, 100.0f);
    BoxCollider2D* bc3 = obj3->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    assert(!bc1->CheckCollision(bc3));
}

static void test_box_collider_type_name() {
    auto obj = std::make_shared<GameObject>();
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>();
    assert(bc->GetTypeName() == "BoxCollider2D");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_gameobject_creation();
    test_gameobject_name();
    test_gameobject_active();
    test_gameobject_unique_ids();

    test_add_get_component();
    test_has_component();
    test_remove_component();
    test_multiple_components();

    test_transform_position();
    test_transform_rotation();
    test_transform_scale();
    test_transform_type_name();

    test_parent_child();
    test_remove_child();
    test_world_transform_with_parent();

    test_component_lifecycle_callbacks();

    test_on_destroy_called();
    test_notify_destroy_idempotent();
    test_destroy_disabled_no_double_disable();
    test_remove_component_calls_on_disable();

    test_box_collider_world_aabb();
    test_box_collider_collision();
    test_box_collider_type_name();

    std::printf("test_ecs: all tests passed\n");
    return 0;
}
