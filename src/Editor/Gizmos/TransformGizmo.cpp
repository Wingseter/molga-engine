#include "Editor/Gizmos/TransformGizmo.h"

#include "Common/Log.h"
#include "Editor/Editor.h"
#include "Editor/Selection/SelectionUtils.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include <algorithm>
#include <cmath>

namespace molga {
namespace {

bool Same(const TransformState& lhs, const TransformState& rhs) {
    return lhs.position == rhs.position && lhs.rotation == rhs.rotation &&
           lhs.scale == rhs.scale;
}

bool CanSetWorldScale(const Transform& transform) {
    GameObject* owner = transform.GetGameObject();
    GameObject* parent = owner ? owner->GetParent() : nullptr;
    Transform* parentTransform = parent ? parent->GetComponent<Transform>() : nullptr;
    if (!parentTransform) return true;
    const Vector2 scale = parentTransform->GetWorldScale();
    constexpr float epsilon = 1.0e-6f;
    return std::fabs(scale.x) > epsilon && std::fabs(scale.y) > epsilon;
}

} // namespace

bool TransformGizmo::Draw(GameObject* target, const ViewportCamera& cam,
                          ImVec2 panelPos, ImVec2 panelSize) {
    return Draw(target ? std::vector<GameObject*>{target} : std::vector<GameObject*>{},
                cam, panelPos, panelSize);
}

void TransformGizmo::CancelDrag() {
    for (const DragTarget& entry : dragTargets_) {
        GameObject* object = Editor::Get().FindObjectById(entry.id);
        Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
        if (!transform) continue;
        transform->SetPosition(entry.localStart.position);
        transform->SetRotation(entry.localStart.rotation);
        transform->SetScale(entry.localStart.scale);
    }
    dragging_ = false;
    grabbedAxis_ = GizmoAxis::None;
    dragTargets_.clear();
}

bool TransformGizmo::Draw(const std::vector<GameObject*>& targets,
                          const ViewportCamera& cam, ImVec2 panelPos,
                          ImVec2 panelSize) {
    if (tool_ == GizmoTool::Select) {
        if (dragging_) CancelDrag();
        return false;
    }

    const std::vector<GameObject*> roots = RootMostObjects(targets);
    std::vector<GizmoWorldState> currentStates;
    for (GameObject* object : roots) {
        if (Transform* transform = object ? object->GetComponent<Transform>() : nullptr) {
            currentStates.push_back({transform->GetWorldPosition(),
                                     transform->GetWorldRotation(),
                                     transform->GetWorldScale()});
        }
    }
    if (!dragging_ && currentStates.empty()) return false;

    const Vector2 pivot = dragging_ ? dragPivot_ : MultiTransformPivot(currentStates);
    float ox = 0.0f, oy = 0.0f;
    WorldToScreen(cam, panelSize.x, panelSize.y, pivot.x, pivot.y, ox, oy);
    ox += panelPos.x;
    oy += panelPos.y;

    constexpr float handleLen = 60.0f;
    constexpr float thickness = 7.0f;
    const ImVec2 mouse = ImGui::GetMousePos();
    const float orientation = space_ == GizmoSpace::Local && dragging_ &&
                              dragTargets_.size() == 1
        ? dragTargets_.front().worldStart.rotation
        : (roots.size() == 1 && space_ == GizmoSpace::Local
            ? currentStates.front().rotation : 0.0f);
    const GizmoAxis hover = PickOrientedAxis(ox, oy, mouse.x, mouse.y,
                                             handleLen, thickness, orientation);
    const GizmoAxis active = dragging_ ? grabbedAxis_ : hover;

    const float orientationRadians = orientation *
        3.14159265358979323846f / 180.0f;
    const ImVec2 xDirection(std::cos(orientationRadians),
                            std::sin(orientationRadians));
    const ImVec2 yDirection(-std::sin(orientationRadians),
                            std::cos(orientationRadians));
    const ImVec2 xEnd(ox + xDirection.x * handleLen,
                      oy + xDirection.y * handleLen);
    const ImVec2 yEnd(ox + yDirection.x * handleLen,
                      oy + yDirection.y * handleLen);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 colX = active == GizmoAxis::X
        ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 0, 0, 255);
    const ImU32 colY = active == GizmoAxis::Y
        ? IM_COL32(100, 255, 100, 255) : IM_COL32(0, 255, 0, 255);
    const ImU32 colBoth = active == GizmoAxis::Both
        ? IM_COL32(255, 255, 150, 180) : IM_COL32(255, 255, 0, 100);

