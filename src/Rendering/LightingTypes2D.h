#pragma once

#include <string>

enum class SpriteLightingMode2D {
    Unlit,
    Lit,
};

inline const char* SpriteLightingMode2DName(SpriteLightingMode2D mode) noexcept {
    return mode == SpriteLightingMode2D::Lit ? "Lit" : "Unlit";
}

inline SpriteLightingMode2D SpriteLightingMode2DFromString(
    const std::string& value) noexcept {
    return value == "Lit" ? SpriteLightingMode2D::Lit
                          : SpriteLightingMode2D::Unlit;
}
