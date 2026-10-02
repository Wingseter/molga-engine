# Editor UI/Text Authoring and Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every approved UI/text authoring field editable through typed editor descriptors, add stable scene-object picking and complete diagnostic presentation, and migrate legacy Canvas/font payloads only through one explicit undoable command.

**Architecture:** Keep authored data on the existing component snapshot-command path and represent scene references with `SceneObjectRef`, never pointers or generic integers. Build editor presentation from the versioned component/import records produced by Milestones 4 and 9, while treating computed layout values as read-only views over the latest immutable UI snapshot. Preserve legacy bytes until the user invokes `MigrateUITextSchemaCommand`; that command owns one before/after scene transaction and any deterministic one-face family assets it creates.

**Tech Stack:** C++17, doctest, nlohmann/json, existing ImGui Inspector/Project Browser, `EditorPropertyDescriptor`, `CommandHistory`, `SceneSerializer`, `AssetDatabase`.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md)

## Global Constraints

- The approved design and the master [`UI/Text Production Backbone Implementation Plan`](../2026-08-20-ui-text-production-backbone.md) are authoritative. Stop for a plan/design amendment if public behavior, failure policy, or schema scope must change.
- Start only after subplans `01` through `04` are green and their commits are present in dependency order. In particular, consume the exact versioned components, `SceneObjectRef`, immutable UI snapshot, and typed diagnostics they produce.
- Preserve unrelated worktree changes. Before each task run `git status --short --branch`; stage only files listed in that task.
- `SceneObjectRef` keeps the source/serialized member name `targetId`; `ObjectId()` is a semantic accessor. Never add duplicate `objectId` storage or write a raw `GameObject*`.
- Every displayed import diagnostic retains all fields: `code`, `severity`, `subsystem`, `message`, `remediation`, `assetGuid`, `sceneObjectId`, `componentType`, and `sourceByteRange`.
- Computed rect, intrinsic size, baseline, clip, driver, focus, hover, composition, caches, and atlas state are read-only runtime views. They never enter component snapshots, prefab overrides, scene dirty state, or Undo writes.
- Opening, previewing, saving, or building a legacy scene must not rewrite legacy Canvas, `UILabel.fontGuid`, or `TextRenderer2D.fontGuid` representation. Only the explicit migration command writes the current schemas.
- `fontFamilyGuid` accepts only records imported by `FontFamilyImporter`; font-license references keep their declared license asset type.
- Editor preview and migration do not bypass package coverage, redistribution, license, or notice validation. An implicit legacy one-face family is still subject to the complete package policy in subplan `06`.
- Each task is red-green-refactor and ends in one independently reviewable commit.

---

## Prerequisite Contract

The executor verifies these exact interfaces before Task 15.1. If one is absent, stop and finish the producing subplan instead of creating a competing editor-only type.

```cpp
// src/ECS/SceneObjectRef.h, produced by Milestone 9
struct SceneObjectRef {
    unsigned int targetId = 0;
    bool IsSet() const noexcept;
    void Clear() noexcept;
    unsigned int ObjectId() const noexcept;
    void Remap(const std::unordered_map<unsigned int, unsigned int>&);
};
using ObjectRef = SceneObjectRef;

// src/Text/TextDiagnostic.h, produced by Milestone 2
struct TextDiagnostic {
    TextDiagnosticCode code;
    TextSeverity severity;
    std::string subsystem;
    std::string message;
    std::string remediation;
    std::string assetGuid;
    unsigned int sceneObjectId;
    std::string componentType;
    SourceByteRange sourceByteRange;
};

// src/Core/AssetDatabase.h, extended by Milestone 4
struct AssetRecord {
    // existing fields remain
    std::vector<molga::text::TextDiagnostic> importDiagnostics;
};
```

The eight authored component types are `UILayoutElement`, `UILayoutGroup`, `UIContentSizeFitter`, `UIMask`, `UIScrollView`, `UISelectable`, `UITextInput`, and `UIAccessibility`. `UICanvas`, `UILabel`, and `TextRenderer2D` expose their loaded-schema marker without serializing it. The final `UISnapshot` supplies immutable node rect/intrinsic/clip data, and text render payloads supply baseline data.

## File Responsibility Map

| File | Responsibility |
|---|---|
| `src/Editor/Properties/EditorPropertyDescriptor.*` | Typed authored fields, asset/reference filters, multi-edit intersection, and read-only metadata |
| `src/Editor/Windows/InspectorWindow.*` | Scene-object picker, immutable snapshot readouts, and lossless diagnostic rows |
| `src/Editor/Windows/ProjectBrowserWindow.*` | Undoable schema-1 `.fontfamily` creation |
| `src/Editor/UIRegistry.cpp` | Icons/type registration for all new UI components and `.fontfamily` |
| `src/Editor/Commands/ObjectCommands.cpp` | Undoable Text Input and layout-container presets |
| `src/Editor/AssetReferenceScan.cpp` | References through families, text input, scrolling, and navigation |
| `src/Editor/Commands/MigrateUITextSchemaCommand.*` | Explicit scene snapshot migration plus deterministic one-face family creation |
| `tests/test_ui_editor.cpp` | Cross-editor integration and legacy migration behavior |
| Existing focused tests | Descriptor, history, and reference-scan regressions |

## Exit Contract

This subplan is complete only when the focused tests, `editor_smoke`, and full Debug suite are green; legacy load/save remains byte-preserving until the command runs; every migration operation is byte-exact Undo/Redo; and no runtime-only UI state appears in a scene/prefab snapshot. The next subplan may then consume the authored family/reference closure. This exit does not establish package or Milestone A qualification.

---

### Task 15.1: Add typed scene references and read-only editor descriptor metadata

**Inherited obligations (recorded by earlier tasks; this task owns closing them):**

- **`overflow_error` at the scene-load boundary.** `World::LoadFromFile` has no diagnostic sink, so
  a checked-geometry exhaustion propagates out into engine code that previously could not throw.
  Task 10.2 verified this is a *reporting* gap, not corruption — the throw leaves destination and
  container unchanged and never publishes zero — and left it because every candidate boundary file
  was outside its Files list. This task introduces typed scene references and has editor
  diagnostics available, so it is the first task that can report it.
  Source: `03-ui-layout-rendering.md:563-573`.


**Files:**

- Modify: `src/Editor/Properties/EditorPropertyDescriptor.h`
- Modify: `src/Editor/Properties/EditorPropertyDescriptor.cpp`
- Modify: `tests/test_editor_property_descriptor.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `SceneObjectRef`, all current component serializers, `AssetDatabase::Find`, and existing descriptor/multi-edit helpers.
- Produces: `EditorPropertyType::SceneObjectRef`, `EditorPropertyValue` with `::SceneObjectRef`, `readOnly`, `readOnlyReason`, `drivenBy`, and strict asset/importer validation.

- [ ] **Step 1: Record the clean task boundary**

  Run:

  ```bash
  git status --short --branch
  ```

  Expected: note pre-existing paths; none of them may be staged by this task.

- [ ] **Step 2: Add the failing stable-reference round-trip test**

  Add these cases to `tests/test_editor_property_descriptor.cpp`:

  ```cpp
  #include "ECS/SceneObjectRef.h"
  #include "ECS/Components/UIScrollView.h"
  #include "ECS/Components/UISelectable.h"

  TEST_CASE("scene object descriptors preserve stable targetId values") {
      UIScrollView scroll;
      scroll.Deserialize({
          {"schemaVersion", UIScrollView::CurrentSchemaVersion},
          {"viewport", {{"targetId", 17}}},
          {"content", {{"targetId", 23}}}
      });
      const auto descriptors = molga::DescribeEditorProperties(scroll);
      const auto& viewport = Find(descriptors, "viewport");
      CHECK(viewport.type == molga::EditorPropertyType::SceneObjectRef);
      CHECK(std::get<SceneObjectRef>(viewport.getter(scroll)).targetId == 17u);
      CHECK(molga::ApplyEditorPropertyValue(
                viewport, std::vector<Component*>{&scroll},
                SceneObjectRef{41u}) == 1u);
      nlohmann::json saved;
      scroll.Serialize(saved);
      CHECK(saved["viewport"]["targetId"] == 41u);
      CHECK_FALSE(saved["viewport"].contains("objectId"));
  }
  ```

- [ ] **Step 3: Add the failing read-only write test**

  ```cpp

  TEST_CASE("read only descriptors reject every write path") {
      molga::EditorPropertyDescriptor descriptor;
      descriptor.key = "computedRect";
      descriptor.type = molga::EditorPropertyType::String;
      descriptor.readOnly = true;
      descriptor.readOnlyReason = "Runtime snapshot value";
      descriptor.drivenBy = "parent UILayoutGroup";
      descriptor.getter = [](Component&) {
          return molga::EditorPropertyValue{std::string("[64,128,320,96]")};
      };
      descriptor.setter = [](Component&, const molga::EditorPropertyValue&) {
          FAIL("read-only setter must not run");
          return true;
      };
      DescriptorFixtureComponent component;
      CHECK_FALSE(molga::IsEditorPropertyValueValid(
          descriptor, std::string("mutated")));
      CHECK(molga::ApplyEditorPropertyValue(
                descriptor, std::vector<Component*>{&component},
                std::string("mutated")) == 0u);
  }
  ```

- [ ] **Step 4: Add the failing active-scene resolver test**

  ```cpp

  TEST_CASE("scene reference validation is scoped to the active scene") {
      auto active = std::make_shared<GameObject>("Active");
      auto otherScene = std::make_shared<GameObject>("Other Scene");
      const molga::EditorSceneReferenceContext context{
          41u,
          [active](unsigned int id) -> GameObject* {
              return id == active->GetID() ? active.get() : nullptr;
          }};
      molga::EditorPropertyDescriptor descriptor;
      descriptor.type = molga::EditorPropertyType::SceneObjectRef;
      CHECK(molga::IsEditorPropertyValueValid(
          descriptor, SceneObjectRef{}, context));
      CHECK(molga::IsEditorPropertyValueValid(
          descriptor, SceneObjectRef{active->GetID()}, context));
      CHECK_FALSE(molga::IsEditorPropertyValueValid(
          descriptor, SceneObjectRef{999999u}, context));
      CHECK_FALSE(molga::IsEditorPropertyValueValid(
          descriptor, SceneObjectRef{otherScene->GetID()}, context));
  }
  ```

- [ ] **Step 5: Add one exact field-coverage table for the new schemas**

  Put the following data in the same test and compare it with `DescribeEditorProperties` output. Base `enabled` is checked separately; every entry below is authored and writable unless marked read-only by the Inspector snapshot layer.

  ```cpp
  const std::map<std::string, std::vector<std::string>> expected = {
      {"UILayoutElement", {"minWidth", "preferredWidth", "flexibleWidth",
          "minHeight", "preferredHeight", "flexibleHeight", "ignoreLayout"}},
      {"UILayoutGroup", {"mode", "padding.left", "padding.right",
          "padding.top", "padding.bottom", "spacing.x", "spacing.y",
          "childAlignment", "controlChildWidth", "controlChildHeight",
          "forceExpandWidth", "forceExpandHeight", "cellSize.x", "cellSize.y",
          "startCorner", "fillAxis", "constraint", "constraintCount"}},
      {"UIContentSizeFitter", {"horizontalFit", "verticalFit"}},
      {"UIMask", {"clipDescendants"}},
      {"UIScrollView", {"viewport", "content", "horizontal", "vertical",
          "movement", "elasticity", "inertia", "decelerationRate",
          "scrollSensitivity",
          "initialNormalizedPosition.x", "initialNormalizedPosition.y"}},
      {"UISelectable", {"interactable", "navigationMode", "selectOnUp",
          "selectOnDown", "selectOnLeft", "selectOnRight"}},
      {"UITextInput", {"initialText", "readOnly", "multiline",
          "maxGraphemes", "contentPolicy", "submitPolicy", "textViewport",
          "renderedLabel", "placeholderLabel", "fontFamilyGuid",
          "paragraphStyle.weight", "paragraphStyle.stretchPercent",
          "paragraphStyle.slant", "paragraphStyle.fontSizeRaw",
          "paragraphStyle.language", "paragraphStyle.features",
          "paragraphStyle.variation", "paragraphStyle.direction",
          "paragraphStyle.wrap", "paragraphStyle.overflow",
          "paragraphStyle.maxLines", "paragraphStyle.lineSpacingRaw",
          "paragraphStyle.horizontalAlignment",
          "paragraphStyle.verticalAlignment",
          "paragraphStyle.ellipsisUtf8"}},
      {"UIAccessibility", {"role", "name", "description", "hidden"}},
  };
  CHECK(CollectAuthoredDescriptorKeysForBuiltinUIComponents() == expected);
  ```

  Define `CollectAuthoredDescriptorKeysForBuiltinUIComponents()` locally in the test by constructing each named component through `ComponentFactory`, removing `enabled`, and collecting the returned descriptor keys. Do not duplicate production field metadata in a new runtime registry.

- [ ] **Step 6: Run the red descriptor gate**

  Run:

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_editor_property_descriptor -j
  ctest --test-dir build/debug -R '^test_editor_property_descriptor$' --output-on-failure
  ```

  Expected: compilation fails because `SceneObjectRef` is absent from `EditorPropertyValue` and descriptor read-only fields do not exist; after only the enum/variant declaration is added, the behavior assertions still fail.

