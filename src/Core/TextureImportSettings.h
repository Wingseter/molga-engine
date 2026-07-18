#pragma once

#include "Common/Types.h"
#include "Core/Guid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace molga {

enum class TextureFilterMode { Nearest, Linear };
enum class TextureWrapMode { Clamp, Repeat, MirroredRepeat };
enum class TextureColorSpace { LegacyLinear, SRGB };
enum class SpriteImportMode { Single, Multiple };

struct SpriteSlice {
    std::string id;
    std::string name;
    Rect pixelRect{};
    Vector2 pivot{0.5f, 0.5f};
    nlohmann::json preserved = nlohmann::json::object();

    bool IsValid() const {
        return Guid::IsValid(id) && pixelRect.width > 0 && pixelRect.height > 0 &&
               std::isfinite(pivot.x) && std::isfinite(pivot.y) &&
               pivot.x >= 0.0f && pivot.x <= 1.0f &&
               pivot.y >= 0.0f && pivot.y <= 1.0f;
    }
};

struct TextureImportSettings {
    TextureFilterMode filter = TextureFilterMode::Linear;
    TextureWrapMode wrapU = TextureWrapMode::Clamp;
    TextureWrapMode wrapV = TextureWrapMode::Clamp;
    bool mipmaps = false;
    TextureColorSpace colorSpace = TextureColorSpace::SRGB;
    float pixelsPerUnit = 1.0f;
    SpriteImportMode spriteMode = SpriteImportMode::Single;
    Vector2 defaultPivot{0.5f, 0.5f};
    std::vector<SpriteSlice> slices;
    // Preserve settings owned by a newer or third-party importer when this
    // editor changes fields it understands.
    nlohmann::json preserved = nlohmann::json::object();

    static TextureImportSettings NewAssetDefaults() { return {}; }

    static TextureImportSettings LegacyDefaults() {
        TextureImportSettings settings;
        settings.colorSpace = TextureColorSpace::LegacyLinear;
        return settings;
    }

    void Sanitize() {
        if (!std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0.0f) pixelsPerUnit = 1.0f;
        if (!std::isfinite(defaultPivot.x)) defaultPivot.x = 0.5f;
        if (!std::isfinite(defaultPivot.y)) defaultPivot.y = 0.5f;
        defaultPivot.x = std::clamp(defaultPivot.x, 0.0f, 1.0f);
        defaultPivot.y = std::clamp(defaultPivot.y, 0.0f, 1.0f);
        for (auto& slice : slices) {
            if (!Guid::IsValid(slice.id)) slice.id = Guid::Generate();
            slice.pivot.x = std::clamp(std::isfinite(slice.pivot.x) ? slice.pivot.x : 0.5f,
                                      0.0f, 1.0f);
            slice.pivot.y = std::clamp(std::isfinite(slice.pivot.y) ? slice.pivot.y : 0.5f,
                                      0.0f, 1.0f);
        }
    }
};

inline const char* ToString(TextureFilterMode value) {
    return value == TextureFilterMode::Nearest ? "Nearest" : "Linear";
}
inline const char* ToString(TextureWrapMode value) {
    if (value == TextureWrapMode::Repeat) return "Repeat";
    if (value == TextureWrapMode::MirroredRepeat) return "MirroredRepeat";
    return "Clamp";
}
inline const char* ToString(TextureColorSpace value) {
    return value == TextureColorSpace::SRGB ? "SRGB" : "LegacyLinear";
}
inline const char* ToString(SpriteImportMode value) {
    return value == SpriteImportMode::Multiple ? "Multiple" : "Single";
}

