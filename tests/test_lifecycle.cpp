#include "doctest.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "Scripting/Script.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/CircleCollider2D.h"
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::string> g_log;

class LifecycleScript : public Script {
public:
    SCRIPT_CLASS(LifecycleScript)
    std::string tag;
    void Awake() override     { g_log.push_back(tag + ":Awake"); }
    void Start() override     { g_log.push_back(tag + ":Start"); }
    void OnEnable() override   { g_log.push_back(tag + ":OnEnable"); }
    void OnDisable() override  { g_log.push_back(tag + ":OnDisable"); }
};

LifecycleScript* AttachLifecycle(GameObject* go, const std::string& tag) {
    auto* s = static_cast<LifecycleScript*>(go->AddComponentRaw(new LifecycleScript()));
    s->tag = tag;
    return s;
}

enum class MutationPhase { Update, FixedUpdate, LateUpdate, Render, ResolveAssets, Disable };

struct PhaseCounts {
    int update = 0;
    int fixedUpdate = 0;
    int lateUpdate = 0;
    int render = 0;
    int resolveAssets = 0;
    int disable = 0;
};

class PhaseVictimScript : public Script {
public:
    SCRIPT_CLASS(PhaseVictimScript)
    PhaseCounts* counts = nullptr;

    void Update(float) override { ++counts->update; }
    void FixedUpdate(float) override { ++counts->fixedUpdate; }
    void LateUpdate(float) override { ++counts->lateUpdate; }
    void Render() override { ++counts->render; }
    void ResolveAssets() override { ++counts->resolveAssets; }
    void OnDisable() override { ++counts->disable; }
};

class PhaseMutatorScript : public Script {
public:
    SCRIPT_CLASS(PhaseMutatorScript)
    MutationPhase mutationPhase = MutationPhase::Update;
    PhaseCounts* counts = nullptr;
    PhaseCounts* replacementCounts = nullptr;

    void Update(float) override {
        ++counts->update;
        MutateIf(MutationPhase::Update);
    }
    void FixedUpdate(float) override {
        ++counts->fixedUpdate;
        MutateIf(MutationPhase::FixedUpdate);
    }
    void LateUpdate(float) override {
        ++counts->lateUpdate;
        MutateIf(MutationPhase::LateUpdate);
    }
    void Render() override {
        ++counts->render;
        MutateIf(MutationPhase::Render);
    }
    void ResolveAssets() override {
        ++counts->resolveAssets;
        MutateIf(MutationPhase::ResolveAssets);
    }
    void OnDisable() override {
        ++counts->disable;
        MutateIf(MutationPhase::Disable);
    }

private:
    void MutateIf(MutationPhase callbackPhase) {
        if (mutationPhase != callbackPhase) return;
        GameObject* owner = gameObject;
        PhaseCounts* newCounts = replacementCounts;
        owner->RemoveComponent<PhaseVictimScript>();
        auto* replacement = owner->AddComponent<PhaseVictimScript>();
        replacement->counts = newCounts;
        // Intentionally remove the callback currently on the stack. Dispatch
        // must not dereference it after user code returns.
        owner->RemoveComponent<PhaseMutatorScript>();
    }
};

using PhaseInvoker = void (*)(GameObject&);

void CheckMutationSafePhase(MutationPhase phase, PhaseInvoker invoke,
                            int PhaseCounts::* counter, int originalExpected = 0) {
    PhaseCounts mutatorCounts;
    PhaseCounts originalCounts;
    PhaseCounts replacementCounts;
    auto object = std::make_shared<GameObject>("phase_mutation");
    auto* mutator = static_cast<PhaseMutatorScript*>(
        object->AddComponentRaw(new PhaseMutatorScript()));
    mutator->mutationPhase = phase;
    mutator->counts = &mutatorCounts;
    mutator->replacementCounts = &replacementCounts;
    auto* victim = static_cast<PhaseVictimScript*>(
        object->AddComponentRaw(new PhaseVictimScript()));
    victim->counts = &originalCounts;

    invoke(*object);
    CHECK(mutatorCounts.*counter == 1);
    CHECK(originalCounts.*counter == originalExpected);
    CHECK(replacementCounts.*counter == 0);
    CHECK_FALSE(object->HasComponent<PhaseMutatorScript>());
    REQUIRE(object->HasComponent<PhaseVictimScript>());

    // A replacement introduced during dispatch belongs to the next pass.
    invoke(*object);
    CHECK(replacementCounts.*counter == 1);
}