- [ ] **Step 7: Extend the exact public descriptor types**

  Apply this contract in `EditorPropertyDescriptor.h`:

  ```cpp
  #include "ECS/SceneObjectRef.h"

  enum class EditorPropertyType {
      Bool, Integer, Float, String, Enum, LayerMask, AssetGuid, SceneObjectRef
  };

  using EditorPropertyValue = std::variant<
      bool, std::int64_t, double, std::string, ::SceneObjectRef>;

  struct EditorSceneReferenceContext {
      std::uint64_t sceneGeneration = 0;
      std::function<GameObject*(unsigned int)> resolveObject;
  };

  struct EditorSceneObjectChoice {
      ::SceneObjectRef value;
      std::uint64_t sourceSceneGeneration = 0;
  };

  struct EditorPropertyDescriptor {
      std::string key;
      std::string label;
      std::string group;
      EditorPropertyType type = EditorPropertyType::Float;
      int channel = -1;
      float epsilon = 1.0e-5f;
      std::vector<std::string> enumLabels;
      std::vector<EditorPropertyValue> enumValues;
      std::string assetType;
      std::string assetUsage;
      bool readOnly = false;
      std::string readOnlyReason;
      std::string drivenBy;
      std::function<EditorPropertyValue(Component&)> getter;
      std::function<bool(Component&, const EditorPropertyValue&)> setter;
      std::function<void(Component&)> afterChange;
  };

  bool IsEditorPropertyValueValid(
      const EditorPropertyDescriptor&, const EditorPropertyValue&,
      const EditorSceneReferenceContext&);
  ```

- [ ] **Step 8: Map authored reference and asset fields explicitly**

  Add a table-driven mapper in `EditorPropertyDescriptor.cpp`; it must run before generic JSON-number inference:

  ```cpp
  struct KnownEditorField {
      const char* component;
      const char* key;
      EditorPropertyType type;
      const char* assetType;
  };

  constexpr KnownEditorField kKnownFields[] = {
      {"UILabel", "fontFamilyGuid", EditorPropertyType::AssetGuid,
       "FontFamilyImporter"},
      {"TextRenderer2D", "fontFamilyGuid", EditorPropertyType::AssetGuid,
       "FontFamilyImporter"},
      {"UITextInput", "fontFamilyGuid", EditorPropertyType::AssetGuid,
       "FontFamilyImporter"},
      {"UIScrollView", "viewport", EditorPropertyType::SceneObjectRef, ""},
      {"UIScrollView", "content", EditorPropertyType::SceneObjectRef, ""},
      {"UISelectable", "selectOnUp", EditorPropertyType::SceneObjectRef, ""},
      {"UISelectable", "selectOnDown", EditorPropertyType::SceneObjectRef, ""},
      {"UISelectable", "selectOnLeft", EditorPropertyType::SceneObjectRef, ""},
      {"UISelectable", "selectOnRight", EditorPropertyType::SceneObjectRef, ""},
      {"UITextInput", "textViewport", EditorPropertyType::SceneObjectRef, ""},
      {"UITextInput", "renderedLabel", EditorPropertyType::SceneObjectRef, ""},
      {"UITextInput", "placeholderLabel", EditorPropertyType::SceneObjectRef, ""},
  };
  ```

  The reference getter/setter reads and writes only `{ "targetId": unsigned }`. The existing two-argument `IsEditorPropertyValueValid` handles scalar/enum/asset types but returns `false` for a set `SceneObjectRef` because it has no scene authority. The three-argument overload accepts an unset ref and otherwise requires `context.sceneGeneration != 0`, a non-null resolver, and `context.resolveObject(targetId) != nullptr`. An asset GUID is valid only when its record exists, is healthy, and `record->importer == descriptor.assetType`.

- [ ] **Step 9: Test the reference resolver overload in isolation**

  Run:

  ```bash
  cmake --build --preset debug --target test_editor_property_descriptor -j
  build/debug/tests/test_editor_property_descriptor --test-case="*reference validation*"
  ```

  Expected: unset and live active-scene refs pass; a removed/unresolvable ID fails. The test does not use global editor state.

- [ ] **Step 10: Keep multi-edit equality type-safe**

  Implement equality and commit policy directly:

  ```cpp
  if (descriptor.type == EditorPropertyType::SceneObjectRef) {
      const auto* left = std::get_if<::SceneObjectRef>(&lhs);
      const auto* right = std::get_if<::SceneObjectRef>(&rhs);
      return left && right && left->targetId == right->targetId;
  }
  ```

- [ ] **Step 11: Keep validation and immediate-commit behavior type-safe**

  Implement the two validation entry points and commit policy:

  ```cpp
  if (descriptor.readOnly) return false; // both validation overloads
  if (descriptor.type == EditorPropertyType::SceneObjectRef) {
      const auto* ref = std::get_if<::SceneObjectRef>(&value);
      if (!ref) return false;
      if (!ref->IsSet()) return true;
      return context.sceneGeneration != 0 && context.resolveObject &&
             context.resolveObject(ref->targetId) != nullptr;
  }

  const bool immediate =
      type == EditorPropertyType::AssetGuid ||
      type == EditorPropertyType::SceneObjectRef ||
      type == EditorPropertyType::Enum ||
      type == EditorPropertyType::Bool;
  ```

- [ ] **Step 12: Run the green descriptor gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_editor_property_descriptor -j
  ctest --test-dir build/debug -R '^test_editor_property_descriptor$' --output-on-failure
  ```

  Expected: the descriptor test passes, all schema fields are present, wrong importer types are rejected, and read-only writes report zero changed components.

- [ ] **Step 13: Commit the descriptor contract**

  ```bash
  git add src/Editor/Properties/EditorPropertyDescriptor.h \
    src/Editor/Properties/EditorPropertyDescriptor.cpp \
    tests/test_editor_property_descriptor.cpp tests/CMakeLists.txt
  git commit -m "feat: type UI editor property descriptors"
  ```

### Task 15.2: Add stable object picking, computed snapshot rows, and full diagnostics

**Files:**

- Create: `tests/test_ui_editor.cpp`
- Modify: `src/Editor/Editor.h`
- Modify: `src/Editor/Editor.cpp`
- Modify: `src/Editor/Windows/InspectorWindow.h`
- Modify: `src/Editor/Windows/InspectorWindow.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 15.1 descriptors, `EditorComponentIdentity`, the Task 10.2
  `Editor::SetActiveWorld(World&)` authority and `World::Objects()`, immutable
  `UISnapshot`, and `AssetRecord::importDiagnostics`.
- Produces: monotonic `Editor::ActiveSceneGeneration`, generation-scoped `ApplySceneObjectPickerValue`, `BuildComputedUILayoutRows`, `BuildImportDiagnosticRows`, and Inspector rendering that never writes runtime state.

- [ ] **Step 1: Register the focused editor test target**

  Add exactly:

  ```cmake
  molga_add_test(test_ui_editor test_ui_editor.cpp)
  ```

- [ ] **Step 2: Write the failing stable picker test**

  Create `tests/test_ui_editor.cpp` with these initial cases:

  ```cpp
  #include "Editor/Windows/InspectorWindow.h"
  #include "Editor/Properties/EditorPropertyDescriptor.h"
  #include "ECS/Components/UIScrollView.h"
  #include "Text/TextDiagnostic.h"
  #include "doctest.h"

  TEST_CASE("scene picker writes an ID through the snapshot property path") {
      auto owner = std::make_shared<GameObject>("Scroll");
      auto target = std::make_shared<GameObject>("Viewport");
      auto* scroll = owner->AddComponent<UIScrollView>();
      const auto descriptors = molga::DescribeEditorProperties(*scroll);
      const auto& viewport = FindDescriptor(descriptors, "viewport");
      const auto identity = molga::CaptureEditorComponentIdentity(*scroll);
      auto resolve = [owner](const molga::EditorComponentIdentity& key)
          -> Component* {
          return key.objectId == owner->GetID() &&
                 key.instanceId == owner->GetComponent<UIScrollView>()->GetInstanceID()
              ? owner->GetComponent<UIScrollView>() : nullptr;
      };
      const molga::EditorSceneReferenceContext scene{
          41u,
          [owner, target](unsigned int id) -> GameObject* {
              if (id == owner->GetID()) return owner.get();
              if (id == target->GetID()) return target.get();
              return nullptr;
          }};
      const molga::EditorSceneObjectChoice choice{
          SceneObjectRef{target->GetID()}, 41u};
      CHECK(molga::ApplySceneObjectPickerValue(
          viewport, {identity}, resolve, scene, choice) == 1u);
      nlohmann::json saved;
      scroll->Serialize(saved);
      CHECK(saved["viewport"] ==
            nlohmann::json{{"targetId", target->GetID()}});
  }
  ```

- [ ] **Step 3: Write the failing stale-picker rejection test**

  ```cpp

  TEST_CASE("scene picker rejects removed and wrong-scene choices") {
      auto owner = std::make_shared<GameObject>("Scroll");
      auto target = std::make_shared<GameObject>("Viewport");
      auto* scroll = owner->AddComponent<UIScrollView>();
      const auto& viewport = FindDescriptor(
          molga::DescribeEditorProperties(*scroll), "viewport");
      const auto component = molga::CaptureEditorComponentIdentity(*scroll);
      const auto resolveComponent = [owner](const auto&) -> Component* {
          return owner->GetComponent<UIScrollView>();
      };
      bool targetAlive = true;
      const molga::EditorSceneReferenceContext active{
          51u, [target, &targetAlive](unsigned int id) -> GameObject* {
              return targetAlive && id == target->GetID() ? target.get() : nullptr;
          }};
      CHECK(molga::ApplySceneObjectPickerValue(viewport, {component},
          resolveComponent, active,
          {SceneObjectRef{target->GetID()}, 99u}) == 0u); // wrong scene
      targetAlive = false;
      CHECK(molga::ApplySceneObjectPickerValue(viewport, {component},
          resolveComponent, active,
          {SceneObjectRef{target->GetID()}, 51u}) == 0u); // removed
      CHECK(molga::ApplySceneObjectPickerValue(viewport, {component},
          resolveComponent, active, {SceneObjectRef{}, 51u}) == 1u); // clear
  }
  ```

