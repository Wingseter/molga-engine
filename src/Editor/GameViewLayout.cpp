#include "Editor/GameViewLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace molga {

namespace {

float PositiveScale(float value) {
    return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}

} // namespace

GameViewLayout GameViewLayout::Calculate(PixelSize output,
                                         GameViewPoint availableLogicalSize,
                                         GameViewPoint scale,
                                         GameViewDisplayMode requestedMode) {
    GameViewLayout result;
    result.outputSize = output;
    result.mode = requestedMode;
    result.framebufferScale = {PositiveScale(scale.x), PositiveScale(scale.y)};

    if (!output.IsValid() || !std::isfinite(availableLogicalSize.x) ||
        !std::isfinite(availableLogicalSize.y) || availableLogicalSize.x <= 0.0f ||
        availableLogicalSize.y <= 0.0f) {
        return result;
    }

    const float availablePhysicalWidth =
        availableLogicalSize.x * result.framebufferScale.x;
    const float availablePhysicalHeight =
        availableLogicalSize.y * result.framebufferScale.y;

    float texelScale = 1.0f;
    if (requestedMode == GameViewDisplayMode::Fit) {
        texelScale = std::min(
            availablePhysicalWidth / static_cast<float>(output.width),
            availablePhysicalHeight / static_cast<float>(output.height));
    }
    if (!std::isfinite(texelScale) || texelScale <= 0.0f) return result;

    result.physicalPixelsPerTexel = texelScale;
    result.imageRect.width = static_cast<float>(output.width) * texelScale /
                             result.framebufferScale.x;
    result.imageRect.height = static_cast<float>(output.height) * texelScale /
                              result.framebufferScale.y;

    // Fit is letterboxed/pillarboxed. At 100%, centering is useful only while
    // the complete image fits; otherwise the caller supplies scrollbars. Keep
    // a 100% image origin on a physical-pixel boundary as well: an odd spare
    // display pixel is left on the right/bottom instead of shifting every
    // output texel by half a physical pixel.
    if (result.imageRect.width <= availableLogicalSize.x) {
        const float remainder = availableLogicalSize.x - result.imageRect.width;
        result.imageRect.x = std::abs(remainder) < 0.0001f ? 0.0f
            : requestedMode == GameViewDisplayMode::PixelPerfect100
                ? std::floor(remainder * result.framebufferScale.x * 0.5f) /
                      result.framebufferScale.x
                : remainder * 0.5f;
    }
    if (result.imageRect.height <= availableLogicalSize.y) {
        const float remainder = availableLogicalSize.y - result.imageRect.height;
        result.imageRect.y = std::abs(remainder) < 0.0001f ? 0.0f
            : requestedMode == GameViewDisplayMode::PixelPerfect100
                ? std::floor(remainder * result.framebufferScale.y * 0.5f) /
                      result.framebufferScale.y
                : remainder * 0.5f;
    }
    return result;
}

std::optional<GamePixel> GameViewLayout::ScreenToGamePixel(
    GameViewPoint point, GameViewPoint imageOrigin) const {
    if (!outputSize.IsValid() || physicalPixelsPerTexel <= 0.0f ||
        imageRect.width <= 0.0f || imageRect.height <= 0.0f) {
        return std::nullopt;
    }

    const float localX = point.x - imageOrigin.x;
    const float localY = point.y - imageOrigin.y;
    // The right and bottom edge are outside; this avoids producing width/height
    // as a pixel coordinate due to exact-edge clicks.
    if (localX < 0.0f || localY < 0.0f || localX >= imageRect.width ||
        localY >= imageRect.height) {
        return std::nullopt;
    }

    const float physicalX = localX * framebufferScale.x;
    const float physicalY = localY * framebufferScale.y;
    int gameX = static_cast<int>(std::floor(physicalX / physicalPixelsPerTexel));
    int gameY = static_cast<int>(std::floor(physicalY / physicalPixelsPerTexel));
    gameX = std::clamp(gameX, 0, outputSize.width - 1);
    gameY = std::clamp(gameY, 0, outputSize.height - 1);
    return GamePixel{gameX, gameY};
}

PixelSize ResolveResolutionPreset(ResolutionPreset preset,
                                  PixelSize buildResolution,
                                  PixelSize customResolution) {
    switch (preset) {
        case ResolutionPreset::BuildResolution: return buildResolution;
        case ResolutionPreset::R320x180: return {320, 180};
        case ResolutionPreset::R640x360: return {640, 360};
        case ResolutionPreset::R1280x720: return {1280, 720};
        case ResolutionPreset::R1920x1080: return {1920, 1080};
        case ResolutionPreset::Custom: return customResolution;
    }
    return buildResolution;
}

const char* ResolutionPresetLabel(ResolutionPreset preset) {
    switch (preset) {
        case ResolutionPreset::BuildResolution: return "Build Resolution";
        case ResolutionPreset::R320x180: return "320 x 180";
        case ResolutionPreset::R640x360: return "640 x 360";
        case ResolutionPreset::R1280x720: return "1280 x 720";
        case ResolutionPreset::R1920x1080: return "1920 x 1080";
        case ResolutionPreset::Custom: return "Custom";
    }
    return "Build Resolution";
}

const char* ResolutionPresetKey(ResolutionPreset preset) {
    switch (preset) {
        case ResolutionPreset::BuildResolution: return "build";
        case ResolutionPreset::R320x180: return "320x180";
        case ResolutionPreset::R640x360: return "640x360";
        case ResolutionPreset::R1280x720: return "1280x720";
        case ResolutionPreset::R1920x1080: return "1920x1080";
        case ResolutionPreset::Custom: return "custom";
    }
    return "build";
}

std::optional<ResolutionPreset> ResolutionPresetFromKey(const char* key) {
    if (!key) return std::nullopt;
    for (ResolutionPreset preset : {
             ResolutionPreset::BuildResolution, ResolutionPreset::R320x180,
             ResolutionPreset::R640x360, ResolutionPreset::R1280x720,
             ResolutionPreset::R1920x1080, ResolutionPreset::Custom}) {
        if (std::strcmp(key, ResolutionPresetKey(preset)) == 0) return preset;
    }
    return std::nullopt;
}

} // namespace molga
