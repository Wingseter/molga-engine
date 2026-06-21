#pragma once

#include <cmath>

namespace molga {

enum class GizmoTool { Select, Move, Rotate, Scale };
enum class GizmoSpace { World, Local };
enum class GizmoAxis { None, X, Y, Both };
enum class SnapMode { Off, Grid, Increment };

// value를 snap 규칙에 따라 보정. Off는 원값, Grid/Increment는 step 배수로 반올림.
inline float SnapValue(float value, SnapMode mode, float step) {
    if (mode == SnapMode::Off || step <= 0.f) return value;
    return std::round(value / step) * step;
}

// gizmo 원점(스크린/world 동일 좌표계)에서 마우스가 어느 축 핸들 위인지.
// 중앙 정사각형(두께 영역)이면 Both, +X/+Y 막대 근처면 X/Y, 아니면 None.
inline GizmoAxis PickAxis(float originX, float originY, float mouseX, float mouseY,
                          float handleLen, float thickness) {
    float dx = mouseX - originX;
    float dy = mouseY - originY;
    if (std::fabs(dx) <= thickness && std::fabs(dy) <= thickness) return GizmoAxis::Both;
    if (dx >= 0.f && dx <= handleLen && std::fabs(dy) <= thickness) return GizmoAxis::X;
    if (dy >= 0.f && dy <= handleLen && std::fabs(dx) <= thickness) return GizmoAxis::Y;
    return GizmoAxis::None;
}

// 잡은 축에 따라 world 드래그 델타를 시작 위치에 적용해 새 위치(nx,ny)를 만든다(snap 포함).
inline void ApplyMoveDelta(GizmoAxis axis, float startX, float startY,
                           float dragWorldDX, float dragWorldDY,
                           SnapMode mode, float step, float& nx, float& ny) {
    float tx = startX + ((axis == GizmoAxis::Y) ? 0.f : dragWorldDX);
    float ty = startY + ((axis == GizmoAxis::X) ? 0.f : dragWorldDY);
    nx = SnapValue(tx, mode, step);
    ny = SnapValue(ty, mode, step);
}

} // namespace molga
