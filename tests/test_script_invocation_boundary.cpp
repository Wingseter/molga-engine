#include "doctest.h"

#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "Core/ProjectSettings.h"
#include "Core/Scheduler.h"
#include "Core/World.h"
#include "ECS/Component.h"
#include "ECS/GameObject.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/Transform.h"
#include "Scripting/Script.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace {

struct InvocationCounts {
    int awake = 0;
    int enable = 0;
    int start = 0;
    int update = 0;
    int fixedUpdate = 0;
    int lateUpdate = 0;
    int disable = 0;
    int collisionEnter = 0;
    int collisionStay = 0;
    int collisionExit = 0;
    int triggerEnter = 0;
    int triggerStay = 0;
    int triggerExit = 0;
};

class ConfigurableFaultScript final : public Script {
public:
    SCRIPT_CLASS(ConfigurableFaultScript)

    InvocationCounts counts;
    ScriptPhase throwPhase = ScriptPhase::Update;
    bool throwEnabled = true;
    bool throwUnknown = false;
    bool throwFromDisableDuringIsolation = false;
    bool deactivateBeforeUpdateFault = false;

    void Awake() override { ++counts.awake; MaybeThrow(ScriptPhase::Awake); }
    void OnEnable() override { ++counts.enable; MaybeThrow(ScriptPhase::OnEnable); }
    void Start() override { ++counts.start; MaybeThrow(ScriptPhase::Start); }
    void Update(float) override {
        ++counts.update;
        if (deactivateBeforeUpdateFault && GetGameObject()) {
            GetGameObject()->SetActive(false);
        }
        MaybeThrow(ScriptPhase::Update);
    }
    void FixedUpdate(float) override {
        ++counts.fixedUpdate;
        MaybeThrow(ScriptPhase::FixedUpdate);
    }
    void LateUpdate(float) override {
        ++counts.lateUpdate;
        MaybeThrow(ScriptPhase::LateUpdate);
    }
    void OnDisable() override {
        ++counts.disable;
        if (throwFromDisableDuringIsolation) {
            throw std::runtime_error("secondary disable failure");
        }
        MaybeThrow(ScriptPhase::OnDisable);
    }
    void OnCollisionEnter(GameObject*) override {
        ++counts.collisionEnter;
        MaybeThrow(ScriptPhase::CollisionEnter);
    }
    void OnCollisionStay(GameObject*) override {
        ++counts.collisionStay;
        MaybeThrow(ScriptPhase::CollisionStay);
    }
    void OnCollisionExit(GameObject*) override {
        ++counts.collisionExit;
        MaybeThrow(ScriptPhase::CollisionExit);
    }
    void OnTriggerEnter(GameObject*) override {
        ++counts.triggerEnter;
        MaybeThrow(ScriptPhase::TriggerEnter);
    }
    void OnTriggerStay(GameObject*) override {
        ++counts.triggerStay;
        MaybeThrow(ScriptPhase::TriggerStay);
    }
    void OnTriggerExit(GameObject*) override {
        ++counts.triggerExit;
        MaybeThrow(ScriptPhase::TriggerExit);
    }

private:
    void MaybeThrow(ScriptPhase phase) const {
        if (!throwEnabled || throwPhase != phase) return;
        if (throwUnknown) throw 17;
        throw std::runtime_error("intentional script failure");
    }
};

class HealthyPeerScript final : public Script {
public:
    SCRIPT_CLASS(HealthyPeerScript)
    InvocationCounts counts;

    void Awake() override { ++counts.awake; }
    void OnEnable() override { ++counts.enable; }
    void Start() override { ++counts.start; }
    void Update(float) override { ++counts.update; }
    void FixedUpdate(float) override { ++counts.fixedUpdate; }
    void LateUpdate(float) override { ++counts.lateUpdate; }
    void OnDisable() override { ++counts.disable; }
    void OnCollisionEnter(GameObject*) override { ++counts.collisionEnter; }
    void OnCollisionStay(GameObject*) override { ++counts.collisionStay; }
    void OnCollisionExit(GameObject*) override { ++counts.collisionExit; }
    void OnTriggerEnter(GameObject*) override { ++counts.triggerEnter; }
    void OnTriggerStay(GameObject*) override { ++counts.triggerStay; }
    void OnTriggerExit(GameObject*) override { ++counts.triggerExit; }
};