- [ ] **Step 4: Write the failing full diagnostic projection test**

  ```cpp

  TEST_CASE("import diagnostic presentation loses no typed context") {
      molga::AssetRecord record;
      record.importDiagnostics.push_back({
          molga::text::TextDiagnosticCode::FontFamilyInvalid,
          molga::text::TextSeverity::Blocker,
          "FontFamilyImporter", "fallback cycle", "remove family B -> A",
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 77u, "UILabel", {12u, 19u}});
      const auto rows = molga::BuildImportDiagnosticRows(record);
      REQUIRE(rows.size() == 1u);
      CHECK(rows[0].stableCode == "TEXT_FONT_FAMILY_INVALID");
      CHECK(rows[0].severity == molga::text::TextSeverity::Blocker);
      CHECK(rows[0].subsystem == "FontFamilyImporter");
      CHECK(rows[0].message == "fallback cycle");
      CHECK(rows[0].remediation == "remove family B -> A");
      CHECK(rows[0].assetGuid == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      CHECK(rows[0].sceneObjectId == 77u);
      CHECK(rows[0].componentType == "UILabel");
      CHECK(rows[0].sourceByteRange.begin == 12u);
      CHECK(rows[0].sourceByteRange.end == 19u);
  }
  ```

- [ ] **Step 5: Write the failing computed-state isolation test**

  ```cpp
  TEST_CASE("computed layout rows are read only and never serialize") {
      molga::ui::UILayoutNodeSnapshot node;
      node.logicalRect = {Raw(64), Raw(128), Raw(320), Raw(96)};
      node.intrinsicSize = {Raw(280), Raw(80)};
      node.logicalClip = molga::FixedRect{Raw(64), Raw(128), Raw(256), Raw(96)};
      const auto rows = molga::BuildComputedUILayoutRows(
          node, "parent UILayoutGroup", Raw(72));
      CHECK(FindRow(rows, "computedRect").readOnly);
      CHECK(FindRow(rows, "intrinsicSize").readOnly);
      CHECK(FindRow(rows, "baseline").readOnly);
      CHECK(FindRow(rows, "clip").readOnly);
      CHECK(FindRow(rows, "computedRect").drivenBy == "parent UILayoutGroup");

      UITextInput input;
      nlohmann::json before;
      input.Serialize(before);
      for (const auto& row : rows) CHECK(row.setter == nullptr);
      nlohmann::json after;
      input.Serialize(after);
      CHECK(after == before);
  }
  ```

  `Raw(n)` is a local helper returning `Fixed26_6::FromRaw(n)`; `FindRow` is a local key lookup with `REQUIRE`.

- [ ] **Step 6: Write failing scene-generation exhaustion and load tests**

  Append the two authority cases. The injected atomic is a test seam; production owns exactly one process-global counter.

  ```cpp
  #include <atomic>
  #include <cstdint>
  #include <limits>

  TEST_CASE("editor scene generations fail closed before wrap") {
      std::atomic<std::uint64_t> next{
          std::numeric_limits<std::uint64_t>::max() - 1u};
      CHECK(molga::AllocateEditorSceneGeneration(next) ==
            std::numeric_limits<std::uint64_t>::max() - 1u);
      CHECK_FALSE(molga::AllocateEditorSceneGeneration(next).has_value());
      CHECK(next.load() == std::numeric_limits<std::uint64_t>::max());
      CHECK_FALSE(molga::AllocateEditorSceneGeneration(next).has_value());
      CHECK(next.load() == std::numeric_limits<std::uint64_t>::max());
      std::atomic<std::uint64_t> wrapped{0u};
      CHECK_FALSE(molga::AllocateEditorSceneGeneration(wrapped).has_value());
      CHECK(wrapped.load() == 0u);
  }

  TEST_CASE("only a successful scene replacement publishes fresh authority") {
      std::atomic<std::uint64_t> next{90u};
      molga::EditorSceneAuthority authority(next);
      REQUIRE(authority.PublishAfterSuccessfulLoad(true)); // editor creation
      const auto initial = authority.Generation();
      CHECK(initial == 90u);

      CHECK_FALSE(authority.PublishAfterSuccessfulLoad(false));
      CHECK(authority.Generation() == initial); // failed Open preserves authority

      REQUIRE(authority.PublishAfterSuccessfulLoad(true));
      CHECK(authority.Generation() == 91u); // successful New/Open is fresh
  }
  ```

- [ ] **Step 7: Run the red Inspector gate**

  Run:

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_editor -j
  ctest --test-dir build/debug -R '^test_ui_editor$' --output-on-failure
  ```

  Expected: compilation fails because the three pure Inspector helpers, presentation records, and scene-authority allocator do not exist.

- [ ] **Step 8: Define presentation records with every required field**

  Add to `InspectorWindow.h` in namespace `molga`:

  ```cpp
  struct InspectorImportDiagnosticRow {
      std::string stableCode;
      molga::text::TextSeverity severity = molga::text::TextSeverity::Error;
      std::string subsystem;
      std::string message;
      std::string remediation;
      std::string assetGuid;
      unsigned int sceneObjectId = 0;
      std::string componentType;
      molga::text::SourceByteRange sourceByteRange;
  };

  struct InspectorComputedRow {
      std::string key;
      std::string value;
      bool readOnly = true;
      std::string readOnlyReason = "Runtime snapshot value";
      std::string drivenBy;
      std::function<bool(const std::string&)> setter; // always empty
  };

  std::size_t ApplySceneObjectPickerValue(
      const EditorPropertyDescriptor&,
      const std::vector<EditorComponentIdentity>&,
      const EditorComponentResolver&,
      const EditorSceneReferenceContext&,
      const EditorSceneObjectChoice&);
  std::vector<InspectorImportDiagnosticRow> BuildImportDiagnosticRows(
      const AssetRecord&);
  std::vector<InspectorComputedRow> BuildComputedUILayoutRows(
      const ui::UILayoutNodeSnapshot&, const std::string& driver,
      Fixed26_6 baseline);
  ```

- [ ] **Step 9: Define the checked nonserialized scene authority**

  Add these declarations to `Editor.h`. `EditorSceneAuthority` is owned by `Editor`; only tests inject another counter.

  ```cpp
  std::optional<std::uint64_t> AllocateEditorSceneGeneration(
      std::atomic<std::uint64_t>& next) noexcept;

  class EditorSceneAuthority final {
  public:
      explicit EditorSceneAuthority(std::atomic<std::uint64_t>& next)
          : next_(next) {}
      bool PublishAfterSuccessfulLoad(bool loadSucceeded) noexcept;
      std::uint64_t Generation() const noexcept { return generation_; }

  private:
      std::atomic<std::uint64_t>& next_;
      std::uint64_t generation_ = 0;
  };
  ```

- [ ] **Step 10: Implement checked allocation without wrap or reuse**

  In `Editor.cpp`, keep the production counter process-global and implement allocation with a checked CAS. `UINT64_MAX` is a permanent exhausted sentinel and is never issued; zero is invalid and is never repaired by wrapping.

  ```cpp
  namespace {
  std::atomic<std::uint64_t> g_nextEditorSceneGeneration{1u};
  }

  std::optional<std::uint64_t> AllocateEditorSceneGeneration(
      std::atomic<std::uint64_t>& next) noexcept {
      auto candidate = next.load(std::memory_order_acquire);
      for (;;) {
          if (candidate == 0u ||
              candidate == std::numeric_limits<std::uint64_t>::max()) {
              return std::nullopt;
          }
          if (next.compare_exchange_weak(candidate, candidate + 1u,
                  std::memory_order_acq_rel, std::memory_order_acquire)) {
              return candidate;
          }
      }
  }

  bool EditorSceneAuthority::PublishAfterSuccessfulLoad(
      bool loadSucceeded) noexcept {
      if (!loadSucceeded) return false;
      const auto fresh = AllocateEditorSceneGeneration(next_);
      if (!fresh) return false;
      generation_ = *fresh;
      return true;
  }
  ```

- [ ] **Step 11: Publish generation only with an active-world replacement**

  Construct `Editor`'s authority with `g_nextEditorSceneGeneration`; expose
  `ActiveSceneGeneration()` as `sceneAuthority_.Generation()`. Preserve Task
  10.2's sole scene binding API: on first scene creation and when
  `SetActiveWorld(World&)` binds a different, successfully prepared world,
  obtain the editor scene authority before publishing the new non-owning
  `World*`. Derive hierarchy/Inspector choices only from
  `activeWorld_->Objects()` and forward the identical `World&` to Scene View
  and Game View. On successful New/Open, prepare the candidate `World` fully,
  obtain a fresh editor generation, and then publish the candidate. If parsing,
  loading, or generation allocation fails, keep both the old active-world
  pointer and its generation unchanged and report a blocking editor
  diagnostic. Do not restore `SetGameObjects(vector*)`, retain a parallel
  scene-vector authority, or serialize either generation.

- [ ] **Step 12: Implement the generation-scoped picker helper**

  Use only stable values:

  ```cpp
  std::size_t ApplySceneObjectPickerValue(
      const EditorPropertyDescriptor& descriptor,
      const std::vector<EditorComponentIdentity>& targets,
      const EditorComponentResolver& resolve,
      const EditorSceneReferenceContext& context,
      const EditorSceneObjectChoice& choice) {
      if (descriptor.readOnly ||
          descriptor.type != EditorPropertyType::SceneObjectRef) return 0;
      if (choice.sourceSceneGeneration != context.sceneGeneration) return 0;
      if (!IsEditorPropertyValueValid(descriptor, choice.value, context)) return 0;
      return ApplyEditorPropertyValue(
          descriptor, targets, resolve, choice.value);
  }
  ```

- [ ] **Step 13: Implement diagnostic row projection**

  ```cpp

  for (const auto& diagnostic : record.importDiagnostics) {
      rows.push_back({StableTextDiagnosticCode(diagnostic.code),
          diagnostic.severity, diagnostic.subsystem, diagnostic.message,
          diagnostic.remediation, diagnostic.assetGuid,
          diagnostic.sceneObjectId, diagnostic.componentType,
          diagnostic.sourceByteRange});
  }
  ```

  No row falls back to `importError` when typed diagnostics exist.

- [ ] **Step 14: Implement computed rows from immutable values**

  Format every fixed value from `Raw()` integers, not locale-sensitive floats. `BuildComputedUILayoutRows` emits exactly `computedRect`, `intrinsicSize`, `baseline`, and `clip`; absent clip is the literal `none`. Each row has an empty setter.

- [ ] **Step 15: Wire only the scene-object picker control**

  Populate choices only from the active editor scene and stamp each with `Editor::ActiveSceneGeneration()`. Pass the matching context resolver to `ApplySceneObjectPickerValue`; never accept a dragged choice carrying another scene generation.

- [ ] **Step 16: Wire read-only and diagnostic rendering**

  Disable the whole row when `readOnly`; render `readOnlyReason` and `drivenBy` alongside the value. Feed an accepted picker change into the same single/multi `ComponentSnapshotCommand` or `BatchComponentSnapshotCommand` path already used for other properties:

  ```cpp
  if (descriptor.readOnly) ImGui::BeginDisabled();
  const bool changed = DrawEditorPropertyValue(descriptor, component, edited);
  if (descriptor.readOnly) ImGui::EndDisabled();
  if (!descriptor.drivenBy.empty()) {
      ImGui::TextDisabled("Driven by: %s", descriptor.drivenBy.c_str());
  }
  ```

  Draw diagnostic stable code, severity, subsystem, message, remediation, asset/object/component context, and byte range from `BuildImportDiagnosticRows`; do not fall back to `importError` when typed rows are present.

- [ ] **Step 17: Run the green Inspector gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_ui_editor \
    test_editor_property_descriptor -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_editor_property_descriptor)$' --output-on-failure
  ```

  Expected: both tests pass; generation exhaustion leaves the counter at `UINT64_MAX`, failed loads retain the old generation, successful loads get a fresh generation, picker writes use `targetId`, read-only rows have no setter, and all diagnostic fields round-trip into presentation.

- [ ] **Step 18: Commit Inspector integration**

  ```bash
  git add src/Editor/Editor.h src/Editor/Editor.cpp \
    src/Editor/Windows/InspectorWindow.h \
    src/Editor/Windows/InspectorWindow.cpp tests/test_ui_editor.cpp \
    tests/CMakeLists.txt
  git commit -m "feat: add UI reference and diagnostic inspector"
  ```

