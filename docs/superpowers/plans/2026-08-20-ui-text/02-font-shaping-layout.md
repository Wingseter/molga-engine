# Font, Shaping, Atlas, and Paragraph Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Milestones 4–8 as one deterministic packaged-font pipeline from static font import through grapheme-atomic fallback, HarfBuzz shaping, GPU-safe glyph residency, authoritative final-line layout, and an atomic UILabel/TextRenderer2D production migration.

**Architecture:** Imported static font bytes are immutable resources selected through a deterministic authored family graph and retained by every shaped glyph. HarfBuzz owns glyph IDs/advances/clusters, the atlas owns only raster images with current/in-flight frame pins, and `TextLayoutService` re-shapes each accepted line before immutable layouts feed both UI and world consumers through one renderer path.

**Tech Stack:** C++17, doctest, nlohmann/json, pinned HarfBuzz 14.3.1 with ICU Unicode funcs, text-owned `stb_truetype` rasterizer, SDL3/SDL_GPU Metal renderer, signed 26.6 fixed point.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md)

**Master plan:** [`docs/superpowers/plans/2026-08-20-ui-text-production-backbone.md`](../2026-08-20-ui-text-production-backbone.md), Milestones 4–8.

## Global Constraints

- Complete [`01-dependencies-unicode.md`](01-dependencies-unicode.md) first. This plan consumes its ready ICU lifetime, portable dependency-contract SHA, byte/source mappings, ICU analysis items, and checked signed 26.6 units.
- The approved design is authoritative. Stop for a design amendment before changing public behavior, fallback order, supported font scope, failure policy, or cache identity.
- Preserve unrelated worktree changes. Run `git status --short --branch` before each task and stage only the named files.
- Production accepts static TTF/OTF outline faces only. Reject variable tables `fvar`, `gvar`, `CFF2` and color tables `COLR`, `CPAL`, `CBDT`, `CBLC`, `sbix`, and the four-byte `SVG\x20` tag. No CoreText/system-font fallback, runtime download, or non-default variation is allowed.
- Font candidates sort lexicographically by `(stylePenalty, abs(stretch-target), abs(weight-target), authoredFaceIndex, fontGuid, faceIndex)`. Fallback families traverse authored-order depth-first with first-visit cycle termination.
- Fallback is extended-grapheme atomic. Probe-shape the complete analysis context; only target-intersecting `.notdef` rejects a face; re-shape the context after selection. Join controls/default ignorables do not require independent cmap glyphs, and variation selectors use explicit UVS mapping or default presentation.
- HarfBuzz buffers use `hb_icu_get_unicode_funcs()`, explicit direction/script/language/size/features, `HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS`, original UTF-8 byte starts as clusters, and exact BOT/EOT flags. `stb_truetype` never shapes, kerns, falls back, or breaks lines.
- Every ready Unicode/font/family/shaping/atlas/cache/layout test executable is registered through Task 2.2's `molga_add_text_test`, uses only `MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT`, and depends on `molga_text_test_runtime_resources`. Explicit not-ready tests are separate generic executables with no session/root; no test stops or restores a successful ICU lifetime in process.
- When no face exists, procedural metrics are exact: grapheme advance is `1 em`, ascent is `3/4 em`, and descent is `1/4 em`, all computed from canonical 26.6 font size with `Fixed26_6::CheckedMulDiv` and its half-away-from-zero rule. Empty/missing-family layout never depends on a host font.
- `FontFaceResource` owns immutable bytes and the exact face/raster object. `ResolvedFace`, every `ShapedGlyph`, every `TextLayout`, and pending atlas work retain that same resource; hot reload never causes an old layout to reopen a GUID or rasterize against new bytes.
- A successful font import publishes a byte-identical content-addressed artifact at `Library/Imported/Fonts/<sha256>.sfnt` before its project catalog record; `sourceSha256 == artifactSha256` is mandatory. `FontRepository` reads only a validated `ProjectLibrary` locator or a sealed-package `PackagedResource` locator authorized by the verified runtime manifest, never the authoring source.
- Font vertical metrics are imported once as exact SFNT integers `{uint16 unitsPerEm,int16 ascender,int16 descender,int16 lineGap}` and scaled only through checked 26.6 `CheckedMulDiv`; rasterizer/float metrics are forbidden in layout.
- The atlas key is `{fontGuid,fontRevision,faceIndex,pixelSize,rasterScaleKey,variationKey(default),renderMode(monochrome),glyphId}`. Default resident budget is exactly `64 * 1024 * 1024` bytes; there is no unlimited mode.
- Current-collection and in-flight GPU pages cannot be evicted. A submitted frame releases one deduplicated token per page only after its fence signals; fence acquisition failure retains tokens until a successful GPU-idle shutdown drain. If idle wait fails, abort before device/resource teardown so no texture destructor observes a dead device. Saturation emits procedural monochrome tofu plus `TEXT_ATLAS_EXHAUSTED`.
- Paragraph shaping only proposes breaks. Every accepted final line is re-shaped with real line BOT/EOT context; backtrack if its final advance exceeds the constraint. Never slice paragraph glyph arrays or cross an unsafe-to-break boundary; shape ellipsis in final-line context.
- Shape/cache identity collision-checks original byte length and bytes and includes every design Section 10 field, including portable dependency-contract SHA, fallback descendant generation, locale/resolved break locale, exact embedding level, feature ranges, BOT/EOT, constraints, overflow, ellipsis, max lines, alignment, and visual revision.
- Final shape keys copy both resolved locales, both break-rule identities, and the nonwrapping analysis generation from immutable `UnicodeAnalysis`; public cache headers express the cluster choice only as `TextClusterPolicy` and contain no HarfBuzz macro/type.
- Warm lookup uses a collision-safe request index built after content-derived family resolution but before ICU/HarfBuzz. Layout-producing invalid UTF-8/missing glyphs cache immutable validation facts and re-emit contextual diagnostics on every hit; later package validation inspects those facts instead of assuming a cache hit is clean.
- UILabel and TextRenderer2D must switch together in Task 8.2. Until that task, compatibility APIs may remain but no production consumer is partly migrated. Task 8.2 removes silent ASCII fallback and legacy codepoint shaping/measurement in one commit.

## Prerequisite Contract

- The complete exit gate of `01-dependencies-unicode.md` is freshly green from its committed HEAD.
- The immutable font corpus and licenses match the Task 1 manifest; the variable Korean TTF remains rejection-only.
- `TextRuntimeDependencies::IsReady()` is true before constructing a resolver, HarfBuzz object, layout service, or font repository runtime handle.

## Exit Contract

- Static font/family import, immutable resource ownership, fallback, shaping, glyph atlas, GPU retirement, final-line layout, hit testing, and cache tests pass in Debug; shaping/layout tests pass ASan and UBSan.
- Every atlas page referenced by a command stays alive through a signaled submission fence, and resident monochrome page bytes never exceed 64 MiB.
- UILabel and TextRenderer2D share one shaped/layout result, preserve their distinct bounds/world-transform contracts, and use procedural tofu plus typed diagnostics for missing text.
- Empty paragraphs/trailing separators retain metric-bearing logical lines, and cold/warm recoverable-invalid results expose identical validation facts and diagnostics without warm ICU/HarfBuzz calls.
- `rg` proves production paths no longer call `GenerateBuiltinFont`, stb shaping advance/kerning, `GetTextWidth`, or `DecodeUtf8`; the complete Debug suite is green after the atomic migration.
- Milestone 8 does not claim UI snapshot/input/IME/package qualification; those start in later subplans.

## File Responsibility Map

| Unit | Authoritative files | Responsibility |
|---|---|---|
| Font asset/import | `src/Assets/FontAsset.*`, `src/Assets/FontFamilyAsset.*`, importer files | Static-face metadata, license state, ordered family authoring, typed import diagnostics |
| Immutable resources | `src/Text/FontRepository.*`, `src/Rendering/FontFace.*` | One verified immutable byte/face resource per content generation |
| Fallback/shaping | `src/Text/FontFamilyResolver.*`, `src/Text/TextShapingService.*` | Deterministic family closure, grapheme-atomic face selection, HarfBuzz output |
| Raster residency | `src/Rendering/FontAtlas.*`, `src/Rendering/GpuRetirementQueue.*`, renderer files | Glyph-ID atlas, 64 MiB page LRU, collection/in-flight lifetime |
| Paragraph layout | `src/Text/TextLayoutTypes.h`, `src/Text/TextLayoutService.*`, `src/Text/TextHitTesting.*`, `src/Text/TextLayoutCache.*` | Final-line reshaping, visual order, caret/selection, collision-safe cache |
| Production consumers | `src/Rendering/TextRenderer.*`, `UILabel.*`, `TextRenderer2D.*`, `UISystem.cpp` | Sole shared rendered-text path and legacy schema preservation |

---

### Task 4.1: Persist typed import diagnostics and import static font faces

**Prerequisite:** Task 1 provides locked static and rejection fixtures; Task 2 provides `TextDiagnostic`.

**Files:**

