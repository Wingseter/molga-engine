#include "Editor/Gizmos/TransformGizmo.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"

namespace molga {

bool TransformGizmo::Draw(GameObject* target, const ViewportCamera& cam,
                          ImVec2 panelPos, ImVec2 panelSize) {
    if (!target || tool_ == GizmoTool::Select) {
        dragging_ = false;
        return false;
    }
    auto* tr = target->GetComponent<Transform>();
    if (!tr) return false;

    Vector2 wp = tr->GetWorldPosition();
    float ox, oy;
    WorldToScreen(cam, panelSize.x, panelSize.y, wp.x, wp.y, ox, oy);
    ox += panelPos.x;
    oy += panelPos.y;

    const float handleLen = 60.f;
    const float thickness = 7.f;

    ImVec2 m = ImGui::GetMousePos();
    GizmoAxis hover = PickAxis(ox, oy, m.x, m.y, handleLen, thickness);

    // If dragging, we lock the axis and use the grabbed axis
    GizmoAxis activeAxis = dragging_ ? grabbedAxis_ : hover;

    // Draw the Gizmo
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Determine Colors
    ImU32 colX = (activeAxis == GizmoAxis::X) ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 0, 0, 255);
    ImU32 colY = (activeAxis == GizmoAxis::Y) ? IM_COL32(100, 255, 100, 255) : IM_COL32(0, 255, 0, 255);
    ImU32 colBoth = (activeAxis == GizmoAxis::Both) ? IM_COL32(255, 255, 150, 180) : IM_COL32(255, 255, 0, 100);

    // Draw X Axis (Red)
    dl->AddLine(ImVec2(ox, oy), ImVec2(ox + handleLen, oy), colX, 3.f);
    dl->AddTriangleFilled(
        ImVec2(ox + handleLen + 8.f, oy),
        ImVec2(ox + handleLen, oy - 4.f),
        ImVec2(ox + handleLen, oy + 4.f),
        colX
    );

    // Draw Y Axis (Green)
    dl->AddLine(ImVec2(ox, oy), ImVec2(ox, oy + handleLen), colY, 3.f);
    dl->AddTriangleFilled(
        ImVec2(ox, oy + handleLen + 8.f),
        ImVec2(ox - 4.f, oy + handleLen),
        ImVec2(ox + 4.f, oy + handleLen),
        colY
    );

    // Draw Center Box (Yellow)
    dl->AddRectFilled(
        ImVec2(ox - thickness, oy - thickness),
        ImVec2(ox + thickness, oy + thickness),
        colBoth
    );
    dl->AddRect(
        ImVec2(ox - thickness, oy - thickness),
        ImVec2(ox + thickness, oy + thickness),
        IM_COL32(255, 255, 0, 255),
        0.f, 0, 1.f
    );

    // Input Handling
    if (!dragging_ && hover != GizmoAxis::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_ = true;
        grabbedAxis_ = hover;
        dragStart_ = TransformState{ tr->GetPosition(), tr->GetRotation(), tr->GetScale() };
        startWorldX_ = tr->GetPosition().x;
        startWorldY_ = tr->GetPosition().y;
    }

    if (dragging_) {
        // Calculate mouse drag delta in world coordinates
        ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float dWorldX = d.x / cam.zoom;
        float dWorldY = d.y / cam.zoom;
        float nx, ny;
        ApplyMoveDelta(grabbedAxis_, startWorldX_, startWorldY_,
                       dWorldX, dWorldY, snapMode_, snapStep_, nx, ny);
        tr->SetPosition(nx, ny); // live preview

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            TransformState after = { tr->GetPosition(), tr->GetRotation(), tr->GetScale() };
            dragging_ = false;
            grabbedAxis_ = GizmoAxis::None;
            // Submit single command
            Editor::Get().SubmitTransformEdit(target->GetID(), dragStart_, after);
        }
        return true; // Mouse consumed by gizmo
    }

    return hover != GizmoAxis::None;
}

} // namespace molga