template <typename T>
T* AttachScript(GameObject& object) {
    return static_cast<T*>(object.AddComponentRaw(new T()));
}

class ThrowingUpdateComponent final : public Component {
public:
    COMPONENT_TYPE(ThrowingUpdateComponent)
    void Update(float) override { throw std::runtime_error("engine component failure"); }
};

class ThrowingAwakeComponent final : public Component {
public:
    COMPONENT_TYPE(ThrowingAwakeComponent)
    void Awake() override { throw std::runtime_error("engine component awake failure"); }
};

class DeferredProbeScript final : public Script {
public:
    SCRIPT_CLASS(DeferredProbeScript)
    void Schedule(int* calls) { Invoke([calls] { ++*calls; }, 0.0f); }
};

class TimerFaultScript final : public Script {
public:
    SCRIPT_CLASS(TimerFaultScript)
    int* canceledTimerCalls = nullptr;
    int* canceledCoroutineCalls = nullptr;
    bool throwUnknown = false;

    void Start() override {
        Invoke([unknown = throwUnknown] {
            if (unknown) throw 17;
            throw std::runtime_error("timer failure");
        }, 0.0f);
        Invoke([calls = canceledTimerCalls] { ++*calls; }, 0.0f);
        StartCoroutine([calls = canceledCoroutineCalls](float) {
            ++*calls;
            return true;
        });
    }
};

class CoroutineFaultScript final : public Script {
public:
    SCRIPT_CLASS(CoroutineFaultScript)
    bool throwUnknown = false;

    void Start() override {
        StartCoroutine([unknown = throwUnknown](float) -> bool {
            if (unknown) throw 17;
            throw std::runtime_error("coroutine failure");
        });
    }
};

class ScheduledHealthyScript final : public Script {
public:
    SCRIPT_CLASS(ScheduledHealthyScript)
    int* timerCalls = nullptr;
    int* coroutineCalls = nullptr;

    void Start() override {
        Invoke([calls = timerCalls] { ++*calls; }, 0.0f);
        StartCoroutine([calls = coroutineCalls](float) {
            ++*calls;
            return false;
        });
    }
};

void ConfigureContactObject(GameObject& object, bool trigger, bool dynamic) {
    object.AddComponent<Transform>();
    auto* collider = object.AddComponent<BoxCollider2D>();
    collider->SetSize(30.0f, 30.0f);
    collider->SetTrigger(trigger);
    if (dynamic) {
        auto* body = object.AddComponent<Rigidbody2D>();
        body->SetBodyType(Rigidbody2D::BodyType::Dynamic);
        body->SetGravityScale(0.0f);
    }
}

int ContactCount(const InvocationCounts& counts, ScriptPhase phase) {
    switch (phase) {
        case ScriptPhase::CollisionEnter: return counts.collisionEnter;
        case ScriptPhase::CollisionStay: return counts.collisionStay;
        case ScriptPhase::CollisionExit: return counts.collisionExit;
        case ScriptPhase::TriggerEnter: return counts.triggerEnter;
        case ScriptPhase::TriggerStay: return counts.triggerStay;
        case ScriptPhase::TriggerExit: return counts.triggerExit;
        default: return 0;
    }
}

} // namespace

