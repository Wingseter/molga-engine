#pragma once

#include "Common/Types.h"
#include <cmath>
#include <vector>

namespace molga {

enum class GizmoTool { Select, Move, Rotate, Scale };
enum class GizmoSpace { World, Local };
enum class GizmoAxis { None, X, Y, Both };
enum class SnapMode { Off, Grid, Increment };

struct GizmoSnapSettings {
    SnapMode mode = SnapMode::Off;
    float step = 0.0f;
};

inline GizmoSnapSettings DefaultSnapForTool(GizmoTool tool) {
    switch (tool) {
        case GizmoTool::Move: return {SnapMode::Grid, 32.0f};
        case GizmoTool::Rotate: return {SnapMode::Increment, 15.0f};
        case GizmoTool::Scale: return {SnapMode::Increment, 0.1f};
        case GizmoTool::Select: break;
    }
    return {};
}

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

inline GizmoAxis PickOrientedAxis(float originX, float originY,
                                  float mouseX, float mouseY,
                                  float handleLen, float thickness,
                                  float rotationDegrees) {
    const float radians = rotationDegrees * 3.14159265358979323846f / 180.0f;
    const Vector2 xAxis(std::cos(radians), std::sin(radians));
    const Vector2 yAxis(-std::sin(radians), std::cos(radians));
    const Vector2 delta(mouseX - originX, mouseY - originY);
    if (std::fabs(delta.x) <= thickness && std::fabs(delta.y) <= thickness)
        return GizmoAxis::Both;
    const float alongX = delta.Dot(xAxis);
    const float awayX = std::fabs(delta.Dot(yAxis));
    if (alongX >= 0.0f && alongX <= handleLen && awayX <= thickness)
        return GizmoAxis::X;
    const float alongY = delta.Dot(yAxis);
    const float awayY = std::fabs(delta.Dot(xAxis));
    if (alongY >= 0.0f && alongY <= handleLen && awayY <= thickness)
        return GizmoAxis::Y;
    return GizmoAxis::None;
}

inline Vector2 ConstrainMoveToLocalAxis(const Vector2& worldDelta,
                                        GizmoAxis axis,
                                        float rotationDegrees) {
    if (axis == GizmoAxis::Both || axis == GizmoAxis::None) return worldDelta;
    const float radians = rotationDegrees * 3.14159265358979323846f / 180.0f;
    const Vector2 direction = axis == GizmoAxis::X
        ? Vector2(std::cos(radians), std::sin(radians))
        : Vector2(-std::sin(radians), std::cos(radians));
    return direction * worldDelta.Dot(direction);
}

// Converts a screen-space drag into scale factors along the rendered gizmo
// axes. Projection matters for a rotated local gizmo: dragging its vertical
// local-X handle must change X rather than the screen-space Y component.
inline Vector2 ScaleFactorsFromScreenDrag(const Vector2& screenDelta,
                                          GizmoAxis axis,
                                          float rotationDegrees,
                                          float pixelsPerUnit = 100.0f) {
    if (axis == GizmoAxis::None || pixelsPerUnit <= 0.0f) {
        return Vector2::One();
    }
    constexpr float kPi = 3.14159265358979323846f;
    const float radians = rotationDegrees * kPi / 180.0f;
    const Vector2 xAxis(std::cos(radians), std::sin(radians));
    const Vector2 yAxis(-std::sin(radians), std::cos(radians));
    const float alongX = screenDelta.Dot(xAxis);
    const float alongY = screenDelta.Dot(yAxis);

    if (axis == GizmoAxis::Both) {
        const float uniform = std::max(
            0.01f, 1.0f + (screenDelta.x + screenDelta.y) /
                              (2.0f * pixelsPerUnit));
        return {uniform, uniform};
    }

    Vector2 factors = Vector2::One();
    if (axis == GizmoAxis::X) {
        factors.x = std::max(0.01f, 1.0f + alongX / pixelsPerUnit);
    } else if (axis == GizmoAxis::Y) {
        factors.y = std::max(0.01f, 1.0f + alongY / pixelsPerUnit);
    }
    return factors;
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

struct GizmoWorldState {
    Vector2 position;
    float rotation = 0.0f;
    Vector2 scale = Vector2::One();
};

inline Vector2 MultiTransformPivot(const std::vector<GizmoWorldState>& states) {
    if (states.empty()) return Vector2::Zero();
    Vector2 sum = Vector2::Zero();
    for (const auto& state : states) sum += state.position;
    return sum / static_cast<float>(states.size());
}

inline Vector2 RotatePointAround(const Vector2& point, const Vector2& pivot,
                                 float degrees) {
    constexpr float kPi = 3.14159265358979323846f;
    const float radians = degrees * kPi / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const Vector2 offset = point - pivot;
    return {pivot.x + offset.x * c - offset.y * s,
            pivot.y + offset.x * s + offset.y * c};
}

inline std::vector<GizmoWorldState> ApplyMultiMove(
    const std::vector<GizmoWorldState>& start, GizmoAxis axis,
    const Vector2& worldDelta, SnapMode mode = SnapMode::Off,
    float snapStep = 1.0f) {
    std::vector<GizmoWorldState> result = start;
    Vector2 delta = worldDelta;
    if (axis == GizmoAxis::X) delta.y = 0.0f;
    if (axis == GizmoAxis::Y) delta.x = 0.0f;
    if (mode != SnapMode::Off && snapStep > 0.0f) {
        const Vector2 pivot = MultiTransformPivot(start);
        if (axis != GizmoAxis::Y) {
            delta.x = SnapValue(pivot.x + delta.x, mode, snapStep) - pivot.x;
        }
        if (axis != GizmoAxis::X) {
            delta.y = SnapValue(pivot.y + delta.y, mode, snapStep) - pivot.y;
        }
    }
    for (auto& state : result) state.position += delta;
    return result;
}

inline std::vector<GizmoWorldState> ApplyMultiRotate(
    const std::vector<GizmoWorldState>& start, const Vector2& pivot,
    float degrees, SnapMode mode = SnapMode::Off, float snapDegrees = 15.0f) {
    if (mode != SnapMode::Off) degrees = SnapValue(degrees, mode, snapDegrees);
    std::vector<GizmoWorldState> result = start;
    for (auto& state : result) {
        state.position = RotatePointAround(state.position, pivot, degrees);
        state.rotation += degrees;
    }
    return result;
}

inline std::vector<GizmoWorldState> ApplyMultiScale(
    const std::vector<GizmoWorldState>& start, const Vector2& pivot,
    GizmoAxis axis, Vector2 factors, SnapMode mode = SnapMode::Off,
    float snapIncrement = 0.1f) {
    if (axis == GizmoAxis::X) factors.y = 1.0f;
    if (axis == GizmoAxis::Y) factors.x = 1.0f;
    if (mode != SnapMode::Off && snapIncrement > 0.0f) {
        factors.x = SnapValue(factors.x, mode, snapIncrement);
        factors.y = SnapValue(factors.y, mode, snapIncrement);
    }
    std::vector<GizmoWorldState> result = start;
    for (auto& state : result) {
        const Vector2 offset = state.position - pivot;
        state.position = {pivot.x + offset.x * factors.x,
                          pivot.y + offset.y * factors.y};
        state.scale = {state.scale.x * factors.x, state.scale.y * factors.y};
    }
    return result;
}

} // namespace molga
