# UI/Text Production Backbone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship one deterministic, packaged-font UI/text pipeline for the editor, standalone development runtime, and copied macOS `.app`, including multilingual shaping, paragraph layout, fixed-point UI layout, clipping, focus/navigation, text editing/IME, fail-closed packaging, parity evidence, and the approved performance gates.

**Architecture:** Vendored ICU and HarfBuzz feed a text-owned Unicode/shaping/layout core; shaped glyph IDs feed a frame-pinned atlas and the existing SDL_GPU/Metal render queue. Authored ECS components produce immutable fixed-point UI snapshots, and the same interaction snapshot drives hit-testing, focus, event routing, text editing, and clipped rendering. Editor, runtime, and package loaders share one portable dependency contract, one resource-root contract, and one canonical evidence exporter; configure additionally emits a machine-local provenance lock that is never packaged.

**Tech Stack:** C++17, CMake 3.27 presets, doctest, SDL 3/SDL_GPU Metal, pinned HarfBuzz 14.3.1, pinned ICU4C 78.3 with packaged `icudt78l.dat`, text-owned `stb_truetype` snapshot, nlohmann/json, macOS arm64 qualification.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../plans/2026-08-20-ui-text-production-backbone-design.md)

## Plan Set and Execution Authority

This file is the master integration contract, dependency order, and final acceptance matrix. Its `Task 1..18` sections preserve cross-subsystem interfaces and milestone traceability; they are review gates, not units to hand wholesale to one implementer. The following executable subplans are authoritative for file-sized TDD steps, red/green commands, and commits, and must run in order:

1. [`01-dependencies-unicode.md`](2026-08-20-ui-text/01-dependencies-unicode.md) — Milestones 1–3
2. [`02-font-shaping-layout.md`](2026-08-20-ui-text/02-font-shaping-layout.md) — Milestones 4–8
3. [`03-ui-layout-rendering.md`](2026-08-20-ui-text/03-ui-layout-rendering.md) — Milestones 9–11
4. [`04-ui-interaction-ime.md`](2026-08-20-ui-text/04-ui-interaction-ime.md) — Milestones 12–14
5. [`05-editor-authoring-migration.md`](2026-08-20-ui-text/05-editor-authoring-migration.md) — Milestone 15
6. [`06-macos-package-policy.md`](2026-08-20-ui-text/06-macos-package-policy.md) — Milestones 16–17
7. [`07-parity-qualification.md`](2026-08-20-ui-text/07-parity-qualification.md) — Milestone 18 and release evidence

An executor checks a master acceptance item only after all linked subplan tasks that implement it are green. If a subplan conflicts with the approved design or this master contract, stop and amend both documents with user approval; do not silently choose one. Each subplan exit gate is the entry prerequisite for the next.

## Global Constraints

- The approved design is authoritative. If an implementation step changes public behavior, failure policy, package layout, or milestone scope, stop and amend the design for user approval before continuing.
- Preserve unrelated worktree changes. Before every task run `git status --short --branch`; stage and commit only the files named by that task.
- Use packaged project/engine fonts only. Do not add CoreText fallback, system-font discovery, Homebrew dependencies, runtime downloads, or text-related `FetchContent`.
- Normal dependency configure/build is network-offline: it performs no download, acquisition-script call, or `FetchContent`. CMake, compiler, SDK, `ar`, `ranlib`, and `nm` remain recorded host toolchain inputs; only dependency source/include/library/backend resolution must stay inside the vendored static prefixes.
- Use the exact dependency artifacts: HarfBuzz commit `ab5ecbb83985034a76214ac0b2b833dcd590d774`; ICU commit `21d1eb0f306e1141c10931e914dfc038c06121da`; `icudt78l.dat` size `33107232` and SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b`; ICU archive SHA-256 `982619632b78887f1895b063e96e8c3cc7f99283337c8abbd05aa71635de613c`; ICU license SHA-256 `e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af`.
- Move the exact ImGui `imstb_truetype.h` bytes at commit `b48d1afbe8ee8b238e2961dc363a949dd7304e23` into the text-owned snapshot. Its required SHA-256 is `c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528`.
- HarfBuzz is static with ICU enabled and CoreText, Cairo, FreeType, Graphite2, GLib/GObject, introspection, utils, subset, raster, vector, GPU, and demos disabled. ICU builds static common/i18n plus the pinned private stubdata intermediate with `U_STATIC_IMPLEMENTATION`, but exposes/packages only logical `icuuc` and `icui18n`; `libicudata` is never a third consumer archive.
- Initialize text dependencies only after verifying dependency/data manifests, then register the aligned/mapped data with `udata_setCommonData`, restrict ICU file access, and call `u_init`. Keep data alive until `u_cleanup`; construct no ICU/HarfBuzz text object outside that lifetime.
- Keep source text byte-exact. Invalid UTF-8 renders as `U+FFFD` but retains its original byte range; never normalize authored strings implicitly.
- Use ICU for extended grapheme, line-break, script/script-extension, and BiDi analysis. Use HarfBuzz with `hb_icu_get_unicode_funcs()` for shaping and source-byte cluster values. Never use `stb_truetype` for shaping, kerning, fallback, or line breaking.
- Fallback is extended-grapheme atomic, deterministic, and context-shaped. Final-line shaping is authoritative; never slice paragraph glyph arrays at a break.
- Missing text uses a procedural monochrome tofu glyph and a typed diagnostic. Remove the production ASCII fallback when shared consumer migration lands.
- All UI measurement, arrangement, hit-testing, and snapshot comparisons use validated signed 26.6 fixed-point logical units. Convert only final physical viewport/scissor edges using floor(min)/ceil(max).
- One immutable `InteractionSnapshot N` owns event targeting. A callback may publish at most one `RenderSnapshot N+1`; never re-hit-test or retarget an event against N+1.
- Event, focus, pointer-capture, and IME ownership use the complete runtime identity `{worldGeneration, objectId, componentRuntimeTypeId, componentInstanceId}` and re-resolve it before use.
- `TextInputArbiter` is the sole engine owner of SDL text-input lifecycle; its one `SdlTextInputPlatform` boundary is the only compiled source that directly calls SDL Start/Stop/Clear/SetTextInputArea APIs. Deep-copy native text payloads on ingest and arbitrate per SDL window and owner generation.
- Release output uses `Game.app/Contents/Resources` as the only `RuntimeResourceRoot`; flat executable-directory layout remains an explicit development mode only.
- Package/startup validation is fail-closed. A dependency, ICU data, font, notice, manifest, or authored coverage failure blocks package build; copied runtime failures exit with code `4` before scene startup.
- Do not claim color/variable-font rendering, native macOS accessibility bridging, signing/notarization, Intel/Universal 2, Linux, Windows, or older-macOS qualification.
- Every task follows red-green-refactor: add the focused failing test, run it and record the expected failure, implement the smallest complete slice, run the focused gate, then commit. Never combine a later task just to make an earlier test pass.
- Standard focused command sequence is:

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target <target> -j
  ctest --test-dir build/debug -R '^<test-name>$' --output-on-failure
  ```

- At each review boundary run the complete preset named by the task. Completion requires fresh Debug, Release, ASan, and UBSan results; an existing build directory or earlier 83-test baseline is not completion evidence.

## Stable Contract Spine

The following names are shared across tasks. Add them exactly once in the task that owns the listed header; later tasks extend implementations, not competing types.

```cpp
// src/Text/TextDiagnostic.h
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace molga::text {
enum class TextSeverity : std::uint8_t { Info, Warning, Error, Blocker };
enum class TextDiagnosticCode : std::uint16_t {
    DependencyInvalid,
    Utf8Invalid,
    FontInvalid,
    FontFamilyInvalid,
    MissingGlyph,
    AtlasExhausted,
    LayoutInvalid,
    LayoutCycle,
    ReferenceInvalid,
    TextInputUnavailable,
    TextInputRangeClamped,
    ReflowDeferred,
    PackageValidationFailed,
};
const char* StableTextDiagnosticCode(TextDiagnosticCode) noexcept;
std::optional<TextDiagnosticCode> ParseStableTextDiagnosticCode(
    std::string_view stableCode) noexcept;
struct SourceByteRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    friend constexpr bool operator==(SourceByteRange a,
                                     SourceByteRange b) noexcept {
        return a.begin == b.begin && a.end == b.end;
    }
};
struct TextDiagnostic {
    TextDiagnosticCode code = TextDiagnosticCode::DependencyInvalid;
    TextSeverity severity = TextSeverity::Error;
    std::string subsystem;
    std::string message;
    std::string remediation;
    std::string assetGuid;
    unsigned int sceneObjectId = 0;
    std::string componentType;
    SourceByteRange sourceByteRange;
};
class TextDiagnosticSink {
public:
    virtual ~TextDiagnosticSink() = default;
    virtual void Report(TextDiagnostic diagnostic) = 0;
};
class VectorTextDiagnosticSink final : public TextDiagnosticSink {
public:
    void Report(TextDiagnostic diagnostic) override;
    const std::vector<TextDiagnostic>& Diagnostics() const noexcept { return diagnostics_; }
private:
    std::vector<TextDiagnostic> diagnostics_;
};
} // namespace molga::text
```

```cpp
// src/Common/Fixed26_6.h
#pragma once
#include <cstdint>
#include <optional>

namespace molga {
class Fixed26_6 {
public:
    static constexpr std::int32_t Scale = 64;
    static std::optional<Fixed26_6> FromFloat(float value);
    static constexpr Fixed26_6 FromRaw(std::int32_t value) { return Fixed26_6(value); }
    constexpr std::int32_t Raw() const { return raw_; }
    float ToFloat() const;
    static std::optional<Fixed26_6> CheckedAdd(Fixed26_6 lhs,
                                                Fixed26_6 rhs) noexcept;
    static std::optional<Fixed26_6> CheckedSub(Fixed26_6 lhs,
                                                Fixed26_6 rhs) noexcept;
    static std::optional<Fixed26_6> CheckedMulDiv(
        Fixed26_6 value, std::int64_t numerator,
        std::int64_t denominator) noexcept;
    friend constexpr bool operator==(Fixed26_6 lhs, Fixed26_6 rhs) noexcept {
        return lhs.raw_ == rhs.raw_;
    }
    friend constexpr bool operator!=(Fixed26_6 lhs, Fixed26_6 rhs) noexcept {
        return !(lhs == rhs);
    }
private:
    explicit constexpr Fixed26_6(std::int32_t value) : raw_(value) {}
    std::int32_t raw_ = 0;
};
struct FixedPoint {
    Fixed26_6 x = Fixed26_6::FromRaw(0);
    Fixed26_6 y = Fixed26_6::FromRaw(0);
    friend constexpr bool operator==(FixedPoint a, FixedPoint b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
    friend constexpr bool operator!=(FixedPoint a, FixedPoint b) noexcept {
        return !(a == b);
    }
};
struct FixedSize {
    Fixed26_6 width = Fixed26_6::FromRaw(0);
    Fixed26_6 height = Fixed26_6::FromRaw(0);
    friend constexpr bool operator==(FixedSize a, FixedSize b) noexcept {
        return a.width == b.width && a.height == b.height;
    }
    friend constexpr bool operator!=(FixedSize a, FixedSize b) noexcept {
        return !(a == b);
    }
};
struct FixedRect {
    Fixed26_6 x = Fixed26_6::FromRaw(0);
    Fixed26_6 y = Fixed26_6::FromRaw(0);
    Fixed26_6 width = Fixed26_6::FromRaw(0);
    Fixed26_6 height = Fixed26_6::FromRaw(0);
    friend constexpr bool operator==(FixedRect a, FixedRect b) noexcept {
        return a.x == b.x && a.y == b.y && a.width == b.width &&
               a.height == b.height;
    }
    friend constexpr bool operator!=(FixedRect a, FixedRect b) noexcept {
        return !(a == b);
    }
};
} // namespace molga
```

`FromFloat` and `CheckedMulDiv` round to nearest raw 1/64 unit with exact half ties away from zero. Checked operations use validated 64-bit intermediates, return `nullopt` for non-finite input, zero denominator, intermediate overflow, or final `int32_t` overflow, and never saturate or wrap. Algorithms needing mathematical floor/ceil (grid count and physical edges) call explicitly named floor/ceil helpers rather than depending on C++ signed division.