TEST_CASE("Script boundary isolates Awake, OnEnable, and Start while peers continue") {
    for (bool unknown : {false, true}) {
      for (ScriptPhase phase : {ScriptPhase::Awake, ScriptPhase::OnEnable,
                                ScriptPhase::Start}) {
        World world;
        auto faultyObject = std::make_shared<GameObject>("Faulty lifecycle object");
        auto* faulty = AttachScript<ConfigurableFaultScript>(*faultyObject);
        faulty->throwPhase = phase;
        faulty->throwUnknown = unknown;
        auto healthyObject = std::make_shared<GameObject>("Healthy lifecycle object");
        auto* healthy = AttachScript<HealthyPeerScript>(*healthyObject);
        world.Add(faultyObject);
        world.Add(healthyObject);

        CHECK_NOTHROW(world.StartPending());
        REQUIRE(faulty->IsFaulted());
        REQUIRE(faulty->GetFaultInfo() != nullptr);
        CHECK(faulty->GetFaultInfo()->phase == phase);
        CHECK(faulty->GetFaultInfo()->unknownException == unknown);
        CHECK_FALSE(faulty->IsEnabled());
        CHECK(faulty->counts.disable == 1);
        CHECK(healthy->counts.awake == 1);
        CHECK(healthy->counts.enable == 1);
        CHECK(healthy->counts.start == 1);
      }
    }
}

TEST_CASE("Script boundary isolates every frame phase and unknown exceptions") {
    for (bool unknown : {false, true}) {
      for (ScriptPhase phase : {ScriptPhase::Update, ScriptPhase::FixedUpdate,
                                ScriptPhase::LateUpdate}) {
        World world;
        auto faultyObject = std::make_shared<GameObject>("Faulty frame object");
        auto* faulty = AttachScript<ConfigurableFaultScript>(*faultyObject);
        faulty->throwEnabled = false;
        auto healthyObject = std::make_shared<GameObject>("Healthy frame object");
        auto* healthy = AttachScript<HealthyPeerScript>(*healthyObject);
        world.Add(faultyObject);
        world.Add(healthyObject);
        world.StartPending();
        faulty->throwPhase = phase;
        faulty->throwEnabled = true;
        faulty->throwUnknown = unknown;

        if (phase == ScriptPhase::Update) CHECK_NOTHROW(world.Update(0.016f));
        if (phase == ScriptPhase::FixedUpdate) CHECK_NOTHROW(world.FixedStep(0.02f));
        if (phase == ScriptPhase::LateUpdate) CHECK_NOTHROW(world.LateUpdate(0.016f));

        REQUIRE(faulty->IsFaulted());
        CHECK(faulty->GetFaultInfo()->phase == phase);
        CHECK(faulty->GetFaultInfo()->unknownException == unknown);
        if (phase == ScriptPhase::Update) CHECK(healthy->counts.update == 1);
        if (phase == ScriptPhase::FixedUpdate) CHECK(healthy->counts.fixedUpdate == 1);
        if (phase == ScriptPhase::LateUpdate) CHECK(healthy->counts.lateUpdate == 1);
      }
    }

    World world;
    auto object = std::make_shared<GameObject>("Unknown exception object");
    auto* script = AttachScript<ConfigurableFaultScript>(*object);
    script->throwEnabled = false;
    world.Add(object);
    world.StartPending();
    script->throwEnabled = true;
    script->throwUnknown = true;
    world.Update(0.0f);
    REQUIRE(script->GetFaultInfo() != nullptr);
    CHECK(script->GetFaultInfo()->unknownException);
    CHECK(script->GetFaultInfo()->exceptionMessage == "unknown C++ exception");
}

