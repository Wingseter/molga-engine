#include "Common/Types.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool approx(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) < eps;
}

// ── Vector2 ──────────────────────────────────────────────────────────────────

static void test_vector2_arithmetic() {
    Vector2 a(3.0f, 4.0f);
    Vector2 b(1.0f, 2.0f);

    Vector2 sum = a + b;
    assert(sum.x == 4.0f && sum.y == 6.0f);

    Vector2 diff = a - b;
    assert(diff.x == 2.0f && diff.y == 2.0f);

    Vector2 scaled = a * 2.0f;
    assert(scaled.x == 6.0f && scaled.y == 8.0f);

    Vector2 divided = a / 2.0f;
    assert(divided.x == 1.5f && divided.y == 2.0f);
}

static void test_vector2_compound_assignment() {
    Vector2 v(1.0f, 2.0f);
    v += Vector2(3.0f, 4.0f);
    assert(v.x == 4.0f && v.y == 6.0f);

    v -= Vector2(1.0f, 1.0f);
    assert(v.x == 3.0f && v.y == 5.0f);

    v *= 2.0f;
    assert(v.x == 6.0f && v.y == 10.0f);

    v /= 2.0f;
    assert(v.x == 3.0f && v.y == 5.0f);
}

static void test_vector2_length() {
    Vector2 v(3.0f, 4.0f);
    assert(approx(v.Length(), 5.0f));
    assert(approx(v.LengthSquared(), 25.0f));
}

static void test_vector2_normalized() {
    Vector2 v(3.0f, 4.0f);
    Vector2 n = v.Normalized();
    assert(approx(n.Length(), 1.0f));
    assert(approx(n.x, 0.6f));
    assert(approx(n.y, 0.8f));

    // Zero vector normalized
    Vector2 zero = Vector2::Zero().Normalized();
    assert(zero.x == 0.0f && zero.y == 0.0f);
}

static void test_vector2_dot_cross() {
    Vector2 a(1.0f, 0.0f);
    Vector2 b(0.0f, 1.0f);
    assert(approx(a.Dot(b), 0.0f));
    assert(approx(a.Cross(b), 1.0f));

    Vector2 c(2.0f, 3.0f);
    Vector2 d(4.0f, 5.0f);
    assert(approx(c.Dot(d), 23.0f));  // 2*4 + 3*5
    assert(approx(c.Cross(d), -2.0f)); // 2*5 - 3*4
}

static void test_vector2_statics() {
    assert(Vector2::Zero().x == 0.0f && Vector2::Zero().y == 0.0f);
    assert(Vector2::One().x == 1.0f && Vector2::One().y == 1.0f);
    assert(Vector2::Up().y == -1.0f);
    assert(Vector2::Down().y == 1.0f);
    assert(Vector2::Left().x == -1.0f);
    assert(Vector2::Right().x == 1.0f);
}

static void test_vector2_distance_lerp() {
    Vector2 a(0.0f, 0.0f);
    Vector2 b(3.0f, 4.0f);
    assert(approx(Vector2::Distance(a, b), 5.0f));

    Vector2 mid = Vector2::Lerp(a, b, 0.5f);
    assert(approx(mid.x, 1.5f));
    assert(approx(mid.y, 2.0f));

    Vector2 start = Vector2::Lerp(a, b, 0.0f);
    assert(start == a);
    Vector2 end = Vector2::Lerp(a, b, 1.0f);
    assert(end == b);
}

static void test_vector2_equality() {
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 2.0f);
    Vector2 c(3.0f, 4.0f);
    assert(a == b);
    assert(a != c);
}

// ── Color ────────────────────────────────────────────────────────────────────

static void test_color_defaults() {
    Color c;
    assert(c.r == 1.0f && c.g == 1.0f && c.b == 1.0f && c.a == 1.0f);
}

static void test_color_statics() {
    assert(Color::White() == Color(1, 1, 1, 1));
    assert(Color::Black() == Color(0, 0, 0, 1));
    assert(Color::Red() == Color(1, 0, 0, 1));
    assert(Color::Transparent() == Color(0, 0, 0, 0));
}

