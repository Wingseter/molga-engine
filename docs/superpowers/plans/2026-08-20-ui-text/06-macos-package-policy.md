# Canonical macOS UI/Text Package Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce one canonical macOS `.app` resource root, stage the complete packaged-font/ICU/license closure, validate authored coverage, and fail closed with exit code `4` before scene or SDL startup when a sealed package is missing or altered.

**Architecture:** Separate executable and resource roots everywhere: release uses `<Game>.app/Contents/{MacOS,Resources}`, while the flat root survives only behind an explicit development flag. Treat the machine-local dependency build lock as build audit input and the portable dependency contract as the only package/runtime dependency record. Scan reachable authored text through production decode/analyze/shape/layout services, materialize a deterministic coverage/font/license/notice closure, then validate the sealed hash chain before any window, renderer, script, asset, or scene is started.

**Tech Stack:** C++17, CMake 3.27, doctest, nlohmann/json, macOS app bundles/Info.plist, SHA-256, `otool`, existing GameBuilder/PackageFinalizer/smoke infrastructure.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md)

## Global Constraints

- The approved design and master [`UI/Text Production Backbone Implementation Plan`](../2026-08-20-ui-text-production-backbone.md) are authoritative. Stop for amendment approval if a package path, terminal action, dependency hash, or release scope must change.
- Begin only after [`05-editor-authoring-migration.md`](05-editor-authoring-migration.md) exits green and Tasks 1–15 are present in dependency order.
- Preserve unrelated worktree changes. Before every task run `git status --short --branch`; stage only that task's files.
- Release output uses `Game.app/Contents/Resources` as the only `RuntimeResourceRoot`. A flat executable directory is legal only for `molga_runtime_dev --development-resource-root <absolute-path>`.
- Never package `${CMAKE_BINARY_DIR}/generated/text_dependency_build_lock.json`. Package only the portable `text_dependency_contract.json`, which contains no checkout/build absolute path.
- `TextRuntimeManifest::dependencyLockSha256` is the retained schema field name but means the SHA-256 of `Engine/Text/text_dependency_contract.json`; tests must make that definition explicit.
- The canonical package contains `icudt78l.dat` at 33,107,232 bytes with SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b` and the exact pinned HarfBuzz/ICU/rasterizer contract and license hashes from subplan `01`.
- The packaged notice is generated from the verified immutable engine notice template plus project-font rows sorted by `{fontGuid,sourceSha256,licenseAssetGuid}`. Generation never edits the source template.
- Font packaging uses policy B: generic asset copying is catalog-record driven,
  excludes every `FontImporter` source/meta and the generic license record
  resolved from its `licenseAssetGuid`, and stages/catalogs only the validated
  reachable font closure. Each reachable face is read only from Task 4.1's
  verified immutable `Library/Imported/Fonts/<sha256>.sfnt` artifact, never its
  authored source, and each distinct artifact is staged once as
  `Contents/Resources/Assets/Fonts/<artifactSha256>.sfnt`. The sealed catalog
  rewrites only its font artifact locator from `ProjectLibrary` to
  `PackagedResource`; `TextRuntimeManifest.fonts[].sourcePath` is that same
  packaged locator and `sourceSha256` is the verified identical artifact hash.
  A package-wide SFNT magic/path audit forbids authored-source copies,
  `Library/Imported` copies, duplicates, and every unmanifested font byte.
- Every reachable scene/prefab `UILabel`, `UITextInput.initialText`, and `TextRenderer2D`, registered localization entry, and `requiredTextFixtures` item is processed through production text services. Invalid authored UTF-8 or missing glyphs fail the build.
- Coverage records also bind every final staged scene, prefab, localization,
  and required-fixture config document by safe resource-relative path and
  SHA-256. The runtime manifest independently binds the final schema-3 asset
  catalog; both edges are verified before asset or scene load.
- Dynamic runtime strings outside declared fixtures are not claimed as covered; invalid/missing dynamic text uses replacement/tofu plus a rate-limited diagnostic and continues.
- Package/startup validation is fail closed. A dependency, data, font, license, notice, manifest, authored coverage, or path/hash failure blocks GameBuilder; the copied sealed runtime prints the typed failure and exits `4` before SDL/window/scene startup.
- Do not add system-font/CoreText fallback, Homebrew dependencies, runtime network fetch, signing, notarization, Universal 2, Intel, or non-macOS qualification claims.
- Code tasks end in independent commits. Package review and release qualification are separate gates and do not get folded into a code commit.

---

## Prerequisite Contract

The executor must find the following producers from earlier milestones:

```cpp
// Task 1: generated artifacts
MOLGA_TEXT_DEPENDENCY_CONTRACT       // portable, package/runtime input

// Task 2
std::optional<TextRuntimeLifetimeGuard> TextRuntimeLifetimeGuard::Create(
    const TextDependencyConfig&, TextDiagnosticSink&);

// Tasks 4, 5, 7
enum class FontArtifactStorage { ProjectLibrary, PackagedResource };
struct FontArtifactLocator {
    FontArtifactStorage storage;
    std::filesystem::path relativePath;
};
struct VerifiedFontArtifact {
    FontArtifactLocator locator;
    std::string sourceSha256;
    std::string artifactSha256;
    std::uint64_t byteSize;
};
class FontArtifactStore {
public:
    struct PackagedAuthority {
        std::filesystem::path relativePath;
        std::string artifactSha256;
        bool operator==(const PackagedAuthority&) const;
    };
    static FontArtifactStore ForProject(std::filesystem::path projectRoot);
    static std::optional<FontArtifactStore> ForSealedPackage(
        std::filesystem::path runtimeResourceRoot,
        std::vector<PackagedAuthority>, TextDiagnosticSink&);
    std::optional<std::shared_ptr<const std::vector<std::uint8_t>>>
    ReadVerified(const VerifiedFontArtifact&, TextDiagnosticSink&) const;
};
FontRepository;
FontFamilyResolver;
TextLayoutService;
std::vector<TextDiagnostic> AssetRecord::importDiagnostics;

