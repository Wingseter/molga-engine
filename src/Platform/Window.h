#pragma once

#include <cstdint>

namespace molga {

using WindowId = std::uint32_t;

struct WindowMetrics {
    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool focused = false;
};

struct WindowPointerState {
    float x = 0.0f;
    float y = 0.0f;
    bool leftDown = false;
    bool valid = false;
};

// These queries intentionally expose engine-owned values only. SDL native
// handles remain private to the platform and ImGui integration layers.
bool QueryWindowMetrics(WindowId windowId, WindowMetrics& metrics);
bool QueryWindowPointer(WindowId windowId, WindowPointerState& pointer);
void RequestApplicationQuit();

} // namespace molga
