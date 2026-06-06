#include "Core/World.h"
#include "ECS/GameObject.h"
#include "Editor/SceneDocument.h"
#include "ECS/Components/Transform.h"
#include "doctest.h"
#include <memory>

TEST_CASE("World::Clone is an independent deep copy") {
    World w;
    auto go = std::make_shared<GameObject>("Player");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(10.0f, 20.0f);
    w.Add(go);

    auto clone = w.Clone();
    REQUIRE(clone->Objects().size() == 1);
    auto* ct = clone->Objects()[0]->GetComponent<Transform>();
    REQUIRE(ct != nullptr);
    CHECK(ct->GetX() == doctest::Approx(10.0f));
    CHECK(clone->Objects()[0]->GetName() == "Player");

    ct->SetPosition(99.0f, 99.0f);            // mutate clone
    CHECK(t->GetX() == doctest::Approx(10.0f)); // original untouched
}

TEST_CASE("World::FindById locates and rejects") {
    World w;
    auto a = std::make_shared<GameObject>("A");
    w.Add(a);
    CHECK(w.FindById(a->GetID()) == a.get());
    CHECK(w.FindById(0) == nullptr);
}

TEST_CASE("SceneDocument restores edit world after Play/Stop") {
    SceneDocument doc;
    auto go = std::make_shared<GameObject>("Mover");
    auto* t = go->AddComponent<Transform>();
    t->SetPosition(5.0f, 5.0f);
    doc.EditWorld().Add(go);

    doc.EnterPlay();
    REQUIRE(doc.IsPlaying());
    auto* pt = doc.ActiveWorld().Objects()[0]->GetComponent<Transform>();
    REQUIRE(pt != nullptr);
    pt->SetPosition(123.0f, 456.0f);          // gameplay mutates the play copy
    CHECK(pt->GetX() == doctest::Approx(123.0f));

    doc.ExitPlay();
    CHECK_FALSE(doc.IsPlaying());
    CHECK(t->GetX() == doctest::Approx(5.0f)); // edit world unchanged
}