### Task 15.3: Register UI authoring, create font families, and scan transitive references

**Inherited obligation (an ordering constraint that was recorded but not honoured):**

- **A newly authored `UILabel` currently has no way to reference a font.** Task 8.2 made `UILabel`
  schema 2 family-only — `src/ECS/Components/UILabel.cpp:171` states outright that schema 2 has no
  `fontGuid` key, and `Serialize` round-trips a legacy payload verbatim only while
  `loadedLegacyFontGuid_` is set. Subplan 02 recorded the consequence and asked for a decision
  rather than resolving it unilaterally: "**Milestone 15 (editor font-family authoring) must land
  before Task 8.2**, because until it does the editor has no other way to point a new label at a
  font."
  **That ordering was not honoured** — Task 8.2 landed first. So between then and this task, a user
  who adds a `UILabel` in the editor can author no font for it: the legacy key is gone and no
  family-authoring UI exists yet. Existing scenes are unaffected, since their legacy payload is
  preserved untouched.
  This task creates font families and registers UI authoring, so it is where the gap closes. Treat
  "a newly created label can be pointed at a font entirely from the editor" as an exit condition,
  not an incidental outcome.
  Sources: `02-font-shaping-layout.md:3626-3636`, `src/ECS/Components/UILabel.cpp:167-200`.


**Files:**

- Create: `tests/support/ProjectFileCommandTestAccess.h`
- Create: `tests/support/ProjectFileCommandTestAccess.cpp`
- Modify: `src/Editor/Windows/ProjectBrowserWindow.h`
- Modify: `src/Editor/Windows/ProjectBrowserWindow.cpp`
- Modify: `src/Editor/Windows/InspectorWindow.cpp`
- Modify: `src/Editor/Commands/ObjectCommands.h`
- Modify: `src/Editor/Commands/ObjectCommands.cpp`
- Modify: `src/Editor/Commands/EditorCommand.h`
- Modify: `src/Editor/Commands/CommandHistory.h`
- Modify: `src/Editor/Commands/ProjectFileCommands.h`
- Modify: `src/Editor/Commands/ProjectFileCommands.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Editor/UIRegistry.cpp`
- Modify: `src/Editor/AssetReferenceScan.cpp`
- Modify: `tests/test_ui_editor.cpp`
- Modify: `tests/test_editor_undo_dirty.cpp`
- Modify: `tests/test_command_history.cpp`
- Modify: `tests/test_project_file_commands.cpp`
- Modify: `tests/test_asset_reference_scan.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `ProjectFileCreateCommand`, `ComponentAddCommand`,
  `FontFamilyImporter`, component factory registrations, and nested authored
  JSON.
- Produces: conditionally accepted commands, atomic undoable `.fontfamily`
  source/meta creation, UI Text Input/Layout Container presets, registry entries
  for all eight components, and deterministic reference scanning.

- [ ] **Step 1: Add failing registry and template tests**

  Append:

  ```cpp
  TEST_CASE("font family template is canonical schema one") {
      const auto json = nlohmann::json::parse(
          ProjectBrowserWindow::FontFamilyTemplateJson());
      CHECK(json == nlohmann::json{
          {"schemaVersion", 1},
          {"faces", nlohmann::json::array()},
          {"fallbackFamilyGuids", nlohmann::json::array()}});
      CHECK_FALSE(json.contains("guid"));
  }

  TEST_CASE("every production UI component has an editor registry entry") {
      for (const char* type : {"UILayoutElement", "UILayoutGroup",
              "UIContentSizeFitter", "UIMask", "UIScrollView",
              "UISelectable", "UITextInput", "UIAccessibility"}) {
          CHECK(UIRegistry::GetComponentInfo(type).registered);
      }
      CHECK(UIRegistry::GetFileTypeInfo(".fontfamily", false).registered);
  }

  TEST_CASE("font family source rejects schema alias") {
      auto source = nlohmann::json{
          {"schema", 1},
          {"faces", nlohmann::json::array()},
          {"fallbackFamilyGuids", nlohmann::json::array()}};
      const auto result = ImportFontFamilyJsonForTesting(source);
      CHECK_FALSE(result.ok);
      REQUIRE(result.diagnostics.size() == 1u);
      CHECK(result.diagnostics[0].code == TextDiagnosticCode::FontFamilyInvalid);
      CHECK(result.diagnostics[0].subsystem == "FontFamilyImporter");
      CHECK(result.diagnostics[0].message.find("schemaVersion") !=
            std::string::npos);
  }
  ```

  Extend `UIRegistry::{FileTypeInfo,ComponentTypeInfo}` with `bool registered`; existing known entries use `true`, fallbacks use `false`, so this test never compares icon glyph addresses.

- [ ] **Step 2: Add the failing transitive-reference test**

  Add to `tests/test_asset_reference_scan.cpp`:

  ```cpp
  TEST_CASE("reference scan follows UI and family authored GUID fields") {
      test_support::TempDirectory temp{"ui-family-refs"};
      const auto root = temp.Path();
      WriteJson(root / "Families/main.fontfamily", {
          {"schemaVersion", 1},
          {"faces", {{{"fontGuid", "font-guid"}, {"faceIndex", 0}}}},
          {"fallbackFamilyGuids", {"fallback-guid"}}});
      WriteJson(root / "Scenes/main.json", {
          {"gameObjects", {{{"components", {{
              {"type", "UITextInput"},
              {"fontFamilyGuid", "family-guid"},
              {"textViewport", {{"targetId", 18}}},
              {"renderedLabel", {{"targetId", 19}}},
              {"placeholderLabel", {{"targetId", 20}}}
          }}}}}}});
      CHECK(HasPath(molga::AssetReferenceScan::FindReferencers(
                        root, "font-guid"), "Families/main.fontfamily"));
      CHECK(HasPath(molga::AssetReferenceScan::FindReferencers(
                        root, "fallback-guid"), "Families/main.fontfamily"));
      CHECK(HasPath(molga::AssetReferenceScan::FindReferencers(
                        root, "family-guid"), "Scenes/main.json"));
  }
  ```

- [ ] **Step 3: Add the failing conditional-history test**

  Add to `tests/test_command_history.cpp`:

  ```cpp
  struct ConditionalCounterCommand final : molga::ICommand {
      int& value;
      bool& permit;
      bool succeeded = false;
      ConditionalCounterCommand(int& valueIn, bool& permitIn)
          : value(valueIn), permit(permitIn) {}
      void Execute() override {
          succeeded = permit;
          if (succeeded) ++value;
      }
      void Undo() override {
          succeeded = permit;
          if (succeeded) --value;
      }
      std::string Name() const override { return "Conditional Counter"; }
      bool Succeeded() const noexcept override { return succeeded; }
  };

  TEST_CASE("failed execute undo and redo preserve history position") {
      int value = 0;
      bool permit = false;
      molga::CommandHistory history;
      CHECK_FALSE(history.Execute(
          std::make_unique<ConditionalCounterCommand>(value, permit)));
      CHECK(value == 0);
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());

      permit = true;
      REQUIRE(history.Execute(
          std::make_unique<ConditionalCounterCommand>(value, permit)));
      permit = false;
      CHECK_FALSE(history.Undo());
      CHECK(value == 1);
      CHECK(history.CanUndo());
      CHECK_FALSE(history.CanRedo());
      CHECK(history.IsDirty());

      permit = true;
      REQUIRE(history.Undo());
      CHECK(value == 0);
      permit = false;
      CHECK_FALSE(history.Redo());
      CHECK(history.CanRedo());
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());
  }
  ```

- [ ] **Step 4: Add the failing successful file-pair Undo/Redo test**

  In `tests/test_project_file_commands.cpp`, execute the new template through a local history:

  ```cpp
  TEST_CASE("font family creation is one undoable file command") {
      auto& authority =
          test_support::AssetDatabaseTestAuthority::Get();
      const auto assetsRoot =
          authority.AssetsCaseRoot("font-family-create");
      const auto target = assetsRoot / "Primary.fontfamily";
      auto& database = molga::AssetDatabase::Get();
      database.Clear();
      std::string authorityError;
      REQUIRE(authority.Bind(database, &authorityError));
      database.ScanProject(assetsRoot);
      const std::string templateBytes =
          "{\n  \"schemaVersion\": 1,\n  \"faces\": [],\n"
          "  \"fallbackFamilyGuids\": []\n}\n";
      molga::CommandHistory history;
      REQUIRE(history.Execute(std::make_unique<molga::ProjectFileCreateCommand>(
          target, templateBytes, false)));
      const auto first = ReadBytes(target);
      CHECK(first == templateBytes);
      const auto meta = molga::AssetMeta::MetaPathFor(target);
      REQUIRE(std::filesystem::exists(meta));
      const auto firstMeta = ReadBytes(meta);
      const auto metaJson = nlohmann::json::parse(firstMeta);
      const auto familyGuid = metaJson.at("guid").get<std::string>();
      CHECK(metaJson.at("importer") == "FontFamilyImporter");
      CHECK(metaJson.at("importerVersion") == 1);
      CHECK(metaJson.at("settings") == nlohmann::json::object());
      CHECK_FALSE(nlohmann::json::parse(first).contains("guid"));
      REQUIRE(history.Undo());
      CHECK_FALSE(std::filesystem::exists(target));
      CHECK_FALSE(std::filesystem::exists(meta));
      REQUIRE(history.Redo());
      CHECK(ReadBytes(target) == first);
      CHECK(ReadBytes(meta) == firstMeta);
      CHECK(database.GuidForSource("Primary.fontfamily") == familyGuid);
      const auto* record = database.Find(familyGuid);
      REQUIRE(record != nullptr);
      CHECK(record->importer == "FontFamilyImporter");
      CHECK(record->importerVersion == 1);
      CHECK(record->settings == nlohmann::json::object());
      database.Clear();
  }
  ```

  Consume Task 4.1's exact header-only
  `tests/AssetDatabaseTestAuthority.h`; do not add a reset API or test friend to
  production `AssetDatabase`. Its function-local process-lifetime `Get()` owns
  one immutable `ProjectRoot()`, `AssetsCaseRoot(caseName)` creates checked
  per-case subtrees below that root's `Assets`, and `Bind(database,error)` binds
  only when absent or requires
  `FontArtifacts()->IsProjectAuthorityFor(ProjectRoot())`. `Clear()` removes
  records and scan state but deliberately retains the store. A local RAII
  fixture clears records before/after each singleton case and never destroys or
  rebinds the process authority.

- [ ] **Step 5: Add the failing initial-meta rollback test**

  ```cpp
  TEST_CASE("font family initial meta failure is not pushed") {
      test_support::TempDirectory temp{"font-family-meta-fail"};
      const auto target = temp.Path() / "Primary.fontfamily";
      const auto meta = molga::AssetMeta::MetaPathFor(target);
      auto fault = molga::test_support::ProjectFileCommandFaultScope::
          FailNextMetaPublish(1u);
      molga::CommandHistory history;
      CHECK_FALSE(history.Execute(
          std::make_unique<molga::ProjectFileCreateCommand>(
              target, ProjectBrowserWindow::FontFamilyTemplateJson(), false)));
      CHECK_FALSE(fs::exists(target));
      CHECK_FALSE(fs::exists(meta));
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());
  }
  ```

- [ ] **Step 6: Add the failing Redo-meta rollback test**

  ```cpp
  TEST_CASE("font family redo meta failure stays redoable and clean") {
      test_support::TempDirectory temp{"font-family-redo-meta-fail"};
      const auto target = temp.Path() / "Primary.fontfamily";
      const auto meta = molga::AssetMeta::MetaPathFor(target);
      molga::CommandHistory history;
      REQUIRE(history.Execute(
          std::make_unique<molga::ProjectFileCreateCommand>(
              target, ProjectBrowserWindow::FontFamilyTemplateJson(), false)));
      REQUIRE(history.Undo());
      REQUIRE_FALSE(fs::exists(target));
      REQUIRE_FALSE(fs::exists(meta));
      auto fault = molga::test_support::ProjectFileCommandFaultScope::
          FailNextMetaPublish(1u);
      CHECK_FALSE(history.Redo());
      CHECK_FALSE(fs::exists(target));
      CHECK_FALSE(fs::exists(meta));
      CHECK(history.CanRedo());
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());
  }
  ```

- [ ] **Step 7: Add the failing preset byte-identity test**

  In `tests/test_editor_undo_dirty.cpp`, reuse its active `s_gameObjects`/Editor harness and add one table case per new preset:

  ```cpp
  TEST_CASE("layout and text-input presets redo byte identically") {
      for (const auto preset : {molga::UIPresetType::LayoutContainer,
                                molga::UIPresetType::TextInput}) {
          World world;
          auto& objects = world.Objects();
          s_gameObjects = &objects;
          s_sceneModified = false;
          auto& history = Editor::Get().GetCommandHistory();
          history.Clear();
          history.Execute(
              std::make_unique<molga::CreateUIPresetCommand>(preset));
          const auto first = SceneSerializer::SerializeScene(
              objects, "EditorTest").dump();
          history.Undo();
          CHECK(objects.empty());
          history.Redo();
          CHECK(SceneSerializer::SerializeScene(
                    objects, "EditorTest").dump() == first);
          history.Clear();
          s_gameObjects = nullptr;
      }
  }
  ```

- [ ] **Step 8: Run the red authoring-discovery gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_ui_editor \
    test_command_history test_project_file_commands test_editor_undo_dirty \
    test_asset_reference_scan -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_command_history|test_project_file_commands|test_editor_undo_dirty|test_asset_reference_scan)$' --output-on-failure
  ```

  Expected: compilation fails because `ICommand::Succeeded` and bool-returning
  history methods are absent; after declarations alone are added, the
  `.fontfamily`, registry, preset, and scan assertions remain red.

