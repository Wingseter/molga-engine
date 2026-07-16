#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/ComponentFactory.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/Transform.h"
#include "Core/ProjectSettings.h"
#include "Physics/PhysicsWorld.h"
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

namespace {

class RecursiveFlushDestroyer final : public Component {
public:
    COMPONENT_TYPE(RecursiveFlushDestroyer)

    World* world = nullptr;
    GameObject* target = nullptr;
    int* destroyCalls = nullptr;

    void OnDestroy() override {
        if (destroyCalls) ++*destroyCalls;
        if (!world || !target) return;
        world->Destroy(target);
        world->FlushDeferred(0.0f);
    }
};

class LifecycleProbe final : public Component {
public:
    COMPONENT_TYPE(LifecycleProbe)

    int* awakeCalls = nullptr;
    int* enableCalls = nullptr;
    int* startCalls = nullptr;

    void Awake() override { if (awakeCalls) ++*awakeCalls; }
    void OnEnable() override { if (enableCalls) ++*enableCalls; }
    void Start() override { if (startCalls) ++*startCalls; }
};

class AwakeDestroyAndFlush final : public Component {
public:
    COMPONENT_TYPE(AwakeDestroyAndFlush)

    World* world = nullptr;
    GameObject* target = nullptr;

    void Awake() override {
        if (!world || !target) return;
        world->Destroy(target);
        world->FlushDeferred(0.0f);
    }
};

class RepopulatePhysicsOnDestroy final : public Component {
public:
    COMPONENT_TYPE(RepopulatePhysicsOnDestroy)

    World* world = nullptr;
    bool* populatedBackend = nullptr;

    void OnDestroy() override {
        if (!world) return;
        auto spawned = std::make_shared<GameObject>("Spawned during shutdown");
        spawned->AddComponent<Transform>();
        spawned->AddComponent<Rigidbody2D>();
        spawned->AddComponent<BoxCollider2D>();
        // Bypass the normal creation API deliberately: Add is rejected during
        // shutdown, while this test still verifies the final backend Reset even
        // if callback code mutates the public object container directly.
        spawned->SetWorld(world);
        world->Objects().push_back(spawned);
        world->FixedStep(1.0f / 60.0f);
        if (populatedBackend) {
            *populatedBackend = world->GetPhysicsWorld()->BodyCount() > 0;
        }
    }
};

class ClearWorldOnUpdate final : public Component {
public:
    COMPONENT_TYPE(ClearWorldOnUpdate)

    World* world = nullptr;
    bool* observedDispatch = nullptr;

    void Update(float) override {
        if (!world) return;
        if (observedDispatch) *observedDispatch = world->IsDispatchingCallbacks();
        world->Clear();
    }
};

class UpdateProbe final : public Component {
public:
    COMPONENT_TYPE(UpdateProbe)

    int* updateCalls = nullptr;
    void Update(float) override { if (updateCalls) ++*updateCalls; }
};

class MoveWorldDuringUpdate final : public Component {
public:
    COMPONENT_TYPE(MoveWorldDuringUpdate)

    World* source = nullptr;
    World* idleDestination = nullptr;
    World* idleSource = nullptr;
    unsigned int destinationObjectId = 0;
    unsigned int idleSourceObjectId = 0;
    bool* allMovesRejected = nullptr;
    bool attempted = false;

    void Update(float) override {
        if (attempted || !source || !idleDestination || !idleSource) return;
        attempted = true;
        GameObject* owner = GetGameObject();
        const unsigned int ownerId = owner ? owner->GetID() : 0;

        *idleDestination = std::move(*source);  // busy source
        const bool sourceAssignmentRejected =
            source->FindById(ownerId) == owner &&
            idleDestination->FindById(destinationObjectId) != nullptr;

        *source = std::move(*idleSource);  // busy destination
        const bool destinationAssignmentRejected =
            source->FindById(ownerId) == owner &&
            idleSource->FindById(idleSourceObjectId) != nullptr;

        World attemptedMoveConstruction(std::move(*source));
        const bool constructionRejected = attemptedMoveConstruction.Objects().empty() &&
            source->FindById(ownerId) == owner && source->IsDispatchingCallbacks();

        if (allMovesRejected) {
            *allMovesRejected = sourceAssignmentRejected &&
                destinationAssignmentRejected && constructionRejected;
        }
    }
};

class ShutdownCreationProbe final : public Component {
public:
    COMPONENT_TYPE(ShutdownCreationProbe)

