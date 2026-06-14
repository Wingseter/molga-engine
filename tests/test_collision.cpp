#include "Physics/Collision.h"
#include "doctest.h"
#include <cmath>

// ── AABB vs AABB ─────────────────────────────────────────────────────────────

TEST_CASE("Collision: check AABB hit") {
    AABB a(0, 0, 10, 10);
    AABB b(5, 5, 10, 10);
    CHECK(Collision::CheckAABB(a, b));
}

TEST_CASE("Collision: check AABB miss") {
    AABB a(0, 0, 10, 10);
    AABB b(20, 20, 5, 5);
    CHECK(!Collision::CheckAABB(a, b));
}

TEST_CASE("Collision: check AABB with result") {
    AABB a(0, 0, 10, 10);
    AABB b(7, 8, 10, 10);
    CollisionResult r = Collision::CheckAABBWithResult(a, b);
    CHECK(r.collided);
    // overlap exists
    CHECK((r.overlapX != 0.0f || r.overlapY != 0.0f));
}

TEST_CASE("Collision: check AABB with result miss") {
    AABB a(0, 0, 5, 5);
    AABB b(10, 10, 5, 5);
    CollisionResult r = Collision::CheckAABBWithResult(a, b);
    CHECK(!r.collided);
}

// ── Circle vs Circle ─────────────────────────────────────────────────────────

TEST_CASE("Collision: check circle hit") {
    Circle a(0, 0, 5);
    Circle b(8, 0, 5);
    CHECK(Collision::CheckCircle(a, b));
}

TEST_CASE("Collision: check circle miss") {
    Circle a(0, 0, 3);
    Circle b(20, 0, 3);
    CHECK(!Collision::CheckCircle(a, b));
}

TEST_CASE("Collision: check circle with result") {
    Circle a(0, 0, 5);
    Circle b(8, 0, 5);
    CollisionResult r = Collision::CheckCircleWithResult(a, b);
    CHECK(r.collided);
    // Overlap should be radius_sum - distance = 10 - 8 = 2
    float expectedOverlap = 2.0f;
    float actualOverlap = std::sqrt(r.overlapX * r.overlapX + r.overlapY * r.overlapY);
    CHECK(actualOverlap == doctest::Approx(expectedOverlap).epsilon(0.1));
}

// ── AABB vs Circle ───────────────────────────────────────────────────────────

TEST_CASE("Collision: check AABB circle hit") {
    AABB box(0, 0, 10, 10);
    Circle circle(12, 5, 5);
    CHECK(Collision::CheckAABBCircle(box, circle));
}

TEST_CASE("Collision: check AABB circle miss") {
    AABB box(0, 0, 10, 10);
    Circle circle(20, 20, 3);
    CHECK(!Collision::CheckAABBCircle(box, circle));
}

TEST_CASE("Collision: check AABB circle inside") {
    AABB box(0, 0, 20, 20);
    Circle circle(10, 10, 3);
    CHECK(Collision::CheckAABBCircle(box, circle));
}

TEST_CASE("Collision: check AABB circle with result") {
    AABB box(0, 0, 10, 10);
    Circle circle(12, 5, 5);
    CollisionResult r = Collision::CheckAABBCircleWithResult(box, circle);
    CHECK(r.collided);
}

// ── Point tests ──────────────────────────────────────────────────────────────

TEST_CASE("Collision: point in AABB") {
    AABB box(0, 0, 10, 10);
    CHECK(Collision::PointInAABB(5, 5, box));
    CHECK(Collision::PointInAABB(0, 0, box));
    CHECK(!Collision::PointInAABB(-1, 5, box));
    CHECK(!Collision::PointInAABB(5, 11, box));
}

TEST_CASE("Collision: point in circle") {
    Circle c(5, 5, 3);
    CHECK(Collision::PointInCircle(5, 5, c));
    CHECK(!Collision::PointInCircle(5, 9, c));
    CHECK(Collision::PointInCircle(6, 6, c));
}