// Task 15
MigrateUITextSchemaCommand; // optional author action, never run by builder
```

The current build target names are `molga_engine` and `molga_runtime`; `runtime_smoke` and `editor_smoke` are doctest executable/CTest names. This plan changes `molga_runtime` into the release bundle target and adds `molga_runtime_dev` for the explicit flat development path.

## File Responsibility Map

| File | Responsibility |
|---|---|
| `src/Core/PathService.*` | Bundle detection and explicit development resource root |
| `src/Core/PackageLayout.*` | Separate executable/resource-root validation and containment |
| `resources/Info.plist.in` | Canonical release bundle metadata |
| `src/Text/TextRuntimeManifest.*` | Portable dependency/font/license/coverage hash contract |
| `src/Assets/FontArtifactStore.*` | Project-library versus sealed-package immutable font reads |
| `src/Text/FontRepository.*` | Catalog-locator and manifest-authority-only packaged face loads |
| `src/Editor/GameBuilder.*` | Atomic `.app` staging using one resource root |
| `src/Core/PackageFinalizer.*` | Final tree validation and safe staged replacement |
| `src/Assets/LocalizationTableAsset.*` and importer | Versioned authored localization strings |
| `src/Text/TextCoverageManifest.*` | Canonical authored coverage result |
| `src/Text/TextPackageValidator.*` | Coverage scan, font closure, sealed startup validation, terminal policy |
| `src/Text/TextNoticeGenerator.*` | Deterministic verified package notice |
| `src/Core/SmokeReport.*` | Stable pre-window package failure evidence |
| `src/Core/RuntimeStartup.*` | Callback-injected pre-engine startup ordering and exit policy |
| `tests/smoke/*` | Copied-app launch, tamper, Mach-O/RPATH, and external-dependency checks |

## Exit Contract

This subplan exits only after Debug correctness/package tests and copied-app smoke pass, a code review over Tasks 9–17 has no blocker/high finding, `Contents/Resources` is the sole release root, every game-named app owns a validated per-game `Info.plist`, build provenance is absent from the package, generic asset staging excludes every `FontImporter` record and every license record named by those fonts, every packaged SFNT is read from a verified immutable project artifact and is owned exactly once by the sealed catalog/runtime manifest at `Assets/Fonts/<artifactSha256>.sfnt`, the runtime manifest hashes the canonical schema-3 asset catalog, coverage hashes every contributing staged source document, the committed deny-network/bounded-write profile rejects a loopback socket bind and out-of-root writes while its hidden SDL_GPU/Metal and visible Cocoa/TIS/CGWindow capability probes pass, each Section 11 action is asserted through the callback-injected startup runner, and every upstream-hashed required-file tamper plus the specified `game.json` canonical/schema/text-link alteration exits `4` before SDL. It does not claim detection of unrelated schema-valid `game.json` edits and does not satisfy parity, performance, GPU golden, or visible IME gates.

---

### Task 16.1: Separate bundle executable and resource roots

**Files:**

- Create: `tests/test_bundle_layout.cpp`
- Create: `tests/support/PathServiceTestAccess.h`
- Create: `tests/support/PathServiceTestAccess.cpp`
- Modify: `src/Core/PathService.h`
- Modify: `src/Core/PathService.cpp`
- Modify: `src/Core/PackageLayout.h`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_path_service.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: current executable discovery, safe-path helpers, and `PackageLayout::ExecutableNameFor`.
- Produces: `PathService::RuntimeResourceRoot`, explicit development root,
  `PackageRoots`, root-only bundle relationship validation, and a temporary
  legacy `PackageLayout::Validate` adapter consumed only until Task 16.2.

- [ ] **Step 1: Register the new focused test and capture status**

  Add:

  ```cmake
  molga_add_test(test_bundle_layout test_bundle_layout.cpp)
  ```

  Run:

  ```bash
  git status --short --branch
  cmake --preset debug
  ```

  Expected: configure succeeds; pre-existing changes are recorded.

- [ ] **Step 2: Write the failing canonical root tests**

  Create `tests/test_bundle_layout.cpp`:

  ```cpp
  #include "Core/PathService.h"
  #include "Core/PackageLayout.h"
  #include "SmokeTestSupport.h"
  #include "doctest.h"

  TEST_CASE("Contents MacOS executable resolves only Contents Resources") {
      test_support::TempDirectory temp{"bundle-root"};
      const auto app = temp.Path() / "Game.app";
      const auto executable = app / "Contents/MacOS/Game";
      const auto resources = app / "Contents/Resources";
      test_support::WriteText(executable, "binary");
      fs::create_directories(resources);
      std::string error;
      const auto result = PathService::DeriveRuntimeResourceRoot(
          executable, error);
      REQUIRE(result.has_value());
      CHECK(*result == fs::weakly_canonical(resources));
      CHECK(error.empty());
  }

  TEST_CASE("pseudo bundle and implicit flat release root fail") {
      test_support::TempDirectory temp{"pseudo-bundle"};
      const auto executable = temp.Path() / "Game.app/MacOS/Game";
      test_support::WriteText(executable, "binary");
      std::string error;
      CHECK_FALSE(PathService::DeriveRuntimeResourceRoot(
          executable, error).has_value());
      CHECK(error.find("Contents/MacOS") != std::string::npos);
  }

  TEST_CASE("development root must be explicit absolute and canonical") {
      test_support::TempDirectory temp{"dev-root"};
      auto service = molga::test_support::PathServiceTestAccess::Create(
          temp.Path() / "molga_runtime_dev");
      std::string error;
      CHECK_FALSE(service.SetDevelopmentRuntimeResourceRoot("relative", error));
      REQUIRE(service.SetDevelopmentRuntimeResourceRoot(temp.Path(), error));
      CHECK(service.RuntimeResourceRoot() == fs::weakly_canonical(temp.Path()));
      CHECK(service.AssetRoot() == service.RuntimeResourceRoot());
      CHECK(service.EngineResource("Engine/Text/icudt78l.dat") ==
            service.RuntimeResourceRoot() / "Engine/Text/icudt78l.dat");
  }

  TEST_CASE("packaged initialization publishes the derived root") {
      test_support::TempDirectory temp{"bundle-publish"};
      const auto executable = temp.Path() / "Game.app/Contents/MacOS/Game";
      fs::create_directories(temp.Path() / "Game.app/Contents/Resources");
      test_support::WriteText(executable, "binary");
      auto service = molga::test_support::PathServiceTestAccess::Create(
          executable);
      std::string error;
      REQUIRE(service.InitializePackagedRuntimeRoot(error));
      CHECK(service.ExecutableDir() == fs::weakly_canonical(
          executable).parent_path());
      CHECK(service.RuntimeResourceRoot() == fs::weakly_canonical(
          temp.Path() / "Game.app/Contents/Resources"));
      CHECK(service.AssetRoot() == service.RuntimeResourceRoot());
  }
  ```

  `PathServiceTestAccess::Create` is defined in a companion test TU and returns
  a value with the supplied discovered executable. Keep the production
  constructor private and retain `Get()` as the sole production instance.

- [ ] **Step 3: Add the failing root-only and full-layout separation tests**

  ```cpp
  TEST_CASE("bundle root relationship is valid before resource assembly") {
      test_support::TempDirectory temp{"bundle-relationship"};
      const auto app = temp.Path() / "Game.app";
      const PackageRoots roots{
          app / "Contents/MacOS/Game",
          app / "Contents/Resources"};
      fs::create_directories(roots.executablePath.parent_path());
      fs::create_directories(roots.resourceRoot);
      test_support::WriteText(roots.executablePath, "binary");
      std::string error;
      CHECK(PackageLayout::ValidateBundleRootRelationship(
          roots, "Game", error));
      CHECK_FALSE(PackageLayout::Validate(roots, "Game", error));
      CHECK(error.find("game.json") != std::string::npos);
  }

  TEST_CASE("PackageLayout rejects executable under Resources") {
      auto fixture = MakeMinimalBundleFixture();
      const PackageRoots wrong{
          fixture.resources / fixture.executableName,
          fixture.resources};
      std::string error;
      CHECK_FALSE(PackageLayout::Validate(
          wrong, fixture.executableName, error));
      CHECK(error.find("Contents/MacOS") != std::string::npos);
  }
  ```

  `MakeMinimalBundleFixture` is defined in the same test file and writes `Info.plist`, `Contents/MacOS/<name>`, `Contents/Resources/game.json`, asset catalog, scene, shader bundle, and placeholder using existing `ShaderPackageTestSupport`.

- [ ] **Step 4: Run the red root gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_bundle_layout test_path_service \
    test_game_builder -j
  ```

  Expected: compile fails on `RuntimeResourceRoot`,
  `DeriveRuntimeResourceRoot`, `SetDevelopmentRuntimeResourceRoot`,
  `PackageRoots`, and `ValidateBundleRootRelationship`.

- [ ] **Step 5: Add the exact `PathService` API**

  ```cpp
  class PathService {
  public:
      static PathService& Get();
      bool InitFromExecutable(const char* argv0, std::string& errorOut);
      static std::optional<std::filesystem::path> DeriveRuntimeResourceRoot(
          const std::filesystem::path& executablePath,
          std::string& errorOut);
      bool InitializePackagedRuntimeRoot(std::string& errorOut);
      bool SetDevelopmentRuntimeResourceRoot(
          const std::filesystem::path&, std::string& errorOut);
      const std::filesystem::path& ExecutableDir() const;
      const std::filesystem::path& RuntimeResourceRoot() const;
      std::filesystem::path EngineResource(const std::string& relative) const;
      // existing asset and safety helpers remain
  private:
      PathService() = default;
      static PathService CreateWithDiscoveredExecutableForTestSeam(
          const std::filesystem::path&);
      friend class molga::test_support::PathServiceTestAccess;
      std::filesystem::path executablePath_;
      std::filesystem::path executableDir_;
      std::filesystem::path runtimeResourceRoot_;
      std::filesystem::path assetRoot_;
  };
  ```

- [ ] **Step 6: Extend the test-seam companion library for `PathService`**

  Forward-declare only `molga::test_support::PathServiceTestAccess` in
  `PathService.h`. Define the private
  `CreateWithDiscoveredExecutableForTestSeam` in the always-compiled
  `PathService.cpp`, and define its sole public caller in
  `tests/support/PathServiceTestAccess.cpp`. Extend the Task 15.3 test-only
  library, rather than compiling `PathService.cpp` a second time:

  ```cmake
  target_sources(molga_test_seams PRIVATE
    support/PathServiceTestAccess.cpp)
  target_link_libraries(test_path_service PRIVATE molga_test_seams)
  target_link_libraries(test_bundle_layout PRIVATE molga_test_seams)
  ```

  Do not define `MOLGA_TESTING` on a test executable: `molga_core` owns the one
  `PathService.cpp` object, so macro-gated declarations would be unresolved.
  Add a focused link invocation to Step 11 and require both test executables to
  link without any undefined `CreateForTesting` symbol.

- [ ] **Step 7: Implement only canonical bundle derivation**

  `DeriveRuntimeResourceRoot` canonicalizes the executable and verifies exact parents:

  ```cpp
  const fs::path canonicalExe = fs::weakly_canonical(executablePath, ec);
  const fs::path macos = canonicalExe.parent_path();
  const fs::path contents = macos.parent_path();
  if (macos.filename() != "MacOS" || contents.filename() != "Contents" ||
      contents.parent_path().extension() != ".app") {
      errorOut = "release executable is not under <name>.app/Contents/MacOS";
      return std::nullopt;
  }
  const fs::path resources = contents / "Resources";
  if (!fs::is_directory(resources)) {
      errorOut = "bundle is missing Contents/Resources";
      return std::nullopt;
  }
  return fs::weakly_canonical(resources, ec);
  ```

- [ ] **Step 8: Publish the packaged derivation into the service**

  `InitFromExecutable` resolves the actual process executable (`_NSGetExecutablePath` on macOS; `argv0` is only the existing non-macOS fallback), stores canonical `executablePath_`/`executableDir_`, and returns `false` with error on failure. `InitializePackagedRuntimeRoot` calls the pure derive function, then atomically publishes both resource and asset roots:

  ```cpp
  const auto derived = DeriveRuntimeResourceRoot(executablePath_, errorOut);
  if (!derived) return false;
  auto nextRuntimeRoot = *derived;
  auto nextAssetRoot = *derived; // allocate/copy before publishing either field
  runtimeResourceRoot_.swap(nextRuntimeRoot); // noexcept path swap
  assetRoot_.swap(nextAssetRoot);
  return true;
  ```

- [ ] **Step 9: Implement only the explicit development setter**

  Reject relative, nonexistent, non-directory, and symlink-escaping roots. Do not call it from `InitFromExecutable`:

  ```cpp
  if (!root.is_absolute() || !fs::is_directory(root, ec)) {
      errorOut = "--development-resource-root must name an existing absolute directory";
      return false;
  }
  auto canonical = fs::weakly_canonical(root, ec);
  if (ec) { errorOut = ec.message(); return false; }
  auto nextRuntimeRoot = canonical;
  auto nextAssetRoot = canonical;
  runtimeResourceRoot_.swap(nextRuntimeRoot);
  assetRoot_.swap(nextAssetRoot);
  return true;
  ```

  `EngineResource(relative)` always joins against `RuntimeResourceRoot`, never `ExecutableDir`. After either runtime root initializer succeeds, `ResolveAsset` uses the same published root. Editor project setup may later override `AssetRoot` explicitly as it does today.

- [ ] **Step 10: Convert the editor and current flat runtime startup calls**

  In `src/main.cpp`, replace the old one-argument call with the explicit editor
  development sequence. This is not a fallback: the editor deliberately
  publishes its canonical executable directory because its current POST_BUILD
  copy places `assets`, `Editor`, and `ShaderBundle` there.

  ```cpp
  std::string pathError;
  auto& paths = PathService::Get();
  if (!paths.InitFromExecutable(argv[0], pathError) ||
      !paths.SetDevelopmentRuntimeResourceRoot(
          paths.ExecutableDir(), pathError)) {
      EmitStartupDiagnostic(TextDiagnostic{
          TextDiagnosticCode::PackageValidationFailed,
          TextSeverity::Error,
          "PathService", pathError,
          "launch the editor from a complete build output",
          "", 0, "", {}});
      return 4;
  }
  // EngineInit follows; no resource read precedes the two successful calls.
  ```

  Make the same two-argument conversion in the existing pre-split
  `src/runtime_main.cpp`, explicitly publishing `ExecutableDir()` as its
  temporary development root. Task 16.2 replaces that temporary flat-runtime
  sequence when it splits `molga_runtime` and `molga_runtime_dev`. Do not retain
  a one-argument `InitFromExecutable` overload.

- [ ] **Step 11: Test packaged/development publish behavior and compile callers**

  Run:

  ```bash
  cmake --build --preset debug --target test_bundle_layout test_path_service \
    molga_engine molga_runtime -j
  build/debug/tests/test_bundle_layout --test-case="*publishes*"
  build/debug/tests/test_bundle_layout --test-case="*development root*"
  ```

  Expected: package and development root tests pass; both real entry points
  compile against `bool InitFromExecutable(argv0,error)`; the editor explicitly
  publishes its executable directory and neither entry point reads a resource
  before publishing a runtime root.

- [ ] **Step 12: Define root-only validation and retain one temporary layout adapter**

  ```cpp
  struct PackageRoots {
      std::filesystem::path executablePath;
      std::filesystem::path resourceRoot;
  };

  class PackageLayout {
  public:
      static bool ValidateBundleRootRelationship(
          const PackageRoots&,
          const std::string& executableName,
          std::string& errorOut);
      static bool Validate(const PackageRoots&,
                           const std::string& executableName,
                           std::string& errorOut);
      // Temporary compatibility adapter; removed by Task 16.2.
      static bool Validate(const std::filesystem::path& legacyFlatRoot,
                           const std::string& executableName,
                           std::string& errorOut);
      static std::string ExecutableNameFor(const std::string& gameName);
  };
  ```

  `ValidateBundleRootRelationship` is pure root validation: canonicalize both
  paths, require executable filename equality, and require exact
  `.../<name>.app/Contents/MacOS/<exe>` with sibling `Contents/Resources`. It
  does not require `game.json`, catalog, scenes, shaders, scripts, or any other
  staged resource. `Validate(PackageRoots,...)` first calls that helper, then
  performs every existing complete-tree check relative to `resourceRoot`.

  Keep the old path overload in Task 16.1 as an explicitly temporary adapter
  with its existing flat-layout behavior so `GameBuilder.cpp`,
  `test_build_smoke.cpp`, `test_build_profile.cpp`, and
  `test_game_builder.cpp` still compile before the staging migration. It must
  be marked deprecated in a source comment, must not be called by new code,
  and is deleted together with all four old call sites in Task 16.2.

- [ ] **Step 13: Run the green root gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_bundle_layout test_path_service \
    test_game_builder molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_bundle_layout|test_path_service|test_game_builder)$' --output-on-failure
  ```

  Expected: all three tests pass; pseudo-bundles and implicit flat release
  roots fail through the new API; the unchanged legacy builder tests pass only
  through the documented temporary adapter; both startup entry points compile.

- [ ] **Step 14: Commit the root contract**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Core/PathService.h \
    src/Core/PathService.cpp src/Core/PackageLayout.h \
    src/Core/PackageLayout.cpp tests/test_bundle_layout.cpp \
    src/main.cpp src/runtime_main.cpp tests/test_path_service.cpp \
    tests/support/PathServiceTestAccess.h \
    tests/support/PathServiceTestAccess.cpp
  git commit -m "feat: separate macOS package resource roots"
  ```

### Task 16.2: Define the portable text manifest and canonical app tree

**Files:**

- Create: `resources/Info.plist.in`
- Create: `src/Text/TextRuntimeManifest.h`
- Create: `src/Text/TextRuntimeManifest.cpp`
- Create: `tests/smoke/check_runtime_target_resources.cmake`
- Modify: `src/Core/BuildProfile.h`
- Modify: `src/Core/BuildProfile.cpp`
- Modify: `src/Core/GameConfig.h`
- Modify: `src/Core/GameConfig.cpp`
- Modify: `src/Core/PackageLayout.h`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Editor/BuildManager.cpp`
- Modify: `src/Scripting/ScriptPackageLoader.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_bundle_layout.cpp`
- Modify: `tests/test_build_smoke.cpp`
- Modify: `tests/test_build_profile.cpp`
- Modify: `tests/test_build_manager.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_runtime_script_package_loader.cpp`
- Modify: `cmake/TextDependencies.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `PackageRoots`, `ValidateBundleRootRelationship`, the temporary
  flat-layout adapter, the build-barrier-verified portable dependency contract,
  current assets/scenes/scripts/shader bundle, and existing runtime source.
- Produces: schema-1 `TextRuntimeManifest`,
  `game.json.text.manifestSha256`, equally configured `molga_runtime` app and
  `molga_runtime_dev` targets, exact configured runtime-executable authority,
  and complete bundle staging beneath `Contents/Resources`; deletes the
  temporary flat-layout adapter.

- [ ] **Step 1: Write the failing manifest portability test**

  Add to `tests/test_bundle_layout.cpp`:

  ```cpp
  TEST_CASE("runtime manifest hashes only portable package inputs") {
      molga::text::TextRuntimeManifest manifest;
      manifest.dependencyLockSha256 = Digest('a');
      manifest.dependencies = {
          {"HarfBuzz", "14.3.1",
           "ab5ecbb83985034a76214ac0b2b833dcd590d774",
           {{"harfbuzz", Digest('b')}},
           "Licenses/HarfBuzz.txt", Digest('c')},
          {"ICU", "78.3",
           "21d1eb0f306e1141c10931e914dfc038c06121da",
           {{"icui18n", Digest('d')}, {"icuuc", Digest('e')}},
           "Licenses/ICU.txt", Digest('f')}
      };
      manifest.icuData = {"Engine/Text/icudt78l.dat", 33107232u,
          "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b"};
      manifest.rasterizerSha256 =
          "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528";
      manifest.rasterizerLicense = {"Licenses/StbTrueType.txt",
          "7587efcca32db8f95bf5860dea6dfa4be410ee723e1964db3689c8ddf2a2e4a9"};
      manifest.assetCatalogPath = "asset_catalog.json";
      manifest.assetCatalogSha256 = Digest('9');
      const auto json = manifest.SerializeCanonical();
      CHECK(json["dependencyLockSha256"] == Digest('a'));
      CHECK(json["assetCatalogPath"] == "asset_catalog.json");
      CHECK(json["assetCatalogSha256"] == Digest('9'));
      CheckJsonHasNoAbsolutePath(json);
      CHECK(json.dump().find("text_dependency_build_lock") == std::string::npos);
      molga::text::TextRuntimeManifest roundTrip;
      REQUIRE(molga::text::TextRuntimeManifest::Deserialize(json, roundTrip));
      CHECK(roundTrip.SerializeCanonical() == json);
  }
  ```

- [ ] **Step 2: Write the failing exact-tree test**

  ```cpp
  TEST_CASE("GameBuilder stages the canonical app resource tree") {
      const auto built = BuildMinimalTextGame();
      const auto app = built.output / "Fixture.app";
      for (const fs::path& relative : {
          "Contents/Info.plist", "Contents/MacOS/Fixture",
          "Contents/Resources/game.json",
          "Contents/Resources/asset_catalog.json",
          "Contents/Resources/Assets",
          "Contents/Resources/Scenes",
          "Contents/Resources/ShaderBundle",
          "Contents/Resources/Resources/missing_texture.png",
          "Contents/Resources/Engine/Text/icudt78l.dat",
          "Contents/Resources/Engine/Text/text_dependency_contract.json",
          "Contents/Resources/Manifests/text_runtime.json",
          "Contents/Resources/Licenses/ThirdPartyNotices.md",
          "Contents/Resources/Licenses/HarfBuzz.txt",
          "Contents/Resources/Licenses/ICU.txt",
          "Contents/Resources/Licenses/StbTrueType.txt"}) {
          INFO(relative.string());
          CHECK(fs::exists(app / relative));
      }
      CHECK_FALSE(fs::exists(app / "Contents/Resources/Engine/Text/"
                                  "text_dependency_build_lock.json"));
  }
  ```

  Before the Step 9 red build, register the provisional development target
  beside the current runtime target in top-level `CMakeLists.txt`:

  ```cmake
  add_executable(molga_runtime_dev src/runtime_main.cpp)
  target_link_libraries(molga_runtime_dev PRIVATE molga_core molga_warnings)
  add_dependencies(molga_runtime_dev molga_shader_bundle)
  set_target_properties(molga_runtime_dev PROPERTIES ENABLE_EXPORTS ON)
  target_compile_definitions(molga_runtime_dev PRIVATE
      MOLGA_DEVELOPMENT_RUNTIME=1)
  molga_attach_text_dependencies(molga_runtime_dev)
  molga_stage_text_runtime_resources(molga_runtime_dev)
  ```

  Step 17 replaces this provisional block with the shared final configuration;
  it does not leave or add a second `molga_runtime_dev` declaration. Do not yet
  register the target-owned path CTest: the current `molga_runtime` is not a
  bundle, so evaluating `$<TARGET_BUNDLE_CONTENT_DIR:molga_runtime>` here would
  make configure fail for the wrong reason. Step 17 owns that first generator
  expression after it converts the target to `MACOSX_BUNDLE`.

  `check_runtime_target_resources.cmake` requires the portable contract and ICU
  data at both
  `${BUNDLE_CONTENT_DIR}/Resources/Engine/Text/{text_dependency_contract.json,icudt78l.dat}`
  and
  `${DEV_TARGET_DIR}/Engine/Text/{text_dependency_contract.json,icudt78l.dat}`.
  It fails if either file exists below `${BUNDLE_CONTENT_DIR}/MacOS/Engine/Text`.

  ```cmake
  cmake_minimum_required(VERSION 3.25)
  foreach(required IN ITEMS
      "${BUNDLE_CONTENT_DIR}/Resources/Engine/Text/text_dependency_contract.json"
      "${BUNDLE_CONTENT_DIR}/Resources/Engine/Text/icudt78l.dat"
      "${DEV_TARGET_DIR}/Engine/Text/text_dependency_contract.json"
      "${DEV_TARGET_DIR}/Engine/Text/icudt78l.dat")
    if(NOT EXISTS "${required}")
      message(FATAL_ERROR "missing runtime text resource: ${required}")
    endif()
  endforeach()
  foreach(forbidden IN ITEMS
      "${BUNDLE_CONTENT_DIR}/MacOS/Engine/Text/text_dependency_contract.json"
      "${BUNDLE_CONTENT_DIR}/MacOS/Engine/Text/icudt78l.dat")
    if(EXISTS "${forbidden}")
      message(FATAL_ERROR "text resource staged under Contents/MacOS: ${forbidden}")
    endif()
  endforeach()
  ```

- [ ] **Step 3: Write the failing per-game Info.plist ownership test**

  Add to `tests/test_game_builder.cpp`:

  ```cpp
  TEST_CASE("GameBuilder writes the staged game plist from final roots") {
      BuildSettings settings = MinimalBuildSettings("Fixture & Friends");
      settings.profile.bundleIdentifier = "com.molga.fixture-friends";
      settings.profile.bundleVersion = "7";
      settings.profile.minimumMacOSVersion = "13.0";
      PackageRoots roots = MakeStageRoots("Fixture-Friends");
      const auto contents = roots.resourceRoot.parent_path();
      std::string error;
      REQUIRE(molga::WriteInfoPlist(settings, roots, error));

      const auto plist = ReadText(contents / "Info.plist");
      CHECK(PlistString(plist, "CFBundleExecutable") ==
            roots.executablePath.filename().string());
      CHECK(PlistString(plist, "CFBundleDisplayName") ==
            "Fixture & Friends");
      CHECK(PlistString(plist, "CFBundleIdentifier") ==
            "com.molga.fixture-friends");
      CHECK(PlistString(plist, "CFBundleShortVersionString") == "0.1.0");
      CHECK(PlistString(plist, "CFBundleVersion") == "7");
      CHECK(PlistString(plist, "LSMinimumSystemVersion") == "13.0");
      CHECK(plist.find("Fixture &amp; Friends") != std::string::npos);
  }
  ```

  `PlistString` uses the test-only platform plist parser, not substring matching,
  for semantic assertions; the final check separately proves XML escaping.

  Add focused persistence cases to `tests/test_build_profile.cpp`:

  ```cpp
  TEST_CASE("existing build profile schema two owns bundle metadata") {
      BuildProfile profile;
      profile.gameName = "Fixture";
      profile.bundleIdentifier = "com.molga.fixture";
      profile.bundleVersion = "7";
      profile.minimumMacOSVersion = "13.0";
      const auto json = profile.Serialize();
      CHECK(json["schemaVersion"] == 2);
      BuildProfile loaded;
      REQUIRE(loaded.Deserialize(json));
      CHECK(loaded.bundleIdentifier == profile.bundleIdentifier);
      CHECK(loaded.bundleVersion == "7");
      CHECK(loaded.minimumMacOSVersion == "13.0");
  }

  TEST_CASE("schema one profile derives safe package metadata in memory") {
      const auto legacy = MakeSchemaOneBuildProfile("My Game!");
      BuildProfile loaded;
      REQUIRE(loaded.Deserialize(legacy));
      CHECK(loaded.bundleIdentifier == "com.molga.my-game");
      CHECK(loaded.bundleVersion == loaded.productVersion);
      CHECK(loaded.minimumMacOSVersion ==
            BuildProfile::EngineMinimumMacOSVersion);
      CHECK(ReadFixtureBytes(legacy.path) == legacy.originalBytes);
  }

  TEST_CASE("older schema two derives missing bundle fields deterministically") {
      auto json = BuildProfile::Defaults("Fixture Game").Serialize();
      json.erase("bundleIdentifier");
      json.erase("bundleVersion");
      json.erase("minimumMacOSVersion");
      const auto original = json.dump();
      BuildProfile loaded;
      REQUIRE(loaded.Deserialize(json));
      CHECK(loaded.schemaVersion == 2);
      CHECK(loaded.bundleIdentifier == "com.molga.fixture-game");
      CHECK(loaded.bundleVersion == loaded.productVersion);
      CHECK(loaded.minimumMacOSVersion ==
            BuildProfile::EngineMinimumMacOSVersion);
      const auto canonical = loaded.Serialize();
      CHECK(canonical["schemaVersion"] == 2);
      CHECK(canonical.contains("bundleIdentifier"));
      CHECK(canonical.contains("bundleVersion"));
      CHECK(canonical.contains("minimumMacOSVersion"));
      CHECK(json.dump() == original); // Deserialize never rewrites its input
  }
  ```

  Loading schema 1 or an older schema-2 document missing the additive fields
  derives values in memory but never rewrites its source file; an explicit
  profile save emits the same current schema 2 with all three fields.

  Extend `BuildManager direct build loads project profile before saving UI
  fields` in `tests/test_build_manager.cpp` by setting
  `bundleIdentifier="com.molga.configured"`, `bundleVersion="7"`, and
  `minimumMacOSVersion="13.0"` before `SaveBuildProfile`, then checking the
  same three values after `manager.Build(...)`. This fails if the manager's UI
  buffer/profile copy drops the authored plist metadata.

- [ ] **Step 4: Write the failing plist injection/path test**

  ```cpp
  TEST_CASE("GameBuilder rejects names that can inject plist or escape app paths") {
      BuildSettings settings = MinimalBuildSettings(
          "../Bad</string><key>Injected</key><string>");
      settings.profile.bundleIdentifier = "com.molga.bad</string>";
      PackageRoots roots = MakeStageRoots("Fixture");
      const auto appRoot = roots.resourceRoot.parent_path().parent_path();
      const auto sentinel = appRoot.parent_path() / "Injected.app";
      std::string error;
      CHECK_FALSE(molga::WriteInfoPlist(settings, roots, error));
      CHECK(error == "PACKAGE_PLIST_INVALID: gameName");
      CHECK_FALSE(std::filesystem::exists(
          roots.resourceRoot.parent_path() / "Info.plist"));
      CHECK_FALSE(std::filesystem::exists(sentinel));
  }
  ```

- [ ] **Step 5: Add the failing plist scalar-validation table**

  ```cpp
  TEST_CASE("GameBuilder rejects every unsafe plist scalar before write") {
      const auto reject = [](BuildSettings settings, PackageRoots roots,
                             const char* field) {
          std::string error;
          CHECK_FALSE(molga::WriteInfoPlist(settings, roots, error));
          CHECK(error == std::string("PACKAGE_PLIST_INVALID: ") + field);
          CHECK_FALSE(fs::exists(
              roots.resourceRoot.parent_path() / "Info.plist"));
      };
      SUBCASE("control byte") {
          auto s = MinimalBuildSettings("Fixture");
          s.profile.gameName = std::string("bad\0name", 8);
          reject(s, MakeStageRoots("Fixture"), "gameName");
      }
      SUBCASE("backslash") {
          reject(MinimalBuildSettings("bad\\name"),
                 MakeStageRoots("Fixture"), "gameName");
      }
      SUBCASE("slash") {
          reject(MinimalBuildSettings("bad/name"),
                 MakeStageRoots("Fixture"), "gameName");
      }
      SUBCASE("single dot segment") {
          reject(MinimalBuildSettings("."),
                 MakeStageRoots("Fixture"), "gameName");
      }
      SUBCASE("dot segment") {
          reject(MinimalBuildSettings(".."),
                 MakeStageRoots("Fixture"), "gameName");
      }
      SUBCASE("bundle identifier") {
          auto s = MinimalBuildSettings("Fixture");
          s.profile.bundleIdentifier = "com.molga.</string>";
          reject(s, MakeStageRoots("Fixture"), "bundleIdentifier");
      }
      SUBCASE("bundle version") {
          auto s = MinimalBuildSettings("Fixture");
          s.profile.bundleVersion = "7-beta";
          reject(s, MakeStageRoots("Fixture"), "bundleVersion");
      }
      SUBCASE("minimum OS") {
          auto s = MinimalBuildSettings("Fixture");
          s.profile.minimumMacOSVersion = "../13.0";
          reject(s, MakeStageRoots("Fixture"), "minimumMacOSVersion");
      }
      SUBCASE("executable is not the Contents MacOS basename") {
          auto roots = MakeStageRoots("Fixture");
          roots.executablePath = roots.resourceRoot / "Fixture";
          reject(MinimalBuildSettings("Fixture"), roots, "executablePath");
      }
      SUBCASE("executable basename is empty") {
          auto roots = MakeStageRoots("Fixture");
          roots.executablePath.clear();
          reject(MinimalBuildSettings("Fixture"), roots, "executablePath");
      }
  }
  ```

  In the same test file add two product-validation failures through the
  internal writer seam (the public `WriteInfoPlist` always supplies the source
  template and real validator):

  ```cpp
  TEST_CASE("GameBuilder validates rendered plist before atomic publish") {
      const auto roots = MakeStageRoots("Fixture");
      const auto malformed = WriteTextFixture(
          "malformed.plist.in",
          "<plist><dict><key>CFBundleExecutable</key>"
          "<string>@MOLGA_EXECUTABLE@</string>");
      std::string error;
      CHECK_FALSE(molga::WriteInfoPlistForTesting(
          MinimalBuildSettings("Fixture"), roots, malformed,
          molga::ProcessPlistProductValidator(), error));
      CHECK(error.find("PACKAGE_PLIST_LINT_FAILED") == 0);
      CHECK_FALSE(fs::exists(
          roots.resourceRoot.parent_path() / "Info.plist"));
      CHECK(FindSiblingTemps(
          roots.resourceRoot.parent_path() / "Info.plist").empty());
  }

  TEST_CASE("GameBuilder rolls back when plist validator cannot run") {
      const auto roots = MakeStageRoots("Fixture");
      std::string error;
      CHECK_FALSE(molga::WriteInfoPlistForTesting(
          MinimalBuildSettings("Fixture"), roots,
          "resources/Info.plist.in",
          AlwaysFailPlistValidator{"validator launch failed"}, error));
      CHECK(error ==
            "PACKAGE_PLIST_LINT_FAILED: validator launch failed");
      CHECK_FALSE(fs::exists(
          roots.resourceRoot.parent_path() / "Info.plist"));
      CHECK(FindSiblingTemps(
          roots.resourceRoot.parent_path() / "Info.plist").empty());
  }
  ```

- [ ] **Step 6: Add the failing configured-runtime payload test**

  Add to `tests/test_game_builder.cpp`; CMake defines
  `MOLGA_TEST_RUNTIME_TARGET_FILE="$<TARGET_FILE:molga_runtime>"` for this
  target and makes it depend on `molga_runtime`:

  ```cpp
  TEST_CASE("GameBuilder copies the configured bundle target payload") {
      const fs::path configured = MOLGA_TEST_RUNTIME_TARGET_FILE;
      REQUIRE(fs::is_regular_file(configured));
      test_support::TempDirectory temp{"runtime-source"};
      const auto stale = temp.Path() / "editor-bin/molga_runtime";
      test_support::WriteText(stale, "stale-adjacent-flat-binary");
      const auto roots = MakeStageRoots("Fixture");
      auto artifacts = molga::ConfiguredRuntimeBuildArtifacts();
      REQUIRE(fs::weakly_canonical(artifacts.runtimeExecutable) ==
              fs::weakly_canonical(configured));
      std::string error;
      REQUIRE(molga::CopyConfiguredRuntimeExecutable(
          artifacts, roots, error));
      CHECK(molga::Sha256File(roots.executablePath) ==
            molga::Sha256File(configured));
      CHECK(ReadBytes(roots.executablePath) == ReadBytes(configured));
      CHECK(ReadBytes(roots.executablePath) != ReadBytes(stale));
      CHECK(HasOwnerExecuteBit(roots.executablePath));

      artifacts.runtimeExecutable = stale;
      CHECK_FALSE(molga::CopyConfiguredRuntimeExecutable(
          artifacts, roots, error));
      CHECK(error == "PACKAGE_RUNTIME_SOURCE_MISMATCH");
  }
  ```

  The test-only macro and the production compiled constant are both emitted
  from the same `$<TARGET_FILE:molga_runtime>` generator expression. The copy
  helper compares its input to the production constant before touching the
  destination, so a stale adjacent flat binary is never an accepted source.

- [ ] **Step 7: Add the failing enabled-script resource-root regression**

  Add to `tests/test_game_builder.cpp`:

  ```cpp
  TEST_CASE("enabled script and game config are staged under Resources") {
      const auto built = BuildMinimalTextGameWithEnabledScript();
      const auto& roots = built.roots;
      const auto config = LoadGameConfigOrFail(
          roots.resourceRoot / "game.json");
      REQUIRE(config.scripts.enabled);
      CHECK(config.scripts.library == "Scripts/libfixture_game.dylib");
      CHECK(fs::exists(roots.resourceRoot / config.scripts.library));
      CHECK_FALSE(fs::exists(
          roots.executablePath.parent_path() / config.scripts.library));
      std::string error;
      CHECK(PackageLayout::Validate(
          roots, roots.executablePath.filename().string(), error));
  }
  ```

  Add a pure resolution case to
  `tests/test_runtime_script_package_loader.cpp`:

  ```cpp
  TEST_CASE("relative script libraries resolve beneath runtime resources") {
      const fs::path root = "/sealed/Game.app/Contents/Resources";
      std::string error;
      const auto path = ScriptPackageLoader::ResolveLibraryPath(
          "Scripts/libfixture_game.dylib", root, error);
      REQUIRE(path.has_value());
      CHECK(*path == root / "Scripts/libfixture_game.dylib");
      CHECK_FALSE(ScriptPackageLoader::ResolveLibraryPath(
          "../MacOS/libfixture_game.dylib", root, error).has_value());
  }
  ```

- [ ] **Step 8: Add the failing runtime-resource read-plan test**

  Add to `tests/test_bundle_layout.cpp`:

  ```cpp
  TEST_CASE("runtime package reads derive from one resource root") {
      const fs::path root = "/Game.app/Contents/Resources";
      const auto paths = PackageLayout::RuntimeResourcePaths(root);
      CHECK(paths.gameConfig == root / "game.json");
      CHECK(paths.assetCatalog == root / "asset_catalog.json");
      CHECK(paths.assets == root / "Assets");
      CHECK(paths.scenes == root / "Scenes");
      CHECK(paths.shaderBundle == root / "ShaderBundle");
      CHECK(paths.placeholder == root / "Resources/missing_texture.png");
      CHECK(paths.scripts == root / "Scripts");
      for (const auto& path : paths.All()) {
          CHECK(IsCanonicalDescendant(root, path));
          CHECK(path.string().find("Contents/MacOS") == std::string::npos);
      }
  }
  ```

- [ ] **Step 9: Run the red manifest/tree/plist/root gate**

  Run:

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_bundle_layout test_game_builder \
    test_build_manager test_runtime_script_package_loader \
    molga_runtime molga_runtime_dev -j
  ```

  Expected: compilation fails on `TextRuntimeManifest`, `WriteInfoPlist`,
  `CopyConfiguredRuntimeExecutable`,
  `RuntimeResourcePaths`, and `ResolveLibraryPath`; after adding only those
  declarations, tree/script assertions fail against flat staging and no
  game-specific plist is written. Configure itself succeeds and
  `molga_runtime_dev` is already a known target; an unknown target or a
  bundle-only generator-expression failure is not an accepted red result.

- [ ] **Step 10: Define the manifest data model**

  Add:

  ```cpp
  namespace molga::text {
  struct TextRuntimeArchiveEntry {
      std::string logicalName;
      std::string sha256;
  };
  struct TextRuntimeDependencyEntry {
      std::string name;
      std::string version;
      std::string sourceCommit;
      std::vector<TextRuntimeArchiveEntry> staticArchives;
      std::string licensePath;
      std::string licenseSha256;
  };
  struct TextRuntimeFileEntry {
      std::string path;
      std::uint64_t size = 0;
      std::string sha256;
  };
  struct TextRuntimeLicenseEntry {
      std::string path;
      std::string sha256;
  };
  struct TextRuntimeFontEntry {
      std::string fontGuid;
      // In a sealed package this is the PackagedResource artifact locator,
      // exactly Assets/Fonts/<sourceSha256>.sfnt; it is not the authored path.
      std::string sourcePath;
      // Task 4.1 guarantees sourceSha256 == artifactSha256 for copied bytes.
      std::string sourceSha256;
      std::uint32_t faceIndex = 0;
      std::string licenseAssetGuid;
      std::string licensePath;
      std::string licenseSha256;
  };
  struct TextRuntimeManifest {
      static constexpr int CurrentSchemaVersion = 1;
      int schemaVersion = CurrentSchemaVersion;
      // Despite the retained field name, this is the portable contract SHA.
      std::string dependencyLockSha256;
      std::vector<TextRuntimeDependencyEntry> dependencies;
      TextRuntimeFileEntry icuData;
      // Logical digest of the statically compiled text-owned header. The
      // header itself is not a package file.
      std::string rasterizerSha256;
      // Unlike the header, this verified legal file is packaged and hashed.
      TextRuntimeLicenseEntry rasterizerLicense;
      std::string assetCatalogPath = "asset_catalog.json";
      std::string assetCatalogSha256;
      std::vector<TextRuntimeFontEntry> fonts;
      std::string coverageManifestPath;
      std::string coverageManifestSha256;
      std::string thirdPartyNoticePath;
      std::string thirdPartyNoticeSha256;
      nlohmann::ordered_json SerializeCanonical() const;
      static bool Deserialize(const nlohmann::json&, TextRuntimeManifest&,
                              std::string* errorOut = nullptr);
  };
  }
  ```

  This schema intentionally retains the approved public names `sourcePath`
  and `sourceSha256`. Task 17.3 fills them from the verified immutable import
  artifact and packaged locator. They never authorize reading the authored
  project source in a sealed runtime.

- [ ] **Step 11: Validate dependency archive vectors**

  Require dependencies sorted by name, archive entries sorted by logical name, exactly one HarfBuzz archive named `harfbuzz`, and exactly two ICU archives named `icui18n` and `icuuc`. Every archive digest must match the portable contract and build-lock cross-check made by GameBuilder.

  ```cpp
  CHECK_ARCHIVES("HarfBuzz", {{"harfbuzz", hbSha}});
  CHECK_ARCHIVES("ICU", {{"icui18n", i18nSha}, {"icuuc", ucSha}});
  ```

- [ ] **Step 12: Validate canonical manifest JSON paths and digests**

  Serialize object keys in the declared order; sort dependencies by `name` and fonts by `{fontGuid,sourceSha256,faceIndex}`. Reject schema mismatch, non-SHA fields, absolute paths, `..`, backslashes, duplicated font identities, and any path containing `text_dependency_build_lock.json`. Require `rasterizerSha256` to equal the portable contract logical digest; do not assign the header a runtime path because it is statically compiled and absent from the bundle. Require `rasterizerLicense.path == Licenses/StbTrueType.txt` with the portable contract's exact SHA because that legal file is present. Round-trip through `Deserialize` and compare canonical bytes before staging.

  Require `assetCatalogPath == "asset_catalog.json"` and a lowercase
  `assetCatalogSha256`; this is the only runtime-manifest edge to catalog bytes.
  The catalog never hashes the runtime manifest, so this field preserves the
  acyclic catalog → runtime direction.

  ```cpp
  if (!IsSha256(dependencyLockSha256)) return Fail("dependencyLockSha256");
  if (!IsSafePackageRelative(entry.path)) return Fail("unsafe manifest path");
  if (entry.path.find("text_dependency_build_lock.json") != std::string::npos)
      return Fail("build provenance cannot be packaged");
  ```

- [ ] **Step 13: Add a non-cyclic game config link**

  Extend `GameConfig` with:

  ```cpp
  struct TextManifestReference {
      std::string path = "Manifests/text_runtime.json";
      std::string manifestSha256;
  };
  std::optional<TextManifestReference> text;
  ```

  `text_runtime.json` never hashes itself or `game.json`; GameBuilder writes the final runtime manifest, computes its SHA, then writes `game.json.text.manifestSha256` last. Existing schema-4 configs without `text` remain valid development inputs; sealed package validation requires it.

- [ ] **Step 14: Define the exact per-game plist inputs**

  Keep the repository's current `BuildProfile::CurrentSchemaVersion == 2` and
  add `bundleIdentifier`, `bundleVersion`, and `minimumMacOSVersion` as a
  backward-compatible schema-2 extension beside existing `productVersion` and
  `gameName`; Task 17 alone advances to schema 3 for
  `requiredTextFixtures`. `BuildSettings` continues to own the complete copied
  `BuildProfile`; it must not introduce shadow transient fields. For schema 1
  and for an older schema-2 document missing any additive field, derive in memory
  `bundleIdentifier = "com.molga." + SlugAsciiLower(gameName)` (collapse each
  non-alphanumeric run to one `-`, trim `-`, and use `game` if empty),
  `bundleVersion=productVersion`, and
  `minimumMacOSVersion=BuildProfile::EngineMinimumMacOSVersion`, whose pinned
  value is `"11.0"` and is asserted equal to the engine-shell CMake minimum.
  Schema-1 loading still performs its existing output-scale migration before
  applying the same bundle defaults. Missing fields derive independently;
  present authored fields are never replaced. Loading is read-only; only
  explicit profile save writes complete schema 2 fields.

  ```cpp
  struct BuildProfile {
      static constexpr int CurrentSchemaVersion = 2;
      static constexpr std::string_view EngineMinimumMacOSVersion = "11.0";
      // existing fields plus the three persisted bundle fields
  };
  ```

  Add three labeled controls to `BuildManager.cpp` bound directly to the
  profile fields. Disable Build and display the exact validation message when
  an authored value is invalid. The Project build-profile serializer,
  round-trip tests, and BuildManager's copied `BuildSettings` must preserve all
  three fields. `profile.productVersion` is the plist short version and
  `profile.gameName` is the display/name value. `resources/Info.plist.in`
  contains exactly `CFBundleExecutable`, `CFBundleIdentifier`,
  `CFBundleDisplayName`, `CFBundleName`, `CFBundleShortVersionString`,
  `CFBundleVersion`, `CFBundlePackageType=APPL`, and
  `LSMinimumSystemVersion` placeholders. Bundle IDs are lower-case reverse-DNS;
  versions are one to three dot-separated decimal components. Reject empty,
  control-bearing, slash-bearing, dot-segment, or XML-markup-bearing game names
  before any path or file is created. Ordinary display characters such as `&`
  are allowed and XML-escaped.

  ```xml
  <?xml version="1.0" encoding="UTF-8"?>
  <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
  <plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>@MOLGA_EXECUTABLE@</string>
    <key>CFBundleIdentifier</key><string>@MOLGA_BUNDLE_IDENTIFIER@</string>
    <key>CFBundleDisplayName</key><string>@MOLGA_DISPLAY_NAME@</string>
    <key>CFBundleName</key><string>@MOLGA_BUNDLE_NAME@</string>
    <key>CFBundleShortVersionString</key><string>@MOLGA_SHORT_VERSION@</string>
    <key>CFBundleVersion</key><string>@MOLGA_BUNDLE_VERSION@</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>LSMinimumSystemVersion</key><string>@MOLGA_MINIMUM_OS@</string>
  </dict></plist>
  ```

- [ ] **Step 15: Render and atomically publish the staged plist**

  Add the exact helper to `GameBuilder.h`:

  ```cpp
  namespace molga {
  class PlistProductValidator {
  public:
      virtual ~PlistProductValidator() = default;
      virtual bool Validate(const std::filesystem::path&,
                            std::string& errorOut) const = 0;
  };
  class ProcessPlistProductValidator final : public PlistProductValidator {
  public:
      bool Validate(const std::filesystem::path&,
                    std::string& errorOut) const override;
  };
  bool WriteInfoPlist(const BuildSettings& settings,
                      const PackageRoots& roots,
                      std::string& errorOut);
  }
  ```

  First call `PackageLayout::ValidateBundleRootRelationship(
  roots, roots.executablePath.filename().string(), errorOut)`. Do not call the
  complete-tree `PackageLayout::Validate` here: `game.json`, catalog, scenes,
  shaders, and scripts do not exist yet. Require
  `roots.executablePath.parent_path() ==
  roots.resourceRoot.parent_path() / "MacOS"` and
  derive `CFBundleExecutable` only from `roots.executablePath.filename()`.
  Validate every input, XML-escape each substituted value, require every known
  template token exactly once and no token left over, then write a sibling
  temporary file and flush/close it. Before rename,
  `ProcessPlistProductValidator` invokes the existing bounded process runner
  with the argument vector
  `{ "/usr/bin/plutil", "-lint", "--", tempPath.string() }`, a 10-second
  timeout, no shell, and captured stdout/stderr. A launch error, timeout,
  signal, or nonzero exit returns `PACKAGE_PLIST_LINT_FAILED` with bounded
  output. Only a lint-successful temp is renamed atomically to
  `roots.resourceRoot.parent_path()/Info.plist`. On any validation, write, or rename
  failure, remove only the command-owned temporary file and leave no final
  plist. GameBuilder calls this helper after `CreateBundleRoots` and
  `CopyExecutable`, before any resource finalization.

- [ ] **Step 16: Make engine plist configuration deterministic**

  Configure `resources/Info.plist.in` for the `molga_runtime` target's own
  build-shell bundle explicitly. Pin the project version and repair only an
  absent or empty deployment-target cache entry; preserve a caller's nonempty
  explicit value:

  ```cmake
  project(molga_engine VERSION 0.1.0 LANGUAGES C CXX)
  if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
                CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Minimum macOS version for engine build artifacts" FORCE)
  endif()
  if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
    message(FATAL_ERROR "CMAKE_OSX_DEPLOYMENT_TARGET must be nonempty")
  endif()
  set(MOLGA_EXECUTABLE "molga_runtime")
  set(MOLGA_BUNDLE_IDENTIFIER "com.molga.runtime")
  set(MOLGA_DISPLAY_NAME "Molga Runtime")
  set(MOLGA_BUNDLE_NAME "molga_runtime")
  set(MOLGA_SHORT_VERSION "${PROJECT_VERSION}")
  set(MOLGA_BUNDLE_VERSION "${PROJECT_VERSION}")
  set(MOLGA_MINIMUM_OS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  configure_file(resources/Info.plist.in
      "${CMAKE_CURRENT_BINARY_DIR}/molga_runtime.Info.plist" @ONLY)
  ```

  Run one clean configure with
  `-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=` and assert the resulting cache and
  parsed generated plist both contain `11.0`; assert both engine version keys
  parse as `0.1.0`. Also configure with `13.0` and assert it remains `13.0`.
  The engine shell plist's default `11.0` is separate from a per-game
  `BuildSettings.minimumMacOSVersion` such as `13.0`. A game-named staged
  `.app` always gets a newly rendered `Contents/Info.plist` from
  `WriteInfoPlist`; GameBuilder never copies the engine target plist.

- [ ] **Step 17: Factor and apply one shared runtime-target configuration**

  Move every existing runtime link, warning/options, export, dependency, and
  resource-copy rule into one helper, replace the original runtime declaration
  and Step 2's provisional development block, then compile the same source
  twice. The resulting CMake contains exactly these two declarations once:

  ```cmake
  function(molga_configure_runtime_target target runtime_resource_root)
    cmake_parse_arguments(ARG "BUNDLE" "" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR
        "unknown runtime target arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    target_link_libraries(${target} PRIVATE molga_core molga_warnings)
    molga_attach_text_dependencies(${target})
    add_dependencies(${target} molga_shader_bundle)
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${runtime_resource_root}/Resources"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/src/Editor/missing_texture.png"
              "${runtime_resource_root}/Resources/missing_texture.png"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/ShaderBundle"
              "${runtime_resource_root}/ShaderBundle"
      VERBATIM)
  endfunction()

  add_executable(molga_runtime MACOSX_BUNDLE src/runtime_main.cpp)
  molga_configure_runtime_target(molga_runtime
      "$<TARGET_BUNDLE_CONTENT_DIR:molga_runtime>/Resources" BUNDLE)
  set_target_properties(molga_runtime PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_BINARY_DIR}/molga_runtime.Info.plist")

  add_executable(molga_runtime_dev src/runtime_main.cpp)
  molga_configure_runtime_target(molga_runtime_dev
      "$<TARGET_FILE_DIR:molga_runtime_dev>")
  target_compile_definitions(molga_runtime_dev PRIVATE
      MOLGA_DEVELOPMENT_RUNTIME=1)
  ```

  Only now, after `molga_runtime` is a real bundle and `molga_runtime_dev` is a
  real executable, register the target-owned path test in
  `tests/CMakeLists.txt`:

  ```cmake
  if(APPLE)
    add_test(NAME test_runtime_target_resource_roots
      COMMAND ${CMAKE_COMMAND}
        -DBUNDLE_CONTENT_DIR=$<TARGET_BUNDLE_CONTENT_DIR:molga_runtime>
        -DDEV_TARGET_DIR=$<TARGET_FILE_DIR:molga_runtime_dev>
        -P ${CMAKE_SOURCE_DIR}/tests/smoke/check_runtime_target_resources.cmake)
  endif()
  ```

  Preserve any platform-specific runtime link options and compile definitions
  in this helper as they exist when the task executes; neither target may own a
  private copy. Add a CMake property test that compares both targets'
  `LINK_LIBRARIES`, dependencies, `ENABLE_EXPORTS`, warnings/options, and
  post-build resource set, permitting only bundle/plist properties and
  `MOLGA_DEVELOPMENT_RUNTIME` to differ. Build and link both targets before
  continuing.

- [ ] **Step 18: Extend and invoke the text-resource stager at each exact root**

  Extend Task 1's existing helper source-compatibly; the one-argument form
  retains its current flat/development destination:

  ```cmake
  function(molga_stage_text_runtime_resources target_name)
    cmake_parse_arguments(ARG "" "DESTINATION_ROOT" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR
        "unknown text runtime staging arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET "${target_name}")
      message(FATAL_ERROR
        "text runtime resource target does not exist: ${target_name}")
    endif()
    if(ARG_DESTINATION_ROOT)
      set(text_destination_root "${ARG_DESTINATION_ROOT}")
    else()
      set(text_destination_root "$<TARGET_FILE_DIR:${target_name}>/Engine/Text")
    endif()
    set(stage_target "${target_name}_text_runtime_resources")
    if(TARGET "${stage_target}")
      message(FATAL_ERROR
        "text runtime resources already attached: ${target_name}")
    endif()
    add_custom_target("${stage_target}"
      COMMAND "${CMAKE_COMMAND}"
        "-DCONTRACT_SOURCE=${MOLGA_TEXT_DEPENDENCY_CONTRACT}"
        "-DICU_DATA_SOURCE=${CMAKE_SOURCE_DIR}/resources/text/icudt78l.dat"
        "-DDESTINATION_ROOT=${text_destination_root}"
        -P "${CMAKE_SOURCE_DIR}/cmake/StageTextRuntimeResources.cmake"
      DEPENDS molga_text_dependencies_ready
              "${CMAKE_SOURCE_DIR}/resources/text/icudt78l.dat"
      VERBATIM)
    add_dependencies("${target_name}" "${stage_target}")
  endfunction()
  ```

  At the end of `molga_configure_runtime_target`, route only the bundle through
  the override and preserve the development call shape:

  ```cmake
  if(ARG_BUNDLE)
    molga_stage_text_runtime_resources(${target}
      DESTINATION_ROOT "${runtime_resource_root}/Engine/Text")
  else()
    molga_stage_text_runtime_resources(${target})
  endif()
  ```

  Delete Task 1's now-redundant direct
  `molga_stage_text_runtime_resources(molga_runtime)` call before invoking the
  shared target helper, or the helper's duplicate-target guard must fail. Keep
  the editor's existing `molga_stage_text_runtime_resources(molga_engine)`
  attachment unchanged; the new helper creates the first and only attachment
  for each release/development runtime target.

  Build both targets, then run
  `ctest --test-dir build/debug -R '^test_runtime_target_resource_roots$'
  --output-on-failure`. Expected: both target roots contain the portable
  contract and ICU data, and the bundle has no
  `Contents/MacOS/Engine/Text` directory.

- [ ] **Step 19: Bind GameBuilder to the actual CMake runtime target**

  Define this function after both runtime targets and invoke it for every
  target that compiles `src/Editor/GameBuilder.cpp`, including
  `molga_engine`, `test_game_builder`, and `test_build_manager`:

  ```cmake
  function(molga_bind_game_builder_runtime target)
    target_compile_definitions(${target} PRIVATE
      MOLGA_CONFIGURED_RUNTIME_TARGET_FILE="$<TARGET_FILE:molga_runtime>")
    add_dependencies(${target} molga_runtime)
  endfunction()
  ```

  `ConfiguredRuntimeBuildArtifacts()` returns the canonical path from that
  definition. Do not derive it from `PathService::ExecutableDir()`. CMake's
  target dependency guarantees the bundle payload exists before the editor or
  a builder test runs. The path is build-only authority and is never emitted
  into `Contents/Resources` or a portable manifest.

- [ ] **Step 20: Implement the split root branch and reserve final startup order**

  This task implements CLI parsing, executable/root publication, the existing
  Task 2 guard, and engine entry for both new targets. It must not reference or
  stub `TextPackageValidator`, which is first created in Task 17.2 and is not a
  complete startup authority until Task 17.5. Keep the packaged post-root body
  in one `RunRuntimeAfterPaths` seam so Task 17.5 can replace that call with its
  callback-injected runner without duplicating startup logic.

  Freeze this exact final order as the explicit Task 17.5 handoff:

  ```text
  molga_runtime:
    parse CLI -> InitFromExecutable -> InitializePackagedRuntimeRoot
    -> ValidateSealedPackage -> TextRuntimeLifetimeGuard::Create
    -> EngineInit -> destroy every engine/text owner -> destroy runtime guard

  molga_runtime_dev:
    parse and require --development-resource-root -> InitFromExecutable
    -> SetDevelopmentRuntimeResourceRoot -> TextRuntimeLifetimeGuard::Create
    -> EngineInit -> destroy every engine/text owner -> destroy runtime guard
  ```

  This `molga_runtime_dev` row is the ordinary `DevelopmentProject` path.
  Task 17.5 reserves the explicit `QualificationSealedLayout` variation used
  only by Task 18.2; that variation inserts `ValidateSealedPackage` between the
  root setter and guard creation without calling packaged-root initialization.

  At the end of Task 16.2, the implemented interim packaged sequence omits only
  the not-yet-produced `ValidateSealedPackage` arrow; do not claim sealed
  startup validation from this commit. Task 17.5 must insert that arrow before
  guard creation and its exit gate rejects the interim sequence. A
  missing/relative development root or failed package derivation returns `4`
  before `EngineInit`. Neither target calls the other root initializer.

- [ ] **Step 21: Give every GameBuilder package helper an explicit root**

  Split helpers so each receives `PackageRoots` or `resourceRoot`, not the app root string:

  ```cpp
  bool CreateBundleRoots(const BuildSettings&, PackageRoots&, std::string&);
  bool CopyExecutable(const PackageRoots&, const RuntimeBuildArtifacts&,
                      std::string& errorOut);
  bool WriteInfoPlist(const BuildSettings&, const PackageRoots&, std::string&);
  bool CopyAssets(const std::filesystem::path& resourceRoot);
  bool CopyScenes(const BuildPlan&, const std::filesystem::path& resourceRoot);
  bool CopyShaders(const std::filesystem::path& resourceRoot);
  bool CopyUserScripts(const std::filesystem::path& resourceRoot,
                       std::string& outResourceRelativeLibrary);
  bool EmitAssetCatalog(const std::filesystem::path& resourceRoot);
  bool CopyPlaceholderResource(const std::filesystem::path& resourceRoot);
  bool GenerateGameConfig(const BuildSettings&, const BuildPlan&,
                          const std::filesystem::path& resourceRoot);
  ```

  Update declarations and call sites to compile with these signatures. Each
  helper receives only the narrow root shown; no helper recomputes it from the
  executable or app-name string.

- [ ] **Step 22: Copy and verify only the configured runtime executable**

  `CopyExecutable` first requires
  `artifacts.runtimeExecutable ==
  ConfiguredRuntimeBuildArtifacts().runtimeExecutable` after canonicalization.
  Require a non-symlink regular file with owner execute permission; copy to
  `roots.executablePath`, flush/close, apply owner/group/other execute bits, and
  compare source/destination SHA-256 and size. A missing configured bundle
  payload, stale `ExecutableDir()/molga_runtime`, mismatch, copy failure, or
  permission failure leaves no final destination and returns
  `PACKAGE_RUNTIME_SOURCE_MISMATCH` or the exact I/O error.

- [ ] **Step 23: Route assets and catalog to `resourceRoot`**

  Change `CopyAssets` and `EmitAssetCatalog` to write only beneath the passed canonical `resourceRoot`. Reject an asset source/destination escape before copying, and make the exact-tree test assert both outputs under `Contents/Resources`.

- [ ] **Step 24: Route scenes, shaders, and placeholder to `resourceRoot`**

  Change `CopyScenes`, `CopyShaders`, and `CopyPlaceholderResource` to use only the passed canonical `resourceRoot`. Update the fixture assertion after each helper so no output appears beside `Contents/MacOS/<executable>`.

- [ ] **Step 25: Route scripts and game config to `resourceRoot`**

  `CopyUserScripts` writes only
  `resourceRoot/Scripts/<library-filename>` and returns the normalized
  resource-relative path `Scripts/<library-filename>`. Reject absolute paths,
  backslashes, `..`, symlink escape, or destination collision.
  `GenerateGameConfig` hashes shader inputs beneath `resourceRoot`, records the
  returned relative script path, and atomically writes only
  `resourceRoot/game.json`. It never accepts the app root or executable
  directory.

- [ ] **Step 26: Complete bundle assembly before full validation**

  Fix the exact GameBuilder order:

  ```text
  CreateBundleRoots -> CopyExecutable -> WriteInfoPlist(root-only validation)
  -> CopyAssets -> CopyShaders -> CopyScenes -> CopyUserScripts
  -> EmitAssetCatalog -> CopyPlaceholderResource -> stage text dependencies
  -> write coverage/runtime manifests -> GenerateGameConfig(last hash link)
  -> PackageLayout::Validate(complete PackageRoots) -> PackageFinalizer
  ```

  Convert every old `PackageLayout::Validate(flatRoot,...)` call in
  `GameBuilder.cpp`, `test_build_smoke.cpp`, `test_build_profile.cpp`, and
  `test_game_builder.cpp` to explicit `PackageRoots`. Then delete the temporary
  path overload from `PackageLayout.h/.cpp`; a source search for the old call
  shape must return no match. The full validator now sees all resources and is
  never called by `WriteInfoPlist`.

- [ ] **Step 27: Root every packaged runtime read beneath one captured root**

  Immediately after the packaged/development initializer succeeds in
  `runtime_main.cpp`, capture:

  ```cpp
  const auto runtimeRoot = PathService::Get().RuntimeResourceRoot();
  const auto packagePaths = PackageLayout::RuntimeResourcePaths(runtimeRoot);
  ```

  Use `packagePaths.gameConfig` for `LoadGameConfig`, `runtimeRoot` for
  `BuildRuntimeSceneCatalog`, `packagePaths.{assets,scenes,shaderBundle}` for
  required-directory checks, `runtimeRoot` for `SetAssetRoot`, and
  `packagePaths.assetCatalog` plus `runtimeRoot` for `LoadCatalog`.
  `ScriptPackageLoader::ResolveLibraryPath` canonicalizes relative libraries
  beneath `RuntimeResourceRoot`, rejects escape, and never joins
  `ExecutableDir`. `ExecutableDir` remains available only for executable,
  dylib, or tool identity; after this migration it has no resource-read call in
  `runtime_main.cpp`.

  Run:

  ```bash
  rg -n 'ExecutableDir\(\)' src/runtime_main.cpp \
    src/Scripting/ScriptPackageLoader.cpp
  ```

  Expected: no output. Then run the focused read-plan and relative-script tests.

- [ ] **Step 28: Revalidate the barrier-owned portable contract**

  Depend on `molga_text_dependencies_ready`, then read only
  `text_dependency_contract.json`. Task 1's verifier has already compared its
  allowlisted shared fields and logical archive digests with the machine-local
  build lock in one transactional publication; GameBuilder may not receive or
  reopen that provenance path. Revalidate the portable schema, versions,
  compile modes, ICU data identity, license identities, exact key sets, and
  absence of any absolute/checkout/build prefix before the first copy.

- [ ] **Step 29: Validate archive vectors and stage only portable inputs**

  Require the portable archive vectors to contain exactly one logical
  HarfBuzz archive and two ICU archives (`icui18n`, `icuuc`), sorted and with
  valid SHA-256 values already bound by Task 1's barrier. Copy only the
  portable contract, ICU data, and license inputs, including the verified
  `StbTrueType.txt`. Record the rasterizer's logical digest in the runtime
  manifest, but do not copy or search for the header.

- [ ] **Step 30: Run the green manifest/tree/plist/root gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_bundle_layout build_smoke \
    test_build_profile test_build_manager test_game_builder \
    test_runtime_script_package_loader \
    molga_engine molga_runtime molga_runtime_dev -j
  ctest --test-dir build/debug \
    -R '^(test_bundle_layout|build_smoke|test_build_profile|test_build_manager|test_game_builder|test_runtime_script_package_loader)$' \
    --output-on-failure
  ctest --test-dir build/debug \
    -R '^test_runtime_target_resource_roots$' --output-on-failure
  ```

  Expected: tests pass, both runtime targets build, the fixture tree contains
  only the portable contract, the staged plist names the exact staged
  executable, ordinary display text is escaped, and every injection/path case
  fails without a partial plist. Both runtime targets link with the shared
  configuration, enabled scripts load from `Contents/Resources/Scripts`, no
  runtime resource read uses `ExecutableDir`, the old layout adapter is gone,
  and the staged executable hash equals `$<TARGET_FILE:molga_runtime>` rather
  than a stale adjacent flat file.

- [ ] **Step 31: Commit the bundle and manifest slice**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt cmake/TextDependencies.cmake \
    resources/Info.plist.in \
    src/Text/TextRuntimeManifest.h src/Text/TextRuntimeManifest.cpp \
    src/Core/BuildProfile.h src/Core/BuildProfile.cpp \
    src/Core/GameConfig.h src/Core/GameConfig.cpp src/Core/PackageLayout.h \
    src/Core/PackageLayout.cpp src/Editor/GameBuilder.h \
    src/Editor/GameBuilder.cpp src/Editor/BuildManager.cpp \
    src/Scripting/ScriptPackageLoader.cpp \
    src/runtime_main.cpp tests/test_bundle_layout.cpp \
    tests/test_build_smoke.cpp tests/test_build_profile.cpp \
    tests/test_build_manager.cpp tests/test_game_builder.cpp \
    tests/test_runtime_script_package_loader.cpp
  git add tests/smoke/check_runtime_target_resources.cmake
  git commit -m "feat: stage canonical macOS runtime bundle"
  ```

### Task 16.3: Finalize and smoke-test a copied app outside the checkout

**Files:**

- Modify: `src/Core/PackageFinalizer.h`
- Modify: `src/Core/PackageFinalizer.cpp`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `tests/test_package_finalizer.cpp`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Create: `tests/smoke/test_run_end_to_end_preflight.cmake`
- Modify: `tests/test_game_builder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: final `PackageRoots`, exact tree from Task 16.2, and existing recoverable staged replacement.
- Produces: validated app finalization, fail-closed E2E input preflight, and an
  external-copy smoke path distinct from development runtime smoke.

- [ ] **Step 1: Add the failing finalizer containment test**

  ```cpp
  TEST_CASE("finalizer rejects a staged bundle with an escaping symlink") {
      test_support::TempDirectory temp{"bundle-symlink"};
      auto staged = MakeMinimalStagedBundle(temp.Path() / "Game.app.staging");
      fs::create_symlink("/tmp", staged.resources / "Assets/escape");
      const auto result = PackageFinalizer::FinalizeStagedPackage(
          staged.appRoot, temp.Path() / "Game.app",
          {staged.executable, staged.resources}, "Game");
      CHECK_FALSE(result.ok);
      CHECK(result.error.find("symlink") != std::string::npos);
      CHECK_FALSE(fs::exists(temp.Path() / "Game.app"));
  }
  ```

- [ ] **Step 2: Add the failing E2E variable-preflight test**

  Create `tests/smoke/test_run_end_to_end_preflight.cmake`. For each name in
  `{EDITOR,FIXTURE_SCRIPT,WORK_ROOT,PACKAGED_APP,SOURCE_DIR,BINARY_DIR,
  GAME_EXECUTABLE_BASENAME}`, invoke
  `run_end_to_end.cmake` with that one `-D` argument omitted and all other
  required arguments set to nonempty absolute fixture values. Require nonzero
  status and the exact diagnostic `<NAME> is required`. Then invoke once with
  all variables and `-DRUN_END_TO_END_PREFLIGHT_ONLY=ON`; require exit `0`.

  Register it exactly:

  ```cmake
  add_test(NAME test_smoke_end_to_end_preflight
    COMMAND "${CMAKE_COMMAND}"
      "-DRUN_END_TO_END=${CMAKE_SOURCE_DIR}/tests/smoke/run_end_to_end.cmake"
      -P "${CMAKE_SOURCE_DIR}/tests/smoke/test_run_end_to_end_preflight.cmake")
  ```

- [ ] **Step 3: Add the failing copied-app smoke assertions**

  Update `run_end_to_end.cmake` to begin with this exact preflight before it
  removes or creates any path:

  ```cmake
  foreach(required IN ITEMS EDITOR FIXTURE_SCRIPT WORK_ROOT PACKAGED_APP
      SOURCE_DIR BINARY_DIR GAME_EXECUTABLE_BASENAME)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "${required} is required")
    endif()
  endforeach()
  if(GAME_EXECUTABLE_BASENAME MATCHES "[/\\\\]" OR
     GAME_EXECUTABLE_BASENAME MATCHES "^\\.\\.?$")
    message(FATAL_ERROR
      "GAME_EXECUTABLE_BASENAME must be one safe basename")
  endif()
  if(RUN_END_TO_END_PREFLIGHT_ONLY)
    return()
  endif()
  ```

  CMake's `smoke_end_to_end` registration passes explicit canonical values:

  ```cmake
  "-DEDITOR=$<TARGET_FILE:molga_engine>"
  "-DFIXTURE_SCRIPT=${CMAKE_SOURCE_DIR}/tests/smoke/create_fixture.cmake"
  "-DWORK_ROOT=${CMAKE_BINARY_DIR}/smoke-e2e"
  "-DPACKAGED_APP=${CMAKE_BINARY_DIR}/smoke-e2e/package/SmokeGame.app"
  "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
  "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
  "-DGAME_EXECUTABLE_BASENAME=SmokeGame"
  ```

  Delete the script's platform branch that assigns a full path to
  `GAME_EXECUTABLE`; `GAME_EXECUTABLE_BASENAME` is never a path. Fail unless
  `PACKAGED_APP` is a `.app`, copy it to a `mktemp -d` equivalent path outside
  `SOURCE_DIR` and `BINARY_DIR`, and launch
  `Contents/MacOS/${GAME_EXECUTABLE_BASENAME}`. Add an assertion that
  `game.json`, scene, catalog, shader manifest, ICU data, and text runtime
  manifest were opened under the copied `Contents/Resources` path.

- [ ] **Step 4: Run the red finalization gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_package_finalizer test_game_builder -j
  ctest --test-dir build/debug -R '^(test_package_finalizer|test_game_builder|test_smoke_end_to_end_preflight|smoke_end_to_end)$' --output-on-failure
  ```

  Expected: the new signature/test fails; copied smoke still assumes a flat directory.

- [ ] **Step 5: Validate before replacing the previous app**

  Extend the finalizer signature:

  ```cpp
  static Result FinalizeStagedPackage(
      const std::filesystem::path& stagingApp,
      const std::filesystem::path& finalApp,
      const PackageRoots& stagedRoots,
      const std::string& executableName);
  ```

  Before renaming, call `PackageLayout::Validate`, walk the entire stage with `symlink_status`, reject every symlink, verify each canonical path remains under `resourceRoot` or is the exact executable/Info.plist, and reject any filename containing `text_dependency_build_lock`. Keep existing backup/restore behavior after validation succeeds.

- [ ] **Step 6: Make copied smoke use only bundle resources**

  In CMake script form:

  ```cmake
  file(REAL_PATH "${PACKAGED_APP}" source_app)
  file(REAL_PATH "${SOURCE_DIR}" source_root)
  file(REAL_PATH "${BINARY_DIR}" binary_root)
  get_filename_component(app_name "${source_app}" NAME)
  execute_process(COMMAND /usr/bin/mktemp -d -t molga-copied-app.XXXXXX
                  OUTPUT_VARIABLE copy_root
                  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  file(COPY "${source_app}" DESTINATION "${copy_root}")
  set(copied_app "${copy_root}/${app_name}")
  execute_process(
      COMMAND "${copied_app}/Contents/MacOS/${GAME_EXECUTABLE_BASENAME}" --smoke
      RESULT_VARIABLE result COMMAND_ERROR_IS_FATAL ANY)
  ```

  Require canonical `copy_root` to be outside both source and binary roots before copying. Derive the `.app` basename from the already resolved input as shown; do not leave a generator expression inside script mode. Do not set a development resource root for this launch.

- [ ] **Step 7: Run the green finalization gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_package_finalizer \
    test_game_builder molga_runtime -j
  ctest --test-dir build/debug -R '^(test_package_finalizer|test_game_builder|test_smoke_end_to_end_preflight|smoke_end_to_end)$' --output-on-failure
  ```

  Expected: staged validation passes, symlink/build-lock fixtures fail, previous-good rollback still passes, and smoke launches only the copied app.

- [ ] **Step 8: Commit copied-app finalization**

  ```bash
  git add CMakeLists.txt src/Core/PackageFinalizer.h \
    src/Core/PackageFinalizer.cpp src/Core/PackageLayout.cpp \
    tests/test_package_finalizer.cpp tests/test_game_builder.cpp \
    tests/smoke/run_end_to_end.cmake \
    tests/smoke/test_run_end_to_end_preflight.cmake
  git commit -m "feat: validate copied macOS app finalization"
  ```

### Task 17.1: Add authored localization and required text fixtures

**Files:**

- Create: `src/Text/AuthoredTextStyle.h`
- Create: `src/Text/AuthoredTextStyle.cpp`
- Create: `src/Assets/LocalizationTableAsset.h`
- Create: `src/Assets/LocalizationTableAsset.cpp`
- Create: `src/Core/Importers/LocalizationTableImporter.h`
- Create: `src/Core/Importers/LocalizationTableImporter.cpp`
- Modify: `src/Core/BuildProfile.h`
- Modify: `src/Core/BuildProfile.cpp`
- Modify: `src/Core/Importers/ImporterRegistry.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `tests/test_build_profile.cpp`
- Modify: `tests/test_asset_catalog.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: text paragraph enums and generic importer/catalog diagnostics.
- Produces: one complete `AuthoredTextStyle`, BuildProfile schema `3`,
  `RequiredTextFixture`, localization schema `1`, production request conversion,
  and deterministic nested unknown-field preservation.

- [ ] **Step 1: Add the failing complete required-fixture round-trip test**

  ```cpp
  static molga::text::AuthoredTextStyle CompleteFixtureStyle() {
      molga::text::AuthoredTextStyle style;
      style.fontFamilyGuid = "family-guid";
      style.fontRequest = {500, 90, molga::FontSlant::Italic};
      style.fontSize = molga::Fixed26_6::FromRaw(18 * 64);
      style.language = "ar";
      style.orderedFeatures = {{"kern", 1}, {"liga", 0}};
      style.variation = "default";
      style.baseDirection = molga::text::BaseDirection::RightToLeft;
      style.wrap = molga::text::TextWrapMode::Word;
      style.overflow = molga::text::TextOverflowMode::Ellipsis;
      style.maxLines = 2;
      style.lineSpacing = molga::Fixed26_6::FromRaw(80);
      style.horizontal = molga::text::TextHorizontalAlignment::Center;
      style.vertical = molga::text::TextVerticalAlignment::Middle;
      style.ellipsisUtf8 = "...";
      style.width = molga::Fixed26_6::FromRaw(320 * 64);
      style.height = molga::Fixed26_6::FromRaw(64 * 64);
      return style;
  }

  TEST_CASE("BuildProfile round trips required text fixtures") {
      BuildProfile profile = BuildProfile::Defaults("Fixture");
      profile.requiredTextFixtures.push_back({
          "dynamic-chat-arabic", u8"سلام", "ar", CompleteFixtureStyle()});
      const auto canonical = profile.Serialize();
      BuildProfile restored;
      REQUIRE(restored.Deserialize(canonical));
      REQUIRE(restored.requiredTextFixtures.size() == 1u);
      CHECK(restored.requiredTextFixtures[0].stableName ==
            "dynamic-chat-arabic");
      CHECK(restored.requiredTextFixtures[0].style.fontRequest.weight == 500);
      CHECK(restored.requiredTextFixtures[0].style.height->Raw() == 4096);
      CHECK(restored.Serialize() == canonical);
  }
  ```

- [ ] **Step 2: Add one exact complete localization fixture helper**

  ```cpp
  static nlohmann::ordered_json CompleteLocalizationJson() {
      return {
          {"schemaVersion", 1},
          {"defaultStyle", {
              {"fontFamilyGuid", "family-guid"}, {"weight", 400},
              {"stretchPercent", 100}, {"slant", "Upright"},
              {"fontSizeRaw", 1024}, {"language", "ko"},
              {"features", {{{"tag", "kern"}, {"value", 1}},
                            {{"tag", "liga"}, {"value", 1}}}},
              {"variation", "default"},
              {"direction", "Auto"}, {"wrap", "NoWrap"},
              {"overflow", "Overflow"}, {"maxLines", 0},
              {"lineSpacingRaw", 64},
              {"horizontalAlignment", "Left"},
              {"verticalAlignment", "Top"}, {"ellipsisUtf8", u8"…"},
              {"widthRaw", nullptr}, {"heightRaw", nullptr},
              {"defaultUnknown", {{"z", 2}, {"a", 1}}}}},
          {"entries", {
              {{"key", "menu.play"}, {"locale", "ko"},
               {"text", u8"시작"},
               {"entryUnknown", {{"visible", true}}}},
              {{"key", "dialog.title"}, {"locale", "ar"},
               {"text", u8"سلام"},
               {"entryUnknown", {{"priority", 7}}},
               {"styleOverride", {
                   {"fontFamilyGuid", "family-override"},
                   {"weight", 500}, {"stretchPercent", 90},
                   {"slant", "Italic"}, {"fontSizeRaw", 1152},
                   {"language", "ar"},
                   {"features", {{{"tag", "kern"}, {"value", 1}},
                                 {{"tag", "liga"}, {"value", 0}}}},
                   {"variation", "default"},
                   {"direction", "RightToLeft"},
                   {"wrap", "Word"}, {"overflow", "Ellipsis"},
                   {"maxLines", 2}, {"lineSpacingRaw", 80},
                   {"horizontalAlignment", "Center"},
                   {"verticalAlignment", "Middle"},
                   {"ellipsisUtf8", "..."}, {"widthRaw", 15360},
                   {"heightRaw", 4096},
                   {"styleUnknown", {{"token", "nested"}}}}}}}},
          {"authorNote", {{"owner", "keep me"}}}};
  }
  ```

- [ ] **Step 3: Add the failing importer dependency and default-style test**

  ```cpp
  TEST_CASE("localization importer retains complete default style") {
      const auto path = WriteLocalizationFixture(CompleteLocalizationJson());
      const auto result = molga::LocalizationTableImporter().Import(path.string());
      REQUIRE(result.success);
      CHECK(result.dependencies == std::vector<std::string>{
          "family-guid", "family-override"});
      CHECK(result.metadata["localization"]["entryCount"] == 2);
      const auto& unknown = result.metadata["preservedUnknown"];
      CHECK(unknown["root"]["authorNote"]["owner"] == "keep me");
      CHECK(unknown["defaultStyle"]["defaultUnknown"]["a"] == 1);
      CHECK(unknown["entries"][0]["entry"]["entryUnknown"]["priority"] == 7);
      CHECK(unknown["entries"][0]["styleOverride"]
                   ["styleUnknown"]["token"] == "nested");

      molga::text::VectorTextDiagnosticSink diagnostics;
      const auto parsed = molga::LocalizationTableAsset::FromJson(
          LoadJson(path), diagnostics);
      REQUIRE(parsed.has_value());
      CHECK(parsed->defaultStyle.fontFamilyGuid == "family-guid");
      CHECK(parsed->defaultStyle.fontRequest.weight == 400);
      CHECK(parsed->defaultStyle.fontRequest.stretchPercent == 100);
      CHECK(parsed->defaultStyle.fontRequest.slant ==
            molga::FontSlant::Upright);
      CHECK(parsed->defaultStyle.fontSize.Raw() == 1024);
      CHECK(parsed->defaultStyle.language == "ko");
      CHECK(parsed->defaultStyle.orderedFeatures ==
            std::vector<molga::text::AuthoredTextFeature>{
                {"kern", 1}, {"liga", 1}});
      CHECK(parsed->defaultStyle.baseDirection ==
            molga::text::BaseDirection::Auto);
      CHECK(parsed->defaultStyle.wrap == molga::text::TextWrapMode::NoWrap);
      CHECK(parsed->defaultStyle.overflow ==
            molga::text::TextOverflowMode::Overflow);
      CHECK(parsed->defaultStyle.maxLines == 0);
      CHECK(parsed->defaultStyle.lineSpacing.Raw() == 64);
      CHECK(parsed->defaultStyle.horizontal ==
            molga::text::TextHorizontalAlignment::Left);
      CHECK(parsed->defaultStyle.vertical ==
            molga::text::TextVerticalAlignment::Top);
      CHECK(parsed->defaultStyle.ellipsisUtf8 == u8"…");
      CHECK_FALSE(parsed->defaultStyle.width.has_value());
      CHECK_FALSE(parsed->defaultStyle.height.has_value());
  }
  ```

- [ ] **Step 4: Add the failing override and nested-unknown round-trip test**

  ```cpp
  TEST_CASE("localization override and unknown objects round trip canonically") {
      molga::text::VectorTextDiagnosticSink diagnostics;
      const auto parsed = molga::LocalizationTableAsset::FromJson(
          CompleteLocalizationJson(), diagnostics);
      REQUIRE(parsed.has_value());
      REQUIRE(parsed->entries[1].styleOverride.has_value());
      const auto& style = *parsed->entries[1].styleOverride;
      CHECK(parsed->entries[1].locale == "ar");
      CHECK(style.fontFamilyGuid == "family-override");
      CHECK(style.fontRequest.weight == 500);
      CHECK(style.fontRequest.stretchPercent == 90);
      CHECK(style.fontRequest.slant == molga::FontSlant::Italic);
      CHECK(style.fontSize.Raw() == 1152);
      CHECK(style.language == "ar");
      CHECK(style.baseDirection == molga::text::BaseDirection::RightToLeft);
      CHECK(style.wrap == molga::text::TextWrapMode::Word);
      CHECK(style.overflow == molga::text::TextOverflowMode::Ellipsis);
      CHECK(style.maxLines == 2);
      CHECK(style.lineSpacing.Raw() == 80);
      CHECK(style.horizontal == molga::text::TextHorizontalAlignment::Center);
      CHECK(style.vertical == molga::text::TextVerticalAlignment::Middle);
      CHECK(style.ellipsisUtf8 == "...");
      CHECK(style.width->Raw() == 15360);
      CHECK(style.height->Raw() == 4096);
      CHECK(parsed->rootExtras["authorNote"]["owner"] == "keep me");
      CHECK(parsed->defaultStyle.extras["defaultUnknown"]["a"] == 1);
      CHECK(parsed->entries[0].extras["entryUnknown"]["visible"] == true);
      CHECK(parsed->entries[1].extras["entryUnknown"]["priority"] == 7);
      CHECK(style.extras["styleUnknown"]["token"] == "nested");

      const auto first = parsed->ToCanonicalJson(diagnostics);
      REQUIRE(first.has_value());
      const auto reparsed = molga::LocalizationTableAsset::FromJson(
          *first, diagnostics);
      REQUIRE(reparsed.has_value());
      const auto second = reparsed->ToCanonicalJson(diagnostics);
      REQUIRE(second.has_value());
      CHECK(first->dump() == second->dump());
      CHECK(first->at("defaultStyle").at("defaultUnknown").at("z") == 2);
      CHECK(first->at("entries").at(1).at("styleOverride")
                 .at("styleUnknown").at("token") == "nested");
  }
  ```

- [ ] **Step 5: Add the failing known-key collision test**

  ```cpp
  TEST_CASE("unknown extras cannot shadow a known key at any owner") {
      const auto reject = [](auto mutate, const char* key) {
          molga::text::VectorTextDiagnosticSink diagnostics;
          auto parsed = molga::LocalizationTableAsset::FromJson(
              CompleteLocalizationJson(), diagnostics);
          REQUIRE(parsed.has_value());
          mutate(*parsed);
          CHECK_FALSE(parsed->ToCanonicalJson(diagnostics).has_value());
          CHECK(HasDiagnostic(diagnostics,
              molga::text::TextDiagnosticCode::LayoutInvalid,
              "LocalizationTableImporter", key));
      };
      SUBCASE("root") {
          reject([](auto& table) { table.rootExtras["schemaVersion"] = 9; },
                 "schemaVersion");
      }
      SUBCASE("default style") {
          reject([](auto& table) {
              table.defaultStyle.extras["fontFamilyGuid"] = "shadow";
          }, "fontFamilyGuid");
      }
      SUBCASE("entry") {
          reject([](auto& table) { table.entries[0].extras["key"] = "shadow"; },
                 "key");
      }
      SUBCASE("override") {
          reject([](auto& table) {
              table.entries[1].styleOverride->extras["wrap"] = "NoWrap";
          }, "wrap");
      }
      CHECK(std::string_view(molga::text::StableTextDiagnosticCode(
                molga::text::TextDiagnosticCode::LayoutInvalid)) ==
            "UI_LAYOUT_INVALID");
  }
  ```

- [ ] **Step 6: Run the red authored-source gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_build_profile test_asset_catalog -j
  ```

  Expected: compile fails because `AuthoredTextStyle`,
  `RequiredTextFixture`, and localization schema types do not exist.

- [ ] **Step 7: Define one complete authored style wrapper**

  ```cpp
  namespace molga::text {
  struct AuthoredTextFeature {
      std::string tag; // exactly four printable ASCII bytes
      std::uint32_t value = 0;
      bool operator==(const AuthoredTextFeature& other) const noexcept {
          return tag == other.tag && value == other.value;
      }
  };
  struct AuthoredTextStyle {
      std::string fontFamilyGuid;
      FontRequest fontRequest;
      Fixed26_6 fontSize = Fixed26_6::FromRaw(16 * 64);
      std::string language = "und";
      std::vector<AuthoredTextFeature> orderedFeatures;
      std::string variation = "default";
      BaseDirection baseDirection = BaseDirection::Auto;
      TextWrapMode wrap = TextWrapMode::NoWrap;
      TextOverflowMode overflow = TextOverflowMode::Overflow;
      std::uint32_t maxLines = 0;
      Fixed26_6 lineSpacing = Fixed26_6::FromRaw(64);
      TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
      TextVerticalAlignment vertical = TextVerticalAlignment::Top;
      std::string ellipsisUtf8 = u8"…";
      std::optional<Fixed26_6> width;
      std::optional<Fixed26_6> height;
      nlohmann::ordered_json extras = nlohmann::ordered_json::object();
  };
  std::optional<TextLayoutRequest> MakeAuthoredTextLayoutRequest(
      std::string utf8, std::string locale,
      const AuthoredTextStyle&, TextDiagnosticContext,
      TextDiagnosticSink&);
  }

  struct RequiredTextFixture {
      std::string stableName;
      std::string utf8;
      std::string locale = "und";
      molga::text::AuthoredTextStyle style;
  };
  ```

  This wrapper is the authored `ParagraphStyle + LayoutConstraints` contract.
  `MakeAuthoredTextLayoutRequest` copies every field directly: family and
  `FontRequest`; font size/language/features in authored order into
  `ShapeStyle`; the source locale and base direction into
  `TextAnalysisOptions`; wrap, overflow, max-lines, spacing, both alignments,
  and ellipsis into `ParagraphStyle`; and optional width/height into
  `LayoutConstraints`. Feature source ranges cover the complete input byte
  interval. It accepts only literal `variation == "default"`; Milestone A does
  not silently drop a non-default variation axis.

- [ ] **Step 8: Migrate BuildProfile to schema three**

  Set `BuildProfile::CurrentSchemaVersion = 3`. Schema 1/2 read with an empty
  fixture list. Schema 3 writes stable name, byte-preserved UTF-8, locale, and
  every known `AuthoredTextStyle` field: weight/stretch/slant, font size,
  language, ordered features, default variation, direction, wrap, overflow,
  max-lines, spacing, alignments, ellipsis, and nullable width/height raw 26.6
  values. Reject duplicate/empty stable names and invalid complete styles.
  Re-serialize deterministically without changing unrelated profile fields.

- [ ] **Step 9: Define localization schema one around the shared style**

  ```cpp
  struct LocalizationEntry {
      std::string key;
      std::string locale = "und";
      std::string utf8;
      std::optional<molga::text::AuthoredTextStyle> styleOverride;
      nlohmann::ordered_json extras = nlohmann::ordered_json::object();
  };
  struct LocalizationTableAsset {
      static constexpr int CurrentSchemaVersion = 1;
      molga::text::AuthoredTextStyle defaultStyle;
      std::vector<LocalizationEntry> entries;
      nlohmann::ordered_json rootExtras = nlohmann::ordered_json::object();
      static std::optional<LocalizationTableAsset> FromJson(
          const nlohmann::json&, molga::text::TextDiagnosticSink&);
      std::optional<nlohmann::ordered_json> ToCanonicalJson(
          molga::text::TextDiagnosticSink&) const;
  };
  ```

  Both style objects use the same canonical field encoding as BuildProfile.
  Optional `widthRaw`/`heightRaw` keys are always present and JSON `null` means
  unconstrained. Every table has a complete `defaultStyle`; an entry either
  uses it or supplies a complete `styleOverride`, never a partial merge.

- [ ] **Step 10: Parse every known localization/style field**

  Parse as `nlohmann::ordered_json`. Reject duplicate `{key,locale}`, missing
  key, invalid locale/language syntax, empty/invalid family GUID,
  weight/stretch/slant out of range, nonpositive font size, a feature tag that
  is not four printable ASCII bytes, duplicate feature tags, any variation
  other than literal `default`, negative width/height, invalid enum/alignment,
  partial override, invalid ellipsis UTF-8, and non-string text. Do not decode
  or normalize entry UTF-8 in the importer. Every error is a complete typed
  diagnostic with code, severity, subsystem, message, remediation, asset GUID,
  scene object ID `0`, component type `LocalizationTable`, and source byte range.

- [ ] **Step 11: Preserve unknown objects at all four ownership levels**

  At root, table `defaultStyle`, each entry, and each `styleOverride`, remove the
  exact known-key set and recursively sort all remaining object keys into the
  corresponding `rootExtras`/`extras`. `ToCanonicalJson` merges extras back at
  the same level after known fields. Reject an extras/known-key collision with
  existing code `LayoutInvalid`/`UI_LAYOUT_INVALID`, subsystem
  `LocalizationTableImporter`, and the exact colliding key in message/context;
  never overwrite either value. Importer
  metadata stores canonical `preservedUnknown` as `{root,defaultStyle,entries}`,
  where `entries` is sorted by `{locale,key}` and records separate `entry` and
  `styleOverride` extras. Thus nested sentinels survive import, catalog
  persistence, parse, and reserialization at their original owners.

- [ ] **Step 12: Register and persist the importer dependencies**

  `LocalizationTableImporter` handles only `.localization`, writes schema/entry
  count plus the complete nested `preservedUnknown` structure, emits full
  `importDiagnostics`, and returns sorted unique family GUID dependencies from
  default plus every override. Register it and include `.localization` in
  AssetDatabase/catalog round-trip without changing generic FNV `hash` into a
  security hash.

- [ ] **Step 13: Run the green authored-source gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_build_profile test_asset_catalog -j
  ctest --test-dir build/debug -R '^(test_build_profile|test_asset_catalog)$' --output-on-failure
  ```

  Expected: both pass; old profiles read an empty list; schema-3 fixtures retain
  every style/constraint value; localization dependencies are exact; root,
  default-style, entry, and override unknown objects survive; and known-key
  collisions fail with the typed diagnostic.

- [ ] **Step 14: Commit authored text sources**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/AuthoredTextStyle.h \
    src/Text/AuthoredTextStyle.cpp src/Assets/LocalizationTableAsset.h \
    src/Assets/LocalizationTableAsset.cpp \
    src/Core/Importers/LocalizationTableImporter.h \
    src/Core/Importers/LocalizationTableImporter.cpp \
    src/Core/Importers/ImporterRegistry.cpp src/Core/BuildProfile.h \
    src/Core/BuildProfile.cpp src/Core/AssetDatabase.h \
    src/Core/AssetDatabase.cpp tests/test_build_profile.cpp \
    tests/test_asset_catalog.cpp
  git commit -m "feat: author packaged text coverage sources"
  ```

### Task 17.2: Scan and shape every reachable authored text source

**Files:**

- Create: `src/Text/TextCoverageManifest.h`
- Create: `src/Text/TextCoverageManifest.cpp`
- Create: `src/Text/TextPackageValidator.h`
- Create: `src/Text/TextPackageValidator.cpp`
- Create: `tests/test_text_package.cpp`
- Modify: `src/Core/AssetDependencyValidator.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: reachable build plan, scene/prefab/localization/profile sources, production `TextLayoutService`, and complete typed diagnostics.
- Produces: `TextCoverageManifest`, `TextPackageValidator::CollectCoverageSources`,
  `ValidateCoverageSource`, `BindCoverageInputFiles`, and package-build blockers.

- [ ] **Step 1: Register `test_text_package` and add one successful source matrix**

  ```cmake
  molga_add_text_test(test_text_package test_text_package.cpp)
  ```

  This target uses Task 1's sole `text_doctest_main.cpp` and installed
  `TextRuntimeTestSession`; it never links `doctest_main`, constructs a second
  text runtime, or derives `Engine/Text` from the working directory. Preserve
  its existing package-test labels/properties after the helper call.

  In the test:

  ```cpp
  TEST_CASE("coverage collector includes each reachable authored source kind") {
      TextPackageFixture fixture;
      molga::text::AuthoredTextStyle localizationStyle;
      localizationStyle.fontFamilyGuid = "family-d";
      localizationStyle.fontRequest = {500, 90, molga::FontSlant::Italic};
      localizationStyle.fontSize = molga::Fixed26_6::FromRaw(1152);
      localizationStyle.language = "ja";
      localizationStyle.orderedFeatures = {{"kern", 1}, {"liga", 0}};
      localizationStyle.variation = "default";
      localizationStyle.baseDirection = molga::text::BaseDirection::Auto;
      localizationStyle.wrap = molga::text::TextWrapMode::Word;
      localizationStyle.overflow = molga::text::TextOverflowMode::Ellipsis;
      localizationStyle.maxLines = 2;
      localizationStyle.lineSpacing = molga::Fixed26_6::FromRaw(80);
      localizationStyle.horizontal =
          molga::text::TextHorizontalAlignment::Center;
      localizationStyle.vertical =
          molga::text::TextVerticalAlignment::Middle;
      localizationStyle.ellipsisUtf8 = "...";
      localizationStyle.width = molga::Fixed26_6::FromRaw(20480);
      localizationStyle.height = molga::Fixed26_6::FromRaw(4096);
      fixture.AddSceneLabel("Scenes/main.json", 11u, u8"سلام", "family-a");
      fixture.AddPrefabTextInput("Assets/dialog.prefab", 12u,
                                 u8"กำลัง", "family-b");
      fixture.AddWorldText("Scenes/main.json", 13u, u8"한글", "family-c");
      fixture.AddLocalization("Assets/ui.localization", "menu.play", "ja",
          u8"開始", localizationStyle);
      fixture.profile.requiredTextFixtures.push_back(
          Fixture("dynamic-score", u8"Score 123", "family-e"));
      const auto sources = fixture.validator.CollectCoverageSources(
          fixture.profile, fixture.buildPlan, fixture.projectRoot,
          fixture.diagnostics);
      CHECK(StableKinds(sources) == std::vector<std::string>{
          "fixture", "localization", "prefab.UITextInput.initialText",
          "scene.TextRenderer2D", "scene.UILabel"});
      const auto& localization = FindByStableId(
          sources, "localization-guid#ja/menu.play");
      CHECK(localization.request.style.fontFamilyGuid == "family-d");
      CHECK(localization.request.style.fontRequest.weight == 500);
      CHECK(localization.request.style.fontRequest.stretchPercent == 90);
      CHECK(localization.request.style.fontRequest.slant ==
            molga::FontSlant::Italic);
      CHECK(localization.request.style.shape.fontSize.Raw() == 1152);
      CHECK(localization.request.style.shape.language == "ja");
      CHECK(FeatureTagsAndValues(localization.request.style.shape) ==
            std::vector<std::pair<std::string, std::uint32_t>>{
                {"kern", 1}, {"liga", 0}});
      CHECK(localization.request.style.analysis.locale == "ja");
      CHECK(localization.request.style.analysis.baseDirection ==
            molga::text::BaseDirection::Auto);
      CHECK(localization.request.style.wrap ==
            molga::text::TextWrapMode::Word);
      CHECK(localization.request.style.overflow ==
            molga::text::TextOverflowMode::Ellipsis);
      CHECK(localization.request.style.maxLines == 2);
      CHECK(localization.request.style.lineSpacing.Raw() == 80);
      CHECK(localization.request.style.horizontal ==
            molga::text::TextHorizontalAlignment::Center);
      CHECK(localization.request.style.vertical ==
            molga::text::TextVerticalAlignment::Middle);
      CHECK(localization.request.style.ellipsisUtf8 == "...");
      REQUIRE(localization.request.constraints.width.has_value());
      CHECK(localization.request.constraints.width->Raw() == 20480);
      REQUIRE(localization.request.constraints.height.has_value());
      CHECK(localization.request.constraints.height->Raw() == 4096);
  }
  ```

  `TextPackageFixture`, `Fixture`, and `StableKinds` are defined in `tests/test_text_package.cpp`; their setup writes real JSON files and imports the pinned test families.

- [ ] **Step 2: Add invalid UTF-8 and missing-glyph failures**

  ```cpp
  TEST_CASE("authored invalid UTF8 and missing glyph block package") {
      TextPackageFixture fixture;
      fixture.AddSceneLabelBytes("Scenes/main.json", 21u,
          std::string("A\xF0\x28\x8C\x28", 5), "latin-only");
      fixture.AddRequiredFixture("emoji", u8"👩‍🚀", "latin-only");
      molga::text::TextCoverageManifest manifest;
      CHECK_FALSE(fixture.validator.ValidateCoverage(
          fixture.profile, fixture.buildPlan, fixture.projectRoot,
          manifest, fixture.diagnostics));
      CHECK(HasDiagnostic(fixture.diagnostics,
          molga::text::TextDiagnosticCode::Utf8Invalid, 21u, {1u, 2u}));
      CHECK(HasDiagnostic(fixture.diagnostics,
          molga::text::TextDiagnosticCode::MissingGlyph,
          /*sceneObjectId=*/0u, /*sourceBytes=*/{0u, 11u},
          /*graphemes=*/{0u, 1u}));
  }
  ```

- [ ] **Step 3: Add content-derived family revision tests**

  ```cpp
  TEST_CASE("family revision hashes authored closure content not process history") {
      TextPackageFixture first;
      first.AddFamily("root", {Face("font-a", Sha('a'), 0, 400, 100,
                                    FontSlant::Upright)}, {"fallback-b", "fallback-c"});
      first.AddFamily("fallback-b", {Face("font-b", Sha('b'))}, {});
      first.AddFamily("fallback-c", {Face("font-c", Sha('c'))}, {});
      const auto revision = first.validator.ComputeFontFamilyRevision("root");
      REQUIRE(IsSha256(revision));

      auto sameBytesDifferentProcessGeneration = first.CloneWithGenerations(99u);
      CHECK(sameBytesDifferentProcessGeneration.validator.
            ComputeFontFamilyRevision("root") == revision);

      auto reordered = first.Clone();
      reordered.SetFallbackOrder("root", {"fallback-c", "fallback-b"});
      CHECK(reordered.validator.ComputeFontFamilyRevision("root") != revision);

      auto changedStyle = first.Clone();
      changedStyle.SetFaceWeight("font-b", 700);
      CHECK(changedStyle.validator.ComputeFontFamilyRevision("root") != revision);

      auto changedBytes = first.Clone();
      changedBytes.SetFontSourceSha("font-c", Sha('d'));
      CHECK(changedBytes.validator.ComputeFontFamilyRevision("root") != revision);

      const auto a = first.ValidateOne(u8"same text", "root");
      const auto b = changedBytes.ValidateOne(u8"same text", "root");
      CHECK(a.sourceSha256 == b.sourceSha256);
      CHECK(a.fontFamilyRevision != b.fontFamilyRevision);
  }
  ```

- [ ] **Step 4: Add the failing complete coverage-descriptor test**

  ```cpp
  TEST_CASE("coverage descriptor copies every production request field") {
      TextPackageFixture fixture;
      const auto input = fixture.MakeCompleteCoverageInput();
      molga::text::TextCoverageSource output;
      REQUIRE(fixture.validator.ValidateCoverageSource(
          input, output, fixture.diagnostics));
      CHECK(output.weight == 500);
      CHECK(output.stretchPercent == 90);
      CHECK(output.slant == molga::FontSlant::Italic);
      CHECK(output.fontSizeRaw == 1152);
      CHECK(output.language == "ja");
      CHECK(output.orderedFeatures ==
            std::vector<molga::text::AuthoredTextFeature>{
                {"kern", 1}, {"liga", 0}});
      CHECK(output.variation == "default");
      CHECK(output.locale == "ja");
      CHECK(output.baseDirection == molga::text::BaseDirection::Auto);
      CHECK(output.wrap == molga::text::TextWrapMode::Word);
      CHECK(output.overflow == molga::text::TextOverflowMode::Ellipsis);
      CHECK(output.maxLines == 2);
      CHECK(output.lineSpacingRaw == 80);
      CHECK(output.horizontal ==
            molga::text::TextHorizontalAlignment::Center);
      CHECK(output.vertical == molga::text::TextVerticalAlignment::Middle);
      CHECK(output.ellipsisUtf8 == "...");
      CHECK(output.authoredWidthRaw == 20480);
      CHECK(output.authoredHeightRaw == 4096);
      molga::text::TextCoverageManifest manifest;
      manifest.sources = {output};
      const auto json = manifest.SerializeCanonical();
      CHECK(json["sources"][0]["features"][1]["tag"] == "liga");
      CHECK(json["sources"][0]["heightRaw"] == 4096);
  }
  ```

  `MakeCompleteCoverageInput` uses the exact style values from Step 1 and
  returns the collected localization source, so this test exercises the
  production converter rather than constructing `TextCoverageSource` directly.

  Add the staged-input binding case:

  ```cpp
  TEST_CASE("coverage manifest binds every contributing staged document") {
      TextPackageFixture fixture;
      fixture.AddSceneLabel("Scenes/main.json", 11u, u8"سلام", "family-a");
      fixture.AddPrefabTextInput("Assets/dialog.prefab", 12u,
                                 u8"กำลัง", "family-b");
      fixture.AddLocalization("Assets/ui.localization", "menu.play", "ja",
                              u8"開始", fixture.CompleteStyle());
      fixture.AddRequiredFixture("dynamic-score", u8"Score 123", "family-e");
      fixture.StageCoverageInputDocuments();
      const auto manifest = fixture.BuildCoverageManifest();
      CHECK(manifest.inputFiles == std::vector<TextCoverageInputFile>{
          {"Assets/dialog.prefab",
           Sha256File(fixture.resources / "Assets/dialog.prefab")},
          {"Assets/ui.localization",
           Sha256File(fixture.resources / "Assets/ui.localization")},
          {"Manifests/text_required_fixtures.json",
           Sha256File(fixture.resources /
                      "Manifests/text_required_fixtures.json")},
          {"Scenes/main.json",
           Sha256File(fixture.resources / "Scenes/main.json")}});
      CHECK(manifest.SerializeCanonical()["inputFiles"].size() == 4u);
  }
  ```

  `text_required_fixtures.json` is a canonical package input generated from
  BuildProfile's complete `requiredTextFixtures` only; it contains no output
  path, build path, runtime manifest hash, or `game.json` field, so it creates
  no hash cycle.

- [ ] **Step 5: Run the red coverage gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package -j
  ```

  Expected: compile fails because coverage manifest/validator contracts do not exist.