    const GameObject* instantiateTemplate = nullptr;
    int* callbackAttempts = nullptr;
    bool* addRejected = nullptr;
    bool* instantiateRejected = nullptr;
    bool* prefabRejected = nullptr;

    void OnDestroy() override {
        GameObject* owner = GetGameObject();
        World* world = owner ? owner->GetWorld() : nullptr;
        // A rejected child was never attached, so its destructor reaches this
        // callback with no World and cannot recursively spawn another child.
        if (!world) return;
        if (callbackAttempts) ++*callbackAttempts;

        auto spawned = std::make_shared<GameObject>("Rejected shutdown spawn");
        auto* nested = spawned->AddComponent<ShutdownCreationProbe>();
        nested->instantiateTemplate = instantiateTemplate;
        nested->callbackAttempts = callbackAttempts;
        nested->addRejected = addRejected;
        nested->instantiateRejected = instantiateRejected;
        nested->prefabRejected = prefabRejected;

        if (addRejected) *addRejected = world->Add(spawned) == nullptr;
        if (instantiateRejected) {
            *instantiateRejected = world->Instantiate(instantiateTemplate) == nullptr;
        }
        if (prefabRejected) {
            *prefabRejected = world->InstantiatePrefab("shutdown-prefab") == nullptr;
        }
    }
};

} // namespace

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
    CHECK(parentObj->GetWorld() == nullptr);
    CHECK(childObj->GetWorld() == nullptr);
}

TEST_CASE("World Lifecycle: Flush detaches a removed child from its surviving parent") {
    World world;
    auto parent = std::make_shared<GameObject>("Surviving parent");
    auto child = std::make_shared<GameObject>("Removed child");
    REQUIRE(child->SetParent(parent.get()));
    world.Add(parent);
    world.Add(child);

    world.Destroy(child.get());
    world.FlushDeferred(0.0f);

    CHECK(world.FindById(parent->GetID()) == parent.get());
    CHECK(world.FindById(child->GetID()) == nullptr);
    CHECK(parent->GetChildren().empty());
    CHECK(child->GetParent() == nullptr);
    CHECK(child->GetWorld() == nullptr);
}

TEST_CASE("World Lifecycle: recursive Destroy and Flush drains without dangling traversal") {
    LifecycleTracker::destroyCount = 0;
    World world;
    int recursiveDestroyCalls = 0;

    auto first = std::make_shared<GameObject>("First");
    auto* destroyer = first->AddComponent<RecursiveFlushDestroyer>();
    destroyer->world = &world;
    destroyer->destroyCalls = &recursiveDestroyCalls;

    auto second = std::make_shared<GameObject>("Second");
    second->AddComponent<LifecycleTracker>();
    destroyer->target = second.get();

    const unsigned int firstId = first->GetID();
    const unsigned int secondId = second->GetID();
    world.Add(first);
    world.Add(second);

    world.Destroy(first.get());
    world.FlushDeferred(0.0f);

    CHECK(recursiveDestroyCalls == 1);
    CHECK(LifecycleTracker::destroyCount == 1);
    CHECK(world.FindById(firstId) == nullptr);
    CHECK(world.FindById(secondId) == nullptr);
    CHECK(first->GetWorld() == nullptr);
    CHECK(second->GetWorld() == nullptr);
}

TEST_CASE("World Lifecycle: StartPending skips an object removed by an earlier Awake") {
    World world;
    int targetAwakeCalls = 0;
    int targetEnableCalls = 0;
    int targetStartCalls = 0;

    auto remover = std::make_shared<GameObject>("Remover");
    auto* removerComponent = remover->AddComponent<AwakeDestroyAndFlush>();
    removerComponent->world = &world;

    auto target = std::make_shared<GameObject>("Target");
    auto* probe = target->AddComponent<LifecycleProbe>();
    probe->awakeCalls = &targetAwakeCalls;
    probe->enableCalls = &targetEnableCalls;
    probe->startCalls = &targetStartCalls;
    removerComponent->target = target.get();
    const unsigned int targetId = target->GetID();

    world.Add(remover);
    world.Add(target);
    world.StartPending();

    CHECK(world.FindById(targetId) == nullptr);
    CHECK(target->GetWorld() == nullptr);
    CHECK(targetAwakeCalls == 0);
    CHECK(targetEnableCalls == 0);
    CHECK(targetStartCalls == 0);
}

