#include "UI/UISystem.h"

#include "Core/World.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/GameObject.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/Shader.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/Texture.h"

#include "Common/Fixed26_6.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutTypes.h"
#include "UI/UIHierarchy.h"
#include "UI/UIInteractionState.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIScrollSystem.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <vector>

namespace {
struct ButtonEntry {
    GameObject* object = nullptr;
    UIButton* button = nullptr;
    AABB rect;
    int canvasOrder = 0;
    int order = 0;
    std::size_t traversal = 0;
};

int CanvasOrder(GameObject* object) {
    for (GameObject* node = object; node; node = node->GetParent()) {
        if (auto* canvas = node->GetComponent<UICanvas>()) return canvas->GetSortingOrder();
    }
    return 0;
}

// 규칙의 사본은 하나다(UI/UIHierarchy.h). 여기에 두 번째 벌을 두면 배치가
// 보는 "활성"과 입력이 보는 "활성"이 갈릴 수 있고, 그 어긋남은 관찰되지 않는다.
using molga::ui::IsHierarchyActive;

UICanvas* ActiveCanvas(GameObject* object) {
    for (GameObject* node = object; node; node = node->GetParent()) {
        if (auto* canvas = node->GetComponent<UICanvas>()) {
            return canvas->IsEnabled() ? canvas : nullptr;
        }
    }
    return nullptr;
}

bool IsHigher(const ButtonEntry& a, const ButtonEntry& b) {
    return std::tie(a.canvasOrder, a.order, a.traversal) >
           std::tie(b.canvasOrder, b.order, b.traversal);
}

std::vector<ButtonEntry> GatherButtons(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const Vector2& viewport) {
    std::vector<ButtonEntry> result;
    if (viewport.x <= 0.0f || viewport.y <= 0.0f) return result;
    std::size_t traversal = 0;
    for (const auto& object : objects) {
        ++traversal;
        if (!object) continue;
        auto* button = object->GetComponent<UIButton>();
        auto* rect = object->GetComponent<RectTransform>();
        if (!button || !rect || !IsHierarchyActive(object.get()) ||
            !button->IsEnabled() || !rect->IsEnabled() || !ActiveCanvas(object.get())) {
            // 제외된 버튼만 여기서 비운다. 포함된 버튼은 ProcessInput이 매 프레임
            // (clicked=false로) 다시 쓰므로 미리 비우면 steady hover가 true->false->true로
            // 의미 세대를 두 번 올린다.
            if (button) button->ApplyPointerState(false, false, false);
            continue;
        }
        result.push_back({object.get(), button, rect->GetScreenRect(viewport),
                          CanvasOrder(object.get()), button->GetSortingOrder(), traversal});
    }
    return result;
}
} // namespace

UISystem::~UISystem() {
    molga::ui::SetUIWorldReleaseHandler(nullptr);
    molga::ui::SetUIDeviceRetireHandler(nullptr);
}

UISystem& UISystem::Get() {
    static UISystem system;
    // 이 시설이 처음 만들어지는 순간에만 등록한다. World가 세대를 은퇴시킬 때
    // 부를 이름이 있어야 죽은 월드의 기하 LRU와 스냅샷 칸이 회수된다. UI를
    // 한 번도 만들지 않은 프로세스에서는 핸들러가 없고, 비울 캐시도 없다.
    static const bool releaseHandlerInstalled = [] {
        molga::ui::SetUIWorldReleaseHandler([](std::uint64_t generation) {
            UISystem::Get().OnWorldReleased(generation);
        });
        // 장치 축의 나머지 절반. GraphicsDevice가 은퇴할 때 그 세대에 묶인
        // 스냅샷/슬롯을 놓는다 — 알리지 않으면 죽은 세대의 텍스처 핸들과
        // 바인딩 수명 토큰을 UI가 계속 들고, 그 토큰이 다음 종료를 막는다.
        molga::ui::SetUIDeviceRetireHandler([](std::uint64_t generation) {
            UISystem::Get().ClearFullSnapshotBindingCache(generation);
        });
        return true;
    }();
    (void)releaseHandlerInstalled;
    return system;
}

