# UI Layout and Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current float/recomputed UI path with versioned authoring components, process-safe runtime identities, deterministic signed-26.6 layout snapshots, shared render/hit payloads, nested rectangular clipping, and deterministic scrolling.

**Architecture:** Authored ECS components remain the serialized source of truth, while `UILayoutSystem` converts them into immutable semantic snapshots keyed by complete semantic and per-device binding dependencies. A monotonic aggregate dirty fast path returns the prior shared snapshot before constructing collision keys when world, viewport, authored/intrinsic state, scroll displacement, device, and runtime bindings are unchanged. Render collection and hit testing consume the same sorted snapshot records and the same already-intersected logical clip; physical conversion happens once at the final viewport edge. Scroll offset and velocity live in runtime identity-keyed tables and invalidate arrangement/render without contaminating serialization or text shaping.

**Tech Stack:** C++17, CMake 3.27 presets, doctest, nlohmann/json, SDL 3/SDL_GPU Metal, signed 26.6 fixed-point logical geometry.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md), especially Sections 7, 8.1, 8.2, 8.5, 9, 10, 11, and 13.

## Global Constraints

- The approved design is authoritative. Stop and obtain renewed approval before changing public behavior, failure policy, or milestone scope.
- Preserve unrelated worktree changes. Before each task run `git status --short --branch`; stage only that task's named files.
- This plan starts only after master Tasks 1-8 pass their focused gates. It consumes `molga::Fixed26_6`, `molga::text::TextLayout`, `TextLayoutService`, `TextRenderer::CollectLayout`, texture/glyph lifetime tokens, and typed `TextDiagnosticSink` records.
- `SceneObjectRef` preserves existing `ObjectRef.targetId`, `IsSet()`, and `Clear()` source and serialized compatibility. It has one storage member; no duplicate `objectId` field is allowed.
- Every live `World` generation comes from one process-global, monotonic, nonzero, never-reused `std::uint64_t` sequence. Construction, `Clear`, successful scene load, and move-based replacement invalidate earlier identities.
- Every `Component::GetInstanceID()` value comes from one checked process-global, monotonic, nonzero, never-reused `std::uint64_t` sequence. Construction, copy construction, and move construction acquire before publication; exhaustion leaves the destination/container unchanged and never wraps through zero.
- Every runtime UI identity is exactly `{worldGeneration, objectId, componentRuntimeTypeId, componentInstanceId}`. Never retain a raw component pointer across callbacks or snapshots.
- Scene/prefab JSON contains authored state only. Computed rects, intrinsic sizes, clips, revisions, focus, hover, capture, scroll state, edit state, caches, and GPU residency are runtime-only.
- All UI validation, measurement, arrangement, hit tests, clips, and snapshot comparisons use signed 26.6 logical units. Reject non-finite values and overflow; normalize signed zero to positive zero; never saturate or wrap.
- `UISnapshot` is semantic cached state. It contains no `frameIndex`, timestamp, physical pixel rect, pointer, or GPU command index. An unchanged frame reuses the identical `shared_ptr<const UISnapshot>`.
- Canonical snapshot identity contains texture GUID/full content SHA/content-derived stable ID but no process-local revision or handle. The runtime full-snapshot cache separately collision-compares device/upload generations, texture/sampler handles, and lifetime identity so same-content reupload/device recreation can reuse geometry without reusing stale GPU bindings.
- The aggregate semantic, viewport, scroll-displacement, texture-binding, and device generations are non-wrapping. `semanticDirtyGeneration` covers authored/intrinsic changes and any later runtime state that changes a published snapshot (hover/press/focus/edit/caret); it is not serialized. Exhaustion disables the affected fast path/cache and rebuilds fail-closed; it never lets an old generation identify new state.
- Device reset/shutdown clears the full-snapshot binding cache. After a successful GPU idle/fence drain, release engine-owned UI snapshots/cache lifetimes and glyph-page command owners before texture/device teardown; either a remaining external texture-binding lifetime or a live external glyph-page token prevents teardown and reports a blocker.
- `UIRenderItemSnapshot` and `UIHitTargetSnapshot` are concrete, sorted, immutable payloads. Both carry the same `UIDrawOrderKey` and the same already-intersected optional logical clip.
- A hit record freezes one source-compatible action target plus an optional sibling focus target, optional sibling text-input target, and ancestor scroll targets in inner-to-outer order as `UIFrozenTarget` values. Later routing never reconstructs those stage targets from ECS state.
- A `UITextInput` exclusively owns each valid referenced rendered/placeholder `UILabel` for snapshot output. Claimed labels never also emit ordinary `UILabel` render items; conflicts and invalid/disabled references follow the explicit fail-closed table in Task 11.1.
- A text-input visual group orders selection solids first, then the complete text glyph/tofu span, then composition underlines and the caret. Ordinary text and the text phase of an input group reserve their complete positioned-record span before any later item receives an order key.
- The full snapshot cache retains only the latest entry per world/device pair; the geometry cache is a collision-checked, bounded 256-entry LRU per world. Superseded device-bound snapshots stay alive only through explicit render-command/fence retirement or external owners.
- Runtime `surfaceWindowId` participates in the complete full-snapshot collision key but not its canonical JSON or geometry key. Rendering the same world on another window replaces the one world/device full slot instead of returning a snapshot stamped for the wrong UI surface.
- Empty clip intersections remove both render and hit records. Unmasked overflow remains visible. Logical-to-physical conversion uses floor(min)/ceil(max) only in `UIPhysicalTransform::ToPhysicalOutward`.
- `RenderCommand::scissor` is state, not batching identity. A clip transition flushes the active batch; equal adjacent scissors do not.
- Scroll input preserves signed X/Y pointer or wheel deltas and the exact signed gamepad axis value. Disabled axes consume no displacement. Runtime scroll offset/velocity never serialize.
- Scroll inertia advances only from `UIDeterministicTick` values. Runtime, embedded Game View, and canonical replay feed the same checked tick value contract; wall-clock sampling never occurs inside a UI subsystem.
- Each task uses red-green-refactor and ends with an independently reviewable commit. Do not pull a later task into an earlier commit.

## Prerequisite Contract

The executor must verify these APIs exist before Task 9.1:

```cpp
namespace molga {
class Fixed26_6 {
public:
    static constexpr std::int32_t Scale = 64;
    static std::optional<Fixed26_6> FromFloat(float);
    static constexpr Fixed26_6 FromRaw(std::int32_t);
    constexpr std::int32_t Raw() const;
    float ToFloat() const;
    static std::optional<Fixed26_6> CheckedAdd(Fixed26_6, Fixed26_6) noexcept;
    static std::optional<Fixed26_6> CheckedSub(Fixed26_6, Fixed26_6) noexcept;
    static std::optional<Fixed26_6> CheckedMulDiv(
        Fixed26_6, std::int64_t numerator, std::int64_t denominator) noexcept;
};
struct FixedPoint { Fixed26_6 x; Fixed26_6 y; };
struct FixedSize { Fixed26_6 width; Fixed26_6 height; };
struct FixedRect { Fixed26_6 x; Fixed26_6 y; Fixed26_6 width; Fixed26_6 height; };
} // namespace molga

namespace molga::text {
struct TextLayout;
class TextDiagnosticSink;
class TextLayoutService;
} // namespace molga::text

struct TextAffine2D {
    float m00 = 1.0f, m01 = 0.0f;
    float m10 = 0.0f, m11 = 1.0f;
    float tx = 0.0f, ty = 0.0f;
    Vector2 Apply(molga::FixedPoint) const;
};
struct TextRasterPolicy {
    std::uint16_t rasterScaleKey = 64;
};
struct TextCollectContext {
    TextAffine2D layoutToOutput;
    Color color = Color::White();
    int cameraPass = 0;
    int sortingLayer = 0;
    int sortingOrder = 0;
    float depthOrYSort = 0.0f;
    TextRasterPolicy rasterPolicy;
};

class TextRenderer {
public:
    molga::text::TextLayoutService& LayoutService() noexcept;
    void CollectLayout(molga::RenderQueue&, const molga::text::TextLayout&,
                       const TextCollectContext&,
                       molga::text::TextDiagnosticSink&);
};
```

Run this prerequisite gate without changing code:

```bash
cmake --preset debug
cmake --build --preset debug --target test_text_layout test_text_cache test_text test_ui -j
ctest --test-dir build/debug -R '^(test_text_layout|test_text_cache|test_text|test_ui)$' --output-on-failure
```

Expected: all four tests pass. If they do not, return to the owning earlier subplan.

## Exit Contract

This subplan is complete only when all of the following are true:

- `SceneObjectRef` preserves `targetId`/`Clear()` compatibility and every UI reference remaps through prefab cloning.
- Replacing a world or component invalidates stale full identities even when object ID and component type are reused.
- Every authored UI component round-trips with an explicit schema version and no runtime state leaks into JSON.
- Cold, warm, and different-edit-history builds of the same UI produce byte-identical semantic snapshots with no `frameIndex` member.
- Six hundred unchanged warm builds return the identical snapshot with zero snapshot-key allocations; every authored/hierarchy/intrinsic/font/content/runtime-binding mutation advances its owning aggregate generation.
- Geometry cache occupancy never exceeds 256 entries per live world and full snapshot occupancy never exceeds one latest entry per live world/device, including detached-window alternation and thousands of scroll/blink/reupload changes.
- Same-content texture reupload/device recreation misses the full-snapshot cache and publishes the new handle/lifetime while old snapshots stay valid; canonical JSON stays byte-identical.
- GPU teardown occurs only after successful idle/fence drain and release of engine-owned snapshot/command lifetimes; an external texture-binding lifetime or glyph-page token fails shutdown closed.
- Layout cycles use authored-axis fallback, never last-good geometry.
- Nested clips produce identical render/hit eligibility, and every render/hit record is complete without consulting a live component.
- Each hit record freezes separate action/focus/text/inner-to-outer-scroll targets; input-owned labels are suppressed from ordinary output and input visuals reserve complete text command spans.
- Render scissor transitions are deterministic and restore the active pass viewport.
- Scroll clamping/elasticity/inertia and X/Y/axis input are deterministic, identity-keyed, and non-serialized.
- Focused Debug plus layout ASan/UBSan gates below pass from fresh command output.

## File Responsibility Map

| Responsibility | Authoritative files |
|---|---|
| Stable scene/runtime identity | `src/ECS/SceneObjectRef.h`, `src/UI/UIRuntimeIdentity.*`, `src/Core/World.*` |
| Versioned UI authoring | `src/ECS/Components/UIComponent.h`, eight new UI component pairs, existing Canvas/Rect/Image/Button/Label components |
| Fixed layout and semantic cache | `src/UI/UILayoutTypes.h`, `src/UI/UILayoutSnapshot.*`, `src/UI/UILayoutSystem.*` |
| Aggregate dirty/binding epochs | `src/UI/UIRuntimeInvalidation.*`, UI setters/hierarchy hooks, texture/device binding hooks |
| Snapshot render/hit payloads | `src/UI/UILayoutSnapshot.h`, `src/UI/UILayoutSystem.cpp`, `src/UI/UIRenderCollector.*` |
| Physical clips and batching | `src/Rendering/RenderQueue.h`, `src/Rendering/RenderSystem2D.cpp`, `src/Rendering/Renderer.*`, `src/Rendering/SpriteBatcher.*` |
| Runtime scrolling | `src/UI/UIScrollSystem.*` |
| Deterministic UI time | `src/UI/UIDeterministicTick.h`, the frame-owned tick stream consumed by `UIScrollSystem` and later `UITextInputSystem` |

---

### Task 9.1: Establish stable scene references and process-global UI identities

**Files:**

- Create: `src/ECS/SceneObjectRef.h`
- Create: `src/ECS/SceneObjectRef.cpp`
- Create: `src/UI/UIRuntimeIdentity.h`
- Create: `src/UI/UIRuntimeIdentity.cpp`
- Create: `tests/test_ui_identity.cpp`
- Modify: `src/Core/World.h`
- Modify: `src/Core/World.cpp`
- Modify: `src/ECS/Component.h`
- Modify: `src/ECS/Component.cpp`
- Modify: `src/Scripting/ScriptField.h`
- Modify: `src/Scripting/Script.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `World::FindById`, `GameObject::GetID`, `Component::{GetRuntimeTypeID,GetInstanceID}`, and existing scripting `ObjectRef` serialization.
- Produces: `SceneObjectRef`, source-compatible `using ObjectRef = SceneObjectRef`, checked process-global world/component instance allocators, `World::Generation()`, `molga::ui::UIRuntimeTargetIdentity`, `CaptureTarget`, and both `ResolveTarget` overloads.

- [x] **Step 1a: Register the identity test target.**

  Add `molga_add_test(test_ui_identity test_ui_identity.cpp)` to `tests/CMakeLists.txt` and add `src/UI/UIRuntimeIdentity.cpp` to `ENGINE_SOURCES`.

- [x] **Step 1b: Write the failing `SceneObjectRef` compatibility test.**

  Create `tests/test_ui_identity.cpp` with:

  ```cpp
  #include "doctest.h"
  #include "Core/World.h"
  #include "ECS/SceneObjectRef.h"
  #include "ECS/Components/UIButton.h"
  #include "ECS/GameObject.h"
  #include "Scripting/ScriptField.h"
  #include "UI/UIRuntimeIdentity.h"
  #include <memory>
  #include <type_traits>

  static_assert(std::is_same<ObjectRef, SceneObjectRef>::value,
                "ObjectRef must remain a source-compatible alias");

  TEST_CASE("SceneObjectRef preserves targetId and Clear") {
      SceneObjectRef ref;
      ref.targetId = 41;
      CHECK(ref.IsSet());
      CHECK(ref.ObjectId() == 41);
      ref.Clear();
      CHECK(ref.targetId == 0);
  }

  ```

- [x] **Step 1c: Write the failing component-replacement identity test.**

  ```cpp
  TEST_CASE("component replacement never inherits a captured identity") {
      World world;
      auto object = std::make_shared<GameObject>("Button");
      auto* first = object->AddComponent<UIButton>();
      world.Add(object);
      const auto captured = molga::ui::CaptureTarget(world, *first);
      object->RemoveComponent<UIButton>();
      object->AddComponent<UIButton>();
      CHECK(molga::ui::ResolveTarget(world, captured) == nullptr);
  }

  ```

- [x] **Step 1d: Write the failing process-global generation test.**

  ```cpp
  TEST_CASE("world generations are process-global and replacement-safe") {
      World a;
      World b;
      CHECK(a.Generation() != 0);
      CHECK(b.Generation() > a.Generation());
      const auto beforeClear = a.Generation();
      a.Clear();
      CHECK(a.Generation() > b.Generation());
      CHECK(a.Generation() != beforeClear);
  }
  ```

- [x] **Step 1e: Write the failing `SceneObjectRef::Resolve` link/parity test.**

  ```cpp
  TEST_CASE("SceneObjectRef mutable and const Resolve link and agree") {
      World world;
      auto object = std::make_shared<GameObject>("Referenced");
      world.Add(object);
      SceneObjectRef ref{object->GetID()};
      CHECK(ref.Resolve(world) == object.get());
      const World& constWorld = world;
      CHECK(ref.Resolve(constWorld) == object.get());
      ref.targetId = object->GetID() + 1000;
      CHECK(ref.Resolve(world) == nullptr);
      CHECK(ref.Resolve(constWorld) == nullptr);
  }
  ```

  This test must link `src/ECS/SceneObjectRef.cpp`; an inline-only declaration or a missing const definition fails the target link.

- [x] **Step 1f: Write the failing component-instance exhaustion publication test.**

  ```cpp
  TEST_CASE("component instance allocation never wraps or publishes zero") {
      GameObject object("Owner");
      const auto countBefore = object.GetComponents().size();
      ScopedComponentInstanceIdForTesting exhausted(UINT64_MAX);
      CHECK_THROWS_AS(object.AddComponent<UIButton>(), std::overflow_error);
      CHECK(object.GetComponents().size() == countBefore);
      CHECK(ComponentInstanceIdForTesting() == UINT64_MAX);
      CHECK(object.GetComponent<UIButton>() == nullptr);
  }
  ```

  Add copy- and move-construction rows. Each throws before publishing a destination component, leaves the source unchanged, and observes neither zero nor a reused ID.

- [x] **Step 2: Configure and run the red identity gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_identity -j
  ```

  Expected: compilation fails because `SceneObjectRef`, `World::Generation`, and runtime target helpers do not exist.

- [x] **Step 3a: Add the one-storage `SceneObjectRef` declaration.**

  Create `src/ECS/SceneObjectRef.h` with this public shape and move the old scripting definition behind the alias:

  ```cpp
  #pragma once
  #include <unordered_map>

  class GameObject;
  class World;

  struct SceneObjectRef {
      unsigned int targetId = 0;

      bool IsSet() const noexcept { return targetId != 0; }
      void Clear() noexcept { targetId = 0; }
      unsigned int ObjectId() const noexcept { return targetId; }
      GameObject* Resolve(World&) const noexcept;
      const GameObject* Resolve(const World&) const noexcept;
      void Remap(const std::unordered_map<unsigned int, unsigned int>& ids) {
          const auto found = ids.find(targetId);
          if (found != ids.end()) targetId = found->second;
      }
      friend bool operator==(SceneObjectRef lhs, SceneObjectRef rhs) {
          return lhs.targetId == rhs.targetId;
      }
      friend bool operator!=(SceneObjectRef lhs, SceneObjectRef rhs) {
          return !(lhs == rhs);
      }
  };

  using ObjectRef = SceneObjectRef;
  ```

  Do not create an `objectId` data member.

- [x] **Step 3b: Alias scripting `ObjectRef` and preserve its serializer.**

  Include `ECS/SceneObjectRef.h` from `ScriptField.h`, remove the old duplicate struct, and keep `Script.cpp` reading/writing the JSON key `targetId` byte-for-byte.

- [x] **Step 3c: Define both `SceneObjectRef::Resolve` overloads in one named translation unit.**

  Create `src/ECS/SceneObjectRef.cpp` and implement without `const_cast`:

  ```cpp
  GameObject* SceneObjectRef::Resolve(World& world) const noexcept {
      return IsSet() ? world.FindById(targetId) : nullptr;
  }

  const GameObject* SceneObjectRef::Resolve(
      const World& world) const noexcept {
      return IsSet() ? world.FindById(targetId) : nullptr;
  }
  ```

  Register this exact `.cpp` in `ENGINE_SOURCES`; do not move either definition back into the header after the link test passes.

- [x] **Step 4: Add a non-wrapping process-global world generation allocator.**

  Use one translation-unit atomic and acquire a fresh generation after construction, `Clear`, a successful `LoadFromFile`, and every move construction/assignment that replaces scene content:

  ```cpp
  namespace {
  std::atomic<std::uint64_t> gNextWorldGeneration{1};

  std::uint64_t AcquireWorldGeneration() {
      std::uint64_t candidate =
          gNextWorldGeneration.load(std::memory_order_relaxed);
      for (;;) {
          if (candidate == 0 ||
              candidate == std::numeric_limits<std::uint64_t>::max()) {
              throw std::overflow_error("UI world generation exhausted");
          }
          if (gNextWorldGeneration.compare_exchange_weak(
                  candidate, candidate + 1,
                  std::memory_order_relaxed,
                  std::memory_order_relaxed)) {
              return candidate;
          }
      }
  }
  } // namespace
  ```

  The compare/exchange loop checks exhaustion before incrementing, so the atomic never wraps to zero. `World` stores `generation_`, exposes `std::uint64_t Generation() const noexcept`, and catches acquisition failure only at an engine boundary that can report a blocker `TextDiagnosticCode::ReferenceInvalid`; it must never publish generation zero or reuse an old generation.

- [x] **Step 5a: Remove `noexcept` from world move operations.**

  Change both declarations and definitions to `World(World&&)` and `World& operator=(World&&)`. Add a compile assertion that `std::is_nothrow_move_constructible_v<World>` is false.

- [x] **Step 5b: Acquire generations before construction/clear/load publication.**

  Construction stores one acquired value. `Clear` acquires first, publishes it before shutdown callbacks, then clears content. `LoadFromFile` loads into a temporary object vector, acquires a generation only after parse success, swaps the vector, then publishes that generation; a failed load preserves both content and generation.

- [x] **Step 5c: Acquire both move-operation generations before mutation.**

  Move construction and move assignment acquire a destination generation and a moved-from generation before transferring content. If either acquisition throws, neither `World` changes. After transfer, publish both values so the moved-from object can be reused without validating old identities.

  ```cpp
  World& World::operator=(World&& other) {
      if (this == &other) return *this;
      const auto replacementGeneration = AcquireWorldGeneration();
      const auto movedFromGeneration = AcquireWorldGeneration();
      if (IsLifecycleMutationActive() || other.IsLifecycleMutationActive()) {
          throw std::logic_error("cannot move a World during callbacks");
      }
      TransferOwnedStateFrom(std::move(other));
      generation_ = replacementGeneration;
      other.generation_ = movedFromGeneration;
      return *this;
  }
  ```

  Add private `void TransferOwnedStateFrom(World&&)` containing the current field transfer and owner/scheduler rebinding code; it does not acquire or publish generations.

  Engine construction/scene-load boundaries catch `std::overflow_error`, emit blocker `ReferenceInvalid`, and abort the attempted replacement. The allocator test hook is test-only and restored after each case.

