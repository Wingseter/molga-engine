#include "doctest.h"

#include "Core/EventBus.h"
#include "Core/Events/SceneEvents.h"
#include "Core/SceneRuntime.h"
#include "Core/World.h"
#include "ECS/ComponentFactory.h"
#include "ECS/Components/UIButton.h"
#include "ECS/GameObject.h"
#include "Editor/SceneDocument.h"
#include "Scripting/BuiltinScripts.h"
#include "Scripting/Script.h"
#include "SmokeTestSupport.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::string>* g_lifecycleLog = nullptr;

class SceneRuntimeProbe : public Component {
public:
    COMPONENT_TYPE(SceneRuntimeProbe)

    void Deserialize(const nlohmann::json& json) override {
        label = json.value("label", label);
        throwOnAwake = json.value("throwOnAwake", throwOnAwake);
        throwOnStart = json.value("throwOnStart", throwOnStart);
        throwOnResolve = json.value("throwOnResolve", throwOnResolve);
        throwOnDestroy = json.value("throwOnDestroy", throwOnDestroy);
    }
    void Serialize(nlohmann::json& json) const override {
        json["label"] = label;
        json["throwOnAwake"] = throwOnAwake;
        json["throwOnStart"] = throwOnStart;
        json["throwOnResolve"] = throwOnResolve;
        json["throwOnDestroy"] = throwOnDestroy;
    }

    void Awake() override {
        Record("Awake");
        if (throwOnAwake) throw std::runtime_error("probe Awake failure");
    }
    void Start() override {
        Record("Start");
        if (throwOnStart) throw std::runtime_error("probe Start failure");
    }
    void OnEnable() override { Record("OnEnable"); }
    void OnDisable() override { Record("OnDisable"); }
    void OnDestroy() override {
        Record("OnDestroy");
        if (throwOnDestroy) throw std::runtime_error("probe OnDestroy failure");
    }
    void ResolveAssets() override {
        Record("ResolveAssets");
        if (throwOnResolve) throw std::runtime_error("probe ResolveAssets failure");
    }

    std::string label;
    bool throwOnAwake = false;
    bool throwOnStart = false;
    bool throwOnResolve = false;
    bool throwOnDestroy = false;

private:
    void Record(const char* action) {
        if (g_lifecycleLog) g_lifecycleLog->push_back(label + ":" + action);
    }
};

class SceneRequestScript : public Script {
public:
    SCRIPT_CLASS(SceneRequestScript)
};

class InitialSceneRequestScript : public Script {
public:
    SCRIPT_CLASS(InitialSceneRequestScript)

    void Start() override {
        observedScenePath = GetActiveScenePath();
        requestAccepted = LoadScene(targetScenePath);
        SceneRuntime* runtime = GetGameObject() && GetGameObject()->GetWorld()
            ? GetGameObject()->GetWorld()->GetSceneRuntime() : nullptr;
        nestedCommitAccepted = runtime && runtime->CommitPendingLoad();
    }

    std::string targetScenePath;
    std::string observedScenePath;
    bool requestAccepted = false;
    bool nestedCommitAccepted = false;
};

class ReentrantSceneRequestProbe : public Script {
public:
    SCRIPT_CLASS(ReentrantSceneRequestProbe)

    void Deserialize(const nlohmann::json& json) override {
        targetScenePath = json.value("targetScenePath", targetScenePath);
    }
    void Serialize(nlohmann::json& json) const override {
        json["targetScenePath"] = targetScenePath;
    }
    void Start() override {
        requestAccepted = LoadScene(targetScenePath);
        SceneRuntime* runtime = GetGameObject() && GetGameObject()->GetWorld()
            ? GetGameObject()->GetWorld()->GetSceneRuntime() : nullptr;
        nestedCommitAccepted = runtime && runtime->CommitPendingLoad();
    }

    std::string targetScenePath;
    bool requestAccepted = false;
    bool nestedCommitAccepted = false;
};

class UpdateSceneCommitProbe : public Script {
public:
    SCRIPT_CLASS(UpdateSceneCommitProbe)

