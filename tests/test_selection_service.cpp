#include "Editor/Selection/SelectionService.h"
#include "doctest.h"
#include <vector>

using molga::SelectionService;
using molga::SelectionSource;

TEST_CASE("single selection sets primary and reports membership") {
    SelectionService sel;
    CHECK_FALSE(sel.HasSelection());
    sel.Select(42, SelectionSource::SceneView);
    CHECK(sel.HasSelection());
    CHECK(sel.PrimaryId() == 42u);
    CHECK(sel.IsSelected(42));
    CHECK(sel.LastSource() == SelectionSource::SceneView);
    CHECK(sel.SelectedIds().size() == 1);
}

TEST_CASE("selecting another id replaces the previous single selection") {
    SelectionService sel;
    sel.Select(1, SelectionSource::Hierarchy);
    sel.Select(2, SelectionSource::SceneView);
    CHECK(sel.PrimaryId() == 2u);
    CHECK_FALSE(sel.IsSelected(1));
    CHECK(sel.SelectedIds().size() == 1);
}

TEST_CASE("Clear empties the selection and primary") {
    SelectionService sel;
    sel.Select(7, SelectionSource::Hierarchy);
    sel.Clear(SelectionSource::Code);
    CHECK_FALSE(sel.HasSelection());
    CHECK(sel.PrimaryId() == 0u);          // 0 = 선택 없음
}

TEST_CASE("change listeners fire on select and clear with the source") {
    SelectionService sel;
    int calls = 0;
    SelectionSource seen = SelectionSource::Code;
    sel.AddListener([&](const SelectionService& s, SelectionSource src) {
        ++calls;
        seen = src;
        (void)s;
    });
    sel.Select(3, SelectionSource::SceneView);
    CHECK(calls == 1);
    CHECK(seen == SelectionSource::SceneView);
    sel.Clear(SelectionSource::Hierarchy);
    CHECK(calls == 2);
    CHECK(seen == SelectionSource::Hierarchy);
}

TEST_CASE("selecting the same id again does not re-notify") {
    SelectionService sel;
    int calls = 0;
    sel.AddListener([&](const SelectionService&, SelectionSource) { ++calls; });
    sel.Select(5, SelectionSource::Hierarchy);
    sel.Select(5, SelectionSource::Hierarchy);   // no-op
    CHECK(calls == 1);
}

TEST_CASE("Rebind keeps selection only for ids that still exist") {
    SelectionService sel;
    sel.Select(10, SelectionSource::SceneView);
    sel.Rebind([](unsigned int id) { return id == 10; });   // 10은 살아있음
    CHECK(sel.IsSelected(10));
    sel.Rebind([](unsigned int) { return false; });          // 모두 사라짐
    CHECK_FALSE(sel.HasSelection());
    CHECK(sel.PrimaryId() == 0u);
}

TEST_CASE("locked inspector target overrides primary until unlocked") {
    SelectionService sel;
    sel.Select(1, SelectionSource::Hierarchy);
    sel.LockInspector(1);
    sel.Select(2, SelectionSource::SceneView);   // 선택은 2로 이동
    CHECK(sel.PrimaryId() == 2u);
    CHECK(sel.InspectorTargetId() == 1u);        // 잠금된 1을 계속 본다
    sel.UnlockInspector();
    CHECK(sel.InspectorTargetId() == 2u);        // 다시 primary를 따라간다
}

TEST_CASE("ordered multi selection is unique and explicit primary is retained") {
    SelectionService sel;
    sel.SelectMany({3, 1, 3, 2, 0, 1}, 1, SelectionSource::Hierarchy);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{3, 1, 2});
    CHECK(sel.PrimaryId() == 1u);
    CHECK(sel.RangeAnchor() == 1u);

    sel.Add(4, SelectionSource::SceneView);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{3, 1, 2, 4});
    CHECK(sel.PrimaryId() == 4u);
    CHECK(sel.RangeAnchor() == 4u);
}

TEST_CASE("toggle removes membership and chooses the last remaining primary") {
    SelectionService sel;
    sel.SelectMany({10, 20, 30}, 20, SelectionSource::Hierarchy);
    sel.Toggle(20, SelectionSource::Hierarchy);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{10, 30});
    CHECK(sel.PrimaryId() == 30u);
    CHECK(sel.RangeAnchor() == 20u);
    sel.Toggle(40, SelectionSource::Hierarchy);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{10, 30, 40});
    CHECK(sel.PrimaryId() == 40u);
}

TEST_CASE("hierarchy range replacement and additive merge keep the original anchor") {
    SelectionService sel;
    const std::vector<unsigned int> visible{1, 2, 3, 4, 5, 6};
    sel.Select(2, SelectionSource::Hierarchy);
    sel.SelectRange(visible, 5, false, SelectionSource::Hierarchy);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{2, 3, 4, 5});
    CHECK(sel.PrimaryId() == 5u);
    CHECK(sel.RangeAnchor() == 2u);

    sel.SelectRange(visible, 1, true, SelectionSource::Hierarchy);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{2, 3, 4, 5, 1});
    CHECK(sel.PrimaryId() == 1u);
    CHECK(sel.RangeAnchor() == 2u);
}

TEST_CASE("inspector lock snapshots the complete target set and notifies lock changes") {
    SelectionService sel;
    int notifications = 0;
    SelectionSource source = SelectionSource::Code;
    sel.AddListener([&](const SelectionService&, SelectionSource next) {
        ++notifications;
        source = next;
    });
    sel.SelectMany({7, 8, 9}, 8, SelectionSource::Hierarchy);
    notifications = 0;

    sel.LockInspector();
    CHECK(notifications == 1);
    CHECK(source == SelectionSource::Inspector);
    CHECK(sel.InspectorTargetIds() == std::vector<unsigned int>{7, 8, 9});
    CHECK(sel.InspectorTargetId() == 8u);

    sel.Select(42, SelectionSource::SceneView);
    CHECK(sel.InspectorTargetIds() == std::vector<unsigned int>{7, 8, 9});
    sel.UnlockInspector();
    CHECK(sel.InspectorTargetIds() == std::vector<unsigned int>{42});
    CHECK(source == SelectionSource::Inspector);
}

TEST_CASE("Rebind prunes selection and lock sets and emits exactly one notification") {
    SelectionService sel;
    sel.SelectMany({1, 2, 3}, 3, SelectionSource::Hierarchy);
    sel.LockInspector();
    int notifications = 0;
    sel.AddListener([&](const SelectionService&, SelectionSource source) {
        ++notifications;
        CHECK(source == SelectionSource::Code);
    });

    sel.Rebind([](unsigned int id) { return id == 1 || id == 2; });
    CHECK(notifications == 1);
    CHECK(sel.SelectedIds() == std::vector<unsigned int>{1, 2});
    CHECK(sel.PrimaryId() == 2u);
    CHECK(sel.RangeAnchor() == 2u);
    CHECK(sel.InspectorTargetIds() == std::vector<unsigned int>{1, 2});
    CHECK(sel.InspectorTargetId() == 2u);

    sel.Rebind([](unsigned int id) { return id == 1 || id == 2; });
    CHECK(notifications == 1);
}