- [x] **Step 6a: Add the full runtime identity value type.**

  Define the runtime identity exactly:

  ```cpp
  namespace molga::ui {
  struct UIRuntimeTargetIdentity {
      std::uint64_t worldGeneration = 0;
      unsigned int objectId = 0;
      std::size_t componentRuntimeTypeId = 0;
      std::uint64_t componentInstanceId = 0;
      explicit operator bool() const noexcept {
          return worldGeneration != 0 && objectId != 0 &&
                 componentInstanceId != 0;
      }
      friend bool operator==(const UIRuntimeTargetIdentity& lhs,
                             const UIRuntimeTargetIdentity& rhs) noexcept {
          return lhs.worldGeneration == rhs.worldGeneration &&
                 lhs.objectId == rhs.objectId &&
                 lhs.componentRuntimeTypeId == rhs.componentRuntimeTypeId &&
                 lhs.componentInstanceId == rhs.componentInstanceId;
      }
      friend bool operator!=(const UIRuntimeTargetIdentity& lhs,
                             const UIRuntimeTargetIdentity& rhs) noexcept {
          return !(lhs == rhs);
      }
  };

  } // namespace molga::ui
  ```

- [x] **Step 6b: Implement `CaptureTarget`.**

  ```cpp
  UIRuntimeTargetIdentity CaptureTarget(const World& world,
                                        const Component& component);
  ```

  Return an empty identity unless the component has an owner, the owner belongs to `world`, and generation/object/instance values are nonzero. Otherwise copy the four exact fields.

- [x] **Step 6c: Implement mutable `ResolveTarget`.**

  ```cpp
  Component* ResolveTarget(World&, const UIRuntimeTargetIdentity&) noexcept;
  ```

  Check all four fields in order and return no pointer until the final instance-ID check succeeds.

- [x] **Step 6d: Implement const `ResolveTarget`.**

  ```cpp
  const Component* ResolveTarget(const World&,
                                 const UIRuntimeTargetIdentity&) noexcept;
  ```

  Share the same four-field predicate without `const_cast`; add parity assertions for every mutable success/failure row.

- [x] **Step 7a: Add move and successful-load invalidation cases.**

  Capture a button, move-assign a replacement world, and successfully load a scene into the same `World`; assert every old identity resolves to `nullptr`.

- [x] **Step 7b: Add cross-world object-ID reuse rejection.**

  Create the same stable object ID and component type in two independently constructed worlds; assert the first identity never resolves in the second.

- [x] **Step 7c: Add non-wrapping exhaustion coverage.**

  Use a scoped test-only allocator hook to set `gNextWorldGeneration` to `UINT64_MAX`; assert acquisition throws, the atomic remains `UINT64_MAX`, and no zero generation is observable. Restore the prior value at scope exit.

- [x] **Step 7d: Replace the existing component `fetch_add` allocator.**

  In `src/ECS/Component.cpp`, replace `gNextComponentInstanceId.fetch_add` with the same checked CAS shape used for world generations:

  ```cpp
  std::uint64_t AcquireComponentInstanceId() {
      auto candidate =
          gNextComponentInstanceId.load(std::memory_order_relaxed);
      for (;;) {
          if (candidate == 0 || candidate == UINT64_MAX) {
              throw std::overflow_error("component instance ID exhausted");
          }
          if (gNextComponentInstanceId.compare_exchange_weak(
                  candidate, candidate + 1,
                  std::memory_order_relaxed,
                  std::memory_order_relaxed)) {
              return candidate;
          }
      }
  }
  ```

  Default/copy/move construction acquires before modifying either object or publishing into `GameObject::componentMap`. Remove `noexcept` from `Component(Component&&)` in declaration and definition because allocation may fail; assignment preserves the destination instance ID and remains non-allocating. Scene/prefab/component-factory boundaries catch exhaustion, emit blocker `ReferenceInvalid`, and abort the complete component/object publication rather than inserting a null/zero identity.

- [x] **Step 7e: Add explicit C++17 equality coverage for identity values.**

  Add compile/runtime assertions for both `==` and `!=` on `SceneObjectRef` and `UIRuntimeTargetIdentity`, including one-field differences. Do not rely on C++20 defaulted comparison; Task 10.1 and Task 11.1 add the fixed/pixel/stable value types, and subplan 04 adds owner/token values when those types are introduced.

- [x] **Step 8: Run identity, scripting, world, serializer, and prefab gates.**

  ```bash
  cmake --build --preset debug --target test_ui_identity test_script_field_snapshot \
    test_world_lifecycle test_scene_serializer test_prefab -j
  ctest --test-dir build/debug -R '^(test_ui_identity|test_script_field_snapshot|test_world_lifecycle|test_scene_serializer|test_prefab)$' --output-on-failure
  ```

  Expected: all selected tests pass; existing `ObjectRef.targetId` snapshots remain unchanged.

- [x] **Step 9: Commit the identity boundary.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/ECS/SceneObjectRef.* \
    src/UI/UIRuntimeIdentity.* src/Core/World.* src/ECS/Component.* \
    src/Scripting/ScriptField.h \
    src/Scripting/Script.cpp tests/test_ui_identity.cpp
  git commit -m "feat: add stable UI runtime identities"
  ```

**Implementation record (2026-09-07).** Commit `48c6f5b`. Audit passed after **seven blocking** and
nine important findings were fixed, each re-run as its own mutant and confirmed killed.

Blocking findings, all in the identity semantics this task exists to establish: a move-constructed
`World` inherited the source generation, so old identities silently survived; move *assignment*
could publish the same generation to both worlds; component copy and move **assignment** could steal
the destination's instance id; the move constructor's `other.gameObject = nullptr` had no success
witness; `TransferOwnedStateFrom`'s owner and scheduler rebinding was unasserted; and a successful
`LoadFromFile` could skip the `SetWorld` rebinding of loaded objects.

`World::Clear` publishing the new generation **after** shutdown callbacks was uncaught even though
the code comment claims an `OnDestroy` handler observes a dead world. Step 1d's case held no objects,
so `Shutdown()` invoked no callback and the published value was identical either way — the reviewer
proved the production ordering correct with a scratch probe, and the assertion is now committed with
a `destroyCalls == 1` witness so "the callback never ran" cannot pass.

Two `ResolveTarget` improvements beyond the findings: a new `GameObject::FindComponentByTypeId`
removes a full component-vector copy per resolve, and the `catch (...)` it existed to justify is
deleted — verified equivalent because every `GetRuntimeTypeID()` returns `ComponentTypeID::Get<Self>`,
exactly the key both `AddComponent<T>` and `AddComponentRaw` use.

An approved deviation: `tests/test_world_lifecycle.cpp` is outside the Files list but Step 5c's
verbatim block replaces a silent `return *this` with a throw, so the existing case threw out of
`World::Update`. Every original state assertion is kept and each row now additionally requires the
throw — strictly stronger. One assertion was dropped as structurally unobservable, and a two-sided
success witness was added, which the case previously lacked entirely.

---

#### BLOCKING PRECONDITION FOR TASK 10.2 AND THE MILESTONE 11 SNAPSHOT CACHE

**The editor replaces a whole scene without advancing `World::Generation()`.** `Editor::OpenScene()`
and `Editor::NewScene()` reach `SceneSerializer::LoadScene` through the **non-const**
`World::Objects()` reference installed at `main.cpp:378`, bypassing both `World::LoadFromFile` and
`World::Clear`. So the Global Constraint that "construction, `Clear`, successful scene load, and
move-based replacement invalidate earlier identities" **does not hold in the running editor**. Every
`.Objects()` call site was checked; `main.cpp:378/400/410/488/613` are the only non-read uses, so the
exposure is exactly those two editor commands.

Nothing breaks **today**, because component instance ids are process-monotonic and never reused, so a
stale identity still fails closed on the fourth field. But this subplan goes on to specify a
"collision-checked, bounded 256-entry LRU per world" geometry cache and a snapshot cache that
"retains only the latest entry per world/device pair". Keyed on `Generation()` — the only per-world
identity this task produces — **those caches will serve the previous scene's entries after the user
picks Open Scene.** Fix it before either cache lands; the root cause is the non-const `Objects()`
escape hatch, and the boundary files are outside Task 9.1's Files list.

---

#### ACCEPTED DEVIATION: no engine boundary catches `std::overflow_error`

Steps 5c and 7d mandate that engine construction, scene-load, prefab and component-factory
boundaries catch identity exhaustion and emit a blocker `TextDiagnosticCode::ReferenceInvalid`.
Verified absent: `grep` finds **zero** catch sites in the whole tree, and `ReferenceInvalid` appears
only in `TextRenderer.cpp` and the enum tables.

Accepted, because: the **negative** half is implemented and tested — `World` and `Component` never
swallow exhaustion, never publish zero, never reuse, and leave the destination and container
unchanged on throw — so this is a reporting gap, not a corruption risk; `uint64` exhaustion is
unreachable in practice; every candidate boundary file is outside both the Files list and the
`git add` list; and `World::LoadFromFile` has no diagnostic sink to report into. Note Step 4's
wording is arguably a *prohibition* ("catches … only at an engine boundary"), which `World` satisfies
by never catching.

**Owner: Task 15.1**, which introduces typed scene references and has editor diagnostics available.
The residual risk until then is an exception propagating out of `World::LoadFromFile` into engine
code that previously could not throw.

### Task 9.2: Add versioned authored UI component schemas

**Files:**

- Create: `src/UI/UIRuntimeInvalidation.h`
- Create: `src/UI/UIRuntimeInvalidation.cpp`
- Create: `src/ECS/Components/UIComponent.h`
- Create: `src/ECS/Components/UILayoutElement.h`
- Create: `src/ECS/Components/UILayoutElement.cpp`
- Create: `src/ECS/Components/UILayoutGroup.h`
- Create: `src/ECS/Components/UILayoutGroup.cpp`
- Create: `src/ECS/Components/UIContentSizeFitter.h`
- Create: `src/ECS/Components/UIContentSizeFitter.cpp`
- Create: `src/ECS/Components/UIMask.h`
- Create: `src/ECS/Components/UIMask.cpp`
- Create: `src/ECS/Components/UIScrollView.h`
- Create: `src/ECS/Components/UIScrollView.cpp`
- Create: `src/ECS/Components/UISelectable.h`
- Create: `src/ECS/Components/UISelectable.cpp`
- Create: `src/UI/UINavigationTypes.h`
- Create: `src/ECS/Components/UITextInput.h`
- Create: `src/ECS/Components/UITextInput.cpp`
- Create: `src/ECS/Components/UIAccessibility.h`
- Create: `src/ECS/Components/UIAccessibility.cpp`
- Create: `tests/test_ui_components.cpp`
- Modify: `src/ECS/BuiltinComponents.cpp`
- Modify: `src/ECS/Components/UICanvas.h`
- Modify: `src/ECS/Components/UICanvas.cpp`
- Modify: `src/ECS/Components/RectTransform.h`
- Modify: `src/ECS/Components/RectTransform.cpp`
- Modify: `src/ECS/Components/UIImage.h`
- Modify: `src/ECS/Components/UIImage.cpp`
- Modify: `src/ECS/Components/UIButton.h`
- Modify: `src/ECS/Components/UIButton.cpp`
- Modify: `src/ECS/Components/UILabel.h`
- Modify: `src/ECS/Components/UILabel.cpp`
- Modify: `src/Core/SceneSerializer.cpp`
- Modify: `src/Core/PrefabUtil.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `SceneObjectRef`, `Component::RemapReferences`, text paragraph enums, and the existing component serialization convention.
- Produces: non-wrapping aggregate UI runtime generations, `UIComponent` invalidation revisions, and eight explicit current-schema components used by layout, scrolling, focus, text editing, and semantic-tree tasks.

- [x] **Step 1a: Write a failing exhaustive component round-trip test.**

  Register `test_ui_components`, then create a table that constructs each new component, assigns every authored field to a non-default value, serializes, deserializes, and checks exact equality. Include this concrete runtime omission case:

  ```cpp
  TEST_CASE("UITextInput serializes authored state only") {
      UITextInput input;
      input.SetInitialText(u8"초기값");
      input.SetReadOnly(true);
      input.SetMultiline(true);
      input.SetMaxGraphemes(17);
      input.SetTextViewport(SceneObjectRef{11});
      input.SetRenderedLabel(SceneObjectRef{12});
      input.SetPlaceholderLabel(SceneObjectRef{13});
      nlohmann::json encoded;
      input.Serialize(encoded);
      const std::set<std::string> expectedKeys = {
          "schemaVersion", "initialText", "readOnly", "multiline",
          "maxGraphemes", "contentPolicy", "submitPolicy", "textViewport",
          "renderedLabel", "placeholderLabel", "fontFamilyGuid",
          "paragraphStyle"};
      CHECK(JsonObjectKeys(encoded) == expectedKeys);
      CHECK(encoded["schemaVersion"] == UITextInput::CurrentSchemaVersion);
      CHECK(encoded["initialText"] == u8"초기값");
      CHECK(encoded["textViewport"]["targetId"] == 11);

      encoded["runtimeValue"] = "must-not-round-trip";
      encoded["caret"] = 4;
      encoded["selection"] = nlohmann::json::array({1, 3});
      encoded["composition"] = "stale";
      encoded["blink"] = true;
      UITextInput decoded;
      decoded.Deserialize(encoded);
      nlohmann::json reencoded;
      decoded.Serialize(reencoded);
      CHECK(JsonObjectKeys(reencoded) == expectedKeys);
  }
  ```

  `JsonObjectKeys` is a test helper that returns the exact top-level key set. Do not add a runtime-value field, runtime setter, or test-only edit-state storage to `UITextInput`; unknown runtime-looking JSON keys are ignored on load and never echoed on save.

- [x] **Step 1b: Write a failing non-wrapping authored-revision test.**

  ```cpp
  TEST_CASE("UI authored revision exhaustion disables snapshot caching") {
      UILabel label;
      label.SetAuthoredRevisionForTesting(UINT64_MAX);
      label.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
      CHECK(label.AuthoredRevision() == UINT64_MAX);
      CHECK_FALSE(label.RevisionCacheable());
      CHECK((label.DirtyMask() & UIAllInvalidationBits) ==
            UIAllInvalidationBits);
  }
  ```

  `SetAuthoredRevisionForTesting` is compiled only into the test target. The snapshot test later verifies that a component with an exhausted revision never hits or populates either geometry or full-snapshot caches and emits one blocker diagnostic.

- [x] **Step 1c: Write a failing non-wrapping aggregate-dirty test.**

  ```cpp
  TEST_CASE("aggregate UI dirty generation never wraps or aliases") {
      ScopedUIRuntimeGenerationForTesting scoped(
          UIRuntimeGenerationKind::SemanticDirty, UINT64_MAX);
      UILabel label;
      label.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
      const auto generations = UIRuntimeInvalidationClock::Current();
      CHECK(generations.semanticDirtyGeneration == UINT64_MAX);
      CHECK_FALSE(generations.cacheable);
      CHECK_FALSE(label.RevisionCacheable());
  }
  ```

  The scoped hook is test-only and restores the process-global clock. Repeat for `ScrollDisplacement`, `TextureBinding`, and `Device`; no counter publishes zero or a reused value.

- [x] **Step 1d: Write failing exact UI policy enum/default/string tests.**

  ```cpp
  static_assert(static_cast<std::uint8_t>(UITextInputContentPolicy::Any) == 0);
  static_assert(static_cast<std::uint8_t>(UITextInputSubmitPolicy::OnEnter) == 0);
  static_assert(static_cast<std::uint8_t>(UIAccessibilityRole::None) == 0);

  TEST_CASE("Milestone A UI policy strings and defaults are exact") {
      UITextInput input;
      UIAccessibility accessibility;
      CHECK(input.ContentPolicy() == UITextInputContentPolicy::Any);
      CHECK(input.SubmitPolicy() == UITextInputSubmitPolicy::OnEnter);
      CHECK(accessibility.Role() == UIAccessibilityRole::None);
      CHECK(ToCanonicalString(input.ContentPolicy()) == "Any");
      CHECK(ToCanonicalString(input.SubmitPolicy()) == "OnEnter");
      CHECK((AllAccessibilityRoleStrings() ==
             std::vector<std::string>{"None", "Panel", "Label", "Button",
                                      "TextInput", "Image", "ScrollView"}));
  }
  ```

- [x] **Step 1e: Write failing unknown-policy and migration tests.**

  Deserialize fresh fixtures with `contentPolicy="Decimal"`, `submitPolicy="OnBlur"`, and `role="Unknown"`. Require `UIComponentSchemaError{TextDiagnosticCode::LayoutInvalid}`, caught by `SceneSerializer` as a typed load failure, and no partial component/world publication. `LayoutInvalid` is the already-closed diagnostic category; `UIComponentSchemaError` supplies the typed deserialize boundary, so this task does not invent a fourteenth diagnostic code. This preserves the existing virtual `void Component::Deserialize(...)` signature rather than inventing a bool overload. Load the approved legacy `UITextInput` payload that omits both policy keys and require explicit migration defaults `Any`/`OnEnter`; load legacy `UIAccessibility` without `role` and require `None`. Re-serialization writes canonical PascalCase strings only after the explicit current-schema migration path.

- [x] **Step 2: Add the failing prefab-remap test.**

  For `UIScrollView` assign viewport/content IDs; for `UISelectable` assign four navigation IDs; for `UITextInput` assign viewport/rendered-label/placeholder IDs. Clone the subtree and assert every in-subtree `targetId` is remapped while an external ID remains unchanged.

- [x] **Step 3: Add failing legacy-load tests.**

  Load legacy `UICanvas`, `UILabel`, and `TextRenderer2D` JSON and assert current behavior plus `LoadedSchema::Legacy`; serialize without an explicit migration command and assert the legacy representation remains.

