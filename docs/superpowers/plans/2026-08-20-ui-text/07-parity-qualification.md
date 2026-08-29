# UI/Text Parity and macOS Qualification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove byte-identical editor/development/copied-package text/UI structure, enforce universal allocation/atlas correctness and reference-machine timing gates, verify SDL_GPU/Metal output, and record two real visible macOS IME sessions against one reviewed candidate commit.

**Architecture:** Export a canonical structural snapshot before physical raster differences, using immutable content identities and canonical trace ordinals while excluding frame/history/runtime identities. Materialize one preserved qualification editor project, one independently staged complete development resource root, and one sealed `.app` from the same immutable inputs; drive editor, explicit development runtime, and a checkout-external copied app from their distinct authorities and compare complete resource hashes plus canonical output bytes. Keep code/harness creation and review separate from qualification: first commit and review the candidate, then run fresh automated gates and two operator-visible sessions, bind all raw artifacts to `testedCommit`, and only then record evidence and update design maturity.

**Tech Stack:** C++17, nlohmann `ordered_json`, SHA-256, CMake/CTest presets, tagged allocator telemetry, SDL_GPU/Metal offscreen tests, macOS shell tooling, `otool`, deny-network sandbox, real Korean and Japanese input sources.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md)

## Global Constraints

- Start only after [`06-macos-package-policy.md`](06-macos-package-policy.md) exits green, its Tasks 9–17 review has no blocker/high finding, and the reviewed package-policy commit is recorded.
- Preserve unrelated worktree changes. Before each code task run `git status --short --branch`; stage only listed files.
- Canonical JSON includes portable dependency-contract SHA; immutable font GUID/source SHA/face; glyph ID and source byte/grapheme range; signed 26.6 advance/offset/position; logical line and visual run order; caret stop/affinity; UI rect/clip; canonical event target; and draw order.
- Canonical JSON excludes pointers, GPU handles, atlas UV/page, physical raster pixels, timestamp, `UIFrameResult::frameIndex`, native `sequence`, world generation, runtime type ID, component instance ID, process-local content generation, cache/layout generation, import/hot-edit history, and native event history.
- Canonical replay assigns consecutive nonzero `traceOrdinal` values from the
  trace array. The exporter serializes `traceOrdinal`, event kind, optional
  `UIStableComponentKey` (`target:null` when absent), and delivered/skipped
  only; missing or duplicate ordinals are errors.
- The three mode outputs must be byte-for-byte identical, not merely semantically equivalent JSON.
- The copied packaged canonical run itself, not only its preflight validator,
  executes through Task 17.5's exact deny-network profile with its only writable
  root set to the canonical parity output directory; the runner hashes and
  audits that sandbox invocation.
- The development resource root is a complete independently staged tree and
  is never the package's `Contents/Resources` directory or a copy thereof.
  Package, development, and preserved editor-project reports bind their own
  canonical roots/tree hashes while proving identical common inputs.
- Universal gates apply on every machine: workload composition, report schema, warm-static reshape `0`, warm-static tagged allocation `0`, and atlas `peakResidentBytes <= 64 MiB` with no unlimited mode.
- Timing gates apply only when the complete fingerprint equals `Mac14,9`, Apple M2 Pro 10-core, 16 GiB, macOS 26.5.1 (25F80), arm64, Apple clang 21.0.0, AC power, Release, backing scale `2.0`, no debugger.
- Timing workloads are exact: static `1,000 × 10 = 10,000` cached glyphs; dynamic exactly 2,000 graphemes split `400/300/300/300/300/400` Latin/Arabic/Hebrew/Devanagari/Thai/CJK; 120 warm-up frames; 600 samples; nearest-rank p95.
- The default resident atlas budget is universally 64 MiB. Source font bytes and temporary upload staging are reported separately, never counted as resident page bytes.
- Visible qualification requires two distinct sessions: A is the checkout-external copied packaged `.app`; B is the exact Release editor opened only through the dedicated qualification-session CLI against the preserved reported editor project, exact scene/catalog/trace, and verified project font artifacts, with Game View, letterbox/crop, detached ImGui viewport, and editor/runtime IME ownership transfer. Synthetic/headless or arbitrary manually opened projects satisfy neither.
- Qualification evidence must be generated only after the complete harness/code/golden candidate is reviewed and committed. `testedCommit` is that clean reviewed candidate commit; evidence is committed afterward and continues to name the candidate, not the later evidence commit.
- Do not claim signing/notarization, Universal 2/Intel, older macOS, Linux/Windows, color/variable font, or native accessibility support.

---

## Prerequisite Contract

The exporter includes and consumes the authoritative Milestone 4 interfaces;
this subplan must not redeclare a narrower competing dispatch record:

```cpp
#include "UI/UIInputEvent.h"

using molga::ui::UIEventDispatchRecord;
// Task 12.1 defines UIEventDispatchRecord in UI/UIInputEvent.h.
// Authoritative fields include sequence, traceOrdinal, kind,
// optional<UIRuntimeTargetIdentity> runtimeTarget,
// optional<UIStableComponentKey> canonicalTarget, actionMask, consumed,
// delivered, arrangementDirty, visualDirty, and optional resultingFocus.
```

Canonical export projects only the approved stable subset; all other
authoritative fields remain available to the runtime audit. A targetless record
keeps both optional identities empty and serializes canonical `target:null`.
The runtime audit keeps real frame/native sequence values outside canonical
JSON. Evidence root is
`docs/qualification/ui-text-milestone-a/<testedCommit>/`; every JSON, Markdown
report, raw log, screenshot index, and event audit records the same 40-character
`testedCommit` and tested executable SHA-256.

## File Responsibility Map

| File | Responsibility |
|---|---|
| `src/Text/TextCanonicalSnapshot.*` | Stable content/UI/event projection and byte serialization |
| `src/Text/TextAllocatorTelemetry.*` | Text/UI tagged allocation counters and measurement interval |
| `src/Text/TextPerformanceHarness.*` | Exact workload generation, measurement, fingerprint, p95, report |
| `src/Tools/TextPerformanceReportMain.cpp` | Fail-closed durable performance-report CLI |
| `src/Tools/TextQualification.cpp` | Shared scene/trace/mode driver for automated qualification |
| `src/Tools/VisibleTextSessionAudit.*` | Native/source/owner-transition visible evidence recorder |
| `src/Platform/MacTextInputSource.*` and `MacVisibleWindow.*` | TIS input-source and PID-owned CG window authority |
| `src/Editor/TextQualificationEditorSession.*` | Fail-closed visible Session B project/argv/hash authority |
| `tests/smoke/run_text_parity.cmake` | Three-mode external-copy execution and byte/SHA comparison |
| `tests/smoke/text_network_deny.sb` | Existing self-tested Task 17.5 deny-network profile consumed unchanged |
| `scripts/qualification/run_ui_text_macos.sh` | Clean-candidate automated evidence runner |
| `scripts/qualification/record_ui_text_ime.sh` | Two-session operator-visible evidence collector/verifier |
| `docs/qualification/ui-text-milestone-a/README.md` | Evidence schema and claim boundary |

## Exit Contract

Milestone A may be marked implemented/qualified only after every automated suite, distinct-root parity, package audit, a durable canonical performance report, reference timing, atlas/allocation, GPU golden, and both visible sessions pass for the same reviewed candidate commit. Session B must bind the dedicated CLI argv and preserved project/scene/catalog/trace/font-artifact hashes. If reference hardware or either real input-source session is unavailable, record the exact open gate and leave the design `NOT STARTED` or in-progress; do not substitute synthetic evidence.

---

### Task 18.1: Export one content-stable canonical snapshot

**Files:**

- Create: `src/Text/TextCanonicalSnapshot.h`
- Create: `src/Text/TextCanonicalSnapshot.cpp`
- Create: `tests/test_text_canonical.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `TextRuntimeManifest`, concrete `UISnapshot::{nodes,renderItems,hitTargets}`, immutable `TextLayout` payloads, and dispatch records.
- Produces: `TextCanonicalSnapshot::{Build,Serialize,Sha256}` with strict trace-ordinal validation.

- [ ] **Step 1: Register the focused canonical test**

  ```cmake
  molga_add_text_test(test_text_canonical test_text_canonical.cpp)
  ```

  Use Task 1's installed `TextRuntimeTestSession` and sole text-test main. Keep
  the target's existing canonical-test labels and compile definitions; do not
  link `doctest_main` or construct a second runtime inside a fixture.

- [ ] **Step 2: Write the failing history-exclusion test fixture**

  Create `tests/test_text_canonical.cpp`. Define `MakeCanonicalInput(contentGeneration, nativeSequence, frameHistorySalt)` in that file: build a schema-1 `TextRuntimeManifest`; one `UISnapshot` node; one `UITextSnapshot` carrying a one-line `TextLayout` with a glyph whose `FontFaceResource::sourceSha256` is fixed and `contentGeneration` is the parameter; and one dispatch with `traceOrdinal=1`, parameterized native `sequence`, canonical target `{7,"UITextInput",1}`, and a parameterized runtime identity. Then assert:

  ```cpp
  TEST_CASE("canonical bytes exclude runtime frame and edit history") {
      const auto cold = MakeCanonicalInput(1u, 100u, 0u);
      const auto warm = MakeCanonicalInput(99u, 5000u, 88u);
      CHECK(molga::text::TextCanonicalSnapshot::Serialize(cold.Input()) ==
            molga::text::TextCanonicalSnapshot::Serialize(warm.Input()));
      CHECK(molga::text::TextCanonicalSnapshot::Sha256(cold.Input()) ==
            molga::text::TextCanonicalSnapshot::Sha256(warm.Input()));
  }
  ```

  `frameHistorySalt` changes only omitted `layoutRevision`, runtime identities, and test-side audit frame values; it never changes authored/content bytes.

- [ ] **Step 3: Add exact required-field assertions**

  ```cpp
  TEST_CASE("canonical snapshot contains structural text UI and event fields") {
      const auto fixture = MakeCanonicalInput(1u, 100u, 0u);
      const auto json = molga::text::TextCanonicalSnapshot::Build(fixture.Input());
      CHECK(json["dependencyContractSha256"] == fixture.contractSha);
      const auto& glyph = json["renderItems"][0]["text"]["lines"][0]
                              ["visualRuns"][0]["glyphs"][0];
      CHECK(glyph["fontGuid"] == "font-guid");
      CHECK(glyph["sourceSha256"] == fixture.fontSha);
      CHECK(glyph["faceIndex"] == 0u);
      CHECK(glyph["glyphId"] == 37u);
      CHECK(glyph["sourceBytes"] == nlohmann::json::array({0u, 2u}));
      CHECK(glyph["graphemes"] == nlohmann::json::array({0u, 1u}));
      CHECK(glyph["advanceRaw"] == nlohmann::json::array({640, 0}));
      CHECK(glyph["offsetRaw"] == nlohmann::json::array({0, -16}));
      CHECK(glyph["positionRaw"] == nlohmann::json::array({64, 128}));
      CHECK(json["events"][0] == nlohmann::json{
          {"traceOrdinal", 1u}, {"kind", "TextCommit"},
          {"target", {{"sceneObjectId", 7u},
                      {"componentTypeName", "UITextInput"},
                      {"componentSchemaVersion", 1u}}},
          {"delivered", true}});
  }
  ```

- [ ] **Step 4: Add forbidden-field and trace-validation tests**

  ```cpp
  TEST_CASE("canonical export rejects missing and duplicate trace ordinals") {
      auto missing = MakeCanonicalInput(1u, 100u, 0u);
      missing.dispatches[0].traceOrdinal = 0;
      CHECK_THROWS_AS(molga::text::TextCanonicalSnapshot::Build(
          missing.Input()), std::invalid_argument);
      auto duplicate = MakeCanonicalInput(1u, 100u, 0u);
      duplicate.dispatches.push_back(duplicate.dispatches[0]);
      CHECK_THROWS_AS(molga::text::TextCanonicalSnapshot::Build(
          duplicate.Input()), std::invalid_argument);
  }

  TEST_CASE("targetless window focus remains a canonical null target") {
      auto fixture = MakeCanonicalInput(1u, 100u, 0u);
      auto& event = fixture.dispatches[0];
      event.kind = molga::ui::UIInputEventKind::WindowFocus;
      event.runtimeTarget.reset();
      event.canonicalTarget.reset();
      event.delivered = false;
      const auto json = molga::text::TextCanonicalSnapshot::Build(
          fixture.Input());
      CHECK(json["events"][0] == nlohmann::json{
          {"traceOrdinal", 1u}, {"kind", "WindowFocus"},
          {"target", nullptr}, {"delivered", false}});
  }

  TEST_CASE("canonical bytes contain no runtime or raster keys") {
      const auto text = molga::text::TextCanonicalSnapshot::Serialize(
          MakeCanonicalInput(77u, 900u, 4u).Input());
      for (const char* forbidden : {"frameIndex", "sequence", "worldGeneration",
              "componentRuntimeTypeId", "componentInstanceId",
              "contentGeneration", "layoutRevision", "atlas", "uv",
              "gpu", "timestamp", "physicalPixel"}) {
          CAPTURE(forbidden);
          CHECK(text.find(forbidden) == std::string::npos);
      }
  }
  ```

- [ ] **Step 5: Run the red canonical gate**

  Run:

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_text_canonical -j
  ```

  Expected: compile fails because `TextCanonicalSnapshot` does not exist.

- [ ] **Step 6: Define the exact exporter API**

  Add `src/Text/TextCanonicalSnapshot.cpp` exactly once to the existing
  `ENGINE_SOURCES` list before `add_library(molga_core ...)`. `molga_core` is
  its sole production object owner; `test_text_canonical` and every executable
  consume it only by linking `molga_core` and must not repeat it in
  `target_sources`.

  ```cpp
  namespace molga::text {
  struct CanonicalSnapshotInput {
      const TextRuntimeManifest& runtimeManifest;
      const molga::ui::UISnapshot& uiSnapshot;
      const std::vector<molga::ui::UIEventDispatchRecord>& eventDispatches;
  };
  class TextCanonicalSnapshot {
  public:
      static nlohmann::ordered_json Build(const CanonicalSnapshotInput&);
      static std::string Serialize(const CanonicalSnapshotInput&);
      static std::string Sha256(const CanonicalSnapshotInput&);
  };
  }
  ```

- [ ] **Step 7: Serialize manifest and UI order fields only**

  Begin the root in fixed key order: `schemaVersion`, `dependencyContractSha256`, `nodes`, `renderItems`, `hitTargets`, `events`. Project `runtimeManifest.dependencyLockSha256` to `dependencyContractSha256`. Serialize all `Fixed26_6` values as signed raw integers. Sort nothing that already has authoritative snapshot/draw/visual order; verify the input order is monotonic and throw if it is not.

- [ ] **Step 8: Serialize concrete text layout content**

  Walk every `UITextSnapshot` payload and emit lines in logical line order, each line's `visualRuns` in stored visual order, and glyphs in stored run order. Read `fontGuid`, `faceIndex`, glyph/source ranges, advances/offsets from `ShapedGlyph`; read immutable `sourceSha256` through `glyph.faceResource`; derive `fontRevision` only as `<sourceSha256>:<faceIndex>`. Never serialize `contentGeneration` or pointer identity.

- [ ] **Step 9: Serialize caret, UI rect/clip, and draw order**

  Emit caret boundary/affinity/position, `UIStableComponentKey`, fixed rect, optional clip, render payload kind, and each `UIDrawOrderKey` field including sibling path and stable submission index. Exclude runtime identities in nodes/render/hit records even when present.

- [ ] **Step 10: Validate and serialize optional event targets by trace ordinal**

  Require ordinals to be exactly `1..N` in vector order with no
  zero/duplicate/gap. Emit stable event-kind strings, `target` as the stable
  key object when `canonicalTarget` is present or JSON `null` when absent, and
  the `delivered` boolean (`false` is the canonical skipped state). Never emit
  native sequence, runtime target, actions, dirty flags, consumption, or
  resulting focus.

- [ ] **Step 11: Run the green canonical gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_canonical -j
  ctest --test-dir build/debug -R '^test_text_canonical$' --output-on-failure
  ```

  Expected: all canonical field, exclusion, byte identity, and ordinal tests pass.

- [ ] **Step 12: Commit canonical export**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextCanonicalSnapshot.h \
    src/Text/TextCanonicalSnapshot.cpp tests/test_text_canonical.cpp
  git commit -m "test: export canonical UI text snapshots"
  ```

### Task 18.2: Drive identical editor, development, and copied-app parity runs

**Files:**

- Create: `src/Tools/TextQualification.h`
- Create: `src/Tools/TextQualification.cpp`
- Create: `tests/fixtures/text/qualification/build_profile.json`
- Create: `tests/fixtures/text/qualification/scene.json`
- Create: `tests/fixtures/text/qualification/trace.json`
- Create: `tests/smoke/run_text_parity.cmake`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Core/BuildProfile.h`
- Modify: `src/Core/BuildProfile.cpp`
- Modify: `src/Core/SmokeReport.h`
- Modify: `src/Core/SmokeReport.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/Platform/NativeInputEvent.h`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `tests/test_text_canonical.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_text_input_arbiter.cpp`
- Modify: `tests/TextQualificationAssetTree.h`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 18.1 exporter, final package/root contract, scene/catalog loader, immutable input trace mapping, and mode executables.
- Produces: byte-fixed scene/trace/profile resources, a preserved reported
  editor project, an independently staged reported development resource root,
  one separately sealed reported `Qualification.app`,
  `TextQualificationOptions`, checked frozen
  `QualificationNativeSurfaceAuthority`, shared
  `RunTextCanonicalQualification`, three identical JSON files, and parity SHA
  report.

- [ ] **Step 1: Add the failing native trace-descriptor loader test**

  ```cpp
  TEST_CASE("canonical trace loader creates native descriptors with global ordinals") {
      const auto trace = WriteTrace({
          {{"kind", "PointerButton"}, {"active", true}},
          {{"kind", "TextEditing"}, {"text", u8"ㅎ"}},
          {{"kind", "TextCommit"}, {"text", u8"한"}}
      });
      const auto loaded = molga::text::LoadCanonicalInputTrace(trace);
      REQUIRE(loaded.size() == 3u);
      CHECK(loaded[0].traceOrdinal == 1u);
      CHECK(loaded[1].traceOrdinal == 2u);
      CHECK(loaded[2].traceOrdinal == 3u);
      CHECK(loaded[0].kind == NativeInputEventKind::PointerButton);
      CHECK(loaded[1].kind == NativeInputEventKind::TextEditing);
      CHECK(loaded[1].text->utf8 == u8"ㅎ");
  }
  ```

  `LoadCanonicalInputTrace` returns immutable
  `CanonicalNativeTraceDescriptor` values, not `UIInputEvent` and not an
  already-stamped `NativeInputEvent`. The host remains the sole sequence and
  text-owner authority.

- [ ] **Step 2: Add the failing two-batch owner-at-ingest test**

  Add to `tests/test_text_input_arbiter.cpp` using its production host/UI
  fixture loaded with the qualification scene:

  ```cpp
  TEST_CASE("qualification trace acquires owner before text ingest") {
      auto f = QualificationInputFixture::Identity1920x1080Surface();
      const auto descriptors = LoadCanonicalInputTrace(
          QualificationFixturePath("trace.json"));
      REQUIRE(descriptors.size() == 6u);

      REQUIRE(f.TraceDriver().InjectBatch(f.Host(),
          f.FrozenSurfaceAuthority(),
          std::vector<CanonicalNativeTraceDescriptor>(
              descriptors.begin(), descriptors.begin() + 4),
          f.Error()));
      auto nativeA = f.Host().TakeNativeInputBatch();
      CHECK(TraceOrdinals(nativeA.events) ==
            std::vector<std::uint64_t>{1, 2, 3, 4});
      REQUIRE(nativeA.surfaceStarts.size() == 1u);
      const auto& startA = nativeA.surfaceStarts.front();
      CHECK(startA.windowId == f.SurfaceWindowId());
      CHECK_FALSE(startA.nativeWindowFocused);
      CHECK_FALSE(startA.pointerWindowPixelValid);
      const auto frameA =
          f.MapCompleteBatchThroughProductionInput(
              std::move(nativeA), f.FrozenSurfaceAuthority());
      const auto dispatchA = f.UISystem().ProcessFrame(
          f.World(), frameA, f.Host().TextInput(),
          f.TextLayoutService(), f.Diagnostics()).dispatches;
      REQUIRE(f.Host().TextInput().CurrentOwner().has_value());
      CHECK(f.Host().TextInput().CurrentOwner()->kind ==
            TextInputOwnerKind::RuntimeUITextInput);
      CHECK(f.Host().TextInput().CurrentOwner()->runtimeTarget ==
            f.RuntimeIdentityFor(140, "UITextInput"));

      REQUIRE(f.TraceDriver().InjectBatch(f.Host(),
          f.FrozenSurfaceAuthority(),
          std::vector<CanonicalNativeTraceDescriptor>(
              descriptors.begin() + 4, descriptors.end()),
          f.Error()));
      auto nativeB = f.Host().TakeNativeInputBatch();
      CHECK(TraceOrdinals(nativeB.events) ==
            std::vector<std::uint64_t>{5, 6});
      REQUIRE(nativeB.surfaceStarts.size() == 1u);
      const auto& startB = nativeB.surfaceStarts.front();
      CHECK(startB.windowId == f.SurfaceWindowId());
      CHECK(startB.nativeWindowFocused);
      REQUIRE(startB.pointerWindowPixelValid);
      CHECK(startB.pointerWindowPixelX == 960);
      CHECK(startB.pointerWindowPixelY == 540);
      for (const auto& event : nativeB.events) {
          REQUIRE(event.text.has_value());
          CHECK(event.text->ownerAtIngest.kind ==
                TextInputOwnerKind::RuntimeUITextInput);
          CHECK(event.text->ownerAtIngest.runtimeTarget ==
                f.RuntimeIdentityFor(140, "UITextInput"));
      }
      const auto frameB =
          f.MapCompleteBatchThroughProductionInput(
              std::move(nativeB), f.FrozenSurfaceAuthority());
      const auto dispatchB = f.UISystem().ProcessFrame(
          f.World(), frameB, f.Host().TextInput(),
          f.TextLayoutService(), f.Diagnostics()).dispatches;
      const auto combined = Concat(dispatchA, dispatchB);
      CHECK(TraceOrdinals(combined) ==
            std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6});
      CHECK(combined[0].canonicalTarget == std::nullopt);
      CHECK(combined[5].canonicalTarget ==
            f.CanonicalKeyFor(140, "UITextInput"));
  }
  ```

  The two `TakeNativeInputBatch` calls consume two distinct host batches.
  `MapCompleteBatchThroughProductionInput` is a fixture adapter over the real
  runtime/Game View mapping: it consumes the complete take-once batch by move, maps its
  unique surface-start record into `pointerAtBatchStart`,
  `pointerAtBatchStartValid`, and `nativeWindowFocusedAtBatchStart`, and moves
  its event vector into `orderedEvents`. There is no events-only compatibility
  overload, direct `UIInputEvent` construction, or six-event `ProcessFrame`
  call. Both batches call the final five-argument production API with the
  fixture's real `World`, host arbiter, shared layout service, and diagnostic
  sink.

  This fixture registers one identity-mapped 1920x1080 surface with copied host
  input initially `{focused:false,pointerValid:false}`. The canonical raw point
  `[61440,34560]` is therefore window pixel `(960,540)`. The second batch-start
  assertions prove that shared ingestion applied the first batch's focus and
  pointer events to persistent copied host input; they are not seeded from the
  second descriptor vector or a live OS query.

  Add the checked coordinate-authority matrix in the same test target:

  ```cpp
  TEST_CASE("qualification logical pointer round trips through native mapping") {
      const molga::FixedPoint logical{
          molga::Fixed26_6::FromRaw(61440),
          molga::Fixed26_6::FromRaw(34560)};
      for (auto& row : QualificationSurfaceMappingRows({
               "standalone-1x", "letterbox", "embedded-origin",
               "detached-origin"})) {
          CAPTURE(row.name);
          const auto descriptor = PointerMotionDescriptor(1u, logical);
          REQUIRE(row.driver.InjectBatch(row.host, row.authority,
                                        {descriptor}, row.error));
          auto batch = row.host.TakeNativeInputBatch();
          REQUIRE(batch.events.size() == 1u);
          const auto frame = row.MapCompleteBatchThroughProductionInput(
              std::move(batch), row.authority);
          REQUIRE(frame.orderedEvents.size() == 1u);
          CHECK(frame.orderedEvents.front().logicalPoint == logical);
          CHECK(frame.orderedEvents.front().logicalPointValid);
      }
  }

  TEST_CASE("qualification native inverse rejects outside and overflow") {
      for (auto& row : QualificationInvalidSurfaceMappingRows(
               {"outside-half-open-content", "forward-overflow"})) {
          CAPTURE(row.name);
          CHECK_FALSE(row.driver.InjectBatch(
              row.host, row.authority, {row.descriptor}, row.error));
          CHECK(row.host.TakeNativeInputBatch().empty());
          CHECK_FALSE(row.error.empty());
      }
  }
  ```

  Each valid row freezes the same mapping value before injection and supplies
  that exact value to the production native-to-UI mapper. The test never builds
  a `UIInputEvent` directly. The invalid rows prove half-open bounds and every
  rational/double/float/int32 conversion fail before host state or a pending
  batch is mutated.

- [ ] **Step 3: Add the failing qualification CLI options test**

  ```cpp

  TEST_CASE("qualification options require all canonical arguments") {
      const char* argv[] = {"tool", "--text-canonical", "Scenes/ui.json",
          "--viewport", "1920x1080", "--input-trace", "trace.json",
          "--output", "out.json", "--qualification-resource-root",
          "/tmp/qualification-resources"};
      const auto options = molga::text::ParseTextQualificationOptions(11, argv);
      REQUIRE(options.has_value());
      CHECK(options->logicalWidth == 1920);
      CHECK(options->logicalHeight == 1080);
      CHECK(options->resourceRootOverride ==
            std::filesystem::path("/tmp/qualification-resources"));
  }
  ```

- [ ] **Step 4: Add the failing byte-exact package-staging test**

  In `tests/test_game_builder.cpp`, use the generic staging API that this task will add:

  ```cpp
  TEST_CASE("qualification scene and trace stage byte exact once") {
      const auto source = SourceRoot() / "tests/fixtures/text/qualification";
      const auto resources = MakeEmptyResourceRoot();
      const std::vector<molga::QualificationFixtureInput> inputs{
          {source / "scene.json", "Qualification/scene.json"},
          {source / "trace.json", "Qualification/trace.json"}};
      std::string error;
      REQUIRE(molga::StageQualificationFixtures(inputs, resources, error));
      CHECK(molga::Sha256File(source / "scene.json") ==
            molga::Sha256File(resources / "Qualification/scene.json"));
      CHECK(molga::Sha256File(source / "trace.json") ==
            molga::Sha256File(resources / "Qualification/trace.json"));
      CHECK_FALSE(std::filesystem::exists(
          resources / "Qualification/Qualification/scene.json"));
  }
  ```

- [ ] **Step 5: Add the failing complete-profile materialization test**

  ```cpp
  TEST_CASE("qualification profile is complete schema three and startup scene exists") {
      const auto bytes = ReadFixtureBytes(
          "tests/fixtures/text/qualification/build_profile.json");
      BuildProfile profile;
      REQUIRE(profile.Deserialize(nlohmann::json::parse(bytes)));
      CHECK(profile.schemaVersion == 3);
      CHECK(profile.outputPath == "Builds/Qualification");
      CHECK(profile.startupScene == "Scenes/qualification.json");
      CHECK(profile.scenes ==
            std::vector<std::string>{"Scenes/qualification.json"});
      CHECK(profile.showConsole == false);
      CHECK(profile.target == "host");
      CHECK(profile.window.outputScaleMode ==
            molga::GameOutputScaleMode::IntegerFit);
      CHECK(profile.SerializeCanonicalBytes() == bytes);

      QualificationAssetTreeFixture source;
      test_support::TempDirectory output{"qualification-project"};
      const auto project = MaterializeQualificationEditorProject(
          source, output.Path(), profile, QualificationSceneFixturePath(),
          QualificationTraceFixturePath());
      REQUIRE(project.has_value());
      CHECK(ReadBytes(project->root / profile.startupScene) ==
            ReadBytes(QualificationSceneFixturePath()));
      CHECK(ReadBytes(project->root / "Qualification/trace.json") ==
            ReadBytes(QualificationTraceFixturePath()));
      BuildPlan plan;
      std::string error;
      REQUIRE(BuildPlanBuilder::Build(profile, project->root.string(),
                                      profile.target, "", plan, error));
      CHECK(plan.startupSceneId == "Scenes/qualification.json");
  }
  ```

- [ ] **Step 6: Add the failing distinct-development-root test**

  ```cpp
  TEST_CASE("qualification development resources are independently staged") {
      const auto outputs = BuildQualificationOutputs();
      REQUIRE(outputs.success);
      CHECK(outputs.developmentResources != outputs.packageResources);
      CHECK_FALSE(IsPathPrefix(outputs.packageResources,
                               outputs.developmentResources));
      CHECK_FALSE(IsPathPrefix(outputs.developmentResources,
                               outputs.packageResources));
      CHECK(outputs.developmentReport.root ==
            Canonical(outputs.developmentResources));
      CHECK(outputs.developmentReport.treeSha256 ==
            HashCanonicalResourceTree(outputs.developmentResources));
      CHECK(outputs.packageReport.resourceTreeSha256 ==
            HashCanonicalResourceTree(outputs.packageResources));
      CHECK(outputs.developmentReport.files ==
            outputs.packageReport.resourceFiles);
      CHECK(outputs.developmentReport.inputSetSha256 ==
            outputs.packageReport.inputSetSha256);
  }
  ```

  `files` is the sorted complete `{relativePath,sha256}` set, including
  `game.json`, catalog, scenes, qualification scene/trace, scripts, shaders,
  placeholder, ICU/portable contract, manifests, packaged font artifacts,
  licenses, and notice. Equality is computed after staging each root
  independently from one immutable `QualificationBuildInputs`; neither root is
  copied from the other.

- [ ] **Step 7: Run the red qualification-driver gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_canonical \
    test_text_input_arbiter test_game_builder test_build_profile -j
  ```

  Expected: compilation fails on native trace descriptors, host injection,
  qualification option/trace functions, project/output materializers, and
  `StageQualificationFixtures`; no qualification scene/trace/profile files or
  distinct development report exist yet.

