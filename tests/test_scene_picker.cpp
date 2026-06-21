#include "Editor/ScenePicker.h"
#include "doctest.h"
#include <vector>

using molga::PickCandidate;
using molga::PickAt;

TEST_CASE("PickAt returns the front-most (highest sortingOrder) hit first") {
    std::vector<PickCandidate> c = {
        { /*id*/1, /*cx*/0.f, 0.f, /*hw*/16.f, /*hh*/16.f, /*order*/0 },
        { /*id*/2, 0.f, 0.f, 16.f, 16.f, /*order*/5 },   // 더 앞
    };
    auto hits = PickAt(c, 0.f, 0.f);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 2u);   // 큰 order = 앞 = 먼저
    CHECK(hits[1] == 1u);
}

TEST_CASE("PickAt skips candidates whose bounds exclude the point") {
    std::vector<PickCandidate> c = {
        { 1, 0.f, 0.f, 16.f, 16.f, 0 },
        { 2, 100.f, 0.f, 16.f, 16.f, 0 },
    };
    auto hits = PickAt(c, 5.f, 5.f);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == 1u);
}

TEST_CASE("PickAt on empty space returns no hits") {
    std::vector<PickCandidate> c = { { 1, 0.f, 0.f, 16.f, 16.f, 0 } };
    CHECK(PickAt(c, 1000.f, 1000.f).empty());
}

TEST_CASE("ties on sortingOrder keep stable input order at front") {
    std::vector<PickCandidate> c = {
        { 1, 0.f, 0.f, 16.f, 16.f, 3 },
        { 2, 0.f, 0.f, 16.f, 16.f, 3 },
    };
    auto hits = PickAt(c, 0.f, 0.f);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 2u);   // 동률이면 입력 뒤쪽(나중에 그려진 것)이 앞
}