    if (tool_ == GizmoTool::Rotate) {
        draw->AddCircle(ImVec2(ox, oy), handleLen * 0.65f, colBoth, 48, 3.0f);
    }
    draw->AddLine(ImVec2(ox, oy), xEnd, colX, 3.0f);
    draw->AddLine(ImVec2(ox, oy), yEnd, colY, 3.0f);
    if (tool_ == GizmoTool::Scale) {
        draw->AddRectFilled(ImVec2(xEnd.x - 4.0f, xEnd.y - 4.0f),
                            ImVec2(xEnd.x + 4.0f, xEnd.y + 4.0f), colX);
        draw->AddRectFilled(ImVec2(yEnd.x - 4.0f, yEnd.y - 4.0f),
                            ImVec2(yEnd.x + 4.0f, yEnd.y + 4.0f), colY);
    } else {
        draw->AddCircleFilled(xEnd, 5.0f, colX, 12);
        draw->AddCircleFilled(yEnd, 5.0f, colY, 12);
    }
    draw->AddRectFilled(ImVec2(ox - thickness, oy - thickness),
                        ImVec2(ox + thickness, oy + thickness), colBoth);

    if (!dragging_ && hover != GizmoAxis::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_ = true;
        grabbedAxis_ = hover;
        dragPivot_ = pivot;
        dragMouseStart_ = mouse;
        dragTargets_.clear();
        for (GameObject* object : roots) {
            Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
            if (!transform) continue;
            dragTargets_.push_back({object->GetID(), TransformCommand::Capture(transform),
                {transform->GetWorldPosition(), transform->GetWorldRotation(),
                 transform->GetWorldScale()}});
        }
    }

    if (!dragging_) return hover != GizmoAxis::None;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelDrag();
        return true;
    }

    std::vector<GizmoWorldState> start;
    start.reserve(dragTargets_.size());
    for (const DragTarget& entry : dragTargets_) start.push_back(entry.worldStart);
    const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    const GizmoSnapSettings snap = SnapSettings();
    std::vector<GizmoWorldState> desired = start;
    if (tool_ == GizmoTool::Move) {
        Vector2 worldDelta(delta.x / cam.zoom, delta.y / cam.zoom);
        if (dragTargets_.size() == 1 && space_ == GizmoSpace::Local &&
            grabbedAxis_ != GizmoAxis::Both) {
            worldDelta = ConstrainMoveToLocalAxis(
                worldDelta, grabbedAxis_, dragTargets_.front().worldStart.rotation);
            desired = ApplyMultiMove(start, GizmoAxis::Both, worldDelta,
                                     snap.mode, snap.step);
        } else {
            desired = ApplyMultiMove(start, grabbedAxis_, worldDelta,
                                     snap.mode, snap.step);
        }
    } else if (tool_ == GizmoTool::Rotate) {
        const float startAngle = std::atan2(dragMouseStart_.y - oy,
                                            dragMouseStart_.x - ox);
        const float currentAngle = std::atan2(mouse.y - oy, mouse.x - ox);
        const float degrees = (currentAngle - startAngle) *
                              180.0f / 3.14159265358979323846f;
        desired = ApplyMultiRotate(start, dragPivot_, degrees,
                                   snap.mode, snap.step);
    } else if (tool_ == GizmoTool::Scale) {
        const Vector2 factors = ScaleFactorsFromScreenDrag(
            {delta.x, delta.y}, grabbedAxis_, orientation);
        desired = ApplyMultiScale(start, dragPivot_, grabbedAxis_, factors,
                                  snap.mode, snap.step);
    }

    bool scaleAllowed = true;
    if (tool_ == GizmoTool::Scale) {
        for (const DragTarget& entry : dragTargets_) {
            GameObject* object = Editor::Get().FindObjectById(entry.id);
            Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
            if (transform && !CanSetWorldScale(*transform)) {
                scaleAllowed = false;
                break;
            }
        }
    }
    if (!scaleAllowed) {
        Log::Warn("TransformGizmo",
                  "Scale gesture rejected: a parent has a near-zero world-scale axis.");
        CancelDrag();
        return true;
    }

    for (std::size_t index = 0; index < dragTargets_.size(); ++index) {
        GameObject* object = Editor::Get().FindObjectById(dragTargets_[index].id);
        Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
        if (!transform) continue;
        transform->SetWorldPosition(desired[index].position);
        transform->SetWorldRotation(desired[index].rotation);
        if (tool_ == GizmoTool::Scale &&
            !transform->TrySetWorldScale(desired[index].scale)) {
            CancelDrag();
            return true;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        std::vector<MultiTransformEntry> entries;
        for (const DragTarget& drag : dragTargets_) {
            GameObject* object = Editor::Get().FindObjectById(drag.id);
            Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
            if (!transform) continue;
            const TransformState after = TransformCommand::Capture(transform);
            if (!Same(drag.localStart, after)) {
                entries.push_back({drag.id, drag.localStart, after});
            }
        }
        dragging_ = false;
        grabbedAxis_ = GizmoAxis::None;
        dragTargets_.clear();
        if (!entries.empty()) {
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<MultiTransformCommand>(nullptr, std::move(entries)));
        }
    }
    return true;
}

} // namespace molga