- [x] **Step 4: Run the component red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_components -j
  ```

  Expected: compilation fails because the eight component classes and schema APIs do not exist.

- [x] **Step 5a: Define the process-global aggregate invalidation clock.**

  ```cpp
  enum class UIRuntimeGenerationKind : std::uint8_t {
      SemanticDirty, ScrollDisplacement, TextureBinding, Device
  };
  struct UIRuntimeGenerationSnapshot {
      std::uint64_t semanticDirtyGeneration = 1;
      std::uint64_t scrollDisplacementGeneration = 1;
      std::uint64_t textureBindingGeneration = 1;
      std::uint64_t deviceGeneration = 1;
      bool cacheable = true;
  };
  class UIRuntimeInvalidationClock {
  public:
      static UIRuntimeGenerationSnapshot Current() noexcept;
      static std::optional<std::uint64_t> Advance(
          UIRuntimeGenerationKind) noexcept;
  };
  ```

  Each counter uses a compare/exchange loop that checks `0`/`UINT64_MAX` before incrementing, stores and returns the same `candidate + 1` value, and leaves the counter at `UINT64_MAX` on exhaustion. Exhaustion atomically makes `cacheable=false`; it never wraps. A setter calls `Advance(SemanticDirty)` only after canonical old/new values differ. Task 11 texture/device binding code uses the returned values for exact published identities, so concurrent acquisitions cannot both read a later shared value.

- [x] **Step 5b: Add typed authoring invalidation to `UIComponent`.**

  Use runtime-only counters and the narrowest invalidation bit:

  ```cpp
  enum class UIInvalidation : std::uint8_t {
      Visual = 1u << 0, Intrinsic = 1u << 1, Layout = 1u << 2,
      Hierarchy = 1u << 3, Interaction = 1u << 4
  };
  constexpr std::uint8_t UIAllInvalidationBits =
      static_cast<std::uint8_t>(UIInvalidation::Visual) |
      static_cast<std::uint8_t>(UIInvalidation::Intrinsic) |
      static_cast<std::uint8_t>(UIInvalidation::Layout) |
      static_cast<std::uint8_t>(UIInvalidation::Hierarchy) |
      static_cast<std::uint8_t>(UIInvalidation::Interaction);

  class UIComponent : public Component {
  public:
      std::uint64_t AuthoredRevision() const noexcept { return revision_; }
      std::uint8_t DirtyMask() const noexcept { return dirtyMask_; }
      bool RevisionCacheable() const noexcept { return revisionCacheable_; }
      void ClearDirtyMask() noexcept { dirtyMask_ = 0; }
  protected:
      void Invalidate(UIInvalidation reason) noexcept {
          const bool aggregateCacheable =
              molga::ui::UIRuntimeInvalidationClock::Advance(
                  molga::ui::UIRuntimeGenerationKind::SemanticDirty)
                  .has_value();
          if (revision_ == std::numeric_limits<std::uint64_t>::max() ||
              !aggregateCacheable) {
              revisionCacheable_ = false;
              dirtyMask_ = UIAllInvalidationBits;
              return;
          }
          ++revision_;
          dirtyMask_ |= static_cast<std::uint8_t>(reason);
      }
  private:
      std::uint64_t revision_ = 1;
      std::uint8_t dirtyMask_ = 0;
      bool revisionCacheable_ = true;
  };
  ```

  Revisions, dirty bits, and aggregate generations never enter serialized JSON, prefab overrides, or editor dirty checks. Once either component or aggregate generation is exhausted, it stays at `UINT64_MAX`; it never wraps or reuses an earlier cache identity. `UILayoutSystem::Build` detects `RevisionCacheable()==false` or aggregate `cacheable==false`, emits one rate-limited blocker, rebuilds uncached, and refuses to look up or insert either cache.

- [x] **Step 6a: Declare the shared layout enums and axis record.**

  Define and validate these authored records without computed state:

  ```cpp
  struct UIAxisConstraint { float minimum = 0; float preferred = 0; float flexible = 0; };
  enum class UILayoutMode : std::uint8_t { Horizontal, Vertical, Grid };
  enum class UIGridConstraint : std::uint8_t { Flexible, FixedColumns, FixedRows };
  enum class UIFitMode : std::uint8_t { Unconstrained, Min, Preferred };
  enum class UIScrollMovement : std::uint8_t { Clamped, Elastic };
  ```

  Reject unknown enum values during load.

- [x] **Step 6b: Implement `UILayoutElement` canonical setters.**

  Store horizontal/vertical constraints plus `ignoreLayout`. Each axis setter rejects non-finite input, then stores `minimum=max(0,minimum)`, `preferred=max(minimum,preferred)`, and `flexible=max(0,flexible)`; a changed canonical value calls `Invalidate(Layout)` once.

- [x] **Step 6c: Serialize and round-trip `UILayoutElement`.**

  Write `schemaVersion`, both complete axis records, and `ignoreLayout`; deserialize through the same canonical setters and write no computed size.

- [x] **Step 6d: Add the `UIContentSizeFitter` schema.**

  Store horizontal and vertical `UIFitMode` independently, validate enum values on load, and invalidate only layout when either authored mode changes.

- [x] **Step 7a: Declare all `UILayoutGroup` authored fields.**

  Store mode, four-sided padding, X/Y spacing, child alignment, per-axis control/expand, cell size, start corner, fill axis, grid constraint, and constraint count; initialize deterministic documented defaults.

- [x] **Step 7b: Implement `UILayoutGroup` validation setters.**

  Reject non-finite values, non-positive grid cells/counts, and negative padding/spacing with `LayoutInvalid`. A canonical value change invalidates layout once; rejection leaves fields/revisions unchanged.

- [x] **Step 7c: Serialize and round-trip `UILayoutGroup`.**

  Write every field plus `schemaVersion` under fixed key names used by the exhaustive Step 1 table; deserialize only through validated setters.

- [x] **Step 8a: Add the `UIMask` schema.**

  Store rectangular descendant clipping enablement only and invalidate layout plus interaction when it changes.

- [x] **Step 8b: Declare `UIScrollView` values and references.**

  Store viewport/content refs, horizontal/vertical enable, movement, elasticity, inertia, deceleration, sensitivity, and initial normalized X/Y.

- [x] **Step 8c: Implement `UIScrollView` validation setters.**

  Reject non-finite values, normalized positions outside `[0,1]`, negative sensitivity/deceleration, and non-positive elasticity when movement is `Elastic`; normalize signed zero before storing and invalidate layout only on canonical change.

- [x] **Step 8d: Serialize/remap `UIScrollView`.**

  Write all authored values and `{targetId}` references, remap both refs, and omit runtime offset/velocity.

- [x] **Step 9a: Add the `UISelectable` value schema.**

  Define `enum class UINavigationMode : std::uint8_t { None, Auto, Explicit };` in `src/UI/UINavigationTypes.h`; store interactable and that mode with interaction invalidation.

- [x] **Step 9b: Add `UISelectable` navigation refs and remapping.**

  Store up/down/left/right `SceneObjectRef`, write each `{targetId}`, and override `RemapReferences` for all four references.

- [x] **Step 10a: Add `UITextInput` scalar/text policy fields.**

  Define the exact Milestone A policy surface and no extra values:

  ```cpp
  enum class UITextInputContentPolicy : std::uint8_t { Any };
  enum class UITextInputSubmitPolicy : std::uint8_t { OnEnter };
  ```

  Defaults are `Any` and `OnEnter`; canonical serialized strings are exactly `"Any"` and `"OnEnter"`. Any other numeric/string value is a typed deserialize failure, not an implicit fallback. Store initial UTF-8, read-only, single/multiline, grapheme max length, these policies, font family GUID, and complete paragraph style with the correct visual/intrinsic/interaction invalidation bits. New content or submit policy values require a later approved design/schema version.

- [x] **Step 10b: Add `UITextInput` authored references.**

  Store viewport/rendered-label/placeholder `SceneObjectRef`, serialize exact `{targetId}` values, and override `RemapReferences` for all three.

- [x] **Step 10c: Serialize `UITextInput` without runtime edit state.**

  Emit exactly the key set asserted in Step 1a; ignore unknown runtime-looking keys on load and never echo them.

- [x] **Step 11a: Add `UIAccessibility` values and invalidation.**

  Define the exact role surface:

  ```cpp
  enum class UIAccessibilityRole : std::uint8_t {
      None, Panel, Label, Button, TextInput, Image, ScrollView
  };
  ```

  Default to `None`; serialize the exact PascalCase enumerator spelling and fail typed deserialization on any unknown numeric/string value. Store role, name, description, and hidden metadata only; changed values invalidate semantic/interaction snapshot content, not geometry.

- [x] **Step 11b: Serialize and round-trip `UIAccessibility`.**

  Write exactly those four values plus `schemaVersion`; omit computed roles/focus state. Migration of an approved role-less legacy payload sets `None`; it never guesses a role from sibling components.

  Every class above exposes `static constexpr std::uint32_t CurrentSchemaVersion`; invalid references remain clear/value-only and no pointer is serialized.

- [x] **Step 12a: Add current `UICanvas` authored fields.**

  Add `ConstantPixelSize`/`ScaleWithViewport`, reference resolution, width-height match, and sorting with finite/range validation and narrow invalidation.

- [x] **Step 12b: Preserve legacy Canvas bytes and behavior.**

  Keep the loaded-schema marker and legacy scaling results; ordinary load/save does not rewrite legacy JSON into the current schema.

- [x] **Step 13a: Preserve the legacy label authoring contract.**

  Preserve `UILabel` legacy `fontGuid` as an implicit one-face family view and its loaded-schema marker.

- [x] **Step 13b: Preserve the button authoring contract.**

  Keep `UIButton` authored color/callback data while moving no runtime state into the component in this task.

- [x] **Step 14a: Register all component sources.**

  Add `UIRuntimeInvalidation.cpp` and every new component `.cpp` to `ENGINE_SOURCES`, then add `test_ui_components`; configure once and verify the target reaches the intended red compile failures.

  Add every new `.cpp` to `ENGINE_SOURCES` and register the test target.

- [x] **Step 14b: Register every component factory.**

  Add all eight concrete component types to `BuiltinComponents.cpp` and assert each exact serialized type name constructs the expected runtime type.

- [x] **Step 15: Exclude runtime keys from prefab normalization.**

  `PrefabUtil` ignores `schemaVersion` and runtime-only keys while comparing overrides, but still remaps authored `targetId` fields.

- [x] **Step 16: Run component, serializer, prefab, and legacy regression gates.**

  ```bash
  cmake --build --preset debug --target test_ui_components test_scene_serializer \
    test_prefab test_script_field_snapshot test_ui -j
  ctest --test-dir build/debug -R '^(test_ui_components|test_scene_serializer|test_prefab|test_script_field_snapshot|test_ui)$' --output-on-failure
  ```

  Expected: all selected tests pass; JSON contains only authored fields and explicit schema versions.

- [x] **Step 17: Commit the authored schema slice.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UIRuntimeInvalidation.* \
    src/ECS/Components/UIComponent.h \
    src/ECS/Components/UILayoutElement.* src/ECS/Components/UILayoutGroup.* \
    src/ECS/Components/UIContentSizeFitter.* src/ECS/Components/UIMask.* \
    src/ECS/Components/UIScrollView.* src/ECS/Components/UISelectable.* \
    src/UI/UINavigationTypes.h \
    src/ECS/Components/UITextInput.* src/ECS/Components/UIAccessibility.* \
    src/ECS/BuiltinComponents.cpp src/ECS/Components/UICanvas.* \
    src/ECS/Components/RectTransform.* src/ECS/Components/UIImage.* \
    src/ECS/Components/UIButton.* src/ECS/Components/UILabel.* \
    src/Core/SceneSerializer.cpp src/Core/PrefabUtil.cpp \
    tests/test_ui_components.cpp
  git commit -m "feat: add versioned UI authoring schemas"
  ```

**Implementation record (2026-09-07).** Commit `0955eab`, plus a controller follow-up for the audit's
one real defect. Audit passed. **Twelve blocking** findings — the heaviest of the program — all fixed
and each re-run as its own mutant.

**Three were live production bugs, not coverage gaps:**

- **`ReadUIUInt` rejected valid non-negative integers stored as signed JSON numbers**, so an ordinary
  Inspector edit of any unsigned field aborted. Fixed by testing the *value* (`is_number_integer`
  plus a `[0, UINT32_MAX]` range) rather than the storage-type predicate.
- **`UIComponentSchemaError` escaped three of the four `Component::Deserialize` callers** — the
  editor Inspector and both `PrefabUtil::ApplyModifications` sites — so a hand-edited or
  forward-versioned scene threw straight through `DeserializeScene`.
- **The suite failed under ASan on correct code.** Two assertions compared `const char*` **pointers**
  rather than strings, which only diverges when the literals are not pooled. Fixed without touching
  the verbatim Step 1d lines: all sixteen `ToCanonicalString` overloads now return
  `std::string_view`, pinned by sixteen `static_assert`s.

**The trap this task's dispatch named by name recurred anyway.** `UITextInput`'s `readOnly` and
`multiline` were `true` in *every* fixture, so swapping them in either direction was undetectable —
exactly Task 8.1's `horizontalAlignment`/`verticalAlignment` hole, one task later, in a brief that
quoted it. `UIScrollView`'s `horizontal`/`vertical`/`inertia` were all `false` for the same reason.
**Naming a defect shape does not prevent it; only mutation testing catches it.** Both are now
asymmetric in both directions.

Also closed: the paragraph-colour length guard was a silent out-of-bounds read rather than a throw;
"an absent key restores the documented default" was untested for every key but two; the signed-zero
test was vacuous because the setter early-returns before the store; the four aggregate generation
axes were never proven independent, so aliasing two counters passed; and the legacy-Canvas scale-mode
force was invisible because the only legacy fixture loaded into a fresh canvas whose default already
matched.

**Controller follow-up — `RectTransform` and `UIImage` bypassed their own invalidating setters.**
Both were converted to `UIComponent` in this commit and given setters that `Invalidate(Layout)` /
`Invalidate(Visual)`, but their `Deserialize` still assigned the fields directly — *inconsistently*,
since the same functions already called `SetAnchorMin`/`SetPivot`/`SetTextureGuid`. Undo and prefab
override re-deserialize into a **live** component, so an authored change through that path altered
values without invalidating. Both now route through their setters, with four cases driven through the
existing `CheckNarrowestBit` helper (which also pins that re-applying the same payload is *not* an
invalidation), and each fix verified by re-applying the direct assignment and watching the case fail.

---

#### A BUILD HAZARD WORTH KNOWING

**The incremental build silently served stale objects.** Editing
`src/ECS/Components/RectTransform.cpp` and rebuilding `test_ui_components` reported zero errors and a
successful link while still running the *previous* object; the change only took effect after
`touch`ing the source. A test result taken across an edit is therefore not trustworthy on its own —
`touch` the sources you changed, or confirm the compile line appears in the build output, before
believing a green run.

---

#### CARRIED FORWARD

1. ~~**Three components emit no `schemaVersion`.**~~ **CLOSED (2026-09-07).** `RectTransform`,
   `UIImage` and `UIButton` declared no `CurrentSchemaVersion` and wrote no key, while the other ten
   UI components all did. Correctly out of Task 9.2's scope — Step 11b scopes to the eight new
   components and Step 13b explicitly asks for no new `UIButton` state — and correctly declined by
   Task 10.1, whose Files list contains no authored UI component. All three now declare
   `CurrentSchemaVersion = 1` and write the key unconditionally, matching the eight new components
   rather than the `LoadedSchema` branch of `UILabel`/`UICanvas`: their authored shape has never
   changed, so schema 1 *is* the legacy shape and the absent-key default already lands on the
   current schema. A marker would instead pin every document on disk to `Legacy` forever and the key
   would never be written. `tests/test_ui_components.cpp` adds two cases — literal expected payloads
   for default and fully-authored state with no two scalars alike, and a legacy load proving an
   on-disk document with the smoke fixture's exact key set and no version key still loads into both
   a fresh and an already-authored component, gaining only the marker on save. Eight mutants: the
   dropped key per component, the constant bumped to 2, `Deserialize` ignoring a version-less
   document, an `anchorMin`/`anchorMax` swap, a `normalColor`/`hoverColor` swap, and the version
   echoed from the payload instead of the constant — that last one killed by exactly one assertion.
   The subplan's Exit bullet "every authored UI component round-trips with an explicit schema
   version" is now **met** for all thirteen.
2. **The typed deserialize boundary is not airtight for the pre-existing components.** `UILabel`,
   `UICanvas`, `RectTransform` and `UIImage` validate `is_array()` and size but then call bare
   `get<float>()` on elements, so a wrong-typed element throws `nlohmann::json::type_error`, which the
   new `catch (const UIComponentSchemaError&)` guards do not catch. Pre-existing and unchanged here;
   the new `UITextInput` reader deliberately avoids the pattern.
3. **`build/ubsan`'s target list is stale** and does not know `test_ui_components`, so the subplan's
   UBSan gate cannot include it without a reconfigure — which risks the known SDL_shadercross/DXC
   flake. `build/asan` is current and the case is green there, which is the configuration the
   `string_view` defect actually manifested in.

### Task 10.1: Define checked UI geometry and immutable semantic snapshots

**Files:**

- Create: `src/UI/UILayoutTypes.h`
- Create: `src/UI/UILayoutSnapshot.h`
- Create: `src/UI/UILayoutSnapshot.cpp`
- Create: `tests/test_ui_fixed.cpp`
- Create: `tests/test_ui_snapshot.cpp`
- Modify: `src/Common/Fixed26_6.h`
- Modify: `src/Common/Fixed26_6.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: the shared `Fixed26_6` primitive and full `UIRuntimeTargetIdentity`.
- Produces: fixed-point edge helpers, `UIDrawOrderKey`, `UILayoutNodeSnapshot`, semantic `UISnapshot`, `UISnapshotPtr`, and stable diagnostic JSON.

- [x] **Step 1: Write failing fixed arithmetic and edge conversion tests.**

  ```cpp
  TEST_CASE("fixed conversion rejects invalid input and normalizes zero") {
      CHECK_FALSE(molga::Fixed26_6::FromFloat(
          std::numeric_limits<float>::infinity()));
      CHECK_FALSE(molga::Fixed26_6::FromFloat(
          std::numeric_limits<float>::quiet_NaN()));
      REQUIRE(molga::Fixed26_6::FromFloat(-0.0f));
      CHECK(molga::Fixed26_6::FromFloat(-0.0f)->Raw() == 0);
      CHECK(molga::Fixed26_6::FromFloat(1.0f / 128.0f)->Raw() == 1);
      CHECK(molga::Fixed26_6::FromFloat(-1.0f / 128.0f)->Raw() == -1);
  }

  TEST_CASE("checked fixed division exposes explicit floor and ceil") {
      CHECK(molga::CheckedFloorDiv(-65, 64) == -2);
      CHECK(molga::CheckedCeilDiv(-65, 64) == -1);
      CHECK(molga::CheckedFloorDiv(65, 64) == 1);
      CHECK(molga::CheckedCeilDiv(65, 64) == 2);
      CHECK_FALSE(molga::CheckedFloorDiv(INT64_MIN, -1));
      CHECK_FALSE(molga::CheckedCeilDiv(INT64_MIN, -1));
      CHECK_FALSE(molga::CheckedFloorDiv(1, 0));
  }
  ```

- [x] **Step 2a: Write the failing semantic snapshot test.**

  ```cpp
  TEST_CASE("UISnapshot is semantic and cacheable") {
      static_assert(!molga::ui::SnapshotHasFrameIndex<
                    molga::ui::UISnapshot>::value,
                    "frameIndex belongs to UIFrameInput/UIFrameResult only");
      molga::ui::UISnapshot snapshot;
      snapshot.worldGeneration = 9;
      snapshot.logicalViewport = {
          molga::Fixed26_6::FromRaw(800 * 64),
          molga::Fixed26_6::FromRaw(600 * 64)};
      const std::string json = molga::ui::StableLayoutSnapshotJson(snapshot);
      CHECK(json.find("frameIndex") == std::string::npos);
      CHECK(json.find("timestamp") == std::string::npos);
      CHECK(json.find("physical") == std::string::npos);
  }
  ```

- [x] **Step 2b: Write failing C++17 value-comparison coverage.**

  ```cpp
  TEST_CASE("fixed UI values provide symmetric C++17 equality") {
      const molga::Fixed26_6 one = molga::Fixed26_6::FromRaw(64);
      const molga::Fixed26_6 two = molga::Fixed26_6::FromRaw(128);
      CHECK(one == one);
      CHECK(one != two);
      CHECK(molga::FixedPoint{one, two} == molga::FixedPoint{one, two});
      CHECK(molga::FixedSize{one, two} != molga::FixedSize{two, one});
      CHECK(molga::FixedRect{one, one, two, two} !=
            molga::FixedRect{one, two, two, two});
  }
  ```

  Add the same one-field-difference matrix for `UIDrawOrderKey`. This target is compiled as C++17, so every used `==` and `!=` must be explicitly declared; no rewritten comparison or C++20 defaulting is available.

- [x] **Step 3: Run the geometry/snapshot red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_fixed test_ui_snapshot -j
  ```

  Expected: compilation fails on floor/ceil helpers and snapshot types.

- [x] **Step 4a: Guard checked floor division before `/` or `%`.**

  ```cpp
  std::optional<std::int64_t> CheckedFloorDiv(std::int64_t n,
                                               std::int64_t d) {
      if (d == 0 || (n == INT64_MIN && d == -1)) return std::nullopt;
      const auto q = n / d;
      const auto r = n % d;
      return q - ((r != 0 && ((r < 0) != (d < 0))) ? 1 : 0);
  }

  ```

- [x] **Step 4b: Guard checked ceil division before `/` or `%`.**

  ```cpp
  std::optional<std::int64_t> CheckedCeilDiv(std::int64_t n,
                                              std::int64_t d) {
      if (d == 0 || (n == INT64_MIN && d == -1)) return std::nullopt;
      const auto q = n / d;
      const auto r = n % d;
      return q + ((r != 0 && ((r < 0) == (d < 0))) ? 1 : 0);
  }
  ```

- [x] **Step 4c: Verify the inherited fixed conversion and multiply/divide contract.**

  Task 3.1 already implements `FromFloat` and `CheckedMulDiv` with validated
  intermediates, nearest rounding with exact half ties away from zero, and
  `nullopt` before a zero divisor, non-finite conversion, intermediate overflow,
  or final `int32_t` overflow. Run Step 1 against that inherited implementation;
  do not fork or reimplement it in Task 10. This task adds only the explicitly
  named checked floor/ceil helpers above. No operation saturates.

- [x] **Step 5a: Define `UIDrawOrderKey`.**

  ```cpp
  namespace molga::ui {
  struct UIDrawOrderKey {
      std::int32_t canvasSortingOrder = 0;
      std::vector<std::uint32_t> siblingPath;
      std::int32_t componentSortingOrder = 0;
      std::uint64_t stableSubmissionIndex = 0;
      bool operator<(const UIDrawOrderKey&) const;
      bool operator==(const UIDrawOrderKey&) const;
      bool operator!=(const UIDrawOrderKey& other) const {
          return !(*this == other);
      }
  };

  } // namespace molga::ui
  ```

- [x] **Step 5b: Verify the inherited C++17 fixed-value equality contract.**

  Task 3.1 already owns the exact field-wise `constexpr noexcept` `==`/`!=`
  definitions for `Fixed26_6`, `FixedPoint`, `FixedSize`, and `FixedRect`.
  Compile and run Step 2b against those inherited definitions; do not redefine
  them in this task. `UIDrawOrderKey` remains the Task 10-owned value type whose
  explicit C++17 equality is added here. Do not use byte comparison because
  padding is not semantic.

- [x] **Step 5c: Define semantic layout node and snapshot records.**

  ```cpp
  namespace molga::ui {
  struct UILayoutNodeSnapshot {
      UIRuntimeTargetIdentity rectTransform;
      molga::FixedRect logicalRect;
      molga::FixedSize intrinsicSize;
      std::optional<molga::FixedRect> logicalClip;
      std::uint64_t layoutRevision = 0;
      bool interactionEligible = false;
  };

  struct UISnapshot {
      molga::WindowId surfaceWindowId = 0;
      std::uint64_t worldGeneration = 0;
      molga::FixedSize logicalViewport;
      std::vector<UILayoutNodeSnapshot> nodes;
  };

  using UISnapshotPtr = std::shared_ptr<const UISnapshot>;
  std::string StableLayoutSnapshotJson(const UISnapshot&);
  } // namespace molga::ui
  ```

  Do not add `frameIndex`, timestamp, physical pixels, raw pointers, or GPU handles to these core layout/node records. `surfaceWindowId` is runtime routing identity and is also omitted from canonical JSON so editor/runtime window allocation cannot change parity. Task 11's concrete sprite variant adds a retained runtime binding, but `StableLayoutSnapshotJson` always omits it. The serializer emits stable integer raw 26.6 fields and sorted nodes.

- [x] **Step 6: Add ordering and byte-stability assertions.**

  Construct snapshots through different insertion orders, sort by `UIDrawOrderKey`, and require identical JSON bytes. Add a compile-time member detector in the test only; do not add reflection support to production.

- [x] **Step 7: Run the focused green gate.**

  ```bash
  cmake --build --preset debug --target test_ui_fixed test_ui_snapshot -j
  ctest --test-dir build/debug -R '^(test_ui_fixed|test_ui_snapshot)$' --output-on-failure
  ```

  Expected: both tests pass and snapshot JSON contains no audit-frame field.