TEST_CASE("Script boundary isolates collision and trigger enter stay exit on both sides") {
    ProjectSettings::Get().SetDefaults();
    for (bool unknown : {false, true}) {
      for (ScriptPhase phase : {
               ScriptPhase::CollisionEnter, ScriptPhase::CollisionStay,
               ScriptPhase::CollisionExit, ScriptPhase::TriggerEnter,
               ScriptPhase::TriggerStay, ScriptPhase::TriggerExit}) {
        const bool trigger = phase == ScriptPhase::TriggerEnter ||
                             phase == ScriptPhase::TriggerStay ||
                             phase == ScriptPhase::TriggerExit;
        const bool entering = phase == ScriptPhase::CollisionEnter ||
                              phase == ScriptPhase::TriggerEnter;
        const bool exiting = phase == ScriptPhase::CollisionExit ||
                             phase == ScriptPhase::TriggerExit;

        World world;
        auto first = std::make_shared<GameObject>("Faulty contact side");
        ConfigureContactObject(*first, trigger, false);
        auto* faulty = AttachScript<ConfigurableFaultScript>(*first);
        faulty->throwEnabled = false;
        faulty->throwPhase = phase;
        faulty->throwUnknown = unknown;

        auto second = std::make_shared<GameObject>("Healthy contact side");
        ConfigureContactObject(*second, false, true);
        auto* secondTransform = second->GetComponent<Transform>();
        secondTransform->SetPosition(10.0f, 10.0f);
        auto* healthy = AttachScript<HealthyPeerScript>(*second);
        world.Add(first);
        world.Add(second);
        world.StartPending();

        if (entering) faulty->throwEnabled = true;
        world.FixedStep(1.0f / 60.0f);
        if (!entering) {
            faulty->throwEnabled = true;
            if (exiting) secondTransform->SetPosition(200.0f, 200.0f);
            else secondTransform->SetPosition(10.0f, 10.0f);
            world.FixedStep(1.0f / 60.0f);
        }

        REQUIRE(faulty->IsFaulted());
        CHECK(faulty->GetFaultInfo()->phase == phase);
        CHECK(faulty->GetFaultInfo()->unknownException == unknown);
        // The counterpart's callback is later in the same complete dispatch
        // plan and must still run after the first side faults.
        CHECK(ContactCount(healthy->counts, phase) == 1);
      }
    }
    ProjectSettings::Get().SetDefaults();
}

TEST_CASE("Fault transition records secondary OnDisable failure only once") {
    Log::ClearSinks();
    auto ring = std::make_shared<Log::RingBufferSink>(16);
    Log::AddSink(ring);

    World world;
    auto object = std::make_shared<GameObject>("Named fault object");
    auto* script = AttachScript<ConfigurableFaultScript>(*object);
    script->throwEnabled = false;
    script->throwFromDisableDuringIsolation = true;
    world.Add(object);
    world.StartPending();
    ring->Clear();
    script->throwEnabled = true;

    CHECK_NOTHROW(world.Update(0.0f));
    CHECK_NOTHROW(world.Update(0.0f));
    REQUIRE(script->GetFaultInfo() != nullptr);
    CHECK(script->counts.update == 1);
    CHECK(script->counts.disable == 1);
    CHECK(script->GetFaultInfo()->secondaryExceptionMessage ==
          "secondary disable failure");

    const auto messages = ring->Snapshot();
    REQUIRE(messages.size() == 2);
    CHECK(messages[0].message.find("Named fault object") != std::string::npos);
    CHECK(messages[0].message.find(std::to_string(object->GetID())) != std::string::npos);
    CHECK(messages[0].message.find("ConfigurableFaultScript") != std::string::npos);
    CHECK(messages[0].message.find(std::to_string(script->GetInstanceID())) !=
          std::string::npos);
    CHECK(messages[0].message.find("Update") != std::string::npos);
    CHECK(messages[0].message.find("intentional script failure") !=
          std::string::npos);
    CHECK(messages[1].message.find("secondary disable failure") !=
          std::string::npos);
    Log::ClearSinks();
}

TEST_CASE("Fault isolation does not repeat OnDisable after callback deactivation") {
    World world;
    auto object = std::make_shared<GameObject>("Deactivate then fault");
    auto* script = AttachScript<ConfigurableFaultScript>(*object);
    script->throwEnabled = false;
    world.Add(object);
    world.StartPending();
    script->throwEnabled = true;
    script->deactivateBeforeUpdateFault = true;

    CHECK_NOTHROW(world.Update(0.0f));
    REQUIRE(script->IsFaulted());
    CHECK(script->counts.update == 1);
    CHECK(script->counts.disable == 1);
}