- [ ] **Step 6: Define canonical coverage records**

  ```cpp
  namespace molga::text {
  struct TextCoverageInputFile {
      std::string path;
      std::string sha256;
      friend bool operator==(const TextCoverageInputFile& lhs,
                             const TextCoverageInputFile& rhs) {
          return lhs.path == rhs.path && lhs.sha256 == rhs.sha256;
      }
  };
  struct TextCoverageSource {
      std::string kind;
      std::string stableId;
      std::string sourceSha256;
      std::string fontFamilyGuid;
      std::string fontFamilyRevision;
      std::uint16_t weight = 400;
      std::uint16_t stretchPercent = 100;
      molga::FontSlant slant = molga::FontSlant::Upright;
      std::int32_t fontSizeRaw = 0;
      std::string language;
      std::vector<AuthoredTextFeature> orderedFeatures;
      std::string variation = "default";
      std::string locale;
      BaseDirection baseDirection = BaseDirection::Auto;
      TextWrapMode wrap = TextWrapMode::NoWrap;
      TextOverflowMode overflow = TextOverflowMode::Overflow;
      std::uint32_t maxLines = 0;
      std::int32_t lineSpacingRaw = 0;
      TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
      TextVerticalAlignment vertical = TextVerticalAlignment::Top;
      std::string ellipsisUtf8;
      std::optional<std::int32_t> authoredWidthRaw;
      std::optional<std::int32_t> authoredHeightRaw;
      std::vector<GraphemeRange> missingGraphemes;
  };
  struct TextCoverageManifest {
      static constexpr int CurrentSchemaVersion = 1;
      std::string dependencyLockSha256;
      std::vector<TextCoverageInputFile> inputFiles;
      std::vector<TextCoverageSource> sources;
      nlohmann::ordered_json SerializeCanonical() const;
  };
  struct AuthoredTextCoverageInput {
      std::string kind;
      std::string stableId;
      std::string sourceSha256;
      std::filesystem::path contributingSourcePath;
      unsigned int sceneObjectId = 0;
      std::string componentType;
      TextLayoutRequest request;
  };
  }
  ```

  Canonical serialization sorts `inputFiles` by `path`, rejects a
  duplicate path, unsafe/absolute/backslash path, non-lowercase SHA-256, or
  path equal to either coverage/runtime manifest, and emits `inputFiles`
  before `sources`.

  Add this exact public helper:

  ```cpp
  std::string ComputeFontFamilyRevision(
      const std::string& rootFamilyGuid, TextDiagnosticSink&) const;
  ```