static void test_color_lerp() {
    Color a = Color::Black();
    Color b = Color::White();
    Color mid = Color::Lerp(a, b, 0.5f);
    assert(approx(mid.r, 0.5f));
    assert(approx(mid.g, 0.5f));
    assert(approx(mid.b, 0.5f));
    assert(approx(mid.a, 1.0f));
}

// ── AABB ─────────────────────────────────────────────────────────────────────

static void test_aabb_edges() {
    AABB box(10.0f, 20.0f, 30.0f, 40.0f);
    assert(box.Left() == 10.0f);
    assert(box.Right() == 40.0f);
    assert(box.Top() == 20.0f);
    assert(box.Bottom() == 60.0f);
    assert(approx(box.CenterX(), 25.0f));
    assert(approx(box.CenterY(), 40.0f));
}

static void test_aabb_contains() {
    AABB box(0.0f, 0.0f, 10.0f, 10.0f);
    assert(box.Contains(5.0f, 5.0f));
    assert(box.Contains(0.0f, 0.0f));
    assert(box.Contains(10.0f, 10.0f));
    assert(!box.Contains(-1.0f, 5.0f));
    assert(!box.Contains(5.0f, 11.0f));

    assert(box.Contains(Vector2(5.0f, 5.0f)));
}

static void test_aabb_intersects() {
    AABB a(0.0f, 0.0f, 10.0f, 10.0f);
    AABB b(5.0f, 5.0f, 10.0f, 10.0f);
    AABB c(20.0f, 20.0f, 5.0f, 5.0f);

    assert(a.Intersects(b));
    assert(b.Intersects(a));
    assert(!a.Intersects(c));
}

// ── Circle ───────────────────────────────────────────────────────────────────

static void test_circle_contains() {
    Circle c(5.0f, 5.0f, 3.0f);
    assert(c.Contains(5.0f, 5.0f));  // center
    assert(c.Contains(5.0f, 8.0f));  // on boundary
    assert(!c.Contains(5.0f, 9.0f)); // outside
    assert(c.Contains(Vector2(6.0f, 6.0f)));
}

static void test_circle_intersects() {
    Circle a(0.0f, 0.0f, 5.0f);
    Circle b(8.0f, 0.0f, 5.0f);
    Circle c(20.0f, 0.0f, 2.0f);

    assert(a.Intersects(b));   // overlapping
    assert(!a.Intersects(c));  // too far apart
}

// ── CollisionResult ──────────────────────────────────────────────────────────

static void test_collision_result() {
    CollisionResult r;
    assert(r.collided == false);
    assert(r.overlapX == 0.0f);

    CollisionResult r2(true, 1.0f, 2.0f, 0.0f, 1.0f);
    assert(r2.collided == true);
    assert(r2.GetOverlap() == Vector2(1.0f, 2.0f));
    assert(r2.GetNormal() == Vector2(0.0f, 1.0f));
}

// ── Frame ────────────────────────────────────────────────────────────────────

static void test_frame() {
    Frame f(0.0f, 0.0f, 0.5f, 0.5f);
    assert(approx(f.Width(), 0.5f));
    assert(approx(f.Height(), 0.5f));

    Frame def;
    assert(approx(def.Width(), 1.0f));
    assert(approx(def.Height(), 1.0f));
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_vector2_arithmetic();
    test_vector2_compound_assignment();
    test_vector2_length();
    test_vector2_normalized();
    test_vector2_dot_cross();
    test_vector2_statics();
    test_vector2_distance_lerp();
    test_vector2_equality();

    test_color_defaults();
    test_color_statics();
    test_color_lerp();

    test_aabb_edges();
    test_aabb_contains();
    test_aabb_intersects();

    test_circle_contains();
    test_circle_intersects();

    test_collision_result();

    test_frame();

    std::printf("test_types: all tests passed\n");
    return 0;
}