- [ ] **Step 8: Rescan the complete committed qualification asset authority**

  Reuse `tests/TextQualificationAssetTree.h` and the exact eleven production
  source/sidecar pairs committed by Task 4.2 unchanged. Add a
  `QualificationSourcePaths()` view containing only the three production
  families, six fonts, and two licenses below; the resolver-only
  `cycle-a.fontfamily`/`cycle-b.fontfamily` fixtures remain available to their
  focused tests but are never copied into the qualification project. Do not
  create or rewrite a `.meta` in this task. In
  `tests/test_text_canonical.cpp`, materialize only that view, rescan its
  `Assets` root, and assert the exact authoritative table:

  ```cpp
  const std::array expected{
      AssetExpectation{"families/primary.fontfamily",
          "11111111111111111111111111111111", "FontFamilyImporter", 1},
      AssetExpectation{"families/arabic.fontfamily",
          "22222222222222222222222222222222", "FontFamilyImporter", 1},
      AssetExpectation{"families/cjk.fontfamily",
          "33333333333333333333333333333333", "FontFamilyImporter", 1},
      AssetExpectation{"fonts/NotoSans-Regular.ttf",
          "44444444444444444444444444444444", "FontImporter", 2},
      AssetExpectation{"fonts/NotoSansHebrew-Regular.ttf",
          "55555555555555555555555555555555", "FontImporter", 2},
      AssetExpectation{"fonts/NotoSansArabic-Regular.ttf",
          "66666666666666666666666666666666", "FontImporter", 2},
      AssetExpectation{"fonts/NotoSansDevanagari-Regular.ttf",
          "12121212121212121212121212121212", "FontImporter", 2},
      AssetExpectation{"fonts/NotoSansThai-Regular.ttf",
          "13131313131313131313131313131313", "FontImporter", 2},
      AssetExpectation{"fonts/NotoSansKR-Regular.otf",
          "77777777777777777777777777777777", "FontImporter", 2},
      AssetExpectation{"licenses/NotoFonts-ffebf8c1-OFL.txt",
          "88888888888888888888888888888888", "GenericImporter", 1},
      AssetExpectation{"licenses/NotoCJK-Sans2.004-OFL.txt",
          "99999999999999999999999999999999", "GenericImporter", 1},
  };
  CheckRescannedAssetRecords(
      tree.ProjectRoot(), tree.AssetsRoot(), expected);
  ```

  Parse `primary.fontfamily` and require exact faces in authored order
  `{44444444444444444444444444444444,
  55555555555555555555555555555555,
  12121212121212121212121212121212,
  13131313131313131313131313131313}`, exact fallbacks
  `{22222222222222222222222222222222,
  33333333333333333333333333333333}`, key
  `schemaVersion:1`, and no `schema` or source `guid`. Resolve the fallbacks and
  require Arabic `66666666666666666666666666666666` and CJK
  `77777777777777777777777777777777`. Assert `FontFamilyAsset.guid ==
  AssetRecord.guid == 11111111111111111111111111111111`; all GUID authority comes
  from the committed sidecars.

- [ ] **Step 9: Create the exact canonical scene fixture**

  Preserve the pinned faces and authored fallback arrays in the existing
  `primary.fontfamily`. Create `tests/fixtures/text/qualification/scene.json`
  with exactly these UTF-8 JSON bytes plus one terminal LF:

  ```json
  {
    "version": "1.0",
    "name": "ui-text-qualification",
    "gameObjects": [
      {
        "name": "Canvas",
        "id": 100,
        "active": true,
        "parentId": -1,
        "components": [
          {"type":"UICanvas","enabled":true,"schemaVersion":2,"scaleMode":"ScaleWithViewport","referenceResolution":[1920.0,1080.0],"matchWidthOrHeight":0.5,"sortingOrder":0},
          {"type":"RectTransform","enabled":true,"anchorMin":[0.0,0.0],"anchorMax":[1.0,1.0],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[0.0,0.0]}
        ]
      },
      {
        "name": "OuterMask",
        "id": 110,
        "active": true,
        "parentId": 100,
        "components": [
          {"type":"RectTransform","enabled":true,"anchorMin":[0.1,0.1],"anchorMax":[0.9,0.9],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[0.0,0.0]},
          {"type":"UIMask","enabled":true,"schemaVersion":1,"clipDescendants":true},
          {"type":"UIScrollView","enabled":true,"schemaVersion":1,"viewport":{"targetId":110},"content":{"targetId":120},"horizontal":false,"vertical":true,"movement":"Clamped","elasticity":0.0,"inertia":false,"decelerationRate":0.0,"scrollSensitivity":1.0,"initialNormalizedPosition":{"x":0.0,"y":1.0}}
        ]
      },
      {
        "name": "Content",
        "id": 120,
        "active": true,
        "parentId": 110,
        "components": [
          {"type":"RectTransform","enabled":true,"anchorMin":[0.0,0.0],"anchorMax":[1.0,1.0],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[0.0,0.0]}
        ]
      },
      {
        "name": "MixedLabel",
        "id": 130,
        "active": true,
        "parentId": 120,
        "components": [
          {"type":"RectTransform","enabled":true,"anchorMin":[0.5,0.72],"anchorMax":[0.5,0.72],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[1280.0,180.0]},
          {"type":"UILabel","enabled":true,"schemaVersion":2,"text":"Latin سلام שלום क्षि กำลัง 한글 日本語","fontFamilyGuid":"11111111111111111111111111111111","fontSizeRaw":2048,"lineSpacingRaw":64,"color":[1.0,1.0,1.0,1.0],"locale":"und","baseDirection":"Auto","wrap":"Word","overflow":"Clip","maxLines":2,"horizontalAlignment":"Center","verticalAlignment":"Middle","sortingOrder":10}
        ]
      },
      {
        "name": "InputViewport",
        "id": 140,
        "active": true,
        "parentId": 120,
        "components": [
          {"type":"RectTransform","enabled":true,"anchorMin":[0.5,0.5],"anchorMax":[0.5,0.5],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[720.0,96.0]},
          {"type":"UIMask","enabled":true,"schemaVersion":1,"clipDescendants":true},
          {"type":"UISelectable","enabled":true,"schemaVersion":1,"interactable":true,"navigationMode":"None","selectOnUp":{"targetId":0},"selectOnDown":{"targetId":0},"selectOnLeft":{"targetId":0},"selectOnRight":{"targetId":0}},
          {"type":"UITextInput","enabled":true,"schemaVersion":1,"initialText":"AB","readOnly":false,"multiline":false,"maxGraphemes":32,"contentPolicy":"Any","submitPolicy":"OnEnter","textViewport":{"targetId":140},"renderedLabel":{"targetId":141},"placeholderLabel":{"targetId":142},"fontFamilyGuid":"11111111111111111111111111111111","paragraphStyle":{"weight":400,"stretchPercent":100,"slant":"Upright","fontSizeRaw":1792,"language":"ko","features":[],"variation":"default","direction":"Auto","wrap":"NoWrap","overflow":"Clip","maxLines":1,"lineSpacingRaw":64,"horizontalAlignment":"Left","verticalAlignment":"Middle","ellipsisUtf8":"…"}}
        ]
      },
      {
        "name": "RenderedInput",
        "id": 141,
        "active": true,
        "parentId": 140,
        "components": [
          {"type":"RectTransform","enabled":true,"anchorMin":[0.0,0.0],"anchorMax":[1.0,1.0],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[-32.0,-16.0]},
          {"type":"UILabel","enabled":true,"schemaVersion":2,"text":"","fontFamilyGuid":"11111111111111111111111111111111","fontSizeRaw":1792,"lineSpacingRaw":64,"color":[1.0,1.0,1.0,1.0],"locale":"ko","baseDirection":"Auto","wrap":"NoWrap","overflow":"Clip","maxLines":1,"horizontalAlignment":"Left","verticalAlignment":"Middle","sortingOrder":21}
        ]
      },
      {
        "name": "Placeholder",
        "id": 142,
        "active": true,
        "parentId": 140,
        "components": [
          {"type":"RectTransform","enabled":true,"anchorMin":[0.0,0.0],"anchorMax":[1.0,1.0],"pivot":[0.5,0.5],"anchoredPosition":[0.0,0.0],"sizeDelta":[-32.0,-16.0]},
          {"type":"UILabel","enabled":true,"schemaVersion":2,"text":"입력","fontFamilyGuid":"11111111111111111111111111111111","fontSizeRaw":1792,"lineSpacingRaw":64,"color":[0.5,0.5,0.5,1.0],"locale":"ko","baseDirection":"Auto","wrap":"NoWrap","overflow":"Clip","maxLines":1,"horizontalAlignment":"Left","verticalAlignment":"Middle","sortingOrder":20}
        ]
      }
    ]
  }
  ```

- [ ] **Step 10: Verify the scene fixture through production serializers**

  Freeze these bytes only after constructing the same graph through production
  component setters and `SceneSerializer::SerializeScene`. Add a test that
  serializes `BuildQualificationSceneFixture()` and compares its canonical
  `dump(2)+"\n"` byte-for-byte with the committed file. For object `140`, also
  require the exact `UITextInput` key set from Task 9.2:

  ```cpp
  CHECK(JsonObjectKeys(FindComponent(scene, 140, "UITextInput")) ==
        std::set<std::string>{"schemaVersion", "initialText", "readOnly",
          "multiline", "maxGraphemes", "contentPolicy", "submitPolicy",
          "textViewport", "renderedLabel", "placeholderLabel",
          "fontFamilyGuid", "paragraphStyle"});
  CHECK(FindComponent(scene, 140, "UISelectable")["interactable"] == true);
  ```

  Then deserialize and reserialize once and require identical canonical bytes.
  A fixture containing `maxLength`, `viewport` inside `UITextInput`, or flat
  paragraph fields is rejected rather than migrated during qualification.

- [ ] **Step 11: Create the exact canonical trace fixture**

  Create `tests/fixtures/text/qualification/trace.json` with exactly these UTF-8 JSON bytes plus one terminal LF:

  ```json
  [
    {"kind":"WindowFocus","active":true},
    {"kind":"PointerMotion","logicalPointRaw":[61440,34560]},
    {"kind":"PointerButton","logicalPointRaw":[61440,34560],"control":1,"active":true},
    {"kind":"PointerButton","logicalPointRaw":[61440,34560],"control":1,"active":false},
    {"kind":"TextEditing","text":{"utf8":"ㅎ","editingStartUtf8Characters":0,"editingLengthUtf8Characters":1}},
    {"kind":"TextCommit","text":{"utf8":"한","editingStartUtf8Characters":0,"editingLengthUtf8Characters":0}}
  ]
  ```

  The file contains no `sequence`, `traceOrdinal`, frame, owner generation,
  window generation, or target identity. The loader assigns the array index
  plus one to the immutable native descriptor; EngineHost supplies sequence and
  ingest-time owner, while the descriptor ordinal is transported unchanged
  through both batches to canonical dispatch.

- [ ] **Step 12: Define the shared driver API**

  ```cpp
  namespace molga::text {
  enum class QualificationMode { EditorGameView, DevelopmentRuntime,
                                 PackagedRuntime };
  struct TextQualificationOptions {
      std::filesystem::path scene;
      std::filesystem::path inputTrace;
      std::filesystem::path output;
      std::optional<std::filesystem::path> resourceRootOverride;
      std::uint32_t logicalWidth = 0;
      std::uint32_t logicalHeight = 0;
  };
  struct CanonicalNativeTextDescriptor {
      std::string utf8;
      std::int32_t editingStartUtf8Characters = 0;
      std::int32_t editingLengthUtf8Characters = 0;
  };
  struct CanonicalNativeTraceDescriptor {
      std::uint64_t traceOrdinal = 0;
      molga::platform::NativeInputEventKind kind;
      molga::FixedPoint logicalPoint;
      std::uint32_t control = 0;
      bool active = false;
      std::optional<CanonicalNativeTextDescriptor> text;
  };
  struct QualificationNativeSurfaceAuthority {
      molga::WindowId windowId = 0;
      molga::ui::UISurfaceCoordinateMapping frozenMapping;
      double windowPixelsPerNativePointX = 1.0;
      double windowPixelsPerNativePointY = 1.0;
  };
  std::optional<TextQualificationOptions> ParseTextQualificationOptions(
      int argc, const char* const* argv, std::string* errorOut = nullptr);
  std::vector<CanonicalNativeTraceDescriptor> LoadCanonicalInputTrace(
      const std::filesystem::path&);
  enum class QualificationTextContextAuthority : std::uint8_t {
      ProjectLibrary,
      ValidatedSealedPackage,
  };
  struct QualificationTextContext {
      QualificationTextContextAuthority authority =
          QualificationTextContextAuthority::ProjectLibrary;
      // Project root for ProjectLibrary; Resources root for sealed authority.
      std::filesystem::path canonicalStorageRoot;
      std::shared_ptr<const FontArtifactStore> artifactStore;
      std::shared_ptr<molga::AssetDatabase> assets;
      std::shared_ptr<FontRepository> fonts;
      std::shared_ptr<FontFamilyResolver> families;
      std::string primaryFamilyGuid;
      std::shared_ptr<const SealedPackageValidationResult> sealedValidation;

      QualificationTextContext() = default;
      QualificationTextContext(const QualificationTextContext&) = delete;
      QualificationTextContext& operator=(
          const QualificationTextContext&) = delete;
      QualificationTextContext(QualificationTextContext&&) noexcept = default;
      QualificationTextContext& operator=(
          QualificationTextContext&&) noexcept = default;
  };
  QualificationTextContext LoadProjectQualificationTextContext(
      const std::filesystem::path& projectRoot,
      const std::filesystem::path& assetsRoot,
      std::string primaryFamilyGuid,
      TextDiagnosticSink&);
  std::optional<QualificationTextContext>
  BindSealedQualificationTextContext(
      const std::filesystem::path& canonicalResourceRoot,
      std::shared_ptr<const SealedPackageValidationResult> exactValidation,
      std::shared_ptr<const FontArtifactStore> preboundStore,
      std::shared_ptr<AssetDatabase> preboundAssets,
      std::shared_ptr<FontRepository> preboundFonts,
      std::shared_ptr<FontFamilyResolver> preboundFamilies,
      std::string primaryFamilyGuid,
      TextDiagnosticSink&);
  class TextQualificationNativeTraceDriver final {
  public:
      bool InjectBatch(EngineHost&,
          const QualificationNativeSurfaceAuthority&,
          const std::vector<CanonicalNativeTraceDescriptor>&,
          std::string& errorOut);
  private:
      std::unordered_set<std::uint64_t> seenTraceOrdinals_;
  };
  int RunTextCanonicalQualification(QualificationMode,
      const TextQualificationOptions&, QualificationTextContext,
      PathService&, SmokeReport&, TextDiagnosticSink&);
  }
  ```

  Add `src/Tools/TextQualification.cpp` exactly once to `ENGINE_SOURCES`.
  `molga_core` is the only production object owner; `molga_engine`,
  `molga_runtime`, `molga_runtime_dev`, the package executable, and tests link
  that owner and never compile this source again.

  `QualificationTextContext` is deliberately move-only. Add C++17 assertions
  that it is not copy constructible/assignable and is move constructible/
  assignable. `LoadProjectQualificationTextContext` sets `ProjectLibrary`, the
  canonical project root, and a null `sealedValidation`; it exists only for the
  performance workload and project-authority tests. The sealed binder requires
  a successful nonnull exact validation object, canonical non-symlink resource
  root, nonnull store/database/repository/resolver, the database's identical
  bound-store identity, and its already loaded `SealedPackage` catalog rooted
  at that same resource root. It sets `ValidatedSealedPackage` and retains the
  exact shared services and validation object; it never validates, opens, or
  rebinds anything.

  The canonical driver consumes the context by value and accepts only
  `ValidatedSealedPackage`. Before scene, SDL, or output work it requires the
  context storage root to equal `PathService::RuntimeResourceRoot()`, the same
  bound store/catalog identities to remain live, and the retained validation
  result to be the successful authority for that root. A project context,
  null/failed validation, wrong root, expired/mismatched service, or rebound
  catalog returns `4`. The driver may not construct a context, consult a
  singleton, reopen a catalog, or repeat package validation.

  Add a lifetime test that binds a sealed context, records weak probes and all
  object addresses, releases every caller-side strong reference, and invokes
  the driver with `std::move(context)`. The same service/validation identities
  must remain alive through canonical serialization and through
  `EngineShutdown(..., sameSink) == Complete`; after the consumed context and
  startup lifetime owner are destroyed, every weak probe expires. Add failure
  rows for copy attempts (compile-time assertions), project authority, wrong
  root, null validation, failed validation, and store/catalog identity mismatch;
  each stops before host/output mutation.

