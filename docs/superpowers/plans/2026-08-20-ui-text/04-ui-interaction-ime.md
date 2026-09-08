# UI Interaction and IME Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route pointer, keyboard, gamepad, scrolling, and text/IME input through one immutable interaction snapshot and one host-owned per-window SDL text-input arbiter, then provide grapheme-safe visible `UITextInput` editing and an internal semantic tree.

**Architecture:** `EngineHost` copies every SDL input event into one strictly ordered, take-once value batch; text payloads are deep-copied and stamped at ingest with the complete arbiter owner value `{kind,runtimeTarget,generation}`. `UISystem::ProcessFrame` is the sole event-vector owner: it makes one native-order projection pass over `orderedEvents` against immutable snapshot N, then one native-order dispatch pass over stored plans through one-event handlers. Each plan freezes separate action/focus/text/inner-to-outer-scroll targets, while its primary audit target follows one explicit event-kind rule; pointer/key policy projects through the batch, but text/edit stays bound to its immutable ingest owner. Runtime text editing is keyed by complete UI identity, consumes explicit arbiter transitions and deterministic UI ticks, publishes explicit selection/text/composition/caret plus blink-independent IME geometry into the one coalesced N+1 snapshot, and shares one Game View coordinate mapper and one host-owned arbiter with editor ImGui.

**Tech Stack:** C++17, CMake 3.27 presets, doctest, SDL 3, Dear ImGui SDL3 backend, ICU-backed Unicode buffers, HarfBuzz-backed immutable text layouts, SDL_GPU Metal.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md), especially Sections 7.4, 7.5, 8.2-8.4, 9, 10, 11, 13.2-13.4, and 14.

## Global Constraints

