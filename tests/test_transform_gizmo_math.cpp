#include "Editor/Gizmos/TransformGizmoMath.h"
#include "doctest.h"

using molga::GizmoTool;
using molga::GizmoAxis;
using molga::SnapMode;
using molga::SnapValue;
using molga::PickAxis;
using molga::ApplyMoveDelta;
using molga::GizmoWorldState;

TEST_CASE("SnapValue off returns the raw value") {
    CHECK(SnapValue(7.3f, SnapMode::Off, 1.0f) == doctest::Approx(7.3f));
}

TEST_CASE("SnapValue grid rounds to nearest multiple of step") {
    CHECK(SnapValue(7.3f,  SnapMode::Grid, 5.f) == doctest::Approx(5.f));
    CHECK(SnapValue(8.0f,  SnapMode::Grid, 5.f) == doctest::Approx(10.f));
    CHECK(SnapValue(-2.4f, SnapMode::Grid, 5.f) == doctest::Approx(0.f));
}

TEST_CASE("PickAxis returns X handle when the cursor is along +X near origin") {
    // 핸들 길이 50, 두께 6, 카메라 zoom 1
    GizmoAxis a = PickAxis(/*originX*/0.f, /*originY*/0.f,
                           /*mouseX*/30.f, /*mouseY*/0.f,
                           /*handleLen*/50.f, /*thickness*/6.f);
    CHECK(a == GizmoAxis::X);
}

TEST_CASE("PickAxis returns None when far from both handles") {
    CHECK(PickAxis(0.f, 0.f, 200.f, 200.f, 50.f, 6.f) == GizmoAxis::None);
}

TEST_CASE("oriented axis picking and local movement follow transform rotation") {
    CHECK(molga::PickOrientedAxis(0.f, 0.f, 0.f, 30.f, 50.f, 4.f, 90.f) ==
          GizmoAxis::X);
    const Vector2 constrained = molga::ConstrainMoveToLocalAxis(
        {3.f, 8.f}, GizmoAxis::X, 90.f);
    CHECK(constrained.x == doctest::Approx(0.f).epsilon(0.0001));
    CHECK(constrained.y == doctest::Approx(8.f).epsilon(0.0001));
}

TEST_CASE("local scale drag is projected onto rotated gizmo axes") {
    const Vector2 alongRotatedX = molga::ScaleFactorsFromScreenDrag(
        {0.f, 50.f}, GizmoAxis::X, 90.f);
    CHECK(alongRotatedX.x == doctest::Approx(1.5f).epsilon(0.0001));
    CHECK(alongRotatedX.y == doctest::Approx(1.0f));

    const Vector2 acrossRotatedX = molga::ScaleFactorsFromScreenDrag(
        {50.f, 0.f}, GizmoAxis::X, 90.f);
    CHECK(acrossRotatedX.x == doctest::Approx(1.0f).epsilon(0.0001));

    const Vector2 uniform = molga::ScaleFactorsFromScreenDrag(
        {25.f, 25.f}, GizmoAxis::Both, 90.f);
    CHECK(uniform.x == doctest::Approx(1.25f));
    CHECK(uniform.y == doctest::Approx(1.25f));
}

TEST_CASE("transform tools keep independent production snap defaults") {
    const auto move = molga::DefaultSnapForTool(GizmoTool::Move);
    const auto rotate = molga::DefaultSnapForTool(GizmoTool::Rotate);
    const auto scale = molga::DefaultSnapForTool(GizmoTool::Scale);
    CHECK(move.mode == SnapMode::Grid);
    CHECK(move.step == doctest::Approx(32.f));
    CHECK(rotate.mode == SnapMode::Increment);
    CHECK(rotate.step == doctest::Approx(15.f));
    CHECK(scale.mode == SnapMode::Increment);
    CHECK(scale.step == doctest::Approx(0.1f));

    const std::vector<GizmoWorldState> start{{{1.f, 0.f}, 0.f, {1.f, 1.f}}};
    const auto rotated = molga::ApplyMultiRotate(
        start, {0.f, 0.f}, 22.f, rotate.mode, rotate.step);
    CHECK(rotated[0].rotation == doctest::Approx(15.f));
    const auto scaled = molga::ApplyMultiScale(
        start, {0.f, 0.f}, GizmoAxis::Both, {1.16f, 1.16f},
        scale.mode, scale.step);
    CHECK(scaled[0].scale.x == doctest::Approx(1.2f));
}