- [ ] **Step 7: Enumerate reachable scene documents once**

  Add:

  ```cpp
  std::vector<AuthoredTextCoverageInput> CollectCoverageSources(
      const BuildProfile&, const BuildPlan&,
      const std::filesystem::path& projectRoot,
      TextDiagnosticSink&);
  ```

  Walk only the build plan's canonical reachable scene paths, reject escape/missing/malformed files, and visit objects in serialized order. Keep one visited canonical-path set; do not scan unrelated project scenes.
  Set every source collected from one scene to that scene's normalized staged
  resource-relative path (for example `Scenes/main.json`) in
  `contributingSourcePath`; do not store an absolute project path.

- [ ] **Step 8: Collect `UILabel` scene sources**

  Extract exact `UILabel.text`, family GUID, locale, direction, wrap, overflow, width/rect constraint, and complete paragraph style. Stable IDs are `scenePath#objectId/UILabel`; hash the original UTF-8 source bytes before shaping.

- [ ] **Step 9: Collect `UITextInput.initialText` scene sources**

  Extract authored `initialText` and the input's complete family/paragraph contract, never runtime edit/composition state. Stable IDs are `scenePath#objectId/UITextInput.initialText`.

- [ ] **Step 10: Collect `TextRenderer2D` scene sources**

  Extract exact world-text bytes plus family, locale, base direction, `NoWrap/Overflow`, font size, and transform-independent shaping inputs. Stable IDs are `scenePath#objectId/TextRenderer2D`.