inline nlohmann::json SerializeTextureImportSettings(const TextureImportSettings& settings) {
    TextureImportSettings sanitized = settings;
    sanitized.Sanitize();
    nlohmann::json result = sanitized.preserved.is_object()
        ? sanitized.preserved : nlohmann::json::object();
    result["filter"] = ToString(sanitized.filter);
    result["wrapU"] = ToString(sanitized.wrapU);
    result["wrapV"] = ToString(sanitized.wrapV);
    result["mipmaps"] = sanitized.mipmaps;
    result["colorSpace"] = ToString(sanitized.colorSpace);
    result["pixelsPerUnit"] = sanitized.pixelsPerUnit;
    result["spriteMode"] = ToString(sanitized.spriteMode);
    result["defaultPivot"] = {sanitized.defaultPivot.x, sanitized.defaultPivot.y};
    result["slices"] = nlohmann::json::array();
    for (const auto& slice : sanitized.slices) {
        nlohmann::json serializedSlice = slice.preserved.is_object()
            ? slice.preserved : nlohmann::json::object();
        serializedSlice["id"] = slice.id;
        serializedSlice["name"] = slice.name;
        serializedSlice["rect"] = {slice.pixelRect.x, slice.pixelRect.y,
                                    slice.pixelRect.width, slice.pixelRect.height};
        serializedSlice["pivot"] = {slice.pivot.x, slice.pivot.y};
        result["slices"].push_back(std::move(serializedSlice));
    }
    return result;
}

inline TextureImportSettings DeserializeTextureImportSettings(
    const nlohmann::json& json, bool legacyWhenMissing = false) {
    TextureImportSettings settings = legacyWhenMissing
        ? TextureImportSettings::LegacyDefaults()
        : TextureImportSettings::NewAssetDefaults();
    if (!json.is_object()) return settings;
    settings.preserved = json;

    auto stringValue = [&](const char* key, const std::string& fallback) {
        const auto found = json.find(key);
        return found != json.end() && found->is_string()
            ? found->get<std::string>() : fallback;
    };
    auto boolValue = [&](const char* key, bool fallback) {
        const auto found = json.find(key);
        return found != json.end() && found->is_boolean()
            ? found->get<bool>() : fallback;
    };
    auto floatValue = [&](const char* key, float fallback) {
        const auto found = json.find(key);
        if (found == json.end() || !found->is_number()) return fallback;
        try {
            const double value = found->get<double>();
            if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
                value > std::numeric_limits<float>::max()) return fallback;
            return static_cast<float>(value);
        } catch (...) {
            return fallback;
        }
    };
    auto arrayFloat = [](const nlohmann::json& array, std::size_t index,
                         float fallback) {
        if (!array.is_array() || index >= array.size() || !array[index].is_number()) {
            return fallback;
        }
        try {
            const double value = array[index].get<double>();
            if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
                value > std::numeric_limits<float>::max()) return fallback;
            return static_cast<float>(value);
        } catch (...) {
            return fallback;
        }
    };
    auto arrayInt = [](const nlohmann::json& array, std::size_t index,
                       int fallback) {
        if (!array.is_array() || index >= array.size() || !array[index].is_number_integer()) {
            return fallback;
        }
        try {
            const long long value = array[index].get<long long>();
            if (value < std::numeric_limits<int>::min() ||
                value > std::numeric_limits<int>::max()) return fallback;
            return static_cast<int>(value);
        } catch (...) {
            return fallback;
        }
    };

    const std::string filter = stringValue("filter", ToString(settings.filter));
    settings.filter = filter == "Nearest" ? TextureFilterMode::Nearest
                                           : TextureFilterMode::Linear;
    auto parseWrap = [](const std::string& value) {
        if (value == "Repeat") return TextureWrapMode::Repeat;
        if (value == "MirroredRepeat") return TextureWrapMode::MirroredRepeat;
        return TextureWrapMode::Clamp;
    };
    settings.wrapU = parseWrap(stringValue("wrapU", ToString(settings.wrapU)));
    settings.wrapV = parseWrap(stringValue("wrapV", ToString(settings.wrapV)));
    settings.mipmaps = boolValue("mipmaps", settings.mipmaps);
    settings.colorSpace = stringValue("colorSpace", ToString(settings.colorSpace)) == "SRGB"
        ? TextureColorSpace::SRGB : TextureColorSpace::LegacyLinear;
    settings.pixelsPerUnit = floatValue("pixelsPerUnit", settings.pixelsPerUnit);
    settings.spriteMode = stringValue("spriteMode", ToString(settings.spriteMode)) == "Multiple"
        ? SpriteImportMode::Multiple : SpriteImportMode::Single;
    if (json.contains("defaultPivot") && json["defaultPivot"].is_array() &&
        json["defaultPivot"].size() >= 2) {
        settings.defaultPivot = {
            arrayFloat(json["defaultPivot"], 0, settings.defaultPivot.x),
            arrayFloat(json["defaultPivot"], 1, settings.defaultPivot.y)};
    }
    if (json.contains("slices") && json["slices"].is_array()) {
        for (const auto& value : json["slices"]) {
            if (!value.is_object()) continue;
            SpriteSlice slice;
            slice.preserved = value;
            const auto id = value.find("id");
            if (id != value.end() && id->is_string()) slice.id = id->get<std::string>();
            const auto name = value.find("name");
            if (name != value.end() && name->is_string()) slice.name = name->get<std::string>();
            if (value.contains("rect") && value["rect"].is_array() &&
                value["rect"].size() >= 4) {
                slice.pixelRect = {
                    arrayInt(value["rect"], 0, 0), arrayInt(value["rect"], 1, 0),
                    arrayInt(value["rect"], 2, 0), arrayInt(value["rect"], 3, 0)};
            }
            if (value.contains("pivot") && value["pivot"].is_array() &&
                value["pivot"].size() >= 2) {
                slice.pivot = {
                    arrayFloat(value["pivot"], 0, settings.defaultPivot.x),
                    arrayFloat(value["pivot"], 1, settings.defaultPivot.y)};
            } else {
                slice.pivot = settings.defaultPivot;
            }
            settings.slices.push_back(std::move(slice));
        }
    }
    settings.Sanitize();
    return settings;
}

