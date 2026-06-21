#include "Editor/Gizmos/TransformGizmoMath.h"
#include "doctest.h"

using molga::GizmoTool;
using molga::GizmoAxis;
using molga::SnapMode;
using molga::SnapValue;
using molga::PickAxis;
using molga::ApplyMoveDelta;

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
