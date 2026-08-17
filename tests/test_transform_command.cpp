#include "Editor/Commands/TransformCommand.h"
#include "Core/PathService.h"
#include "Core/PrefabRegistry.h"
#include "Core/SceneSerializer.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/PrefabInstance.h"
#include "ECS/Components/Transform.h"
#include "SmokeTestSupport.h"
#include "doctest.h"
#include <memory>
#include <vector>

using molga::TransformCommand;
using molga::TransformState;
using molga::MultiTransformCommand;
using molga::MultiTransformEntry;

// Mock Editor implementation to satisfy linker in test target
#include "Editor/Editor.h"

static std::vector<std::shared_ptr<GameObject>>* s_editorObjects = nullptr;

Editor& Editor::Get() {
    static char buffer[sizeof(Editor)];
    return *reinterpret_cast<Editor*>(buffer);
}

GameObject* Editor::FindObjectById(unsigned int id) const {
    if (s_editorObjects) {
        for (const auto& object : *s_editorObjects) {
            if (object && object->GetID() == id) return object.get();
        }
    }
    return nullptr;
}

void Editor::MarkSceneModified() {
}

namespace {
GameObject* SpawnAt(World& w, const char* name, float x, float y) {
    auto go = std::make_shared<GameObject>(name);
    go->AddComponent<Transform>()->SetPosition(x, y);
    return w.Add(go);
}

class EditorPrefabFixture {
public:
    explicit EditorPrefabFixture(bool withChild)
        : temp_("transform-command-prefab"),
          previousAssetRoot_(PathService::Get().AssetRoot()) {
        PathService::Get().SetAssetRoot(temp_.Path());
        PrefabRegistry::Get().ScanAssets();

        root = std::make_shared<GameObject>("PrefabRoot");
        root->AddComponent<Transform>()->SetPosition(0.f, 0.f);
        objects.push_back(root);
        if (withChild) {
            child = std::make_shared<GameObject>("PrefabChild");
            child->AddComponent<Transform>()->SetPosition(2.f, 3.f);
            child->SetParent(root.get());
            objects.push_back(child);
        }

        const nlohmann::json source = SceneSerializer::SerializeSubtree(root.get());
        guid = PrefabRegistry::GenerateGUID();
        REQUIRE(PrefabRegistry::Get().SavePrefab(
            guid, std::filesystem::path(guid + ".prefab"), source));

        std::unordered_map<unsigned int, unsigned int> idRemap{
            {root->GetID(), root->GetID()}};
        if (child) idRemap[child->GetID()] = child->GetID();
        instance = root->AddComponent<PrefabInstance>();
        instance->SetPrefabGuid(guid);
        instance->SetIdRemap(idRemap);
        s_editorObjects = &objects;
    }

    ~EditorPrefabFixture() {
        s_editorObjects = nullptr;
        PathService::Get().SetAssetRoot(previousAssetRoot_);
    }

    std::vector<std::shared_ptr<GameObject>> objects;
    std::shared_ptr<GameObject> root;
    std::shared_ptr<GameObject> child;
    PrefabInstance* instance = nullptr;
    std::string guid;

private:
    test_support::TempDirectory temp_;
    std::filesystem::path previousAssetRoot_;
};

std::size_t CountPositionOverrides(const nlohmann::json& modifications) {
    std::size_t count = 0;
    for (const auto& modification : modifications) {
        if (modification.value("component", "") == "Transform" &&
            modification.value("key", "") == "position") {
            ++count;
        }
    }
    return count;
}
}

TEST_CASE("TransformCommand applies after-state and undo restores before-state") {
    World w;
    GameObject* go = SpawnAt(w, "Obj", 0.f, 0.f);
    TransformState before{ {0.f, 0.f}, 0.f, {1.f, 1.f} };
    TransformState after { {10.f, 5.f}, 90.f, {2.f, 2.f} };

    TransformCommand cmd(&w, go->GetID(), before, after);
    cmd.Execute();
    auto* tr = go->GetComponent<Transform>();
    CHECK(tr->GetPosition().x == doctest::Approx(10.f));
    CHECK(tr->GetRotation()   == doctest::Approx(90.f));
    CHECK(tr->GetScale().y    == doctest::Approx(2.f));

    cmd.Undo();
    CHECK(tr->GetPosition().x == doctest::Approx(0.f));
    CHECK(tr->GetRotation()   == doctest::Approx(0.f));
    CHECK(tr->GetScale().y    == doctest::Approx(1.f));
}

