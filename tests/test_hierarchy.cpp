#include "ECS/GameObject.h"
#include "Editor/Selection/SelectionService.h"
#include "Editor/Selection/SelectionUtils.h"
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

TEST_CASE("root-most selection excludes descendants of selected ancestors") {
    auto root = std::make_shared<GameObject>("Root");
    auto child = std::make_shared<GameObject>("Child");
    auto grandchild = std::make_shared<GameObject>("Grandchild");
    auto sibling = std::make_shared<GameObject>("Sibling");
    child->SetParent(root.get());
    grandchild->SetParent(child.get());

    std::vector<GameObject*> objects{root.get(), child.get(), grandchild.get(), sibling.get()};
    auto resolve = [&objects](unsigned int id) -> GameObject* {
        for (GameObject* object : objects) if (object->GetID() == id) return object;
        return nullptr;
    };
    const auto roots = molga::RootMostSelection(
        {grandchild->GetID(), root->GetID(), child->GetID(), sibling->GetID(),
         root->GetID()}, resolve);
    CHECK(roots == std::vector<unsigned int>{root->GetID(), sibling->GetID()});
}

TEST_CASE("visible hierarchy DFS excludes descendants of collapsed rows") {
    auto root = std::make_shared<GameObject>("Root");
    auto branch = std::make_shared<GameObject>("Branch");
    auto leaf = std::make_shared<GameObject>("Leaf");
    auto sibling = std::make_shared<GameObject>("Sibling");
    branch->SetParent(root.get());
    leaf->SetParent(branch.get());
    sibling->SetParent(root.get());

    std::vector<unsigned int> visible;
    molga::AppendVisibleHierarchyDfs(
        root.get(),
        [&](const GameObject& object) { return object.GetID() == root->GetID(); },
        visible);
    CHECK(visible == std::vector<unsigned int>{
        root->GetID(), branch->GetID(), sibling->GetID()});

    visible.clear();
    molga::AppendVisibleHierarchyDfs(
        root.get(),
        [&](const GameObject& object) {
            return object.GetID() == root->GetID() ||
                   object.GetID() == branch->GetID();
        },
        visible);
    CHECK(visible == std::vector<unsigned int>{
        root->GetID(), branch->GetID(), leaf->GetID(), sibling->GetID()});

    visible.clear();
    molga::AppendVisibleHierarchyDfs(
        root.get(), [](const GameObject&) { return false; }, visible);
    CHECK(visible == std::vector<unsigned int>{root->GetID()});
}

TEST_CASE("visible hierarchy forest is complete before a cross-root range") {
    auto firstRoot = std::make_shared<GameObject>("First Root");
    auto firstChild = std::make_shared<GameObject>("First Child");
    auto secondRoot = std::make_shared<GameObject>("Second Root");
    auto secondChild = std::make_shared<GameObject>("Second Child");
    firstChild->SetParent(firstRoot.get());
    secondChild->SetParent(secondRoot.get());

    std::vector<unsigned int> visible;
    molga::AppendVisibleHierarchyForestDfs(
        {firstRoot.get(), secondRoot.get()},
        [](const GameObject&) { return true; }, visible);
    CHECK(visible == std::vector<unsigned int>{
        firstRoot->GetID(), firstChild->GetID(),
        secondRoot->GetID(), secondChild->GetID()});

    molga::SelectionService selection;
    selection.Select(secondChild->GetID(), molga::SelectionSource::Hierarchy);
    selection.SelectRange(visible, firstRoot->GetID(), false,
                          molga::SelectionSource::Hierarchy);
    CHECK(selection.SelectedIds() == visible);
    CHECK(selection.PrimaryId() == firstRoot->GetID());
    CHECK(selection.RangeAnchor() == secondChild->GetID());
}