void UISystem::ProcessInput(World& world, const Vector2& viewportSize,
                            const UIPointerState& pointer) {
    auto& objects = world.Objects();
    auto buttons = GatherButtons(objects, viewportSize);
    if (!pointer.valid) ResetPointerCapture();
    ButtonEntry* topmost = nullptr;
    if (pointer.valid) {
        for (auto& entry : buttons) {
            if (entry.button->IsInteractable() && entry.rect.Contains(pointer.position) &&
                (!topmost || IsHigher(entry, *topmost))) {
                topmost = &entry;
            }
        }
    }

    if (capturedOwner_ != &world) ResetPointerCapture();
    if (pointer.pressedThisFrame && topmost) {
        capturedOwner_ = &world;
        capturedObjectId_ = topmost->object->GetID();
    }

    ButtonEntry* captured = nullptr;
    for (auto& entry : buttons) {
        if (entry.object->GetID() == capturedObjectId_) {
            captured = &entry;
            break;
        }
    }
    unsigned int clickedId = 0;
    for (auto& entry : buttons) {
        const bool hovered = topmost && topmost->object == entry.object;
        const bool isCaptured = captured == &entry;
        const bool clicked = isCaptured && pointer.releasedThisFrame &&
                             pointer.valid && entry.rect.Contains(pointer.position);
        entry.button->ApplyPointerState(hovered, isCaptured && pointer.down, false);
        if (clicked) clickedId = entry.object->GetID();
    }

    if (pointer.releasedThisFrame || (capturedObjectId_ != 0 && !captured)) {
        ResetPointerCapture();
    }
    // Dispatch only after the raw-entry iteration has finished. A click callback
    // may clear the world or transition scenes and invalidate every entry above.
    if (clickedId != 0) {
        if (GameObject* object = world.FindById(clickedId)) {
            if (auto* button = object->GetComponent<UIButton>()) {
                button->ApplyPointerState(topmost && topmost->object == object, false, true);
            }
        }
    }
}

void UISystem::CollectRender(const molga::ui::UISnapshot& snapshot,
                             const molga::ui::UIPhysicalTransform& transform,
                             molga::RenderQueue& queue,
                             TextRenderer& textRenderer,
                             molga::text::TextDiagnosticSink& textDiagnostics) {
    renderCollector_.Collect(snapshot, transform, queue, textRenderer,
                             textDiagnostics);
}

GameObject* UISystem::PickObject(World& world,
                                 const molga::ui::UISnapshot& snapshot,
                                 molga::FixedPoint logicalPoint) const {
    const molga::ui::UIHitTargetSnapshot* hit =
        molga::ui::TopmostHitRecordAt(snapshot, logicalPoint);
    if (!hit) return nullptr;
    // 스냅샷은 다른 월드 세대의 것일 수 있다. 식별자가 이 월드의 것이 아니면 고르지
    // 않는다.
    if (hit->target.worldGeneration != world.Generation()) return nullptr;
    return world.FindById(hit->target.objectId);
}

std::uint64_t UISystem::RenderCollectorDroppedItemCountForTesting() const noexcept {
    return renderCollector_.DroppedItemCount();
}

std::uint64_t UISystem::RenderCollectorSubmittedItemCountForTesting() const noexcept {
    return renderCollector_.SubmittedItemCount();
}

molga::ui::UISnapshotPtr UISystem::BuildLayout(
    World& world, molga::WindowId surfaceWindowId,
    molga::FixedSize logicalViewport,
    const molga::ui::UITextInputVisualStateProvider& inputVisualStates,
    molga::text::TextLayoutService& textLayout,
    molga::text::TextDiagnosticSink& textDiagnostics) {
    return layout_.Build(world, surfaceWindowId, logicalViewport,
                         inputVisualStates, textLayout, textDiagnostics);
}

void UISystem::OnDeviceGenerationChanged(std::uint64_t oldGeneration,
                                         std::uint64_t newGeneration) {
    layout_.OnDeviceGenerationChanged(oldGeneration, newGeneration);
}

void UISystem::ClearFullSnapshotBindingCache(std::uint64_t deviceGeneration) {
    layout_.ClearFullSnapshotBindingCache(deviceGeneration);
}

std::size_t UISystem::FullSnapshotCacheEntryCountForWorldDevice(
    molga::ui::UISnapshotWorldDeviceSlotKey slot) const noexcept {
    return layout_.FullSnapshotCacheEntryCountForWorldDevice(slot);
}

