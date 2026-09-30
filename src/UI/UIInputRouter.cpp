#include "UI/UIInputRouter.h"

#include "Core/World.h"
#include "ECS/Component.h"
#include "ECS/Components/UIButton.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstdint>
#include <utility>

namespace molga::ui {
namespace {

using molga::text::TextDiagnostic;
using molga::text::TextDiagnosticCode;
using molga::text::TextSeverity;

// 경계 배제. 사각형은 [x, x+width) × [y, y+height) 이고, 그래서 맞닿은 두
// 위젯 사이의 한 픽셀이 양쪽 모두에 속하는 일이 없다. raw는 검증된 26.6
// 정수이고, 합은 int64로 올려서 더한다 — int32 안에서 더하면 극단적인 사각형
// 하나가 조용히 감긴다.
bool ContainsExclusive(const molga::FixedRect& rect,
                       molga::FixedPoint point) noexcept {
    const std::int64_t x = point.x.Raw();
    const std::int64_t y = point.y.Raw();
    const std::int64_t left = rect.x.Raw();
    const std::int64_t top = rect.y.Raw();
    const std::int64_t right = left + static_cast<std::int64_t>(rect.width.Raw());
    const std::int64_t bottom =
        top + static_cast<std::int64_t>(rect.height.Raw());
    return x >= left && x < right && y >= top && y < bottom;
}

// 부모 클립까지 통과해야 그 기록이 그 점을 담는다. 클립은 이미 조상들과
// 교집합을 취한 결과이므로 여기서 다시 조상을 걸어 올라가지 않는다.
bool RouteContains(const UIHitTargetSnapshot& hit,
                   molga::FixedPoint point) noexcept {
    if (hit.logicalClip && !ContainsExclusive(*hit.logicalClip, point)) {
        return false;
    }
    return ContainsExclusive(hit.logicalRect, point);
}

// 게시된 hitTargets는 UIDrawOrderKey 오름차순이다. hit-test는 그 **정확한
// 역순**을 본다 — 가장 나중에 그려진 것이 가장 위에 있다.
//
// 상호작용 여부와 무관하게 맨 위의 기록 하나다. 스크롤이 이것을 쓴다: 목록의
// 글자나 배경 이미지는 상호작용 불가 기록이지만 자기 조상 스크롤 대상을 든다.
const UIHitTargetSnapshot* TopmostRouteAt(const UISnapshot& n,
                                          molga::FixedPoint point) noexcept {
    for (auto it = n.hitTargets.rbegin(); it != n.hitTargets.rend(); ++it) {
        if (RouteContains(*it, point)) return &(*it);
    }
    return nullptr;
}

// ── 포인터 동작 대상 정책 (컨트롤러 결정, Task 12.1 라운드 3) ───────────────
// 그 점 아래에서 **상호작용 가능한** 맨 위 기록이다. 상호작용 불가 기록 — 버튼
// 위의 라벨, 꺼 둔 버튼의 배경 이미지 — 은 포인터 동작 대상 선택에서 투명하다.
//
// 이것은 오늘의 공개 동작을 그대로 지키는 선택이다: 레거시
// UISystem::ProcessInput은 상호작용 가능한 버튼만 보므로, 에디터 기본 Button
// 프리셋(버튼 전체를 거의 덮는 자식 Label)을 눌러도 버튼이 눌린다. 맨 위
// 기록에서 멈추면 그 버튼은 가장자리에서만 눌리고, 그 회귀는 Task 12.3이 실제
// 입력을 PlanNext로 넘기는 순간 에디터와 런타임에 함께 나타난다.
//
// 더 나은 모델(가장 가까운 상호작용 가능 조상으로 보내고, 없으면 가린다)은
// 공개 동작을 바꾸므로 사용자 승인을 기다린다. 바꿀 자리는 이 함수 하나다.
const UIHitTargetSnapshot* PointerActionRouteAt(
    const UISnapshot& n, molga::FixedPoint point) noexcept {
    for (auto it = n.hitTargets.rbegin(); it != n.hitTargets.rend(); ++it) {
        if (it->interactable && RouteContains(*it, point)) return &(*it);
    }
    return nullptr;
}

const UIHitTargetSnapshot* RouteForActionTarget(
    const UISnapshot& n, const UIRuntimeTargetIdentity& target) noexcept {
    for (const auto& hit : n.hitTargets) {
        if (hit.target == target) return &hit;
    }
    return nullptr;
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

const UIHitTargetSnapshot* RouteForTextInputTarget(
    const UISnapshot& n, const UIRuntimeTargetIdentity& input) noexcept {
    for (const auto& hit : n.hitTargets) {
        if (hit.textInputTarget && hit.textInputTarget->runtimeTarget == input) {
            return &hit;
        }
    }
    return nullptr;
}

// 한 hit 경로의 **모든** 단계 대상을 계획에 얼린다. 단계마다 따로 고르지
// 않는다 — 그러면 동작 대상과 스크롤 대상이 서로 다른 경로에서 올 수 있고,
// 그 조합은 스냅샷 N의 어느 기록에도 없던 것이다.
void FreezeRoute(PlannedUIEvent& plan, const UIHitTargetSnapshot& route) {
    plan.targetFromSnapshotN = route.target;
    plan.canonicalTargetFromSnapshotN = route.canonicalTarget;
    plan.focusTargetFromSnapshotN = route.focusTarget;
    plan.textInputTargetFromSnapshotN = route.textInputTarget;
    plan.scrollTargetsFromSnapshotN = route.scrollTargets;
}

TextDiagnostic MakeReferenceInvalid(const char* message,
                                    const char* remediation,
                                    unsigned int sceneObjectId) {
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::ReferenceInvalid;
    diagnostic.severity = TextSeverity::Warning;
    diagnostic.subsystem = "ui.input";
    diagnostic.message = message;
    diagnostic.remediation = remediation;
    diagnostic.sceneObjectId = sceneObjectId;
    return diagnostic;
}

}  // namespace

// ── Step 4c2: 단계 대상을 얻는 유일한 접근자 ────────────────────────────────
std::optional<UIFrozenTarget> PlannedUIEvent::TargetFor(
    UIEventStage stage, std::size_t ordinal) const {
    if (!surfaceEligible) return std::nullopt;
    switch (stage) {
        case UIEventStage::Input:
            if (!targetFromSnapshotN || !canonicalTargetFromSnapshotN) {
                return std::nullopt;
            }
            return UIFrozenTarget{*targetFromSnapshotN,
                                  *canonicalTargetFromSnapshotN};
        case UIEventStage::Focus:
            // 방향 입력이 목적지를 해석했다면 그 목적지가 답이고, 아니면
            // 이 경로가 얼려 둔 포커스 대상이다.
            return focusDestinationFromSnapshotN ? focusDestinationFromSnapshotN
                                                 : focusTargetFromSnapshotN;
        case UIEventStage::TextInput:
            return textInputTargetFromSnapshotN;
        case UIEventStage::Scroll:
            return ordinal < scrollTargetsFromSnapshotN.size()
                       ? std::optional<UIFrozenTarget>{
                             scrollTargetsFromSnapshotN[ordinal]}
                       : std::nullopt;
        case UIEventStage::OwnerTransition:
            return ownerTransitionTargetFromSnapshotN;
    }
    return std::nullopt;
}

// ── Step 6b: 감사 누산 ──────────────────────────────────────────────────────
UIEventDispatchAccumulator::UIEventDispatchAccumulator(
    const PlannedUIEvent& planned, molga::text::TextDiagnosticSink& sink)
    : planned_(&planned), sink_(&sink) {
    record_.sequence = planned.event.sequence;
    record_.traceOrdinal = planned.event.traceOrdinal;
    record_.kind = planned.event.kind;

    // 외부 창 기록은 순서에는 남되 대상이 없다.
    if (!planned.surfaceEligible) return;

    switch (planned.event.kind) {
        case UIInputEventKind::TextEditing:
        case UIInputEventKind::TextCommit:
        case UIInputEventKind::PointerMotion:
        case UIInputEventKind::PointerButton:
        case UIInputEventKind::Key:
        case UIInputEventKind::GamepadButton:
        case UIInputEventKind::GamepadAxis:
            // 도장 받은 텍스트 대상과 포인터/키/게임패드의 동작 대상은 같은
            // 두 필드에 산다. 계획을 만들 때 이미 규칙이 갈렸다.
            record_.runtimeTarget = planned.targetFromSnapshotN;
            record_.canonicalTarget = planned.canonicalTargetFromSnapshotN;
            break;
        case UIInputEventKind::Scroll:
            // 스크롤의 주 대상은 계획이 아니라 결과가 정한다: 안쪽에서
            // 바깥쪽으로 내려가며 **실제로 소비한** 첫 대상이다. 계획의 동작
            // 대상을 미리 넣으면 아무것도 소비하지 않은 스크롤이 소비한
            // 것처럼 기록된다.
            primaryOpenForScroll_ = true;
            break;
        case UIInputEventKind::WindowFocus:
        case UIInputEventKind::Other:
            break;
    }
}

UIEventMergeStatus UIEventDispatchAccumulator::Merge(
    const UIEventHandlerResult& result) {
    if (spent_) {
        sink_->Report(MakeReferenceInvalid(
            "a UI dispatch accumulator received a handler result after it was "
            "finished",
            "merge every handler result before calling Finish", 0));
        return UIEventMergeStatus::RejectedSpent;
    }
    if (result.sequence != record_.sequence) {
        sink_->Report(MakeReferenceInvalid(
            "a UI handler result names a different native sequence than the "
            "event it was merged into",
            "merge each handler result into the accumulator for its own event",
            0));
        return UIEventMergeStatus::RejectedSequence;
    }
    // 기대 대상은 오직 계획에서 온다. 결과가 든 대상을 그대로 믿으면 다른
    // 단계의 (그 자체로는 멀쩡한) 대상이 이 단계의 결과로 통과한다.
    const auto expected = planned_->TargetFor(result.stage,
                                              result.stageTargetOrdinal);
    if (!(expected == result.resolvedStageTarget)) {
        sink_->Report(MakeReferenceInvalid(
            "a UI handler result resolved a target that is not this event's "
            "frozen target for that stage",
            "resolve every stage target through PlannedUIEvent::TargetFor",
            record_.runtimeTarget ? record_.runtimeTarget->objectId : 0U));
        return UIEventMergeStatus::RejectedStageTarget;
    }
    // 기대 대상도 보고 대상도 없는 결과는 "둘 다 nullopt라 같다"는 이유만으로
    // 효과를 들여올 수 없다. 대상 없이 소비/전달/동작/포커스를 주장하는 결과는
    // 거절한다 — 그대로 합치면 외부 창 기록이 "소비되고 클릭됨"이 되거나,
    // 텍스트 대상이 없는 포인터 계획에 Edit가 붙는다. 표면 dirty 플래그는 대상
    // 없이도 정당할 수 있으나(Task 12.2의 포커스 해제), 부적격 계획에는 그것도
    // 들어올 수 없다 — 외부 창 이벤트는 이 표면의 어떤 상태도 바꾸지 못한다.
    if (!result.resolvedStageTarget) {
        const bool claimsEffect = result.actionMask != 0 || result.consumed ||
                                  result.callbackDelivered ||
                                  result.resultingFocus.has_value();
        const bool claimsDirt = result.arrangementDirty || result.visualDirty;
        if (claimsEffect || (!planned_->surfaceEligible && claimsDirt)) {
            sink_->Report(MakeReferenceInvalid(
                "a UI handler result claimed an effect without resolving a "
                "stage target",
                "report consumed/delivered/actions/focus only together with "
                "the frozen stage target they apply to",
                record_.runtimeTarget ? record_.runtimeTarget->objectId : 0U));
            return UIEventMergeStatus::RejectedTargetless;
        }
    }

    record_.actionMask =
        static_cast<std::uint16_t>(record_.actionMask | result.actionMask);
    record_.consumed = record_.consumed || result.consumed;
    record_.delivered = record_.delivered || result.callbackDelivered;
    record_.arrangementDirty = record_.arrangementDirty || result.arrangementDirty;
    record_.visualDirty = record_.visualDirty || result.visualDirty;
    // 포커스 부수효과는 자기 필드에만 들어간다. 주 대상을 대신하지 않는다.
    if (result.resultingFocus) record_.resultingFocus = result.resultingFocus;

    if (primaryOpenForScroll_ && result.stage == UIEventStage::Scroll &&
        result.consumed && result.resolvedStageTarget) {
        record_.runtimeTarget = result.resolvedStageTarget->runtimeTarget;
        record_.canonicalTarget = result.resolvedStageTarget->canonicalTarget;
        primaryOpenForScroll_ = false;
    }
    return UIEventMergeStatus::Merged;
}

UIEventDispatchRecord UIEventDispatchAccumulator::Finish() && {
    if (spent_) {
        sink_->Report(MakeReferenceInvalid(
            "a UI dispatch accumulator was finished twice; the second record "
            "carries no target and no effect",
            "finish each accumulator exactly once", 0));
        UIEventDispatchRecord hollow;
        hollow.sequence = record_.sequence;
        hollow.traceOrdinal = record_.traceOrdinal;
        hollow.kind = record_.kind;
        return hollow;
    }
    spent_ = true;
    return record_;
}

// ── Step 5a/5b: 스냅샷 N 위의 순수 선택과 순차 투사 ─────────────────────────
PlannedUIEvent UIInputRouter::PlanNext(const UISnapshot& n,
                                       molga::WindowId surfaceWindowId,
                                       const UIInputEvent& event,
                                       UIPlanningState& projected) const {
    PlannedUIEvent plan;
    plan.event = event;

    // 창부터 본다. 다른 창의 이벤트는 순서에 남되 이 표면의 어떤 상태도
    // 읽거나 바꾸지 못한다 — 투사 상태에 손대기 전에 돌아간다.
    if (event.windowId != surfaceWindowId) return plan;
    // 다른 표면의 스냅샷으로는 이 표면의 대상을 이름할 수 없다. fail-closed.
    if (n.surfaceWindowId != surfaceWindowId) return plan;

    plan.surfaceEligible = true;

    switch (event.kind) {
        case UIInputEventKind::WindowFocus: {
            projected.nativeWindowFocused = event.active;
            if (!event.active) {
                projected.focused.reset();
                projected.pointerCapture.reset();
                projected.hovered.reset();
                projected.pointerValid = false;
            }
            return plan;
        }

        case UIInputEventKind::PointerMotion:
        case UIInputEventKind::PointerButton: {
            if (!event.logicalPointValid) {
                // 포인터 이탈. 위치가 없으므로 hit-test를 하지 않는다 — 0이나
                // 기본 좌표를 대신 넣으면 원점에 있는 위젯이 hover를 얻는다.
                // 키보드/게임패드 포커스는 건드리지 않는다.
                projected.hovered.reset();
                projected.pointerCapture.reset();
                projected.pointerValid = false;
                return plan;
            }
            projected.pointer = event.logicalPoint;
            projected.pointerValid = true;

            const UIHitTargetSnapshot* route = nullptr;
            const bool press =
                event.kind == UIInputEventKind::PointerButton && event.active;
            const bool release =
                event.kind == UIInputEventKind::PointerButton && !event.active;

            if (press) {
                route = PointerActionRouteAt(n, projected.pointer);
                if (route) {
                    projected.pointerCapture = route->target;
                    projected.hovered = route->target;
                    // 동작 대상과 포커스 대상은 다른 것이다. 포커스는 그
                    // 경로가 얼려 둔 형제 selectable에서만 온다.
                    if (route->focusTarget) {
                        projected.focused = route->focusTarget->runtimeTarget;
                    }
                } else {
                    projected.pointerCapture.reset();
                    projected.hovered.reset();
                }
            } else if (projected.pointerCapture) {
                // 잡혀 있는 동안에는 위치가 어디로 가든 그 경로가 답이다.
                // 다시 hit-test하면 드래그가 사각형 밖으로 나가는 순간 대상이
                // 바뀌고, 그것이 캡처가 막으려던 바로 그 일이다.
                //
                // 단, 스냅샷 N이 그 기록을 상호작용 불가라고 말하면 배달하지
                // 않는다. 캡처는 이전 프레임에서 넘어올 수 있고(Task 12.3이
                // 프레임마다 투사 상태를 다시 세운다), N이 상호작용 여부의
                // 유일한 권위다.
                route = RouteForActionTarget(n, *projected.pointerCapture);
                if (route && !route->interactable) route = nullptr;
                if (release) projected.pointerCapture.reset();
            } else {
                // 잡힌 것이 없는 움직임과 놓기. hover는 갱신한다. 그러나 잡힌
                // 누름이 없는 놓기는 동작 대상이 없다 — 클릭은 **같은 완전한
                // 식별자** 위에서 잡힌 누름을 요구한다. 빈 곳에서 눌러 버튼
                // 위에서 놓거나, 누른 뒤 창 포커스를 잃고 다른 버튼 위에서 놓는
                // 것은 클릭이 아니다.
                const UIHitTargetSnapshot* under =
                    PointerActionRouteAt(n, projected.pointer);
                if (under) {
                    projected.hovered = under->target;
                } else {
                    projected.hovered.reset();
                }
                if (!release) route = under;
            }

            if (route) FreezeRoute(plan, *route);
            return plan;
        }

        case UIInputEventKind::Scroll: {
            // 변위는 위치가 아니다. 투사된 포인터가 없으면 대상도 없다 —
            // (0,0)을 위치로 삼으면 원점의 스크롤 뷰가 남의 휠을 먹는다.
            if (!projected.pointerValid) return plan;
            // 스크롤 대상은 상호작용 여부와 무관하게 **맨 위** 기록의 것이다.
            // 목록의 글자와 배경 이미지는 상호작용 불가 기록이지만 자기 조상
            // 스크롤 뷰를 안쪽에서 바깥쪽으로 든다. 상호작용 가능한 기록만
            // 보면 글자 위의 휠이 아무것도 스크롤하지 않는다. 가림은 그대로다:
            // 겹친 오버레이는 뒤의 목록이 아니라 **자기** 조상을 낸다.
            const UIHitTargetSnapshot* route =
                TopmostRouteAt(n, projected.pointer);
            if (!route) return plan;
            if (route->interactable) {
                FreezeRoute(plan, *route);
            } else {
                // 상호작용 불가 기록은 동작/포커스/텍스트 대상을 내지 않는다.
                // 얼리는 것은 스크롤 사슬뿐이다.
                plan.scrollTargetsFromSnapshotN = route->scrollTargets;
            }
            return plan;
        }

        case UIInputEventKind::Key:
        case UIInputEventKind::GamepadButton:
        case UIInputEventKind::GamepadAxis: {
            if (!projected.focused) return plan;
            const UIHitTargetSnapshot* route =
                RouteForFocusTarget(n, *projected.focused);
            if (route) FreezeRoute(plan, *route);
            return plan;
        }

        case UIInputEventKind::TextEditing:
        case UIInputEventKind::TextCommit: {
            // **투사된 포커스를 보지 않는다.** 이 이벤트는 호스트가 복사할 때
            // 이미 소유자가 정해졌고, 같은 배치의 앞선 포인터가 포커스를 옮겼다
            // 해도 그 도장을 다시 쓸 수는 없다.
            if (!event.text) return plan;
            const auto& stamp = event.text->ownerAtIngest;
            if (!stamp.NamesRuntimeTarget()) return plan;

            const UIRuntimeTargetIdentity stamped = *stamp.runtimeTarget;
            // 감사 기록의 주 대상은 언제나 도장이다. 스냅샷 N에 그 입력창이
            // 남아 있지 않아도 이 이벤트가 누구의 것이었는지는 달라지지 않는다.
            plan.targetFromSnapshotN = stamped;

            const UIHitTargetSnapshot* route =
                RouteForTextInputTarget(n, stamped);
            if (route) {
                plan.canonicalTargetFromSnapshotN =
                    route->textInputTarget->canonicalTarget;
                plan.textInputTargetFromSnapshotN = route->textInputTarget;
                plan.scrollTargetsFromSnapshotN = route->scrollTargets;
            }
            return plan;
        }

        case UIInputEventKind::Other:
            return plan;
    }
    return plan;
}

// ── Step 6a: 발송 시각의 완전한 식별자 해석 ─────────────────────────────────
UIEventHandlerResult UIInputRouter::HandleEvent(
    World& world, const UISnapshot& n, const PlannedUIEvent& planned,
    molga::text::TextDiagnosticSink& sink) {
    UIEventHandlerResult result;
    result.sequence = planned.event.sequence;
    result.stage = UIEventStage::Input;
    result.stageTargetOrdinal = 0;

    // 대상은 오직 여기서만 온다. 이 줄이 ECS를 다시 뒤지는 순간 계획이
    // 얼려 둔 대상은 아무 의미가 없어진다.
    const auto stageTarget = planned.TargetFor(UIEventStage::Input);
    if (!stageTarget) return result;
    result.resolvedStageTarget = stageTarget;

    // 네 필드를 전부 다시 해석한다. 그 사이에 컴포넌트가 지워지고 같은 타입이
    // 다시 붙었다면 여기서 nullptr이고, 그때 이 계획은 낡은 것이다.
    Component* component = ResolveTarget(world, stageTarget->runtimeTarget);
    if (!component) {
        sink.Report(MakeReferenceInvalid(
            "a planned UI event named a target that no longer resolves; the "
            "event was not delivered and was not retargeted",
            "re-plan input against the current snapshot",
            stageTarget->runtimeTarget.objectId));
        return result;
    }

    UIButton* button = nullptr;
    if (stageTarget->runtimeTarget.componentRuntimeTypeId ==
        ComponentTypeID::Get<UIButton>()) {
        button = static_cast<UIButton*>(component);
    }

    switch (planned.event.kind) {
        case UIInputEventKind::PointerButton: {
            if (planned.event.active) {
                result.actionMask = static_cast<std::uint16_t>(
                    UIEventActionBit(UIEventAction::Hover) |
                    UIEventActionBit(UIEventAction::Press));
                result.consumed = true;
                result.callbackDelivered = true;
                result.visualDirty = true;
                if (button) button->ApplyPointerState(true, true, false);
            } else {
                // 클릭 판정은 **스냅샷 N**의 사각형과 클립으로 한다. 살아
                // 있는 컴포넌트의 기하를 다시 읽으면 콜백이 방금 옮겨 놓은
                // 사각형으로 판정하게 된다.
                bool inside = false;
                if (planned.event.logicalPointValid) {
                    const UIHitTargetSnapshot* route =
                        RouteForActionTarget(n, stageTarget->runtimeTarget);
                    inside = route != nullptr &&
                             RouteContains(*route, planned.event.logicalPoint);
                }
                result.actionMask = UIEventActionBit(UIEventAction::Release);
                if (inside) {
                    result.actionMask = static_cast<std::uint16_t>(
                        result.actionMask |
                        UIEventActionBit(UIEventAction::Click));
                }
                result.consumed = true;
                result.callbackDelivered = true;
                result.visualDirty = true;
                if (button) button->ApplyPointerState(inside, false, inside);
            }
            break;
        }
        case UIInputEventKind::PointerMotion: {
            result.actionMask = UIEventActionBit(UIEventAction::Hover);
            result.callbackDelivered = true;
            break;
        }
        case UIInputEventKind::Key:
        case UIInputEventKind::GamepadButton:
        case UIInputEventKind::GamepadAxis:
        case UIInputEventKind::Scroll:
        case UIInputEventKind::TextEditing:
        case UIInputEventKind::TextCommit: {
            // 이 단계가 하는 일은 대상을 확정해 전달하는 것까지다. 포커스와
            // 내비게이션은 Task 12.2의 UIFocusSystem이, 스크롤 소비는
            // UIScrollSystem이, 편집은 Task 14의 UITextInputSystem이 각자
            // 자기 단계에서 한다.
            result.callbackDelivered = true;
            break;
        }
        case UIInputEventKind::WindowFocus:
        case UIInputEventKind::Other:
            break;
    }
    return result;
}

}  // namespace molga::ui
