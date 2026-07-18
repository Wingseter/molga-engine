#pragma once

#include "Rendering/RenderQueue.h"

#include <nlohmann/json.hpp>
#include <string>

namespace molga {

enum class SortMode2D {
    Fixed,
    YAxis,
};

struct WorldSortSettings2D {
    std::string sortingLayer = "Default";
    int sortingOrder = 0;
    SortMode2D sortMode = SortMode2D::Fixed;
    float ySortOffset = 0.0f;
};

const char* ToString(SortMode2D mode);
SortMode2D SortMode2DFromString(const std::string& value);

// Rendering comparisons must never receive NaN or infinity. Invalid authored
// values collapse to zero without mutating the component or its Transform.
float NormalizeWorldSortValue(float value);

// Tilemap layers retain their historical base + layer index + authored
// offset ordering. Clamp malformed/extreme documents instead of overflowing
// signed int while composing the queue key.
int ComposeWorldSortingOrder(int baseOrder, int layerIndex, int layerOffset);

// Resolves the authored layer name against the current ProjectSettings on
// every call. The queue assigns submissionIndex when the command is submitted.
SortKey MakeWorldSortKey(const WorldSortSettings2D& settings,
                         float worldY = 0.0f,
                         int cameraPass = 0);

// World renderer fields stay flat in scene/prefab JSON. Existing documents
// that omit the new fields read as Default / Fixed / zero.
void SerializeWorldSortSettings(nlohmann::json& json,
                                const WorldSortSettings2D& settings);
WorldSortSettings2D DeserializeWorldSortSettings(const nlohmann::json& json);

} // namespace molga