- [ ] **Step 11: Collect prefab sources without revisiting GUIDs**

  Recursively follow prefab GUID dependencies using an explicit visited set and extract the same components. Stable IDs are `prefabGuid#objectId/componentType`; a cycle emits the existing dependency blocker rather than recursing.
  Every extracted source records the prefab's normalized staged path, such as
  `Assets/dialog.prefab`, in `contributingSourcePath`.

- [ ] **Step 12: Collect localization entry sources**

  For each reachable localization entry, choose its complete `styleOverride` or table `defaultStyle`, set the entry locale, and build a real `TextLayoutRequest`. Stable IDs are `localizationGuid#locale/key`.
  All entries from one table record the table's staged
  `Assets/<relative-path>.localization` path as their contributing source.

- [ ] **Step 13: Materialize and re-read the required-fixture input**

  Add `WriteRequiredTextFixtureInput`. It serializes only BuildProfile's
  complete `requiredTextFixtures` as canonical schema-1 JSON to
  `Manifests/text_required_fixtures.json`, atomically publishes it, reopens the
  bytes, requires byte-for-byte canonical form, and parses them with the same
  authored-style decoder as Task 17.1. It writes no output path, build path,
  runtime-manifest hash, or `game.json` field. Missing/malformed publish or
  re-read is a package blocker and leaves no partial destination.

- [ ] **Step 14: Collect profile fixture sources from the staged document**

  Append the re-read fixture entries with their complete authored style and
  stable IDs `fixture#stableName`; every entry uses
  `Manifests/text_required_fixtures.json` as `contributingSourcePath`. Sort the
  combined scene/prefab/localization/fixture vector by
  `{kind,stableId,sourceSha256}`.

- [ ] **Step 15: Run the focused collection test**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package -j
  build/debug/tests/test_text_package --test-case="*collector includes*"
  ```

  Expected: the source matrix case passes; invalid/missing cases still fail until shaping is added.

- [ ] **Step 16: Compute the family revision canonical closure**

  Serialize a canonical ordered object rooted at `rootFamilyGuid`. Preserve authored fallback order and authored face order. Each family record contains its GUID and ordered fallback GUIDs; each face record contains `{fontGuid,sourceSha256,faceIndex,weight,stretchPercent,slant,authoredFaceIndex}`. Hash those canonical bytes with SHA-256. Do not include process generation, import timestamp, absolute path, or traversal cache state.

- [ ] **Step 17: Decode and analyze one coverage source**

  Add:

  ```cpp
  bool ValidateCoverageSource(
      const AuthoredTextCoverageInput&, TextCoverageSource& output,
      TextDiagnosticSink&);
  ```

  Start `ValidateCoverageSource` by calling `UnicodeTextBuffer::Build` and the
  ICU analyzer with the request locale/direction. Any decode diagnostic on
  authored data is a build blocker even though preview uses `U+FFFD`; retain
  exact byte/grapheme context in the typed diagnostic and return before family
  resolution.

- [ ] **Step 18: Resolve, shape, and layout the analyzed source**

  Resolve the exact authored family closure, shape through production
  HarfBuzz, and call `TextLayoutService::Layout` with the unchanged request.
  Collect exact missing grapheme ranges from final shaped glyphs. A missing
  family/face or missing grapheme is a package blocker with source/object/family
  context; do not use system font, last-good state, or the preview tofu result
  as success.

- [ ] **Step 19: Copy the complete descriptor from the accepted request**

  Fill
  `fontFamilyRevision` only with `ComputeFontFamilyRevision`. Copy every known
  request field into the descriptor and canonical JSON: font family and
  weight/stretch/slant, font size, language, ordered features, literal default
  variation, locale/direction, wrap/overflow, max-lines, spacing, alignments,
  ellipsis, and nullable width/height. Do not infer defaults while reading a
  coverage manifest. `AuthoredTextStyle.extras` are preserved in the source
  asset but excluded from this shaping descriptor because they are not
  production request inputs; the original source SHA still binds their bytes.

- [ ] **Step 20: Assemble validated coverage sources without publishing**

  `ValidateCoverage` processes the sorted inputs, retains all diagnostics,
  returns `false` if any authored source has invalid UTF-8/missing
  glyph/blocker, and otherwise writes sources sorted by `{kind,stableId}`. Set
  `dependencyLockSha256` to the portable contract SHA. Do not fill
  `inputFiles` or publish the coverage manifest yet: GameBuilder must first
  finish staging the exact source-document bytes that will ship.

  ```cpp
  bool ValidateCoverage(const BuildProfile&, const BuildPlan&,
                        const std::filesystem::path& projectRoot,
                        TextCoverageManifest&, TextDiagnosticSink&);
  ```

- [ ] **Step 21: Bind final staged source-document bytes**

  Add:

  ```cpp
  bool BindCoverageInputFiles(
      const std::vector<AuthoredTextCoverageInput>& inputs,
      const std::filesystem::path& resourceRoot,
      TextCoverageManifest& manifest,
      TextDiagnosticSink& diagnostics);
  ```

  After GameBuilder has staged final scene, prefab, localization, and required
  fixture documents, normalize and deduplicate every
  `contributingSourcePath`, require canonical containment beneath
  `resourceRoot`, reject symlinks/escape/missing files, reopen the final bytes,
  compute lowercase SHA-256, and fill `manifest.inputFiles` sorted by `path`.
  A duplicate path is allowed only as collection deduplication; a path with
  inconsistent normalization or bytes is a blocker. Never accept a hash
  computed from the project source before staging.

- [ ] **Step 22: Run the green coverage gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package -j
  ctest --test-dir build/debug -R '^test_text_package$' --output-on-failure
  ```

  Expected: source collection and final staged-input binding succeed; the four
  fixture input paths/SHA-256 values are exact, and both authored invalid cases
  fail with exact object/component/byte/grapheme context.