TEST_CASE("Explicit re-enable retries unfinished lifecycle inside the boundary") {
    World world;
    auto object = std::make_shared<GameObject>("Retry object");
    auto* script = AttachScript<ConfigurableFaultScript>(*object);
    script->throwPhase = ScriptPhase::Awake;
    world.Add(object);
    world.StartPending();

    REQUIRE(script->IsFaulted());
    CHECK_FALSE(script->HasAwoken());
    CHECK_FALSE(script->HasStarted());
    CHECK(script->counts.awake == 1);
    CHECK(script->counts.disable == 1);

    // A failed recovery is isolated again and remains explicitly recoverable.
    CHECK_NOTHROW(script->SetEnabled(true));
    CHECK(script->IsFaulted());
    CHECK_FALSE(script->IsEnabled());
    CHECK(script->counts.awake == 2);
    CHECK(script->counts.disable == 2);

    script->throwEnabled = false;
    script->SetEnabled(true);
    CHECK_FALSE(script->IsFaulted());
    CHECK(script->IsEnabled());
    CHECK(script->HasAwoken());
    CHECK(script->HasStarted());
    CHECK(script->counts.awake == 3);
    CHECK(script->counts.enable == 1);
    CHECK(script->counts.start == 1);
    CHECK(script->counts.disable == 2);
}

TEST_CASE("A regular OnDisable exception is isolated without escaping") {
    for (bool unknown : {false, true}) {
        World world;
        auto faultyObject = std::make_shared<GameObject>("Disable fault object");
        auto* faulty = AttachScript<ConfigurableFaultScript>(*faultyObject);
        faulty->throwEnabled = false;
        auto healthyObject = std::make_shared<GameObject>("Disable peer");
        auto* healthy = AttachScript<HealthyPeerScript>(*healthyObject);
        world.Add(faultyObject);
        world.Add(healthyObject);
        world.StartPending();
        faulty->throwPhase = ScriptPhase::OnDisable;
        faulty->throwEnabled = true;
        faulty->throwUnknown = unknown;

        CHECK_NOTHROW(faultyObject->SetActive(false));
        REQUIRE(faulty->GetFaultInfo() != nullptr);
        CHECK(faulty->GetFaultInfo()->phase == ScriptPhase::OnDisable);
        CHECK(faulty->GetFaultInfo()->unknownException == unknown);
        CHECK_FALSE(faulty->IsEnabled());

        healthyObject->SetActive(false);
        CHECK(healthy->counts.disable == 1);
    }
}

TEST_CASE("Script timers and coroutines are isolated and all owned work is canceled") {
    for (bool unknown : {false, true}) {
        World world;
        int canceledTimerCalls = 0;
        int canceledCoroutineCalls = 0;
        int healthyTimerCalls = 0;
        int healthyCoroutineCalls = 0;

        auto faultyObject = std::make_shared<GameObject>("Timer fault");
        auto* faulty = AttachScript<TimerFaultScript>(*faultyObject);
        faulty->canceledTimerCalls = &canceledTimerCalls;
        faulty->canceledCoroutineCalls = &canceledCoroutineCalls;
        faulty->throwUnknown = unknown;
        auto healthyObject = std::make_shared<GameObject>("Timer peer");
        auto* healthy = AttachScript<ScheduledHealthyScript>(*healthyObject);
        healthy->timerCalls = &healthyTimerCalls;
        healthy->coroutineCalls = &healthyCoroutineCalls;
        world.Add(faultyObject);
        world.Add(healthyObject);
        world.StartPending();

        CHECK_NOTHROW(world.Update(0.0f));
        REQUIRE(faulty->GetFaultInfo() != nullptr);
        CHECK(faulty->GetFaultInfo()->phase == ScriptPhase::Invoke);
        CHECK(faulty->GetFaultInfo()->unknownException == unknown);
        CHECK(canceledTimerCalls == 0);
        CHECK(canceledCoroutineCalls == 0);
        CHECK(healthyTimerCalls == 1);
        CHECK(healthyCoroutineCalls == 1);
        CHECK(world.GetScheduler()->ActiveTimerCount() == 0);
        CHECK(world.GetScheduler()->ActiveCoroutineCount() == 0);
    }
}