- [ ] **Step 13: Parse immutable native descriptors and add ordinal transport**

  Require an array with known native event-kind strings and complete required
  fields. Set descriptor `traceOrdinal = index + 1`; descriptors contain no
  native sequence, owner stamp, window generation, or target. Reject explicit
  `traceOrdinal`, `sequence`, owner/window generation, unknown behavior keys,
  non-finite coordinates, and invalid UTF-8 text with a typed failure.

  Extend the authoritative `NativeInputEvent` with
  `std::uint64_t traceOrdinal = 0`; normal SDL polling leaves it zero.
  Keep the shared ingestion entry private and always compiled in
  `Bootstrap.cpp`:

  ```cpp
  namespace molga::text {
  class TextQualificationNativeTraceDriver;
  }
  bool EngineHost::IngestQualificationNativeBatchForFriend(
      const std::vector<molga::platform::NativeInputEvent>&,
      std::string& errorOut);
  friend class molga::text::TextQualificationNativeTraceDriver;
  ```

  The public qualification driver is implemented in
  `TextQualification.cpp` and is the sole friend caller; neither test-target
  nor executable macros change the `molga_core` class definition. Its
  `InjectBatch` first requires exactly one registered qualification surface,
  requires `authority.windowId` to equal that stable nonzero ID, and requires
  finite positive `windowPixelsPerNativePoint{X,Y}` equal to the registered
  surface values. Zero/multiple surfaces or a mismatched authority fail before
  mutation. The runner freezes `authority.frozenMapping` once for the pending
  surface frame and passes the same value, without recomputation, both here and
  to the production native-to-UI frame mapper. The driver copies the authority
  window ID into every converted event and calls the exact shared
  internal ingest routine used after SDL decoding. That routine allocates the
  real nonzero native sequence and stamps `NativeTextEvent.ownerAtIngest` from
  the host arbiter at ingest. It copies the descriptor ordinal one-for-one into
  `NativeInputEvent`; the production native-to-UI mapper copies it again into
  `UIInputEvent`, and `UISystem` copies it into `UIEventDispatchRecord`.

  For each pointer descriptor, perform the checked inverse of the production
  mapping: map its Canvas `FixedPoint` through the frozen
  `UIPhysicalTransform` rational scale into output pixels, apply
  `outputOriginWindowPixel{X,Y}` and
  `windowPixelsPerOutputPixel{X,Y}`, then divide exactly once by
  `windowPixelsPerNativePoint{X,Y}` to obtain the float `NativeInputEvent::x/y`
  consumed by the ordinary mapper. Reject a point outside the half-open logical
  viewport, invalid/zero extent or scale, non-finite intermediate, checked
  integer/rational overflow, or float overflow before host mutation. Finally,
  multiply the candidate native point by the same pixels-per-native-point and
  call `frozenMapping.WindowPixelToCanvas`; require the result to equal the
  original 26.6 point exactly. A non-exact float round trip is a typed failure,
  `LayoutInvalid`, not a tolerance or clamp. The driver never constructs `UIInputEvent`, calls
  `UISystem` directly, treats Canvas coordinates as SDL/window coordinates, or
  substitutes an identity mapping outside the explicit identity test fixture.

  Before stamping any injected event, the host rejects an existing pending
  batch and snapshots every registered surface, in stable window-ID order,
  from its copied `Input` state into `NativeInputBatch::surfaceStarts` through
  the same internal helper used by `PollEvents`. It never calls an OS focus or
  pointer query in this path. As each injected WindowFocus/pointer record is
  accepted, the shared native-ingest routine applies it to that copied host
  state before appending the event. Consequently the first qualification batch
  publishes the registered deterministic unfocused/pointer-invalid seed, while
  the second batch snapshots the focused state and last `(960,540)` pointer
  left by the first batch. Do not derive a seed from the current descriptor
  vector, its first event, or its vector index.

  Preflight the complete descriptor vector, ordinal candidate set, surface
  authority, and every checked coordinate conversion into temporaries before
  touching the host or `seenTraceOrdinals_`. The host then snapshots starts and
  applies the converted events to a copied-input candidate while it stamps a
  temporary event vector. A sequence-allocation failure may burn already
  reserved global sequence values, but publishes neither copied-input state nor
  a pending batch. Only one successful atomic pending-batch publication updates
  copied host state and commits the driver's ordinal set.

  Before that publication, reject zero, duplicate, decreasing, or previously
  seen qualification ordinals against one driver-owned collision set. After
  each `TakeNativeInputBatch`, require the exact expected global ordinals in
  its `events` vector; consume its matching surface-start seed through the
  production frame mapper and
  not recalculate from that batch's local vector index.

- [ ] **Step 14: Resolve qualification inputs beneath the active resource root**

  `RunTextCanonicalQualification` receives and owns the explicit move-only
  `QualificationTextContext`; it never discovers one. Canonicalize the mode's
  resource root and both requested paths. Packaged mode accepts only
  resource-relative scene/trace paths and rejects escape; editor/development
  mode accepts the absolute paths supplied by the parity runner only when both
  are canonical descendants of the initialized `RuntimeResourceRoot`. Consume
  the already loaded asset catalog and dependency manifest through the passed
  context; do not open either file again.

  Never infer a project root by removing an `Assets` suffix. The project helper
  canonicalizes and verifies both explicit roots, requires
  `assetsRoot == projectRoot / "Assets"`, creates
  `FontArtifactStore::ForProject(projectRoot)`, binds that shared store before
  `ScanProject(assetsRoot)`, and retains the store for the context lifetime.
  `CheckRescannedAssetRecords` takes the same explicit `(projectRoot,
  assetsRoot, expected)` pair and performs that bind before scanning.

  Packaged, automated-editor, and development entry points do not call the
  project helper. Packaged mode passes the already startup-validated exact
  shared result, canonical resource root, sealed store, `AssetDatabase`,
  `FontRepository`, and resolver to
  `BindSealedQualificationTextContext`; it neither revalidates nor rebinds
  them. The returned context is the only context passed to the driver.

  The automated editor and development modes consume the independently staged
  development root whose complete file map is intentionally byte-identical to
  package `Contents/Resources`; its schema-3 catalog therefore contains
  `PackagedResource`, not `ProjectLibrary`, font locators. Before either mode
  initializes text dependencies, binds an `AssetDatabase`, or opens a catalog,
  run the exact production
  `TextPackageValidator::ValidateSealedPackage(resourceRoot)` on that root.
  Require success, consume that result's sorted `packagedFontAuthorities`,
  create `FontArtifactStore::ForSealedPackage(resourceRoot, authorities,
  sink)`, bind it once, and call
  `LoadCatalog(..., AssetCatalogMode::SealedPackage, ...)`.

  The editor branch performs this sequence in its dedicated pre-engine helper,
  retains the one successful validation value in a shared const object, binds
  a sealed context, and moves that context to the driver.
  The development branch sets
  `RuntimeTextResourceAuthority::QualificationSealedLayout`; the Task 17.5
  runner performs validation before guard creation and its production
  acquisition callback retains and binds the exact shared result before
  `runEngine(sink)`. That callback locks the weak authority, constructs the
  explicit sealed context from the retained object identities, and moves it to
  the driver with the runner's same sink. The shared qualification driver only
  accepts those already validated/bound services and does not validate or
  rebind them a second time.
  This is a sealed-layout authority check for the dedicated noninteractive
  qualification branch only: it does not call
  `InitializePackagedRuntimeRoot`, does not change `QualificationMode` or the
  development terminal-action policy, and is unavailable to ordinary editor
  or development startup. A validator/store/bind/catalog/context failure
  returns `4` before scene or SDL startup. Reject null, previously bound, or
  differently bound services instead of guessing authority from a path.

- [ ] **Step 15: Load the scene and fixed viewport through production services**

  Load the exact scene, resolve the primary family GUID through the context's
  already loaded catalog/services, initialize normal text/UI services, and
  create logical viewport `1920x1080`. Treat any typed load, family, coverage,
  or layout diagnostic at blocker severity as status `4`; do not reopen the
  catalog, substitute a system font, or generate a scene.

- [ ] **Step 16: Ingest and dispatch the pointer/focus batch first**

  Inject descriptors `1..4` through
  `TextQualificationNativeTraceDriver::InjectBatch`, take that host batch exactly
  once as a complete `NativeInputBatch`, map both its matching surface-start
  seed and events through the production native-to-UI path, and call
  `UISystem::ProcessFrame` for that batch. Require WindowFocus ordinal `1` to
  have no canonical target, pointer ordinals `2..4` to hit the focus shell on
  object `140`, and pointer-down dispatch to acquire the exact
  `RuntimeUITextInput` owner identity before continuing. Keep native sequence
  only in the runtime audit. Before dispatch, require the sole batch-start seed
  to be the registered qualification surface with native focus false and an
  invalid pointer; no host/OS resnapshot occurs after event injection.

- [ ] **Step 17: Ingest and dispatch the text batch after owner acquisition**

  Only after Step 16's dispatch and arbiter owner transition, inject text
  descriptors `5..6`. The shared host ingest stamps both copied text payloads
  with the already-acquired runtime owner; take/map/process this second complete
  batch once, including its matching batch-start surface seed. Require exact
  dispatch ordinals `5,6`, canonical target
  `{140,"UITextInput",1}`, and `delivered=true`. Concatenate the two dispatch
  result vectors without sorting and verify global ordinals `1..6`. There is no
  direct `UIInputEvent` construction, local re-numbering, six-event single
  frame, or extra drain loop. Require this second seed to carry native focus
  true and the prior batch's last valid pointer `(960,540)` on the pinned
  identity fixture. It comes from persistent copied host input before text
  stamping, never from text descriptors or an OS query. Non-identity editor,
  letterboxed, embedded, or detached mappings retain their checked physical
  window point instead; mapping that seed through the same frozen authority
  must recover the identical Canvas point `[61440,34560]`.

- [ ] **Step 18: Serialize and atomically publish the canonical output**

  Call `TextCanonicalSnapshot::Serialize` before GPU encoding, write `output.tmp`, fsync/close, and rename to the requested output. Record mode, output SHA, dependency-contract SHA, and real frame/native sequence only in SmokeReport, never canonical JSON.

- [ ] **Step 19: Add the editor CLI branch**

  In `src/main.cpp`, recognize the complete option set before interactive
  editor startup, require `resourceRootOverride`, publish it through
  `SetDevelopmentRuntimeResourceRoot`, validate Step 14's sealed-layout bytes,
  create the terminal text-runtime guard with the sealed-layout dependency
  policy, retain the exact shared validation result, establish the returned
  resource/catalog/repository authority, bind one move-only sealed context, and
  only then initialize normal text/UI services. Use
  `QualificationMode::EditorGameView`, move the context into the shared driver,
  and pass the same owning diagnostic sink to the driver, `EngineInit`, and
  every `EngineShutdown` retry. Write output only on success. Release known
  external owners, require `EngineShutdown::Complete`, then destroy the context
  and services before the guard and return the shared driver's saved status.
  An engine-body exception follows the same completed-shutdown path. It must
  not open an interactive editor window for this automated mode.

  Parse this branch before ordinary editor project/catalog binding. Perform
  Step 14's direct sealed-layout validation before the text runtime guard is
  created, then bind exactly that authority while the guard is live; never bind
  a project store first and never reuse the editor executable's flat resource
  catalog.

- [ ] **Step 20: Add the development CLI branch**

  In `runtime_main.cpp` compiled as `molga_runtime_dev`, require the explicit canonical `--development-resource-root`, initialize it through `SetDevelopmentRuntimeResourceRoot`, choose `DevelopmentRuntime`, and call the shared driver. It never calls sealed-package initialization.

  Parse this branch before the ordinary development catalog is bound and set
  `RuntimeStartupRequest::textResourceAuthority` to
  `QualificationSealedLayout`. `RunRuntimeStartup` performs Step 14's fresh
  direct validation of the independent root before guard creation and passes
  that exact nonnull shared result to the production acquisition callback,
  which retains it and binds only the resulting sealed-layout store/catalog/
  repository authority. The guarded `runEngine(sink)` locks the same weak
  authority, binds an explicit context from those identical retained objects,
  and moves it to the shared driver; it does not revalidate, reopen, or rebind.
  The driver owns that context until same-host shutdown is `Complete`. A normal
  development launch uses `DevelopmentProject` plus an empty validation
  authority and cannot select this path implicitly.

- [ ] **Step 21: Add the packaged CLI branch**

  In the bundle target, derive `Contents/Resources`, retain the exact startup
  sealed-package validation and already bound services in one sealed context,
  accept only resource-relative qualification paths, choose `PackagedRuntime`,
  move that context into the same driver with the same runner-owned diagnostic
  sink, and require completed same-host shutdown before return. It never
  accepts `--development-resource-root`, constructs a project context, or
  reopens package authority.

- [ ] **Step 22: Define byte-exact qualification staging**

  Add:

  ```cpp
  struct QualificationFixtureInput {
      std::filesystem::path source;
      std::filesystem::path packageRelativePath;
  };
  bool StageQualificationFixtures(
      const std::vector<QualificationFixtureInput>&,
      const std::filesystem::path& resourceRoot,
      std::string& errorOut);
  ```

  Validate every source as a regular non-symlink file; accept only normalized destinations beneath `Qualification/`; reject duplicate/colliding destinations before copying. Copy bytes once, rehash source/destination, and fail GameBuilder on mismatch. The qualification build settings pass exactly `scene.json -> Qualification/scene.json` and `trace.json -> Qualification/trace.json`; both become package resources without parse/reserialize.

- [ ] **Step 23: Define the production qualification BuildProfile and asset root**

  Add one canonical production serializer to `BuildProfile`; project settings,
  the fixture generator, and the parity assertion all use it instead of a
  test-only JSON writer:

  ```cpp
  nlohmann::ordered_json BuildProfile::SerializeCanonical() const;
  std::string BuildProfile::SerializeCanonicalBytes() const {
      return SerializeCanonical().dump(2) + "\n";
  }
  ```

  Declare both methods in `BuildProfile.h`. Make existing `Serialize()`
  delegate to `SerializeCanonical()` while retaining its public
  `nlohmann::json` return type. Emit every schema-3 field exactly once in the
  order shown below. Add a focused assertion that a deserialize/reserialize is
  byte-identical and that deleting or adding any top-level key breaks fixture
  parity.

  Create `tests/fixtures/text/qualification/build_profile.json` by serializing
  BuildProfile schema 3 with these exact authored values:

  ```json
  {
    "schemaVersion": 3,
    "gameName": "Qualification",
    "productVersion": "1.0.0",
    "companyName": "Molga",
    "outputPath": "Builds/Qualification",
    "startupScene": "Scenes/qualification.json",
    "scenes": ["Scenes/qualification.json"],
    "window": {"width":1920,"height":1080,"fullscreen":false,"resizable":false,"outputScaleMode":"IntegerFit"},
    "developmentBuild": false,
    "showConsole": false,
    "target": "host",
    "bundleIdentifier": "com.molga.ui-text-qualification",
    "bundleVersion": "1",
    "minimumMacOSVersion": "13.0",
    "requiredTextFixtures": [
      {"stableName":"qualification-mixed","utf8":"Latin سلام שלום क्षि กำลัง 한글 日本語","locale":"und","style":{"fontFamilyGuid":"11111111111111111111111111111111","weight":400,"stretchPercent":100,"slant":"Upright","fontSizeRaw":2048,"language":"und","features":[{"tag":"kern","value":1},{"tag":"liga","value":1}],"variation":"default","direction":"Auto","wrap":"Word","overflow":"Clip","maxLines":2,"lineSpacingRaw":64,"horizontalAlignment":"Center","verticalAlignment":"Middle","ellipsisUtf8":"…","widthRaw":81920,"heightRaw":11520}},
      {"stableName":"qualification-ime","utf8":"입력 한","locale":"ko","style":{"fontFamilyGuid":"11111111111111111111111111111111","weight":400,"stretchPercent":100,"slant":"Upright","fontSizeRaw":1792,"language":"ko","features":[{"tag":"kern","value":1},{"tag":"liga","value":1}],"variation":"default","direction":"Auto","wrap":"NoWrap","overflow":"Clip","maxLines":1,"lineSpacingRaw":64,"horizontalAlignment":"Left","verticalAlignment":"Middle","ellipsisUtf8":"…","widthRaw":46080,"heightRaw":6144}}
    ]
  }
  ```

  Generate the committed canonical bytes through the production serializer
  (`ordered_json::dump(2) + "\n"`) in that exact field order; deserialize then
  reserialize them identically. The expected key set is exactly
  `{schemaVersion,gameName,productVersion,companyName,outputPath,startupScene,
  scenes,window,developmentBuild,showConsole,target,bundleIdentifier,
  bundleVersion,minimumMacOSVersion,requiredTextFixtures}`. No production field
  or required style field may be omitted or defaulted only by the reader, and
  `Letterbox` is rejected because the supported authored mode is `IntegerFit`.

  The qualification package CLI receives `tests/fixtures/text` as its read-only
  asset-source root and materializes the preserved editor project with
  `QualificationAssetTreeFixture` semantics (copy each listed source and its
  existing sidecar, never generate metadata). Copy the committed scene bytes
  exactly to `<editorProject>/Scenes/qualification.json` before BuildPlan
  validation, copy the trace to
  `<editorProject>/Qualification/trace.json`, write this exact profile to its
  production project-settings location, and rescan. The serializer-parity and
  materialization assertions from Step 5 run before GameBuilder. The profile roots at
  authoritative family GUID `11111111111111111111111111111111`, whose complete
  reachable closure is all six font GUIDs and both license GUIDs asserted in
  Step 8. Resolver, package coverage, parity, and performance all consume this
  same rescanned tree.

  Stage `scene.json` and `trace.json` byte-exactly as
  `Qualification/{scene,trace}.json` before finalization. The package catalog
  and font closure come from the rescan; never infer a GUID from a filename or
  embed one in a family source.

- [ ] **Step 24: Parse the qualification-build argv and freeze its inputs**

  Add the noninteractive editor branch
  `--build-ui-text-qualification --profile <path> --asset-source-root <path>
  --scene <path> --trace <path> --editor-project-output <directory>
  --editor-project-report <json> --development-resource-output <directory>
  --development-report <json> --output <app> --report <json>`. Reject missing,
  duplicate, relative-output, symlinked, aliased, nested, or escaping paths and
  any app output not named `Qualification.app`. Load the production
  `BuildProfile` once and freeze one `QualificationBuildInputs` containing the
  canonical profile/scene/trace bytes, exact eleven source/sidecar pairs, and
  imported artifact authorities. Add a focused parser test for one valid argv
  and each rejection class before continuing.

- [ ] **Step 25: Materialize the preserved editor-project bytes atomically**

  Write the exact frozen profile, startup scene, trace, eleven source files,
  and eleven existing sidecars to a sibling temporary project. Reject a
  destination collision before the first write, rehash every staged file, and
  atomically replace the requested persistent editor-project output. Never use
  a cleanup-on-exit temporary project. Add a focused failure-injection test
  proving an existing reported project remains byte-identical on publish
  failure.

- [ ] **Step 26: Rescan and report the preserved editor authority**

  Bind the project `FontArtifactStore`, rescan the materialized project,
  publish its project-library font artifacts/catalog, and validate the
  `BuildPlan` startup scene. Atomically write an editor report containing the
  canonical root, complete tree SHA, profile SHA, startup-scene SHA, trace SHA,
  catalog SHA, and sorted six
  `{fontGuid,ProjectLibrary locator,artifactSha256}` records. Add one focused
  test that mutates a sidecar GUID and requires report generation to fail.

- [ ] **Step 27: Stage the independent development resource tree**

  From the frozen inputs, call the deterministic production resource-staging
  functions into a dedicated sibling temporary development root. Do not read
  or copy the completed package root. Rehash the complete sorted relative
  file/SHA set, atomically publish the root, and write its canonical report with
  `inputSetSha256`, canonical root, and tree SHA. Add one test proving the
  development and package staging destinations are disjoint before any write.
  Add a second focused test that opens the completed development root through
  `ValidateSealedPackage`, binds the returned authority to a fresh database as
  `AssetCatalogMode::SealedPackage`, and resolves all six font GUIDs. Loading
  the same catalog with a project store/mode must fail; this prevents a
  package-layout catalog from silently entering the ordinary development path.

- [ ] **Step 28: Build and report the separately sealed app**

  From the same frozen inputs, run normal `GameBuilder`, full `PackageLayout`
  validation, and `PackageFinalizer` into an independent sibling temporary app.
  Atomically publish `Qualification.app`, then atomically write its package
  report with the same input-set SHA and complete resource file/SHA set. Add a
  focused test proving the package builder never reads the finished
  development root and rolls back both app/report publication on finalizer
  failure.

- [ ] **Step 29: Add the exact qualification-package CMake target**

  Add a CMake custom target with exact output
  `${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app`:

  ```cmake
  set(MOLGA_TEXT_QUALIFICATION_RELATIVE_SOURCES
    fonts/NotoSans-Regular.ttf
    fonts/NotoSansHebrew-Regular.ttf
    fonts/NotoSansArabic-Regular.ttf
    fonts/NotoSansDevanagari-Regular.ttf
    fonts/NotoSansThai-Regular.ttf
    fonts/NotoSansKR-Regular.otf
    licenses/NotoFonts-ffebf8c1-OFL.txt
    licenses/NotoCJK-Sans2.004-OFL.txt
    families/primary.fontfamily
    families/arabic.fontfamily
    families/cjk.fontfamily)
  set(MOLGA_TEXT_QUALIFICATION_SOURCE_AND_META_FILES)
  foreach(relative IN LISTS MOLGA_TEXT_QUALIFICATION_RELATIVE_SOURCES)
    list(APPEND MOLGA_TEXT_QUALIFICATION_SOURCE_AND_META_FILES
      "${CMAKE_SOURCE_DIR}/tests/fixtures/text/${relative}"
      "${CMAKE_SOURCE_DIR}/tests/fixtures/text/${relative}.meta")
  endforeach()
  add_custom_command(
    OUTPUT
      "${CMAKE_BINARY_DIR}/ui-text-qualification/validation.stamp.json"
    BYPRODUCTS
      "${CMAKE_BINARY_DIR}/ui-text-qualification/build-report.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/sealed-validation.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/development-report.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/editor-project-report.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app/Contents/MacOS/Qualification"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app/Contents/Resources/game.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app/Contents/Resources/asset_catalog.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app/Contents/Resources/Manifests/text_runtime.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/development/Resources/.qualification-root.json"
      "${CMAKE_BINARY_DIR}/ui-text-qualification/editor-project/.qualification-project.json"
    COMMAND $<TARGET_FILE:molga_engine>
      --build-ui-text-qualification
      --profile
        "${CMAKE_SOURCE_DIR}/tests/fixtures/text/qualification/build_profile.json"
      --asset-source-root "${CMAKE_SOURCE_DIR}/tests/fixtures/text"
      --scene
        "${CMAKE_SOURCE_DIR}/tests/fixtures/text/qualification/scene.json"
      --trace
        "${CMAKE_SOURCE_DIR}/tests/fixtures/text/qualification/trace.json"
      --editor-project-output
        "${CMAKE_BINARY_DIR}/ui-text-qualification/editor-project"
      --editor-project-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/editor-project-report.json"
      --development-resource-output
        "${CMAKE_BINARY_DIR}/ui-text-qualification/development/Resources"
      --development-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/development-report.json"
      --output
        "${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app"
      --report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/build-report.json"
    COMMAND
      "${CMAKE_BINARY_DIR}/ui-text-qualification/Qualification.app/Contents/MacOS/Qualification"
      --validate-sealed-package-only
      --report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/sealed-validation.json"
    COMMAND $<TARGET_FILE:molga_engine>
      --validate-ui-text-qualification-outputs
      --build-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/build-report.json"
      --sealed-validation-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/sealed-validation.json"
      --development-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/development-report.json"
      --editor-project-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/editor-project-report.json"
      --stamp
        "${CMAKE_BINARY_DIR}/ui-text-qualification/validation.stamp.json"
    DEPENDS molga_engine molga_runtime
      ${MOLGA_TEXT_QUALIFICATION_SOURCE_AND_META_FILES}
      "${CMAKE_SOURCE_DIR}/tests/fixtures/text/qualification/build_profile.json"
      "${CMAKE_SOURCE_DIR}/tests/fixtures/text/qualification/scene.json"
      "${CMAKE_SOURCE_DIR}/tests/fixtures/text/qualification/trace.json"
    VERBATIM)
  add_custom_target(ui_text_qualification_package
    DEPENDS
      "${CMAKE_BINARY_DIR}/ui-text-qualification/validation.stamp.json"
    COMMAND $<TARGET_FILE:molga_engine>
      --validate-ui-text-qualification-outputs
      --build-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/build-report.json"
      --sealed-validation-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/sealed-validation.json"
      --development-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/development-report.json"
      --editor-project-report
        "${CMAKE_BINARY_DIR}/ui-text-qualification/editor-project-report.json"
      --existing-stamp
        "${CMAKE_BINARY_DIR}/ui-text-qualification/validation.stamp.json"
    VERBATIM)
  ```

- [ ] **Step 30: Validate live qualification outputs into one atomic stamp**

  `ValidateQualificationOutputsAndWriteStamp` rehashes the live app,
  independent development root, and preserved editor project against all four
  reports, runs sealed validation, and only then atomically publishes canonical
  `validation.stamp.json` with exact keys
  `{schemaVersion,inputSetSha256,packageTreeSha256,developmentTreeSha256,
  editorProjectTreeSha256,buildReportSha256,sealedValidationReportSha256,
  developmentReportSha256,editorProjectReportSha256}`. The always-run
  `--existing-stamp` form repeats every live check and requires byte-identical
  stamp content; on a missing/mutated byproduct it removes the stale stamp and
  fails so the next build rematerializes instead of reporting an up-to-date
  package. Add deletion/mutation regression cases for the app executable,
  package manifest, development sentinel, and editor-project sentinel.

