#include "Editor/Commands/TransformCommand.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "doctest.h"
#include <memory>

using molga::TransformCommand;
using molga::TransformState;

// Mock Editor implementation to satisfy linker in test target
#include "Editor/Editor.h"

Editor& Editor::Get() {
    static char buffer[sizeof(Editor)];
    return *reinterpret_cast<Editor*>(buffer);
}

GameObject* Editor::FindObjectById(unsigned int id) const {
    (void)id;
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