- The approved design is authoritative. Stop and obtain renewed approval before changing public behavior, failure policy, or milestone scope.
- Preserve unrelated worktree changes. Before each task run `git status --short --branch`; stage only that task's named files.
- This plan starts only after `03-ui-layout-rendering.md` passes its Exit Contract. It consumes semantic cached `UISnapshot` values with concrete render/hit records, full runtime identities, `UIPhysicalTransform`, fixed scroll state, and immutable text layouts.
- Snapshot N is the sole authority for target planning, hit order, visibility, parent clip, focus eligibility, and interactability. A callback may publish at most one render snapshot N+1; no event is re-hit-tested or retargeted against N+1.
- `UIHitTargetSnapshot`/`PlannedUIEvent` preserve their source-compatible action target and also freeze optional `focusTarget`, optional `textInputTarget`, and `scrollTargets` in inner-to-outer order as complete `UIFrozenTarget` values. `TargetFor(stage,ordinal)` is the only stage-target accessor.
- `UISystem::ProcessFrame` owns the one and only loop over `UIFrameInput::orderedEvents`. `UIInputRouter`, `UIFocusSystem`, `UIScrollSystem`, and `UITextInputSystem` expose one-event `HandleEvent` methods and never accept or walk an event vector.
- Whole-batch planning sequentially projects focus/capture/navigation changes in `UIPlanningState` using snapshot N only. Later pointer/key/gamepad events in the same native batch see those projected policy transitions, but pre-ingested text/edit events stay on their stamped runtime owner. No projected target observes callbacks, authored mutation, or N+1 geometry.
- Preserve strict native order across pointer, key, gamepad, scroll, editing, and commit events. Do not maintain a second text queue and do not implement a text-only drain.
- `UIFrameInput::windowId` names one UI surface. A foreign-window event stays in order and produces one targetless ignored audit record; it cannot read or mutate that surface's focus, capture, scroll, edit state, or owner transition. Focus/capture state is keyed by `{windowId,worldGeneration}`.
- Scroll events retain signed logical X/Y deltas. Gamepad-axis events retain exact signed axis values and named axes; no boolean reduction is allowed.
- Every target, focus, capture, scroll owner, IME owner, and edit state uses `{worldGeneration, objectId, componentRuntimeTypeId, componentInstanceId}` and re-resolves all fields before use.
- `EngineHost` owns exactly one `SdlTextInputPlatform` and one `TextInputArbiter`. Editor, Game View, standalone runtime, and tests receive references to that arbiter; none may construct a second production arbiter.
- `TextInputArbiter::SetOwner` and `SetArea` are idempotent. Repeating the same owner or area performs no generation change, boundary change, or SDL call.
- Every mutating arbiter API returns a `TextInputOwnerTransition` value containing exact retired/current tokens, its strictly increasing per-window `boundarySequence`, status, and `cancelEngineComposition`. A real applied/fail-closed state change places the boundary at or after every stamped/provided native sequence and strictly after the prior transition; `NoChange`/`Rejected` preserves it. `UISystem` immediately gives every transition to idempotent `UITextInputSystem::ApplyOwnerTransition`; no retired-owner history or second transition queue exists.
- `TextInputArbiter::SetOwner(TextInputOwnerRequest)` atomically enforces same-window priority: `EditorImGui` cannot replace/move/release active `RuntimeUITextInput`; a runtime request may replace editor only with `RuntimeStandaloneWindowFocused` or `RuntimeGameViewKeyboardFocused` authority validated by its caller. There is no separate `CanAcquire` precheck; windows remain independent.
- Text owner generations come from one process-global non-wrapping monotonic allocator shared by all arbiter instances/windows. Exhaustion never publishes zero or reuses a token; the requested transition fails closed with a blocker diagnostic.
- `TextInputArbiter` is the sole compiled caller of `SDL_StartTextInput`, `SDL_StopTextInput`, `SDL_ClearComposition`, and `SDL_SetTextInputArea` through `SdlTextInputPlatform.cpp`.
- Text/edit payloads are deep-copied during ingest and own an immutable `{kind,runtimeTarget,generation}` owner stamp. For every text event, `NativeInputEvent::sequence == NativeTextEvent::sequence`; the mapped UI event retains that sequence, owner stamp, and vector position. `RuntimeUITextInput` stamps map only to that full runtime identity; `EditorImGui`/`None` stamps have no UI target.
- `EngineHost` exposes each polled native batch exactly once. Native sequence numbers come from one process-global non-wrapping allocator, are nonzero, and strictly increase across hosts/windows; a second take returns an empty events-plus-surface-start batch, and `UISystem` rejects a replay/non-increasing native sequence before planning.
- The native observer keeps its pre-`Input` ordering for ordinary events, but receives `(raw SDL event, const stamped NativeInputEvent&)` only after metadata exists. Text/edit SDL events are never sent through the old unconditional pre-stamp callback path.
- Runtime text value, caret, selection, composition, blink, owner tokens, and focus state never serialize.
- `UISystem::ProcessFrame` receives the host `TextInputArbiter`, shared `TextLayoutService`, and diagnostic sink explicitly once runtime editing is installed. `UISystem::CollectRender` always receives a `TextDiagnosticSink`; no hidden service/sink/arbiter is constructed.
- Scroll inertia and caret blink consume the exact same ordered `UIDeterministicTick` values carried by `UIFrameInput::uiTicks`. Runtime, embedded Game View, and canonical replay do not sample their own UI time.
- Caret and edits operate on extended grapheme boundaries. Left/Right follow visual stops; insertion, deletion, selection storage, and max length use logical grapheme boundaries.
- Milestone A input policy is exactly `UITextInputContentPolicy::Any` and `UITextInputSubmitPolicy::OnEnter`: single-line Enter submits; multiline Enter without Control/GUI inserts one newline; multiline Control+Enter or GUI(Command)+Enter submits; public `Submit` submits either mode. Copied `NativeKeyModifiers` values, never live keyboard state, select the branch.
- SDL editing `start`/`length` are UTF-8 character counts, not bytes; convert them through `UnicodeTextBuffer` as specified by [`SDL_TextEditingEvent`](https://wiki.libsdl.org/SDL3/SDL_TextEditingEvent).
- SDL text input `cursorOffset` is a window-coordinate pixel offset relative to `rect.x`, as specified by [`SDL_SetTextInputArea`](https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea).
- `UITextInput` visual output is explicit immutable render data under the already-computed clip: committed selection rectangles appear only without active composition, text glyphs always reserve their complete span, composition underlines appear only with active composition, and caret rectangles are last.
- Valid rendered/placeholder label references are exclusively owned by their `UITextInput` snapshot binding. Ordinary `UILabel` output stays suppressed; text glyph command spans are reserved before following underline/caret indices. The value-only `UITextInputVisualStateProvider` is copied during `UILayoutSystem::Build` and never retained.
- Embedded Game View owns runtime IME only while the panel has keyboard focus and its focused `UITextInput` full identity resolves. Pointer departure clears hover/capture only; native window focus loss also releases pressed state, owner, and composition.
- `UIAccessibilityTree` is test/debug-only. Do not call macOS Accessibility APIs or claim native accessibility support.
- Each task uses red-green-refactor and ends with an independently reviewable commit.

## Prerequisite Contract

Verify these exact upstream shapes before Task 12.1:

```cpp
namespace molga::ui {
struct UIRuntimeTargetIdentity {
    std::uint64_t worldGeneration;
    unsigned int objectId;
    std::size_t componentRuntimeTypeId;
    std::uint64_t componentInstanceId;
};

struct UINavigationSnapshot {
    UINavigationMode mode;
    std::array<std::optional<UIRuntimeTargetIdentity>, 4> explicitTargets;
    std::array<std::optional<UIStableComponentKey>, 4> canonicalTargets;
};

struct UIFrozenTarget {
    UIRuntimeTargetIdentity runtimeTarget;
    UIStableComponentKey canonicalTarget;
};

struct UIHitTargetSnapshot {
    UIRuntimeTargetIdentity target;
    UIStableComponentKey canonicalTarget;
    std::optional<UIFrozenTarget> focusTarget;
    std::optional<UIFrozenTarget> textInputTarget;
    std::vector<UIFrozenTarget> scrollTargets;
    UIDrawOrderKey order;
    molga::FixedRect logicalRect;
    std::optional<molga::FixedRect> logicalClip;
    bool interactable;
    bool focusable;
    bool acceptsTextInput;
    UINavigationSnapshot navigation;
};

struct UISnapshot {
    molga::WindowId surfaceWindowId;
    std::uint64_t worldGeneration;
    molga::FixedSize logicalViewport;
    std::vector<UILayoutNodeSnapshot> nodes;
    std::vector<UIRenderItemSnapshot> renderItems;
    std::vector<UIHitTargetSnapshot> hitTargets;
    std::vector<UITextInputLabelSnapshot> textInputLabels;
    std::vector<UITextInputImeGeometrySnapshot> textInputImeGeometry;
};
using UISnapshotPtr = std::shared_ptr<const UISnapshot>;

struct UIDeterministicTick {
    std::uint64_t tickIndex;
    molga::Fixed26_6 deltaSeconds;
};

class UITextInputVisualStateProvider {
public:
    virtual ~UITextInputVisualStateProvider() = default;
    virtual std::optional<UITextInputVisualState> GetVisualState(
        molga::WindowId, const UIRuntimeTargetIdentity&) const = 0;
};
} // namespace molga::ui
```

Run the prerequisite gate without changing code:

```bash
cmake --preset debug
cmake --build --preset debug --target test_ui_layout test_ui_snapshot \
  test_ui_render_clip test_ui_scroll -j
ctest --test-dir build/debug -R '^(test_ui_layout|test_ui_snapshot|test_ui_render_clip|test_ui_scroll)$' --output-on-failure
```

Expected: all selected tests pass. If not, return to `03-ui-layout-rendering.md`.

## Exit Contract

This subplan is complete only when all of the following are true:

- Callback-time hide, reparent, remove/add, object-ID reuse, and scene replacement never retarget an event or transfer focus/capture/IME state.
- Action, sibling focus, sibling text input, and each ancestor scroll consumer remain distinct frozen targets; every stage result is validated against its own target while the audit record keeps the event's one primary target.
- `UISystem::ProcessFrame` is the sole `orderedEvents` loop; every subsystem processes exactly one `PlannedUIEvent` per call.
- Pointer, key, gamepad, scroll X/Y, gamepad axis, edit, and commit actions share one audited native order.
- Foreign-window entries remain in that audit order but are targetless/ignored, and detached UI surfaces retain independent focus/capture/owner state.
- Each window has at most one idempotent text owner token, detached windows remain independent, and an already-ingested old-owner text event can affect only its stamped full runtime identity, once, never a new owner.
- Source/binary checks show SDL text lifecycle APIs only in `SdlTextInputPlatform.cpp` among compiled production sources.
- The editor-owned ImGui bridge cannot stop or reposition an active runtime owner in the same or another window; an authority-checked runtime request may replace an editor owner only through one arbiter call.
- Grapheme editing, BiDi visual caret motion, composition ranges, exact single-line/multiline Enter and Control/Command modifier consumption/callback behavior, and serialization omission pass focused tests.
- Pointer caret placement/captured drag, BiDi midpoint ties, clipped hit behavior, public `GetValue`/`SetValue`/callback APIs, deterministic tick replay, and owner-transition composition cancellation pass focused tests.
- Every visible input snapshot contains the applicable conditional selection/text/underline/caret layers with one shared clip; Game View input-area mapping passes scale, letterbox, crop, and detached-origin cases.
- Text reserves its full glyph/tofu command span before underline/caret, and focused input IME geometry remains immutable/present while caret blink is off.
- All pre-publication dirty requests and ticks coalesce into the sole N+1; only a post-publication generation reports `ReflowDeferred`.
- Native window focus loss releases owner/composition; panel pointer departure does not clear keyboard/gamepad focus.
- Internal semantic tree output is stable and no native accessibility bridge is claimed.
- Focused Debug plus interaction ASan/UBSan gates below pass from fresh command output.

## File Responsibility Map

| Responsibility | Authoritative files |
|---|---|
| UI event values and frozen target plan | `src/UI/UIInputEvent.h`, `src/UI/UIInputRouter.*` |
| Focus/navigation/capture | `src/UI/UIFocusSystem.*` |
| One-loop frame orchestration | `src/UI/UISystem.*`, production Game View/runtime call sites |
| Native copied events and text ownership | `src/Platform/NativeKeyModifiers.h`, `TextInputOwner.h`, `NativeInputEvent.h`, `NativeTextEvent.h`, `TextInputArbiter.*` |
| Stamped observer/take-once host batch | `src/Core/Bootstrap.*`, `src/Systems/Input.*` |
| Sole SDL lifecycle adapter | `src/Platform/SdlTextInputPlatform.*` |
| Editor IME ownership bridge | `src/Editor/ThirdParty/imgui_impl_sdl3_molga.cpp`, `src/Editor/ImGuiTextInputBridge.*` |
| Grapheme edit/runtime visuals | `src/UI/UITextInputSystem.*`, `src/UI/UILayoutSystem.cpp` |
| Deterministic frame ticks | `src/UI/UIDeterministicTick.h`, `UIFrameInput::uiTicks`, runtime/Game View/canonical replay call sites |
| Game View input-area mapping | `src/Editor/GameViewTextInputMapping.*`, `GameViewWindow.*` |
| Internal semantics | `src/UI/UIAccessibilityTree.*` |

---

### Task 12.1: Define ordered UI events and freeze target plans against snapshot N

**Files:**

- Create: `src/Platform/TextInputOwner.h`
- Create: `src/Platform/NativeKeyModifiers.h`
- Create: `src/UI/UIInputEvent.h`
- Create: `src/UI/UIInputRouter.h`
- Create: `src/UI/UIInputRouter.cpp`
- Create: `tests/test_ui_input.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: immutable `UISnapshot`, complete runtime identities, stable component keys, fixed geometry, and exact reverse draw order.
- Produces: C++17-comparable `NativeKeyModifiers`, `UIInputEvent`, surface-owned `UIPlanningState`, source-compatible multi-stage `PlannedUIEvent::{TargetFor,ScrollTargetCount}`, stage-validating `UIEventHandlerResult`, primary-target `UIEventDispatchAccumulator`/`UIEventDispatchRecord`, tick-bearing `UIFrameInput`, `UIFrameResult`, `UIInputRouter::{PlanNext,HandleEvent}`, and no subsystem vector-processing API.

- [ ] **Step 1a: Write a failing frozen-target test.**

  ```cpp
  TEST_CASE("planned target never changes after snapshot N") {
      UIFrozenInputFixture f;
      const auto snapshotN = f.BuildSnapshot();
      const auto high = f.HighButtonIdentity();
      const molga::ui::UIInputEvent down = f.PointerDownAtOverlap(10);
      auto planning = f.InitialPlanningState();
      const auto planned = f.Router().PlanNext(
          *snapshotN, f.SurfaceWindowId(), down, planning);
      REQUIRE(planned.targetFromSnapshotN);
      CHECK(*planned.targetFromSnapshotN == high);

      f.HideHighAndPublishNewGeometry();
      molga::ui::UIEventDispatchAccumulator accumulator(planned);
      accumulator.Merge(f.Router().HandleEvent(
          f.World(), *snapshotN, planned, f.Diagnostics()));
      const auto dispatch = std::move(accumulator).Finish();
      CHECK(dispatch.runtimeTarget == high);
      CHECK_FALSE(dispatch.delivered); // hidden/replaced target is stale
      CHECK(f.LowButtonClicks() == 0); // no retarget to lower button
  }
  ```

- [ ] **Step 1b: Add the frozen-target lifecycle/edge table.**

  Add reparent-with-same-identity (delivered once), remove/add same type (skipped), replacement world with reused object ID (skipped), scene transition (skipped), exact reverse draw order, parent clip, and boundary exclusion.

- [ ] **Step 1c: Write a failing projected pointer-capture batch test.**

  ```cpp
  TEST_CASE("batch planning projects pointer capture using snapshot N") {
      UIFrozenInputFixture f;
      const auto n = f.BuildSnapshot();
      const std::vector<UIInputEvent> events{
          f.PointerDown(20, {10, 10}),
          f.PointerMove(21, {700, 500}),
          f.PointerUp(22, {700, 500})};
      const auto plans = f.ProjectAll(*n, events);
      CHECK(plans[1].targetFromSnapshotN == plans[0].targetFromSnapshotN);
      CHECK(plans[2].targetFromSnapshotN == plans[0].targetFromSnapshotN);
  }
  ```

- [ ] **Step 1d: Write a failing projected-focus versus ingest-owner test.**

  ```cpp
  TEST_CASE("key follows projected focus but text stays on its ingest owner") {
      UIFrozenInputFixture f;
      const auto n = f.BuildSnapshotWithTwoTextInputs();
      const auto oldInput = f.FirstTextInputIdentity();
      const auto newInput = f.SecondTextInputIdentity();
      const std::vector<UIInputEvent> events{
          f.PointerDownOn(newInput, 23),
          f.KeyDown(24, Key::A),
          f.TextCommit(25, "old", f.RuntimeStamp(oldInput, 7)),
          f.TextCommit(26, "editor", f.EditorStamp(9))};
      const auto plans = f.ProjectAll(*n, events);
      CHECK(plans[1].targetFromSnapshotN == newInput);
      CHECK(plans[2].targetFromSnapshotN == oldInput);
      CHECK_FALSE(plans[3].targetFromSnapshotN);
  }
  ```

  This fixture models one SDL batch ingested while the old runtime owner was active. The pointer-down projects a new focus for later key policy, but it cannot rewrite the immutable owner stamp on text already copied by the host. An editor-stamped event remains available to the ImGui observer only and has no runtime UI target.

- [ ] **Step 1e: Add callback-mutation assertions to both projection cases.**

  Preplan the full batch, then make the pointer callback hide/reparent the new focus and transition owner. Require the key plan to retain the projected N identity, the text plan to retain the old stamped identity, and both to rely on dispatch-time full-identity resolution rather than N+1 retargeting.

- [ ] **Step 1f: Write failing per-stage target and primary-audit tests.**

  ```cpp
  TEST_CASE("handlers resolve only their frozen stage target") {
      UIFrozenInputFixture f;
      const auto plan = f.PointerPlanWithButtonSelectableInputAndTwoScrolls();
      CHECK(plan.TargetFor(UIEventStage::Input) == f.ButtonTarget());
      CHECK(plan.TargetFor(UIEventStage::Focus) == f.SelectableTarget());
      CHECK(plan.TargetFor(UIEventStage::TextInput) == f.TextInputTarget());
      CHECK(plan.TargetFor(UIEventStage::Scroll, 0) == f.InnerScrollTarget());
      CHECK(plan.TargetFor(UIEventStage::Scroll, 1) == f.OuterScrollTarget());
      CHECK_FALSE(plan.TargetFor(UIEventStage::Scroll, 2));
  }
  ```

  Add accumulator rows proving: a runtime-stamped text target is primary; the first consumed inner-to-outer scroll target is primary; pointer/key action target is primary; window-focus and foreign-window events have no primary; a focus-stage result never replaces primary. A handler result naming another stage's valid target must be rejected with `ReferenceInvalid` just like an unknown target.

- [ ] **Step 1g: Write a failing cross-window surface-isolation test.**

  Build surface window 10 with focus/capture and pass ordered events from windows `{10,20,10}`. Require three audit records in the same sequence; the window-20 record is `surfaceEligible=false`, targetless, unconsumed, and cannot clear/project/dispatch window-10 state. Run the same world in detached window 20 and require an independent `{windowId,worldGeneration}` focus/capture state. A foreign `WindowFocus(active=false)` must not release the local surface.

- [ ] **Step 1h: Write a failing unstamped text target rule.**

  ```cpp
  TEST_CASE("unstamped text is targetless and never follows projected focus") {
      UIFrozenInputFixture f;
      auto state = f.FocusedPlanningState();
      const auto plan = f.Router().PlanNext(
          *f.Snapshot(), f.SurfaceWindowId(), f.UnstampedCommit(27, "x"), state);
      CHECK_FALSE(plan.targetFromSnapshotN);
      CHECK_FALSE(plan.textInputTargetFromSnapshotN);
  }
  ```

  Repeat with a `None` stamp. Only an explicit valid `RuntimeUITextInput` ingest stamp may populate a text plan; tests and canonical replay helpers may not synthesize projected-focus text.

- [ ] **Step 2: Write failing event-value tests for scroll, gamepad axes, and key modifiers.**

  ```cpp
  TEST_CASE("UI events preserve signed scroll and axis values") {
      molga::ui::UIInputEvent scroll;
      scroll.kind = molga::ui::UIInputEventKind::Scroll;
      scroll.logicalDelta = {molga::Fixed26_6::FromRaw(-17),
                             molga::Fixed26_6::FromRaw(29)};
      molga::ui::UIInputEvent axis;
      axis.kind = molga::ui::UIInputEventKind::GamepadAxis;
      axis.axisValue = molga::Fixed26_6::FromRaw(-31);
      CHECK(scroll.logicalDelta.x.Raw() == -17);
      CHECK(scroll.logicalDelta.y.Raw() == 29);
      CHECK(axis.axisValue.Raw() == -31);
      const molga::platform::NativeKeyModifiers command{
          false, false, false, true};
      CHECK(command == molga::platform::NativeKeyModifiers{
                           false, false, false, true});
      CHECK(command != molga::platform::NativeKeyModifiers{});
  }
  ```

  Add a one-field-difference table for all four modifier booleans under C++17; no defaulted C++20 comparison is permitted.

- [ ] **Step 3: Run the UI-event red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_input -j
  ```

  Expected: compilation fails because ordered event and planner contracts do not exist.

- [ ] **Step 4a: Define the shared value-only owner stamp.**

  ```cpp
  namespace molga::platform {
  enum class TextInputOwnerKind : std::uint8_t {
      None, RuntimeUITextInput, EditorImGui
  };

  struct TextInputOwner {
      TextInputOwnerKind kind = TextInputOwnerKind::None;
      std::optional<molga::ui::UIRuntimeTargetIdentity> runtimeTarget;
      bool operator==(const TextInputOwner&) const noexcept;
      bool operator!=(const TextInputOwner& other) const noexcept {
          return !(*this == other);
      }
  };

  struct TextInputOwnerStamp {
      TextInputOwnerKind kind = TextInputOwnerKind::None;
      std::optional<molga::ui::UIRuntimeTargetIdentity> runtimeTarget;
      std::uint64_t generation = 0;
      explicit operator bool() const noexcept {
          return kind != TextInputOwnerKind::None && generation != 0;
      }
      bool operator==(const TextInputOwnerStamp&) const noexcept;
      bool operator!=(const TextInputOwnerStamp& other) const noexcept {
          return !(*this == other);
      }
  };
  } // namespace molga::platform
  ```

  Equality compares all fields. `RuntimeUITextInput` is valid only with a full runtime target; `EditorImGui` is valid only without one. The stamp is a value snapshot, not a pointer into arbiter state and not a generation lookup key.

- [ ] **Step 4b: Define value-only UI input records.**

  ```cpp
  namespace molga::ui {
  enum class UIInputEventKind : std::uint8_t {
      Other, WindowFocus, PointerMotion, PointerButton, Scroll, Key,
      GamepadButton, GamepadAxis, TextEditing, TextCommit
  };

  struct UITextInputEventPayload {
      std::string utf8;
      std::int32_t editingStartUtf8Characters = 0;
      std::int32_t editingLengthUtf8Characters = 0;
      molga::platform::TextInputOwnerStamp ownerAtIngest;
  };

  struct UIInputEvent {
      std::uint64_t sequence = 0;
      std::uint64_t traceOrdinal = 0;
      molga::WindowId windowId = 0;
      UIInputEventKind kind = UIInputEventKind::Other;
      std::uint32_t nativeType = 0;
      molga::FixedPoint logicalPoint;
      bool logicalPointValid = false;
      molga::FixedPoint logicalDelta;
      molga::Fixed26_6 axisValue = molga::Fixed26_6::FromRaw(0);
      std::uint32_t control = 0;
      molga::platform::NativeKeyModifiers keyModifiers;
      bool active = false;
      std::optional<UITextInputEventPayload> text;
  };
  } // namespace molga::ui
  ```

  Define the included value in `src/Platform/NativeKeyModifiers.h` without SDL dependencies:

  ```cpp
  namespace molga::platform {
  struct NativeKeyModifiers {
      bool shift = false;
      bool control = false;
      bool alt = false;
      bool gui = false; // SDL KMOD_GUI; Command on macOS.
      bool operator==(const NativeKeyModifiers&) const noexcept;
      bool operator!=(const NativeKeyModifiers& other) const noexcept {
          return !(*this == other);
      }
  };
  } // namespace molga::platform
  ```

  Equality compares all four booleans. The all-false default applies to injected/legacy descriptors. The Milestone 18 qualification trace contains no Enter key event, so it retains identical JSON and behavior without adding a qualification schema key; focused Milestone 14 tests own the modifier-bearing rows.

- [ ] **Step 4c: Define projected planning state and plans.**

  ```cpp
  namespace molga::ui {

  enum class UIEventStage : std::uint8_t {
      Input, Focus, Scroll, TextInput, OwnerTransition
  };

  struct PlannedUIEvent {
      UIInputEvent event;
      bool surfaceEligible = false;
      // Source-compatible action target fields.
      std::optional<UIRuntimeTargetIdentity> targetFromSnapshotN;
      std::optional<UIStableComponentKey> canonicalTargetFromSnapshotN;
      std::optional<UIFrozenTarget> focusTargetFromSnapshotN;
      std::optional<UIFrozenTarget> focusDestinationFromSnapshotN;
      std::optional<UIFrozenTarget> textInputTargetFromSnapshotN;
      std::vector<UIFrozenTarget> scrollTargetsFromSnapshotN;
      std::optional<UIFrozenTarget> ownerTransitionTargetFromSnapshotN;

      std::optional<UIFrozenTarget> TargetFor(
          UIEventStage, std::size_t ordinal = 0) const;
      std::size_t ScrollTargetCount() const noexcept {
          return scrollTargetsFromSnapshotN.size();
      }
  };

  struct UIPlanningState {
      molga::WindowId surfaceWindowId = 0;
      std::uint64_t surfaceWorldGeneration = 0;
      std::optional<UIRuntimeTargetIdentity> focused;
      std::optional<UIRuntimeTargetIdentity> pointerCapture;
      std::optional<UIRuntimeTargetIdentity> hovered;
      molga::FixedPoint pointer;
      bool pointerValid = false;
      bool nativeWindowFocused = false;
  };
  } // namespace molga::ui
  ```

- [ ] **Step 4c2: Implement the sole stage-target accessor.**

  ```cpp
  std::optional<UIFrozenTarget> PlannedUIEvent::TargetFor(
      UIEventStage stage, std::size_t ordinal) const {
      if (!surfaceEligible) return std::nullopt;
      switch (stage) {
      case UIEventStage::Input:
          if (!targetFromSnapshotN || !canonicalTargetFromSnapshotN)
              return std::nullopt;
          return UIFrozenTarget{*targetFromSnapshotN,
                                *canonicalTargetFromSnapshotN};
      case UIEventStage::Focus:
          return focusDestinationFromSnapshotN
              ? focusDestinationFromSnapshotN
              : focusTargetFromSnapshotN;
      case UIEventStage::TextInput:
          return textInputTargetFromSnapshotN;
      case UIEventStage::Scroll:
          return ordinal < scrollTargetsFromSnapshotN.size()
              ? std::optional<UIFrozenTarget>{scrollTargetsFromSnapshotN[ordinal]}
              : std::nullopt;
      case UIEventStage::OwnerTransition:
          return ownerTransitionTargetFromSnapshotN;
      }
      return std::nullopt;
  }
  ```

  No handler directly reads a sibling target field; tests compile-fail private/internal access where practical and exercise this accessor for every stage.

- [ ] **Step 4d: Define one-event handler result values.**

  ```cpp
  namespace molga::ui {

  enum class UIEventAction : std::uint16_t {
      None = 0, Hover = 1u << 0, Press = 1u << 1, Click = 1u << 2,
      Focus = 1u << 3, Navigate = 1u << 4, Scroll = 1u << 5,
      Edit = 1u << 6, Submit = 1u << 7, Release = 1u << 8
  };
  struct UIEventHandlerResult {
      std::uint64_t sequence = 0;
      UIEventStage stage = UIEventStage::Input;
      std::uint16_t actionMask = 0;
      bool consumed = false;
      bool callbackDelivered = false;
      bool arrangementDirty = false;
      bool visualDirty = false;
      std::size_t stageTargetOrdinal = 0;
      std::optional<UIFrozenTarget> resolvedStageTarget;
      std::optional<UIRuntimeTargetIdentity> resultingFocus;
  };
  } // namespace molga::ui
  ```

- [ ] **Step 4e: Define the aggregate dispatch record and accumulator.**

  ```cpp
  namespace molga::ui {
  struct UIEventDispatchRecord {
      std::uint64_t sequence = 0;
      std::uint64_t traceOrdinal = 0;
      UIInputEventKind kind = UIInputEventKind::Other;
      std::optional<UIRuntimeTargetIdentity> runtimeTarget;
      std::optional<UIStableComponentKey> canonicalTarget;
      std::uint16_t actionMask = 0;
      bool consumed = false;
      bool delivered = false;
      bool arrangementDirty = false;
      bool visualDirty = false;
      std::optional<UIRuntimeTargetIdentity> resultingFocus;
  };

  class UIEventDispatchAccumulator {
  public:
      explicit UIEventDispatchAccumulator(const PlannedUIEvent&);
      void Merge(const UIEventHandlerResult&);
      UIEventDispatchRecord Finish() &&;
  };
  } // namespace molga::ui
  ```

- [ ] **Step 4f: Define frame audit input and result values.**

  ```cpp
  namespace molga::ui {

  struct UIFrameInput {
      std::uint64_t frameIndex = 0;
      molga::WindowId windowId = 0;
      molga::FixedSize logicalViewport;
      std::vector<UIInputEvent> orderedEvents;
      std::vector<UIDeterministicTick> uiTicks;
      molga::FixedPoint pointerAtBatchStart;
      bool pointerAtBatchStartValid = false;
      bool nativeWindowFocusedAtBatchStart = false;
  };

  struct UIFrameResult {
      std::uint64_t frameIndex = 0;
      UISnapshotPtr interactionSnapshot;
      UISnapshotPtr renderSnapshot;
      std::vector<UIEventDispatchRecord> dispatches;
  };
  } // namespace molga::ui
  ```

  `frameIndex` exists only in frame audit values, never in `UISnapshot` or its
  cache key. The pointer value/validity and native-focus `AtBatchStart` fields
  are mapped from the same take-once host batch seed before any event in
  `orderedEvents`; no caller substitutes post-poll/current input state.

- [ ] **Step 5a: Implement pure hit selection against snapshot N.**

  Read `hitTargets` in exact reverse `UIDrawOrderKey`, test the stored rect and stored clip, and copy source-compatible action fields plus `focusTarget`, `textInputTarget`, and inner-to-outer `scrollTargets` into the plan. Key/gamepad lookup finds the snapshot hit route whose frozen focus target equals projected focus; captured pointer events find the route whose action target equals projected capture. Never store a component pointer.

- [ ] **Step 5b: Implement sequential `UIPlanningState` projection.**

  ```cpp
  PlannedUIEvent UIInputRouter::PlanNext(const UISnapshot& n,
                                         molga::WindowId surfaceWindowId,
                                         const UIInputEvent& event,
                                         UIPlanningState& projected) const;
  ```

  First compare `event.windowId` with `surfaceWindowId`. A mismatch returns `surfaceEligible=false` with every stage target empty and changes no projection state. For matching `PointerMotion`/`PointerButton`, `logicalPointValid=false` produces a targetless but surface-eligible pointer-departure plan, clears projected hover/capture/pointer validity, and never clears keyboard/gamepad focus; no zero/default point is hit-tested. A valid pointer event first replaces `projected.pointer` and marks it valid. Pointer down then selects N's hit route and projects action capture plus its separate focus target; pointer move/up copies every stage target from the captured N route until up releases it. A `Scroll` plan uses only the current projected pointer (batch-start seed or preceding valid pointer event) to freeze the hit route and inner-to-outer scroll targets; if `pointerValid=false` it stays targetless without treating delta or `(0,0)` as a position. Key/gamepad/navigation events find the route for projected focus. `TextEditing`/`TextCommit` never use `projected.focus`: a valid `RuntimeUITextInput` stamp populates both the source-compatible primary fields and `textInputTargetFromSnapshotN`; `EditorImGui`/`None`/missing stamps produce no UI target. Matching-window focus loss clears projected focus/capture. Task 12.2's mutable `UIFocusSystem::ProjectEvent` resolves a navigation destination only from N, stores it in `focusDestinationFromSnapshotN`, makes `TargetFor(Focus)` return that destination (falling back to `focusTargetFromSnapshotN` for non-navigation focus), and advances `projected.focus` to the identical value. Neither planning stage invokes a callback or reads callback/N+1 state.

- [ ] **Step 6a: Implement one-event dispatch with final identity resolution.**

  ```cpp
  class UIInputRouter {
  public:
      PlannedUIEvent PlanNext(const UISnapshot&, molga::WindowId,
                              const UIInputEvent&,
                              UIPlanningState&) const;
      UIEventHandlerResult HandleEvent(
          World&, const UISnapshot&, const PlannedUIEvent&,
          molga::text::TextDiagnosticSink&);
  };
  ```

  `HandleEvent` asks `planned.TargetFor(Input)` and immediately resolves all four fields of that stage target. Dispatch at most once; stale plans return `callbackDelivered=false` and the same `resolvedStageTarget` for validation. It never validates against the focus/text/scroll sibling by accident. Reparenting does not invalidate a live identity, but its geometry is not reconsidered.

- [ ] **Step 6b: Implement dispatch accumulation.**

  Seed sequence/trace/kind and choose primary by exact rule: stamped text target; action target for pointer/key/gamepad; no target for window-focus/foreign events; scroll remains empty until a scroll handler reports the first consumed target. `Merge` rejects a different sequence, obtains the expected target from `TargetFor(result.stage,result.stageTargetOrdinal)`, requires field-for-field equality with `resolvedStageTarget`, ORs action/dirty/consumed/delivered fields, and never substitutes a focus side effect. A consumed scroll result may set the still-empty primary once to its validated consumer. `Finish` returns one immutable record and marks the accumulator spent.

- [ ] **Step 6c: Run the focused accumulator assertions.**

  ```bash
  cmake --build --preset debug --target test_ui_input -j
  build/debug/tests/test_ui_input --test-case="dispatch accumulator*"
  ```

  Expected: merge-order permutations produce the same flags/target; sequence/target conflicts report `ReferenceInvalid` and never substitute a target.

- [ ] **Step 7: Prohibit subsystem vector overloads at compile time.**

  Add a test-only detection trait that fails if `UIInputRouter`, `UIFocusSystem`, `UIScrollSystem`, or `UITextInputSystem` exposes `HandleEvents(vector<...>)` or `Process(vector<...>)`. Keep only `PlanNext` and one-event `HandleEvent` public.

- [ ] **Step 8: Run the planner green gate.**

  ```bash
  cmake --build --preset debug --target test_ui_input -j
  ctest --test-dir build/debug -R '^test_ui_input$' --output-on-failure
  ```

  Expected: all frozen-target, order, clip, delta, and no-vector assertions pass.

- [ ] **Step 9: Commit ordered event planning.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Platform/TextInputOwner.h \
    src/Platform/NativeKeyModifiers.h src/UI/UIInputEvent.h \
    src/UI/UIInputRouter.* tests/test_ui_input.cpp
  git commit -m "feat: freeze UI event targets against snapshot N"
  ```

### Task 12.2: Add full-identity focus, capture, navigation, and one-event scrolling

**Files:**

- Create: `src/UI/UIFocusSystem.h`
- Create: `src/UI/UIFocusSystem.cpp`
- Create: `tests/test_ui_focus.cpp`
- Modify: `src/UI/UIInputRouter.h`
- Modify: `src/UI/UIInputRouter.cpp`
- Modify: `src/UI/UIScrollSystem.h`
- Modify: `src/UI/UIScrollSystem.cpp`
- Modify: `src/ECS/Components/UIButton.h`
- Modify: `src/ECS/Components/UIButton.cpp`
- Modify: `tests/test_ui_input.cpp`
- Modify: `tests/test_ui_scroll.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `PlannedUIEvent`, snapshot hit records, `UISelectable`, scroll runtime APIs, and full identity resolution.
- Produces: `UIFocusSystem::HandleEvent`, `UIScrollSystem::HandleEvent`, `{windowId,worldGeneration}`-owned hover/pressed/capture/focus tables, stage-target validation, inner-to-outer scroll consumption, and deterministic navigation.

- [ ] **Step 1a: Write the failing component-replacement focus test.**

  ```cpp
  TEST_CASE("component replacement cannot inherit focus") {
      UIFocusFixture f;
      const auto old = f.FocusButton();
      f.ReplaceButtonComponent();
      f.Focus().Revalidate(f.World(), *f.Snapshot());
      CHECK_FALSE(f.Focus().Focused(f.WindowId(), f.World().Generation()));
      CHECK(f.NewButtonIdentity() != old);
  }
  ```

- [ ] **Step 1b: Write the failing pointer-departure/window-loss distinction.**

  ```cpp
  TEST_CASE("pointer departure differs from native focus loss") {
      UIFocusFixture f;
      const auto focused = f.FocusButton();
      f.Focus().ClearPointerState(f.WindowId(), f.World().Generation());
      CHECK(f.Focus().Focused(f.WindowId(), f.World().Generation()) == focused);
      f.Focus().OnNativeWindowFocusLost(f.WindowId());
      CHECK_FALSE(f.Focus().Focused(f.WindowId(), f.World().Generation()));
      CHECK_FALSE(f.Focus().CapturedPointer(
          f.WindowId(), f.World().Generation()));
  }
  ```

- [ ] **Step 1c: Write the failing focus/capture lifecycle matrix.**

  Cover pointer focus, keyboard/gamepad focus, pointer capture, disabled/inactive cleanup, world release, and native focus loss. Each case supplies the starting full identities, one event/mutation, and exact focused/captured identities afterward.

- [ ] **Step 1d: Write failing explicit-navigation tests.**

  Test all four explicit refs plus invalid/missing explicit refs; every row checks the frozen runtime and canonical targets copied into snapshot N.

- [ ] **Step 1e: Write failing automatic-navigation ranking tests.**

  Test half-plane eligibility, primary/secondary distance, stable sibling path, stable object-ID tie break, and no eligible target with exact chosen identities.

- [ ] **Step 1f: Write a failing same-batch navigation projection test.**

  ```cpp
  TEST_CASE("navigation projects key focus but stamped text keeps its owner") {
      UIFocusFixture f;
      const auto n = f.SnapshotWithThreeFocusCandidates();
      UIPlanningState state;
      state.focused = f.LeftIdentity();
      const auto nav = f.PlanAndProject(*n, f.GamepadRight(30), state);
      CHECK(nav.targetFromSnapshotN == f.LeftIdentity());
      CHECK(nav.focusDestinationFromSnapshotN->runtimeTarget ==
            f.RightIdentity());
      CHECK(state.focused == f.RightIdentity());
      const auto navResult = f.focus.HandleEvent(f.world, *n, nav, f.sink);
      CHECK(navResult.resultingFocus == f.RightIdentity());
      CHECK(f.focus.Focused(f.Window(), f.WorldGeneration()) == f.RightIdentity());
      const auto key = f.PlanAndProject(*n, f.KeyDown(31, Key::A), state);
      const auto text = f.PlanAndProject(
          *n, f.TextCommit(32, "a", f.RuntimeStamp(f.LeftIdentity(), 7)), state);
      CHECK(key.targetFromSnapshotN == f.RightIdentity());
      CHECK(text.targetFromSnapshotN == f.LeftIdentity());
  }
  ```

  Add a second `None`-stamped commit and require no target. An unstamped/`None` text event is never treated as a key event and never receives projected focus.

- [ ] **Step 2: Write failing ordered scroll-handler tests.**

  Feed `Scroll(-7,+11)`, pointer drag `(+3,-5)`, then gamepad vertical axis `-13` as three `PlannedUIEvent` values. Call `HandleEvent` once per plan and assert the scroll audit/state preserves that exact sequence and signed fields. Disabled horizontal axis must ignore X without rewriting Y.

- [ ] **Step 3: Run the focus/scroll red gate.**

  ```bash
  cmake --build --preset debug --target test_ui_focus test_ui_input test_ui_scroll -j
  ```

  Expected: compilation fails on `UIFocusSystem` and one-event scroll handlers.

- [ ] **Step 4: Define focus and capture APIs around full identities.**

  ```cpp
  class UIFocusSystem {
  public:
      void ProjectEvent(const UISnapshot&, PlannedUIEvent&,
                        UIPlanningState&) const;
      UIEventHandlerResult HandleEvent(
          World&, const UISnapshot&, const PlannedUIEvent&,
          molga::text::TextDiagnosticSink&);
      void Revalidate(World&, const UISnapshot&);
      std::optional<UIRuntimeTargetIdentity> Focused(
          molga::WindowId, std::uint64_t worldGeneration) const;
      std::optional<UIRuntimeTargetIdentity> CapturedPointer(
          molga::WindowId, std::uint64_t worldGeneration) const;
      void ClearPointerState(molga::WindowId,
                             std::uint64_t worldGeneration);
      void OnNativeWindowFocusLost(molga::WindowId);
      void OnWorldReleased(std::uint64_t worldGeneration);
  };
  ```

  Tables are keyed first by `{windowId,worldGeneration}` and hold complete target identities only. A window event touches only its exact surface key. Revalidate at frame start and after each callback that can mutate hierarchy/components; `OnWorldReleased` removes every window entry for that generation.

- [ ] **Step 5a: Implement explicit navigation resolution.**

  Resolve `SceneObjectRef` in the current world and verify the resulting active/interactable `UISelectable` full identity. An invalid ref leaves focus unchanged and reports `ReferenceInvalid` once.

- [ ] **Step 5b: Implement automatic navigation ranking.**

  Rank eligible snapshot-N candidates by requested half-plane, primary-axis distance, secondary-axis distance, sibling path, stable object ID, then component instance ID. Never read current component geometry after N is frozen.

- [ ] **Step 5c: Project navigation into later same-batch plans.**

  `UIFocusSystem::ProjectEvent` reads only `UIHitTargetSnapshot::navigation` and snapshot rect/order. The navigation event itself retains the pre-transition focused target; after planning it writes the chosen identity into `UIPlanningState::focused`, so subsequent key/gamepad events target it. Text/edit remains bound to `ownerAtIngest`. It invokes no callback and does not mutate runtime focus.

- [ ] **Step 5c2: Resolve the frozen focus stage only.**

  `UIFocusSystem::HandleEvent` uses `planned.TargetFor(Focus)`, returns it as `resolvedStageTarget`, and never treats the action component or `textInputTarget` as the selectable. Foreign-window/window-focus plans with no focus target may clear only the matching surface state and return no stage target.

- [ ] **Step 5d: Publish real focus/capture visual invalidation.**

  When `HandleEvent` actually changes focused/captured/hovered state visible in the next snapshot, acquire `UIRuntimeInvalidationClock::Advance(SemanticDirty)` before publishing the table mutation. Exhaustion leaves prior runtime state/snapshot visible, reports a blocker, and never lets `UILayoutSystem`'s fast path return a stale focus visual. Pure projection does not advance the clock.

- [ ] **Step 6: Move UIButton runtime state into the router.**

  Store hover/pressed/clicked-by-frame in identity-keyed runtime tables. Before publishing a changed visible state, acquire `Advance(SemanticDirty)` exactly once; a no-op event does not advance. Keep source-compatible `UIButton::{IsHovered,IsPressed,WasClickedThisFrame}` getters backed by the active router facade, but serialize only authored colors, sorting, interactability, and callback metadata.

- [ ] **Step 7: Add one-event scroll handling.**

  ```cpp
  UIEventHandlerResult UIScrollSystem::HandleEvent(
      World& world, const UISnapshot& snapshot,
      const PlannedUIEvent& planned,
      molga::text::TextDiagnosticSink& diagnostics);
  ```

  Map `logicalDelta.x/y` directly for wheel/drag and map `axisValue` plus the named control axis for gamepad. Walk only `planned.scrollTargetsFromSnapshotN` from inner to outer, re-resolve each full identity, and stop at the first enabled target whose `ApplyInput` reports consumption; return that target/ordinal as the Scroll-stage result. An inner target at its boundary may decline so the exact delta reaches its outer ancestor once. Handle one plan only, project `UIScrollMutation` into `UIEventHandlerResult::{actionMask,arrangementDirty,visualDirty}`, and never iterate or retain `orderedEvents`.

- [ ] **Step 8: Run focus, input, scroll, and serialization green gates.**

  ```bash
  cmake --build --preset debug --target test_ui_focus test_ui_input \
    test_ui_scroll test_scene_serializer -j
  ctest --test-dir build/debug -R '^(test_ui_focus|test_ui_input|test_ui_scroll|test_scene_serializer)$' --output-on-failure
  ```

  Expected: all selected tests pass with complete-identity cleanup and exact signed input values.

- [ ] **Step 9: Commit focus and one-event interaction state.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UIFocusSystem.* \
    src/UI/UIInputRouter.* src/UI/UIScrollSystem.* \
    src/ECS/Components/UIButton.* tests/test_ui_focus.cpp \
    tests/test_ui_input.cpp tests/test_ui_scroll.cpp
  git commit -m "feat: add identity-safe UI focus and navigation"
  ```

### Task 12.3: Make `UISystem::ProcessFrame` the sole ordered-vector loop

**Inherited obligation (its original owner has already landed without closing it):**

- **`DecodeUtf8` is ~4x slower on well-formed text and ~70x on ill-formed text, on a per-frame
  render path.** Subplan 01 Step 10 routed it through `UnicodeTextBuffer` (in-spec), and the cost
  lands on rendering. It was assigned to **Task 8.2** with an explicit escape clause: "if Task 8.2
  lands as planned the regression disappears with the path. If that task slips, **or the path
  survives it**, the lazy-diagnostic fix becomes required on its own."
  **The path survived.** Task 8.2 has landed, and `src/UI/UISystem.cpp:287` still runs
  `textRenderer.Layout(...)` per enabled label per frame through the legacy immediate path.
  `src/Rendering/Utf8.cpp:73` still builds a `VectorTextDiagnosticSink` per call and discards it —
  which stopped the log flooding but not the per-ill-formed-byte diagnostic construction the
  amendment named. The remedy is lazy diagnostic-string construction in
  `src/Text/UnicodeTextBuffer.cpp`.
  **This task is the new owner** because its `CollectRender(const molga::ui::UISnapshot&, ...)`
  signature is what finally retires the legacy per-frame text-layout path. If that retirement
  removes the hot caller outright, record that and close the item; if any per-frame caller of
  `DecodeUtf8` survives this task, implement the lazy-diagnostic fix here.
  Source: `01-dependencies-unicode.md:2261-2267`.


**Files:**

- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UILayoutSystem.cpp`
- Create: `src/UI/UIFixedTickClock.h`
- Create: `src/UI/UIFixedTickClock.cpp`
- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/GameOutputRenderer.h`
- Modify: `src/Rendering/GameOutputRenderer.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_ui.cpp`
- Modify: `tests/test_ui_input.cpp`
- Modify: `tests/test_game_view.cpp`
- Modify: `tests/test_scene_runtime.cpp`

**Interfaces:**

- Consumes: layout/render/hit snapshots, ordered frame values, one-event router/focus/scroll APIs, and runtime `World&`.
- Produces: global `UISystem::{ProcessFrame,CollectRender,OnWorldReleased}`, `UIFixedTickClock`, exact coalesced N/N+1 publication, one shared action audit, sink-bearing rendering, and world-aware production/canonical tick calls.

- [ ] **Step 1: Write the failing one-loop and N/N+1 test.**

  Create events `[pointer-down, pointer-up, key-down]`; the click callback hides itself and requests layout twice before publication. Assert one plan per event, one pointer-only geometry hit, one vector-loop visit per sequence, one coalesced N+1 build, zero N+1 re-hit-tests, and no `ReflowDeferred` diagnostic for either pre-publication request.

  ```cpp
  TEST_CASE("ProcessFrame freezes N and publishes at most one N plus one") {
      UIFrameFixture f;
      const auto snapshotN = f.BuildN();
      f.SetOrderedEvents({f.PointerDown(1), f.PointerUp(2), f.KeyDown(3)});
      f.OnClick([&] { f.HideTarget(); f.RequestLayoutTwice(); });
      const auto result = f.ProcessFrame();
      CHECK(result.interactionSnapshot.get() == snapshotN.get());
      CHECK(result.renderSnapshot.get() != snapshotN.get());
      CHECK(f.PlanCount() == 3);
      CHECK(f.PointerGeometryHitTestCount() == 1);
      CHECK(f.LayoutBuildCountAfterN() == 1);
      CHECK((f.AuditSequences() == std::vector<std::uint64_t>{1, 2, 3}));
      CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReflowDeferred) == 0);
  }
  ```

- [ ] **Step 1a2: Write a failing post-publication deferral test.**

  Install a test publication observer that invalidates layout only after N+1 is atomically published. Require that mutation to remain dirty for the next frame, exactly one rate-limited `ReflowDeferred`, no N+2 build in the current frame, and one rebuild at the next `ProcessFrame`. The observer is test instrumentation around publication, not an implementation callback exposed to gameplay.

- [ ] **Step 1b: Write a failing replayed-native-sequence test.**

  ```cpp
  TEST_CASE("ProcessFrame rejects a native batch replay before dispatch") {
      UIFrameFixture f;
      f.SetOrderedEvents({f.PointerDown(40), f.PointerUp(41)});
      CHECK(f.ProcessFrame().dispatches.size() == 2);
      f.SetOrderedEvents({f.PointerDown(40), f.PointerUp(41)});
      const auto replay = f.ProcessFrame();
      CHECK(replay.dispatches.empty());
      CHECK(f.Clicks() == 1);
      CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
  }
  ```

  Also pass `{42,42}` and `{44,43}` in fresh fixtures. Reject the complete batch before any callback; do not silently sort or partially dispatch it. Give detached surface 20 the same valid sequence values already completed by surface 10 and require independent acceptance, then replay them on surface 20 and require rejection. Pass a frame whose `UIFrameInput::windowId` differs from snapshot N's `surfaceWindowId` and require no planning/dispatch/publication. Canonical trace order remains the separate nonzero `traceOrdinal` field and never relaxes native `sequence` monotonicity.

- [ ] **Step 1c: Write failing production/canonical deterministic-tick tests.**

  Feed elapsed nanoseconds `{7'000'000, 9'000'000, 31'250'000}` through separate runtime and Game View `UIFixedTickClock` instances and require identical emitted values `{tickIndex 1,2,3; deltaSecondsRaw 1}` with the same carry. Encode/decode that exact vector through the canonical replay fixture and require byte/value equality. Add a frame with invalid `{tickIndex=0}`, duplicate/decreasing index, or delta outside `[1,64]`; require no scroll step, normal ordered-event audit preservation, and one `LayoutInvalid`.

- [ ] **Step 2: Run the facade red gate.**

  ```bash
  cmake --build --preset debug --target test_ui_input test_ui test_game_view \
    test_scene_runtime molga_runtime -j
  ```

  Expected: compilation fails because the frame facade and world-aware render path are absent.

- [ ] **Step 3: Define the production facade and delete generation-less overloads.**

  ```cpp
  class UISystem {
  public:
      static UISystem& Get();
      molga::ui::UIFrameResult ProcessFrame(
          World&, const molga::ui::UIFrameInput&,
          molga::text::TextLayoutService&,
          molga::text::TextDiagnosticSink&);
      void CollectRender(const molga::ui::UISnapshot&,
                         const molga::ui::UIPhysicalTransform&,
                         molga::RenderQueue&, TextRenderer&,
                         molga::text::TextDiagnosticSink&);
      void OnWorldReleased(std::uint64_t worldGeneration);
  };
  ```

  Remove production overloads accepting only object vectors and float viewport
  sizes. Every caller passes `textRenderer.LayoutService()` from Task 8; no
  facade constructs/caches another service. Update tests through fixtures
  rather than keeping an identity-less compatibility path.

- [ ] **Step 3b: Define the one production fixed-tick clock.**

  ```cpp
  class UIFixedTickClock {
  public:
      static constexpr std::uint64_t StepNanoseconds = 15'625'000;
      std::vector<UIDeterministicTick> AdvanceElapsedNanoseconds(
          std::uint64_t elapsedNanoseconds,
          molga::text::TextDiagnosticSink&);
  private:
      std::uint64_t carryNanoseconds_ = 0;
      std::uint64_t nextTickIndex_ = 1;
  };
  ```

  Use checked integer addition only. Emit one `deltaSeconds=Fixed26_6::FromRaw(1)` per complete 1/64-second interval, keep the remainder, and check `nextTickIndex_` before increment so it never wraps. Runtime and `GameViewWindow` each own this same class and receive the same host monotonic elapsed-nanosecond sample; neither uses `float dt`/ImGui delta. Canonical replay bypasses the clock and supplies the recorded exact values to `UIFrameInput::uiTicks`.

- [ ] **Step 4a: Seed projection state from snapshot-N and batch-start state.**

  Build snapshot N, then require nonzero `UIFrameInput::windowId == snapshotN->surfaceWindowId`; a mismatch fails the complete surface frame before projection. Create `UIPlanningState` from the revalidated focus/capture/hover identities plus `nativeWindowFocusedAtBatchStart`, `pointerAtBatchStart`, and `pointerAtBatchStartValid` mapped from the same take-once host batch and frozen surface-coordinate mapping. An invalid start point seeds no hover/capture and is never treated as logical zero. Add three first-scroll rows: a valid start point selects its frozen scroll chain; an invalid start is targetless; and a valid pointer motion before scroll replaces the seed and selects the new route. The state stores identities only and records the same surface window/world pair. Never seed from post-poll `Input` state; each matching `WindowFocus`/pointer event advances the projection sequentially from this start value.

- [ ] **Step 4b: Validate the complete batch before dispatch.**

  During the one projection pass, require each native `sequence` to be nonzero, greater than the preceding entry, and greater than `lastCompletedNativeSequenceBySurface_[{input.windowId,world.Generation()}]`. If any check fails, discard the stored plans and fail the whole batch with `ReferenceInvalid` before dispatch. Advance that surface-owned scalar only after the batch passes this validation; foreign-window entries still advance the batch's one end sequence because they remain audited in the same vector. A detached surface uses its own scalar, and `OnWorldReleased` erases all keys for that world. This bounded ledger complements Task 13.2's host take-once API and prevents replay even if a caller retained a copy.

- [ ] **Step 4b2: Validate the complete tick batch independently.**

  Require every tick index to be nonzero/strictly increasing past `lastCompletedTickBySurface_[{input.windowId,world.Generation()}]` and each raw delta in `[1,64]`. A bad tick batch executes no tick but does not discard valid native events; record one `LayoutInvalid`. A good batch advances only that bounded surface-owned scalar and is consumed once after event dispatch and before snapshot publication, calling `scrollSystem_.AdvanceTick` for each exact value. `OnWorldReleased` erases its tick keys. Task 14 adds `textInputSystem_.AdvanceTick` in the same tick loop; no subsystem receives the tick vector.

- [ ] **Step 4c: Preplan the complete batch with sequential policy projection.**

  Call `PlanNext(N,input.windowId,event,planningState)` followed by mutable `focusSystem_.ProjectEvent(N,plan,planningState)` in native order and store every result before invoking any callback. The focus projector writes the exact snapshot-N navigation destination into the plan before updating `planningState`; the handler later commits only that frozen destination. Foreign-window plans stay targetless and cannot update projection. Matching pointer capture/focus/navigation transitions update only `planningState`; text/edit target identity comes from `ownerAtIngest` and does not alter or consult projected focus. Canonical replay may additionally provide nonzero `traceOrdinal`; planning stores no pointer and never observes callback mutations.

- [ ] **Step 5a: Define exact accumulator merge rules.**

  `UIEventDispatchAccumulator` is seeded from one `PlannedUIEvent` and its event-kind primary-target rule. `Merge` requires the same sequence, validates `resolvedStageTarget` against `TargetFor(stage,stageTargetOrdinal)`, ORs action masks/dirty/consumed/delivered fields, and stores the latest nonempty `resultingFocus` without making focus primary. A stage mismatch is a debug assertion plus `ReferenceInvalid`; it is never substituted. The first consumed valid scroll result sets the initially empty scroll primary. `Finish` may be called once and returns exactly one record.

- [ ] **Step 5b: Dispatch each plan through one-event handlers and one accumulator.**

  ```cpp
  for (const PlannedUIEvent& planned : plannedEvents) {
      UIEventDispatchAccumulator audit(planned);
      audit.Merge(inputRouter_.HandleEvent(
          world, *snapshotN, planned, diagnostics));
      audit.Merge(focusSystem_.HandleEvent(
          world, *snapshotN, planned, diagnostics));
      audit.Merge(scrollSystem_.HandleEvent(
          world, *snapshotN, planned, diagnostics));
      // Task 14.1 adds one textInputSystem_.HandleEvent Merge here.
      focusSystem_.Revalidate(world, *snapshotN);
      result.dispatches.push_back(std::move(audit).Finish());
  }
  ```

  `UISystem::ProcessFrame` is the only function that owns vector traversal: one projection pass over `orderedEvents`, followed by one dispatch pass over the resulting plans. No subsystem receives either vector. Even ignored/stale events append one record with their original sequence.

- [ ] **Step 6: Publish at most one N+1 without retargeting.**

  Accumulate every layout/visibility invalidation from all event handlers and deterministic ticks before publication; multiple requests coalesce into one dirty set with no diagnostic. After both loops finish, build and atomically publish at most one render snapshot N+1 from the final pre-publication state. Only an invalidation whose generation is acquired after that N+1 publication remains dirty for the next frame and reports one rate-limited `ReflowDeferred`. Return N for both fields when no pre-publication geometry/visibility changed.

- [ ] **Step 7a: Extend the existing world-bearing output API.** Task 10.2
  already changed `GameOutputRenderer::Render`/`RenderLogical` and editor
  windows to take a real `World&`. Add the final UI snapshot as an explicit
  render input, use `world.Objects()` only for camera/world traversal, and
  never rebuild UI internally or restore an object-vector overload.

- [ ] **Step 7b: Make Game View and standalone runtime retain the frame result.**

  Bind Game View to `World&`. Both modes advance `UIFixedTickClock` from the same host monotonic elapsed-nanosecond sample, assign the returned values to `UIFrameInput::uiTicks`, call `ProcessFrame(world,input,textRenderer.LayoutService(),sink)` before script `Update`, retain `UIFrameResult::renderSnapshot`, render cameras, then call the sink-bearing `CollectRender` on that exact snapshot. Canonical replay assigns decoded ticks directly and executes the identical `ProcessFrame` path.

- [ ] **Step 8: Run frame-order, Game View, scene-transition, and full Debug gates.**

  ```bash
  cmake --build --preset debug --target test_ui_input test_ui_focus test_ui \
    test_ui_scroll test_game_view test_scene_runtime molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_input|test_ui_focus|test_ui|test_ui_scroll|test_game_view|test_scene_runtime|runtime_smoke)$' --output-on-failure
  ctest --preset debug
  ```

  Expected: all tests pass, and audit sequences prove one ordered loop and no N+1 retargeting.

- [ ] **Step 9: Commit the frozen frame facade.**

  ```bash
  git add src/UI/UISystem.* src/UI/UILayoutSystem.cpp src/UI/UIFixedTickClock.* \
    src/Rendering/TextRenderer.h \
    src/Rendering/GameOutputRenderer.* src/Editor/Windows/GameViewWindow.* \
    src/runtime_main.cpp tests/test_ui.cpp tests/test_ui_input.cpp \
    tests/test_game_view.cpp tests/test_scene_runtime.cpp
  git commit -m "feat: process UI events through one frozen frame"
  ```

### Task 13.1: Build a platform-independent, idempotent per-window text-input arbiter

**Files:**

- Modify: `src/Platform/TextInputOwner.h`
- Create: `src/Platform/NativeInputEvent.h`
- Create: `src/Platform/NativeTextEvent.h`
- Create: `src/Platform/TextInputArbiter.h`
- Create: `src/Platform/TextInputArbiter.cpp`
- Create: `tests/test_text_input_arbiter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `WindowId`, runtime target identity, `PixelRectU32`, and typed diagnostics.
- Produces: value-owned native input/text events, `ITextInputPlatform`, C++17-comparable authority/owner/token/area values, `TextInputOwnerTransition`, and idempotent `TextInputArbiter` behavior without direct SDL calls or retired-generation history.

