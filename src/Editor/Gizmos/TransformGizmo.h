#pragma once

#include "Editor/Gizmos/TransformGizmoMath.h"
#include "Editor/Commands/TransformCommand.h"
#include "Editor/ViewportMath.h"
#include <imgui.h>
#include <vector>

class GameObject;

namespace molga {

// 선택된 root-most GameObject들 위에 2D gizmo를 그리고 drag로 transform을
// 편집한다. drag 한 번 = MultiTransformCommand 한 개.
class TransformGizmo {
public:
    void SetTool(GizmoTool t)   {
        if (dragging_ && t != tool_) CancelDrag();
        tool_ = t;
    }
    GizmoTool Tool() const      { return tool_; }
    void SetSpace(GizmoSpace s) {
        if (dragging_ && s != space_) CancelDrag();
        space_ = s;
    }
    GizmoSpace Space() const    { return space_; }
    void SetSnap(SnapMode m, float step) {
        GizmoSnapSettings* settings = nullptr;
        switch (tool_) {
            case GizmoTool::Move: settings = &moveSnap_; break;
            case GizmoTool::Rotate: settings = &rotateSnap_; break;
            case GizmoTool::Scale: settings = &scaleSnap_; break;
            case GizmoTool::Select: break;
        }
        if (settings) *settings = {m, step};
    }
    GizmoSnapSettings SnapSettings() const {
        switch (tool_) {
            case GizmoTool::Move: return moveSnap_;
            case GizmoTool::Rotate: return rotateSnap_;
            case GizmoTool::Scale: return scaleSnap_;
            case GizmoTool::Select: break;
        }
        return {};
    }

    bool IsDragging() const { return dragging_; }

    // target(없으면 무시)에 대해 gizmo를 ImGui drawlist로 그리고 입력을 처리한다.
    // 반환값: 이 프레임에 gizmo가 마우스를 소비했는가(좌클릭 픽킹 억제용).
    bool Draw(GameObject* target, const ViewportCamera& cam, ImVec2 panelPos, ImVec2 panelSize);
    bool Draw(const std::vector<GameObject*>& targets, const ViewportCamera& cam,
              ImVec2 panelPos, ImVec2 panelSize);

private:
    struct DragTarget {
        unsigned int id = 0;
        TransformState localStart;
        GizmoWorldState worldStart;
    };

    void CancelDrag();

    GizmoTool  tool_   = GizmoTool::Move;
    GizmoSpace space_  = GizmoSpace::World;
    GizmoSnapSettings moveSnap_ = DefaultSnapForTool(GizmoTool::Move);
    GizmoSnapSettings rotateSnap_ = DefaultSnapForTool(GizmoTool::Rotate);
    GizmoSnapSettings scaleSnap_ = DefaultSnapForTool(GizmoTool::Scale);

    bool dragging_ = false;
    GizmoAxis grabbedAxis_ = GizmoAxis::None;
    std::vector<DragTarget> dragTargets_;
    Vector2 dragPivot_ = Vector2::Zero();
    ImVec2 dragMouseStart_{0.0f, 0.0f};
};

} // namespace molga