- [ ] **Step 31: Validate all three reports and exclude resolver-only assets**

  Keep that exact eleven-item list byte-for-byte equal to
  `TextQualificationAssetTree.h::QualificationSourcePaths()`; do not use a
  recursive glob and assert neither cycle-family basename exists beneath the
  preserved project, development root, or sealed app. The package build report
  requires `status:"sealed"`, canonical app path, executable SHA-256,
  `inputSetSha256`, resource-tree SHA, complete sorted resource file/SHA map,
  `game.json` SHA-256, catalog SHA-256, runtime-manifest SHA-256, and the two
  qualification resource SHA-256 values. The development report independently
  hashes its canonical disjoint root and requires the identical input-set SHA
  and complete relative file/SHA map. The editor-project report binds the exact
  profile/startup scene/catalog/trace and all six `ProjectLibrary` artifact
  locators/hashes after rescan. The sealed validation report requires exit `0`,
  `validatedBeforeSDL:true`, and matching package hashes. Reject a report whose
  recorded roots alias/nest, whose live tree digest differs, or whose common
  input/file map differs. This target never writes into or treats CMake's
  engine-shell `molga_runtime.app` as a game package.

- [ ] **Step 32: Validate all runner inputs and containment**

  `run_text_parity.cmake` requires `EDITOR`, `DEV_RUNTIME`, `PACKAGED_APP`,
  `BUILD_REPORT`, `SEALED_VALIDATION_REPORT`, `DEV_REPORT`,
  `EDITOR_PROJECT_REPORT`, `QUALIFICATION_VALIDATION_STAMP`, `DEV_ROOT`,
  `SCENE`, `TRACE`, `SANDBOX_PROFILE`, and `OUTPUT_DIR`.
  Canonicalize each existing input; require `PACKAGED_APP` to be
  a bundle named exactly `Qualification.app`, never `molga_runtime.app`.
  Normally it must be the canonical app path named by `BUILD_REPORT`; the only
  permitted alternative is a checkout-external copy for Task 18.4 whose
  executable, `game.json`, catalog, runtime manifest, scene, and trace hashes
  all equal the report before execution. Require `DEV_ROOT` to equal the
  canonical root in `DEV_REPORT`, and require it to be neither equal to nor
  inside/containing the original or selected app's `Contents/Resources`. Use
  `cmake_path(IS_PREFIX ... NORMALIZE)` to require
  `DEV_ROOT/Qualification/{scene,trace}.json` beneath it. Parse all four
  reports, require sealed/exit-zero/pre-SDL status, independently rehash the
  app and development trees, verify their complete relative file/SHA maps
  equal, verify the editor-project report's
  input-set/profile/scene/trace/font hashes match the same frozen inputs, then
  rehash the app executable, `game.json`, catalog, runtime manifest, scene, and
  trace. Missing variables, aliased roots, report mismatch, stale output, or
  containment failure is fatal.

  Parse and canonical-byte-check `QUALIFICATION_VALIDATION_STAMP`, require its
  four report hashes and three tree/input hashes to match the live values, and
  reject a missing/stale stamp before copying or launching any mode.

  Canonicalize `SANDBOX_PROFILE` and require it to equal the regular,
  non-symlink `${CMAKE_CURRENT_LIST_DIR}/text_network_deny.sb` committed by
  Task 17.5. Require `/usr/bin/sandbox-exec`. Create `OUTPUT_DIR`, reject a
  symlink, canonicalize it, and use that exact canonical directory as the sole
  `MOLGA_ALLOWED_WRITE_ROOT`. Compute the profile SHA before any mode starts;
  a missing/different profile or profile hash change during the run is fatal.

- [ ] **Step 33: Copy the app and compare all fixture SHA-256 values**

  Copy the validated `Qualification.app` beneath a fresh
  `/usr/bin/mktemp -d -t molga-text-parity.XXXXXX` directory outside the
  checkout/build tree. Invoke its exact copied executable once with
  `--validate-sealed-package-only --report <fresh-report>` through the committed
  profile with `-D MOLGA_ALLOWED_WRITE_ROOT=${OUTPUT_DIR}` and require exit `0`
  plus `validatedBeforeSDL:true`. Use this exact invocation after the copy and
  before any mode runs:

  ```cmake
  set(COPIED_VALIDATION_REPORT
      "${OUTPUT_DIR}/copied-sealed-validation.json")
  if(EXISTS "${COPIED_VALIDATION_REPORT}" OR
     IS_SYMLINK "${COPIED_VALIDATION_REPORT}")
    message(FATAL_ERROR "copied sealed-validation report is not fresh")
  endif()
  execute_process(COMMAND /usr/bin/sandbox-exec
      -D "MOLGA_ALLOWED_WRITE_ROOT=${OUTPUT_DIR}"
      -f "${SANDBOX_PROFILE}" --
      /usr/bin/env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C
      "${COPIED_APP_EXECUTABLE}" --validate-sealed-package-only
      --report "${COPIED_VALIDATION_REPORT}"
      COMMAND_ERROR_IS_FATAL ANY)
  if(NOT EXISTS "${COPIED_VALIDATION_REPORT}" OR
     IS_DIRECTORY "${COPIED_VALIDATION_REPORT}" OR
     IS_SYMLINK "${COPIED_VALIDATION_REPORT}")
    message(FATAL_ERROR "copied sealed-validation report was not published")
  endif()
  file(REAL_PATH "${COPIED_VALIDATION_REPORT}"
       COPIED_VALIDATION_REPORT_REAL)
  cmake_path(IS_PREFIX OUTPUT_DIR "${COPIED_VALIDATION_REPORT_REAL}"
             NORMALIZE COPIED_REPORT_IS_CONTAINED)
  if(NOT COPIED_REPORT_IS_CONTAINED)
    message(FATAL_ERROR "copied sealed-validation report escaped output root")
  endif()
  ```

  Parse that fresh report with the same exact schema/canonical-byte validator as
  `SEALED_VALIDATION_REPORT`; require its recorded app/resource/executable
  hashes to match both the copied live tree and the original reports, then
  record its SHA-256 in the runner result. Compute SHA-256 for the source `SCENE`/`TRACE`,
  independently staged `DEV_ROOT/Qualification/{scene,trace}.json`, and copied-app
  `Contents/Resources/Qualification/{scene,trace}.json`; require each three-way
  set to match before any mode runs. Also require copied executable,
  `game.json`, catalog, and runtime-manifest hashes to match the original build
  report. Recompute the copied app's complete resource file/SHA map and require
  it equal both the original package report and independent development report;
  copying the app must not change `DEV_ROOT`.

- [ ] **Step 34: Run all modes from their distinct resource roots**

  Use these commands; editor and development read the independently staged
  development resource bytes, while packaged runtime resolves the same
  relative names under the copied package resource root:

  ```cmake
  set(SCENE_REL "Qualification/scene.json")
  set(TRACE_REL "Qualification/trace.json")
  set(DEV_SCENE "${DEV_ROOT}/${SCENE_REL}")
  set(DEV_TRACE "${DEV_ROOT}/${TRACE_REL}")
  execute_process(COMMAND "${EDITOR}" --text-canonical "${DEV_SCENE}"
      --qualification-resource-root "${DEV_ROOT}"
      --viewport 1920x1080 --input-trace "${DEV_TRACE}"
      --output "${OUTPUT_DIR}/editor.json" COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${DEV_RUNTIME}" --development-resource-root
      "${DEV_ROOT}" --text-canonical "${DEV_SCENE}" --viewport 1920x1080
      --input-trace "${DEV_TRACE}" --output "${OUTPUT_DIR}/dev.json"
      COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND /usr/bin/sandbox-exec
      -D "MOLGA_ALLOWED_WRITE_ROOT=${OUTPUT_DIR}"
      -f "${SANDBOX_PROFILE}" --
      /usr/bin/env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C
      "${COPIED_APP_EXECUTABLE}" --text-canonical
      "${SCENE_REL}" --viewport 1920x1080 --input-trace "${TRACE_REL}"
      --output "${OUTPUT_DIR}/package.json" COMMAND_ERROR_IS_FATAL ANY)
  ```

  Atomically write noncanonical evidence
  `${OUTPUT_DIR}/packaged-sandbox-audit.json` with fixed keys
  `{schemaVersion,sandboxedLaunch,profilePath,profileSha256,
  allowedWriteRoot,executablePath,executableSha256,argv,argvSha256,exitCode}`.
  Require `sandboxedLaunch:true`, exact canonical paths, unchanged profile SHA,
  and exit `0`. `argv` is the complete literal sandbox vector, including `-D`,
  `-f`, `--`, `/usr/bin/env`, `-i`, the three shown environment assignments,
  copied executable, and qualification arguments; it is not reconstructed as
  an app-only suffix. This audit never enters the three canonical snapshot
  bytes.

- [ ] **Step 35: Compare canonical bytes and hashes**

  After all processes exit zero, compare files and then require their SHA strings to be identical:

  ```cmake
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
      "${OUTPUT_DIR}/editor.json" "${OUTPUT_DIR}/dev.json"
      COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
      "${OUTPUT_DIR}/editor.json" "${OUTPUT_DIR}/package.json"
      COMMAND_ERROR_IS_FATAL ANY)
  file(SHA256 "${OUTPUT_DIR}/editor.json" EDITOR_SHA)
  file(SHA256 "${OUTPUT_DIR}/dev.json" DEV_SHA)
  file(SHA256 "${OUTPUT_DIR}/package.json" PACKAGE_SHA)
  if(NOT EDITOR_SHA STREQUAL DEV_SHA OR NOT EDITOR_SHA STREQUAL PACKAGE_SHA)
    message(FATAL_ERROR "canonical snapshot SHA mismatch")
  endif()
  ```

- [ ] **Step 36: Run the green driver and parity test**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_canonical molga_engine \
    test_text_input_arbiter molga_runtime_dev \
    ui_text_qualification_package -j
  ctest --test-dir build/debug \
    -R '^(test_text_canonical|test_text_input_arbiter)$' --output-on-failure
  cmake -DEDITOR="$PWD/build/debug/molga_engine" \
        -DDEV_RUNTIME="$PWD/build/debug/molga_runtime_dev" \
        -DPACKAGED_APP="$PWD/build/debug/ui-text-qualification/Qualification.app" \
        -DBUILD_REPORT="$PWD/build/debug/ui-text-qualification/build-report.json" \
        -DSEALED_VALIDATION_REPORT="$PWD/build/debug/ui-text-qualification/sealed-validation.json" \
        -DDEV_REPORT="$PWD/build/debug/ui-text-qualification/development-report.json" \
        -DEDITOR_PROJECT_REPORT="$PWD/build/debug/ui-text-qualification/editor-project-report.json" \
        -DQUALIFICATION_VALIDATION_STAMP="$PWD/build/debug/ui-text-qualification/validation.stamp.json" \
        -DDEV_ROOT="$PWD/build/debug/ui-text-qualification/development/Resources" \
        -DSCENE="$PWD/tests/fixtures/text/qualification/scene.json" \
        -DTRACE="$PWD/tests/fixtures/text/qualification/trace.json" \
        -DSANDBOX_PROFILE="$PWD/tests/smoke/text_network_deny.sb" \
        -DOUTPUT_DIR="$PWD/build/debug/text-parity" \
        -P tests/smoke/run_text_parity.cmake
  ```

  Expected: the distinct GameBuilder-produced `Qualification.app` validates
  before SDL; independently staged package/development/preserved-project
  reports and live hashes match the frozen input set; the packaged canonical
  run has a passing deny-network invocation audit bound to the exact Task 17.5
  profile and output root; source, development, and
  copied-package fixture hashes match; native two-batch ownership tests pass;
  and all three canonical output files compare byte-identically with one
  SHA-256. The engine shell `molga_runtime.app` is neither read nor modified.

- [ ] **Step 37: Commit the parity driver and exact fixtures**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Tools/TextQualification.h \
    src/Tools/TextQualification.cpp \
    src/main.cpp src/runtime_main.cpp src/Core/BuildProfile.h \
    src/Core/BuildProfile.cpp src/Core/SmokeReport.h \
    src/Core/SmokeReport.cpp src/Core/Bootstrap.h src/Core/Bootstrap.cpp \
    src/Platform/NativeInputEvent.h src/UI/UISystem.h src/UI/UISystem.cpp \
    src/Editor/GameBuilder.h \
    src/Editor/GameBuilder.cpp tests/test_text_canonical.cpp \
    tests/test_game_builder.cpp tests/test_text_input_arbiter.cpp \
    tests/TextQualificationAssetTree.h \
    tests/fixtures/text/qualification/build_profile.json \
    tests/fixtures/text/qualification/scene.json \
    tests/fixtures/text/qualification/trace.json \
    tests/smoke/run_text_parity.cmake
  git commit -m "test: add three-mode UI text parity runner"
  ```

### Task 18.3: Enforce allocation, workload, atlas, and reference timing gates

**Files:**

- Create: `src/Text/TextAllocatorTelemetry.h`
- Create: `src/Text/TextAllocatorTelemetry.cpp`
- Create: `src/Text/TextPerformanceHarness.h`
- Create: `src/Text/TextPerformanceHarness.cpp`
- Create: `src/Tools/TextPerformanceReportMain.cpp`
- Create: `tests/test_text_performance.cpp`
- Modify: `src/Text/UnicodeTextBuffer.cpp`
- Modify: `src/Text/UnicodeAnalysis.cpp`
- Modify: `src/Text/FontFamilyResolver.cpp`
- Modify: `src/Text/TextShapingService.cpp`
- Modify: `src/Text/TextLayoutCache.cpp`
- Modify: `src/Text/TextLayoutService.cpp`
- Modify: `src/Rendering/FontAtlas.cpp`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UIRenderCollector.cpp`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/Rendering/RenderSystem2D.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: the exact production Unicode buffer/analysis, resolver, shaper,
  layout cache/service, atlas, UI layout/facade/collector, and render-queue
  adapters plus exact fixture fonts/text.
- Produces: adapter-proven tagged allocation/visit measurement, exact workload
  builders, nearest-rank p95, fingerprint qualification, and canonical
  performance report.

- [ ] **Step 1: Register the performance test and add p95/fingerprint failures**

  ```cmake
  molga_add_text_test(test_text_performance test_text_performance.cpp)

  add_executable(molga_text_performance_report
    src/Tools/TextPerformanceReportMain.cpp)
  target_link_libraries(molga_text_performance_report PRIVATE
    molga_core molga_warnings)
  molga_attach_text_dependencies(molga_text_performance_report)
  molga_stage_text_runtime_resources(molga_text_performance_report)
  ```

  The workload uses `TextRuntimeTestSession::Current()` from Task 1. Retain all
  existing Release/Metal/SDL target links, labels, compile definitions, and
  target-specific properties after the helper creates the executable; the
  conversion changes only lifetime/resource ownership and never replaces
  target-specific setup with generic defaults.

  Create the production-shaped red driver in
  `src/Tools/TextPerformanceReportMain.cpp`; it deliberately fails to compile
  until Steps 26–27 define the parser and runner, while the CMake target itself
  exists from this step onward:

  ```cpp
  #include "Text/TextDiagnostic.h"
  #include "Text/TextPerformanceHarness.h"
  #include <string>
  #include <vector>

  int main(int argc, char** argv) {
      std::string error;
      const std::vector<const char*> arguments(argv, argv + argc);
      const auto options = molga::text::ParseTextPerformanceReportOptions(
          argc, arguments.data(), error);
      if (!options) return 2;
      molga::text::VectorTextDiagnosticSink sink;
      return molga::text::RunTextPerformanceReport(*options, sink);
  }
  ```

  ```cpp
  TEST_CASE("nearest rank p95 uses ceil point nine five N") {
      std::vector<double> values;
      for (int i = 1; i <= 20; ++i) values.push_back(i);
      CHECK(molga::text::NearestRankPercentile(values, 0.95) == 19.0);
  }

  TEST_CASE("timing qualification requires the complete reference fingerprint") {
      auto reference = molga::text::ReferenceMachineFingerprint();
      CHECK(reference.TimingQualified());
      reference.onAcPower = false;
      CHECK_FALSE(reference.TimingQualified());
      reference = molga::text::ReferenceMachineFingerprint();
      reference.backingScale = 1.0;
      CHECK_FALSE(reference.TimingQualified());
  }
  ```

- [ ] **Step 2: Add exact workload-composition failures**

  ```cpp
  TEST_CASE("performance workloads have frozen composition") {
      QualificationAssetTreeFixture tree;
      const auto context = LoadProjectQualificationTextContext(
          tree.ProjectRoot(), tree.AssetsRoot(),
          "11111111111111111111111111111111", tree.Diagnostics());
      const auto staticWork = molga::text::BuildStaticWorkload(context);
      CHECK(staticWork.labels.size() == 1000u);
      CHECK(TotalGraphemes(staticWork) == 10000u);
      CHECK(UniqueFontSizes(staticWork).size() == 3u);
      CHECK(ScriptsRoundRobin(staticWork));

      const auto dynamicWork = molga::text::BuildDynamicWorkload(context);
      CHECK(dynamicWork.samples.size() == 600u);
      for (const auto& sample : dynamicWork.samples) {
          CHECK(sample.totalGraphemes == 2000u);
          CHECK(sample.scriptCounts == std::array<std::uint32_t, 6>{
              400u, 300u, 300u, 300u, 300u, 400u});
          CHECK(sample.changedGraphemeCount == 1u);
      }
  }
  ```

- [ ] **Step 3: Add universal zero/cap gate failures**

  ```cpp
  TEST_CASE("universal performance gates never skip on a nonreference host") {
      auto harness = MakeWarmedPerformanceHarness();
      const auto measured = harness.MeasureUniversalGates();
      CHECK(measured.workloads.warmupFrames == 120u);
      CHECK(measured.workloads.sampleCount == 600u);
      CHECK(measured.staticReshapeCount == 0u);
      CHECK(measured.staticTaggedAllocationCount == 0u);
      CHECK(measured.atlas.peakResidentBytes <=
            64ULL * 1024ULL * 1024ULL);
      CHECK(measured.atlas.sourceFontBytes > 0u);
      CHECK(measured.atlas.uploadStagingPeakBytes > 0u);
      CHECK_FALSE(measured.atlas.unlimitedMode);
      CHECK(measured.universalPassed);
  }

  TEST_CASE("durable report bytes consume the exact measured result") {
      auto measured = MakePassingMeasuredResult();
      const TextPerformanceReportProvenance provenance{
          FortyHex('a'), SixtyFourHex('b'), SixtyFourHex('c')};
      std::string error;
      const auto report = BuildTextPerformanceReport(
          provenance, measured, error);
      REQUIRE(report.has_value());
      const auto bytes = report->SerializeCanonical().dump(2) + "\n";
      const auto json = nlohmann::json::parse(bytes);
      CHECK(json.at("testedCommit") == provenance.testedCommit);
      CHECK(json.at("measurement").at("adapters").size() ==
            static_cast<std::size_t>(TextAllocationTag::Count));
      CHECK(json.at("failureReasons") == measured.failureReasons);
      CHECK(bytes == CanonicalBytes(json));
  }
  ```

- [ ] **Step 4: Add the failing production-adapter tag-hit test**

  ```cpp
  TEST_CASE("one cold production frame visits every measured adapter tag") {
      auto fixture = MakeColdProductionPerformanceFixture();
      molga::text::TextAllocatorTelemetry::ResetForTest();
      REQUIRE(fixture.RunOneFrameThroughProductionUIAndRenderQueue());
      const auto snapshot = molga::text::TextAllocatorTelemetry::Snapshot();
      const std::array expected{
          TextAllocationTag::UnicodeBuffer,
          TextAllocationTag::UnicodeAnalysis,
          TextAllocationTag::FontFallback,
          TextAllocationTag::Shaping,
          TextAllocationTag::LayoutCache,
          TextAllocationTag::ParagraphLayout,
          TextAllocationTag::GlyphAtlas,
          TextAllocationTag::UILayout,
          TextAllocationTag::UISystem,
          TextAllocationTag::UIRenderCollect,
          TextAllocationTag::TextRenderCollect,
          TextAllocationTag::RenderQueue};
      for (const auto tag : expected) {
          CAPTURE(molga::text::TextAllocationTagName(tag));
          CHECK(snapshot.byTag.at(tag).visitCount > 0u);
      }
      CHECK(fixture.UsedTextPerformanceHarnessBypass() == false);
  }
  ```

  The fixture calls the production `UISystem`/render path; it may inject the
  allocator sink but cannot call `RecordVisit` or `RecordAllocation` itself.

- [ ] **Step 5: Run the red performance gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_performance \
    molga_text_performance_report -j
  ```

  Expected: compile fails because telemetry, adapter tags, workload, and
  percentile/report CLI APIs do not exist.

- [ ] **Step 6: Define tagged allocation counters**

  ```cpp
  namespace molga::text {
  enum class TextAllocationTag : std::uint8_t {
      UnicodeBuffer, UnicodeAnalysis, FontFallback, Shaping, LayoutCache,
      ParagraphLayout, GlyphAtlas, UILayout, UISystem, UIRenderCollect,
      TextRenderCollect, RenderQueue, Count
  };
  struct TextAllocationTagSnapshot {
      std::uint64_t visitCount = 0;
      std::uint64_t allocationCount = 0;
      std::uint64_t allocatedBytes = 0;
  };
  struct TextAllocationSnapshot {
      std::array<TextAllocationTagSnapshot,
                 static_cast<std::size_t>(TextAllocationTag::Count)> byTag;
      std::uint64_t allocationCount = 0;
      std::uint64_t allocatedBytes = 0;
  };
  class TextAllocatorTelemetry {
  public:
      static void RecordVisit(TextAllocationTag) noexcept;
      static void RecordAllocation(TextAllocationTag,
                                   std::size_t bytes) noexcept;
      static TextAllocationSnapshot Snapshot() noexcept;
      static void BeginMeasuredInterval() noexcept;
      static TextAllocationSnapshot EndMeasuredInterval() noexcept;
      static void ResetForTest() noexcept;
  };
  std::string_view TextAllocationTagName(TextAllocationTag) noexcept;
  }
  ```

  Use fixed atomic counters only; recording must allocate nothing. Tests inject a counting allocator; do not globally override unrelated engine `operator new`.

- [ ] **Step 7: Define measurement and durable report types**

  ```cpp
  struct TextPerformanceFingerprint {
      std::string modelIdentifier, cpu, memory, osVersion, osBuild;
      std::string architecture, compiler;
      bool onAcPower = false;
      bool releaseBuild = false;
      bool debuggerAttached = false;
      double backingScale = 0.0;
      bool TimingQualified() const noexcept;
  };
  struct TextPerformanceWorkloadReport {
      std::uint32_t warmupFrames = 120;
      std::uint32_t sampleCount = 600;
      std::uint32_t staticLabelCount = 1000;
      std::uint32_t staticTotalGraphemes = 10000;
      std::array<std::uint32_t, 6> staticScriptCounts{};
      std::uint32_t dynamicGraphemesPerSample = 2000;
      std::array<std::uint32_t, 6> dynamicScriptCounts{
          400, 300, 300, 300, 300, 400};
      std::uint32_t changedGraphemesPerSample = 1;
  };
  struct TextPerformanceAtlasReport {
      std::uint64_t budgetBytes = 64ULL * 1024ULL * 1024ULL;
      std::uint64_t peakResidentBytes = 0;
      std::uint64_t sourceFontBytes = 0;
      std::uint64_t uploadStagingPeakBytes = 0;
      bool unlimitedMode = false;
  };
  struct TextPerformanceMeasurementResult {
      TextPerformanceFingerprint fingerprint;
      TextPerformanceWorkloadReport workloads;
      double staticP95Milliseconds = 0.0;
      double dynamicP95Milliseconds = 0.0;
      std::uint64_t staticReshapeCount = 0;
      std::uint64_t staticTaggedAllocationCount = 0;
      std::array<TextAllocationTagSnapshot,
          static_cast<std::size_t>(TextAllocationTag::Count)> adapterCounters;
      TextPerformanceAtlasReport atlas;
      bool universalPassed = false;
      bool timingApplicable = false;
      bool timingPassed = false;
      std::vector<std::string> failureReasons;
  };
  struct TextPerformanceReportProvenance {
      std::string testedCommit;
      std::string executableSha256;
      std::string inputSetSha256;
  };
  struct TextPerformanceReport {
      TextPerformanceReportProvenance provenance;
      TextPerformanceMeasurementResult measured;
      nlohmann::ordered_json SerializeCanonical() const;
  };
  std::optional<TextPerformanceReport> BuildTextPerformanceReport(
      TextPerformanceReportProvenance,
      TextPerformanceMeasurementResult,
      std::string& errorOut);
  ```

  `TextPerformanceHarness::MeasureUniversalGates()` returns exactly
  `TextPerformanceMeasurementResult`. The CLI obtains provenance only after
  validating the preserved-project report and hashing its own executable, then
  calls `BuildTextPerformanceReport`. The builder validates lowercase hash
  lengths, exact workload constants, a complete one-entry-per-tag counter
  array, atlas budget, sorted-unique failure reasons, and decision consistency.
  Tests serialize this durable `TextPerformanceReport`, never an unrelated
  summary or ad hoc JSON object.