- [ ] **Step 23: Commit coverage scanning**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextCoverageManifest.h \
    src/Text/TextCoverageManifest.cpp src/Text/TextPackageValidator.h \
    src/Text/TextPackageValidator.cpp src/Core/AssetDependencyValidator.cpp \
    src/Editor/GameBuilder.h src/Editor/GameBuilder.cpp \
    tests/test_text_package.cpp
  git commit -m "feat: validate authored text coverage"
  ```

### Task 17.3: Materialize the complete font and notice closure

**Files:**

- Create: `src/Text/TextNoticeGenerator.h`
- Create: `src/Text/TextNoticeGenerator.cpp`
- Modify: `src/Assets/FontArtifactStore.h`
- Modify: `src/Assets/FontArtifactStore.cpp`
- Modify: `src/Text/FontRepository.h`
- Modify: `src/Text/FontRepository.cpp`
- Modify: `src/Text/TextRuntimeManifest.h`
- Modify: `src/Text/TextRuntimeManifest.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Text/TextPackageValidator.h`
- Modify: `src/Text/TextPackageValidator.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `tests/test_text_package.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_font_assets.cpp`
- Modify: `tests/test_asset_catalog.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: validated coverage family roots, `FontFamilyAsset`, Task 4.1's
  exact `FontArtifactStorage`/locator/store contract, immutable
  `FontFaceResource`, license metadata, and Task 1 notice/license hashes.
- Produces: deterministic `FontPackageClosure`, one content-addressed packaged
  artifact per distinct verified project artifact, sealed-catalog locator
  rewrite, manifest-backed packaged `FontRepository`, `TextNoticeGenerator`,
  staged licenses, and final notice SHA.

- [ ] **Step 1: Add failing fallback-closure tests**

  ```cpp
  TEST_CASE("package closure includes fallback-only faces and licenses once") {
      TextPackageFixture fixture;
      fixture.AddFamily("root", {"font-a"}, {"fallback"});
      fixture.AddFamily("fallback", {"font-b", "font-c"}, {});
      const auto closure = fixture.validator.BuildFontPackageClosure(
          {"root"}, fixture.diagnostics);
      REQUIRE(closure.has_value());
      CHECK(FontGuids(*closure) ==
            std::vector<std::string>{"font-a", "font-b", "font-c"});
      CHECK(UniqueLicenseGuids(*closure).size() == 3u);
  }

  TEST_CASE("cycle invalid GUID and unconfirmed redistribution block closure") {
      TextPackageFixture fixture;
      fixture.AddFamily("a", {"unconfirmed-font"}, {"b"});
      fixture.AddFamily("b", {}, {"a", "missing-family"});
      CHECK_FALSE(fixture.validator.BuildFontPackageClosure(
          {"a"}, fixture.diagnostics).has_value());
      CHECK(HasDiagnostic(fixture.diagnostics,
          molga::text::TextDiagnosticCode::FontFamilyInvalid));
      CHECK(HasDiagnostic(fixture.diagnostics,
          molga::text::TextDiagnosticCode::FontInvalid));
  }

  ```

- [ ] **Step 2: Add the failing immutable-artifact staging test**

  ```cpp
  TEST_CASE("package stages the verified imported artifact not authored source") {
      auto fixture = MakePackagedFontFixture();
      const auto imported = fixture.AddReachableConfirmedFont(
          "font-used", "Assets/AuthoredFonts/used.otf", Sha('a'));
      REQUIRE(imported.artifact.locator.storage ==
              molga::FontArtifactStorage::ProjectLibrary);
      CHECK(imported.artifact.locator.relativePath ==
            fs::path("Library/Imported/Fonts") /
                (imported.artifact.artifactSha256 + ".sfnt"));
      fixture.ReplaceAuthoredSourceAfterImport("font-used", Bytes('z'));

      const auto built = fixture.Build();
      REQUIRE(built.success);
      const auto resources = built.app / "Contents/Resources";
      const auto expectedPath = fs::path("Assets/Fonts") /
          (imported.artifact.artifactSha256 + ".sfnt");
      const auto packagedRecord = LoadCatalogRecord(
          resources / "asset_catalog.json", "font-used");
      CHECK(packagedRecord.fontArtifact.locator.storage ==
            molga::FontArtifactStorage::PackagedResource);
      CHECK(packagedRecord.fontArtifact.locator.relativePath == expectedPath);
      CHECK(packagedRecord.fontArtifact.sourceSha256 ==
            imported.artifact.sourceSha256);
      CHECK(packagedRecord.fontArtifact.artifactSha256 ==
            imported.artifact.artifactSha256);
      CHECK(packagedRecord.fontArtifact.byteSize == imported.artifact.byteSize);
      CHECK(ReadBytes(resources / expectedPath) ==
            fixture.ReadProjectArtifact(imported.artifact));
      CHECK(ReadBytes(resources / expectedPath) !=
            fixture.ReadAuthoredSource("font-used"));
      CHECK_FALSE(fs::exists(resources / "Assets/AuthoredFonts/used.otf"));

      const auto runtime = LoadTextRuntimeManifest(resources);
      const auto& font = RuntimeFont(runtime, "font-used", 0);
      CHECK(font.sourcePath == expectedPath.generic_string());
      CHECK(font.sourceSha256 == imported.artifact.artifactSha256);
  }
  ```

- [ ] **Step 3: Add the failing no-source-fallback package test**

  ```cpp
  TEST_CASE("missing project artifact blocks build despite intact source") {
      auto fixture = MakePackagedFontFixture();
      const auto imported = fixture.AddReachableConfirmedFont(
          "font-used", "Assets/AuthoredFonts/used.otf", Sha('a'));
      fixture.RemoveProjectArtifact(imported.artifact);
      REQUIRE(fs::exists(fixture.AuthoredSource("font-used")));
      const auto built = fixture.Build();
      CHECK_FALSE(built.success);
      CHECK(fixture.AuthoringSourceOpenCountDuringClosure() == 0u);
      CHECK(HasDiagnostic(built.diagnostics,
          molga::text::TextDiagnosticCode::FontInvalid,
          "font-used", "Library/Imported/Fonts"));
      CHECK(FindSfntFilesByMagic(built.stage / "Contents/Resources").empty());
  }
  ```

- [ ] **Step 4: Add the failing sealed-repository authority test**

  Add to the existing Task 4/5 session-backed `tests/test_font_assets.cpp`:

  ```cpp
  TEST_CASE("sealed repository reads only matching catalog and manifest locator") {
      auto package = MakeSealedFontRepositoryFixture();
      const auto runtime = package.RuntimeManifest();
      const auto authorities = molga::text::PackagedFontAuthorities(runtime);
      auto store = molga::FontArtifactStore::ForSealedPackage(
          package.ResourceRoot(), authorities, package.Diagnostics());
      REQUIRE(store.has_value());
      auto repository = package.MakeRepository(*store);
      REQUIRE(repository.Load(package.FontGuid(), 0).has_value());
      CHECK(package.AuthoringSourceOpenCount() == 0u);

      package.RewriteCatalogArtifactPath("Assets/Fonts/other.sfnt");
      CHECK_FALSE(package.MakeRepository(*store).Load(
          package.FontGuid(), 0).has_value());
      CHECK(package.AuthoringSourceOpenCount() == 0u);

      package.RestoreCatalog();
      package.RewriteManifestFontSha(Sha('f'));
      auto rejected = molga::FontArtifactStore::ForSealedPackage(
          package.ResourceRoot(), package.ManifestAuthorities(),
          package.Diagnostics());
      CHECK_FALSE(rejected.has_value());
      CHECK(package.AuthoringSourceOpenCount() == 0u);
  }
  ```

- [ ] **Step 5: Add the failing deterministic locator and deduplication test**

  ```cpp
  TEST_CASE("packaged artifact path is content addressed and published once") {
      TextPackageFixture fixture;
      const auto sameArtifact = fixture.ProjectArtifact(Sha('a'));
      auto closure = fixture.MakeClosureEntries({
          ConfirmedPackageFace("font-a", sameArtifact, 0),
          ConfirmedPackageFace("font-a", sameArtifact, 0)});
      std::string error;
      CHECK(molga::text::PackagedFontArtifactPath(Sha('a'), error) ==
            fs::path("Assets/Fonts") / (Sha('a') + ".sfnt"));
      CHECK_FALSE(molga::text::PackagedFontArtifactPath("ABC", error)
                      .has_value());
      REQUIRE(fixture.validator.ValidateFontPackageDestinations(
          closure, fixture.ResourceRoot(), fixture.diagnostics));
      const auto built = fixture.PublishClosure(closure);
      CHECK(FindSfntFilesByMagic(built.resources) ==
            std::vector<fs::path>{
                fs::path("Assets/Fonts") / (Sha('a') + ".sfnt")});
  }
  ```

- [ ] **Step 6: Add the failing unused-font exclusion test**

  Choose package policy **B** explicitly: generic asset copying never packages
  a `FontImporter` record or the generic asset record named by any font's
  `licenseAssetGuid`, regardless of that license record's importer. Only the
  validated reachable closure may add font bytes and legal material.

  ```cpp
  TEST_CASE("unused unconfirmed font is absent from package and catalog") {
      auto fixture = MakePackagedFontFixture();
      fixture.AddReachableConfirmedFont(
          "font-used", "Assets/Fonts/used.otf", Sha('a'));
      fixture.AddUnusedUnconfirmedFont(
          "font-unused", "Assets/Fonts/unused.otf", Sha('b'));
      const auto built = fixture.Build();
      REQUIRE(built.success);
      const auto resources = built.app / "Contents/Resources";
      CHECK(fs::exists(resources / "Assets/Fonts" / (Sha('a') + ".sfnt")));
      CHECK_FALSE(fs::exists(resources / "Assets/Fonts" / (Sha('b') + ".sfnt")));
      CHECK_FALSE(fs::exists(resources / "Assets/Fonts/used.otf"));
      CHECK_FALSE(fs::exists(resources / "Assets/Fonts/unused.otf"));
      CHECK_FALSE(fs::exists(resources / "Library/Imported/Fonts"));
      CHECK_FALSE(fs::exists(resources / "Assets/Licenses/used-OFL.txt"));
      CHECK_FALSE(CatalogHasGuid(
          resources / "asset_catalog.json", "font-unused"));
      CHECK(CatalogHasGuid(resources / "asset_catalog.json", "font-used"));
      const auto packagedLicense = ManifestLicensePathFor(
          resources / "Manifests/text_runtime.json", "font-used");
      CHECK(packagedLicense.generic_string().rfind("Licenses/Fonts/", 0) == 0);
      CHECK(fs::exists(resources / packagedLicense));
  }

  TEST_CASE("reachable closure rejects a font with unresolved license guid") {
      auto fixture = MakePackagedFontFixture();
      fixture.AddReachableFontWithLicenseGuid(
          "font-orphan", "Assets/Fonts/orphan.otf", "missing-license-guid");
      const auto built = fixture.Build();
      CHECK_FALSE(built.success);
      CHECK(HasDiagnostic(built.diagnostics,
          molga::text::TextDiagnosticCode::FontInvalid,
          "font-orphan", "missing-license-guid"));
      CHECK(FindSfntFilesByMagic(
          built.stage / "Contents/Resources").empty());
  }
  ```

  `AddUnusedUnconfirmedFont` still creates a resolvable license asset record;
  only its redistribution flag is unconfirmed. Thus the first case proves
  unreachable legal status is not treated as reachable closure, while the
  second proves a font becomes a blocker when its family is reachable and its
  required legal record cannot be resolved.

- [ ] **Step 7: Add the failing package-wide SFNT ownership test**

  ```cpp
  TEST_CASE("every packaged SFNT path appears exactly once in runtime manifest") {
      const auto built = MakePackagedFontFixture().Build();
      REQUIRE(built.success);
      const auto resources = built.app / "Contents/Resources";
      molga::text::TextRuntimeManifest runtime;
      REQUIRE(molga::text::TextRuntimeManifest::Deserialize(
          LoadJson(resources / "Manifests/text_runtime.json"), runtime));
      const auto diskPaths = FindSfntFilesByMagic(resources);
      const auto manifestPaths = SortedFontSourcePaths(runtime);
      CHECK(diskPaths == manifestPaths);
      CHECK(std::adjacent_find(manifestPaths.begin(), manifestPaths.end()) ==
            manifestPaths.end());
      for (const auto& path : manifestPaths) {
          CHECK(fs::is_regular_file(resources / path));
          CHECK_FALSE(fs::is_symlink(resources / path));
          CHECK(path.generic_string().rfind("Assets/Fonts/", 0) == 0);
          CHECK(path.extension() == ".sfnt");
      }
      CHECK_FALSE(fs::exists(resources / "Library/Imported/Fonts"));
      CHECK(FindAuthoredFontSourcePaths(resources).empty());
  }
  ```

  `FindSfntFilesByMagic` recursively inspects regular files for TrueType,
  OpenType, and collection magic (`0x00010000`, `OTTO`, `true`, `ttcf`) in
  addition to case-insensitive `.ttf/.otf/.ttc/.otc` suffixes, so renaming an
  unmanifested font does not bypass this assertion.

- [ ] **Step 8: Add the failing deterministic notice test**

  ```cpp
  TEST_CASE("notice generation is sorted and leaves source template unchanged") {
      auto fixture = MakeNoticeFixtureInReverseFontOrder();
      const std::string sourceBefore = ReadFile(fixture.baseTemplate);
      molga::text::TextNoticeResult result;
      REQUIRE(molga::text::TextNoticeGenerator::Generate(
          fixture.baseTemplate, fixture.entries, fixture.output,
          result, fixture.diagnostics));
      CHECK(ReadFile(fixture.baseTemplate) == sourceBefore);
      CHECK(IndexOf(ReadFile(fixture.output), "font-a") <
            IndexOf(ReadFile(fixture.output), "font-b"));
      CHECK(result.sha256 == molga::Sha256File(fixture.output));
  }
  ```

- [ ] **Step 9: Run the red closure/selection/notice gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package test_game_builder \
    test_font_assets test_asset_catalog -j
  ```

  Expected: compilation fails on `FontPackageClosure`, packaged artifact
  authority/store construction, filtered asset selection, and
  `TextNoticeGenerator`; the current recursive `CopyAssets` would also expose
  `unused.otf` and no sealed repository can validate a manifest authority.

- [ ] **Step 10: Define closure entries and DFS validation**

  ```cpp
  struct FontPackageClosureEntry {
      std::string familyGuid;
      TextRuntimeFontEntry runtime;
      VerifiedFontArtifact projectArtifact;
      std::filesystem::path licenseAbsolutePath;
  };
  struct FontPackageClosure {
      std::vector<FontPackageClosureEntry> fonts;
  };
  std::optional<FontPackageClosure> BuildFontPackageClosure(
      const std::vector<std::string>& rootFamilyGuids,
      TextDiagnosticSink&);
  ```

  Traverse authored fallback graphs with a white/gray/black state map to
  diagnose cycles and missing GUIDs. Validate every face via the project
  `FontRepository` and retain its complete `VerifiedFontArtifact`; require
  `ProjectLibrary`, exact
  `Library/Imported/Fonts/<artifactSha256>.sfnt`, equal source/artifact SHA,
  recorded size, face index, `redistributableConfirmed`, nonempty license GUID,
  existing license bytes, and recorded license SHA. Reopen the artifact through
  `FontArtifactStore::ForProject(projectRoot).ReadVerified`; no closure code
  calls `AbsoluteSourcePath` or opens the authored source. Include all reachable
  faces even when coverage used only the root face. Sort final entries by
  `{fontGuid,sourceSha256,faceIndex}` independent of DFS order.

- [ ] **Step 11: Run only the closure cases**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package -j
  build/debug/tests/test_text_package --test-case="*closure*"
  ```

  Expected: valid fallback-only faces appear once; cycle/missing/unconfirmed cases fail with full typed fields.

- [ ] **Step 12: Define the notice records and generator signature**

  ```cpp
  struct TextNoticeEntry {
      std::string fontGuid;
      std::string sourceSha256;
      std::string licenseAssetGuid;
      std::string licenseKind;
      std::string copyright;
      std::filesystem::path verifiedLicensePath;
      std::string verifiedLicenseSha256;
  };
  struct TextNoticeResult { std::string sha256; };
  class TextNoticeGenerator {
  public:
      static bool Generate(const std::filesystem::path& verifiedBaseTemplate,
                           std::vector<TextNoticeEntry>,
                           const std::filesystem::path& output,
                           TextNoticeResult&, TextDiagnosticSink&);
  };
  ```

  Keep input entries by value so sorting cannot mutate the closure or source
  metadata. The generator never accepts raw unverified notice text.

- [ ] **Step 13: Verify notice inputs before rendering**

  Reopen and hash the Task 1 base template, require its portable-contract
  identity, then reopen every dependency/font license and require its recorded
  SHA. Reject an unsafe path, duplicate `{fontGuid,sourceSha256,licenseAssetGuid}`
  tuple, missing copyright/kind, or mismatch before creating the output temp.

- [ ] **Step 14: Render and hash the deterministic notice bytes**

  Sort entries by `{fontGuid,sourceSha256,licenseAssetGuid}`, emit the fixed
  heading and complete verified license bytes with LF normalization defined by
  Task 1, write a sibling temporary output, reopen it, and return its SHA-256.
  Never write the source template or final package path in this helper.

- [ ] **Step 15: Replace recursive asset copying with a filtered selection**

  Add:

  ```cpp
  struct PackageAssetSelection {
      std::vector<AssetRecord> genericCatalogRecords;
      std::vector<std::string> deferredFontGuids;
      std::vector<std::string> deferredLicenseGuids;
  };
  bool BuildPackageAssetSelection(const AssetDatabase&,
                                  PackageAssetSelection&,
                                  TextDiagnosticSink&);
  bool CopyAssets(const PackageAssetSelection&,
                  const std::filesystem::path& resourceRoot,
                  TextDiagnosticSink&);
  ```

  Enumerate canonical `AssetDatabase` records rather than recursively copying
  `Assets/`. First put every `FontImporter` GUID in `deferredFontGuids`; read its
  imported `licenseAssetGuid`, and, when that nonempty GUID resolves exactly one
  generic `AssetRecord`, put it in `deferredLicenseGuids` regardless of
  importer. Empty/unresolved license metadata may emit a nonblocking project
  diagnostic here but does not fail solely because the font is unreachable;
  `BuildFontPackageClosure` fails it if a reachable family selects that font.
  Do not copy deferred sources or `.meta` files and do not emit them in the
  generic catalog. Copy every other selected source through its validated
  catalog-relative path. Any package-essential non-catalog input must use an
  explicit GameBuilder helper, never fall through a directory walk. This makes
  an unused or even unregistered font file impossible to ship accidentally.

  Reorder GameBuilder to validate coverage/build the reachable font closure,
  compute `PackageAssetSelection`, and only then call the filtered `CopyAssets`.
  Remove the old recursive call entirely; no pre-closure operation may populate
  `Contents/Resources/Assets`.

- [ ] **Step 16: Derive one content-addressed destination per artifact**

  Add:

  ```cpp
  std::optional<std::filesystem::path> PackagedFontArtifactPath(
      std::string_view artifactSha256,
      std::string& errorOut);
  ```

  Require lowercase 64-hex and return exactly
  `Assets/Fonts/<artifactSha256>.sfnt`. Resolve that relative path beneath the
  canonical `resourceRoot`, reject symlinks/escape, and never derive a package
  path from `AssetRecord::sourcePath`, a filename, GUID, or project
  `Library/Imported` locator.

- [ ] **Step 17: Reject destination collisions before staging**

  Add `ValidateFontPackageDestinations(const FontPackageClosure&,
  const std::filesystem::path& resourceRoot, TextDiagnosticSink&)`. Build a map
  from content-addressed destination to complete
  `{fontGuid,faceIndex,artifactSha256,byteSize,projectLocator}`. Repeated graph
  visits to the exact same face identity deduplicate to one manifest/catalog
  entry and destination. A second distinct face identity that maps to an
  already-owned path is `PackageValidationFailed` in this schema rather than
  duplicating the manifest locator. No read/copy begins until the whole map
  validates, and publication contains one file per accepted artifact SHA.

- [ ] **Step 18: Copy verified project artifacts to sibling temporary files**

  For each unique collision-free project artifact, call only
  `FontArtifactStore::ForProject(projectRoot).ReadVerified`, write those
  immutable bytes to a unique sibling temporary beside
  `Contents/Resources/Assets/Fonts/<artifactSha256>.sfnt`, reopen, and require
  byte size plus source/artifact SHA equality. Delete all caller-owned
  temporaries and fail on the first mismatch. The authored source path and
  `Library/Imported` directory are never copied.

- [ ] **Step 19: Copy verified licenses to sibling temporary files**

  Derive `Licenses/Fonts/<fontGuid>-<licenseAssetGuid>.<ext>`, reject a duplicate destination, copy to a sibling temporary file, and reopen/verify `licenseSha256`. Do not publish a font or license yet.

- [ ] **Step 20: Generate the final notice as a temporary output**

  Call `TextNoticeGenerator` with the verified base template and sorted closure entries, targeting a sibling temporary path for `Licenses/ThirdPartyNotices.md`. Verify the returned SHA against reopened bytes and retain the source template unchanged.

- [ ] **Step 21: Publish the closure files with a reverse journal**

  After every font, license, and notice temporary validates, rename them to
  final destinations in sorted relative-path order. Maintain a reverse journal;
  if rename `N` fails, remove new outputs in reverse and restore prior bytes.

- [ ] **Step 22: Rewrite reachable font artifact locators in the package catalog**

  Only after file publication succeeds, clone exactly the reachable
  `FontImporter` records into the filtered package catalog. Preserve GUID,
  authored provenance `sourcePath`, source/artifact SHA, byte size, metrics,
  metadata, importer version, face/license data, and diagnostics; rewrite only
  `fontArtifact.locator` from
  `{ProjectLibrary,Library/Imported/Fonts/<sha>.sfnt}` to
  `{PackagedResource,Assets/Fonts/<sha>.sfnt}`. For a sealed catalog,
  `AssetDatabase::AbsoluteSourcePath(fontGuid)` returns empty and
  `FontRepository` is the sole font-byte consumer. Do not add the generic
  records resolved from `licenseAssetGuid`: their verified bytes live only at
  manifest-owned `Licenses/Fonts/...` paths. No font/license `.meta` or
  `Library/Imported` file is packaged. Set each matching
  `TextRuntimeManifest.fonts[].sourcePath` to that exact packaged locator and
  `sourceSha256` to the equal artifact SHA. Return no catalog/runtime entry
  until the file transaction and filtered-record assembly both succeed; a
  record-assembly failure invokes the same reverse journal.

- [ ] **Step 23: Audit the completed resource tree for unmanifested SFNT**

  Immediately before PackageFinalizer, recursively scan the staged resource
  tree using suffix plus file-magic detection. Canonicalize every discovered
  path beneath `RuntimeResourceRoot`; require the sorted list to equal the
  sorted unique `TextRuntimeManifest.fonts[].sourcePath` list byte-for-byte.
  Require every path to match `Assets/Fonts/<lowercase-64-hex>.sfnt`, resolve to
  exactly one regular non-symlink file, match `sourceSha256`, and equal the
  matching packaged catalog's `PackagedResource` locator/hash. Reject any
  authored-font source path or `Library/Imported` directory even if its bytes
  are also manifested. Any missing, duplicate, renamed, or extra SFNT is a
  package blocker.

- [ ] **Step 24: Run the green closure/selection/notice gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package test_game_builder \
    test_font_assets test_asset_catalog -j
  ctest --test-dir build/debug \
    -R '^(test_text_package|test_game_builder|test_font_assets|test_asset_catalog)$' \
    --output-on-failure
  ```

  Expected: closure/notice tests pass, source notice remains unchanged,
  fallback-only font/license files are staged once from immutable project
  artifacts, the unused unconfirmed font and catalog record are absent,
  packaged repository loads use matching catalog/manifest authority with zero
  source opens, and disk SFNT paths equal packaged locators 1:1.

- [ ] **Step 25: Commit closure, filtered selection, and notice generation**

  ```bash
  git add CMakeLists.txt src/Text/TextNoticeGenerator.h \
    src/Text/TextNoticeGenerator.cpp src/Text/TextPackageValidator.h \
    src/Text/TextPackageValidator.cpp src/Text/TextRuntimeManifest.h \
    src/Text/TextRuntimeManifest.cpp src/Assets/FontArtifactStore.h \
    src/Assets/FontArtifactStore.cpp src/Text/FontRepository.h \
    src/Text/FontRepository.cpp src/Core/AssetDatabase.h \
    src/Core/AssetDatabase.cpp src/Editor/GameBuilder.h \
    src/Editor/GameBuilder.cpp tests/test_text_package.cpp \
    tests/test_game_builder.cpp tests/test_font_assets.cpp \
    tests/test_asset_catalog.cpp
  git commit -m "feat: stage font and license closure"
  ```

### Task 17.4: Seal catalog, coverage, runtime manifest, and game config hashes

**Files:**

- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Text/TextPackageValidator.h`
- Modify: `src/Text/TextPackageValidator.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `tests/test_asset_catalog.cpp`
- Modify: `tests/test_text_package.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_package_finalizer.cpp`

**Interfaces:**

- Consumes: coverage manifest, font/notice closure, portable dependency contract, and Task 16 manifest/config types.
- Produces: asset catalog schema `3`, content SHA-256 records, coverage input
  file hashes, and the non-cyclic sealed manifest chain.

- [ ] **Step 1: Add the failing catalog security-hash test**

  ```cpp
  TEST_CASE("catalog schema three stores content SHA separately from cache hash") {
      const auto fixture = BuildCatalogWithOneFont();
      const auto json = LoadJson(fixture.catalog);
      CHECK(json["schemaVersion"] == 3);
      const auto& record = json["records"][0];
      CHECK(IsSha256(record["contentSha256"].get<std::string>()));
      CHECK(record["hash"].is_string());
      CHECK(record["contentSha256"] != record["hash"]);
      CHECK(record["fontArtifact"]["storage"] == "PackagedResource");
      const auto artifactPath =
          record["fontArtifact"]["relativePath"].get<std::string>();
      CHECK(artifactPath ==
            "Assets/Fonts/" +
                record["fontArtifact"]["artifactSha256"].get<std::string>() +
                ".sfnt");
      CHECK(record["contentSha256"] ==
            record["fontArtifact"]["artifactSha256"]);
  }
  ```

- [ ] **Step 2: Add the failing hash-chain test**

  ```cpp
  TEST_CASE("sealed text hash chain has no self or build-lock edge") {
      const auto bundle = BuildMinimalTextGame().bundle;
      const auto resources = bundle / "Contents/Resources";
      const auto game = LoadJson(resources / "game.json");
      const auto runtime = LoadJson(resources / "Manifests/text_runtime.json");
      CHECK(game["text"]["manifestSha256"] ==
            molga::Sha256File(resources / "Manifests/text_runtime.json"));
      CHECK(runtime["assetCatalogPath"] == "asset_catalog.json");
      CHECK(runtime["assetCatalogSha256"] ==
            molga::Sha256File(resources / "asset_catalog.json"));
      CHECK(runtime["coverageManifestSha256"] ==
            molga::Sha256File(resources /
              runtime["coverageManifestPath"].get<std::string>()));
      CHECK(runtime["dependencyLockSha256"] ==
            molga::Sha256File(resources /
              "Engine/Text/text_dependency_contract.json"));
      CHECK(runtime.dump().find("game.json") == std::string::npos);
      CHECK(runtime.dump().find("text_dependency_build_lock") ==
            std::string::npos);
  }
  ```

