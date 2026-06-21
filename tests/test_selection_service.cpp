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