- [x] **Step 8: Commit geometry and snapshot contracts.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Common/Fixed26_6.* \
    src/UI/UILayoutTypes.h src/UI/UILayoutSnapshot.* \
    tests/test_ui_fixed.cpp tests/test_ui_snapshot.cpp
  git commit -m "feat: define semantic UI layout snapshots"
  ```

**Implementation record (2026-09-07).** Commit `1ae3613`, plus `c6a7b54` closing this subplan's
carried item 1. Audit passed. Debug suite **105/105**.

**Two spec ambiguities were settled with authority, not preference:**

- **`layoutRevision` must not enter the canonical JSON.** The spec lens flagged that emitting it would
  make the subplan's "cold, warm and different-edit-history builds produce byte-identical semantic
  snapshots" bullet unmeetable, since the name invites a per-recompute counter. The decisive citation
  is stronger than that reasoning: **subplan 07 line 193 lists `layoutRevision` explicitly among the
  forbidden canonical keys.** It stays on the runtime record and is excluded from the bytes, with the
  reason in the code.
- **`StableLayoutSnapshotJson` correctly does not sort.** `UILayoutNodeSnapshot` carries no
  `UIDrawOrderKey`, so the serializer physically cannot sort by it, and Step 6 places the sort on the
  caller. The committed assertions that prove the serializer is order-*sensitive* are the right
  non-vacuity witness for the sorted-equality check above them.

**A factual correction that mattered.** The implementer reported the missing `schemaVersion` on
`UIImage` as "unowned in either subplan — someone must add it to a Files list or it falls through the
program". That was wrong: all three components sit in **Task 9.2's own Files list** (lines 602–607,
repeated in its `git add` at 955–956). So it was unfinished work inside a completed task, not
unassigned work — and the distinction is the difference between fixing it and waiting for a future
task to volunteer. Closed in `c6a7b54`.

**Carried item 1 — CLOSED.** `RectTransform`, `UIImage` and `UIButton` now declare
`CurrentSchemaVersion = 1` and write the key unconditionally, following Task 9.2's eight new
components rather than the `LoadedSchema` branch of `UILabel`/`UICanvas`. That branch was correctly
rejected: their authored shape has never changed, so schema 1 *is* the legacy shape, and a legacy
marker would pin every document on disk to `Legacy` forever — the key would never actually be
written, leaving the Exit bullet unmet in a different way.

The legacy proof avoids the traps this program keeps hitting: the version-less payloads use the
**exact key set** `tests/smoke/create_fixture.cmake` writes to disk, values chosen dyadic so the
comparison is byte-exact rather than approximate, each loaded into a fresh component **and** into an
already-authored one (the undo/prefab-override path), with the same document *plus* an explicit
version asserted to give an identical result — so neither shape is left untested. Eight mutants were
killed, and notably the `anchorMin`/`anchorMax` and `normalColor`/`hoverColor` swaps **die only under
the authored payload**, because the defaults are equal — exactly the axis-swap trap that survived
Tasks 8.1 and 9.2.

---

#### A LATENT UNDO BUG, SURFACED AND DELIBERATELY NOT FIXED HERE

**A snapshot missing a key does not undo that field.** `RectTransform`, `UIImage`, `UIButton`,
`UILabel` and `UICanvas` all use the **current member value** as the `Deserialize` fallback rather
than the documented default. So restoring a snapshot that omits a key leaves the field at its
post-edit value instead of reverting it. This is precisely the defect `CheckSchemaContract`'s points
4 and 5 were built to catch, which is why those three components could not be added to that table.

Changing it is a semantic change to undo and prefab-override behaviour, not a schema version, so it
was correctly left alone and asserted through a helper that pins the same literal-payload contract
without the default-restoration points. **Owner: Task 15.4**, which owns making UI/text migration
"byte-exactly undoable" — but note this affects undo **today**, not only migration.

### Task 10.2: Implement deterministic measure, arrangement, caching, and SCC fallback

**Files:**

- Create: `src/UI/UILayoutSystem.h`
- Create: `src/UI/UILayoutSystem.cpp`
- Create: `tests/test_ui_layout.cpp`
- Modify: `src/UI/UIRuntimeInvalidation.h`
- Modify: `src/UI/UIRuntimeInvalidation.cpp`
- Modify: `src/ECS/GameObject.h`
- Modify: `src/ECS/GameObject.cpp`
- Modify: `src/Core/World.h`
- Modify: `src/Core/World.cpp`
- Modify: `src/ECS/Components/RectTransform.cpp`
- Modify: `src/Text/FontRepository.cpp`
- Modify: `src/Text/FontFamilyResolver.cpp`
- Modify: `src/Text/TextLayoutService.cpp`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Core/TextureManager.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/Editor/Editor.h`
- Modify: `src/Editor/Editor.cpp`
- Modify: `src/Editor/Windows/SceneViewWindow.h`
- Modify: `src/Editor/Windows/SceneViewWindow.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Rendering/GameOutputRenderer.h`
- Modify: `src/Rendering/GameOutputRenderer.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_ui_snapshot.cpp`
- Modify: `tests/test_game_view.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: all authored layout components, `TextLayout::intrinsicSize`, checked fixed geometry, hierarchy revisions, and semantic snapshot types.
- Produces: `UILayoutSystem::Build(World&, WindowId, FixedSize, TextDiagnosticSink&)`, `UILayoutFastPathStamp`, deterministic driver resolution, SCC authored-axis fallback, a collision-checked 256-entry-per-world geometry LRU, one latest full-snapshot slot per world/device, and an allocation-free unchanged fast path.

- [x] **Step 1: Register layout-bearing tests with the common text runtime session.** Create `test_ui_layout` through `molga_add_text_test` and replace the Task 10.1 registration of `test_ui_snapshot` with the same helper before either fixture constructs `TextLayoutService`. The helper itself performs the idempotent `molga_attach_text_dependencies`; do not add a second explicit attach. Use only `MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT`, do not link `doctest_main`, and preserve every existing label/include/property after target creation. Both targets reuse Task 2.2's installed `TextRuntimeTestSession`; no local ICU init or alternate resource root is allowed.

- [x] **Step 1a: Write failing horizontal/vertical allocation cases.**

  Add `LayoutCase` rows `horizontal-padding-spacing`, `vertical-cross-align`, `authored-size-child`, `control-expand`, and `below-minimum-overflow`. Each row supplies authored JSON, viewport raw units, and the exact expected raw child rect vector; loop over the rows and compare all four rect fields.

- [x] **Step 1b: Write failing grid and remainder cases.**

  Add rows `fixed-columns`, `fixed-rows`, `flexible-grid`, and `sibling-order`, then add this exact raw-unit remainder case:

  ```cpp
  TEST_CASE("layout remainder follows sibling order") {
      LayoutFixture f({molga::Fixed26_6::FromRaw(193),
                       molga::Fixed26_6::FromRaw(64)});
      f.HorizontalGroup().ControlWidth(true).ExpandWidth(true);
      const auto ids = f.AddEqualChildren(3);
      const auto snapshot = f.Build();
      CHECK(f.WidthRaw(snapshot, ids[0]) == 65);
      CHECK(f.WidthRaw(snapshot, ids[1]) == 64);
      CHECK(f.WidthRaw(snapshot, ids[2]) == 64);
  }
  ```

- [x] **Step 1c: Write failing Canvas, anchor, hierarchy, and driver cases.**

  Add rows `anchors`, `constant-pixel-canvas`, `scaled-viewport-canvas`, `fitter-below-parent-driver`, and `inactive-ancestor`, each with exact expected raw rects.

- [x] **Step 2a: Write the failing SCC authored-fallback test.**

  Create a parent group → child intrinsic text → content fitter → parent-size cycle. Require all driven properties in that axis to use authored RectTransform, one `LayoutCycle` diagnostic, and identical JSON for cold, warm, and different edit histories.

- [x] **Step 2b: Write the failing unchanged full-cache pointer test.**

  Build an unchanged frame twice and assert pointer identity:

  ```cpp
  const auto first = layout.Build(world, WindowId{7}, viewport, diagnostics);
  const auto second = layout.Build(world, WindowId{7}, viewport, diagnostics);
  REQUIRE(first);
  CHECK(first.get() == second.get());
  ```

  Build the same world/viewport next on window 8. Require a full-snapshot miss, unchanged geometry-build count, `surfaceWindowId==8`, and canonical JSON byte-equal to window 7. Build window 7 once more and require another full miss with `surfaceWindowId==7` while full-cache occupancy stays exactly one for that world/device. This prevents detached surfaces from reusing the wrong runtime routing identity without multiplying the bounded slot count.

- [x] **Step 2c: Write failing visual/content and interaction cache tests.**

  ```cpp
  TEST_CASE("payload-only edits miss snapshot cache but reuse geometry") {
      UILayoutCacheFixture f;
      const auto first = f.Build();
      const auto geometryBuilds = f.GeometryBuildCount();
      f.Label().SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
      const auto recolored = f.Build();
      REQUIRE(first);
      REQUIRE(recolored);
      CHECK(first.get() != recolored.get());
      CHECK(f.GeometryBuildCount() == geometryBuilds);
      f.Selectable().SetInteractable(false);
      const auto disabled = f.Build();
      CHECK(recolored.get() != disabled.get());
      CHECK(f.GeometryBuildCount() == geometryBuilds);
  }

  TEST_CASE("exhausted UI revision bypasses every snapshot cache") {
      UILayoutCacheFixture f;
      f.Label().SetAuthoredRevisionForTesting(UINT64_MAX);
      f.Label().SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
      const auto first = f.Build();
      const auto second = f.Build();
      REQUIRE(first);
      REQUIRE(second);
      CHECK(first.get() != second.get());
      CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
  }
  ```

  Extend the first case as a table with one edit per fresh fixture: `UILabel` text, label color, immutable layout content identity, `UIImage` tint, texture content SHA under the same GUID, selectable interactability, one explicit navigation ref, and mask enablement. Each edit must miss the full-snapshot cache; pure visual/interaction edits retain the identical geometry-cache entry. Task 11.1 adds exact updated render/hit payload assertions once those records exist.

- [x] **Step 2d: Write the failing RectTransform/Canvas dirty-epoch table.**

  ```cpp
  const std::array rectCanvasMutations{
      UIFieldMutation::AnchorMin, UIFieldMutation::AnchorMax,
      UIFieldMutation::Pivot, UIFieldMutation::AnchoredPosition,
      UIFieldMutation::SizeDelta, UIFieldMutation::CanvasScaleMode,
      UIFieldMutation::CanvasReferenceResolution,
      UIFieldMutation::CanvasMatchWidthOrHeight,
      UIFieldMutation::CanvasSortingOrder};
  CheckEachMutationAdvancesSemanticEpoch(rectCanvasMutations);
  ```

  `CheckEachMutationAdvancesSemanticEpoch` creates a fresh attached component, records `UIRuntimeInvalidationClock::Current().semanticDirtyGeneration`, applies exactly the named setter with a different finite canonical value, and requires a strictly greater nonzero generation. Its exhaustive `switch(UIFieldMutation)` has no `default`.

- [x] **Step 2e: Write the failing layout/fitter/group dirty-epoch table.**

  ```cpp
  const std::array layoutMutations{
      UIFieldMutation::HorizontalMinimum,
      UIFieldMutation::HorizontalPreferred,
      UIFieldMutation::HorizontalFlexible,
      UIFieldMutation::VerticalMinimum,
      UIFieldMutation::VerticalPreferred,
      UIFieldMutation::VerticalFlexible,
      UIFieldMutation::IgnoreLayout,
      UIFieldMutation::FitterHorizontal,
      UIFieldMutation::FitterVertical,
      UIFieldMutation::GroupMode,
      UIFieldMutation::PaddingLeft, UIFieldMutation::PaddingTop,
      UIFieldMutation::PaddingRight, UIFieldMutation::PaddingBottom,
      UIFieldMutation::SpacingX, UIFieldMutation::SpacingY,
      UIFieldMutation::ChildAlignment,
      UIFieldMutation::ControlWidth, UIFieldMutation::ControlHeight,
      UIFieldMutation::ExpandWidth, UIFieldMutation::ExpandHeight,
      UIFieldMutation::CellWidth, UIFieldMutation::CellHeight,
      UIFieldMutation::StartCorner, UIFieldMutation::FillAxis,
      UIFieldMutation::GridConstraint,
      UIFieldMutation::ConstraintCount};
  CheckEachMutationAdvancesSemanticEpoch(layoutMutations);
  ```

- [x] **Step 2f: Write the failing mask/scroll/selectable dirty-epoch table.**

  ```cpp
  const std::array interactionMutations{
      UIFieldMutation::MaskEnabled,
      UIFieldMutation::ScrollViewport, UIFieldMutation::ScrollContent,
      UIFieldMutation::ScrollHorizontal, UIFieldMutation::ScrollVertical,
      UIFieldMutation::ScrollMovement, UIFieldMutation::ScrollElasticity,
      UIFieldMutation::ScrollInertia, UIFieldMutation::ScrollDeceleration,
      UIFieldMutation::ScrollSensitivity,
      UIFieldMutation::ScrollInitialNormalizedX,
      UIFieldMutation::ScrollInitialNormalizedY,
      UIFieldMutation::SelectableInteractable,
      UIFieldMutation::NavigationMode, UIFieldMutation::NavigationUp,
      UIFieldMutation::NavigationDown, UIFieldMutation::NavigationLeft,
      UIFieldMutation::NavigationRight};
  CheckEachMutationAdvancesSemanticEpoch(interactionMutations);
  ```

- [x] **Step 2g: Write the failing visual/text/semantic dirty-epoch table.**

  ```cpp
  const std::array payloadMutations{
      UIFieldMutation::ImageTextureGuid, UIFieldMutation::ImageTint,
      UIFieldMutation::ImageSortingOrder,
      UIFieldMutation::LabelText, UIFieldMutation::LabelFontFamilyGuid,
      UIFieldMutation::LabelFontSize, UIFieldMutation::LabelLineSpacing,
      UIFieldMutation::LabelColor,
      UIFieldMutation::LabelHorizontalAlignment,
      UIFieldMutation::LabelVerticalAlignment,
      UIFieldMutation::LabelSortingOrder,
      UIFieldMutation::ButtonInteractable,
      UIFieldMutation::ButtonNormalColor, UIFieldMutation::ButtonHoverColor,
      UIFieldMutation::ButtonPressedColor,
      UIFieldMutation::ButtonDisabledColor,
      UIFieldMutation::ButtonSortingOrder,
      UIFieldMutation::InputInitialText, UIFieldMutation::InputReadOnly,
      UIFieldMutation::InputMultiline, UIFieldMutation::InputMaxGraphemes,
      UIFieldMutation::InputContentPolicy,
      UIFieldMutation::InputSubmitPolicy,
      UIFieldMutation::InputTextViewport,
      UIFieldMutation::InputRenderedLabel,
      UIFieldMutation::InputPlaceholderLabel,
      UIFieldMutation::InputFontFamilyGuid,
      UIFieldMutation::InputParagraphStyle,
      UIFieldMutation::AccessibilityRole,
      UIFieldMutation::AccessibilityName,
      UIFieldMutation::AccessibilityDescription,
      UIFieldMutation::AccessibilityHidden};
  CheckEachMutationAdvancesSemanticEpoch(payloadMutations);
  ```

  Add one same-value row per component type; setting an already-canonical identical value must change neither the component revision nor the aggregate epoch.

- [x] **Step 2h: Write the failing hierarchy/intrinsic/font dirty-epoch table.**

  ```cpp
  const std::array externalMutations{
      UIFieldMutation::ObjectActive,
      UIFieldMutation::ChildAdded, UIFieldMutation::ChildRemoved,
      UIFieldMutation::ChildReparented,
      UIFieldMutation::SiblingOrder,
      UIFieldMutation::IntrinsicLayoutIdentity,
      UIFieldMutation::FontFaceBytes,
      UIFieldMutation::FontFamilyFallbackOrder,
      UIFieldMutation::FontFamilyStyleMap,
      UIFieldMutation::TextureContentSha};
  CheckEachMutationAdvancesSemanticEpoch(externalMutations);
  ```

  Hierarchy hooks bump only when an active UI Canvas subtree could change. Font repository/family/layout generation publication bumps after successful immutable replacement, not on a failed/no-op reload.

- [x] **Step 2i: Write the failing 600-frame zero snapshot-key-allocation test.**

  ```cpp
  TEST_CASE("warm static UI bypasses snapshot key construction and allocation") {
      UILayoutCacheFixture f;
      const auto warm = f.Build();
      REQUIRE(warm);
      const auto keyAllocations = f.SnapshotKeyAllocationCount();
      const auto keyBuilds = f.SnapshotKeyBuildCount();
      for (int frame = 0; frame < 600; ++frame) {
          CHECK(f.Build().get() == warm.get());
      }
      CHECK(f.SnapshotKeyBuildCount() == keyBuilds);
      CHECK(f.SnapshotKeyAllocationCount() == keyAllocations);
  }
  ```

  The counter is incremented by the cache-key scratch allocator's upstream allocation path, so this is the same `ui.snapshot-key` allocation boundary consumed by the later warm-static performance gate, not a generic frame counter.

- [x] **Step 2j: Write failing bounded-cache churn tests.**

  ```cpp
  TEST_CASE("UI snapshot and geometry caches remain bounded under churn") {
      UILayoutCacheFixture f;
      for (std::uint32_t i = 0; i < 4096; ++i) {
          f.SetRuntimeVisualRevision(i + 1); // scroll/blink-style full miss
          REQUIRE(f.Build());
          CHECK(f.FullSnapshotCacheEntryCountForWorldDevice() <= 1);
      }
      for (std::uint32_t i = 0; i < 2048; ++i) {
          f.SetDistinctLayoutConstraintRaw(640 + static_cast<int>(i));
          REQUIRE(f.Build());
          CHECK(f.GeometryCacheEntryCountForWorld() <= 256);
      }
      CHECK(f.GeometryCacheEntryCountForWorld() == 256);
      CHECK_FALSE(f.GeometryCacheContainsOldestFixtureKey());
  }
  ```

  Add an interleaved two-world/two-device row: each `{worldGeneration,deviceGeneration}` has at most one full entry, each world has at most 256 geometry entries, releasing a world removes both its LRU and every full slot, and a device-generation transition removes old-device full slots without removing reusable geometry.

- [x] **Step 3: Run the layout red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_layout test_ui_snapshot -j
  ```

  Expected: compilation fails because `UILayoutSystem` does not exist.

- [x] **Step 4a: Define the layout service boundary.**

  ```cpp
  namespace molga::ui {
  class UILayoutSystem {
  public:
      UISnapshotPtr Build(World&, molga::WindowId surfaceWindowId,
                          molga::FixedSize logicalViewport,
                          molga::text::TextDiagnosticSink&);
      void OnWorldReleased(std::uint64_t worldGeneration);
  };
  } // namespace molga::ui
  ```

  Reject a zero surface window, invalid viewport, or invalid/non-finite authored value before key construction. Update every Task 10 fixture/facade caller to pass its exact surface window; no overload guesses a global keyboard window.

- [x] **Step 4b: Define the allocation-free fast-path stamp.**

  ```cpp
  struct UILayoutFastPathStamp {
      molga::WindowId surfaceWindowId = 0;
      std::uint64_t worldGeneration = 0;
      molga::FixedSize logicalViewport;
      std::uint64_t viewportGeneration = 0;
      std::uint64_t semanticDirtyGeneration = 0;
      std::uint64_t scrollDisplacementGeneration = 0;
      std::uint64_t textureBindingGeneration = 0;
      std::uint64_t deviceGeneration = 0;
      bool operator==(const UILayoutFastPathStamp&) const noexcept;
  };
  ```

  `UILayoutSystem` stores the last successful stamp and `UISnapshotPtr`. `surfaceWindowId` is the exact nonzero UI surface window supplied by the caller; it is runtime-only and excluded from canonical JSON. At the first line of `Build` after surface/viewport validation, read the process-global generation snapshot and compare these scalar/value fields. If all fields match and the clocks remain cacheable, return the prior shared pointer immediately: do not traverse Canvas trees, allocate/clear/resize a vector, canonicalize JSON, build a string, hash a key, or touch the scratch allocator. A raw viewport change first acquires a checked new `viewportGeneration`; exhaustion disables the fast path and both caches.

- [x] **Step 4c: Define the collision-checked geometry cache key.**

  ```cpp
  struct UILayoutGeometryCacheKey {
      std::uint64_t worldGeneration = 0;
      molga::FixedSize viewport;
      std::uint64_t viewportGeneration = 0;
      std::vector<std::uint64_t> canvasScaleRevisions;
      std::vector<std::uint64_t> hierarchyAndSiblingRevisions;
      std::vector<std::uint64_t> rectAndLayoutRevisions;
      std::vector<std::uint64_t> intrinsicGenerations;
      bool operator==(const UILayoutGeometryCacheKey&) const;
  };
  ```

  Store the original ordered vectors and compare them after any hash match; do not trust a hash alone. This key may reuse measured/arranged geometry after a tint, focusability, or other payload-only edit.

- [x] **Step 4d: Define the visual/content cache identity.**

  ```cpp
  struct UIVisualCacheIdentity {
      unsigned int sceneObjectId = 0;
      std::string componentTypeName;
      std::uint32_t componentSchemaVersion = 0;
      std::uint64_t authoredRevision = 0;
      std::string canonicalAuthoredPayload;
      std::string immutableTextLayoutIdentity;
      std::string textureGuid;
      std::string textureContentSha256;
      std::uint64_t textureContentStableId = 0;
      bool operator==(const UIVisualCacheIdentity&) const;
  };
  ```

  `canonicalAuthoredPayload` contains collision-checked canonical values such as label UTF-8/style/color and image tint; it is not a hash-only shortcut. The immutable layout identity is content-derived and equality-checked. Texture identity is the authored GUID plus validated content SHA and a content-derived stable ID, never a process-local asset/upload revision.