void UISystem::InstallLayoutDependencies(
    const molga::ui::UITextInputVisualStateProvider& inputVisualStates,
    molga::text::TextLayoutService& textLayout) noexcept {
    inputVisualStates_ = &inputVisualStates;
    textLayout_ = &textLayout;
}

bool UISystem::HasLayoutDependencies() const noexcept {
    return inputVisualStates_ != nullptr && textLayout_ != nullptr;
}

molga::ui::UISnapshotPtr UISystem::BuildLayout(
    World& world, molga::WindowId surfaceWindowId,
    molga::FixedSize logicalViewport,
    molga::text::TextDiagnosticSink& textDiagnostics) {
    if (!HasLayoutDependencies()) return nullptr;
    return layout_.Build(world, surfaceWindowId, logicalViewport,
                         *inputVisualStates_, *textLayout_, textDiagnostics);
}

void UISystem::OnWorldReleased(std::uint64_t worldGeneration) {
    layout_.OnWorldReleased(worldGeneration);
    // 렌더 수집기의 진단 예산도 같은 seam에서 놓는다(Task 12.3: 소유자가 생겼다).
    renderCollector_.OnWorldReleased(worldGeneration);
    // ── Task 11.3: 런타임 스크롤 상태도 이 seam에서만 회수된다 ──────────────
    // Task 10.2에서 UILayoutSystem::OnWorldReleased는 옳았고, 손으로 시험되었고,
    // 아무도 부르지 않았다 — 은퇴한 모든 월드 세대가 캐시를 통째로 흘렸다.
    // 스크롤 표도 같은 모양이므로 같은 라우터에 붙인다: World ->
    // NotifyUIWorldReleased -> 여기 -> 표 세 개(상태/실패 집합/tick 커서).
    molga::ui::UIScrollSystem::Get().OnWorldReleased(worldGeneration);
    // 포커스/캡처/hover/pressed 표도 같은 seam에서 회수한다.
    molga::ui::UIInteractionStateTable::Get().OnWorldReleased(worldGeneration);
    // 프레임 장부와 한 번만 보고하는 장부도 그 세대의 것을 놓는다.
    const auto eraseGeneration = [worldGeneration](auto& ledger) {
        for (auto it = ledger.begin(); it != ledger.end();) {
            if (it->first.worldGeneration == worldGeneration) {
                it = ledger.erase(it);
            } else {
                ++it;
            }
        }
    };
    eraseGeneration(lastCompletedNativeSequenceBySurface_);
    eraseGeneration(lastCompletedTickBySurface_);
    depsReported_.erase(worldGeneration);
    reflowDeferredReported_.erase(worldGeneration);
}

void UISystem::ResetPointerCapture() {
    capturedOwner_ = nullptr;
    capturedObjectId_ = 0;
}

// ── Task 12.3 Step 4: ProcessFrame ───────────────────────────────────────────
namespace {
molga::ui::detail::UIFramePublicationObserver g_publicationObserver = nullptr;

void ReportFrame(molga::text::TextDiagnosticSink& sink,
                 molga::text::TextDiagnosticCode code,
                 molga::text::TextSeverity severity, const char* message,
                 const char* remediation) {
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.subsystem = "ui.frame";
    diagnostic.message = message;
    diagnostic.remediation = remediation;
    sink.Report(std::move(diagnostic));
}
}  // namespace

namespace molga::ui::detail {
void SetUIFramePublicationObserverForTest(UIFramePublicationObserver observer) noexcept {
    g_publicationObserver = observer;
}
}  // namespace molga::ui::detail

