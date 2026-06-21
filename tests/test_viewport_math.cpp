#include "Editor/ViewportMath.h"
#include "doctest.h"

using molga::ViewportCamera;
using molga::ScreenToWorld;
using molga::WorldToScreen;
using molga::PointInAabb;

TEST_CASE("ScreenToWorld matches the existing SceneView convention") {
    // SceneViewWindow.cpp:486-501 과 동일한 규약:
    // outX = camX + w*0.5 + (sx - w*0.5)/zoom
    ViewportCamera cam{ /*camX*/ 100.f, /*camY*/ 50.f, /*zoom*/ 1.f };
    float wx, wy;
    ScreenToWorld(cam, 800.f, 600.f, 400.f, 300.f, wx, wy);
    CHECK(wx == doctest::Approx(100.f + 400.f + (400.f - 400.f) / 1.f));
    CHECK(wy == doctest::Approx(50.f  + 300.f + (300.f - 300.f) / 1.f));
}

TEST_CASE("ScreenToWorld and WorldToScreen are inverse at zoom != 1") {
    ViewportCamera cam{ -20.f, 30.f, 2.f };
    float wx, wy; ScreenToWorld(cam, 640.f, 480.f, 120.f, 90.f, wx, wy);
    float sx, sy; WorldToScreen(cam, 640.f, 480.f, wx, wy, sx, sy);
    CHECK(sx == doctest::Approx(120.f));
    CHECK(sy == doctest::Approx(90.f));
}

TEST_CASE("PointInAabb respects center+halfsize bounds") {
    CHECK(PointInAabb(10.f, 10.f, /*cx*/10.f, /*cy*/10.f, /*hw*/16.f, /*hh*/16.f));
    CHECK(PointInAabb(25.f, 10.f, 10.f, 10.f, 16.f, 16.f));   // 경계 안 (26까지)
    CHECK_FALSE(PointInAabb(27.f, 10.f, 10.f, 10.f, 16.f, 16.f));
}
