#pragma once

#include "Rendering/PixelSize.h"

#include <optional>

namespace molga {

enum class GameViewDisplayMode {
    Fit,
    PixelPerfect100
};

enum class ResolutionPreset {
    BuildResolution,
    R320x180,
    R640x360,
    R1280x720,
    R1920x1080,
    Custom
};

struct GameViewPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct GameViewRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct GamePixel {
    int x = 0;
    int y = 0;
};

// Pure logical/physical-pixel layout. All rectangles are in logical ImGui
// units; framebufferScale converts those units to physical display pixels.
class GameViewLayout {
public:
    static GameViewLayout Calculate(PixelSize output,
                                    GameViewPoint availableLogicalSize,
                                    GameViewPoint framebufferScale,
                                    GameViewDisplayMode mode);

    std::optional<GamePixel> ScreenToGamePixel(
        GameViewPoint screenPoint,
        GameViewPoint imageScreenOrigin) const;

    PixelSize outputSize{};
    GameViewRect imageRect{};
    GameViewPoint framebufferScale{1.0f, 1.0f};
    GameViewDisplayMode mode = GameViewDisplayMode::Fit;
    // Number of physical display pixels occupied by one output texel.
    float physicalPixelsPerTexel = 0.0f;
};

PixelSize ResolveResolutionPreset(ResolutionPreset preset,
                                  PixelSize buildResolution,
                                  PixelSize customResolution);
const char* ResolutionPresetLabel(ResolutionPreset preset);
const char* ResolutionPresetKey(ResolutionPreset preset);
std::optional<ResolutionPreset> ResolutionPresetFromKey(const char* key);

} // namespace molga