molga::ui::UIFrameResult UISystem::ProcessFrame(
    World& world, const molga::ui::UIFrameInput& input,
    molga::text::TextLayoutService& textLayout,
    molga::text::TextDiagnosticSink& sink) {
    using namespace molga::ui;
    using molga::text::TextDiagnosticCode;
    using molga::text::TextSeverity;

    UIFrameResult result;
    result.frameIndex = input.frameIndex;

    // 1. 가드. 설치된 편집 상태 제공자가 없으면 빌드할 수 없다(서비스는 인자다).
    if (inputVisualStates_ == nullptr) {
        if (depsReported_.insert(world.Generation()).second) {
            ReportFrame(sink, TextDiagnosticCode::LayoutInvalid, TextSeverity::Error,
                        "UISystem::ProcessFrame ran before the layout dependencies "
                        "were installed; this frame processed no input",
                        "call InstallLayoutDependencies once at startup");
        }
        return result;
    }
    if (input.windowId == 0) {
        ReportFrame(sink, TextDiagnosticCode::ReferenceInvalid, TextSeverity::Error,
                    "a UI frame named window 0; window 0 is not a surface",
                    "pass the exact surface window id");
        return result;
    }

    // 2. 스냅샷 N. 이 배치의 라우팅·포커스·스크롤은 전부 N만 본다.
    UISnapshotPtr n = layout_.Build(world, input.windowId, input.logicalViewport,
                                    *inputVisualStates_, textLayout, sink);
    if (!n) return result;  // Build가 이유를 보고했다.
    if (n->surfaceWindowId != input.windowId ||
        n->worldGeneration != world.Generation()) {
        ReportFrame(sink, TextDiagnosticCode::ReferenceInvalid, TextSeverity::Error,
                    "the layout snapshot does not belong to this frame's surface "
                    "window and world generation",
                    "build the snapshot for the frame's own surface");
        return result;
    }
    result.interactionSnapshot = n;

    // 3. 프레임 시작: 사라진 대상을 정리한다.
    focusSystem_.Revalidate(world, *n, sink);

    // 4. 클릭 가장자리를 지우고 투사 상태를 표와 호스트 씨앗에서 세운다. 배치마다
    // 새로 세우므로 투사 도중의 hover 발산은 다음 프레임으로 넘어가지 않는다.
    const UISurfaceKey key{input.windowId, n->worldGeneration};
    auto& table = UIInteractionStateTable::Get();
    table.ClearClickedThisFrame(key);
    UIPlanningState state;
    state.surfaceWindowId = key.windowId;
    state.surfaceWorldGeneration = key.worldGeneration;
    {
        const UISurfaceInteractionState surface = table.SurfaceOrEmpty(key);
        state.focused = surface.focused;
        state.pointerCapture = surface.pointerCapture;
        state.hovered = surface.hovered;
    }
    state.pointer = input.pointerAtBatchStart;
    state.pointerValid = input.pointerAtBatchStartValid;
    state.nativeWindowFocused = input.nativeWindowFocusedAtBatchStart;

    // 5. 시퀀스 장부: 배치 전체를 먼저 검증한다. 0, 같은 번호, 역행, 이미 처리한
    // 번호가 하나라도 있으면 아무것도 계획하거나 발송하지 않는다.
    std::vector<PlannedUIEvent> plans;
    bool sequencesValid = true;
    if (!input.orderedEvents.empty()) {
        const auto found = lastCompletedNativeSequenceBySurface_.find(key);
        std::uint64_t previous =
            found == lastCompletedNativeSequenceBySurface_.end() ? 0 : found->second;
        for (const UIInputEvent& event : input.orderedEvents) {
            if (event.sequence == 0 || event.sequence <= previous) {
                sequencesValid = false;
                break;
            }
            previous = event.sequence;
        }
        if (!sequencesValid) {
            ReportFrame(sink, TextDiagnosticCode::ReferenceInvalid, TextSeverity::Error,
                        "the native input sequence is zero, repeated, out of order or "
                        "already processed; the whole batch was rejected",
                        "the host allocates strictly increasing sequence numbers");
        }
    }

    if (sequencesValid && !input.orderedEvents.empty()) {
        // 6. 계획. N만 읽고 콜백은 하나도 부르지 않는다.
        plans.reserve(input.orderedEvents.size());
        for (const UIInputEvent& event : input.orderedEvents) {
            PlannedUIEvent plan =
                inputRouter_.PlanNext(*n, input.windowId, event, state);
            focusSystem_.ProjectEvent(*n, plan, state);
            plans.push_back(std::move(plan));
        }
        lastCompletedNativeSequenceBySurface_[key] =
            input.orderedEvents.back().sequence;

        // 7. 발송. 이벤트마다 라우터 -> 포커스 -> 스크롤 순서로 합치고, 콜백이 대상을
        // 지웠을 수 있으므로 매번 정리한다. 거절된 합침은 누산기가 이미 보고했다.
        result.dispatches.reserve(plans.size());
        bool worldReplaced = false;
        for (const PlannedUIEvent& planned : plans) {
            UIEventDispatchAccumulator audit(planned, sink);
            if (!worldReplaced) {
                (void)audit.Merge(inputRouter_.HandleEvent(world, *n, planned, sink));
                (void)audit.Merge(focusSystem_.HandleEvent(world, *n, planned, sink));
                (void)audit.Merge(
                    UIScrollSystem::Get().HandleEvent(world, *n, planned, sink));
                focusSystem_.Revalidate(world, *n, sink);
                // 콜백이 씬을 바꿨다면 남은 계획은 은퇴한 세대의 이름을 든다. 발송하지
                // 않고 감사 기록만 남긴다 — 죽은 세대의 표에 다시 쓰지 않는다.
                if (world.Generation() != n->worldGeneration) {
                    worldReplaced = true;
                    ReportFrame(sink, TextDiagnosticCode::ReferenceInvalid,
                                TextSeverity::Warning,
                                "an input callback replaced the world; the rest of "
                                "the batch was not delivered",
                                "events after a scene transition belong to the "
                                "retired world");
                }
            }
            result.dispatches.push_back(std::move(audit).Finish());
        }
    }

    // 8. tick: 묶음 전체를 검증하고 한 번만 소비한다.
    const bool worldStillN = world.Generation() == n->worldGeneration;
    if (worldStillN && !input.uiTicks.empty()) {
        const auto found = lastCompletedTickBySurface_.find(key);
        const std::uint64_t last =
            found == lastCompletedTickBySurface_.end() ? 0 : found->second;
        if (!UIDeterministicTickBatchIsValid(input.uiTicks) ||
            input.uiTicks.front().tickIndex <= last) {
            ReportFrame(sink, TextDiagnosticCode::LayoutInvalid, TextSeverity::Error,
                        "the UI tick batch is invalid or was already consumed; no "
                        "tick of it was applied",
                        "ticks are strictly increasing, 1 to 64 raw seconds each, "
                        "and each index is consumed once");
        } else {
            for (const UIDeterministicTick& tick : input.uiTicks) {
                (void)UIScrollSystem::Get().AdvanceTick(world, *n, tick, sink);
            }
            lastCompletedTickBySurface_[key] = input.uiTicks.back().tickIndex;
        }
    }

    // 9. N+1. 아무것도 바뀌지 않았다면 캐시가 같은 포인터를 돌려준다. 여기서 한 번만
    // 짓고 다시 짓지 않는다: 게시 뒤에 얻은 무효화 세대는 다음 프레임의 일이다.
    result.renderSnapshot =
        layout_.Build(world, input.windowId, input.logicalViewport,
                      *inputVisualStates_, textLayout, sink);
    if (result.renderSnapshot && g_publicationObserver) {
        const UIRuntimeGenerationSnapshot published = UIRuntimeInvalidationClock::Current();
        g_publicationObserver(world, *result.renderSnapshot);
        const UIRuntimeGenerationSnapshot after = UIRuntimeInvalidationClock::Current();
        if ((after.semanticDirtyGeneration != published.semanticDirtyGeneration ||
             after.scrollDisplacementGeneration != published.scrollDisplacementGeneration) &&
            reflowDeferredReported_.insert(world.Generation()).second) {
            ReportFrame(sink, TextDiagnosticCode::ReflowDeferred, TextSeverity::Warning,
                        "a UI mutation happened after this frame's layout was "
                        "published; it appears next frame",
                        "mutate UI state before ProcessFrame returns the snapshot");
        }
    }
    return result;
}

void UISystem::OnSurfacePointerDeparted(
    molga::WindowId windowId, std::uint64_t worldGeneration,
    molga::text::TextDiagnosticSink& textDiagnostics) {
    focusSystem_.ClearPointerState(windowId, worldGeneration, textDiagnostics);
}

std::uint64_t UISystem::LayoutGeometryBuildCountForTesting() const noexcept {
    return layout_.GeometryBuildCount();
}

std::uint64_t UISystem::LastCompletedNativeSequenceForTesting(
    molga::ui::UISurfaceKey key) const noexcept {
    const auto found = lastCompletedNativeSequenceBySurface_.find(key);
    return found == lastCompletedNativeSequenceBySurface_.end() ? 0 : found->second;
}

std::uint64_t UISystem::LastCompletedTickForTesting(
    molga::ui::UISurfaceKey key) const noexcept {
    const auto found = lastCompletedTickBySurface_.find(key);
    return found == lastCompletedTickBySurface_.end() ? 0 : found->second;
}