- [x] **Step 4e: Define interaction identity and the complete snapshot key.**

  ```cpp
  struct UIInteractionCacheIdentity {
      unsigned int sceneObjectId = 0;
      std::string componentTypeName;
      std::uint32_t componentSchemaVersion = 0;
      std::uint64_t authoredRevision = 0;
      bool active = false;
      bool interactable = false;
      bool focusable = false;
      bool acceptsTextInput = false;
      bool maskEnabled = false;
      UINavigationMode navigationMode = UINavigationMode::None;
      std::array<SceneObjectRef, 4> explicitNavigation;
      bool operator==(const UIInteractionCacheIdentity&) const;
  };
  struct UISnapshotCacheKey {
      molga::WindowId surfaceWindowId = 0;
      UILayoutGeometryCacheKey geometry;
      std::uint64_t semanticDirtyGeneration = 0;
      std::uint64_t scrollDisplacementGeneration = 0;
      std::uint64_t textureBindingGeneration = 0;
      std::uint64_t deviceGeneration = 0;
      std::vector<UIVisualCacheIdentity> visualContent;
      std::vector<UIInteractionCacheIdentity> interaction;
      bool operator==(const UISnapshotCacheKey&) const;
  };
  ```

  Define cache ownership separately from key identity:

  ```cpp
  inline constexpr std::size_t kUILayoutGeometryEntriesPerWorld = 256;
  struct UISnapshotWorldDeviceSlotKey {
      std::uint64_t worldGeneration = 0;
      std::uint64_t deviceGeneration = 0;
      bool operator==(const UISnapshotWorldDeviceSlotKey&) const noexcept;
      bool operator!=(const UISnapshotWorldDeviceSlotKey& other) const noexcept {
          return !(*this == other);
      }
  };
  ```

  `UILayoutSystem` owns one recency-updated, collision-compared geometry LRU of at most 256 entries for each live world and exactly one `{completeKey,snapshot}` full entry for each live world/device slot. A full miss replaces that slot instead of accumulating a history of focus, scroll, blink, or upload keys.

  `surfaceWindowId` is nonzero runtime routing identity and is compared before returning the one world/device slot; it never enters geometry reuse or canonical JSON. Build both ordered identity vectors in the same stable Canvas DFS order. These process-local non-wrapping scalars prevent a fast-path invalidation—including later hover/focus/text visual state—from falling back onto a prior full snapshot; Task 11.1 adds exact per-binding collision fields and Task 11.3 adds exact scroll displacement fields. They are runtime cache fields only and never enter stable snapshot JSON. Task 11 adds the resolved render/hit records and exact binding identities under this key. GPU handles, pointers, process-local generations, and frame audit values appear in neither the geometry key nor canonical JSON.

- [x] **Step 4f: Define reusable collision-key scratch storage.**

  Keep member-owned, capacity-retaining scratch vectors for geometry revisions, visual content, interaction, and later runtime bindings. On a changed stamp, `resize` and overwrite entries in stable Canvas DFS order; reuse per-entry string capacity. Count only upstream capacity growth as `ui.snapshot-key` allocation. Copy/move a completed collision-checked key into cache ownership only on a miss; an unchanged fast path never resets the scratch.

- [x] **Step 4g: Implement exact bounded lookup and eviction.**

  Geometry lookup first compares hash and then every original key field. A hit moves its intrusive/list recency node to MRU without reallocating; insertion at capacity evicts exactly the LRU entry for that world. A full-snapshot lookup addresses the single `UISnapshotWorldDeviceSlotKey` entry, collision-compares its complete `UISnapshotCacheKey`, and replaces the old entry on miss. `OnWorldReleased` erases that world's geometry LRU, full slots, fast-path stamp, and reserved per-world scratch metadata.

- [x] **Step 4h: Publish component-setter mutations.**

  Route every `UIComponent::Invalidate` through `Advance(SemanticDirty)` and make the Step 2d–2g setter tables pass. Each setter compares canonical old/new state first and advances exactly once after success; a failed mutation/no-op does not advance.

- [x] **Step 4i: Publish hierarchy mutations.**

  Mark active-state, add/remove, reparent, and sibling-order changes in `GameObject`/`World` after the mutation succeeds. Limit notifications to changes that can affect an active UI Canvas subtree and make the hierarchy rows in Step 2h pass.

- [x] **Step 4j: Publish intrinsic/font/content mutations.**

  Advance semantic dirty after successful immutable text-layout identity, font-face byte, fallback-order, style-map, or validated texture content-SHA replacement. Failed/no-op repository/resolver/asset changes preserve the epoch.

- [x] **Step 4k: Implement fast-path lookup before key construction.**

  Validate the viewport, update its checked local generation when raw width/height changes, read `UIRuntimeInvalidationClock::Current`, and construct only the scalar `UILayoutFastPathStamp` on the stack. Return `lastSnapshot_` immediately on equality. On change, build collision keys with reserved scratch; publish `lastFastPathStamp_`/`lastSnapshot_` only after a complete snapshot has been built and inserted/resolved. If any generation is exhausted or a reachable component is uncacheable, clear the saved fast-path pointer and rebuild without lookup/insertion.

- [x] **Step 5a: Gather active Canvas trees in exact sibling DFS order.**

  ```text
  gather(node, siblingPath):
    if node or an ancestor is inactive: return
    append node with siblingPath
    for childIndex in [0, childCount):
      gather(child[childIndex], siblingPath + childIndex)
  ```

  Build `UIDrawOrderKey` during this traversal and never derive a second traversal index later.

- [x] **Step 5b: Measure intrinsic constraints child-to-parent.**

  ```text
  measure(node):
    measure active layout children in sibling order
    compute intrinsic min/preferred from content and group
    return canonical fixed constraints

  ```

- [x] **Step 5c: Resolve each arranged axis owner.**

  ```cpp
  enum class UILayoutDriver : std::uint8_t {
      Canvas, ParentGroup, SelfFitter, AuthoredRect
  };
  UILayoutDriver ResolveDriver(bool canvasRoot, bool parentDrives,
                               bool fitterDrives) noexcept;
  ```

  Return the first true driver in `Canvas > parent group > self fitter > authored RectTransform`; `UILayoutElement` never appears as a driver.

- [x] **Step 6a: Allocate horizontal/vertical main-axis minimums.**

  Remove padding and fixed spacing. Preserve authored size for `controlChildSize=false`, clamped to minimum; initialize controlled children at minimum. If available space is below the sum, retain minimums and allow overflow.

- [x] **Step 6b: Distribute preferred and flexible deltas.**

  ```text
  distribute(extra, weights, caps):
    allocate proportional fixed raw units without exceeding each cap
    give each remaining raw unit to the first eligible sibling
  ```

  Apply once with weights `preferred-min` and caps `preferred`, then with flexible weights; `forceExpand` maps zero flexible weight to one.

- [x] **Step 6c: Place aligned cross-axis rectangles.**

  Honor min/preferred, child-control, expand, and authored alignment. Remaining main-axis space changes only leading offset; never stretch authored spacing.

- [x] **Step 6d: Arrange fixed and flexible grids.**

  Use authored positive cell size. `FixedColumns/FixedRows` uses the exact positive count. `Flexible` calls `CheckedFloorDiv(inner + spacing, cell + spacing)` and clamps the valid result to at least one. Start corner, fill axis, and sibling order uniquely determine each cell.

- [x] **Step 7a: Build the driven-property dependency graph.**

  Create one node for `(stableObjectId, Width)` and `(stableObjectId, Height)` when a group/fitter/intrinsic dependency exists; add directed edges before layout evaluation.

- [x] **Step 7b: Run Tarjan and mark cyclic property sets.**

  ```text
  strongConnect(v):
    assign index/lowlink; push v
    visit every edge in stable node order
    when lowlink == index, pop one SCC
    cyclic = size > 1 or self-edge
  ```

- [x] **Step 7c: Recompute cyclic axes from authored rectangles.**

  For the full cyclic SCC, ignore every dynamic driver on that axis and recompute from authored RectTransform in the current pass. Never read cached last-good geometry. Emit one rate-limited `LayoutCycle` diagnostic keyed by sorted stable object IDs and axis.

- [x] **Step 8a: Publish nodes in stable draw order without pointers.**

  Populate nodes in stable draw order, retain no live pointers, and sort once.

- [x] **Step 8b: Cache geometry and the complete immutable snapshot separately.**

  Store measured/arranged geometry in the bounded per-world LRU, then store the final `shared_ptr<const UISnapshot>` in the one latest full slot for `{worldGeneration,deviceGeneration}`. A full-key hit returns the same shared object with zero snapshot allocation; a miss atomically replaces the slot only after the new immutable snapshot is complete. Dirty propagation flows `intrinsic -> every ancestor layout`; a visual/content/interaction/runtime-binding-only edit may reuse geometry but must miss the full-snapshot slot and rebuild its render/hit payload. If any reachable `UIComponent` reports an exhausted revision or any aggregate generation is uncacheable, emit one blocker, clear `lastSnapshot_`, and bypass both lookup and insertion rather than risk stale reuse. Task 11.2 attaches submitted superseded snapshots to their exact GPU fence before dropping the final internal strong owner.

- [x] **Step 9a: Route the `UISystem` facade through `UILayoutSystem`.**

  Keep `UISystem` as the public facade, own one `UILayoutSystem`, and route production layout through `Build(World&, WindowId, FixedSize, sink)`.

- [x] **Step 9b: Give editor windows an owning-world authority.** Replace
  `Editor::SetGameObjects(vector*)` with `SetActiveWorld(World&)`; store the
  non-owning active `World*`, derive the hierarchy vector only as
  `world.Objects()`, and forward the same `World*` to Scene View and Game View.
  Update every edit/play/load transition in `main.cpp` before a window may
  render or hit-test.

- [x] **Step 9c: Convert Scene View UI calls.** Replace
  `SceneViewWindow::gameObjects_` as the UI authority with the injected
  `World*`. Its UI render and hit-test calls pass `*world_` to `UISystem`; world
  sprite/camera iteration may use `world_->Objects()`. A missing/stale world
  skips the surface rather than constructing generation zero.

- [x] **Step 9d: Convert Game View and output rendering.** Change
  `GameViewWindow` and `GameOutputRenderer::{Render,RenderLogical}` to accept a
  real `World&`; only camera/world traversal derives `world.Objects()`.
  Update `runtime_main.cpp`, `test_game_view.cpp`, and every
  `test_rendering_sdlgpu.cpp` call in the same slice.

- [x] **Step 9e: Remove generation-less UI overloads.** Delete every
  `UISystem` overload accepting only `vector<shared_ptr<GameObject>>` after the
  Scene View, Game View, GameOutputRenderer, standalone runtime, and tests all
  compile with `World&`. Do not retain an address-derived identity adapter or
  invent generation zero.

- [x] **Step 10: Run focused, sanitizer, and existing UI gates.**

  ```bash
  cmake --build --preset debug --target molga_engine molga_runtime \
    test_ui_layout test_ui_snapshot test_ui test_game_view \
    test_rendering_sdlgpu -j
  ctest --test-dir build/debug -R '^(test_ui_layout|test_ui_snapshot|test_ui|test_game_view|test_rendering_sdlgpu|runtime_smoke)$' --output-on-failure
  cmake --preset asan
  cmake --build --preset asan --target test_ui_layout test_ui_snapshot -j
  ctest --test-dir build/asan -R '^(test_ui_layout|test_ui_snapshot)$' --output-on-failure
  cmake --preset ubsan
  cmake --build --preset ubsan --target test_ui_layout test_ui_snapshot -j
  ctest --test-dir build/ubsan -R '^(test_ui_layout|test_ui_snapshot)$' --output-on-failure
  ```

  Expected: all selected Debug/ASan/UBSan tests pass with byte-stable snapshots.

- [x] **Step 11: Commit deterministic layout.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UILayoutSystem.* \
    src/UI/UIRuntimeInvalidation.* src/ECS/GameObject.* src/Core/World.* \
    src/ECS/Components/RectTransform.cpp src/Text/FontRepository.cpp \
    src/Text/FontFamilyResolver.cpp src/Text/TextLayoutService.cpp src/UI/UISystem.* \
    src/Core/AssetDatabase.cpp src/Core/TextureManager.cpp \
    src/Editor/Editor.* src/Editor/Windows/SceneViewWindow.* \
    src/Editor/Windows/GameViewWindow.* src/Rendering/GameOutputRenderer.* \
    src/main.cpp src/runtime_main.cpp tests/test_ui_layout.cpp \
    tests/test_ui_snapshot.cpp tests/test_game_view.cpp \
    tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: publish deterministic fixed UI layout"
  ```

#### Implementation record (2026-09-08)

Landed with the full suite at **106/106, 0 failures**, plus the Step 10 sanitizer gate
(`test_ui_layout`, `test_ui_snapshot` clean under ASan and UBSan).

**The defect this task kept reproducing: a seam with a consumer and no producer.**
`UIIntrinsicLayoutRegistry` was read at three call sites in `Build` and written by nobody
outside `tests/test_ui_layout.cpp`'s `PublishIntrinsic` helper. Every fitter, group and cycle
case was green on sizes the tests fed it themselves, while in a real session every `UILabel`
and `UITextInput` measured zero. The same shape appeared in Step 4j, where the epoch hook sat
inside an `if (artifact)` branch that `.fontfamily` assets never reach.

**Where the producer went, and why not where the brief first said.** The controller's first
instruction was to publish from inside `Build`'s measure pass. That was wrong twice over: the
header states the contract outright ("배치는 텍스트 서비스를 스스로 찾지 않는다(Build의
인자에 없다)"), and this task's own Produces line fixes `Build(World&, WindowId, FixedSize,
TextDiagnosticSink&)` — reaching a text service from there would have changed the signature,
which is Task 11.1 Step 3i's work. The implementing agent stopped and challenged the
instruction rather than following it; the correction is recorded here because the brief was
the controller's error, not the agent's.

The producer is `PublishLabelIntrinsic` in `UISystem::CollectRender`, the one place holding
both a confirmed immutable `TextLayout` and the target identity. It measures with a **second
request whose constraints are stripped**. Publishing the render layout's `intrinsicSize`
instead looks obviously right and oscillates: a fitter reads the size, resizes the rect, the
rect becomes next frame's constraint, and the value never settles — so `Publish` returns true
every frame and the unchanged-frame fast path can never hit again. The unconstrained
measurement depends only on text and style, so it converges on first publication.

That distinction was invisible to the first version of the test, which used the default
`NoWrap` label: with no wrapping, a width constraint changes nothing, and the oscillating
implementation passed all 17 assertions. Enabling `TextWrapMode::Word` made the constraint
load-bearing; the mutation then fails on constrained width 2326 against unconstrained 8905.

**Findings from the two review lenses, all fixed here.** Each fix was mutation-verified by
deleting it and confirming a named test goes red.

1. `Component::SetEnabled` never reached `UIComponent::Invalidate`, so disabling a `UICanvas`
   or `RectTransform` changed no revision and no epoch — while `Build` filters everything on
   `IsEnabled()`. The fast path returned a stale snapshot forever, and `enabled` is not in
   `canonicalAuthoredPayload`, so the geometry key could not tell the two states apart either.
2. Nothing in production ever called `OnWorldReleased`. Every retired generation leaked its
   geometry LRU, its full slot and its intrinsic bucket — on editor Play/Stop, on Open Scene,
   and on the shipping runtime's scene transitions. `World` now notifies at all five
   retirement sites through a registered handler, so `Core` does not depend on `UI`.
3. The editor's `AddExistingObject`/`InsertExistingObjectAt`/`RemoveObjectsByIds` mutated the
   raw object vector, bypassing `World::Add`'s hierarchy hook. That is paste, duplicate,
   delete, undo and prefab instantiate. Routed through new `World::InsertAt`/`RemoveByIds`.
4. `DeleteObjectCommand` never detached the deleted root, and `Build` descends by
   `GetChildren()` — so a deleted Canvas child kept getting a snapshot node. It now detaches,
   symmetrically with `Undo`'s `SetParent`.
5. `TextLayoutService` advanced the UI semantic epoch on **every** cold paragraph store,
   including world-space `TextRenderer2D`. One animated score counter would rebuild the whole
   UI snapshot every frame, destroying the 600-unchanged-frames property. Removed; the epoch
   now moves only through the intrinsic publisher, which knows the paragraph belongs to UI.
   The Step 4j case was re-pointed to assert the narrow contract rather than the broad one.
6. `viewportGeneration` was a bare counter, so Scene View and Game View alternating on the
   singleton produced 1,2,3,4… and no two builds ever shared a geometry key. It is now
   memoized per distinct viewport value, bounded at eight, so a returning viewport recovers
   its name. Eviction hands out a fresh name, which is a miss and never a wrong hit.
7. `node.dropped` was not restored on a geometry-cache hit, so clipped-out nodes entered the
   visual key on warm builds but not cold ones. The flags now travel with the cache entry.
   This one is only observable through the new `LastVisualKeyEntryCount()` accessor: the
   snapshot content comes from the cached node vector, so both versions render identically,
   and the global semantic epoch moves on every edit, so pointer identity cannot see it either.
8. The geometry key omitted the label state that `HasSizeDependentIntrinsicWidth` reads, so
   toggling wrap mode reused stale geometry. **First fix was too broad** — adding the label's
   whole revision broke "payload-only edits reuse geometry", caught by the existing suite, not
   by review. The key now carries the wrap mode itself; intrinsic size is already covered by
   `intrinsicGenerations`.
9. `AssetDatabase::IndexOne` read every texture twice per scan. The second read is now gated
   on `rec.hash` being unchanged **and** an identity already being published, so a first scan
   or a previously unreadable file still publishes.

**Approved deviations from the Files list.** `src/Text/FontFamilyResolver.cpp` owns no cache
and publishes nothing, so Step 4j has no boundary there; the family hook belongs in
`AssetDatabase`. `src/Core/TextureManager.cpp` is path-indexed with no authored GUID and no
source bytes, so the only identity it could publish is the upload ordinal that
`UITextureContentIdentity` forbids — its real obligation is the binding-generation axis, which
Tasks 11.1 and 11.2 list. `tests/test_ui_snapshot.cpp` and `tests/test_game_view.cpp` needed
no change: Step 1's obligation is CMake-side, and Step 9d's three signatures are not called
from either file. `tests/test_ui.cpp` is **outside** the Files list and was modified anyway —
the production-entry-point intrinsic case needs the `CollectRender` fixture that lives there.

**Carried forward.**

- **Attaching or detaching a UI component publishes nothing.** `UIComponent::Invalidate` is
  reached only from setters, so `AddComponent<UICanvas>()` on an object already in the world
  advances no revision and no epoch. Reported SUSPECTED by the quality lens and left alone
  here because the plan's mutation tables have no row for component attachment and inventing
  one silently would be worse. **Owner: Task 11.1**, which first makes an unpublished Canvas
  visible on screen.
- **Four mutation survivors remain in this task's own suite**, all rooted in the same fact:
  the global semantic generation is a scalar in the full key and moves on every edit, so
  per-field key *content* is nearly untestable through pointer identity. Deleting the whole
  `visualContent`/`interaction` collision key, the `LayoutCycle` rate limit, the `lru.splice`
  recency update, and the viewport-generation exhaustion check each leave the suite green.
  `LastVisualKeyEntryCount()` now gives partial observability. **Owner: Task 11.2**, which
  needs the collision key to be load-bearing when render payloads depend on it.
- **The legacy immediate path and the snapshot path are not proven to agree.**
  `UISystem::CollectRender` resolves float screen rects with per-node recursion; `Build`
  resolves checked 26.6 against a validated logical viewport with one gathered DFS. Only the
  legacy path renders at this milestone, so they cannot disagree yet. **Owner: Task 11.1.**

### Task 11.1: Publish concrete render and hit payloads with one nested clip

**Files:**

- Create: `tests/test_ui_render_clip.cpp`
- Create: `src/UI/UITextInputVisualState.h`
- Modify: `src/UI/UILayoutSnapshot.h`
- Modify: `src/UI/UILayoutSystem.h`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UIRuntimeInvalidation.h`
- Modify: `src/UI/UIRuntimeInvalidation.cpp`
- Modify: `src/Rendering/GraphicsDevice.h`
- Modify: `src/Rendering/GraphicsDevice.cpp`
- Create: `src/Rendering/TextureBindingRegistry.h`
- Create: `src/Rendering/TextureBindingRegistry.cpp`
- Modify: `src/Rendering/Texture.h`
- Modify: `src/Rendering/Texture.cpp`
- Modify: `src/Core/TextureManager.h`
- Modify: `src/Core/TextureManager.cpp`
- Modify: `tests/test_ui_snapshot.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: semantic layout nodes, authored `UIMask`/`UITextInput`, immutable `TextLayout`, value-only `UITextInputVisualState`, validated texture GUID/content SHA/content-derived stable ID plus generation-safe runtime bindings/lifetimes, and `UIDrawOrderKey`.
- Produces: `TextureRuntimeBindingIdentity`, `UIRuntimeBindingCacheIdentity`, `UIStableComponentKey`, `UIFrozenTarget`, frozen input-label ownership, concrete sprite/text/solid render variants with reserved command spans, multi-stage `UIHitTargetSnapshot`, immutable input IME geometry, and `UIPhysicalTransform`.

- [ ] **Step 1: Register the render/clip fixture with the common text runtime session.** Create `test_ui_render_clip` through `molga_add_text_test`, never `molga_add_test`/`doctest_main`, before its first immutable `TextLayout` or `TextLayoutService` fixture is constructed. The helper performs the idempotent dependency attach; do not add a separate attach call. Preserve its renderer/SDL labels and any target-specific properties after creation; reuse only the installed `TextRuntimeTestSession` and `MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT`.

- [ ] **Step 1a: Write failing nested clip parity tests.**

  ```cpp
  TEST_CASE("empty nested clip removes render and hit records together") {
      UIClipFixture f;
      f.ParentMask({0, 0, 64, 64});
      f.ChildMask({128, 0, 64, 64});
      f.AddButtonWithImageAndLabel();
      const auto snapshot = f.Build();
      CHECK(snapshot->renderItems.empty());
      CHECK(snapshot->hitTargets.empty());
  }

  TEST_CASE("render and hit records share the exact clip and order") {
      const auto snapshot = UIClipFixture::NestedVisible().Build();
      REQUIRE(snapshot->renderItems.size() == 1);
      REQUIRE(snapshot->hitTargets.size() == 1);
      CHECK(snapshot->renderItems[0].logicalClip ==
            snapshot->hitTargets[0].logicalClip);
      CHECK(snapshot->renderItems[0].order == snapshot->hitTargets[0].order);
  }
  ```

- [ ] **Step 1b: Write the failing clip edge-policy table.**

  Add this explicit edge-policy table:

  ```cpp
  const std::vector<ClipCase> cases{
      {"unmasked-overflow", std::nullopt, RectRaw(-10, 0, 80, 20), true, true},
      {"inactive-ancestor", RectRaw(0, 0, 64, 64), RectRaw(1, 1, 8, 8), false, false},
      {"disabled-mask", std::nullopt, RectRaw(70, 0, 8, 8), true, true},
      {"negative-origin", RectRaw(-32, -16, 64, 64), RectRaw(-1, 0, 2, 2), true, true},
      {"right-edge-exclusive", RectRaw(0, 0, 64, 64), RectRaw(64, 1, 1, 1), false, false},
  };
  for (const auto& c : cases) {
      const auto snapshot = UIClipFixture(c).Build();
      CHECK((!snapshot->renderItems.empty()) == c.renders);
      CHECK((!snapshot->hitTargets.empty()) == c.hits);
  }
  ```

- [ ] **Step 1c: Write a failing content-identity cache regression.**

  ```cpp
  TEST_CASE("same texture GUID with new content SHA republishes sprite payload") {
      UISnapshotPayloadFixture f;
      f.SetTexture("texture-guid", Sha256Of("pixels-a"));
      const auto first = f.Build();
      const auto geometryBuilds = f.GeometryBuildCount();
      f.SetTexture("texture-guid", Sha256Of("pixels-b"));
      const auto second = f.Build();
      REQUIRE(first);
      REQUIRE(second);
      REQUIRE(std::holds_alternative<UISpriteSnapshot>(
          second->renderItems.at(0).payload));
      CHECK(first.get() != second.get());
      CHECK(f.GeometryBuildCount() == geometryBuilds);
      CHECK(std::get<UISpriteSnapshot>(second->renderItems.at(0).payload)
                .textureContentSha256 == Sha256Of("pixels-b"));
  }
  ```

  Add table rows for label text/color/layout identity, image tint, interactability, explicit navigation, and mask enablement; assert the second snapshot's concrete render/hit value, not only pointer inequality.

- [ ] **Step 1d: Write a failing same-content device-recreate binding test.**

  ```cpp
  TEST_CASE("same texture content republishes bindings after device recreation") {
      UISnapshotPayloadFixture f;
      f.SetTextureContent("texture-guid", Sha256Of("same-pixels"));
      f.BindTexture(DeviceBinding{/*device*/ 11, /*upload*/ 3,
                                  f.TextureHandleA(), f.SamplerHandleA(), 101});
      const auto oldSnapshot = f.Build();
      const auto oldJson = StableSnapshotJson(*oldSnapshot);
      const auto geometryBuilds = f.GeometryBuildCount();
      const auto oldLifetime = f.Sprite(*oldSnapshot).resourceLifetime;
      f.BindTexture(DeviceBinding{/*device*/ 12, /*upload*/ 1,
                                  f.TextureHandleB(), f.SamplerHandleB(), 202});
      const auto newSnapshot = f.Build();
      REQUIRE(oldSnapshot);
      REQUIRE(newSnapshot);
      CHECK(oldSnapshot.get() != newSnapshot.get());
      CHECK(f.GeometryBuildCount() == geometryBuilds);
      CHECK(f.Sprite(*newSnapshot).binding.texture == f.TextureHandleB());
      CHECK(f.Sprite(*newSnapshot).binding.sampler == f.SamplerHandleB());
      CHECK(f.Sprite(*newSnapshot).resourceLifetime != oldLifetime);
      CHECK(f.LifetimeAlive(oldLifetime));
      CHECK(StableSnapshotJson(*newSnapshot) == oldJson);
  }
  ```

- [ ] **Step 1e: Write the failing runtime-binding field matrix.**

  Starting from one warm fixture with unchanged GUID/SHA/tint, change exactly one of `deviceGeneration`, `uploadGeneration`, `texture`, `sampler`, and `lifetimeIdentity` per fresh row. The device row strictly advances `UIRuntimeGenerationSnapshot::deviceGeneration`; every other row strictly advances `textureBindingGeneration`. Each row reuses geometry, misses the full-snapshot cache, publishes the changed binding field, and keeps canonical JSON byte-identical. Exact `TextureHandle`/`SamplerHandle` equality includes resource index and handle generation; no hash-only comparison is allowed.

- [ ] **Step 1f: Write a failing binding-generation exhaustion test.**

  Set the process-global texture-binding or device clock to `UINT64_MAX`, request reupload/device replacement, and require no new binding publication, no zero/reused generation, one blocker diagnostic, full-snapshot cache lookup/insertion disabled, and the prior snapshot/lifetime unchanged.

- [ ] **Step 1g: Write a failing multi-stage frozen-hit target test.**

  ```cpp
  TEST_CASE("one hit freezes action focus text and inner-to-outer scroll targets") {
      UIMultiTargetFixture f;
      const auto snapshot = f.BuildButtonSelectableInputInNestedScrolls();
      REQUIRE(snapshot->hitTargets.size() == 1);
      const auto& hit = snapshot->hitTargets.front();
      CHECK(hit.target == f.ButtonActionIdentity());
      REQUIRE(hit.focusTarget);
      CHECK(hit.focusTarget->runtimeTarget == f.SelectableIdentity());
      REQUIRE(hit.textInputTarget);
      CHECK(hit.textInputTarget->runtimeTarget == f.TextInputIdentity());
      REQUIRE(hit.scrollTargets.size() == 2);
      CHECK(hit.scrollTargets[0].runtimeTarget == f.InnerScrollIdentity());
      CHECK(hit.scrollTargets[1].runtimeTarget == f.OuterScrollIdentity());
  }
  ```

  Add a disabled selectable row (no `focusTarget`), disabled text input row (no `textInputTarget`), and invalid/replaced ancestor scroll row (omitted rather than replaced by another component with the same object ID). Every frozen pair contains complete runtime and canonical identities from snapshot N.

  Add a second fixture shaped like the qualification input shell: one object has
  `RectTransform`, enabled `UIMask`, enabled/interactable `UISelectable`, and an
  enabled/usable `UITextInput`, but no `UIButton`, `UIImage`, or `UILabel`.
  Require exactly one hit record whose action and focus targets are the
  `UISelectable` and whose text target is the `UITextInput`; pointer-down must
  therefore reach focus/text-owner acquisition without a decorative visual.

- [ ] **Step 1h: Write failing input-controlled label ownership tests.**

  ```cpp
  TEST_CASE("UITextInput-owned labels never also render as ordinary labels") {
      UITextInputLabelFixture f;
      const auto snapshot = f.BuildActiveInput();
      CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
      CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
      REQUIRE(snapshot->textInputLabels.size() == 1);
      CHECK(snapshot->textInputLabels[0].renderedLabel.label.runtimeTarget ==
            f.RenderedLabelIdentity());
  }
  ```

  Add exact rows for two inputs claiming one rendered label; one input using one label as both roles; unset/wrong-type/out-of-world rendered ref; disabled rendered label; unset/invalid/disabled placeholder; disabled/inactive input; inactive label ancestor; and empty-unfocused versus focused/nonempty placeholder visibility. Conflicting resolved labels are suppressed from ordinary output and every affected input binding/text-input hit target is omitted with one `ReferenceInvalid` blocker. A missing/invalid/disabled rendered label disables that input's visual/text-input target; a missing/invalid/disabled placeholder only disables placeholder output. An inactive/disabled input makes no ownership claim, so an otherwise independent active label follows ordinary `UILabel` visibility.

  Give the input and rendered label deliberately different family, size,
  language, wrap, and alignment values. Require the frozen effective input
  request to use `UITextInput::fontFamilyGuid` plus every
  `UITextInput::paragraphStyle` field while taking only viewport constraints,
  color, label identity, and diagnostic provenance from the rendered label.
  The top-level input family is the sole family authority: copy the input style
  and then set `ParagraphStyle::fontFamilyGuid` from the top-level field; never
  consume or serialize a second nested family value.

- [ ] **Step 1i: Write failing value-provider and blink-off IME-geometry shape tests.**

  Compile a `RecordingUITextInputVisualStateProvider`, pass it plus an exact nonzero surface window explicitly to `UILayoutSystem::Build`, and require one `(surfaceWindowId,inputIdentity)` lookup for each valid active input only after the scalar fast path misses. A provider result is copied into the immutable snapshot; mutating the provider afterward does not change the published value. Task 14.2 adds the concrete layout assertions that focused state publishes `UITextInputImeGeometrySnapshot` even when `caretVisible=false`.

- [ ] **Step 1j: Write failing command-span and C++17 value-equality tests.**

  Construct a text layout with three drawable positioned glyphs followed by a
  solid item. Require `reservedCommandSpan==3`, require the next item's
  `stableSubmissionIndex` to equal `text.order.stableSubmissionIndex+3`, and
  fail on checked addition overflow. Add a space/zero-bitmap record between a
  drawable glyph and following solid: it consumes one reserved ordinal, emits
  no command, and the solid still begins after the complete record span. Add
  one-field-difference `==`/`!=` matrices for `UIStableComponentKey`,
  `UIFrozenTarget`, `TextureRuntimeBindingIdentity`,
  `UIRuntimeBindingCacheIdentity`, and `PixelRectU32`.

- [ ] **Step 2: Run the render/hit payload red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_render_clip test_ui_snapshot -j
  ```

  Expected: compilation fails because `UISnapshot` has no concrete render/hit vectors.