- [ ] **Step 8: Implement nearest-rank p95 only**

  Reject empty input or `p <= 0 || p > 1`, sort a copy, and select `ceil(p*N)-1`. Do not interpolate adjacent samples.

- [ ] **Step 9: Build the frozen static workload**

  Accept the rescanned `QualificationTextContext` from Task 18.2; require its
  primary/fallback closure to resolve exact Latin, Hebrew, Arabic, Devanagari,
  Thai, and CJK GUIDs from the Task 4.2 sidecars. Make exactly 1,000 labels with
  10 extended graphemes each and three fixed font sizes. Assign those six
  scripts round-robin by label index and assert total `10,000` before
  returning. Never open a second font tree or synthesize a family.

- [ ] **Step 10: Build the frozen dynamic workload**

  Reuse the same `QualificationTextContext`. Emit exactly 600 distinct
  2,000-grapheme fixtures. Each contains `400/300/300/300/300/400`
  Latin/Arabic/Hebrew/Devanagari/Thai/CJK graphemes and changes exactly one
  same-length grapheme from the prior sample. Assert every script resolves to
  the expected committed face GUID before timing begins.

- [ ] **Step 11: Run the focused percentile/workload slice**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_performance -j
  build/debug/tests/test_text_performance --test-case="*nearest rank*"
  build/debug/tests/test_text_performance --test-case="*frozen composition*"
  ```

  Expected: p95 and both composition cases pass; the universal gate still fails until telemetry and measurement exist.

- [ ] **Step 12: Instrument Unicode buffer and analysis adapters**

  In `UnicodeTextBuffer.cpp`, record `UnicodeBuffer` visit at the production
  decode entry and record only decoded scalar/source-map/grapheme vector growth
  through the injected text allocator. In `UnicodeAnalysis.cpp`, record
  `UnicodeAnalysis` at `UnicodeTextAnalyzer::Analyze` and tag ICU-owned analysis
  result copies/runs created for that request. Do not count unrelated ICU
  process-global initialization or general engine allocations.

- [ ] **Step 13: Instrument the fallback resolver adapter**

  In `FontFamilyResolver.cpp`, record `FontFallback` at the production resolve
  entry and tag the finite candidate/coverage storage allocated for that
  request. Cache hits still record a visit but allocate zero; font artifact
  bytes are reported separately and never charged to this tag.

- [ ] **Step 14: Instrument shaping, layout-cache, and paragraph adapters**

  In `TextShapingService.cpp`, record `Shaping` and tag HarfBuzz-output copies
  retained by the immutable result. In `TextLayoutCache.cpp`, record
  `LayoutCache` for each request/final lookup or store and tag only cache-owned
  key/value growth. In `TextLayoutService.cpp`, record `ParagraphLayout` and tag
  final line, run, positioned-glyph, caret, and validation-fact storage.

- [ ] **Step 15: Instrument the glyph-atlas adapter**

  In `FontAtlas.cpp`, record `GlyphAtlas` at `GlyphAtlasCache::GetGlyph` and
  record resident page allocation/free against that tag. Continue reporting
  source font bytes and temporary upload staging in separate non-resident
  fields; neither contributes to `atlasPeakResidentBytes` or tagged resident
  allocation.

- [ ] **Step 16: Instrument UI layout and facade adapters**

  In `UILayoutSystem.cpp`, record `UILayout` at `Build` before its scalar fast
  path; tag only immutable geometry/snapshot payload allocation after a miss.
  In `UISystem.cpp`, record `UISystem` at the production frame/layout facade and
  tag its text/UI frame-owned staging. An unchanged full-snapshot fast-path
  visit allocates zero.

- [ ] **Step 17: Instrument UI collection and render-queue adapters**

  In `UIRenderCollector.cpp`, record `UIRenderCollect`; in `TextRenderer.cpp`,
  record `TextRenderCollect`; and in `RenderSystem2D.cpp`, record `RenderQueue`
  at the production text/UI command append boundary. Tag only vector growth
  owned by the measured text/UI commands, not unrelated sprite or renderer
  allocations.

- [ ] **Step 18: Run the focused production tag-hit slice**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_performance -j
  build/debug/tests/test_text_performance \
    --test-case="*cold production frame visits every measured adapter tag*"
  ```

  Expected: every named production adapter reports at least one visit, no
  harness bypass is observed, and removing any one adapter call makes this
  focused case fail with that stable tag name.

- [ ] **Step 19: Warm static caches for exactly 120 frames**

  Run the unchanged static workload through decode/analysis/fallback/shape/layout/UI/atlas/queue for 120 unmeasured frames. Assert every label has a cached layout and resident glyph before opening the measured interval; reset telemetry counters only after this assertion.

- [ ] **Step 20: Measure each static sample on the exact boundary**

  For 600 samples, start the monotonic clock and tagged interval immediately after UI event dequeue; stop both immediately after RenderQueue completion. Exclude GPU encode/submit/present. Record decode/analysis/fallback/shape/final-line/cache/atlas/snapshot/queue counters and require aggregate reshape/allocation zero.

- [ ] **Step 21: Measure each dynamic sample on the exact boundary**

  For each frozen fixture, start before UTF-8 decode and stop after final line layout. Assert the one-grapheme mutation causes a content miss, includes ICU analysis/fallback/HarfBuzz shaping/final line layout, and yields exactly 600 monotonic samples.

- [ ] **Step 22: Enforce the universal atlas contract**

  Configure resident atlas budget to exactly `64 * 1024 * 1024` bytes on every host. Fail on unlimited mode or a higher configured limit even if observed use is smaller. Track page allocations/frees and require `peakResidentBytes <= 64 MiB`; report source fonts and upload staging separately.

- [ ] **Step 23: Collect and compare the complete timing fingerprint**

  Populate every fingerprint field from macOS/system/build state. `TimingQualified()` returns true only for exact `Mac14,9`, `Apple M2 Pro 10-core`, `16 GiB`, `macOS 26.5.1`, build `25F80`, `arm64`, `Apple clang 21.0.0`, AC power, Release, backing scale `2.0`, and no debugger; partial matches return false.

- [ ] **Step 24: Apply universal and reference-only decisions**

  Always fail for composition mismatch, report schema mismatch, static reshape/allocation nonzero, resident bytes over `64 * 1024 * 1024`, or unlimited mode. Set `timingApplicable` only for an exact fingerprint match; then require static p95 `<=4.0` ms and dynamic p95 `<=8.0` ms. A nonmatching machine records timing and passes only if universal gates pass; it never claims timing qualification.

- [ ] **Step 25: Serialize one exact report schema canonically**

  Emit root keys in exactly this order:
  `{schemaVersion,testedCommit,executableSha256,inputSetSha256,fingerprint,
  workloads,measurement,atlas,universalPassed,timingApplicable,timingPassed,
  failureReasons}`. `schemaVersion` is `1`. `fingerprint` contains exactly
  `{modelIdentifier,cpu,memory,osVersion,osBuild,architecture,compiler,
  onAcPower,releaseBuild,debuggerAttached,backingScale}`. `workloads` contains
  exact frozen static/dynamic script counts plus warm-up/sample counts.
  `measurement` contains static/dynamic p95 and every named adapter's
  visit/allocation/bytes counters plus static reshape/allocation. `atlas`
  contains exactly `{budgetBytes,peakResidentBytes,sourceFontBytes,
  uploadStagingPeakBytes,unlimitedMode}`. `failureReasons` is a sorted unique
  string array. Reject extra/missing keys on read; reparse and byte-compare
  `BuildTextPerformanceReport(...)->SerializeCanonical().dump(2) + "\n"` in
  the test. Serializer fields come only from `provenance`,
  `measured.workloads`, `measured.adapterCounters`, `measured.atlas`, the
  measured decisions, and measured failure reasons; it cannot consult globals
  or recreate missing data.

- [ ] **Step 26: Define the production report CLI and exit contract**

  Add to `TextPerformanceHarness.h`:

  ```cpp
  struct TextPerformanceReportOptions {
      std::filesystem::path qualificationProject;
      std::filesystem::path editorProjectReport;
      std::filesystem::path outputRoot;
      std::filesystem::path reportRelativePath;
      std::string testedCommit;
  };
  std::optional<TextPerformanceReportOptions>
  ParseTextPerformanceReportOptions(int argc, const char* const* argv,
                                    std::string& errorOut);
  int RunTextPerformanceReport(const TextPerformanceReportOptions&,
                               TextDiagnosticSink&);
  ```

  `src/Tools/TextPerformanceReportMain.cpp` accepts only this exact argv:

  ```text
  molga_text_performance_report
    --qualification-project <absolute-preserved-project>
    --editor-project-report <absolute-report.json>
    --output-root <absolute-existing-directory>
    --report performance.json
    --tested-commit <40-lowercase-hex>
  ```

  Reject unknown/duplicate/missing options, path aliases, relative/symlink
  roots, any report name other than the single safe basename
  `performance.json`, stale project report, or a project/profile/scene/catalog/
  trace/font-artifact/input-set hash mismatch before measurement. Hash the
  running executable into the report. Return `2` for CLI/authority errors, `4`
  for text-runtime/input initialization failure, `5` for output publication
  failure, and `6` when a universal gate fails or applicable reference timing
  fails. Return `0` only after durable report publication with universal pass
  and either non-applicable timing or applicable passing timing; a nonreference
  machine still records `timingApplicable:false,timingPassed:false`.
  The handoff is exactly
  `MeasureUniversalGates() -> TextPerformanceMeasurementResult ->
  BuildTextPerformanceReport(validatedProvenance, measured) -> atomic bytes`.
  The validated project report supplies `inputSetSha256`, the options supply
  `testedCommit`, and a live hash of `argv[0]` supplies
  `executableSha256`; any durable-builder validation failure returns `6` and
  records no final report.

- [ ] **Step 27: Complete the durable report target and test hostile paths**

  Complete the already registered `molga_text_performance_report` production
  executable and its Step 1 red main; do not add a second target or staging
  attachment. Its main publishes the target's explicit development root, creates one
  `TextRuntimeLifetimeGuard`, runs the harness, destroys every text owner, and
  then shuts down. `RunTextPerformanceReport` serializes canonical bytes to a
  unique sibling temp under the canonical `outputRoot`, flushes/closes,
  reparses and byte-compares, then atomically renames to
  `outputRoot/performance.json`. It removes only its temp on failure and never
  overwrites a preexisting report.

  In `test_text_performance`, test missing/duplicate args, relative output,
  symlink root, `../performance.json`, stale project report, preexisting final,
  injected short write, and successful byte/SHA parity from a hostile current
  directory. Require no final file for every failure and exact exit codes via
  callback-injected file/measurement seams.

- [ ] **Step 28: Run the Debug universal performance gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_performance \
    molga_text_performance_report -j
  ctest --test-dir build/debug -R '^test_text_performance$' --output-on-failure
  ```

  Expected: the frozen composition, zero warm-static allocation/reshape, and
  exact 64 MiB resident-budget gates pass; timing is reported but Debug never
  supplies reference timing evidence.

- [ ] **Step 29: Run the Release durable report and conditional timing gate**

  Run:

  ```bash
  cmake --preset release
  cmake --build --preset release --target test_text_performance \
    molga_text_performance_report ui_text_qualification_package -j
  ctest --test-dir build/release -R '^test_text_performance$' --output-on-failure
  performance_root="$PWD/build/release/ui-text-performance"
  mkdir -p "$performance_root"
  "$PWD/build/release/molga_text_performance_report" \
    --qualification-project \
      "$PWD/build/release/ui-text-qualification/editor-project" \
    --editor-project-report \
      "$PWD/build/release/ui-text-qualification/editor-project-report.json" \
    --output-root "$performance_root" \
    --report performance.json \
    --tested-commit "$(git rev-parse HEAD)"
  python3 -m json.tool "$performance_root/performance.json" >/dev/null
  shasum -a 256 "$performance_root/performance.json"
  ```

  Expected: universal gates pass and one canonical durable report plus printed
  SHA exist; Release enforces timing only if the complete fingerprint matches
  and otherwise records timing as non-qualifying with process exit `0`.

- [ ] **Step 30: Commit telemetry, harness, and report CLI**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextAllocatorTelemetry.h \
    src/Text/TextAllocatorTelemetry.cpp src/Text/TextPerformanceHarness.h \
    src/Text/TextPerformanceHarness.cpp src/Tools/TextPerformanceReportMain.cpp \
    src/Text/UnicodeTextBuffer.cpp \
    src/Text/UnicodeAnalysis.cpp src/Text/FontFamilyResolver.cpp \
    src/Text/TextShapingService.cpp src/Text/TextLayoutCache.cpp \
    src/Text/TextLayoutService.cpp src/Rendering/FontAtlas.cpp \
    src/UI/UILayoutSystem.cpp src/UI/UISystem.cpp \
    src/UI/UIRenderCollector.cpp src/Rendering/TextRenderer.cpp \
    src/Rendering/RenderSystem2D.cpp tests/test_text_performance.cpp
  git commit -m "test: add UI text performance gates"
  ```

### Task 18.4: Add GPU golden and reproducible macOS qualification scripts

**Files:**

- Create: `tests/fixtures/text/expected/gpu-command-stream.json`
- Create: `tests/fixtures/text/expected/gpu-mixed-ui.rgba`
- Create: `src/Editor/TextQualificationEditorSession.h`
- Create: `src/Editor/TextQualificationEditorSession.cpp`
- Create: `src/Tools/VisibleTextSessionAudit.h`
- Create: `src/Tools/VisibleTextSessionAudit.cpp`
- Create: `src/Platform/MacTextInputSource.h`
- Create: `src/Platform/MacTextInputSource.mm`
- Create: `src/Platform/MacVisibleWindow.h`
- Create: `src/Platform/MacVisibleWindow.mm`
- Create: `src/Platform/NativeInputProvenance.h`
- Create: `src/Platform/TextInputOwnerTransitionAudit.h`
- Create: `src/Tools/VisibleWindowCatalogMain.cpp`
- Create: `tests/test_text_qualification_editor.cpp`
- Create: `tests/test_visible_text_session_audit.cpp`
- Create: `scripts/qualification/run_ui_text_macos.sh`
- Create: `scripts/qualification/record_ui_text_ime.sh`
- Create: `docs/qualification/ui-text-milestone-a/README.md`
- Modify: `.gitattributes`
- Modify: `src/Tools/TextQualification.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Editor/ImGuiTextInputBridge.h`
- Modify: `src/Editor/ImGuiTextInputBridge.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/Platform/NativeInputEvent.h`
- Modify: `src/UI/UIInputEvent.h`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/Core/SmokeReport.h`
- Modify: `src/Core/SmokeReport.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: parity/performance harness, copied app, Task 17.5's existing
  self-tested deny-network profile, SDL_GPU/Metal command stream, preserved
  qualification editor-project report, and visible engine audit output.
- Produces: source-root-bound pinned offscreen golden contract, fail-closed
  Release editor qualification-session CLI, native-input/source-bound visible
  audit output, automated qualification runner, two-session evidence schema,
  and clean-candidate enforcement.

- [ ] **Step 1: Add the failing command-stream and golden test**

  Extend `tests/test_rendering_sdlgpu.cpp`:

  ```cpp
  TEST_CASE("mixed text selection caret and clips match command golden") {
      auto fixture = BuildMixedScriptGpuFixture(
          u8"Latin سلام שלום क्षि กำลัง 한글 日本語");
      const auto commands = fixture.Collect();
      CHECK(CanonicalGpuCommands(commands) == LoadJson(
          TextExpectedFixturePath("gpu-command-stream.json")));
      const auto image = fixture.RenderOffscreenMetal();
      CHECK(CompareRgbaGolden(image,
          TextExpectedFixturePath("gpu-mixed-ui.rgba"),
          /*perChannelTolerance=*/2, /*maxDifferentPixels=*/32));
      CHECK(fixture.AtlasTelemetry().residentBytes <=
            64ULL * 1024ULL * 1024ULL);
      CHECK(fixture.AllTextureLifetimesRetiredAfterFence());
  }
  ```

  The committed command JSON and raw RGBA fixture are reviewed inputs created by the pinned engine renderer, not captured from a system font path.

- [ ] **Step 2: Add the failing source-root and hostile-CWD golden test**

  ```cpp
  TEST_CASE("GPU golden paths are source rooted and independent of cwd") {
      test_support::TempDirectory hostile{"gpu-golden-cwd"};
      test_support::ScopedCurrentPath cwd{hostile.Path()};
      const auto command = TextExpectedFixturePath("gpu-command-stream.json");
      const auto rgba = TextExpectedFixturePath("gpu-mixed-ui.rgba");
      CHECK(command == fs::path(MOLGA_TEXT_EXPECTED_ROOT) /
                           "gpu-command-stream.json");
      CHECK(rgba == fs::path(MOLGA_TEXT_EXPECTED_ROOT) /
                        "gpu-mixed-ui.rgba");
      CHECK(fs::is_regular_file(command));
      CHECK(fs::is_regular_file(rgba));
      CHECK_FALSE(IsPathPrefix(hostile.Path(), command));
      CHECK(CanonicalGpuCommands(BuildMixedScriptGpuFixture(
                u8"Latin سلام שלום क्षि กำลัง 한글 日本語").Collect()) ==
            LoadJson(command));
  }
  ```

- [ ] **Step 3: Add script self-test contracts**

  Create both scripts with exact first line `#!/bin/zsh`, set their tracked mode
  to `100755`, and require `test -x` for each before the red gate. Both scripts
  implement `--self-test`. The automated script self-test verifies required
  executables, clean-worktree rejection, output layout, and exact command list
  without running builds. The IME script self-test creates synthetic metadata
  files only to prove schema validation rejects missing Session A/B, missing
  tested executable hashes, or missing screenshot/event-audit hashes; it never
  reports visible success.

  ```bash
  chmod 0755 scripts/qualification/run_ui_text_macos.sh \
    scripts/qualification/record_ui_text_ime.sh
  test -x scripts/qualification/run_ui_text_macos.sh
  test -x scripts/qualification/record_ui_text_ime.sh
  ```

  In `tests/CMakeLists.txt`, register both script owners:

  ```cmake
  if(APPLE)
    add_test(NAME test_ui_text_qualification_scripts_automated
        COMMAND /bin/zsh
        "${CMAKE_SOURCE_DIR}/scripts/qualification/run_ui_text_macos.sh"
        --self-test)
    add_test(NAME test_ui_text_qualification_scripts_ime
        COMMAND /bin/zsh
        "${CMAKE_SOURCE_DIR}/scripts/qualification/record_ui_text_ime.sh"
        --self-test)
  endif()
  ```

- [ ] **Step 4: Add the failing visible-editor authority test**

  Register both session-backed tests through the sole Task 1 helper; it owns
  their dependency attachment, resource barrier, and test main:

  ```cmake
  molga_add_text_test(test_text_qualification_editor
    test_text_qualification_editor.cpp)
  molga_add_text_test(test_visible_text_session_audit
    test_visible_text_session_audit.cpp)
  ```

  On Apple, also register the
  visible-window catalog production target now, before Step 5's red build, and
  create its production source path with this deliberately incomplete driver:

  ```cmake
  if(APPLE)
    add_executable(molga_visible_window_catalog
      src/Tools/VisibleWindowCatalogMain.cpp)
    target_link_libraries(molga_visible_window_catalog PRIVATE
      molga_core molga_warnings)
  endif()
  ```

  ```cpp
  #include "Platform/MacVisibleWindow.h"

  int main() {
      return molga::platform::VisibleWindowsForPid(0).empty() ? 2 : 0;
  }
  ```

  Step 5 must therefore fail at the missing `MacVisibleWindow` production API,
  not at CMake's unknown-target handling. Add the authority test:

  ```cpp
  TEST_CASE("visible editor session accepts only the reported project authority") {
      auto fixture = MaterializedQualificationEditorProjectFixture();
      molga::editor::TextQualificationEditorSessionOptions options{
          fixture.projectRoot,
          "Scenes/qualification.json",
          "Library/asset_catalog.json",
          "Qualification/trace.json",
          fixture.projectReport,
          fixture.TestedCommit(),
          fixture.ReleaseEditorSha256(),
          fixture.auditOutput,
          fixture.VisibleAuditOptions()};
      const auto authority =
          molga::editor::ValidateTextQualificationEditorSession(
              options, fixture.ReleaseEditorExecutable(), fixture.diagnostics);
      REQUIRE(authority.has_value());
      CHECK(authority->inputSetSha256 == fixture.InputSetSha256());
      CHECK(authority->projectTreeSha256 == fixture.ProjectTreeSha256());
      CHECK(authority->profileSha256 == fixture.ProfileSha256());
      CHECK(authority->sceneSha256 == fixture.SceneSha256());
      CHECK(authority->catalogSha256 == fixture.CatalogSha256());
      CHECK(authority->traceSha256 == fixture.TraceSha256());
      CHECK(authority->fontArtifacts.size() == 6u);
      CHECK(authority->canonicalArgv == fixture.ExpectedCanonicalArgv());
      CHECK(authority->canonicalArgvSha256 ==
            Sha256CanonicalStringArray(fixture.ExpectedCanonicalArgv()));

      fixture.MutateProjectFontArtifact(0);
      CHECK_FALSE(molga::editor::ValidateTextQualificationEditorSession(
          options, fixture.ReleaseEditorExecutable(), fixture.diagnostics)
                      .has_value());
      CHECK_FALSE(fs::exists(fixture.auditOutput));
  }
  ```

  `fixture.VisibleAuditOptions()` supplies
  `ReleaseEditorGameView`, the same tested commit and editor executable SHA,
  and fresh ready/event-audit paths beneath the fixture's canonical evidence
  root. `ExpectedCanonicalArgv()` includes those exact visible-session values;
  an omitted/default visible-audit aggregate is not a valid authority fixture.

  Add these focused cases in `tests/test_visible_text_session_audit.cpp`:

  ```cpp
  TEST_CASE("visible audit rejects synthetic or input-source-less text") {
      SUBCASE("synthetic") {
          auto f = VisibleAuditFixture::NativePackagedSession();
          f.events[1].provenance =
              NativeInputProvenance::QualificationSynthetic;
          CHECK_FALSE(f.CloseAndPublish());
          CHECK_FALSE(fs::exists(f.auditOutput));
      }
      SUBCASE("missing input source") {
          auto f = VisibleAuditFixture::NativePackagedSession();
          f.events[1].textInputSource.reset();
          CHECK_FALSE(f.CloseAndPublish());
          CHECK_FALSE(fs::exists(f.auditOutput));
      }
  }

  TEST_CASE("visible audit binds native Korean and Japanese composition") {
      auto f = VisibleAuditFixture::CompleteNativeSession();
      REQUIRE(f.CloseAndPublish());
      const auto audit = LoadJson(f.auditOutput);
      CHECK(audit["syntheticEventCount"] == 0);
      CHECK(audit["inputSources"] == nlohmann::json::array({
          {{"sourceId", "com.apple.inputmethod.Korean.2SetKorean"},
           {"primaryLanguage", "ko"}},
          {{"sourceId", "com.apple.inputmethod.Kotoeri.RomajiTyping.Japanese"},
           {"primaryLanguage", "ja"}}}));
      CHECK(audit["requiredObservations"] == f.ExpectedObservations());
      CHECK(ReadBytes(f.auditOutput) == CanonicalBytes(audit));
  }

  TEST_CASE("direct editor runtime transitions are authoritative audit rows") {
      auto f = VisibleAuditFixture::ReleaseEditorSession();
      const auto toRuntime = f.ApplyEditorToRuntimeTransition();
      REQUIRE(toRuntime.status == TextInputOwnerTransitionStatus::Applied);
      REQUIRE(toRuntime.boundaryPublished);
      REQUIRE(f.Recorder().RecordOwnerTransition(
          f.Window(), TextInputOwnerTransitionAuditCause::RuntimeAcquire,
          toRuntime));
      const auto releaseRuntime = f.ReleaseRuntimeOwner();
      REQUIRE(releaseRuntime.status == TextInputOwnerTransitionStatus::Applied);
      REQUIRE(releaseRuntime.boundaryPublished);
      REQUIRE(f.Recorder().RecordOwnerTransition(
          f.Window(), TextInputOwnerTransitionAuditCause::RuntimeRelease,
          releaseRuntime));
      const auto toEditor = f.AcquireEditorOwner();
      REQUIRE(toEditor.status == TextInputOwnerTransitionStatus::Applied);
      REQUIRE(toEditor.boundaryPublished);
      REQUIRE(f.Recorder().RecordOwnerTransition(
          f.Window(), TextInputOwnerTransitionAuditCause::EditorAcquire,
          toEditor));
      REQUIRE(f.CloseAndPublish());
      const auto rows = LoadJson(f.auditOutput).at("ownerTransitions");
      REQUIRE(rows.size() >= 3u);
      CHECK(rows.at(0).at("triggerSequence").is_null());
      CHECK(rows.at(0).at("boundarySequence") ==
            toRuntime.boundarySequence);
      CHECK(rows.at(0).at("boundaryPublished") == true);
      CHECK(rows.at(0).at("status") == "Applied");
      CHECK(rows.at(0).at("retiredOwner") == OwnerJson(toRuntime.retired));
      CHECK(rows.at(0).at("currentOwner") == OwnerJson(toRuntime.current));
      CHECK(rows.at(1).at("boundarySequence") ==
            releaseRuntime.boundarySequence);
      CHECK(rows.at(1).at("boundaryPublished") == true);
      CHECK(rows.at(2).at("boundarySequence") ==
            toEditor.boundarySequence);
      CHECK(rows.at(2).at("boundaryPublished") == true);
      CHECK(f.InferredTransitionCount() == 0u);
  }

  TEST_CASE("native focus transition joins only its exact host sequence") {
      auto f = VisibleAuditFixture::NativeFocusLossSession();
      const auto event = f.NativeFocusLossEvent();
      REQUIRE(event.sequence != 0u);
      REQUIRE(event.textInputOwnerTransition.has_value());
      REQUIRE(event.textInputOwnerTransition->status ==
              TextInputOwnerTransitionStatus::Applied);
      REQUIRE(event.textInputOwnerTransition->boundaryPublished);
      REQUIRE(f.Recorder().RecordOwnerTransition(
          event.windowId, TextInputOwnerTransitionAuditCause::NativeFocusLoss,
          *event.textInputOwnerTransition, event.sequence));
      REQUIRE(f.CloseAndPublish());
      const auto row = LoadJson(f.auditOutput)
                           .at("ownerTransitions").at(0);
      CHECK(row.at("triggerSequence") == event.sequence);
      CHECK(row.at("boundarySequence").get<std::uint64_t>() >=
            event.sequence);
      CHECK(row.at("boundaryPublished") == true);
      CHECK(row.at("status") == "Applied");

      auto wrong = VisibleAuditFixture::NativeFocusLossSession();
      const auto wrongEvent = wrong.NativeFocusLossEvent();
      CHECK_FALSE(wrong.Recorder().RecordOwnerTransition(
          wrongEvent.windowId,
          TextInputOwnerTransitionAuditCause::NativeFocusLoss,
          *wrongEvent.textInputOwnerTransition, wrongEvent.sequence + 1u));
      CHECK_FALSE(wrong.CloseAndPublish());
  }
  ```

