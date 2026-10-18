#include "UI/UIFocusSystem.h"

#include "Core/World.h"
#include "ECS/Component.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/GameObject.h"
#include "Systems/Input.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIHierarchy.h"
#include "UI/UIInteractionState.h"

#include <algorithm>
#include <array>
#include <utility>

namespace molga::ui {
namespace {

using molga::text::TextDiagnostic;
using molga::text::TextDiagnosticCode;
using molga::text::TextSeverity;

void ReportInvalid(molga::text::TextDiagnosticSink& sink, const char* message,
                   const char* remediation, unsigned int sceneObjectId) {
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::ReferenceInvalid;
    diagnostic.severity = TextSeverity::Warning;
    diagnostic.subsystem = "ui.focus";
    diagnostic.message = message;
    diagnostic.remediation = remediation;
    diagnostic.sceneObjectId = sceneObjectId;
    sink.Report(std::move(diagnostic));
}

void ClearPointer(UISurfaceInteractionState& state) {
    state.pointerCapture.reset();
    state.hovered.reset();
    for (auto& entry : state.buttons) {
        entry.second.hovered = false;
        entry.second.pressed = false;
    }
}

void ClearEverything(UISurfaceInteractionState& state) {
    state.focused.reset();
    ClearPointer(state);
}

// 이 식별자가 지금도 살아 있는 상호작용 대상인가: 네 필드가 해석되고, 컴포넌트와
// 소유 계층이 활성이고, 상호작용 가능하다.
bool IsAlive(World& world, const UIRuntimeTargetIdentity& id) {
    Component* component = ResolveTarget(world, id);
    if (!component || !component->IsEnabled()) return false;
    if (!IsHierarchyActive(component->GetGameObject())) return false;
    if (id.componentRuntimeTypeId == ComponentTypeID::Get<UISelectable>()) {
        return static_cast<UISelectable*>(component)->Interactable();
    }
    if (id.componentRuntimeTypeId == ComponentTypeID::Get<UIButton>()) {
        return static_cast<UIButton*>(component)->IsInteractable();
    }
    return true;
}

const UIHitTargetSnapshot* RouteForFocusTarget(
    const UISnapshot& n, const UIRuntimeTargetIdentity& focus) noexcept {
    for (const auto& hit : n.hitTargets) {
        if (hit.focusTarget && hit.focusTarget->runtimeTarget == focus) {
            return &hit;
        }
    }
    return nullptr;
}

std::size_t DirectionIndex(UINavigationDirection direction) noexcept {
    return static_cast<std::size_t>(direction);
}

// 중심의 두 배. 반 칸을 쪼개지 않으려고 정수로 둔다.
struct DoubledCenter {
    std::int64_t x = 0;
    std::int64_t y = 0;
};
DoubledCenter CenterOf(const molga::FixedRect& rect) noexcept {
    return {2 * static_cast<std::int64_t>(rect.x.Raw()) + rect.width.Raw(),
            2 * static_cast<std::int64_t>(rect.y.Raw()) + rect.height.Raw()};
}

// 명시 내비게이션: N에 얼어붙은 참조만 읽는다. 그 대상이 N에서 포커스 가능한
// 기록으로 게시되어 있어야 한다(상호작용 불가 selectable은 참조는 풀려도
// 포커스 기록이 없다).
std::optional<UIFrozenTarget> ResolveExplicit(
    const UISnapshot& n, const UIHitTargetSnapshot& source,
    UINavigationDirection direction) {
    const auto& target = source.navigation.explicitTargets[DirectionIndex(direction)];
    if (!target) return std::nullopt;
    const UIHitTargetSnapshot* record = RouteForFocusTarget(n, *target);
    if (!record) return std::nullopt;
    return record->focusTarget;
}

// 자동 내비게이션: 반평면 -> 주축 -> 보조축 -> 형제 경로 -> 오브젝트 id ->
// 인스턴스 id. 좌표계는 위가 작은 값(y가 아래로 큼)이다.
std::optional<UIFrozenTarget> ResolveAutomatic(
    const UISnapshot& n, const UIHitTargetSnapshot& source,
    UINavigationDirection direction) {
    const DoubledCenter from = CenterOf(source.logicalRect);
    std::optional<UINavigationCandidateKey> best;
    const UIHitTargetSnapshot* bestRecord = nullptr;
    for (const auto& hit : n.hitTargets) {
        if (!hit.focusTarget) continue;
        if (hit.focusTarget->runtimeTarget == source.focusTarget->runtimeTarget) {
            continue;
        }
        // 사용자 결정 d: 모드 None은 자동 내비게이션 목적지에서 빠진다.
        if (hit.navigation.mode == UINavigationMode::None) continue;
        const DoubledCenter to = CenterOf(hit.logicalRect);
        const std::int64_t dx = to.x - from.x;
        const std::int64_t dy = to.y - from.y;
        UINavigationCandidateKey key;
        switch (direction) {
            case UINavigationDirection::Right:
                if (dx <= 0) continue;
                key.primary = dx;
                key.secondary = dy < 0 ? -dy : dy;
                break;
            case UINavigationDirection::Left:
                if (dx >= 0) continue;
                key.primary = -dx;
                key.secondary = dy < 0 ? -dy : dy;
                break;
            case UINavigationDirection::Down:
                if (dy <= 0) continue;
                key.primary = dy;
                key.secondary = dx < 0 ? -dx : dx;
                break;
            case UINavigationDirection::Up:
                if (dy >= 0) continue;
                key.primary = -dy;
                key.secondary = dx < 0 ? -dx : dx;
                break;
        }
        key.siblingPath = hit.order.siblingPath;
        key.objectId = hit.focusTarget->runtimeTarget.objectId;
        key.componentInstanceId = hit.focusTarget->runtimeTarget.componentInstanceId;
        if (!best || UINavigationCandidateLess(key, *best)) {
            best = std::move(key);
            bestRecord = &hit;
        }
    }
    if (!bestRecord) return std::nullopt;
    return bestRecord->focusTarget;
}

// 방향 입력이 N에서 이르는 포커스 목적지. 없으면 nullopt.
std::optional<UIFrozenTarget> ResolveDestination(
    const UISnapshot& n, const UIHitTargetSnapshot& source,
    UINavigationDirection direction) {
    switch (source.navigation.mode) {
        case UINavigationMode::None:
            return std::nullopt;
        case UINavigationMode::Explicit:
            return ResolveExplicit(n, source, direction);
        case UINavigationMode::Auto:
            return ResolveAutomatic(n, source, direction);
    }
    return std::nullopt;
}

}  // namespace

bool UINavigationCandidateLess(const UINavigationCandidateKey& a,
                               const UINavigationCandidateKey& b) noexcept {
    if (a.primary != b.primary) return a.primary < b.primary;
    if (a.secondary != b.secondary) return a.secondary < b.secondary;
    if (a.siblingPath != b.siblingPath) return a.siblingPath < b.siblingPath;
    if (a.objectId != b.objectId) return a.objectId < b.objectId;
    return a.componentInstanceId < b.componentInstanceId;
}

// ── 방향 도메인 (사용자 결정 2026-10-18) ────────────────────────────────────
std::optional<UINavigationDirection> UINavigationDirectionOf(
    const UIInputEvent& event) noexcept {
    if (!event.active) return std::nullopt;
    switch (event.kind) {
        case UIInputEventKind::Key:
            switch (static_cast<Input::KeyCode>(event.control)) {
                case Input::KeyCode::Up: return UINavigationDirection::Up;
                case Input::KeyCode::Down: return UINavigationDirection::Down;
                case Input::KeyCode::Left: return UINavigationDirection::Left;
                case Input::KeyCode::Right: return UINavigationDirection::Right;
                default: return std::nullopt;
            }
        case UIInputEventKind::GamepadButton:
            switch (static_cast<Input::GamepadButton>(event.control)) {
                case Input::GamepadButton::DpadUp:
                    return UINavigationDirection::Up;
                case Input::GamepadButton::DpadDown:
                    return UINavigationDirection::Down;
                case Input::GamepadButton::DpadLeft:
                    return UINavigationDirection::Left;
                case Input::GamepadButton::DpadRight:
                    return UINavigationDirection::Right;
                default: return std::nullopt;
            }
        default:
            return std::nullopt;
    }
}

void UIFocusSystem::ProjectEvent(const UISnapshot& n, PlannedUIEvent& plan,
                                 UIPlanningState& projected) const {
    // 부적격 계획에는 아무것도 쓰지 않는다. 표도 세대도 콜백도 건드리지 않고
    // N만 읽는다.
    if (!plan.surfaceEligible) return;
    const auto direction = UINavigationDirectionOf(plan.event);
    if (!direction) return;
    // 출발점은 이 경로가 얼려 둔 포커스 대상이다. 투사된 포커스가 없거나 N에
    // 그 기록이 없으면 갈 곳도 없다.
    const auto source = plan.TargetFor(UIEventStage::Focus);
    if (!source) return;
    const UIHitTargetSnapshot* record =
        RouteForFocusTarget(n, source->runtimeTarget);
    if (!record) return;
    const auto destination = ResolveDestination(n, *record, *direction);
    if (!destination) return;
    plan.focusDestinationFromSnapshotN = destination;
    projected.focused = destination->runtimeTarget;
}

UIEventHandlerResult UIFocusSystem::HandleEvent(
    World& world, const UISnapshot& n, const PlannedUIEvent& planned,
    molga::text::TextDiagnosticSink& sink) {
    UIEventHandlerResult result;
    result.sequence = planned.event.sequence;
    result.stage = UIEventStage::Focus;
    result.stageTargetOrdinal = 0;
    // 부적격 계획은 어떤 표도 건드리지 않고, 대상도 효과도 없는 빈 결과를 낸다.
    if (!planned.surfaceEligible) return result;

    const UISurfaceKey key{n.surfaceWindowId, n.worldGeneration};
    auto& table = UIInteractionStateTable::Get();
    const auto& event = planned.event;

    // 창 포커스 상실: 이 표면의 포커스·캡처·hover·pressed 전부. 대상이 없는
    // 결과는 dirty 플래그만 가질 수 있다.
    if (event.kind == UIInputEventKind::WindowFocus) {
        if (event.active) return result;
        UISurfaceInteractionState next = table.SurfaceOrEmpty(key);
        ClearEverything(next);
        bool changed = false;
        if (table.Apply(key, std::move(next), &sink, "ui.focus", 0, &changed)) {
            result.visualDirty = changed;
        }
        return result;
    }

    result.resolvedStageTarget = planned.TargetFor(UIEventStage::Focus);

    const auto direction = UINavigationDirectionOf(event);
    if (direction) {
        if (!planned.focusDestinationFromSnapshotN) {
            // 목적지가 없다. 조용한 경우(후보 없음, 모드 None, 풀리지 않은
            // 참조)와 보고할 경우를 가른다: 명시 모드에서 N이 참조를 얼려 뒀는데
            // 그 대상이 N에서 포커스 가능한 기록이 아니면 한 번 보고한다.
            if (result.resolvedStageTarget) {
                const UIHitTargetSnapshot* record = RouteForFocusTarget(
                    n, result.resolvedStageTarget->runtimeTarget);
                if (record && record->navigation.mode == UINavigationMode::Explicit &&
                    record->navigation.explicitTargets[DirectionIndex(*direction)]) {
                    ReportInvalid(
                        sink,
                        "explicit UI navigation names a target that snapshot N "
                        "does not publish as focusable; focus did not move",
                        "point explicit navigation at an enabled, interactable "
                        "UISelectable",
                        result.resolvedStageTarget->runtimeTarget.objectId);
                }
            }
            return result;
        }
        const UIRuntimeTargetIdentity& destination =
            planned.focusDestinationFromSnapshotN->runtimeTarget;
        if (!ResolveTarget(world, destination)) {
            ReportInvalid(sink,
                          "a planned UI focus destination no longer resolves; "
                          "focus was not moved and was not retargeted",
                          "re-plan input against the current snapshot",
                          destination.objectId);
            return result;
        }
        UISurfaceInteractionState next = table.SurfaceOrEmpty(key);
        if (next.focused == destination) return result;
        next.focused = destination;
        bool changed = false;
        if (table.Apply(key, std::move(next), &sink, "ui.focus",
                        destination.objectId, &changed)) {
            result.actionMask = static_cast<std::uint16_t>(
                UIEventActionBit(UIEventAction::Navigate) |
                UIEventActionBit(UIEventAction::Focus));
            result.consumed = true;
            result.resultingFocus = destination;
            result.visualDirty = changed;
        }
        return result;
    }

    if (event.kind == UIInputEventKind::PointerButton && event.active &&
        result.resolvedStageTarget) {
        const UIRuntimeTargetIdentity& target =
            result.resolvedStageTarget->runtimeTarget;
        if (!ResolveTarget(world, target)) {
            ReportInvalid(sink,
                          "a planned UI focus target no longer resolves; focus "
                          "was not moved and was not retargeted",
                          "re-plan input against the current snapshot",
                          target.objectId);
            return result;
        }
        UISurfaceInteractionState next = table.SurfaceOrEmpty(key);
        if (next.focused == target) return result;
        next.focused = target;
        bool changed = false;
        if (table.Apply(key, std::move(next), &sink, "ui.focus",
                        target.objectId, &changed)) {
            result.actionMask = UIEventActionBit(UIEventAction::Focus);
            result.resultingFocus = target;
            result.visualDirty = changed;
        }
    }
    return result;
}

void UIFocusSystem::Revalidate(World& world, const UISnapshot& n,
                               molga::text::TextDiagnosticSink& sink) {
    auto& table = UIInteractionStateTable::Get();
    const UISurfaceKey key{n.surfaceWindowId, n.worldGeneration};
    const UISurfaceInteractionState* current = table.Surface(key);
    if (!current) return;
    UISurfaceInteractionState next = *current;
    unsigned int objectId = 0;
    const auto drop = [&](std::optional<UIRuntimeTargetIdentity>& slot) {
        if (slot && !IsAlive(world, *slot)) {
            objectId = slot->objectId;
            slot.reset();
        }
    };
    drop(next.focused);
    drop(next.pointerCapture);
    drop(next.hovered);
    for (auto it = next.buttons.begin(); it != next.buttons.end();) {
        if (IsAlive(world, it->first)) {
            ++it;
        } else {
            objectId = it->first.objectId;
            it = next.buttons.erase(it);
        }
    }
    (void)table.Apply(key, std::move(next), &sink, "ui.focus", objectId);
}

std::optional<UIRuntimeTargetIdentity> UIFocusSystem::Focused(
    molga::WindowId windowId, std::uint64_t worldGeneration) const {
    return UIInteractionStateTable::Get()
        .SurfaceOrEmpty({windowId, worldGeneration})
        .focused;
}

std::optional<UIRuntimeTargetIdentity> UIFocusSystem::CapturedPointer(
    molga::WindowId windowId, std::uint64_t worldGeneration) const {
    return UIInteractionStateTable::Get()
        .SurfaceOrEmpty({windowId, worldGeneration})
        .pointerCapture;
}

void UIFocusSystem::ClearPointerState(molga::WindowId windowId,
                                      std::uint64_t worldGeneration,
                                      molga::text::TextDiagnosticSink& sink) {
    auto& table = UIInteractionStateTable::Get();
    const UISurfaceKey key{windowId, worldGeneration};
    UISurfaceInteractionState next = table.SurfaceOrEmpty(key);
    ClearPointer(next);
    (void)table.Apply(key, std::move(next), &sink, "ui.focus", 0);
}

void UIFocusSystem::OnNativeWindowFocusLost(
    molga::WindowId windowId, molga::text::TextDiagnosticSink& sink) {
    auto& table = UIInteractionStateTable::Get();
    // Apply가 표를 바꾸므로 키를 먼저 모은다(SurfacesForWindow는 사본이다).
    for (const UISurfaceKey& key : table.SurfacesForWindow(windowId)) {
        UISurfaceInteractionState next = table.SurfaceOrEmpty(key);
        ClearEverything(next);
        (void)table.Apply(key, std::move(next), &sink, "ui.focus", 0);
    }
}

void UIFocusSystem::OnWorldReleased(std::uint64_t worldGeneration) {
    UIInteractionStateTable::Get().OnWorldReleased(worldGeneration);
}

}  // namespace molga::ui