struct StopDispatchCounts {
    int first = 0;
    int later = 0;
    int laterDestroy = 0;
};

class LaterUpdateScript : public Script {
public:
    SCRIPT_CLASS(LaterUpdateScript)
    StopDispatchCounts* counts = nullptr;
    void Update(float) override { ++counts->later; }
    void OnDestroy() override { ++counts->laterDestroy; }
};

class StopDispatchScript : public Script {
public:
    SCRIPT_CLASS(StopDispatchScript)
    StopDispatchCounts* counts = nullptr;
    bool destroyOwner = false;

    void Update(float) override {
        ++counts->first;
        if (destroyOwner) gameObject->NotifyDestroy();
        else gameObject->SetActive(false);
    }
};

struct StartStopCounts {
    int firstAwake = 0;
    int laterAwake = 0;
    int laterEnable = 0;
    int laterStart = 0;
    int laterDestroy = 0;
};

class LaterStartScript : public Script {
public:
    SCRIPT_CLASS(LaterStartScript)
    StartStopCounts* counts = nullptr;
    void Awake() override { ++counts->laterAwake; }
    void OnEnable() override { ++counts->laterEnable; }
    void Start() override { ++counts->laterStart; }
    void OnDestroy() override { ++counts->laterDestroy; }
};

class DestroyDuringAwakeScript : public Script {
public:
    SCRIPT_CLASS(DestroyDuringAwakeScript)
    StartStopCounts* counts = nullptr;
    void Awake() override {
        ++counts->firstAwake;
        gameObject->NotifyDestroy();
    }
};

} // namespace

TEST_CASE("Lifecycle: all Awake run before all Start across objects") {
    g_log.clear();
    World w;
    auto a = std::make_shared<GameObject>("A");
    AttachLifecycle(a.get(), "A");
    auto b = std::make_shared<GameObject>("B");
    AttachLifecycle(b.get(), "B");
    w.Add(a);
    w.Add(b);

    w.StartPending();

    // Unity 순서: 모든 Awake → 모든 OnEnable → 모든 Start.
    REQUIRE(g_log.size() == 6);
    CHECK(g_log[0] == "A:Awake");
    CHECK(g_log[1] == "B:Awake");
    CHECK(g_log[2] == "A:OnEnable");
    CHECK(g_log[3] == "B:OnEnable");
    CHECK(g_log[4] == "A:Start");
    CHECK(g_log[5] == "B:Start");
}

TEST_CASE("Lifecycle: GetComponents preserves insertion order (deterministic)") {
    GameObject go("GO");
    auto* t = go.AddComponent<Transform>();
    auto* b = go.AddComponent<BoxCollider2D>();
    auto* c = go.AddComponent<CircleCollider2D>();

    auto comps = go.GetComponents();
    REQUIRE(comps.size() == 3);
    CHECK(comps[0] == t);
    CHECK(comps[1] == b);
    CHECK(comps[2] == c);
}

TEST_CASE("Lifecycle: SetActive does not fire callbacks before play (not running)") {
    g_log.clear();
    World w;
    auto go = std::make_shared<GameObject>("GO");
    AttachLifecycle(go.get(), "S");
    w.Add(go);

    go->SetActive(false);  // running_=false -> 플래그만 변경
    go->SetActive(true);
    CHECK(g_log.empty());
}

TEST_CASE("Lifecycle: SetActive fires OnDisable/OnEnable during play") {
    g_log.clear();
    World w;
    auto go = std::make_shared<GameObject>("GO");
    AttachLifecycle(go.get(), "S");
    w.Add(go);

    w.StartPending();   // S:Awake, S:Start, running_=true
    g_log.clear();

    go->SetActive(false);
    REQUIRE(g_log.size() == 1);
    CHECK(g_log[0] == "S:OnDisable");

    g_log.clear();
    go->SetActive(true);
    REQUIRE(g_log.size() == 1);
    CHECK(g_log[0] == "S:OnEnable");  // Awake/Start는 1회뿐, 재실행 안 됨
}