```cpp
// src/UI/UIRuntimeIdentity.h
#pragma once
#include <cstddef>
#include <cstdint>

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

## File Responsibility Map

| Area | New authoritative files | Existing seams modified |
|---|---|---|
| Dependency/runtime lock | `cmake/TextDependencies.cmake`, `cmake/Merge*.cmake`, `cmake/Repair*RawInstall.cmake`, `cmake/VerifyTextDependencies.cmake`, `cmake/AcquireText*.cmake`, `src/Text/TextDiagnostic.*`, `src/Text/TextRuntimeDependencies.*`, `resources/text/*`, `external/text/*` | `.gitmodules`, `.gitattributes`, `CMakeLists.txt`, `src/main.cpp`, `src/runtime_main.cpp` |
| Unicode and shaping | `src/Text/UnicodeTextBuffer.*`, `src/Text/UnicodeAnalysis.*`, `src/Assets/FontAsset.*`, `src/Assets/FontFamilyAsset.*`, `src/Text/FontRepository.*`, `src/Text/FontFamilyResolver.*`, `src/Text/TextShapingService.*` | `src/Core/Importers/FontImporter.*`, `src/Core/AssetDatabase.*`, `src/Rendering/FontFace.*` |
| Layout and glyph render | `src/Common/Fixed26_6.*`, `src/Text/TextLayoutTypes.h`, `src/Text/TextLayoutService.*`, `src/Text/TextHitTesting.*` | `src/Rendering/FontAtlas.*`, `src/Rendering/TextRenderer.*`, `src/ECS/Components/TextRenderer2D.*`, `src/ECS/Components/UILabel.*`, `src/UI/UISystem.*` |
| UI authoring/runtime | `src/ECS/SceneObjectRef.h`, eight new UI component pairs, `src/UI/UILayoutTypes.h`, `src/UI/UILayoutSystem.*`, `src/UI/UIFocusSystem.*`, `src/UI/UIInputRouter.*`, `src/UI/UITextInputSystem.*`, `src/UI/UIRuntimeIdentity.*` | `src/Core/World.*`, `src/ECS/BuiltinComponents.cpp`, `src/ECS/Components/UICanvas.*`, `RectTransform.*`, `UIButton.*`, `src/UI/UISystem.*` |
| SDL text input | `src/Platform/NativeInputEvent.h`, `src/Platform/NativeTextEvent.h`, `src/Platform/TextInputArbiter.*`, `src/Platform/SdlTextInputPlatform.*` | `src/Core/Bootstrap.*`, `src/Editor/ImGuiLayer.*`, `src/Editor/Windows/GameViewWindow.*`, `src/runtime_main.cpp` |
| Rendering clip | `src/UI/UIRenderCollector.*`, `src/UI/UIScrollSystem.*` | `src/Rendering/RenderQueue.h`, `TextRenderer.*`, `RenderSystem2D.cpp`, `Renderer.*`, `SpriteBatcher.*` |
| Editor/package/evidence | `src/Text/TextCoverageManifest.*`, `src/Text/TextCanonicalSnapshot.*`, `src/Text/TextPerformanceHarness.*`, `resources/Info.plist.in`, qualification scripts | `src/Editor/Properties/EditorPropertyDescriptor.*`, inspector/project browser/object commands, `src/Editor/GameBuilder.*`, `src/Core/PathService.*`, `PackageLayout.*`, `PackageFinalizer.*`, `SmokeReport.*`, CMake smoke scripts |

---

## Task 1: Pin and audit the text dependency toolchain

**Files:**

- Create: `cmake/TextDependencies.cmake`
- Create: `cmake/MergeIcuStubdata.cmake`
- Create: `cmake/MergeHarfBuzzIcu.cmake`
- Create: `cmake/RepairIcuRawInstall.cmake`
- Create: `cmake/RepairHarfBuzzRawInstall.cmake`
- Create: `cmake/ProbeIcuStubdataLink.cmake`
- Create: `cmake/ProbeHarfBuzzIcuLink.cmake`
- Create: `cmake/VerifyTextDependencies.cmake`
- Create: `cmake/AcquireTextRuntimeData.cmake`
- Create: `cmake/AcquireTextFixtures.cmake`
- Create: `cmake/TextArtifactTransaction.cmake`
- Create: `external/text/rasterizer/imstb_truetype.h`
- Create: `external/text/rasterizer/LICENSE.txt`
- Create: `resources/text/icudt78l.dat`
- Create: `resources/text/dependency-contract.input.json`
- Create: `resources/licenses/HarfBuzz.txt`
- Create: `resources/licenses/ICU.txt`
- Create: `resources/licenses/StbTrueType.txt`
- Create: `resources/licenses/ThirdPartyNotices.md`
- Create: `assets/fonts/Inter-v4.0-OFL.txt`
- Create: `tests/fixtures/text/font_manifest.json`
- Create: `tests/fixtures/text/fonts/NotoSans-Regular.ttf`
- Create: `tests/fixtures/text/fonts/NotoSansArabic-Regular.ttf`
- Create: `tests/fixtures/text/fonts/NotoSansHebrew-Regular.ttf`
- Create: `tests/fixtures/text/fonts/NotoSansDevanagari-Regular.ttf`
- Create: `tests/fixtures/text/fonts/NotoSansThai-Regular.ttf`
- Create: `tests/fixtures/text/fonts/NotoSansKR-Regular.otf`
- Create: `tests/fixtures/text/licenses/NotoFonts-ffebf8c1-OFL.txt`
- Create: `tests/fixtures/text/licenses/NotoCJK-Sans2.004-OFL.txt`
- Create: `tests/test_text_dependencies.cpp`
- Create: `tests/test_text_harfbuzz_link.cpp`
- Create: `tests/test_text_icu_link.cpp`
- Create: `tests/probes/icu_stub_link.cpp`
- Create: `tests/probes/harfbuzz_icu_link.cpp`
- Modify: `.gitmodules`
- Modify: `.gitattributes`
- Modify: `.gitignore`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: approved source/data/license hashes from Global Constraints and the existing `molga_core` target.
- Produces: build barrier `molga_text_dependencies_ready`; CMake targets `molga_text_icuuc`, `molga_text_icui18n`, `molga_text_harfbuzz`, `molga_text_rasterizer`; `molga_text_icuuc` is the deterministic composite of raw ICU common plus the pinned stubdata object and contains exactly one defined `icudt78_dat`; `molga_text_harfbuzz` is the deterministic composite of upstream raw `harfbuzz` plus raw `harfbuzz-icu` and contains exactly one defined `hb_icu_get_unicode_funcs`; separate idempotent barrier-only and portable-contract helpers, a provenance-test-only helper, and direct ICU/normal HarfBuzz helpers composed over the barrier; machine-local `${CMAKE_BINARY_DIR}/generated/text_dependency_build_lock.json`; and portable `${CMAKE_BINARY_DIR}/generated/text_dependency_contract.json`. Only an explicitly registered portable C++ reader receives `MOLGA_TEXT_DEPENDENCY_CONTRACT`; only `test_text_dependencies` receives `MOLGA_TEXT_DEPENDENCY_BUILD_LOCK`, `MOLGA_SOURCE_DIR`, `MOLGA_BINARY_DIR`, `MOLGA_CMAKE_COMMAND`, or `MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT`. Stubdata and the upstream HarfBuzz ICU adapter remain composite provenance inside one logical archive each, never additional public/package archives.

- [ ] Add `external/harfbuzz` and `external/icu` as git submodules, checkout the approved commits, and make the gitlinks the only accepted source roots.

  ```bash
  git submodule add https://github.com/harfbuzz/harfbuzz.git external/harfbuzz
  git -C external/harfbuzz checkout ab5ecbb83985034a76214ac0b2b833dcd590d774
  git submodule add https://github.com/unicode-org/icu.git external/icu
  git -C external/icu checkout 21d1eb0f306e1141c10931e914dfc038c06121da
  git submodule status external/harfbuzz external/icu
  ```

  Expected: status lines start with the two exact 40-character commits and neither line starts with `+`, `-`, or `U`.

- [ ] Track the 33 MiB ICU data and binary font fixtures with Git LFS. Do not copy any artifact directly to its final path. `AcquireTextRuntimeData.cmake` first copies the exact rasterizer bytes from the pinned ImGui submodule into a unique ignored acquisition root, verifies every staged data/license/rasterizer artifact, then publishes the complete allowlisted set through `TextArtifactTransaction.cmake` with journal recovery. Every recovery call receives the current caller's independently canonicalized destination-root allowlist and rejects a journal path outside it; roots recorded inside an untrusted journal never authorize cleanup. A failure before or during rename restores all prior bytes; no mixed generation is visible.

  ```bash
  git lfs track 'resources/text/*.dat' 'tests/fixtures/text/fonts/*.ttf' 'tests/fixtures/text/fonts/*.otf'
  shasum -a 256 external/imgui/imstb_truetype.h
  ```

  Expected: the pinned source rasterizer is `c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528`; no final text-owned path exists yet.

- [ ] Implement `cmake/AcquireTextRuntimeData.cmake` as a maintainer-only clean-checkout bootstrap; normal configure/build never invokes it. Download the official `icu4c-78.3-data-bin-l.zip` from `https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-data-bin-l.zip` with `EXPECTED_HASH SHA256=982619632b78887f1895b063e96e8c3cc7f99283337c8abbd05aa71635de613c`, extract exactly one `icudt78l.dat`, require size `33107232` and SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b`, then atomically install it at `resources/text/icudt78l.dat`.

  The same script requires the two submodules at their pinned gitlinks; copies `external/harfbuzz/COPYING` to `resources/licenses/HarfBuzz.txt` and verifies SHA-256 `ba8f810f2455c2f08e2d56bb49b72f37fcf68f1f4fade38977cfd7372050ad64`; copies `external/icu/LICENSE` to `resources/licenses/ICU.txt` and verifies SHA-256 `e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af`; LF-preserving extracts lines `5045..5085` from the exact locked rasterizer bytes to both `external/text/rasterizer/LICENSE.txt` and `resources/licenses/StbTrueType.txt`, requiring SHA-256 `7587efcca32db8f95bf5860dea6dfa4be410ee723e1964db3689c8ddf2a2e4a9`; and downloads `https://raw.githubusercontent.com/rsms/inter/2ce9119398be143fa289c3e180824db1b7ed803e/LICENSE.txt` to `assets/fonts/Inter-v4.0-OFL.txt` with SHA-256 `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a`. Generate `resources/licenses/ThirdPartyNotices.md` deterministically as the immutable engine-dependency base template. The portable contract records the generator/template identity and individual dependency-license hashes, not the final notice SHA; Task 17 creates a packaged copy with sorted project-font entries. Every download goes to a temporary file, and no destination is replaced until every check passes.

  ```bash
  cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextRuntimeData.cmake
  shasum -a 256 resources/text/icudt78l.dat resources/licenses/* \
    external/text/rasterizer/LICENSE.txt assets/fonts/Inter-v4.0-OFL.txt
  wc -c resources/text/icudt78l.dat
  ```

  Expected: ICU data/license, HarfBuzz license, stb license, and Inter license match the exact hashes above; ICU data size is `33107232`.

- [ ] Implement `cmake/AcquireTextFixtures.cmake` as an explicit maintainer-only download command; normal configure/build never invokes it. It downloads to a `file(MAKE_DIRECTORY)` temporary directory, uses `file(DOWNLOAD ... EXPECTED_HASH SHA256=...)`, and only then copies these exact immutable artifacts into the fixture tree:

  | Local file | Immutable source | Bytes | SHA-256 |
  |---|---|---:|---|
  | `NotoSans-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-Regular.ttf` | 569208 | `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` |
  | `NotoSansArabic-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansArabic/NotoSansArabic-Regular.ttf` | 240456 | `ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b` |
  | `NotoSansHebrew-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansHebrew/NotoSansHebrew-Regular.ttf` | 26900 | `a7fa16fffb27bedb060a0866267c29e9859aeb9c21cc33f5b3aaf6eb062eca85` |
  | `NotoSansDevanagari-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansDevanagari/NotoSansDevanagari-Regular.ttf` | 219212 | `385e78e6359a9d88a0f243d53b1209d7548361ba2194e2b9ec779bcaa7e8949d` |
  | `NotoSansThai-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansThai/NotoSansThai-Regular.ttf` | 37752 | `404ddfb5ed0aaa6b6ec8a85700d682978992062d67da93903967b56cbd9a4acc` |
  | `NotoSansKR-Regular.otf` | `https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf` | 4644748 | `69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68` |
  | `NotoFonts-ffebf8c1-OFL.txt` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/LICENSE` | verify manifest | `0dab92d0544f7b233403f14b84a663bdbfa746982eda629e7f4f9ffe1b036feb` |
  | `NotoCJK-Sans2.004-OFL.txt` | `https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/LICENSE` | verify manifest | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` |

  Run once and verify all eight outputs:

  ```bash
  cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextFixtures.cmake
  shasum -a 256 tests/fixtures/text/fonts/* tests/fixtures/text/licenses/*
  ```

- [ ] Record those exact paths/sizes/hashes, source commits, intended scripts, and license mapping in `tests/fixtures/text/font_manifest.json`. Also record existing `assets/fonts/Inter-Regular.ttf` as Inter v4.0 SHA `64f8be6e55c37e32ef03da99714bf3aa58b8f2099bfe4f759a7578e3b8291123`, source tag commit `2ce9119398be143fa289c3e180824db1b7ed803e`, and license SHA `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a`. Use Noto Sans, not Inter, as the authoritative `fi/ffi` fixture because Inter v4.0 has no `fi/ffi` GSUB ligature.

- [ ] Keep existing `tests/fixtures/fonts/NotoSansKR-Regular.ttf` only as the variable-font rejection case: assert SHA `194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252` and presence of `fvar/gvar/avar/HVAR`. Do not treat its existing modified `OFL.txt` as exact provenance. Lock the corpus strings `ffi`, `AV`, `x\u0301`, `سلام`, `ن\u200Dن`, `abc שלום 123!`, `क्षि`, `क्\u200Dष`, `กำลัง เก่ง`, `한글 日本語 中文 漢字（、。）`, `！\uFE00`, and missing/tofu `👩\u200D🚀` in the manifest.

- [ ] Write `resources/text/dependency-contract.input.json` with schema `1`, every approved source/data/license hash, the permitted HarfBuzz/ICU option matrix, stable logical archive names, and repo-relative canonical source identities. `VerifyTextDependencies.cmake` emits two deliberately different artifacts:

  - `text_dependency_build_lock.json` is build-only provenance. It adds compiler ID/version, target architecture, canonical absolute source/include/archive paths, and static archive SHA-256 values. Tests may inspect it, but GameBuilder must reject any attempt to stage it.
  - `text_dependency_contract.json` is the portable runtime/package contract. It carries the same commits, option matrix, compiler identity, architecture, archive SHA-256 values, data/rasterizer/license hashes, repo-relative source identities, and stable logical archive names, but contains no checkout/build absolute path. This is the only dependency artifact hashed by runtime caches and copied into a package.

  ```json
  {
    "schemaVersion": 1,
    "harfbuzz": {"version":"14.3.1","commit":"ab5ecbb83985034a76214ac0b2b833dcd590d774"},
    "icu": {"version":"78.3","commit":"21d1eb0f306e1141c10931e914dfc038c06121da"},
    "icuData": {"file":"Engine/Text/icudt78l.dat","size":33107232,"sha256":"d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b"},
    "rasterizer": {"sha256":"c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528"}
  }
  ```

- [ ] Add a failing `test_text_dependencies` that reads both generated paths, verifies the build lock's static archives and canonical source prefixes, compares every shared field with the portable contract, checks all committed SHA/size values and forbidden options, and recursively asserts that the portable JSON contains no absolute path or checkout/build prefix.

  ```cpp
  TEST_CASE("text dependency provenance and portable contract agree") {
      const auto lock = LoadJson(MOLGA_TEXT_DEPENDENCY_BUILD_LOCK);
      const auto contract = LoadJson(MOLGA_TEXT_DEPENDENCY_CONTRACT);
      const auto input = LoadJson(std::filesystem::path(MOLGA_SOURCE_DIR) /
                                  "resources/text/dependency-contract.input.json");
      CHECK(lock["harfbuzz"]["commit"] ==
            "ab5ecbb83985034a76214ac0b2b833dcd590d774");
      CHECK(lock["icu"]["commit"] ==
            "21d1eb0f306e1141c10931e914dfc038c06121da");
      CHECK(lock["icuData"]["size"] == 33107232);
      CHECK(lock["icuData"]["sha256"] ==
            "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b");
      CHECK_FALSE(lock["harfbuzz"]["options"]["coretext"]);
      CHECK_FALSE(lock["harfbuzz"]["options"]["freetype"]);
      CHECK(lock["harfbuzz"]["options"]["icu"]);
      CHECK(lock["icu"]["libraries"] == nlohmann::json::array({"icui18n", "icuuc"}));
      CHECK(contract["icu"]["libraries"] ==
            nlohmann::json::array({"icui18n", "icuuc"}));
      CheckExactPortableObjectKeys(contract["icu"], input["icu"],
                                   {"archiveSha256"});
      CheckExactObjectKeys(contract["icu"]["archiveSha256"],
                           {"icui18n", "icuuc"});
      CHECK(contract["icu"]["archiveSha256"] ==
            lock["icu"]["archiveSha256"]);
      CHECK(contract["icu"]["archiveSha256"]["icuuc"] ==
            lock["icu"]["commonComposite"]["finalCompositeSha256"]);
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "commonComposite"));
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "rawCommon"));
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "rawI18n"));
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "stubSource"));
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "stubObject"));
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "libicudata"));
      CHECK_FALSE(JsonContainsSubstring(contract["icu"], "icudt78_dat"));
      CHECK(lock["harfbuzz"]["libraries"] == nlohmann::json::array({"harfbuzz"}));
      CHECK(contract["harfbuzz"]["libraries"] == nlohmann::json::array({"harfbuzz"}));
      CHECK(lock["harfbuzz"]["compositeSha256"] ==
            contract["harfbuzz"]["compositeSha256"]);
      CHECK(lock["harfbuzz"]["compositeSha256"] ==
            RequiredSha256File(BuildPath(
                "text-dependencies/harfbuzz/lib/libharfbuzz.a")));
      const auto& hbComposite = lock["harfbuzz"]["icuComposite"];
      CHECK(hbComposite["finalCompositeSha256"] ==
            lock["harfbuzz"]["compositeSha256"]);
      CHECK(hbComposite["rawCoreDefinedAdapterSymbols"] == 0);
      CHECK(hbComposite["rawIcuAdapterDefinedAdapterSymbols"] == 1);
      CHECK(hbComposite["compositeDefinedAdapterSymbols"] == 1);
      CheckHarfBuzzCompositeProvenanceMatchesFiles(lock);
      CheckExactPortableObjectKeys(contract["harfbuzz"], input["harfbuzz"],
                                   {"compositeSha256"});
      CHECK_FALSE(JsonContainsSubstring(contract, "harfbuzz-icu"));
      CHECK_FALSE(JsonContainsSubstring(contract, "icuComposite"));
      CHECK_FALSE(JsonContainsSubstring(contract, "rawCore"));
      CHECK_FALSE(JsonContainsSubstring(contract, "rawAdapter"));
      CHECK_FALSE(JsonContainsSubstring(contract, "adapterObject"));
      CheckCanonicalPathEquals(lock["harfbuzz"]["sourcePath"],
                               std::filesystem::path(MOLGA_SOURCE_DIR) /
                                   "external/harfbuzz");
      CheckCanonicalPathEquals(lock["icu"]["sourcePath"],
                               std::filesystem::path(MOLGA_SOURCE_DIR) /
                                   "external/icu");
      CheckEveryIncludeAndArchiveUnderNestedPrefix(
          lock, std::filesystem::path(MOLGA_BINARY_DIR) / "text-dependencies");
      CheckEveryDependencyArchiveIsStatic(lock);
      CheckSharedDependencyFieldsEqual(lock, contract);
      CheckJsonHasNoAbsolutePath(contract, MOLGA_SOURCE_DIR, MOLGA_BINARY_DIR);
  }
  ```

  Replace the immutable-input task's temporary direct definitions with the
  provenance-test helper, and define `MOLGA_SOURCE_DIR`/`MOLGA_BINARY_DIR`
  only on this test target.
  `BuildPath` rejects absolute/escaping input and uses component-wise canonical
  containment; `RequiredSha256File` wraps `molga::Sha256File` with explicit
  error/64-hex validation. `JsonContainsSubstring` scans every object key and
  string value; `CheckExactObjectKeys` rejects every missing/extra key;
  `CheckExactPortableObjectKeys` requires exactly the committed input keys plus
  the supplied generated keys (`archiveSha256` for ICU and
  `compositeSha256` for HarfBuzz); and
  `CheckCanonicalPathEquals` requires exact canonical source roots, and
  `CheckHarfBuzzCompositeProvenanceMatchesFiles` invokes the verifier's closed
  `HARFBUZZ_READ_ONLY` mode through absolute `MOLGA_CMAKE_COMMAND` and
  `MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT` definitions with literal
  `MODE/BUILD_LOCK/SOURCE_ROOT/BINARY_ROOT/RESULT_ROOT/RESULT_FILE` arguments.
  It requires exit `0` and an exact schema-1 result containing only mode plus
  raw/final hashes, exact extracted-object hash, filtered-member digest,
  configured tool/flag identities, and raw-zero/adapter-one/final-one symbol
  counts, then validates the schema/mode discriminators and compares every
  remaining composite value with the lock. The script rejects an
  existing/escaping result and every production publication argument, writes
  only beneath the caller-owned unique test root, and cannot publish or replace
  either generated record/archive. These six test-private definitions never
  leak to a normal or package consumer.

- [ ] Run the red gate before adding the dependency module.

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_text_dependencies -j
  ```

  Expected: configure or compile fails because the two dependency artifacts and vendored static targets do not exist yet; failure must not resolve `/opt/homebrew`, `/usr/local`, or an Apple framework as a substitute.

- [ ] Implement `cmake/TextDependencies.cmake` as a repo-local nested build/install into `${CMAKE_BINARY_DIR}/text-dependencies`: configure pinned ICU with exact `--disable-shared --enable-static --disable-tools --disable-tests --disable-samples --disable-extras --disable-icuio --disable-layoutex --prefix=<icu-raw>` flags, reject an ignored/unknown flag, and build only raw common/i18n plus the pinned tiny stubdata target without compiled full data. Run configure only through `cmake -E env ZERO_AR_DATE=1 PKG_CONFIG=false ac_cv_prog_PYTHON=`. Require `config.log`, `config.status`, and generated makefiles to resolve no host `pkg-config`, Python, or `icu-le-hb`; require `PKG_CONFIG`, `PYTHON`, `ICULEHB_CFLAGS`, and `ICULEHB_LIBS` exactly empty; reject `Spawning Python`; and require the disabled `data/rules.mk` and `test/testdata/rules.mk` to be one newline with no rule. A literal `PKG_CONFIG=false` configured value is not accepted as empty.

  Give the nested ICU project exact `BUILD_BYPRODUCTS` for `${icu_build}/lib/libicuuc.a`, `${icu_build}/lib/libicui18n.a`, `${icu_build}/stubdata/libicudata.a`, and `${icu_build}/stubdata/stubdata.ao`. Enumerate only clean pinned `icu4c/source/{common,i18n}/unicode/*.h` regular files, reject symlink/escape and duplicate installed basenames, require exactly 203 names and SHA-256 `fe6c48d6b56a735a1434c0aabd47c4dbbb2a1f71df26c7fccc98cf74dec8947b` for their bytewise-sorted LF-joined source-relative manifest with one terminal LF, and declare the corresponding 203 installed headers plus raw installed `libicuuc.a`/`libicui18n.a` as the exact 205 `INSTALL_BYPRODUCTS`. Because Make does not regenerate a missing byproduct uniformly, the always-checked `molga_text_icu_raw_install` target's sole command invokes `cmake/RepairIcuRawInstall.cmake` with closed `MODE=RAW_INSTALL_WRAPPER`, canonical ICU source/build/raw-prefix paths, the configured absolute Make program, and expected header manifest; it rejects unknown/escaping input. The script first repairs any missing build-tree output by running the exact pinned nested Make targets for common, i18n, and stubdata under `ZERO_AR_DATE=1` and revalidates all four build files. It then compares both installed archives to those build-tree authorities and every installed header to its clean pinned source, reruns the exact install on an installed missing/mismatch, and revalidates all 205 installed files. Installed stubdata/CMake/pkg-config files are incidental and never consumed or used as authority. In a dedicated CMake 3.27 `Unix Makefiles` build, delete each of the four build outputs and installed `include/unicode/utypes.h`, `libicuuc.a`, and `libicui18n.a` one at a time, rebuild only the wrapper, and require exact restoration; validate every deletion below that temporary tree first.

  Declare final composite `libicuuc.a` and copied/verified `libicui18n.a` as explicit custom-command `OUTPUT`s. `MergeIcuStubdata.cmake` extracts exactly one stub object, rejects duplicate members, copies raw common to a staged output, appends the object with the configured deterministic archiver/ranlib and `ZERO_AR_DATE=1`, then atomically publishes the composite `libicuuc.a`. Before imported targets or standalone tests exist, run a target-independent `ProbeIcuStubdataLink.cmake` against `tests/probes/icu_stub_link.cpp`: the raw archive probe succeeds only after observing an undefined `icudt78_dat`, and the composite probe succeeds only after linking the same source. Hash the stub source/object, raw archive, ordered member list, tool/flags, and composite; require two rebuilds to be byte-identical and `nm -g` to find exactly one externally defined `icudt78_dat`.

  Build HarfBuzz in `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build`, install raw outputs below `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-raw`, and pin exact cache values `BUILD_SHARED_LIBS=OFF`, `BUILD_FRAMEWORK=OFF`, `HB_HAVE_ICU=ON`, `HB_HAVE_CORETEXT=OFF`, `HB_HAVE_CAIRO=OFF`, `HB_HAVE_FREETYPE=OFF`, `HB_HAVE_GRAPHITE2=OFF`, `HB_HAVE_GLIB=OFF`, `HB_HAVE_GOBJECT=OFF`, `HB_HAVE_INTROSPECTION=OFF`, `HB_BUILD_UTILS=OFF`, `HB_BUILD_SUBSET=OFF`, `HB_BUILD_RASTER=OFF`, `HB_BUILD_VECTOR=OFF`, `HB_BUILD_GPU=OFF`, and string `HB_BUILD_GPU_DEMO=OFF`; assert every nested cache type/value. Pin `ICU_INCLUDE_DIR` and both `ICU_UC_LIBRARY_RELEASE/DEBUG` to the nested static paths, never rely on the singular FindICU library variable, and verify the exact nested cache selections, the `hb-icu.cc` compile command's nested ICU include plus `U_STATIC_IMPLEMENTATION`, and the standalone driver probe's exact static-common path. Upstream install may emit `harfbuzzConfig*.cmake`/`harfbuzz*.pc` inside the raw prefix, but no Molga step loads or parses them as an input, links through them, stages them, or packages them; they describe the upstream raw split and are intentionally outside the byproduct/repair set. Raw archive commands need only reject dependency include/library/backend host paths because the configured CMake/compiler/SDK/ar/ranlib/nm paths are recorded host toolchain provenance. Set `CMAKE_DISABLE_FIND_PACKAGE_Python3=ON` because every Python-using branch is disabled, and wrap every raw configure/build/install archive command in `ZERO_AR_DATE=1`.

  The pinned upstream CMake produces separate raw `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build/libharfbuzz.a` and `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build/libharfbuzz-icu.a`; declare those exact two archives as `BUILD_BYPRODUCTS`. Its configure-generated public authority `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build/src/hb-features.h` instead belongs to an explicit `ExternalProject_Add_Step` between configure and build, with the clean template plus `cmake/RepairHarfBuzzRawInstall.cmake` as dependencies and the generated header as its exact `BYPRODUCTS`; do not assign a configure output to the archive build command. The step invokes exact `MODE=GENERATED_HEADER_PREBUILD`, while the always-checked raw-install target's sole command invokes exact `MODE=RAW_INSTALL_WRAPPER`; both receive the canonical roots/toolchain/nested-ICU paths and typed cache matrix and reject unknown/escaping input. Under the exact macOS option matrix, the generated header must have SHA-256 `b9f5b0184edfab48fa3953f3b5fe8f72f72db0c08ebf6ccfc2541451c8bbc597`. Declare raw installed `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-raw/lib/libharfbuzz.a`, `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-raw/lib/libharfbuzz-icu.a` plus the exact 34-header pinned public install set as the 36 `INSTALL_BYPRODUCTS`; never bind the public target to raw core alone. Both modes first reassert the complete typed cache and independently pinned generated-header hash. A cache/header missing or mismatch reruns the exact pinned configure command under `ZERO_AR_DATE=1` with the same canonical source/binary/prefix/toolchain/nested-ICU/Python-disable/cache arguments and reasserts both. Pre-build mode then returns to its declared following build step; wrapper mode invokes exact nested `harfbuzz`/`harfbuzz-icu` targets before trusting the raw archives, repairs either otherwise-missing archive with those targets, and repairs/revalidates all 36 installed outputs against build-tree/source/generated-header authorities. In a dedicated CMake 3.27 `Unix Makefiles` tree, delete the build-tree generated header and rebuild only the wrapper; then identically tamper both build-tree and installed generated-header copies and rebuild only the wrapper. Require exact configure/archive-target logs, restored pinned header bytes in both places, unchanged raw archive hashes, and no mutation outside the temporary tree. Require raw core to define zero and raw adapter exactly one `hb_icu_get_unicode_funcs`. Filter only archive-table pseudo-members (`__.SYMDEF`, `__.SYMDEF SORTED`, GNU `/`, `//`), require one remaining adapter object, and have `MergeHarfBuzzIcu.cmake` extract that exact named object rather than blanket-extracting the archive. Append it after raw core members with the same deterministic archiver policy, run ranlib, and atomically publish one logical `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz/lib/libharfbuzz.a` custom-command `OUTPUT`. `ProbeHarfBuzzIcuLink.cmake` compiles with both HarfBuzz/ICU include roots plus `U_STATIC_IMPLEMENTATION`, proves raw core fails for that symbol, and proves the final composite links when followed by i18n/common. Hash raw archives, adapter object, filtered member order, tool/flags, and final composite; in the same canonical build tree delete both build-tree raw archives, both installed raw copies, and the final composite while preserving `.o` files, then build only the raw-install wrapper followed by the composite producer. Require the wrapper log to prove it ran both nested archive targets and require identical raw/object/member/final provenance plus exactly one final symbol definition; reinstalling unchanged build-tree archives is not a determinism test, and cross-root Debug identity is not claimed. Delete a transitive installed header and require the install producer to restore it. Expose only `molga_text_icuuc`, `molga_text_icui18n`, and the one composite `molga_text_harfbuzz`; raw `harfbuzz-icu` is provenance, not a fourth dependency target or second manifest library.

  Make the HarfBuzz raw-install target an always-checked repair boundary because
  Make does not uniformly rerun an ExternalProject configure/install when only
  a byproduct disappears. Verify both installed archives against their
  build-tree archives, 33 installed source headers against the clean pinned
  source, the generated build-tree `hb-features.h` against its independent
  pinned SHA, and the installed copy against that verified build authority;
  never baseline either generated copy. Rerun the exact configure or install
  repair under `ZERO_AR_DATE=1` as applicable, then revalidate all 36 consumed
  installed outputs before consumers compile. Scope host-path rejection to dependency
  source/include/library/backend cache values and compile/link arguments; the
  configured host toolchain paths are recorded rather than rejected, and
  offline means no network/acquisition during normal dependency builds.

  Each imported target depends on the producer that owns its `IMPORTED_LOCATION`, so clean Ninja/Make links have both target-order and file-level rules. Use `NO_DEFAULT_PATH` for every include/archive lookup and add explicit dependencies so HarfBuzz cannot configure before the composite ICU prefix exists. Encode the graph as composite `molga_text_harfbuzz -> molga_text_icui18n -> molga_text_icuuc`; a normal text consumer links HarfBuzz plus rasterizer, while a direct-ICU test/helper links both ICU targets. `molga_attach_text_verification_barrier` is target-property-idempotent and adds only `molga_text_dependencies_ready`; normal/direct helpers compose it but add no contract path. `molga_attach_text_portable_contract_consumer` adds only `MOLGA_TEXT_DEPENDENCY_CONTRACT` plus that barrier and is used only by a C++ target that reads the portable record. `molga_attach_text_provenance_test` rejects every target except exact `test_text_dependencies`, is idempotent, composes the portable helper, and is the sole final definition path for `MOLGA_TEXT_DEPENDENCY_BUILD_LOCK`, `MOLGA_SOURCE_DIR`, `MOLGA_BINARY_DIR`, `MOLGA_CMAKE_COMMAND`, and `MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT`; replace the earlier temporary root definitions rather than duplicating them. Call each helper only after its consumer exists. Configure-time assertions require all six test-private definitions exactly once on the provenance test and the five machine-local definitions absent from normal/direct/product/package targets. Configure may validate existing source/data/input hashes but must not `REAL_PATH`, hash, or require an archive that the nested build has not produced.

- [ ] Implement `molga_text_dependencies_ready` as an `ALL` custom target/command that runs only after both ICU and HarfBuzz raw installs, deterministic composites, and target-independent link probes, invokes `cmake/VerifyTextDependencies.cmake`, and declares both JSON files as `BYPRODUCTS`. Before either nested configure and again inside the verifier immediately before hashing and publication, require superproject gitlink OID == submodule `HEAD` == approved commit plus empty `git status --porcelain=v1 --untracked-files=all` for ICU and HarfBuzz. At the barrier the verifier resolves every source/include/archive through `REAL_PATH`, requires the two recorded source roots to equal canonical `external/icu` and `external/harfbuzz`, rejects an individual source/output outside its exact authority, repeats both composite member/symbol checks, hashes inputs/outputs, and atomically emits both generated artifacts.

  Its closed `HARFBUZZ_READ_ONLY` mode requires all and only `MODE`, `BUILD_LOCK`, `SOURCE_ROOT`, `BINARY_ROOT`, `RESULT_ROOT`, and `RESULT_FILE`; requires the result to be a new direct child of the existing canonical caller root; rejects production destinations/journal/injection arguments; never calls the publisher or mutates an archive; and returns nonzero with no result on failure. On success it writes exact schema-1 `{schemaVersion,mode,rawCoreSha256,rawAdapterSha256,adapterObjectSha256,finalCompositeSha256,filteredMemberDigest,archiverFamily,arPath,ranlibPath,nmPath,arAppendFlags,ranlibFlags,zeroArDate,rawCoreDefinedAdapterSymbols,rawIcuAdapterDefinedAdapterSymbols,compositeDefinedAdapterSymbols}`. The dependency test invokes it with the configured absolute CMake/script paths, requires exit `0`, rejects wrong/extra result fields, and compares every value to the build lock.

  The build lock records exact `icu.archiveSha256` keys `{icui18n,icuuc}`, requires `icu.archiveSha256.icuuc == icu.commonComposite.finalCompositeSha256`, and keeps raw common/i18n plus stub source/object/tool/member provenance only under the build-only composite record. The portable `icu` object has exactly the committed input keys plus generated `archiveSha256`; that map has exactly `{icui18n,icuuc}`, and no ICU key/string contains `commonComposite`, `rawCommon`, `rawI18n`, `stubSource`, `stubObject`, `libicudata`, or `icudt78_dat`. The build lock records canonical HarfBuzz raw paths plus exact `archiverFamily`, ar/ranlib/nm paths, `arAppendFlags`, `ranlibFlags`, `zeroArDate`, and complete raw/object/member/final provenance under `harfbuzz.icuComposite`. The portable `harfbuzz` object has exactly the committed input keys plus `compositeSha256`; its input-carried `libraries` field is exactly `["harfbuzz"]`, and no key/string value contains `harfbuzz-icu`, `icuComposite`, raw core, raw adapter, or adapter object. `molga_core`, standalone link probes, every ready text-runtime test, and each CMake staging/package producer attach the barrier through the appropriate seam so no compiler/runtime reads a missing or stale contract. Only `test_text_dependencies` receives machine-local provenance macros; a later C++ package reader receives the portable path only. A mismatch calls `message(FATAL_ERROR ...)`.

- [ ] Add a clean-build ownership regression in a dedicated temporary build
  tree. Build both standalone link probes, delete exactly one validated
  build-tree final composite `libharfbuzz.a`, final `libicui18n.a`, or composite `libicuuc.a`
  while leaving the JSON records, then rebuild the affected probe. Each owning
  producer must recreate and reverify its archive before link; a missing-rule
  error, stale JSON shortcut, or source/workspace deletion fails the test.

- [ ] Link `molga_core` through the encoded HarfBuzz/ICU graph plus `molga_text_rasterizer`, expose only the imported targets' public include directories, and define `U_STATIC_IMPLEMENTATION` on ICU consumers (`molga_core` and direct ICU probes), never on the header-only rasterizer target. Keep `${IMGUI_DIR}` as the existing private include needed by unrelated engine component sources, while changing only `FontFace.cpp` to include `text/rasterizer/imstb_truetype.h` through the dedicated interface target rooted at `${CMAKE_SOURCE_DIR}/external`; source checks prove FontFace no longer reaches ImGui's private rasterizer path. Add a standalone HarfBuzz-symbol probe and a direct ICU-symbol probe that do not link `molga_core`, catching transitive order/include regressions.

- [ ] Run the green dependency gate and prove that no dynamic ICU/HarfBuzz library entered the test executable.

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_text_dependencies \
    test_text_harfbuzz_link test_text_icu_link -j
  ctest --test-dir build/debug \
    -R '^(test_text_dependencies|test_text_harfbuzz_link|test_text_icu_link)$' \
    --output-on-failure
  nm -g build/debug/text-dependencies/harfbuzz/lib/libharfbuzz.a | \
    rg -c ' [TDS] _?hb_icu_get_unicode_funcs$' | rg '^1$'
  otool -L build/debug/tests/test_text_harfbuzz_link | \
    rg -i 'harfbuzz|icu|homebrew' && exit 1 || true
  otool -L build/debug/tests/test_text_icu_link | \
    rg -i 'harfbuzz|icu|homebrew' && exit 1 || true
  ```

  Expected: all three tests pass, the final composite has exactly one `hb_icu_get_unicode_funcs` definition, and neither linked probe has a dynamic ICU/HarfBuzz/Homebrew dependency. The dependency test proves the portable contract exposes one logical HarfBuzz archive.

- [ ] Commit only the dependency provenance/contract slice.

  ```bash
  git add .gitmodules .gitattributes .gitignore CMakeLists.txt tests/CMakeLists.txt \
    cmake/TextDependencies.cmake cmake/MergeIcuStubdata.cmake \
    cmake/MergeHarfBuzzIcu.cmake cmake/RepairIcuRawInstall.cmake \
    cmake/RepairHarfBuzzRawInstall.cmake \
    cmake/ProbeIcuStubdataLink.cmake \
    cmake/ProbeHarfBuzzIcuLink.cmake \
    cmake/VerifyTextDependencies.cmake \
    cmake/AcquireTextRuntimeData.cmake \
    cmake/AcquireTextFixtures.cmake cmake/TextArtifactTransaction.cmake \
    external/harfbuzz external/icu external/text resources/text \
    resources/licenses/HarfBuzz.txt resources/licenses/ICU.txt \
    resources/licenses/StbTrueType.txt resources/licenses/ThirdPartyNotices.md \
    assets/fonts/Inter-v4.0-OFL.txt \
    src/Rendering/FontFace.cpp \
    tests/fixtures/text tests/test_text_dependencies.cpp \
    tests/test_text_harfbuzz_link.cpp tests/test_text_icu_link.cpp \
    tests/probes/icu_stub_link.cpp tests/probes/harfbuzz_icu_link.cpp
  git commit -m "build: pin deterministic text dependencies"
  ```

## Task 2: Add typed diagnostics and the verified ICU runtime lifetime

**Files:**

- Create: `src/Text/TextDiagnostic.h`
- Create: `src/Text/TextDiagnostic.cpp`
- Create: `src/Text/TextRuntimeDependencies.h`
- Create: `src/Text/TextRuntimeDependencies.cpp`
- Create: `cmake/StageTextRuntimeResources.cmake`
- Create: `tests/TextRuntimeTestSession.h`
- Create: `tests/TextRuntimeTestSession.cpp`
- Create: `tests/TextRuntimeDependenciesTestAccess.h`
- Create: `tests/TextRuntimeDependenciesTestAccess.cpp`
- Create: `tests/text_doctest_main.cpp`
- Create: `tests/text_runtime_probe_main.cpp`
- Create: `tests/test_text_runtime_session.cpp`
- Create: `tests/test_text_runtime_dependencies.cpp`
- Modify: `cmake/TextDependencies.cmake`
- Modify: `src/Common/Log.h`
- Modify: `src/Common/Log.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `molga_text_icuuc`, `molga_text_icui18n`, the verified portable dependency contract (after the build-time barrier compares it with the build lock), and packaged `icudt78l.dat` from Task 1.
- Produces: `molga::text::TextDiagnosticSink`, exhaustive `StableTextDiagnosticCode`/`ParseStableTextDiagnosticCode` mappings, `TextDependencyConfig::FromEngineTextRoot`, terminal single-lifetime `TextRuntimeDependencies::{Initialize,Shutdown,IsReady,DependencyContractSha256,OutstandingClientHandleCount}`, move-only `TextRuntimeLifetimeGuard`, process-wide start-once/end-once `TextRuntimeTestSession`, fresh-process `molga_text_runtime_probe`, `molga_define_text_runtime_resource_stage(stage_name,destination_root)`, `molga_stage_text_runtime_resources(target_name)`, and `molga_add_text_test(target_name,source_name)`.

- [ ] Add the `TextDiagnostic` contract from Stable Contract Spine plus exhaustive forward/reverse stable string mappings (`TEXT_DEPENDENCY_INVALID`, `TEXT_UTF8_INVALID`, `TEXT_FONT_INVALID`, `TEXT_FONT_FAMILY_INVALID`, `TEXT_MISSING_GLYPH`, `TEXT_ATLAS_EXHAUSTED`, `UI_LAYOUT_INVALID`, `UI_LAYOUT_CYCLE`, `UI_REFERENCE_INVALID`, `TEXT_INPUT_UNAVAILABLE`, `TEXT_INPUT_RANGE_CLAMPED`, `UI_REFLOW_DEFERRED`, `PACKAGE_VALIDATION_FAILED`) and a rate-limited logger adapter keyed by `{code, assetGuid, sceneObjectId, componentType, sourceByteRange}`. An unknown persisted string returns `nullopt` and catalog deserialization fails closed. Tests must prove every enum round-trips, two identical records log once, and distinct source ranges remain distinct.

- [ ] Add failing lifecycle tests around the following exact API. Use a temporary copied portable-contract/data file for tamper tests; never modify the committed data fixture.

  ```cpp
  // src/Text/TextRuntimeDependencies.h
  #pragma once
  #include <cstddef>
  #include <cstdint>
  #include <filesystem>
  #include <memory>
  #include <string>
  #include "Text/TextDiagnostic.h"

  namespace molga::text {
  struct TextDependencyConfig {
      std::filesystem::path dependencyContract;
      std::filesystem::path icuData;
      bool packagedRuntime = false;
      static TextDependencyConfig FromEngineTextRoot(
          const std::filesystem::path&, bool packagedRuntime);
  };
  class TextRuntimeDependencies {
  public:
      static TextRuntimeDependencies& Get();
      bool Initialize(const TextDependencyConfig&, TextDiagnosticSink&);
      void Shutdown();
      bool IsReady() const noexcept;
      const std::string& DependencyContractSha256() const noexcept;
      std::size_t OutstandingClientHandleCount() const noexcept;
  private:
      enum class Lifecycle { NeverInitialized, Ready, TerminallyCleaned };
      struct State;
      Lifecycle lifecycle_ = Lifecycle::NeverInitialized;
      std::unique_ptr<State> state_;
  };
  class TextRuntimeLifetimeGuard {
  public:
      static std::optional<TextRuntimeLifetimeGuard> Create(
          const TextDependencyConfig&, TextDiagnosticSink&);
      TextRuntimeLifetimeGuard(TextRuntimeLifetimeGuard&&) noexcept;
      TextRuntimeLifetimeGuard& operator=(TextRuntimeLifetimeGuard&&) noexcept;
      TextRuntimeLifetimeGuard(const TextRuntimeLifetimeGuard&) = delete;
      TextRuntimeLifetimeGuard& operator=(const TextRuntimeLifetimeGuard&) = delete;
      ~TextRuntimeLifetimeGuard();
  private:
      explicit TextRuntimeLifetimeGuard(bool active) noexcept;
      bool active_ = false;
  };
  } // namespace molga::text
  ```

  ```cpp
  TEST_CASE("ICU data tamper fails before u_init") {
      const auto report = RunTextRuntimeProbe("tampered-data");
      CHECK_FALSE(report.firstInitialize);
      CHECK_FALSE(report.readyBeforeShutdown);
      CHECK(report.icuCallsBeforePublish == 0);
      CHECK(report.diagnosticCodes == std::vector<std::string>{
          "TEXT_DEPENDENCY_INVALID"});
  }
  ```

- [ ] Run the red test.

  ```bash
  cmake --build --preset debug --target test_text_runtime_dependencies -j
  ```

  Expected: compile fails because `TextRuntimeDependencies` does not exist.

- [ ] Implement strict JSON/schema/hash/size verification, aligned read-only data ownership, `udata_setCommonData`, file-access restriction, then `u_init`. A failure before the first ICU call leaves `NeverInitialized` and may be retried; a partial failure after an ICU call performs cleanup and latches `TerminallyCleaned`. Track all ICU/HarfBuzz-owning client handles and reject cleanup while any remain. `TextRuntimeLifetimeGuard` initializes once and, because it is declared before all text services, destroys after them; only then may it call terminal `u_cleanup`, release mapped bytes, and move from `Ready` to `TerminallyCleaned`. `Initialize` from `Ready` or `TerminallyCleaned` returns `DependencyInvalid` before every ICU/HarfBuzz call; `Shutdown` is idempotent. Route every editor/runtime early return after initialization through stack unwinding rather than `exit`/`_Exit`, then return from the process-owned scoped main immediately after cleanup. Pinned `hb_icu_get_unicode_funcs()` caches ICU normalizer pointers process-statically, so no successful lifetime is restarted in process.

- [ ] Add `cmake/StageTextRuntimeResources.cmake`, `molga_define_text_runtime_resource_stage(stage_name,destination_root)`, and `molga_stage_text_runtime_resources(target_name)`, backed by an always-checked target-specific staging dependency. After `molga_text_dependencies_ready`, transactionally publish and reverify the portable contract plus exact ICU data beneath the explicit `Engine/Text` directory; invoke it for editor and development runtime. This CMake-only staging path consumes `${MOLGA_TEXT_DEPENDENCY_CONTRACT}` directly and adds no C++ compile definition. Tests delete/tamper each staged copy and prove initialization fails before any ICU call. It never guesses a fallback root or stages the build lock. Task 16 uses the same explicit-destination helper for `molga_runtime_dev` and final bundle.

- [ ] Give every ready Unicode/font/shaping/layout/cache test process the same one-way lifetime. `molga_add_text_test` builds it with `tests/text_doctest_main.cpp` and `TextRuntimeTestSession`, injects the exact staged `Engine/Text` root, calls `molga_attach_text_dependencies` after target creation, initializes once before doctest, and performs terminal cleanup once immediately before main returns; it never links the old `doctest_main` simultaneously. Make `molga_attach_text_dependencies` idempotent with exact target property `MOLGA_TEXT_DEPENDENCIES_ATTACHED=TRUE`: a repeated call returns before appending libraries, `U_STATIC_IMPLEMENTATION`, or the verification barrier. Add a configure regression that registers `test_text_runtime_session` through the common helper, calls the attach helper again, and requires exactly one `molga_text_harfbuzz`, one `molga_text_rasterizer`, one `U_STATIC_IMPLEMENTATION`, and one `molga_text_dependencies_ready`; require both `MOLGA_TEXT_DEPENDENCIES_ATTACHED=TRUE` and `MOLGA_TEXT_VERIFICATION_BARRIER_ATTACHED=TRUE`, and require the portable/build-lock/source/binary/CMake/verifier-script definitions absent. Existing explicit normal reattachments are then verified no-ops.

  Immediately after the generic parent `test_text_runtime_dependencies` exists, attach `molga_attach_text_portable_contract_consumer` exactly once and define `MOLGA_TEXT_RUNTIME_PROBE="$<TARGET_FILE:molga_text_runtime_probe>"`, `MOLGA_TEXT_RUNTIME_FIXTURE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/Engine/Text"`, and `MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT="$<TARGET_FILE_DIR:molga_engine>/Engine/Text"` only there; add parent dependencies on the child plus both owning staging targets. The child receives no compiled root. As the parent's one portable-path read, `RunTextRuntimeProbe(mode)` first requires the generated contract bytes to equal the staged fixture contract, then canonicalizes the two parent-owned roots, creates a unique canonical temp/report path, and launches without a shell using literal argv `[probe,"--mode",mode,"--fixture-root",fixture,"--development-root",development,"--report",report]`. The child's closed CLI rejects duplicate/unknown switches/modes, noncanonical/symlink roots, and a report that exists or is not a new direct child of its canonical caller root before initialization. The doctest parent never initializes ICU. Unicode and shaping not-ready assertions use separate generic executables with no session/root, so no helper stops/restores a successful process. Add subprocess tests proving a second post-cleanup `Initialize` makes zero ICU calls and editor/runtime nonzero early returns destroy the last text handle before guard shutdown and `u_cleanup`.

- [ ] Wire editor startup to keep the ImGui shell alive but mark Game View text unavailable on failure. Wire `runtime_main.cpp` to print the typed code, failed path/hash, and remediation and return `4` before window, renderer, scripts, assets, or scene startup. Do not use the later `.app` resource-root API yet; pass the existing explicit development root into `TextDependencyConfig`.

- [ ] Run lifecycle, tamper, and full baseline regression gates.

  ```bash
  cmake --build --preset debug --target test_text_runtime_dependencies molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_text_dependencies|test_text_runtime_dependencies)$' --output-on-failure
  ctest --preset debug
  ```

  Expected: both focused tests pass and the current full suite remains green.

- [ ] Commit the runtime lifetime slice.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextDiagnostic.* \
    src/Text/TextRuntimeDependencies.* cmake/TextDependencies.cmake \
    cmake/StageTextRuntimeResources.cmake \
    tests/TextRuntimeTestSession.* tests/text_doctest_main.cpp \
    tests/TextRuntimeDependenciesTestAccess.* tests/text_runtime_probe_main.cpp \
    tests/test_text_runtime_session.cpp \
    src/Common/Log.* src/main.cpp \
    src/runtime_main.cpp tests/test_text_runtime_dependencies.cpp
  git commit -m "feat: verify text runtime dependencies"
  ```

## Task 3: Build byte-preserving Unicode buffers and ICU analysis

**Files:**

- Create: `src/Text/UnicodeTextBuffer.h`
- Create: `src/Text/UnicodeTextBuffer.cpp`
- Create: `src/Text/UnicodeAnalysis.h`
- Create: `src/Text/UnicodeAnalysis.cpp`
- Create: `src/Common/Fixed26_6.h`
- Create: `src/Common/Fixed26_6.cpp`
- Create: `tests/UnicodeTextAnalyzerTestAccess.h`
- Create: `tests/UnicodeTextAnalyzerTestAccess.cpp`
- Create: `tests/test_unicode_text.cpp`
- Create: `tests/test_unicode_not_ready.cpp`
- Modify: `src/Rendering/Utf8.h`
- Modify: `src/Rendering/Utf8.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: ready `TextRuntimeDependencies` and ICU `icuuc`/`icui18n` lifetime from Task 2.
- Produces: `molga::Fixed26_6`, `UnicodeTextBuffer::Build`, `UnicodeTextAnalyzer::Analyze`, `UnicodeAnalysis`, immutable `UnicodeAnalysisIdentity`, `AnalysisItem`, `Utf16Range`, `ScalarRange`, and `GraphemeRange`.

- [ ] Add failing table-driven tests for valid/invalid UTF-8, byte↔scalar↔UTF-16 round trips, surrogate pairs, combining sequences, emoji ZWJ/variation selector boundaries, CRLF/paragraph boundaries, Arabic/Latin script resolution, Thai/CJK line opportunities, and mixed Hebrew/number/punctuation BiDi levels.

  ```cpp
  TEST_CASE("invalid UTF-8 keeps the original byte range") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build(
          std::string("A\xF0\x28\x8C\x28Z", 6), sink);
      REQUIRE(buffer.has_value());
      CHECK(buffer->OriginalUtf8().size() == 6);
      CHECK(buffer->Scalars()[1].value == U'\uFFFD');
      const molga::text::SourceByteRange expectedBytes{1, 2};
      CHECK(buffer->Scalars()[1].sourceBytes.begin == expectedBytes.begin);
      CHECK(buffer->Scalars()[1].sourceBytes.end == expectedBytes.end);
      CHECK(buffer->HadDecodeErrors());
  }

  TEST_CASE("analysis preserves exact embedding levels") {
      auto fixture = AnalyzeFixture(u8"abc אבג 123", "und");
      CHECK(fixture.analysis.Items()[1].embeddingLevel !=
            fixture.analysis.Items()[2].embeddingLevel);
      CHECK(fixture.analysis.GraphemeBoundaries().back() ==
            fixture.buffer.OriginalUtf8().size());
  }
  ```

- [ ] Define the exact owned mapping types.

  ```cpp
  // src/Text/UnicodeTextBuffer.h
  namespace molga::text {
  struct Utf16Range {
      std::uint32_t begin = 0;
      std::uint32_t end = 0;
      friend constexpr bool operator==(Utf16Range a, Utf16Range b) noexcept {
          return a.begin == b.begin && a.end == b.end;
      }
  };
  struct ScalarRange {
      std::uint32_t begin = 0;
      std::uint32_t end = 0;
      friend constexpr bool operator==(ScalarRange a, ScalarRange b) noexcept {
          return a.begin == b.begin && a.end == b.end;
      }
  };
  struct GraphemeRange {
      std::uint32_t begin = 0;
      std::uint32_t end = 0;
      friend constexpr bool operator==(GraphemeRange a,
                                       GraphemeRange b) noexcept {
          return a.begin == b.begin && a.end == b.end;
      }
  };
  struct DecodedScalar {
      char32_t value = U'\0';
      std::uint32_t scalarIndex = 0;
      SourceByteRange sourceBytes;
      Utf16Range utf16Units;
  };
  class UnicodeTextBuffer {
  public:
      static std::optional<UnicodeTextBuffer> Build(
          std::string originalUtf8, TextDiagnosticSink&);
      const std::string& OriginalUtf8() const noexcept;
      const std::u16string& SanitizedUtf16() const noexcept;
      const std::vector<DecodedScalar>& Scalars() const noexcept;
      std::optional<SourceByteRange> SourceBytesForUtf16(Utf16Range) const;
      std::optional<Utf16Range> Utf16ForSourceBytes(SourceByteRange) const;
      bool HadDecodeErrors() const noexcept;
  };
  } // namespace molga::text
  ```

  Both mapping functions accept exact scalar boundaries and the empty range at the corresponding buffer end. They return `nullopt` for reversed/out-of-bounds ranges or a boundary inside a UTF-8 scalar/UTF-16 surrogate pair; tests cover each case explicitly.

  ```cpp
  // src/Text/UnicodeAnalysis.h
  namespace molga::text {
  enum class BaseDirection : std::uint8_t { Auto, LeftToRight, RightToLeft };
  struct TextAnalysisOptions { std::string locale = "und"; BaseDirection baseDirection = BaseDirection::Auto; };
  struct AnalysisItem {
      SourceByteRange sourceBytes;
      Utf16Range utf16Units;
      GraphemeRange graphemes;
      std::int32_t scriptCode = 0;
      std::uint8_t embeddingLevel = 0;
      bool paragraphStart = false;
      bool paragraphEnd = false;
      std::uint32_t logicalRunId = 0;
  };
  struct UnicodeAnalysisIdentity {
      std::string resolvedGraphemeLocale;
      std::string resolvedLineBreakLocale;
      std::string graphemeRuleIdentity;
      std::string lineBreakRuleIdentity;
      std::uint64_t analysisGeneration = 0;
  };
  class UnicodeAnalysis {
  public:
      const std::vector<std::uint32_t>& GraphemeBoundaries() const noexcept;
      const std::vector<std::uint32_t>& LineBreakBoundaries() const noexcept;
      const std::vector<AnalysisItem>& Items() const noexcept;
      const UnicodeAnalysisIdentity& Identity() const noexcept;
  };
  class UnicodeTextAnalyzer {
  public:
      static std::optional<UnicodeAnalysis> Analyze(
          const UnicodeTextBuffer&, const TextAnalysisOptions&, TextDiagnosticSink&);
  };
  } // namespace molga::text
  ```

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_unicode_text -j
  ```

  Expected: compile fails on the new headers/types.

- [ ] Implement `Fixed26_6` with finite checking, signed-zero canonicalization, nearest conversion into the signed 26.6 range, checked add/subtract/multiply-divide helpers, and exact raw comparisons. Then implement strict maximal-subpart UTF-8 replacement with source mapping, UTF-16 conversion, ICU character/line break iterators, script/script-extension resolution, paragraph BiDi, isolate/control boundaries, and AnalysisItem splitting. Store ICU-resolved grapheme/line locales and stable rule/tailoring identities in `UnicodeAnalysisIdentity`; each rule identity hashes the exact `ubrk_getBinaryRules` bytes plus break kind, resolved locale, ICU major, endianness, and character-set family, because the compiled format is not portable across those boundaries. Allocate its process-global generation monotonically without wrap and fail before creating ICU objects on exhaustion. `tests/UnicodeTextAnalyzerTestAccess.{h,cpp}` alone exposes ICU-object creation counts and an RAII generation override; add that companion exactly once to `test_unicode_text` and `test_unicode_not_ready`, assert it is absent from every product target, and link neither test seam into editor/runtime. Canonicalize locale failure to a typed diagnostic; do not normalize text. Cache tests later vary each identity field independently.

- [ ] Keep `DecodeUtf8` only as a compatibility wrapper over the new decoder and mark it non-authoritative in its header. No production consumer migration occurs in this task.

- [ ] Run focused Unicode/ICU tests and sanitizer versions.

  ```bash
  cmake --build --preset debug --target test_unicode_text test_unicode_not_ready test_text -j
  ctest --test-dir build/debug -R '^(test_unicode_text|test_unicode_not_ready|test_text)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_unicode_text -j
  ctest --test-dir build/asan -R '^test_unicode_text$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_unicode_text -j
  ctest --test-dir build/ubsan -R '^test_unicode_text$' --output-on-failure
  ```

- [ ] Commit the Unicode foundation.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Common/Fixed26_6.* \
    src/Text/UnicodeTextBuffer.* \
    src/Text/UnicodeAnalysis.* src/Rendering/Utf8.* tests/test_unicode_text.cpp \
    tests/test_unicode_not_ready.cpp tests/UnicodeTextAnalyzerTestAccess.*
  git commit -m "feat: add byte-preserving Unicode analysis"
  ```

## Task 4: Import static fonts and authored font families

**Files:**

- Create: `src/Assets/FontAsset.h`
- Create: `src/Assets/FontAsset.cpp`
- Create: `src/Assets/FontArtifactStore.h`
- Create: `src/Assets/FontArtifactStore.cpp`
- Create: `src/Assets/FontFamilyAsset.h`
- Create: `src/Assets/FontFamilyAsset.cpp`
- Create: `src/Text/FontRepository.h`
- Create: `src/Text/FontRepository.cpp`
- Create: `src/Core/Importers/FontFamilyImporter.h`
- Create: `src/Core/Importers/FontFamilyImporter.cpp`
- Create: `tests/test_font_assets.cpp`
- Create: `tests/AssetDatabaseTestAuthority.h`
- Create: `tests/TextQualificationAssetTree.h`
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
- Create: `tests/fixtures/text/fonts/NotoSansArabic-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansHebrew-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansDevanagari-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansThai-Regular.ttf.meta`
- Create: `tests/fixtures/text/fonts/NotoSansKR-Regular.otf.meta`
- Create: `tests/fixtures/text/licenses/NotoFonts-ffebf8c1-OFL.txt.meta`
- Create: `tests/fixtures/text/licenses/NotoCJK-Sans2.004-OFL.txt.meta`
- Modify: `src/Core/Importers/FontImporter.h`
- Modify: `src/Core/Importers/FontImporter.cpp`
- Modify: `src/Core/Importers/Importer.h`
- Modify: `src/Core/Importers/ImporterRegistry.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Rendering/RenderQueue.h`
- Modify: `src/Core/PersistentStorage.h`
- Modify: `src/Core/PersistentStorage.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
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

- Consumes: `Common/Sha256`, the text-owned rasterizer target, and `AssetDatabase` importer metadata.
- Produces: typed `ImportResult::importDiagnostics` persisted as `AssetRecord::importDiagnostics`, `molga::FontAsset`, `FontDesignMetrics`, `FontArtifactStorage`, `FontArtifactLocator`, content-addressed `FontArtifactStore`, explicit `AssetCatalogMode`, `FontFamilyAsset`, `FontFamilyImporter`, `FontFaceResource`, `FontRepository`, `FontFace::LoadFromBytes`, `FontFace::GlyphId`, and per-GUID content generations.

- [ ] Add failing importer/repository tests for face selection, static TTF/OTF acceptance, TTC face bounds, variable/color-font rejection, SHA-256 metadata, coverage ranges, license fields, family ordering, malformed references, unknown-field round trips, immutable byte/raster-face lifetime after database mutation, source SHA mismatch, and last-good resource reuse after failed hot reload. Require every `TextDiagnostic` field and two distinct source ranges to survive `ImportResult → AssetRecord → catalog reload`; a free-form `error` or metadata-only code is not sufficient.

  ```cpp
  TEST_CASE("font importer emits package-grade metadata") {
      const auto result = molga::FontImporter().Import(
          MOLGA_TEXT_LATIN_FONT, {
              {"faceIndex", 0},
              {"redistributableConfirmed", true},
              {"licenseKind", "OFL-1.1"},
              {"licenseAssetGuid", MOLGA_TEXT_OFL_GUID}});
      REQUIRE(result.success);
      CHECK(result.metadata["font"]["sourceSha256"] == MOLGA_TEXT_LATIN_SHA256);
      CHECK(result.metadata["font"]["staticOutline"] == true);
      CHECK(result.metadata["font"]["faceIndex"] == 0);
      CHECK(result.metadata["font"]["redistributableConfirmed"] == true);
  }
  ```

- [ ] Extend the generic importer result and asset record with `std::vector<molga::text::TextDiagnostic> importDiagnostics`. Importers emit typed diagnostics while retaining `error` only as a compatibility summary. `AssetDatabase` attaches its asset GUID only when the diagnostic field is empty, never overwrites importer context, and serializes/restores the entire record: stable code string, severity, subsystem, message, remediation, asset GUID, scene object ID, component type, and source-byte begin/end. Expose the records unchanged to the Task 15 Inspector, rate-limit key, and Task 17 build preflight.

- [ ] Introduce these persisted models; numeric values are canonical integer ranges, not arbitrary floats.

  ```cpp
  // src/Assets/FontAsset.h
  namespace molga {
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
  struct ScaledFontDesignMetrics {
      Fixed26_6 ascent = Fixed26_6::FromRaw(0);
      Fixed26_6 descent = Fixed26_6::FromRaw(0);
      Fixed26_6 lineGap = Fixed26_6::FromRaw(0);
  };
  std::optional<ScaledFontDesignMetrics> ScaleFontDesignMetrics(
      const FontDesignMetrics&, Fixed26_6 fontSize) noexcept;
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
  } // namespace molga
  ```

  ```cpp
  // src/Assets/FontArtifactStore.h
  namespace molga {
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
          std::vector<PackagedAuthority>, text::TextDiagnosticSink&);
      std::optional<VerifiedFontArtifact> Publish(
          const std::filesystem::path& sourcePath,
          std::string_view expectedSourceSha256,
          text::TextDiagnosticSink&) const;
      std::optional<std::shared_ptr<const std::vector<std::uint8_t>>>
      ReadVerified(const VerifiedFontArtifact&, text::TextDiagnosticSink&) const;
      bool IsProjectAuthorityFor(
          const std::filesystem::path& projectRoot) const noexcept;
  };
  enum class AssetCatalogMode : std::uint8_t { Project, SealedPackage };
  } // namespace molga
  ```

  ```cpp
  // src/Text/FontRepository.h
  namespace molga { class FontFace; }
  namespace molga::text {
  struct FontFaceResource {
      std::shared_ptr<const molga::FontAsset> asset;
      std::shared_ptr<const std::vector<std::uint8_t>> bytes;
      std::shared_ptr<const molga::FontFace> rasterFace;
      std::string sourceSha256;
      std::string artifactSha256;
      molga::FontArtifactLocator artifactLocator;
      molga::FontDesignMetrics designMetrics;
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
  } // namespace molga::text
  ```

  ```cpp
  // src/Assets/FontFamilyAsset.h
  namespace molga {
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
  } // namespace molga
  ```

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_font_assets -j
  ```

  Expected: compile fails because the asset models/importer do not exist.

- [ ] Upgrade `FontImporter` to version `2`. Read `faceIndex`, canonical weight `1..1000`, stretch `50..200`, slant, redistribution confirmation, and license asset GUID from preserved meta settings. Compute SHA-256 with `Common/Sha256`, enumerate cmap coverage, and inspect SFNT tables. Parse big-endian `unitsPerEm`, signed ascender/descender/lineGap, enforce their exact ranges/order with checked arithmetic, and reject malformed metrics before publication. Reject unsupported face indices, non-outline faces, color tables (`COLR`, `CPAL`, `CBDT`, `CBLC`, `sbix`, `SVG `), and variation tables (`fvar`, `gvar`, `CFF2`) with stable diagnostic codes.

- [ ] On successful project import, atomically publish the exact verified source bytes to immutable `Library/Imported/Fonts/<sha>.sfnt` before replacing the catalog record or generation. Persist `{ProjectLibrary,path,sourceSha256,artifactSha256,byteSize}` and require both SHAs equal; failed artifact/catalog publication preserves the complete last-good record. `AssetDatabase` binds one project artifact store before scan. A corrupt authoring source before first preview or after restart must still load the persisted artifact, while a corrupt artifact fails typed and never reopens the source.

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

- [ ] Add `.fontfamily` JSON import through `FontFamilyImporter`, preserving authored face/fallback order and unknown top-level fields and using only the exact integer key `schemaVersion`. `FontFamilyAsset::guid` comes only from the validated 32-hex `AssetRecord.guid` supplied by the adjacent `.fontfamily.meta`; reserve and reject a source-level `guid` so source JSON cannot override asset identity. Commit source/sidecar fixture pairs for primary, Arabic, CJK, and both cycle families plus deterministic `FontImporter` sidecars for all six pinned faces and `GenericImporter` sidecars for both pinned license records. The primary family owns Latin, Hebrew, Devanagari, and Thai faces in authored order and falls back to the Arabic and CJK families, so the exact same clean-checkout closure serves resolver, package, parity, and performance tests. Make tests resolve all family/font/license GUIDs from sidecars after a real `AssetDatabase` rescan. Validate field ranges and reference GUID syntax at import; defer graph cycles and cross-asset existence to the resolver/package validator. Persist importer metadata through the existing `ImportResult.metadata` and catalog path without converting the existing generic FNV `AssetRecord.hash` into a content-security hash.

- [ ] Change `FontFace` to own shared immutable bytes and an explicit face index while keeping its old codepoint API temporarily for existing callers.

  ```cpp
  bool LoadFromBytes(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                     std::uint32_t faceIndex,
                     std::string* errorOut = nullptr);
  std::uint32_t FaceIndex() const noexcept;
  std::uint32_t GlyphId(char32_t codepoint) const noexcept;
  bool HasCodepoint(char32_t codepoint) const noexcept;
  ```

- [ ] Scale imported design metrics only through `CheckedMulDiv(fontSize,metric,unitsPerEm)` into canonical 26.6 ascent, positive descent, and line gap. Reject invalid/overflow results and never use the legacy float rasterizer `Metrics()` for line geometry or GDEF scaling.

- [ ] In `AssetDatabase`, invalidate font/family generations only after a successful artifact-plus-record publication and keep the last-good imported record on hot-reload failure. Add mode-bearing `LoadCatalog(path,storageRoot,AssetCatalogMode,errorOut)`, `BindFontArtifactStore`, and `FontArtifacts`; project catalogs accept only `ProjectLibrary`, while sealed catalogs accept only a Task 17 manifest-authorized normalized `PackagedResource` under `Assets/`. Expose a nonwrapping `ContentGeneration(guid)` to later cache keys; do not make a failed import look successful.

- [ ] Update every existing `LoadCatalog` and `ScanProject` caller in this same
  API-change slice. Editor startup binds
  `ForProject(explicitProjectRoot)` before scanning the separate
  `projectRoot/Assets`. GameBuilder uses `BuildSettings::projectRoot`; it reuses
  an existing singleton store only when
  `FontArtifacts()->IsProjectAuthorityFor(settings.projectRoot)` succeeds,
  binds once only for a fresh standalone database, and fails a mismatch without
  rebinding. Runtime passes the explicit storage root plus catalog mode (Task
  17 later swaps only its authority construction to sealed mode).
  Every current animation/asset/audio/editor/font/post-process/GPU/tilemap test
  binds a temporary project store before its first scan, and catalog tests use
  the four-argument mode-bearing load. Add unbound, rebind, and mismatched-mode
  failures; never strip an `Assets` suffix or use the current directory as an
  authority.

  Singleton-using test targets share one process-lifetime explicit project
  root through exact helper
  `test_support::AssetDatabaseTestAuthority::{Get,ProjectRoot,AssetsCaseRoot,Bind}`, where
  `Get()` is an inline function-local static, `AssetsCaseRoot` uses a checked
  never-wrapping counter and rejects escape/collision, and `Bind` either binds
  once or calls `IsProjectAuthorityFor`. Tests create per-case subtrees below its
  `Assets` directory and reuse-verify the same store. `AssetDatabase::Clear`
  clears records/scan state but retains the immutable store binding. A two-case
  test proves no rebind/cross-case records; no `MOLGA_TESTING` reset symbol is
  added to the production static library.

- [ ] Implement `FontRepository` as the only source of production font bytes and raster faces. Resolve the GUID to `FontAsset`, obtain the database-bound store, and call only `ReadVerified` with its locator/SHA/size; `FontRepository.cpp` may not call `AbsoluteSourcePath` or open an authoring source. Verify face index, construct one immutable `FontFace` from those exact shared bytes, and cache `FontFaceResource` by `{guid,contentGeneration,sourceSha256,artifactSha256,storage,relativePath,byteSize,faceIndex}`. Existing shared owners remain valid after replacement. In sealed mode `ForSealedPackage(RuntimeResourceRoot, manifest authorities)` requires manifest path/SHA == catalog locator/SHA == the sole staged SFNT; project `Library` copies, unmanifested `Assets` files, stale cache, and system/source fallback are terminal typed failures.

- [ ] Implement `IsProjectAuthorityFor` as a component-wise comparison of the
  same canonical, symlink-safe project root used by project artifact reads. It
  returns false for sealed stores and never exposes the private root or compares
  unnormalized strings.

- [ ] Run focused asset/import/font tests.

  ```bash
  cmake --build --preset debug -j
  ctest --preset debug --output-on-failure
  ```

  Expected: all four tests pass; unsupported fonts fail with their stable code and valid fixtures retain SHA/license metadata.

- [ ] Commit the asset schema slice.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Assets/FontAsset.* \
    src/Assets/FontFamilyAsset.* src/Assets/FontArtifactStore.* \
    src/Core/Importers/FontImporter.* \
    src/Core/Importers/FontFamilyImporter.* src/Core/Importers/Importer.h \
    src/Core/Importers/ImporterRegistry.cpp \
    src/Core/AssetDatabase.* src/Core/PersistentStorage.* \
    src/Text/FontRepository.* src/Rendering/FontFace.* \
    src/main.cpp src/runtime_main.cpp src/Editor/GameBuilder.cpp \
    tests/test_font_assets.cpp tests/AssetDatabaseTestAuthority.h \
    tests/TextQualificationAssetTree.h \
    tests/fixtures/text/families \
    tests/fixtures/text/fonts/*.meta tests/fixtures/text/licenses/*.meta \
    tests/test_animation.cpp tests/test_asset_catalog.cpp \
    tests/test_asset_database.cpp tests/test_asset_reference_migration.cpp \
    tests/test_audio.cpp tests/test_editor_property_descriptor.cpp \
    tests/test_editor_undo_dirty.cpp tests/test_font.cpp \
    tests/test_post_process.cpp tests/test_rendering_sdlgpu.cpp \
    tests/test_tilemap.cpp
  git commit -m "feat: import static fonts and font families"
  ```

## Task 5: Shape deterministic fallback runs with HarfBuzz

**Files:**

- Create: `src/Text/FontFamilyResolver.h`
- Create: `src/Text/FontFamilyResolver.cpp`
- Create: `src/Text/TextShapingService.h`
- Create: `src/Text/TextShapingService.cpp`
- Create: `tests/test_font_family.cpp`
- Create: `tests/test_text_shaping.cpp`
- Create: `tests/test_text_shaping_not_ready.cpp`
- Create: `tests/fixtures/text/expected/shaping.json`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `UnicodeTextBuffer`, `UnicodeAnalysis`, `FontAsset`, `FontFamilyAsset`, immutable bytes from `FontRepository`, explicit-face `FontFace`, and `hb_icu_get_unicode_funcs()`.
- Produces: `FontFamilyResolver::BuildCandidates`, `BuildLegacySingleFace`, `TextShapingService::ShapeAnalysisItem`, `ResolvedFamilyNode`, `ResolvedFamily`, `ShapeStyle`, `ShapedRun`, and `ShapedGlyph`.

- [ ] Add a small test-only reference executable inside `test_text_shaping.cpp` that calls the pinned HarfBuzz C API directly, writes stable glyph/cluster records for the committed fixture corpus, and review those records into `expected/shaping.json`. The production wrapper is compared byte-for-byte to this direct-API record; no system `hb-shape` binary is allowed.

- [ ] Add failing tests for lexicographic face selection, authored depth-first fallback with first-visit cycle termination, missing GUID diagnostics, extended-grapheme atomic fallback, default-ignorable/variation-selector handling, target-only `.notdef` rejection, context re-shaping after face selection, exact embedding-level boundaries, Arabic/Indic contextual forms, Latin `fi`, and invalid UTF-8 source-byte clusters.

  ```cpp
  TEST_CASE("fallback never splits an extended grapheme") {
      FixtureTextContext context = LoadTextFixture(u8"Aक्‍षB", "und");
      const auto shaped = context.shaper.ShapeAnalysisItem(
          context.buffer, context.analysis, context.analysis.Items().at(0),
          context.family, context.style, {}, context.diagnostics);
      REQUIRE(shaped.has_value());
      CHECK(AllGlyphsForGraphemeUseOneFace(*shaped, 1));
      CHECK(AllClustersMapToOriginalBytes(*shaped));
  }
  ```

- [ ] Define the candidate and output spine.

  ```cpp
  // src/Text/FontFamilyResolver.h
  namespace molga::text {
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
  } // namespace molga::text
  ```

  ```cpp
  // src/Text/TextShapingService.h
  namespace molga::text {
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
      TextClusterPolicy clusterPolicy = TextClusterPolicy::MonotoneCharacters;
  };
  struct ShapeBoundaryFlags { bool beginningOfText = false; bool endOfText = false; };
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
  struct ShapedRun { std::vector<ShapedGlyph> glyphs; std::uint8_t bidiLevel = 0; };
  class TextShapingService {
  public:
      std::optional<std::vector<ShapedRun>> ShapeAnalysisItem(
          const UnicodeTextBuffer&, const UnicodeAnalysis&, const AnalysisItem&,
          const ResolvedFamily&, const ShapeStyle&, ShapeBoundaryFlags,
          TextDiagnosticSink&);
  };
  } // namespace molga::text
  ```

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_font_family test_text_shaping -j
  ```

  Expected: compile fails on the resolver/shaper types.

- [ ] Implement deterministic candidate sorting and authored-order DFS closure. Load each candidate through `FontRepository`, retain its complete immutable `FontFaceResource` in `ResolvedFace`, and reject metadata/byte/face mismatches before shaping. Detect cycles/invalid GUIDs as diagnostics, terminate editor preview by first visit, and retain enough graph-generation state to invalidate all consumers when any descendant face changes.

  Define `ResolvedFace::fontRevision` as the content-derived string
  `<artifactSha256>:<faceIndex>` after verifying the artifact SHA equals the
  recorded source SHA and preserving locator/byte-size authority; keep
  `FontFaceResource::contentGeneration` as a separate process-local cache
  invalidator. Never serialize the generation into canonical parity output.

- [ ] Create HarfBuzz blobs/faces/fonts directly from `ResolvedFace::resource->bytes` and its exact face index, retain that resource for the full HarfBuzz object lifetime, and copy the same `FontFaceResourcePtr` into every emitted `ShapedGlyph`. Public headers use only `TextClusterPolicy`; `TextShapingService.cpp` alone maps it to `HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS`, so the private HarfBuzz usage requirement never leaks into cache/editor headers. Assign `hb_icu_get_unicode_funcs()`, explicit direction/script/language/size/features, original UTF-8 byte starts, and exact BOT/EOT flags. Probe the complete sanitized AnalysisItem for each target grapheme and reject only `.notdef` output intersecting that target. No shaper may reopen a path or ask `AssetDatabase` for bytes.

- [ ] Finish face selection for every grapheme before publishing output. Coalesce maximal adjacent spans only when immutable face/revision, level, script, direction, language, size, cluster policy, ordered features, and paragraph/isolate/control boundaries agree. For each selected span, feed the full sanitized containing context through `hb_buffer_add_codepoints(fullArray,fullCount,spanOffset,spanCount)`, replace input clusters with original scalar byte starts, and call `hb_shape` exactly once. Candidate probes never become output. Validate selected grapheme spans and emitted glyph records form nonoverlapping covers, with an Arabic contextual/ligature fixture proving adjacent same-fallback graphemes produce one span and no duplicate/dropped glyph.

- [ ] Every normal or missing glyph copies the exact positive `ShapeStyle::fontSize`; layout, atlas-key construction, and rendering never infer it from advance, bitmap bounds, or affine scale. During each final selected-span shape, query adjusted GDEF ligature carets from that exact scaled `hb_font_t`, direction, and glyph ID. Checked-convert the returned positions to canonical 26.6 and retain them on `ShapedGlyph`; a valid zero count is an empty vector, while allocation/count/status inconsistency is `LayoutInvalid`. This is the only stage that consults GDEF.

- [ ] Emit procedural `MissingGlyph` records only after every candidate fails. They carry the whole grapheme source range and an advance of exactly `1 em`, computed with checked 26.6 arithmetic; no ASCII renderer or system font call is permitted.

- [ ] Fence `FontFace::Advance`, `Kerning`, and codepoint rasterization so they
  remain compiled only for the unchanged pre-Task-8 legacy renderer. New
  shaping selects coverage from imported `FontAsset::coverage`, exact cmap
  format-14 UVS/default-presentation rules parsed from immutable bytes, and the
  target-scoped HarfBuzz probe; it never calls stb or a `FontFace` codepoint
  helper for fallback. Task 8 deletes the fenced measurement/raster adapter
  atomically. `HasCodepoint`/`GlyphId` may remain as non-fallback inspection
  utilities but are forbidden in shaping/layout/render consumer paths.

- [ ] Run the reference parity and focused sanitizer gates.

  ```bash
  cmake --build --preset debug --target test_font_family test_text_shaping -j
  ctest --test-dir build/debug -R '^(test_font_family|test_text_shaping)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_font_family test_text_shaping -j
  ctest --test-dir build/asan -R '^(test_font_family|test_text_shaping)$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_font_family test_text_shaping -j
  ctest --test-dir build/ubsan -R '^(test_font_family|test_text_shaping)$' --output-on-failure
  ```

- [ ] Commit shaping and fallback.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/FontFamilyResolver.* \
    src/Text/TextShapingService.* src/Rendering/FontFace.* \
    tests/test_font_family.cpp tests/test_text_shaping.cpp \
    tests/test_text_shaping_not_ready.cpp \
    tests/fixtures/text/expected/shaping.json
  git commit -m "feat: shape deterministic font fallback runs"
  ```

## Task 6: Convert the atlas to glyph IDs with GPU-safe frame pinning

**Files:**

- Create: `src/Rendering/GpuRetirementQueue.h`
- Create: `src/Rendering/GpuRetirementQueue.cpp`
- Create: `tests/test_glyph_atlas.cpp`
- Create: `tests/test_gpu_retirement.cpp`
- Modify: `src/Rendering/FontAtlas.h`
- Modify: `src/Rendering/FontAtlas.cpp`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `src/Rendering/RenderQueue.h`
- Modify: `src/Rendering/RenderSystem2D.cpp`
- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/Rendering/Renderer.h`
- Modify: `src/Rendering/Renderer.cpp`
- Modify: `src/Rendering/GraphicsDevice.h`
- Modify: `src/Rendering/GraphicsDevice.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_render_queue.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `tests/test_gpu_retirement.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `FontFace::RasterizeGlyph`, shaped `glyphId`, active `Renderer`/`FrameContext`, and `TextDiagnosticSink`.
- Produces: `GlyphAtlasKey`, `GlyphHandle`, `GlyphAtlasCache::{DefaultResidentBudgetBytes,BeginFrame,GetGlyph,EndCollection,LiveExternalPagePinCount,ReleaseAfterGpuIdle}`, `TextRenderer::{BeginGlyphCollection,GlyphAtlas}` over one renderer-owned cache, `Renderer::RetainUntilFrameComplete`, and fence-backed `GpuRetirementQueue`.

- [ ] Add failing tests for the complete atlas key, glyph-ID rasterization, revision invalidation, multiple face indices, pixel/raster scale separation, 64 MiB accounting, page-level LRU, current-frame pinning, submitted-frame retirement, upload telemetry, saturation tofu, and no dangling texture handle after eviction. Add a render-queue test proving a command lifetime token is handed to the renderer before its corresponding sprite/text draw and does not change `BatchKey` equality.

  ```cpp
  TEST_CASE("in-flight atlas pages survive eviction pressure") {
      FakeGpuFenceSource fences;
      GlyphAtlasFixture fixture(fences, 2 * GlyphAtlasFixture::PageBytes);
      auto first = fixture.ResolveAndSubmitGlyph(10, 1);
      fixture.SubmitFrame(1);
      fixture.FillUntilEviction(2);
      CHECK(first.PageIsResident());
      fences.Signal(1);
      fixture.BeginFrame(3);
      fixture.FillUntilEviction(3);
      CHECK_FALSE(first.PageIsResident());
  }
  ```

- [ ] Add a parallel glyph-ID `GlyphAtlasCache` with this exact logical key and
  API. Keep the old codepoint-facing `FontAtlasCache` isolated for unchanged
  consumers; it must neither call nor populate the new cache, and no
  production consumer switches until Task 8.

  ```cpp
  namespace molga {
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
      bool operator==(const GlyphAtlasKey& other) const;
  };
  struct GlyphAtlasTelemetry {
      std::uint64_t hits = 0, misses = 0, uploads = 0, evictions = 0;
      std::uint64_t residentBytes = 0, peakResidentBytes = 0;
  };
  struct GlyphHandle {
      GlyphInfo glyph;
      std::uint64_t pageIdentity = 0;
      std::shared_ptr<const void> pageLifetime;
      bool proceduralTofu = false;
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
  } // namespace molga
  ```

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_glyph_atlas test_gpu_retirement -j
  ```

  Expected: compile fails on the glyph-key/lifetime contracts.

- [ ] Implement `FontGlyphBitmap FontFace::RasterizeGlyph(std::uint32_t glyphId, std::uint16_t pixelHeight, std::uint16_t rasterScale) const` using only the text-owned stb snapshot for bitmap bounds/bearing. Logical advances/offsets always come from HarfBuzz.

- [ ] Add a fence-backed retirement path: `Renderer::RetainUntilFrameComplete(std::uint64_t pageIdentity, std::shared_ptr<const void>)` validates nonzero identity/active frame and deduplicates that atlas page in the active `FrameContext`; SDL submission acquires a fence and `GpuRetirementQueue` releases tokens only after that fence signals. Poll at frame begin, drain after `SDL_WaitForGPUIdle` on shutdown, and retain indefinitely rather than destroy early if fence acquisition fails.

- [ ] Add a direct old-resource atlas test without changing the production
  `TextRenderer` path: shape a layout, replace the font, derive a
  `GlyphAtlasKey` from one old `ShapedGlyph`, and call
  `GetGlyph(key, *oldGlyph.faceResource->rasterFace, ...)` after forcing a
  miss. Prove the old glyph rasterizes from its retained bytes while a newly
  shaped glyph carries the replacement SHA. Task 8 performs the sole
  production switch.

- [ ] Add `std::uint64_t resourceLifetimeIdentity` plus `std::shared_ptr<const void> resourceLifetime` to `RenderCommand` and make text commands carry their page identity/token there, separately from `BatchKey`. In `RenderSystem2D`, immediately before submitting each actual command to `SpriteBatcher`, call `renderer.RetainUntilFrameComplete(command.resourceLifetimeIdentity, command.resourceLifetime)` when non-null; skipped/culled commands do not retain. A token does not affect sorting, batching, or canonical output. Page LRU can evict only pages with no current collection pin and no retained in-flight token. Empty budget space after legal eviction produces procedural tofu plus `AtlasExhausted`; it never grows beyond the configured budget.

- [ ] Allocate every atlas page a process-lifetime, nonzero, never-reused
  identity with checked increment. Exhaustion returns procedural tofu plus
  `AtlasExhausted` and never publishes zero or a wrapped identity. Keep cache
  ownership separate from external pins: the page record owns its resource,
  retains only a weak reference to the one external lifetime token, and never
  uses `shared_ptr::use_count()` for eviction. Test ID exhaustion and eviction
  after the last external token expires.

- [ ] `LiveExternalPagePinCount` counts only nonexpired weak external page-token
  records in stable page-ID order, excluding cache/current-collection ownership
  and never inspecting `shared_ptr::use_count()`. `ReleaseAfterGpuIdle` returns
  false before mutation while collection is active or that count is nonzero;
  otherwise, only after successful GPU idle/fence drain and release of internal
  command/snapshot owners, it clears page/cache GPU ownership and resident bytes
  while preserving monotonic telemetry and never-reused page IDs.

- [ ] Add one non-nestable RAII glyph-collection scope to `TextRenderer`, which owns exactly one `GlyphAtlasCache atlas_`. Expose exact `GlyphAtlasCache& GlyphAtlas() noexcept` and `const GlyphAtlasCache& GlyphAtlas() const noexcept`; both return that object and `BeginGlyphCollection` begins/ends it. The mutable accessor is limited to startup budget configuration and focused fixtures, qualification reads telemetry through the const accessor, and normal rendering reaches the cache only through `CollectLayout`. Editor `main.cpp`, standalone `runtime_main.cpp`, canonical/offscreen paths, and smoke proof open the scope exactly once after the frame index is fixed and before any world/UI text queue collection; destruction calls `EndCollection` exactly once only after every command has copied its page token, including early-return/error paths. A second begin in the same frame is a tested logic error. Default construction uses the 64 MiB cap, and ordinary correctness tests on every machine assert `residentBytes <= DefaultResidentBudgetBytes` with no unlimited-growth mode.

- [ ] Run CPU lifetime/atlas tests and the SDL_GPU pixel test that submits, pressures, and retires an atlas page.

  ```bash
  cmake --build --preset debug --target test_glyph_atlas test_gpu_retirement test_render_queue test_rendering_sdlgpu -j
  ctest --test-dir build/debug -R '^(test_glyph_atlas|test_gpu_retirement|test_render_queue|test_rendering_sdlgpu)$' --output-on-failure
  ```

- [ ] Commit atlas residency as one atomic consumer-safe slice.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Rendering/GpuRetirementQueue.* \
    src/Rendering/FontAtlas.* src/Rendering/FontFace.* \
    src/Rendering/RenderQueue.h src/Rendering/RenderSystem2D.cpp \
    src/Rendering/Renderer.* src/Rendering/TextRenderer.* \
    src/Rendering/GraphicsDevice.* src/main.cpp src/runtime_main.cpp \
    tests/test_glyph_atlas.cpp \
    tests/test_gpu_retirement.cpp tests/test_render_queue.cpp \
    tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: add frame-pinned glyph atlas residency"
  ```

## Task 7: Lay out authoritative final lines, carets, selections, and caches

**Files:**

- Create: `src/Text/TextLayoutTypes.h`
- Create: `src/Text/TextLayoutService.h`
- Create: `src/Text/TextLayoutService.cpp`
- Create: `src/Text/TextHitTesting.h`
- Create: `src/Text/TextHitTesting.cpp`
- Create: `src/Text/TextLayoutCache.h`
- Create: `src/Text/TextLayoutCache.cpp`
- Create: `tests/test_text_layout.cpp`
- Create: `tests/test_text_cache.cpp`
- Create: `tests/test_text_cache_header.cpp`
- Create: `tests/fixtures/text/expected/layout.json`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `FontFamilyResolver`, `TextShapingService`, `UnicodeTextAnalyzer`, `Fixed26_6`, and the exact cache identity fields in design Section 10.
- Produces: `TextLayoutRequest`, immutable `TextLayout`, `TextLayoutService::Layout`, `TextHitTesting::{HitTest,CaretRects,SelectionRects}`, and `TextLayoutCache` telemetry.

- [ ] Add failing cases for explicit separators, `NoWrap`, ICU-word wrap, grapheme wrap, overlong Overflow/Clip/Ellipsis, max lines, line spacing/alignment, unsafe-to-break boundaries, Arabic/Indic/`fi` final-line reshaping, ellipsis shaping in final context, ICU line BiDi visual order, affinity carets, ligature carets, multi-rect selections, and half-open hit-test midpoint ties.

  ```cpp
  TEST_CASE("every accepted line is shaped in its final context") {
      LayoutFixture fixture = LoadLayoutFixture("final-line-fi");
      const auto layout = fixture.LayoutAtWidth(Fixed26_6::FromRaw(9 * 64));
      REQUIRE(layout.has_value());
      CHECK(fixture.shaper.ParagraphShapeCount() == 1);
      CHECK(fixture.shaper.FinalLineShapeCount() == layout->lines.size());
      CHECK(fixture.CanonicalJson(**layout) == fixture.ExpectedCanonicalJson());
  }
  ```

- [ ] Add one-field-at-a-time cache tests for every identity field from design Section 10, including original-byte collision checks, fallback descendant generation, exact BiDi level, component locale, both `UnicodeAnalysisIdentity` resolved locales/rule identities/nonwrapping generation, BOT/EOT flags, project-owned `TextClusterPolicy`, feature ranges, portable dependency-contract SHA, width/height, overflow/ellipsis, max lines, and visual revision. Public cache headers contain no `hb_*` type or `HB_BUFFER_*` macro. Identical inputs must hit; same hash with different bytes must miss.

- [ ] Register both `test_text_cache` and the isolated one-translation-unit
  `test_text_cache_header` through `molga_add_text_test`; the helper performs
  the idempotent dependency attach, so do not call
  `molga_attach_text_dependencies` separately. The latter test includes only
  `TextLayoutCache.h` and fails compilation if an `hb_*` type or
  `HB_BUFFER_*` macro leaks through the public header.

- [ ] Define the exact public layout types.

  ```cpp
  // src/Text/TextLayoutTypes.h
  namespace molga::text {
  enum class TextWrapMode : std::uint8_t { NoWrap, Word, Grapheme };
  enum class TextOverflowMode : std::uint8_t { Overflow, Clip, Ellipsis };
  enum class TextHorizontalAlignment : std::uint8_t { Left, Center, Right };
  enum class TextVerticalAlignment : std::uint8_t { Top, Middle, Bottom };
  enum class CaretAffinity : std::uint8_t { Upstream, Downstream };
  struct LayoutConstraints {
      std::optional<Fixed26_6> width;
      std::optional<Fixed26_6> height;
  };
  struct ParagraphStyle {
      std::string fontFamilyGuid;
      FontRequest fontRequest;
      ShapeStyle shape;
      TextAnalysisOptions analysis;
      TextWrapMode wrap = TextWrapMode::NoWrap;
      TextOverflowMode overflow = TextOverflowMode::Overflow;
      std::uint32_t maxLines = 0; // zero means unlimited
      Fixed26_6 lineSpacing = Fixed26_6::FromRaw(64);
      TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
      TextVerticalAlignment vertical = TextVerticalAlignment::Top;
      std::string ellipsisUtf8 = u8"…";
  };
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
  struct VisualRun { std::uint32_t logicalRunId = 0; std::uint8_t bidiLevel = 0; std::vector<PositionedGlyph> glyphs; };
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
  struct TextLayout {
      std::vector<TextLine> lines;
      std::vector<CaretStop> caretStops;
      FixedSize intrinsicSize;
      bool clipped = false;
      bool ellipsized = false;
      std::vector<TextValidationFact> validationFacts;
  };
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
  } // namespace molga::text
  ```

  ```cpp
  // src/Text/TextLayoutService.h
  namespace molga::text {
  class TextLayoutService {
  public:
      TextLayoutService(FontFamilyResolver&, TextShapingService&,
                        TextLayoutCache&);
      std::optional<std::shared_ptr<const TextLayout>> Layout(
          const TextLayoutRequest&, TextDiagnosticSink&);
  };
  struct CaretPosition { std::uint32_t boundary = 0; CaretAffinity affinity = CaretAffinity::Downstream; };
  class TextHitTesting {
  public:
      static CaretPosition HitTest(const TextLayout&, FixedPoint);
      static std::vector<FixedRect> CaretRects(const TextLayout&, CaretPosition,
                                               Fixed26_6 thickness);
      static std::vector<FixedRect> SelectionRects(
          const TextLayout&, GraphemeRange logicalSelection);
  };
  } // namespace molga::text
  ```

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_text_layout test_text_cache \
    test_text_cache_header -j
  ```

  Expected: compile fails on layout/cache types.

- [ ] First resolve the content-derived family closure and perform a collision-safe request-index lookup before constructing `UnicodeTextBuffer`, ICU, or HarfBuzz objects. The request key includes the original length/bytes, full authored style/constraints/visual revision, decode/rule/dependency identities, and transitive family content identity. A hit re-emits cached validation facts with the current diagnostic context and returns without ICU/HarfBuzz work.

- [ ] On a miss, implement candidate fitting from paragraph shaping, then re-shape **every** accepted line with real line BOT/EOT context. Preserve each positioned glyph's `FontFaceResourcePtr` so immutable `TextLayout` owns the exact bytes/raster face until its last snapshot/cache consumer releases it. If post-break width exceeds the constraint, backtrack and reshape the previous candidate. If none exists, apply the approved overlong policy. For Ellipsis, shape the complete retained-line-plus-ellipsis candidate on every grapheme-removal attempt and map synthetic clusters to the zero-length truncation boundary; never concatenate a separately shaped token, splice paragraph glyph arrays, or cross unsafe-to-break boundaries.

- [ ] Materialize exact source-byte/grapheme ranges plus imported-and-checked ascent/descent/lineGap/top/bottom/baseline for every line. Scale only `FontDesignMetrics` through `ScaleFontDesignMetrics`; no float raster metrics enter layout. Empty input has one metric-bearing empty line, and a trailing explicit separator creates its metric-bearing empty successor. When no valid face exists, use exact procedural metrics `ascent = 3/4 em`, `descent = 1/4 em`, `lineGap = 0`, and missing advance `1 em`, all through `CheckedMulDiv`; test missing-family, empty, and trailing-line cases. Cache layout-producing replacement/tofu diagnostics as immutable facts; only a hard failure that produces no layout skips storage, and cold/warm calls emit byte-identical contextual diagnostics.

- [ ] Run ICU line BiDi after final breaks, generate visual runs and logical↔visual maps, then validate each final glyph's stored adjusted GDEF caret set against its grapheme count/direction/advance. Store accepted positions as absolute `GlyphInteriorCaret`; otherwise materialize the complete proportional fallback once during layout with checked arithmetic. `TextHitTesting` consumes only these immutable stored positions—never a font, scale, or GDEF API—when producing grapheme carets, half-open selections, and deterministic visual-direction midpoint ties.

- [ ] Implement `TextLayoutCache` with an explicit structured key and byte collision check. Store immutable shared results, dependency/family generations, per-stage hit/miss/shape counters, and bounded ownership. Warm static lookups must not invoke ICU/HarfBuzz again.

- [ ] Review the direct structured output into `expected/layout.json`, then run focused and sanitizer gates.

  ```bash
  cmake --build --preset debug --target test_text_layout test_text_cache \
    test_text_cache_header -j
  ctest --test-dir build/debug -R '^(test_text_layout|test_text_cache|test_text_cache_header)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_text_layout test_text_cache test_text_cache_header -j
  ctest --test-dir build/asan -R '^(test_text_layout|test_text_cache|test_text_cache_header)$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_text_layout test_text_cache test_text_cache_header -j
  ctest --test-dir build/ubsan -R '^(test_text_layout|test_text_cache|test_text_cache_header)$' --output-on-failure
  ```

- [ ] Commit paragraph layout and cache.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextLayoutTypes.h \
    src/Text/TextLayoutService.* src/Text/TextHitTesting.* \
    src/Text/TextLayoutCache.* tests/test_text_layout.cpp \
    tests/test_text_cache.cpp tests/test_text_cache_header.cpp \
    tests/fixtures/text/expected/layout.json
  git commit -m "feat: add deterministic paragraph text layout"
  ```

## Task 8: Migrate UILabel and TextRenderer2D to the shared pipeline

**Files:**

- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/Rendering/FontAtlas.h`
- Modify: `src/Rendering/FontAtlas.cpp`
- Modify: `src/Rendering/FontFace.h`
- Modify: `src/Rendering/FontFace.cpp`
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
- Modify: `tests/test_text.cpp`
- Modify: `tests/test_font.cpp`
- Modify: `tests/test_ui.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `tests/test_text_runtime_dependencies.cpp`
- Modify: `tests/test_world_sort.cpp`
- Modify: `tests/test_camera_output_layout.cpp`
- Modify: `tests/test_scene_serializer.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `TextLayoutService`, `TextHitTesting`, `GlyphAtlasCache`, immutable face resources, and page lifetime tokens from Tasks 6–7.
- Produces: `TextRasterPolicy`, `TextCollectContext`, `WorldRenderCollectionContext`, `TextRenderer::{Init,ShutdownAfterGpuIdle,LayoutService,Layout,CollectLayout,BeginGlyphCollection,GlyphAtlas}`, UILabel schema `2`, TextRenderer2D schema `2`, and the sole production rendered-text path.

  `TextRenderer::Init` borrows its diagnostic sink only for that call and never
  stores a sink pointer/reference; every later operation receives its own sink.

- [ ] Replace the existing silent-fallback assertions with failing tests proving that UILabel and TextRenderer2D emit the same glyph IDs/clusters/baselines for the same text/style, a missing family produces tofu plus a diagnostic, UILabel honors its bounded RectTransform, and world text remains explicit-newline `NoWrap/Overflow` with the existing transform/sort contract.

  ```cpp
  TEST_CASE("UI and world consumers share one shaping result") {
      SharedTextConsumerFixture fixture;
      const auto ui = fixture.CollectLabel(u8"سلام हिन्दी");
      const auto world = fixture.CollectWorldText(u8"سلام हिन्दी");
      CHECK(ui.CanonicalGlyphRecords() == world.CanonicalGlyphRecords());
      CHECK(fixture.ShapeCountForText(u8"سلام हिन्दी") == 1);
  }
  ```

- [ ] Run the consumer migration red gate.

  ```bash
  cmake --build --preset debug --target test_text test_font test_ui test_rendering_sdlgpu -j
  ctest --test-dir build/debug -R '^(test_text|test_font|test_ui|test_rendering_sdlgpu)$' --output-on-failure
  ```

  Expected: the new shared-consumer/tofu assertions fail against the current codepoint/ASCII paths.

- [ ] Extend `UILabel` to schema version `2` with authored `fontFamilyGuid`, locale (`und`), base direction, wrap, overflow, max lines, horizontal/vertical alignment, line spacing, color, and sorting order. Preserve the loaded-format marker when reading legacy `fontGuid`; expose an implicit one-face family view without rewriting the scene on load.

- [ ] Extend `TextRenderer2D` to schema version `2` with `fontFamilyGuid`, locale, and base direction while retaining `fontGuid`, `fontName`, world transform, local logical font size, explicit newline, and world sorting migration behavior. Saving an unmigrated legacy payload must preserve its legacy representation until the later explicit editor migration command runs.

- [ ] Add scene-serializer rows for current schema-2 UILabel/TextRenderer2D,
  untouched legacy `fontGuid`/`fontName` payloads, and explicit migration. A
  deserialize-serialize round trip before the migration command must preserve
  the exact legacy representation; current payloads retain every authored text
  field and explicit migration alone writes schema 2.

- [ ] Replace `TextRenderer` measurement/collection with one shared service API and remove production calls to stb/codepoint advance, kerning, `DecodeUtf8`, and the built-in ASCII texture.

  ```cpp
  struct TextAffine2D {
      float m00 = 1.0f, m01 = 0.0f;
      float m10 = 0.0f, m11 = 1.0f;
      float tx = 0.0f, ty = 0.0f;
      Vector2 Apply(molga::FixedPoint point) const;
  };
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
  `AssetDatabase` by stable reference, then atomically constructs one owned
  `TextServices` aggregate in member order: `FontRepository`,
  `FontFamilyResolver`, `TextShapingService`,
  `TextLayoutCache{TextLayoutCacheLimits::Production()}`, and
  `TextLayoutService` referencing those same preceding members. Reverse member
  destruction releases layout/cache/shaper/resolver/repository before terminal
  ICU cleanup. `Init` stores no sink and publishes no partial aggregate on
  failure. A project/sealed `FontArtifactStore` must be bound before the first
  font-bearing layout. Put the per-call check at the exact owned
  `TextLayoutService::Layout`/repository edge so both a direct injected-service
  call and `TextRenderer::Layout` (which delegates to it) report `FontInvalid`
  with no source/system fallback when absent; a wrapper-only gate is forbidden.
  This allows an editor no-project shell to initialize without granting an
  unbound request implicit authority. `LayoutService()` returns that exact owned
  object. `TextRenderer::Get()` remains the process composition root,
  but each outer frame owner resolves it once and passes the exact
  `TextRenderer&`/`LayoutService()` reference downstream. Components and systems
  may not look up the singleton internally, construct a clone, or cache a second
  service. The renderer likewise owns exactly one `GlyphAtlasCache`; both
  `GlyphAtlas()` overloads and `BeginGlyphCollection` use that same object.
  `ShutdownAfterGpuIdle` runs only after successful GPU idle/retirement. It emits
  `ReferenceInvalid` and returns false before mutation when a collection scope is
  active or `GlyphAtlas().LiveExternalPagePinCount()!=0`; otherwise it requires
  `GlyphAtlas().ReleaseAfterGpuIdle()`, then destroys `TextServices` and returns
  true. The application text-runtime guard performs terminal cleanup last.

- [ ] Delete the Task 6 transition-only `FontAtlasCache`, its codepoint-keyed
  lookup, and the `FontFace` advance/kerning/codepoint-raster/measurement
  compatibility entry points. Keep glyph-ID rasterization through the exact
  retained `FontFaceResource`; retain `HasCodepoint`/`GlyphId` only as
  non-fallback inspection utilities and make the build fail if a shaping,
  layout, or render consumer calls them.

- [ ] Before any migrated consumer fixture constructs a text service, replace
  the existing `test_text`, `test_ui`, and `test_rendering_sdlgpu`
  registrations with `molga_add_text_test`; `test_font` was converted in Task
  4. Preserve every existing compile definition, source, include, SDL/GPU
  property, working directory, timeout, and label after target creation. These
  executables use the one installed `TextRuntimeTestSession` and never link
  `doctest_main` or initialize ICU locally.

  `TextAffine2D::Apply` is exactly `x'=m00*x+m01*y+tx`,
  `y'=m10*x+m11*y+ty` after checked fixed-to-float conversion. Reject all six
  non-finite values with `LayoutInvalid` before emitting a command, transform all
  four glyph/tofu corners, and derive world bounds from those transformed corners.
  UI uses identity plus logical translation. `TextRenderer2D` composes component
  scale times world scale, then world rotation, then world translation, preserving
  negative and nonuniform scales. Task 11 extends this same `TextCollectContext`
  with translated UI draw order and optional final physical scissor; it does not
  add a sink-less overload. One non-nestable RAII glyph-collection scope surrounds
  every world/UI collection entry and closes on every early return.

  Quantize every positive finite UI/world scale once into unsigned Q10.6
  `TextRasterPolicy::rasterScaleKey` in `[1,65535]`; reject zero, nonfinite, and
  overflow. Build each normal glyph key from the retained resource identity,
  glyph ID, exact `ShapedGlyph::fontSize`, and that policy. Compute
  `pixelSize = roundHalfAway(fontSize.Raw() * rasterScaleKey / 4096)` with
  checked arithmetic and require `[1,65535]`; never infer size or raster scale
  from advance or affine.

- [ ] In the current `UISystem` compatibility facade, build one bounded label request from `RectTransform`; in TextRenderer2D build an unbounded request. A normal shaped glyph uses its retained `FontFaceResource`, glyph-atlas handle, and page token. A successful zero-bitmap/non-drawable atlas record (for example a space) emits no command and is never changed into tofu. A missing glyph/null face or atlas-saturation handle instead emits the same deterministic solid-color tofu-box geometry from fixed advance plus line ascent/descent, with no atlas dereference/upload/page token; test a completely missing family and a zero-budget atlas. Task 10 routes label layout into the semantic snapshot, and Task 11 atomically replaces this temporary UI collection path with `UIRenderCollector`; no legacy UILabel collection remains by Task 11. Task 12 only consumes frozen snapshots for interaction.

- [ ] Add global `WorldRenderCollectionContext` forward declaration beside
  `RenderQueue` in `Component.h`, add the two-argument virtual
  `Component::CollectRender(RenderQueue&,const WorldRenderCollectionContext&)`
  whose default delegates to the legacy one-argument hook for non-text
  components, and make `TextRenderer2D` override only the two-argument form.
  Both world traversal overloads require the context and invoke only that
  virtual. Game Output and Scene View pass the caller-owned `TextRenderer&`, its
  exact `LayoutService()`, the current diagnostic sink, and camera-derived base
  raster policy. A reachable text component rejects a null renderer/sink before
  layout or atlas work and never calls `TextRenderer::Get()` internally;
  `NonTextOnlyForTesting()` is accepted only after a fixture proves no active
  `TextRenderer2D` exists.

- [ ] Preserve startup/unwind ownership in editor and runtime mains: create the
  text-runtime guard before `TextRenderer::Init(assetDatabase,sink)`, pass the
  same process-owned `AssetDatabase` by stable reference, retain it until after
  renderer shutdown, and bind its project/sealed font-artifact store before the
  first font-bearing layout (the editor may remain an unbound no-project shell).
  On every normal or early return, stop collection,
  complete renderer idle/retirement, call
  `TextRenderer::ShutdownAfterGpuIdle(sink)` and require success before destroying
  renderer/device owners, then let the runtime guard clean up last. Extend
  `test_text_runtime_dependencies` with the exact observed order
  `atlas_cleared < text_services_destroyed < u_cleanup`; a failed idle/drain
  performs none of those teardown markers.

- [ ] In this same atomic migration, remove the Task 4-only direct
  `AssetDatabase` calls/includes for `TextRenderer::{InvalidateFont,
  InvalidateAllFonts}` and delete those legacy renderer methods. A successfully
  published font/family import advances its content generation, already present
  in resolver/layout/resource keys; a failed import leaves that generation and
  the last-good resource unchanged. Prove old/new layouts coexist across a real
  rescan without a Core-to-Rendering callback.

- [ ] Delete `TextRenderer2D::RenderSprite(Renderer*)` and every immediate or
  built-in-font bypass. Remove the legacy `FontAtlasCache`, `FontAtlasGlyph`,
  `CharInfo`, `TextMetrics`, `TextDrawParams`, `FontFace::Metrics`,
  `MeasureText`, `GetTextWidth`, `GetTextHeight`, `RenderText`, and
  `CollectText`, `GetAtlasPageCount`, and `GetCachedFontSizeCount` APIs once all
  consumers use `CollectLayout`; runtime/GPU proofs inspect only the exact
  `renderer.GlyphAtlas().Telemetry()` authority and shaped commands with nonzero
  page identity/token. Do not retain a sink-less compatibility overload.

- [ ] Assert by `rg` that no production consumer still routes text through the legacy fallback.

  ```bash
  rg -n 'GenerateBuiltinFont|FontAtlasCache|FontAtlasGlyph|CharInfo|TextMetrics|TextDrawParams|FontFace::Metrics|GetKerning\(|GetTextWidth\(|GetTextHeight\(|GetAtlasPageCount\(|GetCachedFontSizeCount\(|MeasureText\(|DecodeUtf8\(|RenderText\(|CollectText\(|TextRenderer2D::RenderSprite|InvalidateAllFonts\(|InvalidateFont\(' \
    src/Rendering/TextRenderer.h src/Rendering/TextRenderer.cpp \
    src/Rendering/FontAtlas.h src/Rendering/FontAtlas.cpp \
    src/Rendering/FontFace.h src/Rendering/FontFace.cpp \
    src/UI/UISystem.cpp src/Core/AssetDatabase.h src/Core/AssetDatabase.cpp \
    src/ECS/Components/TextRenderer2D.h \
    src/ECS/Components/TextRenderer2D.cpp src/runtime_main.cpp
  rg -n 'void Shutdown\(\)|TextRenderer::Shutdown\(' \
    src/Rendering/TextRenderer.h src/Rendering/TextRenderer.cpp
  rg -n 'bool Init\(molga::text::TextDiagnosticSink&\)' \
    src/Rendering/TextRenderer.h
  ```

  Expected: no matches.

- [ ] Run focused consumer, GPU, serialization, and complete Debug gates.

  ```bash
  cmake --build --preset debug --target molga_engine molga_runtime \
    test_text test_font test_ui \
    test_scene_serializer test_glyph_atlas test_gpu_retirement \
    test_rendering_sdlgpu test_text_runtime_dependencies \
    test_world_sort test_camera_output_layout -j
  ctest --test-dir build/debug -R '^(test_text|test_font|test_ui|test_scene_serializer|test_glyph_atlas|test_gpu_retirement|test_rendering_sdlgpu|test_text_runtime_dependencies|test_world_sort|test_camera_output_layout)$' --output-on-failure
  ctest --preset debug
  ```

- [ ] Commit the atomic production consumer migration.

  ```bash
  git add CMakeLists.txt src/Rendering/TextRenderer.* src/Rendering/Utf8.* \
    src/Rendering/FontAtlas.* src/Rendering/FontFace.* \
    src/Rendering/RenderQueue.h \
    src/Rendering/WorldRenderTraversal.* src/Rendering/GameOutputRenderer.* \
    src/Editor/Windows/SceneViewWindow.* src/ECS/Component.h \
    src/ECS/Components/UILabel.* src/ECS/Components/TextRenderer2D.* \
    src/UI/UISystem.cpp src/Core/AssetDatabase.* src/main.cpp src/runtime_main.cpp \
    tests/test_text.cpp tests/test_font.cpp \
    tests/test_ui.cpp tests/test_rendering_sdlgpu.cpp \
    tests/test_text_runtime_dependencies.cpp \
    tests/test_world_sort.cpp tests/test_camera_output_layout.cpp \
    tests/test_scene_serializer.cpp tests/CMakeLists.txt
  git commit -m "feat: route all rendered text through shared layout"
  ```

## Task 9: Add stable scene references, world generations, and UI component schemas

**Files:**

- Create: `src/ECS/SceneObjectRef.h`
- Create: `src/ECS/SceneObjectRef.cpp`
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
- Create: `src/ECS/Components/UITextInput.h`
- Create: `src/ECS/Components/UITextInput.cpp`
- Create: `src/ECS/Components/UIAccessibility.h`
- Create: `src/ECS/Components/UIAccessibility.cpp`
- Create: `src/UI/UIRuntimeIdentity.h`
- Create: `src/UI/UIRuntimeIdentity.cpp`
- Create: `src/UI/UIRuntimeInvalidation.h`
- Create: `src/UI/UIRuntimeInvalidation.cpp`
- Create: `src/UI/UINavigationTypes.h`
- Create: `tests/test_ui_components.cpp`
- Create: `tests/test_ui_identity.cpp`
- Modify: `src/Core/World.h`
- Modify: `src/Core/World.cpp`
- Modify: `src/ECS/Component.h`
- Modify: `src/ECS/Component.cpp`
- Modify: `src/Scripting/ScriptField.h`
- Modify: `src/Scripting/Script.cpp`
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

- Consumes: `Component::{GetRuntimeTypeID,GetInstanceID,RemapReferences}`, `World::FindById`, `Fixed26_6`, and text paragraph enums.
- Produces: `SceneObjectRef`, process-global nonwrapping `World::Generation` and component-instance allocation, `CaptureTarget`, `ResolveTarget`, `UIComponent` invalidation revisions, non-wrapping aggregate semantic/scroll/binding/device invalidation clocks, navigation types, and the eight versioned authored UI components.

- [ ] Add failing round-trip/remap tests for every authored field, explicit component schema version, legacy Canvas/UILabel/TextRenderer2D reads, runtime-only omission, prefab clone references, remove/add instance reuse, world replacement, and invalid/non-finite authored values.

  ```cpp
  TEST_CASE("UI identity rejects component replacement") {
      World world;
      auto object = std::make_shared<GameObject>("Button");
      auto* first = object->AddComponent<UIButton>();
      world.Add(object);
      const auto identity = molga::ui::CaptureTarget(world, *first);
      object->RemoveComponent<UIButton>();
      object->AddComponent<UIButton>();
      CHECK(molga::ui::ResolveTarget(world, identity) == nullptr);
  }

  TEST_CASE("runtime text state is never serialized") {
      UITextInput input;
      input.SetInitialText("authored");
      nlohmann::json json;
      input.Serialize(json);
      CHECK(json["initialText"] == "authored");
      CHECK_FALSE(json.contains("runtimeValue"));
      CHECK_FALSE(json.contains("caret"));
      CHECK_FALSE(json.contains("composition"));
  }
  ```

- [ ] Define the one process-global non-wrapping invalidation clock used by all later cache and runtime-state keys.

  ```cpp
  namespace molga::ui {
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
  } // namespace molga::ui
  ```

  Each counter uses checked compare/exchange, publishes the exact unique value
  it returns, and never exposes zero or wraps. Exhaustion leaves the counter at
  `UINT64_MAX`, atomically makes `cacheable=false`, and fails the owning
  publication before mutation.

- [ ] Run the component/identity red gate.

  ```bash
  cmake --build --preset debug --target test_ui_identity test_ui_components -j
  ```

  Expected: compile fails because `SceneObjectRef`, UI identities, and new component types do not exist.

- [ ] Implement `SceneObjectRef` as the one engine-level stable scene ID type and make scripting `ObjectRef` a source-compatible alias.

  ```cpp
  struct SceneObjectRef {
      // Preserve the existing scripting source/serialized member name.
      unsigned int targetId = 0;
      bool IsSet() const noexcept { return targetId != 0; }
      void Clear() noexcept { targetId = 0; }
      unsigned int ObjectId() const noexcept { return targetId; }
      GameObject* Resolve(World&) const noexcept;
      const GameObject* Resolve(const World&) const noexcept;
      void Remap(const std::unordered_map<unsigned int, unsigned int>&);
      friend bool operator==(SceneObjectRef lhs, SceneObjectRef rhs) {
          return lhs.targetId == rhs.targetId;
      }
      friend bool operator!=(SceneObjectRef lhs, SceneObjectRef rhs) {
          return !(lhs == rhs);
      }
  };
  using ObjectRef = SceneObjectRef;
  ```

  Existing C++ using `ObjectRef.targetId`, `IsSet()`, or `Clear()`, existing script snapshots, and serialized `targetId` payloads must compile/read byte-for-byte unchanged. New editor/UI code uses `ObjectId()` when it wants semantic naming; do not add duplicate `objectId` storage or silently rewrite the JSON key.

- [ ] Allocate every `World` generation and every `Component` instance ID from separate process-global checked CAS allocators of never-reused nonzero 64-bit values—not per-object counters or wrapping `fetch_add`. Construction, move construction/assignment used for scene replacement, `Clear`, and successful scene load acquire a fresh world value; component construction acquires its own fresh instance value. Exhaustion throws/fails publication before zero/reuse and emits the typed blocker at the owning boundary. Implement `CaptureTarget`/`ResolveTarget` against all four identity fields and test independently constructed worlds, object-ID reuse, component remove/add, both `SceneObjectRef::Resolve` definitions, and C++17 one-field equality matrices.

- [ ] Add `UIComponent` with a runtime-only authored revision and typed invalidation (`Visual`, `Intrinsic`, `Layout`, `Hierarchy`, `Interaction`). All UI setters validate/canonicalize input and bump the narrowest revision. A revision may never wrap or be reused: exhaustion emits a blocker and makes the component ineligible for cache lookup/insertion. Never serialize revisions or computed state.

- [ ] Implement the eight approved component schemas exactly:

  - `UILayoutElement`: per-axis min/preferred/flexible and `ignoreLayout`.
  - `UILayoutGroup`: horizontal/vertical/grid, padding, spacing, child alignment/control/expand, cell size/start corner/fill axis, and `Flexible/FixedColumns/FixedRows` with positive count.
  - `UIContentSizeFitter`: per-axis `Unconstrained/Min/Preferred`.
  - `UIMask`: enabled rectangular descendant clipping only.
  - `UIScrollView`: viewport/content `SceneObjectRef`, axis enables, clamped/elastic, inertia/deceleration/sensitivity, authored initial normalized position.
  - `UISelectable`: interactable, `None/Auto/Explicit`, four explicit navigation refs.
  - `UITextInput`: initial text, read-only, single/multiline, `maxGraphemes`, content/submit policy, `textViewport`/rendered-label/placeholder refs, family and nested paragraph style.
  - `UIAccessibility`: role, name, description, hidden metadata only.

  The Milestone A enums are closed and exact: `UITextInputContentPolicy { Any }`, `UITextInputSubmitPolicy { OnEnter }`, and `UIAccessibilityRole { None, Panel, Label, Button, TextInput, Image, ScrollView }`. Their serialized strings are the exact PascalCase enumerator names; any unknown numeric/string value fails typed deserialization. Single-line Enter submits, multiline plain Enter inserts newline, multiline Control/Command+Enter submits, and the explicit `Submit` API submits either mode.

- [ ] Upgrade `UICanvas` to current schema with `ConstantPixelSize`/`ScaleWithViewport`, reference resolution, width-height match, and sorting. A legacy payload with `referenceResolution/match/sortingOrder` reads with behavior identical to today and retains a runtime-only `LoadedSchema::Legacy` marker; merely loading/saving does not silently rewrite it.

- [ ] Make every component holding a `SceneObjectRef` override `RemapReferences`. Keep computed rect, baseline, clip, layout revision, hover/pressed/focus/capture, scroll velocity/offset, text value/caret/selection/composition/blink, caches, and atlas state out of scene JSON, prefab overrides, and editor dirty state.

- [ ] Register every new source and component in `CMakeLists.txt` and `BuiltinComponents.cpp`. Extend Prefab normalization to ignore `schemaVersion` and runtime-only keys while still remapping authored references.

- [ ] Run component, serializer, prefab, scripting-reference, and lifecycle gates.

  ```bash
  cmake --build --preset debug --target test_ui_identity test_ui_components test_scene_serializer \
    test_prefab test_script_field_snapshot test_world_lifecycle -j
  ctest --test-dir build/debug -R '^(test_ui_identity|test_ui_components|test_scene_serializer|test_prefab|test_script_field_snapshot|test_world_lifecycle)$' --output-on-failure
  ```

- [ ] Commit authored schemas and identity together.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/ECS/SceneObjectRef.* \
    src/UI/UIRuntimeIdentity.* src/UI/UIRuntimeInvalidation.* \
    src/UI/UINavigationTypes.h src/ECS/Components/UIComponent.h \
    src/ECS/Components/UILayoutElement.* \
    src/ECS/Components/UILayoutGroup.* src/ECS/Components/UIContentSizeFitter.* \
    src/ECS/Components/UIMask.* src/ECS/Components/UIScrollView.* \
    src/ECS/Components/UISelectable.* src/ECS/Components/UITextInput.* \
    src/ECS/Components/UIAccessibility.* \
    src/Core/World.* src/ECS/Component.* \
    src/Scripting/ScriptField.h src/Scripting/Script.cpp \
    src/ECS/BuiltinComponents.cpp src/ECS/Components/UICanvas.* \
    src/ECS/Components/RectTransform.* src/ECS/Components/UIImage.* \
    src/ECS/Components/UIButton.* src/ECS/Components/UILabel.* \
    src/Core/SceneSerializer.cpp src/Core/PrefabUtil.cpp \
    tests/test_ui_components.cpp tests/test_ui_identity.cpp
  git commit -m "feat: add versioned UI authoring schemas"
  ```

## Task 10: Publish deterministic fixed-point UI layout snapshots

**Files:**

- Create: `src/UI/UILayoutTypes.h`
- Create: `src/UI/UILayoutSnapshot.h`
- Create: `src/UI/UILayoutSnapshot.cpp`
- Create: `src/UI/UILayoutSystem.h`
- Create: `src/UI/UILayoutSystem.cpp`
- Create: `tests/test_ui_fixed.cpp`
- Create: `tests/test_ui_layout.cpp`
- Create: `tests/test_ui_snapshot.cpp`
- Modify: `src/Common/Fixed26_6.h`
- Modify: `src/Common/Fixed26_6.cpp`
- Modify: `src/ECS/Components/RectTransform.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UIRuntimeInvalidation.h`
- Modify: `src/UI/UIRuntimeInvalidation.cpp`
- Modify: `src/ECS/GameObject.h`
- Modify: `src/ECS/GameObject.cpp`
- Modify: `src/Core/World.h`
- Modify: `src/Core/World.cpp`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Core/TextureManager.cpp`
- Modify: `src/Text/FontRepository.cpp`
- Modify: `src/Text/FontFamilyResolver.cpp`
- Modify: `src/Text/TextLayoutService.cpp`
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
- Modify: `tests/test_game_view.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: versioned UI components, `TextLayout` intrinsic measurements, `World::Generation`, and checked `Fixed26_6` arithmetic.
- Produces: `UIDrawOrderKey`, immutable semantic `UISnapshot`, `UILayoutNodeSnapshot`, and the interim `UILayoutSystem::Build(World&,WindowId,FixedSize,sink)` with deterministic SCC fallback; a 256-entry-per-world geometry LRU and one latest full snapshot per world/device. Task 11 atomically adds the visual-state provider/text-layout inputs when their types exist and removes this interim overload.

- [ ] Add failing fixed-point tests for NaN/infinity/overflow rejection, negative-zero canonicalization, checked arithmetic, outward physical conversion, and exact sibling-order remainder distribution.

- [ ] Add failing layout tables covering anchors, Canvas scale modes, horizontal/vertical/group/grid intrinsic size, min/preferred/flexible distribution, authored-size children, control/expand, padding/spacing/alignment, fixed/flexible grid counts, content fitter priority, inactive ancestors, hierarchy sibling order, and overflow beyond min size.

- [ ] Add SCC tests where parent group, content fitter, and intrinsic text form cycles. Assert the entire driven-property SCC falls back to authored RectTransform on that axis, emits one typed diagnostic, and produces byte-identical cold/warm/different-edit-history snapshots; never feed last-good geometry into render or hit-test.

- [ ] Define immutable snapshot records and a single order key.

  ```cpp
  namespace molga::ui {
  struct UIDrawOrderKey {
      std::int32_t canvasSortingOrder = 0;
      std::vector<std::uint32_t> siblingPath;
      std::int32_t componentSortingOrder = 0;
      std::uint64_t stableSubmissionIndex = 0;
      bool operator<(const UIDrawOrderKey& other) const;
      bool operator==(const UIDrawOrderKey&) const;
      bool operator!=(const UIDrawOrderKey& other) const {
          return !(*this == other);
      }
  };
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
  struct UILayoutFastPathStamp {
      std::uint64_t worldGeneration = 0;
      molga::WindowId surfaceWindowId = 0;
      molga::FixedSize logicalViewport;
      std::uint64_t viewportGeneration = 0;
      std::uint64_t semanticDirtyGeneration = 0;
      std::uint64_t scrollDisplacementGeneration = 0;
      std::uint64_t textureBindingGeneration = 0;
      std::uint64_t deviceGeneration = 0;
      bool operator==(const UILayoutFastPathStamp&) const noexcept;
  };
  struct UISnapshotWorldDeviceSlotKey {
      std::uint64_t worldGeneration = 0;
      std::uint64_t deviceGeneration = 0;
      bool operator==(const UISnapshotWorldDeviceSlotKey&) const noexcept;
      bool operator!=(const UISnapshotWorldDeviceSlotKey& other) const noexcept {
          return !(*this == other);
      }
  };
  inline constexpr std::size_t kUILayoutGeometryEntriesPerWorld = 256;
  using UISnapshotPtr = std::shared_ptr<const UISnapshot>;
  class UILayoutSystem {
  public:
      UISnapshotPtr Build(
          World&, molga::WindowId surfaceWindowId,
          molga::FixedSize viewport,
          molga::text::TextDiagnosticSink&);
      void OnWorldReleased(std::uint64_t worldGeneration);
  };
  } // namespace molga::ui
  ```

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_ui_fixed test_ui_layout test_ui_snapshot -j
  ```

  Expected: compile fails on layout/snapshot types.

- [ ] Register `test_ui_layout` through `molga_add_text_test` and convert the
  earlier `test_ui_snapshot` registration to the same helper before either
  fixture constructs `TextLayoutService`. Preserve target properties and use
  only the common staged text root/session.

- [ ] Implement active-Canvas DFS in exact sibling order, child-to-parent intrinsic measurement, then parent-to-child arrangement. Apply driver priority `Canvas > parent group > self fitter > authored RectTransform`; `UILayoutElement` supplies constraints only. Use checked fixed-point arithmetic throughout.

- [ ] Implement Horizontal/Vertical/Grid distribution exactly as design Section 8.1, including min preservation, preferred distribution, flexible weights, force-expand zero-to-one, front-to-back one-unit remainders, and authored alignment for leftover space. Detect property SCCs with Tarjan and apply authored-axis fallback.

- [ ] Remove the generation-less vector facade in the same atomic slice.
  Replace `Editor::SetGameObjects(vector*)` with `SetActiveWorld(World&)`, retain
  one non-owning active `World*`, derive hierarchy/camera iteration only via
  `world.Objects()`, and forward that same world to Scene View and Game View.
  Convert Scene View render/hit calls, Game View,
  `GameOutputRenderer::{Render,RenderLogical}`, standalone runtime, and their
  tests to `World&` before deleting every object-vector-only `UISystem`
  overload. A missing/stale world skips the surface; no address-derived or
  generation-zero adapter remains.

- [ ] Split Task 10 caching into a collision-checked geometry key (intrinsic/hierarchy/layout/viewport generations and original ordered values) and the currently expressible semantic-snapshot key (surface window, geometry, and authored visual/content/interaction identities). Task 11 atomically extends the complete key with its newly defined input-visual values and runtime-binding identities; Task 10 must not invent or forward-declare those future records. A tint, texture-content/binding, focusability, navigation, mask, device-generation, or surface-window change may reuse measured/arranged geometry but must miss the full snapshot. Label/input text, font bytes, fallback/style, immutable layout identity, or any other intrinsic-affecting change advances its intrinsic generation and must miss geometry as well; Task 11 adds edit/upload/handle/sampler/lifetime misses. Retain at most `kUILayoutGeometryEntriesPerWorld = 256` geometry entries per world and only the latest full snapshot for each `UISnapshotWorldDeviceSlotKey{worldGeneration,deviceGeneration}`; retired commands/fences or external owners alone may keep older binding lifetimes alive. At the first line of `Build` after surface/viewport validation, compare the scalar fast-path stamp; an exact cacheable match returns the prior shared snapshot without traversal, key/string construction, hashing, or allocation. Exhaustion never wraps and disables lookup/insertion. `UISnapshot` contains no frame index/timestamp; `UIFrameInput/UIFrameResult` carry audit frame/tick identity. Add 600-frame zero-allocation and 4096-mutation bounded-cache tests. Stable JSON excludes pointers, runtime bindings/GPU handles, audit frame/tick values, and physical pixels.

- [ ] Run focused, sanitizer, and legacy UI regression gates.

  ```bash
  cmake --build --preset debug --target molga_engine molga_runtime \
    test_ui_fixed test_ui_layout test_ui_snapshot test_ui test_game_view \
    test_rendering_sdlgpu -j
  ctest --test-dir build/debug -R '^(test_ui_fixed|test_ui_layout|test_ui_snapshot|test_ui|test_game_view|test_rendering_sdlgpu|runtime_smoke)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_ui_layout test_ui_snapshot -j
  ctest --test-dir build/asan -R '^(test_ui_layout|test_ui_snapshot)$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_ui_layout test_ui_snapshot -j
  ctest --test-dir build/ubsan -R '^(test_ui_layout|test_ui_snapshot)$' --output-on-failure
  ```

- [ ] Commit fixed-point layout.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Common/Fixed26_6.* \
    src/UI/UILayoutTypes.h src/UI/UILayoutSnapshot.* src/UI/UILayoutSystem.* \
    src/UI/UIRuntimeInvalidation.* src/ECS/Components/RectTransform.cpp \
    src/ECS/GameObject.* src/Core/World.* src/Core/AssetDatabase.cpp \
    src/Core/TextureManager.cpp src/Text/FontRepository.cpp \
    src/Text/FontFamilyResolver.cpp src/Text/TextLayoutService.cpp \
    src/UI/UISystem.* src/Editor/Editor.* \
    src/Editor/Windows/SceneViewWindow.* \
    src/Editor/Windows/GameViewWindow.* src/Rendering/GameOutputRenderer.* \
    src/main.cpp src/runtime_main.cpp tests/test_ui_fixed.cpp \
    tests/test_ui_layout.cpp tests/test_ui_snapshot.cpp \
    tests/test_game_view.cpp tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: publish deterministic UI layout snapshots"
  ```

## Task 11: Make rendering and scrolling use the same nested clip snapshot

**Files:**

- Create: `src/UI/UIRenderCollector.h`
- Create: `src/UI/UIRenderCollector.cpp`
- Create: `src/UI/UIScrollSystem.h`
- Create: `src/UI/UIScrollSystem.cpp`
- Create: `src/UI/UIDeterministicTick.h`
- Create: `src/UI/UITextInputVisualState.h`
- Create: `src/Rendering/TextureBindingRegistry.h`
- Create: `src/Rendering/TextureBindingRegistry.cpp`
- Create: `tests/test_ui_render_clip.cpp`
- Create: `tests/test_ui_scroll.cpp`
- Modify: `src/UI/UILayoutSnapshot.h`
- Modify: `src/UI/UILayoutSystem.h`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UIRuntimeInvalidation.h`
- Modify: `src/UI/UIRuntimeInvalidation.cpp`
- Modify: `src/Rendering/RenderQueue.h`
- Modify: `src/Rendering/RenderSystem2D.cpp`
- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/Rendering/GraphicsDevice.h`
- Modify: `src/Rendering/GraphicsDevice.cpp`
- Modify: `src/Rendering/Texture.h`
- Modify: `src/Rendering/Texture.cpp`
- Modify: `src/Rendering/Renderer.h`
- Modify: `src/Rendering/Renderer.cpp`
- Modify: `src/Rendering/SpriteBatcher.h`
- Modify: `src/Rendering/SpriteBatcher.cpp`
- Modify: `src/Core/TextureManager.h`
- Modify: `src/Core/TextureManager.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_render_queue.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `tests/test_gpu_retirement.cpp`
- Modify: `tests/test_platform_sdl.cpp`
- Modify: `tests/test_ui_snapshot.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: immutable `UISnapshot`, authored `UIMask`/`UIScrollView`, `TextRenderer::CollectLayout`, and existing renderer scissor support.
- Produces: `UIPhysicalTransform`, `UIRenderItemSnapshot`, multi-stage `UIHitTargetSnapshot`, `UIFrozenTarget`, `UIRuntimeBindingCacheIdentity`, value-only `UITextInputVisualStateProvider`, frozen input-label/IME geometry, `UIRenderCollector::Collect`, `UIDeterministicTick`, `UIScrollSystem`, `RenderCommand::scissor`, and `Renderer::ResetPassScissor`.

- [ ] Add failing tests proving nested rectangular masks intersect in fixed-point, empty intersections remove both render items and hit candidates, unmasked visual overflow remains visible, and floor(min)/ceil(max) happens once at the final physical transform.

- [ ] Add command-stream tests proving a scissor change flushes an active sprite batch, equal adjacent scissors do not flush, leaving a clip restores the full pass scissor, scissor never enters `BatchKey`, and sort order remains Canvas/sibling/component/submission order.

- [ ] Add scroll tests for authored initial normalized position, clamped/elastic movement, axis enable, inertia, deceleration, sensitivity, fixed-step deterministic integration, viewport/content references, nested mask hit parity, and runtime-only offset/velocity serialization omission.

- [ ] Run the clip/scroll red gate.

  ```bash
  cmake --build --preset debug --target test_ui_render_clip test_ui_scroll test_render_queue -j
  ```

  Expected: new clip/scroll tests fail because snapshot render items and command scissors do not exist.

- [ ] Register `test_ui_render_clip` through `molga_add_text_test` before its
  first immutable `TextLayout` fixture; preserve renderer/SDL properties and
  do not create another main, ICU lifetime, or resource root.

- [ ] Extend snapshot records with fully renderable render items and fully targetable hit items. Both store the same already-intersected optional logical clip value and the same `UIDrawOrderKey`; neither consumer may consult authored components to fill missing state.

  ```cpp
  namespace molga { class TextureBindingLifetime; }
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
  struct UISolidRectSnapshot { Color color = Color::White(); };
  using UIRenderPayload = std::variant<
      UISpriteSnapshot, UITextSnapshot, UISolidRectSnapshot>;
  struct UIRenderItemSnapshot {
      UIRuntimeTargetIdentity source;
      UIStableComponentKey canonicalSource;
      UIDrawOrderKey order;
      molga::FixedRect logicalRect;
      std::optional<molga::FixedRect> logicalClip;
      std::uint64_t reservedCommandSpan = 1;
      UIRenderPayload payload;
  };
  struct UIFrozenTarget {
      UIRuntimeTargetIdentity runtimeTarget;
      UIStableComponentKey canonicalTarget;
      explicit operator bool() const noexcept;
      bool operator==(const UIFrozenTarget&) const noexcept;
      bool operator!=(const UIFrozenTarget& other) const noexcept {
          return !(*this == other);
      }
  };
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
  struct UIDeterministicTick {
      std::uint64_t tickIndex = 0;
      molga::Fixed26_6 deltaSeconds = molga::Fixed26_6::FromRaw(0);
      bool operator==(const UIDeterministicTick&) const noexcept;
      bool operator!=(const UIDeterministicTick& other) const noexcept {
          return !(*this == other);
      }
  };
  struct UIPhysicalTransform {
      molga::FixedRect logicalViewport;
      molga::PixelRectU32 physicalViewport;
      std::uint64_t deviceGeneration = 0;
      std::optional<molga::PixelRectU32> ToPhysicalOutward(
          const molga::FixedRect& logicalRect) const noexcept;
      std::optional<molga::FixedPoint> ToLogicalPoint(
          double outputPixelX, double outputPixelY) const noexcept;
      std::optional<TextAffine2D> LayoutToOutputAffine(
          molga::FixedPoint logicalOrigin) const noexcept;
      std::optional<TextRasterPolicy> RasterPolicy(
          molga::text::TextDiagnosticSink&) const;
  };
  class UIRenderCollector {
  public:
      void Collect(const UISnapshot&, const UIPhysicalTransform&,
                   molga::RenderQueue&, TextRenderer&,
                   molga::text::TextDiagnosticSink&) const;
  };
  } // namespace molga::ui
  ```

  Add field-wise C++17 `operator==` and `operator!=` to `PixelRectU32` in
  `GraphicsDevice.h`; compare named fields, never object representation.

  Define the value-only input/provider and frozen input records in the same
  snapshot layer; no field points back into edit-system or component storage:

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
          molga::WindowId, const UIRuntimeTargetIdentity&) const override;
  };
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

  In this Task 11 commit, replace the interim Task 10 overload at every call
  site with the final boundary; no compatibility overload remains:

  ```cpp
  UISnapshotPtr UILayoutSystem::Build(
      World&, molga::WindowId surfaceWindowId, molga::FixedSize,
      const UITextInputVisualStateProvider&,
      molga::text::TextLayoutService&,
      molga::text::TextDiagnosticSink&);
  ```

  Until Task 14 installs the real provider, production and test callers pass
  `EmptyUITextInputVisualStateProvider::Instance()` and the same production
  `TextRenderer::LayoutService()` reference used for ordinary labels. The outer
  owner resolves the renderer/service once; `UILayoutSystem` neither looks up
  `TextRenderer::Get()` nor owns another service.

  Extend `UISnapshotCacheKey` with stable-DFS-ordered
  `std::vector<UITextInputVisualState> inputVisualStates` and
  `UILayoutGeometryCacheKey` with stable-DFS-ordered
  `std::vector<UITextInputGeometryCacheIdentity> inputGeometry`. Build each
  geometry identity from the current committed-plus-composition visible UTF-8
  and complete effective request. Caret/selection/focus/blink/surface revision
  remain full-key-only, while visible text and its effective request are geometry
  authority; collision-compare all original fields after a hash match.

  Preserve one fail-closed host shutdown boundary:

  ```cpp
  enum class EngineShutdownStatus : std::uint8_t {
      Complete, GpuDrainFailed, ExternalGpuLifetime
  };
  EngineShutdownStatus EngineShutdown(
      std::unique_ptr<EngineHost>&,
      molga::text::TextDiagnosticSink&);
  ```

  Only `Complete` resets the host. Either failure emits blocker
  `ReferenceInvalid` and leaves the host/device non-null. The caller records a
  failing eventual exit status, releases only known external snapshot/page
  owners when applicable, and retries through that same host; it may not
  return, throw past the owner scope, destroy the diagnostic sink/text-runtime
  guard, or force-reset while the result is non-`Complete`. A persistent
  failure remains in an explicit fail-closed shutdown-blocked state with all
  GPU/text/ICU owners retained and permits no normal C++ teardown. No
  void/force-reset overload remains. Engine-body exceptions are caught inside
  that owner scope, converted to an eventual failing status, and cannot unwind
  the host before the same shutdown loop reaches `Complete`. In this same
  API-change slice, delete Task 6's interim SIGABRT subprocess expectation and
  `GPU_IDLE_WAIT_FAILED` `std::abort()` branch. A failed idle wait now returns
  `GpuDrainFailed` before any teardown mutation, and a later call can retry;
  abort and status-return behaviors never coexist in the Task 11 commit.

  Create at most one hit record per active object. Choose its action target in
  exact precedence: enabled/interactable `UIButton`; enabled `UIImage`; enabled
  `UILabel`; enabled/interactable `UISelectable`; enabled/usable `UITextInput`.
  Independently freeze the sibling focus/text-input targets and every resolved
  ancestor scroll target inner-to-outer; an undecorated selectable/input shell
  therefore remains interactive without duplicate records, and no router may
  reconstruct roles from live ECS.

  Gather active `UITextInput` rendered/placeholder label claims before ordinary
  label output. A conflicting resolved label is suppressed from ordinary output,
  omits every affected input binding/text-input hit target, and emits one
  `ReferenceInvalid`; an unset/wrong-type/out-of-world/disabled rendered label
  disables only that input's visual/text target, while an invalid/disabled
  placeholder disables placeholder output only. Inactive/disabled inputs claim
  nothing. For a valid binding, build `effectiveInputRequestTemplate` from the
  input's paragraph style, overwrite its embedded family with top-level
  `UITextInput::fontFamilyGuid`, and take only constraints, color, label identity,
  and diagnostic provenance from the rendered label/viewport. Task 14 replaces
  only UTF-8. Copy provider state plus blink-independent logical input/caret/clip
  geometry into the snapshot; never retain provider/edit-system storage.
  Reserve input layers in exact order: selection, complete text glyph/tofu span,
  complete composition underline, then caret. A later item receives its index
  only after the entire preceding span.

  Set `reservedCommandSpan=1` for sprite/solid items and use checked
  `TextRenderCommandSpan(*layout)`—the count of all positioned glyph/tofu records,
  including a normal non-drawable zero-bitmap record—for every text item. Advance
  the following frozen `stableSubmissionIndex` only by that complete span;
  overflow omits the complete source group with `LayoutInvalid`, never a partial
  ordering.

  `UIPhysicalTransform::ToPhysicalOutward` is the sole logical→physical rect
  conversion: translate relative to `logicalViewport`, scale with checked
  rational arithmetic, floor each min edge, ceil each max edge, clamp to
  `physicalViewport`, and return `nullopt` for empty/invalid/overflowed results.
  `ToLogicalPoint` is the sole inverse: require a finite point inside the
  half-open physical viewport, subtract its origin, apply checked inverse
  rational scale, add the logical origin, and round once to 26.6 half-away from
  zero. `RasterPolicy` computes the checked Q10.6 maximum of physical/logical X/Y
  scale and is the one policy reused by every UI text item; it never derives
  raster scale from affine floats. Rendering/scissor uses the rect conversion,
  while pointer mapping uses the inverse point conversion and Game View IME uses
  the same frozen surface mapping built from both.

- [ ] Publish device and texture binding identities before snapshot use. Every
  successfully created graphics device exposes the exact unique nonzero value
  returned by `UIRuntimeInvalidationClock::Advance(Device)` through
  `GraphicsDevice::Generation()`. Each successful texture creation/reupload
  checks and acquires a nonzero per-texture `uploadGeneration`, process-global
  `lifetimeIdentity`, and the unique broad value from
  `Advance(TextureBinding)` before atomically replacing the old binding. Any
  exhaustion destroys the unpublished resource after its fence, retains the
  old binding, disables affected cache insertion, and reports a blocker.

- [ ] Make `TextureBindingRegistry` retain only weak records while each
  texture, immutable snapshot, submitted command, and frame fence owns the
  published lifetime token. `LiveRetainedBindingCount` therefore detects a real
  external owner after caches/manager/commands are explicitly released; never
  infer ownership from `shared_ptr::use_count()`. Add
  `UILayoutSystem::OnDeviceGenerationChanged(oldGeneration,newGeneration)` and
  route it through `UISystem`: require the already-published nonzero new value,
  clear `lastSnapshot_`, every old-device full slot, and old binding scratch,
  but preserve the bounded geometry LRU. No fast path may return an old native
  handle.

  Add stable-order `renderItems`, `hitTargets`, `textInputLabels`, and
  `textInputImeGeometry` vectors to `UISnapshot`; render/hit vectors are sorted by
  `UIDrawOrderKey`. Snapshot construction resolves and pins complete texture
  binding/lifetime, full content SHA/stable ID, and immutable `TextLayout`; the
  lifetime agrees field-for-field with `binding`. Extend the full key with the
  stable-DFS original runtime binding and input visual values while keeping them
  out of geometry/canonical serialization. Device recreation or same-content
  reupload republishes a usable snapshot; old snapshots keep resources alive
  through command/fence retirement. `UIRenderCollector` converts only frozen
  geometry/clip. The stable serializer includes stable component/role targets,
  portable texture identity, immutable layout, rect/clip, input ownership/
  navigation, payload kind, span, and order; it excludes runtime/window/device/
  GPU/edit identities and lifetime tokens.

  Shutdown submits pending work and waits for GPU idle before releasing owners;
  a failed wait performs no teardown, cache, or owner-release mutation and keeps
  the submitted work owned for retry. After the first successful drain, call
  `Renderer::ReleaseCompletedGpuLifetimes()`, then clear full-snapshot caches,
  latest frame results, commands, and TextureManager owners. Only then classify
  both `TextureBindingRegistry::LiveRetainedBindingCount(deviceGeneration)` and
  `TextRenderer::GlyphAtlas().LiveExternalPagePinCount()` as external. Either
  nonzero returns `ExternalGpuLifetime` before further destruction and retains
  the completed phases for retry; neither check uses `shared_ptr::use_count()`.
  Otherwise require `TextRenderer::ShutdownAfterGpuIdle(sink)` (atlas release then
  text-service destruction), destroy retired texture bindings, renderer device
  resources, and finally the graphics device. Never force-reset a failed host;
  only `Complete` resets it.

- [ ] Extend the sole Task 8 `TextCollectContext` in place with exact fields
  `TextAffine2D layoutToOutput`, `Color color`, `int cameraPass`,
  `int sortingLayer`, `int sortingOrder`, `float depthOrYSort`,
  `TextRasterPolicy rasterPolicy`, `optional<UIDrawOrderKey> uiDrawOrder`,
  `uint64_t stableSubmissionBase`, and `optional<PixelRectU32> scissor`. Keep the
  sole sink-bearing
  `TextRenderer::CollectLayout(RenderQueue&,const TextLayout&,const TextCollectContext&,TextDiagnosticSink&)`
  overload. `UIRenderCollector` obtains affine and raster policy exactly once
  from `UIPhysicalTransform`, drops the complete item before atlas work if either
  fails, and copies item order/base/scissor into the context. Assign each emitted
  command checked ordinal `stableSubmissionBase + positionedGlyphOrdinal`.
  A non-drawable zero-bitmap glyph consumes its reserved ordinal but emits no
  command; require the visited positioned-glyph/tofu record count—not emitted
  command count—to equal `reservedCommandSpan`, with every emitted ordinal unique
  and in range. On mismatch/overflow emit `LayoutInvalid` and submit no partial
  label. The API may not drop clip/order/sink or recompute order, font size, or
  raster scale from a component/affine. Add mixed image/text tests proving exact
  nested scissor, affine placement, and UI draw rank, and reject cross-device
  sprite bindings when
  `binding.deviceGeneration != physicalTransform.deviceGeneration`.

- [ ] Add `std::optional<PixelRectU32> scissor` to `RenderCommand`; do not add it to `BatchKey`. Add `Renderer::ResetPassScissor()` using the active pass viewport. In `RenderSystem2D`, compare scissor state before every command, flush before a change, set/reset the renderer state, and restore full scissor before ending the batch.

- [ ] Implement `UIScrollSystem` runtime tables keyed by complete UI identity. Apply signed X/Y wheel/drag and exact gamepad-axis input only to the frozen inner-to-outer consumer chain. Inertia advances only from checked `UIDeterministicTick` values supplied identically by runtime, Game View, and canonical replay; no UI subsystem samples wall time. Scroll changes mark arrangement/render dirty but not text shape dirty. Invalid runtime refs disable only that feature and emit a rate-limited diagnostic.

- [ ] Run CPU command, UI, and SDL_GPU clip tests.

  ```bash
  cmake --build --preset debug --target molga_engine molga_runtime \
    test_ui_render_clip test_ui_scroll test_render_queue \
    test_rendering_sdlgpu test_gpu_retirement -j
  ctest --test-dir build/debug -R '^(test_ui_render_clip|test_ui_scroll|test_render_queue|test_rendering_sdlgpu|test_gpu_retirement)$' --output-on-failure
  ```

- [ ] Commit clip-aware render and scroll state.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UIRenderCollector.* \
    src/UI/UIScrollSystem.* src/UI/UIDeterministicTick.h \
    src/UI/UITextInputVisualState.h \
    src/UI/UILayoutSnapshot.h src/UI/UILayoutSystem.* \
    src/UI/UISystem.* src/UI/UIRuntimeInvalidation.* \
    src/Rendering/RenderQueue.h src/Rendering/TextRenderer.* \
    src/Rendering/GraphicsDevice.* src/Rendering/Texture.* \
    src/Rendering/TextureBindingRegistry.* src/Core/TextureManager.* \
    src/Core/Bootstrap.* src/main.cpp src/runtime_main.cpp \
    src/Rendering/RenderSystem2D.cpp \
    src/Rendering/Renderer.* src/Rendering/SpriteBatcher.* \
    tests/test_ui_render_clip.cpp tests/test_ui_scroll.cpp \
    tests/test_render_queue.cpp tests/test_rendering_sdlgpu.cpp \
    tests/test_gpu_retirement.cpp \
    tests/test_platform_sdl.cpp tests/test_ui_snapshot.cpp
  git commit -m "feat: share nested clips across UI render and input"
  ```

## Task 12: Freeze interaction snapshot N across input, focus, and callbacks

**Files:**

- Create: `src/Platform/TextInputOwner.h`
- Create: `src/Platform/NativeKeyModifiers.h`
- Create: `src/UI/UIInputEvent.h`
- Create: `src/UI/UIInputRouter.h`
- Create: `src/UI/UIInputRouter.cpp`
- Create: `src/UI/UIFocusSystem.h`
- Create: `src/UI/UIFocusSystem.cpp`
- Create: `src/UI/UIFixedTickClock.h`
- Create: `src/UI/UIFixedTickClock.cpp`
- Create: `tests/test_ui_input.cpp`
- Create: `tests/test_ui_focus.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UIScrollSystem.h`
- Modify: `src/UI/UIScrollSystem.cpp`
- Modify: `src/UI/UILayoutSnapshot.h`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/Rendering/TextRenderer.h`
- Modify: `src/ECS/Components/UIButton.h`
- Modify: `src/ECS/Components/UIButton.cpp`
- Modify: `src/Rendering/GameOutputRenderer.h`
- Modify: `src/Rendering/GameOutputRenderer.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_game_view.cpp`
- Modify: `tests/test_ui.cpp`
- Modify: `tests/test_ui_scroll.cpp`
- Modify: `tests/test_scene_runtime.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `UISnapshot`, `UIPhysicalTransform`, full runtime identities, scroll state, and ordered `UIInputEvent` values.
- Produces: shared value-only `TextInputOwnerStamp`, `NativeKeyModifiers`, global `UISystem::{ProcessFrame,CollectRender,OnWorldReleased}`, `UIInputEvent`, multi-stage `PlannedUIEvent::TargetFor`, primary-target `UIEventDispatchRecord`, tick-bearing `UIFrameInput`, `UIFrameResult`, and one-event `HandleEvent` APIs on `UIInputRouter`/`UIFocusSystem`/`UIScrollSystem`.

- [ ] Add failing callback mutation tests: hide, disable, reparent, remove/add same component type, reuse object ID in a replacement world, and scene transition. The originally selected full identity is the only legal target; reparented live identity receives its original event, stale identities are skipped, and nothing is retargeted against N+1.

- [ ] Add hit-order/focus tests for exact reverse draw order, parent clip/interactable eligibility from the same snapshot, pointer capture, keyboard/gamepad navigation, explicit refs, automatic spatial navigation, stable sibling/object-ID tie breaks, focus cleanup, and native window focus loss. Add ordered wheel/drag/gamepad-axis tests with nonzero signed deltas/axis values. Leaving Game View presentation clears hover/capture only, not keyboard/gamepad focus.

- [ ] Run the frozen-input red gate.

  ```bash
  cmake --build --preset debug --target test_ui_input test_ui_focus test_ui test_ui_scroll -j
  ```

  Expected: compile or assertions fail because the frame facade, full-identity routing, and focus system do not exist.

- [ ] Define the frame facade and remove production vector overloads.

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
      std::uint64_t traceOrdinal = 0; // nonzero only for canonical replay
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
  enum class UIEventStage : std::uint8_t;
  struct PlannedUIEvent {
      UIInputEvent event;
      bool surfaceEligible = false;
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
  enum class UIEventStage : std::uint8_t {
      Input, Focus, Scroll, TextInput, OwnerTransition
  };
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
  class UIEventDispatchAccumulator {
  public:
      explicit UIEventDispatchAccumulator(const PlannedUIEvent&);
      void Merge(const UIEventHandlerResult&);
      UIEventDispatchRecord Finish() &&;
  };
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

  Define the sole production tick accumulator; runtime and Game View each own
  this same type, while canonical replay supplies already-recorded tick values:

  ```cpp
  namespace molga::ui {
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
  } // namespace molga::ui
  ```

  It uses checked integer addition, emits one
  `deltaSeconds=Fixed26_6::FromRaw(1)` for every complete 1/64-second interval,
  retains the remainder, and fails before the nonzero tick index can wrap. No
  UI subsystem samples wall time or accepts this clock directly.

  The owner stamp is a complete immutable value copied at native ingest. Runtime
  text plans target only its stamped full runtime identity; editor/none stamps
  have no UI target. Projected pointer/key focus must never retarget a pre-ingested
  editing or commit event.

  Every caller passes the exact outer-owned `textRenderer.LayoutService()` into
  `ProcessFrame`; the facade neither calls `TextRenderer::Get()` nor constructs,
  caches, or clones a layout service. Seed `UIPlanningState` from revalidated
  identities plus `pointerAtBatchStart`, its validity, and native focus frozen in
  the same take-once host batch before the first ordered event. An invalid start
  point seeds no hover/capture and is never interpreted as logical zero.

- [ ] Before invoking any callback, map the whole ordered input vector to
  `std::vector<PlannedUIEvent>` against immutable N, storing identities rather
  than pointers. A foreign `event.windowId` is `surfaceEligible=false`,
  targetless, audited, and cannot mutate surface state. A matching
  `PointerMotion`/`PointerButton` with `logicalPointValid=false` is a
  surface-eligible targetless departure: it
  clears projected hover/capture/pointer validity but preserves keyboard/gamepad
  focus and never hit-tests `(0,0)`. A valid pointer event first replaces the
  projected pointer. A `Scroll` event freezes its route from that current valid
  pointer (batch-start or prior event); with no valid pointer it stays targetless
  and never interprets delta/default coordinates as a position. Otherwise copy
  action/focus/text/inner-to-outer-scroll roles and project sequentially, so
  pointer down projects its separate focus role, captured move/up stays on N,
  and later key/navigation sees projected focus. Resolve each navigation destination only from N, store it in
  `focusDestinationFromSnapshotN`, and make `TargetFor(Focus)` return it with
  `focusTargetFromSnapshotN` fallback for non-navigation focus. Text editing/
  commit plans only the exact runtime identity stamped at ingest. `TargetFor` is
  the sole handler target accessor. `UISystem::ProcessFrame` alone walks the
  vectors and calls one-event handlers; no subsystem accepts or re-walks them.

- [ ] Seed one `UIEventDispatchAccumulator` per planned event, merge every subsystem result only when `resolvedStageTarget` equals `TargetFor(stage,ordinal)`, and finish one record even for ignored/stale/foreign input. Primary target rules are exact: stamped text target; action target for pointer/key/gamepad; first consumed scroll target; null for window focus/foreign input. Focus side effects never replace primary. Immediately before a callback resolve all four identity fields and dispatch at most once. Coalesce all dirty requests and validated deterministic ticks received before publication into the sole N+1 rebuild; only a mutation observed after publication remains dirty and reports `ReflowDeferred`. Never retarget or rebuild N twice.

- [ ] Track take-once surface replay watermarks as `lastCompletedNativeSequenceBySurface_[{windowId,worldGeneration}]` and `lastCompletedTickBySurface_[{windowId,worldGeneration}]`. First require `UIFrameInput.windowId == snapshotN.surfaceWindowId`; a mismatch rejects the complete surface frame before projection. Validate the entire native vector before any callback: every sequence is nonzero, strictly increasing within the batch, and greater than that surface watermark. One invalid/duplicate/decreasing/replayed entry discards every stored plan and dispatches zero callbacks; never accept a prefix. Validate ticks independently: a zero/duplicate/decreasing index or raw delta outside `[1,64]` skips the complete tick batch with one `LayoutInvalid` but preserves and dispatches a valid native-event batch. Advance each watermark only after its corresponding complete validation succeeds, isolate detached windows, and feed identical checked ticks to scroll and Task 14 caret blink. Wall time is sampled only by the frame owner before it creates `UIDeterministicTick`.

- [ ] Target wheel and pointer-drag scrolling against snapshot N using the same reverse order/clip rules, then apply `logicalDelta` and ordered gamepad `axisValue` to `UIScrollSystem` in native sequence order. Scroll runtime state may request the single allowed N+1 arrangement/render rebuild, but must not trigger a second re-hit-test or a second same-frame reflow; early exits retain the original dispatch records.

- [ ] Move UIButton hover/pressed/clicked runtime state into `UIInputRouter` identity tables while retaining source-compatible getters. `UIButton` keeps authored colors/callback and no runtime state is serialized.

- [ ] Implement deterministic `UIFocusSystem` for pointer, keyboard, gamepad, explicit and automatic navigation. Focus/capture tables use full identity and are revalidated at frame start and after callbacks.

- [ ] Extend the Task 10 world-bearing `GameOutputRenderer` and Game View paths
  with ordered frame input. Runtime and Game View consume one take-once native
  batch, freeze the matching surface start and shared coordinate mapping, call
  `ProcessFrame(world,input,textRenderer.LayoutService(),sink)` before script
  Update, retain the returned final snapshot, render cameras, then collect that
  exact N or N+1 through the same renderer/sink. Do not restore an object-vector
  overload or rebuild UI inside `GameOutputRenderer`.

- [ ] Run mutation, focus, Game View, scene transition, and full Debug gates.

  ```bash
  cmake --build --preset debug --target test_ui_input test_ui_focus test_ui \
    test_ui_scroll test_game_view test_scene_runtime molga_runtime -j
  ctest --test-dir build/debug -R '^(test_ui_input|test_ui_focus|test_ui|test_ui_scroll|test_game_view|test_scene_runtime|runtime_smoke)$' --output-on-failure
  ctest --preset debug
  ```

- [ ] Commit frozen event routing.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Platform/TextInputOwner.h \
    src/Platform/NativeKeyModifiers.h \
    src/UI/UIInputEvent.h \
    src/UI/UIInputRouter.* src/UI/UIFocusSystem.* src/UI/UIFixedTickClock.* \
    src/UI/UISystem.* \
    src/UI/UILayoutSnapshot.h src/UI/UILayoutSystem.cpp src/UI/UIScrollSystem.* \
    src/Rendering/TextRenderer.h \
    src/ECS/Components/UIButton.* src/Rendering/GameOutputRenderer.* \
    src/Editor/Windows/GameViewWindow.* src/runtime_main.cpp \
    tests/test_ui_input.cpp tests/test_ui_focus.cpp tests/test_game_view.cpp \
    tests/test_ui.cpp tests/test_ui_scroll.cpp tests/test_scene_runtime.cpp
  git commit -m "feat: freeze UI input and focus snapshots"
  ```

## Task 13: Centralize native SDL text input in a per-window arbiter

**Files:**

- Modify: `src/Platform/TextInputOwner.h`
- Create: `src/Platform/NativeInputEvent.h`
- Create: `src/Platform/NativeTextEvent.h`
- Create: `src/Platform/TextInputArbiter.h`
- Create: `src/Platform/TextInputArbiter.cpp`
- Create: `src/Platform/SdlTextInputPlatform.h`
- Create: `src/Platform/SdlTextInputPlatform.cpp`
- Create: `src/UI/UISurfaceCoordinateMapping.h`
- Create: `src/UI/UISurfaceCoordinateMapping.cpp`
- Create: `src/Editor/ThirdParty/imgui_impl_sdl3_molga.cpp`
- Create: `src/Editor/ThirdParty/IMGUI_SDL3_LICENSE.txt`
- Create: `src/Editor/ImGuiTextInputBridge.h`
- Create: `src/Editor/ImGuiTextInputBridge.cpp`
- Create: `tests/test_text_input_arbiter.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Editor/ImGuiLayer.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Systems/Input.h`
- Modify: `src/Systems/Input.cpp`
- Modify: `src/UI/UIInputEvent.h`
- Modify: `tests/test_platform_sdl.cpp`
- Modify: `tests/test_imgui_sdlgpu.cpp`
- Modify: `tests/test_gpu_sdl.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 12 `TextInputOwner`/`TextInputOwnerStamp`, `UIRuntimeTargetIdentity`, SDL window IDs/events, ordered `UIInputEvent`, and `TextDiagnosticSink`.
- Produces: `NativeInputEvent`, `NativeTextEvent`, `NativeInputBatch` with per-surface batch-start seeds, `ITextInputPlatform`, exactly one host-owned `TextInputArbiter`, `EngineHost::{TakeNativeInputBatch,TextInput}`, one shared `UISurfaceCoordinateMapping`, and the engine-owned ImGui IME bridge.

- [ ] Add a mockable platform boundary and failing call-audit tests for Start/Stop/Clear/SetArea ordering, one owner per window, independent detached windows, generation stamping, stale release, transition sequence boundary, failed Start, failed Clear/Stop, focus loss, owner deletion, deep-copy payload lifetime, UTF-8 editing ranges, and editor/runtime contention. Repeating the identical owner every frame must return the existing token with zero generation/boundary/platform calls; repeating an unchanged area must make zero platform calls. Add an explicit `KeyDown → TextEditing → TextCommit → KeyUp` input batch and require identical outer/inner sequence values and unchanged mapping/dispatch order.

  ```cpp
  namespace molga::platform {
  class ITextInputPlatform {
  public:
      virtual ~ITextInputPlatform() = default;
      virtual bool ClearComposition(molga::WindowId, std::string&) = 0;
      virtual bool StopTextInput(molga::WindowId, std::string&) = 0;
      virtual bool StartTextInput(molga::WindowId, std::string&) = 0;
      virtual bool SetTextInputArea(molga::WindowId, molga::PixelRectU32,
                                    std::int32_t cursorOffset,
                                    std::string&) = 0;
  };
  } // namespace molga::platform
  ```

- [ ] Define value-owned native events and owner tokens.

  ```cpp
  namespace molga::platform {
  enum class NativeTextEventKind : std::uint8_t { Editing, Commit };
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

  ```cpp
  // src/Platform/NativeInputEvent.h
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

  In this same slice, extend the already-owned `molga::ui::UIInputEvent` with
  `std::optional<molga::platform::TextInputOwnerTransition> textInputOwnerTransition`;
  the one native-to-UI mapper copies the complete
  attached value without recomputing a target or boundary. This is the sole
  transition channel consumed in Task 14's ordered loop.

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

  `EngineHost::Impl` owns one `SdlTextInputPlatform` followed by one `TextInputArbiter`; every runtime UI and ImGui bridge receives `host.TextInput()` and may not construct another arbiter. The diagnostic sink passed to `EngineInit` must outlive the host. Update every editor/runtime/test call site explicitly rather than retaining an implicit alternate overload.

- [ ] Run the red gate.

  ```bash
  cmake --build --preset debug --target test_text_input_arbiter -j
  ```

  Expected: compile fails on platform/arbiter contracts.

- [ ] Make `SetOwner` one atomic authority-and-transition operation: editor cannot replace an active runtime owner in the same window; an authority-verified runtime request may replace an editor owner; windows remain independent. Every mutator returns a complete `TextInputOwnerTransition` value. If `{windowId,kind,runtimeTarget}` equals current, return `NoChange` with the existing token, unchanged boundary, and `boundaryPublished=false` with no generation/platform call; unchanged area, rejected authority, and a failure before state mutation are equally nonpublishing. `TextInputArea::cursorOffset` is always rect.x-relative SDL window pixels in `[0,rect.width]`, never an absolute window/desktop X coordinate or UTF-8 index. For a real change compute `floor=max(lastStampedSequence,nativeFocusLossSequenceOrZero)` and choose `candidate = floor > lastTransitionBoundary ? floor : CheckedAdd(lastTransitionBoundary,1)`. A zero/overflowed boundary fails before mutation and leaves owner/area/platform unchanged. After reserving a valid boundary, acquire the process-global non-wrapping owner generation. If that allocator is exhausted, Clear/Stop and invalidate the old owner if present, publish the reserved boundary, leave no owner, and return `GenerationExhausted` with `{retired=old,current=null,boundaryPublished=true,cancelEngineComposition=old-is-runtime}`; never Start/SetArea or retain a stale owner. A successfully acquired but unused generation is burned. A real runtime retirement sets `cancelEngineComposition=true`, but the arbiter has no UI callback/listener/history: `UISystem` applies the returned value immediately and idempotently to the exact retired runtime identity. `StampForCurrentOwner` copies `{kind,runtimeTarget,generation}` at ingest; `Accepts` compares that immutable stamp and retains no retired-generation history.

- [ ] In `EngineHost::PollEvents`, report `ReferenceInvalid` and return before
  `Input::BeginFrame` or `SDL_PollEvent` when the previous batch has not been
  taken; never clear/overwrite pending values or consume SDL. Otherwise capture
  exactly one unique `NativeSurfaceBatchStart` per surface before event
  processing, including native focus and an explicitly valid/invalid physical
  pointer query, then begin the event vector. `TakeNativeInputBatch` atomically
  moves both vectors once and returns empty on a second take; a missing/duplicate
  surface seed rejects that complete UI surface frame.

  Assign every copied event from one process-global checked CAS sequence
  allocator. Reject `0`/`UINT64_MAX` before increment, leave exhaustion at max,
  and never publish zero/reuse. Deep-copy text and immutable modifiers. For text,
  set `inner.sequence == outer.sequence`, stamp the complete owner, and retain
  vector position. On native focus loss, call
  `OnWindowFocusLost(windowId,sequence)` once before observation and attach its
  transition to that same event. Preserve exact order
  `build complete copied metadata -> observer(raw,metadata) -> Input::Process* -> append metadata once`.
  Map one-for-one, including transition/modifiers/validity; Task 14 walks it once,
  no text drain exists, and replay/non-increasing sequence is rejected. Text
  reaches ImGui only for an exact `EditorImGui` stamp.

- [ ] Freeze one `UISurfaceCoordinateMapping` from the exact presentation used
  to build/render snapshot N. Standalone uses its window/framebuffer origin and
  scale; embedded/detached Game View converts the window-local image origin to
  physical window pixels. Multiply copied SDL logical points by that event
  window's pixels-per-point exactly once, subtract output origin, divide by
  window-pixels-per-output-pixel, then call
  `UIPhysicalTransform::ToLogicalPoint`. Outside the half-open presentation,
  invalid/nonfinite, or overflow returns no point and sets
  `UIInputEvent::logicalPointValid=false`; never clamp or invent `(0,0)`. Map the
  valid batch-start physical pointer through the identical value into
  `UIFrameInput` before processing events. Signed scroll remains semantic and is
  quantized once without pointer scaling. The same mapper's outward rect/cursor
  methods are Task 14 Game View IME authority; no second origin/scale formula is
  allowed.

- [ ] Make `SdlTextInputPlatform.cpp` the only compiled source that calls `SDL_StartTextInput`, `SDL_StopTextInput`, `SDL_ClearComposition`, or `SDL_SetTextInputArea`. Copy the pinned ImGui SDL3 backend source into `imgui_impl_sdl3_molga.cpp`, retain its license, remove its direct SDL IME lifecycle callback, and compile this engine-owned backend in place of `external/imgui/backends/imgui_impl_sdl3.cpp`.

- [ ] Install `ImGuiTextInputBridge` into `ImGui::GetPlatformIO().Platform_SetImeDataFn` immediately after `ImGui_ImplSDL3_InitForSDLGPU`. Preserve the ImGui ABI `void(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*)`, resolve the passed viewport's SDL window ID, and submit `{visible, area, cursorOffset}` to the arbiter. An editor request cannot stop or reposition an active runtime owner in the same or another window.

- [ ] Prove sole ownership at source and binary level.

  ```bash
  rg -n 'SDL_(StartTextInput|StopTextInput|ClearComposition|SetTextInputArea)' src external/imgui/backends/imgui_impl_sdl3.cpp
  ```

  Expected: production `src/` calls appear only in `src/Platform/SdlTextInputPlatform.cpp`; the external backend may contain upstream code but is no longer in the target source list.

- [ ] Run arbiter, platform, and ImGui backend tests.

  ```bash
  cmake --build --preset debug --target test_text_input_arbiter test_platform_sdl \
    test_imgui_sdlgpu test_gpu_sdl test_rendering_sdlgpu \
    molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_text_input_arbiter|test_platform_sdl|test_imgui_sdlgpu|test_gpu_sdl|test_rendering_sdlgpu)$' --output-on-failure
  ```

- [ ] Commit native text ownership.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Platform/TextInputOwner.h \
    src/Platform/NativeInputEvent.h \
    src/Platform/NativeTextEvent.h \
    src/Platform/TextInputArbiter.* src/Platform/SdlTextInputPlatform.* \
    src/UI/UISurfaceCoordinateMapping.* \
    src/Editor/ThirdParty/imgui_impl_sdl3_molga.cpp \
    src/Editor/ThirdParty/IMGUI_SDL3_LICENSE.txt \
    src/Editor/ImGuiTextInputBridge.* src/Core/Bootstrap.* \
    src/main.cpp src/runtime_main.cpp \
    src/Editor/ImGuiLayer.cpp src/Editor/Windows/GameViewWindow.* \
    src/Systems/Input.* src/UI/UIInputEvent.h \
    tests/test_text_input_arbiter.cpp tests/test_platform_sdl.cpp \
    tests/test_imgui_sdlgpu.cpp tests/test_gpu_sdl.cpp \
    tests/test_rendering_sdlgpu.cpp
  git commit -m "feat: arbitrate SDL text input per window"
  ```

## Task 14: Implement grapheme-safe UITextInput, Game View IME mapping, and semantic trees

**Files:**

- Create: `src/UI/UITextInputSystem.h`
- Create: `src/UI/UITextInputSystem.cpp`
- Create: `src/UI/UIAccessibilityTree.h`
- Create: `src/UI/UIAccessibilityTree.cpp`
- Create: `src/Editor/GameViewTextInputMapping.h`
- Create: `src/Editor/GameViewTextInputMapping.cpp`
- Create: `tests/test_ui_text_input.cpp`
- Create: `tests/test_ui_accessibility.cpp`
- Create: `tests/test_ui_game_view_ime.cpp`
- Modify: `tests/test_game_view.cpp`
- Modify: `tests/test_text_input_arbiter.cpp`
- Modify: `tests/test_ui_render_clip.cpp`
- Modify: `src/ECS/Components/UITextInput.h`
- Modify: `src/ECS/Components/UITextInput.cpp`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UIFocusSystem.cpp`
- Modify: `src/UI/UIInputRouter.cpp`
- Modify: `src/UI/UILayoutSnapshot.h`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UIRenderCollector.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Editor/Editor.h`
- Modify: `src/Editor/Editor.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: per-stage `PlannedUIEvent`, immutable ingest-owner stamps and attached owner transitions, `NativeKeyModifiers`, `UnicodeTextBuffer`, `TextLayoutService`, `TextHitTesting`, deterministic UI ticks, frozen UI snapshots, and owner validation from the one host-owned `TextInputArbiter`.
- Produces: `UITextEditState`, surface-only blink/drag state, one-event `UITextInputSystem::{HandleEvent,ApplyOwnerTransition,AdvanceTick}`, explicit runtime-service injection, public value/submission callback APIs, immutable UITextInput text/selection/composition/caret render items, `GameViewTextInputMapping`, and internal-only `UIAccessibilityTree`. No text-input subsystem accepts a frame/vector/drain API.

- [ ] Add failing editing tests for focus/value initialization, insertion,
  Backspace/Delete, visual Left/Right versus logical deletion,
  Upstream/Downstream affinity, Home/End, shift selection, clipboard-free
  replace, read-only, grapheme max length, combining/ZWJ/variation sequences,
  mixed BiDi, composition update/commit/cancel, placeholder visibility,
  blink-only invalidation, and callback-time target replacement. Include one
  same-batch mixed-BiDi `TextCommit -> Right -> Home` row proving each navigation
  observes the text/layout produced by the preceding event, while every target
  and hit geometry still comes from N and only one N+1 snapshot is published.
  The exact Enter policy is: single-line Enter submits; multiline plain Enter
  inserts one newline; multiline Ctrl+Enter or Cmd+Enter submits; Shift/Alt alone
  do not submit; explicit `Submit` works for either mode. Key-up never repeats a
  callback. Add pointer caret/captured-drag tests using the frozen text-input
  target and render assertions for selection, composed glyphs, underline,
  focused caret, shared clip, exact order, plus an unfocused input with
  `caretVisible=true` that emits no caret.

- [ ] Register new `test_ui_text_input` through `molga_add_text_test` and
  convert `test_game_view` to the same common session before either target
  constructs the final injected layout/hit-testing services. Preserve all
  existing Game View/SDL sources and properties; no alternate main or ICU
  lifetime is allowed.

- [ ] Add failing SDL conversion tests proving editing start/length in UTF-8 character units maps through `UnicodeTextBuffer` to original bytes/scalars, out-of-range input clamps with a diagnostic, and a caret never lands inside UTF-16 surrogate units or an extended grapheme.

- [ ] Run the text-editing red gate.

  ```bash
  cmake --build --preset debug --target test_ui_text_input test_ui_accessibility \
    test_ui_game_view_ime test_ui_render_clip -j
  ```

  Expected: compile fails because the runtime editing, semantic-tree, and Game View mapping types do not exist.

- [ ] Implement runtime-only state keyed by full identity.

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

  Install the final facade in the same slice and delete the earlier overload:

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

  `UISystem::ProcessFrame` retains the sole ordered-vector loop. During
  preplanning, an event-attached transition whose retired owner is
  `RuntimeUITextInput` is copied into `ownerTransitionTargetFromSnapshotN` only
  when that complete identity exists in N; `TargetFor(OwnerTransition)` is the
  sole stage accessor. At each current position it first applies/merges the exact
  attached transition only for an eligible plan, then invokes `HandleEvent` once
  and merges into that event's accumulator; a foreign transition is targetless
  and cannot mutate the surface. An arbiter transition initiated inside a
  handler is applied immediately before that call returns. The separate validated
  tick loop passes individual identical ticks to scroll and input; neither
  subsystem accepts vectors. For `TextEditing`/`TextCommit`, require an exact
  runtime stamp, reconstruct the stamped token, re-resolve its full identity, and
  call `arbiter.Accepts`; the stamp—not projected focus/`CurrentOwner`—is
  authoritative. After any value/composition mutation and before a later visual
  navigation event in the same batch, build or reuse one immutable current
  layout from `UITextInputLabelSnapshot::effectiveInputRequestTemplate` through
  the injected service. Navigation reads that updated layout, but targets and
  pointer geometry remain N and publication remains the sole N+1. One audit log
  preserves pointer-focus/key/edit/commit/key-up order across all subsystems.

  Before every possible N or N+1 build, call `SynchronizeAuthoredInputs` with the current `UIRuntimeGenerationSnapshot`, then pass `textInputSystem_` as the exact `UITextInputVisualStateProvider&` and the injected `TextLayoutService&` to `UILayoutSystem::Build`; the final path retains no `EmptyUITextInputVisualStateProvider` or hidden service. For visible input text, copy `UITextInputLabelSnapshot::effectiveInputRequestTemplate`, replace only `utf8`, and call that service once. The input's top-level family and paragraph style are authoritative; the rendered label contributes only viewport constraints, color, identity, and diagnostic provenance. Retain the exact visible effective request in the geometry key so caret/selection/focus/blink-only changes reuse layout and visible/style changes cannot reuse stale geometry. `SynchronizeAuthoredInputs` initializes runtime values from `initialText` only for newly active complete identities and uses the non-wrapping semantic generation to make the unchanged path traversal/allocation-free. Edit/callback tables key by complete identity; surface focus/blink/drag state keys by `{windowId,worldGeneration}`. Every visible edit or surface-state mutation advances both its nonzero checked revision and the shared semantic-dirty clock; exhaustion leaves the old state and fails closed. Public setters never serialize runtime value or callbacks, and invocation copies callback state then re-resolves the target before at-most-once delivery.

- [ ] Treat SDL editing start/length as composition-string-relative UTF-8
  character units. Convert through the composition `UnicodeTextBuffer` into a
  clamped half-open grapheme range, then build visible committed-plus-composition
  text and exact boundary maps. Without composition, map committed selection and
  caret; with composition, place the caret at mapped
  `compositionSelection.end`, underline the complete composition, and do not
  separately highlight the replaced committed selection. Pointer down/drag uses
  `planned.TargetFor(TextInput)`, frozen viewport/layout, and `TextHitTesting`;
  capture retains the anchor, clamps outside motion to first/last visual stop,
  and never transfers across replacement/window loss. Use one immutable visible
  layout for all selection/caret/underline geometry. Publish clipped layers in
  exact order `selection solids -> complete text glyph/tofu span -> all
  composition underline solids -> caret solid`; reserve every full span before
  the next layer. Emit caret only when
  `focused && caretVisible && clippedCaretNonempty`; an unfocused input never
  draws one. The rendered label remains exclusively input-owned. Placeholder is
  visible exactly when committed/composition are empty, input is unfocused, and
  the frozen placeholder is enabled/visible. One edit may use the Task 12 N+1
  publication but never mutates/re-hit-tests N. Blink changes caret presence
  only, reuses layout, and republishes identical blink-independent focused IME
  geometry. Test empty/nested clips, combining/emoji, BiDi, multi-run
  composition, unfocused caret, and command-span reservation.

- [ ] Apply `TextInputOwnerTransition` values monotonically per window. First validate `NoChange`, `Rejected`, and a nonpublishing `PlatformFailed`/`GenerationExhausted` (`boundaryPublished=false`) as ordinary no-ops that do not consult or replace the latest published value. For `boundaryPublished=true`, a lower boundary or field-identical replay is an idempotent no-op; a nonidentical equal boundary is invalid; only a strictly greater boundary replaces the latest value. Test `Applied → NoChange/Rejected/nonpublishing failure` at the same boundary with zero diagnostics. When `cancelEngineComposition` is true, re-resolve only the retired `RuntimeUITextInput` identity, clear only its composition fields, preserve committed value/caret/selection, and advance semantic dirty only if composition was visible. Native window focus loss consumes only the transition attached to that ordered event; panel pointer departure clears pointer state and performs no owner release.

- [ ] Make runtime setters and `valueChanged`/`submitted` callbacks invocation-safe: copy callback state, re-resolve identity before dispatch, and never serialize runtime value. Edit-mode changes to `initialText` still use editor property/snapshot commands.

- [ ] Add a single `GameViewTextInputMapping::Map` that consumes the immutable
  `UITextInputImeGeometrySnapshot` and exact frozen
  `UISurfaceCoordinateMapping` already used for pointer input. Require any
  explicitly supplied presentation/origin/scale/bounds fields to agree with that
  value or fail. Delegate Canvas rect and cursor conversion to
  `CanvasRectToWindowPixels`/`CanvasCursorXToWindowPixel`; then compute
  `cursorOffset = clamp(cursorWindowPixelX - rect.x, 0, rect.width)` in SDL
  window pixels. It is neither a UTF-8 index nor desktop/absolute X and reads no
  live edit/component state. Test scale 1/2, integer-fit letterbox, crop,
  detached ImGui viewport origins, exact half-open edges, blink-off geometry,
  authority mismatch, and invalid/nonfinite presentation. Game View and arbiter
  duplicate none of this math.

- [ ] Preserve the real `World*` Game View binding introduced in Task 10 and
  use it for IME target validation. Runtime owns IME only while the native
  window and Game View keyboard focus are valid and its focused `UITextInput`
  identity resolves. Both editor Game View and standalone runtime pass the same
  `EngineHost::TextInput()`, exact `textRenderer.LayoutService()` reference, and
  diagnostic sink through final `UISystem::ProcessFrame`; no editor, Game View,
  runtime, or test-local production arbiter/service clone is allowed. Immediately
  apply every returned `SetOwner`/`SetArea`/`Release` transition through
  `UISystem::ApplyTextInputOwnerTransition`; identical owner/area submissions
  remain zero-call no-ops. Pointer departure clears pointer state only; native
  focus loss relies on its single event-attached transition to release pressed/
  capture/owner/composition.

- [ ] Build `UIAccessibilityTree` from final snapshot plus authored `UIAccessibility` metadata with stable parent/child order, role/name/description/hidden fields, rect, and focusable state. Expose it for tests/debugging only; do not call macOS Accessibility APIs or advertise a native bridge.

- [ ] Run text editing, accessibility, Game View, platform, and serialization tests.

  ```bash
  cmake --build --preset debug --target molga_engine molga_runtime \
    test_ui_text_input test_ui_accessibility test_ui_game_view_ime \
    test_game_view test_text_input_arbiter test_ui_render_clip \
    test_scene_serializer -j
  ctest --test-dir build/debug -R '^(test_ui_text_input|test_ui_accessibility|test_ui_game_view_ime|test_game_view|test_text_input_arbiter|test_ui_render_clip|test_scene_serializer)$' --output-on-failure
  ```

- [ ] Commit text editing and internal semantics.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/UI/UITextInputSystem.* \
    src/UI/UIAccessibilityTree.* src/Editor/GameViewTextInputMapping.* \
    src/ECS/Components/UITextInput.* src/UI/UISystem.* \
    src/UI/UIFocusSystem.cpp src/UI/UIInputRouter.cpp \
    src/UI/UILayoutSnapshot.h src/UI/UILayoutSystem.cpp \
    src/UI/UIRenderCollector.cpp \
    src/Editor/Windows/GameViewWindow.* src/Editor/Editor.* \
    src/runtime_main.cpp tests/test_ui_text_input.cpp \
    tests/test_ui_accessibility.cpp tests/test_ui_game_view_ime.cpp \
    tests/test_ui_render_clip.cpp tests/test_game_view.cpp \
    tests/test_text_input_arbiter.cpp
  git commit -m "feat: add grapheme-safe UI text editing"
  ```

## Task 15: Integrate UI/text authoring, migrations, references, and diagnostics in the editor

**Files:**

- Create: `src/Editor/Commands/MigrateUITextSchemaCommand.h`
- Create: `src/Editor/Commands/MigrateUITextSchemaCommand.cpp`
- Create: `tests/support/ProjectFileCommandTestAccess.h`
- Create: `tests/support/ProjectFileCommandTestAccess.cpp`
- Create: `tests/support/UITextMigrationTestAccess.h`
- Create: `tests/support/UITextMigrationTestAccess.cpp`
- Create: `tests/test_ui_editor.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Editor/Properties/EditorPropertyDescriptor.h`
- Modify: `src/Editor/Properties/EditorPropertyDescriptor.cpp`
- Modify: `src/Editor/Windows/InspectorWindow.h`
- Modify: `src/Editor/Windows/InspectorWindow.cpp`
- Modify: `src/Editor/Windows/ProjectBrowserWindow.h`
- Modify: `src/Editor/Windows/ProjectBrowserWindow.cpp`
- Modify: `src/Editor/Commands/ObjectCommands.h`
- Modify: `src/Editor/Commands/ObjectCommands.cpp`
- Modify: `src/Editor/Commands/ProjectFileCommands.h`
- Modify: `src/Editor/Commands/ProjectFileCommands.cpp`
- Modify: `src/Editor/Commands/EditorCommand.h`
- Modify: `src/Editor/Commands/CommandHistory.h`
- Modify: `src/Editor/UIRegistry.cpp`
- Modify: `src/Editor/AssetReferenceScan.cpp`
- Modify: `src/Editor/Editor.h`
- Modify: `src/Editor/Editor.cpp`
- Modify: `tests/test_editor_property_descriptor.cpp`
- Modify: `tests/test_command_history.cpp`
- Modify: `tests/test_editor_undo_dirty.cpp`
- Modify: `tests/test_project_file_commands.cpp`
- Modify: `tests/test_asset_reference_scan.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: all versioned UI/text components, `SceneObjectRef`, font/family importer metadata, and existing snapshot-command Undo/Redo paths.
- Produces: `EditorPropertyType::SceneObjectRef`, read-only/driven descriptors, scene-object picker writes, font-family asset creation, and `MigrateUITextSchemaCommand`.

- [ ] Add failing descriptor/Inspector tests for every new component field, FontFamily asset filtering, `SceneObjectRef` picker, multi-edit intersection, invalid ref display, driven-field read-only metadata, source-driver label, runtime-only omission, exact `AssetRecord::importDiagnostics` code/severity/message/remediation display, and undo/redo.

- [ ] Run the editor integration red gate.

  ```bash
  cmake --build --preset debug --target test_ui_editor test_editor_property_descriptor -j
  ```

  Expected: compile or assertions fail because the picker/read-only metadata and migration command do not exist.

- [ ] Extend editor property contracts without overloading integer/asset types.

  ```cpp
  enum class EditorPropertyType {
      Bool, Integer, Float, String, Enum, LayerMask, AssetGuid, SceneObjectRef
  };
  using EditorPropertyValue = std::variant<
      bool, std::int64_t, double, std::string, ::SceneObjectRef>;
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
  ```

- [ ] Map `fontFamilyGuid` only to `FontFamilyImporter`, font-license refs to their declared asset type, and UI navigation/content/viewport/label refs to `SceneObjectRef`. Implement a scene-object picker that writes stable IDs through the existing property snapshot command path; never store a pointer.

- [ ] Surface computed rect/intrinsic/baseline/clip as separate read-only descriptors with `drivenBy`; they are not included in component serialization snapshots and never become prefab overrides or Undo writes.

- [ ] Before adding any fallible file-creation command, extend `ICommand::Succeeded()` and boolean `CommandHistory::{Execute,Undo,Redo}` so initial execute, undo, and redo failure leaves both stacks, clean marker, and dirty state unchanged. Make `ProjectFileCreateCommand` transactionally publish the source and authoritative meta pair, override `Succeeded()`, and test failed source write, failed meta write, and failed redo before using it from Project Browser.

- [ ] Add all eight component types to `UIRegistry`, Inspector Add Component, and object-creation commands. Add Project Browser creation of `.fontfamily` with exact `schemaVersion:1`, empty ordered face/fallback arrays, undoable source-plus-meta creation, and immediate import diagnostics. Show font face/coverage/SHA/license state and family cycle/missing-GUID blockers in the asset Inspector.

- [ ] Implement `MigrateUITextSchemaCommand` as an explicit scene snapshot transaction. It converts legacy Canvas and implicit `fontGuid` payloads to current schemas and can create a deterministic one-face `.fontfamily` **plus authoritative `.fontfamily.meta`** as one inseparable pair; the source omits `guid`, the sidecar alone owns it, and partial/different pre-existing pairs block before mutation. Stage all bytes first, publish with a reverse rollback journal, and make Undo/Redo preflight the exact current scene/source/meta/stash bytes so an external edit or destination collision is never overwritten or deleted. Reuse the already-established conditional history contract so a failed operation remains on its original stack with clean-marker/dirty state unchanged. Opening, previewing, building, or saving without invoking this command must not silently rewrite legacy representation; package validation still applies full license/coverage rules to the implicit family view.

- [ ] Extend asset-reference scans through family faces/fallbacks, text-input label refs, scroll refs, and selectable navigation refs. Extend build preflight display to consume `AssetRecord::importDiagnostics` directly and list typed blocker code, object/asset context, and remediation.

- [ ] Run editor property/command/reference and editor smoke gates.

  ```bash
  cmake --build --preset debug --target test_ui_editor \
    test_editor_property_descriptor test_command_history \
    test_asset_reference_scan molga_engine -j
  ctest --test-dir build/debug -R '^(test_ui_editor|test_editor_property_descriptor|test_command_history|test_asset_reference_scan|editor_smoke)$' --output-on-failure
  ```

- [ ] Commit the editor integration.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt \
    src/Core/AssetDatabase.* \
    src/Editor/Commands/MigrateUITextSchemaCommand.* \
    src/Editor/Commands/ProjectFileCommands.* \
    src/Editor/Commands/ObjectCommands.* \
    src/Editor/Properties/EditorPropertyDescriptor.* \
    src/Editor/Windows/InspectorWindow.* \
    src/Editor/Windows/ProjectBrowserWindow.* \
    src/Editor/Commands/EditorCommand.h \
    src/Editor/Commands/CommandHistory.h \
    src/Editor/UIRegistry.cpp \
    src/Editor/AssetReferenceScan.cpp src/Editor/Editor.* \
    tests/support/ProjectFileCommandTestAccess.* \
    tests/support/UITextMigrationTestAccess.* tests/test_ui_editor.cpp \
    tests/test_editor_property_descriptor.cpp tests/test_command_history.cpp \
    tests/test_editor_undo_dirty.cpp tests/test_project_file_commands.cpp \
    tests/test_asset_reference_scan.cpp
  git commit -m "feat: integrate UI text authoring in editor"
  ```

## Task 16: Produce one canonical macOS app resource root

**Files:**

- Create: `resources/Info.plist.in`
- Create: `src/Text/TextRuntimeManifest.h`
- Create: `src/Text/TextRuntimeManifest.cpp`
- Create: `tests/support/PathServiceTestAccess.h`
- Create: `tests/support/PathServiceTestAccess.cpp`
- Create: `tests/smoke/check_runtime_target_resources.cmake`
- Create: `tests/smoke/test_run_end_to_end_preflight.cmake`
- Create: `tests/test_bundle_layout.cpp`
- Modify: `cmake/TextDependencies.cmake`
- Modify: `src/Core/PathService.h`
- Modify: `src/Core/PathService.cpp`
- Modify: `src/Core/PackageLayout.h`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `src/Core/PackageFinalizer.h`
- Modify: `src/Core/PackageFinalizer.cpp`
- Modify: `src/Core/GameConfig.h`
- Modify: `src/Core/GameConfig.cpp`
- Modify: `src/Core/BuildProfile.h`
- Modify: `src/Core/BuildProfile.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Editor/BuildManager.cpp`
- Modify: `src/Scripting/ScriptPackageLoader.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_path_service.cpp`
- Modify: `tests/test_build_profile.cpp`
- Modify: `tests/test_build_manager.cpp`
- Modify: `tests/test_build_smoke.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_package_finalizer.cpp`
- Modify: `tests/test_runtime_script_package_loader.cpp`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: verified text runtime resources/manifests, existing GameBuilder/PackageFinalizer, shader/asset/scene catalogs, and the runtime executable.
- Produces: `PathService::RuntimeResourceRoot`, explicit development root, `PackageRoots`, canonical `.app`, `TextRuntimeManifest`, `molga_runtime` bundle, and `molga_runtime_dev`.

- [ ] Add failing path tests for `.../Game.app/Contents/MacOS/<exe> → .../Contents/Resources`, symlink/canonical paths, malformed pseudo-bundles, and an explicitly supplied flat developer root. A release runtime outside a valid app bundle with no explicit development root must fail rather than infer executable directory.

- [ ] Replace the single-root package test assumptions with separate executable/resource roots and assert the exact bundle tree:

  ```text
  Game.app/Contents/Info.plist
  Game.app/Contents/MacOS/<game-executable>
  Game.app/Contents/Resources/game.json
  Game.app/Contents/Resources/asset_catalog.json
  Game.app/Contents/Resources/Assets/
  Game.app/Contents/Resources/Scenes/
  Game.app/Contents/Resources/ShaderBundle/
  Game.app/Contents/Resources/Resources/missing_texture.png
  Game.app/Contents/Resources/Engine/Text/icudt78l.dat
  Game.app/Contents/Resources/Engine/Text/text_dependency_contract.json
  Game.app/Contents/Resources/Manifests/text_runtime.json
  Game.app/Contents/Resources/Licenses/ThirdPartyNotices.md
  Game.app/Contents/Resources/Licenses/HarfBuzz.txt
  Game.app/Contents/Resources/Licenses/ICU.txt
  Game.app/Contents/Resources/Licenses/StbTrueType.txt
  ```

- [ ] Run the bundle-layout red gate.

  ```bash
  cmake --build --preset debug --target test_bundle_layout test_path_service test_game_builder -j
  ```

  Expected: compile or assertions fail because the separate bundle roots and canonical app staging do not exist.

- [ ] Implement the resource-root API.

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
      // existing asset helpers remain
  private:
      PathService() = default;
  };
  ```

  `InitFromExecutable` discovers and stores the actual executable but does not guess a resource root. Preserve the singleton's private constructor and update every `main.cpp`, `runtime_main.cpp`, and test call site when its return type changes. App detection requires canonical `Contents/MacOS`; `InitializePackagedRuntimeRoot` derives and atomically publishes its sibling `Contents/Resources` as both runtime and asset root. The development setter is the only flat fallback and must be invoked from an explicit editor/development CLI or build-mode root, not automatically. The editor publishes its executable-directory development resource root before any shader/text resource lookup; the bundled runtime never accepts that fallback. All failure paths return before `EngineInit` without partially changing the previously published root.

- [ ] Define `TextRuntimeManifest` schema `1` with `dependencyLockSha256` explicitly defined as the SHA of portable `Engine/Text/text_dependency_contract.json`; sorted dependency records containing exactly one logical `harfbuzz` archive whose SHA is `harfbuzz.compositeSha256` and exactly two logical ICU archives (`icui18n`, `icuuc`). It rejects any `harfbuzz-icu` or raw-core/raw-adapter/adapter-object manifest record. Also record ICU data size/SHA; the rasterizer's logical compiled-header digest with no package path; license hashes; ordered font/license entries; canonical `assetCatalogPath/Sha256`; and coverage-manifest path/SHA. It never references or stages `text_dependency_build_lock.json`, never hashes itself or `game.json`; instead `game.json.text.manifestSha256` hashes `Manifests/text_runtime.json`, avoiding a self/config hash cycle. Finalization order is catalog, coverage, runtime manifest, then `game.json`.

- [ ] Change `PackageLayout::Validate` to accept separate paths and validate canonical containment.

  ```cpp
  struct PackageRoots {
      std::filesystem::path executablePath;
      std::filesystem::path resourceRoot;
  };
  class PackageLayout {
  public:
      static bool Validate(const PackageRoots&, const std::string& executableName,
                           std::string& errorOut);
      static std::string ExecutableNameFor(const std::string& gameName);
  };
  ```

  Introduce the new overload without stranding existing callers: keep a temporary flat-root adapter while Task 16.1 establishes the type, migrate `GameBuilder` and all build/profile/smoke tests in Task 16.2, and remove the adapter only after a repository-wide call-site search is clean. Use a root-only bundle/plist validator while staging; call complete-tree `PackageLayout::Validate` only after resources, scripts, config, catalog, manifests, and executable all exist.

- [ ] Make GameBuilder—not only CMake's development bundle—own per-game plist generation through `bool WriteInfoPlist(const BuildSettings&, const PackageRoots&, std::string&)`. Persist the authoritative bundle identifier, bundle version, product version, and minimum macOS version through `BuildProfile` serialization and `BuildManager`; schema migration supplies deterministic defaults rather than empty transient settings. Render `resources/Info.plist.in` to a sibling temporary file, validate it with the platform plist parser before publish, and atomically rename it. `CFBundleExecutable` must byte-match the sanitized basename placed in `Contents/MacOS`; validate/escape display name, reverse-DNS bundle ID, version, and minimum-OS values so quotes/XML/metacharacters or path components cannot alter the plist or escape the bundle.

- [ ] Pin `project(molga_engine VERSION 0.1.0)` so the engine-shell plist never expands an empty `PROJECT_VERSION`. Make the existing macOS `11.0` deployment default effective even when the cache entry exists but is empty, propagate it into vendored static builds, and add a configure assertion/test; this engine-shell minimum remains distinct from a per-game BuildProfile minimum. Make the release `molga_runtime` target a `MACOSX_BUNDLE` (or equivalent install/bundle target) with the plist template. Add a separate non-bundle `molga_runtime_dev` target that requires `--development-resource-root <absolute-path>`. Factor their complete common sources, link libraries, warnings, compile options/definitions, dependencies, and exports through one helper so the two targets cannot drift. Extend Task 2's staging helper with an optional explicit destination while preserving its one-argument development form: `molga_runtime_dev` stages below `$<TARGET_FILE_DIR>/Engine/Text`, while the bundle stages below `$<TARGET_BUNDLE_CONTENT_DIR:molga_runtime>/Resources/Engine/Text`; assert no copy appears under `Contents/MacOS/Engine/Text`. Both read everything through `RuntimeResourceRoot`.

- [ ] Rework `GameBuilder` staging to create `<name>.app/Contents/{MacOS,Resources}`, call the same `WriteInfoPlist` helper, copy the exact CMake target payload (`$<TARGET_FILE:molga_runtime>`) under the sanitized game name, and place every approved asset/scene/shader/config/catalog/placeholder under Resources. Expose that target path to the editor build flow, make `molga_engine` depend on it, and reject a stale adjacent flat `molga_runtime`. Route `CopyUserScripts`, `GenerateGameConfig`, `EmitAssetCatalog`, and every other resource helper to `PackageRoots::resourceRoot`; `ScriptPackageLoader` resolves an enabled relative library through `RuntimeResourceRoot`. Add a copied-app enabled-script test. Signing/notarization is not part of this task.

- [ ] Package the verified ICU data, portable dependency contract,
  HarfBuzz/ICU/rasterizer licenses, and initial text runtime manifest.
  `GameBuilder` depends on `molga_text_dependencies_ready` and reads only the
  portable contract; Task 1's transactional barrier already cross-checks it
  against the machine-local build lock, whose path is never exposed to this
  product target. Recursively reject an absolute/checkout/build path in the
  portable JSON. `PackageFinalizer` hashes after the final tree is assembled
  and rejects symlink escapes, missing entries, a root mismatch, or any
  packaged build-provenance lock.

- [ ] Update every runtime read of `game.json`, scene catalog/scenes, asset root/catalog, shaders, scripts, placeholder, and text resources to use the initialized `RuntimeResourceRoot`. Update end-to-end smoke to copy the whole `.app` to a fresh `mktemp -d` path outside the checkout and launch `Contents/MacOS/<game-executable>`. Keep development-runtime smoke separate.

- [ ] Run path, bundle, builder, finalizer, and copied-app smoke gates.

  ```bash
  cmake --build --preset debug --target test_path_service test_bundle_layout \
    test_game_builder test_package_finalizer molga_runtime molga_runtime_dev -j
  ctest --test-dir build/debug -R '^(test_path_service|test_bundle_layout|test_game_builder|test_package_finalizer|smoke_end_to_end|runtime_smoke)$' --output-on-failure
  ```

- [ ] Commit the canonical bundle migration.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt cmake/TextDependencies.cmake \
    resources/Info.plist.in \
    src/Text/TextRuntimeManifest.* src/Core/PathService.* \
    src/Core/PackageLayout.* src/Core/PackageFinalizer.* \
    src/Core/GameConfig.* src/Core/BuildProfile.* \
    src/Editor/GameBuilder.* src/Editor/BuildManager.cpp \
    src/Scripting/ScriptPackageLoader.cpp src/main.cpp src/runtime_main.cpp \
    tests/test_bundle_layout.cpp tests/test_path_service.cpp \
    tests/support/PathServiceTestAccess.* \
    tests/test_build_profile.cpp tests/test_build_manager.cpp \
    tests/test_build_smoke.cpp \
    tests/test_game_builder.cpp tests/test_package_finalizer.cpp \
    tests/test_runtime_script_package_loader.cpp \
    tests/smoke/check_runtime_target_resources.cmake \
    tests/smoke/test_run_end_to_end_preflight.cmake \
    tests/smoke/run_end_to_end.cmake
  git commit -m "feat: package runtime as canonical macOS app"
  ```

## Task 17: Enforce text coverage, font licensing, and fail-closed package startup

**Files:**

- Create: `src/Assets/LocalizationTableAsset.h`
- Create: `src/Assets/LocalizationTableAsset.cpp`
- Create: `src/Text/AuthoredTextStyle.h`
- Create: `src/Text/AuthoredTextStyle.cpp`
- Create: `src/Core/Importers/LocalizationTableImporter.h`
- Create: `src/Core/Importers/LocalizationTableImporter.cpp`
- Create: `src/Text/TextCoverageManifest.h`
- Create: `src/Text/TextCoverageManifest.cpp`
- Create: `src/Text/TextPackageValidator.h`
- Create: `src/Text/TextPackageValidator.cpp`
- Create: `src/Text/TextNoticeGenerator.h`
- Create: `src/Text/TextNoticeGenerator.cpp`
- Create: `src/Core/RuntimeStartup.h`
- Create: `src/Core/RuntimeStartup.cpp`
- Create: `tests/test_text_package.cpp`
- Create: `tests/test_runtime_startup.cpp`
- Create: `tests/smoke/text_network_deny.sb`
- Create: `tests/smoke/test_text_network_deny.cmake`
- Create: `tests/smoke/text_sandbox_capability_probe.mm`
- Modify: `src/Assets/FontArtifactStore.h`
- Modify: `src/Assets/FontArtifactStore.cpp`
- Modify: `src/Core/BuildProfile.h`
- Modify: `src/Core/BuildProfile.cpp`
- Modify: `src/Core/Importers/ImporterRegistry.cpp`
- Modify: `src/Core/AssetDatabase.h`
- Modify: `src/Core/AssetDatabase.cpp`
- Modify: `src/Core/AssetDependencyValidator.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Core/PackageLayout.cpp`
- Modify: `src/Text/FontRepository.h`
- Modify: `src/Text/FontRepository.cpp`
- Modify: `src/Text/TextRuntimeManifest.h`
- Modify: `src/Text/TextRuntimeManifest.cpp`
- Modify: `src/Core/SmokeReport.h`
- Modify: `src/Core/SmokeReport.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `tests/test_asset_catalog.cpp`
- Modify: `tests/test_build_profile.cpp`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_package_finalizer.cpp`
- Modify: `tests/test_runtime_smoke.cpp`
- Modify: `tests/test_font_assets.cpp`
- Modify: `tests/smoke/create_fixture.cmake`
- Modify: `tests/smoke/run_end_to_end.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: production decode/shape/layout services, authored scene/prefab/localization data, FontFamily closure, canonical bundle roots, and `TextRuntimeManifest`.
- Produces: `TextCoverageManifest`, `TextPackageValidator::ValidateAndStage`, `TextNoticeGenerator`, asset catalog schema `3`, complete font/license closure, and pre-window exit-4 validation.

- [ ] Add data-driven failing cases for every design Section 11 row: lock/data tamper, broken hot reload with/without last-good import, authored invalid UTF-8, reachable missing glyph, dynamic missing glyph continuation, family cycle/invalid GUID/unconfirmed license, authored ref/layout blockers, runtime ref/layout continuation, atlas exhaustion, and second callback reflow. Feed importer failures through `AssetRecord::importDiagnostics` and assert exact stable code plus editor/dev/build/copied-runtime action and exit code.

- [ ] Run the package-policy red gate.

  ```bash
  cmake --build --preset debug --target test_text_package test_asset_catalog test_game_builder -j
  ```

  Expected: compile or assertions fail because coverage manifests, catalog SHA-256, license closure, and terminal-action policy are absent.

- [ ] Define one shared complete `AuthoredTextStyle` used by `BuildProfile` fixtures and localization defaults/overrides: family GUID; weight/stretch/slant; font size; language; ordered features; literal `variation="default"`; base direction; wrap/overflow/max lines; line spacing; horizontal/vertical alignment; ellipsis; optional width/height constraints; and preserved ordered unknown fields. Convert every field into `ParagraphStyle + LayoutConstraints` through one helper and reject non-default variation for Milestone A. Extend `BuildProfile` to schema `3` with authored `requiredTextFixtures` containing stable name, byte-preserved UTF-8, locale, and that complete style; schemas 1/2 read as an empty fixture list. Add `.localization` schema `1` assets with one complete table default and either no override or one complete per-entry override alongside stable key/locale/UTF-8 text—never a partial merge. Preserve unknown fields independently at root/default/entry/override scope, reject any unknown key that collides with a known key on serialization, import all family dependencies, and reject a string without a resolved family/style rather than borrowing a system/default font.

- [ ] Define package coverage records.

  ```cpp
  namespace molga::text {
  struct TextCoverageInputFile {
      std::string path;
      std::string sha256;
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
  class TextPackageValidator {
  public:
      bool ValidateAndStage(const BuildProfile&,
                            const BuildPlan&,
                            const std::filesystem::path& projectRoot,
                            const std::filesystem::path& resourceRoot,
                            TextCoverageManifest&,
                            TextDiagnosticSink&);
  };
  } // namespace molga::text
  ```

- [ ] Scan every reachable scene/prefab `UILabel`, `UITextInput.initialText`, and `TextRenderer2D`, every registered localization entry with its resolved family/style, and every required fixture. Decode/analyze/shape/layout them through the production services. Invalid authored UTF-8 or missing grapheme fails with source/object/family context; dynamic strings outside fixtures are explicitly not claimed as covered. Compute `fontFamilyRevision` as SHA-256 over one canonical byte encoding of the complete transitive content closure—authored family/face order, immutable font GUID/source SHA/face index/style, and fallback edges—never a process-local generation. Record every staged scene, prefab, localization, and configuration file that contributed authored text as a sorted safe resource-relative `TextCoverageInputFile`; compute its SHA from the final staged bytes so startup can prove the coverage scan still names the loaded content.

- [ ] Make generic `CopyAssets`/catalog emission exclude every `FontImporter` source/sidecar and every license-asset GUID named by any font record. Then compute the reachable transitive fallback-family closure, independent of traversal order, and explicitly inject only its validated font/license records and bytes. For every reachable face, read the verified immutable project artifact at `Library/Imported/Fonts/<artifactSha256>.sfnt`, stage those exact bytes once at `Assets/Fonts/<artifactSha256>.sfnt`, and rewrite the sealed catalog locator to that exact packaged path; never stage the authored source path or a project `Library` path. Stage each required notice/license once; an unused unconfirmed font is absent rather than accidentally copied. Reject normalized-path collisions with different bytes. Recursively scan final package resources for SFNT signatures/extensions and require an exact one-to-one match with manifest font paths—no extra font bytes, duplicate manifest paths, or manifest entries without bytes. Implement `TextNoticeGenerator` to verify the Task 1 base template and individual dependency-license hashes, append project-font entries sorted by `{fontGuid,sourceSha256,licenseAssetGuid}` into the package's `Licenses/ThirdPartyNotices.md`, and return the final notice SHA for `TextRuntimeManifest`; it never edits the source template. Reject invalid/cyclic graphs, unconfirmed redistribution, missing notice, mismatched source/artifact SHA or face index, or a font referenced only through a fallback edge that was not staged.

- [ ] Upgrade asset catalog to schema `3` with per-source SHA-256 while preserving legacy catalog read migration. Keep generic revision/FNV only as a non-security cache field. Canonically finalize the filtered catalog and hash it first; produce `TextCoverageManifest` with the final staged input-file hashes second; finalize `TextRuntimeManifest` with both catalog and coverage path/SHA plus all font/license hashes third; update `game.json` with the text manifest SHA last. The catalog bytes are an upstream-bound object, not a mutable list whose entries can be rewritten alongside assets.

- [ ] Add a pre-window packaged startup validator that checks `game.json → text_runtime.json → dependency/data/font/license/asset-catalog/coverage` hashes and paths. Require canonical catalog schema `3`, verify every catalog source path against its `contentSha256`, and verify every `TextCoverageInputFile` before asset or scene load. On failure print stable `PackageValidationFailed`, failed path/hash, and remediation to stderr and SmokeReport, then return `4` before SDL/window/scene. A sealed package never uses a last-good artifact, system font, or ASCII fallback.

- [ ] Add copied-package tamper cases for every file sealed by `text_runtime.json`, including `asset_catalog.json` and one coverage-contributing scene, plus a separate root-link case that mutates `game.json.text.path/manifestSha256` and proves the chain is rejected. Do not claim arbitrary un-hashed `game.json` fields are tamper-evident. Add recursive `otool -L` and `LC_RPATH` checks over every Mach-O; reject `/opt/homebrew`, `/usr/local`, checkout/build paths, HarfBuzz/ICU dynamic libs, or a non-allowlisted external dylib.

- [ ] Run all package-policy gates and an independent review checkpoint before qualification work.

  ```bash
  cmake --build --preset debug --target test_text_package test_asset_catalog \
    test_build_profile test_game_builder test_package_finalizer \
    runtime_smoke -j
  ctest --test-dir build/debug -R '^(test_text_package|test_asset_catalog|test_build_profile|test_game_builder|test_package_finalizer|runtime_smoke|smoke_end_to_end)$' --output-on-failure
  ctest --preset debug
  ```

  Expected: all automated correctness/package tests pass. Then invoke `superpowers:requesting-code-review` over Tasks 9–17 and resolve all blocker/high findings before proceeding.

- [ ] Commit coverage and fail-closed package policy.

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/AuthoredTextStyle.* \
    src/Assets/LocalizationTableAsset.* src/Assets/FontArtifactStore.* \
    src/Core/Importers/LocalizationTableImporter.* \
    src/Core/Importers/ImporterRegistry.cpp src/Text/TextCoverageManifest.* \
    src/Text/TextPackageValidator.* src/Text/TextNoticeGenerator.* \
    src/Text/FontRepository.* src/Text/TextRuntimeManifest.* \
    src/Core/BuildProfile.* src/Core/RuntimeStartup.* \
    src/Core/AssetDatabase.* src/Core/AssetDependencyValidator.cpp \
    src/Editor/GameBuilder.* src/Core/PackageLayout.cpp \
    src/Core/SmokeReport.* src/runtime_main.cpp tests/test_text_package.cpp \
    tests/test_asset_catalog.cpp tests/test_build_profile.cpp \
    tests/test_font_assets.cpp tests/test_runtime_startup.cpp \
    tests/test_game_builder.cpp tests/test_package_finalizer.cpp \
    tests/test_runtime_smoke.cpp tests/smoke/create_fixture.cmake \
    tests/smoke/text_network_deny.sb \
    tests/smoke/test_text_network_deny.cmake \
    tests/smoke/text_sandbox_capability_probe.mm \
    tests/smoke/run_end_to_end.cmake
  git commit -m "feat: enforce packaged text coverage and licenses"
  ```

## Task 18: Prove canonical parity, performance, GPU output, and visible macOS behavior

**Files:**

- Create: `src/Text/TextCanonicalSnapshot.h`
- Create: `src/Text/TextCanonicalSnapshot.cpp`
- Create: `src/Text/TextAllocatorTelemetry.h`
- Create: `src/Text/TextAllocatorTelemetry.cpp`
- Create: `src/Text/TextPerformanceHarness.h`
- Create: `src/Text/TextPerformanceHarness.cpp`
- Create: `src/Tools/TextQualification.h`
- Create: `src/Tools/TextQualification.cpp`
- Create: `src/Tools/TextPerformanceReportMain.cpp`
- Create: `src/Editor/TextQualificationEditorSession.h`
- Create: `src/Editor/TextQualificationEditorSession.cpp`
- Create: `src/Tools/VisibleTextSessionAudit.h`
- Create: `src/Tools/VisibleTextSessionAudit.cpp`
- Create: `src/Tools/VisibleWindowCatalogMain.cpp`
- Create: `src/Platform/MacTextInputSource.h`
- Create: `src/Platform/MacTextInputSource.mm`
- Create: `src/Platform/MacVisibleWindow.h`
- Create: `src/Platform/MacVisibleWindow.mm`
- Create: `src/Platform/NativeInputProvenance.h`
- Create: `src/Platform/TextInputOwnerTransitionAudit.h`
- Create: `tests/test_text_canonical.cpp`
- Create: `tests/test_text_performance.cpp`
- Create: `tests/test_text_qualification_editor.cpp`
- Create: `tests/test_visible_text_session_audit.cpp`
- Create: `tests/smoke/run_text_parity.cmake`
- Create: `tests/fixtures/text/expected/gpu-command-stream.json`
- Create: `tests/fixtures/text/expected/gpu-mixed-ui.rgba`
- Create: `tests/fixtures/text/qualification/build_profile.json`
- Create: `tests/fixtures/text/qualification/scene.json`
- Create: `tests/fixtures/text/qualification/trace.json`
- Create: `scripts/qualification/run_ui_text_macos.sh`
- Create: `scripts/qualification/record_ui_text_ime.sh`
- Create: `docs/qualification/ui-text-milestone-a/README.md`
- Modify: `.gitattributes`
- Modify: `docs/plans/2026-08-20-ui-text-production-backbone-design.md`
- Modify: `src/Core/BuildProfile.h`
- Modify: `src/Core/BuildProfile.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Core/Bootstrap.h`
- Modify: `src/Core/Bootstrap.cpp`
- Modify: `src/Core/SmokeReport.h`
- Modify: `src/Core/SmokeReport.cpp`
- Modify: `src/Platform/NativeInputEvent.h`
- Modify: `src/UI/UIInputEvent.h`
- Modify: `src/UI/UISystem.h`
- Modify: `src/UI/UISystem.cpp`
- Modify: `src/UI/UILayoutSystem.cpp`
- Modify: `src/UI/UIRenderCollector.cpp`
- Modify: `src/Editor/ImGuiTextInputBridge.h`
- Modify: `src/Editor/ImGuiTextInputBridge.cpp`
- Modify: `src/Editor/Windows/GameViewWindow.h`
- Modify: `src/Editor/Windows/GameViewWindow.cpp`
- Modify: `src/Editor/GameBuilder.h`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/Text/UnicodeTextBuffer.cpp`
- Modify: `src/Text/UnicodeAnalysis.cpp`
- Modify: `src/Text/FontFamilyResolver.cpp`
- Modify: `src/Text/TextShapingService.cpp`
- Modify: `src/Text/TextLayoutCache.cpp`
- Modify: `src/Text/TextLayoutService.cpp`
- Modify: `src/Rendering/FontAtlas.cpp`
- Modify: `src/Rendering/TextRenderer.cpp`
- Modify: `src/Rendering/RenderSystem2D.cpp`
- Modify: `tests/TextQualificationAssetTree.h`
- Modify: `tests/test_game_builder.cpp`
- Modify: `tests/test_text_input_arbiter.cpp`
- Modify: `tests/test_rendering_sdlgpu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: final runtime manifest, text layouts, UI snapshots/events/draw order, package outputs, renderer telemetry, and all prior automated gates.
- Produces: `TextCanonicalSnapshot`, tagged allocator telemetry, `TextPerformanceHarness`, three-mode parity reports, GPU golden evidence, copied-app audit, and visible Korean/Japanese IME qualification artifacts.

- [ ] Add failing canonical-export tests. Stable JSON includes portable dependency-contract SHA; each glyph binding's `{fontGuid,sourceSha256,faceIndex}` and only a revision derived solely from immutable content; glyph ID; source byte/grapheme range; 26.6 advance/offset/position; logical lines and visual run order; caret stop/affinity; UI rect/clip; canonical event target; and draw order. Exclude `contentGeneration`/import history, shared pointers, GPU handles, atlas UV/page, timestamp, physical raster pixels, runtime frame/world/type/instance IDs. Cold, warm, and hot-edit histories ending in identical bytes must export byte-identically.

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
  } // namespace molga::text
  ```

  The runtime audit retains absolute `UIFrameResult::frameIndex` and native `sequence`, but canonical JSON excludes both because editor/dev/package can process different leading frames/events. Canonical replay assigns consecutive nonzero `traceOrdinal` values from the input-trace file; the exporter serializes `{traceOrdinal,event kind,optional UIStableComponentKey target,delivered/skipped}` and rejects missing/duplicate ordinals. Targetless events such as `WindowFocus` emit JSON `null`; it never fabricates object `0`. It also omits `runtimeTarget`, world generation, runtime type ID, and component instance ID. It walks concrete `UISnapshot::{nodes,renderItems,hitTargets}`, serializes the immutable text layout carried by each text render payload, and emits no placeholder/event-kind-only record.

- [ ] Run the qualification-harness red gate.

  ```bash
  cmake --build --preset debug --target test_text_canonical test_text_performance -j
  ```

  Expected: compile fails because canonical export, allocator telemetry, and the performance harness do not exist.

- [ ] Add `--text-canonical <scene> --viewport 1920x1080 --input-trace <json> --output <json>` to editor, development runtime, and packaged runtime. Generate and freeze the scene through production serializers using the exact current component schemas (`UITextInput.maxGraphemes`, `textViewport`, nested `paragraphStyle`, and a sibling `UISelectable` focus shell), then require a deserialize/reserialize byte check. The trace loader produces native trace descriptors, not already-routed `UIInputEvent` values, and assigns one global consecutive `traceOrdinal` sequence. Replay focus/pointer-down through the shared `EngineHost` ingestion and dispatch path as batch one so runtime focus acquires the IME owner; only then ingest/stamp/edit/commit as batch two. Preserve global ordinals across the boundary, use the sole Task 12 vector loop in each batch, and retain each mode's absolute native sequence only in noncanonical audit. Each mode loads the same scene/catalog/trace and writes stable JSON before GPU raster differences.

- [ ] Make GameBuilder produce and finalize a complete `Qualification.app` containing the exact scene, trace, schema-3 catalog, six-font/two-license family closure, scripts/config/shaders, and sealed manifests. Keep the CMake `molga_runtime.app` engine shell distinct; it is not a game package and is never supplied as `PACKAGED_APP`. Stage byte-exact qualification inputs once under `Contents/Resources/Qualification`, build an equally complete explicit development resource root, copy `Qualification.app` outside the checkout, and require all source/development/package fixture hashes to match before launching the three modes. The automated editor and `DevelopmentRuntime` qualification branches are a dedicated noninteractive exception to ordinary project/development startup: before catalog or scene startup, each freshly calls `TextPackageValidator::ValidateSealedPackage` on the independent development root, consumes that result's `packagedFontAuthorities`, binds `FontArtifactStore::ForSealedPackage` to a fresh database, and loads the schema-3 catalog with `AssetCatalogMode::SealedPackage`. They must not use `ForProject`/project catalog mode merely because their mode name is editor or development; ordinary interactive editor/development startup remains unchanged. The copied-app branch independently uses the freshly validated authority for its own checkout-external `Contents/Resources` root.

- [ ] Add a tagged Text/UI allocator counter and instrumentation counters for decode, analysis, fallback, shape, final-line layout, cache hits, atlas uploads/bytes, snapshot rebuilds, and queue collection. Tests must prove warm static workload performs zero reshape and zero tagged heap allocation in the measured interval.

- [ ] Implement the exact performance workloads and nearest-rank p95 calculation from design Section 13.5: 1,000 labels × 10 graphemes = 10,000 cached glyphs; 2,000 mixed graphemes with 400/300/300/300/300/400 Latin/Arabic/Hebrew/Devanagari/Thai/CJK split; 120 warm-up frames; 600 samples; measurement boundaries from event-dequeue completion through queue completion for static and full decode→layout for dynamic.

- [ ] Make `test_text_performance` validate workload composition/report schema, zero warm-static reshape/allocation, and `peakResidentBytes <= 64 MiB` with no unlimited growth on **every** machine. Enforce only the timing gates `p95 <= 4 ms` and `p95 <= 8 ms` when the machine fingerprint exactly matches `Mac14,9`, M2 Pro 10-core, 16 GiB, macOS 26.5.1 (25F80), arm64, Apple clang 21.0.0, AC power, Release, backing scale 2.0, no debugger. A nonmatching machine records timing measurements as non-qualifying; it must still pass the universal correctness/cap/allocation gates.

- [ ] Extend command-stream/GPU tests with mixed-script glyph order, texture lifetime, nested scissor transitions, caret/selection, and an SDL_GPU/Metal offscreen golden with explicit channel tolerance. The golden proves this pinned fixture/backend only; it does not replace canonical structural parity.

- [ ] Implement `run_ui_text_macos.sh` to require a clean worktree with both tracked and untracked entries rejected except one newly created, collision-checked evidence directory; capture `testedCommit=$(git rev-parse HEAD)`, create `docs/qualification/ui-text-milestone-a/$testedCommit/`, and write that exact value into every report. Build Release, use GameBuilder to produce `Qualification.app`, run all presets, copy the finalized app to a fresh external temp directory, unset font/Homebrew variables, use the deny-network sandbox profile, recursively audit every Mach-O with `otool -L`/`otool -l`, run parity/performance/GPU gates, and save explicit raw command/stdout/stderr files, hashes, machine fingerprint, reports, and screenshots. Never use a wildcard `cat` as the evidence allowlist or silently reuse an existing candidate directory.

- [ ] Implement `record_ui_text_ime.sh` as an operator-assisted two-session visible gate. Session A launches the checkout-external copied `.app` and records real Korean/Japanese composition-update-commit, mixed-script rendering, nested clips, caret/selection, and HiDPI. Session B launches the exact Release editor and records Game View letterbox/crop, detached ImGui viewport mapping, and editor↔runtime IME ownership/focus transfer. Store distinct event audits/screenshots with tested executable hashes; exit nonzero unless both evidence sets are complete. A standalone packaged game is never expected to expose editor viewports, and synthetic SDL tests alone never satisfy either session.

- [ ] Invoke `superpowers:requesting-code-review` for the complete code/harness milestone, fix every blocker/high finding, rerun affected focused gates, then commit the reviewed candidate code, scripts, and golden inputs **before** creating qualification evidence.

  ```bash
  git add .gitattributes CMakeLists.txt tests/CMakeLists.txt \
    src/Text/TextCanonicalSnapshot.* \
    src/Text/TextAllocatorTelemetry.* src/Text/TextPerformanceHarness.* \
    src/Tools/TextQualification.* src/Tools/TextPerformanceReportMain.cpp \
    src/Tools/VisibleTextSessionAudit.* \
    src/Tools/VisibleWindowCatalogMain.cpp \
    src/Platform/MacTextInputSource.* src/Platform/MacVisibleWindow.* \
    src/Platform/NativeInputProvenance.h \
    src/Platform/TextInputOwnerTransitionAudit.h \
    src/Platform/NativeInputEvent.h src/main.cpp src/runtime_main.cpp \
    src/Core/Bootstrap.* src/Core/BuildProfile.* src/Core/SmokeReport.* \
    src/Editor/GameBuilder.* src/Editor/ImGuiTextInputBridge.* \
    src/Editor/TextQualificationEditorSession.* \
    src/Editor/Windows/GameViewWindow.* src/UI/UIInputEvent.h \
    src/UI/UISystem.* src/UI/UILayoutSystem.cpp src/UI/UIRenderCollector.cpp \
    src/Text/UnicodeTextBuffer.cpp src/Text/UnicodeAnalysis.cpp \
    src/Text/FontFamilyResolver.cpp src/Text/TextShapingService.cpp \
    src/Text/TextLayoutCache.cpp src/Text/TextLayoutService.cpp \
    src/Rendering/FontAtlas.cpp src/Rendering/TextRenderer.cpp \
    src/Rendering/RenderSystem2D.cpp tests/TextQualificationAssetTree.h \
    tests/test_text_canonical.cpp tests/test_text_performance.cpp \
    tests/test_text_qualification_editor.cpp \
    tests/test_visible_text_session_audit.cpp tests/test_game_builder.cpp \
    tests/test_text_input_arbiter.cpp tests/test_rendering_sdlgpu.cpp \
    tests/fixtures/text/expected tests/fixtures/text/qualification \
    tests/smoke/run_text_parity.cmake scripts/qualification \
    docs/qualification/ui-text-milestone-a/README.md
  git commit -m "test: add macOS UI text qualification harness"
  test -z "$(git status --porcelain)"
  ```

- [ ] Run fresh full verification using `superpowers:verification-before-completion`.

  Do not invoke `run_text_parity.cmake` with reconstructed paths here. The automated qualification runner owns the fresh checkout-external copied-app path and invokes the exact authoritative `07-parity-qualification.md` Task 18.4 Step 22 command, including its distinct development root, five build/validation/report inputs, validation stamp, and sandbox profile.

  ```bash
  cmake --preset debug && cmake --build --preset debug -j && ctest --preset debug
  cmake --preset release && cmake --build --preset release -j && ctest --preset release
  cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan
  cmake --preset ubsan && cmake --build --preset ubsan -j && ctest --preset ubsan
  scripts/qualification/run_ui_text_macos.sh
  scripts/qualification/record_ui_text_ime.sh
  ```

  Expected: all four preset suites pass; the three canonical JSON files are byte-identical; copied-app dependency/network/font checks pass; reference-machine performance passes; visible Korean/Japanese IME evidence is complete. If reference hardware or a human input-source session is unavailable, report that exact gate as open and do not mark Milestone A complete.

- [ ] Verify that every evidence report's `testedCommit` still equals current clean `HEAD`. Any code/script/golden change after the run invalidates the entire evidence directory: commit the revised candidate, remove only that uncommitted invalid run directory, and rerun both qualification scripts. Once evidence is immutable and complete, change the design maturity from `NOT STARTED` only to the exact verified state and commit evidence/design separately.

  ```bash
  tested_commit="$(git rev-parse HEAD)"
  test -f "docs/qualification/ui-text-milestone-a/$tested_commit/testedCommit.txt"
  test "$(cat "docs/qualification/ui-text-milestone-a/$tested_commit/testedCommit.txt")" = "$tested_commit"
  git add "docs/qualification/ui-text-milestone-a/$tested_commit"
  git add docs/plans/2026-08-20-ui-text-production-backbone-design.md
  git commit -m "docs: record macOS UI text qualification evidence"
  ```

## Final Completion Gate

- [ ] Confirm `git status --short --branch` contains no unintended files and every task commit is present in dependency order.
- [ ] Confirm every automatic correctness, serialization, GPU, package, Debug, Release, ASan, and UBSan gate is fresh and green.
- [ ] Confirm editor, development runtime, and copied `.app` canonical JSON SHA values are identical.
- [ ] Confirm every Section 11 terminal action was exercised with its exact continue/block/exit-4 behavior.
- [ ] Confirm copied `.app` runs with no Homebrew, system-font, network, checkout, or build-path dependency.
- [ ] Confirm actual visible Korean/Japanese IME and mixed-script evidence, reference-machine performance, 64 MiB atlas cap, license/notice closure, and manifest hashes are recorded.
- [ ] Mark the design document maturity as implemented/qualified only after all prior boxes are checked. Otherwise leave it `NOT STARTED`/in progress and list the precise open gates.

---

## Execution Handoff

The master contract is saved here and executable steps are split across `docs/superpowers/plans/2026-08-20-ui-text/01-*.md` through `07-*.md`. Choose one execution mode:

Before either mode starts, require a clean checkout whose `HEAD` already contains this reviewed master plus all seven subplans as one documentation-only plan-baseline commit. The approved design remains `NOT STARTED`; an executor must not leave these plan files untracked, fold them into an implementation commit, or treat their presence as implementation evidence.

1. **Subagent-Driven (recommended):** execute one subplan `Task N.M` at a time in this session using `superpowers:subagent-driven-development`, with spec and code-quality review after each independently committed slice.
2. **Inline Execution:** start a dedicated execution session using `superpowers:executing-plans`, process subplans `01 → 07` in order, stop at every stated exit/review checkpoint, and preserve the declared commits.
