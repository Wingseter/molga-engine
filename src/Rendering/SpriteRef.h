#pragma once

#include "Common/Types.h"

#include <nlohmann/json.hpp>
#include <string>

class Texture;

namespace molga {

struct SpriteRef {
    std::string textureGuid;
    std::string sliceId; // Empty selects the full texture/single sprite.

    bool Empty() const { return textureGuid.empty(); }
    bool operator==(const SpriteRef& other) const {
        return textureGuid == other.textureGuid && sliceId == other.sliceId;
    }
    bool operator!=(const SpriteRef& other) const { return !(*this == other); }
};

struct ResolvedSprite {
    Texture* texture = nullptr;
    Frame uv{};
    Vector2 pivot{0.5f, 0.5f};
    Vector2 nativeSize{}; // Authored world units after pixels-per-unit.
    Rect pixelRect{};
    bool valid = false;
};

inline nlohmann::json SerializeSpriteRef(const SpriteRef& value) {
    return {{"textureGuid", value.textureGuid}, {"sliceId", value.sliceId}};
}

inline SpriteRef DeserializeSpriteRef(const nlohmann::json& value) {
    SpriteRef result;
    if (value.is_object()) {
        result.textureGuid = value.value("textureGuid", std::string{});
        result.sliceId = value.value("sliceId", std::string{});
    } else if (value.is_string()) {
        result.textureGuid = value.get<std::string>();
    }
    return result;
}

} // namespace molga