TEST_CASE("Lifecycle: activating an initially-inactive object runs Awake->OnEnable->Start once") {
    g_log.clear();
    World w;
    auto go = std::make_shared<GameObject>("GO");
    AttachLifecycle(go.get(), "S");
    go->SetActive(false);  // 비활성으로 시작 (world 미연결, 플래그만)
    w.Add(go);

    w.StartPending();      // 비활성이라 Awake/Start 건너뜀
    CHECK(g_log.empty());

    go->SetActive(true);   // running_=true -> 최초 활성화
    REQUIRE(g_log.size() == 3);
    CHECK(g_log[0] == "S:Awake");
    CHECK(g_log[1] == "S:OnEnable");
    CHECK(g_log[2] == "S:Start");

    // 다시 토글해도 Awake/Start는 재실행되지 않는다.
    g_log.clear();
    go->SetActive(false);
    go->SetActive(true);
    REQUIRE(g_log.size() == 2);
    CHECK(g_log[0] == "S:OnDisable");
    CHECK(g_log[1] == "S:OnEnable");
}

TEST_CASE("Lifecycle: callback dispatch tolerates component removal and replacement") {
    SUBCASE("Update") {
        CheckMutationSafePhase(MutationPhase::Update,
            [](GameObject& object) { object.Update(0.016f); }, &PhaseCounts::update);
    }
    SUBCASE("FixedUpdate") {
        CheckMutationSafePhase(MutationPhase::FixedUpdate,
            [](GameObject& object) { object.FixedUpdateScripts(0.02f); },
            &PhaseCounts::fixedUpdate);
    }
    SUBCASE("LateUpdate") {
        CheckMutationSafePhase(MutationPhase::LateUpdate,
            [](GameObject& object) { object.LateUpdateScripts(0.016f); },
            &PhaseCounts::lateUpdate);
    }
    SUBCASE("Render") {
        CheckMutationSafePhase(MutationPhase::Render,
            [](GameObject& object) { object.Render(); }, &PhaseCounts::render);
    }
    SUBCASE("ResolveAssets") {
        CheckMutationSafePhase(MutationPhase::ResolveAssets,
            [](GameObject& object) { object.ResolveAssets(); },
            &PhaseCounts::resolveAssets);
    }
    SUBCASE("Disable") {
        // Removing the original victim legitimately disables it once. The
        // replacement must not receive the in-flight disable callback.
        CheckMutationSafePhase(MutationPhase::Disable,
            [](GameObject& object) { object.DisableScripts(); },
            &PhaseCounts::disable, 1);
    }
}

TEST_CASE("Lifecycle: Update stops after its owner becomes inactive or destroyed") {
    SUBCASE("inactive") {
        StopDispatchCounts counts;
        auto object = std::make_shared<GameObject>("deactivate_during_update");
        auto* first = static_cast<StopDispatchScript*>(
            object->AddComponentRaw(new StopDispatchScript()));
        first->counts = &counts;
        auto* later = static_cast<LaterUpdateScript*>(
            object->AddComponentRaw(new LaterUpdateScript()));
        later->counts = &counts;

        object->Update(0.016f);
        CHECK(counts.first == 1);
        CHECK(counts.later == 0);
        CHECK_FALSE(object->IsActive());
    }

    SUBCASE("destroyed") {
        StopDispatchCounts counts;
        auto object = std::make_shared<GameObject>("destroy_during_update");
        auto* first = static_cast<StopDispatchScript*>(
            object->AddComponentRaw(new StopDispatchScript()));
        first->counts = &counts;
        first->destroyOwner = true;
        auto* later = static_cast<LaterUpdateScript*>(
            object->AddComponentRaw(new LaterUpdateScript()));
        later->counts = &counts;

        object->Update(0.016f);
        CHECK(counts.first == 1);
        CHECK(counts.later == 0);
        CHECK(counts.laterDestroy == 1);
    }
}

TEST_CASE("Lifecycle: startup phases stop after an object is destroyed during Awake") {
    StartStopCounts counts;
    auto object = std::make_shared<GameObject>("destroy_during_awake");
    auto* first = static_cast<DestroyDuringAwakeScript*>(
        object->AddComponentRaw(new DestroyDuringAwakeScript()));
    first->counts = &counts;
    auto* later = static_cast<LaterStartScript*>(
        object->AddComponentRaw(new LaterStartScript()));
    later->counts = &counts;

    object->AwakeScripts();
    object->EnableScripts();
    object->StartScripts();

    CHECK(counts.firstAwake == 1);
    CHECK(counts.laterAwake == 0);
    CHECK(counts.laterEnable == 0);
    CHECK(counts.laterStart == 0);
    CHECK(counts.laterDestroy == 1);
}
