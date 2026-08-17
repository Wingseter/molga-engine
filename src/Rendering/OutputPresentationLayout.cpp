#include "Rendering/OutputPresentationLayout.h"

#include <algorithm>
#include <cmath>

namespace molga {

namespace {

// Mathematical floor(value / 2), including negative odd values. This makes
// the top/left bias deterministic for both odd letterboxes and odd crops.
int FloorHalf(int value) {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

} // namespace

const char* GameOutputScaleModeName(GameOutputScaleMode mode) {
    switch (mode) {
        case GameOutputScaleMode::Native: return "Native";
        case GameOutputScaleMode::IntegerFit: return "IntegerFit";
    }
    return "Native";
}

bool TryParseGameOutputScaleMode(std::string_view value,
                                 GameOutputScaleMode& modeOut) {
    if (value == "Native") {
        modeOut = GameOutputScaleMode::Native;
        return true;
    }
    if (value == "IntegerFit") {
        modeOut = GameOutputScaleMode::IntegerFit;
        return true;
    }
    return false;
}

OutputPresentationLayout OutputPresentationLayout::Calculate(
    GameOutputScaleMode mode, PixelSize logical, PixelSize framebuffer) {
    return mode == GameOutputScaleMode::IntegerFit
        ? IntegerFit(logical, framebuffer)
        : Native(framebuffer);
}

OutputPresentationLayout OutputPresentationLayout::Native(PixelSize framebuffer) {
    OutputPresentationLayout result;
    result.logicalSize = framebuffer;
    result.framebufferSize = framebuffer;
    if (!framebuffer.IsValid()) return result;
    result.scale = 1;
    result.contentRect = {0, 0, framebuffer.width, framebuffer.height};
    return result;
}

OutputPresentationLayout OutputPresentationLayout::IntegerFit(
    PixelSize logical, PixelSize framebuffer) {
    OutputPresentationLayout result;
    result.logicalSize = logical;
    result.framebufferSize = framebuffer;
    if (!logical.IsValid() || !framebuffer.IsValid()) return result;

    const int fitScale = std::min(framebuffer.width / logical.width,
                                  framebuffer.height / logical.height);
    result.scale = std::max(1, fitScale);
    result.contentRect.width = logical.width * result.scale;
    result.contentRect.height = logical.height * result.scale;
    result.contentRect.x = FloorHalf(framebuffer.width - result.contentRect.width);
    result.contentRect.y = FloorHalf(framebuffer.height - result.contentRect.height);
    result.cropped = result.contentRect.width > framebuffer.width ||
                     result.contentRect.height > framebuffer.height;
    return result;
}

std::optional<PixelPoint> OutputPresentationLayout::FramebufferToLogical(
    float framebufferX, float framebufferY) const {
    if (!IsValid() || !std::isfinite(framebufferX) ||
        !std::isfinite(framebufferY) || framebufferX < 0.0f ||
        framebufferY < 0.0f ||
        framebufferX >= static_cast<float>(framebufferSize.width) ||
        framebufferY >= static_cast<float>(framebufferSize.height)) {
        return std::nullopt;
    }

    const float localX = framebufferX - static_cast<float>(contentRect.x);
    const float localY = framebufferY - static_cast<float>(contentRect.y);
    if (localX < 0.0f || localY < 0.0f ||
        localX >= static_cast<float>(contentRect.width) ||
        localY >= static_cast<float>(contentRect.height)) {
        return std::nullopt;
    }

    const int logicalX = static_cast<int>(std::floor(localX / scale));
    const int logicalY = static_cast<int>(std::floor(localY / scale));
    if (logicalX < 0 || logicalY < 0 || logicalX >= logicalSize.width ||
        logicalY >= logicalSize.height) {
        return std::nullopt;
    }
    return PixelPoint{logicalX, logicalY};
}

} // namespace molga
