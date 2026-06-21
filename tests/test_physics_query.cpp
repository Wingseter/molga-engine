#include "doctest.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/CircleCollider2D.h"
#include "Physics/Physics2D.h"
#include <memory>

// ── 좌표 규약 (BoxCollider2D::GetWorldBounds 기준) ──────────────────────────
//   AABB 코너 = worldPos + offset,  크기 = size (회전 미지원)
//
//   "Box"  pos(100,100) size(40,40) → x:[100,140] y:[100,140], center(120,120)
//   "Box2" pos(200,100) size(40,40) → x:[200,240] y:[100,140]
//   ray O(0,120) dir(+1,0): Box 진입 x=100 (dist 100), Box2 진입 x=200 (dist 200)

namespace {

std::shared_ptr<GameObject> MakeBox(World& w, const std::string& name,
                                    Vector2 pos, Vector2 size, int layer = 0) {
    auto o = std::make_shared<GameObject>(name);
    o->SetLayer(layer);
    auto* t = o->AddComponent<Transform>();
    t->SetPosition(pos);
    o->AddComponent<BoxCollider2D>()->SetSize(size);
    w.Add(o);
    return o;
}

std::shared_ptr<GameObject> MakeCircle(World& w, const std::string& name,
                                       Vector2 pos, float radius, int layer = 0) {
    auto o = std::make_shared<GameObject>(name);
    o->SetLayer(layer);
    auto* t = o->AddComponent<Transform>();
    t->SetPosition(pos);
    o->AddComponent<CircleCollider2D>()->SetRadius(radius);
    w.Add(o);
    return o;
}

} // namespace

TEST_CASE("Physics2D::Raycast hits a box and reports point/normal/distance") {
    World w;
    auto box = MakeBox(w, "Box", {100, 100}, {40, 40});

    RaycastHit2D hit = Physics2D::Raycast(w, {0, 120}, {1, 0});
    REQUIRE(hit.hit);
    CHECK(hit.collider == box.get());
    CHECK(hit.distance == doctest::Approx(100.0f));
    CHECK(hit.point.x == doctest::Approx(100.0f));
    CHECK(hit.point.y == doctest::Approx(120.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));
    CHECK(hit.normal.y == doctest::Approx(0.0f));
}

TEST_CASE("Physics2D::Raycast misses when the ray passes outside the box") {
    World w;
    MakeBox(w, "Box", {100, 100}, {40, 40});

    RaycastHit2D hit = Physics2D::Raycast(w, {0, 200}, {1, 0});  // y=200, 박스 밖
    CHECK_FALSE(hit.hit);
    CHECK(hit.collider == nullptr);
}

TEST_CASE("Physics2D::Raycast returns the nearest of multiple hits") {
    World w;
    auto near = MakeBox(w, "Box", {100, 100}, {40, 40});
    MakeBox(w, "Box2", {200, 100}, {40, 40});

    RaycastHit2D hit = Physics2D::Raycast(w, {0, 120}, {1, 0});
    REQUIRE(hit.hit);
    CHECK(hit.collider == near.get());          // 가장 가까운 박스
    CHECK(hit.distance == doctest::Approx(100.0f));
}

TEST_CASE("Physics2D::Raycast respects the layer mask") {
    World w;
    MakeBox(w, "Box", {100, 100}, {40, 40}, /*layer*/5);
    auto far = MakeBox(w, "Box2", {200, 100}, {40, 40}, /*layer*/6);

    // 레이어 6만 포함 → 가까운 Box(레이어5)는 무시되고 Box2가 맞는다.
    int mask = (1 << 6);
    RaycastHit2D hit = Physics2D::Raycast(w, {0, 120}, {1, 0}, Physics2D::kInfinity, mask);
    REQUIRE(hit.hit);
    CHECK(hit.collider == far.get());
    CHECK(hit.distance == doctest::Approx(200.0f));
}

TEST_CASE("Physics2D::Raycast hits a circle") {
    World w;
    auto c = MakeCircle(w, "Circle", {100, 100}, 20.0f);  // center(100,100) r=20 → x:[80,120]

    RaycastHit2D hit = Physics2D::Raycast(w, {0, 100}, {1, 0});
    REQUIRE(hit.hit);
    CHECK(hit.collider == c.get());
    CHECK(hit.distance == doctest::Approx(80.0f));
    CHECK(hit.point.x == doctest::Approx(80.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));
}

TEST_CASE("Physics2D overlap queries") {
    World w;
    auto box = MakeBox(w, "Box", {100, 100}, {40, 40});  // [100,140]x[100,140]
    MakeBox(w, "Far", {500, 500}, {10, 10});

    SUBCASE("OverlapCircleAll") {
        auto hits = Physics2D::OverlapCircleAll(w, {120, 120}, 5.0f);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0] == box.get());
    }
    SUBCASE("OverlapBoxAll") {
        auto hits = Physics2D::OverlapBoxAll(w, {120, 120}, {5, 5});
        REQUIRE(hits.size() == 1);
        CHECK(hits[0] == box.get());
    }
    SUBCASE("OverlapPoint inside") {
        CHECK(Physics2D::OverlapPoint(w, {120, 120}) == box.get());
    }
    SUBCASE("OverlapPoint outside") {
        CHECK(Physics2D::OverlapPoint(w, {0, 0}) == nullptr);
    }
}

TEST_CASE("World Find / FindObjectOfType") {
    World w;
    auto box = MakeBox(w, "Box", {100, 100}, {40, 40});
    MakeCircle(w, "Circle", {0, 0}, 10.0f);

    CHECK(w.Find("Box") == box.get());
    CHECK(w.Find("Nonexistent") == nullptr);

    CHECK(w.FindObjectOfType<BoxCollider2D>() != nullptr);
    CHECK(w.FindObjectsOfType<Transform>().size() == 2);
    CHECK(w.FindObjectsOfType<BoxCollider2D>().size() == 1);
}

TEST_CASE("GameObject GetComponentInChildren / GetComponentInParent") {
    World w;
    // parent P: Transform + CircleCollider2D,  child C: Transform + BoxCollider2D
    auto p = std::make_shared<GameObject>("P");
    p->AddComponent<Transform>();
    auto* pCircle = p->AddComponent<CircleCollider2D>();
    w.Add(p);

    auto c = std::make_shared<GameObject>("C");
    c->AddComponent<Transform>();
    auto* cBox = c->AddComponent<BoxCollider2D>();
    w.Add(c);
    c->SetParent(p.get());

    // 자손 탐색: P 자신엔 Box 없음 → 자식 C에서 발견.
    CHECK(p->GetComponent<BoxCollider2D>() == nullptr);
    CHECK(p->GetComponentInChildren<BoxCollider2D>() == cBox);
    // self 우선: C 자신이 Box를 가짐.
    CHECK(c->GetComponentInChildren<BoxCollider2D>() == cBox);

    // 조상 탐색: C엔 Circle 없음 → 부모 P에서 발견.
    CHECK(c->GetComponent<CircleCollider2D>() == nullptr);
    CHECK(c->GetComponentInParent<CircleCollider2D>() == pCircle);
}