    void Update(float) override {
        if (attempted) return;
        attempted = true;
        SceneRuntime* runtime = GetGameObject() && GetGameObject()->GetWorld()
            ? GetGameObject()->GetWorld()->GetSceneRuntime() : nullptr;
        if (runtime) runtime->Shutdown();
        shutdownWasRejected = runtime && runtime->HasActiveWorld();
        requestAccepted = LoadScene(targetScenePath);
        nestedCommitAccepted = runtime && runtime->CommitPendingLoad();
    }

    std::string targetScenePath;
    bool attempted = false;
    bool shutdownWasRejected = false;
    bool requestAccepted = false;
    bool nestedCommitAccepted = false;
};

void RegisterProbe() {
    ComponentFactory::Get().Register<SceneRuntimeProbe>(SceneRuntimeProbe::StaticTypeName());
}

void WriteScene(const std::filesystem::path& path,
                const std::string& objectName,
                const std::string& probeLabel = {},
                const std::string& failurePhase = {}) {
    nlohmann::json components = nlohmann::json::array();
    if (!probeLabel.empty()) {
        components.push_back({
            {"type", "SceneRuntimeProbe"},
            {"enabled", true},
            {"label", probeLabel},
            {"throwOnAwake", failurePhase == "Awake"},
            {"throwOnStart", failurePhase == "Start"},
            {"throwOnResolve", failurePhase == "ResolveAssets"},
            {"throwOnDestroy", failurePhase == "OnDestroy"}
        });
    }
    test_support::WriteText(path, nlohmann::json{
        {"version", "1.0"},
        {"name", objectName + " Scene"},
        {"gameObjects", nlohmann::json::array({{
            {"name", objectName},
            {"id", objectName == "B" ? 200u : 300u},
            {"active", true},
            {"parentId", -1},
            {"components", std::move(components)}
        }})}
    }.dump(2));
}

std::unique_ptr<World> MakeInitialWorld(const std::string& probeLabel = {}) {
    auto world = std::make_unique<World>();
    auto object = std::make_shared<GameObject>("A");
    if (!probeLabel.empty()) {
        auto* probe = object->AddComponent<SceneRuntimeProbe>();
        probe->label = probeLabel;
    }
    world->Add(object);
    return world;
}

} // namespace

TEST_CASE("SceneRuntime commits a prepared scene with deterministic lifecycle events") {
    RegisterProbe();
    test_support::TempDirectory temp{"scene-runtime-order"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B", "B");

    std::vector<std::string> order;
    g_lifecycleLog = &order;
    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld("A"), "Scenes/a.json"));
    order.clear();

    const auto unloadSub = EventBus::Subscribe<SceneUnloadEvent>([&](SceneUnloadEvent& event) {
        order.push_back("Unload:" + event.scenePath);
    });
    const auto loadSub = EventBus::Subscribe<SceneLoadEvent>([&](SceneLoadEvent& event) {
        order.push_back("Load:" + event.scenePath);
    });

    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    CHECK(runtime.IsSceneLoadPending());
    REQUIRE(runtime.CommitPendingLoad());

    const std::vector<std::string> expected = {
        "B:ResolveAssets",
        "Unload:Scenes/a.json",
        "A:OnDisable",
        "A:OnDestroy",
        "B:Awake",
        "B:OnEnable",
        "B:Start",
        "Load:Scenes/b.json"
    };
    CHECK(order == expected);
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
    REQUIRE(runtime.ActiveWorld().Objects().size() == 1);
    CHECK(runtime.ActiveWorld().Objects().front()->GetName() == "B");

    EventBus::Unsubscribe(unloadSub);
    EventBus::Unsubscribe(loadSub);
    g_lifecycleLog = nullptr;
}

TEST_CASE("SceneRuntime exposes and preserves an initial Start scene request") {
    test_support::TempDirectory temp{"scene-runtime-initial-start-request"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    auto initial = std::make_unique<World>();
    auto object = std::make_shared<GameObject>("Initial Requester");
    auto* requester = static_cast<InitialSceneRequestScript*>(
        object->AddComponentRaw(new InitialSceneRequestScript()));
    requester->targetScenePath = "Scenes/b.json";
    initial->Add(object);

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(std::move(initial), "Scenes/a.json"));

    CHECK(requester->observedScenePath == "Scenes/a.json");
    CHECK(requester->requestAccepted);
    CHECK_FALSE(requester->nestedCommitAccepted);
    CHECK(runtime.IsSceneLoadPending());
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
}