- [ ] **Step 3a: Define the stable canonical source key.**

  ```cpp
  namespace molga::ui {
  struct UIStableComponentKey {
      unsigned int sceneObjectId = 0;
      std::string componentTypeName;
      std::uint32_t componentSchemaVersion = 0;
      bool operator==(const UIStableComponentKey&) const noexcept;
      bool operator!=(const UIStableComponentKey& other) const noexcept {
          return !(*this == other);
      }
  };

  struct UIFrozenTarget {
      UIRuntimeTargetIdentity runtimeTarget;
      UIStableComponentKey canonicalTarget;
      explicit operator bool() const noexcept {
          return static_cast<bool>(runtimeTarget);
      }
      bool operator==(const UIFrozenTarget&) const noexcept;
      bool operator!=(const UIFrozenTarget& other) const noexcept {
          return !(*this == other);
      }
  };
  } // namespace molga::ui
  ```

- [ ] **Step 3b: Define exact runtime binding and cache identities.**

  ```cpp
  namespace molga::ui {
  struct TextureRuntimeBindingIdentity {
      std::uint64_t deviceGeneration = 0;
      std::uint64_t uploadGeneration = 0;
      molga::TextureHandle texture;
      molga::SamplerHandle sampler;
      std::uint64_t lifetimeIdentity = 0;
      bool operator==(const TextureRuntimeBindingIdentity&) const noexcept;
      bool operator!=(const TextureRuntimeBindingIdentity& other) const noexcept {
          return !(*this == other);
      }
  };

  struct UIRuntimeBindingCacheIdentity {
      unsigned int sceneObjectId = 0;
      std::string componentTypeName;
      std::uint32_t componentSchemaVersion = 0;
      TextureRuntimeBindingIdentity binding;
      bool operator==(const UIRuntimeBindingCacheIdentity&) const noexcept;
      bool operator!=(const UIRuntimeBindingCacheIdentity& other) const noexcept {
          return !(*this == other);
      }
  };
  } // namespace molga::ui
  ```

  Extend `UISnapshotCacheKey` with `std::vector<UIRuntimeBindingCacheIdentity> runtimeBindings` in the same stable Canvas DFS order. The broad `textureBindingGeneration`/`deviceGeneration` scalars invalidate the allocation-free fast path; the original source fields and every binding field above are retained and collision-compared in the full key. None enters the geometry key or canonical JSON.

- [ ] **Step 3c: Define the weak-record binding registry contract.**

  ```cpp
  namespace molga {
  class TextureBindingLifetime {
  public:
      const ui::TextureRuntimeBindingIdentity& Identity() const noexcept;
  private:
      friend class TextureBindingRegistry;
      ui::TextureRuntimeBindingIdentity identity_;
  };

  class TextureBindingRegistry {
  public:
      std::shared_ptr<const TextureBindingLifetime> Publish(
          const ui::TextureRuntimeBindingIdentity&);
      std::size_t LiveRetainedBindingCount(
          std::uint64_t deviceGeneration) const;
      bool DestroyRetiredBindings(std::uint64_t deviceGeneration,
                                  std::string& errorOut);
  };
  } // namespace molga
  ```

  Registry records retain the handles/identity needed for deferred destruction but only a `weak_ptr` to each lifetime token. After GPU drain and explicit release of cache/latest-frame/texture-manager strong owners, a non-expired weak token is therefore a real external retained snapshot/command owner—not an inferred `shared_ptr::use_count()` threshold. `DestroyRetiredBindings` refuses while any such token remains.

- [ ] **Step 3d: Define retained binding lifetime and concrete sprite/text payloads.**

  ```cpp
  namespace molga::ui {
  struct UISpriteSnapshot {
      std::string textureGuid;
      std::string textureContentSha256;
      std::uint64_t textureContentStableId = 0;
      TextureRuntimeBindingIdentity binding;
      Color tint = Color::White();
      std::shared_ptr<const molga::TextureBindingLifetime> resourceLifetime;
  };

  struct UITextSnapshot {
      std::shared_ptr<const molga::text::TextLayout> layout;
      molga::FixedPoint origin;
      Color color = Color::White();
  };

  } // namespace molga::ui
  ```

  `textureContentStableId` is derived deterministically from the validated GUID/SHA tuple and is collision-checked against both original strings in snapshot/cache equality. `binding` is runtime-only and must agree field-for-field with the retained lifetime object. Do not copy a process-local asset revision, binding field, pointer, or GPU-handle value into canonical identity.

- [ ] **Step 3e: Define the value-only runtime input visual-provider boundary.**

  Create `src/UI/UITextInputVisualState.h` with no pointer to edit-system storage:

  ```cpp
  namespace molga::ui {
  struct UITextInputVisualState {
      molga::WindowId surfaceWindowId = 0;
      UIRuntimeTargetIdentity input;
      std::string committedUtf8;
      molga::text::CaretPosition caret;
      molga::text::GraphemeRange selection;
      std::string compositionUtf8;
      molga::text::GraphemeRange compositionSelection;
      bool focused = false;
      bool caretVisible = false;
      std::uint64_t editRevision = 0;
      std::uint64_t surfaceRevision = 0;
      bool operator==(const UITextInputVisualState&) const;
      bool operator!=(const UITextInputVisualState& other) const {
          return !(*this == other);
      }
  };

  struct UITextInputGeometryCacheIdentity {
      UIRuntimeTargetIdentity input;
      // Complete effective input request, including current visible UTF-8 and
      // input-owned family/style plus the frozen viewport constraints.
      molga::text::TextLayoutRequest effectiveRequest;
      bool operator==(const UITextInputGeometryCacheIdentity&) const;
      bool operator!=(const UITextInputGeometryCacheIdentity& other) const {
          return !(*this == other);
      }
  };

  class UITextInputVisualStateProvider {
  public:
      virtual ~UITextInputVisualStateProvider() = default;
      virtual std::optional<UITextInputVisualState> GetVisualState(
          molga::WindowId, const UIRuntimeTargetIdentity&) const = 0;
  };

  class EmptyUITextInputVisualStateProvider final
      : public UITextInputVisualStateProvider {
  public:
      static const EmptyUITextInputVisualStateProvider& Instance();
      std::optional<UITextInputVisualState> GetVisualState(
          molga::WindowId,
          const UIRuntimeTargetIdentity&) const override {
          return std::nullopt;
      }
  };
  } // namespace molga::ui
  ```

  `UILayoutSystem::Build` takes this provider and the exact renderer-owned
  `TextLayoutService&` explicitly. Its direct layout calls retain Task 8's
  per-call font-artifact-store gate at the service/repository edge; they never
  bypass it through a wrapper-only check. The provider returns a complete value
  copy; neither the provider nor edit-system storage is retained in `UISnapshot`.
  Until Task 14 installs the real provider, editor/runtime/tests pass
  `EmptyUITextInputVisualStateProvider::Instance()`.

  Extend `UISnapshotCacheKey` with stable-DFS-ordered `std::vector<UITextInputVisualState> inputVisualStates` and extend `UILayoutGeometryCacheKey` with stable-DFS-ordered `std::vector<UITextInputGeometryCacheIdentity> inputGeometry`. Build each geometry identity from the current committed-plus-composition visible UTF-8 and the complete effective input request. Collision-compare every original field after a hash match and keep capacity-retaining scratch for the strings/requests. Selection endpoints, caret/affinity, focus, blink, and surface revision remain full-key-only, so their changes reuse geometry; a visible text/composition or effective-request change cannot hit pre-edit intrinsic/arranged geometry. The aggregate semantic generation invalidates the allocation-free fast path, and runtime values remain absent from scene/prefab JSON.

- [ ] **Step 3f: Freeze input label templates and immutable IME geometry.**

  ```cpp
  namespace molga::ui {
  struct UIFrozenInputLabelVisual {
      UIFrozenTarget label;
      molga::text::TextLayoutRequest requestTemplate;
      Color color = Color::White();
      bool enabledAndVisible = false;
  };
  struct UITextInputLabelSnapshot {
      UIFrozenTarget input;
      UIFrozenInputLabelVisual renderedLabel;
      std::optional<UIFrozenInputLabelVisual> placeholderLabel;
      molga::text::TextLayoutRequest effectiveInputRequestTemplate;
      molga::FixedRect logicalViewport;
      std::optional<molga::FixedRect> logicalClip;
      UIDrawOrderKey baseOrder;
  };
  struct UITextInputImeGeometrySnapshot {
      UIFrozenTarget input;
      molga::FixedRect logicalInputArea;
      molga::FixedPoint logicalCursor;
      std::optional<molga::FixedRect> logicalClip;
      bool focused = false;
      bool operator==(const UITextInputImeGeometrySnapshot&) const noexcept;
      bool operator!=(const UITextInputImeGeometrySnapshot& other) const noexcept {
          return !(*this == other);
      }
  };
  } // namespace molga::ui
  ```

  Add stable-order `textInputLabels` and `textInputImeGeometry` vectors to `UISnapshot`. Build `effectiveInputRequestTemplate` by copying the input's paragraph style, overwriting its embedded family with the top-level `UITextInput::fontFamilyGuid`, and taking only width/height constraints and diagnostic provenance from the rendered label/viewport. The rendered label's own font/style is provenance, not runtime input-text authority; its color remains the input text color. Task 14 replaces only the effective template's UTF-8 with current visible text. Runtime caret/selection/focus/blink revisions live in the full snapshot key, while the exact effective visible request lives in `inputGeometry`, so caret/blink-only changes reuse shaping/layout and visible edits cannot reuse stale intrinsic geometry. IME geometry is a value in the published snapshot and does not depend on caret draw visibility.

- [ ] **Step 3g: Define solid payload and concrete render item.**

  ```cpp
  namespace molga::ui {
  struct UISolidRectSnapshot { Color color = Color::White(); };
  using UIRenderPayload = std::variant<UISpriteSnapshot, UITextSnapshot,
                                       UISolidRectSnapshot>;

  struct UIRenderItemSnapshot {
      UIRuntimeTargetIdentity source;
      UIStableComponentKey canonicalSource;
      UIDrawOrderKey order;
      molga::FixedRect logicalRect;
      std::optional<molga::FixedRect> logicalClip;
      std::uint64_t reservedCommandSpan = 1;
      UIRenderPayload payload;
  };

  } // namespace molga::ui
  ```

- [ ] **Step 3h: Define concrete hit item and snapshot vectors.**

  ```cpp
  namespace molga::ui {
  struct UINavigationSnapshot {
      UINavigationMode mode = UINavigationMode::None;
      std::array<std::optional<UIRuntimeTargetIdentity>, 4> explicitTargets;
      std::array<std::optional<UIStableComponentKey>, 4> canonicalTargets;
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
      bool interactable = false;
      bool focusable = false;
      bool acceptsTextInput = false;
      UINavigationSnapshot navigation;
  };
  } // namespace molga::ui
  ```

  Create at most one hit record per active object. Choose its action target in
  this exact precedence: enabled/interactable `UIButton`; otherwise enabled
  `UIImage`; otherwise enabled `UILabel`; otherwise enabled/interactable
  `UISelectable`; otherwise enabled/usable `UITextInput`. The first three
  preserve the source-compatible visual-action route, while the final two make
  an undecorated selectable/input shell interactive. Omit the record only when
  no action candidate exists. `focusTarget` independently resolves the sibling
  enabled/interactable `UISelectable`; `textInputTarget` independently resolves
  the sibling enabled/usable `UITextInput`; `scrollTargets` walks resolved
  ancestor `UIScrollView` components from innermost to outermost. A valid input
  label claim may suppress label rendering without deleting the shell's single
  action record; the Step 1h failure table alone decides when the text target is
  omitted. Extend `UISnapshot` with sorted `renderItems`, `hitTargets`,
  `textInputLabels`, and `textInputImeGeometry`. A consumer may not consult
  authored components to complete any payload.

