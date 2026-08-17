#pragma once

#include "Editor/ViewportMath.h"
#include "Rendering/RenderQueue.h"
#include <algorithm>
#include <vector>

namespace molga {

// One Sprite candidate with the exact resolved world render key.
struct PickCandidate {
    unsigned int id;
    float cx, cy;     // world center
    float hw, hh;     // half width/height
    SortKey sortKey;
};

// Returns hits front-to-back. Render keys are ascending on draw, therefore the
// visually topmost (largest) key is first for picking.
inline std::vector<unsigned int> PickAt(const std::vector<PickCandidate>& candidates,
                                        float wx, float wy) {
    std::vector<const PickCandidate*> hits;
    for (const auto& c : candidates) {
        if (PointInAabb(wx, wy, c.cx, c.cy, c.hw, c.hh)) hits.push_back(&c);
    }
    std::stable_sort(hits.begin(), hits.end(),
        [](const PickCandidate* a, const PickCandidate* b) {
            return b->sortKey < a->sortKey;
        });
    std::vector<unsigned int> out;
    out.reserve(hits.size());
    for (auto* h : hits) out.push_back(h->id);
    return out;
}

} // namespace molga