TEST_CASE("TransformCommand on a missing id is a safe no-op") {
    World w;
    TransformState s{ {0.f, 0.f}, 0.f, {1.f, 1.f} };
    TransformCommand cmd(&w, 9999u, s, s);
    cmd.Execute();   // 대상 없음 — 크래시 없이 통과
    cmd.Undo();
    CHECK(cmd.Name() == "Transform");
}

TEST_CASE("Capture reads current transform into a TransformState") {
    World w;
    GameObject* go = SpawnAt(w, "Obj", 3.f, 4.f);
    go->GetComponent<Transform>()->SetRotation(45.f);
    TransformState s = TransformCommand::Capture(go->GetComponent<Transform>());
    CHECK(s.position.x == doctest::Approx(3.f));
    CHECK(s.rotation   == doctest::Approx(45.f));
}

TEST_CASE("MultiTransformCommand applies and restores one local snapshot per target") {
    World w;
    GameObject* first = SpawnAt(w, "First", 1.f, 2.f);
    GameObject* second = SpawnAt(w, "Second", -3.f, 4.f);
    const TransformState firstBefore =
        TransformCommand::Capture(first->GetComponent<Transform>());
    const TransformState secondBefore =
        TransformCommand::Capture(second->GetComponent<Transform>());
    const TransformState firstAfter{{8.f, 9.f}, 30.f, {2.f, 3.f}};
    const TransformState secondAfter{{-5.f, 7.f}, -45.f, {4.f, 0.5f}};

    MultiTransformCommand command(&w, {
        MultiTransformEntry{first->GetID(), firstBefore, firstAfter},
        MultiTransformEntry{second->GetID(), secondBefore, secondAfter},
    });
    command.Execute();
    CHECK(first->GetComponent<Transform>()->GetPosition().x == doctest::Approx(8.f));
    CHECK(second->GetComponent<Transform>()->GetRotation() == doctest::Approx(-45.f));
    command.Undo();
    CHECK(first->GetComponent<Transform>()->GetPosition().x == doctest::Approx(1.f));
    CHECK(second->GetComponent<Transform>()->GetPosition().y == doctest::Approx(4.f));
}

TEST_CASE("MultiTransformCommand skips a deleted target without losing other targets") {
    World w;
    GameObject* live = SpawnAt(w, "Live", 0.f, 0.f);
    TransformState before{{0.f, 0.f}, 0.f, {1.f, 1.f}};
    TransformState after{{6.f, 7.f}, 10.f, {2.f, 2.f}};
    MultiTransformCommand command(&w, {
        {999999u, before, after}, {live->GetID(), before, after}});
    CHECK_NOTHROW(command.Execute());
    CHECK(live->GetComponent<Transform>()->GetPosition().x == doctest::Approx(6.f));
    CHECK_NOTHROW(command.Undo());
}

TEST_CASE("editor TransformCommand refreshes nearest prefab overrides on execute and undo") {
    EditorPrefabFixture fixture(false);
    Transform* transform = fixture.root->GetComponent<Transform>();
    const TransformState before = TransformCommand::Capture(transform);
    TransformState after = before;
    after.position = {11.f, 7.f};

    TransformCommand command(nullptr, fixture.root->GetID(), before, after);
    command.Execute();
    CHECK(CountPositionOverrides(fixture.instance->GetModifications()) == 1);
    command.Undo();
    CHECK(fixture.instance->GetModifications().empty());
}

TEST_CASE("editor MultiTransformCommand refreshes one prefab root with all target changes") {
    EditorPrefabFixture fixture(true);
    REQUIRE(fixture.child != nullptr);
    Transform* rootTransform = fixture.root->GetComponent<Transform>();
    Transform* childTransform = fixture.child->GetComponent<Transform>();
    const TransformState rootBefore = TransformCommand::Capture(rootTransform);
    const TransformState childBefore = TransformCommand::Capture(childTransform);
    TransformState rootAfter = rootBefore;
    TransformState childAfter = childBefore;
    rootAfter.position.x += 4.f;
    childAfter.position.y += 6.f;

    MultiTransformCommand command(nullptr, {
        {fixture.root->GetID(), rootBefore, rootAfter},
        {fixture.child->GetID(), childBefore, childAfter},
    });
    command.Execute();
    CHECK(CountPositionOverrides(fixture.instance->GetModifications()) == 2);
    command.Undo();
    CHECK(fixture.instance->GetModifications().empty());
}