- [ ] **Step 3i: Change `UILayoutSystem::Build` and all current call sites atomically.**

  ```cpp
  UISnapshotPtr UILayoutSystem::Build(
      World&, molga::WindowId surfaceWindowId, molga::FixedSize,
      const UITextInputVisualStateProvider&,
      molga::text::TextLayoutService&,
      molga::text::TextDiagnosticSink&);
  ```

  Update `UISystem`, editor/runtime main paths, and every Task 10/11 fixture in this commit. Delete the old three-argument overload; tests that do not yet exercise runtime editing pass `EmptyUITextInputVisualStateProvider::Instance()` and the same production `TextLayoutService` reference.

- [ ] **Step 4a: Add checked fixed-rect intersection.**

  ```cpp
  std::optional<molga::FixedRect> IntersectFixedRects(
      const molga::FixedRect&, const molga::FixedRect&) noexcept;
  ```

  Compute max(min edges), min(max edges), and return `nullopt` for empty or checked-arithmetic failure.

- [ ] **Step 4b: Carry one inherited clip through snapshot DFS.**

  Carry `optional<FixedRect> inheritedClip` through Canvas DFS. An enabled `UIMask` intersects its logical rect with the inherited clip; an empty result prunes both visual and interactive descendants. A disabled/no mask carries the parent clip unchanged.

- [ ] **Step 4c: Copy the identical clip/order into render and hit records.**

  Store one calculated clip and `UIDrawOrderKey` into every render and hit item created for a source, then sort both vectors by that key.

- [ ] **Step 4d: Pre-resolve and suppress input-controlled labels.**

  Before ordinary label emission, gather active/enabled input claims in stable Canvas DFS order and resolve each ref to an enabled `UILabel` in the same world/Canvas. Build a label-identity ownership map, detect every multi-owner/multi-role conflict before publication, and apply the Step 1h table atomically. Valid claimed rendered/placeholder labels populate `UITextInputLabelSnapshot` and are omitted from ordinary `UILabel` render items. Do not mutate either `UILabel`, and do not infer ownership later in Task 14.

- [ ] **Step 4e: Freeze multi-stage hit targets and command spans.**

  For each object, apply Step 3h's exact action precedence once, then copy that
  pair plus its sibling focus/text-input pairs and all resolved scroll
  ancestors. Never create duplicate records for the sibling roles. For each
  render item, set `reservedCommandSpan=1` for sprite/solid and use checked
  `TextRenderCommandSpan(*layout)`—the sum of positioned glyph/tofu records—for
  text. Reserve the next item's `stableSubmissionIndex` only after checked
  addition of the prior span; overflow emits `LayoutInvalid` and omits the
  complete source group, never a partially ordered group.

- [ ] **Step 5a: Publish non-wrapping device generations.**

  Every successfully created graphics device uses the unique value returned by `UIRuntimeInvalidationClock::Advance(Device)` as its process-global nonzero `deviceGeneration` before publication and exposes it through `std::uint64_t GraphicsDevice::Generation() const noexcept`. If acquisition is exhausted, destroy the unpublished device, retain the old active device, disable affected snapshot caching, and emit a blocker.

- [ ] **Step 5b: Publish non-wrapping texture binding values.**

  Every successful texture creation/reupload acquires a checked per-texture `uploadGeneration`, a process-global nonzero `lifetimeIdentity`, and the unique value from `Advance(TextureBinding)` before swapping the current binding. If any acquisition is exhausted, destroy the unpublished resource after the required GPU fence, retain the old binding, disable affected snapshot caching, and emit a blocker; never publish zero or reuse a tuple.

- [ ] **Step 5c: Retain immutable runtime bindings across snapshot lifetimes.**

  `Texture` exposes one `shared_ptr<const TextureBindingLifetime>` whose value contains the exact `TextureRuntimeBindingIdentity`. Create a new lifetime object and new handles before atomically replacing a binding; do not update/destroy a handle retained by an existing snapshot. Dropping the texture manager's old pointer merely retires it—the binding registry releases its handles only after the final snapshot/command owner and GPU fence are complete.

- [ ] **Step 5d: Pin sprite content identity, runtime binding, and lifetime.**

  Resolve and validate the texture's authored GUID, full content SHA-256, content-derived stable ID, complete runtime binding, and lifetime while building the snapshot. Require `resourceLifetime->Identity() == binding` and append the matching `UIRuntimeBindingCacheIdentity`. Compare GUID/full SHA and every runtime-binding field after any hash match. If resolution fails, emit the typed diagnostic and publish the approved missing-texture payload, never a half-populated variant. Content change or same-content binding change misses the full-snapshot cache while permitting geometry reuse.

- [ ] **Step 5e: Pin immutable label layout.**

  Resolve `UILabel` to one immutable `TextLayout` and store its shared pointer plus origin/color in `UITextSnapshot`. Missing glyphs remain procedural tofu within the valid layout.

- [ ] **Step 5f: Freeze navigation policy into hit records.**

  Copy authored `UINavigationMode` into `UINavigationSnapshot`. Resolve each explicit ref once while building N and store both its complete runtime identity and canonical stable key; invalid refs remain `nullopt` with `ReferenceInvalid`. Same-batch focus projection later consumes only these frozen fields.

- [ ] **Step 6: Add the sole physical rectangle conversion.**

  ```cpp
  struct UIPhysicalTransform {
      molga::FixedRect logicalViewport;
      molga::PixelRectU32 physicalViewport;
      std::uint64_t deviceGeneration = 0;
      std::optional<molga::PixelRectU32> ToPhysicalOutward(
          const molga::FixedRect&) const noexcept;
      std::optional<molga::FixedPoint> ToLogicalPoint(
          double outputPixelX, double outputPixelY) const noexcept;
      std::optional<TextAffine2D> LayoutToOutputAffine(
          molga::FixedPoint logicalOrigin) const noexcept;
      std::optional<TextRasterPolicy> RasterPolicy(
          molga::text::TextDiagnosticSink&) const;
  };
  ```

  `deviceGeneration` is the exact current `GraphicsDevice::Generation()` and must be nonzero before collection. `ToPhysicalOutward` translates relative to the logical viewport, applies checked rational scale, floors each min edge, ceils each max edge, clamps to the physical viewport, and returns `nullopt` for empty, invalid, or overflowed output. `ToLogicalPoint` is the sole inverse point conversion: require a finite point inside the half-open physical viewport, subtract its origin, apply the checked inverse rational scale, add the logical origin, and round once to 26.6 with the shared half-away rule; outside/invalid/overflow returns `nullopt`. `LayoutToOutputAffine` returns the Task 8 affine `physicalViewport.min + (logicalOrigin + local - logicalViewport.min) * scale`: `m00=scaleX`, `m11=scaleY`, `m01=m10=0`, `tx=physicalViewport.x+(logicalOrigin.x-logicalViewport.x)*scaleX`, and likewise for `ty`; reject non-finite/zero logical extents and float overflow. Tests assert exact outward edges, inverse half-open boundaries, round trips, and all six affine fields at backing scales 1 and 2 plus nonzero viewport origins.

  `RasterPolicy` uses Task 8's checked integer
  `max(physicalWidth*4096/logicalWidthRaw,
  physicalHeight*4096/logicalHeightRaw)` half-away rule, requires Q10.6
  `[1,65535]`, and returns the same value used by every UI text item collected
  under this transform. It never derives scale from the affine's floats.

  Add explicit field-wise C++17 `==`/`!=` to `PixelRectU32`; do not compare object representation or rely on C++20 rewritten operators.

- [ ] **Step 7: Extend stable snapshot JSON with semantic payload fields only.**

  Include `UIStableComponentKey`, canonical action/focus/text/inner-to-outer-scroll targets, canonical input-label ownership/roles, texture GUID/full content SHA/content-derived stable ID, immutable text layout structure, raw fixed rect/clip, reserved command span, payload kind, draw order, navigation mode, and canonical explicit-navigation targets. Exclude every runtime identity (including surface window, stage/navigation runtime targets), runtime edit/owner state, `TextureRuntimeBindingIdentity` in full, process-local texture/upload/device generations, native texture/sampler handles, lifetime tokens, atlas page/UV, and physical pixels. The device-recreate fixture must therefore produce byte-identical JSON.

- [ ] **Step 8: Run snapshot/clip tests.**

  ```bash
  cmake --build --preset debug --target test_ui_render_clip test_ui_snapshot test_ui_layout -j
  ctest --test-dir build/debug -R '^(test_ui_render_clip|test_ui_snapshot|test_ui_layout)$' --output-on-failure
  ```

  Expected: all selected tests pass, and render/hit vectors remain order- and clip-identical.

- [ ] **Step 9: Commit concrete snapshot payloads.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UILayoutSnapshot.h \
    src/UI/UILayoutSystem.* src/UI/UIRuntimeInvalidation.* \
    src/UI/UITextInputVisualState.h src/UI/UISystem.* \
    src/Rendering/GraphicsDevice.* src/Rendering/TextureBindingRegistry.* \
    src/Rendering/Texture.* \
    src/Core/TextureManager.* src/main.cpp src/runtime_main.cpp \
    tests/test_ui_render_clip.cpp tests/test_ui_snapshot.cpp
  git commit -m "feat: publish complete UI render and hit snapshots"
  ```

### Task 11.2: Enforce snapshot scissors through the render queue and SDL_GPU pass

**Files:**

- Create: `src/UI/UIRenderCollector.h`
- Create: `src/UI/UIRenderCollector.cpp`
- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/Rendering/RenderQueue.h`
- Modify: `src/Rendering/RenderSystem2D.cpp`
- Modify: `src/Rendering/Renderer.h`
- Modify: `src/Rendering/Renderer.cpp`
- Modify: `src/Rendering/SpriteBatcher.h`
- Modify: `src/Rendering/SpriteBatcher.cpp`
- Modify: `src/Rendering/GraphicsDevice.h`
- Modify: `src/Rendering/GraphicsDevice.cpp`
- Modify: `src/Rendering/TextureBindingRegistry.h`
- Modify: `src/Rendering/TextureBindingRegistry.cpp`
- Modify: `src/Rendering/Texture.h`
- Modify: `src/Rendering/Texture.cpp`
- Modify: `src/Core/TextureManager.h`
- Modify: `src/Core/TextureManager.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/UI/UILayoutSystem.h`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_render_queue.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `tests/test_gpu_retirement.cpp`
- Modify: `tests/test_ui_render_clip.cpp`
- Modify: `tests/test_ui_snapshot.cpp`
- Modify: `tests/test_platform_sdl.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: complete `UIRenderItemSnapshot` values and `UIPhysicalTransform`.
- Produces: `UIRenderCollector::Collect`, the extended existing `TextCollectContext`, optional command UI order/scissor, deterministic clip transitions, `Renderer::ResetPassScissor`, and fail-closed device-binding teardown.

- [ ] **Step 1a: Write the failing scissor command-stream audit.**

  Add three adjacent commands with scissor A, A, B, followed by an unclipped command. Require the audit sequence `set A, draw, draw, flush, set B, draw, flush, reset full, draw`, exactly two clip-change flushes, no flush for equal A, and full-pass restore before batch end.

- [ ] **Step 1b: Write the failing batch-identity invariant.**

  ```cpp
  TEST_CASE("scissor is not batch identity") {
      molga::RenderCommand a;
      molga::RenderCommand b;
      a.batchKey.shaderName = b.batchKey.shaderName = "batch";
      a.scissor = molga::PixelRectU32{0, 0, 10, 10};
      b.scissor = molga::PixelRectU32{10, 0, 10, 10};
      CHECK(a.batchKey == b.batchKey);
  }
  ```

- [ ] **Step 2a: Add failing image/text clip-order integration tests.**

  Build one clipped image followed by clipped text under a logical viewport `(10,20,200,100)` mapped to physical `(50,70,400,300)`. Assert every text glyph command carries the exact same final physical scissor and translated UI order as its `UITextSnapshot`, plus `layoutToOutput={2,0,0,3,70,100}` for logical text origin `(20,30)`. No text method recomputes a clip/transform from a component.

- [ ] **Step 2b: Write a failing device teardown order audit.**

  ```cpp
  TEST_CASE("device teardown waits idle and refuses external UI snapshot owner") {
      UIDeviceTeardownFixture f;
      auto externalSnapshot = f.BuildSnapshotWithTexture();
      CHECK_FALSE(f.Shutdown());
      CHECK((f.Calls() == std::vector<std::string>{
          "WaitIdle", "ReleaseCompletedGpuLifetimes",
          "ClearFullSnapshotBindingCache",
          "ReleaseEngineSnapshots", "ReleaseTextureManagerBindings",
          "ExternalBindingOwnerBlocked"}));
      CHECK(f.DeviceAlive());
      CHECK(f.BindingAlive(f.Sprite(*externalSnapshot).resourceLifetime));
      f.ClearCalls();
      externalSnapshot.reset();
      CHECK(f.Shutdown());
      CHECK((f.Calls() == std::vector<std::string>{
          "ReleaseGlyphAtlas", "DestroyTextServices",
          "DestroyRetiredTextureBindings", "DestroyRendererResources",
          "DestroyGraphicsDevice"}));
  }
  ```

- [ ] **Step 2c: Replace the interim abort row with failing retryable-drain rows.**

  Delete Task 6.2's `SIGABRT` subprocess assertion and its terminal-abort expectation in this same slice. A failed `WaitIdle`/fence drain must instead return `EngineShutdownStatus::GpuDrainFailed`, leave caches, snapshot owners, texture bindings, atlas, text services, renderer resources, host, and device untouched, and emit one blocker; a second call after the injected failure clears must complete without repeating a successful drain phase. Add an independent external glyph-page token row: after successful drain/internal-owner release it returns `ExternalGpuLifetime` with atlas/services/device unchanged, then after the token expires the retry logs `ReleaseGlyphAtlas` before `DestroyTextServices` and completes without another idle wait. A successful device-generation change must clear the full-snapshot binding cache/fast-path pointer while retaining semantic geometry/content caches; rebuilding same content publishes the new device binding. No test or production branch may require both abort and retry behavior after this commit.
  Add an application-owner audit around both rows: before `Complete`, markers
  `ReturnFromEngine`, `DestroyDiagnosticSink`, and `DestroyTextRuntimeGuard`
  remain absent and the host pointer stays nonnull; after the known external
  owner is released and retry completes, those markers occur once in that
  order. A repeated-failure row first proves the shutdown-blocked state has
  zero ordinary return/destructor markers, then clears only the injected fault
  and requires a final `Complete` retry before fixture scope may exit; the test
  never silently destroys the retained host.

- [ ] **Step 2d: Write the failing mixed text-span order test.**

  ```cpp
  TEST_CASE("text reserves every glyph command before underline and caret") {
      UIRenderOrderFixture f;
      f.AddText(/* base */ 40, /* drawable glyphs */ 3);
      f.AddCompositionUnderline(/* expected base */ 43);
      f.AddCaret(/* expected base */ 44);
      const auto commands = f.Collect();
      CHECK((f.UISubmissionIndices(commands) ==
             std::vector<std::uint64_t>{40, 41, 42, 43, 44}));
      CHECK((f.Kinds(commands) ==
             std::vector<std::string>{"glyph", "glyph", "glyph",
                                      "underline", "caret"}));
  }
  ```

  Add zero-glyph text (span zero, no command), a non-drawable space record
  (span one, no command but one ordinal consumed), tofu (one command per missing
  positioned glyph), and `UINT64_MAX` checked-reservation failure rows. A text
  item may not reserve only one submission index merely because it emits fewer
  drawable commands.

- [ ] **Step 2e: Write failing bounded reupload/fence-retirement churn.**

  Build, collect, and submit 4096 same-content reuploads in one world/device while completing each preceding fixture fence. After every replacement require one full-snapshot slot and at most the renderer's configured in-flight frame count of retained old binding lifetimes. Before completing a selected old fence, require its command-pinned lifetime alive even after cache replacement; immediately after that exact fence completes, require it released unless an explicit external snapshot owner remains. Repeat 4096 scroll/blink semantic misses and require no growth in full-snapshot entries.

- [ ] **Step 3: Run the render scissor red gate.**

  ```bash
  cmake --build --preset debug --target test_ui_render_clip test_render_queue test_rendering_sdlgpu -j
  ```

  Expected: compilation or assertions fail because `RenderCommand` has no scissor and the render loop does not transition clip state.

- [ ] **Step 4a: Extend the existing text collection context in place.**

  ```cpp
  struct TextCollectContext {
      TextAffine2D layoutToOutput;
      Color color = Color::White();
      int cameraPass = 0;
      int sortingLayer = 0;
      int sortingOrder = 0;
      float depthOrYSort = 0.0f;
      TextRasterPolicy rasterPolicy;
      std::optional<molga::ui::UIDrawOrderKey> uiDrawOrder;
      std::uint64_t stableSubmissionBase = 0;
      std::optional<molga::PixelRectU32> scissor;
  };

  void TextRenderer::CollectLayout(molga::RenderQueue& queue,
                                   const molga::text::TextLayout& layout,
                                   const TextCollectContext& context,
                                   molga::text::TextDiagnosticSink& sink);
  ```

  This is the same type and sole sink-bearing overload produced by Task 8.2. Do not introduce `UITextCollectContext`, a sink-less overload, or an origin/color parameter overload. World text continues to set the complete `layoutToOutput` affine, color, and world sort fields with empty `uiDrawOrder`/`scissor`.

- [ ] **Step 4b: Copy UI order and clip into every glyph command.**

  Before UI text collection, obtain `context.rasterPolicy` exactly once from
  `UIPhysicalTransform::RasterPolicy(sink)`; failure drops the whole item before
  atlas lookup. Copy `context.uiDrawOrder` and `context.scissor` into every
  emitted command. Replace only the copied key's `stableSubmissionIndex` with
  checked `context.stableSubmissionBase + positionedGlyphOrdinal`. A
  non-drawable zero-bitmap glyph consumes its reserved ordinal but emits no
  command. Require visited positioned-glyph/tofu record count to equal
  `reservedCommandSpan` and every emitted ordinal to be unique/in-range; do not
  compare emitted command count with the span. On overflow/span mismatch emit
  `LayoutInvalid` through the supplied sink and submit no partial label. The
  glyph loop never recomputes order, clip, font size, or raster scale from a
  component/affine.

- [ ] **Step 5a: Define the render collector boundary.**

  ```cpp
  class UIRenderCollector {
  public:
      void Collect(const UISnapshot&, const UIPhysicalTransform&,
                   molga::RenderQueue&, TextRenderer&,
                   molga::text::TextDiagnosticSink&) const;
  };
  ```

- [ ] **Step 5b: Convert sprite/solid snapshot items without ECS reads.**

  `UIRenderCollector::Collect` visits sorted `snapshot.renderItems`, converts `logicalRect` and `logicalClip` with `ToPhysicalOutward`, and emits sprite commands from `UISpriteSnapshot::binding.texture`/`binding.sampler`; copy `binding.lifetimeIdentity` and the retained token into the Task 6.2 `RenderCommand::{resourceLifetimeIdentity,resourceLifetime}` fields. Solid commands use their stored variant. Require the lifetime identity/binding to match and `binding.deviceGeneration == physicalTransform.deviceGeneration` before enqueue. Invalid/empty converted clips or a retired/cross-device/mismatched binding drop the command and emit rate-limited `LayoutInvalid`.

- [ ] **Step 5c: Convert text snapshot items with the shared explicit context.**

  Populate the existing `TextCollectContext` with `physicalTransform.LayoutToOutputAffine(storedLogicalOrigin)`, color, exact `UIDrawOrderKey`, the item's already-reserved `stableSubmissionIndex`, and final physical scissor. Reject a missing affine before submitting any glyph; never replace it with origin-only translation. Before calling `CollectLayout(queue, layout, context, sink)`, recompute/check `TextRenderCommandSpan(layout) == item.reservedCommandSpan`; do not assign or advance a second collector-local base. The following item's frozen order already begins after the complete text span. `UIRenderCollector` never calls `World::FindById` or reads a component.

- [ ] **Step 6a: Add optional UI order and scissor fields to `RenderCommand`.**

  Add `std::optional<ui::UIDrawOrderKey> uiDrawOrder` and `std::optional<PixelRectU32> scissor` after geometry/bounds fields. Neither enters `BatchKey::{operator==,operator<}`.

- [ ] **Step 6b: Sort UI commands by the complete draw key.**

  In `RenderQueue::Sort`, commands with `uiDrawOrder` compare by that complete key; all UI commands must carry it, and the distinct UI `cameraPass` keeps them from world commands. If two complete UI keys compare equal, use the already-assigned `SortKey::submissionIndex` as the final tie-break so `std::sort` cannot reorder them. Add a mixed sibling/component test that would fail if `siblingPath` were flattened into `sortingOrder`.

- [ ] **Step 6c: Track active pass viewport in `Renderer::Impl`.**

  Set it in every target/swapchain pass begin, update it from `SetPassViewport`, and clear it after `EndTarget`.

- [ ] **Step 6d: Add `Renderer::ResetPassScissor`.**

  Track the current active-pass viewport in `Renderer::Impl`; implement:

  ```cpp
  bool Renderer::ResetPassScissor(std::string* errorOut) {
      if (!impl_->passRecording || impl_->activePassViewport.width == 0 ||
          impl_->activePassViewport.height == 0) {
          if (errorOut) *errorOut = "scissor reset requires an active pass";
          return false;
      }
      return SetPassScissor(impl_->activePassViewport, errorOut);
  }
  ```

  Clear the tracked viewport at `EndTarget`; initialize it at every target/swapchain pass begin and update it from `SetPassViewport`.

- [ ] **Step 7a: Compare effective scissor before each draw.**

  Keep `optional<PixelRectU32> currentScissor`. Equal values continue batching; a changed value flushes before any renderer state call.

- [ ] **Step 7b: Apply changed scissor or restore full pass.**

  Call `SetPassScissor` for a clip or `ResetPassScissor` for no clip, then draw. Propagate a failed state call as a renderer error; never continue drawing under the previous clip.

- [ ] **Step 7c: Restore full pass state at queue end.**

  After the final command, flush and call `ResetPassScissor` if a clip remains before `batcher_.End()`.

- [ ] **Step 7d: Clear device-bound snapshots on generation change.**

  Add `UILayoutSystem::OnDeviceGenerationChanged(oldGeneration, newGeneration)` and route it through `UISystem`. Require the exact nonzero generation already acquired/published by `GraphicsDevice` (do not advance it a second time), then clear `lastSnapshot_`, every old-device `UISnapshotWorldDeviceSlotKey`, and runtime-binding scratch entries for the old device while preserving the bounded geometry LRU and canonical semantic scratch. No old native handle can be returned from the fast path.

- [ ] **Step 7e: Retire superseded binding lifetimes through the submitted frame fence.**

  Cache replacement drops only the cache's strong snapshot owner. `UIRenderCollector` copies every sprite/glyph binding lifetime into its command, and `RenderSystem2D` calls the existing `Renderer::RetainUntilFrameComplete(identity, lifetime)` only for commands actually submitted. The owning `FrameContext` releases those tokens only after its exact completion fence. Culled/unsubmitted items transfer no lifetime; external `UISnapshotPtr` values remain explicitly external. This keeps internal full-snapshot storage at one entry without destroying a binding still used by an in-flight command.

- [ ] **Step 7f: Define fail-closed host shutdown status.**

  ```cpp
  enum class EngineShutdownStatus : std::uint8_t {
      Complete, GpuDrainFailed, ExternalGpuLifetime
  };
  EngineShutdownStatus EngineShutdown(
      std::unique_ptr<EngineHost>&,
      molga::text::TextDiagnosticSink&);
  ```

  `Complete` resets the host. Either failure leaves the host/device lifetime
  object non-null and emits blocker `ReferenceInvalid`. The caller records a
  failing eventual exit status, releases only its known external snapshot/page
  owners when applicable, and retries through the same owning host. It may not
  return, throw past the owner scope, destroy the diagnostic sink/text runtime
  guard, or force-reset the host while the result is non-`Complete`. Editor and
  runtime call sites therefore remain in an explicit shutdown-blocked state;
  a persistent failure retains every owner and permits no normal C++ teardown
  or ICU cleanup. Update those call sites and their retained-owner audit in this
  same commit, and remove the old void overload.

  At this API boundary remove Renderer/Bootstrap's interim
  `GPU_IDLE_WAIT_FAILED` `std::abort()` branch from Task 6.2. A failed idle wait
  returns `GpuDrainFailed` before any teardown mutation and is retryable through
  the still-owned host; only this status-return contract remains compiled.

- [ ] **Step 7g: Implement idempotent teardown phases in exact order.**

  ```text
  if phase == Running:
    submit pending frame/fences
    if !WaitIdle(): return GpuDrainFailed          // no teardown/owner release
    Renderer.ReleaseCompletedGpuLifetimes()
    phase = Drained
  if phase == Drained:
    UISystem.ClearFullSnapshotBindingCache(deviceGeneration)
    release Game View/runtime/editor-owned latest UIFrameResult snapshots
    TextureManager.ReleaseBindings(deviceGeneration)
    phase = InternalOwnersReleased
  if registry.LiveRetainedBindingCount(deviceGeneration) != 0 ||
     textRenderer.GlyphAtlas().LiveExternalPagePinCount() != 0:
    return ExternalGpuLifetime                    // destroy nothing
  if !textRenderer.ShutdownAfterGpuIdle(sink):
    return ExternalGpuLifetime                    // active scope/pin; no mutation
  registry.DestroyRetiredBindings(deviceGeneration)
  Renderer.DestroyDeviceResources()
  GraphicsDevice.Destroy()
  phase = Complete
  ```

  The first successful GPU drain is retained across a retry. The registry detects
  nonexpired weak lifetime-token records keyed by `lifetimeIdentity`; it never
  owns the token merely to count it. The atlas likewise counts only nonexpired
  weak external page tokens after its current-collection/internal owners are
  released. Neither check uses `shared_ptr::use_count()`.
  `ShutdownAfterGpuIdle` calls `ReleaseAfterGpuIdle` to clear the atlas before
  destroying its owned text-service aggregate. Old handle/page tokens never
  outlive their device or ICU runtime.

- [ ] **Step 8: Run CPU, GPU, and UI clip green gates.**

  ```bash
  cmake --build --preset debug --target test_ui_render_clip test_render_queue \
    test_rendering_sdlgpu test_gpu_retirement test_ui_snapshot test_platform_sdl \
    molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_render_clip|test_render_queue|test_rendering_sdlgpu|test_gpu_retirement|test_ui_snapshot|test_platform_sdl)$' --output-on-failure
  ```

  Expected: all selected tests pass with the exact audited flush/set/reset and fail-closed binding teardown sequences.

- [ ] **Step 9: Commit clip-aware rendering.**

  ```bash
  git add CMakeLists.txt src/UI/UIRenderCollector.* src/Rendering/TextRenderer.* \
    src/Rendering/RenderQueue.h src/Rendering/RenderSystem2D.cpp \
    src/Rendering/Renderer.* src/Rendering/SpriteBatcher.* \
    src/Rendering/GraphicsDevice.* src/Rendering/TextureBindingRegistry.* \
    src/Rendering/Texture.* \
    src/Core/TextureManager.* src/Core/Bootstrap.* src/UI/UILayoutSystem.* \
    src/UI/UISystem.* src/main.cpp src/runtime_main.cpp \
    tests/test_render_queue.cpp tests/test_rendering_sdlgpu.cpp \
    tests/test_gpu_retirement.cpp tests/test_ui_render_clip.cpp \
    tests/test_ui_snapshot.cpp \
    tests/test_platform_sdl.cpp
  git commit -m "feat: enforce UI clips in render commands"
  ```

### Task 11.3: Add deterministic identity-keyed scroll state

**Files:**

- Create: `src/UI/UIScrollSystem.h`
- Create: `src/UI/UIScrollSystem.cpp`
- Create: `src/UI/UIDeterministicTick.h`
- Create: `tests/test_ui_scroll.cpp`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UILayoutSnapshot.h`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: authored `UIScrollView`, complete runtime identities, fixed layout snapshots, viewport/content `SceneObjectRef`, and fixed-point time/deltas.
- Produces: `UIDeterministicTick`, `UIScrollState`, `UIScrollInput`, `UIScrollMutation`, `UIScrollSystem::{ApplyInput,AdvanceTick,State,OnWorldReleased}` and arrangement/render invalidation for Task 12's one-event/tick orchestrator.

