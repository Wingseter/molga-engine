#pragma once

#include "Common/Constants.h"
#include "Common/Types.h"
#include "Rendering/CameraOutputLayout.h"

#include <array>
#include <cmath>

namespace molga {

// SceneView 에디터 카메라의 최소 상태(좌상단 기준 camX/camY + 균일 zoom).
struct ViewportCamera {
    float camX;
    float camY;
    float zoom;
};

// 패널 내 스크린 픽셀 → world. SceneViewWindow.cpp:486-501 규약을 그대로 옮긴다.
inline void ScreenToWorld(const ViewportCamera& cam, float vpW, float vpH,
                          float sx, float sy, float& outX, float& outY) {
    outX = cam.camX + vpW * 0.5f + (sx - vpW * 0.5f) / cam.zoom;
    outY = cam.camY + vpH * 0.5f + (sy - vpH * 0.5f) / cam.zoom;
}

// world → 패널 내 스크린 픽셀 (ScreenToWorld의 역).
inline void WorldToScreen(const ViewportCamera& cam, float vpW, float vpH,
                          float wx, float wy, float& outSx, float& outSy) {
    outSx = (wx - cam.camX - vpW * 0.5f) * cam.zoom + vpW * 0.5f;
    outSy = (wy - cam.camY - vpH * 0.5f) * cam.zoom + vpH * 0.5f;
}

// 점(px,py)이 중심(cx,cy)·반치수(hw,hh) AABB 안에 있는가(경계 포함).
inline bool PointInAabb(float px, float py, float cx, float cy, float hw, float hh) {
    return px >= cx - hw && px <= cx + hw && py >= cy - hh && py <= cy + hh;
}

// Converts a viewport-local output pixel through the immutable view snapshot
// captured by CameraOutputLayout. This mirrors its pointer mapping contract and
// lets Scene View draw camera frusta without reading a mutable Camera2D.
inline Vector2 CameraViewLocalToWorld(const CameraViewSnapshot& view,
                                      float localX, float localY) {
    const float halfWidth = static_cast<float>(view.viewportSize.width) * 0.5f;
    const float halfHeight = static_cast<float>(view.viewportSize.height) * 0.5f;
    const float relativeX = (localX - halfWidth) / view.zoom;
    const float relativeY = (localY - halfHeight) / view.zoom;
    const float radians = view.rotation * Constants::DEG_TO_RAD;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        view.x + halfWidth + cosine * relativeX + sine * relativeY,
        view.y + halfHeight - sine * relativeX + cosine * relativeY,
    };
}

// Clockwise corners beginning at viewport-local top-left.
inline std::array<Vector2, 4> CameraFrustumWorldCorners(
    const CameraViewSnapshot& view) {
    const float width = static_cast<float>(view.viewportSize.width);
    const float height = static_cast<float>(view.viewportSize.height);
    return {
        CameraViewLocalToWorld(view, 0.0f, 0.0f),
        CameraViewLocalToWorld(view, width, 0.0f),
        CameraViewLocalToWorld(view, width, height),
        CameraViewLocalToWorld(view, 0.0f, height),
    };
}

} // namespace molga