- Create: `src/Assets/FontAsset.h`
- Create: `src/Assets/FontAsset.cpp`
- Create: `src/Assets/FontArtifactStore.h`
- Create: `src/Assets/FontArtifactStore.cpp`
- Create: `tests/test_font_assets.cpp`
- Create: `tests/AssetDatabaseTestAuthority.h`
- Modify: `src/Core/Importers/Importer.h`
- Modify: `src/Core/Importers/FontImporter.h`
- Modify: `src/Core/Importers/FontImporter.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Core/PersistentStorage.h`
- Modify: `src/Core/PersistentStorage.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `tests/test_animation.cpp`
- Modify: `tests/test_asset_catalog.cpp`
- Modify: `tests/test_asset_database.cpp`
- Modify: `tests/test_asset_reference_migration.cpp`
- Modify: `tests/test_audio.cpp`
- Modify: `tests/test_editor_property_descriptor.cpp`
- Modify: `tests/test_editor_undo_dirty.cpp`
- Modify: `tests/test_font.cpp`
- Modify: `tests/test_post_process.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `tests/test_tilemap.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `molga::Sha256File`, locked font/license fixtures, existing importer metadata/catalog path.
- Produces: `ImportResult::importDiagnostics`, `AssetRecord::{importDiagnostics,fontArtifact}`, `AssetCatalogMode`, `FontSlant`, exact signed-SFNT `FontDesignMetrics`, `FontLicenseMetadata`, `VerifiedFontArtifact`, `FontArtifactStore::{Publish,ReadVerified}`, `FontAsset::FromRecord`, and `FontImporter` version `2` with settings overload.

- [x] **Step 1: Add the failing static-font and diagnostic round-trip tests.**

  ```cpp
  TEST_CASE("font importer emits package-grade static face metadata") {
      nlohmann::json settings = {
          {"faceIndex", 0}, {"weight", 400}, {"stretchPercent", 100},
          {"slant", "Upright"}, {"redistributableConfirmed", true},
          {"licenseKind", "OFL-1.1"},
          {"licenseAssetGuid", "88888888888888888888888888888888"}};
      const auto result = molga::FontImporter().Import(
          MOLGA_TEXT_LATIN_FONT, settings);
      REQUIRE(result.success);
      CHECK(result.metadata["font"]["sourceSha256"] == MOLGA_TEXT_LATIN_SHA256);
      CHECK(result.metadata["font"]["staticOutline"] == true);
      CHECK(result.metadata["font"]["faceIndex"] == 0);
  }
  ```

- [x] **Step 1a: Add the failing persisted-diagnostic round-trip test.**

  ```cpp
  TEST_CASE("typed import diagnostics survive catalog reload") {
      ImportResult failed;
      failed.importDiagnostics.push_back({
          molga::text::TextDiagnosticCode::FontInvalid,
          molga::text::TextSeverity::Blocker, "font-import", "variable face",
          "use a static outline face", "font-guid", 0, "FontAsset", {12, 16}});
      const AssetRecord restored = RoundTripAssetRecord(failed);
      REQUIRE(restored.importDiagnostics.size() == 1);
      CHECK(std::string_view(StableTextDiagnosticCode(
                restored.importDiagnostics[0].code)) == "TEXT_FONT_INVALID");
      CHECK(restored.importDiagnostics[0].sourceByteRange.begin == 12);
      CHECK(restored.importDiagnostics[0].remediation == "use a static outline face");
  }
  ```

- [x] **Step 1b: Add the failing selected-face/rejected-table test.**

  ```cpp
  TEST_CASE("font importer rejects invalid face variable and color tables") {
      const auto base = ValidStaticFontSettings();
      auto wrongFace = base;
      wrongFace["faceIndex"] = 1;
      CHECK_FALSE(molga::FontImporter().Import(
          MOLGA_TEXT_LATIN_FONT, wrongFace).success);
      const auto variable = molga::FontImporter().Import(
          MOLGA_TEXT_VARIABLE_FONT, base);
      CHECK_FALSE(variable.success);
      CHECK(HasDiagnostic(variable.importDiagnostics,
            molga::text::TextDiagnosticCode::FontInvalid));
      const ScopedTempFont color = AddSfntTableDirectoryEntry(
          MOLGA_TEXT_LATIN_FONT, "COLR");
      const auto colorResult = molga::FontImporter().Import(color.path, base);
      CHECK_FALSE(colorResult.success);
      CHECK(HasDiagnostic(colorResult.importDiagnostics,
            molga::text::TextDiagnosticCode::FontInvalid));
  }
  ```

- [x] **Step 1c: Add the failing unknown-diagnostic-code test.**

  ```cpp
  TEST_CASE("unknown persisted diagnostic code fails catalog load closed") {
      auto json = ValidAssetRecordJson();
      json["importDiagnostics"][0]["code"] = "TEXT_UNKNOWN_FROM_FUTURE";
      std::string error;
      CHECK_FALSE(AssetRecordFromJson(json, error));
      CHECK(error.find("unknown diagnostic code") != std::string::npos);
  }
  ```

- [x] **Step 1d: Implement the synthetic SFNT-table fixture.** `AddSfntTableDirectoryEntry` writes a temporary structurally valid zero-length table entry, updates `numTables` plus header/table checksums, and removes the temporary file through RAII; it never edits a committed font.

- [x] **Step 1e: Register the font-asset fixture contract.** Add `molga_add_text_test(test_font_assets test_font_assets.cpp)` from Task 2.2 and define `MOLGA_TEXT_LATIN_FONT`, `MOLGA_TEXT_ARABIC_FONT`, `MOLGA_TEXT_CJK_FONT`, `MOLGA_TEXT_VARIABLE_FONT`, and `MOLGA_TEXT_LATIN_SHA256` on that target from the exact Task 1 manifest paths/values; no test discovers a host font. The common main initializes the staged ICU runtime before this importer/resource target executes.

- [x] **Step 1f: Add the failing TTC selected-face/bounds test.**

  ```cpp
  TEST_CASE("TTC imports the authored static face and rejects out of range") {
      const ScopedTempFont ttc = BuildTwoFaceTtc(
          MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
      auto secondSettings = ValidStaticFontSettings();
      secondSettings["faceIndex"] = 1;
      const auto second = molga::FontImporter().Import(ttc.path, secondSettings);
      REQUIRE(second.success);
      CHECK(second.metadata["font"]["faceIndex"] == 1);
      CHECK(CoverageContains(second.metadata["font"]["coverage"], U'س'));
      secondSettings["faceIndex"] = 2;
      const auto outside =
          molga::FontImporter().Import(ttc.path, secondSettings);
      CHECK_FALSE(outside.success);
      CHECK(HasDiagnostic(outside.importDiagnostics,
            molga::text::TextDiagnosticCode::FontInvalid));
  }
  ```

- [x] **Step 1g: Implement the two-face TTC fixture.** `BuildTwoFaceTtc` reads only the two locked source files, emits big-endian `ttcf` version `0x00010000` with `numFonts=2`, places both face directories at distinct four-byte-aligned offsets, rewrites every copied table offset to its absolute TTC-file offset, and preserves the unchanged table bytes/checksums (including the SFNT-defined zeroed-adjustment `head` checksum rule). It writes one RAII temporary file and never changes committed bytes.

- [x] **Step 1h: Add the failing design-metrics persistence test.** For the locked Latin face, compare imported values against a test parser that reads only big-endian `head.unitsPerEm` plus `hhea.ascender`, `hhea.descender`, and `hhea.lineGap`, then round-trip the record and require exact integer equality; the canonical JSON contains integers and no float metric field.

- [x] **Step 1i: Add the failing malformed-design-metrics table.** From a valid temporary SFNT, independently write each case `{unitsPerEm=0}`, `{unitsPerEm=16385}`, `{ascender=0}`, `{descender=1}`, `{ascender<=descender}`, `{lineGap=-1}`, and truncated `head`/`hhea` metric fields; repair table checksums after each value mutation. Every real import returns `success=false` with one `FontInvalid` and publishes neither a catalog generation nor an artifact path.

- [x] **Step 1j: Add the failing immutable-artifact publication test.** Import a temporary project font through the real `AssetDatabase`, then assert the successful record has storage `ProjectLibrary`, path `Library/Imported/Fonts/<sourceSha256>.sfnt`, `artifactSha256 == sourceSha256`, exact byte size, byte-for-byte equality to the source as read during import, and no temporary/journal sibling. Reimporting identical bytes returns the same locator/hash without rewriting the file.

- [x] **Step 2: Run the font-import red gate.**

  Run: `cmake --preset debug && cmake --build --preset debug --target test_font_assets -j`

  Expected: compile FAIL because typed import records and `FontAsset` do not exist.

- [x] **Step 3: Add typed diagnostic storage.** Add `std::vector<molga::text::TextDiagnostic> importDiagnostics` to `ImportResult` and `AssetRecord` without removing legacy `error`.

- [x] **Step 3a: Serialize every diagnostic field.** Persist the stable code string—not its numeric enum—plus severity, subsystem, message, remediation, asset GUID, scene object ID, component type, and byte-range begin/end.

- [x] **Step 3b: Deserialize stable codes fail-closed.** Parse only through `ParseStableTextDiagnosticCode`; reject the complete asset record for an unknown/numeric/aliased code rather than dropping that diagnostic.

- [x] **Step 3c: Preserve legacy importer compatibility.** `AssetDatabase` fills only an empty diagnostic asset GUID and retains legacy `error` as a human-readable compatibility summary.

- [x] **Step 4: Add the exact persisted font model.**

  ```cpp
  enum class FontSlant : std::uint8_t { Upright, Italic, Oblique };
  struct FontLicenseMetadata {
      bool redistributableConfirmed = false;
      std::string licenseKind;
      std::string copyright;
      std::string licenseAssetGuid;
  };
  struct FontDesignMetrics {
      std::uint16_t unitsPerEm = 0;
      std::int16_t ascender = 0;
      std::int16_t descender = 0;
      std::int16_t lineGap = 0;
  };
  enum class FontArtifactStorage : std::uint8_t {
      ProjectLibrary,
      PackagedResource
  };
  struct FontArtifactLocator {
      FontArtifactStorage storage = FontArtifactStorage::ProjectLibrary;
      std::filesystem::path relativePath;
      bool operator==(const FontArtifactLocator&) const;
  };
  struct VerifiedFontArtifact {
      FontArtifactLocator locator;
      std::string sourceSha256;
      std::string artifactSha256;
      std::uint64_t byteSize = 0;
  };
  struct FontAsset {
      std::string guid;
      std::string sourceSha256;
      FontArtifactLocator artifactLocator;
      std::string artifactSha256;
      std::uint64_t artifactByteSize = 0;
      std::uint32_t faceIndex = 0;
      std::uint16_t weight = 400;
      std::uint16_t stretchPercent = 100;
      FontSlant slant = FontSlant::Upright;
      std::uint64_t contentRevision = 0;
      FontDesignMetrics designMetrics;
      std::vector<std::pair<char32_t, char32_t>> coverage;
      FontLicenseMetadata license;
      static std::optional<FontAsset> FromRecord(
          const AssetRecord&, std::string& errorOut);
  };
  ```

- [x] **Step 4a: Add the exact immutable artifact-store API.** Put this in `FontArtifactStore.h`:

  ```cpp
  class FontArtifactStore {
  public:
      struct PackagedAuthority {
          std::filesystem::path relativePath;
          std::string artifactSha256;
          bool operator==(const PackagedAuthority&) const;
      };
      static FontArtifactStore ForProject(
          std::filesystem::path projectRoot);
      static std::optional<FontArtifactStore> ForSealedPackage(
          std::filesystem::path runtimeResourceRoot,
          std::vector<PackagedAuthority> manifestFonts,
          molga::text::TextDiagnosticSink&);
      std::optional<VerifiedFontArtifact> Publish(
          const std::filesystem::path& sourcePath,
          std::string_view expectedSourceSha256,
          molga::text::TextDiagnosticSink&) const;
      std::optional<std::shared_ptr<const std::vector<std::uint8_t>>>
      ReadVerified(const VerifiedFontArtifact&,
                   molga::text::TextDiagnosticSink&) const;
      bool IsProjectAuthorityFor(
          const std::filesystem::path& projectRoot) const noexcept;
  private:
      FontArtifactStorage storage_;
      std::filesystem::path storageRoot_;
      std::vector<PackagedAuthority> packagedAuthorities_;
  };
  ```

- [x] **Step 4b: Persist the artifact authority in `AssetRecord`.** Add optional font-only fields `{sourceSha256,artifactStorage,artifactRelativePath,artifactSha256,artifactByteSize}` to canonical serialization, with stable storage strings `ProjectLibrary` and `PackagedResource`. A normal/project catalog accepts only `ProjectLibrary` with normalized path exactly `Library/Imported/Fonts/<artifactSha256>.sfnt`; it rejects `PackagedResource`, absolute/root/`..` paths, non-lowercase SHA, zero size, and `sourceSha256 != artifactSha256`.

- [x] **Step 4c: Add the explicit catalog trust boundary.** Replace ambiguous catalog loads with this exact mode-bearing signature while updating existing callers:

  ```cpp
  enum class AssetCatalogMode : std::uint8_t { Project, SealedPackage };
  bool AssetDatabase::BindFontArtifactStore(
      std::shared_ptr<const FontArtifactStore>,
      std::string* errorOut = nullptr);
  const FontArtifactStore* AssetDatabase::FontArtifacts() const noexcept;
  bool AssetDatabase::LoadCatalog(
      const std::filesystem::path& catalogPath,
      const std::filesystem::path& storageRoot,
      AssetCatalogMode mode,
      std::string* errorOut = nullptr);
  ```

  Bind exactly one store before `ScanProject` or `LoadCatalog`; reject a null/rebind or any font import/load without one, and retain the shared store for database/repository lifetime. `Project` requires a `ProjectLibrary` store and rejects every `PackagedResource`. `SealedPackage` requires a sealed-package store and accepts its locator only after Task 17 has verified the runtime manifest; its normalized forward-slash path must begin exactly `Assets/` and remain below `storageRoot`. The package rewrite changes only locator storage/path, preserving GUID, source/artifact SHA, byte size, metrics, face, license, and generation identity; `FontArtifactStore` separately requires the exact manifest path/SHA authority before reading bytes.

- [x] **Step 4d: Migrate editor startup at the signature boundary.** In
  `main.cpp`, construct `FontArtifactStore::ForProject(openedProjectRoot)`, bind
  it successfully, then call `ScanProject(openedProjectRoot / "Assets")`.
  Neither the current directory nor string removal from an Assets path may
  supply the project root.

- [x] **Step 4e: Migrate the current runtime catalog caller.** In
  `runtime_main.cpp`, bind the explicit store selected by its current
  development/package authority and call the four-argument `LoadCatalog` with
  an explicit storage root and mode. Keep this intermediate caller buildable;
  Task 17 replaces only its authority construction with the verified sealed
  manifest store.

- [x] **Step 4f: Migrate GameBuilder scan authority.** In `GameBuilder.cpp`,
  use `BuildSettings::projectRoot` as the canonical filesystem authority. If
  the singleton database already has a store, require
  `FontArtifacts()->IsProjectAuthorityFor(settings.projectRoot)` and reuse it;
  a mismatch is an explicit build failure. A standalone builder/test with a
  fresh database may bind `ForProject(settings.projectRoot)` once before
  scanning `settings.projectRoot / "Assets"`; it never rebinds an editor-owned
  singleton.

- [x] **Step 4g: Migrate catalog tests.** Update
  `test_asset_catalog.cpp` and `test_post_process.cpp` so every catalog fixture
  first binds a store rooted at its explicit temporary project/storage root and
  every `LoadCatalog` call passes `AssetCatalogMode::Project` plus that root.
  Add one unbound and one mismatched-mode rejection assertion.

- [x] **Step 4h: Migrate core asset scan fixtures.** In
  `test_asset_database.cpp`, `test_asset_reference_migration.cpp`,
  `test_editor_property_descriptor.cpp`, and `test_editor_undo_dirty.cpp`, bind
  a fresh project store before each database's first `ScanProject`; use an
  explicit fixture project root distinct from its Assets root.

- [x] **Step 4i: Migrate media/component scan fixtures.** Apply the same
  explicit project-root/store binding to `test_animation.cpp`,
  `test_audio.cpp`, `test_font.cpp`, and `test_tilemap.cpp`; do not derive the
  authority by stripping an `Assets` suffix.

- [x] **Step 4j: Migrate GPU scan fixtures.** Bind the fixture's project store
  before every `ScanProject` in `test_rendering_sdlgpu.cpp`, preserving all SDL
  compile definitions, labels, timeouts, and working-directory properties.

- [x] **Step 4k: Add one process-root test authority.** Implement the
  header-only `tests/AssetDatabaseTestAuthority.h` around one process-lifetime
  `TempDirectory` with this exact API:

  ```cpp
  namespace test_support {
  class AssetDatabaseTestAuthority {
  public:
      static AssetDatabaseTestAuthority& Get();
      const std::filesystem::path& ProjectRoot() const noexcept;
      std::filesystem::path AssetsCaseRoot(std::string_view caseName);
      bool Bind(molga::AssetDatabase&, std::string* errorOut = nullptr);
  private:
      AssetDatabaseTestAuthority();
      TempDirectory temp_;
      std::filesystem::path projectRoot_;
      std::uint64_t nextCase_ = 1;
  };
  } // namespace test_support
  ```

  `Get()` is an inline function-local static and therefore one authority per
  test process. `AssetsCaseRoot` sanitizes a nonempty case label, uses checked
  never-wrapping `nextCase_`, creates a fresh child below
  `ProjectRoot()/Assets`, and rejects escape/collision. `Bind` binds
  `ForProject(ProjectRoot())` only when absent; otherwise it requires
  `IsProjectAuthorityFor(ProjectRoot())`. Refactor singleton-using cases in
  `test_rendering_sdlgpu.cpp`, `test_tilemap.cpp`, and
  `test_asset_reference_migration.cpp` to place each case's files under that
  shared project root. `AssetDatabase::Clear()` clears records/root scan state
  but deliberately retains the immutable store binding. Add a two-case
  regression proving different Assets subtrees reuse the same store without
  rebind or cross-case records; do not add a test-only production reset API.

- [x] **Step 5: Add the version-2 settings overload.**

  ```cpp
  class FontImporter : public IImporter {
  public:
      std::string Name() const override { return "FontImporter"; }
      int Version() const override { return 2; }
      bool CanImport(const std::string& extension) const override;
      ImportResult Import(const std::string& absoluteSourcePath) const override;
      ImportResult Import(const std::string& absoluteSourcePath,
                          const nlohmann::json& settings) const override;
  };
  ```

  Require face index, weight `1..1000`, stretch `50..200`, slant, redistribution confirmation and license fields, and persist the canonical settings into importer metadata.

- [x] **Step 6: Select the exact SFNT face.** Accept a top-level `ttcf` only by resolving an in-range authored face offset; a non-collection file accepts only face index `0`. Emit `FontInvalid` for an invalid collection header/count/offset before parsing tables.

- [x] **Step 6a: Validate the selected face directory.** Require signature `0x00010000` or `OTTO`, validate every table range/checksum without overlap or out-of-file access, and require either `glyf`+`loca` or `CFF ` outlines.

- [x] **Step 6b: Reject unsupported variable/color tables.** Before raster-face creation, emit `FontInvalid` if the selected directory contains `fvar`, `gvar`, `CFF2`, `COLR`, `CPAL`, `CBDT`, `CBLC`, `sbix`, or `SVG\x20`.

- [x] **Step 6c: Parse exact design metrics.** Read `unitsPerEm` as big-endian unsigned 16-bit from `head` and the three `hhea` FWORD values as big-endian signed 16-bit. Require `16 <= unitsPerEm <= 16384`, `ascender > 0`, `descender <= 0`, `ascender > descender`, `lineGap >= 0`, and checked `int32_t(ascender) - int32_t(descender) + int32_t(lineGap)`; otherwise emit `FontInvalid` before artifact/catalog publication.

- [x] **Step 7: Emit content and coverage identity.** Compute source SHA-256 and enumerate canonical merged cmap coverage ranges for the selected face; never substitute generic `AssetRecord.hash` for security identity.

- [x] **Step 7a: Emit selected face/style/license metadata.** Record exact face index, authored style, static-outline marker, and every validated license setting in the import result.

- [x] **Step 7b: Add the binary immutable publish primitive.** Implement `PersistentStorage::AtomicPublishImmutableBytes(destination, bytes, expectedSha256)`: create parent directories, take a sibling lock, write a unique same-directory file with create-new semantics, flush file data, verify size/SHA, and rename only when the destination is absent. If the destination exists, accept it only when size/SHA/bytes match; never overwrite or delete a conflicting immutable artifact, and clean only the caller-owned temp on failure.

- [x] **Step 7c: Publish the content-addressed font artifact.** `FontArtifactStore::Publish` first requires a `ProjectLibrary` store, reads the source exactly once into immutable bytes, verifies `expectedSourceSha256`, computes `artifactSha256` over those identical bytes, requires equality, and publishes to `Library/Imported/Fonts/<artifactSha256>.sfnt` through Step 7b. It returns `FontArtifactLocator{ProjectLibrary,path}`. The original source is provenance/authoring input; the artifact is a byte-identical immutable authority, so the SHA relationship is mandatory equality rather than a derived-font transform.

- [x] **Step 7d: Bind the explicit project artifact root.** Editor/project startup creates `FontArtifactStore::ForProject(theOpenedProjectRoot)` and calls `BindFontArtifactStore` before `ScanProject`; tests do the same with their temporary project root. Never derive the project root from current working directory, `Assets` string stripping, or a source path.

- [x] **Step 7e: Publish catalog authority only after the artifact.** On a successful font import, `AssetDatabase` first obtains a verified `VerifiedFontArtifact`, then atomically publishes the catalog/`AssetRecord` and only afterward increments `contentGeneration`. Artifact or catalog failure leaves the prior record/generation authoritative; a newly created but unreferenced content-addressed artifact may remain safely orphaned for later garbage collection.

- [x] **Step 7f: Implement project artifact reads.** In a `ProjectLibrary` store, `ReadVerified` accepts only a matching `ProjectLibrary` locator with the exact content-addressed path, resolves it under `projectRoot` by path components, reads it once, and requires recorded byte size plus both SHA fields before returning immutable shared bytes.

- [x] **Step 7f.1: Implement project-authority comparison.**
  `IsProjectAuthorityFor` returns true only for `ProjectLibrary` and an exact
  canonical component-wise match to the store's construction root after the
  same symlink/escape checks used by `ReadVerified`; it never compares raw
  strings or exposes the private root.

- [x] **Step 7g: Implement sealed packaged artifact reads.** `ForSealedPackage` validates/sorts/deduplicates its manifest-derived `{relativePath,artifactSha256}` authority vector before publishing the store. In that store, `ReadVerified` accepts only a `PackagedResource` locator whose exact path/SHA pair occurs once in the authority vector, resolves it below `runtimeResourceRoot`, and verifies recorded size plus bytes/SHA. Cross-storage locators, escape/symlink, missing authority, mismatch, or corruption emit `FontInvalid`; neither mode opens the authoring source as fallback.

- [x] **Step 8: Run static/rejection/catalog green gates.**

  Run: `cmake --build --preset debug -j && ctest --preset debug --output-on-failure`

  Expected: every migrated caller compiles, the complete Debug suite passes,
  locked static fixtures pass, and the variable Korean fixture plus
  color/invalid faces fail with complete typed context.

- [x] **Step 9: Commit static import and diagnostic persistence.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Assets/FontAsset.* \
    src/Assets/FontArtifactStore.* src/Core/PersistentStorage.* \
    src/Core/Importers/Importer.h src/Core/Importers/FontImporter.* \
    src/Core/AssetDatabase.* src/main.cpp src/runtime_main.cpp \
    src/Editor/GameBuilder.cpp tests/test_font_assets.cpp \
    tests/AssetDatabaseTestAuthority.h \
    tests/test_animation.cpp tests/test_asset_catalog.cpp \
    tests/test_asset_database.cpp tests/test_asset_reference_migration.cpp \
    tests/test_audio.cpp tests/test_editor_property_descriptor.cpp \
    tests/test_editor_undo_dirty.cpp tests/test_font.cpp \
    tests/test_post_process.cpp tests/test_rendering_sdlgpu.cpp \
    tests/test_tilemap.cpp
  git commit -m "feat: import static font assets"
  ```

**Exit:** Imported font records are content-addressed, license-aware, static-only, and preserve machine-readable failures through catalog reload.

**Implementation record (2026-09-04).** Commits `0ae07d8` (task), `24c55eb` (mutation-driven test
additions) and `febcb2c` (review fixes). Debug suite 92/92 on a solo run.

Reviewed by four lenses. Two ran late because an API outage killed them mid-run; the task was
committed on spec + build evidence first and the remaining gates closed retroactively before
Task 4.2 started, rather than being declared met.

**The mutation sweep found five of ten mutations escaping the committed suite.** The worst was
fail-open on the byte authority: deleting the comparison of hashed source bytes against the
expected SHA-256 left every test green, so tampered or stale font bytes would import silently.
Two escapes were subtler than a missing test:

- The null-bind assertion **existed but passed for the wrong reason** — it ran on a database that
  already had a store, so the rebind guard rejected the null and the null guard was never
  exercised. It is now asserted on a fresh database, with a positive control.
- The expected-SHA format guard is **behaviourally redundant**: a malformed digest also fails the
  content comparison, so no "was it rejected" assertion can separate the paths. The observable
  difference is the diagnostic — a format violation says recompute the digest, a content mismatch
  says reimport the asset. Both are pinned, with a control proving the paths diverge.

Also pinned: the `Clear()` exemption preserving the binding, and the `Assets/` prefix rule in
catalog parsing, which was guarded in the store but not there.

**Two defects fixed, each reproduced with a scratch program rather than inferred:**

- **Builds were not reproducible.** The persisted `contentRevision` was a process counter that
  neither `Clear()` nor `ScanProject` reset, so identical fixtures scanned 1,2 → 3,4 → 5,6. Since
  `GameBuilder` scans twice per build, an unchanged project emitted a different
  `asset_catalog.json` every build — which Task 17.4's catalog sealing and Task 18.2's
  byte-identical parity would both have inherited. Nothing requires monotonicity: identity is
  carried by `artifactSha256`, and Task 5.1 keeps the counter out of `fontRevision`.
- **A killed publisher wedged an artifact permanently.** The publish lock was a single-shot
  `O_EXCL` create with no retry or staleness handling; four processes publishing byte-identical
  content had three fail. For content-addressed bytes, another publisher finishing first is a
  success condition, not a failure.

**Approved deviation:** `tests/test_build_manager.cpp` is outside the Files list. Step 4f makes a
store/project mismatch an explicit build failure, and that test built two projects in one process,
which a bind-once store cannot serve. Verified before changing it that one project per editor
process is genuinely the production contract — `ProjectWindow::OnGUI` runs only inside the startup
picker loop and the main editor loop never draws it — so no path opens a second project after the
store is bound. No assertion was dropped.

**Carried forward:** an unauthored font `.meta` (no v2 settings) imports as a silent success with
no artifact and ships; Milestones 5–8 stay closed only because `FontAsset::FromRecord` refuses the
record. Its source comment named Task 8.2/15.2, but a grep of all 65 task files found neither
mentions the seam. Now owned by an amendment under Task 15.4 in
`05-editor-authoring-migration.md`, because closing it is itself a legacy migration.

---

### Task 4.2: Import ordered authored font families

**Prerequisite:** Task 4.1 provides validated `FontAsset` metadata.

**Files:**

- Create: `src/Assets/FontFamilyAsset.h`
- Create: `src/Assets/FontFamilyAsset.cpp`
- Create: `src/Core/Importers/FontFamilyImporter.h`
- Create: `src/Core/Importers/FontFamilyImporter.cpp`
- Create: `tests/fixtures/text/families/primary.fontfamily`
- Create: `tests/fixtures/text/families/primary.fontfamily.meta`
- Create: `tests/fixtures/text/families/arabic.fontfamily`
- Create: `tests/fixtures/text/families/arabic.fontfamily.meta`
- Create: `tests/fixtures/text/families/cjk.fontfamily`
- Create: `tests/fixtures/text/families/cjk.fontfamily.meta`
- Create: `tests/fixtures/text/families/cycle-a.fontfamily`
- Create: `tests/fixtures/text/families/cycle-a.fontfamily.meta`
- Create: `tests/fixtures/text/families/cycle-b.fontfamily`
- Create: `tests/fixtures/text/families/cycle-b.fontfamily.meta`
- Create: `tests/fixtures/text/fonts/NotoSans-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansHebrew-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansArabic-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansDevanagari-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansThai-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansKR-Regular.otf.meta`
- Create: `tests/fixtures/text/licenses/NotoFonts-ffebf8c1-OFL.txt.meta`
- Create: `tests/fixtures/text/licenses/NotoCJK-Sans2.004-OFL.txt.meta`
- Create: `tests/TextQualificationAssetTree.h`
- Modify: `src/Core/Importers/ImporterRegistry.cpp`
- Modify: `tests/test_font_assets.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `FontAsset`, generic importer metadata, authored JSON order.
- Produces: `FontFamilyFaceEntry`, schema-1 `FontFamilyAsset`, and `FontFamilyImporter`.

- [x] **Step 1: Add a failing ordered-family round-trip test.**

  ```cpp
  TEST_CASE("font family preserves authored face fallback and unknown-field order") {
      constexpr std::string_view kPrimaryGuid =
          "11111111111111111111111111111111";
      constexpr std::string_view kArabicGuid =
          "22222222222222222222222222222222";
      constexpr std::string_view kCjkGuid =
          "33333333333333333333333333333333";
      const auto result = molga::FontFamilyImporter().Import(
          MOLGA_TEXT_PRIMARY_FAMILY);
      REQUIRE(result.success);
      QualificationAssetTreeFixture fixture;
      molga::AssetDatabase db;
      auto store = std::make_shared<const molga::FontArtifactStore>(
          molga::FontArtifactStore::ForProject(fixture.ProjectRoot()));
      REQUIRE(db.BindFontArtifactStore(store));
      db.ScanProject(fixture.AssetsRoot());
      const molga::AssetRecord* record = db.Find(std::string(kPrimaryGuid));
      REQUIRE(record != nullptr);
      CHECK(record->importer == "FontFamilyImporter");
      CHECK(record->importerVersion == 1);
      const auto family =
          molga::FontFamilyAsset::FromRecord(*record, TestError());
      REQUIRE(family);
      CHECK(family->guid == std::string(kPrimaryGuid));
      CHECK(family->faces.at(0).authoredFaceIndex == 0);
      CHECK(family->faces.at(1).authoredFaceIndex == 1);
      CHECK(family->faces.at(2).authoredFaceIndex == 2);
      CHECK(family->faces.at(3).authoredFaceIndex == 3);
      CHECK(FaceGuids(*family) == std::vector<std::string>{
          "44444444444444444444444444444444",
          "55555555555555555555555555555555",
          "12121212121212121212121212121212",
          "13131313131313131313131313131313"});
      CHECK(family->fallbackFamilyGuids ==
            std::vector<std::string>{std::string(kArabicGuid),
                                     std::string(kCjkGuid)});
      REQUIRE(result.metadata.contains("unknownAuthoringField"));
      CHECK(result.metadata["unknownAuthoringField"] == "preserved");
  }
  ```

- [x] **Step 1a: Add the failing sidecar-authority test.**

  ```cpp
  TEST_CASE("font family source cannot override sidecar GUID authority") {
      nlohmann::json source = ValidFontFamilySourceJson();
      source["guid"] = "ffffffffffffffffffffffffffffffff";
      const auto result = ImportTemporaryFontFamily(source);
      CHECK_FALSE(result.success);
      CHECK(HasDiagnostic(result.importDiagnostics,
            molga::text::TextDiagnosticCode::FontFamilyInvalid));
  }
  ```

- [x] **Step 1b: Add the failing clean-checkout qualification-tree test.**

  ```cpp
  TEST_CASE("qualification asset tree rescans six fonts and two licenses") {
      QualificationAssetTreeFixture fixture;
      molga::AssetDatabase db;
      auto store = std::make_shared<const molga::FontArtifactStore>(
          molga::FontArtifactStore::ForProject(fixture.ProjectRoot()));
      REQUIRE(db.BindFontArtifactStore(store));
      db.ScanProject(fixture.AssetsRoot());
      const std::vector<std::tuple<std::string, std::string, int>> expected{
          {"44444444444444444444444444444444", "FontImporter", 2},
          {"55555555555555555555555555555555", "FontImporter", 2},
          {"66666666666666666666666666666666", "FontImporter", 2},
          {"12121212121212121212121212121212", "FontImporter", 2},
          {"13131313131313131313131313131313", "FontImporter", 2},
          {"77777777777777777777777777777777", "FontImporter", 2},
          {"88888888888888888888888888888888", "GenericImporter", 1},
          {"99999999999999999999999999999999", "GenericImporter", 1},
      };
      for (const auto& [guid, importer, version] : expected) {
          const molga::AssetRecord* record = db.Find(guid);
          REQUIRE_MESSAGE(record != nullptr, guid);
          CHECK(record->guid == guid);
          CHECK(record->importer == importer);
          CHECK(record->importerVersion == version);
      }
  }
  ```

- [x] **Step 2: Run the family-import red gate.**

  Run: `cmake --build --preset debug --target test_font_assets -j`

  Expected: compile FAIL because `FontFamilyAsset` and its importer do not exist.

- [x] **Step 3: Add the exact authored family models.**

  ```cpp
  struct FontFamilyFaceEntry {
      std::string fontGuid;
      std::uint32_t faceIndex = 0;
      std::uint16_t weight = 400;
      std::uint16_t stretchPercent = 100;
      FontSlant slant = FontSlant::Upright;
      std::uint32_t authoredFaceIndex = 0;
  };
  struct FontFamilyAsset {
      static constexpr int CurrentSchemaVersion = 1;
      std::string guid;
      int schemaVersion = CurrentSchemaVersion;
      std::vector<FontFamilyFaceEntry> faces;
      std::vector<std::string> fallbackFamilyGuids;
      static std::optional<FontFamilyAsset> FromRecord(
          const AssetRecord&, std::string& errorOut);
  };
  ```

  `FromRecord` sets `guid = record.guid` after validating it as 32 hexadecimal characters; no source/importer metadata field may replace it.

- [x] **Step 4: Parse ordered `.fontfamily` arrays.** Copy face/fallback arrays in source order and assign each face its source array index as `authoredFaceIndex`.

- [x] **Step 4a: Preserve extension fields but reserve identity.** Retain unknown top-level fields in importer metadata. Reserve and reject source-level `guid`; `FontFamilyAsset::guid` always comes from `AssetRecord::guid`/the sidecar and importer metadata never treats embedded identity as authoritative.

- [x] **Step 4b: Commit the exact primary family source.** Write this JSON value with no source-level `guid` (canonical serializer whitespace is allowed):

  | Source | Exact JSON value |
  |---|---|
  | `primary.fontfamily` | `{"schemaVersion":1,"faces":[{"fontGuid":"44444444444444444444444444444444","faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright"},{"fontGuid":"55555555555555555555555555555555","faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright"},{"fontGuid":"12121212121212121212121212121212","faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright"},{"fontGuid":"13131313131313131313131313131313","faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright"}],"fallbackFamilyGuids":["22222222222222222222222222222222","33333333333333333333333333333333"],"unknownAuthoringField":"preserved"}` |

- [x] **Step 4c: Commit the exact fallback/cycle family sources.** Write these four JSON values with no source-level `guid` (canonical serializer whitespace is allowed):

  | Source | Exact JSON value |
  |---|---|
  | `arabic.fontfamily` | `{"schemaVersion":1,"faces":[{"fontGuid":"66666666666666666666666666666666","faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright"}],"fallbackFamilyGuids":[]}` |
  | `cjk.fontfamily` | `{"schemaVersion":1,"faces":[{"fontGuid":"77777777777777777777777777777777","faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright"}],"fallbackFamilyGuids":[]}` |
  | `cycle-a.fontfamily` | `{"schemaVersion":1,"faces":[],"fallbackFamilyGuids":["bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"]}` |
  | `cycle-b.fontfamily` | `{"schemaVersion":1,"faces":[],"fallbackFamilyGuids":["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"]}` |

- [x] **Step 4d: Commit canonical family sidecars.** Write these exact sidecar objects (canonical serializer whitespace is allowed):

  | Sidecar | Exact JSON value |
  |---|---|
  | `primary.fontfamily.meta` | `{"guid":"11111111111111111111111111111111","importer":"FontFamilyImporter","importerVersion":1,"settings":{}}` |
  | `arabic.fontfamily.meta` | `{"guid":"22222222222222222222222222222222","importer":"FontFamilyImporter","importerVersion":1,"settings":{}}` |
  | `cjk.fontfamily.meta` | `{"guid":"33333333333333333333333333333333","importer":"FontFamilyImporter","importerVersion":1,"settings":{}}` |
  | `cycle-a.fontfamily.meta` | `{"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","importer":"FontFamilyImporter","importerVersion":1,"settings":{}}` |
  | `cycle-b.fontfamily.meta` | `{"guid":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","importer":"FontFamilyImporter","importerVersion":1,"settings":{}}` |

- [x] **Step 4e: Commit the Latin/Hebrew Noto Fonts sidecars.** Write these exact objects (canonical serializer whitespace is allowed):

  | Sidecar | Exact JSON value |
  |---|---|
  | `NotoSans-Regular.ttf.meta` | `{"guid":"44444444444444444444444444444444","importer":"FontImporter","importerVersion":2,"settings":{"faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright","redistributableConfirmed":true,"licenseKind":"OFL-1.1","copyright":"fixture provenance: Noto Fonts ffebf8c1","licenseAssetGuid":"88888888888888888888888888888888"}}` |
  | `NotoSansHebrew-Regular.ttf.meta` | `{"guid":"55555555555555555555555555555555","importer":"FontImporter","importerVersion":2,"settings":{"faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright","redistributableConfirmed":true,"licenseKind":"OFL-1.1","copyright":"fixture provenance: Noto Fonts ffebf8c1","licenseAssetGuid":"88888888888888888888888888888888"}}` |

- [x] **Step 4f: Commit the Arabic Noto Fonts sidecar.** Write `tests/fixtures/text/fonts/NotoSansArabic-Regular.ttf.meta` as `{"guid":"66666666666666666666666666666666","importer":"FontImporter","importerVersion":2,"settings":{"faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright","redistributableConfirmed":true,"licenseKind":"OFL-1.1","copyright":"fixture provenance: Noto Fonts ffebf8c1","licenseAssetGuid":"88888888888888888888888888888888"}}` modulo canonical serializer whitespace.

- [x] **Step 4g: Commit the Indic Noto Fonts sidecars.** Write these exact objects (canonical serializer whitespace is allowed):

  | Sidecar | Exact JSON value |
  |---|---|
  | `NotoSansDevanagari-Regular.ttf.meta` | `{"guid":"12121212121212121212121212121212","importer":"FontImporter","importerVersion":2,"settings":{"faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright","redistributableConfirmed":true,"licenseKind":"OFL-1.1","copyright":"fixture provenance: Noto Fonts ffebf8c1","licenseAssetGuid":"88888888888888888888888888888888"}}` |
  | `NotoSansThai-Regular.ttf.meta` | `{"guid":"13131313131313131313131313131313","importer":"FontImporter","importerVersion":2,"settings":{"faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright","redistributableConfirmed":true,"licenseKind":"OFL-1.1","copyright":"fixture provenance: Noto Fonts ffebf8c1","licenseAssetGuid":"88888888888888888888888888888888"}}` |

- [x] **Step 4h: Commit the Noto CJK sidecar.** Write `tests/fixtures/text/fonts/NotoSansKR-Regular.otf.meta` as `{"guid":"77777777777777777777777777777777","importer":"FontImporter","importerVersion":2,"settings":{"faceIndex":0,"weight":400,"stretchPercent":100,"slant":"Upright","redistributableConfirmed":true,"licenseKind":"OFL-1.1","copyright":"fixture provenance: Noto CJK 523d033d","licenseAssetGuid":"99999999999999999999999999999999"}}` modulo canonical serializer whitespace.

- [x] **Step 4i: Commit both license sidecars.** Write `NotoFonts-ffebf8c1-OFL.txt.meta` as `{"guid":"88888888888888888888888888888888","importer":"GenericImporter","importerVersion":1,"settings":{}}` and `NotoCJK-Sans2.004-OFL.txt.meta` as `{"guid":"99999999999999999999999999999999","importer":"GenericImporter","importerVersion":1,"settings":{}}`, beside the locked license bytes.

- [x] **Step 5: Validate local family fields.** Require integer `schemaVersion == 1`, sidecar-valid 32-hex face/fallback GUID strings, face index, weight/stretch/slant ranges and array value types. Reject a missing/unknown `schemaVersion`, legacy `schema`, and source `guid`, but do not resolve cross-asset GUIDs or cycles in the importer.

- [x] **Step 5a: Implement the qualification-tree source list.** In `tests/TextQualificationAssetTree.h`, include `<array>`, `<filesystem>`, `<stdexcept>`, `<string>`, `<string_view>`, and define this exact source-relative list; a source and its adjacent `.meta` are always copied as one pair:

  ```cpp
  constexpr std::array<std::string_view, 13> kQualificationSources{
      "fonts/NotoSans-Regular.ttf",
      "fonts/NotoSansHebrew-Regular.ttf",
      "fonts/NotoSansArabic-Regular.ttf",
      "fonts/NotoSansDevanagari-Regular.ttf",
      "fonts/NotoSansThai-Regular.ttf",
      "fonts/NotoSansKR-Regular.otf",
      "licenses/NotoFonts-ffebf8c1-OFL.txt",
      "licenses/NotoCJK-Sans2.004-OFL.txt",
      "families/primary.fontfamily",
      "families/arabic.fontfamily",
      "families/cjk.fontfamily",
      "families/cycle-a.fontfamily",
      "families/cycle-b.fontfamily",
  };
  ```

- [x] **Step 5b: Implement the qualification-tree copier.** Include `SmokeTestSupport.h`, then add this fixture in the same header; it copies bytes only and never generates sidecar JSON:

  ```cpp
  class QualificationAssetTreeFixture {
  public:
      QualificationAssetTreeFixture()
          : temp_("text-qualification-assets"),
            projectRoot_(temp_.Path()),
            assetsRoot_(projectRoot_ / "Assets") {
          const std::filesystem::path sourceRoot =
              MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT;
          for (const std::string_view relative : kQualificationSources) {
              const std::string relativePath(relative);
              CopyRequired(sourceRoot / relativePath, assetsRoot_ / relativePath);
              CopyRequired(sourceRoot / (std::string(relative) + ".meta"),
                           assetsRoot_ / (std::string(relative) + ".meta"));
          }
      }
      const std::filesystem::path& ProjectRoot() const { return projectRoot_; }
      const std::filesystem::path& AssetsRoot() const { return assetsRoot_; }

  private:
      static void CopyRequired(const std::filesystem::path& source,
                               const std::filesystem::path& destination) {
          if (!std::filesystem::is_regular_file(source))
              throw std::runtime_error("missing fixture pair member: " + source.string());
          std::filesystem::create_directories(destination.parent_path());
          if (!std::filesystem::copy_file(
                  source, destination, std::filesystem::copy_options::none))
              throw std::runtime_error("fixture destination exists: " + destination.string());
      }

      test_support::TempDirectory temp_;
      std::filesystem::path projectRoot_;
      std::filesystem::path assetsRoot_;
  };
  ```

- [x] **Step 6: Register family import and fixture paths.** Map only `.fontfamily` to `FontFamilyImporter`, keep generic `AssetRecord.hash` as a non-security cache field, and define `MOLGA_TEXT_PRIMARY_FAMILY`, `MOLGA_TEXT_FAMILY_FIXTURE_ROOT`, and `MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT` on `test_font_assets` as exact `${CMAKE_SOURCE_DIR}/tests/fixtures/text/families/primary.fontfamily`, `${CMAKE_SOURCE_DIR}/tests/fixtures/text/families`, and `${CMAKE_SOURCE_DIR}/tests/fixtures/text` paths respectively.

- [x] **Step 7: Run the family green gate.**

  Run: `cmake --build --preset debug --target test_font_assets test_importer -j && ctest --test-dir build/debug -R '^(test_font_assets|test_importer)$' --output-on-failure`

  Expected: PASS; valid authored order round-trips, malformed fields emit `FontFamilyInvalid`, and cycle fixtures import because graph validation belongs to Task 5.1.

- [x] **Step 8: Commit family authoring.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Assets/FontFamilyAsset.* \
    src/Core/Importers/FontFamilyImporter.* \
    src/Core/Importers/ImporterRegistry.cpp tests/test_font_assets.cpp \
    tests/TextQualificationAssetTree.h \
    tests/fixtures/text/families tests/fixtures/text/fonts/*.meta \
    tests/fixtures/text/licenses/*.meta
  git commit -m "feat: import authored font families"
  ```

**Exit:** Authored family order is stable and reviewable without prematurely accepting broken graph references.

**Implementation record (2026-09-05).** Commit `8bbce41`. Debug suite 92/92 run alone. Audit passed
after both blocking findings were fixed and re-killed by the mutations that had exposed them.

**Two blocking coverage holes, both in exactly the contract this task exists to establish:**

- **Fallback family order was not actually pinned.** Three separate ordering mutations survived,
  including *sorting the fallback list by GUID*. Since Milestone 5's deterministic candidate
  resolution consumes this ordering, a silent re-sort here would have produced plausible,
  wrong fallback everywhere downstream. The new case authors a deliberately non-lexicographic list
  and asserts the metadata directly — a single-application witness, so an involutive defect such as
  a reverse cannot cancel itself across a round trip.
- **Every numeric range endpoint was untested.** The rejection table probed only *outside* each
  range, so both inclusive boundaries could be moved without detection. Now weight 1/1000, stretch
  50/200 and faceIndex 0/65535 are accepted and round-tripped.

Also pinned: per-face unknown authored fields (preserved by code and by comment, tested by nothing),
and the 4 MiB source cap, which was invisible to removal.

**Found while verifying, reported by no lens:** this test executable lacks doctest's
`TREAT_CHAR_STAR_AS_STRING`, so every `const char*` table label printed as a pointer address — a
failure in a 30-row table named no row. Routed through a `Label()` helper.

**Approved deviation — `.gitignore`.** Line 114 carries a blanket `*.meta` from the Visual Studio
boilerplate, and no `.meta` was tracked anywhere in the repo. All 18 fixture sidecars this task adds
were ignored. Reproduced both failure shapes: the glob form of Step 8's `git add` aborts with
`pathspec did not match any files`, and the directory form exits 0 while **silently staging nothing**
— after which a clean checkout fails Step 1b with `missing fixture pair member` and `ScanProject`
mints fresh GUIDs, breaking the pinned-GUID contract Milestone 5 depends on. Three negations scoped
to the three fixture directories only. `.gitignore` belongs in Step 8's `git add` list.

---

#### AMENDMENT (2026-09-05): two items carried out of Task 4.2

**1. `contentRevision` is a directory-enumeration ordinal.** `AssetDatabase.cpp:517/:561` assigns it
from `recursive_directory_iterator` order. Task 4.1 already fixed the *worse* half of this — it was a
process counter that never reset, so an unchanged project emitted a different catalog every build —
but the value still depends on filesystem enumeration order, and this task's six-font tree is the
first place that becomes load-bearing. It is serialised into `asset_catalog.json`, which **Task 17.4
seals by hash** and **Task 18.2 proves byte-identical**. Nothing requires it to be monotonic:
identity is carried by `artifactSha256`, and Task 5.1 keeps it out of `fontRevision`.
**Owner: Task 17.4**, which must either make the value content-derived, or drop it from the sealed
catalog, before sealing a hash over an enumeration-order artefact. **Task 5.1 Step 1c consumes this
tree** and should not build ordering on the field.

**2. A plan-verbatim block carried a process-abort hazard, and the block is amended above.**
Step 1's `CHECK(result.metadata["unknownAuthoringField"] == "preserved")` reads a **const**
`nlohmann::json` with `operator[]`, which aborts on a missing key. So if this contract ever broke,
the failure mode was not one named assertion but a dead process taking eleven unrelated cases down
as "skipped". The implementer correctly refused to edit a verbatim block on its own authority and
escalated instead. Step 1's block now carries a `REQUIRE(... .contains(...))` guard ahead of the
`CHECK`, matching the guarded form already used elsewhere in the same file; reviewers diffing this
block mechanically should expect the guard.

---

### Task 4.3: Publish immutable font resources across hot reload

**Prerequisite:** Tasks 4.1–4.2 provide validated font/family records.

**Files:**

- Create: `src/Text/FontRepository.h`
- Create: `src/Text/FontRepository.cpp`
- Modify: `src/Assets/FontAsset.h`
- Modify: `src/Assets/FontAsset.cpp`
- Modify: `src/Assets/FontArtifactStore.h`
- Modify: `src/Assets/FontArtifactStore.cpp`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `tests/test_font_assets.cpp`
- Modify: `tests/test_font.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: catalog-authoritative `FontAsset` artifact identity, `FontArtifactStore::ReadVerified`, and the text-owned rasterizer; it never consumes `AssetDatabase::AbsoluteSourcePath`.
- Produces: `ScaledFontDesignMetrics`, `ScaleFontDesignMetrics`, `FontArtifactStore::{ForProject,ForSealedPackage}`, `FontFace::LoadFromBytes`, `FaceIndex`, `GlyphId`, `HasCodepoint`; `FontFaceResource`, `FontFaceResourcePtr`, `FontRepository::{Load,Invalidate}`, and `AssetDatabase::ContentGeneration(const std::string& guid)` for font/family invalidation.

- [ ] **Step 1: Add failing byte-ownership and hot-reload tests.**

  ```cpp
  TEST_CASE("old font resource keeps exact bytes after successful replacement") {
      FontRepositoryFixture fixture;
      auto oldResource = fixture.repository.Load("font-a", 0, fixture.sink);
      REQUIRE(oldResource);
      const std::string oldSha = (*oldResource)->sourceSha256;
      fixture.ReplaceFontWithVerifiedBytes("font-a", MOLGA_TEXT_ARABIC_FONT);
      fixture.repository.Invalidate("font-a");
      auto newResource = fixture.repository.Load("font-a", 0, fixture.sink);
      REQUIRE(newResource);
      CHECK((*oldResource)->sourceSha256 == oldSha);
      CHECK((*newResource)->sourceSha256 != oldSha);
      CHECK((*oldResource)->rasterFace->FaceIndex() == 0);
  }
  ```

- [ ] **Step 1a: Add the failing last-good failed-reload test.**

  ```cpp
  TEST_CASE("failed font replacement preserves last-good generation and resource") {
      FontRepositoryFixture fixture;
      const auto before = fixture.repository.Load("font-a", 0, fixture.sink);
      REQUIRE(before);
      const auto generation = fixture.database.ContentGeneration("font-a");
      fixture.ReplaceFontWithCorruptBytes("font-a");
      CHECK_FALSE(fixture.database.TryReimport("font-a"));
      CHECK(fixture.database.ContentGeneration("font-a") == generation);
      const auto after = fixture.repository.Load("font-a", 0, fixture.sink);
      REQUIRE(after);
      CHECK(*after == *before);
      CHECK(HasDiagnostic(fixture.sink,
            molga::text::TextDiagnosticCode::FontInvalid));
  }
  ```

- [ ] **Step 1b: Implement the failed-reload fixture mutation.** `ReplaceFontWithCorruptBytes` replaces only the fixture's temporary source with a same-size copy whose SFNT signature byte is flipped, invokes the real import publication path, and retains the temporary prior catalog/resource owners for assertions; it never edits a committed font.

- [ ] **Step 1c: Add the failing corrupt-source-before-first-load test.** Import a valid temporary source, construct no repository resource yet, flip that authoring source's SFNT signature without reimport, then call `Load`; require the loaded bytes/SHA equal the catalog artifact, the source-read counter remains zero, and glyph lookup succeeds. This proves import publication, not an already-cached resource, owns the last-good bytes.

- [ ] **Step 1d: Add the failing restart/catalog-reload last-good test.** Import valid bytes, attempt and fail a corrupt-source reimport, destroy the database/repository, reconstruct both from the persisted catalog and same artifact root, and require the original content generation identity, artifact SHA/path, metrics, and glyph result. The recreated repository must record zero authoring-source opens.

- [ ] **Step 1e: Add the failing corrupt-artifact test.** After a successful import and before first `Load`, mutate only a copied project's `Library/Imported/Fonts/<sha>.sfnt`; require `Load` returns `nullopt` with `FontInvalid`, no raster face is created, and the intact source is never opened as fallback.

- [ ] **Step 1f: Add the failing canonical metric-scale test.** Scale `{1000,750,-250,125}` at `Fixed26_6::FromRaw(1056)` and assert exact raw results from `CheckedMulDiv(1056,numerator,1000)` for ascent, descent, and line gap. Add `descender=INT16_MIN` and maximum font-size cases that either return exact checked values or `nullopt`; no assertion uses float or epsilon comparison.

- [ ] **Step 1g: Bind the existing font test to the common runtime session.** Replace only `molga_add_test(test_font test_font.cpp)` with `molga_add_text_test(test_font test_font.cpp)` and remove its `doctest_main` linkage; `test_font_assets` already uses the same helper from Task 4.1.

- [ ] **Step 1h: Add the failing project-versus-package locator test.** Build one project store and one sealed-package store over isolated roots:

  ```cpp
  TEST_CASE("artifact stores accept only their authorized storage locator") {
      ArtifactLocatorFixture f;
      const auto project = molga::FontArtifactStore::ForProject(f.projectRoot);
      auto packaged = molga::FontArtifactStore::ForSealedPackage(
          f.runtimeResourceRoot,
          std::vector<molga::FontArtifactStore::PackagedAuthority>{
              {"Assets/Fonts/used.otf", f.sha256}},
          f.sink);
      REQUIRE(packaged);
      const molga::VerifiedFontArtifact projectRecord{
          {molga::FontArtifactStorage::ProjectLibrary,
           std::filesystem::path("Library/Imported/Fonts") /
               (f.sha256 + ".sfnt")},
          f.sha256, f.sha256, f.byteSize};
      const molga::VerifiedFontArtifact packageRecord{
          {molga::FontArtifactStorage::PackagedResource,
           "Assets/Fonts/used.otf"},
          f.sha256, f.sha256, f.byteSize};
      CHECK(project.ReadVerified(projectRecord, f.sink));
      CHECK(packaged->ReadVerified(packageRecord, f.sink));
      CHECK_FALSE(project.ReadVerified(packageRecord, f.sink));
      CHECK_FALSE(packaged->ReadVerified(projectRecord, f.sink));
      CHECK(f.AuthoringSourceOpenCount() == 0);
  }
  ```

- [ ] **Step 1i: Add the failing sealed-authority rejection table.** For a package containing exactly one SFNT, independently try locator `../Assets/Fonts/used.otf`, absolute path, symlink escape, unmanifested `Assets/Fonts/extra.otf`, manifest-authorized path with wrong artifact SHA, and catalog path differing from the authority by case. Every `ReadVerified` returns `nullopt` with `FontInvalid`, package SFNT count remains one, and the authoring-source open counter remains zero.

- [ ] **Step 1j: Add the failing sealed-catalog restart test.** Serialize a project record, rewrite only its locator to `{PackagedResource,"Assets/Fonts/used.otf"}` as Task 17 does, reload the sealed catalog plus manifest-derived authority into a new database/store/repository, and require the same GUID/source SHA/artifact SHA/size/metrics/face result. Loading that rewritten record as an ordinary project catalog must fail closed.

- [ ] **Step 2: Run the repository red gate.**

  Run: `cmake --build --preset debug --target test_font_assets test_font -j`

  Expected: compile FAIL because immutable `FontFaceResource` and byte loading do not exist.

- [ ] **Step 3: Change `FontFace` to the exact immutable-byte API.**

  ```cpp
  bool LoadFromBytes(
      std::shared_ptr<const std::vector<std::uint8_t>> bytes,
      std::uint32_t faceIndex, std::string* errorOut = nullptr);
  std::uint32_t FaceIndex() const noexcept;
  std::uint32_t GlyphId(char32_t codepoint) const noexcept;
  bool HasCodepoint(char32_t codepoint) const noexcept;
  ```

- [ ] **Step 3a: Retain bytes for the complete raster-face lifetime.** Store the shared byte owner beside stb state before publishing a loaded face and release stb state before releasing those bytes.

- [ ] **Step 3b: Fence legacy codepoint helpers from new code.** Keep existing codepoint `Advance/Kerning/Rasterize` only for the still-unmigrated legacy renderer until Task 8.2; new shaping code cannot call them. `HasCodepoint`/`GlyphId` remain non-fallback inspection utilities, but shaping selection must use imported coverage, immutable cmap-14 UVS/default-presentation rules, and target-scoped HarfBuzz probes instead.

- [ ] **Step 4: Add the exact repository resource.**

  ```cpp
  struct FontFaceResource {
      std::shared_ptr<const molga::FontAsset> asset;
      std::shared_ptr<const std::vector<std::uint8_t>> bytes;
      std::shared_ptr<const molga::FontFace> rasterFace;
      std::string sourceSha256;
      std::string artifactSha256;
      FontArtifactLocator artifactLocator;
      FontDesignMetrics designMetrics;
      std::uint64_t contentGeneration = 0;
      std::uint32_t faceIndex = 0;
  };
  using FontFaceResourcePtr = std::shared_ptr<const FontFaceResource>;
  class FontRepository {
  public:
      explicit FontRepository(const molga::AssetDatabase&);
      std::optional<FontFaceResourcePtr> Load(
          const std::string& fontGuid, std::uint32_t faceIndex,
          TextDiagnosticSink&) const;
      void Invalidate(const std::string& fontGuid);
  };
  ```

- [ ] **Step 4a: Add the exact scaled-metrics API.** Put the project-facing integer record beside `FontDesignMetrics`:

  ```cpp
  struct ScaledFontDesignMetrics {
      Fixed26_6 ascent = Fixed26_6::FromRaw(0);
      Fixed26_6 descent = Fixed26_6::FromRaw(0);
      Fixed26_6 lineGap = Fixed26_6::FromRaw(0);
  };
  std::optional<ScaledFontDesignMetrics> ScaleFontDesignMetrics(
      const FontDesignMetrics&, Fixed26_6 fontSize) noexcept;
  ```

- [ ] **Step 4b: Implement canonical integer metric scaling.** Require the already-validated metric invariants and positive `fontSize`, then compute `ascent=CheckedMulDiv(fontSize, int64_t(ascender), unitsPerEm)`, `descent=CheckedMulDiv(fontSize, -int64_t(descender), unitsPerEm)`, and `lineGap=CheckedMulDiv(fontSize, int64_t(lineGap), unitsPerEm)` with the shared half-away-from-zero rule. Return `nullopt` on any checked failure; never call rasterizer `Metrics`, convert through float, or round twice.

- [ ] **Step 5: Implement verified resource construction.** Resolve the catalog record and `FontAsset`, obtain the database-bound mode-matched store through `FontArtifacts()`, construct `VerifiedFontArtifact` from its authoritative storage locator/SHA/size, and call only `ReadVerified`. Recompute/compare artifact identity and face index, then create `FontFace` from those same immutable shared bytes; a missing store is `FontInvalid`, and `FontRepository.cpp` contains no `AbsoluteSourcePath`, source-path resolution, or source-file open.

- [ ] **Step 6: Implement repository cache identity.** Cache the complete `FontFaceResource` by `{guid,contentGeneration,sourceSha256,artifactSha256,artifactLocator.storage,artifactLocator.relativePath,artifactByteSize,faceIndex}` and return the identical shared resource for repeated matching loads.

- [ ] **Step 7: Publish content generations after successful import.** Add process-local `AssetDatabase::ContentGeneration(guid)` and increment that GUID only after its successfully published import record changes; apply the same rule to fonts and `.fontfamily` assets.

- [ ] **Step 7a: Invalidate only future repository lookups.** `FontRepository::Invalidate(guid)` removes matching cache ownership so a new load sees the verified replacement; it never mutates an already returned shared resource.

- [ ] **Step 7b: Preserve last-good editor preview on failed import.** Record the failed import diagnostics without publishing a new content generation or replacing the last-good resource; packaged mismatch remains Step 8's hard failure.

- [ ] **Step 8: Enforce artifact mismatch behavior.** In editor restart and packaged runtime alike, when artifact bytes/size/SHA/face differ from catalog metadata, return no resource with `FontInvalid`; never reopen the authoring source, use an editor-only cache, or select a system face.

- [ ] **Step 8a: Fix the Task 17 sealed-package handoff.** After verifying `TextRuntimeManifest`, Task 17 constructs `FontArtifactStore::PackagedAuthority{entry.sourcePath,entry.sourceSha256}` for every manifest font and rewrites each matching filtered catalog record to `artifactStorage="PackagedResource"`, `artifactRelativePath=entry.sourcePath`; it preserves `artifactSha256==entry.sourceSha256` and the recorded byte size. Runtime passes `Contents/Resources` as `runtimeResourceRoot` to `ForSealedPackage`, binds the returned shared store to `AssetDatabase`, then calls `LoadCatalog(...,AssetCatalogMode::SealedPackage)`. The manifest path, sealed-catalog locator, and sole staged SFNT path must be byte-identical after slash normalization; the package must not contain or reference the project `Library/Imported/Fonts` copy.

- [ ] **Step 9: Run repository/lifetime green gates.**

  Run: `cmake --build --preset debug --target test_font_assets test_font test_asset_database -j && ctest --test-dir build/debug -R '^(test_font_assets|test_font|test_asset_database)$' --output-on-failure`

  Expected: old shared owners remain usable, new loads see only verified artifact replacements, corrupt sources cannot affect a published generation, and corrupt artifact/face mismatches are typed failures without source fallback.

- [ ] **Step 10: Commit immutable font resources.**

  ```bash
  git add CMakeLists.txt src/Text/FontRepository.* src/Rendering/FontFace.* \
    src/Assets/FontAsset.* src/Assets/FontArtifactStore.* \
    src/Core/AssetDatabase.* tests/CMakeLists.txt \
    tests/test_font_assets.cpp tests/test_font.cpp
  git commit -m "feat: retain immutable font face resources"
  ```

**Exit:** Every downstream face is bound to immutable verified bytes and an exact face index, independent of later asset database mutation.

---

### Task 5.1: Resolve deterministic family candidates

**Prerequisite:** Task 4.3 provides immutable resources and per-GUID generations.

**Files:**

- Create: `src/Text/FontFamilyResolver.h`
- Create: `src/Text/FontFamilyResolver.cpp`
- Create: `tests/test_font_family.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `FontFamilyAsset`, `FontAsset`, `FontRepository`, and content/family generations.
- Produces: `FontRequest`, `ResolvedFace`, `ResolvedFamilyNode`, `ResolvedFamily`, and `FontFamilyResolver::{BuildCandidates,BuildLegacySingleFace}`.

- [ ] **Step 1: Add the failing sort/DFS test.**

  ```cpp
  TEST_CASE("family candidates use exact lexicographic order and first-visit DFS") {
      FontFamilyFixture f;
      f.database.ScanProject(f.AssetsRoot());
      constexpr const char* kPrimary =
          "11111111111111111111111111111111";
      REQUIRE(f.database.Find(kPrimary) != nullptr);
      const auto resolved = f.resolver.BuildCandidates(
          kPrimary, {500, 100, molga::FontSlant::Upright}, f.sink);
      REQUIRE(resolved);
      CHECK(FontGuids(*resolved) == std::vector<std::string>{
          "44444444444444444444444444444444",
          "55555555555555555555555555555555",
          "12121212121212121212121212121212",
          "13131313131313131313131313131313",
          "66666666666666666666666666666666",
          "77777777777777777777777777777777"});
      CHECK(VisitedFamilyGuids(*resolved) == std::vector<std::string>{
          "11111111111111111111111111111111",
          "22222222222222222222222222222222",
          "33333333333333333333333333333333"});
      CHECK(EachFamilyVisitedOnce(*resolved));
  }
  ```

- [ ] **Step 1a: Add the failing finite-cycle test.**

  ```cpp
  TEST_CASE("family cycle terminates at first visit with a typed diagnostic") {
      FontFamilyFixture f;
      f.database.ScanProject(f.AssetsRoot());
      constexpr const char* kCycleA =
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
      REQUIRE(f.resolver.BuildCandidates(
          kCycleA, {400, 100, molga::FontSlant::Upright}, f.sink));
      CHECK(HasDiagnostic(f.sink, molga::text::TextDiagnosticCode::FontFamilyInvalid));
  }
  ```

- [ ] **Step 1b: Reuse the committed qualification tree without generated metadata.** Include `TextQualificationAssetTree.h`; `FontFamilyFixture` exposes separate `ProjectRoot()` and `AssetsRoot()` delegates. Its constructor creates `FontArtifactStore::ForProject(ProjectRoot())`, binds that shared store successfully to its database, and only then scans `AssetsRoot()`. It never derives one root from the other or writes/rewrites font, license, or family sidecars.

- [ ] **Step 1c: Prove the resolver starts from sidecar authority.** Before constructing `FontFamilyResolver`, rescan `AssetsRoot()` through the already bound project store and require `AssetDatabase::Find("11111111111111111111111111111111")`, `Find("12121212121212121212121212121212")`, and `Find("13131313131313131313131313131313")`; assert their importers/versions are `FontFamilyImporter`/`1`, `FontImporter`/`2`, and `FontImporter`/`2` respectively.

- [ ] **Step 1d: Prove both license references resolve.** In the same fixture, require GUID `88888888888888888888888888888888` and GUID `99999999999999999999999999999999`, assert both records use `GenericImporter` version `1`, and assert every resolved face's `FontAsset::license.licenseAssetGuid` names the correct existing record.

- [ ] **Step 2: Run the resolver red gate.**

  Run: `cmake --build --preset debug --target test_font_family -j`

  Expected: compile FAIL because `FontFamilyResolver` contracts do not exist.

- [ ] **Step 3: Add request and resolved-face values.**

  ```cpp
  struct FontRequest {
      std::uint16_t weight = 400;
      std::uint16_t stretchPercent = 100;
      molga::FontSlant slant = molga::FontSlant::Upright;
  };
  struct ResolvedFace {
      std::string fontGuid;
      std::string fontRevision;
      std::uint32_t faceIndex = 0;
      std::uint32_t authoredFaceIndex = 0;
      FontFaceResourcePtr resource;
  };
  ```

- [ ] **Step 3a: Add resolved closure-node and family values.**

  ```cpp
  struct ResolvedFamilyNode {
      std::string familyGuid;
      bool exists = false;
      std::uint64_t contentGeneration = 0;
      std::vector<std::string> authoredFallbackGuids;
  };
  struct ResolvedFamily {
      std::string requestedGuid;
      std::uint64_t fallbackGraphGeneration = 0;
      std::vector<ResolvedFamilyNode> depthFirstFamilyNodes;
      std::vector<ResolvedFace> candidates;
  };
  ```

- [ ] **Step 3b: Add the exact resolver declaration.**

  ```cpp
  class FontFamilyResolver {
  public:
      FontFamilyResolver(const molga::AssetDatabase&, FontRepository&);
      std::optional<ResolvedFamily> BuildCandidates(
          const std::string& familyGuid, const FontRequest&,
          TextDiagnosticSink&) const;
      std::optional<ResolvedFamily> BuildLegacySingleFace(
          const std::string& fontGuid, const FontRequest&,
          TextDiagnosticSink&) const;
  };
  ```

- [ ] **Step 3c: Wire the resolver qualification root.** After `molga_add_text_test(test_font_family test_font_family.cpp)`, define `MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT` as exact `${CMAKE_SOURCE_DIR}/tests/fixtures/text`; `TextQualificationAssetTree.h` supplies all six font, two license, and five family source/sidecar pairs beneath that root, so no individual path macro or host-font discovery remains.

- [ ] **Step 4: Implement per-family face sorting.** Canonicalize the request, compute `stylePenalty` exact=0, Italic/Oblique-compatible=1, other=2, and sort by `(stylePenalty,abs(stretch-target),abs(weight-target),authoredFaceIndex,fontGuid,faceIndex)` using a stable value comparator.

- [ ] **Step 5: Implement authored-order fallback DFS.** Append primary faces, then visit fallback families depth-first in authored order with one visited GUID set. Record one `ResolvedFamilyNode` on first encounter, including `exists=false` for a missing requested/descendant GUID and the exact authored fallback edge vector for an existing node. A cycle or missing/out-of-range reference emits `FontFamilyInvalid`; first visit makes editor preview finite.

- [ ] **Step 5a: Return finite recoverable family failures.** Return the collected `ResolvedFamily`—possibly with no candidates—for graph/reference/font-content failures so runtime layout can produce typed tofu and later package validation can block it; return `nullopt` only for a hard dependency/state failure that cannot produce a layout.

- [ ] **Step 6: Bind immutable content identity.** Load each candidate only through `FontRepository`, reject missing/mismatched resources, and set `fontRevision = artifactSha256 + ":" + std::to_string(faceIndex)` while also retaining the equal source SHA plus authoritative artifact relative path/size. Keep process-local generation out of the revision string.

- [ ] **Step 7: Compute transitive graph generation.** Fold every visited family generation and descendant face `contentGeneration` into a stable cache invalidator so any transitive edit changes `fallbackGraphGeneration`.

- [ ] **Step 8: Run the resolver green gate.**

  Run: `cmake --build --preset debug --target test_font_family test_font_assets -j && ctest --test-dir build/debug -R '^(test_font_family|test_font_assets)$' --output-on-failure`

  Expected: deterministic order, finite cycles, missing-reference diagnostics, and descendant invalidation all pass.

- [ ] **Step 9: Commit deterministic resolution.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/FontFamilyResolver.* \
    tests/test_font_family.cpp
  git commit -m "feat: resolve deterministic font families"
  ```

**Exit:** Shaping receives one finite, content-bound candidate list whose ordering cannot depend on hash-map iteration or host fonts.

---

### Task 5.2: Shape grapheme-atomic context runs with HarfBuzz

**Prerequisite:** Task 5.1 candidates and Milestone 3 analysis/source-byte maps are green.

**Files:**

- Create: `src/Text/TextShapingService.h`
- Create: `src/Text/TextShapingService.cpp`
- Create: `tests/test_text_shaping.cpp`
- Create: `tests/test_text_shaping_not_ready.cpp`
- Create: `tests/fixtures/text/expected/shaping.json`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `UnicodeTextBuffer`, `UnicodeAnalysis`, `ResolvedFamily`, immutable resources, and pinned HarfBuzz/ICU Unicode funcs.
- Produces: `ShapeFeature`, `ShapeStyle`, `ShapeBoundaryFlags`, `ShapedGlyph`, `ShapedRun`, and `TextShapingService::ShapeAnalysisItem`.

- [ ] **Step 1: Define the explicit one-face/script reference runs.** Add this test-only record and exact run table; no row contains a family or fallback selector:

  ```cpp
  struct ExplicitReferenceRun {
      std::filesystem::path facePath;
      std::string fontGuid;
      std::uint32_t faceIndex;
      std::string utf8;
      hb_direction_t direction;
      hb_script_t script;
      std::string language;
      molga::Fixed26_6 fontSize;
      molga::text::ShapeBoundaryFlags boundaries;
      std::vector<molga::text::ShapeFeature> features;
  };
  std::array<ExplicitReferenceRun, 3> ExplicitPinnedRuns() {
      const std::filesystem::path root = MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT;
      return {{
          {root / "fonts/NotoSans-Regular.ttf",
           "44444444444444444444444444444444", 0, u8"ffi",
           HB_DIRECTION_LTR, HB_SCRIPT_LATIN, "en",
           molga::Fixed26_6::FromRaw(16 * 64), {true, true},
           {{HB_TAG('l','i','g','a'), 1, {0, 3}}}},
          {root / "fonts/NotoSansArabic-Regular.ttf",
           "66666666666666666666666666666666", 0, u8"سلام",
           HB_DIRECTION_RTL, HB_SCRIPT_ARABIC, "ar",
           molga::Fixed26_6::FromRaw(16 * 64), {true, true}, {}},
          {root / "fonts/NotoSansDevanagari-Regular.ttf",
           "12121212121212121212121212121212", 0, u8"क्षि",
           HB_DIRECTION_LTR, HB_SCRIPT_DEVANAGARI, "hi",
           molga::Fixed26_6::FromRaw(16 * 64), {true, true}, {}},
      }};
  }
  ```

- [ ] **Step 1a: Wire the exact shaping fixture paths.** Create ready target `test_text_shaping` through `molga_add_text_test`, whose Task 2.2 implementation already performs the idempotent normal dependency attach; define `MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures/text"` and `MOLGA_TEXT_EXPECTED_SHAPING="${CMAKE_SOURCE_DIR}/tests/fixtures/text/expected/shaping.json"` after target creation. Create `test_text_shaping_not_ready` through generic `molga_add_test` with no text-session main, staged root, or initialization; link its test-access counter companion only there. Process isolation replaces every stop/restore/serial-lifecycle seam.

- [ ] **Step 1b: Build the direct one-face HarfBuzz handle path.** `DirectHarfBuzzSingleFaceRecord` reads only `run.facePath`, hashes those bytes, constructs `hb_blob_t`, `hb_face_t(run.faceIndex)`, `hb_font_t`, and `hb_buffer_t`, installs `hb_ot_font_set_funcs`, scale from `run.fontSize.Raw()`, `hb_icu_get_unicode_funcs()`, the row's direction/script/language, monotone-character clusters, and exactly the row's BOT/EOT flags. It never constructs `FontFamilyResolver`, calls coverage preflight, or examines another face.

- [ ] **Step 1c: Shape and serialize the direct record.** Feed exactly `run.utf8` through `hb_buffer_add_utf8`, convert each row feature without reordering, call `hb_shape`, and serialize ordered objects containing `{fontSha,faceIndex,glyphId,cluster,advanceX,advanceY,offsetX,offsetY}`. Destroy buffer, font, face, then blob on every exit through RAII.

- [ ] **Step 1d: Add the failing production/direct parity test.**

  ```cpp
  TEST_CASE("production shaper matches pinned HarfBuzz reference records") {
      nlohmann::ordered_json records = nlohmann::ordered_json::array();
      for (const ExplicitReferenceRun& run : ExplicitPinnedRuns()) {
          const auto reference = DirectHarfBuzzSingleFaceRecord(run);
          const auto production = ProductionSingleResolvedFaceRecord(run);
          CHECK(production == reference);
          records.push_back(production);
      }
      CHECK(records.dump(2) == ReadFile(MOLGA_TEXT_EXPECTED_SHAPING));
  }
  ```

  `ProductionSingleResolvedFaceRecord` passes a `ResolvedFamily` containing only the explicit locked face. It never calls production family/fallback selection.

- [ ] **Step 1e: Add the failing grapheme-atomic fallback test.**

  ```cpp
  TEST_CASE("fallback never splits an extended grapheme") {
      auto fixture = LoadTextFixture(u8"Aक्‍षB", "und");
      const auto shaped = fixture.ShapeFirstItem();
      REQUIRE(shaped);
      CHECK(AllGlyphsForGraphemeUseOneFace(*shaped, 1));
      CHECK(AllClustersMapToOriginalBytes(*shaped));
  }
  ```

- [ ] **Step 1f: Add the failing resource-free missing-grapheme test.**

  ```cpp
  TEST_CASE("all-face failure emits one resource-free grapheme record") {
      auto fixture = LoadTextFixture(u8"👩‍🚀", "und");
      fixture.UseResolvedFamilyWithNoCandidates();
      const auto shaped = fixture.ShapeFirstItem();
      REQUIRE(shaped);
      const auto glyphs = FlattenGlyphs(*shaped);
      REQUIRE(glyphs.size() == 1);
      CHECK(glyphs[0].missing);
      CHECK_FALSE(glyphs[0].faceResource);
      CHECK(glyphs[0].sourceBytes.begin == 0);
      CHECK(glyphs[0].sourceBytes.end == fixture.OriginalUtf8().size());
      CHECK(glyphs[0].graphemes.begin == 0);
      CHECK(glyphs[0].graphemes.end == 1);
      CHECK(glyphs[0].advanceX == fixture.ShapeStyle().fontSize);
      CHECK(HasDiagnostic(fixture.sink,
            molga::text::TextDiagnosticCode::MissingGlyph));
  }
  ```

  Grapheme fallback behavior is verified only by Steps 1e–1f, preventing the direct reference helper from duplicating production selection logic.

- [ ] **Step 1g: Add the failing fresh-process not-ready HarfBuzz test.** Put this case in `test_text_shaping_not_ready.cpp`; its generic test main begins in `NeverInitialized`, and `BuildNotReadyShapingRequestWithoutHarfBuzz` constructs only immutable project-owned request/resource values:

  ```cpp
  TEST_CASE("shaper creates no HarfBuzz object before runtime ready") {
      auto fixture = BuildNotReadyShapingRequestWithoutHarfBuzz("A", "und");
      ResetHarfBuzzObjectCreationCountForTest();
      CHECK_FALSE(fixture.ShapeFirstItem());
      CHECK(HarfBuzzObjectCreationCountForTest() == 0);
      CHECK(HasDiagnostic(fixture.sink,
            molga::text::TextDiagnosticCode::DependencyInvalid));
  }
  ```

  The target never calls `Initialize`, `Shutdown`, `hb_icu_get_unicode_funcs`,
  or `u_cleanup`; failure occurs at the service ready gate before any HarfBuzz
  blob/face/font/buffer allocation, and process exit supplies isolation.

- [ ] **Step 1h: Add the failing default-ignorable/variation-selector test.**

  ```cpp
  TEST_CASE("join controls and supported variation selectors do not force fallback") {
      auto join = LoadTextFixture(u8"ن\u200Dن", "ar");
      const auto joined = join.ShapeFirstItem();
      REQUIRE(joined);
      CHECK_FALSE(AnyMissingGlyph(*joined));
      CHECK(join.CmapRequirementCount(U'\u200D') == 0);

      auto variation = LoadTextFixture(u8"！\uFE00", "ja");
      const auto varied = variation.ShapeFirstItem();
      REQUIRE(varied);
      CHECK_FALSE(AnyMissingGlyph(*varied));
      CHECK(AllGlyphsForGraphemeUseOneFace(*varied, 0));
      CHECK(FaceGuidForGrapheme(*varied, 0) ==
            "77777777777777777777777777777777");
      CHECK(variation.CmapRequirementCount(U'\uFE00') == 0);
  }
  ```

- [ ] **Step 1i: Add the failing target-only-notdef/context-reshape test.**

  ```cpp
  TEST_CASE("surrounding notdef does not reject target and selection reshapes context") {
      auto fixture = LoadTextFixture(u8"👩 क्षि", "hi");
      fixture.RestrictCandidatesToFont(
          "12121212121212121212121212121212");
      const auto shaped = fixture.ShapeFirstItem();
      REQUIRE(shaped);
      CHECK(fixture.ProbeNotdefIntersectsGrapheme(0));
      CHECK_FALSE(fixture.ProbeNotdefIntersectsGrapheme(2));
      CHECK(FaceGuidForGrapheme(*shaped, 2) ==
            "12121212121212121212121212121212");
      CHECK(fixture.FinalSelectedContextShapeCountForGrapheme(2) == 1);
      CHECK(fixture.FinalSelectedContextCoveredCompleteItem(2));
  }
  ```

- [ ] **Step 1j: Add the failing invalid-byte cluster test.**

  ```cpp
  TEST_CASE("sanitized replacement glyph clusters remain original byte starts") {
      auto fixture = LoadTextFixture(std::string("A\xF0\x28\x8C\x28Z", 6),
                                     "und");
      const auto shaped = fixture.ShapeFirstItem();
      REQUIRE(shaped);
      CHECK(AllClustersAreDecodedScalarByteStarts(*shaped, fixture.Buffer()));
      CHECK(HasGlyphSourceRange(*shaped, {1, 2}));
      CHECK_FALSE(AnyClusterIsUtf16OnlyOffset(*shaped, fixture.Buffer()));
  }
  ```

- [ ] **Step 1k: Add the failing exact-level/logical-run propagation test.**

  ```cpp
  TEST_CASE("every shaped glyph retains its exact analysis level and run") {
      auto fixture = LoadTextFixture(u8"abc שלום 123!", "und");
      const auto shaped = fixture.ShapeAllItems();
      REQUIRE(shaped);
      for (const auto& glyph : FlattenGlyphs(*shaped)) {
          const auto& item = fixture.AnalysisItemAtByte(glyph.sourceBytes.begin);
          CHECK(glyph.bidiLevel == item.embeddingLevel);
          CHECK(glyph.logicalRunId == item.logicalRunId);
      }
  }
  ```

- [ ] **Step 1l: Implement focused shaping test counters.** Count cmap requirements only at preflight, record each probe `.notdef` source range, and count the post-selection complete-context shape separately from candidate probes. `AllClustersAreDecodedScalarByteStarts` compares output clusters only with `DecodedScalar::sourceBytes.begin`; `AnyClusterIsUtf16OnlyOffset` reports an offset that is a UTF-16 boundary but not an original scalar byte start. Test instrumentation observes the production wrappers without changing selection results.

- [ ] **Step 1m: Add the failing maximal-fallback-span test.** Use a primary Latin-only candidate followed by the locked Arabic fallback and shape `u8"لا"`; both adjacent graphemes select the same fallback face. Require `FinalSelectedSpanCount()==1`, `FinalShapeCallsForSelectedSpan()==1`, output equals a direct one-face Arabic reference with the same BOT/EOT/style, and emitted original byte/grapheme ranges form one exact nonoverlapping cover with no duplicate or dropped glyph record.

- [ ] **Step 1n: Add the failing adjusted-GDEF-caret test.** Shape locked Latin `u8"ffi"` with `liga=1`, query the same pinned face directly through `hb_ot_layout_get_ligature_carets`, and require the ligature glyph's stored `adjustedGdefCaretOffsets` exactly equal the direct adjusted/scaled raw positions in returned order. A face/glyph with no GDEF carets stores an empty vector, not synthesized positions.

- [ ] **Step 2: Run the shaping red gate.**

  Run: `cmake --build --preset debug --target test_text_shaping -j`

  Expected: compile FAIL because `TextShapingService` and shaped output types do not exist.

- [ ] **Step 3: Add exact shaping input records.**

  ```cpp
  struct ShapeFeature {
      std::uint32_t tag = 0;
      std::uint32_t value = 0;
      SourceByteRange sourceBytes;
  };
  enum class TextClusterPolicy : std::uint8_t {
      MonotoneCharacters = 1
  };
  struct ShapeStyle {
      Fixed26_6 fontSize = Fixed26_6::FromRaw(16 * 64);
      std::string language = "und";
      std::vector<ShapeFeature> orderedFeatures;
      TextClusterPolicy clusterPolicy =
          TextClusterPolicy::MonotoneCharacters;
  };
  struct ShapeBoundaryFlags { bool beginningOfText = false; bool endOfText = false; };
  ```

- [ ] **Step 3a: Add the exact owned glyph record.**

  ```cpp
  struct ShapedGlyph {
      std::string fontGuid;
      std::string fontRevision;
      std::uint32_t faceIndex = 0;
      std::uint32_t glyphId = 0;
      FontFaceResourcePtr faceResource;
      Fixed26_6 fontSize = Fixed26_6::FromRaw(0);
      Fixed26_6 advanceX = Fixed26_6::FromRaw(0);
      Fixed26_6 advanceY = Fixed26_6::FromRaw(0);
      Fixed26_6 offsetX = Fixed26_6::FromRaw(0);
      Fixed26_6 offsetY = Fixed26_6::FromRaw(0);
      SourceByteRange sourceBytes;
      GraphemeRange graphemes;
      std::uint8_t bidiLevel = 0;
      std::uint32_t logicalRunId = 0;
      std::uint32_t harfbuzzGlyphFlags = 0;
      std::vector<Fixed26_6> adjustedGdefCaretOffsets;
      bool missing = false;
  };
  ```

  Every emitted normal or missing glyph copies the exact `ShapeStyle::fontSize`;
  layout and rendering never infer it from advances, bitmap bounds, or affine
  scale.

- [ ] **Step 3b: Add the run and service declarations.**

  ```cpp
  struct ShapedRun {
      std::vector<ShapedGlyph> glyphs;
      std::uint8_t bidiLevel = 0;
  };
  class TextShapingService {
  public:
      std::optional<std::vector<ShapedRun>> ShapeAnalysisItem(
          const UnicodeTextBuffer&, const UnicodeAnalysis&,
          const AnalysisItem&, const ResolvedFamily&, const ShapeStyle&,
          ShapeBoundaryFlags, TextDiagnosticSink&);
  };
  ```

- [ ] **Step 4: Enforce the shaper ready gate.** Before creating a blob, face, font, or buffer, require `TextRuntimeDependencies::Get().IsReady()`; otherwise emit one `DependencyInvalid` and return `nullopt`.

- [ ] **Step 4a: Implement resource-owned HarfBuzz handles.** Create blob/face/font directly from `ResolvedFace::resource->bytes` and exact face index, keep a `FontFaceResourcePtr` beside the handles until destruction, and never reopen a path or GUID.

- [ ] **Step 5: Map the project cluster policy privately.** Only `TextShapingService.cpp` includes/mentions `hb_buffer_cluster_level_t` or `HB_BUFFER_CLUSTER_LEVEL_*`. Add a closed `ToHarfBuzzClusterLevel(TextClusterPolicy)` switch mapping `MonotoneCharacters` to `HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS`; an invalid enum value emits `LayoutInvalid` and returns before shaping.

- [ ] **Step 5a: Configure HarfBuzz Unicode/context fields.** Assign `hb_icu_get_unicode_funcs()`, explicit direction, script, language, font size, the privately mapped cluster level, and exact BOT/EOT flags before shaping.

- [ ] **Step 5b: Feed sanitized ranges with real surrounding context.** Build one `std::vector<hb_codepoint_t>` for the complete sanitized `AnalysisItem`, call `hb_buffer_add_codepoints(buffer, values.data(), values.size(), targetScalarBegin, targetScalarCount)`, then fetch the still-unshaped input infos and replace each public `cluster` with its target scalar's `sourceBytes.begin`. Require input-info count equals `targetScalarCount`; never feed ill-formed original bytes, a UTF-16 offset, or a sanitized-array index as the final cluster.

- [ ] **Step 5c: Apply ordered source-range features.** Convert each `ShapeFeature` tag/value and original-byte start/end to one ordered `hb_feature_t`; retain exact overlap/order instead of normalizing feature ranges.

- [ ] **Step 6: Convert HarfBuzz output to owned records.** Copy glyph ID, 26.6 advances/offsets, source byte/grapheme range, exact level/logical run, `hb_glyph_info_get_glyph_flags(info)`, and the same `FontFaceResourcePtr` into each `ShapedGlyph`. Layout treats `HB_GLYPH_FLAG_UNSAFE_TO_BREAK` as an authoritative prohibited boundary.

- [ ] **Step 6a: Capture final adjusted GDEF carets.** For every glyph from a final selected-span shape, use that exact scaled `hb_font_t`, final direction, and glyph ID in the two-call `hb_ot_layout_get_ligature_carets` count/fill protocol. Checked-convert each returned `hb_position_t` directly to `Fixed26_6`, preserve HarfBuzz order, and store it in `adjustedGdefCaretOffsets`; allocation/status/count inconsistency is `LayoutInvalid`, while a valid zero count stores an empty vector.

- [ ] **Step 7: Implement coverage preflight.** Require candidate coverage for each grapheme's essential scalars; exempt join controls/ZWJ/default ignorables and accept a variation selector only through explicit UVS mapping or documented default presentation.

- [ ] **Step 7a: Make variation decisions deterministic.** Inspect the selected face's cmap format-14 default and non-default UVS records from its immutable bytes. Accept `(base,selector)` only when the pair has an explicit/default UVS record or the selector requests the Unicode-defined default presentation of the covered base; never require a standalone selector glyph or consult a host font.

- [ ] **Step 8: Implement target-scoped probe shaping.** Shape the complete `AnalysisItem` for each candidate and reject it only when a `.notdef` output source range intersects the target grapheme; surrounding-context `.notdef` does not reject the target face.

- [ ] **Step 9: Finish face selection before final shaping.** Run coverage/probe selection for every grapheme and record `{grapheme/source range, immutable face identity or missing, exact level,script,direction,language,ShapeStyle}` without emitting final glyphs. Candidate probes are not final output and may not populate the shape cache.

- [ ] **Step 9a: Coalesce maximal selected spans.** In one logical pass, merge adjacent selected graphemes iff face resource identity, face index/revision, exact level, script, direction, language, font size, cluster policy, and ordered features are equal and no paragraph/isolate/control boundary lies between them. Preserve a missing grapheme as its own procedural record; derive BOT/EOT from each final span's real text endpoints and do not reorder spans into visual order here.

- [ ] **Step 9b: Build sanitized final-span context.** For each nonmissing span, slice the containing `AnalysisItem` at paragraph/isolate/control boundaries into one full sanitized codepoint array and derive the span's scalar offset/count inside it. Invoke the exact Step 5b `hb_buffer_add_codepoints(fullArray,fullCount,spanOffset,spanCount)` path so pinned HarfBuzz retains sanitized pre/post context, then rewrite input clusters to original UTF-8 byte starts before `hb_shape`; do not call nonexistent pre/post-context setters or feed ill-formed original bytes.

- [ ] **Step 9c: Shape each final span exactly once.** Call `hb_shape` once for each coalesced nonmissing span, increment the final-span counter at that call site, and copy each returned info/position record once; candidate probe results are never copied into final output.

- [ ] **Step 9d: Restore original ranges for final glyphs.** Resolve each final HarfBuzz cluster extent through `UnicodeTextBuffer` into exact original source-byte and grapheme ranges, rejecting a non-scalar-start cluster or range outside its selected span with `LayoutInvalid`.

- [ ] **Step 9e: Validate the selected/output partitions.** Before return, require selected grapheme spans to form a gap-free nonoverlapping cover of the `AnalysisItem`; require every nonmissing output record to belong to exactly one span and prohibit duplicate source/glyph records. Emit one `LayoutInvalid` and return `nullopt` on the first invariant failure.

- [ ] **Step 10: Emit deterministic missing glyphs.** After all candidates fail, output one `missing=true` record covering the complete grapheme with `advanceX == style.fontSize` (exact `1 em`), null `faceResource`, and `MissingGlyph`; no ASCII/system-font call is permitted.

- [ ] **Step 11: Prove stb is raster-only.** Add a test counter/assertion around legacy `FontFace::Advance/Kerning` and require zero calls during every `TextShapingService` test.

- [ ] **Step 12: Review reference JSON and run sanitizer green gates.**

  ```bash
  cmake --build --preset debug --target test_font_family test_text_shaping test_text_shaping_not_ready -j
  ctest --test-dir build/debug -R '^(test_font_family|test_text_shaping|test_text_shaping_not_ready)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_text_shaping -j
  ctest --test-dir build/asan -R '^test_text_shaping$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_text_shaping -j
  ctest --test-dir build/ubsan -R '^test_text_shaping$' --output-on-failure
  ```

  Expected: Arabic/Indic contextual forms, adjacent same-fallback ligatures, Noto Sans `fi/ffi` plus adjusted GDEF carets, invalid UTF-8 clusters, exact levels, target-only `.notdef`, and reference bytes all pass.

- [ ] **Step 13: Commit shaping.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextShapingService.* \
    src/Rendering/FontFace.* tests/test_text_shaping.cpp \
    tests/test_text_shaping_not_ready.cpp \
    tests/fixtures/text/expected/shaping.json
  git commit -m "feat: shape grapheme-safe font runs"
  ```

**Exit:** Every shaped record is content-bound, source-byte-addressable, and independent of OS font/shaper behavior.

---

### Task 6.1: Replace the codepoint atlas key with glyph-ID pages and a hard budget

**Prerequisite:** Task 5.2 produces shaped glyph IDs bound to immutable face resources; production consumers still use their legacy adapter until Task 8.2.

**Files:**

- Create: `tests/test_glyph_atlas.cpp`
- Modify: `src/Rendering/FontAtlas.h`
- Modify: `src/Rendering/FontAtlas.cpp`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `ShapedGlyph::glyphId`, `FontFaceResource::rasterFace`, and `TextDiagnosticSink`.
- Produces: `GlyphAtlasKey`, `GlyphAtlasKeyHash`, `GlyphInfo`, `GlyphHandle`, `GlyphAtlasTelemetry`, and `GlyphAtlasCache::{DefaultResidentBudgetBytes,SetResidentBudget,BeginFrame,GetGlyph,EndCollection,Telemetry,LiveExternalPagePinCount,ReleaseAfterGpuIdle}`.

- [ ] **Step 1: Add the failing complete-key and glyph-raster tests.**

  ```cpp
  TEST_CASE("every logical glyph field participates in atlas identity") {
      GlyphAtlasFixture f;
      f.cache.BeginFrame(1);
      molga::GlyphAtlasKey key = f.KeyForGlyph(42);
      const auto baseline = f.cache.GetGlyph(key, f.FaceForKey(key), f.sink);
      REQUIRE_FALSE(baseline.proceduralTofu);
      for (const auto& changed : MutateEachLogicalKeyField(key)) {
          CHECK_FALSE(changed == key);
          CHECK(molga::GlyphAtlasKeyHash{}(changed) !=
                molga::GlyphAtlasKeyHash{}(key));
          const auto before = f.cache.Telemetry();
          const auto other =
              f.cache.GetGlyph(changed, f.FaceForKey(changed), f.sink);
          CHECK_FALSE(other.proceduralTofu);
          CHECK(f.cache.Telemetry().misses == before.misses + 1);
          CHECK(f.cache.Telemetry().uploads == before.uploads + 1);
          CHECK(other.glyph.glyphId == changed.glyphId);
          CHECK(f.cache.LastUploadedKeyForTest() == changed);
          CHECK(f.FaceForKey(changed).LastRasterizedGlyphId() == changed.glyphId);
      }
      f.cache.EndCollection(1);
  }
  ```

- [ ] **Step 1a: Add the failing resident-budget test.**

  ```cpp
  TEST_CASE("atlas never exceeds its configured page budget") {
      GlyphAtlasFixture f(2 * GlyphAtlasFixture::PageBytes);
      f.cache.BeginFrame(1);
      f.FillWithDistinctGlyphs();
      f.cache.EndCollection(1);
      CHECK(f.cache.Telemetry().residentBytes <= 2 * GlyphAtlasFixture::PageBytes);
      CHECK(f.cache.Telemetry().peakResidentBytes <= 2 * GlyphAtlasFixture::PageBytes);
      CHECK(molga::GlyphAtlasCache::DefaultResidentBudgetBytes ==
            64ULL * 1024ULL * 1024ULL);
  }
  ```

- [ ] **Step 1b: Add the failing zero-capacity test.**

  ```cpp
  TEST_CASE("zero atlas budget is zero capacity") {
      GlyphAtlasFixture f(0);
      f.cache.BeginFrame(1);
      const auto handle = f.ResolveGlyph(9);
      CHECK(handle.proceduralTofu);
      CHECK(handle.pageIdentity == 0);
      CHECK_FALSE(handle.pageLifetime);
      CHECK(f.cache.Telemetry().uploads == 0);
      CHECK(f.cache.Telemetry().residentBytes == 0);
      CHECK(HasDiagnostic(f.sink,
            molga::text::TextDiagnosticCode::AtlasExhausted));
      f.cache.EndCollection(1);
  }
  ```

- [ ] **Step 1c: Add the failing cache-owner eviction test.**

  ```cpp
  TEST_CASE("cache ownership is not mistaken for an external page pin") {
      GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
      f.cache.BeginFrame(1);
      auto first = f.ResolveGlyph(1);
      const auto firstPage = first.pageIdentity;
      f.cache.EndCollection(1);
      first.pageLifetime.reset();
      f.cache.BeginFrame(2);
      f.ResolveGlyph(2);
      CHECK_FALSE(f.cache.IsPageResident(firstPage));
      f.cache.EndCollection(2);
  }
  ```

- [ ] **Step 1d: Add the failing current-collection pin test.**

  ```cpp
  TEST_CASE("current collection pin blocks eviction without an external owner") {
      GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
      f.cache.BeginFrame(1);
      auto first = f.ResolveGlyph(1);
      const auto firstPage = first.pageIdentity;
      first.pageLifetime.reset();
      CHECK(f.ResolveGlyph(2).proceduralTofu);
      CHECK(f.cache.IsPageResident(firstPage));
      f.cache.EndCollection(1);
  }
  ```

- [ ] **Step 1e: Add the failing external-owner pin test.**

  ```cpp
  TEST_CASE("external page owner blocks eviction after collection ends") {
      GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
      f.cache.BeginFrame(1);
      auto first = f.ResolveGlyph(1);
      const auto firstPage = first.pageIdentity;
      f.cache.EndCollection(1);
      f.cache.BeginFrame(2);
      CHECK(f.ResolveGlyph(2).proceduralTofu);
      CHECK(f.cache.IsPageResident(firstPage));
      f.cache.EndCollection(2);
  }
  ```

- [ ] **Step 1f: Add the failing page-identity exhaustion test.**

  ```cpp
  TEST_CASE("page identity exhaustion fails closed without reuse") {
      GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
      f.SetNextPageIdentityForTest(UINT64_MAX);
      f.cache.BeginFrame(1);
      auto last = f.ResolveGlyph(1);
      CHECK(last.pageIdentity == UINT64_MAX);
      f.cache.EndCollection(1);
      last.pageLifetime.reset();
      f.cache.BeginFrame(2);
      const auto exhausted = f.ResolveGlyph(2);
      CHECK(exhausted.proceduralTofu);
      CHECK(exhausted.pageIdentity == 0);
      CHECK(HasDiagnostic(f.sink,
            molga::text::TextDiagnosticCode::AtlasExhausted));
      f.cache.EndCollection(2);
  }
  ```

- [ ] **Step 1g: Add the failing external-pin teardown test.** End collection
  while retaining one returned `pageLifetime`; require
  `LiveExternalPagePinCount()==1`, `ReleaseAfterGpuIdle()==false`, and unchanged
  resident bytes/pages. Reset that exact token, require the count becomes zero,
  then require `ReleaseAfterGpuIdle()==true` and zero resident bytes/pages.
  Repeat with cache ownership alone and require it is never counted external.

- [ ] **Step 1h: Implement the atlas test-only fixtures.** `MutateEachLogicalKeyField` alters exactly one of the eight fields and returns the expected key label; `FaceForKey` returns a fixture face whose face index matches that key while tracking its requested glyph ID. `OneGlyphPerPage()` uses a deterministic one-cell page allocator and one-page resident budget, so each new glyph needs a new page identity; production packing remains unrestricted.

- [ ] **Step 1i: Register the atlas test with the common runtime main.** Call `molga_add_text_test(test_glyph_atlas test_glyph_atlas.cpp)`; its idempotent helper owns normal dependency attachment. Do not link `doctest_main`, call the attach helper again, or stage a second runtime root.

- [ ] **Step 2: Run the CPU atlas red gate.**

  Run: `cmake --build --preset debug --target test_glyph_atlas -j`

  Expected: compile FAIL because `GlyphAtlasKey`, glyph-ID rasterization, and telemetry do not exist.

- [ ] **Step 3: Add the exact atlas-key contract.**

  ```cpp
  enum class GlyphRenderMode : std::uint8_t { Monochrome };
  struct GlyphAtlasKey {
      std::string fontGuid;
      std::string fontRevision;
      std::uint32_t faceIndex = 0;
      std::uint16_t pixelSize = 0;
      std::uint16_t rasterScaleKey = 64;
      std::uint64_t variationKey = 0;
      GlyphRenderMode renderMode = GlyphRenderMode::Monochrome;
      std::uint32_t glyphId = 0;
      bool operator==(const GlyphAtlasKey&) const;
  };
  struct GlyphAtlasKeyHash {
      std::size_t operator()(const GlyphAtlasKey&) const noexcept;
  };
  ```

- [ ] **Step 3a: Add the exact glyph-info and lifetime handle.**

  ```cpp
  struct GlyphInfo {
      Texture* texture = nullptr;
      std::uint32_t glyphId = 0;
      int pageIndex = -1;
      float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
      int width = 0, height = 0, xOffset = 0, yOffset = 0;
      bool drawable = false;
  };
  using FontAtlasGlyph = GlyphInfo; // removed with the legacy adapter in Task 8.2
  struct GlyphHandle {
      GlyphInfo glyph;
      std::uint64_t pageIdentity = 0;
      std::shared_ptr<const void> pageLifetime;
      bool proceduralTofu = false;
  };
  ```

- [ ] **Step 3b: Add telemetry and cache declarations.**

  ```cpp
  struct GlyphAtlasTelemetry {
      std::uint64_t hits = 0, misses = 0, uploads = 0, evictions = 0;
      std::uint64_t residentBytes = 0, peakResidentBytes = 0;
  };
  class GlyphAtlasCache {
  public:
      static constexpr std::uint64_t DefaultResidentBudgetBytes =
          64ULL * 1024ULL * 1024ULL;
      void SetResidentBudget(std::uint64_t bytes);
      void BeginFrame(std::uint64_t frameIndex);
      GlyphHandle GetGlyph(const GlyphAtlasKey&, const FontFace&,
                           molga::text::TextDiagnosticSink&);
      void EndCollection(std::uint64_t frameIndex);
      const GlyphAtlasTelemetry& Telemetry() const noexcept;
      std::size_t LiveExternalPagePinCount() const noexcept;
      bool ReleaseAfterGpuIdle() noexcept;
  };
  ```

- [ ] **Step 4: Keep the legacy adapter isolated until the atomic migration.** Leave the existing `FontAtlasCache` class and its codepoint-facing methods compiled only for the unchanged pre-Milestone-8 consumers; it does not call or populate `GlyphAtlasCache`. New shaping/layout code may depend only on `GlyphAtlasCache`. Task 8.2 deletes `FontAtlasCache`, rather than silently adapting one consumer early.

- [ ] **Step 5: Add glyph-ID-only rasterization.** Implement `FontGlyphBitmap FontFace::RasterizeGlyph(std::uint32_t glyphId, std::uint16_t pixelHeight, std::uint16_t rasterScaleKey) const`. It returns bitmap bounds/bearing/coverage only; logical advance and offsets remain the HarfBuzz values already stored in `ShapedGlyph`. This const signature matches `GlyphAtlasCache::GetGlyph(const GlyphAtlasKey&, const FontFace&, TextDiagnosticSink&)`.

- [ ] **Step 6: Implement exact resident accounting.** Each monochrome GPU page contributes `width * height * bytesPerPixel`; default budget is `64ULL * 1024ULL * 1024ULL`, and `SetResidentBudget(0)` means zero capacity rather than unlimited.

- [ ] **Step 7: Implement the never-reused page ID allocator.** Give each newly created page a process-lifetime nonzero `pageIdentity`; detect `uint64_t` exhaustion before increment/wrap and return tofu plus `AtlasExhausted` rather than zero or reuse.

- [ ] **Step 7a: Separate cache and external page ownership.** A `PageRecord` owns `shared_ptr<PageResource>`; one `PageLifetimeToken` also owns that resource, while the record stores only `weak_ptr<const PageLifetimeToken> externalToken`. No eviction decision may inspect `PageResource::shared_ptr::use_count()`.

- [ ] **Step 7b: Track current-collection ownership explicitly.** Store the current collection's page-ID set; `GetGlyph` inserts the ID and returns or renews the page's one external token, while `EndCollection` clears only that set.

- [ ] **Step 7c: Keep raster work synchronous or resource-owned.** Complete `RasterizeGlyph` and the GPU page upload inside `GetGlyph` before returning. Do not queue a raw `FontFace&`; if a future backend needs deferred work, its pending record must instead retain the exact `FontFaceResourcePtr` and is outside this task's synchronous implementation.

- [ ] **Step 7d: Separate external-pin detection from device-safe release.**
  `LiveExternalPagePinCount` counts nonexpired weak external page-token records
  in stable page-ID order and ignores cache/current-collection ownership; it
  never uses `shared_ptr::use_count()`. `ReleaseAfterGpuIdle` returns false
  before mutation while a collection is active or that count is nonzero;
  otherwise it clears all page/cache GPU ownership and resets resident bytes
  while preserving monotonic telemetry and the never-reused process page-ID
  allocator. Task 11 calls it only after a successful device idle/fence drain
  and after engine command/snapshot owners are released.

- [ ] **Step 8: Implement page-level LRU eviction.** Before allocation, evict the least-recently-used page only when its ID is absent from current-collection pins and its external pin weak pointer is expired. Cache ownership alone never blocks eviction; update resident bytes immediately after destruction.

- [ ] **Step 9: Implement saturation tofu.** If no legal page fits after eviction, return a procedural monochrome tofu handle with `pageIdentity=0`, report rate-limited `AtlasExhausted`, and leave resident bytes unchanged.

- [ ] **Step 10: Implement atlas telemetry.** Update hits, misses, uploads, evictions, current resident, and high-water peak at the single lookup/allocation/eviction points; no caller recomputes telemetry.

- [ ] **Step 11: Run the CPU atlas green gate.**

  Run: `cmake --build --preset debug --target test_glyph_atlas test_font -j && ctest --test-dir build/debug -R '^(test_glyph_atlas|test_font)$' --output-on-failure`

  Expected: full-key identity, glyph-ID rasterization, face/revision separation, page LRU, tofu, telemetry, and the 64 MiB default all pass.

- [ ] **Step 12: Commit the CPU glyph atlas.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Rendering/FontAtlas.* \
    src/Rendering/FontFace.* tests/test_glyph_atlas.cpp
  git commit -m "feat: cache rasterized glyph IDs"
  ```

**Exit:** The atlas is shaped-glyph-addressed and memory-bounded, but GPU submission ownership is deliberately handled by Task 6.2.

---

### Task 6.2: Retire submitted page lifetimes only after GPU fences

**Prerequisite:** Task 6.1 pages expose shared lifetime tokens.

**Files:**

- Create: `src/Rendering/GpuRetirementQueue.h`
- Create: `src/Rendering/GpuRetirementQueue.cpp`
- Create: `tests/test_gpu_retirement.cpp`
- Modify: `src/Rendering/Renderer.h`
- Modify: `src/Rendering/Renderer.cpp`
- Modify: `src/Rendering/GraphicsDevice.h`
- Modify: `src/Rendering/GraphicsDevice.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: SDL_GPU submission fence creation/polling and atlas page tokens.
- Produces: `IGpuCompletionFence`, `GpuRetirementQueue::{Enqueue,RetainWithoutFence,Poll,DrainAfterGpuIdle}`, and `Renderer::RetainUntilFrameComplete(std::uint64_t pageIdentity, std::shared_ptr<const void>)` with per-frame page deduplication.

- [ ] **Step 1: Add the failing fence-retirement tests.**

  ```cpp
  TEST_CASE("submitted page lifetime releases only after its fence signals") {
      FakeGpuCompletionFence* rawFence = nullptr;
      molga::GpuRetirementQueue queue;
      auto lifetime = std::make_shared<int>(7);
      std::weak_ptr<const void> weak = lifetime;
      auto fence = MakeFakeFence(rawFence);
      queue.Enqueue(std::move(fence), {lifetime});
      lifetime.reset();
      queue.Poll();
      CHECK_FALSE(weak.expired());
      rawFence->Signal();
      queue.Poll();
      CHECK(weak.expired());
  }
  ```

- [ ] **Step 1a: Add the failing missing-fence retention test.**

  ```cpp
  TEST_CASE("missing submission fence retains resources until idle drain") {
      molga::GpuRetirementQueue queue;
      auto lifetime = std::make_shared<int>(9);
      std::weak_ptr<const void> weak = lifetime;
      queue.RetainWithoutFence({lifetime});
      lifetime.reset();
      queue.Poll();
      CHECK_FALSE(weak.expired());
      queue.DrainAfterGpuIdle();
      CHECK(weak.expired());
  }
  ```

- [ ] **Step 1b: Add the failing per-frame page-deduplication test.**

  ```cpp
  TEST_CASE("one submitted frame retains one token per atlas page") {
      RendererFixture f;
      f.BeginFrame(9);
      auto page = std::make_shared<int>(3);
      f.renderer.RetainUntilFrameComplete(17, page);
      f.renderer.RetainUntilFrameComplete(17, page);
      CHECK_THROWS_AS(f.renderer.RetainUntilFrameComplete(
          17, std::make_shared<int>(99)), std::logic_error);
      f.renderer.RetainUntilFrameComplete(18, std::make_shared<int>(4));
      CHECK(f.renderer.ActiveFrameRetainedPageCount() == 2);
      f.SubmitAndCompleteFrame();
  }
  ```

- [ ] **Step 1c: Add the interim failing idle-wait abort-order test.**

  ```cpp
  TEST_CASE("failed GPU idle wait aborts before resource teardown") {
      const SubprocessResult result = RunRendererShutdownSubprocess(
          {"--inject-gpu-idle-wait-failure", "--write-shutdown-markers"});
      CHECK(result.terminationSignal == SIGABRT);
      CHECK(result.stderrText.find("GPU_IDLE_WAIT_FAILED") != std::string::npos);
      CHECK(result.markers.count("atlas-destroyed") == 0);
      CHECK(result.markers.count("device-destroyed") == 0);
  }
  ```

  This is only the fail-safe contract available before host-level retryable
  shutdown exists. Task 11.2 atomically deletes this SIGABRT subprocess row and
  terminal branch while adding `EngineShutdownStatus::GpuDrainFailed`; no final
  implementation retains both behaviors.

- [ ] **Step 1d: Implement the shutdown subprocess fixture.** Define `RunRendererShutdownSubprocess` in `test_gpu_retirement.cpp` with the existing process-test helper and a uniquely named temporary marker file; the child owns a live page token before injected shutdown.

- [ ] **Step 2: Run the retirement red gate.**

  Run: `cmake --build --preset debug --target test_gpu_retirement -j`

  Expected: compile FAIL because the fence abstraction and retirement queue do not exist.

- [ ] **Step 3: Add the mockable fence boundary.**

  ```cpp
  class IGpuCompletionFence {
  public:
      virtual ~IGpuCompletionFence() = default;
      virtual bool IsSignaled() const noexcept = 0;
  };
  ```

- [ ] **Step 3a: Add the retirement queue declaration.**

  ```cpp
  class GpuRetirementQueue {
  public:
      void Enqueue(std::unique_ptr<IGpuCompletionFence>,
                   std::vector<std::shared_ptr<const void>> lifetimes);
      void RetainWithoutFence(
          std::vector<std::shared_ptr<const void>> lifetimes);
      void Poll();
      void DrainAfterGpuIdle();
      std::size_t PendingSubmissionCount() const noexcept;
  };
  ```

- [ ] **Step 4: Implement signaled-fence retirement.** `Enqueue` stores one fence with its token vector; `Poll` erases that entry only after `IsSignaled()==true`, preserving insertion order for deterministic audits.

- [ ] **Step 5: Implement unfenced retention.** `RetainWithoutFence` appends tokens to a separate list ignored by `Poll`; `DrainAfterGpuIdle` clears both signaled/unfenced storage only after the caller proves a successful GPU-idle wait.

- [ ] **Step 6: Add the active-frame retain guard.** `Renderer::RetainUntilFrameComplete` ignores null tokens, rejects a zero page identity, and throws `logic_error` when called outside an active `FrameContext`.

- [ ] **Step 6a: Deduplicate by page identity.** Insert the first token into the active frame map keyed by nonzero page identity; repeated commands for that page leave the retained-token count unchanged.

- [ ] **Step 6b: Reject conflicting owners for one page ID.** Compare shared ownership identity with `!a.owner_before(b) && !b.owner_before(a)` and throw `logic_error` if the same page ID arrives with a different owner.

- [ ] **Step 7: Transfer frame tokens at submit.** Move the complete vector with the acquired SDL fence into `GpuRetirementQueue`; if fence acquisition fails, call `RetainWithoutFence` and do not release tokens.

- [ ] **Step 8: Poll at the frame boundary.** Call `GpuRetirementQueue::Poll()` once after beginning a renderer frame and before collecting new commands.

- [ ] **Step 8a: Stop work and wait for successful GPU idle.** Shutdown first prevents new collection/submission, then calls `SDL_WaitForGPUIdle`; it performs no atlas/device teardown until that call reports success.

- [ ] **Step 8b: Drain and destroy in device-safe order.** After successful idle, drain fenced/unfenced tokens, destroy atlas/text GPU resources while `GraphicsDevice` is alive, and destroy the device last.

- [ ] **Step 9: Fail safely when idle wait or device-loss drain fails.** Wire the Step 1 subprocess injection at the `SDL_WaitForGPUIdle` result and route any equivalent device-loss teardown failure through the same interim terminal branch. Emit fatal marker/code `GPU_IDLE_WAIT_FAILED` and call `std::abort()` before atlas or device teardown; do not unwind GPU page destructors against a stale device. The OS owns final process resource reclamation in this pre-Task-11 path; Task 11.2 replaces the test and branch in one buildable commit with retryable host-owned shutdown.

- [ ] **Step 10: Run CPU and SDL_GPU retirement green gates.**

  Run: `cmake --build --preset debug --target test_gpu_retirement test_rendering_sdlgpu -j && ctest --test-dir build/debug -R '^(test_gpu_retirement|test_rendering_sdlgpu)$' --output-on-failure`

  Expected: PASS; fake fences and the real SDL_GPU fixture preserve tokens until signaled/idle and leave no premature texture destruction.

- [ ] **Step 11: Commit fence-backed retirement.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Rendering/GpuRetirementQueue.* \
    src/Rendering/Renderer.* src/Rendering/GraphicsDevice.* \
    tests/test_gpu_retirement.cpp tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: retire GPU resources after fences"
  ```

**Exit:** GPU submission, not CPU frame age, is the sole authority for releasing in-flight atlas page tokens.

---

### Task 6.3: Pin glyph pages across collection and render-command submission

**Prerequisite:** Tasks 6.1–6.2 provide bounded pages and fence retirement.

**Files:**

- Modify: `src/Rendering/RenderQueue.h`
- Modify: `src/Rendering/RenderSystem2D.cpp`
- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_glyph_atlas.cpp`
- Modify: `tests/test_render_queue.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: `GlyphHandle::pageLifetime`, `Renderer::RetainUntilFrameComplete`, and per-frame collection index.
- Produces: `RenderCommand::{resourceLifetimeIdentity,resourceLifetime}`, `TextRenderer::GlyphCollectionScope`, and `TextRenderer::{BeginGlyphCollection,GlyphAtlas}` over one renderer-owned atlas.

- [ ] **Step 1: Add failing command and collection-scope tests.**

  ```cpp
  TEST_CASE("resource lifetime is retained immediately before actual draw") {
      RenderQueueFixture f;
      auto token = std::make_shared<int>(1);
      f.EnqueueTextCommand(17, token);
      f.Render();
      CHECK(f.renderer.CallOrder() ==
            std::vector<std::string>{"retain", "draw"});
      CHECK(f.BatchKeyBefore() == f.BatchKeyAfter());
  }
  ```

- [ ] **Step 1a: Add the failing non-nestable scope test.**

  ```cpp
  TEST_CASE("glyph collection scope is non-nestable and exception safe") {
      TextRendererFixture f;
      {
          auto scope = f.renderer.BeginGlyphCollection(41);
          CHECK_THROWS_AS(f.renderer.BeginGlyphCollection(41), std::logic_error);
      }
      CHECK(f.atlas.EndCollectionCount(41) == 1);
  }
  ```

- [ ] **Step 2: Run the page-pin red gate.**

  Run: `cmake --build --preset debug --target test_glyph_atlas test_render_queue -j`

  Expected: compile FAIL because command lifetime and collection scope do not exist.

- [ ] **Step 3: Add page lifetime fields outside `BatchKey`.** Add `std::uint64_t resourceLifetimeIdentity = 0` and `std::shared_ptr<const void> resourceLifetime` to `RenderCommand`. Text commands copy `GlyphHandle::pageIdentity` and token together; sort/equality/batch functions ignore both.

- [ ] **Step 4: Retain only submitted commands.** In `RenderSystem2D`, immediately before each actual `SpriteBatcher` submission call `renderer.RetainUntilFrameComplete(command.resourceLifetimeIdentity, command.resourceLifetime)` when non-null. Culled/skipped commands do not transfer a token; repeated glyphs on one page dedupe in `FrameContext`.

- [ ] **Step 5: Run the focused command-lifetime gate.**

  Run: `cmake --build --preset debug --target test_render_queue -j && ctest --test-dir build/debug -R '^test_render_queue$' --output-on-failure`

  Expected: command call order is retain-before-draw and `BatchKey` remains unchanged.

- [ ] **Step 6: Add the movable, noncopyable collection-scope declaration.**

  ```cpp
  class TextRenderer::GlyphCollectionScope {
  public:
      GlyphCollectionScope(GlyphCollectionScope&&) noexcept;
      ~GlyphCollectionScope();
      GlyphCollectionScope(const GlyphCollectionScope&) = delete;
      GlyphCollectionScope& operator=(const GlyphCollectionScope&) = delete;
  private:
      friend class TextRenderer;
      GlyphCollectionScope(TextRenderer&, std::uint64_t);
      TextRenderer* owner_ = nullptr;
      std::uint64_t frameIndex_ = 0;
  };
  GlyphCollectionScope BeginGlyphCollection(std::uint64_t frameIndex);
  GlyphAtlasCache& GlyphAtlas() noexcept;
  const GlyphAtlasCache& GlyphAtlas() const noexcept;
  ```

- [ ] **Step 6a: Implement exact scope enter/leave behavior.** `TextRenderer` owns exactly one `GlyphAtlasCache atlas_`; both `GlyphAtlas()` overloads return that exact object and never a process-global second cache. Construction throws before mutation when another scope is active, otherwise calls `atlas_.BeginFrame`; move transfers the owner, and destruction calls `atlas_.EndCollection` exactly once when it still owns the scope. The mutable accessor is used for startup budget configuration and focused fixtures; normal rendering reaches the atlas only through `CollectLayout`. Qualification/performance reads telemetry through the const overload.

- [ ] **Step 7: Run the focused scope gate.**

  Run: `cmake --build --preset debug --target test_glyph_atlas -j && ctest --test-dir build/debug -R '^test_glyph_atlas$' --output-on-failure`

  Expected: nesting throws and every normal/exception return ends collection once.

- [ ] **Step 8: Open one scope in every frame collection entry.** Editor `main.cpp`, standalone `runtime_main.cpp`, canonical/offscreen paths, and SDL_GPU smoke fixtures create it after frame index is fixed and before world/UI text queue collection; lexical scope covers every early return until all commands have copied page tokens.

- [ ] **Step 9: Add the direct old-resource atlas test.** Shape a layout, replace the font, derive a key from one old `ShapedGlyph`, then open `auto scope = textRenderer.BeginGlyphCollection(93)` and call `textRenderer.GlyphAtlas().GetGlyph(key, *oldGlyph.faceResource->rasterFace, sink)` after forcing a miss. Verify the old raster face receives the glyph ID while a newly shaped glyph carries/uses the replacement SHA; let the scope close before assertions finish. This tests ownership without switching a production consumer.

- [ ] **Step 10: Run complete atlas/lifetime/render green gates.**

  Run: `cmake --build --preset debug --target test_glyph_atlas test_gpu_retirement test_render_queue test_rendering_sdlgpu -j && ctest --test-dir build/debug -R '^(test_glyph_atlas|test_gpu_retirement|test_render_queue|test_rendering_sdlgpu)$' --output-on-failure`

  Expected: PASS; page lifetimes survive collection, sorting, submission, eviction pressure and fences, and resident bytes stay at or below the configured cap.

- [ ] **Step 11: Commit collection-to-fence ownership.**

  ```bash
  git add CMakeLists.txt src/Rendering/RenderQueue.h \
    src/Rendering/RenderSystem2D.cpp src/Rendering/TextRenderer.* \
    src/main.cpp src/runtime_main.cpp tests/test_glyph_atlas.cpp \
    tests/test_render_queue.cpp tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: pin glyph pages through GPU submission"
  ```

**Exit:** Every future text command can carry the exact page lifetime from collection through a real completion fence without affecting draw batching.

---

### Task 7.1: Define immutable layout records and collision-safe cache identity

**Prerequisite:** Shaping and resource ownership are green; no paragraph consumer has been migrated.

**Files:**

- Create: `src/Text/TextLayoutTypes.h`
- Create: `src/Text/TextLayoutCache.h`
- Create: `src/Text/TextLayoutCache.cpp`
- Create: `tests/test_text_cache.cpp`
- Create: `tests/test_text_cache_header.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `ParagraphStyle` inputs, `ShapedGlyph`, portable dependency-contract SHA, and design Section 10 identity fields.
- Produces: immutable layout/value/validation-fact types, `TextLayoutRequestIndexKey`, structured `TextShapeCacheKey`, `TextParagraphCacheKey`, and bounded `TextLayoutCache::{FindShape,StoreShape,FindByRequest,FindByFinal,Store,Telemetry}`.

- [ ] **Step 1: Add failing one-field identity and collision tests.**

  ```cpp
  TEST_CASE("every shape and paragraph identity field causes a cache miss") {
      TextCacheFixture f;
      const auto request = f.RequestKeyFor(u8"سلام ffi");
      const auto shapeKey = f.ShapeKeyFor(request);
      const auto finalKey = f.FinalKeyFor(request);
      f.cache.StoreShape(shapeKey, f.ShapeResult());
      f.cache.Store(request, finalKey, f.Layout());
      CHECK(f.cache.FindByRequest(request)->layout == f.Layout());
      CHECK(f.cache.FindShape(shapeKey) == f.ShapeResult());
      CHECK(f.cache.FindByFinal(finalKey) == f.Layout());
      for (const auto& mutated : MutateEachShapeIdentityField(shapeKey)) {
          CHECK(f.cache.FindShape(mutated) == nullptr);
      }
      for (const auto& mutated : MutateEachRequestIdentityField(request)) {
          CHECK_FALSE(f.cache.FindByRequest(mutated));
      }
      for (const auto& mutated : MutateEachFinalIdentityField(finalKey)) {
          CHECK(f.cache.FindByFinal(mutated) == nullptr);
      }
  }
  ```

- [ ] **Step 1a: Add the failing request-byte collision test.**

  ```cpp
  TEST_CASE("hash collision never aliases different original bytes") {
      TextCacheFixture f;
      auto a = f.RequestKeyForBytesWithForcedHash("abc", 7);
      auto b = f.RequestKeyForBytesWithForcedHash("abd", 7);
      f.cache.Store(a, f.FinalKeyFor(a), f.Layout());
      CHECK_FALSE(f.cache.FindByRequest(b));
  }
  ```

- [ ] **Step 1b: Add the failing synthetic-shape-input collision test.**

  ```cpp
  TEST_CASE("shape-input hash collision never aliases ellipsis bytes") {
      TextCacheFixture f;
      auto a = f.ShapeKeyWithForcedInputHash(u8"سلام…", 11);
      auto b = f.ShapeKeyWithForcedInputHash(u8"سلا…", 11);
      f.cache.StoreShape(a, f.ShapeResult());
      CHECK(f.cache.FindShape(b) == nullptr);
  }
  ```

- [ ] **Step 1c: Add the failing direct request-index hit test.**

  ```cpp
  TEST_CASE("request index hits before ICU and HarfBuzz") {
      TextCacheFixture f;
      const auto requestKey = f.RequestKeyFor(u8"سلام ffi");
      const auto finalKey = f.FinalKeyFor(requestKey);
      f.cache.Store(requestKey, finalKey, f.LayoutWithNoValidationFacts());
      f.ResetAnalysisAndShapeCounters();
      REQUIRE(f.cache.FindByRequest(requestKey));
      CHECK(f.IcuCallCount() == 0);
      CHECK(f.HarfBuzzCallCount() == 0);
  }
  ```

- [ ] **Step 1d: Implement the one-field mutation tables.** Define separate `MutateEachShapeIdentityField`, `MutateEachRequestIdentityField`, and `MutateEachFinalIdentityField` tables; each changes exactly one named field and includes that field name in a failed miss assertion.

- [ ] **Step 1e: Add the failing cache-capacity/owner test.**

  ```cpp
  TEST_CASE("bounded eviction drops only cache ownership and zero is disabled") {
      TextCacheFixture one({1, 1, 4096});
      const auto firstRequest = one.RequestKeyFor("first");
      const auto firstLayout = one.Layout();
      one.cache.Store(firstRequest, one.FinalKeyFor(firstRequest), firstLayout);
      const auto secondRequest = one.RequestKeyFor("second");
      one.cache.Store(secondRequest, one.FinalKeyFor(secondRequest), one.OtherLayout());
      CHECK_FALSE(one.cache.FindByRequest(firstRequest));
      CHECK(one.CanonicalLayout(*firstLayout) ==
            one.ExpectedFirstCanonicalLayout());

      TextCacheFixture zero({0, 0, 0});
      zero.cache.Store(firstRequest, zero.FinalKeyFor(firstRequest), firstLayout);
      CHECK_FALSE(zero.cache.FindByRequest(firstRequest));
  }
  ```

- [ ] **Step 1f: Add the failing analysis-identity mutation test.** Starting from one stored shape key, independently mutate `resolvedGraphemeLocale`, `resolvedLineBreakLocale`, `graphemeRuleIdentity`, `lineBreakRuleIdentity`, and `analysisGeneration`; also mutate `clusterPolicy` to a test-only invalid enum value:

  ```cpp
  TEST_CASE("analysis identity and project cluster policy are cache identity") {
      TextCacheFixture f;
      const auto key = f.ShapeKeyFor(f.RequestKeyFor(u8"ภาษาไทย"));
      f.cache.StoreShape(key, f.ShapeResult());
      for (const auto& changed : MutateAnalysisIdentityAndClusterPolicy(key)) {
          CAPTURE(changed.fieldName);
          CHECK_FALSE(changed.key == key);
          CHECK(TextShapeCacheKeyHash{}(changed.key) !=
                TextShapeCacheKeyHash{}(key));
          CHECK(f.cache.FindShape(changed.key) == nullptr);
      }
  }
  ```

  `MutateAnalysisIdentityAndClusterPolicy` returns exactly six entries and changes only its named field; the invalid cluster enum is tested only as a key value and is rejected before an actual shape call.

- [ ] **Step 1g: Add the public-header HarfBuzz leak probe.** Put this complete probe in `tests/test_text_cache_header.cpp`; its separate target in Step 1i prevents any earlier test include from predefining HarfBuzz macros:

  ```cpp
  #include "doctest.h"
  #include "Text/TextLayoutCache.h"
  #include <type_traits>
  #ifdef HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS
  #error "TextLayoutCache.h leaked a HarfBuzz cluster macro"
  #endif
  #ifdef HB_H
  #error "TextLayoutCache.h leaked hb.h"
  #endif
  static_assert(std::is_same_v<
      decltype(molga::text::TextShapeCacheKey{}.clusterPolicy),
      molga::text::TextClusterPolicy>);
  TEST_CASE("public cache key uses the project cluster policy") {
      CHECK(molga::text::TextShapeCacheKey{}.clusterPolicy ==
            molga::text::TextClusterPolicy::MonotoneCharacters);
  }
  ```

- [ ] **Step 1h: Register the cache behavior test.** Call `molga_add_text_test(test_text_cache test_text_cache.cpp)`; its helper owns normal dependency attachment. Use only the common root/session main.

- [ ] **Step 1i: Register the cache-header isolation test.** Call `molga_add_text_test(test_text_cache_header test_text_cache_header.cpp)` without a second attach call; keep it a separate executable/translation unit from `test_text_cache`.

- [ ] **Step 2: Run the layout-cache red gate.**

  Run: `cmake --build --preset debug --target test_text_cache test_text_cache_header -j`

  Expected: compile FAIL because layout/cache types do not exist.

- [ ] **Step 3: Add layout policy enums and constraints.**

  ```cpp
  enum class TextWrapMode : std::uint8_t { NoWrap, Word, Grapheme };
  enum class TextOverflowMode : std::uint8_t { Overflow, Clip, Ellipsis };
  enum class TextHorizontalAlignment : std::uint8_t { Left, Center, Right };
  enum class TextVerticalAlignment : std::uint8_t { Top, Middle, Bottom };
  enum class CaretAffinity : std::uint8_t { Upstream, Downstream };
  struct LayoutConstraints {
      std::optional<Fixed26_6> width;
      std::optional<Fixed26_6> height;
  };
  ```

- [ ] **Step 3a: Add the exact paragraph style.**

  ```cpp
  struct ParagraphStyle {
      std::string fontFamilyGuid;
      FontRequest fontRequest;
      ShapeStyle shape;
      TextAnalysisOptions analysis;
      TextWrapMode wrap = TextWrapMode::NoWrap;
      TextOverflowMode overflow = TextOverflowMode::Overflow;
      std::uint32_t maxLines = 0;
      Fixed26_6 lineSpacing = Fixed26_6::FromRaw(64);
      TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
      TextVerticalAlignment vertical = TextVerticalAlignment::Top;
      std::string ellipsisUtf8 = u8"…";
  };
  ```

- [ ] **Step 3b: Add positioned glyph, visual-run, and line records.**

  ```cpp
  struct GlyphInteriorCaret {
      std::uint32_t logicalGraphemeBoundary = 0;
      FixedPoint position;
      bool fromAdjustedGdef = false;
  };
  struct PositionedGlyph {
      ShapedGlyph glyph;
      FixedPoint origin;
      std::vector<GlyphInteriorCaret> interiorCarets;
  };
  struct VisualRun {
      std::uint32_t logicalRunId = 0;
      std::uint8_t bidiLevel = 0;
      std::vector<PositionedGlyph> glyphs;
  };
  struct TextLine {
      SourceByteRange sourceBytes;
      GraphemeRange graphemes;
      Fixed26_6 baseline = Fixed26_6::FromRaw(0);
      Fixed26_6 advance = Fixed26_6::FromRaw(0);
      Fixed26_6 ascent = Fixed26_6::FromRaw(0);
      Fixed26_6 descent = Fixed26_6::FromRaw(0);
      Fixed26_6 lineGap = Fixed26_6::FromRaw(0);
      Fixed26_6 top = Fixed26_6::FromRaw(0);
      Fixed26_6 bottom = Fixed26_6::FromRaw(0);
      std::vector<VisualRun> visualRuns;
  };
  ```

- [ ] **Step 3c: Add caret and validation-fact records.**

  ```cpp
  struct CaretStop {
      std::uint32_t logicalGraphemeBoundary = 0;
      CaretAffinity affinity = CaretAffinity::Downstream;
      FixedPoint position;
      std::uint32_t lineIndex = 0;
  };
  struct TextValidationFact {
      TextDiagnosticCode code = TextDiagnosticCode::Utf8Invalid;
      TextSeverity severity = TextSeverity::Error;
      std::string subsystem;
      std::string message;
      std::string remediation;
      SourceByteRange sourceBytes;
      GraphemeRange graphemes;
      bool recoverableAtRuntime = false;
      bool blocksAuthoredPackage = true;
  };
  ```

- [ ] **Step 3d: Add the immutable layout aggregate.**

  ```cpp
  struct TextLayout {
      std::vector<TextLine> lines;
      std::vector<CaretStop> caretStops;
      FixedSize intrinsicSize;
      bool clipped = false;
      bool ellipsized = false;
      std::vector<TextValidationFact> validationFacts;
  };
  ```

- [ ] **Step 3e: Add diagnostic context and request values.**

  ```cpp
  struct TextDiagnosticContext {
      std::string assetGuid;
      unsigned int sceneObjectId = 0;
      std::string componentType;
  };
  struct TextLayoutRequest {
      std::string utf8;
      ParagraphStyle style;
      LayoutConstraints constraints;
      std::uint64_t visualRevision = 0;
      TextDiagnosticContext diagnosticContext;
  };
  ```

- [ ] **Step 4: Implement cacheable validation facts.** Store facts for replacement-decoded input, missing graphemes/tofu, fallback problems and other layout-producing conditions. Rebuild a `TextDiagnostic` on every cold/warm call by combining the immutable fact with the current request's `TextDiagnosticContext`.

- [ ] **Step 5: Define the family-closure request identities.**

  ```cpp
  struct FamilyNodeRequestIdentity {
      std::string familyGuid;
      bool exists = false;
      std::uint64_t contentGeneration = 0;
      std::vector<std::string> authoredFallbackGuids;
      bool operator==(const FamilyNodeRequestIdentity&) const;
  };
  struct FaceRequestIdentity {
      std::string fontGuid, fontRevision, sourceSha256, artifactSha256;
      FontArtifactLocator artifactLocator;
      std::uint64_t artifactByteSize = 0;
      std::uint64_t contentGeneration = 0;
      std::uint32_t faceIndex = 0, authoredFaceIndex = 0;
      bool operator==(const FaceRequestIdentity&) const;
  };
  struct ResolvedFamilyRequestIdentity {
      std::string requestedGuid;
      std::vector<FamilyNodeRequestIdentity> depthFirstFamilyNodes;
      std::vector<FaceRequestIdentity> orderedCandidates;
      bool operator==(const ResolvedFamilyRequestIdentity&) const;
  };
  ```

- [ ] **Step 5a: Define the early request-index key.**

  ```cpp
  struct TextLayoutRequestIndexKey {
      std::uint32_t decodePolicyVersion = 1;
      std::uint32_t familyResolutionPolicyVersion = 1;
      std::uint32_t fallbackPolicyVersion = 1;
      std::string originalUtf8;
      std::uint64_t originalBytesHash = 0;
      ParagraphStyle style;
      LayoutConstraints constraints;
      std::uint64_t visualRevision = 0;
      ResolvedFamilyRequestIdentity familyClosure;
      std::string harfbuzzRevision;
      std::string icuRevision;
      std::string icuDataSha256;
      std::string requestedGraphemeRulePolicyIdentity;
      std::string requestedLineBreakRulePolicyIdentity;
      std::string dependencyContractSha256;
      bool operator==(const TextLayoutRequestIndexKey&) const;
  };
  ```

  Collision-check `originalUtf8` length/bytes, every canonical style/constraint field, every family node/edge/generation, and every ordered candidate content identity; hashes select buckets only. Exclude `diagnosticContext` so the same immutable layout can be reused while facts are re-contextualized. The key contains no analysis/final-line result and is buildable after family resolution but before ICU/HarfBuzz.

- [ ] **Step 6: Define selected-face and shape-input mapping identities.**

  ```cpp
  struct SelectedFaceShapeIdentity {
      std::string fontGuid, fontRevision, sourceSha256, artifactSha256;
      FontArtifactLocator artifactLocator;
      std::uint64_t artifactByteSize = 0;
      std::uint64_t contentGeneration = 0;
      std::uint32_t faceIndex = 0;
  };
  struct ShapeInputSourceSpan {
      SourceByteRange shapeInputBytes;
      SourceByteRange originalSourceBytes;
      GraphemeRange originalGraphemes;
      bool synthetic = false;
      bool operator==(const ShapeInputSourceSpan&) const;
  };
  ```

- [ ] **Step 6a: Define the complete structured shape key.**

  ```cpp
  struct TextShapeCacheKey {
      std::uint32_t decodePolicyVersion = 1;
      std::string originalUtf8;
      std::uint64_t originalBytesHash = 0;
      std::string shapeInputUtf8;
      std::uint64_t shapeInputBytesHash = 0;
      std::vector<ShapeInputSourceSpan> shapeInputMapping;
      SourceByteRange paragraphBytes, runBytes;
      std::uint64_t fallbackGraphGeneration = 0;
      std::vector<SelectedFaceShapeIdentity> selectedFaces;
      Fixed26_6 fontSize = Fixed26_6::FromRaw(0);
      std::uint64_t variationKey = 0;
      std::uint8_t embeddingLevel = 0;
      std::int32_t direction = 0, scriptCode = 0;
      std::string language, componentLocale;
      std::string resolvedGraphemeLocale, resolvedLineBreakLocale;
      std::string graphemeRuleIdentity, lineBreakRuleIdentity;
      std::uint64_t analysisGeneration = 0;
      ShapeBoundaryFlags boundaries;
      std::uint32_t harfbuzzBufferFlags = 0;
      TextClusterPolicy clusterPolicy =
          TextClusterPolicy::MonotoneCharacters;
      std::vector<ShapeFeature> orderedFeatures;
      std::string harfbuzzRevision, icuRevision, icuDataSha256;
      std::string dependencyContractSha256;
      bool operator==(const TextShapeCacheKey&) const;
  };
  ```

- [ ] **Step 7: Define the complete final paragraph key.**

  ```cpp
  struct TextParagraphCacheKey {
      std::vector<TextShapeCacheKey> finalLineShapeKeys;
      LayoutConstraints constraints;
      TextWrapMode wrap = TextWrapMode::NoWrap;
      TextOverflowMode overflow = TextOverflowMode::Overflow;
      std::string overlongTokenPolicy;
      std::string ellipsisUtf8;
      ShapeStyle ellipsisStyle;
      std::uint32_t maxLines = 0;
      Fixed26_6 lineSpacing = Fixed26_6::FromRaw(64);
      TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
      TextVerticalAlignment vertical = TextVerticalAlignment::Top;
      std::uint64_t visualRevision = 0;
      bool operator==(const TextParagraphCacheKey&) const;
  };
  ```

  This is the audit/store identity produced after cold layout, not the warm-entry lookup key.

- [ ] **Step 8: Add the exact multi-stage cache value types.**

  ```cpp
  struct CachedShapeResult {
      std::vector<ShapedRun> runs;
      std::vector<TextValidationFact> validationFacts;
  };
  struct CachedTextLayout {
      TextParagraphCacheKey finalKey;
      std::shared_ptr<const TextLayout> layout;
  };
  struct TextLayoutCacheTelemetry {
      std::uint64_t shapeHits = 0, shapeMisses = 0, shapeStores = 0;
      std::uint64_t requestHits = 0, requestMisses = 0;
      std::uint64_t finalHits = 0, finalMisses = 0, finalStores = 0;
      std::uint64_t evictions = 0;
  };
  struct TextLayoutCacheLimits {
      std::size_t maxShapeEntries = 0;
      std::size_t maxParagraphEntries = 0;
      std::uint64_t maxOwnedBytes = 0;
      static TextLayoutCacheLimits Production() noexcept;
  };
  ```

  `Production()` returns exactly `{4096, 1024, 64ULL * 1024ULL * 1024ULL}`. Each limit is a hard capacity; zero means zero cache capacity, never unlimited.

- [ ] **Step 8a: Add the exact cache service declaration.**

  ```cpp
  class TextLayoutCache {
  public:
      explicit TextLayoutCache(TextLayoutCacheLimits);
      std::shared_ptr<const CachedShapeResult> FindShape(
          const TextShapeCacheKey&);
      void StoreShape(const TextShapeCacheKey&,
                      std::shared_ptr<const CachedShapeResult>);
      std::optional<CachedTextLayout> FindByRequest(
          const TextLayoutRequestIndexKey&);
      std::shared_ptr<const TextLayout> FindByFinal(
          const TextParagraphCacheKey&);
      void Store(const TextLayoutRequestIndexKey&,
                 const TextParagraphCacheKey&,
                 std::shared_ptr<const TextLayout>);
      const TextLayoutCacheTelemetry& Telemetry() const noexcept;
  };
  ```

- [ ] **Step 8b: Keep public cache headers HarfBuzz-free.** `TextLayoutCache.h` may include the project `TextShapingService.h` for `TextClusterPolicy` but may not include `hb.h`, expose `hb_*` types, or mention `HB_BUFFER_*`; only `TextShapingService.cpp` performs the mapping from Task 5.2.

- [ ] **Step 9: Implement collision-safe request lookup.** The request hash selects a bucket only; compare every request-index field plus original length/bytes before returning the stored final key/layout. The exact authored locale bytes are already in `ParagraphStyle::analysis`; without canonicalizing them, derive the two requested rule-policy identities as SHA-256 of `{policy kind,policy version,ICU revision,ICU-data SHA}` from compile-time/portable-contract values. They are not claimed to be the actual iterator rule identities. This method contains no Unicode/ICU/HarfBuzz call.

- [ ] **Step 9a: Implement collision-safe final-key lookup.** `FindByFinal` compares every complete final key field after bucket selection for audits/one-field tests; it never replaces the early request lookup.

- [ ] **Step 10: Implement collision-safe shape lookup.** `FindShape` compares authored and actual shape-input byte lengths/bytes, every synthetic/source mapping span, all five immutable `UnicodeAnalysis::Identity()` fields, `TextClusterPolicy`, and every remaining structured shape field after bucket selection.

- [ ] **Step 10a: Implement shape storage.** `StoreShape` owns immutable runs plus context-free validation facts; an equal key replaces cache ownership, while a hash collision appends a distinct entry.

- [ ] **Step 11: Implement paragraph `Store`.** Replace an exactly equal request entry; otherwise append one linked request/final-key/immutable-layout entry so eviction cannot leave a dangling request index.

- [ ] **Step 11a: Enforce cache limits.** After each store, evict least-recently-used shape and linked paragraph entries until all exact `TextLayoutCacheLimits` entry/owned-byte bounds hold; dropping cache ownership never mutates external shared owners. Production explicitly uses `TextLayoutCacheLimits::Production()`; tests pass smaller values, and any zero field is zero capacity for that resource rather than unlimited.

- [ ] **Step 12: Implement cache telemetry.** Increment shape/request/final hit, miss, store and eviction counters at their authoritative operations; provide a const telemetry snapshot for warm-static tests.

- [ ] **Step 13: Run the cache identity green gate.**

  Run: `cmake --build --preset debug --target test_text_cache test_text_cache_header -j && ctest --test-dir build/debug -R '^(test_text_cache|test_text_cache_header)$' --output-on-failure`

  Expected: every one-field mutation misses, identical values hit, forced hash collision misses, and eviction never mutates a shared immutable layout.

- [ ] **Step 14: Commit layout records and cache identity.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextLayoutTypes.h \
    src/Text/TextLayoutCache.* tests/test_text_cache.cpp \
    tests/test_text_cache_header.cpp
  git commit -m "feat: add collision-safe text layout cache"
  ```

**Exit:** Paragraph layout has immutable output types and cannot conflate different bytes, locales, fallback descendants, boundary flags, or constraints.

---

### Task 7.2: Lay out and re-shape every authoritative final line

**Prerequisite:** Task 7.1 cache/types and Task 5.2 shaping are green.

**Files:**

- Create: `src/Text/TextLayoutService.h`
- Create: `src/Text/TextLayoutService.cpp`
- Create: `tests/test_text_layout.cpp`
- Create: `tests/fixtures/text/expected/layout.json`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `FontFamilyResolver`, `TextShapingService`, `UnicodeTextAnalyzer`, and `TextLayoutCache`.
- Produces: `TextLayoutService::Layout(const TextLayoutRequest&, TextDiagnosticSink&)` returning `optional<shared_ptr<const TextLayout>>`.

- [ ] **Step 1: Add the failing final-line reshaping test.**

  ```cpp
  TEST_CASE("every accepted line is shaped in its real line context") {
      LayoutFixture f = LoadLayoutFixture("final-line-fi");
      const auto layout = f.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
      REQUIRE(layout);
      CHECK(f.shaper.ParagraphShapeCount() == 1);
      CHECK(f.shaper.FinalLineShapeCount() == (*layout)->lines.size());
      CHECK(f.CanonicalJson(**layout) == f.ExpectedCanonicalJson());
  }
  ```

- [ ] **Step 1a: Add the failing overlong-policy test.**

  ```cpp
  TEST_CASE("word wrap never splits an overlong unbreakable span") {
      LayoutFixture f = LoadLayoutFixture("overlong-word");
      CHECK(f.Layout(TextWrapMode::Word, TextOverflowMode::Overflow)->lines.size() == 1);
      CHECK(f.Layout(TextWrapMode::Word, TextOverflowMode::Clip)->clipped);
      CHECK(f.Layout(TextWrapMode::Word, TextOverflowMode::Ellipsis)->ellipsized);
  }
  ```

- [ ] **Step 1b: Add the failing empty/trailing-line range test.**

  ```cpp
  TEST_CASE("empty paragraphs and trailing newline retain metric-bearing lines") {
      LayoutFixture f = LoadLayoutFixture("empty-lines");
      const auto empty = f.LayoutText("");
      REQUIRE(empty->lines.size() == 1);
      CHECK(empty->lines[0].sourceBytes.begin == 0);
      CHECK(empty->lines[0].sourceBytes.end == 0);
      CHECK(empty->lines[0].graphemes.begin == 0);
      CHECK(empty->lines[0].graphemes.end == 0);
      CHECK(empty->lines[0].bottom.Raw() > empty->lines[0].top.Raw());
      const auto trailing = f.LayoutText("A\n");
      REQUIRE(trailing->lines.size() == 2);
      CHECK(trailing->lines[0].sourceBytes.begin == 0);
      CHECK(trailing->lines[0].sourceBytes.end == 1);
      CHECK(trailing->lines[0].graphemes.begin == 0);
      CHECK(trailing->lines[0].graphemes.end == 1);
      CHECK(trailing->lines[1].sourceBytes.begin == 2);
      CHECK(trailing->lines[1].sourceBytes.end == 2);
      CHECK(trailing->lines[1].graphemes.begin == 2);
      CHECK(trailing->lines[1].graphemes.end == 2);
      CHECK(trailing->lines[1].advance.Raw() == 0);
      CHECK(trailing->lines[1].bottom.Raw() > trailing->lines[1].top.Raw());
  }
  ```

- [ ] **Step 1c: Add the failing no-face procedural-metrics test.**

  ```cpp
  TEST_CASE("procedural em metrics cover empty and missing-family lines") {
      LayoutFixture f = LoadLayoutFixture("no-face-metrics");
      const auto em = Fixed26_6::FromRaw(16 * 64);
      const auto empty = f.LayoutTextWithFamily("", "missing-family", em);
      REQUIRE(empty->lines.size() == 1);
      CHECK(empty->lines[0].ascent.Raw() == 12 * 64);
      CHECK(empty->lines[0].descent.Raw() == 4 * 64);
      CHECK(empty->lines[0].lineGap.Raw() == 0);
      const auto trailing =
          f.LayoutTextWithFamily("\n", "missing-family", em);
      REQUIRE(trailing->lines.size() == 2);
      CHECK(trailing->lines[1].sourceBytes.begin == 1);
      CHECK(trailing->lines[1].sourceBytes.end == 1);
      CHECK(trailing->lines[1].graphemes.begin == 1);
      CHECK(trailing->lines[1].graphemes.end == 1);
      CHECK(trailing->lines[1].ascent.Raw() == 12 * 64);
      CHECK(trailing->lines[1].descent.Raw() == 4 * 64);
      CHECK(trailing->lines[1].lineGap.Raw() == 0);
      const auto missing =
          f.LayoutTextWithFamily(u8"👩‍🚀", "missing-family", em);
      REQUIRE(missing);
      const auto& glyph = FirstGlyph(*missing);
      CHECK(glyph.missing);
      CHECK_FALSE(glyph.faceResource);
      CHECK(glyph.advanceX.Raw() == 16 * 64);
  }
  ```

- [ ] **Step 1d: Add the failing imported-design-metrics layout test.** Lay out a line through a fixture face with persisted `{1000,750,-250,125}` design units and size raw `1056`; require line ascent/descent/lineGap equal the three canonical `CheckedMulDiv` results, reset the rasterizer metrics-call counter, repeat from a cold repository/layout, and require that counter remains zero.

- [ ] **Step 1e: Add the failing whole-candidate ellipsis test.**

  ```cpp
  TEST_CASE("ellipsis reshapes retained text and token as one final candidate") {
      LayoutFixture f = LoadLayoutFixture("arabic-ellipsis-context");
      const auto layout = f.LayoutEllipsized();
      REQUIRE(layout);
      CHECK(f.shaper.SeparateEllipsisShapeCount() == 0);
      CHECK(f.shaper.FullRetainedPlusEllipsisShapeCount() ==
            f.EllipsisRemovalAttemptCount());
      CHECK(f.shaper.EveryEllipsisCandidateUsedFinalBotEot());
  }
  ```

- [ ] **Step 1f: Add the failing cold/warm validation-fact test.** Exercise both replacement-decoded input and a completely missing family through the real service:

  ```cpp
  TEST_CASE("warm recoverable layouts re-emit identical facts without ICU or HB") {
      LayoutFixture f = LoadLayoutFixture("recoverable-cache");
      for (const TextLayoutRequest& request : {
               f.RequestForBytes(std::string("A\xFFZ", 3)),
               f.RequestForMissingFamily(u8"👩‍🚀")}) {
          molga::text::VectorTextDiagnosticSink cold;
          const auto first = f.service.Layout(request, cold);
          REQUIRE(first);
          REQUIRE_FALSE((*first)->validationFacts.empty());
          CHECK(HasBlockingAuthoredPackageFact((*first)->validationFacts));
          f.ResetIcuAndHarfBuzzCounters();
          molga::text::VectorTextDiagnosticSink warm;
          const auto second = f.service.Layout(request, warm);
          REQUIRE(second == first);
          CHECK(f.IcuCallCount() == 0);
          CHECK(f.HarfBuzzCallCount() == 0);
          CHECK(CanonicalValidationFacts((*second)->validationFacts) ==
                CanonicalValidationFacts((*first)->validationFacts));
          CHECK(CanonicalDiagnosticRecords(warm.Diagnostics()) ==
                CanonicalDiagnosticRecords(cold.Diagnostics()));
      }
  }
  ```

  Define both canonical serializers to include every field in stable order; neither may compare only code/message.

- [ ] **Step 1g: Add the failing unsafe-to-break rejection test.**

  ```cpp
  TEST_CASE("layout never accepts a HarfBuzz unsafe boundary") {
      LayoutFixture f = LoadLayoutFixture("unsafe-arabic-boundary");
      const auto unsafe = f.FirstParagraphUnsafeGraphemeBoundary();
      REQUIRE(unsafe);
      const auto layout = f.LayoutAtWidth(
          f.WidthThatWouldOtherwiseChoose(*unsafe));
      REQUIRE(layout);
      CHECK_FALSE(AnyLineEndsAtGrapheme(**layout, *unsafe));
      CHECK(f.WasBreakCandidateDiscarded(*unsafe));
      CHECK(f.EveryAcceptedLineWasFinalReshaped());
  }
  ```

- [ ] **Step 1h: Implement the unsafe-boundary fixture observer.** Build `unsafe-arabic-boundary` from locked Arabic text `u8"سلامك"`; derive `FirstParagraphUnsafeGraphemeBoundary` only from emitted `HB_GLYPH_FLAG_UNSAFE_TO_BREAK`, choose a width strictly between the advances at that and the preceding legal boundary, and observe the production candidate set without altering flags or break selection.

- [ ] **Step 1i: Register layout tests with the common runtime main.** Call `molga_add_text_test(test_text_layout test_text_layout.cpp)` and rely on its owned dependency attach; no layout case may initialize ICU/HarfBuzz from a fixture-local root or call the attach helper again.

- [ ] **Step 2: Run the final-line red gate.**

  Run: `cmake --build --preset debug --target test_text_layout -j`

  Expected: compile FAIL because `TextLayoutService` does not exist.

- [ ] **Step 3: Add the exact service constructor and result API.**

  ```cpp
  class TextLayoutService {
  public:
      TextLayoutService(FontFamilyResolver&, TextShapingService&,
                        TextLayoutCache&);
      std::optional<std::shared_ptr<const TextLayout>> Layout(
          const TextLayoutRequest&, TextDiagnosticSink&);
  };
  ```

- [ ] **Step 4: Resolve the pre-analysis family closure.** Call the resolver with a local collecting sink, retain its exact depth-first nodes/edges/generations/candidates—including `exists=false` nodes—and convert its records to context-free validation facts without reporting to the caller yet.

- [ ] **Step 4a: Perform the early request-index lookup.** Build `TextLayoutRequestIndexKey` from the original request, portable/rule versions, and resolved closure, then call `FindByRequest` before constructing `UnicodeTextBuffer`, an ICU object, or a HarfBuzz object.

- [ ] **Step 4b: Re-emit a warm result's validation facts.** On a request hit, merge current resolution facts with stored facts by `{code,severity,subsystem,message,remediation,sourceBytes,graphemes,recoverable,blocksPackage}`, deduplicate, apply the current diagnostic context, report each once, and return the cached layout.

- [ ] **Step 5: Build cold Unicode paragraph analysis.** Construct `UnicodeTextBuffer` and `UnicodeAnalysis` only after a request-index miss; convert every replacement range into one immutable validation fact.

- [ ] **Step 5a: Copy exact resolved analysis identity into shape keys.** For every paragraph/final-line shape key, copy `resolvedGraphemeLocale`, `resolvedLineBreakLocale`, `graphemeRuleIdentity`, `lineBreakRuleIdentity`, and `analysisGeneration` directly from the immutable `UnicodeAnalysis::Identity()`; never reconstruct them from the requested locale or current ICU process state.

- [ ] **Step 5b: Shape cold paragraph analysis items once.** Shape each paragraph item to measure candidate breaks, convert every missing grapheme/tofu/fallback condition to a fact, and deduplicate these plus Step 4/5 facts by the same stable tuple.

- [ ] **Step 6: Generate legal break candidates.** `NoWrap` keeps explicit separators only, `Word` keeps extended-grapheme-aligned ICU line opportunities, and `Grapheme` keeps every extended-grapheme boundary; discard candidates crossing HarfBuzz unsafe-to-break output.

- [ ] **Step 7: Re-shape one proposed final line.** Construct exact line-range analysis items, derive real line/paragraph BOT/EOT flags, call `ShapeAnalysisItem` for every item, and concatenate only these new results—never paragraph glyph slices.

- [ ] **Step 8: Implement width backtracking.** If authoritative line advance exceeds the constraint, move to the preceding legal candidate and repeat Step 7; when no prior candidate exists, defer to the selected overlong policy.

- [ ] **Step 9: Implement Overflow and Clip policies.** Overflow retains an overlong unbreakable line; Clip retains logical glyphs and marks the output clipped. Apply height/max-lines truncation without manufacturing a grapheme break.

- [ ] **Step 10: Build one retained-line-plus-ellipsis candidate.** Create an ephemeral Unicode buffer containing the current whole-grapheme retained prefix followed by `ellipsisUtf8`, with source mapping spans that distinguish retained and synthetic bytes.

- [ ] **Step 10a: Shape the complete ellipsis candidate.** Analyze and shape the entire ephemeral candidate with real final BOT/EOT plus original style/run context; never invoke a separate ellipsis-token shape and concatenate it.

- [ ] **Step 10b: Restore source mappings for ellipsis output.** Map retained clusters through their original ranges and every synthetic cluster to the zero-length truncation byte/grapheme boundary.

- [ ] **Step 10c: Repeat only at whole-grapheme boundaries.** If the shaped candidate is over width, remove exactly one retained grapheme and repeat Steps 10–10b until it fits or the candidate is ellipsis-only.

- [ ] **Step 11: Compute line visual order.** Run ICU line BiDi for each accepted source range and emit `VisualRun`s in ICU visual order with the exact final glyph records.

- [ ] **Step 12: Materialize exact line ranges and vertical fields.** Every logical line stores exact source-byte/grapheme ranges, baseline, nonnegative ascent/descent/lineGap, and checked `top = baseline - ascent`, `bottom = baseline + descent`; the checked baseline step to the next line includes `lineGap` before applying the authored `lineSpacing` policy.

- [ ] **Step 12a: Derive metrics from imported design integers.** For a nonempty line, call `ScaleFontDesignMetrics(resource->designMetrics,fontSize)` for every final glyph resource and take checked maxima of ascent/descent/lineGap. For an empty/trailing line, use the first ordered candidate in its resolved family; only a family with no candidate falls through to Step 12b. No layout code calls `FontFace::Metrics`, stb metric APIs, or converts these values through float.

- [ ] **Step 12b: Derive exact no-face metrics.** With no face, compute ascent via `CheckedMulDiv(fontSize,3,4)`, descent via `CheckedMulDiv(fontSize,1,4)`, line gap as exact zero, and missing-grapheme advance as exactly `fontSize`, using the shared half-away-from-zero rule.

- [ ] **Step 12c: Materialize empty logical lines.** Empty input produces one metric-bearing `{0,0}` byte/grapheme line. Display line ranges exclude the separator grapheme itself: for `"A\n"`, the first range is byte/grapheme `{0,1}` and its empty successor is `{2,2}`. Use selected-family metrics or Step 12b when no face exists.

- [ ] **Step 13: Position and retain final output.** Compute checked 26.6 advances, horizontal/vertical alignment and intrinsic size from line top/bottom; every positioned glyph retains its existing exact `FontFaceResourcePtr`.

- [ ] **Step 13a: Validate final adjusted GDEF carets.** For a final glyph covering `N>1` graphemes, accept its `adjustedGdefCaretOffsets` only when there are exactly `N-1` checked positions, strictly ordered in visual progression and strictly inside the glyph's final advance; otherwise select the Step 13c fallback path as a whole.

- [ ] **Step 13b: Store valid GDEF carets in final layout.** Map the accepted `N-1` offsets to logical grapheme boundaries according to run direction, add the final glyph origin with checked arithmetic, and store absolute `GlyphInteriorCaret{boundary,position,true}` values on that `PositionedGlyph`.

- [ ] **Step 13c: Materialize proportional fallback carets once.** When the final shaped record has no valid complete GDEF set, compute all `N-1` positions during layout with `CheckedMulDiv(finalAdvance.Raw(), i, N)` in visual progression, map them to logical boundaries, and store `{...,false}`. No later hit-test call may recalculate this fallback or inspect a font resource.

- [ ] **Step 14: Store every layout-producing result.** Build the final paragraph key, attach immutable deduplicated facts, and cache replacement/tofu/fallback-producing layouts under the linked request/final keys; only a hard failure that produced no layout skips storage.

- [ ] **Step 14a: Emit cold validation facts through current context.** Convert each stored fact to one contextual diagnostic, report it once, and return the same immutable layout that was stored.

- [ ] **Step 14b: Expose package-grade validation facts.** Keep `TextLayout::validationFacts` public and immutable; the later package validator iterates every fact and rejects any `blocksAuthoredPackage==true`, never inferring validity from cache hit/miss state or the absence of newly executed analysis.

- [ ] **Step 15: Review expected JSON and run final-line green gates.** The canonical fixture serializes every line's exact byte/grapheme ranges, baseline, ascent, descent, lineGap, top and bottom—including empty/trailing lines—before the glyph/run arrays.

  ```bash
  cmake --build --preset debug --target test_text_layout test_text_cache -j
  ctest --test-dir build/debug -R '^(test_text_layout|test_text_cache)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_text_layout -j
  ctest --test-dir build/asan -R '^test_text_layout$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_text_layout -j
  ctest --test-dir build/ubsan -R '^test_text_layout$' --output-on-failure
  ```

  Expected: explicit separators, all wrap/overflow modes, Arabic/Indic/`fi` contextual changes, ellipsis, max lines, alignment and warm-cache zero-reshape pass.

- [ ] **Step 16: Commit authoritative paragraph layout.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextLayoutService.* \
    tests/test_text_layout.cpp tests/fixtures/text/expected/layout.json
  git commit -m "feat: reshape authoritative final text lines"
  ```

**Exit:** A final line's glyph array is always the result of shaping that line, never a slice of paragraph output.

---

### Task 7.3: Add BiDi-affinity caret, selection, and visual hit testing

**Prerequisite:** Task 7.2 emits final visual runs and grapheme/source mappings.

**Files:**

- Create: `src/Text/TextHitTesting.h`
- Create: `src/Text/TextHitTesting.cpp`
- Modify: `src/Text/TextLayoutService.cpp`
- Modify: `tests/test_text_layout.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: immutable final `TextLayout`, its precomputed `PositionedGlyph::interiorCarets`, and ICU visual run order; it has no HarfBuzz/font dependency.
- Produces: `CaretPosition` and `TextHitTesting::{HitTest,CaretRects,SelectionRects}`.

- [ ] **Step 1: Add failing affinity/ligature/selection/hit tests.**

  ```cpp
  TEST_CASE("BiDi boundary exposes two affinity-specific visual stops") {
      const auto layout = LoadLayoutFixture("hebrew-number-boundary").Layout();
      const auto stops = StopsAtBoundary(*layout, 4);
      REQUIRE(stops.size() == 2);
      CHECK(stops[0].affinity != stops[1].affinity);
      CHECK(stops[0].position.x != stops[1].position.x);
  }
  ```

- [ ] **Step 1a: Add the failing visual-midpoint test.**

  ```cpp
  TEST_CASE("visual midpoint ties move in the visual run direction") {
      const auto ltr = LoadLayoutFixture("ltr-midpoint").Layout();
      const auto rtl = LoadLayoutFixture("rtl-midpoint").Layout();
      CHECK(TextHitTesting::HitTest(*ltr, ExactMidpoint(*ltr)).boundary == 1);
      CHECK(TextHitTesting::HitTest(*rtl, ExactMidpoint(*rtl)).boundary == 0);
  }
  ```

- [ ] **Step 1b: Add the failing ligature-caret test.**

  ```cpp
  TEST_CASE("ligature carets remain on every grapheme boundary") {
      auto fixture = LoadLayoutFixture("latin-ffi-ligature");
      const auto layout = fixture.Layout();
      CHECK(AllInteriorCaretsCameFromAdjustedGdef(*layout));
      fixture.ResetFontAndShaperCounters();
      CHECK(CaretBoundarySequence(*layout) ==
            std::vector<std::uint32_t>{0, 1, 2, 3});
      CHECK(CaretXPositionsAreStrictlyIncreasing(*layout));
      CHECK(NoCaretInsideUtf16ScalarOrGrapheme(*layout));
      CHECK(TextHitTesting::HitTest(*layout, fixture.PointInsideLigature()).boundary > 0);
      CHECK(fixture.HarfBuzzCallCount() == 0);
      CHECK(fixture.FontResourceCallCount() == 0);
  }
  ```

- [ ] **Step 1c: Add the failing stored-fallback-caret test.** Use a synthetic final shaped ligature with empty `adjustedGdefCaretOffsets`, lay it out, and require all `PositionedGlyph::interiorCarets` are present with `fromAdjustedGdef=false` and exact `CheckedMulDiv` positions. Reset layout/shaper/font counters, call every hit/caret/selection API, and require all counters remain zero with identical returned stops.

- [ ] **Step 1d: Add the failing mixed-BiDi selection test.**

  ```cpp
  TEST_CASE("mixed BiDi logical selection emits stable visual rectangles") {
      const auto fixture = LoadLayoutFixture("mixed-bidi-selection");
      const auto layout = fixture.Layout();
      const auto rects = TextHitTesting::SelectionRects(*layout, {2, 9});
      CHECK(rects.size() > 1);
      CHECK(CanonicalFixedRects(rects) == fixture.ExpectedSelectionRects());
      CHECK(RectsAreInStableVisualOrder(rects));
  }
  ```

- [ ] **Step 2: Run the hit-testing red gate.**

  Run: `cmake --build --preset debug --target test_text_layout -j`

  Expected: compile FAIL because `TextHitTesting` and affinity-aware results do not exist.

- [ ] **Step 3: Add the exact public hit API.**

  ```cpp
  struct CaretPosition {
      std::uint32_t boundary = 0;
      CaretAffinity affinity = CaretAffinity::Downstream;
  };
  class TextHitTesting {
  public:
      static CaretPosition HitTest(const TextLayout&, FixedPoint);
      static std::vector<FixedRect> CaretRects(
          const TextLayout&, CaretPosition, Fixed26_6 thickness);
      static std::vector<FixedRect> SelectionRects(
          const TextLayout&, GraphemeRange logicalSelection);
  };
  ```

- [ ] **Step 4: Generate grapheme-only caret stops.** At each logical grapheme boundary emit the visual stop(s) from final line visual order. A BiDi boundary may emit Upstream and Downstream at different visual positions. Never create a stop inside a surrogate pair or combining/ZWJ grapheme.

- [ ] **Step 5: Consume only stored ligature carets.** Copy `PositionedGlyph::interiorCarets` into the line's grapheme-boundary stop sequence in stable visual order. `TextHitTesting.cpp` may not include HarfBuzz/font headers, dereference `faceResource`, call `hb_ot_layout_get_ligature_carets`, or perform proportional division; missing/invalid interior-caret storage is a layout invariant failure, not a late fallback.

- [ ] **Step 6: Implement half-open visual hit testing.** Build intervals between adjacent visual stops, treat them as half-open, and resolve an exact midpoint to the stop in visual progression direction.

- [ ] **Step 7: Implement caret rectangles.** Resolve the requested logical boundary plus affinity to its exact visual stop and produce checked 26.6 rectangles with the caller's positive thickness.

- [ ] **Step 8: Implement selection rectangles.** For logical half-open `[start,end)`, intersect each visual run segment and emit one rect per nonempty segment in stable visual order; mixed BiDi may return multiple rects.

- [ ] **Step 9: Run hit-testing and sanitizer green gates.**

  Run: `cmake --build --preset debug --target test_text_layout -j && ctest --test-dir build/debug -R '^test_text_layout$' --output-on-failure && cmake --build --preset asan --target test_text_layout -j && ctest --test-dir build/asan -R '^test_text_layout$' --output-on-failure`

  Expected: affinity, ligature fallback, multi-rect selection, visual order, and exact tie behavior pass.

- [ ] **Step 10: Commit hit testing.**

  ```bash
  git add CMakeLists.txt src/Text/TextHitTesting.* \
    src/Text/TextLayoutService.cpp tests/test_text_layout.cpp
  git commit -m "feat: add BiDi text hit testing"
  ```

**Exit:** Caret and selection operate only on logical grapheme boundaries while preserving multiple visual positions at BiDi boundaries.

---

### Task 8.1: Add legacy-preserving UILabel and TextRenderer2D schemas

**Prerequisite:** Paragraph style/types are stable; rendering remains on the legacy path during this task.

**Files:**

- Modify: `src/ECS/Components/UILabel.h`
- Modify: `src/ECS/Components/UILabel.cpp`
- Modify: `src/ECS/Components/TextRenderer2D.h`
- Modify: `src/ECS/Components/TextRenderer2D.cpp`
- Modify: `tests/test_scene_serializer.cpp`
- Modify: `tests/test_ui.cpp`

**Interfaces:**

- Consumes: `ParagraphStyle` enums and existing component serialization.
- Produces: UILabel schema `2`, TextRenderer2D schema `2`, and a runtime-only loaded-format marker preserving legacy representation until explicit Milestone 15 migration.

- [ ] **Step 1: Add failing legacy/current round-trip tests.**

  ```cpp
  TEST_CASE("legacy UILabel load save does not silently rewrite fontGuid") {
      const nlohmann::json legacy = {
          {"type", "UILabel"}, {"fontGuid", "font-a"}, {"text", "hello"}};
      UILabel label;
      label.Deserialize(legacy);
      CHECK(label.LoadedLegacyFontGuid());
      nlohmann::json saved;
      label.Serialize(saved);
      CHECK(saved["fontGuid"] == "font-a");
      CHECK_FALSE(saved.contains("fontFamilyGuid"));
  }
  ```

- [ ] **Step 1a: Add the failing current world-text contract test.**

  ```cpp
  TEST_CASE("current world text preserves unbounded world contract") {
      TextRenderer2D text;
      text.SetFontFamilyGuid("family-a");
      CHECK(text.WrapMode() == molga::text::TextWrapMode::NoWrap);
      CHECK(text.OverflowMode() == molga::text::TextOverflowMode::Overflow);
      CHECK_FALSE(text.HasAuthoredLayoutBounds());
  }
  ```

- [ ] **Step 2: Run the schema red gate.**

  Run: `cmake --build --preset debug --target test_scene_serializer test_ui -j`

  Expected: assertions FAIL because schema-2 fields and loaded-format preservation do not exist.

- [ ] **Step 3: Add UILabel schema 2.** Persist `fontFamilyGuid`, font size, line spacing, color, locale default `und`, base direction, `NoWrap/Word/Grapheme`, `Overflow/Clip/Ellipsis`, max lines, horizontal/vertical alignment, and sorting order. Legacy `fontGuid` produces an implicit one-face family view in memory and retains its loaded-format marker.

- [ ] **Step 4: Add TextRenderer2D schema 2.** Persist `fontFamilyGuid`, locale and base direction while retaining legacy `fontGuid/fontName`, local logical font size/component scale, explicit-newline `NoWrap/Overflow`, and sorting. Continue consuming position/world scale/rotation from the sibling `Transform` schema; do not duplicate those fields or invent width/height bounds.

- [ ] **Step 5: Preserve representation until explicit migration.** Saving an unmigrated legacy payload writes the legacy key/shape byte-for-byte except unrelated canonical serializer formatting. New authoring writes schema 2; runtime-only loaded markers never serialize or mark the scene dirty.

- [ ] **Step 6: Run schema/serializer green gates.**

  Run: `cmake --build --preset debug --target test_scene_serializer test_ui -j && ctest --test-dir build/debug -R '^(test_scene_serializer|test_ui)$' --output-on-failure`

  Expected: legacy/current round trips, UILabel bounded fields, and unbounded world-text contract pass.

- [ ] **Step 7: Commit schema compatibility.**

  ```bash
  git add src/ECS/Components/UILabel.* \
    src/ECS/Components/TextRenderer2D.* tests/test_scene_serializer.cpp \
    tests/test_ui.cpp
  git commit -m "feat: add shared text component schemas"
  ```

**Exit:** Both consumers can describe the shared service without changing how either production path renders yet.

---

### Task 8.2: Atomically migrate every rendered-text consumer to the shared pipeline

**Prerequisite:** Tasks 4–7 and schema Task 8.1 are green. This is the only task allowed to remove the legacy production renderer.

**Files:**

- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/ECS/Components/UILabel.h`
- Modify: `src/ECS/Components/UILabel.cpp`
- Modify: `src/ECS/Components/TextRenderer2D.h`
- Modify: `src/ECS/Components/TextRenderer2D.cpp`
- Modify: `src/ECS/Component.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Rendering/RenderQueue.h`
- Modify: `src/Rendering/WorldRenderTraversal.h`
- Modify: `src/Rendering/WorldRenderTraversal.cpp`
- Modify: `src/Rendering/GameOutputRenderer.h`
- Modify: `src/Rendering/GameOutputRenderer.cpp`
- Modify: `src/Editor/Windows/SceneViewWindow.h`
- Modify: `src/Editor/Windows/SceneViewWindow.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Rendering/Utf8.h`
- Modify: `src/Rendering/Utf8.cpp`
- Modify: `src/Rendering/FontAtlas.h`
- Modify: `src/Rendering/FontAtlas.cpp`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `tests/test_text.cpp`
- Modify: `tests/test_font.cpp`
- Modify: `tests/test_ui.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `tests/test_text_runtime_dependencies.cpp`
- Modify: `tests/test_world_sort.cpp`
- Modify: `tests/test_camera_output_layout.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `TextLayoutService`, `TextHitTesting`, `GlyphAtlasCache`, immutable resources, page tokens, and schema-2 authored data.
- Produces: `TextRasterPolicy`, `TextCollectContext`, `WorldRenderCollectionContext`, `TextRenderer::{Init,ShutdownAfterGpuIdle,LayoutService,Layout,CollectLayout,BeginGlyphCollection,GlyphAtlas}` and the sole production rendered-text path. Initialization/shutdown borrow their sink only for that call; no sink reference is stored.

- [ ] **Step 0: Move every newly production-text-backed regression executable onto the common runtime session.** Replace the existing `molga_add_test` registrations for `test_text`, `test_ui`, and `test_rendering_sdlgpu` with `molga_add_text_test`; `test_font` was already converted in Task 4.1. Do not link `doctest_main` or create another fixture-local ICU lifetime. Preserve every existing source, include, compile definition, `gpu;sdlgpu;pixel` label, working directory, timeout, and SDL/Metal property after target creation. All four executables use only `MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT` and the installed `TextRuntimeTestSession`; add a focused test-main audit that exactly one text runtime session is active before any `SharedTextConsumerFixture` constructs `TextLayoutService`.

- [ ] **Step 1: Replace silent-fallback expectations with failing shared-consumer tests.**

  ```cpp
  TEST_CASE("UI and world consumers share one shaping result") {
      SharedTextConsumerFixture f;
      const auto ui = f.CollectLabel(u8"سلام हिन्दी");
      const auto world = f.CollectWorldText(u8"سلام हिन्दी");
      CHECK(ui.CanonicalGlyphRecords() == world.CanonicalGlyphRecords());
      CHECK(f.ShapeCountForText(u8"سلام हिन्दी") == 1);
  }
  ```

- [ ] **Step 1a: Add the failing old/new hot-reload collection test.**

  ```cpp
  TEST_CASE("old cached layout never reopens a font after hot reload") {
      SharedTextConsumerFixture f;
      const auto oldLayout = f.LayoutLabel("family-a", u8"ffi");
      REQUIRE(oldLayout);
      const std::string oldSha = FirstGlyph(**oldLayout).faceResource->sourceSha256;
      f.ReplaceFamilyFaceWithVerifiedBytes(
          "family-a", MOLGA_TEXT_INTER_FONT);
      const auto newLayout = f.LayoutLabel("family-a", u8"ffi");
      REQUIRE(newLayout);
      const std::string newSha = FirstGlyph(**newLayout).faceResource->sourceSha256;
      REQUIRE(newSha != oldSha);
      const auto loadsBeforeCollection = f.repository.ByteLoadCount();
      {
          auto scope = f.renderer.BeginGlyphCollection(71);
          f.renderer.CollectLayout(
              f.queue, **oldLayout, f.UiCollectContext(), f.sink);
          f.renderer.CollectLayout(
              f.queue, **newLayout, f.WorldCollectContext(), f.sink);
      }
      CHECK(f.repository.ByteLoadCount() == loadsBeforeCollection);
      CHECK(f.RasterizationCountForSourceSha(oldSha) > 0);
      CHECK(f.RasterizationCountForSourceSha(newSha) > 0);
  }
  ```

- [ ] **Step 1b: Add the failing zero-budget missing-family tofu test.**

  ```cpp
  TEST_CASE("missing family renders deterministic tofu without atlas access") {
      SharedTextConsumerFixture f;
      f.glyphAtlas.SetResidentBudget(0);
      const auto draw = f.CollectLabelWithFamily(u8"A👩‍🚀", "missing-family");
      REQUIRE(draw.TextCommands().size() == 2); // two extended graphemes
      CHECK(draw.CanonicalTofuRects() ==
            TofuRectsFromLayoutMetrics(draw.Layout()));
      CHECK(AllUseInvalidTextureHandleWhiteFallback(draw.TextCommands()));
      CHECK(f.glyphAtlas.LookupCountForTest() == 0);
      CHECK(f.glyphAtlas.Telemetry().uploads == 0);
      CHECK(HasDiagnostic(f.Diagnostics(),
            molga::text::TextDiagnosticCode::MissingGlyph));
      CHECK_FALSE(draw.UsedBuiltinAsciiTexture());
  }
  ```

- [ ] **Step 1c: Add the failing legacy-entrypoint removal test.**

  ```cpp
  TEST_CASE("TextRenderer2D has no immediate RenderSprite text path") {
      SharedTextConsumerFixture f;
      Component* component = &f.worldText;
      component->RenderSprite(&f.immediateRenderer);
      component->CollectRender(f.queue);
      CHECK(f.immediateRenderer.DrawCallCount() == 0);
      CHECK(f.queue.TextCommandCount() == 0);
      component->CollectRender(f.queue, f.WorldCollectionContext());
      CHECK(f.queue.TextCommandCount() > 0);
  }
  ```

  Before the migration this row fails to compile because the context-bearing
  virtual does not exist; after the migration the inherited one-argument base
  hooks are deliberate non-text no-ops and only the explicit two-argument
  traversal path may collect `TextRenderer2D`.

- [ ] **Step 1d: Add the failing world-affine corner/AABB test.**

  ```cpp
  TEST_CASE("world text applies nonuniform negative scale then rotation") {
      SharedTextConsumerFixture f;
      f.worldText.SetComponentScale(1.0f);
      f.worldTransform.SetWorldScale({-2.0f, 3.0f});
      f.worldTransform.SetWorldRotationDegrees(90.0f);
      f.worldTransform.SetWorldPosition({10.0f, 20.0f});
      const auto vertices = f.CollectOneLogicalQuad(
          f.worldText, FixedRect{Fixed26_6::FromRaw(0),
                                 Fixed26_6::FromRaw(0),
                                 Fixed26_6::FromRaw(2 * 64),
                                 Fixed26_6::FromRaw(1 * 64)});
      CHECK(vertices[0] == Vector2(10.0f, 20.0f));
      CHECK(vertices[1] == Vector2(10.0f, 16.0f));
      CHECK(vertices[2] == Vector2(7.0f, 16.0f));
      CHECK(vertices[3] == Vector2(7.0f, 20.0f));
      const AABB bounds = f.LastCommandWorldBounds();
      CHECK(bounds.x == doctest::Approx(7.0f));
      CHECK(bounds.y == doctest::Approx(16.0f));
      CHECK(bounds.width == doctest::Approx(3.0f));
      CHECK(bounds.height == doctest::Approx(4.0f));
  }
  ```

- [ ] **Step 1e: Add the failing UI affine parity test.**

  ```cpp
  TEST_CASE("UI identity affine plus translation preserves canonical vertices") {
      SharedTextConsumerFixture f;
      const FixedPoint origin{Fixed26_6::FromRaw(5 * 64),
                              Fixed26_6::FromRaw(7 * 64)};
      CHECK(f.CollectLabelWithAffine(origin).CanonicalVertices() ==
            f.CollectLabelReferenceTranslation(origin).CanonicalVertices());
  }
  ```

- [ ] **Step 1f: Add the failing non-finite affine test.**

  ```cpp
  TEST_CASE("non-finite text affine produces no render command") {
      SharedTextConsumerFixture f;
      TextCollectContext context = f.UiCollectContext();
      context.layoutToOutput.m00 =
          std::numeric_limits<float>::quiet_NaN();
      f.renderer.CollectLayout(f.queue, *f.ValidLayout(), context, f.sink);
      CHECK(f.queue.TextCommandCount() == 0);
      CHECK(HasDiagnostic(f.sink,
            molga::text::TextDiagnosticCode::LayoutInvalid));
  }
  ```

  The fixture canonicalizes exact quarter-turn sine/cosine before constructing the expected cardinal matrix; non-cardinal tests use component-wise `doctest::Approx`.

- [ ] **Step 1g: Register the hot-reload fixture path.** Place the Step 1a test/helper in `tests/test_text.cpp`, then add `target_compile_definitions(test_text PRIVATE MOLGA_TEXT_INTER_FONT="${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf")` immediately after that target is created; the test never discovers a host font.

- [ ] **Step 1h: Add failing raster-scale authority tests.** Reuse one immutable
  fractional-size layout at raster keys `64` and `128`; require distinct
  atlas keys/pixel heights, the exact quantized formulas below, and logical
  bitmap quads within one 26.6 raw unit after inverse-scale conversion. Add a
  nonuniform negative world transform row whose explicit frame policy is
  combined with `max(abs(scaleX),abs(scaleY))`, plus zero/NaN/overflow scale
  rows that emit `LayoutInvalid` and no atlas lookup/command.

- [ ] **Step 1i: Add failing world-service authority tests.** A world containing
  `TextRenderer2D` receives one `WorldRenderCollectionContext` and observes the
  exact caller-owned renderer, `renderer.LayoutService()`, diagnostic sink, and
  raster policy. A null text context is allowed only through
  `NonTextOnlyForTesting()` after the fixture proves it contains no
  `TextRenderer2D`; production Game Output and Scene View compile with no
  context-less traversal overload. Assert that `TextRenderer::Init` stores no
  sink and that world collection uses the frame sink rather than a temporary or
  hidden reference.

- [ ] **Step 2: Run the atomic migration red gate.**

  Run: `cmake --build --preset debug --target test_text test_font test_ui test_rendering_sdlgpu -j && ctest --test-dir build/debug -R '^(test_text|test_font|test_ui|test_rendering_sdlgpu)$' --output-on-failure`

  Expected: FAIL against the current codepoint/ASCII UILabel and TextRenderer2D paths.

- [ ] **Step 3: Add the exact text affine POD.**

  ```cpp
  struct TextAffine2D {
      float m00 = 1.0f, m01 = 0.0f;
      float m10 = 0.0f, m11 = 1.0f;
      float tx = 0.0f, ty = 0.0f;
      Vector2 Apply(molga::FixedPoint point) const;
  };
  ```

- [ ] **Step 3a: Add the exact collection context.**

  ```cpp
  struct TextRasterPolicy {
      std::uint16_t rasterScaleKey = 64; // unsigned Q10.6; 64 == 1x
      static std::optional<TextRasterPolicy> FromUiScale(
          molga::FixedSize logicalViewport, molga::PixelSize physicalViewport,
          molga::text::TextDiagnosticSink&);
      static std::optional<TextRasterPolicy> FromWorldPixelsPerUnit(
          double pixelsPerWorldUnit, molga::text::TextDiagnosticSink&);
      std::optional<TextRasterPolicy> ScaledForWorldTransform(
          float scaleX, float scaleY,
          molga::text::TextDiagnosticSink&) const;
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
  class TextRenderer;
  struct WorldRenderCollectionContext {
      TextRenderer* textRenderer = nullptr;
      molga::text::TextDiagnosticSink* textDiagnostics = nullptr;
      TextRasterPolicy baseTextRasterPolicy;
      static WorldRenderCollectionContext NonTextOnlyForTesting();
  };
  ```

  Quantize a positive finite scale with half-away-from-zero to unsigned Q10.6
  `[1,65535]`; reject zero, nonfinite, and overflow. UI computes X/Y scale keys
  from checked integer `physicalPixels * 4096 / logicalRawExtent` and uses their
  maximum. Game Output derives `pixelsPerWorldUnit` from the active camera's
  world-to-physical output transform; TextRenderer2D multiplies that key by the
  maximum absolute component/world scale with the same checked quantization.
  Neither path infers raster scale from glyph advance or the collection affine.

- [ ] **Step 3b: Replace the `TextRenderer` public production API.**

  ```cpp
  class TextRenderer {
  public:
      bool Init(const molga::AssetDatabase&,
                molga::text::TextDiagnosticSink&);
      bool ShutdownAfterGpuIdle(molga::text::TextDiagnosticSink&);
      molga::text::TextLayoutService& LayoutService() noexcept;
      std::optional<std::shared_ptr<const molga::text::TextLayout>> Layout(
          const molga::text::TextLayoutRequest&,
          molga::text::TextDiagnosticSink&);
      void CollectLayout(molga::RenderQueue&,
                         const molga::text::TextLayout&,
                         const TextCollectContext&,
                         molga::text::TextDiagnosticSink&);
      GlyphCollectionScope BeginGlyphCollection(std::uint64_t frameIndex);
      GlyphAtlasCache& GlyphAtlas() noexcept;
      const GlyphAtlasCache& GlyphAtlas() const noexcept;
      static TextRenderer& Get();
  };
  ```

  `Init` first requires ready text dependencies and takes the process
  `AssetDatabase` by stable reference, then constructs one owned `TextServices`
  aggregate in dependency order: `FontRepository`, `FontFamilyResolver`,
  `TextShapingService`, `TextLayoutCache{TextLayoutCacheLimits::Production()}`,
  and `TextLayoutService` referencing those same preceding members. The
  aggregate's reverse member destruction releases layout/cache/shaper/resolver/
  repository before the application runtime guard performs terminal ICU
  cleanup. `Init` borrows its diagnostic sink only until it returns and stores
  no sink reference; failure publishes neither a partial aggregate nor an
  initialized renderer. A project/sealed `FontArtifactStore` must be bound
  before the first layout that resolves a font. The exact owned
  `TextLayoutService::Layout`/shared-repository edge rechecks that boundary on
  every font-bearing request and reports `FontInvalid` without a source/system
  fallback when absent, so both the `TextRenderer::Layout` facade and direct
  `LayoutService()` consumers use the same gate. This
  permits the editor shell to initialize before a project opens without giving
  an unbound font request implicit authority. `LayoutService()` returns that exact owned object and
  `Layout` delegates to it. `TextRenderer::Get()` remains the single process
  composition root, but UI/world/Game View/runtime resolve it once at their
  outer owner and pass the exact `TextRenderer&`/`LayoutService()` reference
  downstream; a component/system may not construct a clone, cache a second
  service, or perform an internal singleton lookup.
  `ShutdownAfterGpuIdle` runs only after the Task 6 renderer idle/retirement
  drain has succeeded. It reports `ReferenceInvalid` and returns false before
  mutation when a glyph-collection scope is active or
  `GlyphAtlas().LiveExternalPagePinCount()!=0`; otherwise it requires
  `GlyphAtlas().ReleaseAfterGpuIdle()`, then destroys the owned text-service
  aggregate and returns true. Before Task 11 an idle failure
  takes Task 6's interim terminal branch and never calls it; Task 11 replaces
  that outer branch with the retryable `EngineShutdown` state machine without
  weakening this destruction order.

- [ ] **Step 3c: Implement affine application and validation.** `Apply` means `x'=m00*x+m01*y+tx`, `y'=m10*x+m11*y+ty` after converting the checked fixed point to float. Before creating any command, `CollectLayout` rejects the context with `LayoutInvalid` unless all six floats are finite.

- [ ] **Step 3d: Transform exact command geometry.** For a drawable bitmap,
  convert each signed bearing/unsigned width/height from raster pixels to 26.6
  layout units with `pixel * 4096 / rasterScaleKey`, checked signed arithmetic,
  and half-away-from-zero. Starting from positioned origin + HarfBuzz offsets,
  compute left/top from bitmap bearings and right/bottom by checked size add;
  never substitute advance bounds. Apply `layoutToOutput` to all four bitmap or
  tofu corners and compute `worldBounds` only from those transformed corners.
  Task 11 extends this same context with translated UI draw order and optional
  final physical scissor; it does not introduce a sink-less overload.

- [ ] **Step 4: Derive a normal glyph's complete atlas key.** Only when
  `!glyph.missing && glyph.faceResource && glyph.faceResource->rasterFace`, copy
  `fontGuid`, `fontRevision`, `faceIndex`, `glyphId`, and exact positive
  `glyph.fontSize` plus `context.rasterPolicy.rasterScaleKey`. Compute
  `pixelSize = roundHalfAway(glyph.fontSize.Raw() * rasterScaleKey / 4096)` in
  checked 64-bit arithmetic and require `[1,65535]`; copy that `uint16_t`, the
  Q10.6 raster key, default variation, and monochrome mode into `GlyphAtlasKey`.
  Failure emits `LayoutInvalid` and no lookup/partial command. Never query
  `AssetDatabase`, reopen a GUID, or infer size from advance/affine.

- [ ] **Step 4a: Resolve and retain the normal glyph handle.** Call `GetGlyph(key, *glyph.faceResource->rasterFace, sink)`; for a drawable handle, set `command.batchKey.texture = handle.glyph.texture->Handle()` and copy both nonzero `pageIdentity` and `pageLifetime` into the command.

- [ ] **Step 5: Emit missing/null-face tofu without atlas access.** When `glyph.missing`, `!glyph.faceResource`, or `!glyph.faceResource->rasterFace`, perform no atlas lookup and create one solid-color command with `texture=molga::TextureHandle{}`, `pageIdentity=0`, and no lifetime token.

- [ ] **Step 5a: Compute deterministic tofu geometry.** Use checked logical rect `{origin.x, line.baseline-line.ascent, width, line.ascent+line.descent}`, where `width` is the widened absolute raw `glyph.advanceX` with minimum raw value `1`; reject an impossible fixed-point overflow instead of wrapping.

- [ ] **Step 5b: Route atlas saturation through identical tofu.** When normal `GetGlyph` returns `proceduralTofu`, call the same geometry/command helper as Steps 5–5a and retain no atlas page identity/token.

- [ ] **Step 5c: Bind invalid texture handles to renderer white.** Keep `molga::TextureHandle{}` in `BatchKey`; the sprite path maps that invalid handle to its renderer-owned white fallback, preserving one renderable command per missing grapheme even at zero atlas budget.

- [ ] **Step 6: Build UILabel's bounded shared request.** `UISystem` maps schema-2 label/style plus exact RectTransform width/height into one `TextLayoutRequest` and obtains its immutable layout from the shared renderer service.

- [ ] **Step 6a: Collect UILabel with UI translation affine.** Set `layoutToOutput` to identity with `tx/ty` from the fixed logical UI origin, preserve UI color/sorting values, and call `CollectLayout(queue, layout, context, sink)`.

- [ ] **Step 7: Build TextRenderer2D's unbounded shared request.** Map schema 2 to unbounded `NoWrap/Overflow`, reuse the shared shape/baseline result, and do not synthesize width/height constraints.

- [ ] **Step 7a: Build the world scale-rotate-translate affine.** Let `sx/sy = componentScale * Transform::GetWorldScale()`, `degrees = Transform::GetWorldRotation()`, `r = degrees * pi / 180`, and `p = Transform::GetWorldPosition()`; set `layoutToOutput = {cos(r)*sx, -sin(r)*sy, sin(r)*sx, cos(r)*sy, p.x, p.y}` to preserve negative/nonuniform scale.

- [ ] **Step 7b: Collect world text through the shared renderer.** Copy the complete current `SortKey` inputs—camera pass, integer sorting layer/order, and `depthOrYSort`—into `TextCollectContext`, then call `worldContext.textRenderer->CollectLayout(queue, layout, context, *worldContext.textDiagnostics)`. Reject either null pointer before layout/atlas work whenever a text component is reachable; never synthesize a temporary sink or consult `TextRenderer::Get()`.

- [ ] **Step 7c: Add the explicit world collection context.** Add the
  global forward declaration `struct WorldRenderCollectionContext;` beside the
  existing `RenderQueue` declaration in `Component.h`, then add the
  two-argument virtual `Component::CollectRender(RenderQueue&,
  const WorldRenderCollectionContext&)` whose default implementation calls the
  existing one-argument virtual for non-text components. `TextRenderer2D`
  overrides only the two-argument form. Change both `CollectWorldRender`
  overloads to require the context and make traversal call only that virtual;
  delete every context-less traversal overload after updating all callers.

- [ ] **Step 7d: Pass one renderer/service/sink authority through every world
  output.** `GameOutputRenderer` and Scene View receive the caller-owned
  `TextRenderer&`, its `LayoutService()`, current frame diagnostic sink, and
  camera-derived base raster policy, then pass one context through traversal.
  Update editor/runtime mains and the world-sort/camera-output fixtures in this
  slice. `NonTextOnlyForTesting()` contains null pointers and is accepted only
  after a fixture asserts no active `TextRenderer2D`; production code and any
  text-containing fixture fail a compile/audit check if they use it.

- [ ] **Step 7e: Migrate the runtime and GPU text proofs.** Replace
  `runtime_main.cpp` and the existing font/SDL_GPU fixture calls to
  `CollectText`, `GetAtlasPageCount`, and `GetCachedFontSizeCount` with the
  exact `Layout`/`CollectLayout` path. Tests inspect the exact Task 6
  `renderer.GlyphAtlas().Telemetry()` authority; do not create or inject a
  second cache.
  The packaged startup proof succeeds only after a shaped glyph-ID command is
  collected with a nonzero page identity and retained page token; it never uses
  a font-GUID/codepoint atlas counter as evidence.

- [ ] **Step 7f: Preserve startup and shutdown owner order.** In editor/runtime
  mains, create the Task 2 text runtime guard before
  `TextRenderer::Init(assetDatabase, sink)`; the passed database is the same
  process-owned authority that later receives the project/sealed artifact-store
  binding and outlives the renderer. Bind the project/sealed font-artifact
  store before the first font-bearing
  layout (the editor may remain an unbound no-project shell). On every normal/early return,
  stop world/UI collection, complete the Task 6 renderer idle/retirement drain,
  call `TextRenderer::ShutdownAfterGpuIdle(sink)` and require success before
  destroying renderer/device owners, then let the
  text runtime guard perform terminal cleanup last. Extend the existing unwind
  audit with `atlas_cleared < text_services_destroyed < u_cleanup` and require
  neither clear/destruction marker after the interim failed-idle abort.

- [ ] **Step 8: Remove legacy text measurement/collection APIs.** Delete production `MeasureText`, `GetTextWidth`, `GetTextHeight`, and `CollectText` declarations/definitions, then update their now-shared-pipeline callers.

- [ ] **Step 8a: Delete the isolated codepoint atlas.** Remove legacy `FontAtlasCache`, its codepoint lookup, and the temporary `FontAtlasGlyph` alias after both consumers compile against `GlyphAtlasCache`.

- [ ] **Step 8a.1: Delete legacy raster measurements.** Remove
  `FontFace::Advance`, `Kerning`, codepoint rasterization, and float
  measurement compatibility once the last consumer moves. Retain
  `HasCodepoint`/`GlyphId` only for non-fallback inspection and add an `rg`/link
  test proving no shaping, layout, or render consumer calls them.

- [ ] **Step 8b: Delete stb measurement from production.** Remove advance/kerning measurement calls and compatibility methods that no remaining non-production test/API needs; stb remains bitmap rasterization only.

- [ ] **Step 8c: Delete orphaned legacy text value types.** Remove `CharInfo`, `TextMetrics`, and `TextDrawParams` once all callers use `TextLayoutRequest` and `TextCollectContext`.

- [ ] **Step 8d: Remove the core-to-legacy-renderer invalidation seam.** Delete
  every `AssetDatabase` include/call of
  `TextRenderer::{InvalidateFont,InvalidateAllFonts}` and delete those legacy
  renderer methods. A successfully published font/family record advances the
  Task 4 content generation, which is already part of resolver/layout/resource
  cache identity; a failed import advances nothing. Extend the old/new
  hot-reload test to rescan through `AssetDatabase` (without a direct renderer
  callback), require the next layout uses the new resource/revision, and retain
  the old layout/resource unchanged. Bounded caches may age out old-generation
  entries normally; no Core layer reaches into Rendering.

- [ ] **Step 9: Remove the TextRenderer2D immediate-render bypass.** Delete the
  `TextRenderer2D::RenderSprite(Renderer*)` and one-argument
  `CollectRender(RenderQueue&)` overrides; its two-argument context-bearing
  collection is the only text submission entrypoint.

- [ ] **Step 10: Remove built-in ASCII asset creation/storage.** Delete `GenerateBuiltinFont`, its built-in texture, and its character map after the shared tofu test is in place.

- [ ] **Step 10a: Remove the silent fallback selection branch.** Delete every branch that selected those assets; missing family/glyph now reaches only procedural tofu plus typed diagnostics.

- [ ] **Step 11: Remove production compatibility-decoder calls.** Keep `DecodeUtf8` only if a non-production API/test still consumes it; no renderer, UILabel, or TextRenderer2D path may call it.

- [ ] **Step 12: Verify the source-level removal before green tests.**

  ```bash
  rg -n 'GenerateBuiltinFont|FontAtlasCache|FontAtlasGlyph|CharInfo|TextMetrics|TextDrawParams|FontFace::Metrics|GetKerning\(|GetTextWidth\(|GetTextHeight\(|GetAtlasPageCount\(|GetCachedFontSizeCount\(|MeasureText\(|DecodeUtf8\(|RenderText\(|CollectText\(|TextRenderer2D::RenderSprite|InvalidateAllFonts\(|InvalidateFont\(' \
    src/Rendering/TextRenderer.h src/Rendering/TextRenderer.cpp \
    src/Rendering/FontAtlas.h src/Rendering/FontAtlas.cpp \
    src/Rendering/FontFace.h src/Rendering/FontFace.cpp \
    src/UI/UISystem.cpp src/Core/AssetDatabase.h src/Core/AssetDatabase.cpp \
    src/ECS/Components/TextRenderer2D.h \
    src/ECS/Components/TextRenderer2D.cpp src/runtime_main.cpp
  ```

  Expected: no matches.

- [ ] **Step 13: Run focused consumer/GPU/serialization gates.**

  Run: `cmake --build --preset debug --target test_text test_font test_ui test_scene_serializer test_glyph_atlas test_gpu_retirement test_rendering_sdlgpu test_text_runtime_dependencies test_world_sort test_camera_output_layout molga_engine molga_runtime -j && ctest --test-dir build/debug -R '^(test_text|test_font|test_ui|test_scene_serializer|test_glyph_atlas|test_gpu_retirement|test_rendering_sdlgpu|test_text_runtime_dependencies|test_world_sort|test_camera_output_layout)$' --output-on-failure`

  Expected: UI/world canonical glyph records match, bounded/unbounded contracts hold, missing glyphs use tofu, and GPU page lifetimes remain valid.

- [ ] **Step 14: Run the complete Debug regression gate.**

  Run: `ctest --preset debug`

  Expected: the complete suite passes with no remaining silent ASCII fallback.

- [ ] **Step 15: Commit the atomic production migration.**

  ```bash
  git add src/Rendering/TextRenderer.* src/Rendering/Utf8.* \
    src/Rendering/FontAtlas.* src/Rendering/FontFace.* src/Rendering/RenderQueue.h \
    src/Rendering/WorldRenderTraversal.* src/Rendering/GameOutputRenderer.* \
    src/Editor/Windows/SceneViewWindow.* src/ECS/Component.h \
    src/ECS/Components/UILabel.* src/ECS/Components/TextRenderer2D.* \
    src/UI/UISystem.cpp src/Core/AssetDatabase.* src/main.cpp src/runtime_main.cpp \
    tests/test_text.cpp tests/test_font.cpp tests/test_ui.cpp \
    tests/test_rendering_sdlgpu.cpp tests/test_text_runtime_dependencies.cpp \
    tests/test_world_sort.cpp \
    tests/test_camera_output_layout.cpp tests/CMakeLists.txt
  git commit -m "feat: route rendered text through shared layout"
  ```

**Exit:** UILabel and TextRenderer2D reach HarfBuzz, final-line layout, glyph-ID atlas, and fence-backed page ownership through exactly one production pipeline.

## Subplan Completion Gate

- [ ] Run `git diff --check` and scan this plan without self-matching the pattern: `rg -n 'T[B]D|T[O]DO|F[I]XME|implement l[a]ter|similar t[o]' docs/superpowers/plans/2026-08-20-ui-text/02-font-shaping-layout.md`.
- [ ] Run all Exit Contract gates from a fresh Debug configure and the listed ASan/UBSan shaping/layout gates; retain actual outputs as review evidence.
- [ ] Invoke `superpowers:requesting-code-review` for the complete Milestones 4–8 commit range and resolve every blocker/high finding before beginning UI layout Milestone 9.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-20-ui-text/02-font-shaping-layout.md`. Execute it with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`; do not advance to UI snapshot work until the atomic migration and full subplan exit gate are green.