- [ ] **Step 1a: Write a failing platform call-audit test.**

  ```cpp
  TEST_CASE("same owner and area are strictly idempotent") {
      MockTextInputPlatform platform;
      molga::text::VectorTextDiagnosticSink diagnostics;
      molga::platform::TextInputArbiter arbiter(platform, diagnostics);
      const auto request = RuntimeGameViewRequest(
          7, Target(1, 2, 3, 4), /* keyboardFocusVerified */ true);
      const auto acquired = arbiter.SetOwner(request);
      REQUIRE(acquired.current);
      const auto first = *acquired.current;
      CHECK(platform.Calls() == std::vector<std::string>{"Start:7"});
      platform.ClearCalls();
      const auto second = arbiter.SetOwner(request);
      REQUIRE(second.current);
      CHECK(second.status == TextInputOwnerTransitionStatus::NoChange);
      CHECK(second.current->generation == first.generation);
      CHECK_FALSE(second.cancelEngineComposition);
      CHECK(platform.Calls().empty());
      CHECK(arbiter.SetArea(first, {{{1, 2, 30, 10}}, 3}).Accepted());
      platform.ClearCalls();
      CHECK(arbiter.SetArea(first, {{{1, 2, 30, 10}}, 3}).status ==
            TextInputOwnerTransitionStatus::NoChange);
      CHECK(platform.Calls().empty());
  }
  ```