TEST_CASE("A coroutine exception faults only its owning Script") {
    for (bool unknown : {false, true}) {
        World world;
        int healthyTimerCalls = 0;
        int healthyCoroutineCalls = 0;
        auto faultyObject = std::make_shared<GameObject>("Coroutine fault");
        auto* faulty = AttachScript<CoroutineFaultScript>(*faultyObject);
        faulty->throwUnknown = unknown;
        auto healthyObject = std::make_shared<GameObject>("Coroutine peer");
        auto* healthy = AttachScript<ScheduledHealthyScript>(*healthyObject);
        healthy->timerCalls = &healthyTimerCalls;
        healthy->coroutineCalls = &healthyCoroutineCalls;
        world.Add(faultyObject);
        world.Add(healthyObject);
        world.StartPending();

        CHECK_NOTHROW(world.Update(0.0f));
        REQUIRE(faulty->GetFaultInfo() != nullptr);
        CHECK(faulty->GetFaultInfo()->phase == ScriptPhase::Coroutine);
        CHECK(faulty->GetFaultInfo()->unknownException == unknown);
        CHECK(healthyTimerCalls == 1);
        CHECK(healthyCoroutineCalls == 1);
    }
}

TEST_CASE("Deferred Script work skips a deleted and replaced instance") {
    World world;
    int calls = 0;
    auto object = std::make_shared<GameObject>("Replacement object");
    auto* original = AttachScript<DeferredProbeScript>(*object);
    world.Add(object);
    original->Schedule(&calls);
    const std::uint64_t oldInstance = original->GetInstanceID();

    object->RemoveComponent<DeferredProbeScript>();
    auto* replacement = AttachScript<DeferredProbeScript>(*object);
    CHECK(replacement->GetInstanceID() != oldInstance);

    world.Update(0.0f);
    CHECK(calls == 0);
    CHECK_FALSE(replacement->IsFaulted());
    CHECK(world.GetScheduler()->ActiveTimerCount() == 0);
}

TEST_CASE("Scheduler restores ticking and pending queues after fail-loud callback exceptions") {
    Scheduler scheduler;
    int pendingCalls = 0;
    int laterCalls = 0;
    int owner = 0;
    int pendingOwner = 0;
    scheduler.InvokeRepeating(&owner, 1, [&] {
        scheduler.Invoke(&pendingOwner, 1, [&] { ++pendingCalls; }, 0.0f);
        throw std::runtime_error("generic scheduler failure");
    }, 0.0f, 1.0f);

    CHECK_THROWS_AS(scheduler.Tick(0.0f), std::runtime_error);
    // This must join the active queue directly. A stale ticking_ flag would
    // incorrectly defer it again.
    scheduler.Invoke(&laterCalls, 1, [&] { ++laterCalls; }, 0.0f);
    CHECK_NOTHROW(scheduler.Tick(0.0f));
    CHECK(pendingCalls == 1);
    CHECK(laterCalls == 1);
    CHECK(scheduler.ActiveTimerCount() == 0);

    SUBCASE("generic null ownership does not alias Script handles") {
        const ScriptHandle handle{7, 11, 13};
        scheduler.Invoke(handle, [] {}, 1.0f);
        scheduler.StartCoroutine(handle, [](float) { return true; });
        scheduler.CancelInvoke(nullptr);
        scheduler.StopCoroutines(nullptr);
        CHECK(scheduler.ActiveTimerCount() == 1);
        CHECK(scheduler.ActiveCoroutineCount() == 1);
    }
}

TEST_CASE("Non-Script Component exceptions preserve fail-loud behavior") {
    SUBCASE("Update") {
        World world;
        auto object = std::make_shared<GameObject>("Engine component");
        object->AddComponent<ThrowingUpdateComponent>();
        world.Add(object);
        CHECK_THROWS_AS(world.Update(0.0f), std::runtime_error);
        CHECK_FALSE(world.IsDispatchingCallbacks());
    }

    SUBCASE("Awake") {
        World world;
        auto object = std::make_shared<GameObject>("Engine lifecycle component");
        object->AddComponent<ThrowingAwakeComponent>();
        world.Add(object);
        CHECK_THROWS_AS(world.StartPending(), std::runtime_error);
        CHECK_FALSE(world.IsDispatchingCallbacks());
    }
}
