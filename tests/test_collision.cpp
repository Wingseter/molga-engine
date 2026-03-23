#include "Physics/Collision.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool approx(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) < eps;
}

// ── AABB vs AABB ─────────────────────────────────────────────────────────────

static void test_check_aabb_hit() {
    AABB a(0, 0, 10, 10);
    AABB b(5, 5, 10, 10);
    assert(Collision::CheckAABB(a, b));
}

static void test_check_aabb_miss() {
    AABB a(0, 0, 10, 10);
    AABB b(20, 20, 5, 5);
    assert(!Collision::CheckAABB(a, b));
}

static void test_check_aabb_with_result() {
    AABB a(0, 0, 10, 10);
    AABB b(7, 8, 10, 10);
    CollisionResult r = Collision::CheckAABBWithResult(a, b);
    assert(r.collided);
    // overlap exists
    assert(r.overlapX != 0.0f || r.overlapY != 0.0f);
}

static void test_check_aabb_with_result_miss() {
    AABB a(0, 0, 5, 5);
    AABB b(10, 10, 5, 5);
    CollisionResult r = Collision::CheckAABBWithResult(a, b);
    assert(!r.collided);
}

// ── Circle vs Circle ─────────────────────────────────────────────────────────

static void test_check_circle_hit() {
    Circle a(0, 0, 5);
    Circle b(8, 0, 5);
    assert(Collision::CheckCircle(a, b));
}

static void test_check_circle_miss() {
    Circle a(0, 0, 3);
    Circle b(20, 0, 3);
    assert(!Collision::CheckCircle(a, b));
}

static void test_check_circle_with_result() {
    Circle a(0, 0, 5);
    Circle b(8, 0, 5);
    CollisionResult r = Collision::CheckCircleWithResult(a, b);
    assert(r.collided);
    // Overlap should be radius_sum - distance = 10 - 8 = 2
    float expectedOverlap = 2.0f;
    float actualOverlap = std::sqrt(r.overlapX * r.overlapX + r.overlapY * r.overlapY);
    assert(approx(actualOverlap, expectedOverlap, 0.1f));
}

// ── AABB vs Circle ───────────────────────────────────────────────────────────

static void test_check_aabb_circle_hit() {
    AABB box(0, 0, 10, 10);
    Circle circle(12, 5, 5);
    assert(Collision::CheckAABBCircle(box, circle));
}

static void test_check_aabb_circle_miss() {
    AABB box(0, 0, 10, 10);
    Circle circle(20, 20, 3);
    assert(!Collision::CheckAABBCircle(box, circle));
}

static void test_check_aabb_circle_inside() {
    AABB box(0, 0, 20, 20);
    Circle circle(10, 10, 3);
    assert(Collision::CheckAABBCircle(box, circle));
}

static void test_check_aabb_circle_with_result() {
    AABB box(0, 0, 10, 10);
    Circle circle(12, 5, 5);
    CollisionResult r = Collision::CheckAABBCircleWithResult(box, circle);
    assert(r.collided);
}

// ── Point tests ──────────────────────────────────────────────────────────────

static void test_point_in_aabb() {
    AABB box(0, 0, 10, 10);
    assert(Collision::PointInAABB(5, 5, box));
    assert(Collision::PointInAABB(0, 0, box));
    assert(!Collision::PointInAABB(-1, 5, box));
    assert(!Collision::PointInAABB(5, 11, box));
}

static void test_point_in_circle() {
    Circle c(5, 5, 3);
    assert(Collision::PointInCircle(5, 5, c));
    assert(!Collision::PointInCircle(5, 9, c));
    assert(Collision::PointInCircle(6, 6, c));
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_check_aabb_hit();
    test_check_aabb_miss();
    test_check_aabb_with_result();
    test_check_aabb_with_result_miss();

    test_check_circle_hit();
    test_check_circle_miss();
    test_check_circle_with_result();

    test_check_aabb_circle_hit();
    test_check_aabb_circle_miss();
    test_check_aabb_circle_inside();
    test_check_aabb_circle_with_result();

    test_point_in_aabb();
    test_point_in_circle();

    std::printf("test_collision: all tests passed\n");
    return 0;
}