- [ ] **Step 1b: Write a failing owner-generation exhaustion test.**

  Stamp an event for the old runtime owner, set a scoped owner-generation allocator to `UINT64_MAX`, and request a real owner transition. Require `{status=GenerationExhausted, retired=old, current=nullopt, cancelEngineComposition=true}`, a boundary strictly greater than the prior one, Clear/Stop for the old current owner but no Start/SetArea, current owner `nullopt`, and one blocker `TextInputUnavailable` diagnostic. Assert the generation counter remains `UINT64_MAX`. The already-ingested old event still matches its old token/stamp once in its original host batch; a post-failure event stamps `None` and cannot use the old token. Replay rejection belongs to the host/`UISystem` monotonic one-use ledger, not to unbounded arbiter history.

  In a fresh row set the per-window transition boundary to `UINT64_MAX` and leave the owner-generation allocator usable. Require `GenerationExhausted` with unchanged current owner/area and last boundary, empty platform calls, `cancelEngineComposition=false`, no owner-generation acquisition, and one blocker. Neither counter wraps or publishes zero.

- [ ] **Step 1c: Write a failing one-call owner-priority test.**

  ```cpp
  TEST_CASE("arbiter enforces runtime over editor priority atomically") {
      TextInputArbiterFixture f;
      const auto runtimeResult = f.Arbiter().SetOwner(
          f.RuntimeGameViewRequest(10, f.TargetA(), true));
      REQUIRE(runtimeResult.current);
      const auto runtime = *runtimeResult.current;
      f.ClearCalls();
      const auto deniedEditor = f.Arbiter().SetOwner(f.EditorRequest(10));
      CHECK(deniedEditor.status == TextInputOwnerTransitionStatus::Rejected);
      CHECK(f.Calls().empty());
      CHECK(f.Arbiter().CurrentOwner(10) == runtime);

      const auto editorOtherResult = f.Arbiter().SetOwner(f.EditorRequest(20));
      REQUIRE(editorOtherResult.current);
      const auto editorOther = *editorOtherResult.current;
      f.ClearCalls();
      const auto deniedRuntime = f.Arbiter().SetOwner(
          f.RuntimeGameViewRequest(20, f.TargetB(), false));
      CHECK(deniedRuntime.status == TextInputOwnerTransitionStatus::Rejected);
      CHECK(f.Calls().empty());
      const auto runtimeOther = f.Arbiter().SetOwner(
          f.RuntimeGameViewRequest(20, f.TargetB(), true));
      REQUIRE(runtimeOther.current);
      CHECK((f.Calls() == std::vector<std::string>{
          "Clear:20", "Stop:20", "Start:20"}));
  }
  ```

  Do not expose a separate `CanAcquire` query. `SetOwner(request)` validates the request authority, reads current state, applies the priority rule, and performs any transition as one arbiter operation, so the bridge cannot race a precheck against owner replacement.

- [ ] **Step 1d: Write failing transition-value and engine-composition tests.**

  Add exact A-runtime → B-runtime, runtime → editor-denied, editor → authorized runtime, changed-area, release, and native-window-loss rows. For every row assert `status`, complete `retired`/`current` token values, `boundarySequence`, and `cancelEngineComposition`. Applied A→B/changed-area/release boundaries are strictly increasing even with no intervening native text; an idempotent area/owner call and rejected editor request preserve the prior boundary. A→B/release/window loss retires A with `cancelEngineComposition=true`; area-only update, rejected editor request, or editor retirement has it false. Calling the same transition twice through a recording `ApplyOwnerTransition` fixture cancels A exactly once and never clears committed text.

- [ ] **Step 1e: Write failing C++17 owner/token equality tests.**

  Add one-field-difference `==`/`!=` tables for `NativeKeyModifiers`, `TextInputOwner`, `TextInputOwnerStamp`, `TextInputOwnerRequest`, `TextInputOwnerToken`, `TextInputArea`, and `TextInputOwnerTransition`. Compile under C++17; do not rely on defaulted comparison.

- [ ] **Step 1f: Write the failing exact arbiter state-machine matrix.**

  Add one row each for one-owner-per-window, independent detached windows, stale release, failed Start, failed Clear, failed Stop, focus loss, owner deletion, and generation boundary. Each row declares the exact platform call vector, owner/token afterward, exact pre-stamped-event acceptance, post-transition stamping result, and diagnostic count.

- [ ] **Step 2: Write a failing deep-copy and sequence test.**

  Construct `NativeTextEvent` from a temporary UTF-8 buffer, destroy the source, and assert the copied `std::string` remains. Require text inner/outer sequence equality and explicit X/Y delta/axis fields on `NativeInputEvent`.

- [ ] **Step 3: Run the arbiter red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_text_input_arbiter -j
  ```

  Expected: compilation fails because the platform-independent arbiter types do not exist.

- [ ] **Step 4a: Define the deep-copied native text record.**

  ```cpp
  namespace molga::platform {
  enum class NativeTextEventKind : std::uint8_t { Editing, Commit };
  struct NativeTextEvent {
      std::uint64_t sequence = 0;
      molga::WindowId windowId = 0;
      std::uint64_t timestampNanoseconds = 0;
      NativeTextEventKind kind = NativeTextEventKind::Commit;
      std::string utf8;
      std::int32_t editingStartUtf8Characters = 0;
      std::int32_t editingLengthUtf8Characters = 0;
      TextInputOwnerStamp ownerAtIngest;
  };

  } // namespace molga::platform
  ```

- [ ] **Step 4b: Define the outer ordered native input record.**

  Include the Task 12.1 `src/Platform/NativeKeyModifiers.h` value; do not duplicate its booleans in this header.

  ```cpp
  namespace molga::platform {
  enum class NativeInputEventKind : std::uint8_t {
      Other, WindowFocus, PointerMotion, PointerButton, Scroll, Key,
      GamepadButton, GamepadAxis, TextEditing, TextCommit
  };
  struct NativeInputEvent {
      std::uint64_t sequence = 0;
      molga::WindowId windowId = 0;
      std::uint64_t timestampNanoseconds = 0;
      NativeInputEventKind kind = NativeInputEventKind::Other;
      std::uint32_t nativeType = 0;
      float x = 0.0f;
      float y = 0.0f;
      float deltaX = 0.0f;
      float deltaY = 0.0f;
      float axisValue = 0.0f;
      std::uint32_t control = 0;
      NativeKeyModifiers keyModifiers;
      bool active = false;
      std::optional<NativeTextEvent> text;
      std::optional<TextInputOwnerTransition> textInputOwnerTransition;
  };
  } // namespace molga::platform
  ```

- [ ] **Step 5a: Define authority-bearing requests, tokens, and area values.**

  Extend `src/Platform/TextInputOwner.h` with these values so both `NativeInputEvent.h` and `TextInputArbiter.h` include one acyclic definition source:

  ```cpp
  namespace molga::platform {
  enum class TextInputOwnerAuthority : std::uint8_t {
      EditorBridge,
      RuntimeStandaloneWindowFocused,
      RuntimeGameViewKeyboardFocused
  };
  struct TextInputOwnerRequest {
      molga::WindowId windowId = 0;
      TextInputOwner owner;
      TextInputOwnerAuthority authority = TextInputOwnerAuthority::EditorBridge;
      bool operator==(const TextInputOwnerRequest&) const noexcept;
      bool operator!=(const TextInputOwnerRequest& other) const noexcept {
          return !(*this == other);
      }
  };
  struct TextInputOwnerToken {
      molga::WindowId windowId = 0;
      std::uint64_t generation = 0;
      TextInputOwner owner;
      explicit operator bool() const noexcept {
          return windowId != 0 && generation != 0;
      }
      bool operator==(const TextInputOwnerToken&) const noexcept;
      bool operator!=(const TextInputOwnerToken& other) const noexcept {
          return !(*this == other);
      }
  };
  struct TextInputArea {
      molga::PixelRectU32 rect;
      std::int32_t cursorOffset = 0;
      bool operator==(const TextInputArea&) const noexcept;
      bool operator!=(const TextInputArea& other) const noexcept {
          return !(*this == other);
      }
  };

  enum class TextInputOwnerTransitionStatus : std::uint8_t {
      NoChange, Applied, Rejected, PlatformFailed, GenerationExhausted
  };
  struct TextInputOwnerTransition {
      TextInputOwnerTransitionStatus status =
          TextInputOwnerTransitionStatus::Rejected;
      std::optional<TextInputOwnerToken> retired;
      std::optional<TextInputOwnerToken> current;
      std::uint64_t boundarySequence = 0;
      bool boundaryPublished = false;
      bool cancelEngineComposition = false;
      bool Accepted() const noexcept {
          return status == TextInputOwnerTransitionStatus::NoChange ||
                 status == TextInputOwnerTransitionStatus::Applied;
      }
      bool operator==(const TextInputOwnerTransition&) const noexcept;
      bool operator!=(const TextInputOwnerTransition& other) const noexcept {
          return !(*this == other);
      }
  };
  } // namespace molga::platform

  ```

  `EditorBridge` is valid only for `EditorImGui`; it cannot replace, release, or move the area of an active `RuntimeUITextInput` owner in the same window. A runtime request is valid only with a full runtime identity and either verified standalone native-window focus or verified embedded Game View keyboard-focus authority; such a request may replace an editor owner. Window state is independent. These checks live in `TextInputArbiter::SetOwner`, not in a bridge precheck.

- [ ] **Step 5b: Define the mockable platform boundary.**

  ```cpp
  namespace molga::platform {
  class ITextInputPlatform {
  public:
      virtual ~ITextInputPlatform() = default;
      virtual bool ClearComposition(molga::WindowId, std::string&) = 0;
      virtual bool StopTextInput(molga::WindowId, std::string&) = 0;
      virtual bool StartTextInput(molga::WindowId, std::string&) = 0;
      virtual bool SetTextInputArea(molga::WindowId, molga::PixelRectU32,
                                    std::int32_t, std::string&) = 0;
  };
  } // namespace molga::platform
  ```

- [ ] **Step 6a: Add the process-global non-wrapping owner-generation allocator.**

  Define one translation-unit/process-global atomic shared by every arbiter and window. Use a compare/exchange loop identical in safety shape to the world allocator: check `candidate == 0 || candidate == UINT64_MAX` before incrementing; return `nullopt` without changing the atomic on exhaustion. Never use `fetch_add`.

  ```cpp
  std::optional<std::uint64_t> AcquireTextOwnerGeneration() noexcept {
      auto candidate = gNextTextOwnerGeneration.load(std::memory_order_relaxed);
      for (;;) {
          if (candidate == 0 || candidate == UINT64_MAX) return std::nullopt;
          if (gNextTextOwnerGeneration.compare_exchange_weak(
                  candidate, candidate + 1,
                  std::memory_order_relaxed,
                  std::memory_order_relaxed)) {
              return candidate;
          }
      }
  }
  ```

- [ ] **Step 6b: Implement idempotent same-owner acquisition.**

  ```cpp
  namespace molga::platform {
  class TextInputArbiter {
  public:
      TextInputArbiter(ITextInputPlatform&, molga::text::TextDiagnosticSink&);
      TextInputOwnerTransition SetOwner(const TextInputOwnerRequest&);
      TextInputOwnerTransition Release(const TextInputOwnerToken&);
      NativeTextEvent StampForCurrentOwner(NativeTextEvent);
      bool Accepts(const TextInputOwnerToken&, const NativeTextEvent&) const;
      bool Accepts(const TextInputOwnerToken&, molga::WindowId,
                   std::uint64_t sequence,
                   const TextInputOwnerStamp&) const;
      TextInputOwnerTransition SetArea(
          const TextInputOwnerToken&, TextInputArea);
      TextInputOwnerTransition OnWindowFocusLost(
          molga::WindowId, std::uint64_t nativeSequence);
      std::optional<TextInputOwnerToken> CurrentOwner(molga::WindowId) const;
  };
  } // namespace molga::platform
  ```

  Validate authority and apply same-window runtime-over-editor priority inside this single method. If `{windowId,kind,runtimeTarget}` is unchanged, return `{NoChange,nullopt,current,lastTransitionBoundary,boundaryPublished=false,cancel=false}` with no calls or state changes. A denied priority/authority request returns `{Rejected,nullopt,current,lastTransitionBoundary,false,false}` and makes no generation, state, area, or platform-call change.

- [ ] **Step 6c: Implement idempotent area updates.**

  `SetArea` first requires the exact current token. If the canonical `PixelRectU32` and rect.x-relative `cursorOffset` equal the stored area, return `NoChange` with the current token, `boundaryPublished=false`, and no platform call; otherwise call the platform once and publish the new area only on success, returning `Applied` with `boundaryPublished=true`. Rejection/failure reports its status and never fabricates a retired token or composition cancellation; a failure before any owner/area state mutation has `boundaryPublished=false`.

- [ ] **Step 7a: Reserve the next owner generation and transition boundary before mutation.**

  For a real state change, compute the Step 7c boundary candidate without publishing it before any Clear/Stop. Boundary exhaustion leaves the current owner/area unchanged, performs no platform call, emits blocker `TextInputUnavailable`, and returns `GenerationExhausted` with the last valid boundary and `cancelEngineComposition=false`. For an owner change, acquire the process-global owner generation next. If that allocator is exhausted, Clear/Stop the old owner if present, invalidate it, publish the already-valid new boundary, leave the window with no owner, emit the blocker, and return `{GenerationExhausted,old,nullopt,newBoundary,old-is-runtime}`; do not call Start or SetArea. A successfully acquired but later unused owner generation is burned, never rolled back/reused, while no zero generation or boundary is published. A changed area needs no owner generation.

- [ ] **Step 7b: Stamp complete owner values without retired history.**

  `StampForCurrentOwner` copies the current `{kind,runtimeTarget,generation}` into the event; with no current owner it writes the all-invalid `None` stamp. `Accepts` requires nonzero sequence, exact window, and field-for-field equality between caller token and immutable event stamp. It does not require that token to remain current, because an event ingested before a later transition still belongs to its old owner; it also does not retain a generation-to-owner map, retired-token list, or sequence-range history. Host take-once batches plus the completed-sequence ledger prevent replay, and UI dispatch re-resolves the stamped full identity immediately before mutation.

- [ ] **Step 7c: Implement exact transition ordering and bounded boundaries.**

  Retain exactly two scalar watermarks per live window: `lastStampedSequence` and `lastTransitionBoundary`; erase them when the window is destroyed. For a real state change compute `floor=max(lastStampedSequence,nativeFocusLossSequenceOrZero)`, then choose `candidate = floor > lastTransitionBoundary ? floor : CheckedAdd(lastTransitionBoundary,1)`. Reject zero/overflow fail-closed. `NoChange`/`Rejected` and a failure before mutation return the unchanged boundary with `boundaryPublished=false`; a successful area/owner mutation or a fail-closed path that actually retired/cleared owner state publishes `candidate` with `boundaryPublished=true`. No transition list or generation lookup map exists.

  Call SDL Clear, call Stop, invalidate the old current token, publish the already-reserved nonzero generation, call Start, then set the current area if present. Return the complete old/new tokens and selected boundary, and set `cancelEngineComposition` exactly when the retired owner is runtime. The arbiter cannot mutate engine composition itself; Task 14 immediately applies this value. New ingested events receive the new complete stamp; already-ingested values are never restamped or redirected. Failed Start preserves UI focus but marks native entry unavailable. Every platform failure emits `TextInputUnavailable`; old stamps can never match a new owner token.

- [ ] **Step 8: Run all arbiter state-machine tests.**

  ```bash
  cmake --build --preset debug --target test_text_input_arbiter -j
  ctest --test-dir build/debug -R '^test_text_input_arbiter$' --output-on-failure
  ```

  Expected: all idempotency, transition order, boundary, window isolation, failure, and deep-copy tests pass.

- [ ] **Step 9: Commit the platform-independent arbiter.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Platform/NativeInputEvent.h \
    src/Platform/TextInputOwner.h src/Platform/NativeTextEvent.h \
    src/Platform/TextInputArbiter.* \
    tests/test_text_input_arbiter.cpp
  git commit -m "feat: add idempotent text input arbitration"
  ```

### Task 13.2: Make `EngineHost` own one strictly ordered native event vector and arbiter

**Files:**