- [ ] **Step 9: Make command acceptance explicit in the command base**

  Extend the base with default success so existing commands remain
  source-compatible, and move a command between stacks only after its most
  recent operation succeeds:

  ```cpp
  class ICommand {
  public:
      virtual ~ICommand() = default;
      virtual void Execute() = 0;
      virtual void Undo() = 0;
      virtual std::string Name() const = 0;
      virtual bool Succeeded() const noexcept { return true; }
  };

  bool CommandHistory::Execute(std::unique_ptr<ICommand> command) {
      if (!command) return false;
      command->Execute();
      if (!command->Succeeded()) return false;
      if (hasCleanMarker_ && cleanIndex_ > static_cast<int>(undo_.size()))
          hasCleanMarker_ = false;
      redo_.clear();
      undo_.push_back(std::move(command));
      return true;
  }

  bool CommandHistory::Undo() {
      if (undo_.empty()) return false;
      undo_.back()->Undo();
      if (!undo_.back()->Succeeded()) return false;
      redo_.push_back(std::move(undo_.back()));
      undo_.pop_back();
      return true;
  }

  bool CommandHistory::Redo() {
      if (redo_.empty()) return false;
      redo_.back()->Execute();
      if (!redo_.back()->Succeeded()) return false;
      undo_.push_back(std::move(redo_.back()));
      redo_.pop_back();
      return true;
  }
  ```

  Change all three declarations to `bool`; callers may ignore the result.
  Redo-branch truncation and clean-marker invalidation occur only after a
  successful fresh Execute. Failed Execute/Undo/Redo leaves both stack sizes
  and dirty state unchanged. Mark existing conditional implementations such as
  `AssetContentCommand::Succeeded` as `const noexcept override`.

- [ ] **Step 10: Run the focused conditional-history test**

  Run:

  ```bash
  cmake --build --preset debug --target test_command_history -j
  ctest --test-dir build/debug -R '^test_command_history$' --output-on-failure
  ```

  Expected: successful existing commands behave unchanged; failed Execute is
  not pushed, failed Undo remains undoable/dirty, and failed Redo remains
  redoable/clean.

- [ ] **Step 11: Add canonical `.fontfamily` creation**

  Add `CreateMode::FontFamily`, `StartCreateFontFamily()`, and:

  ```cpp
  static std::string FontFamilyTemplateJson() {
      nlohmann::ordered_json json;
      json["schemaVersion"] = 1;
      json["faces"] = nlohmann::ordered_json::array();
      json["fallbackFamilyGuids"] = nlohmann::ordered_json::array();
      return json.dump(2) + "\n";
  }
  ```

  The Project Browser menu executes exactly one `ProjectFileCreateCommand(currentPath / (name + ".fontfamily"), FontFamilyTemplateJson(), false)` through the asset command history. The source never contains `guid`; after the first file write, the normal AssetDatabase scan creates the canonical `.fontfamily.meta`. Extend `ProjectFileCreateCommand` to capture those exact sidecar bytes after first import, remove source+sidecar on Undo, and restore source+captured sidecar before `OnSourceAdded` on Redo, so the GUID never changes. `FontFamilyAsset.guid` is copied only from `AssetRecord.guid`. A source-level `guid` is an importer blocker rather than an alternate authority. The new file is imported immediately; show its typed importer diagnostics rather than claiming a valid family when its face list is empty.

  `ProjectFileCreateCommand` resets `succeeded_=false` at the start of every
  Execute/Undo, overrides `bool Succeeded() const noexcept`, and sets it true
  only after the complete source/meta operation and AssetDatabase notification
  succeed. The FontFamily importer accepts only `schemaVersion`; a source
  containing legacy/ambiguous key `schema` fails with its typed importer
  diagnostic and is never normalized silently.

  Before any singleton-backed project test calls `ScanProject`, establish a
  `shared_ptr<const FontArtifactStore>` from
  `FontArtifactStore::ForProject(exactProjectRoot)` through
  `BindFontArtifactStore` and assert the error output. A missing store remains
  fail-closed; no command/test may rely on an unbound scan.

  ```cpp
  // ProjectFileCreateCommand::Execute, after source AtomicWriteText succeeds
  const auto meta = AssetMeta::MetaPathFor(target_);
  if (capturedMetaBytes_) {
      if (!PersistentStorage::AtomicWriteText(meta, *capturedMetaBytes_)) {
          std::filesystem::remove(target_);
          return;
      }
  }
  AssetDatabase::Get().OnSourceAdded(RelativeToAssetRoot(target_));
  if (!capturedMetaBytes_) capturedMetaBytes_ = ReadFileExact(meta);
  succeeded_ = capturedMetaBytes_.has_value();
  ```

  Add `std::optional<std::string> capturedMetaBytes_` and
  `bool succeeded_=false`. If initial import does not produce a readable
  canonical meta, or Redo cannot publish the captured meta, remove the
  just-written source and any command-owned partial meta, send the matching
  AssetDatabase removal notification, and leave `succeeded_` false. Undo
  preflights exact captured source/meta bytes, moves the pair to command-owned
  sibling stashes with reverse rollback, notifies removal only after both moves,
  and then sets success. Thus a failed initial Execute is never pushed and a
  failed Undo/Redo remains on its current stack without partial pair bytes.

- [ ] **Step 12: Compile the file-command fault companion seam**

  Forward-declare only `ProjectFileCommandTestAccess` in the production command
  header. `AssetDatabase` receives no test accessor, reset hook, or conditional
  definition; singleton tests consume `AssetDatabaseTestAuthority::Get()` and
  its Clear-retains-binding contract. In `ProjectFileCreateCommand`, keep this
  always-compiled state private:

  ```cpp
  enum class TestFaultPoint : std::uint8_t { None, MetaPublish };
  static void ConfigureTestFaultForFriend(
      TestFaultPoint, std::size_t oneBasedIndex) noexcept;
  static void ClearTestFaultForFriend() noexcept;
  friend class molga::test_support::ProjectFileCommandTestAccess;
  ```

  `ProjectFileCommandTestAccess.cpp` implements the RAII
  `ProjectFileCommandFaultScope`; its destructor always clears the hook. Create
  one test-only static library, without `MOLGA_TESTING`:

  ```cmake
  add_library(molga_test_seams STATIC
    support/ProjectFileCommandTestAccess.cpp)
  target_include_directories(molga_test_seams PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(molga_test_seams PUBLIC molga_core molga_warnings)
  target_link_libraries(test_project_file_commands PRIVATE molga_test_seams)
  target_link_libraries(test_ui_editor PRIVATE molga_test_seams)
  ```

  The wrapper is declared only under `tests/support`; the editor command TU
  editor compile the private hook definitions exactly once, while production
  source cannot call them. The Step 22 green build is the link-focused check;
  expected output contains no undefined `ForTesting` symbol.

- [ ] **Step 13: Register the file and component type metadata**

  Add `.fontfamily` and all eight component names to `UIRegistry`, with `registered=true` only for real entries. Do not compare or persist icon glyph pointers.

- [ ] **Step 14: Add all eight components to the Inspector menu**

  Add `UILayoutElement`, `UILayoutGroup`, `UIContentSizeFitter`, `UIMask`, `UIScrollView`, `UISelectable`, `UITextInput`, and `UIAccessibility` to the UI Add Component submenu. Each action continues to execute one `ComponentAddCommand` through normal history.

- [ ] **Step 15: Build the Layout Container preset**

  Extend `UIPresetType` with `LayoutContainer`; build it exactly as follows:

  ```cpp
  element->AddComponent<RectTransform>();
  element->AddComponent<UILayoutGroup>();
  element->AddComponent<UIContentSizeFitter>();
  ```

- [ ] **Step 16: Build the Text Input preset shell**

  Extend `UIPresetType` with `TextInput`; construct the root exactly as follows:

  ```cpp
  element->AddComponent<RectTransform>();
  element->AddComponent<UIImage>();
  element->AddComponent<UISelectable>();
  element->AddComponent<UITextInput>();
  element->AddComponent<UIAccessibility>();
  ```

- [ ] **Step 17: Attach Text Input children and stable references**

  Create two child labels named `Text` and `Placeholder`. After IDs exist,
  assign `UITextInput.renderedLabel`, `placeholderLabel`, and `textViewport`
  through `SceneObjectRef.targetId`. Undo removes the entire preset and Redo
  reuses/remaps the same stable IDs through the existing command ownership.
  `UIScrollView.viewport` remains its separate authored field; never serialize a
  `viewport` alias inside `UITextInput`.

- [ ] **Step 18: Enumerate only supported authored documents**

  Include `.json`, `.prefab`, `.fontfamily`, `.localization`, and the current engine-authored JSON asset extensions. Canonicalize candidate paths, skip directories/symlinks/unknown binary extensions, and emit a typed scan diagnostic for malformed supported JSON.

- [ ] **Step 19: Visit only string GUID values recursively**

  Recursively compare **string GUID values**; do not compare numeric `SceneObjectRef.targetId` against an asset GUID. Sort and unique canonical result paths before returning:

  ```cpp
  if (value.is_string() && value.get_ref<const std::string&>() == guid) {
      found = true;
  } else if (value.is_array() || value.is_object()) {
      for (const auto& child : value) Visit(child);
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  ```

- [ ] **Step 20: Run the focused file-command case**

  Run:

  ```bash
  cmake --build --preset debug --target test_project_file_commands -j
  build/debug/tests/test_project_file_commands --test-case="*font family creation*"
  ```

  Expected: source and authoritative sidecar bytes survive Execute/Undo/Redo
  exactly, the sidecar GUID is unchanged, and the source contains no `guid`.

- [ ] **Step 21: Run the focused preset-history case**

  Run:

  ```bash
  cmake --build --preset debug --target test_editor_undo_dirty -j
  build/debug/tests/test_editor_undo_dirty --test-case="*presets redo byte identically*"
  ```

  Expected: both presets Undo completely and Redo to byte-identical scene JSON with stable `targetId` references.

