#pragma once

namespace molga {

// Integer pixel dimensions used at render-target boundaries. Keeping this
// distinct from logical UI sizes prevents accidental DPI-dependent cameras.
struct PixelSize {
    int width = 0;
    int height = 0;

    constexpr bool IsValid() const { return width > 0 && height > 0; }
};

constexpr bool operator==(PixelSize lhs, PixelSize rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

constexpr bool operator!=(PixelSize lhs, PixelSize rhs) {
    return !(lhs == rhs);
}

} // namespace molga
