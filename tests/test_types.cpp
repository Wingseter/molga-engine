#include "Common/Types.h"
#include "doctest.h"

// ── Vector2 ──────────────────────────────────────────────────────────────────

TEST_CASE("Vector2 arithmetic") {
    Vector2 a(3.0f, 4.0f);
    Vector2 b(1.0f, 2.0f);

    Vector2 sum = a + b;
    CHECK(sum.x == 4.0f);
    CHECK(sum.y == 6.0f);

    Vector2 diff = a - b;
    CHECK(diff.x == 2.0f);
    CHECK(diff.y == 2.0f);

    Vector2 scaled = a * 2.0f;
    CHECK(scaled.x == 6.0f);
    CHECK(scaled.y == 8.0f);

    Vector2 divided = a / 2.0f;
    CHECK(divided.x == 1.5f);
    CHECK(divided.y == 2.0f);
}

TEST_CASE("Vector2 compound assignment") {
    Vector2 v(1.0f, 2.0f);
    v += Vector2(3.0f, 4.0f);
    CHECK(v.x == 4.0f);
    CHECK(v.y == 6.0f);

    v -= Vector2(1.0f, 1.0f);
    CHECK(v.x == 3.0f);
    CHECK(v.y == 5.0f);

    v *= 2.0f;
    CHECK(v.x == 6.0f);
    CHECK(v.y == 10.0f);

    v /= 2.0f;
    CHECK(v.x == 3.0f);
    CHECK(v.y == 5.0f);
}

TEST_CASE("Vector2 length") {
    Vector2 v(3.0f, 4.0f);
    CHECK(v.Length() == doctest::Approx(5.0f));
    CHECK(v.LengthSquared() == doctest::Approx(25.0f));
}

TEST_CASE("Vector2 normalized") {
    Vector2 v(3.0f, 4.0f);
    Vector2 n = v.Normalized();
    CHECK(n.Length() == doctest::Approx(1.0f));
    CHECK(n.x == doctest::Approx(0.6f));
    CHECK(n.y == doctest::Approx(0.8f));

    // Zero vector normalized
    Vector2 zero = Vector2::Zero().Normalized();
    CHECK(zero.x == 0.0f);
    CHECK(zero.y == 0.0f);
}

TEST_CASE("Vector2 dot cross") {
    Vector2 a(1.0f, 0.0f);
    Vector2 b(0.0f, 1.0f);
    CHECK(a.Dot(b) == doctest::Approx(0.0f));
    CHECK(a.Cross(b) == doctest::Approx(1.0f));

    Vector2 c(2.0f, 3.0f);
    Vector2 d(4.0f, 5.0f);
    CHECK(c.Dot(d) == doctest::Approx(23.0f));  // 2*4 + 3*5
    CHECK(c.Cross(d) == doctest::Approx(-2.0f)); // 2*5 - 3*4
}

TEST_CASE("Vector2 statics") {
    CHECK(Vector2::Zero().x == 0.0f);
    CHECK(Vector2::Zero().y == 0.0f);
    CHECK(Vector2::One().x == 1.0f);
    CHECK(Vector2::One().y == 1.0f);
    CHECK(Vector2::Up().y == -1.0f);
    CHECK(Vector2::Down().y == 1.0f);
    CHECK(Vector2::Left().x == -1.0f);
    CHECK(Vector2::Right().x == 1.0f);
}

TEST_CASE("Vector2 distance lerp") {
    Vector2 a(0.0f, 0.0f);
    Vector2 b(3.0f, 4.0f);
    CHECK(Vector2::Distance(a, b) == doctest::Approx(5.0f));

    Vector2 mid = Vector2::Lerp(a, b, 0.5f);
    CHECK(mid.x == doctest::Approx(1.5f));
    CHECK(mid.y == doctest::Approx(2.0f));

    Vector2 start = Vector2::Lerp(a, b, 0.0f);
    CHECK(start == a);
    Vector2 end = Vector2::Lerp(a, b, 1.0f);
    CHECK(end == b);
}

TEST_CASE("Vector2 equality") {
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 2.0f);
    Vector2 c(3.0f, 4.0f);
    CHECK(a == b);
    CHECK(a != c);
}

// ── Color ────────────────────────────────────────────────────────────────────

