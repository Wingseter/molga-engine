#include "Editor/ScenePicker.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "doctest.h"
#include <memory>
#include <vector>

using molga::PickCandidate;
using molga::PickAt;

namespace {

PickCandidate Candidate(unsigned int id, int layer, int order, float y,
                        std::uint64_t submission) {
    molga::SortKey key;
    key.sortingLayer = layer;
    key.sortingOrder = order;
    key.depthOrYSort = y;
    key.submissionIndex = submission;
    return {id, 0.0f, 0.0f, 16.0f, 16.0f, key};
}

} // namespace

TEST_CASE("PickAt returns the front-most (highest sortingOrder) hit first") {
    std::vector<PickCandidate> c = {
        Candidate(1, 0, 0, 0.0f, 0),
        Candidate(2, 0, 5, 0.0f, 1),
    };
    auto hits = PickAt(c, 0.f, 0.f);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 2u);   // 큰 order = 앞 = 먼저
    CHECK(hits[1] == 1u);
}

TEST_CASE("PickAt skips candidates whose bounds exclude the point") {
    std::vector<PickCandidate> c = {
        Candidate(1, 0, 0, 0.0f, 0),
        {2, 100.0f, 0.0f, 16.0f, 16.0f, Candidate(2, 0, 0, 0.0f, 1).sortKey},
    };
    auto hits = PickAt(c, 5.f, 5.f);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == 1u);
}

TEST_CASE("PickAt on empty space returns no hits") {
    std::vector<PickCandidate> c = {Candidate(1, 0, 0, 0.0f, 0)};
    CHECK(PickAt(c, 1000.f, 1000.f).empty());
}

TEST_CASE("ties on sortingOrder keep stable input order at front") {
    std::vector<PickCandidate> c = {
        Candidate(1, 0, 3, 0.0f, 0),
        Candidate(2, 0, 3, 0.0f, 1),
    };
    auto hits = PickAt(c, 0.f, 0.f);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 2u);   // 동률이면 입력 뒤쪽(나중에 그려진 것)이 앞
}

TEST_CASE("PickAt matches layer order then sorting order then Y then submission") {
    std::vector<PickCandidate> candidates = {
        Candidate(1, 0, 100, 100.0f, 100),
        Candidate(2, 1, -10, -10.0f, 0),
        Candidate(3, 1, -10, 20.0f, 1),
        Candidate(4, 1, -10, 20.0f, 2),
    };

    const auto hits = PickAt(candidates, 0.0f, 0.0f);
    REQUIRE(hits.size() == 4U);
    CHECK(hits == std::vector<unsigned int>{4U, 3U, 2U, 1U});
}

TEST_CASE("Sprite picker bounds match custom render geometry scale and rotation") {
    auto object = std::make_shared<GameObject>("Custom Sprite");
    auto* transform = object->AddComponent<Transform>(10.0f, 20.0f);
    transform->SetScale(2.0f, 3.0f);
    auto* sprite = object->AddComponent<SpriteRenderer>();
    sprite->SetSize(4.0f, 6.0f);

    auto bounds = sprite->GetWorldBounds();
    REQUIRE(bounds);
    CHECK(bounds->x == doctest::Approx(10.0f));
    CHECK(bounds->y == doctest::Approx(20.0f));
    CHECK(bounds->width == doctest::Approx(8.0f));
    CHECK(bounds->height == doctest::Approx(18.0f));

    transform->SetRotation(90.0f);
    bounds = sprite->GetWorldBounds();
    REQUIRE(bounds);
    CHECK(bounds->x == doctest::Approx(5.0f));
    CHECK(bounds->y == doctest::Approx(25.0f));
    CHECK(bounds->width == doctest::Approx(18.0f));
    CHECK(bounds->height == doctest::Approx(8.0f));

    const PickCandidate candidate{
        object->GetID(), bounds->x + bounds->width * 0.5f,
        bounds->y + bounds->height * 0.5f,
        bounds->width * 0.5f, bounds->height * 0.5f, {}};
    CHECK(PickAt({candidate}, 22.9f, 32.9f) ==
          std::vector<unsigned int>{object->GetID()});
    CHECK(PickAt({candidate}, 23.1f, 32.9f).empty());
}
