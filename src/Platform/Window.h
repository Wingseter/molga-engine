#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace molga {

using WindowId = std::uint32_t;

struct WindowMetrics {
    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // SDL_GetWindowPixelDensity 값(창 점 -> 창 물리 픽셀). 조회 실패면 0이다. 호스트의
    // 배치 시작 포인터 씨앗이 쓰는 바로 그 값이라, 이벤트 점과 씨앗이 같은 배율로
    // 물리 픽셀이 된다.
    float pixelDensity = 0.0f;
    bool focused = false;
};

struct WindowPixelsPerPoint {
    double x = 1.0;
    double y = 1.0;
};

// 창 점 -> 창 물리 픽셀 배율의 유일한 결정 규칙. 픽셀 밀도가 유한한 양수면 그것(씨앗과
// 같은 출처)이고, 아니면 픽셀 크기 / 논리 크기, 그것도 안 되면 nullopt다(배율을 지어내지
// 않는다).
inline std::optional<WindowPixelsPerPoint> ResolveWindowPixelsPerPoint(
    const WindowMetrics& metrics) noexcept {
    if (std::isfinite(metrics.pixelDensity) && metrics.pixelDensity > 0.0f) {
        const double density = static_cast<double>(metrics.pixelDensity);
        return WindowPixelsPerPoint{density, density};
    }
    if (metrics.logicalWidth > 0 && metrics.logicalHeight > 0 &&
        metrics.pixelWidth > 0 && metrics.pixelHeight > 0) {
        return WindowPixelsPerPoint{
            static_cast<double>(metrics.pixelWidth) / metrics.logicalWidth,
            static_cast<double>(metrics.pixelHeight) / metrics.logicalHeight};
    }
    return std::nullopt;
}

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
