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

}  // namespace

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

void UIFocusSystem::ProjectEvent(const UISnapshot& /*n*/, PlannedUIEvent& plan,
                                 UIPlanningState& /*projected*/) const {
    // 부적격 계획에는 아무것도 쓰지 않는다. 방향 투사는 Step D에서 얹는다.
    if (!plan.surfaceEligible) return;
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
