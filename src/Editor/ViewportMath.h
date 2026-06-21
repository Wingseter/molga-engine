#pragma once

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

} // namespace molga