- Create: `src/Platform/SdlTextInputPlatform.h`
- Create: `src/Platform/SdlTextInputPlatform.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/Systems/Input.h`
- Modify: `src/Systems/Input.cpp`
- Modify: `src/UI/UIInputEvent.h`
- Create: `src/UI/UISurfaceCoordinateMapping.h`
- Create: `src/UI/UISurfaceCoordinateMapping.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Editor/ImGuiLayer.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_platform_sdl.cpp`
- Modify: `tests/test_text_input_arbiter.cpp`
- Modify: `tests/test_imgui_sdlgpu.cpp`
- Modify: `tests/test_gpu_sdl.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: platform-independent native values/arbiter, SDL events, and the existing `Input` state API.
- Produces: `EngineHost::{TakeNativeInputBatch,TextInput}`, stamped `NativeEventObserver` metadata, one host-owned platform/arbiter lifetime, strict ingest sequence, immutable SDL key-modifier copies, and one checked `UISurfaceCoordinateMapping` used by pointer and later IME mapping.

- [ ] **Step 1: Write the failing native order integration test.**

  Push SDL events in this order: `KeyDown`, `TextEditing`, `TextCommit`, `KeyUp`, wheel X/Y, gamepad axis. After `PollEvents`, require one vector with six entries, strictly increasing sequence, identical inner/outer sequence for both text events, exact UTF-8 copies, exact wheel deltas, and exact signed axis value.

  ```cpp
  TEST_CASE("EngineHost preserves one native event order including text") {
      EngineHostInputFixture f;
      const auto ownerTransition = f.Host().TextInput().SetOwner(
          f.RuntimeStandaloneRequest(f.OldTextInputIdentity(), true));
      REQUIRE(ownerTransition.current);
      const auto owner = *ownerTransition.current;
      f.PushKeyDown();
      f.PushTextEditing(u8"ㅎ", 0, 1);
      f.PushTextCommit(u8"한");
      f.PushKeyUp();
      f.PushWheel(-2.5f, 4.0f);
      f.PushGamepadAxis(-0.75f);
      f.Host().PollEvents();
      auto batch = f.Host().TakeNativeInputBatch();
      REQUIRE(batch.surfaceStarts.size() == 1);
      CHECK(batch.surfaceStarts[0].windowId == f.WindowId());
      CHECK(batch.surfaceStarts[0].pointerWindowPixelValid);
      const auto& events = batch.events;
      REQUIRE(events.size() == 6);
      CHECK(events[0].kind == NativeInputEventKind::Key);
      CHECK(events[1].kind == NativeInputEventKind::TextEditing);
      REQUIRE(events[1].text);
      CHECK(events[1].sequence == events[1].text->sequence);
      CHECK(events[1].text->ownerAtIngest.kind ==
            TextInputOwnerKind::RuntimeUITextInput);
      CHECK(events[1].text->ownerAtIngest.runtimeTarget ==
            f.OldTextInputIdentity());
      CHECK(events[1].text->ownerAtIngest.generation == owner.generation);
      CHECK(events[2].kind == NativeInputEventKind::TextCommit);
      CHECK(events[3].kind == NativeInputEventKind::Key);
      CHECK(events[4].deltaX == doctest::Approx(-2.5f));
      CHECK(events[4].deltaY == doctest::Approx(4.0f));
      CHECK(events[5].axisValue < 0.0f);
      CHECK(f.Host().TakeNativeInputBatch().empty());
  }
  ```

- [ ] **Step 1b: Write a failing pending-batch ownership test.**

  ```cpp
  TEST_CASE("EngineHost never overwrites or returns a native batch twice") {
      EngineHostInputFixture f;
      f.PushKeyDown();
      f.Host().PollEvents();
      f.PushKeyUp();
      f.Host().PollEvents(); // pending first batch: do not poll/overwrite
      CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
      const auto first = f.Host().TakeNativeInputBatch();
      REQUIRE(first.events.size() == 1);
      CHECK(first.events[0].active);
      CHECK(f.Host().TakeNativeInputBatch().empty());
      f.Host().PollEvents();
      const auto second = f.Host().TakeNativeInputBatch();
      REQUIRE(second.events.size() == 1);
      CHECK_FALSE(second.events[0].active);
      CHECK(second.events[0].sequence > first.events[0].sequence);
  }
  ```

  A `PollEvents` call with an untaken batch fails closed before `SDL_PollEvent`, leaving both the pending value batch and SDL queue intact. Every editor/runtime loop takes the batch once, even when it has no active UI world.

- [ ] **Step 1c: Write a failing observer stamp/order call audit.**

  ```cpp
  TEST_CASE("native observer sees stamped metadata before Input mutation") {
      EngineHostInputFixture f;
      const auto owner = f.AcquireRuntimeOwner();
      bool sawKeyBeforeInput = false;
      bool sawStampedText = false;
      f.Host().SetNativeEventObserver(
          [&](const void* raw, const NativeInputEvent& metadata) {
              const auto& event = *static_cast<const SDL_Event*>(raw);
              CHECK(metadata.sequence != 0);
              CHECK(metadata.nativeType == event.type);
              if (event.type == SDL_EVENT_KEY_DOWN) {
                  sawKeyBeforeInput = !f.InputHasKeyDown();
              }
              if (event.type == SDL_EVENT_TEXT_INPUT) {
                  REQUIRE(metadata.text);
                  CHECK(metadata.sequence == metadata.text->sequence);
                  CHECK(metadata.text->ownerAtIngest.generation ==
                        owner.generation);
                  sawStampedText = true;
              }
          });
      f.PushKeyDown();
      f.PushTextCommit("x");
      f.Host().PollEvents();
      CHECK(sawKeyBeforeInput);
      CHECK(sawStampedText);
      CHECK(f.InputHasKeyDown());
  }
  ```

  This is a call-order audit of the existing Bootstrap seam: metadata construction (including deep copy and owner stamp) precedes the observer, while the observer still precedes the existing `Input::Process*` call for ordinary events. There is no raw-text observer invocation before the stamp exists.

- [ ] **Step 1d: Write a failing cross-host sequence exhaustion test.**

  Poll one event from host A and then host B; require B's sequence to be greater. With the scoped process-global allocator at `UINT64_MAX`, polling publishes no event, leaves the counter unchanged, and emits one blocker. Restore the counter at scope exit.

- [ ] **Step 1e: Write a failing window-focus transition attachment test.**

  ```cpp
  TEST_CASE("window focus loss carries its owner transition in the same event") {
      EngineHostInputFixture f;
      const auto acquiredTransition = f.AcquireRuntimeOwnerTransition();
      REQUIRE(acquiredTransition.current);
      f.PushWindowFocusLost();
      f.Host().PollEvents();
      const auto events = f.Host().TakeNativeInputBatch().events;
      REQUIRE(events.size() == 1);
      REQUIRE(events[0].textInputOwnerTransition);
      const auto& transition = *events[0].textInputOwnerTransition;
      CHECK(transition.retired == acquiredTransition.current);
      CHECK_FALSE(transition.current);
      CHECK(transition.boundarySequence >= events[0].sequence);
      CHECK(transition.boundarySequence >
            acquiredTransition.boundarySequence);
      CHECK(transition.cancelEngineComposition);
  }
  ```

  Require the mapped `UIInputEvent` to contain the identical transition value. A second focus listener, transition queue, retired-owner map, or out-of-band callback count must remain zero/absent; the observer sees the already-attached value before ordinary `Input` mutation.

- [ ] **Step 1f: Write a failing SDL key-modifier mapping table.**

  Push four `SDL_EVENT_KEY_DOWN` rows whose `keysym.mod` values are respectively `SDL_KMOD_NONE`, `SDL_KMOD_CTRL`, `SDL_KMOD_GUI`, and `SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_SHIFT`. Require the resulting `NativeInputEvent::keyModifiers` and mapped `UIInputEvent::keyModifiers` to equal respectively `{}`, `{control=true}`, `{gui=true}`, and `{shift=true,control=true,gui=true}`; `alt` remains false. Repeat the last row for key-up and require identical modifier bits with `active=false`. This is a value-copy test, not a live SDL keyboard-state query.

- [ ] **Step 1g: Write failing surface-coordinate round-trip tests.**

  Freeze the same `UIPhysicalTransform` and window embedding used for rendering,
  then cover standalone 1x/2x, letterboxed output, cropped output, embedded Game
  View, and detached-window origins. A window point mapped from the center of a
  Canvas rect must hit that rect; points on the half-open content max edge or in
  a letterbox bar return invalid and clear pointer hover/capture without clearing
  keyboard focus. Map `NativeSurfaceBatchStart` and a first pointer-motion event
  at the same physical pixel and require byte-equal logical point/validity. Also
  round-trip a Canvas IME rect/cursor through Task 14.3 and require the same
  `UISurfaceCoordinateMapping` value, not a separately recomputed scale/origin.

- [ ] **Step 2: Run the host integration red gate.**

  ```bash
  cmake --build --preset debug --target test_platform_sdl test_text_input_arbiter -j
  ```

  Expected: compilation fails because `EngineHost` exposes no ordered take-once batch or arbiter.

- [ ] **Step 3a: Implement SDL Clear/Stop/Start adapters.**

  `SdlTextInputPlatform` resolves `WindowId` to `SDL_Window*`. Implement `ClearComposition`, `StopTextInput`, and `StartTextInput` as one direct SDL call each; copy `SDL_GetError()` to the out string on failure. This file is the sole compiled SDL lifecycle-call site.

- [ ] **Step 3a2: Implement checked SDL input-area conversion.**

  In `SetTextInputArea`, checked-convert `PixelRectU32` and rect.x-relative window-pixel cursor offset to `SDL_Rect`/`int`, reject overflow without an SDL call, then call `SDL_SetTextInputArea` once and copy `SDL_GetError()` on failure.

- [ ] **Step 3b: Change host construction to explicit diagnostic lifetime.**

  ```cpp
  namespace molga::platform {
  struct NativeSurfaceBatchStart {
      molga::WindowId windowId = 0;
      bool nativeWindowFocused = false;
      std::int32_t pointerWindowPixelX = 0;
      std::int32_t pointerWindowPixelY = 0;
      bool pointerWindowPixelValid = false;
      bool operator==(const NativeSurfaceBatchStart&) const noexcept;
  };
  struct NativeInputBatch {
      std::vector<NativeInputEvent> events;
      std::vector<NativeSurfaceBatchStart> surfaceStarts;
      bool empty() const noexcept {
          return events.empty() && surfaceStarts.empty();
      }
      const NativeSurfaceBatchStart* FindSurface(
          molga::WindowId) const noexcept;
  };
  } // namespace molga::platform

  class EngineHost {
  public:
      molga::platform::NativeInputBatch TakeNativeInputBatch();
      molga::platform::TextInputArbiter& TextInput() noexcept;
  };

  using NativeEventObserver = std::function<void(
      const void* rawSdlEvent,
      const molga::platform::NativeInputEvent& stampedMetadata)>;

  std::unique_ptr<EngineHost> EngineInit(
      const WindowConfig&, molga::text::TextDiagnosticSink&);

  EngineShutdownStatus EngineShutdown(
      std::unique_ptr<EngineHost>&,
      molga::text::TextDiagnosticSink&);
  ```

  `EngineHost::Impl` declares/constructs `SdlTextInputPlatform` before one `TextInputArbiter` and destroys them in reverse order before SDL shutdown. Preserve Task 11.2's fail-closed `EngineShutdownStatus` and binding-lifetime teardown order while threading the same sink through init/shutdown. The diagnostic sink must outlive the host. Update the friend declarations/definitions together and do not keep an overload that creates a hidden sink or another arbiter.

- [ ] **Step 3c: Update editor and standalone runtime host call sites.**

  Construct an owning diagnostic sink before the host in `src/main.cpp` and `src/runtime_main.cpp`, pass it to `EngineInit` and `EngineShutdown`, and keep it alive until a `Complete` shutdown result. Never force-reset a host after `GpuDrainFailed`/`ExternalGpuLifetime`.
  A non-`Complete` result records the eventual failing process status but does
  not return or unwind: release only known external snapshot/page owners, then
  retry the same host. A persistent failure remains in a fail-closed
  shutdown-blocked state with host, device, text services, diagnostic sink, and
  terminal text-runtime guard alive; it never reaches ordinary destructors or
  `u_cleanup`. Catch an engine-body exception inside this owner scope, save its
  failing status, and finish the identical retained-host shutdown loop before
  leaving the scope; stack unwinding is not a substitute for `EngineShutdown`.

- [ ] **Step 3d: Update every SDL host test call site.**

  Update all `EngineInit` and `EngineShutdown` calls in `tests/test_platform_sdl.cpp`, `tests/test_imgui_sdlgpu.cpp`, `tests/test_gpu_sdl.cpp`, and `tests/test_rendering_sdlgpu.cpp`. Each fixture owns its sink outside the host and compiles in this commit; no test-only compatibility overload is allowed.

- [ ] **Step 3e: Update every native-observer call site.**

  Change the lambdas in `src/Editor/ImGuiLayer.cpp` and `tests/test_platform_sdl.cpp` to accept both raw SDL event and stamped metadata. Keep the metadata reference call-scoped; code that needs it later copies the value. Delete the one-argument observer typedef/overload so raw text cannot use the old pre-stamp path.

- [ ] **Step 4a: Build metadata before the existing observer/Input switch.**

  Refactor the current `eventObserver(&event)`-before-switch placement. For every polled SDL event, first acquire the next nonzero host-lifetime sequence and build a local `NativeInputEvent` with `nativeType` (using `Other` for non-UI host events). Invoke `eventObserver(&event, metadata)` only after the metadata is complete, then run the existing host/Input switch, then append the value exactly once. This preserves observer-before-`Input::Process*` behavior without exposing raw text pre-stamp.

- [ ] **Step 4a2: Attach native window-loss owner transition before observation.**

  For `SDL_EVENT_WINDOW_FOCUS_LOST`, after assigning the event sequence and before the observer, call `arbiter.OnWindowFocusLost(windowId, sequence)` once and store the returned value in `NativeInputEvent::textInputOwnerTransition`. In this same slice extend `UIInputEvent` with:

  ```cpp
  std::optional<molga::platform::TextInputOwnerTransition>
      textInputOwnerTransition;
  ```

  Map the optional field one-for-one. Do not register another SDL listener or append to another vector; Task 14 applies it at this event's exact position in the sole dispatch loop.

- [ ] **Step 4b: Copy window, pointer, button, and scroll SDL fields.**

  Copy window/control/X/Y/delta fields at the native position. Preserve both signed scroll axes after applying SDL's flipped-direction rule exactly once.

- [ ] **Step 4c: Copy key modifiers and gamepad SDL events.**

  Copy key/button control plus active state and exact normalized signed gamepad axis value. For a key event, derive the four booleans solely from that event's immutable `keysym.mod`: `shift=(mod & SDL_KMOD_SHIFT)!=0`, `control=(mod & SDL_KMOD_CTRL)!=0`, `alt=(mod & SDL_KMOD_ALT)!=0`, and `gui=(mod & SDL_KMOD_GUI)!=0`. Copy the resulting `NativeKeyModifiers` into the mapped UI event one-for-one; never query current keyboard state while dispatching. Use `SDL_GetTicksNS()` or the event timestamp only as copied audit data; sequence determines order.

- [ ] **Step 5a: Stamp text through the same host arbiter before observation.**

  For editing/commit, copy UTF-8 plus SDL UTF-8-character start/length, set inner sequence equal to outer sequence, call `StampForCurrentOwner`, and place it in the outer event before the observer call. Never store SDL text pointers. Owner transitions during later UI callbacks cannot restamp already-ingested events.

- [ ] **Step 5b: Implement the take-once pending-batch state.**

  `PollEvents` checks for a prior pending batch before `Input::BeginFrame` or `SDL_PollEvent`; on conflict it reports one `ReferenceInvalid` and changes neither Input state, pending values, nor SDL queue. `TakeNativeInputBatch` atomically moves the surface-start seeds and event vector, marks them taken, and returns an empty batch on a second take. Acquire every outer sequence from one process-global compare/exchange allocator shared across host/window instances; check `0`/`UINT64_MAX` before incrementing. Exhaustion leaves the atomic at `UINT64_MAX`, emits a blocker, and stops polling without publishing zero or a reused sequence.

- [ ] **Step 6a: Map pointer and scroll fields into checked fixed values.**

  Add this exact shared value boundary:

  ```cpp
  namespace molga::ui {
  struct UISurfaceCoordinateMapping {
      UIPhysicalTransform canvasToOutput;
      double outputOriginWindowPixelX = 0.0;
      double outputOriginWindowPixelY = 0.0;
      double windowPixelsPerOutputPixelX = 1.0;
      double windowPixelsPerOutputPixelY = 1.0;
      molga::PixelRectU32 sdlWindowBoundsPixels;
      std::optional<molga::FixedPoint> WindowPixelToCanvas(
          double windowPixelX, double windowPixelY) const noexcept;
      std::optional<molga::PixelRectU32> CanvasRectToWindowPixels(
          const molga::FixedRect&) const noexcept;
      std::optional<std::int32_t> CanvasCursorXToWindowPixel(
          molga::Fixed26_6 logicalX) const noexcept;
  };
  } // namespace molga::ui
  ```

  The frame owner freezes one mapping for the same presentation used to build
  and render snapshot N. Standalone uses window/framebuffer origin and scale;
  embedded or detached Game View converts its window-local image origin to
  physical window pixels before constructing the value. To map an SDL window
  point, multiply its copied logical-point coordinates by the event window's
  pixels-per-point exactly once, subtract `outputOriginWindowPixel`, divide by
  `windowPixelsPerOutputPixel`, then call the sole
  `UIPhysicalTransform::ToLogicalPoint`. A finite point outside the half-open
  presentation content returns `nullopt`, not a clamped Canvas edge. Map the
  batch-start physical pixel through the identical value only when
  `pointerWindowPixelValid`; an unavailable platform pointer query leaves the
  mapped batch-start validity false without inventing `(0,0)`. Invalid/non-finite or
  overflowed input emits `LayoutInvalid`, preserves the original sequence audit,
  and sets the corresponding validity flag false. Signed scroll X/Y remains a
  semantic SDL scroll value and is quantized directly once; it is not scaled as
  a pointer displacement.

- [ ] **Step 6b: Map key, gamepad, and text fields without reordering.**

  Copy controls, `keyModifiers`, active states, exact signed axis, deep-copied text payload including complete `ownerAtIngest`, and any attached `textInputOwnerTransition` into one `UIInputEvent` per native entry. Pointer entries copy the mapped value plus `logicalPointValid`; all other kinds ignore those fields. Preserve vector position and sequence, move the take-once batch's event vector into `UIFrameInput::orderedEvents`, and map the matching unique surface seed into `pointerAtBatchStart`/`pointerAtBatchStartValid`/`nativeWindowFocusedAtBatchStart` before calling `UISystem::ProcessFrame`. A missing or duplicate surface seed rejects that complete surface frame. Ordinary `Input` state may update from each same native entry, but it must not synthesize or reorder a second UI/transition stream. The all-false modifier default keeps the existing Milestone 18 canonical trace descriptor and bytes unchanged because that fixture has no key event; a future modifier-bearing qualification trace would require a separately approved schema change.

- [ ] **Step 7: Add strict-order, observer-order, and no-drain assertions.**

  Assert every mapped sequence is strictly increasing and text inner/outer sequence matches. Audit `src/Core/Bootstrap.cpp` to require `StampForCurrentOwner` before every text observer invocation and to forbid a one-argument `eventObserver(&event)` call. Search production code for `DrainText`, `PendingTextEvents`, or a second text vector and remove those paths if present.

- [ ] **Step 8: Run platform, GPU-host, and runtime gates.**

  ```bash
  cmake --build --preset debug --target test_platform_sdl test_text_input_arbiter \
    test_imgui_sdlgpu test_gpu_sdl test_rendering_sdlgpu \
    molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_platform_sdl|test_text_input_arbiter|test_imgui_sdlgpu|test_gpu_sdl|test_rendering_sdlgpu)$' --output-on-failure
  ```

  Expected: all selected tests pass; host batches are take-once, preserve exact native sequence and complete ownership stamps, and never expose raw text before stamping.

- [ ] **Step 9: Commit host ownership and ordered ingest.**

  ```bash
  git add CMakeLists.txt src/Platform/SdlTextInputPlatform.* \
    src/Core/Bootstrap.* src/Systems/Input.* src/UI/UIInputEvent.h \
    src/UI/UISurfaceCoordinateMapping.* \
    src/Editor/ImGuiLayer.cpp src/Editor/Windows/GameViewWindow.* src/main.cpp \
    src/runtime_main.cpp tests/test_platform_sdl.cpp \
    tests/test_text_input_arbiter.cpp tests/test_imgui_sdlgpu.cpp \
    tests/test_gpu_sdl.cpp \
    tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: ingest one ordered native input stream"
  ```

### Task 13.3: Make the engine-owned ImGui bridge obey the arbiter

**Files:**

- Create: `src/Editor/ThirdParty/imgui_impl_sdl3_molga.cpp`
- Create: `src/Editor/ThirdParty/IMGUI_SDL3_LICENSE.txt`
- Create: `src/Editor/ImGuiTextInputBridge.h`
- Create: `src/Editor/ImGuiTextInputBridge.cpp`
- Modify: `src/Editor/ImGuiLayer.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_imgui_sdlgpu.cpp`
- Modify: `tests/test_text_input_arbiter.cpp`
- Modify: `tests/test_platform_sdl.cpp`

**Interfaces:**

- Consumes: host-owned `TextInputArbiter`, `ITextInputPlatform`, ImGui `Platform_SetImeDataFn`, and SDL window IDs.
- Produces: the sole compiled SDL lifecycle-call site, engine-owned pinned ImGui SDL3 backend, and editor owner requests that cannot interfere with runtime owners.

- [ ] **Step 1: Write failing ImGui/runtime contention call-audit tests.**

  Acquire a runtime owner in window A, then send an editor IME request for window A and another for detached window B. Require no Stop/Clear/SetArea in A for the editor request, independent editor Start/SetArea in B, and unchanged runtime generation in A. Hide the B editor request and require only B Clear/Stop.

  ```cpp
  TEST_CASE("ImGui bridge cannot disturb runtime owner or another window") {
      ImGuiBridgeFixture f;
      const auto runtime = f.AcquireRuntimeOwner(/* window */ 10);
      f.ShowEditorIme(/* window */ 10, Rect(1, 2, 30, 10), 3);
      CHECK_FALSE(f.EditorToken(10));
      CHECK(f.CallsFor(10).empty());
      CHECK(f.Arbiter().CurrentOwner(10)->generation == runtime.generation);
      f.ShowEditorIme(/* detached window */ 20, Rect(4, 5, 40, 12), 6);
      CHECK((f.CallsFor(20) ==
             std::vector<std::string>{"Start:20", "SetArea:20"}));
      f.ClearCalls();
      f.HideEditorIme(20);
      CHECK((f.AllCalls() ==
             std::vector<std::string>{"Clear:20", "Stop:20"}));
      CHECK(f.Arbiter().CurrentOwner(10)->generation == runtime.generation);
  }
  ```

  This test exercises one `TextInputArbiter::SetOwner(EditorRequest(...))` call from the bridge. The bridge must not perform a separate `CurrentOwner`/`CanAcquire` precheck. `TextInputOwnerTransitionStatus::Rejected` with the unchanged runtime `current` token is the normal same-window contention result, and the bridge must not follow it with `SetArea`, `Release`, Clear, Stop, or Start.

- [ ] **Step 1b: Write a failing exact-stamp raw-text gate test.**

  ```cpp
  TEST_CASE("ImGui receives only editor-stamped text metadata") {
      ImGuiBridgeFixture f;
      const auto runtime = f.AcquireRuntimeOwner(10);
      f.PushTextCommit(10, "runtime");
      f.PollAndTakeNativeBatch();
      CHECK(f.ImGuiTextEvents() == 0);

      const auto released = f.Arbiter().Release(runtime);
      CHECK(released.status == TextInputOwnerTransitionStatus::Applied);
      const auto editor = f.ShowEditorIme(10, Rect(1, 2, 30, 10), 3);
      REQUIRE(editor);
      f.PushTextCommit(10, "editor");
      f.PollAndTakeNativeBatch();
      CHECK(f.ImGuiTextEvents() == 1);
      CHECK(f.LastObservedMetadata().text->ownerAtIngest.kind ==
            TextInputOwnerKind::EditorImGui);
      CHECK(f.LastObservedMetadata().text->ownerAtIngest.generation ==
            editor.generation);
  }
  ```

  Add wrong-window, wrong-token, and `None` stamp rows. Every raw SDL text/edit event reaches at most one consumer; runtime/none rows never call `ImGui_ImplSDL3_ProcessEvent`.

- [ ] **Step 2: Run the bridge red gate.**

  ```bash
  cmake --build --preset debug --target test_imgui_sdlgpu test_text_input_arbiter -j
  ```

  Expected: assertions fail because the upstream backend calls SDL text lifecycle functions directly.

- [ ] **Step 3: Vendor the pinned backend source and retain its license.**

  Copy the exact current pinned `external/imgui/backends/imgui_impl_sdl3.cpp` into `src/Editor/ThirdParty/imgui_impl_sdl3_molga.cpp`, retain the upstream license text, remove/disable its direct `PlatformSetImeData` lifecycle logic, and compile this engine-owned source instead of the external backend source. Keep ordinary event translation intact.

- [ ] **Step 4: Install the engine-owned ABI-compatible bridge.**

  ```cpp
  class ImGuiTextInputBridge {
  public:
      static void Install(molga::platform::TextInputArbiter&);
      static void PlatformSetImeData(ImGuiContext*, ImGuiViewport*,
                                     ImGuiPlatformImeData*);
      static void Shutdown();
  };
  ```

  Immediately before `Input::BeginFrame`, snapshot each registered window's
  focus and window-local pointer pixels into one stable window-ID-sorted
  `surfaceStarts` vector, then poll/append events. The take-once move transfers
  seeds and events atomically. Runtime/Game View find their surface seed and map
  its pointer through the exact frozen `UISurfaceCoordinateMapping` also used for
  every event and Task 14.3 IME output into `UIFrameInput::pointerAtBatchStart`
  plus its validity flag; absence/duplicate seed fails the complete surface frame
  rather than using post-poll state.

  Install immediately after `ImGui_ImplSDL3_InitForSDLGPU`. Resolve the passed viewport's SDL window ID, not global keyboard focus. Convert `{WantVisible || WantTextInput, InputPos, InputLineHeight}` into one `EditorRequest` passed directly to `TextInputArbiter::SetOwner`; call `SetArea` only when the transition is accepted and `current` is the exact requested editor owner. Store that token. When hidden, call `Release` only for the bridge's matching editor token and consume its returned transition; an editor retirement never requests engine-composition cancellation. The arbiter, not this bridge, owns the same-window priority decision: editor requests cannot replace an active runtime owner; an authority-verified runtime request may replace an editor owner; other windows remain independent.

- [ ] **Step 5a: Preserve ordinary observer delivery with stamped metadata.**

  Update `ImGuiLayer`'s two-argument observer. Forward ordinary SDL events to the vendored backend in their existing observer-before-`Input` position; metadata is audit-only for those kinds.

- [ ] **Step 5b: Gate text/edit delivery by the exact ingest stamp.**

  For text/edit metadata, require payload presence and `ownerAtIngest.kind == EditorImGui`; construct/use the bridge's matching editor token and call `arbiter.Accepts(token, windowId, sequence, ownerAtIngest)`. Only then forward the raw SDL event. `RuntimeUITextInput` and `None` stamps return without calling ImGui. Never substitute `CurrentOwner` for the immutable stamp and never forward text through a pre-stamp observer path.

- [ ] **Step 6: Prove sole ownership at source and linked-target level.**

  ```bash
  rg -n 'SDL_(StartTextInput|StopTextInput|ClearComposition|SetTextInputArea)' \
    src external/imgui/backends/imgui_impl_sdl3.cpp
  rg -n 'eventObserver\(&event\)|NativeEventObserver.*function<void\(const void\*\)>' \
    src tests
  cmake --build --preset debug --target molga_engine molga_runtime -j --verbose
  ```

  Expected: production lifecycle matches in `src/` appear only in `src/Platform/SdlTextInputPlatform.cpp`; the old one-argument/pre-stamp observer search has no match; the verbose link/source list includes `imgui_impl_sdl3_molga.cpp` and excludes `external/imgui/backends/imgui_impl_sdl3.cpp`. Upstream source may still contain dormant lifecycle calls because it is not compiled.

- [ ] **Step 7: Run bridge, platform, and renderer gates.**

  ```bash
  cmake --build --preset debug --target test_imgui_sdlgpu \
    test_text_input_arbiter test_platform_sdl test_gpu_sdl \
    test_rendering_sdlgpu molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_imgui_sdlgpu|test_text_input_arbiter|test_platform_sdl|test_gpu_sdl|test_rendering_sdlgpu)$' --output-on-failure
  ```

  Expected: all selected tests pass and contention audits show window/owner isolation.

- [ ] **Step 8: Commit sole SDL ownership.**

  ```bash
  git add CMakeLists.txt src/Editor/ThirdParty/imgui_impl_sdl3_molga.cpp \
    src/Editor/ThirdParty/IMGUI_SDL3_LICENSE.txt \
    src/Editor/ImGuiTextInputBridge.* src/Editor/ImGuiLayer.cpp \
    tests/test_imgui_sdlgpu.cpp tests/test_text_input_arbiter.cpp \
    tests/test_platform_sdl.cpp
  git commit -m "feat: centralize SDL text input ownership"
  ```

### Task 14.1: Implement grapheme-safe runtime text editing as a one-event handler

**Files:**

- Create: `src/UI/UITextInputSystem.h`
- Create: `src/UI/UITextInputSystem.cpp`
- Create: `tests/test_ui_text_input.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UIFocusSystem.cpp`
- Modify: `src/UI/UIInputRouter.cpp`
- Modify: `src/ECS/Components/UITextInput.h`
- Modify: `src/ECS/Components/UITextInput.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_game_view.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: per-stage `PlannedUIEvent`, immutable ingest-owner stamps/owner transitions, copied `NativeKeyModifiers`, `UnicodeTextBuffer`, `TextLayoutService`, `TextHitTesting`, `UIDeterministicTick`, projected key focus, and host arbiter acceptance.
- Produces: `UITextEditState`, `UITextInputSystem::{HandleEvent,ApplyOwnerTransition,AdvanceTick,GetVisualState,State,OnWorldReleased}`, explicit runtime service injection, pointer caret/drag selection, public `UISystem::{GetValue,SetValue,Submit,SetValueChangedCallback,SetSubmittedCallback}`, invocation-safe callbacks, and no frame/vector/drain API.