- [ ] **Step 22: Run the green authoring-discovery gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_ui_editor \
    test_command_history test_project_file_commands test_editor_undo_dirty \
    test_asset_reference_scan -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_command_history|test_project_file_commands|test_editor_undo_dirty|test_asset_reference_scan)$' --output-on-failure
  ```

  Expected: all five tests pass; registry fallbacks are distinguishable,
  template/source/meta bytes and sidecar GUID are stable, preset Redo is
  byte-identical, and family/UI GUID references are found once in sorted order.

- [ ] **Step 23: Commit authoring discovery and presets**

  ```bash
  git add src/Editor/Windows/ProjectBrowserWindow.h \
    src/Editor/Windows/ProjectBrowserWindow.cpp \
    src/Editor/Windows/InspectorWindow.cpp \
    src/Editor/Commands/EditorCommand.h \
    src/Editor/Commands/CommandHistory.h \
    src/Editor/Commands/ProjectFileCommands.h \
    src/Editor/Commands/ProjectFileCommands.cpp \
    src/Editor/Commands/ObjectCommands.h src/Editor/Commands/ObjectCommands.cpp \
    src/Core/AssetDatabase.h src/Core/AssetDatabase.cpp \
    src/Editor/UIRegistry.cpp src/Editor/AssetReferenceScan.cpp \
    tests/support/ProjectFileCommandTestAccess.h \
    tests/support/ProjectFileCommandTestAccess.cpp \
    tests/test_ui_editor.cpp tests/test_project_file_commands.cpp \
    tests/test_editor_undo_dirty.cpp tests/test_command_history.cpp \
    tests/test_asset_reference_scan.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat: add UI text authoring discovery"
  ```

#### AMENDMENT (2026-09-04) — inherited from Task 4.1: close the unauthored-font legacy branch

Task 4.1's review found a seam whose closer was named in a code comment but in no plan step. A
`.meta` carrying **no v2 font settings** takes the legacy import branch, so a font that the strict
path would reject imports with `importFailed = 0`, zero diagnostics, and no artifact — and then
passes `GameBuilder.cpp:269`, `runtime_main.cpp:298`, `AssetDependencyValidator` and
`EditorPropertyDescriptor`. In other words it ships. Milestones 5–8 stay closed only because
`FontAsset::FromRecord` refuses such a record, so nothing renders from it.

The behaviour is deliberate, tested and commented at `src/Core/Importers/FontImporter.cpp:470`,
which names "Task 8.2/15.2" as the closers. A grep of all 65 task files found **neither task
mentions it**, so the handoff existed only in a source comment. That is what this amendment fixes.

**Owner: Task 15.4**, because closing the branch *is* a legacy migration: it must convert
unauthored font `.meta` files to authored v2 settings, and the conversion has to be explicit and
byte-exactly undoable like every other migration this task owns. Add a step that:

- Detects a font `.meta` with no v2 settings and reports it as a migration candidate rather than
  importing it silently.
- Migrates it to authored v2 settings through the same undoable path as the other legacy
  conversions here.
- After migration, makes the unauthored branch a typed, blocking import failure rather than a
  silent success, so a `.meta` that was never migrated cannot ship.
- Updates the comment at `src/Core/Importers/FontImporter.cpp:470` to name the step that actually
  closed it, so the next reader is not sent to a task that does not mention the seam.

---

### Task 15.4: Make legacy UI/text migration explicit and byte-exactly undoable

**Inherited obligations (recorded by earlier tasks; this task owns closing them):**

- **A snapshot missing a key does not undo that field.** `RectTransform`, `UIImage`, `UIButton`,
  `UILabel` and `UICanvas` use the *current member value* as their `Deserialize` fallback instead of
  the documented default, so restoring a snapshot that omits a key leaves the field at its post-edit
  value. This is exactly what `CheckSchemaContract` points 4 and 5 exist to catch, which is why
  those components could not join that table. It affects undo **today**, not only migration, and
  changing it is a semantic change to undo/prefab-override behaviour rather than a schema version.
  Source: `03-ui-layout-rendering.md:1288-1298`.
- **An unauthored font `.meta` imports as a silent success and ships.** A `.meta` without authored
  v2 settings publishes no verified artifact, so no face binds through either resolver entry point
  and a user who drops a `.ttf` gets tofu unless the editor's font inspector authors settings.
  Milestones 5-8 stay closed only because `FontAsset::FromRecord` refuses the record. Closing the
  branch is itself a legacy migration, which is why it lands here.
  Sources: `02-font-shaping-layout.md:520-524` and `:4248-4254`, plus the "AMENDMENT (2026-09-04) —
  inherited from Task 4.1" block recorded under Task 15.3 in this file.


**Files:**

- Create: `src/Editor/Commands/MigrateUITextSchemaCommand.h`
- Create: `src/Editor/Commands/MigrateUITextSchemaCommand.cpp`
- Create: `tests/support/UITextMigrationTestAccess.h`
- Create: `tests/support/UITextMigrationTestAccess.cpp`
- Modify: `src/Editor/Windows/InspectorWindow.cpp`
- Modify: `tests/test_ui_editor.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: legacy loaded-schema markers,
  `SceneSerializer::{SerializeScene,DeserializeScene}`, the conditionally
  accepted `ICommand`/`CommandHistory` contract from Task 15.3, atomic
  `ProjectFileCreateCommand` semantics, and `FontFamilyAsset` schema `1`.
- Produces: `MigrateUITextSchemaCommand`, deterministic legacy-font family
  GUID/path rules, a publish rollback journal, and an explicit Inspector action.

- [ ] **Step 1: Add the literal legacy fixture and no-rewrite test**

  Append this exact structure to `tests/test_ui_editor.cpp`:

  ```cpp
  #include "Assets/FontFamilyAsset.h"
  #include "Core/AssetDatabase.h"
  #include "Core/AssetMeta.h"

  nlohmann::json MakeLegacyUITextSceneJson() {
      return {
          {"version", "1.0"}, {"name", "Legacy"},
          {"gameObjects", {{{"name", "Legacy Label"}, {"id", 7u},
              {"active", true}, {"parentId", -1},
              {"components", {
                  {{"type", "UICanvas"}, {"enabled", true},
                   {"schemaVersion", 1},
                   {"referenceResolution", {800.0, 600.0}},
                   {"matchWidthOrHeight", 0.5}, {"sortingOrder", 0}},
                  {{"type", "RectTransform"}, {"enabled", true},
                   {"anchorMin", {0.5, 0.5}}, {"anchorMax", {0.5, 0.5}},
                   {"pivot", {0.5, 0.5}},
                   {"anchoredPosition", {0.0, 0.0}},
                   {"sizeDelta", {320.0, 64.0}}},
                  {{"type", "UILabel"}, {"enabled", true},
                   {"text", "legacy"}, {"fontGuid", "legacy-font-guid"},
                   {"fontSizePx", 24.0}, {"lineSpacing", 1.0},
                   {"color", {1.0, 1.0, 1.0, 1.0}},
                   {"horizontalAlignment", 0},
                   {"verticalAlignment", 0}, {"sortingOrder", 1}}
              }}}}}};
  }

  std::vector<std::shared_ptr<GameObject>> LoadLegacyUITextObjects() {
      std::vector<std::shared_ptr<GameObject>> objects;
      if (!SceneSerializer::DeserializeScene(
              MakeLegacyUITextSceneJson(), objects)) {
          throw std::runtime_error("legacy UI/text fixture did not load");
      }
      return objects;
  }

  TEST_CASE("legacy load save stays legacy until explicit migration") {
      const nlohmann::json legacy = MakeLegacyUITextSceneJson();
      std::vector<std::shared_ptr<GameObject>> objects;
      REQUIRE(SceneSerializer::DeserializeScene(legacy, objects));
      const auto unopenedSave = SceneSerializer::SerializeScene(objects, "Legacy");
      CHECK(CanonicalComponents(unopenedSave) == CanonicalComponents(legacy));
      CHECK(FindComponent(unopenedSave, "UILabel").contains("fontGuid"));
      CHECK_FALSE(FindComponent(unopenedSave, "UILabel").contains("fontFamilyGuid"));
      CHECK(FindComponent(unopenedSave, "UICanvas")["schemaVersion"] == 1);
  }
  ```

- [ ] **Step 2: Add the successful migration Undo/Redo test**

  Define `ScopedSingletonAssetProjectAuthority` locally in
  `tests/test_ui_editor.cpp`: its constructor obtains
  `AssetDatabaseTestAuthority::Get()`, creates one checked
  `AssetsCaseRoot(caseName)`, clears records, calls the exact shared
  authority's `Bind(database,&error)`, and exposes `ProjectRoot()`,
  `AssetsRoot()`, and `Bound()`. A failed bind throws with the captured error
  before any command is constructible. Its destructor clears records but retains the
  immutable process-lifetime store. Every Task 15.4 test that executes
  `MigrateUITextSchemaCommand` creates this fixture before the command, even
  failure/collision cases, so `OnSourceAdded` is never exercised against an
  unbound singleton.

  ```cpp

  TEST_CASE("explicit migration is byte exact across undo redo") {
      ScopedSingletonAssetProjectAuthority project{"ui-text-migration"};
      REQUIRE(project.Bound());
      molga::AssetDatabase::Get().ScanProject(project.AssetsRoot());
      auto objects = LoadLegacyUITextObjects();
      const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, project.AssetsRoot() / "FontFamilies");
      auto* observed = command.get();
      REQUIRE(history.Execute(std::move(command)));
      REQUIRE(observed->Succeeded());
      const auto after = SceneSerializer::SerializeScene(objects, "Legacy");
      CHECK(FindComponent(after, "UICanvas")["schemaVersion"] ==
            UICanvas::CurrentSchemaVersion);
      CHECK(FindComponent(after, "UILabel").contains("fontFamilyGuid"));
      CHECK_FALSE(FindComponent(after, "UILabel").contains("fontGuid"));
      const auto created = observed->CreatedFamilyFiles();
      REQUIRE(created.size() == 1u);
      CHECK(fs::exists(created[0]));
      const auto metaPath = molga::AssetMeta::MetaPathFor(created[0]);
      CHECK(fs::exists(metaPath));
      CHECK(observed->CreatedFiles().size() == 2u);
      const auto familyGuid = molga::MigrateUITextSchemaCommand::
          FamilyGuidForLegacyGuid("legacy-font-guid");
      CHECK(FindComponent(after, "UILabel")["fontFamilyGuid"] == familyGuid);
      CHECK_FALSE(nlohmann::json::parse(ReadFile(created[0])).contains("guid"));

      molga::AssetDatabase database;
      std::string authorityError;
      REQUIRE(database.BindFontArtifactStore(
          std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(project.ProjectRoot())),
          &authorityError));
      database.ScanProject(project.AssetsRoot());
      const auto* familyRecord = database.Find(familyGuid);
      REQUIRE(familyRecord != nullptr);
      CHECK(familyRecord->guid == familyGuid);
      CHECK(familyRecord->importer == "FontFamilyImporter");
      CHECK_FALSE(familyRecord->importFailed);
      std::string familyError;
      const auto familyAsset = molga::FontFamilyAsset::FromRecord(
          *familyRecord, familyError);
      REQUIRE(familyAsset.has_value());
      CHECK(familyAsset->guid == familyRecord->guid);
      const auto& migratedLabel = FindComponent(after, "UILabel");
      const auto* resolvedForComponent = database.Find(
          migratedLabel["fontFamilyGuid"].get<std::string>());
      CHECK(resolvedForComponent == familyRecord);

      REQUIRE(history.Undo());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
      CHECK_FALSE(fs::exists(created[0]));
      CHECK_FALSE(fs::exists(metaPath));
      REQUIRE(history.Redo());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == after);
      CHECK(ReadFile(created[0]) == observed->CreatedFamilyBytes().at(created[0]));
      CHECK(ReadFile(metaPath) == observed->CreatedFamilyBytes().at(metaPath));
  }
  ```

- [ ] **Step 3: Add the failed-migration atomicity test**

  Add the migration failure case to `tests/test_ui_editor.cpp`:

  ```cpp
  TEST_CASE("failed migration is not pushed and leaves scene clean") {
      auto objects = LoadLegacyUITextObjects();
      const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, fs::path("/dev/null/cannot-create-family"));
      CHECK_FALSE(history.Execute(std::move(command)));
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
  }
  ```

