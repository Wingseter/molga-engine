#include "Rendering/WorldSort2D.h"

#include "Core/ProjectSettings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace molga {

const char* ToString(SortMode2D mode) {
    return mode == SortMode2D::YAxis ? "YAxis" : "Fixed";
}

SortMode2D SortMode2DFromString(const std::string& value) {
    return value == "YAxis" ? SortMode2D::YAxis : SortMode2D::Fixed;
}

float NormalizeWorldSortValue(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

int ComposeWorldSortingOrder(int baseOrder, int layerIndex, int layerOffset) {
    const std::int64_t composed = static_cast<std::int64_t>(baseOrder) +
                                  static_cast<std::int64_t>(layerIndex) +
                                  static_cast<std::int64_t>(layerOffset);
    return static_cast<int>(std::clamp(
        composed,
        static_cast<std::int64_t>(std::numeric_limits<int>::min()),
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

SortKey MakeWorldSortKey(const WorldSortSettings2D& settings,
                         float worldY,
                         int cameraPass) {
    SortKey key;
    key.cameraPass = cameraPass;
    key.sortingLayer =
        ProjectSettings::Get().ResolveSortingLayerIndex(settings.sortingLayer);
    key.sortingOrder = settings.sortingOrder;
    if (settings.sortMode == SortMode2D::YAxis) {
        const float resolvedY = NormalizeWorldSortValue(worldY);
        const float resolvedOffset = NormalizeWorldSortValue(settings.ySortOffset);
        key.depthOrYSort = NormalizeWorldSortValue(resolvedY + resolvedOffset);
    }
    return key;
}

void SerializeWorldSortSettings(nlohmann::json& json,
                                const WorldSortSettings2D& settings) {
    json["sortingLayer"] = settings.sortingLayer;
    json["sortingOrder"] = settings.sortingOrder;
    json["sortMode"] = ToString(settings.sortMode);
    json["ySortOffset"] = NormalizeWorldSortValue(settings.ySortOffset);
}

WorldSortSettings2D DeserializeWorldSortSettings(const nlohmann::json& json) {
    WorldSortSettings2D settings;
    if (!json.is_object()) return settings;
    if (json.contains("sortingLayer") && json["sortingLayer"].is_string()) {
        settings.sortingLayer = json["sortingLayer"].get<std::string>();
    }
    if (json.contains("sortingOrder") && json["sortingOrder"].is_number_integer()) {
        settings.sortingOrder = json["sortingOrder"].get<int>();
    }
    if (json.contains("sortMode") && json["sortMode"].is_string()) {
        settings.sortMode = SortMode2DFromString(json["sortMode"].get<std::string>());
    }
    if (json.contains("ySortOffset") && json["ySortOffset"].is_number()) {
        settings.ySortOffset =
            NormalizeWorldSortValue(json["ySortOffset"].get<float>());
    }
    return settings;
}

} // namespace molga