- [ ] **Step 1: Register editing/service-injection tests with the common text runtime session.** Create `test_ui_text_input` through `molga_add_text_test` and replace the existing `test_game_view` registration with the same helper before either target constructs the injected `TextLayoutService`/`TextHitTesting` fixtures. The helper performs the idempotent dependency attach; do not call `molga_attach_text_dependencies` separately. Use only `MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT`, never `doctest_main` or fixture-local ICU initialization, and preserve Game View's existing includes, labels, SDL properties, working directory, timeout, and extra sources after target creation. Both executables reuse Task 2.2's installed `TextRuntimeTestSession`.

- [ ] **Step 1a: Write failing logical grapheme edit tests.**

  ```cpp
  TEST_CASE("backspace deletes one extended grapheme") {
      UITextInputFixture f(u8"A👩‍👩‍👧‍👦B");
      f.PlaceCaretAfterGrapheme(2);
      f.HandleKey(Key::Backspace, 11);
      CHECK(f.Value() == "AB");
      CHECK(f.Caret().boundary == 1);
  }
  ```

- [ ] **Step 1b: Add the failing Unicode edit-edge table.**

  ```cpp
  const std::vector<EditCase> cases{
      {u8"e\u0301x", Edit::BackspaceAfter(1), "x", 0},
      {u8"✈️x", Edit::DeleteAt(0), "x", 0},
      {u8"a👩‍👩‍👧‍👦b", Edit::SelectAndReplace(1, 2, "Z"), "aZb", 2},
  };
  for (const auto& c : cases) CHECK(RunEdit(c) == c.expected);
  ```

- [ ] **Step 1c: Add the failing edit-policy table.**

  Add exact rows for initialization, plain insertion, shift selection, clipboard-free replace, read-only insertion, and max-grapheme rejection. Then run this Enter matrix against a focused input whose value starts as `"A"`:

  ```cpp
  const std::vector<EnterCase> enterCases{
      // multiline, modifiers, value after key-down, submitted callbacks
      {false, Mods(),                    "A",   1},
      {false, Mods(/*ctrl=*/true),       "A",   1},
      {false, Mods(false, /*gui=*/true), "A",   1},
      {true,  Mods(),                    "A\n", 0},
      {true,  Mods(/*ctrl=*/true),       "A",   1},
      {true,  Mods(false, /*gui=*/true), "A",   1},
      {true,  Mods(/*ctrl=*/true,
                   /*gui=*/true),        "A",   1},
      {true,  ShiftAltMods(),            "A\n", 0},
  };
  for (const auto& row : enterCases) {
      UITextInputFixture f("A", row.multiline);
      const auto down = f.HandleKeyDown(Key::Enter, row.modifiers);
      CHECK(down.consumed);
      CHECK(f.Value() == row.expectedValue);
      CHECK(f.SubmittedCallbackCount() == row.expectedSubmits);
      const auto up = f.HandleKeyUp(Key::Enter, row.modifiers);
      CHECK(up.consumed);
      CHECK(f.Value() == row.expectedValue);
      CHECK(f.SubmittedCallbackCount() == row.expectedSubmits);
  }
  ```

  Here `Mods(ctrl,gui)` sets exactly `NativeKeyModifiers::control/gui`; `ShiftAltMods()` proves Shift/Alt alone do not form a submit chord. Also require each submit callback receives exactly the pre-submit `"A"` once, newline insertion invokes the value callback once and no submit callback, and key-up invokes neither callback. Finally call the public `Submit` API once on multiline input and require `Applied`, one submitted callback, unchanged value/caret/selection, and no key-consumption record because it is not an event. Every row checks logical caret boundary/affinity and selection as well. Milestone A has no alternate content/submit enum value.

- [ ] **Step 1d: Write failing visual BiDi caret tests.**

  ```cpp
  TEST_CASE("BiDi arrows move visual stops while deletion stays logical") {
      UITextInputFixture f(u8"ab אב 12");
      f.PlaceCaret({3, CaretAffinity::Downstream});
      const auto visualRight = f.HandleKey(Key::Right, 20).Caret();
      CHECK(visualRight == f.Layout().NextVisualStop({3, CaretAffinity::Downstream}));
      f.HandleKey(Key::Backspace, 21);
      CHECK(f.LastDeletedRange() == f.LogicalGraphemeBefore(visualRight.boundary));
      f.HandleKey(Key::Home, 22);
      CHECK(f.Caret() == f.Layout().FirstVisualStopOnCurrentLine());
      f.HandleKey(Key::End, 23);
      CHECK(f.Caret() == f.Layout().LastVisualStopOnCurrentLine());
  }
  ```

  Add an ordered-frame row whose initial mixed-BiDi value is shaped in snapshot
  N, then dispatch `TextCommit` followed by `Key::Right` and `Key::Home` to the
  same stamped/focused input. Require both keys to use visual stops from the
  post-commit value, not N's old layout, while hit targets/viewport remain N and
  the frame still publishes at most one final N+1.

- [ ] **Step 2: Write failing composition and SDL range conversion tests.**

  Feed editing ranges in SDL UTF-8-character units for ASCII, multibyte scalar, combining sequence, and emoji. Require mapping through `UnicodeTextBuffer` to original byte/scalar ranges, clamp out-of-range values with `TextInputRangeClamped`, and never place caret inside a UTF-16 surrogate or extended grapheme.

  ```cpp
  TEST_CASE("composition range clamps to composition-relative graphemes") {
      UITextInputFixture f("AB");
      f.PlaceCaretAfterGrapheme(1);
      // SDL UTF-8 characters: e=0, combining acute=1, emoji=2.
      f.HandleEditing(u8"e\u0301🙂", 1, 1, 40);
      const auto& state = f.State();
      CHECK((state.compositionSelection == GraphemeRange{0, 1}));
      CHECK(f.Diagnostics().Count(TextDiagnosticCode::TextInputRangeClamped) == 1);
  }
  ```

- [ ] **Step 3: Write the failing single-loop acceptance test.**

  ```cpp
  TEST_CASE("pre-ingested text edits stamped owner while key uses projected focus") {
      UITextInputFrameFixture f;
      const auto oldInput = f.FocusedInputA();
      const auto newInput = f.InputB();
      const auto oldToken = f.AcquireRuntimeOwner(oldInput);
      const auto editing = f.IngestEditing(
          52, u8"ㅎ", 0, 1, oldToken); // immutable old-owner stamp
      const auto commit = f.IngestCommit(53, u8"한", oldToken);
      f.SetOrderedEvents({
          f.PointerDownOn(newInput, 50), f.KeyDown(51, Key::Right),
          editing, commit, f.KeyUp(54, Key::Right)});
      const auto result = f.ProcessFrame();
      CHECK((f.AuditSequences(result) ==
             std::vector<std::uint64_t>{50, 51, 52, 53, 54}));
      CHECK(f.LastKeyTarget() == newInput);
      CHECK(f.Value(oldInput) == u8"한");
      CHECK(f.Value(newInput).empty());
      CHECK(result.dispatches[2].runtimeTarget == oldInput);
      CHECK(result.dispatches[3].runtimeTarget == oldInput);
  }
  ```

- [ ] **Step 3b: Add failing text-stamp rejection and API-shape rows.**

  Add exact wrong-window, zero generation, wrong runtime identity, `EditorImGui`, `None`, non-increasing/replayed sequence, and deleted stamped target rows. Each produces one ignored audit record and no mutation/callback; an editor stamp has no UI target. Add compile-time detection that `UITextInputSystem` has no `Drain`, `ProcessFrame`, or vector overload.

- [ ] **Step 3c: Write failing pointer caret placement/captured-drag tests.**

  ```cpp
  TEST_CASE("pointer places caret and captured drag extends selection") {
      UITextInputPointerFixture f(u8"ab אב 12");
      const auto down = f.PointerDownAtVisualStop(2, 60);
      CHECK(f.Handle(down).consumed);
      CHECK(f.Caret() == f.ExpectedStop(2));
      CHECK(f.HasSelectionCapture());
      f.Handle(f.PointerMoveOutsideViewportToVisualEnd(61));
      CHECK(f.Selection() == f.RangeFromAnchorToVisualEnd());
      CHECK(f.Handle(f.PointerUpOutsideViewport(62)).consumed);
      CHECK_FALSE(f.HasSelectionCapture());
  }
  ```

  Add exact LTR/RTL midpoint ties using `TextHitTesting::HitTest` (`ltr boundary=1`, `rtl boundary=0`), nested-clip rejection, right/bottom-exclusive edge rejection, capture drag clamping to first/last visual stop, shift-click extension, and component/window replacement during drag. Pointer handling uses `planned.TargetFor(TextInput)`, never the button action or sibling selectable.

- [ ] **Step 3d: Write failing owner-transition composition cancellation tests.**

  Start composition on runtime A, then test A→B, A→editor denied, A release, and native window loss. Require A→B/release/loss to preserve A's committed value/caret, clear only A's composition once, advance semantic dirty only when composition was visible, and leave B untouched. Denied/no-change/editor-only transitions do not cancel engine composition. After one `Applied` boundary, feed same-boundary `NoChange`, `Rejected`, and a nonpublishing `PlatformFailed` row; all are validated ordinary no-ops, emit zero diagnostic, and do not replace the latest applied value. Replay the exact boundary-publishing transition and require an idempotent no-op; a nonidentical equal boundary with `boundaryPublished=true` is invalid. The system retains only the latest published transition value per window, not history.

- [ ] **Step 3e: Write failing arbiter injection/shared-service call-site tests.**

  Compile `UISystem::ProcessFrame(World&, const UIFrameInput&, TextInputArbiter&, TextLayoutService&, TextDiagnosticSink&)` and reject detection of the earlier four-argument overload without the arbiter. A recording fixture must observe the exact host arbiter and unchanged `textRenderer.LayoutService()` reference passed through Game View and standalone runtime into `UITextInputSystem::HandleEvent`; `CollectRender` must observe the same diagnostic sink. Build `test_game_view` and `molga_runtime` in this task's red/green gates so no call site lags the signature. Task 14 adds only the arbiter argument; it never replaces or clones the Task 12 layout-service authority.

- [ ] **Step 3f: Write failing public value/callback facade tests.**

  Test `GetValue` initialization, `SetValue` applied/unchanged/max-grapheme rejection, stale/wrong-world/replaced/disabled identity rejection, composition clearing, and explicit `Submit`. Register value/submitted callbacks, replace the target inside the first callback, and require at-most-once delivery to the old identity with no transfer. Serialize after runtime calls and require authored `initialText` unchanged with no value/caret/callback keys.

