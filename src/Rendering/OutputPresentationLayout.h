#pragma once

#include "Rendering/PixelSize.h"

#include <optional>
#include <string_view>

namespace molga {

enum class GameOutputScaleMode {
    Native,
    IntegerFit
};

const char* GameOutputScaleModeName(GameOutputScaleMode mode);
bool TryParseGameOutputScaleMode(std::string_view value,
                                 GameOutputScaleMode& modeOut);

struct PixelPoint {
    int x = 0;
    int y = 0;
};

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Pure presentation math shared by rendering and pointer mapping. Rectangles
// use top-left framebuffer coordinates; the OpenGL presenter performs the
// bottom-left conversion only at the blit boundary.
struct OutputPresentationLayout {
    static OutputPresentationLayout Calculate(GameOutputScaleMode mode,
                                               PixelSize logicalSize,
                                               PixelSize framebufferSize);
    static OutputPresentationLayout Native(PixelSize framebufferSize);
    static OutputPresentationLayout IntegerFit(PixelSize logicalSize,
                                                PixelSize framebufferSize);

    std::optional<PixelPoint> FramebufferToLogical(float framebufferX,
                                                   float framebufferY) const;
    std::optional<PixelPoint> FramebufferToLogical(PixelPoint point) const {
        return FramebufferToLogical(static_cast<float>(point.x),
                                    static_cast<float>(point.y));
    }

    bool IsValid() const {
        return logicalSize.IsValid() && framebufferSize.IsValid() && scale > 0 &&
               contentRect.width > 0 && contentRect.height > 0;
    }

    PixelSize logicalSize{};
    PixelSize framebufferSize{};
    int scale = 0;
    PixelRect contentRect{};
    bool cropped = false;
};

} // namespace molga
