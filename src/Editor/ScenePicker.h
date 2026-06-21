#pragma once

#include "Editor/ViewportMath.h"
#include <algorithm>
#include <vector>

namespace molga {

// 픽킹 후보 1개: GameObject ID + world AABB(center, half-size) + sortingOrder.
struct PickCandidate {
    unsigned int id;
    float cx, cy;     // world center
    float hw, hh;     // half width/height
    int order;        // sortingOrder (큰 값 = 앞)
};

// world 점 (wx,wy)에 맞는 후보를 앞에서 뒤 순서(order 내림차순, 동률은 입력 뒤가 앞)로 반환.
inline std::vector<unsigned int> PickAt(const std::vector<PickCandidate>& candidates,
                                        float wx, float wy) {
    std::vector<const PickCandidate*> hits;
    for (const auto& c : candidates) {
        if (PointInAabb(wx, wy, c.cx, c.cy, c.hw, c.hh)) hits.push_back(&c);
    }
    // 동률일 때 입력 뒤쪽(나중에 그려진 것)을 앞으로 하기 위해 뒤집음
    std::reverse(hits.begin(), hits.end());
    // 안정 정렬: order 내림차순.
    std::stable_sort(hits.begin(), hits.end(),
        [](const PickCandidate* a, const PickCandidate* b) { return a->order > b->order; });
    std::vector<unsigned int> out;
    out.reserve(hits.size());
    for (auto* h : hits) out.push_back(h->id);
    return out;
}

} // namespace molga