TEST_CASE("Color defaults") {
    Color c;
    CHECK(c.r == 1.0f);
    CHECK(c.g == 1.0f);
    CHECK(c.b == 1.0f);
    CHECK(c.a == 1.0f);
}

TEST_CASE("Color statics") {
    CHECK(Color::White() == Color(1, 1, 1, 1));
    CHECK(Color::Black() == Color(0, 0, 0, 1));
    CHECK(Color::Red() == Color(1, 0, 0, 1));
    CHECK(Color::Transparent() == Color(0, 0, 0, 0));
}

TEST_CASE("Color lerp") {
    Color a = Color::Black();
    Color b = Color::White();
    Color mid = Color::Lerp(a, b, 0.5f);
    CHECK(mid.r == doctest::Approx(0.5f));
    CHECK(mid.g == doctest::Approx(0.5f));
    CHECK(mid.b == doctest::Approx(0.5f));
    CHECK(mid.a == doctest::Approx(1.0f));
}

// ── AABB ─────────────────────────────────────────────────────────────────────

TEST_CASE("AABB edges") {
    AABB box(10.0f, 20.0f, 30.0f, 40.0f);
    CHECK(box.Left() == 10.0f);
    CHECK(box.Right() == 40.0f);
    CHECK(box.Top() == 20.0f);
    CHECK(box.Bottom() == 60.0f);
    CHECK(box.CenterX() == doctest::Approx(25.0f));
    CHECK(box.CenterY() == doctest::Approx(40.0f));
}

TEST_CASE("AABB contains") {
    AABB box(0.0f, 0.0f, 10.0f, 10.0f);
    CHECK(box.Contains(5.0f, 5.0f));
    CHECK(box.Contains(0.0f, 0.0f));
    CHECK(box.Contains(10.0f, 10.0f));
    CHECK(!box.Contains(-1.0f, 5.0f));
    CHECK(!box.Contains(5.0f, 11.0f));

    CHECK(box.Contains(Vector2(5.0f, 5.0f)));
}

TEST_CASE("AABB intersects") {
    AABB a(0.0f, 0.0f, 10.0f, 10.0f);
    AABB b(5.0f, 5.0f, 10.0f, 10.0f);
    AABB c(20.0f, 20.0f, 5.0f, 5.0f);

    CHECK(a.Intersects(b));
    CHECK(b.Intersects(a));
    CHECK(!a.Intersects(c));
}

// ── Circle ───────────────────────────────────────────────────────────────────

TEST_CASE("Circle contains") {
    Circle c(5.0f, 5.0f, 3.0f);
    CHECK(c.Contains(5.0f, 5.0f));  // center
    CHECK(c.Contains(5.0f, 8.0f));  // on boundary
    CHECK(!c.Contains(5.0f, 9.0f)); // outside
    CHECK(c.Contains(Vector2(6.0f, 6.0f)));
}

TEST_CASE("Circle intersects") {
    Circle a(0.0f, 0.0f, 5.0f);
    Circle b(8.0f, 0.0f, 5.0f);
    Circle c(20.0f, 0.0f, 2.0f);

    CHECK(a.Intersects(b));   // overlapping
    CHECK(!a.Intersects(c));  // too far apart
}

// ── CollisionResult ──────────────────────────────────────────────────────────

TEST_CASE("CollisionResult basic") {
    CollisionResult r;
    CHECK(r.collided == false);
    CHECK(r.overlapX == 0.0f);

    CollisionResult r2(true, 1.0f, 2.0f, 0.0f, 1.0f);
    CHECK(r2.collided == true);
    CHECK(r2.GetOverlap() == Vector2(1.0f, 2.0f));
    CHECK(r2.GetNormal() == Vector2(0.0f, 1.0f));
}

// ── Frame ────────────────────────────────────────────────────────────────────

TEST_CASE("Frame basic") {
    Frame f(0.0f, 0.0f, 0.5f, 0.5f);
    CHECK(f.Width() == doctest::Approx(0.5f));
    CHECK(f.Height() == doctest::Approx(0.5f));

    Frame def;
    CHECK(def.Width() == doctest::Approx(1.0f));
    CHECK(def.Height() == doctest::Approx(1.0f));
}
