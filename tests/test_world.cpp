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

TEST_CASE("World::FindWithTag and GameObject tags and layers") {
    World w;
    auto a = std::make_shared<GameObject>("A");
    a->SetTag("Player");
    a->SetLayer(3);
    w.Add(a);

    auto b = std::make_shared<GameObject>("B");
    b->SetTag("Enemy");
    b->SetLayer(4);
    w.Add(b);

    auto c = std::make_shared<GameObject>("C");
    c->SetTag("Player");
    c->SetLayer(3);
    w.Add(c);

    // Initial search (all are active)
    CHECK(w.FindWithTag("Player") == a.get());
    
    auto players = w.FindAllWithTag("Player");
    REQUIRE(players.size() == 2);
    CHECK(players[0] == a.get());
    CHECK(players[1] == c.get());

    CHECK(a->CompareTag("Player"));
    CHECK_FALSE(a->CompareTag("Enemy"));

    CHECK(a->GetLayer() == 3);
    CHECK(b->GetLayer() == 4);

    // Inactive object should not be found
    a->SetActive(false);
    CHECK(w.FindWithTag("Player") == c.get());
    players = w.FindAllWithTag("Player");
    REQUIRE(players.size() == 1);
    CHECK(players[0] == c.get());
}