TEST_CASE("World Lifecycle: Shutdown resets physics created by OnDestroy callbacks") {
    ProjectSettings::Get().SetDefaults();
    World world;
    bool populatedBackend = false;

    auto object = std::make_shared<GameObject>("Physics repopulator");
    auto* repopulator = object->AddComponent<RepopulatePhysicsOnDestroy>();
    repopulator->world = &world;
    repopulator->populatedBackend = &populatedBackend;
    world.Add(object);

    world.Clear();

    CHECK(populatedBackend);
    CHECK(world.Objects().empty());
    CHECK(world.GetPhysicsWorld()->BodyCount() == 0);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 0);
    CHECK(object->GetWorld() == nullptr);
}

TEST_CASE("World Lifecycle: Shutdown rejects creation APIs and terminates spawn chains") {
    World world;
    int callbackAttempts = 0;
    bool addRejected = false;
    bool instantiateRejected = false;
    bool prefabRejected = false;

    auto instantiateTemplate = std::make_shared<GameObject>("Template");
    instantiateTemplate->AddComponent<Transform>();
    auto object = std::make_shared<GameObject>("Shutdown creator");
    auto* probe = object->AddComponent<ShutdownCreationProbe>();
    probe->instantiateTemplate = instantiateTemplate.get();
    probe->callbackAttempts = &callbackAttempts;
    probe->addRejected = &addRejected;
    probe->instantiateRejected = &instantiateRejected;
    probe->prefabRejected = &prefabRejected;
    world.Add(object);

    world.Clear();

    CHECK(callbackAttempts == 1);
    CHECK(addRejected);
    CHECK(instantiateRejected);
    CHECK(prefabRejected);
    CHECK(world.Objects().empty());
    CHECK(object->GetWorld() == nullptr);
}

TEST_CASE("World Lifecycle: callback dispatch is visible and skips objects removed during Update") {
    World world;
    bool observedDispatch = false;
    int laterUpdateCalls = 0;

    auto clearer = std::make_shared<GameObject>("Clearer");
    auto* clearComponent = clearer->AddComponent<ClearWorldOnUpdate>();
    clearComponent->world = &world;
    clearComponent->observedDispatch = &observedDispatch;

    auto later = std::make_shared<GameObject>("Later");
    later->AddComponent<UpdateProbe>()->updateCalls = &laterUpdateCalls;
    world.Add(clearer);
    world.Add(later);

    CHECK_FALSE(world.IsDispatchingCallbacks());
    world.Update(1.0f / 60.0f);

    CHECK(observedDispatch);
    CHECK_FALSE(world.IsDispatchingCallbacks());
    CHECK(laterUpdateCalls == 0);
    CHECK(world.Objects().empty());
    CHECK(clearer->GetWorld() == nullptr);
    CHECK(later->GetWorld() == nullptr);
}

TEST_CASE("World Lifecycle: move operations reject busy source and destination Worlds") {
    World world;
    World idleDestination;
    World idleSource;
    bool allMovesRejected = false;

    auto destinationObject = std::make_shared<GameObject>("Destination sentinel");
    auto sourceObject = std::make_shared<GameObject>("Source sentinel");
    idleDestination.Add(destinationObject);
    idleSource.Add(sourceObject);

    auto moverObject = std::make_shared<GameObject>("Move requester");
    auto* mover = moverObject->AddComponent<MoveWorldDuringUpdate>();
    mover->source = &world;
    mover->idleDestination = &idleDestination;
    mover->idleSource = &idleSource;
    mover->destinationObjectId = destinationObject->GetID();
    mover->idleSourceObjectId = sourceObject->GetID();
    mover->allMovesRejected = &allMovesRejected;
    const unsigned int moverId = moverObject->GetID();
    world.Add(moverObject);

    world.Update(0.0f);

    CHECK(mover->attempted);
    CHECK(allMovesRejected);
    CHECK_FALSE(world.IsDispatchingCallbacks());
    CHECK(world.FindById(moverId) == moverObject.get());
    CHECK(idleDestination.FindById(destinationObject->GetID()) == destinationObject.get());
    CHECK(idleSource.FindById(sourceObject->GetID()) == sourceObject.get());
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
