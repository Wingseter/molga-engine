#pragma once

#include "Editor/Gizmos/TransformGizmoMath.h"
#include "Editor/Commands/TransformCommand.h"
#include "Editor/ViewportMath.h"
#include <imgui.h>

class GameObject;

namespace molga {

// 선택된 단일 GameObject 위에 2D gizmo를 그리고 drag로 transform을 편집한다.
// drag 한 번 = TransformCommand 한 개(begin 스냅샷 → live preview → release commit).
class TransformGizmo {
public:
    void SetTool(GizmoTool t)   { tool_ = t; }
    GizmoTool Tool() const      { return tool_; }
    void SetSpace(GizmoSpace s) { space_ = s; }
    void SetSnap(SnapMode m, float step) { snapMode_ = m; snapStep_ = step; }

    bool IsDragging() const { return dragging_; }

    // target(없으면 무시)에 대해 gizmo를 ImGui drawlist로 그리고 입력을 처리한다.
    // 반환값: 이 프레임에 gizmo가 마우스를 소비했는가(좌클릭 픽킹 억제용).
    bool Draw(GameObject* target, const ViewportCamera& cam, ImVec2 panelPos, ImVec2 panelSize);

private:
    GizmoTool  tool_   = GizmoTool::Move;
    GizmoSpace space_  = GizmoSpace::World;
    SnapMode   snapMode_ = SnapMode::Off;
    float      snapStep_ = 32.f;

    bool dragging_ = false;
    GizmoAxis grabbedAxis_ = GizmoAxis::None;
    TransformState dragStart_;   // begin 스냅샷
    float startWorldX_ = 0.f, startWorldY_ = 0.f;  // drag 시작 시 target world pos
};

} // namespace molga