TEST_CASE("ApplyMoveDelta on X axis moves only X, grid-snapped") {
    float nx, ny;
    ApplyMoveDelta(GizmoAxis::X, /*startX*/0.f, /*startY*/0.f,
                   /*dragWorldDX*/7.f, /*dragWorldDY*/9.f,
                   SnapMode::Grid, /*step*/5.f, nx, ny);
    CHECK(nx == doctest::Approx(5.f));   // 7 → grid 5
    CHECK(ny == doctest::Approx(0.f));   // Y 고정
}

TEST_CASE("ApplyMoveDelta on Both axis moves freely with no snap") {
    float nx, ny;
    ApplyMoveDelta(GizmoAxis::Both, 1.f, 2.f, 3.f, 4.f, SnapMode::Off, 5.f, nx, ny);
    CHECK(nx == doctest::Approx(4.f));
    CHECK(ny == doctest::Approx(6.f));
}

TEST_CASE("multi rotate orbits positions around center and increments world rotation") {
    std::vector<GizmoWorldState> start{
        {{-2.f, 0.f}, 10.f, {1.f, 1.f}},
        {{ 2.f, 0.f}, -5.f, {2.f, 3.f}},
    };
    const Vector2 pivot = molga::MultiTransformPivot(start);
    CHECK(pivot.x == doctest::Approx(0.f));
    auto result = molga::ApplyMultiRotate(start, pivot, 90.f);
    CHECK(result[0].position.x == doctest::Approx(0.f).epsilon(0.0001));
    CHECK(result[0].position.y == doctest::Approx(-2.f).epsilon(0.0001));
    CHECK(result[1].position.y == doctest::Approx(2.f).epsilon(0.0001));
    CHECK(result[0].rotation == doctest::Approx(100.f));
    CHECK(result[1].rotation == doctest::Approx(85.f));
}

TEST_CASE("multi scale changes center offsets and world scales per axis") {
    std::vector<GizmoWorldState> start{
        {{0.f, 2.f}, 0.f, {2.f, 3.f}},
        {{4.f, 2.f}, 0.f, {1.f, 5.f}},
    };
    auto result = molga::ApplyMultiScale(
        start, {2.f, 2.f}, GizmoAxis::X, {2.f, 9.f});
    CHECK(result[0].position.x == doctest::Approx(-2.f));
    CHECK(result[1].position.x == doctest::Approx(6.f));
    CHECK(result[0].position.y == doctest::Approx(2.f));
    CHECK(result[0].scale.x == doctest::Approx(4.f));
    CHECK(result[0].scale.y == doctest::Approx(3.f));
}

TEST_CASE("multi move applies the same snapped world delta to every target") {
    std::vector<GizmoWorldState> start{
        {{1.f, 2.f}, 0.f, {1.f, 1.f}},
        {{9.f, 5.f}, 0.f, {1.f, 1.f}},
    };
    auto result = molga::ApplyMultiMove(
        start, GizmoAxis::Both, {2.6f, 7.4f}, SnapMode::Increment, 1.f);
    CHECK(result[0].position.x == doctest::Approx(4.f));
    CHECK(result[0].position.y == doctest::Approx(9.5f));
    CHECK(result[1].position.x == doctest::Approx(12.f));
    CHECK(result[1].position.y == doctest::Approx(12.5f));
}