- [ ] **Step 3: Run the red sealing gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_asset_catalog \
    test_text_package test_game_builder test_package_finalizer -j
  ```

  Expected: catalog schema and sealed hash assertions fail.

- [ ] **Step 4: Write catalog content SHA independently**

  Set catalog schema `3`; compute `contentSha256` independently of existing
  revision/FNV `hash`. For an ordinary record, hash its staged `sourcePath`.
  For `FontImporter`, hash only its staged
  `fontArtifact.locator.relativePath`, require `PackagedResource`, and require
  equality with both source/artifact SHA fields. The authored font
  `sourcePath` remains provenance and is never opened in a sealed catalog.

  ```cpp
  recordJson["hash"] = record.hash; // cache/revision only
  recordJson["contentSha256"] = record.contentSha256; // security
  ```

- [ ] **Step 5: Read legacy and schema-three catalog records**

  Schema 1/2 loads with an empty content SHA and is accepted only as a legacy
  development catalog. Schema 3 requires a valid lowercase SHA-256 for each
  record and the complete Task 4.1 `VerifiedFontArtifact` fields for every
  font. Construct the sealed store only from the already verified manifest
  authorities, bind it once, and then call the exact Task 4.1 API:

  ```cpp
  auto sealedStore = FontArtifactStore::ForSealedPackage(
      runtimeResourceRoot, PackagedFontAuthorities(runtimeManifest), sink);
  REQUIRE(sealedStore.has_value());
  REQUIRE(database.BindFontArtifactStore(
      std::make_shared<const FontArtifactStore>(std::move(*sealedStore)),
      &error));
  REQUIRE(database.LoadCatalog(catalogPath, runtimeResourceRoot,
      AssetCatalogMode::SealedPackage, &error));
  ```

  `LoadCatalog(catalogPath, storageRoot, mode, errorOut)` rejects a font unless
  the pre-bound store mode and record locator agree. Sealed mode requires
  `PackagedResource` with a path below `Assets/`; project scan/save requires a
  pre-bound project store plus `ProjectLibrary` and exact
  `Library/Imported/Fonts/<artifactSha256>.sfnt`. Never add a narrower overload
  or let catalog loading construct a store. Preserve the existing `hash`
  string exactly on every read/write path.

- [ ] **Step 6: Recompute staged catalog security hashes**

  Before GameBuilder publishes a catalog, open each ordinary staged source and
  each font's packaged artifact locator through the canonical resource root,
  recompute SHA-256, and write schema 3. Assert the font catalog locator equals
  the matching runtime-manifest `fonts[].sourcePath` and both hashes equal its
  `sourceSha256`. A missing file, authored-font-source open, `Library/Imported`
  path, symlink escape, destination collision, or digest disagreement fails the
  build; never copy a legacy empty `contentSha256` into a sealed catalog.

- [ ] **Step 7: Finalize and hash the asset catalog first**

  Serialize the complete schema-3 catalog only after generic assets and the
  reachable font closure are staged. Write `asset_catalog.json` to a sibling
  temporary file, reopen it, require supported schema and byte-for-byte
  canonical form, verify every ordinary record source and every font's
  `PackagedResource` artifact locator/`contentSha256` against final staged
  bytes, atomically publish it, and compute its SHA-256. Freeze both its bytes
  and digest; no later package step may add or rewrite a catalog record.

- [ ] **Step 8: Bind, write, and hash the coverage manifest second**

  After all scene, prefab, localization, and
  `Manifests/text_required_fixtures.json` bytes are final, call
  `BindCoverageInputFiles` against `resourceRoot`. Serialize
  `Manifests/text_coverage.json` canonically, write to a sibling temporary
  file, re-read/parse/compare canonical bytes (including sorted `inputFiles`),
  atomically publish, then compute SHA. Do not mutate any contributing input or
  the coverage manifest after this point.

- [ ] **Step 9: Finalize the runtime manifest third**

  Fill `assetCatalogPath="asset_catalog.json"` and its frozen SHA, the coverage
  path/SHA, portable contract SHA, pinned dependencies/archive/license hashes,
  ICU/rasterizer entries, sorted font entries whose `sourcePath` equals the
  sealed catalog's packaged artifact locator and whose `sourceSha256` equals
  its verified artifact SHA, and final notice path/SHA. Reject duplicate font
  locators before serialization.
  Serialize `Manifests/text_runtime.json` canonically, re-read/compare, publish,
  and compute its SHA. The runtime manifest points to catalog and coverage; the
  catalog and coverage never point back to it.

- [ ] **Step 10: Write game config last**

  Add only `{path:"Manifests/text_runtime.json",manifestSha256:<sha>}` to `game.json.text`, then write `game.json`. `PackageLayout` verifies the chain in that order and rejects missing, extra build-lock, unsafe path, mismatch, or symlink escape.

- [ ] **Step 11: Run the green sealing gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_asset_catalog \
    test_text_package test_game_builder test_package_finalizer -j
  ctest --test-dir build/debug -R '^(test_asset_catalog|test_text_package|test_game_builder|test_package_finalizer)$' --output-on-failure
  ```

  Expected: all four pass; catalog v1/v2 remains readable for development,
  while sealed output is schema 3 and follows the immutable acyclic finalization
  order `asset_catalog.json` -> `text_coverage.json` -> `text_runtime.json` ->
  `game.json`.

- [ ] **Step 12: Commit sealed manifests**

  ```bash
  git add src/Core/AssetDatabase.h src/Core/AssetDatabase.cpp \
    src/Text/TextPackageValidator.h src/Text/TextPackageValidator.cpp \
    src/Editor/GameBuilder.cpp src/Core/PackageLayout.cpp \
    tests/test_asset_catalog.cpp tests/test_text_package.cpp \
    tests/test_game_builder.cpp tests/test_package_finalizer.cpp
  git commit -m "feat: seal packaged text manifests"
  ```

### Task 17.5: Enforce the terminal-action matrix and pre-window exit 4

**Files:**

- Create: `src/Core/RuntimeStartup.h`
- Create: `src/Core/RuntimeStartup.cpp`
- Create: `tests/test_runtime_startup.cpp`
- Create: `tests/smoke/text_network_deny.sb`
- Create: `tests/smoke/test_text_network_deny.cmake`
- Create: `tests/smoke/text_sandbox_capability_probe.mm`
- Modify: `src/Text/TextPackageValidator.h`
- Modify: `src/Text/TextPackageValidator.cpp`
- Modify: `src/Core/SmokeReport.h`
- Modify: `src/Core/SmokeReport.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_text_package.cpp`
- Modify: `tests/test_runtime_smoke.cpp`
- Modify: `tests/smoke/create_fixture.cmake`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: sealed hash chain, typed diagnostics, runtime dependency initializer, and all design Section 11 conditions.
- Produces: `TextFailureDecision`, `ValidateSealedPackage`, callback-injected
  `RunRuntimeStartup`, uniquely owned `RuntimeTextLifetimeOwner` carrying the
  move-only terminal guard, stable SmokeReport
  failure fields, a self-tested committed deny-network profile, recursive
  Mach-O audit, and exit `4` before SDL.

- [ ] **Step 1: Add the complete failing terminal-policy table**

  In `tests/test_text_package.cpp`:

  ```cpp
  TEST_CASE("Section 11 terminal actions are exact") {
      using C = molga::text::TextFailureCondition;
      using M = molga::text::TextExecutionMode;
      using A = molga::text::TextFailureAction;
      const std::vector<Row> rows = {
          {C::DependencyInvalid, M::EditorPreview, A::BlockGameViewText, 0},
          {C::DependencyInvalid, M::DevelopmentRuntime, A::ExitProcess, 4},
          {C::DependencyInvalid, M::PackageBuild, A::FailBuild, 0},
          {C::DependencyInvalid, M::CopiedPackage, A::ExitProcess, 4},
          {C::FontHotReloadInvalid, M::EditorPreview, A::UseLastGoodOrTofu, 0},
          {C::FontHotReloadInvalid, M::DevelopmentRuntime, A::UseTofuContinue, 0},
          {C::FontHotReloadInvalid, M::PackageBuild, A::FailIfReachable, 0},
          {C::FontHotReloadInvalid, M::CopiedPackage, A::ExitProcess, 4},
          {C::AuthoredUtf8Invalid, M::EditorPreview, A::ReplacementBlocker, 0},
          {C::AuthoredUtf8Invalid, M::DevelopmentRuntime, A::ReplacementContinue, 0},
          {C::AuthoredUtf8Invalid, M::PackageBuild, A::FailBuild, 0},
          {C::AuthoredUtf8Invalid, M::CopiedPackage, A::ExitProcess, 4},
          {C::ReachableMissingGlyph, M::EditorPreview, A::UseTofuBlocker, 0},
          {C::ReachableMissingGlyph, M::DevelopmentRuntime, A::UseTofuContinue, 0},
          {C::ReachableMissingGlyph, M::PackageBuild, A::FailBuild, 0},
          {C::ReachableMissingGlyph, M::CopiedPackage, A::ExitProcess, 4},
          {C::DynamicTextInvalid, M::DevelopmentRuntime, A::ReplacementOrTofuContinue, 0},
          {C::DynamicTextInvalid, M::CopiedPackage, A::ReplacementOrTofuContinue, 0},
          {C::FamilyOrLicenseInvalid, M::EditorPreview, A::DisableFamilyBlocker, 0},
          {C::FamilyOrLicenseInvalid, M::DevelopmentRuntime, A::FiniteFallbackContinue, 0},
          {C::FamilyOrLicenseInvalid, M::PackageBuild, A::FailBuild, 0},
          {C::FamilyOrLicenseInvalid, M::CopiedPackage, A::ExitProcess, 4},
          {C::AuthoredReferenceOrLayoutInvalid, M::EditorPreview, A::AuthoredFallbackBlocker, 0},
          {C::AuthoredReferenceOrLayoutInvalid, M::DevelopmentRuntime, A::AuthoredFallbackContinue, 0},
          {C::AuthoredReferenceOrLayoutInvalid, M::PackageBuild, A::FailBuild, 0},
          {C::AuthoredReferenceOrLayoutInvalid, M::CopiedPackage, A::ExitProcess, 4},
          {C::RuntimeReferenceOrLayoutInvalid, M::EditorPreview, A::DisableFeatureContinue, 0},
          {C::RuntimeReferenceOrLayoutInvalid, M::DevelopmentRuntime, A::DisableFeatureContinue, 0},
          {C::RuntimeReferenceOrLayoutInvalid, M::CopiedPackage, A::DisableFeatureContinue, 0},
          {C::AtlasExhausted, M::EditorPreview, A::UseTofuContinue, 0},
          {C::AtlasExhausted, M::DevelopmentRuntime, A::UseTofuContinue, 0},
          {C::AtlasExhausted, M::PackageBuild, A::FailStressGate, 0},
          {C::AtlasExhausted, M::CopiedPackage, A::UseTofuContinue, 0},
          {C::SecondCallbackReflow, M::EditorPreview, A::DeferContinue, 0},
          {C::SecondCallbackReflow, M::DevelopmentRuntime, A::DeferContinue, 0},
          {C::SecondCallbackReflow, M::PackageBuild, A::FailStressGate, 0},
          {C::SecondCallbackReflow, M::CopiedPackage, A::DeferContinue, 0},
      };
      for (const Row& row : rows) {
          CAPTURE(static_cast<int>(row.condition), static_cast<int>(row.mode));
          CHECK(molga::text::DecideTextFailure(row.condition, row.mode) ==
                molga::text::TextFailureDecision{row.action, row.exitCode});
      }
  }
  ```

- [ ] **Step 2: Add the failing upstream-hashed-file tamper table**

  ```cpp
  TEST_CASE("every upstream-hashed text file tamper fails with exit four") {
      for (const auto& path : {"Manifests/text_runtime.json",
              "asset_catalog.json",
              "Manifests/text_coverage.json",
              "Scenes/main.json", "Assets/dialog.prefab",
              "Assets/ui.localization",
              "Manifests/text_required_fixtures.json",
              "Engine/Text/text_dependency_contract.json",
              "Engine/Text/icudt78l.dat", "Licenses/ThirdPartyNotices.md",
              "Licenses/HarfBuzz.txt", "Licenses/ICU.txt",
              "Licenses/StbTrueType.txt",
              "Licenses/Fonts/primary-OFL.txt"}) {
          auto package = CopyValidSealedPackage();
          FlipOneByte(package.resources / path);
          const auto result = molga::text::TextPackageValidator::
              ValidateSealedPackage(package.resources);
          CAPTURE(path);
          CHECK_FALSE(result.ok);
          CHECK(result.exitCode == 4);
          CHECK(result.diagnostic.code ==
                molga::text::TextDiagnosticCode::PackageValidationFailed);
          CHECK(result.failedPath == path);
          CHECK_FALSE(result.actualSha256.empty());
          CHECK_FALSE(result.expectedSha256.empty());
          CHECK_FALSE(result.diagnostic.remediation.empty());
      }

      auto fontPackage = CopyValidSealedPackage();
      const auto fontPath = LoadTextRuntimeManifest(fontPackage.resources)
                                .fonts.at(0).sourcePath;
      REQUIRE(fontPath.rfind("Assets/Fonts/", 0) == 0);
      FlipOneByte(fontPackage.resources / fontPath);
      const auto fontResult = molga::text::TextPackageValidator::
          ValidateSealedPackage(fontPackage.resources);
      CHECK_FALSE(fontResult.ok);
      CHECK(fontResult.exitCode == 4);
      CHECK(fontResult.failedPath == fontPath);
      CHECK_FALSE(fontResult.expectedSha256.empty());
      CHECK_FALSE(fontResult.actualSha256.empty());
  }
  ```

  `CopyValidSealedPackage` uses the same minimal package as Task 17.2's staged
  input test and therefore contains all four listed coverage contributors; it
  asserts each path exists before the loop so a missing fixture cannot become
  a false-positive tamper result.

- [ ] **Step 3: Add the failing unmanifested-SFNT startup test**

  ```cpp
  TEST_CASE("renamed unmanifested SFNT exits four before startup") {
      auto package = CopyValidSealedPackage();
      const auto extra = package.resources / "Assets/cache.bin";
      WriteBytes(extra, std::string("OTTO\0\x01\0\0", 8));
      const auto result = molga::text::TextPackageValidator::
          ValidateSealedPackage(package.resources);
      CHECK_FALSE(result.ok);
      CHECK(result.exitCode == 4);
      CHECK(result.failedPath == "Assets/cache.bin");
      CHECK(result.expectedSha256.empty());
      CHECK(result.actualSha256.empty());
      CHECK(result.diagnostic.code ==
            molga::text::TextDiagnosticCode::PackageValidationFailed);
  }
  ```

- [ ] **Step 4: Add failing `game.json` root-link cases**

  `game.json` is the hash-chain root, so it has no in-package expected SHA. Test its canonical parse/schema contract and its two security-critical text-link fields separately:

  ```cpp
  TEST_CASE("sealed game root rejects noncanonical encoding and altered text link") {
      SUBCASE("valid JSON with noncanonical trailing newline") {
          auto package = CopyValidSealedPackage();
          const auto path = package.resources / "game.json";
          WriteBytes(path, ReadBytes(path) + "\n");
          const auto result = molga::text::TextPackageValidator::
              ValidateSealedPackage(package.resources);
          CHECK_FALSE(result.ok);
          CHECK(result.exitCode == 4);
          CHECK(result.failedPath == "game.json");
          CHECK(result.expectedSha256.empty());
          CHECK(result.actualSha256.empty());
      }

      SUBCASE("text path is not the canonical manifest path") {
          auto package = CopyValidSealedPackage();
          auto game = LoadJson(package.resources / "game.json");
          game["text"]["path"] = "Manifests/other.json";
          WriteCanonicalJson(package.resources / "game.json", game);
          const auto result = molga::text::TextPackageValidator::
              ValidateSealedPackage(package.resources);
          CHECK_FALSE(result.ok);
          CHECK(result.exitCode == 4);
          CHECK(result.failedPath == "game.json");
          CHECK(result.expectedSha256.empty());
          CHECK(result.actualSha256.empty());
      }

      SUBCASE("text manifest SHA no longer names the manifest bytes") {
          auto package = CopyValidSealedPackage();
          auto game = LoadJson(package.resources / "game.json");
          game["text"]["manifestSha256"] = std::string(64u, '0');
          WriteCanonicalJson(package.resources / "game.json", game);
          const auto result = molga::text::TextPackageValidator::
              ValidateSealedPackage(package.resources);
          CHECK_FALSE(result.ok);
          CHECK(result.exitCode == 4);
          CHECK(result.failedPath == "Manifests/text_runtime.json");
          CHECK(result.expectedSha256 == std::string(64u, '0'));
          CHECK(result.actualSha256 == Sha256File(
              package.resources / "Manifests/text_runtime.json"));
      }
  }
  ```

  Do not add a test or product claim that an attacker can never change an unrelated, schema-valid `game.json` field: that needs a signature or digest embedded outside `game.json`, neither of which this milestone defines.

- [ ] **Step 5: Add the failing callback-injected startup-order test**

  Create `tests/test_runtime_startup.cpp` and register
  `molga_add_text_test(test_runtime_startup test_runtime_startup.cpp)`. The
  fixture supplies callbacks only; it does not call `runtime_main` or SDL:

  ```cpp
  TEST_CASE("packaged startup stops before engine on sealed validation failure") {
      RuntimeStartupFixture f = RuntimeStartupFixture::Packaged();
      f.audit.push_back("parse-cli");
      f.validation = FailedSealedValidation("Assets/Fonts/bad.sfnt");
      CHECK(molga::RunRuntimeStartup(
          f.request, f.callbacks, f.Diagnostics(), &f.audit) == 4);
      CHECK(f.audit == std::vector<std::string>{
          "parse-cli", "InitFromExecutable",
          "InitializePackagedRuntimeRoot", "ValidateSealedPackage",
          "write-package-failure"});
      CHECK(f.engineInitCalls == 0u);
      CHECK(f.sdlInitCalls == 0u);
  }

  TEST_CASE("development startup skips sealed validator") {
      RuntimeStartupFixture f =
          RuntimeStartupFixture::DevelopmentWithExistingAbsoluteResourceRoot();
      f.audit.push_back("parse-cli");
      CHECK(molga::RunRuntimeStartup(
          f.request, f.callbacks, f.Diagnostics(), &f.audit) == 0);
      CHECK(f.audit == std::vector<std::string>{
          "parse-cli", "InitFromExecutable",
          "SetDevelopmentRuntimeResourceRoot",
          "TextRuntimeLifetimeGuard::Create", "EngineInit",
          "EngineShutdown::Complete", "engine-text-owner-destroyed",
          "text-lifetime-destroyed"});
      CHECK(f.validateCalls == 0u);
  }
  ```

- [ ] **Step 6: Add the failing deny-network profile self-test**

  Create `tests/smoke/text_sandbox_capability_probe.mm` now with a
  production-shaped red `main(int,char**)` that accepts no success mode yet and
  returns nonzero. Before `add_subdirectory(tests)`, register its final target
  and framework/C++17 ownership so the CTest generator expression and Step 7
  red gate name a real target:

  ```cmake
  if(APPLE)
    enable_language(OBJCXX)
    find_library(MOLGA_SANDBOX_COCOA Cocoa REQUIRED)
    find_library(MOLGA_SANDBOX_CARBON Carbon REQUIRED)
    find_library(MOLGA_SANDBOX_COREGRAPHICS CoreGraphics REQUIRED)
    find_library(MOLGA_SANDBOX_METAL Metal REQUIRED)
    add_executable(text_sandbox_capability_probe
      tests/smoke/text_sandbox_capability_probe.mm)
    set_target_properties(text_sandbox_capability_probe PROPERTIES
      OBJCXX_STANDARD 17
      OBJCXX_STANDARD_REQUIRED YES
      OBJCXX_EXTENSIONS NO)
    target_link_libraries(text_sandbox_capability_probe PRIVATE
      SDL3::SDL3 molga_warnings
      "${MOLGA_SANDBOX_COCOA}" "${MOLGA_SANDBOX_CARBON}"
      "${MOLGA_SANDBOX_COREGRAPHICS}" "${MOLGA_SANDBOX_METAL}")
  endif()
  ```

  Step 26 completes this already registered source; it does not add a second
  target. Create `tests/smoke/test_text_network_deny.cmake` and register this
  macOS-only CTest:

  ```cmake
  if(APPLE)
    add_test(NAME test_text_network_deny_profile
      COMMAND "${CMAKE_COMMAND}"
        "-DPROFILE=${CMAKE_SOURCE_DIR}/tests/smoke/text_network_deny.sb"
        "-DWORK_ROOT=${CMAKE_BINARY_DIR}/text-network-deny-self-test"
        "-DPROBE=$<TARGET_FILE:text_sandbox_capability_probe>"
        -P "${CMAKE_SOURCE_DIR}/tests/smoke/test_text_network_deny.cmake")
  endif()
  ```

  `DevelopmentWithExistingAbsoluteResourceRoot()` creates its canonical
  temporary resource directory and explicitly sets
  `RuntimeTextResourceAuthority::DevelopmentProject` before constructing the
  fixture; `Packaged()` explicitly sets `PackagedSealed`. No test calls
  a member on an object whose lifetime has not begun. Its injected successful
  acquisition returns a `RecordingRuntimeTextLifetimeOwner`; `runEngine`
  receives the fixture's one diagnostic sink, creates a recording host, releases
  its known external owner, and must obtain `EngineShutdown::Complete` before
  appending `engine-text-owner-destroyed`. Only then may the startup lifetime
  owner's destructor append `text-lifetime-destroyed`. Add a second row whose
  engine body throws after host creation: the callback catches and records the
  exception as status `4`, completes shutdown on that same live host, and then
  returns `4`; it never unwinds the host. Both destruction labels still occur
  in order and no process-exit API is used. Add retry rows for
  `GpuDrainFailed` and `ExternalGpuLifetime`: release only the fixture's known
  external owner, retain the host/sink/startup owner, and require a later
  `Complete` before the fixture scope can exit. The persistent-failure audit
  reaches a shutdown-blocked marker with no ordinary return or destructor
  marker; the test harness then clears only its injected fault and drives the
  same host to `Complete` before teardown.

  The self-test must require `/usr/bin/sandbox-exec`, run `/usr/bin/true`
  successfully, and write one file beneath `WORK_ROOT`. Then pass this exact
  bracket-quoted program as the single argument after `/usr/bin/python3 -c`:

  ```python
  import errno
  import socket
  import sys

  probe = socket.socket()
  try:
      probe.bind(("127.0.0.1", 0))
  except OSError as error:
      sys.exit(77 if error.errno in (errno.EPERM, errno.EACCES) else 78)
  sys.exit(0)
  ```

  Invoke that vector through the exact parameterized profile and require status
  `77`. Status `0` is a network-policy hole; `78` or any other status is a probe
  failure, not proof of denial. This remains locale-independent and never
  contacts a remote endpoint: socket creation is allowed, while the loopback
  bind must be denied. Also attempt one write outside `WORK_ROOT` and require
  `EPERM`/`EACCES` through the same stable-status convention.

  Build `text_sandbox_capability_probe` only on Apple from the committed
  Objective-C++ source. Its `--hidden-sdl-gpu --output <inside-WORK_ROOT>` mode
  initializes SDL video, creates a hidden window and Metal SDL_GPU device,
  submits one empty frame/fence, shuts down, and writes a canonical success
  record. Its `--visible-cocoa-input-window --output <inside-WORK_ROOT>` mode
  creates and shows one Cocoa window, pumps one event cycle, reads the current
  TIS input-source ID/language, finds the same PID-owned layer-zero window via
  `CGWindowListCopyWindowInfo`, closes it, and writes canonical
  `{pid,windowId,inputSourceId,primaryLanguage}`. Run both modes through the
  exact profile and require exit zero/nonempty fields. This proves profile
  capability only; it is not visible IME evidence.

