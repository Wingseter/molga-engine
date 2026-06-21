#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/ComponentFactory.h"
#include "ECS/Components/Transform.h"
#include "doctest.h"
#include <memory>
#include <vector>

// Define a test component to track Start and OnDestroy calls
class LifecycleTracker : public Component {
public:
    static inline int destroyCount = 0;
    static inline int startCount = 0;

    std::string GetTypeName() const override { return "LifecycleTracker"; }
    size_t GetRuntimeTypeID() const override { return ComponentTypeID::Get<LifecycleTracker>(); }

    void Start() override {
        startCount++;
    }

    void OnDestroy() override {
        destroyCount++;
    }
};

TEST_CASE("World Lifecycle: Instantiate creates fresh IDs and copies hierarchy") {
    // Register the test component
    ComponentFactory::Get().Register<LifecycleTracker>("LifecycleTracker");
    LifecycleTracker::destroyCount = 0;
    LifecycleTracker::startCount = 0;

    World world;

    // Create a hierarchy: Parent -> Child -> Grandchild
    auto parentObj = std::make_shared<GameObject>("Parent");
    auto* pt = parentObj->AddComponent<Transform>(1.0f, 2.0f);
    parentObj->AddComponent<LifecycleTracker>();

    auto childObj = std::make_shared<GameObject>("Child");
    childObj->AddComponent<Transform>(3.0f, 4.0f);
    childObj->SetParent(parentObj.get());

    auto grandchildObj = std::make_shared<GameObject>("Grandchild");
    grandchildObj->AddComponent<Transform>(5.0f, 6.0f);
    grandchildObj->SetParent(childObj.get());

    world.Add(parentObj);
    world.Add(childObj);
    world.Add(grandchildObj);

    // Call resolve and start scripts on the world
    world.ResolveAssets();
    world.StartPending();

    // Verify initial states
    CHECK(LifecycleTracker::startCount == 1);
    CHECK(LifecycleTracker::destroyCount == 0);

    // 1. Instantiate the parent
    GameObject* clonedParent = world.Instantiate(parentObj.get());
    REQUIRE(clonedParent != nullptr);
    CHECK(clonedParent->GetName() == "Parent");
    CHECK(clonedParent->GetID() != parentObj->GetID()); // Fresh ID

    // Root should not have a parent in the new instance
    CHECK(clonedParent->GetParent() == nullptr);

    // The cloned parent shouldn't be in the world yet, only in pendingAdds_
    CHECK(world.FindById(clonedParent->GetID()) == nullptr);

    // Flush to move cloned objects to the world
    world.FlushDeferred(0.01f);

    // Now they should be in the world
    GameObject* foundClonedParent = world.FindById(clonedParent->GetID());
    REQUIRE(foundClonedParent != nullptr);
    CHECK(foundClonedParent->HasComponent<LifecycleTracker>());

    // Start count should have incremented for the clone
    CHECK(LifecycleTracker::startCount == 2);

    // Verify subtree hierarchy was cloned
    const auto& parentChildren = foundClonedParent->GetChildren();
    REQUIRE(parentChildren.size() == 1);
    
    GameObject* clonedChild = parentChildren[0];
    CHECK(clonedChild->GetName() == "Child");
    CHECK(clonedChild->GetID() != childObj->GetID());
    CHECK(clonedChild->GetParent() == foundClonedParent);

    const auto& childChildren = clonedChild->GetChildren();
    REQUIRE(childChildren.size() == 1);
    
    GameObject* clonedGrandchild = childChildren[0];
    CHECK(clonedGrandchild->GetName() == "Grandchild");
    CHECK(clonedGrandchild->GetID() != grandchildObj->GetID());
    CHECK(clonedGrandchild->GetParent() == clonedChild);
}