- [ ] **Step 4: Run the editing red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_text_input test_game_view \
    molga_runtime -j
  ```

  Expected: compilation fails because runtime edit state, final injected-service facade, and one-event editing do not exist.

- [ ] **Step 5: Define runtime-only state and one-event API.**

  ```cpp
  namespace molga::ui {
  struct UITextEditState {
      std::string value;
      molga::text::CaretPosition caret;
      molga::text::GraphemeRange selection;
      std::string compositionUtf8;
      molga::text::GraphemeRange compositionSelection;
      std::uint64_t stateRevision = 1;
  };
  struct UITextInputSurfaceState {
      bool focused = false;
      bool caretVisible = true;
      molga::Fixed26_6 blinkElapsed = molga::Fixed26_6::FromRaw(0);
      std::optional<molga::text::CaretPosition> dragAnchor;
      std::uint64_t stateRevision = 1;
  };

  enum class UITextInputMutationStatus : std::uint8_t {
      Applied, Unchanged, TargetInvalid, ReadOnly,
      MaxGraphemesExceeded, GenerationExhausted
  };
  struct UITextInputTickMutation {
      bool visualDirty = false;
  };
  using UITextValueChangedCallback = std::function<void(
      UIRuntimeTargetIdentity, std::string)>;
  using UITextSubmittedCallback = std::function<void(
      UIRuntimeTargetIdentity, std::string)>;

  class UITextInputSystem final : public UITextInputVisualStateProvider {
  public:
      UIEventHandlerResult HandleEvent(
          World&, const UISnapshot&, const PlannedUIEvent&,
          UIFocusSystem&, molga::platform::TextInputArbiter&,
          molga::text::TextLayoutService&,
          molga::text::TextDiagnosticSink&);
      void SynchronizeAuthoredInputs(
          World&, molga::WindowId, const UIFocusSystem&,
          const UIRuntimeGenerationSnapshot&,
          molga::text::TextDiagnosticSink&);
      UIEventHandlerResult ApplyOwnerTransition(
          World&, const PlannedUIEvent&,
          const molga::platform::TextInputOwnerTransition&,
          molga::text::TextDiagnosticSink&);
      UITextInputMutationStatus ApplyOwnerTransition(
          World&, const molga::platform::TextInputOwnerTransition&,
          molga::text::TextDiagnosticSink&);
      UITextInputTickMutation AdvanceTick(
          World&, const UISnapshot&, const UIDeterministicTick&,
          molga::text::TextDiagnosticSink&);
      std::optional<UITextInputVisualState> GetVisualState(
          molga::WindowId,
          const UIRuntimeTargetIdentity&) const override;
      std::optional<std::string> GetValue(
          World&, const UIRuntimeTargetIdentity&);
      UITextInputMutationStatus SetValue(
          World&, const UIRuntimeTargetIdentity&, std::string_view,
          molga::text::TextDiagnosticSink&);
      UITextInputMutationStatus Submit(
          World&, const UIRuntimeTargetIdentity&,
          molga::text::TextDiagnosticSink&);
      bool SetValueChangedCallback(
          World&, const UIRuntimeTargetIdentity&,
          UITextValueChangedCallback);
      bool SetSubmittedCallback(
          World&, const UIRuntimeTargetIdentity&,
          UITextSubmittedCallback);
      const UITextEditState* State(const UIRuntimeTargetIdentity&) const;
      void OnWorldReleased(std::uint64_t worldGeneration);
  };
  } // namespace molga::ui
  ```

  Edit/callback tables use complete input identity; focus/caret-blink/selection-capture projections additionally key by `{windowId,worldGeneration}`. `SynchronizeAuthoredInputs` compares the non-wrapping semantic generation with its last synchronized value before traversing; on a match it performs no traversal/allocation. On a changed generation it initializes newly active full identities from authored `initialText`, removes stale identities, and mirrors exact surface focus into the surface table before N/N+1 build. `GetVisualState(windowId,input)` composes those tables into a value copy with `surfaceWindowId` and exposes no reference/pointer. `OnWorldReleased` erases edit/callback/selection-capture/surface-focus state plus every latest-applied transition key for that generation.

- [ ] **Step 5b: Install the final explicit `UISystem` service facade.**

  ```cpp
  class UISystem {
  public:
      static UISystem& Get();
      molga::ui::UIFrameResult ProcessFrame(
          World&, const molga::ui::UIFrameInput&,
          molga::platform::TextInputArbiter&,
          molga::text::TextLayoutService&,
          molga::text::TextDiagnosticSink&);
      void CollectRender(const molga::ui::UISnapshot&,
                         const molga::ui::UIPhysicalTransform&,
                         molga::RenderQueue&, TextRenderer&,
                         molga::text::TextDiagnosticSink&);
      void OnWorldReleased(std::uint64_t worldGeneration);
      std::optional<std::string> GetValue(
          World&, const molga::ui::UIRuntimeTargetIdentity&);
      molga::ui::UITextInputMutationStatus SetValue(
          World&, const molga::ui::UIRuntimeTargetIdentity&,
          std::string_view, molga::text::TextDiagnosticSink&);
      molga::ui::UITextInputMutationStatus Submit(
          World&, const molga::ui::UIRuntimeTargetIdentity&,
          molga::text::TextDiagnosticSink&);
      molga::ui::UITextInputMutationStatus ApplyTextInputOwnerTransition(
          World&, const molga::platform::TextInputOwnerTransition&,
          molga::text::TextDiagnosticSink&);
      bool SetValueChangedCallback(
          World&, const molga::ui::UIRuntimeTargetIdentity&,
          molga::ui::UITextValueChangedCallback);
      bool SetSubmittedCallback(
          World&, const molga::ui::UIRuntimeTargetIdentity&,
          molga::ui::UITextSubmittedCallback);
  };
  ```

  `UISystem` forwards these public calls, including immediate caller-owned transition application, to its one `UITextInputSystem`. Replace the earlier `ProcessFrame(...,layoutService,sink)` declaration by inserting the arbiter argument and update Game View/runtime/tests in this same commit; retain no overload that manufactures an arbiter/layout service/sink. Before each possible N/N+1 build call `SynchronizeAuthoredInputs` with the current generation snapshot; its unchanged check preserves the upstream allocation-free fast path. Pass `textInputSystem_` as the exact `UITextInputVisualStateProvider&` and the unchanged `textRenderer.LayoutService()` reference into every `UILayoutSystem::Build`.

- [ ] **Step 6a: Initialize and rebuild runtime Unicode state.**

  Initialize `UITextEditState::value` from authored `initialText` on first full-identity access. Rebuild `UnicodeTextBuffer` after each accepted mutation and store selection as a half-open logical grapheme range.

- [ ] **Step 6b: Implement insertion and selected-range replacement.**

  Replace the selected logical grapheme range with input and place a downstream-affinity caret after the inserted range. Slice only through `UnicodeTextBuffer` logical grapheme boundaries.

- [ ] **Step 6c: Implement max-grapheme enforcement.**

  Count the full candidate's extended graphemes and reject the complete edit with `MaxGraphemesExceeded` when it exceeds nonzero authored `maxGraphemes`; leave value/selection/caret unchanged. Milestone A `UITextInputContentPolicy::Any` performs no character filtering and does not imply truncation.

- [ ] **Step 6d: Implement Backspace/Delete on logical boundaries.**

  Backspace deletes the grapheme before the logical caret boundary; Delete removes the grapheme after it. A nonempty selection is removed first. Neither path slices a scalar/combining/ZWJ sequence.

- [ ] **Step 6e: Implement visual Left/Right and line Home/End.**

  Use immutable layout visual stops and affinity. Before each visual key, compose
  the current per-event edit value and obtain/reuse an immutable layout keyed by
  that value plus the frozen
  `UITextInputLabelSnapshot::effectiveInputRequestTemplate` and viewport
  constraints through the injected `TextLayoutService`. Thus a preceding
  commit/edit in the same ordered batch is visible to a later
  Left/Right/Home/End even though target, clip, and viewport geometry remain from
  snapshot N. Never use N's stale text layout merely because N owns targeting,
  and never publish/re-hit-test between events. Home/End chooses the first/last
  visual stop on the current line without changing subsequent logical
  insertion/deletion semantics.

- [ ] **Step 6f: Publish text-state invalidation before mutation.**

  For every accepted value/selection/composition/caret change that affects a snapshot, first check `UITextEditState::stateRevision` is neither zero nor `UINT64_MAX`, then acquire `UIRuntimeInvalidationClock::Advance(SemanticDirty)` and checked-increment that revision before replacing the edit state. Focus/blink/drag-visible mutations use the same rule on `UITextInputSurfaceState::stateRevision`. Either exhaustion leaves the old state visible, emits a blocker, and returns an ignored handler result; `GetVisualState` publishes these as separate `editRevision`/`surfaceRevision` fields so no zero/reused revision reaches the full key. Identical/no-op edits do not advance.

- [ ] **Step 6g: Implement the exact `Any`/`OnEnter` behavior.**

  Accept every valid UTF-8/grapheme input under `Any`. For a focused TextInput-stage `Key::Enter`, compute `submitChord = event.keyModifiers.control || event.keyModifiers.gui` from the copied event value. On active key-down, single-line input submits for every modifier combination; multiline input submits when `submitChord`, otherwise it inserts one logical `"\n"`. Shift/Alt alone do not change the multiline newline path. Submit never inserts text, newline never invokes the submit callback, and either accepted key-down is consumed. Consume the corresponding focused Enter key-up without editing or invoking either callback so it cannot leak to a parent action. Ctrl+Gui is one chord and invokes at most one callback. The explicit public `Submit` API invokes the submitted callback for either mode without synthesizing an event/consumption record. No current keyboard-state query or hidden alternate policy branch exists.

- [ ] **Step 6h: Implement public value/callback operations through full identity.**

  `GetValue`/callback registration re-resolve all four fields and require an active, enabled `UITextInput`. `SetValue` validates UTF-8/grapheme max before acquiring semantic dirty; on success it copies the value, clears composition, collapses selection/caret at the final grapheme boundary, then invokes a copied value callback with copied identity/string. `Submit` copies the current string/callback, re-resolves immediately before invocation, and dispatches at most once. None of these calls changes authored `initialText` or serialization.

- [ ] **Step 6i: Implement pointer caret placement and captured selection drag.**

  On matching-window pointer down, require `planned.TargetFor(TextInput)`, the frozen input viewport/text layout, and a point inside both logical rect and clip. Call `TextHitTesting::HitTest`; its existing half-open interval/midpoint rule is authoritative for LTR/RTL. Store a complete-identity drag anchor per `{windowId,worldGeneration}`, place/collapse or shift-extend selection, and consume. Matching captured move clamps outside coordinates to the first/last visual stop and extends from the logical anchor; up applies the final point then releases. Replacement/window loss clears capture without transferring it.

- [ ] **Step 6j: Advance caret blink only from deterministic ticks.**

  Use exact fixed `kCaretBlinkPeriodRaw=32` (0.5 seconds). For each accepted `UIDeterministicTick`, checked-add `deltaSeconds` to focused inputs, compute whole period crossings, retain the remainder, and toggle `caretVisible` only on an odd crossing count. Acquire `Advance(SemanticDirty)` before a visible toggle; no wall clock, float `dt`, or ImGui delta is read. Unfocused inputs reset to visible with zero elapsed through the same checked mutation rule.

- [ ] **Step 7a: Validate text against its immutable runtime-owner stamp.**

  For editing/commit, require payload presence, `ownerAtIngest.kind == RuntimeUITextInput`, a full stamped runtime identity, and exact equality between that identity and `planned.targetFromSnapshotN`. Reconstruct the old token value from the stamp and require:

  ```cpp
  const auto& stamp = planned.event.text->ownerAtIngest;
  const molga::platform::TextInputOwnerToken stampedToken{
      planned.event.windowId,
      stamp.generation,
      {stamp.kind, stamp.runtimeTarget}};
  arbiter.Accepts(stampedToken, planned.event.windowId,
                  planned.event.sequence, stamp)
  ```

  Do not require this token to remain current and do not substitute projected/current focus: the host may have ingested the event immediately before a pointer callback changed owner. Reject mismatched token/window/sequence/stamp without mutation. Immediately before changing `UITextEditState`, re-resolve all four fields of the stamped target; a removed/replaced target fails closed rather than receiving or transferring the text.

- [ ] **Step 7b: Convert SDL UTF-8-character ranges into grapheme-safe composition.**

  Build a `UnicodeTextBuffer` for `compositionUtf8` itself. Convert SDL `editingStartUtf8Characters`/`editingLengthUtf8Characters` from UTF-8-character units to composition scalar/original-byte boundaries, then clamp the start down and end up to extended-grapheme boundaries. Store the result as a composition-relative half-open `compositionSelection`; it is never indexed in committed text. Emit `TextInputRangeClamped` for any numeric or grapheme-boundary clamp. Commit replaces the logical committed selection, clears composition, and preserves already committed text on later owner failure.

- [ ] **Step 7c: Apply owner transitions immediately and idempotently.**

  Extend planning so an event-attached retired runtime owner is looked up only in snapshot N and copied to `ownerTransitionTargetFromSnapshotN`; `TargetFor(OwnerTransition)` returns it. `ApplyOwnerTransition` first requires an exact event-attached transition or an immediate caller-owned result from the just-completed arbiter call. Validate `NoChange`, `Rejected`, and `PlatformFailed`/`GenerationExhausted` with `boundaryPublished=false` as ordinary no-ops before consulting the replay ledger; they neither cancel composition nor replace the latest published value. For `boundaryPublished=true`, retain one latest complete transition per window: a lower boundary or field-identical equal-boundary value is a no-op; a nonidentical equal-boundary value is invalid and emits `ReferenceInvalid`; only a strictly greater boundary replaces it. When cancellation is true, re-resolve the retired runtime identity, acquire semantic dirty if its composition is nonempty, then clear only `compositionUtf8`/`compositionSelection`. Preserve committed value/caret/selection. Return an `OwnerTransition` stage result resolved to `planned.TargetFor(OwnerTransition)` for event-attached window loss; the accumulator validates it but keeps window-focus primary null.

- [ ] **Step 8: Merge one text result into the existing sole event loop.**

  At each current plan position, first merge `textInputSystem_.ApplyOwnerTransition(...)` only when `planned.surfaceEligible` and `planned.event.textInputOwnerTransition` exist, then merge `textInputSystem_.HandleEvent(...)`. A foreign-window transition remains targetless/ignored for this surface. For an owner change initiated inside the handler, call arbiter once and immediately apply its returned transition before the handler returns. Return `Edit`/`Submit`, consumed/delivered, and dirty flags through `UIEventHandlerResult`; do not mutate the shared record directly or add another vector loop. Native window focus loss uses only its attached transition—no second release/listener/queue—while panel pointer departure does neither. In the separate validated tick loop, call `textInputSystem_.AdvanceTick` with every same value already given to scroll.

- [ ] **Step 9a: Copy callback state before invocation.**

  Copy `valueChanged`/`submitted` callable state and callback arguments before invocation so mutation cannot invalidate the callable storage.

- [ ] **Step 9b: Re-resolve identity and dispatch at most once.**

  Re-resolve all four identity fields immediately before the callback. If it is stale, skip. If the callback replaces the component or world, stop processing that target; the new instance never inherits state. Runtime value stays out of serialization.

- [ ] **Step 10: Run editing, arbiter, focus, and serialization gates.**

  ```bash
  cmake --build --preset debug --target test_ui_text_input \
    test_text_input_arbiter test_ui_focus test_ui_input test_scene_serializer \
    test_game_view molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_text_input|test_text_input_arbiter|test_ui_focus|test_ui_input|test_scene_serializer|test_game_view|runtime_smoke)$' --output-on-failure
  ```

  Expected: all selected tests pass, including exact mixed key/edit/commit order, injected production services, public value/callback APIs, and transition cancellation.

- [ ] **Step 11: Commit grapheme-safe editing.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UITextInputSystem.* \
    src/UI/UISystem.* src/UI/UIFocusSystem.cpp src/UI/UIInputRouter.cpp \
    src/ECS/Components/UITextInput.* src/Editor/Windows/GameViewWindow.* \
    src/runtime_main.cpp tests/test_ui_text_input.cpp tests/test_game_view.cpp
  git commit -m "feat: add grapheme-safe UI text editing"
  ```

### Task 14.2: Publish visible selection, text, composition, and caret render items

**Files:**