- [ ] **Step 7: Run the red policy/startup gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package \
    test_runtime_startup runtime_smoke text_sandbox_capability_probe -j
  ctest --test-dir build/debug \
    -R '^(test_runtime_startup|test_text_network_deny_profile)$' \
    --output-on-failure
  ```

  Expected: compile fails on policy, sealed-validation, and `RuntimeStartup`
  types; the network-profile CTest fails because the committed profile does not
  exist yet and the probe has no success mode. Neither an unknown target nor an
  invalid `$<TARGET_FILE:...>` expression is an accepted red result.

- [ ] **Step 8: Define the closed terminal-policy enums**

  Add every enum value used by the test plus:

  ```cpp
  struct TextFailureDecision {
      TextFailureAction action;
      int exitCode = 0;
      friend bool operator==(TextFailureDecision lhs,
                             TextFailureDecision rhs) {
          return lhs.action == rhs.action && lhs.exitCode == rhs.exitCode;
      }
  };
  TextFailureDecision DecideTextFailure(TextFailureCondition,
                                        TextExecutionMode) noexcept;
  ```

  Implement it as an exhaustive `switch` over condition and mode; no default silently maps a new condition. A compiler-visible unreachable/fail-closed branch handles impossible pairs.

- [ ] **Step 9: Define the sealed startup result**

  ```cpp
  struct SealedPackageValidationResult {
      bool ok = false;
      int exitCode = 4;
      TextDiagnostic diagnostic;
      std::string failedPath;
      std::string expectedSha256;
      std::string actualSha256;
      std::vector<molga::FontArtifactStore::PackagedAuthority>
          packagedFontAuthorities;
  };
  static SealedPackageValidationResult ValidateSealedPackage(
      const std::filesystem::path& resourceRoot);
  ```

  Leave `expectedSha256` and `actualSha256` empty when no upstream expected
  digest exists. Every failure also leaves `packagedFontAuthorities` empty. A
  successful result carries the sorted unique authority vector constructed in
  Step 14 from the already verified manifest/catalog/disk intersection; no
  caller reparses the manifest to recreate it.

- [ ] **Step 10: Validate the `game.json` root link**

  Resolve `resourceRoot/game.json` without symlink escape, require canonical JSON bytes and the supported game schema, require `text.path == Manifests/text_runtime.json`, then hash that path and compare it with `text.manifestSha256` before parsing the runtime manifest. Malformed/noncanonical JSON and a wrong path report `failedPath=game.json` without fabricated SHA evidence; a manifest-SHA mismatch reports the game-provided expected SHA and actual manifest SHA.

- [ ] **Step 11: Validate the catalog seal and every catalog record**

  From the already hash-validated runtime manifest, require
  `assetCatalogPath == "asset_catalog.json"`, resolve it beneath
  `resourceRoot` without symlink traversal, and compare its bytes with
  `assetCatalogSha256` before parsing. Require schema `3`, byte-for-byte
  canonical JSON, sorted unique records, safe paths, and lowercase
  `contentSha256`. Reopen an ordinary record's staged `sourcePath`; for every
  `FontImporter` require `fontArtifact.locator.storage == PackagedResource`,
  require its safe locator equal one runtime-manifest `fonts[].sourcePath`, and
  reopen only that locator. Compare final bytes with `contentSha256` and both
  artifact/source SHA fields. Reject a packaged font `sourcePath` open,
  `ProjectLibrary`, `Library/Imported`, or missing manifest authority. Return
  the first full typed diagnostic with relative path,
  expected/actual digest, subsystem, object/component context when available,
  and remediation. This entire pass completes before `AssetDatabase` or any
  scene/script loader sees package data.

- [ ] **Step 12: Validate the portable contract and dependency archives**

  From the parsed runtime manifest, verify safe containment and SHA/size for `Engine/Text/text_dependency_contract.json` and `Engine/Text/icudt78l.dat`. Require the HarfBuzz archive vector to contain exactly one logical archive and ICU exactly the sorted `icuuc` and `icui18n` pair. Compare `rasterizerSha256` with the sealed portable contract's logical digest only; never search the package for `stb_truetype.h`, because the header is statically compiled and intentionally absent from the app tree. Separately require `rasterizerLicense.path == Licenses/StbTrueType.txt`, and verify its actual SHA against both the runtime-manifest entry and the portable contract before continuing.

- [ ] **Step 13: Validate notices and license files**

  Verify the final notice path/SHA and every dependency/font license path/SHA in stable manifest order. Reject missing, unsafe, duplicated, or mismatched entries and return the first typed failure with exact relative path, expected/actual SHA where an upstream digest exists, and remediation.

- [ ] **Step 14: Validate manifest-owned font paths and hashes**

  Require font entries sorted and unique by
  `{fontGuid,sourceSha256,faceIndex}` and require each `sourcePath` to match
  exactly `Assets/Fonts/<sourceSha256>.sfnt`. Resolve it beneath the resource
  root and verify SHA; require the matching sealed catalog record's locator to
  be exactly `{PackagedResource,sourcePath}` with equal SHA/size. Construct the
  `FontArtifactStore::PackagedAuthority{relativePath,artifactSha256}` vector
  only from these
  already verified entries. Recursively discover SFNT suffix/magic as in Task
  17.3 and require the disk path set to equal the manifest source-path set
  exactly once; reject authored source locations and `Library/Imported`
  recursively. This pass never loads a font face.

- [ ] **Step 15: Validate coverage and every contributing input file**

  Resolve the single canonical coverage path, compare its actual SHA to the
  runtime manifest, parse it, require supported schema/canonical bytes and the
  portable contract SHA, and reject duplicated or unsorted source IDs. Require
  `inputFiles` sorted and unique by path; for every entry, reject an absolute,
  backslash, dot-segment, coverage/runtime/catalog path, symlink, or containment
  escape, then reopen the final staged bytes and compare them with the entry's
  lowercase SHA-256. This verifies scene, prefab, localization, and required
  fixture/config bytes before any scene or asset load. Do not shape or
  initialize ICU during startup validation.

- [ ] **Step 16: Reject packaged build provenance recursively**

  Walk `Contents/Resources` without following symlinks and reject any filename
  equal to `text_dependency_build_lock.json`. Return its exact relative path
  and remediation with empty expected/actual hashes because no upstream digest
  is needed to prove forbidden provenance exists.

- [ ] **Step 17: Run the focused tamper cases**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package -j
  build/debug/tests/test_text_package --test-case="*upstream-hashed text file tamper*"
  build/debug/tests/test_text_package --test-case="*unmanifested SFNT*"
  build/debug/tests/test_text_package --test-case="*sealed game root*"
  ```

  Expected: each listed file, including `asset_catalog.json` and
  `Scenes/main.json`, fails with code `TEXT_PACKAGE_VALIDATION_FAILED`, exact
  relative path, nonempty expected/actual SHA-256, and exit `4`.

- [ ] **Step 18: Add stable package-failure SmokeReport fields**

  Add `packageValidationCode`, `packageValidationFailedPath`, `packageValidationExpectedSha256`, `packageValidationActualSha256`, and `packageValidationRemediation`. `WritePackageFailureToStderrAndSmokeReport` prints the same stable code/path/remediation; it omits SHA labels when both fields are empty, as for a malformed root `game.json`.

- [ ] **Step 19: Define the callback-injected runtime startup API**

  Create `src/Core/RuntimeStartup.h` with no SDL/renderer include:

  ```cpp
  namespace molga {
  enum class RuntimeStartupMode { Packaged, Development };
  enum class RuntimeTextResourceAuthority : std::uint8_t {
      PackagedSealed,
      DevelopmentProject,
      QualificationSealedLayout,
  };
  struct RuntimeStartupRequest {
      RuntimeStartupMode mode = RuntimeStartupMode::Packaged;
      RuntimeTextResourceAuthority textResourceAuthority =
          RuntimeTextResourceAuthority::PackagedSealed;
      std::filesystem::path argv0;
      std::optional<std::filesystem::path> developmentResourceRoot;
  };
  class RuntimeTextLifetimeOwner {
  public:
      virtual ~RuntimeTextLifetimeOwner() = default;
      RuntimeTextLifetimeOwner(const RuntimeTextLifetimeOwner&) = delete;
      RuntimeTextLifetimeOwner& operator=(
          const RuntimeTextLifetimeOwner&) = delete;
  protected:
      RuntimeTextLifetimeOwner() = default;
  };
  struct RuntimeStartupCallbacks {
      std::function<bool(const std::filesystem::path&, std::string&)>
          initFromExecutable;
      std::function<bool(std::string&)> initializePackagedRuntimeRoot;
      std::function<bool(const std::filesystem::path&, std::string&)>
          setDevelopmentRuntimeResourceRoot;
      std::function<std::filesystem::path()> runtimeResourceRoot;
      std::function<text::SealedPackageValidationResult(
          const std::filesystem::path&)> validateSealedPackage;
      std::function<std::unique_ptr<RuntimeTextLifetimeOwner>(
          const std::filesystem::path&,
          RuntimeTextResourceAuthority,
          std::shared_ptr<const text::SealedPackageValidationResult>,
          text::TextDiagnosticSink&)>
          acquireTextRuntimeLifetime;
      std::function<void(const text::SealedPackageValidationResult&)>
          writePackageFailure;
      std::function<int(text::TextDiagnosticSink&)> runEngine;
  };
  int RunRuntimeStartup(const RuntimeStartupRequest&,
                        RuntimeStartupCallbacks&,
                        text::TextDiagnosticSink&,
                        std::vector<std::string>* callAudit = nullptr);
  RuntimeStartupCallbacks MakeProductionRuntimeStartupCallbacks();
  }
  ```

  Add `src/Core/RuntimeStartup.cpp` to `molga_core`'s explicit source list and
  link `test_runtime_startup` to that same production object/library; do not
  compile a test-only copy of startup logic.

- [ ] **Step 20: Implement fail-closed startup ordering in one runner**

  In `RuntimeStartup.cpp`, append an audit label immediately before each
  callback. The runner forwards its required diagnostic sink by reference to
  the acquisition callback; neither the runner nor a callback creates or owns a
  second sink. Always call `initFromExecutable` first. Packaged mode then calls
  `initializePackagedRuntimeRoot`, obtains the root, validates the sealed
  package, moves the returned value exactly once into a
  `shared_ptr<const SealedPackageValidationResult>`, writes and returns `4` on
  failure, passes `{PackagedSealed,exactResult}` to the text-runtime acquisition
  callback, and only then calls `runEngine(sink)`. Development mode requires one absolute existing
  root and calls `setDevelopmentRuntimeResourceRoot`. Ordinary
  `DevelopmentProject` skips sealed validation and passes
  `{DevelopmentProject,{}}`. The reserved
  `QualificationSealedLayout` branch calls the same validator on that published
  root before any dependency initialization, writes/returns `4` on failure,
  retains the one exact shared result, and passes
  `{QualificationSealedLayout,exactResult}`. Reject every other
  mode/authority combination before a callback: packaged mode permits only
  `PackagedSealed`, and development permits only `DevelopmentProject` or
  `QualificationSealedLayout`. The acquisition callback enforces the identical
  shared-result matrix: sealed authorities require one successful nonnull
  shared result and ordinary development requires an empty one. A missing
  callback, callback exception, false path result, null lifetime result, empty published
  root, or unexpected validation state returns `4`; no later callback runs.

  Store the returned nonnull `std::unique_ptr<RuntimeTextLifetimeOwner>` in a
  local declared before invoking `runEngine(sink)`. Do not move it into the
  engine callback or reset it early. The callback owns every host and
  engine-local text service until `EngineShutdown(sameHost, sameSink)` returns
  `Complete`; only after that may it destroy those services and return its
  saved engine status. A production engine-body exception is caught inside the
  callback owner scope, converted to status `4`, and followed by the identical
  retained-host shutdown loop; it is not rethrown. A non-`Complete` shutdown
  releases only known external snapshot/page owners and retries the same host.
  A persistent failure remains in the fail-closed shutdown-blocked state with
  host, diagnostic sink, resource authority, and terminal guard live: it may
  neither return nor unwind into `RunRuntimeStartup`. The runner returns the
  callback status only after that completed ordering and then destroys its
  lifetime owner. A seam callback that throws before owning a host is caught as
  status `4`, but no exception path may bypass shutdown for a live host. Audit
  acquisition as exactly `TextRuntimeLifetimeGuard::Create` and completed host
  teardown as exactly `EngineShutdown::Complete`.

- [ ] **Step 21: Wire `runtime_main` through the production callbacks**

  `runtime_main.cpp` parses CLI into `RuntimeStartupRequest`, creates
  `MakeProductionRuntimeStartupCallbacks()`, and calls only
  `RunRuntimeStartup`, passing one process-local `LoggerTextDiagnosticSink`
  whose lifetime encloses the runner and all callbacks. The production
  callbacks wrap the exact Task 16
  `PathService` APIs, `ValidateSealedPackage`,
  `TextRuntimeLifetimeGuard::Create`, typed report writer, and the existing
  engine body. The production acquisition returns one private final
  `RuntimeTextLifetimeOwner` implementation whose sole terminal lifetime member
  is the move-only guard; it never calls `TextRuntimeDependencies::Initialize`
  directly. Move the engine body behind the `runEngine` callback without
  changing its order. There is no second inline sealed-validation or dependency
  branch in `runtime_main.cpp`.

  `MakeProductionRuntimeStartupCallbacks` creates one private shared callback
  state captured by the acquisition and `runEngine` closures. That state keeps
  only a `weak_ptr` to the current production text-resource authority; it never
  owns an `AssetDatabase`, store, repository, validation result, or dependency
  client. A successful sealed acquisition creates one authority that retains
  the exact shared successful validation result plus the bound store,
  `AssetDatabase`, `FontRepository`, resolver, primary family GUID, and canonical
  resource root. It stores the weak handoff and gives the sole strong authority
  reference to the private `RuntimeTextLifetimeOwner`. Ordinary development
  authority instead retains only its validated mode/root token.

  `runEngine(sink)` must lock that exact weak reference and reject a
  missing/expired or mode/root-mismatched authority before `EngineInit`; it may
  not reopen a catalog, repeat sealed validation, or obtain authority from a
  singleton/global. The exact callback argument must be the same sink object
  used by the runner and by both `EngineInit` and every `EngineShutdown` retry.
  For the dedicated qualification branch it moves those retained shared
  services and exact validation pointer into Task 18.2's explicit move-only
  `QualificationTextContext`, then passes that context to the canonical driver.
  The driver/context remain live until the same host reports
  `EngineShutdown::Complete`. Declare the owner's terminal guard before its
  strong authority member so reverse member destruction releases the authority
  first and the guard last. Clear the weak handoff only after completed shutdown
  on ordinary return; while shutdown is blocked, retain it and every owner. This
  is the exact acquisition-to-engine handoff; there is no zero-argument callback.

  The parser assigns `PackagedSealed` only to the bundle target,
  `DevelopmentProject` to ordinary `molga_runtime_dev`, and reserves
  `QualificationSealedLayout` for Task 18.2's complete dedicated
  `--text-canonical` argv. A user-supplied authority switch does not exist.

- [ ] **Step 22: Acquire and retain text dependencies only after file validation**

  The production acquisition callback first requires the exact authority
  handoff described in Step 20, forms
  `TextDependencyConfig::FromEngineTextRoot(runtimeRoot / "Engine/Text",
  authority != RuntimeTextResourceAuthority::DevelopmentProject)`, and passes
  that exact config to
  `TextRuntimeLifetimeGuard::Create`.
  Only while that local guard is live, and only for `PackagedSealed` or
  `QualificationSealedLayout` after the full validator has succeeded, construct
  `FontArtifactStore::ForSealedPackage(runtimeRoot,
  sealedValidation->packagedFontAuthorities, sink)`. The sealed validation
  argument must be the nonnull exact shared object supplied by the runner, not a
  copied/reparsed result. Move the returned store into one
  `shared_ptr<const FontArtifactStore>`, bind it exactly once through
  `AssetDatabase::BindFontArtifactStore`, then call only
  `LoadCatalog(catalogPath, runtimeRoot, AssetCatalogMode::SealedPackage,
  &error)`. Ordinary development mode retains its explicit Task 16
  development/project store and catalog authority and never constructs a
  sealed-package store. Its lifetime-acquisition callback therefore does not
  pre-bind a catalog before `runEngine` has selected the exact CLI branch.
  The only reserved exception is Task 18.2's dedicated noninteractive
  `--text-canonical` branch: it may bind a `SealedPackage`-mode store for its
  independently staged, package-byte-equivalent development resource root,
  but only after running this same `ValidateSealedPackage` file/hash validator
  directly on that root, before dependency initialization, and consuming the
  returned authority vector through the exact
  `QualificationSealedLayout` handoff. That
  exception does not call `InitializePackagedRuntimeRoot`, does not change the
  process failure-policy mode, and is unavailable to normal development
  startup. Do not pass an unbound store around a parallel catalog path. Map
  store/bind/catalog/dependency failure to
  `PackageValidationFailed`, write the same typed report, and return `4` before
  `EngineInit`; the still-local guard then performs terminal cleanup.

  On success, move the guard into the private production
  `RuntimeTextLifetimeOwner`. For a sealed authority, move the bound
  database/store/catalog authority, repository/resolver services, primary
  family GUID, canonical root, and the exact shared validation result into that
  owner's strong authority object;
  for ordinary `DevelopmentProject`, store only the validated mode/root token
  and let `runEngine` construct its explicit project store/catalog locally.
  Publish only the corresponding weak callback-state handoff and return that
  owner to `RunRuntimeStartup`.
  Construct worlds, host-owned text/layout services, renderers, and every other
  ICU/HarfBuzz client not already retained by sealed resource authority only
  inside `runEngine(sink)`, after the runner owns that lifetime. Release known
  external frame/snapshot/page owners and drive the same host through
  `EngineShutdown(..., sink) == Complete` before destroying callback-local
  services or returning. An engine-body exception is saved as status `4` inside
  this owner scope and follows the same shutdown path. Neither the callback nor
  a singleton/static retains a client handle after completed shutdown. A sealed run never
  creates `ForProject`, reads a last-good
  source, opens `AssetRecord::sourcePath`, or invokes system/ASCII fallback.

- [ ] **Step 23: Assert packaged-runtime call order**

  In `tests/test_runtime_smoke.cpp`, inject a startup call audit and tampered package. Assert:

  ```cpp
  std::vector<std::string> audit{"parse-cli"};
  CHECK(molga::RunRuntimeStartup(
      tampered.request, tampered.callbacks, tampered.Diagnostics(), &audit) == 4);
  CHECK(audit == std::vector<std::string>{
      "parse-cli", "InitFromExecutable",
      "InitializePackagedRuntimeRoot", "ValidateSealedPackage",
      "write-package-failure"});
  CHECK(std::find(audit.begin(), audit.end(), "EngineInit") == audit.end());
  CHECK(std::find(audit.begin(), audit.end(), "SDL_Init") == audit.end());
  CHECK(std::find(audit.begin(), audit.end(), "load-scene") == audit.end());

  auto passing = RuntimeStartupFixture::Packaged();
  audit = {"parse-cli"};
  CHECK(molga::RunRuntimeStartup(
      passing.request, passing.callbacks, passing.Diagnostics(), &audit) == 0);
  CHECK(audit == std::vector<std::string>{
      "parse-cli", "InitFromExecutable",
      "InitializePackagedRuntimeRoot", "ValidateSealedPackage",
      "TextRuntimeLifetimeGuard::Create", "EngineInit",
      "EngineShutdown::Complete", "engine-text-owner-destroyed",
      "text-lifetime-destroyed"});
  CHECK(passing.AcquiredPackagedAuthorities() ==
        passing.validation.packagedFontAuthorities);
  ```

  The passing fixture also records both the acquisition authority object's
  address and `exactValidation.get()`. It requires `runEngine(sink)` to lock
  that identical authority, receive the fixture's identical sink address, and
  observe the identical validation pointer plus database/store/repository/
  resolver identities. After the runner returns, require the callback state's
  weak handoff and weak service/validation probes to be empty/expired; a copied
  validation result, a reopened service, a strong callback-state reference, or
  a stale handoff fails the test.

  Add an exhaustion/derivation failure case whose audit stops after `InitializePackagedRuntimeRoot`, returns `4`, and contains neither `ValidateSealedPackage` nor `EngineInit`.

- [ ] **Step 24: Assert development-runtime call order**

  With an existing canonical absolute `--development-resource-root`, assert exactly:

  ```cpp
  std::vector<std::string> audit{"parse-cli"};
  CHECK(molga::RunRuntimeStartup(
      dev.request, dev.callbacks, dev.Diagnostics(), &audit) == 0);
  CHECK(audit == std::vector<std::string>{
      "parse-cli", "InitFromExecutable",
      "SetDevelopmentRuntimeResourceRoot",
      "TextRuntimeLifetimeGuard::Create", "EngineInit",
      "EngineShutdown::Complete", "engine-text-owner-destroyed",
      "text-lifetime-destroyed"});
  CHECK(std::find(audit.begin(), audit.end(),
                  "InitializePackagedRuntimeRoot") == audit.end());
  CHECK(std::find(audit.begin(), audit.end(),
                  "ValidateSealedPackage") == audit.end());
  ```

  Include `<algorithm>` in `tests/test_runtime_smoke.cpp`; `callAudit` is the
  declared `std::vector<std::string>`, not a custom container. A relative or
  missing development root returns `4` immediately after the attempted setter
  and never reaches dependency or engine initialization.

  Add success, nonzero engine-return, and throwing-engine rows using the same
  recording startup owner and same sink address. Each proves
  `EngineShutdown::Complete` precedes `engine-text-owner-destroyed`, which
  precedes `text-lifetime-destroyed`; the callback catches the engine-body
  exception, completes same-host shutdown, and returns `4`. Also inject each
  non-`Complete` shutdown status once, release only the known external owner,
  and require the retry on the same host to complete. A test that merely
  observes ready state without completed shutdown and both terminal
  destruction labels is insufficient.

  Add a reserved-authority row using a byte-valid sealed-layout resource root
  and `QualificationSealedLayout`. Its exact audit is
  `parse-cli`, `InitFromExecutable`,
  `SetDevelopmentRuntimeResourceRoot`, `ValidateSealedPackage`,
  `TextRuntimeLifetimeGuard::Create`, `EngineInit`,
  `EngineShutdown::Complete`, `engine-text-owner-destroyed`,
  `text-lifetime-destroyed`; the acquisition receives the validator's exact
  shared result and authority vector. A one-byte tamper stops
  after `ValidateSealedPackage`, returns `4`, writes the typed validation
  failure, and reaches neither dependency nor engine initialization. Also
  reject `Packaged + DevelopmentProject` and
  `Development + PackagedSealed` before either root initializer is called.

- [ ] **Step 25: Add recursive Mach-O and RPATH audit to copied smoke**

  Find every Mach-O with `file`; run `otool -L` and `otool -l`. Reject `/opt/homebrew`, `/usr/local`, source/build canonical prefixes, dynamic HarfBuzz/ICU names, and any non-system path outside `@rpath`, `@loader_path`, `@executable_path`, `/System/Library`, or `/usr/lib`. Unset `FONTCONFIG_*`, `HB_*`, `ICU_*`, `DYLD_*`, `HOMEBREW_*`, and proxy variables; launch with the existing deny-network harness.

- [ ] **Step 26: Create and prove the committed deny-network profile**

  Create `tests/smoke/text_network_deny.sb` with these complete rules:

  ```scheme
  (version 1)
  (allow default)
  (deny network*)
  (deny file-write*)
  (allow file-write*
    (subpath (param "MOLGA_ALLOWED_WRITE_ROOT")))
  ```

  `allow default` intentionally preserves WindowServer, Cocoa/TIS,
  SDL_GPU/Metal, IOKit, preferences-read, and required Mach-service behavior;
  this is a deny-network and bounded-write profile, not a full process
  sandbox. The explicit `network*` and global `file-write*` denies remain in
  force, with the canonical parameter subtree as the sole more-specific write
  allowance. Do not replace this with a brittle implicit mach/IOKit allowlist.

  Retain Step 6's single capability-probe target and complete its source; do not
  repeat its `add_executable`, framework links, or Objective-C++ properties.

  Implement the Step 6 positive/negative cases by invoking
  `/usr/bin/sandbox-exec -D
  MOLGA_ALLOWED_WRITE_ROOT=<canonical-WORK_ROOT> -f <PROFILE> -- <argv...>` as
  an argument vector, never a shell-composed command. Require the profile to be
  a regular non-symlink file inside `SOURCE_DIR/tests/smoke` in production
  callers. The copied-app smoke uses a fresh evidence/write root and fails if
  `sandbox-exec` or the profile is absent.

- [ ] **Step 27: Run the green policy/startup gate**

  Run:

  ```bash
  cmake --build --preset debug --target test_text_package test_asset_catalog \
    test_build_profile test_game_builder test_package_finalizer \
    test_runtime_startup runtime_smoke molga_runtime molga_runtime_dev \
    text_sandbox_capability_probe -j
  ctest --test-dir build/debug -R '^(test_text_package|test_asset_catalog|test_build_profile|test_game_builder|test_package_finalizer|test_runtime_startup|test_text_network_deny_profile|runtime_smoke|smoke_end_to_end)$' --output-on-failure
  ctest --preset debug
  ```

  Expected: the focused group and complete Debug suite pass; the sandbox
  profile permits required file/process work and denies a loopback socket bind; each
  upstream-hashed-file and specified game-root case exits `4` through
  `RunRuntimeStartup` before SDL; dynamic invalid text continuation and
  stress-gate-only failures do not become package startup exits.

- [ ] **Step 28: Commit fail-closed package startup**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextPackageValidator.h \
    src/Text/TextPackageValidator.cpp src/Core/SmokeReport.h \
    src/Core/SmokeReport.cpp src/Core/RuntimeStartup.h \
    src/Core/RuntimeStartup.cpp src/runtime_main.cpp \
    tests/test_text_package.cpp tests/test_runtime_startup.cpp \
    tests/test_runtime_smoke.cpp tests/smoke/create_fixture.cmake \
    tests/smoke/run_end_to_end.cmake \
    tests/smoke/text_network_deny.sb \
    tests/smoke/test_text_network_deny.cmake \
    tests/smoke/text_sandbox_capability_probe.mm
  git commit -m "feat: fail closed on sealed text package errors"
  ```

---

## Package Policy Review Gate

This gate is deliberately separate from code tasks and commits.

- [ ] Run `superpowers:requesting-code-review` over the commits implementing master Tasks 9–17. Give the reviewer the design, master plan, and subplans `03`–`06`.
- [ ] Resolve every blocker/high finding in a focused correction commit; rerun the owning red/green gate after each correction.
- [ ] Run the complete package boundary one final time:

  ```bash
  cmake --preset debug
  cmake --build --preset debug -j
  ctest --preset debug
  git diff --check
  git status --short --branch
  ```

  Expected: Debug is fully green, no whitespace errors, and no unintended/uncommitted files are present. Record the reviewed commit hash for subplan `07`; do not create qualification evidence yet.

---

## Execution Handoff

After the review gate is satisfied, continue to [`07-parity-qualification.md`](07-parity-qualification.md). Package tests establish policy and copied-app startup only; they do not establish three-mode parity, timing qualification, GPU golden output, or visible Korean/Japanese IME behavior.