TEST_CASE("SceneRuntime ignores Shutdown requested from transition event subscribers") {
    test_support::TempDirectory temp{"scene-runtime-event-shutdown"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));

    int unloads = 0;
    int loads = 0;
    const auto unloadSub = EventBus::Subscribe<SceneUnloadEvent>(
        [&](SceneUnloadEvent&) {
            ++unloads;
            runtime.Shutdown();
            CHECK(runtime.HasActiveWorld());
            CHECK(runtime.CurrentScenePath() == "Scenes/a.json");
        });
    const auto loadSub = EventBus::Subscribe<SceneLoadEvent>(
        [&](SceneLoadEvent&) {
            ++loads;
            runtime.Shutdown();
            CHECK(runtime.HasActiveWorld());
            CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
        });

    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(unloads == 1);
    CHECK(loads == 1);
    CHECK(runtime.HasActiveWorld());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
    CHECK_FALSE(runtime.IsSceneLoadPending());
    REQUIRE(runtime.ActiveWorld().Objects().size() == 1);
    CHECK(runtime.ActiveWorld().Objects().front()->GetName() == "B");

    EventBus::Unsubscribe(unloadSub);
    EventBus::Unsubscribe(loadSub);
}

TEST_CASE("SceneDocument keeps Play ownership when ExitPlay is requested during commit") {
    test_support::TempDirectory temp{"scene-document-reentrant-exit"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    SceneDocument document;
    document.EditWorld().Add(std::make_shared<GameObject>("Edit A"));
    REQUIRE(document.EnterPlay({{"Scenes/b.json", sceneB.string()}},
                               "Scenes/a.json"));

    int exitAttempts = 0;
    const auto unloadSub = EventBus::Subscribe<SceneUnloadEvent>(
        [&](SceneUnloadEvent&) {
            ++exitAttempts;
            document.ExitPlay();
            CHECK(document.IsPlaying());
        });

    SceneRuntime* runtime = document.PlayRuntime();
    REQUIRE(runtime != nullptr);
    REQUIRE(runtime->RequestLoad("Scenes/b.json"));
    REQUIRE(runtime->CommitPendingLoad());
    CHECK(exitAttempts == 1);
    CHECK(document.IsPlaying());
    CHECK(document.PlayRuntime() == runtime);
    CHECK(document.PlayRuntime()->CurrentScenePath() == "Scenes/b.json");

    EventBus::Unsubscribe(unloadSub);
    document.ExitPlay();
    CHECK_FALSE(document.IsPlaying());
}

TEST_CASE("SceneRuntime defers a nested Start commit to the next frame") {
    ComponentFactory::Get().Register<ReentrantSceneRequestProbe>(
        ReentrantSceneRequestProbe::StaticScriptName());
    test_support::TempDirectory temp{"scene-runtime-reentrant-start"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    const auto sceneC = temp.Path() / "Scenes/c.json";
    test_support::WriteText(sceneB, nlohmann::json{
        {"version", "1.0"},
        {"name", "B Scene"},
        {"gameObjects", nlohmann::json::array({{
            {"name", "B"}, {"id", 200u}, {"active", true}, {"parentId", -1},
            {"components", nlohmann::json::array({{
                {"type", "ReentrantSceneRequestProbe"},
                {"enabled", true},
                {"targetScenePath", "Scenes/c.json"}
            }})}
        }})}
    }.dump(2));
    WriteScene(sceneC, "C");

    SceneRuntime runtime({
        {"Scenes/b.json", sceneB.string()},
        {"Scenes/c.json", sceneC.string()}
    });
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));
    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
    REQUIRE(runtime.ActiveWorld().Objects().size() == 1);
    auto* probe = runtime.ActiveWorld().Objects().front()
        ->GetComponent<ReentrantSceneRequestProbe>();
    REQUIRE(probe != nullptr);
    CHECK(probe->requestAccepted);
    CHECK_FALSE(probe->nestedCommitAccepted);
    CHECK(runtime.IsSceneLoadPending());

    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/c.json");
}