- [ ] **Step 5: Run the red GPU/script gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_rendering_sdlgpu \
    test_text_qualification_editor test_visible_text_session_audit \
    molga_visible_window_catalog molga_engine molga_runtime -j
  ctest --test-dir build/debug \
    -R '^(test_rendering_sdlgpu|test_text_qualification_editor|test_visible_text_session_audit|test_ui_text_qualification_scripts_(automated|ime))$' \
    --output-on-failure
  ```

  Expected: GPU assertions fail until golden inputs/collector/source-root fields
  are complete, the editor-session authority and `MacVisibleWindow` APIs are
  missing, and both script CTests are missing. The catalog target is already a
  known build target at this red gate.

- [ ] **Step 6: Register raw RGBA fixtures with the existing LFS policy**

  Add exactly this line to `.gitattributes`, alongside Task 1's text-font/data entries:

  ```gitattributes
  tests/fixtures/text/expected/*.rgba filter=lfs diff=lfs merge=lfs -text
  ```

  Run:

  ```bash
  git check-attr filter diff merge text -- tests/fixtures/text/expected/gpu-mixed-ui.rgba
  ```

  Expected: `filter`, `diff`, and `merge` are `lfs`; `text` is `unset`. Do not store the 1920×1080 raw RGBA payload as an ordinary Git blob.

- [ ] **Step 7: Bind GPU fixture lookup to the source root**

  On `test_rendering_sdlgpu`, add exactly:

  ```cmake
  target_compile_definitions(test_rendering_sdlgpu PRIVATE
    MOLGA_TEXT_EXPECTED_ROOT=\"${CMAKE_SOURCE_DIR}/tests/fixtures/text/expected\")
  ```

  Define `TextExpectedFixturePath(std::string_view basename)` in the anonymous
  test-support namespace of `tests/test_rendering_sdlgpu.cpp`: accept only one filename from
  `{gpu-command-stream.json,gpu-mixed-ui.rgba}`, canonicalize the compile-time
  root, require the result beneath it, and never inspect `current_path()`.
  Golden read/update branches both use this helper. The update branches reject
  a symlink root or destination and write atomically to that exact source path.

- [ ] **Step 8: Define the canonical GPU command schema**

  Add `nlohmann::ordered_json CanonicalGpuCommands(const std::vector<RenderCommand>&)`. Emit root keys `schemaVersion`, `target`, `commands`, and `lifetimeAudit`; target is exactly `{width:1920,height:1080,format:"RGBA8"}`. A command emits stable kind, `UIDrawOrderKey`, optional integer scissor, immutable texture stable ID, and text glyph `{fontGuid,sourceSha256,faceIndex,glyphId}`. Exclude pointer/GPU handle/atlas UV/page identities.

- [ ] **Step 9: Project clip, overlay, and lifetime order**

  Emit nested scissor transitions in encoder order and require selection backgrounds before glyphs, composition underlines after glyphs, and caret last for the same stable input key. Emit texture retain and post-fence retire records by stable texture ID. Add forbidden-key assertions for `pointer`, `gpuHandle`, `atlasPage`, and `uv`.

- [ ] **Step 10: Generate and review the exact command JSON fixture**

  Add a test-only update branch gated by `MOLGA_UPDATE_GPU_COMMAND_GOLDEN=1`; it writes `CanonicalGpuCommands(commands).dump(2) + "\n"` to `tests/fixtures/text/expected/gpu-command-stream.json`. Run:

  ```bash
  cmake --build --preset debug --target test_rendering_sdlgpu -j
  MOLGA_UPDATE_GPU_COMMAND_GOLDEN=1 \
    build/debug/tests/test_rendering_sdlgpu \
    --test-case="*mixed text selection caret and clips*"
  python3 -m json.tool tests/fixtures/text/expected/gpu-command-stream.json >/dev/null
  git diff -- tests/fixtures/text/expected/gpu-command-stream.json
  ```

  Expected: one schema-1 JSON document containing the pinned fixture's full command/lifetime sequence. Compare every text glyph record with Task 18.1 canonical layout before accepting the file.

- [ ] **Step 11: Implement fenced 1920×1080 Metal readback**

  Render the fixed logical fixture into an `RGBA8` 1920×1080 offscreen texture, submit, wait for the exact submission fence, then copy tightly packed top-left-origin RGBA bytes to CPU. Assert backend `metal`, format `RGBA8`, row bytes `7680`, and payload size `8,294,400`; no readback may occur before the fence.

- [ ] **Step 12: Generate and verify the raw RGBA LFS fixture**

  Add a test-only update branch gated by `MOLGA_UPDATE_GPU_RGBA_GOLDEN=1` that writes the fenced bytes to `tests/fixtures/text/expected/gpu-mixed-ui.rgba`. Run on a Metal-capable host:

  ```bash
  MOLGA_UPDATE_GPU_RGBA_GOLDEN=1 \
    build/debug/tests/test_rendering_sdlgpu \
    --test-case="*mixed text selection caret and clips*"
  test "$(stat -f %z tests/fixtures/text/expected/gpu-mixed-ui.rgba)" = 8294400
  git add .gitattributes tests/fixtures/text/expected/gpu-mixed-ui.rgba
  git lfs status | rg -F 'tests/fixtures/text/expected/gpu-mixed-ui.rgba'
  git show :tests/fixtures/text/expected/gpu-mixed-ui.rgba | \
    rg -F 'version https://git-lfs.github.com/spec/v1'
  shasum -a 256 tests/fixtures/text/expected/gpu-mixed-ui.rgba
  ```

  Expected: size is exact, `git lfs status` reports the path as an LFS object, and a reviewable SHA-256 is printed.

- [ ] **Step 13: Implement the bounded RGBA comparator**

  Require equal dimensions/length, count a pixel different when any channel delta exceeds `2`, and pass only when at most `32` pixels differ. Print the first differing coordinate, channel deltas, total count, backend, and format on failure. A host without Metal records `qualifying=false` and fails the qualification target; it never converts a skip into evidence success.

- [ ] **Step 14: Consume the existing deny-network profile**

  Require the committed Task 17.5 file at exactly
  `tests/smoke/text_network_deny.sb`, re-run its owning
  `test_text_network_deny_profile` CTest, and record the profile SHA-256 before
  either qualification script launches a process. Neither script creates,
  edits, substitutes, nor copies the profile. Every copied-bundle invocation by
  `run_ui_text_macos.sh` uses `/usr/bin/sandbox-exec -D
  MOLGA_ALLOWED_WRITE_ROOT=<canonical-fresh-write-root> -f <exact-profile> --`
  as an argument vector and otherwise fails the qualification gate; it never
  runs unsandboxed. A sealed-validator invocation also receives `--report`
  naming a fresh regular non-symlink file beneath that same allowed root; a
  missing or preexisting report is fatal. A missing profile, failed
  positive/negative self-test, or profile-hash change after the test is a hard
  error.

- [ ] **Step 15: Implement reviewed clean-candidate capture**

  The script begins:

  ```bash
  set -euo pipefail
  repo_root="$(git rev-parse --show-toplevel)"
  test -z "$(git -C "$repo_root" status --porcelain)"
  tested_commit="$(git -C "$repo_root" rev-parse HEAD)"
  case "$tested_commit" in (*[!0-9a-f]*|'') exit 2;; esac
  test "${#tested_commit}" -eq 40
  review_marker="$repo_root/build/qualification/reviewed-candidate.txt"
  test -f "$review_marker"
  test "$(cat "$review_marker")" = "$tested_commit"
  evidence="$repo_root/docs/qualification/ui-text-milestone-a/$tested_commit"
  test ! -e "$evidence"
  mkdir -p "$evidence/raw" "$evidence/hashes" "$evidence/screenshots"
  printf '%s\n' "$tested_commit" > "$evidence/testedCommit.txt"
  ```

  The marker is created only by the Candidate Review Gate after blocker/high resolution and fresh candidate tests. Any pre-review or marker/HEAD mismatch exits before creating evidence.

- [ ] **Step 16: Add one logged-command primitive**

  Implement `run_logged <stable-name> <command...>` to write the exact shell-escaped argv, combined stdout/stderr, exit code, and SHA-256 sidecars beneath `raw/` and `hashes/`. It returns the command's status and never overwrites an existing stable name.

- [ ] **Step 17: Run and log the Debug suite**

  Invoke `run_logged` for `cmake --preset debug`,
  `cmake --build --preset debug -j`, and `ctest --preset debug` as three stable
  command names. Stop at the first nonzero result, write that name to
  `INCOMPLETE`, and do not continue to Release.

- [ ] **Step 18: Run and log the Release suite**

  Invoke `run_logged` separately for `cmake --preset release`,
  `cmake --build --preset release -j`, and `ctest --preset release`. Require all
  three zero before proceeding; a failure updates `INCOMPLETE` and writes no
  pass report.

- [ ] **Step 19: Run and log the ASan suite**

  Invoke `run_logged` separately for `cmake --preset asan`,
  `cmake --build --preset asan -j`, and `ctest --preset asan`. Preserve the
  exact sanitizer environment in the logged argv/environment record and stop
  on the first nonzero result.

- [ ] **Step 20: Run and log the UBSan suite**

  Invoke `run_logged` separately for `cmake --preset ubsan`,
  `cmake --build --preset ubsan -j`, and `ctest --preset ubsan`. Require zero
  exits and SHA sidecars for all three logs before package copying.

- [ ] **Step 21: Build, copy, and audit the Release qualification app**

  Run `cmake --build --preset release --target
  ui_text_qualification_package -j` through `run_logged`, then require the
  GameBuilder, sealed-validation, independent-development, and preserved-editor
  reports at
  `build/release/ui-text-qualification/{build-report,sealed-validation,
  development-report,editor-project-report}.json` and the final
  `build/release/ui-text-qualification/validation.stamp.json`.
  Require `status:"sealed"`, exit zero, `validatedBeforeSDL:true`, and live
  hashes matching all four reports and the canonical stamp before copying.
  Re-run `--validate-ui-text-qualification-outputs --existing-stamp` through
  `run_logged` at this boundary; deletion or mutation of any declared package,
  development, or editor-project byproduct must fail before `ditto`. Require
  the canonical
  development root named by `development-report.json` to be exactly
  `build/release/ui-text-qualification/development/Resources`, disjoint from
  the app, and complete; require the editor-project report to name the exact
  persistent `build/release/ui-text-qualification/editor-project`. Record each
  root's independently computed tree SHA and reject an aliased/nested root.
  Copy exactly
  `build/release/ui-text-qualification/Qualification.app` under a fresh
  `mktemp -d` root outside the checkout; record source/copied executable,
  `game.json`, catalog, runtime-manifest, scene, and trace SHA-256 equality.
  Recursively log the copied bundle's Mach-O/RPATH audit, then run its
  `Contents/MacOS/Qualification --validate-sealed-package-only --report
  <fresh-report>` through the exact parameterized deny-network profile. Absence
  of `sandbox-exec`, a missing/preexisting/noncanonical report, a different app
  basename, or any hash mismatch fails this gate.

  ```bash
  run_logged release-qualification-package \
    cmake --build --preset release --target ui_text_qualification_package -j
  release_app="$repo_root/build/release/ui-text-qualification/Qualification.app"
  build_report="$repo_root/build/release/ui-text-qualification/build-report.json"
  sealed_report="$repo_root/build/release/ui-text-qualification/sealed-validation.json"
  development_report="$repo_root/build/release/ui-text-qualification/development-report.json"
  editor_project_report="$repo_root/build/release/ui-text-qualification/editor-project-report.json"
  qualification_validation_stamp="$repo_root/build/release/ui-text-qualification/validation.stamp.json"
  development_root="$repo_root/build/release/ui-text-qualification/development/Resources"
  editor_project="$repo_root/build/release/ui-text-qualification/editor-project"
  run_logged release-qualification-live-validation \
    "$repo_root/build/release/molga_engine" \
    --validate-ui-text-qualification-outputs \
    --build-report "$build_report" \
    --sealed-validation-report "$sealed_report" \
    --development-report "$development_report" \
    --editor-project-report "$editor_project_report" \
    --existing-stamp "$qualification_validation_stamp"
  copy_root="$(/usr/bin/mktemp -d -t molga-ui-text-release.XXXXXX)"
  copied_app="$copy_root/Qualification.app"
  /usr/bin/ditto "$release_app" "$copied_app"
  copied_validation_root="$evidence/raw/copied-sealed-validation"
  test ! -e "$copied_validation_root"
  mkdir -p "$copied_validation_root"
  copied_validation_root="$(cd "$copied_validation_root" && pwd -P)"
  copied_validation_report="$copied_validation_root/report.json"
  test ! -e "$copied_validation_report"
  run_logged release-copied-sealed-validation \
    /usr/bin/sandbox-exec \
    -D "MOLGA_ALLOWED_WRITE_ROOT=$copied_validation_root" \
    -f "$repo_root/tests/smoke/text_network_deny.sb" -- \
    /usr/bin/env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C \
    "$copied_app/Contents/MacOS/Qualification" \
    --validate-sealed-package-only \
    --report "$copied_validation_report"
  ```

  Canonicalize both app paths after the copy. Require the validator report to
  be a newly created regular non-symlink file whose canonical path remains
  beneath `copied_validation_root`; parse it with an exact-key/schema check,
  require canonical bytes, exit `0`, `validatedBeforeSDL:true`, and live copied
  package/executable/resource hashes equal to the original sealed/build reports.
  The `/usr/bin/env -i` boundary is mandatory: only the shown deterministic
  `PATH`, `LANG`, and `LC_ALL` reach the copied validator, so no font,
  Homebrew, proxy, ICU/HarfBuzz, or dynamic-loader override is inherited.
  Record its SHA-256 and the exact sandbox argv SHA-256. Do not install a trap that deletes
  `copy_root`: the checkout-external bundle is a leased input for visible
  Session A and must remain a regular existing bundle through Gate B's final
  verifier (cleanup is permitted only after immutable evidence is committed).

- [ ] **Step 22: Run and hash the three-mode parity command**

  Call `run_logged text-parity` with the exact Task 18.2 `cmake -P` invocation,
  including explicit Release `EDITOR` and `DEV_RUNTIME`, checkout-external
  copied `PACKAGED_APP`, the original GameBuilder `BUILD_REPORT` and
  `SEALED_VALIDATION_REPORT`, independent `DEV_REPORT`, preserved
  `EDITOR_PROJECT_REPORT`, independently staged `DEV_ROOT`, source
  `SCENE`/`TRACE`, the live qualification validation stamp, and evidence-local
  `OUTPUT_DIR`. `DEV_ROOT` is exactly the
  canonical disjoint root from `development-report.json`; it must not equal,
  contain, or be contained by either original or copied app Resources. The
  runner applies Task 18.2's external-copy hash-equality exception only to
  `PACKAGED_APP`. Recompute and record the development tree SHA, original app
  tree SHA, copied-app tree SHA, and three identical canonical output SHAs; a
  missing report/root or alias is a hard script error.

  ```bash
  run_logged text-parity cmake \
    -DEDITOR="$repo_root/build/release/molga_engine" \
    -DDEV_RUNTIME="$repo_root/build/release/molga_runtime_dev" \
    -DPACKAGED_APP="$copied_app" \
    -DBUILD_REPORT="$build_report" \
    -DSEALED_VALIDATION_REPORT="$sealed_report" \
    -DDEV_REPORT="$development_report" \
    -DEDITOR_PROJECT_REPORT="$editor_project_report" \
    -DQUALIFICATION_VALIDATION_STAMP="$qualification_validation_stamp" \
    -DDEV_ROOT="$development_root" \
    -DSCENE="$repo_root/tests/fixtures/text/qualification/scene.json" \
    -DTRACE="$repo_root/tests/fixtures/text/qualification/trace.json" \
    -DSANDBOX_PROFILE="$repo_root/tests/smoke/text_network_deny.sb" \
    -DOUTPUT_DIR="$evidence/raw/text-parity" \
    -P "$repo_root/tests/smoke/run_text_parity.cmake"
  ```

- [ ] **Step 23: Run universal and reference performance commands**

  Use these literal commands and no environment-only output switch:

  ```bash
  run_logged performance-universal ctest \
    --test-dir "$repo_root/build/release" \
    -R '^test_text_performance$' --output-on-failure
  performance_output_root="$evidence/raw/performance"
  mkdir -p "$performance_output_root"
  run_logged performance-report \
    "$repo_root/build/release/molga_text_performance_report" \
    --qualification-project "$editor_project" \
    --editor-project-report "$editor_project_report" \
    --output-root "$performance_output_root" \
    --report performance.json \
    --tested-commit "$tested_commit"
  performance_report="$performance_output_root/performance.json"
  test -f "$performance_report"
  python3 -m json.tool "$performance_report" >/dev/null
  performance_report_sha="$(shasum -a 256 "$performance_report" | awk '{print $1}')"
  ```

  Require the report's tested commit, reporter executable SHA, project
  input-set SHA, exact schema/key sets, universal result, fingerprint, and
  timing fields to match live inputs before accepting the SHA. A nonmatching
  machine may pass universal gates but must retain
  `timingApplicable:false,timingPassed:false`.

- [ ] **Step 24: Run and hash the GPU qualification command**

  Use this literal Release executable and one exact doctest filter:

  ```bash
  gpu_test="$repo_root/build/release/tests/test_rendering_sdlgpu"
  gpu_case='mixed text selection caret and clips match command golden'
  test -x "$gpu_test"
  gpu_executable_sha256="$(shasum -a 256 "$gpu_test" | awk '{print $1}')"
  run_logged gpu-golden "$gpu_test" \
    --test-case="$gpu_case" --no-skip=true --success=true
  gpu_command_golden="$repo_root/tests/fixtures/text/expected/gpu-command-stream.json"
  gpu_rgba_golden="$repo_root/tests/fixtures/text/expected/gpu-mixed-ui.rgba"
  gpu_command_sha256="$(shasum -a 256 "$gpu_command_golden" | awk '{print $1}')"
  gpu_rgba_sha256="$(shasum -a 256 "$gpu_rgba_golden" | awk '{print $1}')"
  ```

  Make that one test print exactly one canonical marker:

  ```text
  MOLGA_GPU_QUALIFICATION backend=metal format=RGBA8 fence=passed tolerance=passed atlas=passed lifetime=passed
  ```

  The runner requires the log's
  doctest summary to report exactly one selected test case, one passed, zero
  failed, and zero skipped, plus exactly one marker with every listed value.
  Require backend/format, fence, tolerance, atlas, and lifetime assertions and
  both post-run golden hashes. A non-Metal backend, skip, second selected case,
  missing/duplicate marker, or changed golden fails this gate.

- [ ] **Step 25: Write the automated evidence summary**

  Only after Steps 14–24 pass, write canonical
  `automated-verification.json` containing `testedCommit`, Release
  editor/package executable SHA-256 values, every raw-log/sidecar hash, parity
  SHA, universal result, exact fingerprint, timing applicability/result, GPU
  result, exact performance-report path/SHA/reporter executable SHA/input-set
  SHA, copied-app audit result, the fresh copied sealed-validation report's
  canonical path/SHA/argv SHA/allowed-root/exit/pre-SDL result, sandbox-profile
  SHA, the canonical Release `molga_visible_window_catalog` path and live
  executable SHA-256, packaged canonical
  sandbox-audit path/SHA/argv SHA/allowed-root/exit result, and all four build
  authority reports plus their SHA-256 values. Record the independently staged
  development root and its live tree SHA separately from the original and
  copied app resource-tree SHAs. Record the preserved editor-project canonical
  path, tree SHA, input-set SHA, profile/scene/catalog/trace hashes, and six
  ProjectLibrary artifact hashes. Record the exact qualification validation
  stamp path/SHA and require its four report hashes and three tree/input hashes
  to equal the live reports before publishing the summary. Remove `INCOMPLETE`
  last; a nonreference
  machine retains `timingPassed:false` and does not satisfy Release Gate A.

  Bind GPU evidence with exact fields `gpuExecutablePath`,
  `gpuExecutableSha256`, `gpuArgv` (the four literal array elements above),
  `gpuArgvSha256`, `gpuLogSha256`, `gpuSelectedCaseCount:1`,
  `gpuSkippedCaseCount:0`, `gpuBackend:"metal"`,
  `gpuCommandGoldenSha256`, and `gpuRgbaGoldenSha256`. Recompute these live
  values before serializing; a human-authored `gpuResult:true` is insufficient.

  The summary has exact canonical absolute string keys `originalAppPath` and
  `copiedAppPath`, plus `originalAppTreeSha256`, `copiedAppTreeSha256`, and the
  two executable SHA fields. Both paths must exist, be disjoint, name
  `Qualification.app`, and match the recorded live trees at summary publish;
  no consumer reconstructs a random `mktemp` path.

- [ ] **Step 26: Freeze the visible audit API and canonical key sets**

  Create `VisibleTextSessionAudit.h` with these production signatures; include
  the authoritative Task 12 `UIEventDispatchRecord` and `UIFrameResult` types
  rather than redeclaring either one:

  ```cpp
  namespace molga::qualification {
  enum class VisibleTextSessionKind : std::uint8_t {
      PackagedApp,
      ReleaseEditorGameView,
  };
  struct VisibleTextSessionAuditOptions {
      VisibleTextSessionKind kind;
      std::string testedCommit;
      std::string expectedExecutableSha256;
      std::filesystem::path evidenceRoot;
      std::filesystem::path readyOutput;
      std::filesystem::path eventAuditOutput;
  };
  class VisibleTextSessionAuditRecorder final {
  public:
      static std::optional<VisibleTextSessionAuditRecorder> Create(
          const VisibleTextSessionAuditOptions&, const std::filesystem::path&,
          text::TextDiagnosticSink&);
      bool RecordNativeEvent(const platform::NativeInputEvent&);
      bool RecordIngestedEvent(const ui::UIInputEvent&);
      bool RecordFrame(const ui::UIFrameResult&);
      bool RecordOwnerTransition(
          WindowId, platform::TextInputOwnerTransitionAuditCause,
          const platform::TextInputOwnerTransition&,
          std::optional<std::uint64_t> triggerSequence = std::nullopt);
      bool PublishReady(std::uint64_t pid,
                        std::uint32_t captureWindowId);
      bool FinalizeAndPublish();
  };
  }
  ```

  `ready.json` has exactly `schemaVersion`, `session`, `testedCommit`,
  `executableSha256`, `pid`, and `windowId`. `event-audit.json` has exactly
  `schemaVersion`, `session`, `testedCommit`, `executableSha256`,
  `readyRecordSha256`, `windowId`, `nativeEventCount`, `syntheticEventCount`,
  `inputSources`, `events`, `ownerTransitions`, `requiredObservations`, and
  `closedCleanly`; both files require integer `schemaVersion:1` and reject every
  unknown/missing key.
  `inputSources` is sorted unique `{sourceId,primaryLanguage}`. Each ordered
  event has exactly `auditOrdinal`, `sequence`, `nativeType`, `provenance`, `inputSourceId`,
  `inputSourceLanguage`, `textPhase`, `textUtf8Sha256`, `textByteCount`,
  `ownerBefore`, `ownerAtIngest`, `ownerAfter`, `canonicalTarget`, `delivered`,
  and `resultingFocus`; nullable fields serialize as JSON `null`. Raw composed
  text, pointer addresses, GPU handles, and clocks are forbidden. Each ordered
  transition has exactly `auditOrdinal`, `transitionIndex`, `windowId`,
  `cause`, `triggerSequence`, `boundarySequence`, `boundaryPublished`, `status`,
  `retiredOwner`, `currentOwner`, and `cancelEngineComposition`.
  `boundaryPublished` is the authoritative boolean copied from
  `TextInputOwnerTransition`; it is never derived from status or boundary
  movement. Owner values use the complete
  runtime `{kind,runtimeTarget,generation}` value or `null`; they are copied from the
  authoritative transition and never inferred from adjacent text stamps.
  Serialize provenance as the closed strings `Unknown`, `NativeSdl`, or
  `QualificationSynthetic`; text phase is JSON `null`, `TextEditing`, or
  `TextCommit`. Transition `cause` uses exactly the six enum spellings from
  Step 29, and `status` uses exactly `NoChange`, `Applied`, `Rejected`,
  `PlatformFailed`, or `GenerationExhausted`. A present owner `kind` is exactly
  `EditorImGui` or `RuntimeUITextInput`; the all-invalid `None` value serializes
  as a whole-owner JSON `null`. Reject an enum outside a closed table rather
  than serializing an integer or an implementation-defined name.
  This event audit is noncanonical runtime evidence and expressly retains those
  runtime identities/generations; Task 18.1's parity exporter continues to
  exclude them from content-stable canonical JSON.

  `ownerBefore` and `ownerAfter` come only from the authoritative per-event
  fields added to `UIEventDispatchRecord` in Steps 27 and 30. `ownerAtIngest`
  comes only from the ingested `UIInputEvent` text stamp. The recorder never
  samples `TextInputArbiter::CurrentOwner()`, reconstructs an owner from an
  adjacent event/transition, or substitutes one of these three boundaries for
  another.

  The root `windowId` in both ready/audit documents is the unsigned macOS
  capture window ID passed to `PublishReady`; it is the ID later accepted by
  the visible-window catalog and `screencapture`. In contrast,
  `ownerTransitions[].windowId` is the engine's typed `molga::WindowId` copied
  from the arbiter transition boundary. These two ID domains are never
  compared, cast into one another, or reconstructed. The recorder retains the
  native/UI engine window ID internally and requires it to agree within every
  sequence join even though the event row deliberately omits that runtime
  field.

- [ ] **Step 27: Add native provenance without creating a qualification path**

  Define the shared values once in `Platform/NativeInputProvenance.h`; both the
  Task 12 native/UI records include that header rather than redeclaring them:

  ```cpp
  enum class NativeInputProvenance : std::uint8_t {
      Unknown,
      NativeSdl,
      QualificationSynthetic,
  };
  struct NativeTextInputSourceSnapshot {
      std::string sourceId;
      std::string primaryLanguage;
  };
  // NativeInputEvent-only additions in namespace molga::platform:
  NativeInputProvenance provenance = NativeInputProvenance::Unknown;
  std::optional<NativeTextInputSourceSnapshot> textInputSource;
  // Identical additions to UIInputEvent and UIEventDispatchRecord:
  molga::platform::NativeInputProvenance provenance =
      molga::platform::NativeInputProvenance::Unknown;
  std::optional<molga::platform::NativeTextInputSourceSnapshot>
      textInputSource;
  // UIEventDispatchRecord-only authoritative event-boundary additions:
  std::optional<molga::platform::TextInputOwnerStamp> ownerBefore;
  std::optional<molga::platform::TextInputOwnerStamp> ownerAfter;
  ```

  The regular SDL mapper sets `NativeSdl`; the canonical qualification injector
  sets `QualificationSynthetic`; hand-built events remain `Unknown`. Preserve
  provenance/source plus the authoritative process-global nonzero `sequence`
  one-for-one through `NativeInputEvent`, `UIInputEvent`, the dispatch
  accumulator, and final `UIEventDispatchRecord`. Leave
  regular SDL `traceOrdinal == 0`; that separate field remains exclusive to
  automated injected replay and is not a visible-audit join key. Add a focused
  case where changing a native event to
  `QualificationSynthetic` makes `FinalizeAndPublish()` return false and leaves
  no audit file. Add a mapping/accumulator test asserting all three fields are
  identical at native, UI, and dispatch boundaries. In that test also require
  the dispatch record's owner-before/after optionals to equal the two exact
  arbiter boundary stamps supplied by the production event loop; Task 18.1
  canonical export intentionally ignores provenance/source and both owner
  boundary fields and remains byte-identical.

- [ ] **Step 28: Snapshot the real macOS input source at text ingest**

  Implement `MacTextInputSource.mm` with
  `std::optional<NativeTextInputSourceSnapshot>
  CurrentMacTextInputSource(TextDiagnosticSink&)`. On Apple, use
  `TISCopyCurrentKeyboardInputSource`, `kTISPropertyInputSourceID`, and the
  first `kTISPropertyInputSourceLanguages` value; copy both UTF-8 strings before
  releasing the source. In top-level `CMakeLists.txt`, before either source is
  registered, configure Objective-C++ (idempotently after Task 17.5) and keep
  the repo's C++17 boundary exact:

  ```cmake
  if(APPLE)
    enable_language(OBJCXX)
    find_library(MOLGA_CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(MOLGA_COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(MOLGA_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_sources(molga_core PRIVATE
      src/Platform/MacTextInputSource.mm
      src/Platform/MacVisibleWindow.mm)
    set_target_properties(molga_core PROPERTIES
      OBJCXX_STANDARD 17
      OBJCXX_STANDARD_REQUIRED YES
      OBJCXX_EXTENSIONS NO)
    target_link_libraries(molga_core PRIVATE
      "${MOLGA_CARBON_FRAMEWORK}"
      "${MOLGA_COCOA_FRAMEWORK}"
      "${MOLGA_COREGRAPHICS_FRAMEWORK}")
  endif()
  ```

  The catalog executable was registered in Step 4; do not add or link it a
  second time here. This step supplies the platform sources, frameworks, and
  C++17 Objective-C++ implementation that its red driver consumes.

  Do not register either `.mm` on non-Apple. Both headers provide a C++17
  inline fallback returning `nullopt`/an empty vector, and
  `test_visible_text_session_audit` has a non-Apple compile/runtime case that
  calls both fallbacks and verifies visible qualification remains false.

  In `Bootstrap.cpp`, stamp the snapshot on every native `TextEditing` and
  `TextCommit` event before the native observer, owner-at-ingest snapshot, or
  UI dispatch. A missing/empty ID, language other than exact `ko`/`ja` for the
  required observation, or source change between update and its commit makes
  visible evidence fail closed. Add the focused macOS adapter test with an
  injected CoreFoundation-reader callback so no global input-source mutation is
  needed.

- [ ] **Step 29: Define the nullable owner-transition audit sink**

  Put the cycle-free optional sink contract in
  `Platform/TextInputOwnerTransitionAudit.h`; Platform/UI/Editor code never
  includes the qualification recorder:

  ```cpp
  namespace molga::platform {
  enum class TextInputOwnerTransitionAuditCause : std::uint8_t {
      EditorAcquire, EditorRelease, RuntimeAcquire, RuntimeRelease,
      RuntimeAreaChange, NativeFocusLoss
  };
  using TextInputOwnerTransitionAuditSink = std::function<void(
      WindowId, TextInputOwnerTransitionAuditCause,
      const TextInputOwnerTransition&,
      std::optional<std::uint64_t> triggerSequence)>;
  }

  // UISystem.h
  void SetTextInputOwnerTransitionAuditSink(
      platform::TextInputOwnerTransitionAuditSink);
  UIEventHandlerResult ApplyTextInputOwnerTransition(
      const platform::TextInputOwnerTransition&,
      platform::TextInputOwnerTransitionAuditCause,
      std::optional<std::uint64_t> triggerSequence = std::nullopt);

  // ImGuiTextInputBridge.h
  void SetTextInputOwnerTransitionAuditSink(
      platform::TextInputOwnerTransitionAuditSink);
  ```

  An empty `std::function` is the only ordinary-runtime value. Add a focused
  default-runtime test proving no callback occurs when the sink is empty. The
  validated visible-session entry point installs one lambda that calls its
  recorder and keeps the first recorder failure sticky; reset it before
  recorder destruction.

- [ ] **Step 30: Join native, ingested, and dispatch rows by host sequence**

  Implement the recorder with one collision-checked map keyed by the host's
  nonzero process-global `sequence`. `RecordNativeEvent` accepts only the event observed by the
  shared `EngineHost`; `RecordIngestedEvent` records the Task 12 stamped
  `ownerAtIngest`; `RecordFrame` consumes the authoritative optional-target
  `UIEventDispatchRecord`, including its owner-before/after values, and
  resulting focus. In the sole production event loop, `UISystem` snapshots the
  arbiter's complete current owner stamp immediately before handling each
  planned event and again after every handler and that event's exact owner
  transition application completes, then places those two values in the
  returned dispatch record. No-owner is `nullopt`; a present value must be a
  complete valid stamp. The accumulator receives these caller-owned boundary
  values and cannot query or infer them. One native text event must join
  exactly one ingested event and one delivered/skipped dispatch record, with
  equal native/inner-text/UI/dispatch sequence values. A
  targetless window-focus record serializes `canonicalTarget:null`; a targeted
  text commit serializes the stable key object. Zero, duplicate, decreasing,
  replayed, or missing sequences,
  synthetic/unknown provenance, inconsistent source, or a parallel audit-only
  dispatch is fatal. Add focused missing/duplicate/zero/decreasing sequence,
  targetless/targeted row, owner acquisition/release between the two boundary
  snapshots, and missing/invalid owner-boundary cases before wiring transition
  callbacks.

- [ ] **Step 31: Wire runtime and focus transitions at the UI apply boundary**

  Maintain one recorder-owned `auditOrdinal` across native, dispatch, and
  transition callbacks. `UISystem::ApplyTextInputOwnerTransition` invokes the
  sink only after it accepts the exact transition as either a publishing apply
  or a valid nonpublishing no-op. It passes the complete transition, including
  `status`, `boundarySequence`, and `boundaryPublished`, without reconstruction.
  A rejected transition value may be an accepted nonpublishing no-op; a
  validation/application failure produces no callback. `GameViewWindow` passes the
  exact direct-runtime cause with each `SetOwner`/`SetArea`/`Release` result;
  the ordered native-focus path passes `NativeFocusLoss` and its event sequence
  to that same method. Neither Game View nor the focus-loss path calls the
  recorder separately. Add a focused callback-order test proving a failed
  production apply produces no audit row.

- [ ] **Step 32: Wire editor transitions at the ImGui bridge boundary**

  `ImGuiTextInputBridge` invokes its injected sink immediately after its editor
  `SetOwner`/`SetArea`/`Release` result is successfully consumed, passing that
  complete result including `boundaryPublished`; it never recreates a
  transition from current-owner state. Install this nullable sink
  only for a validated visible session, share the recorder's sticky failure,
  and clear it before recorder destruction. Add focused acquire/release/area
  callback tests and prove ordinary editor operation keeps an empty sink and
  unchanged behavior.

- [ ] **Step 33: Enforce owner-transition causality and observation rules**

  Direct transitions require `triggerSequence:null`. Native focus loss requires
  the exact nonzero event sequence and a field-for-field match, including
  `status`, `boundarySequence`, `boundaryPublished`, owners, and cancellation,
  to that event's attached `TextInputOwnerTransition`. A native-focus row that
  publishes a boundary also requires `boundarySequence >= triggerSequence`.

  Only `boundaryPublished:true` rows participate in the recorder's per-window
  published-boundary monotonic/conflict check, keyed by the exact nonzero
  `WindowId`. Reject a second record for the same
  transition, a decreasing published boundary, an equal-but-different published
  boundary, wrong cause, or an audit call before the production apply returns.
  A field-identical published replay is still a duplicate audit call and is
  rejected. A valid `boundaryPublished:false` row may retain the prior boundary
  and must not advance or replace published-boundary state; this applies to any
  upstream `NoChange`, `Rejected`, `PlatformFailed`, or `GenerationExhausted`
  result that is legitimately marked nonpublishing. Record its exact status and
  `boundaryPublished:false` in schema version 1, but never let it satisfy a
  required ownership observation.

  Session B's `editor-to-runtime` observation comes only from an `EditorImGui`
  retired owner plus `status:Applied`, `boundaryPublished:true`
  `RuntimeAcquire`; `runtime-to-editor` requires actual Applied/published
  `RuntimeRelease` followed by Applied/published `EditorAcquire`; and
  `native-focus-loss` requires the joined event-attached Applied/published row.
  A publishing `GenerationExhausted` fail-closed retirement is audited but
  cannot satisfy those observations. Never synthesize these claims from
  adjacent `ownerAtIngest` stamps or current-owner snapshots. Add one focused
  test for each accepted observation and each rejected wrong cause/sequence/
  duplicate/published-boundary ordering, plus a nonpublishing repeated-boundary
  case that serializes `boundaryPublished:false` without advancing the ledger.

- [ ] **Step 34: Publish ready and event-audit files atomically**

  `PublishReady` atomically writes and reparses canonical `ready.json` only
  after the normal window, scene, UI focus shell, and text services are live.
  `FinalizeAndPublish` first requires a clean window close and every session
  observation, then atomically writes/reparses `event-audit.json`; no partial
  file survives failure. Inject one write/rename failure per output and assert
  the prior final file remains byte-identical with no sibling temporary file.

- [ ] **Step 35: Add the packaged visible-session CLI contract**

  `runtime_main.cpp` accepts this complete branch in addition to its normal
  sealed-package startup arguments:

  ```text
  Qualification --ui-text-visible-session packaged-app
    --visible-evidence-root <absolute-existing-candidate-session-a-directory>
    --visible-ready-output <absolute-root/session-a-ready.json>
    --visible-event-audit-output <absolute-root/session-a-event-audit.json>
    --visible-tested-commit <40-lowercase-hex>
    --visible-expected-executable-sha256 <64-lowercase-hex>
  ```

  Reject an unknown, missing, duplicate, relative, symlinked, or escaping
  argument before `EngineInit`; hash the running executable and require equality.
  After normal startup, attach the recorder to the existing `EngineHost`
  observer and feed the same ingested batch and returned five-argument
  `UISystem::ProcessFrame` result. Add an argv parser test proving an output
  outside `visible-evidence-root` exits `2` before creating a window.

- [ ] **Step 36: Freeze Session A evidence and observation names**

  Session A's audit requires, in this order,
  `korean-composition-update`, `korean-composition-commit`,
  `japanese-composition-update`, `japanese-composition-commit`, and
  `focus-cancel`. Its final `session-a.json` has exactly:

  ```json
  {
    "schemaVersion":1,
    "session":"packaged-app",
    "testedCommit":"40-lowercase-hex",
    "executableSha256":"64-lowercase-hex",
    "readyRecordPath":"raw/visible/session-a/session-a-ready.json",
    "readyRecordSha256":"64-lowercase-hex",
    "eventAuditPath":"raw/visible/session-a/session-a-event-audit.json",
    "eventAuditSha256":"64-lowercase-hex",
    "screenshotManifestPath":"screenshots/packaged-app/manifest.json",
    "screenshotManifestSha256":"64-lowercase-hex",
    "visuals":["mixed-script","nested-clip","caret-selection","hidpi"]
  }
  ```

  The operator cannot type these claims into the JSON: the script derives all
  fields from live ready/audit/screenshot artifacts and rejects an audit whose
  native/source observations do not satisfy the exact list.

- [ ] **Step 37: Bind every screenshot to a PID-owned macOS window**

  Add `MacVisibleWindow.{h,mm}` with callback-testable
  `CaptureWindowForNativeHandle(void*)` and
  `VisibleWindowsForPid(std::uint64_t)`, backed by the Cocoa window number and
  `CGWindowListCopyWindowInfo`. Return exact
  `{windowId,ownerPid,title,onScreen,layer,bounds}` and accept only layer-zero,
  on-screen, positive-area windows whose `kCGWindowOwnerPID` equals the
  requested live PID. `EngineHost::CaptureWindowId()` uses the native-handle
  function. Thus `PublishReady(pid, windowId)` records the real CG window ID,
  not the unrelated SDL/engine ID.

  Complete the already registered `molga_visible_window_catalog` target by
  replacing Step 4's red driver in `VisibleWindowCatalogMain.cpp`; do not add a
  second CMake target. Its exact CLI
  `--pid <positive-decimal> --output <absolute-canonical-json>` atomically emits
  the sorted canonical records. Reject unknown/duplicate/missing arguments,
  zero/overflow PID, a relative/noncanonical output, a symlinked/non-directory
  parent, or a preexisting output with exit `2`; a missing live PID, empty
  result, duplicate window ID, or conflicting owner exits `3`; publication
  failure exits `4`; only a durable canonical file returns `0`. The file is
  schema version 1 with exact root keys `{schemaVersion,pid,windows}`. `windows`
  is sorted by unsigned `windowId` and each row has exactly
  `{windowId,ownerPid,title,onScreen,layer,bounds}`; `bounds` has exact signed
  integer `x`,`y` and positive unsigned `width`,`height`. Every row requires
  `ownerPid == pid`, `onScreen:true`, `layer:0`, and positive area. Serialize as
  `ordered_json.dump(2) + "\n"`, reparse/exact-key check and byte-compare before
  the atomic rename. Multiple valid PID-owned windows are retained—the caller,
  not the CLI, applies the detached-title uniqueness rule.
  In `record_ui_text_ime.sh`, implement
  `capture_visible_png <session-pid> <session> <stable-name> <window-id>`.
  Require the PID from the live ready record to still exist. For every call,
  allocate the next stable capture ordinal and a fresh absent path
  `<session-root>/window-catalog/<four-digit-ordinal>-<stable-name>.json`;
  reject a symlink/path alias or preexisting output. Rehash the exact absolute
  `molga_visible_window_catalog` executable against
  `automated-verification.json`, invoke only
  `molga_visible_window_catalog --pid <pid> --output <fresh-absolute-path>`,
  and retain the resulting regular non-symlink canonical schema-1 file. Parse
  and byte-check it, require that exact window ID to be owned by that PID, then run
  `/usr/sbin/screencapture -l "$window_id" -x "$unique_tmp_png"`; require PNG magic and a
  regular non-symlink file created after session start and before process exit,
  read positive pixel width/height with `/usr/bin/sips`, and atomically rename
  it beneath the candidate's exact `screenshots/<session>/` directory. Accept
  only Session A names `mixed-script`, `nested-clip`, `caret-selection`,
  `hidpi` and Session B names `letterbox`, `crop`, `detached-viewport`, once
  each. All Session A captures use the exact ready-record window ID. Session B
  `letterbox` and `crop` use the ready Game View ID; immediately before the
  detached capture, query the catalog and require exactly one additional
  PID-owned window with the pinned title `Qualification — Game View (Detached)`,
  then pass that returned ID. The detached capture's own retained fresh catalog
  must independently contain exactly that one titled additional window and the
  same ID; the preliminary resolver output alone is not evidence. Never use
  `screencapture -i`, import a
  caller-supplied image, or accept a window owned by another process.

  Atomically write canonical `manifest.json` with exact root keys
  `schemaVersion`, `session`, `testedCommit`, `executableSha256`,
  `windowCatalogExecutableSha256`, and `screenshots`; each ordered screenshot
  has exactly `stableName`,
  `relativePath`, `sha256`, `byteSize`, `width`, `height`, `ownerPid`,
  `windowId`, `windowCatalogPath`, `windowCatalogSha256`, and `capturedAtUtc`.
  `windowCatalogPath` is the candidate-root-relative retained query path and
  its SHA is recomputed after capture; the catalog executable SHA is recomputed
  before manifest publication. `ownerPid` must equal the ready PID and
  `windowId` must equal the row selected from that exact retained catalog.
  `capturedAtUtc` is RFC 3339 UTC and is checked against the
  ready/process-close interval but is excluded from every content-derived
  authority digest. The script
  self-test uses a fake capture executable and proves stale, duplicate,
  wrong-PID, foreign-window, ambiguous-detached-window, non-PNG, interactive
  capture, preexisting/reused catalog output, catalog schema/hash mutation,
  catalog-executable replacement, and escaping output cases fail without
  publishing a manifest.

- [ ] **Step 38: Define the visible editor-session authority API**

  Create `TextQualificationEditorSession.h`:

  ```cpp
  namespace molga::editor {
  struct TextQualificationEditorSessionOptions {
      std::filesystem::path projectRoot;
      std::filesystem::path sceneRelativePath;
      std::filesystem::path catalogRelativePath;
      std::filesystem::path traceRelativePath;
      std::filesystem::path projectReport;
      std::string testedCommit;
      std::string expectedEditorExecutableSha256;
      std::filesystem::path authorityOutput;
      qualification::VisibleTextSessionAuditOptions visibleAudit;
  };
  struct TextQualificationEditorFontAuthority {
      std::string fontGuid;
      std::filesystem::path projectLibraryArtifact;
      std::string artifactSha256;
  };
  struct TextQualificationEditorSessionAuthority {
      std::filesystem::path canonicalProjectRoot;
      std::string testedCommit;
      std::string editorExecutableSha256;
      std::string inputSetSha256;
      std::string projectTreeSha256;
      std::string profileSha256;
      std::string sceneSha256;
      std::string catalogSha256;
      std::string traceSha256;
      std::vector<TextQualificationEditorFontAuthority> fontArtifacts;
      std::vector<std::string> canonicalArgv;
      std::string canonicalArgvSha256;
      std::filesystem::path canonicalAuthorityOutput; // runtime-only
  };
  std::optional<TextQualificationEditorSessionAuthority>
  ValidateTextQualificationEditorSession(
      const TextQualificationEditorSessionOptions&,
      const std::filesystem::path& editorExecutable,
      text::TextDiagnosticSink&);
  bool RevalidateTextQualificationEditorSessionAfterClose(
      const TextQualificationEditorSessionAuthority&,
      text::TextDiagnosticSink&);
  }
  ```

- [ ] **Step 39: Validate the preserved project and canonical editor argv**

  In `GameBuilder`, add a strict canonical parser for the Task 18.2
  `editor-project-report.json`; both the build target and visible-session seam
  use this parser. Before any `EngineInit`, canonicalize and require the exact
  reported project root plus exact relative
  `Scenes/qualification.json`, `Library/asset_catalog.json`, and
  `Qualification/trace.json`. Rehash the project profile, scene, catalog,
  trace, and all six `ProjectLibrary` artifacts; rescan the project and require
  matching GUID/locator/hash records. Hash the running Release editor and
  require the expected automated-report SHA. Build the exact canonical argv
  array and its SHA; reject extra/missing/duplicate options, path aliases,
  symlinks, wrong tested commit, or any mismatch without creating
  `authorityOutput`.

  Freeze `session-b-editor-authority.json` as canonical schema version `1`
  with exactly these root keys, in order:
  `{schemaVersion,session,testedCommit,editorExecutableSha256,
  canonicalProjectRoot,inputSetSha256,projectTreeSha256,profileSha256,
  sceneSha256,catalogSha256,traceSha256,fontArtifacts,canonicalArgv,
  canonicalArgvSha256,postSessionAuthorityMatched}`. `session` is exactly
  `release-editor-game-view`; `fontArtifacts` is the sorted six-entry array
  whose rows have exactly
  `{fontGuid,projectLibraryArtifact,artifactSha256}`. Reject unknown/missing
  keys and noncanonical bytes. `canonicalAuthorityOutput` is the already
  validated absolute output path used by the close-time API and is not a
  separately serialized key; it is already bound as the value following
  `--authority-output` in `canonicalArgv`.

- [ ] **Step 40: Wire the dedicated Release editor qualification CLI**

  `main.cpp` recognizes only this complete branch before generic project-path
  handling:

  ```text
  molga_engine --ui-text-visible-editor-session
    --project <preserved-editor-project>
    --scene Scenes/qualification.json
    --catalog Library/asset_catalog.json
    --trace Qualification/trace.json
    --project-report <editor-project-report.json>
    --tested-commit <40-lowercase-hex>
    --expected-editor-sha256 <64-lowercase-hex>
    --authority-output <session-authority.json>
    --visible-evidence-root <absolute-existing-candidate-session-b-directory>
    --visible-ready-output <absolute-root/session-b-ready.json>
    --visible-event-audit-output <absolute-root/session-b-event-audit.json>
  ```

  Validate authority before window/SDL. On success, use the regular
  `Project::Open`, asset rescan, `SceneDocument::Open`, and editor Play/Game
  View paths for that exact startup scene, while disabling project writes and
  watcher-driven reimport for this qualification session. Atomically write the
  preflight authority report only after normal editor initialization succeeds.
  Attach the same recorder to the regular editor `EngineHost`, and record the
  real `GameViewWindow` ingested events and five-argument `ProcessFrame` result;
  editor-shell events alone cannot satisfy runtime observations. Publish the
  ready record only after Play/Game View is live.
  The initial canonical authority report has
  `postSessionAuthorityMatched:false`. At clean close,
  `RevalidateTextQualificationEditorSessionAfterClose` uses only
  `canonicalAuthorityOutput`, rehashes every bound file/artifact, reparses the
  existing false report, and atomically replaces it with otherwise
  byte-identical authority fields plus
  `postSessionAuthorityMatched:true`. A mismatch or write/rename failure leaves
  the false report in place, makes the process fail, and cannot become Session
  B evidence. Add success, post-init mutation, and injected close-time publish
  failure tests that prove no caller reconstructs the output path from argv.

- [ ] **Step 41: Define Session B evidence exactly**

  Freeze this complete Session B schema before launching the editor; arrays are
  ordered and the six `fontArtifacts` entries are sorted by `fontGuid`:

  ```json
  {
    "schemaVersion":1,
    "session":"release-editor-game-view",
    "testedCommit":"40-hex",
    "editorExecutableSha256":"64-hex",
    "authorityReportSha256":"64-hex",
    "canonicalProjectRoot":"absolute-canonical-path",
    "projectTreeSha256":"64-hex",
    "inputSetSha256":"64-hex",
    "profileSha256":"64-hex",
    "sceneSha256":"64-hex",
    "catalogSha256":"64-hex",
    "traceSha256":"64-hex",
    "fontArtifacts":[
      {"fontGuid":"12121212121212121212121212121212","projectLibraryArtifact":"Library/Imported/Fonts/64-hex.sfnt","artifactSha256":"64-hex"},
      {"fontGuid":"13131313131313131313131313131313","projectLibraryArtifact":"Library/Imported/Fonts/64-hex.sfnt","artifactSha256":"64-hex"},
      {"fontGuid":"44444444444444444444444444444444","projectLibraryArtifact":"Library/Imported/Fonts/64-hex.sfnt","artifactSha256":"64-hex"},
      {"fontGuid":"55555555555555555555555555555555","projectLibraryArtifact":"Library/Imported/Fonts/64-hex.sfnt","artifactSha256":"64-hex"},
      {"fontGuid":"66666666666666666666666666666666","projectLibraryArtifact":"Library/Imported/Fonts/64-hex.sfnt","artifactSha256":"64-hex"},
      {"fontGuid":"77777777777777777777777777777777","projectLibraryArtifact":"Library/Imported/Fonts/64-hex.sfnt","artifactSha256":"64-hex"}
    ],
    "canonicalArgv":[
      "absolute-editor",
      "--ui-text-visible-editor-session",
      "--project","absolute-preserved-editor-project",
      "--scene","Scenes/qualification.json",
      "--catalog","Library/asset_catalog.json",
      "--trace","Qualification/trace.json",
      "--project-report","absolute-editor-project-report.json",
      "--tested-commit","40-hex",
      "--expected-editor-sha256","64-hex",
      "--authority-output","absolute-session-b-editor-authority.json",
      "--visible-evidence-root","absolute-candidate-session-b-directory",
      "--visible-ready-output","absolute-session-b-ready.json",
      "--visible-event-audit-output","absolute-session-b-event-audit.json"
    ],
    "canonicalArgvSha256":"64-hex",
    "postSessionAuthorityMatched":true,
    "readyRecordPath":"raw/visible/session-b/session-b-ready.json",
    "readyRecordSha256":"64-hex",
    "eventAuditPath":"raw/visible/session-b/session-b-event-audit.json",
    "eventAuditSha256":"64-hex",
    "screenshotManifestPath":"screenshots/release-editor-game-view/manifest.json",
    "screenshotManifestSha256":"64-hex",
    "events":["korean-composition-update","korean-composition-commit","japanese-composition-update","japanese-composition-commit","editor-to-runtime","runtime-to-editor","native-focus-loss"],
    "visuals":["letterbox","crop","detached-viewport"],
    "screenshotStableNames":["letterbox","crop","detached-viewport"]
  }
  ```

  Each `64-hex` value is filled from the reviewed candidate's report at runtime;
  the validator requires exactly these six GUID authorities from
  `editor-project-report.json`, byte-for-byte equal after sorting. The report
  binds the preserved qualification project, not an arbitrary editor project
  or app Resources directory. The operator performs real Korean and Japanese
  composition/commit, letterbox, crop, detached ImGui viewport, and
  editor→runtime→editor ownership/focus transfer.

- [ ] **Step 42: Validate visible-session launch authority**

  `record_ui_text_ime.sh` reads `testedCommit`, both executable hashes, the
  copied-app path, preserved project path, visible-window catalog path/hash, and
  project/report hashes only from a passing `automated-verification.json`.
  Canonicalize `originalAppPath` and
  `copiedAppPath` from that report,
  require both bundles still exist, rehash their complete trees and executable,
  and compare every value to the summary/build report before creating either
  session root. A missing/stale/replaced copied path fails before launch; the
  script self-test deletes and mutates its fake copied app and requires both
  cases to fail without session JSON. Before either launch, recompute the
  selected executable, the exact Release visible-window catalog executable,
  project tree, profile, scene, catalog, trace, and six artifact hashes and
  require equality with the automated/project reports. Every catalog query uses
  that canonical executable by absolute path; PATH lookup or a caller override
  is forbidden.
  Refuse path aliases, unknown arguments, concurrent sessions, or a synthetic
  replacement.

- [ ] **Step 43: Launch, capture, and close visible Session A**

  Create a fresh evidence-local Session A root. Launch only the exact
  checkout-external copied app in the background with all six Step 35
  arguments; wait at most 30 seconds for its canonical ready record and require
  matching PID/executable/commit before prompting for captures. Run exactly:

  ```bash
  sandbox_profile="$repo_root/tests/smoke/text_network_deny.sb"
  original_app="$(read_exact_json_string \
    "$evidence/automated-verification.json" originalAppPath)"
  copied_app="$(read_exact_json_string \
    "$evidence/automated-verification.json" copiedAppPath)"
  session_a_root="$evidence/raw/visible/session-a"
  mkdir -p "$session_a_root"
  /usr/bin/sandbox-exec \
    -D "MOLGA_ALLOWED_WRITE_ROOT=$session_a_root" \
    -f "$sandbox_profile" -- \
    "$copied_app/Contents/MacOS/Qualification" \
    --ui-text-visible-session packaged-app \
    --visible-evidence-root "$session_a_root" \
    --visible-ready-output "$session_a_root/session-a-ready.json" \
    --visible-event-audit-output "$session_a_root/session-a-event-audit.json" \
    --visible-tested-commit "$tested_commit" \
    --visible-expected-executable-sha256 "$packaged_executable_sha256" &
  session_a_pid=$!
  wait_for_ready "$session_a_pid" "$session_a_root/session-a-ready.json" 30
  session_a_window_id="$(read_ready_window_id \
    "$session_a_root/session-a-ready.json" "$session_a_pid")"
  capture_visible_png "$session_a_pid" packaged-app mixed-script \
    "$session_a_window_id"
  capture_visible_png "$session_a_pid" packaged-app nested-clip \
    "$session_a_window_id"
  capture_visible_png "$session_a_pid" packaged-app caret-selection \
    "$session_a_window_id"
  capture_visible_png "$session_a_pid" packaged-app hidpi \
    "$session_a_window_id"
  wait "$session_a_pid"
  ```

  Require exit `0`, a closed-cleanly audit, and the four screenshot hashes
  before creating Session B's root.

- [ ] **Step 44: Launch, capture, and close visible Session B**

  Construct the exact preserved-project Release editor argv and run:

  ```bash
  sandbox_profile="$repo_root/tests/smoke/text_network_deny.sb"
  session_b_root="$evidence/raw/visible/session-b"
  editor_authority="$session_b_root/session-b-editor-authority.json"
  mkdir -p "$session_b_root"
  /usr/bin/sandbox-exec \
    -D "MOLGA_ALLOWED_WRITE_ROOT=$session_b_root" \
    -f "$sandbox_profile" -- \
    "$release_editor" --ui-text-visible-editor-session \
    --project "$editor_project" \
    --scene Scenes/qualification.json \
    --catalog Library/asset_catalog.json \
    --trace Qualification/trace.json \
    --project-report "$editor_project_report" \
    --tested-commit "$tested_commit" \
    --expected-editor-sha256 "$release_editor_sha256" \
    --authority-output "$editor_authority" \
    --visible-evidence-root "$session_b_root" \
    --visible-ready-output "$session_b_root/session-b-ready.json" \
    --visible-event-audit-output "$session_b_root/session-b-event-audit.json" &
  session_b_pid=$!
  wait_for_ready "$session_b_pid" "$session_b_root/session-b-ready.json" 30
  session_b_window_id="$(read_ready_window_id \
    "$session_b_root/session-b-ready.json" "$session_b_pid")"
  capture_visible_png "$session_b_pid" release-editor-game-view letterbox \
    "$session_b_window_id"
  capture_visible_png "$session_b_pid" release-editor-game-view crop \
    "$session_b_window_id"
  detached_window_id="$(resolve_unique_owned_window_id \
    "$session_b_pid" 'Qualification — Game View (Detached)')"
  capture_visible_png "$session_b_pid" release-editor-game-view \
    detached-viewport "$detached_window_id"
  wait "$session_b_pid"
  ```

  Require exit `0`, a closed-cleanly audit, and all three screenshot hashes.
  Persist the exact canonical argv array and canonical JSON SHA. Record
  distinct start/end timestamps and PIDs only as audit metadata, outside the
  authority hash.

- [ ] **Step 45: Make visible verification fail closed**

  After both interactive runs, parse the editor-produced authority report and
  require its tested commit, editor SHA, canonical project root, input-set,
  profile/scene/catalog/trace hashes, sorted six font authorities, canonical
  argv, canonical argv SHA, and `postSessionAuthorityMatched:true` to equal the
  preflight/project reports. Recompute all live hashes once more, including the
  authority report itself, before copying those fields into Session B. Check
  both session executable hashes against the automated report and every
  required enum string exactly once. Reparse each ready record, event audit,
  and screenshot manifest canonically; rehash every referenced live PNG and
  retained window-catalog JSON, require each catalog's exact PID/window row,
  canonical bytes, and live catalog-executable SHA bound by the
  manifest/automated report; and
  require `syntheticEventCount:0`, exact `NativeSdl` provenance, exact Korean
  and Japanese source IDs/languages, clean close, and the session-specific
  observation list. Atomically write
  `visible-ime-verification.json` only when both complete session documents
  pass. Exit nonzero on a post-close mutation, report/argv mismatch,
  cancellation, synthetic-only audit, missing screenshot/catalog query,
  catalog mutation/reuse, wrong tested commit, or incomplete input source.

- [ ] **Step 46: Document evidence and claim limits**

  `docs/qualification/ui-text-milestone-a/README.md` defines the hashed directory layout, required reports, two sessions, candidate/evidence commit relationship, nonmatching-machine behavior, and explicit nonclaims. It states that screenshots without event audits and synthetic SDL tests without real sessions are insufficient.

- [ ] **Step 47: Run the green GPU/script gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_rendering_sdlgpu \
    test_text_qualification_editor test_visible_text_session_audit \
    molga_visible_window_catalog molga_engine molga_runtime -j
  ctest --test-dir build/debug \
    -R '^(test_rendering_sdlgpu|test_text_qualification_editor|test_visible_text_session_audit|test_ui_text_qualification_scripts_(automated|ime))$' \
    --output-on-failure
  scripts/qualification/run_ui_text_macos.sh --self-test
  scripts/qualification/record_ui_text_ime.sh --self-test
  ```

  Expected: GPU fixture/tolerance passes on Metal-capable test hosts; both self-tests pass while still rejecting incomplete visible evidence.

- [ ] **Step 48: Commit the complete qualification harness before evidence**

  ```bash
  git add .gitattributes CMakeLists.txt tests/CMakeLists.txt \
    src/Tools/TextQualification.cpp \
    src/Tools/VisibleTextSessionAudit.h \
    src/Tools/VisibleTextSessionAudit.cpp \
    src/Platform/MacTextInputSource.h \
    src/Platform/MacTextInputSource.mm \
    src/Platform/MacVisibleWindow.h \
    src/Platform/MacVisibleWindow.mm \
    src/Platform/NativeInputProvenance.h \
    src/Platform/TextInputOwnerTransitionAudit.h \
    src/Tools/VisibleWindowCatalogMain.cpp \
    src/Platform/NativeInputEvent.h \
    src/UI/UIInputEvent.h src/UI/UISystem.h \
    src/Core/Bootstrap.h src/Core/Bootstrap.cpp \
    src/Core/SmokeReport.h src/Core/SmokeReport.cpp \
    src/Editor/TextQualificationEditorSession.h \
    src/Editor/TextQualificationEditorSession.cpp \
    src/Editor/GameBuilder.h src/Editor/GameBuilder.cpp \
    src/Editor/ImGuiTextInputBridge.h \
    src/Editor/ImGuiTextInputBridge.cpp \
    src/Editor/Windows/GameViewWindow.h \
    src/Editor/Windows/GameViewWindow.cpp \
    src/UI/UISystem.cpp src/main.cpp src/runtime_main.cpp \
    tests/test_text_qualification_editor.cpp \
    tests/test_visible_text_session_audit.cpp \
    tests/test_rendering_sdlgpu.cpp \
    tests/fixtures/text/expected/gpu-command-stream.json \
    tests/fixtures/text/expected/gpu-mixed-ui.rgba \
    scripts/qualification/run_ui_text_macos.sh \
    scripts/qualification/record_ui_text_ime.sh \
    docs/qualification/ui-text-milestone-a/README.md
  git commit -m "test: add macOS UI text qualification harness"
  ```

---

## Candidate Review Gate

This gate precedes every evidence run.

- [ ] Invoke `superpowers:requesting-code-review` for Tasks 1–18 code, tests, scripts, trace, and golden inputs. Give the reviewer the approved design and all seven subplans.
- [ ] Resolve every blocker/high finding in focused commits and rerun the owning tests immediately.
- [ ] Run the complete automated candidate boundary:

  ```bash
  cmake --preset debug && cmake --build --preset debug -j && ctest --preset debug
  cmake --preset release && cmake --build --preset release -j && ctest --preset release
  cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan
  cmake --preset ubsan && cmake --build --preset ubsan -j && ctest --preset ubsan
  git diff --check
  test -z "$(git status --porcelain)"
  git rev-parse HEAD
  ```

  Expected: all four suites pass, worktree is clean, and the printed 40-character HEAD is the reviewed candidate. No qualification evidence directory exists for that hash yet.

- [ ] After the reviewer reports no blocker/high finding and the complete boundary passes, bind the local review marker to that exact clean commit:

  ```bash
  mkdir -p build/qualification
  git rev-parse HEAD > build/qualification/reviewed-candidate.txt
  test "$(cat build/qualification/reviewed-candidate.txt)" = "$(git rev-parse HEAD)"
  test -z "$(git status --porcelain)"
  ```

  Expected: the marker names the reviewed candidate and does not change the tracked worktree. The evidence runner refuses any other HEAD.

---

## Release Gate A: Automated parity, package, performance, and GPU evidence

- [ ] From the clean reviewed candidate, run:

  ```bash
  scripts/qualification/run_ui_text_macos.sh
  ```

- [ ] Verify the evidence root's `testedCommit.txt` equals the candidate HEAD captured before the script created files.
- [ ] Verify Debug, Release, ASan, UBSan, three-mode byte/SHA parity, copied-app no-font/Homebrew/network/path audit, terminal exit-4 cases, universal zero-allocation/64 MiB gates, reference timing applicability/result, and GPU golden result each have raw log and SHA sidecar.
- [ ] If the machine fingerprint is not the exact reference, mark the timing gate open. Do not edit the report into a pass.

## Release Gate B: Two real visible IME sessions

- [ ] With the same unmodified candidate binaries, run:

  ```bash
  scripts/qualification/record_ui_text_ime.sh
  ```

- [ ] Complete Session A in the checkout-external copied packaged `.app` with real Korean and Japanese input sources.
- [ ] Complete Session B in the exact Release editor/Game View with real Korean and Japanese input sources, letterbox/crop, detached viewport, and editor/runtime ownership transfer.
- [ ] Run the script's final verifier and require both session JSON files, event audit hashes, screenshot hashes, executable hashes, and `visible-ime-verification.json` to pass. A missing human/input source leaves the gate open.

## Release Gate C: Bind and commit immutable evidence

- [ ] Resolve exactly one evidence directory for the reviewed `HEAD`; never
  concatenate `testedCommit.txt` files from a glob:

  ```bash
  evidence_root="docs/qualification/ui-text-milestone-a"
  candidate="$(git rev-parse HEAD)"
  evidence_dir="$evidence_root/$candidate"
  test -d "$evidence_dir"
  test -f "$evidence_dir/testedCommit.txt"
  test "$(cat "$evidence_dir/testedCommit.txt")" = "$candidate"

  matching_dirs="$(
    find "$evidence_root" -mindepth 2 -maxdepth 2 -type f \
      -name testedCommit.txt -exec sh -c '
        candidate=$1
        shift
        for file do
          test "$(cat "$file")" = "$candidate" && dirname "$file"
        done
      ' sh "$candidate" {} +
  )"
  test "$(printf '%s\n' "$matching_dirs" | awk 'NF { n++ } END { print n + 0 }')" -eq 1
  test "$matching_dirs" = "$evidence_dir"
  test -f "$evidence_dir/visible-ime-verification.json"
  ```

- [ ] Before changing design maturity or staging anything, prove every tracked
  path outside the exact evidence namespace still equals the reviewed candidate:

  ```bash
  git diff --exit-code "$candidate" -- . \
    ':(exclude)docs/qualification/ui-text-milestone-a/**'
  ```

  Expected: exit `0` with no output. This covers `CMakeLists.txt`, `cmake/`,
  `external/`, `.gitmodules`, `.gitattributes`, `resources/`, `src/`, `tests/`,
  scripts, trace inputs, and golden inputs; it is deliberately not a hand-picked
  path list.

- [ ] Fail closed on every tracked or untracked status entry except files under
  the exact candidate evidence directory. Define the reusable allowlist check:

  ```bash
  assert_qualification_status_allowlist() {
    local record status path
    while IFS= read -r -d '' record; do
      status="${record:0:2}"
      path="${record:3}"
      case "$path" in
        "$evidence_dir"/*)
          test "$status" = "??" || {
            printf 'evidence path is not a new untracked file: %s %s\n' \
              "$status" "$path" >&2
            return 1
          }
          ;;
        *)
          printf 'candidate drift outside evidence directory: %s %s\n' \
            "$status" "$path" >&2
          return 1
          ;;
      esac
    done < <(git status --porcelain=v1 -z --untracked-files=all)
  }
  assert_qualification_status_allowlist
  ```

  Expected: exit `0`; an untracked source, resource, CMake, external,
  submodule-control, attribute, trace, or golden file fails just like a tracked
  edit.

- [ ] If either candidate comparison fails, do not reuse evidence. Commit the
  revised candidate, remove only
  `docs/qualification/ui-text-milestone-a/$candidate/` when it is uncommitted
  invalid evidence for that candidate, and rerun Candidate Review plus Gates A
  and B.

- [ ] When every gate is complete, update only
  `docs/plans/2026-08-20-ui-text-production-backbone-design.md` from `NOT
  STARTED` to the exact verified state; retain all nonclaims and independently
  open gates. Then recheck that no code/input drift was hidden by the intended
  documentation edit:

  ```bash
  design_doc="docs/plans/2026-08-20-ui-text-production-backbone-design.md"
  git diff --exit-code "$candidate" -- . \
    ':(exclude)docs/qualification/ui-text-milestone-a/**' \
    ":(exclude)$design_doc"

  assert_qualification_status_allowlist_with_design() {
    local record status path
    while IFS= read -r -d '' record; do
      status="${record:0:2}"
      path="${record:3}"
      case "$path" in
        "$evidence_dir"/*)
          test "$status" = "??" || return 1
          ;;
        "$design_doc")
          test "$status" = " M" || return 1
          ;;
        *)
          printf 'non-allowlisted candidate drift: %s %s\n' \
            "$status" "$path" >&2
          return 1
          ;;
      esac
    done < <(git status --porcelain=v1 -z --untracked-files=all)
  }
  assert_qualification_status_allowlist_with_design
  ```

  Expected: exit `0`; the complete status set is exactly new files beneath the
  one candidate evidence directory plus one unstaged design-maturity edit.

- [ ] Commit evidence and the maturity update separately from candidate code:

  ```bash
  test "$candidate" = "$(git rev-parse HEAD)"
  git add "$evidence_dir"
  git add "$design_doc"
  unexpected_staged=0
  while IFS= read -r -d '' path; do
    case "$path" in
      "$evidence_dir"/*|"$design_doc") ;;
      *)
        printf 'unexpected staged path: %s\n' "$path" >&2
        unexpected_staged=1
        ;;
    esac
  done < <(git diff --cached --name-only -z)
  test "$unexpected_staged" -eq 0
  git commit -m "docs: record macOS UI text qualification evidence"
  ```

  After this commit, `HEAD` is the evidence commit and `testedCommit` intentionally names its reviewed parent candidate. Never rewrite `testedCommit` to the evidence commit without rerunning the tested binaries.

---

## Final Completion Gate

- [ ] Confirm every code commit from subplans `01`–`07` is present in dependency order and `git status --short --branch` has no unintended files.
- [ ] Confirm fresh Debug, Release, ASan, UBSan, package, GPU, and copied-app results.
- [ ] Confirm editor, development runtime, and copied `.app` canonical JSON bytes and SHA-256 are identical.
- [ ] Confirm each Section 11 terminal action has the exact continue/block/fail/exit-4 result.
- [ ] Confirm universal warm-static allocation/reshape and 64 MiB atlas gates, and reference timing if applicable.
- [ ] Confirm distinct visible Session A and Session B real Korean/Japanese evidence bound to the reviewed candidate.
- [ ] Mark Milestone A qualified only if every preceding item is true. This still does not establish overall engine commercial readiness or any declared non-goal.
