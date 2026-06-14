#include "ECS/GameObject.h"
#include "doctest.h"
#include <memory>
#include <vector>

TEST_CASE("destroying a parent leaves children with a null parent (no dangling)") {
    auto parent = std::make_shared<GameObject>("Parent");
    auto child  = std::make_shared<GameObject>("Child");
    parent->AddChild(child.get());
    REQUIRE(child->GetParent() == parent.get());

    parent.reset();                          // destroy the parent object
    CHECK(child->GetParent() == nullptr);    // must NOT dangle
}

TEST_CASE("SetParent rejects self-parenting") {
    auto a = std::make_shared<GameObject>("A");
    CHECK_FALSE(a->SetParent(a.get()));
    CHECK(a->GetParent() == nullptr);
}

TEST_CASE("SetParent rejects cycles") {
    auto a = std::make_shared<GameObject>("A");
    auto b = std::make_shared<GameObject>("B");
    a->AddChild(b.get());                    // a -> b
    CHECK_FALSE(a->SetParent(b.get()));      // b -> a would create a cycle
    CHECK(a->GetParent() == nullptr);
    CHECK(b->GetParent() == a.get());
}

TEST_CASE("CollectSubtree returns self then all descendants") {
    auto a = std::make_shared<GameObject>("A");
    auto b = std::make_shared<GameObject>("B");
    auto c = std::make_shared<GameObject>("C");
    a->AddChild(b.get());
    b->AddChild(c.get());

    std::vector<GameObject*> out;
    a->CollectSubtree(out);
    CHECK(out.size() == 3);
    CHECK(out[0] == a.get());                // parent before children
}

TEST_CASE("reparenting moves a child without duplicating it") {
    auto p1 = std::make_shared<GameObject>("P1");
    auto p2 = std::make_shared<GameObject>("P2");
    auto c  = std::make_shared<GameObject>("C");
    CHECK(c->SetParent(p1.get()));
    CHECK(p1->GetChildren().size() == 1);
    CHECK(c->SetParent(p2.get()));
    CHECK(p1->GetChildren().size() == 0);
    CHECK(p2->GetChildren().size() == 1);
}