TEST_CASE("SceneRuntime rejects a commit from World Update until frame boundary") {
    test_support::TempDirectory temp{"scene-runtime-update-commit"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    auto initial = std::make_unique<World>();
    auto object = std::make_shared<GameObject>("Update Requester");
    auto* requester = static_cast<UpdateSceneCommitProbe*>(
        object->AddComponentRaw(new UpdateSceneCommitProbe()));
    requester->targetScenePath = "Scenes/b.json";
    initial->Add(object);

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(std::move(initial), "Scenes/a.json"));

    CHECK_NOTHROW(runtime.ActiveWorld().Update(0.0f));
    CHECK(requester->attempted);
    CHECK(requester->shutdownWasRejected);
    CHECK(requester->requestAccepted);
    CHECK_FALSE(requester->nestedCommitAccepted);
    CHECK(runtime.CurrentScenePath() == "Scenes/a.json");
    CHECK(runtime.IsSceneLoadPending());

    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
}

TEST_CASE("SceneRuntime rolls back the exact active World when candidate preparation fails") {
    RegisterProbe();
    test_support::TempDirectory temp{"scene-runtime-preparation-rollback"};
    const auto failResolve = temp.Path() / "Scenes/fail-resolve.json";
    WriteScene(failResolve, "FailResolve", "FailResolve", "ResolveAssets");

    std::vector<std::string> lifecycle;
    g_lifecycleLog = &lifecycle;
    SceneRuntime runtime({{"Scenes/fail-resolve.json", failResolve.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld("A"), "Scenes/a.json"));
    lifecycle.clear();

    World* const originalWorld = &runtime.ActiveWorld();
    int unloads = 0;
    int loads = 0;
    int failures = 0;
    const auto unloadSub = EventBus::Subscribe<SceneUnloadEvent>(
        [&](SceneUnloadEvent&) { ++unloads; });
    const auto loadSub = EventBus::Subscribe<SceneLoadEvent>(
        [&](SceneLoadEvent&) { ++loads; });
    const auto failureSub = EventBus::Subscribe<SceneLoadFailedEvent>(
        [&](SceneLoadFailedEvent&) { ++failures; });

    REQUIRE(runtime.RequestLoad("Scenes/fail-resolve.json"));
    bool committed = true;
    CHECK_NOTHROW(committed = runtime.CommitPendingLoad());
    CHECK_FALSE(committed);
    CHECK(&runtime.ActiveWorld() == originalWorld);
    CHECK(runtime.CurrentScenePath() == "Scenes/a.json");
    CHECK(runtime.ActiveWorld().Objects().front()->GetName() == "A");
    CHECK(runtime.LastError().find("Failed to prepare scene") != std::string::npos);

    CHECK(unloads == 0);
    CHECK(loads == 0);
    CHECK(failures == 1);
    CHECK(std::none_of(lifecycle.begin(), lifecycle.end(), [](const std::string& entry) {
        return entry.rfind("A:OnDisable", 0) == 0 || entry.rfind("A:OnDestroy", 0) == 0;
    }));

    EventBus::Unsubscribe(unloadSub);
    EventBus::Unsubscribe(loadSub);
    EventBus::Unsubscribe(failureSub);
    g_lifecycleLog = nullptr;
}

TEST_CASE("SceneRuntime keeps the committed World coherent when candidate Start throws") {
    RegisterProbe();
    test_support::TempDirectory temp{"scene-runtime-start-exception"};
    const auto failStart = temp.Path() / "Scenes/fail-start.json";
    WriteScene(failStart, "FailStart", "FailStart", "Start");

    SceneRuntime runtime({{"Scenes/fail-start.json", failStart.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));
    int loads = 0;
    const auto loadSub = EventBus::Subscribe<SceneLoadEvent>(
        [&](SceneLoadEvent&) { ++loads; });

    REQUIRE(runtime.RequestLoad("Scenes/fail-start.json"));
    CHECK_NOTHROW(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/fail-start.json");
    REQUIRE(runtime.ActiveWorld().Objects().size() == 1);
    CHECK(runtime.ActiveWorld().Objects().front()->GetName() == "FailStart");
    CHECK(runtime.LastError().find("Start callback failed") != std::string::npos);
    CHECK_FALSE(runtime.IsSceneLoadPending());
    CHECK(loads == 1);

    EventBus::Unsubscribe(loadSub);
}

TEST_CASE("SceneRuntime remains usable when an unload subscriber throws") {
    test_support::TempDirectory temp{"scene-runtime-unload-exception"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));
    World* const originalWorld = &runtime.ActiveWorld();

    const auto throwingSub = EventBus::Subscribe<SceneUnloadEvent>(
        [](SceneUnloadEvent&) { throw std::runtime_error("unload subscriber failure"); });
    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    bool committed = true;
    CHECK_NOTHROW(committed = runtime.CommitPendingLoad());
    CHECK_FALSE(committed);
    CHECK(&runtime.ActiveWorld() == originalWorld);
    CHECK(runtime.CurrentScenePath() == "Scenes/a.json");
    EventBus::Unsubscribe(throwingSub);

    // Both SceneRuntime's commit guard and EventBus's publishing guard must
    // have been restored after the exception.
    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
}

TEST_CASE("SceneRuntime completes outgoing destruction when a component callback throws") {
    RegisterProbe();
    test_support::TempDirectory temp{"scene-runtime-destroy-exception"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    auto initial = MakeInitialWorld("A");
    auto* probe = initial->Objects().front()->GetComponent<SceneRuntimeProbe>();
    REQUIRE(probe != nullptr);
    probe->throwOnDestroy = true;

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(std::move(initial), "Scenes/a.json"));
    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    CHECK_NOTHROW(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
    REQUIRE(runtime.ActiveWorld().Objects().size() == 1);
    CHECK(runtime.ActiveWorld().Objects().front()->GetName() == "B");
}

TEST_CASE("SceneRuntime reports when SetCatalog invalidates a pending request") {
    test_support::TempDirectory temp{"scene-runtime-catalog-invalidation"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));
    int failures = 0;
    const auto failureSub = EventBus::Subscribe<SceneLoadFailedEvent>(
        [&](SceneLoadFailedEvent&) { ++failures; });

    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    runtime.SetCatalog({});
    CHECK_FALSE(runtime.IsSceneLoadPending());
    CHECK(failures == 1);
    CHECK(runtime.LastError().find("removed from the scene catalog") != std::string::npos);

    EventBus::Unsubscribe(failureSub);
}

TEST_CASE("SceneRuntime keeps the active World on load failure and first valid request wins") {
    test_support::TempDirectory temp{"scene-runtime-rollback"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    const auto sceneC = temp.Path() / "Scenes/c.json";
    const auto malformed = temp.Path() / "Scenes/malformed.json";
    WriteScene(sceneB, "B");
    WriteScene(sceneC, "C");
    test_support::WriteText(malformed, "{ definitely not json");

    SceneRuntime runtime({
        {"Scenes/b.json", sceneB.string()},
        {"Scenes/c.json", sceneC.string()},
        {"Scenes/missing.json", (temp.Path() / "Scenes/missing.json").string()},
        {"Scenes/malformed.json", malformed.string()}
    });
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));

    int failures = 0;
    const auto failureSub = EventBus::Subscribe<SceneLoadFailedEvent>(
        [&](SceneLoadFailedEvent&) { ++failures; });

    CHECK_FALSE(runtime.RequestLoad("Scenes/not-registered.json"));
    CHECK(failures == 1);
    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    CHECK_FALSE(runtime.RequestLoad("Scenes/c.json"));
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");

    World* active = &runtime.ActiveWorld();
    REQUIRE(runtime.RequestLoad("Scenes/missing.json"));
    CHECK_FALSE(runtime.CommitPendingLoad());
    CHECK(&runtime.ActiveWorld() == active);
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
    CHECK_FALSE(runtime.LastError().empty());
    CHECK(failures == 2);

    REQUIRE(runtime.RequestLoad("Scenes/malformed.json"));
    CHECK_FALSE(runtime.CommitPendingLoad());
    CHECK(&runtime.ActiveWorld() == active);
    CHECK(failures == 3);

    // Same-path requests are explicit reloads, then another transition works.
    REQUIRE(runtime.RequestLoad("Scenes/b.json"));
    REQUIRE(runtime.CommitPendingLoad());
    REQUIRE(runtime.RequestLoad("Scenes/c.json"));
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/c.json");

    EventBus::Unsubscribe(failureSub);
}

TEST_CASE("SceneRuntime rolls back when a scene prefab cannot be resolved") {
    test_support::TempDirectory temp{"scene-runtime-missing-prefab"};
    const auto scene = temp.Path() / "Scenes/missing-prefab.json";
    test_support::WriteText(scene, nlohmann::json{
        {"version", "1.0"},
        {"name", "Missing Prefab"},
        {"gameObjects", nlohmann::json::array({{
            {"prefabInstance", {
                {"guid", "ffffffffffffffffffffffffffffffff"},
                {"rootId", 500u},
                {"parentId", -1},
                {"modifications", nlohmann::json::array()}
            }}
        }})}
    }.dump(2));

    SceneRuntime runtime({{"Scenes/missing-prefab.json", scene.string()}});
    REQUIRE(runtime.SetInitialWorld(MakeInitialWorld(), "Scenes/a.json"));
    World* const originalWorld = &runtime.ActiveWorld();
    REQUIRE(runtime.RequestLoad("Scenes/missing-prefab.json"));
    CHECK_FALSE(runtime.CommitPendingLoad());
    CHECK(&runtime.ActiveWorld() == originalWorld);
    CHECK(runtime.CurrentScenePath() == "Scenes/a.json");
    CHECK(runtime.LastError().find("Failed to load scene") != std::string::npos);
}

TEST_CASE("Script scene helpers route through the owning World runtime") {
    test_support::TempDirectory temp{"scene-runtime-script"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    auto initial = std::make_unique<World>();
    auto object = std::make_shared<GameObject>("Requester");
    auto* script = static_cast<SceneRequestScript*>(
        object->AddComponentRaw(new SceneRequestScript()));
    initial->Add(object);

    SceneRuntime runtime({{"Scenes/b.json", sceneB.string()}});
    REQUIRE(runtime.SetInitialWorld(std::move(initial), "Scenes/a.json"));
    CHECK(script->GetActiveScenePath() == "Scenes/a.json");
    CHECK_FALSE(script->IsSceneLoadPending());
    REQUIRE(script->LoadScene("Scenes/b.json"));
    CHECK(script->IsSceneLoadPending());
    CHECK_FALSE(script->LoadScene("Scenes/b.json"));
    REQUIRE(runtime.CommitPendingLoad());
    CHECK(runtime.CurrentScenePath() == "Scenes/b.json");
}

TEST_CASE("SceneLoadButton restores its callback after re-enabling") {
    auto initial = std::make_unique<World>();
    auto object = std::make_shared<GameObject>("Scene Button");
    auto* button = object->AddComponent<UIButton>();
    auto* loader = object->AddComponent<SceneLoadButton>();
    loader->scenePath = "Scenes/b.json";
    initial->Add(object);

    SceneRuntime runtime({{"Scenes/b.json", "unused-for-request.json"}});
    REQUIRE(runtime.SetInitialWorld(std::move(initial), "Scenes/a.json"));

    loader->SetEnabled(false);
    button->ApplyPointerState(true, false, true);
    CHECK_FALSE(runtime.IsSceneLoadPending());

    loader->SetEnabled(true);
    button->ApplyPointerState(true, false, true);
    CHECK(runtime.IsSceneLoadPending());
}

TEST_CASE("SceneDocument restores edit World after its play runtime changes scenes") {
    test_support::TempDirectory temp{"scene-document-transition"};
    const auto sceneB = temp.Path() / "Scenes/b.json";
    WriteScene(sceneB, "B");

    SceneDocument document;
    auto editObject = std::make_shared<GameObject>("EditOnly");
    document.EditWorld().Add(editObject);

    REQUIRE(document.EnterPlay({{"Scenes/b.json", sceneB.string()}}, "Scenes/a.json"));
    REQUIRE(document.PlayRuntime() != nullptr);
    REQUIRE(document.PlayRuntime()->RequestLoad("Scenes/b.json"));
    REQUIRE(document.PlayRuntime()->CommitPendingLoad());
    CHECK(document.ActiveWorld().Objects().front()->GetName() == "B");

    document.ExitPlay();
    CHECK_FALSE(document.IsPlaying());
    REQUIRE(document.EditWorld().Objects().size() == 1);
    CHECK(document.EditWorld().Objects().front()->GetName() == "EditOnly");
}