- [ ] **Step 4: Add the identical existing-family reuse test**

  ```cpp
  TEST_CASE("migration reuses identical deterministic family bytes") {
      auto objects = LoadLegacyUITextObjects();
      ScopedSingletonAssetProjectAuthority project{"migration-identical"};
      const auto path = molga::MigrateUITextSchemaCommand::
          FamilyPathForLegacyGuid(project.AssetsRoot(), "legacy-font-guid");
      const auto bytes = molga::MigrateUITextSchemaCommand::
          FamilyBytesForLegacyGuid("legacy-font-guid");
      const auto metaPath = molga::AssetMeta::MetaPathFor(path);
      const auto metaBytes = molga::MigrateUITextSchemaCommand::
          FamilyMetaBytesForLegacyGuid("legacy-font-guid");
      test_support::WriteText(path, bytes);
      test_support::WriteText(metaPath, metaBytes);
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, project.AssetsRoot());
      REQUIRE(history.Execute(std::move(command)));
      REQUIRE(history.Undo());
      CHECK(ReadFile(path) == bytes); // pre-existing file is never deleted
      CHECK(ReadFile(metaPath) == metaBytes);
  }
  ```

- [ ] **Step 5: Add the conflicting existing-family rejection test**

  ```cpp

  TEST_CASE("migration rejects conflicting deterministic family bytes") {
      auto objects = LoadLegacyUITextObjects();
      const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
      ScopedSingletonAssetProjectAuthority project{"migration-conflict"};
      const auto path = molga::MigrateUITextSchemaCommand::
          FamilyPathForLegacyGuid(project.AssetsRoot(), "legacy-font-guid");
      test_support::WriteText(path, "different bytes\n");
      molga::CommandHistory history;
      CHECK_FALSE(history.Execute(
          std::make_unique<molga::MigrateUITextSchemaCommand>(
              objects, project.AssetsRoot())));
      CHECK_FALSE(history.IsDirty());
      CHECK(ReadFile(path) == "different bytes\n");
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
  }
  ```

- [ ] **Step 6: Add missing and mismatched meta rejection tests**

  ```cpp
  TEST_CASE("migration requires an exact source and meta pair for reuse") {
      const auto validMeta = molga::MigrateUITextSchemaCommand::
          FamilyMetaBytesForLegacyGuid("legacy-font-guid");
      auto wrongGuid = nlohmann::json::parse(validMeta);
      wrongGuid["guid"] = "00000000000000000000000000000000";
      auto wrongImporter = nlohmann::json::parse(validMeta);
      wrongImporter["importer"] = "FontImporter";
      const std::vector<std::pair<std::string,
          std::optional<std::string>>> rows{
          {"missing-meta", std::nullopt},
          {"wrong-guid", wrongGuid.dump(2)},
          {"wrong-importer", wrongImporter.dump(2)}};

      for (const auto& [name, authoredMeta] : rows) {
          CAPTURE(name);
          auto objects = LoadLegacyUITextObjects();
          const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
          ScopedSingletonAssetProjectAuthority project{name};
          const auto source = molga::MigrateUITextSchemaCommand::
              FamilyPathForLegacyGuid(project.AssetsRoot(), "legacy-font-guid");
          const auto meta = molga::AssetMeta::MetaPathFor(source);
          test_support::WriteText(source,
              molga::MigrateUITextSchemaCommand::
                  FamilyBytesForLegacyGuid("legacy-font-guid"));
          if (authoredMeta) test_support::WriteText(meta, *authoredMeta);
          molga::CommandHistory history;
          CHECK_FALSE(history.Execute(
              std::make_unique<molga::MigrateUITextSchemaCommand>(
                  objects, project.AssetsRoot())));
          CHECK_FALSE(history.CanUndo());
          CHECK_FALSE(history.IsDirty());
          CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
          CHECK(fs::exists(meta) == authoredMeta.has_value());
          if (authoredMeta) CHECK(ReadFile(meta) == *authoredMeta);
      }
  }
  ```

- [ ] **Step 7: Add the source-level GUID rejection test**

  ```cpp
  TEST_CASE("migration rejects a source-level family guid") {
      auto objects = LoadLegacyUITextObjects();
      const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
      ScopedSingletonAssetProjectAuthority project{"embedded-family-guid"};
      const auto source = molga::MigrateUITextSchemaCommand::
          FamilyPathForLegacyGuid(project.AssetsRoot(), "legacy-font-guid");
      const auto meta = molga::AssetMeta::MetaPathFor(source);
      auto sourceJson = nlohmann::json::parse(
          molga::MigrateUITextSchemaCommand::
              FamilyBytesForLegacyGuid("legacy-font-guid"));
      sourceJson["guid"] = molga::MigrateUITextSchemaCommand::
          FamilyGuidForLegacyGuid("legacy-font-guid");
      test_support::WriteText(source, sourceJson.dump(2) + "\n");
      test_support::WriteText(meta,
          molga::MigrateUITextSchemaCommand::
              FamilyMetaBytesForLegacyGuid("legacy-font-guid"));

      molga::CommandHistory history;
      CHECK_FALSE(history.Execute(
          std::make_unique<molga::MigrateUITextSchemaCommand>(
              objects, project.AssetsRoot())));
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
      CHECK(nlohmann::json::parse(ReadFile(source)).contains("guid"));
      CHECK_FALSE(history.IsDirty());
  }
  ```

- [ ] **Step 8: Add a mid-publish rollback test**

  ```cpp
  std::vector<std::shared_ptr<GameObject>>
  LoadLegacyUITextObjectsWithTwoFontGuids() {
      auto json = MakeLegacyUITextSceneJson();
      json["gameObjects"][0]["components"].push_back({
          {"type", "TextRenderer2D"}, {"enabled", true},
          {"text", "second"}, {"fontGuid", "second-legacy-font-guid"},
          {"fontSizePx", 18.0}, {"sortingOrder", 2}});
      std::vector<std::shared_ptr<GameObject>> objects;
      if (!SceneSerializer::DeserializeScene(json, objects)) {
          throw std::runtime_error("two-font legacy fixture did not load");
      }
      return objects;
  }

  TEST_CASE("migration rolls back every published file when publish two fails") {
      auto objects = LoadLegacyUITextObjectsWithTwoFontGuids();
      const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
      ScopedSingletonAssetProjectAuthority project{"migration-rollback"};
      auto fault = molga::test_support::UITextMigrationFaultScope::FailNext(
          molga::test_support::UITextMigrationFault::Publish, 2u);
      molga::CommandHistory history;
      CHECK_FALSE(history.Execute(
          std::make_unique<molga::MigrateUITextSchemaCommand>(
              objects, project.AssetsRoot())));
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());
      CHECK(fs::is_empty(project.AssetsRoot()));
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
  }
  ```

- [ ] **Step 9: Add the external file collision Undo test**

  ```cpp
  TEST_CASE("migration undo refuses externally modified family bytes") {
      auto objects = LoadLegacyUITextObjects();
      ScopedSingletonAssetProjectAuthority project{"migration-undo-collision"};
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, project.AssetsRoot());
      auto* observed = command.get();
      REQUIRE(history.Execute(std::move(command)));
      const auto after = SceneSerializer::SerializeScene(objects, "Legacy");
      const auto source = observed->CreatedFamilyFiles().at(0);
      const auto meta = molga::AssetMeta::MetaPathFor(source);
      const auto expectedMeta = ReadFile(meta);
      test_support::WriteText(source, "external replacement\n");

      CHECK_FALSE(history.Undo());
      CHECK(history.CanUndo());
      CHECK_FALSE(history.CanRedo());
      CHECK(history.IsDirty());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == after);
      CHECK(ReadFile(source) == "external replacement\n");
      CHECK(ReadFile(meta) == expectedMeta);
  }
  ```

- [ ] **Step 10: Add the external scene collision Undo test**

  ```cpp
  TEST_CASE("migration undo refuses a scene changed after migration") {
      auto objects = LoadLegacyUITextObjects();
      ScopedSingletonAssetProjectAuthority project{"migration-scene-collision"};
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, project.AssetsRoot());
      auto* observed = command.get();
      REQUIRE(history.Execute(std::move(command)));
      objects.at(0)->GetComponent<UILabel>()->SetText("external edit");
      CHECK(objects.at(0)->GetComponent<UILabel>()->GetText() ==
            "external edit");
      const auto external = SceneSerializer::SerializeScene(objects, "Legacy");
      const auto createdBytes = observed->CreatedFamilyBytes();

      CHECK_FALSE(history.Undo());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == external);
      for (const auto& [path, bytes] : createdBytes) {
          CHECK(ReadFile(path) == bytes);
      }
      CHECK(history.CanUndo());
      CHECK_FALSE(history.CanRedo());
  }
  ```

- [ ] **Step 11: Add the Undo I/O rollback test**

  ```cpp
  TEST_CASE("failed undo file move rolls back and stays undoable") {
      auto objects = LoadLegacyUITextObjectsWithTwoFontGuids();
      ScopedSingletonAssetProjectAuthority project{"migration-undo-io"};
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, project.AssetsRoot());
      auto* observed = command.get();
      REQUIRE(history.Execute(std::move(command)));
      const auto after = SceneSerializer::SerializeScene(objects, "Legacy");
      const auto createdBytes = observed->CreatedFamilyBytes();
      REQUIRE(createdBytes.size() == 4u); // two sources plus two metas
      auto fault = molga::test_support::UITextMigrationFaultScope::FailNext(
          molga::test_support::UITextMigrationFault::UndoMove, 2u);

      CHECK_FALSE(history.Undo());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == after);
      for (const auto& [path, bytes] : createdBytes) {
          CHECK(fs::exists(path));
          CHECK(ReadFile(path) == bytes);
      }
      CHECK(molga::test_support::UITextMigrationTestAccess::
                JournalStashPaths(*observed).empty());
      CHECK(history.CanUndo());
      CHECK_FALSE(history.CanRedo());
  }
  ```

- [ ] **Step 12: Add the Redo destination collision test**

  ```cpp
  TEST_CASE("migration redo refuses an externally occupied destination") {
      auto objects = LoadLegacyUITextObjects();
      ScopedSingletonAssetProjectAuthority project{"migration-redo-collision"};
      const auto before = SceneSerializer::SerializeScene(objects, "Legacy");
      molga::CommandHistory history;
      auto command = std::make_unique<molga::MigrateUITextSchemaCommand>(
          objects, project.AssetsRoot());
      auto* observed = command.get();
      REQUIRE(history.Execute(std::move(command)));
      const auto source = observed->CreatedFamilyFiles().at(0);
      REQUIRE(history.Undo());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
      test_support::WriteText(source, "external occupant\n");

      CHECK_FALSE(history.Redo());
      CHECK(SceneSerializer::SerializeScene(objects, "Legacy") == before);
      CHECK(ReadFile(source) == "external occupant\n");
      CHECK(history.CanRedo());
      CHECK_FALSE(history.CanUndo());
      CHECK_FALSE(history.IsDirty());
  }
  ```

