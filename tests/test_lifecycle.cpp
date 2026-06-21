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