- [ ] **Step 1a: Write the failing signed delta/axis test.**

  ```cpp
  TEST_CASE("scroll preserves signed deltas and axis selection") {
      UIScrollFixture f;
      const auto target = f.ScrollTarget();
      f.System().ApplyInput(f.World(), *f.Snapshot(), target,
          {molga::FixedPoint{molga::Fixed26_6::FromRaw(0),
                             molga::Fixed26_6::FromRaw(0)},
           molga::Fixed26_6::FromRaw(-32), molga::ui::UIScrollAxis::Vertical},
          f.Diagnostics());
      const auto* state = f.System().State(target);
      REQUIRE(state);
      CHECK(state->offset.x.Raw() == 0);       // horizontal authored disabled
      CHECK(state->offset.y.Raw() == -32);     // exact signed axis value
  }
  ```

- [ ] **Step 1b: Write the failing runtime-only serialization test.**

  ```cpp
  TEST_CASE("scroll runtime state is not serialized") {
      UIScrollView view;
      nlohmann::json encoded;
      view.Serialize(encoded);
      CHECK_FALSE(encoded.contains("offset"));
      CHECK_FALSE(encoded.contains("velocity"));
  }
  ```

- [ ] **Step 1c: Write the failing exact elastic recurrence test.**

  ```cpp
  TEST_CASE("elastic fixed step uses the exact signed 26.6 recurrence") {
      UIScrollFixture f;
      f.SetVerticalExtentRaw(-640, 0, 640);
      f.SetVerticalStateRaw(64, 128);
      f.SetRatesRaw(/* elasticity */ 128, /* deceleration */ 64);
      REQUIRE(f.System().AdvanceTick(
          f.World(), *f.Snapshot(),
          UIDeterministicTick{1, molga::Fixed26_6::FromRaw(16)},
          f.Diagnostics()).changed);
      const auto* state = f.System().State(f.ScrollTarget());
      REQUIRE(state);
      CHECK(state->offset.y.Raw() == 48);
      CHECK(state->velocity.y.Raw() == 96);
  }
  ```

- [ ] **Step 1d: Write the failing stable-identity iteration test.**

  ```cpp
  TEST_CASE("fixed step visits complete identities in stable order") {
      UIScrollFixture forward;
      UIScrollFixture reverse;
      forward.AddThreeStatesInIdentityOrder();
      reverse.AddThreeStatesInReverseIdentityOrder();
      forward.StepRaw(16);
      reverse.StepRaw(16);
      CHECK(forward.StableStateJson() == reverse.StableStateJson());
      CHECK(forward.LastVisitedIdentitiesForTesting() ==
            reverse.LastVisitedIdentitiesForTesting());
  }
  ```

- [ ] **Step 1e: Write the failing exact scroll policy matrix.**

  Add one table row each for authored initial normalized position, clamped movement, sensitivity, inertia-off, disabled axis, nested-mask parity, invalid references, checked-overflow fail-closed, identity replacement, and world release. Give every row exact raw input/output and diagnostic counts. The `64 -> 48` fixture is intentionally above legal `[-640,0]`; its declared viewport extent is 640 raw units.

- [ ] **Step 1f: Write a failing deterministic-tick identity/replay test.**

  ```cpp
  TEST_CASE("scroll consumes exact deterministic UI ticks") {
      UIScrollFixture runtime;
      UIScrollFixture gameView;
      UIScrollFixture replay;
      const std::vector<UIDeterministicTick> ticks{
          {81, molga::Fixed26_6::FromRaw(16)},
          {82, molga::Fixed26_6::FromRaw(16)},
          {83, molga::Fixed26_6::FromRaw(8)}};
      runtime.AdvanceTicks(ticks);
      gameView.AdvanceTicks(ticks);
      replay.AdvanceTicks(DecodeCanonicalTicks(EncodeCanonicalTicks(ticks)));
      CHECK(runtime.StableStateJson() == gameView.StableStateJson());
      CHECK(runtime.StableStateJson() == replay.StableStateJson());
      CHECK_FALSE(runtime.AdvanceTick(ticks.back()));
  }
  ```

  Add zero/non-increasing index, zero delta, and delta raw greater than 64 rows. The frame orchestrator rejects the complete bad tick batch before stepping; neither scroll nor later caret blink samples wall-clock time.

- [ ] **Step 2: Run the scroll red gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_scroll -j
  ```

  Expected: compilation fails because `UIScrollSystem` and its fixed input contract do not exist.

- [ ] **Step 3: Define explicit X/Y and gamepad-axis input.**

  ```cpp
  namespace molga::ui {
  struct UIDeterministicTick {
      std::uint64_t tickIndex = 0;
      molga::Fixed26_6 deltaSeconds = molga::Fixed26_6::FromRaw(0);
      bool operator==(const UIDeterministicTick&) const noexcept;
      bool operator!=(const UIDeterministicTick& other) const noexcept {
          return !(*this == other);
      }
  };
  enum class UIScrollAxis : std::uint8_t { Horizontal, Vertical };
  struct UIScrollInput {
      molga::FixedPoint logicalDelta;
      molga::Fixed26_6 axisValue = molga::Fixed26_6::FromRaw(0);
      UIScrollAxis axis = UIScrollAxis::Vertical;
  };
  struct UIScrollState {
      molga::FixedPoint offset;
      molga::FixedPoint velocity;
  };
  struct UIScrollMutation {
      bool changed = false;
      bool arrangementDirty = false;
      bool renderDirty = false;
      bool textShapeDirty = false;
  };
  class UIScrollSystem {
  public:
      UIScrollMutation ApplyInput(World&, const UISnapshot&,
          const UIRuntimeTargetIdentity&, const UIScrollInput&,
          molga::text::TextDiagnosticSink&);
      UIScrollMutation AdvanceTick(World&, const UISnapshot&,
          const UIDeterministicTick&,
          molga::text::TextDiagnosticSink&);
      const UIScrollState* State(const UIRuntimeTargetIdentity&) const;
      void OnWorldReleased(std::uint64_t worldGeneration);
  };
  } // namespace molga::ui
  ```

  `offset` is logical displacement and `velocity` is logical displacement per second, both signed 26.6. For each axis the legal range is `[min(0, viewportLength-contentLength), 0]`. An input event's checked sensitivity-scaled displacement is applied to `offset` immediately and, when inertia is enabled, added as an equally valued per-second velocity impulse; this definition avoids a hidden wall-clock sample interval.

  `UIDeterministicTick` is defined in `src/UI/UIDeterministicTick.h` and serialized in canonical traces as exact nonzero integer `tickIndex` plus `deltaSecondsRaw`; no timestamp or floating duration is accepted. `UISystem` owns validation/iteration of the frame tick stream in Task 12.3 and passes each same value to scroll and later caret blink.

- [ ] **Step 4a: Resolve viewport/content refs to complete identities.**

  Resolve viewport/content `SceneObjectRef` through the current world and capture full identities. A missing/replaced ref disables that feature and emits one rate-limited `ReferenceInvalid`; it must not redirect to the same numeric ID in another generation.

- [ ] **Step 4b: Validate authored rates and initialize one runtime scroll state.**

  Convert authored values once with `Fixed26_6::FromFloat`: normalized positions must be in `[0,1]`, sensitivity/deceleration are non-negative per-second rates, and elasticity is a positive per-second return rate for `Elastic`. Normalize signed zero; conversion failure disables only that scroll identity with `LayoutInvalid`. Compute each legal extent from snapshot N and initialize `offset = max + RoundNearestAway((min-max) * normalized)` only when the complete scroll identity has no state.

- [ ] **Step 5a: Apply signed wheel/pointer and named-axis deltas with checked Q6 math.**

  Add wheel/pointer `logicalDelta` per enabled axis and `axisValue` only to its named axis, then multiply by sensitivity with the checked round-nearest/ties-away helper below. Both sources are additive if both are intentionally nonzero. Add the scaled value to `offset`; if inertia is enabled, also add it to velocity, otherwise set velocity to zero. A disabled axis consumes no displacement and remains unchanged.

- [ ] **Step 5b: Apply clamped movement.**

  Clamp each enabled-axis offset to the current legal content extent immediately and zero outward velocity at the boundary.

- [ ] **Step 5c: Implement checked Q6 multiply and delta validation.**

  `AdvanceTick` accepts only nonzero `tickIndex` and `1 <= tick.deltaSeconds.Raw() <= 64`; use that exact delta throughout the step. Implement a checked `MulQ6NearestAway(a,b)` using an `int64_t` product, absolute remainder, and final `int32_t` bounds; promote to `int64_t` before absolute-value operations. Every add/subtract is checked before publication.

- [ ] **Step 5d: Iterate runtime states in complete-identity order.**

  Copy active keys, sort lexicographically by `(worldGeneration, objectId, componentRuntimeTypeId, componentInstanceId)`, then look up and step each state. Never iterate the unordered table directly.

- [ ] **Step 5e: Apply the exact per-axis elastic/deceleration recurrence.**

  For each enabled axis, execute this order exactly:

  ```text
  deltaSeconds = tick.deltaSeconds
  integrated   = offset + MulQ6NearestAway(velocity, deltaSeconds)
  decayStep    = clamp(MulQ6NearestAway(decelerationRate, deltaSeconds), 0, 64)
  retained     = 64 - decayStep
  nextVelocity = MulQ6NearestAway(velocity, retained)

  if movement == Clamped:
      nextOffset = clamp(integrated, legalMin, legalMax)
      if nextOffset == legalMin and nextVelocity < 0: nextVelocity = 0
      if nextOffset == legalMax and nextVelocity > 0: nextVelocity = 0
  else:
      overscrollLimit = max(64, viewportExtent / 2)  // positive integer floor
      bounded   = clamp(integrated,
                        legalMin - overscrollLimit,
                        legalMax + overscrollLimit)
      legal     = clamp(bounded, legalMin, legalMax)
      overscroll = bounded - legal
      returnStep = clamp(MulQ6NearestAway(elasticityRate,
                                          deltaSeconds), 0, 64)
      correction = MulQ6NearestAway(overscroll, returnStep)
      nextOffset = bounded - correction
      if abs(nextOffset - clamp(nextOffset, legalMin, legalMax)) <= 1 and
         abs(nextVelocity) <= 1:
          nextOffset = clamp(nextOffset, legalMin, legalMax)
          nextVelocity = 0
  ```

- [ ] **Step 5f: Fail closed on arithmetic errors and lock the hand fixture.**

  The hand fixture computes `integrated=96`, `retained=48`, `nextVelocity=96`, `overscroll=96`, `returnStep=32`, `correction=48`, and `nextOffset=48`. Any invalid delta or checked-arithmetic failure leaves the prior state unchanged, records the identity in a runtime fail-closed set until its authored revision/world changes, and emits one rate-limited `LayoutInvalid`; it never publishes a partial/wrapped state.

- [ ] **Step 6a: Translate the content subtree during arrangement.**

  Translate the content subtree by runtime offset before final node/render/hit rects are published.

- [ ] **Step 6b: Add semantic scroll displacement to snapshot cache identity.**

  Before publishing any changed offset, require `UIRuntimeInvalidationClock::Advance(ScrollDisplacement)`; exhaustion leaves the prior offset visible, disables snapshot caching, and emits one blocker rather than aliasing the warm fast-path stamp. Extend the geometry/full-snapshot cache inputs with an ordered collision-checked vector of `{scrollTargetIdentity, offsetXRaw, offsetYRaw, viewportIdentity, contentIdentity}`. Velocity is excluded because it is not visible until it changes offset and does not advance this clock by itself. This vector is derived after the stable identity sort; it contains original fields, not a hash alone. An offset change therefore republishes arranged node/render/hit rects while reusing immutable text shape/layout when constraints are unchanged.

- [ ] **Step 6c: Preserve clip parity and narrow dirty propagation.**

  Keep viewport clipping identical for render and hit records. A changed offset returns `arrangementDirty=true`, `renderDirty=true`, and `textShapeDirty=false`; reuse text layout unless its width/height constraint changes.

- [ ] **Step 7: Run scroll, layout, clip, and serialization gates.**

  ```bash
  cmake --build --preset debug --target test_ui_scroll test_ui_layout \
    test_ui_render_clip test_scene_serializer -j
  ctest --test-dir build/debug -R '^(test_ui_scroll|test_ui_layout|test_ui_render_clip|test_scene_serializer)$' --output-on-failure
  ```

  Expected: all selected tests pass; identical fixed input sequences produce identical raw offsets and snapshots.

- [ ] **Step 8: Commit runtime scrolling.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UIScrollSystem.* \
    src/UI/UIDeterministicTick.h src/UI/UILayoutSystem.cpp src/UI/UILayoutSnapshot.h \
    tests/test_ui_scroll.cpp
  git commit -m "feat: add deterministic UI scrolling"
  ```

## Final Verification

- [ ] **Run the complete focused Debug boundary.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_identity test_ui_components \
    test_ui_fixed test_ui_layout test_ui_snapshot test_ui_render_clip \
    test_ui_scroll test_render_queue test_rendering_sdlgpu test_platform_sdl \
    test_ui molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_identity|test_ui_components|test_ui_fixed|test_ui_layout|test_ui_snapshot|test_ui_render_clip|test_ui_scroll|test_render_queue|test_rendering_sdlgpu|test_platform_sdl|test_ui)$' --output-on-failure
  ```

- [ ] **Run the complete Debug regression suite.**

  ```bash
  ctest --preset debug
  ```

- [ ] **Run source-contract checks.**

  ```bash
  rg -n 'frameIndex|timestamp' src/UI/UILayoutSnapshot.h
  rg -n 'targetId|void Clear\(' src/ECS/SceneObjectRef.h
  rg -n 'scissor' src/Rendering/RenderQueue.h src/Rendering/RenderSystem2D.cpp
  rg -n 'struct (TextCollectContext|UITextCollectContext)|layoutToOutput' \
    src/Rendering/TextRenderer.h src/UI
  rg -n 'TextureRuntimeBindingIdentity|runtimeBindings|deviceGeneration|uploadGeneration|lifetimeIdentity' \
    src/UI src/Rendering src/Core/TextureManager.*
  rg -n 'void EngineShutdown\(' src tests
  rg -n 'void Shutdown\(\)|TextRenderer::Shutdown\(' \
    src/Rendering/TextRenderer.h src/Rendering/TextRenderer.cpp
  ```

  Expected: the first, final two shutdown commands have no matches; `SceneObjectRef` shows the compatibility member/method; scissor appears in `RenderCommand` and render transition code but not `BatchKey`; exactly one `TextCollectContext` exists and retains `layoutToOutput`; runtime binding fields appear in the full cache/payload path but not stable JSON emission.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-20-ui-text/03-ui-layout-rendering.md`. Continue with `04-ui-interaction-ime.md` only after this plan's Exit Contract has fresh evidence.