- Modify: `src/UI/UITextInputSystem.h`
- Modify: `src/UI/UITextInputSystem.cpp`
- Modify: `src/UI/UILayoutSnapshot.h`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UIRenderCollector.cpp`
- Modify: `tests/test_ui_text_input.cpp`
- Modify: `tests/test_ui_render_clip.cpp`

**Interfaces:**

- Consumes: value-only `UITextInputVisualStateProvider`, frozen input-label templates/ownership, immutable text layout/hit-testing, input viewport clip/order, deterministic blink state, and concrete `UISolidRectSnapshot`/`UITextSnapshot` payloads.
- Produces: immutable visible input render items in conditional layer order: optional committed selection (only with no active composition) → complete glyph-command span → optional composition underline → optional focused/blink-visible caret, fixed placeholder behavior, and blink-independent `UITextInputImeGeometrySnapshot`.

  Use this transient mapping contract while building visuals:

  ```cpp
  struct UITextVisibleBoundaryMap {
      std::vector<std::uint32_t> committedToVisible;
      std::vector<std::uint32_t> compositionToVisible;
      molga::text::GraphemeRange compositionVisibleRange;
  };
  ```

- [ ] **Step 1a: Write failing concrete render-item assertions.**

  ```cpp
  TEST_CASE("focused selected input publishes selection text and caret") {
      UITextInputRenderFixture f(u8"abc");
      f.Select(0, 1);
      const auto snapshot = f.BuildRenderSnapshot();
      const auto items = f.InputRenderItems(*snapshot);
      REQUIRE(items.size() == 3);
      CHECK(f.Kind(items[0]) == "solid-selection");
      CHECK(f.Kind(items[1]) == "text");
      CHECK(f.Kind(items[2]) == "solid-caret");
      for (const auto* item : items) CHECK(item->logicalClip == items[0]->logicalClip);
  }

  TEST_CASE("active composition suppresses replaced committed selection") {
      UITextInputRenderFixture f(u8"abc");
      f.Select(0, 1);
      f.Compose(u8"한", 1, 1);
      const auto snapshot = f.BuildRenderSnapshot();
      const auto items = f.InputRenderItems(*snapshot);
      REQUIRE(items.size() == 3);
      CHECK(f.SelectionItems(*snapshot).empty());
      CHECK(f.Kind(items[0]) == "text");
      CHECK(f.Kind(items[1]) == "solid-composition-underline");
      CHECK(f.Kind(items[2]) == "solid-caret");
      for (const auto* item : items) CHECK(item->logicalClip == items[0]->logicalClip);
  }
  ```

- [ ] **Step 1b: Add failing geometry, placeholder, and blink assertions.**

  Add these exact focused assertions:

  ```cpp
  CHECK(f.BidiSelectionItems().size() == f.Layout().SelectionRects({1, 5}).size());
  CHECK(f.CompositionUnderlineItems().size() ==
        f.Layout().VisualRunsIntersectingComposition().size());
  CHECK(f.WithEmptyNestedClip().InputRenderItems().empty());
  CHECK(f.EmptyUnfocused().PlaceholderVisible());
  CHECK_FALSE(f.EmptyFocused().PlaceholderVisible());
  CHECK_FALSE(f.NonEmptyUnfocused().PlaceholderVisible());
  CHECK(f.CaretItems(*f.NonEmptyUnfocused().BuildRenderSnapshot()).empty());
  const auto shapesBeforeBlink = f.ShapeCount();
  f.AdvanceBlinkPeriod();
  CHECK(f.ShapeCount() == shapesBeforeBlink);
  ```

- [ ] **Step 1c: Write exact combining/emoji visible-caret mapping assertions.**

  ```cpp
  TEST_CASE("composition caret and underline use visible boundary mapping") {
      UITextInputRenderFixture f("AB");
      f.PlaceCaretAfterGrapheme(1);
      f.ComposeFromSdl(u8"e\u0301🙂", 1, 1); // clamps to composition [0,1)
      const auto visual = f.BuildVisibleInput();
      CHECK((visual.boundaries.compositionVisibleRange == GraphemeRange{1, 3}));
      CHECK(visual.visibleCaret.boundary == 2); // insertion 1 + comp boundary 1
      CHECK(visual.visibleCaret.affinity == CaretAffinity::Downstream);
      CHECK((visual.underlineLogicalRange == GraphemeRange{1, 3}));
      CHECK(visual.layout->SourceGraphemeAt(visual.visibleCaret.boundary) !=
            SourceGraphemeKind::Interior);
  }
  ```

- [ ] **Step 1d: Write failing conditional glyph-span/solid ordering assertions.**

  First build no composition with one selection rect, three drawable glyph/tofu commands, and one caret; require bases/spans `{selection 10/1, text 11/3, caret 14/1}` and final command indices `{10,11,12,13,14}`. Then activate a composition that replaces that selection and produces two underline rects; require no selection item, bases/spans `{text 10/3, underline 13/1, underline 14/1, caret 15/1}`, and final indices `{10,11,12,13,14,15}`. Add checked overflow at `UINT64_MAX-1` and require the complete input visual group omitted with one `LayoutInvalid`.

- [ ] **Step 1e: Write failing blink-off immutable IME geometry assertions.**

  ```cpp
  TEST_CASE("IME geometry remains in the snapshot while caret blink is off") {
      UITextInputRenderFixture f("abc");
      f.FocusAndPlaceCaret(2);
      const auto on = f.BuildRenderSnapshot();
      f.AdvanceTicksToBlinkOff();
      const auto off = f.BuildRenderSnapshot();
      CHECK(f.CaretItems(*on).size() == 1);
      CHECK(f.CaretItems(*off).empty());
      CHECK(off->textInputImeGeometry == on->textInputImeGeometry);
      CHECK(off->textInputImeGeometry[0].logicalCursor ==
            f.ExpectedCaretLogicalPoint(2));
  }
  ```

  Mutate provider state after each build and require both snapshot values unchanged. Add clipped-caret and composition-cursor rows; IME cursor geometry uses the mapped visible caret even when its draw item is absent. Add `focused=false, caretVisible=true` and require zero caret render items; the stored blink reset must never make an unfocused caret drawable.

- [ ] **Step 2: Run the visible-input red gate.**

  ```bash
  cmake --build --preset debug --target test_ui_text_input test_ui_render_clip -j
  ```

  Expected: assertions fail because edit state is not materialized into snapshot render records.

- [ ] **Step 3a: Compose visible UTF-8 without mutating committed state.**

  Replace the committed selection `[start,end)` in a temporary buffer with active `compositionUtf8`. Fill `committedToVisible` for prefix/suffix boundaries, collapse committed boundaries inside the replaced selection to the insertion boundary, fill `compositionToVisible[i] = insertionVisibleBoundary + i`, and store the half-open full composition range. Never mutate committed state.

- [ ] **Step 3b: Build and retain one immutable visible layout.**

  During `UILayoutSystem::Build`, call `provider.GetVisualState(surfaceWindowId,inputIdentity)` and copy the result. Copy `UITextInputLabelSnapshot::effectiveInputRequestTemplate`, replace only `utf8` with the composed visible value, call the injected renderer-owned `TextLayoutService` once for the frozen viewport constraints, and store the shared immutable layout in `UITextSnapshot`. That direct service call retains Task 8's per-call font-artifact-store gate at the service/repository edge; it cannot bypass the check through a `TextRenderer::Layout`-only wrapper. The effective template uses the input-owned top-level family/paragraph style; rendered-label font/style is provenance only, while its color and viewport constraints remain frozen presentation data. Retain the effective request identity so caret/selection/focus/blink-only state reuses layout, while visible text participates in the geometry key. Never mutate the provider, `UILabel`, or authored initial text. A full fast-path hit returns before provider lookup.

- [ ] **Step 4a: Map committed selection or composition cursor into visible boundaries.**

  With no active composition, map committed selection endpoints and committed caret through `committedToVisible`, preserving caret affinity. With active composition, do not pass the committed caret directly to visible layout: map `compositionSelection.end` through `compositionToVisible` and use downstream affinity for the visible composition cursor; map the full composition range for underlining.

- [ ] **Step 4b: Materialize selection and caret geometry from visible positions.**

  Use `TextHitTesting::SelectionRects` with mapped visible selection and `CaretRects` with mapped visible caret. When composition is active, the replaced committed selection is not separately highlighted.

- [ ] **Step 4c: Materialize composition underlines and apply the shared clip.**

  Generate composition underline rectangles for every intersected visual run. Translate all selection/underline/caret rectangles by input origin, intersect with the already-computed input viewport/nested mask clip, and drop empty rectangles.

- [ ] **Step 5: Assign exact stable sub-order.**

  Reserve consecutive command ranges under the input's `UIDrawOrderKey`: each selection solid reserves one; the text item reserves exactly `TextRenderCommandSpan(visibleLayout)`; every composition underline reserves one; a caret reserves one only when `focused && caretVisible` and its clipped rectangle is nonempty. The first underline base is therefore `textBase+text.reservedCommandSpan`, never `textBase+1`. Use checked addition and omit the complete input group on overflow. Each item carries the same input source/canonical identity and logical clip, while the frozen label identities remain provenance in `UITextInputLabelSnapshot`.

- [ ] **Step 6a: Implement fixed placeholder visibility.**

  Show the frozen authored placeholder label only when committed value and composition are empty, the input is unfocused, and its frozen label visual is enabled/visible. Use its stored request/color; do not read or emit the ordinary `UILabel` and do not add a placeholder policy field. Conflicted/invalid/disabled claims were already omitted by Task 11.1.

- [ ] **Step 6b: Implement allocation-free caret blink invalidation.**

  Consume both `focused` and `caretVisible` from the copied provider value. Emit
  a caret item exactly when `focused && caretVisible` and the clipped caret rect
  is nonempty; an unfocused input emits no caret even though its stored blink
  state resets to visible. Task 14.1's deterministic tick path already acquires
  `Advance(SemanticDirty)` before toggling it. A blink-only snapshot rebuild
  toggles only caret item presence, reuses the immutable text layout with no
  ICU/HarfBuzz call, and always republishes the same value-owned
  `UITextInputImeGeometrySnapshot` viewport/cursor/clip for a focused input.

- [ ] **Step 7: Use the one allowed N+1 publication.**

  An edit marks visual/intrinsic/layout dirtiness as required and contributes visible items only to the final render snapshot. It never mutates interaction snapshot N or requests a second same-frame reflow.

- [ ] **Step 8: Run visible text, clip, and text-cache gates.**

  ```bash
  cmake --build --preset debug --target test_ui_text_input \
    test_ui_render_clip test_text_cache -j
  ctest --test-dir build/debug -R '^(test_ui_text_input|test_ui_render_clip|test_text_cache)$' --output-on-failure
  ```

  Expected: all selected tests pass; every input visual is explicit immutable render data under one clip.

- [ ] **Step 9: Commit visible input snapshots.**

  ```bash
  git add src/UI/UITextInputSystem.* src/UI/UILayoutSnapshot.h \
    src/UI/UILayoutSystem.cpp src/UI/UIRenderCollector.cpp \
    tests/test_ui_text_input.cpp tests/test_ui_render_clip.cpp
  git commit -m "feat: publish visible UI text input layers"
  ```

### Task 14.3: Map Game View IME areas and enforce runtime/editor owner lifecycle

**Files:**

- Create: `src/Editor/GameViewTextInputMapping.h`
- Create: `src/Editor/GameViewTextInputMapping.cpp`
- Create: `tests/test_ui_game_view_ime.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Editor/Editor.h`
- Modify: `src/Editor/Editor.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `tests/test_game_view.cpp`
- Modify: `tests/test_text_input_arbiter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `UIPhysicalTransform`, the exact frozen `UISurfaceCoordinateMapping`, immutable `UITextInputImeGeometrySnapshot`, output presentation layout, framebuffer backing scale/origin, focused input identity, returned `TextInputOwnerTransition`, and the one host arbiter.
- Produces: one `GameViewTextInputMapping::Map` function that delegates logical-to-window conversion to the shared surface mapper, real `World*` Game View binding, blink-independent input area/cursor offset, and immediate shared runtime/editor owner-transition lifecycle.

- [ ] **Step 1a: Write a hand-calculated mapping fixture with declared units.**

  ```cpp
  TEST_CASE("Game View maps canvas units through framebuffer into window pixels") {
      GameViewTextInputMappingInput input;
      input.canvasLogicalInputArea = FixedRectUnits(10, 20, 100, 24);
      input.canvasLogicalCursor = FixedPointUnits(45, 20);
      input.canvasLogicalViewport = FixedRectUnits(0, 0, 320, 180);
      input.outputPresentationFramebufferPixels =
          IntegerFitPresentation({320, 180}, {800, 600});
      input.imageOriginWindowPoints = {50.0f, 30.0f};
      input.windowPixelsPerPointX = 2.0;
      input.windowPixelsPerPointY = 2.0;
      input.windowPixelsPerOutputFramebufferPixelX = 1.0;
      input.windowPixelsPerOutputFramebufferPixelY = 1.0;
      input.sdlWindowBoundsPixels = {0, 0, 1600, 1000};
      input.surfaceMapping = FrozenSurfaceMappingForPresentedInput(input);
      const auto mapped = GameViewTextInputMapping::Map(input);
      REQUIRE(mapped);
      // Hand calculation:
      // presentation content = (80,120,640,360) framebuffer pixels;
      // logical rect -> (100,160)-(300,208) framebuffer pixels;
      // image origin = (100,60) window pixels; final rect=(200,220,200,48);
      // logical cursor x=45 -> framebuffer x=170 -> window x=270;
      // SDL cursorOffset is 270 - rect.x(200) = 70 window pixels.
      CHECK((mapped->area.rect == molga::PixelRectU32{200, 220, 200, 48}));
      CHECK(mapped->area.cursorOffset == 70);
  }
  ```

  Expected values are the independent arithmetic recorded in the comment, not values returned by a second production helper.

- [ ] **Step 1b: Add scale, letterbox, crop, detached-origin, and invalid cases.**

  ```cpp
  const std::vector<MappingCase> cases{
      {"scale-1", NativeMapping(1.0, {10, 20}), PixelRect(20, 40, 100, 24), 35},
      {"scale-2", NativeMapping(2.0, {10, 20}), PixelRect(30, 60, 100, 24), 35},
      {"detached-origin", NativeMapping(2.0, {40, 25}), PixelRect(90, 70, 100, 24), 35},
  };
  for (const auto& c : cases) {
      const auto mapped = GameViewTextInputMapping::Map(c.input);
      REQUIRE_MESSAGE(mapped, c.name);
      CHECK(mapped->area.rect == c.expectedRect);
      CHECK(mapped->area.cursorOffset == c.expectedCursorOffset);
  }
  CHECK_FALSE(GameViewTextInputMapping::Map(EmptyPresentationInput()));
  CHECK_FALSE(GameViewTextInputMapping::Map(NonFiniteScaleInput()));
  CHECK(GameViewTextInputMapping::Map(CroppedInput())->area.rect ==
        PixelRect(0, 0, 40, 24));
  ```

  `NativeMapping` uses logical viewport `320x180`, native output presentation `320x180`, logical input `(10,20,100,24)`, logical cursor X `45`, and one window pixel per output framebuffer pixel. It also freezes `surfaceMapping` from those presentation fields without performing input-area conversion. Its `imageOriginWindowPoints` argument is already window-local; detached setup computes it as `imageScreenOrigin - ImGuiViewport::Pos` before calling `Map`.

- [ ] **Step 2: Write failing lifecycle tests.**

  ```cpp
  TEST_CASE("Game View owner follows keyboard focus and full identity") {
      GameViewImeFixture f;
      CHECK_FALSE(f.UpdateOwner().has_value());
      f.FocusPanelAndInput();
      const auto token = f.UpdateOwner();
      REQUIRE(token);
      f.PointerLeavesPresentation();
      CHECK(f.UpdateOwner()->generation == token->generation);
      const auto stampedBeforeReplacement = f.IngestCommit("old-owner");
      f.ReplaceInputComponent();
      CHECK_FALSE(f.UpdateOwner());
      CHECK(f.Arbiter().Accepts(*token, stampedBeforeReplacement));
      CHECK_FALSE(f.DispatchStampedText(stampedBeforeReplacement));
      CHECK(f.ReplacementValue().empty());
      f.FocusPanelAndInput();
      f.BeginComposition(u8"한");
      f.NativeWindowFocusLost();
      CHECK_FALSE(f.CurrentOwner());
      CHECK(f.Composition().empty());
      CHECK_FALSE(f.PointerCapture());
  }
  ```

  `Accepts` proves only that the immutable event stamp matches the old token. The dispatch assertion proves the separate safety boundary: full-identity re-resolution rejects the removed target and never transfers already-ingested text to its replacement.

- [ ] **Step 2b: Write a failing editor-to-authorized-runtime transition test.**

  ```cpp
  TEST_CASE("Game View runtime replaces editor only with verified keyboard focus") {
      GameViewImeFixture f;
      const auto editor = f.ShowEditorIme();
      REQUIRE(editor);
      f.ClearTextPlatformCalls();
      f.SetPanelKeyboardFocus(false);
      CHECK_FALSE(f.UpdateOwner());
      CHECK(f.CurrentOwner() == editor);
      CHECK(f.TextPlatformCalls().empty());
      f.SetPanelKeyboardFocus(true);
      f.FocusRuntimeInput();
      const auto runtime = f.UpdateOwner();
      REQUIRE(runtime);
      CHECK(runtime->owner.kind == TextInputOwnerKind::RuntimeUITextInput);
      CHECK((f.TextPlatformCalls() == std::vector<std::string>{
          "Clear", "Stop", "Start", "SetArea"}));
  }
  ```

  `UpdateOwner` performs focus/full-identity validation and passes `RuntimeGameViewKeyboardFocused` in the same `SetOwner` request. It never calls a separate arbiter priority precheck.

- [ ] **Step 2c: Write a failing blink-off snapshot mapping test.**

  Focus one input, capture its published `UITextInputImeGeometrySnapshot`, advance deterministic ticks until the caret draw item disappears, and map the new snapshot. Require identical SDL area/cursor offset and no live `UITextEditState`, UILabel, or `TextHitTesting` read from `GameViewWindow`.

- [ ] **Step 3: Run the Game View IME red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_game_view_ime test_game_view \
    test_text_input_arbiter -j
  ```

  Expected: compilation fails because the single mapping function and real-world binding do not exist.

- [ ] **Step 4: Define one mapping input/output contract.**

  ```cpp
  struct GameViewTextInputMappingInput {
      // Signed 26.6 Canvas logical UI units.
      molga::FixedRect canvasLogicalInputArea;
      molga::FixedPoint canvasLogicalCursor;
      molga::FixedRect canvasLogicalViewport;
      // contentRect/framebufferSize are output-framebuffer pixels.
      molga::OutputPresentationLayout outputPresentationFramebufferPixels;
      // SDL-window-local logical points, not desktop/screen points.
      molga::GameViewPoint imageOriginWindowPoints;
      // Physical SDL window pixels per one window logical point.
      double windowPixelsPerPointX = 1.0;
      double windowPixelsPerPointY = 1.0;
      // Physical SDL window pixels occupied by one output framebuffer pixel.
      double windowPixelsPerOutputFramebufferPixelX = 1.0;
      double windowPixelsPerOutputFramebufferPixelY = 1.0;
      // SDL window-local physical pixel bounds.
      molga::PixelRectU32 sdlWindowBoundsPixels;
      // Exact value already used to map this surface's native pointer batch.
      molga::ui::UISurfaceCoordinateMapping surfaceMapping;
  };
  struct GameViewTextInputMappingResult {
      molga::platform::TextInputArea area;
  };
  class GameViewTextInputMapping {
  public:
      static std::optional<GameViewTextInputMappingResult> Map(
          const GameViewTextInputMappingInput&);
  };
  ```

- [ ] **Step 5a: Map Canvas logical edges to output framebuffer pixels.**

  Require every explicit presentation/origin/scale/bounds field to agree with
  `surfaceMapping`; disagreement fails instead of choosing one authority. Use
  `surfaceMapping.CanvasRectToWindowPixels` and
  `CanvasCursorXToWindowPixel` for the actual conversion, so pointer hit mapping
  and IME placement are exact inverses of one frozen surface value. The mapper
  applies floor to rect min and ceil to rect max only after checked rational
  Canvas-to-output mapping.

- [ ] **Step 5b: Validate the already-frozen window mapping authority.**

  At the surface-mapping construction boundary, set window-pixel output origin
  from `imageOriginWindowPoints * windowPixelsPerPoint` and set its per-output-
  pixel scales from the frozen presentation. `GameViewTextInputMapping::Map`
  only validates that those derived values and `sdlWindowBoundsPixels` agree
  with `surfaceMapping`; it does not repeat the arithmetic after Step 5a.
  `CanvasRectToWindowPixels` owns outward rounding and bounds clamping. Reject
  non-finite/non-positive scale, empty presentation/result, authority mismatch,
  or overflow.

- [ ] **Step 5c: Compute SDL cursor offset from the final window rect.**

  Compute cursor X in SDL window-coordinate pixels, then set `cursorOffset = clamp(cursorWindowPixelX - finalRect.x, 0, finalRect.width)`. This is a rect.x-relative window-coordinate pixel offset per the SDL contract; it is not a UTF-8 index and not a desktop coordinate.

- [ ] **Step 6a: Preserve Game View's real-world binding.** Task 10.2 already
  replaced object-vector-only storage with `World*`. Reuse that exact authority
  for IME target validation and keep camera/output traversal limited to
  `world.Objects()`; do not add a second world pointer or compatibility path.

- [ ] **Step 6b: Pass the one host arbiter to editor and runtime UI.**

  Editor and standalone runtime both receive `EngineHost::TextInput()` and pass it plus the shared `TextLayoutService`/diagnostic sink into the final `UISystem::ProcessFrame` signature. Game View, editor, and runtime may store tokens but may not own an arbiter or layout service clone.

- [ ] **Step 7a: Acquire or retain runtime ownership at frame start.**

  Re-resolve the focused identity and acquire or idempotently retain runtime ownership only when native window and panel keyboard focus are valid. Pass `RuntimeGameViewKeyboardFocused` in that one `SetOwner` request; standalone runtime passes `RuntimeStandaloneWindowFocused` only after native-window focus validation. Immediately pass the returned transition to `UISystem::ApplyTextInputOwnerTransition` before using `transition.current`; a valid runtime request may transition an editor owner through arbiter policy. Call `SetArea` only when the transition is accepted/current matches and the mapped immutable snapshot area changed; immediately consume that returned area transition too.

- [ ] **Step 7b: Revalidate ownership after each callback.**

  Re-resolve all four fields. Call `Release` for an old token if the target was removed/replaced/disabled and immediately apply its returned transition; never transfer it to the new instance.

- [ ] **Step 7c: Separate pointer departure from native focus loss.**

  On pointer departure call `ClearPointerState(windowId,worldGeneration)` only. Native focus loss is already represented by the same ordered `NativeInputEvent::textInputOwnerTransition`; `UISystem` applies it once, clears matching pressed/capture, and cancels runtime composition from the transition flag. Game View must not call `Release`/`OnWindowFocusLost` again or install a second listener.

- [ ] **Step 8: Run mapping, Game View, arbiter, and runtime gates.**

  ```bash
  cmake --build --preset debug --target test_ui_game_view_ime test_game_view \
    test_text_input_arbiter test_ui_text_input molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_game_view_ime|test_game_view|test_text_input_arbiter|test_ui_text_input|runtime_smoke)$' --output-on-failure
  ```

  Expected: all selected tests pass, including detached-window and focus-loss ownership audits.

- [ ] **Step 9: Commit Game View IME mapping and lifecycle.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Editor/GameViewTextInputMapping.* \
    src/Editor/Windows/GameViewWindow.* src/Editor/Editor.* \
    src/runtime_main.cpp src/UI/UISystem.* tests/test_ui_game_view_ime.cpp \
    tests/test_game_view.cpp tests/test_text_input_arbiter.cpp
  git commit -m "feat: map Game View text input ownership"
  ```

### Task 14.4: Build a stable internal accessibility tree without a native bridge

**Files:**

- Create: `src/UI/UIAccessibilityTree.h`
- Create: `src/UI/UIAccessibilityTree.cpp`
- Create: `tests/test_ui_accessibility.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: final immutable snapshot, authored `UIAccessibility` metadata, stable component keys, and focusable state.
- Produces: deterministic internal-only `UIAccessibilityTree::Build` and stable debug JSON.

- [ ] **Step 1: Write the failing semantic-tree test.**

  ```cpp
  TEST_CASE("semantic tree follows stable visible hierarchy order") {
      UIAccessibilityFixture f;
      const auto tree = f.Build();
      REQUIRE(tree.nodes.size() == 3);
      CHECK(tree.nodes[0].role == UIAccessibilityRole::Panel);
      CHECK(tree.nodes[1].name == "Play");
      CHECK(tree.nodes[1].focusable);
      CHECK(tree.nodes[2].name == "Player name");
      CHECK(tree.StableJson().find("runtimeTarget") == std::string::npos);
  }
  ```

  Add hidden metadata, inactive subtree, parent/child order, clipped-but-semantic content policy, stable rect, and different runtime identity/same canonical output cases.

- [ ] **Step 2: Run the semantic-tree red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_accessibility -j
  ```

  Expected: compilation fails because `UIAccessibilityTree` does not exist.

- [ ] **Step 3: Define stable internal records.**

  ```cpp
  struct UIAccessibilityNode {
      UIStableComponentKey source;
      UIAccessibilityRole role = UIAccessibilityRole::None;
      std::string name;
      std::string description;
      bool hidden = false;
      bool focusable = false;
      molga::FixedRect logicalRect;
      std::vector<std::uint32_t> children;
  };

  class UIAccessibilityTree {
  public:
      static UIAccessibilityTree Build(World&, const UISnapshot&);
      std::string StableJson() const;
      std::vector<UIAccessibilityNode> nodes;
  };
  ```

- [ ] **Step 4: Build from final snapshot order and authored metadata.**

  Traverse stable hierarchy order, use final snapshot rect/focusability, omit hidden/inactive descendants according to authored metadata, and project runtime identity to `UIStableComponentKey`. Retain no component pointers after build.

- [ ] **Step 5: Keep the feature internal-only.**

  Expose the tree to tests and debug inspection only. Add no Objective-C/macOS Accessibility call, entitlement, feature flag, or support claim.

- [ ] **Step 6: Run accessibility, snapshot, and serialization gates.**

  ```bash
  cmake --build --preset debug --target test_ui_accessibility \
    test_ui_snapshot test_scene_serializer -j
  ctest --test-dir build/debug -R '^(test_ui_accessibility|test_ui_snapshot|test_scene_serializer)$' --output-on-failure
  ```

  Expected: all selected tests pass with byte-stable internal semantic JSON.

- [ ] **Step 7: Commit internal semantics.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UIAccessibilityTree.* \
    tests/test_ui_accessibility.cpp
  git commit -m "feat: add internal UI semantic tree"
  ```

## Final Verification

- [ ] **Run the complete focused Debug boundary.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_input test_ui_focus \
    test_ui_scroll test_text_input_arbiter test_platform_sdl \
    test_imgui_sdlgpu test_ui_text_input test_ui_render_clip \
    test_ui_game_view_ime test_game_view test_ui_accessibility \
    test_scene_runtime molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_input|test_ui_focus|test_ui_scroll|test_text_input_arbiter|test_platform_sdl|test_imgui_sdlgpu|test_ui_text_input|test_ui_render_clip|test_ui_game_view_ime|test_game_view|test_ui_accessibility|test_scene_runtime|runtime_smoke)$' --output-on-failure
  ```

- [ ] **Run sanitizer interaction boundaries.**

  ```bash
  cmake --preset asan
  cmake --build --preset asan --target test_ui_input test_ui_focus \
    test_text_input_arbiter test_ui_text_input -j
  ctest --test-dir build/asan -R '^(test_ui_input|test_ui_focus|test_text_input_arbiter|test_ui_text_input)$' --output-on-failure
  cmake --preset ubsan
  cmake --build --preset ubsan --target test_ui_input test_ui_focus \
    test_text_input_arbiter test_ui_text_input -j
  ctest --test-dir build/ubsan -R '^(test_ui_input|test_ui_focus|test_text_input_arbiter|test_ui_text_input)$' --output-on-failure
  ```

- [ ] **Run the complete Debug regression suite.**

  ```bash
  ctest --preset debug
  ```

- [ ] **Run single-loop, no-drain, and sole-SDL-owner source checks.**

  ```bash
  rg -n 'orderedEvents' src/UI
  rg -n 'Drain(Text|NativeText)|PendingTextEvents|vector<.*PlannedUIEvent' src/UI src/Platform src/Core
  rg -n '\bNativeInputEvents\(\)|eventObserver\(&event\)|function<void\(const void\*\)>' \
    src tests
  rg -n 'SDL_(StartTextInput|StopTextInput|ClearComposition|SetTextInputArea)' \
    src external/imgui/backends/imgui_impl_sdl3.cpp
  ```

  Expected: `orderedEvents` iteration appears only in `UISystem::ProcessFrame`; the drain/second-vector and old reusable-batch/one-argument observer searches have no production matches; compiled `src/` lifecycle calls appear only in `SdlTextInputPlatform.cpp`.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-20-ui-text/04-ui-interaction-ime.md`. After its Exit Contract passes, continue to the editor/package/evidence subplans; do not claim visible macOS IME qualification from these synthetic/hidden-window tests alone.