// Re-slicing keeps stable IDs whenever a new cell has the same pixel rectangle.
inline std::vector<SpriteSlice> BuildGridSlices(
    int textureWidth, int textureHeight, int cellWidth, int cellHeight,
    const std::vector<SpriteSlice>& previous, Vector2 pivot = {0.5f, 0.5f}) {
    std::vector<SpriteSlice> result;
    if (textureWidth <= 0 || textureHeight <= 0 || cellWidth <= 0 || cellHeight <= 0) {
        return result;
    }
    pivot.x = std::clamp(std::isfinite(pivot.x) ? pivot.x : 0.5f, 0.0f, 1.0f);
    pivot.y = std::clamp(std::isfinite(pivot.y) ? pivot.y : 0.5f, 0.0f, 1.0f);
    for (int y = 0; y + cellHeight <= textureHeight; y += cellHeight) {
        for (int x = 0; x + cellWidth <= textureWidth; x += cellWidth) {
            SpriteSlice slice;
            slice.pixelRect = {x, y, cellWidth, cellHeight};
            slice.pivot = pivot;
            const auto old = std::find_if(previous.begin(), previous.end(), [&](const SpriteSlice& item) {
                return item.pixelRect.x == x && item.pixelRect.y == y &&
                       item.pixelRect.width == cellWidth && item.pixelRect.height == cellHeight;
            });
            const std::size_t index = result.size();
            slice.id = old != previous.end() && Guid::IsValid(old->id) ? old->id : Guid::Generate();
            slice.name = old != previous.end() && !old->name.empty()
                ? old->name : "slice_" + std::to_string(index);
            if (old != previous.end()) {
                slice.preserved = old->preserved;
                slice.pivot.x = std::clamp(std::isfinite(old->pivot.x) ? old->pivot.x : 0.5f,
                                           0.0f, 1.0f);
                slice.pivot.y = std::clamp(std::isfinite(old->pivot.y) ? old->pivot.y : 0.5f,
                                           0.0f, 1.0f);
            }
            result.push_back(std::move(slice));
        }
    }
    return result;
}

} // namespace molga