- [ ] **Step 13: Run the red migration gate**

  Run:

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_ui_editor test_command_history -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_command_history)$' --output-on-failure
  ```

  Expected: compilation fails because `MigrateUITextSchemaCommand` is absent; legacy preservation continues to pass independently.

- [ ] **Step 14: Define one snapshot transaction**

  Add this API:

  ```cpp
  namespace molga {
  class MigrateUITextSchemaCommand final : public ICommand {
  public:
      MigrateUITextSchemaCommand(
          std::vector<std::shared_ptr<GameObject>>& objects,
          std::filesystem::path familyDirectory);
      void Execute() override;
      void Undo() override;
      std::string Name() const override { return "Migrate UI/Text Schemas"; }
      bool Succeeded() const noexcept override { return succeeded_; }
      const std::string& Error() const noexcept { return error_; }
      const std::vector<std::filesystem::path>& CreatedFamilyFiles() const;
      const std::vector<std::filesystem::path>& CreatedFiles() const;
      const std::map<std::filesystem::path, std::string>& CreatedFamilyBytes() const;
      static std::string FamilyGuidForLegacyGuid(
          const std::string& legacyFontGuid);
      static std::filesystem::path FamilyPathForLegacyGuid(
          const std::filesystem::path& directory,
          const std::string& legacyFontGuid);
      static std::string FamilyBytesForLegacyGuid(
          const std::string& legacyFontGuid);
      static std::string FamilyMetaBytesForLegacyGuid(
          const std::string& legacyFontGuid);
  private:
      enum class TestFaultPoint : std::uint8_t {
          None, Publish, UndoMove, RedoMove
      };
      static void ConfigureTestFaultForFriend(
          TestFaultPoint, std::size_t oneBasedIndex) noexcept;
      static void ClearTestFaultForFriend() noexcept;
      const std::vector<std::filesystem::path>&
          JournalStashPathsForFriend() const noexcept;
      friend class molga::test_support::UITextMigrationTestAccess;
      bool BuildTransaction();
      bool StageInitialJournal();
      bool PublishInitialJournal();
      bool PreflightAppliedState() const;
      bool PreflightUndoneState() const;
      bool MoveCreatedFilesToStashes();
      bool MoveStashesToCreatedFiles();
      void RollBackMovesInReverse(bool finalsAreDestination) noexcept;
      bool PrepareScene(const nlohmann::json&,
          std::vector<std::shared_ptr<GameObject>>&) const;
      std::vector<std::shared_ptr<GameObject>>* objects_ = nullptr;
      std::filesystem::path familyDirectory_;
      nlohmann::json beforeScene_;
      nlohmann::json afterScene_;
      struct FileMutation {
          std::filesystem::path path;
          std::filesystem::path temporaryPath;
          std::filesystem::path undoStashPath;
          bool existedBefore = false;
          std::string afterBytes;
          bool published = false;
          bool moved = false;
      };
      std::vector<FileMutation> fileJournal_;
      std::vector<std::filesystem::path> createdFamilyFiles_;
      std::vector<std::filesystem::path> createdFiles_;
      std::vector<std::filesystem::path> journalStashPaths_;
      enum class Phase { Initial, Applied, Undone };
      Phase phase_ = Phase::Initial;
      std::string error_;
      bool built_ = false;
      bool succeeded_ = false;
  };
  }
  ```

  Add `UITextMigrationTestAccess.cpp` as a third source of the test-only
  `molga_test_seams` library and link that library to `test_ui_editor`. The
  always-compiled setters remain private/friend-only; the companion TU exposes
  an RAII `UITextMigrationFaultScope` whose destructor clears the fault even
  after a failing `REQUIRE`. Each configured fault affects exactly the next
  matching operation and self-clears on the attempted operation. There is no
  `MOLGA_TESTING` declaration/definition split and no public production fault
  API.

- [ ] **Step 15: Derive one deterministic family source and meta pair**

  Use these deterministic rules:

  ```cpp
  const std::string familyGuid =
      Sha256String("molga:legacy-font-family:v1:" + legacyFontGuid).substr(0, 32);
  const fs::path familyPath =
      familyDirectory_ / (familyGuid + ".fontfamily");
  nlohmann::ordered_json family = {
      {"schemaVersion", 1},
      {"faces", nlohmann::ordered_json::array({{
          {"fontGuid", legacyFontGuid}, {"faceIndex", 0},
          {"weight", 400}, {"stretchPercent", 100}, {"slant", "Upright"},
          {"authoredFaceIndex", 0}}})},
      {"fallbackFamilyGuids", nlohmann::ordered_json::array()}
  };
  const fs::path metaPath = molga::AssetMeta::MetaPathFor(familyPath);
  nlohmann::ordered_json meta = {
      {"guid", familyGuid},
      {"importer", "FontFamilyImporter"},
      {"importerVersion", 1},
      {"settings", nlohmann::ordered_json::object()}
  };
  ```

  `FamilyBytesForLegacyGuid` returns `family.dump(2) + "\n"` and must never
  contain `guid`; `FamilyMetaBytesForLegacyGuid` returns `meta.dump(2)` to
  match `AssetMeta::Write`. The component receives exactly
  `FamilyGuidForLegacyGuid`, which equals only the authoritative sidecar
  `guid`; after rescan `FontFamilyAsset.guid` is populated from that
  `AssetRecord.guid`. Reuse one source/meta pair for repeated identical legacy
  font GUIDs. Treat any source-level `guid` as a blocker, even when it happens
  to equal the sidecar.

- [ ] **Step 16: Preflight every existing deterministic destination**

  Build `fileJournal_` without writes, treating a `.fontfamily` and its `.fontfamily.meta` as one inseparable pair. Both absent means create both; both present and byte-identical means reuse both. Source-only, meta-only, wrong GUID, wrong importer/version/settings, or any byte mismatch fails before making a temp file. Canonicalize both destinations under `familyDirectory_`, reject symlink/escape paths, and add only absent pairs to `createdFiles_`.

  ```cpp
  if (fs::exists(source) != fs::exists(meta)) {
      return Fail("deterministic font family source/meta pair is incomplete");
  }
  if (fs::exists(source) &&
      (ReadFile(source) != expectedSource ||
       ReadFile(meta) != expectedMeta)) {
      return Fail("deterministic font family source/meta pair differs");
  }
  ```

- [ ] **Step 17: Build and validate the after-scene off to the side**

  Canvas migration copies old reference resolution, match, scale, and sorting into the current schema. `UILabel`/`TextRenderer2D` replace legacy `fontGuid` with exactly `FamilyGuidForLegacyGuid(oldFontGuid)` only in `afterScene_`; unknown fields remain. Require every new family GUID to have its exact source/meta journal pair, require the source to omit `guid`, and require the sidecar GUID/importer/version/settings to be authoritative. Deserialize `afterScene_` into a temporary vector and serialize it again before any file/live-scene mutation.

- [ ] **Step 18: Stage every new source and meta into sibling temporary files**

  For each absent source and meta destination, write `afterBytes` to a unique sibling temp, flush/close, re-read, and compare bytes. Precompute a unique sibling undo-stash path and require it absent. A failure removes all temps and changes no destination or live scene.

- [ ] **Step 19: Publish with a reverse rollback journal**

  Prepare a fully deserialized `afterScene_` vector first. Publish only absent-pair temps in sorted destination order. After each rename set `published=true`. If any rename fails, remove published command-created files in reverse and leave the live scene untouched. After all renames succeed, swap the live object vector with the prepared vector (noexcept), set `phase_=Applied`, and return success. Reused exact pairs are never rewritten.

  Before the scene swap, notify the active `AssetDatabase` of each newly
  published source in sorted order and require `Find(familyGuid)`, importer
  `FontFamilyImporter`, nonfailed import, and
  `FontFamilyAsset::FromRecord(...)->guid == familyGuid`. If any lookup/import
  check fails, notify removal for only the sources this command added, roll the
  published files back in reverse, and leave the scene untouched. Thus the
  component GUID is resolvable at the instant the migrated scene becomes live.

  ```cpp
  for (FileMutation& mutation : fileJournal_) {
      if (mutation.existedBefore) continue;
      if (!Publish(mutation.temporaryPath, mutation.path)) {
          RollBackMovesInReverse(/*finalsAreDestination=*/true);
          return Fail("font family publish failed");
      }
      mutation.published = true;
  }
  ```

- [ ] **Step 20: Make Undo replay stored journal bytes**

  At the start of each operation set `succeeded_=false`. Undo first requires canonical live scene bytes equal `afterScene_`, every created source/meta final exists with exact `afterBytes`, every reused pair remains exact, and every undo-stash path is absent. Prepare `beforeScene_` off to the side. Rename each created final to its stash; if move `N` fails, rename prior stashes back in reverse and leave the scene unchanged. Only after all moves succeed, notify `AssetDatabase::OnSourceRemoved` for the created sources, swap in the prepared before-scene, set `phase_=Undone`, and set success. Final asset paths are absent, but command-owned exact bytes remain in stashes for Redo.

- [ ] **Step 21: Make Redo replay the same stored journal bytes**

  Redo first requires canonical live scene bytes equal `beforeScene_`, every
  created final destination absent, every stash present with exact
  `afterBytes`, and every reused pair exact. Prepare `afterScene_`, rename
  stashes to finals with the symmetric reverse rollback, notify and revalidate
  the same AssetDatabase records as initial Execute, then swap the scene and
  set `phase_=Applied`. A destination collision, stash mutation, scene
  edit, or injected I/O failure changes nothing and leaves the command on its
  current history stack. Never recompute GUID/source/meta JSON. When an undone
  command is discarded because the redo branch is cleared, delete only its
  exact command-owned stash files; retain and diagnose a cleanup failure as a
  recovery artifact.

- [ ] **Step 22: Expose exactly one explicit editor action**

  In the Inspector legacy-schema warning, add `Migrate UI/Text Schemas…`. Execute the command through scene `CommandHistory`; do not invoke it from `OnGUI`, save, GameBuilder, or importer code without a button confirmation. If the command fails, show `Error()` and keep the scene clean/unchanged.

- [ ] **Step 23: Run the green migration gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_ui_editor \
    test_command_history test_scene_serializer test_prefab -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_command_history|test_scene_serializer|test_prefab)$' --output-on-failure
  ```

  Expected: migration tests pass; source/meta and AssetDatabase GUID/importer resolution are exact; initial publish, failed Undo, and failed Redo are atomic; external scene/file collisions leave history and bytes untouched; successful Undo restores exact legacy payload/final-path absence; and Redo restores exact current payload/source/meta bytes.

- [ ] **Step 24: Commit explicit migration**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt \
    src/Editor/Commands/MigrateUITextSchemaCommand.h \
    src/Editor/Commands/MigrateUITextSchemaCommand.cpp \
    src/Editor/Windows/InspectorWindow.cpp \
    tests/support/UITextMigrationTestAccess.h \
    tests/support/UITextMigrationTestAccess.cpp tests/test_ui_editor.cpp
  git commit -m "feat: add explicit UI text schema migration"
  ```

## Milestone 15 Exit Gate

**Files:**

- None. This gate is read-only; failures return to the owning code task.

**Interfaces:**

- Consumes: all Task 15 commits.
- Produces: the subplan exit evidence required by package work; no package or qualification claim.

- [ ] **Prove runtime-only state never enters authored snapshots**

  Run:

  ```bash
  cmake --build --preset debug --target test_ui_editor \
    test_editor_property_descriptor test_command_history \
    test_asset_reference_scan test_scene_serializer test_prefab -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_editor_property_descriptor|test_command_history|test_asset_reference_scan|test_scene_serializer|test_prefab)$' --output-on-failure
  ```

  Expected: all focused tests pass; serialization assertions contain no computed rect, intrinsic size, baseline, clip, focus, runtime value, caret, selection, composition, blink, cache, or atlas residency key.

- [ ] **Run editor smoke and complete Debug regression**

  Run:

  ```bash
  cmake --build --preset debug --target molga_engine editor_smoke -j
  ctest --test-dir build/debug -R '^editor_smoke$' --output-on-failure
  ctest --preset debug
  ```

  Expected: `editor_smoke` and the complete Debug suite pass.

- [ ] **Inspect legacy and migration representations directly**

  Run:

  ```bash
  build/debug/tests/test_ui_editor --test-case="*legacy*"
  build/debug/tests/test_ui_editor --test-case="*migration*"
  git diff --check
  git status --short --branch
  ```

  Expected: both doctest filters pass; no whitespace errors; only intended committed work and pre-existing unrelated paths remain.

If any exit check fails, return to the owning Task 15.1–15.4, add a focused failing regression, implement the narrow fix, make that task's independent commit, and rerun this entire gate. Do not make an empty or mixed verification commit.

---

## Execution Handoff

After every Task 15 commit and the exit contract are green, continue to [`06-macos-package-policy.md`](06-macos-package-policy.md). Do not begin package work with a partially migrated editor contract.