TEST_CASE("World Lifecycle: Instantiate position and parent overloads") {
    World world;

    auto templateObj = std::make_shared<GameObject>("Template");
    templateObj->AddComponent<Transform>(10.0f, 10.0f);
    world.Add(templateObj);

    // 1. Position overload
    Vector2 spawnPos(50.0f, 60.0f);
    GameObject* cloneA = world.Instantiate(templateObj.get(), spawnPos);
    REQUIRE(cloneA != nullptr);
    
    // 2. Parent overload
    auto parentObj = std::make_shared<GameObject>("NewParent");
    world.Add(parentObj);
    GameObject* cloneB = world.Instantiate(templateObj.get(), parentObj.get());
    REQUIRE(cloneB != nullptr);

    // Flush
    world.FlushDeferred(0.01f);

    // Verify Position
    auto* tA = cloneA->GetComponent<Transform>();
    REQUIRE(tA != nullptr);
    CHECK(tA->GetX() == doctest::Approx(50.0f));
    CHECK(tA->GetY() == doctest::Approx(60.0f));

    // Verify Parent
    CHECK(cloneB->GetParent() == parentObj.get());
    CHECK(parentObj->GetChildren().size() == 1);
    CHECK(parentObj->GetChildren()[0] == cloneB);
}

TEST_CASE("World Lifecycle: Destroy cascades and removes safely") {
    LifecycleTracker::destroyCount = 0;
    World world;

    auto parentObj = std::make_shared<GameObject>("Parent");
    parentObj->AddComponent<LifecycleTracker>();

    auto childObj = std::make_shared<GameObject>("Child");
    childObj->AddComponent<LifecycleTracker>();
    childObj->SetParent(parentObj.get());

    world.Add(parentObj);
    world.Add(childObj);

    // Destroy the parent
    world.Destroy(parentObj.get());

    // Before flush, they should still be in the world and OnDestroy not called
    CHECK(world.FindById(parentObj->GetID()) != nullptr);
    CHECK(world.FindById(childObj->GetID()) != nullptr);
    CHECK(LifecycleTracker::destroyCount == 0);

    // Flush
    world.FlushDeferred(0.01f);

    // Now they should be gone and OnDestroy called for both
    CHECK(world.FindById(parentObj->GetID()) == nullptr);
    CHECK(world.FindById(childObj->GetID()) == nullptr);
    CHECK(LifecycleTracker::destroyCount == 2);
}

TEST_CASE("World Lifecycle: Destroy with delay") {
    LifecycleTracker::destroyCount = 0;
    World world;

    auto target = std::make_shared<GameObject>("Target");
    target->AddComponent<LifecycleTracker>();
    world.Add(target);

    // Destroy with 2.0s delay
    world.Destroy(target.get(), 2.0f);

    // Flush with small dt
    world.FlushDeferred(0.5f);
    CHECK(world.FindById(target->GetID()) != nullptr);
    CHECK(LifecycleTracker::destroyCount == 0);

    // Flush with more dt
    world.FlushDeferred(1.0f);
    CHECK(world.FindById(target->GetID()) != nullptr);
    CHECK(LifecycleTracker::destroyCount == 0);

    // Flush to exceed the delay
    world.FlushDeferred(0.6f); // Total elapsed = 2.1s
    CHECK(world.FindById(target->GetID()) == nullptr);
    CHECK(LifecycleTracker::destroyCount == 1);
}

TEST_CASE("World Lifecycle: Iterator safety during Update") {
    LifecycleTracker::destroyCount = 0;
    World world;

    // Create several objects
    for (int i = 0; i < 5; ++i) {
        auto go = std::make_shared<GameObject>("Obj" + std::to_string(i));
        go->AddComponent<LifecycleTracker>();
        world.Add(go);
    }

    world.ResolveAssets();
    world.StartPending();

    // Iterate objects and trigger Instantiate / Destroy
    auto& objects = world.Objects();
    REQUIRE(objects.size() == 5);

    // Simulate an Update loop that calls Instantiate and Destroy
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i]->GetName() == "Obj2") {
            // Instantiate during iteration
            world.Instantiate(objects[i].get());
        }
        if (objects[i]->GetName() == "Obj4") {
            // Destroy during iteration
            world.Destroy(objects[i].get());
        }
    }

    // Since they are deferred, size should still be 5
    CHECK(objects.size() == 5);
    CHECK(LifecycleTracker::destroyCount == 0);

    // Flush updates
    world.FlushDeferred(0.01f);

    // Obj4 is destroyed (1 gone), clone of Obj2 is added (1 new), total still 5
    CHECK(objects.size() == 5);
    CHECK(LifecycleTracker::destroyCount == 1); // Obj4 destroyed
}
