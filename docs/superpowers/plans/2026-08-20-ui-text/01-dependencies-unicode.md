# Dependencies and Unicode Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the reproducible packaged-text dependency boundary, verified ICU lifetime, typed diagnostics, signed 26.6 arithmetic, and byte-preserving Unicode/ICU analysis required by Milestones 1–3.

**Architecture:** Maintainer-only acquisition scripts place immutable inputs in the repository, while normal builds compile only pinned vendored HarfBuzz/ICU sources and emit two deliberately different records: a machine-local provenance lock and a portable runtime/package contract. Each executable target stages and verifies the portable contract plus ICU data under its own `Engine/Text` root. A verified process lifetime owns those staged bytes, after which a text-owned UTF-8 buffer and ICU analyzer expose lossless source mappings and deterministic analysis in shared fixed-point units.

**Tech Stack:** C++17, CMake 3.27 presets, doctest, Git submodules, Git LFS, pinned HarfBuzz 14.3.1, pinned ICU4C 78.3, nlohmann/json, `Common/Sha256`.

**Spec:** [`docs/plans/2026-08-20-ui-text-production-backbone-design.md`](../../../plans/2026-08-20-ui-text-production-backbone-design.md)

**Master plan:** [`docs/superpowers/plans/2026-08-20-ui-text-production-backbone.md`](../2026-08-20-ui-text-production-backbone.md), Milestones 1–3.

## Global Constraints

- The approved design is authoritative. Stop and seek a design amendment before changing public behavior, failure policy, package layout, or scope.
- Preserve unrelated worktree changes. Before each task run `git status --short --branch`; stage only that task's listed files.
- HarfBuzz is commit `ab5ecbb83985034a76214ac0b2b833dcd590d774`; ICU is commit `21d1eb0f306e1141c10931e914dfc038c06121da`. Neither may resolve through Homebrew, a system installation, or network `FetchContent`.
- Build HarfBuzz static with ICU enabled and CoreText, Cairo, FreeType, Graphite2, GLib/GObject, introspection, utils, subset, raster, vector, GPU, and demos disabled. Build static ICU common/i18n plus the pinned private stubdata intermediate with `U_STATIC_IMPLEMENTATION`, but expose/package only logical `icuuc` and `icui18n`; `libicudata` is never a third consumer archive.
- The ICU archive is `icu4c-78.3-data-bin-l.zip`, SHA-256 `982619632b78887f1895b063e96e8c3cc7f99283337c8abbd05aa71635de613c`. The packaged `icudt78l.dat` is exactly `33107232` bytes with SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b`.
- The text-owned rasterizer is the exact `imstb_truetype.h` from ImGui commit `b48d1afbe8ee8b238e2961dc363a949dd7304e23`, SHA-256 `c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528`.
- License hashes are ICU `e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af`, HarfBuzz `ba8f810f2455c2f08e2d56bb49b72f37fcf68f1f4fade38977cfd7372050ad64`, stb `7587efcca32db8f95bf5860dea6dfa4be410ee723e1964db3689c8ddf2a2e4a9`, and Inter `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a`.
- Normal dependency configure/build is network-offline: it never downloads, runs an acquisition script, or invokes `FetchContent`. This does not require CMake, the compiler, SDK, `ar`, `ranlib`, or `nm` to live below the nested dependency prefix; those host toolchain paths are recorded in the machine-local build lock, while dependency source/include/library/backend resolution alone must remain vendored and static. Acquisition writes temporary files, validates every member, then replaces destinations only after the complete set passes.
- `text_dependency_build_lock.json` is machine-local build provenance and may contain canonical absolute source/include/archive paths. `text_dependency_contract.json` is portable, uses an explicit field allowlist, contains no checkout/build absolute path, and is the only dependency record later hashed by caches or packaged.
- Editor and development runtime read only `$<TARGET_FILE_DIR>/Engine/Text/{text_dependency_contract.json,icudt78l.dat}`. Their always-run CMake staging targets repair missing/tampered copies from verified build/repository inputs; application code never falls back to a source-tree or current-working-directory path.
- Verify manifests and data before `udata_setCommonData`; restrict ICU file access, then call `u_init`. Keep aligned data alive through terminal `u_cleanup`, and construct no ICU/HarfBuzz text object outside that lifetime. A successfully initialized process never restarts ICU: pinned `hb_icu_get_unicode_funcs()` caches ICU normalizer pointers process-statically, so `u_cleanup` occurs once only after every text client is gone and immediately before process return. Not-ready and post-cleanup tests run in fresh subprocesses rather than stop/restore the same process.
- Preserve original UTF-8 bytes. Invalid subsequences render as `U+FFFD` with their original byte ranges, and authored text is never normalized implicitly.
- ICU owns extended-grapheme, line-break, script/script-extension, and BiDi analysis. All logical measurement uses validated signed 26.6 fixed point.

## Prerequisite Contract

- The master plan and approved design above are present and unchanged.
- The task starts from a reviewed commit with `cmake --preset debug && cmake --build --preset debug -j && ctest --preset debug` green. If the inherited checkout is dirty, record the unrelated paths and do not stage them.
- Network is permitted only for the explicit maintainer acquisition commands in Task 1.1. Every later red/green command must succeed offline from committed/submodule inputs.

## Exit Contract

- `test_text_dependencies`, `test_text_runtime_dependencies`, `test_unicode_text`, and `test_unicode_not_ready` pass from a fresh Debug configure; `test_unicode_text` also passes ASan and UBSan.
- The build barrier proves source/archive provenance and transactionally publishes a mutually consistent build-lock/portable-contract pair. The portable record contains no absolute checkout/build path, and no test executable links a dynamic ICU/HarfBuzz library.
- Both development executable roots contain a verified `Engine/Text` contract/data pair. Editor failure leaves the ImGui shell usable with Game View text blocked; development runtime failure returns `4` before window/renderer/script/scene startup.
- `UnicodeTextBuffer` preserves byte/scalar/UTF-16 mappings, `UnicodeAnalysis` exposes deterministic grapheme/line/script/BiDi items, and `DecodeUtf8` is compatibility-only.
- Do not begin [`02-font-shaping-layout.md`](02-font-shaping-layout.md) until all gates above are freshly green.

## File Responsibility Map

| Unit | Authoritative files | Responsibility |
|---|---|---|
| Immutable inputs | `cmake/AcquireTextRuntimeData.cmake`, `cmake/AcquireTextFixtures.cmake`, `resources/text/*`, `resources/licenses/*`, `tests/fixtures/text/*`, `external/text/rasterizer/*` | Maintainer-only acquisition, SHA/size/license/corpus lock |
| Static builds and records | `cmake/TextDependencies.cmake`, `cmake/MergeIcuStubdata.cmake`, `cmake/MergeHarfBuzzIcu.cmake`, `cmake/RepairIcuRawInstall.cmake`, `cmake/RepairHarfBuzzRawInstall.cmake`, `cmake/VerifyTextDependencies.cmake`, `cmake/StageTextRuntimeResources.cmake`, `resources/text/dependency-contract.input.json` | Vendored static dependency build, repairable raw outputs, deterministic logical composites, build lock, portable contract, build barrier, per-target development staging |
| Diagnostics/lifetime | `src/Text/TextDiagnostic.*`, `src/Text/TextRuntimeDependencies.*` | Stable typed errors, rate limiting, verified ICU process lifetime |
| Fixed point | `src/Common/Fixed26_6.*` | Checked signed 26.6 logical-unit arithmetic |
| Unicode | `src/Text/UnicodeTextBuffer.*`, `src/Text/UnicodeAnalysis.*` | Byte-preserving decode and ICU paragraph analysis |

---

### Task 1.1: Acquire and lock immutable text inputs

**Prerequisite:** Only the approved hashes in Global Constraints and immutable URLs below are accepted; do not substitute a tag head or locally installed file.

**Files:**

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
- Modify: `.gitmodules`
- Modify: `.gitattributes`
- Modify: `.gitignore`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: pinned ImGui submodule and `molga::Sha256File(const std::filesystem::path&, std::string*)`.
- Produces: exact vendored source roots, committed runtime data/licenses/font corpus, schema-1 `dependency-contract.input.json`, and schema-1 `font_manifest.json`.

- [x] **Step 1: Register the failing immutable-input test.** Add `molga_add_test(test_text_dependencies test_text_dependencies.cpp)`, define `MOLGA_SOURCE_DIR` and `MOLGA_BINARY_DIR` from the exact CMake source/binary directories, and add this actual test:

  ```cpp
  #include "doctest.h"
  #include <filesystem>
  #include "Common/Sha256.h"

  TEST_CASE("immutable text inputs match the approved bytes") {
      const std::filesystem::path root = MOLGA_SOURCE_DIR;
      CHECK(std::filesystem::file_size(root / "resources/text/icudt78l.dat") ==
            33107232ULL);
      CHECK(molga::Sha256File(root / "resources/text/icudt78l.dat") ==
            "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b");
      CHECK(molga::Sha256File(root / "external/text/rasterizer/imstb_truetype.h") ==
            "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528");
      CHECK(molga::Sha256File(root / "tests/fixtures/text/fonts/NotoSansKR-Regular.otf") ==
            "69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68");
  }
  ```

- [x] **Step 2: Run the immutable-input red gate.**

  Run: `cmake --preset debug && cmake --build --preset debug --target test_text_dependencies -j && ctest --test-dir build/debug -R '^test_text_dependencies$' --output-on-failure`

  Expected: build succeeds, then the test FAILS because `resources/text/icudt78l.dat` and the new locked fixture tree do not exist; no system dependency is consulted.

- [x] **Step 3: Pin the HarfBuzz source gitlink.**

  ```bash
  git submodule add https://github.com/harfbuzz/harfbuzz.git external/harfbuzz
  git -C external/harfbuzz checkout ab5ecbb83985034a76214ac0b2b833dcd590d774
  test "$(git -C external/harfbuzz rev-parse HEAD)" = \
    ab5ecbb83985034a76214ac0b2b833dcd590d774
  ```

  Expected: the exact commit comparison succeeds.

- [x] **Step 4: Pin the ICU source gitlink.**

  ```bash
  git submodule add https://github.com/unicode-org/icu.git external/icu
  git -C external/icu checkout 21d1eb0f306e1141c10931e914dfc038c06121da
  git submodule status external/harfbuzz external/icu
  ```

  Expected: both lines contain the exact 40-character commits and neither begins with `+`, `-`, or `U`.

- [x] **Step 5: Register only large binary text artifacts with Git LFS.** Add `/.text-acquire-tmp/` to `.gitignore`, then run:

  Run: `git lfs track 'resources/text/*.dat' 'tests/fixtures/text/fonts/*.ttf' 'tests/fixtures/text/fonts/*.otf' && git lfs track`

  Expected: the three patterns appear once; JSON, CMake, license, and rasterizer source files remain normal Git objects.

- [x] **Step 6: Stage and verify the text-owned rasterizer snapshot.** In `AcquireTextRuntimeData.cmake`, create the unique acquisition root first and copy the ImGui source only into that root—not its final destination:

  ```cmake
  string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef acquire_id)
  set(acquire_root "${SOURCE_ROOT}/.text-acquire-tmp/${acquire_id}")
  file(MAKE_DIRECTORY "${acquire_root}")
  file(COPY_FILE
    "${SOURCE_ROOT}/external/imgui/imstb_truetype.h"
    "${acquire_root}/imstb_truetype.h" ONLY_IF_DIFFERENT)
  file(SHA256 "${acquire_root}/imstb_truetype.h" rasterizer_sha)
  if(NOT rasterizer_sha STREQUAL
     "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528")
    message(FATAL_ERROR "text rasterizer snapshot hash mismatch")
  endif()
  ```

  Expected: no byte under `external/text/rasterizer` changes before the complete transaction publishes.

- [x] **Step 7: Implement ICU archive download and exact-member extraction.** Use this executable CMake structure in `AcquireTextRuntimeData.cmake`:

  ```cmake
  set(archive_sha "982619632b78887f1895b063e96e8c3cc7f99283337c8abbd05aa71635de613c")
  file(DOWNLOAD
    "https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-data-bin-l.zip"
    "${acquire_root}/icu.zip"
    EXPECTED_HASH "SHA256=${archive_sha}" STATUS download_status)
  list(GET download_status 0 download_code)
  if(NOT download_code EQUAL 0)
    message(FATAL_ERROR "ICU data download failed: ${download_status}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${acquire_root}/icu.zip"
       DESTINATION "${acquire_root}/icu")
  file(GLOB_RECURSE data_candidates
       LIST_DIRECTORIES false
       "${acquire_root}/icu/*icudt78l.dat")
  list(LENGTH data_candidates data_count)
  if(NOT data_count EQUAL 1)
    message(FATAL_ERROR "expected exactly one icudt78l.dat")
  endif()
  get_filename_component(data_name "${data_candidates}" NAME)
  if(NOT data_name STREQUAL "icudt78l.dat")
    message(FATAL_ERROR "unexpected ICU data member name: ${data_name}")
  endif()
  file(SIZE "${data_candidates}" data_size)
  file(SHA256 "${data_candidates}" data_sha)
  if(NOT data_size EQUAL 33107232 OR
     NOT data_sha STREQUAL "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b")
    message(FATAL_ERROR "ICU data member size/hash mismatch")
  endif()
  ```

- [x] **Step 8: Stage vendored dependency licenses.** Copy `external/harfbuzz/COPYING`, `external/icu/LICENSE`, and LF-preserving rasterizer lines `5045..5085` into the unique acquisition root, then verify the HarfBuzz/ICU/stb hashes from Global Constraints.

- [x] **Step 8a: Stage the pinned Inter license.** Download only `https://raw.githubusercontent.com/rsms/inter/2ce9119398be143fa289c3e180824db1b7ed803e/LICENSE.txt` into the same root and require Inter SHA-256 `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a`.

- [x] **Step 8b: Stage the engine notice base.** Generate the immutable engine-only `ThirdPartyNotices.md` base template in the same root from the four verified logical license names; package-specific font rows remain a later packaging task.

- [x] **Step 9: Add the exact transaction entrypoint and pair validation.** Define one positional API in `TextArtifactTransaction.cmake`; callers pass a semicolon-separated allowlist of canonical destination roots, then alternating staged and destination paths:

  ```cmake
  function(text_publish_artifact_set result_var journal_path inject_failure_at
           allowed_destination_roots)
    text_recover_artifact_journal(
      recovery_ok "${journal_path}" "${allowed_destination_roots}")
    if(NOT recovery_ok)
      message(FATAL_ERROR "text artifact journal recovery failed")
    endif()
    set(pair_args ${ARGN})
    list(LENGTH pair_args pair_arg_count)
    math(EXPR pair_remainder "${pair_arg_count} % 2")
    if(pair_arg_count EQUAL 0 OR NOT pair_remainder EQUAL 0)
      message(FATAL_ERROR
        "text_publish_artifact_set requires staged/destination pairs")
    endif()
    text_prepare_artifact_transaction(
      prepare_ok "${journal_path}" "${allowed_destination_roots}"
      "${pair_args}")
    if(NOT prepare_ok)
      set(${result_var} FALSE PARENT_SCOPE)
      return()
    endif()
    text_publish_prepared_artifact_transaction(
      publish_ok "${journal_path}" "${inject_failure_at}")
    if(NOT publish_ok)
      text_recover_artifact_journal(
        recovered "${journal_path}" "${allowed_destination_roots}")
      if(NOT recovered)
        message(FATAL_ERROR "text artifact rollback validation failed")
      endif()
      set(${result_var} FALSE PARENT_SCOPE)
      return()
    endif()
    set(${result_var} TRUE PARENT_SCOPE)
  endfunction()
  ```

  Implement the three named helpers in Steps 10–12. Reject semicolons in path arguments, duplicate destinations, a staged path equal to its destination, a missing staged file, or any destination outside the caller-approved source/build roots before changing a destination.

- [x] **Step 10: Allocate the transaction identity and entry records.** After all staged inputs pass their task-specific size/SHA checks, generate one 128-bit hexadecimal transaction ID and one unique transaction directory beside the journal. For each alternating pair, compute `stagedSha256`, `destinationExisted`, and—when present—`destinationSha256`; reject any unreadable input before creating a publish sibling.

- [x] **Step 10a: Stage each new same-directory sibling.** Set `newSibling` to `"." + destinationFilename + ".molga-new-" + transactionId + "-" + decimalIndex`, copy the staged file beside its destination, and verify that sibling against `stagedSha256`.

- [x] **Step 10b: Stage each rollback sibling.** For an existing destination set `backupSibling` to the corresponding `.molga-backup-` name, copy the old bytes beside the destination, and verify `destinationSha256`; record an empty backup path for a previously absent destination. Do not publish any destination until every new/backup sibling verifies.

- [x] **Step 11: Persist the prepared journal.** Atomically replace `journal_path` with schema-1 JSON containing the transaction ID, unique directory, every exact path/SHA/existence flag, and per-entry state `prepared`; read it back and validate the complete entry count before publishing index 1.

- [x] **Step 11a: Enter one publish operation durably.** For index `1..N`, atomically mark only that entry `publishing` before touching its destination. If positive `inject_failure_at == index`, branch immediately to rollback.

- [x] **Step 11b: Replace and verify one destination.** Call `file(RENAME "${newSibling}" "${destination}" RESULT rename_result)`, verify the destination against `stagedSha256`, and branch to rollback on either error without visiting the next index.

- [x] **Step 11c: Close one publish operation durably.** If negative `inject_failure_at == -index`, branch to rollback after the verified rename but before the state update; otherwise atomically mark the entry `published` and continue to the next index.

- [x] **Step 12: Load and validate a pending journal.** Implement exact API `text_recover_artifact_journal(result_var journal_path allowed_destination_roots)`. At function entry and after a Step 11 failure, parse only schema `1`; canonicalize the caller-supplied roots, require every journal destination/new/backup/transaction path to satisfy exact equality or containment under that independent allowlist, then revalidate every exact path/index/transaction ID and refuse a new transaction until recovery completes. Never trust a root copied from the journal itself.

- [x] **Step 12a: Classify reverse rollback entries.** Visit every `published` or `publishing` entry in reverse index order. A destination with `stagedSha256` needs restoration, and one already matching the recorded old SHA needs no write; any third SHA is a fatal external conflict that retains the journal and backups for manual recovery.

- [x] **Step 12b: Restore one destination.** For a destination requiring restoration, rename its verified `backupSibling` back and verify `destinationSha256` when `destinationExisted` is true; otherwise remove only that newly created exact destination and verify it is absent.

- [x] **Step 12c: Clean verified transaction artifacts.** Only after every restoration verifies, remove unpublished new siblings, remaining backups, the unique transaction directory, and journal; `text_recover_artifact_journal` then returns `TRUE` to mean the root is safe and has no pending transaction, while the enclosing publisher returns `FALSE` for the injected/current publish failure. An external conflict returns recovery `FALSE` and retains the journal/backups. After a fully successful publish, the publish helper removes the same backup/journal/transaction artifacts and the enclosing publisher returns `TRUE`.

- [x] **Step 12d: Bound caller cleanup.** Each acquisition caller removes only its exact `${acquire_root}` or fixture temporary directory after successful publication or verified rollback; it never recursively removes the shared `.text-acquire-tmp` parent.

- [x] **Step 13: Build the offline transaction fixture.** Under `TEXT_TRANSACTION_SELF_TEST=ON`, generate five old synthetic destinations and five different staged files in one uniquely named fixture root, then record all ten SHA values.

- [x] **Step 13a: Exercise one injected failure.** Call the real publisher with the requested `INJECT_PUBLISH_FAILURE_AT`, assert a false result, all five old destination SHA values, and no leftover journal/new/backup files.

- [x] **Step 13b: Exercise the clean rerun.** Call the same publisher with injection disabled, assert all five new SHA values, and assert the exact fixture transaction directory/journal are absent.

- [x] **Step 13c: Run both crash windows twice.** Execute:

  ```bash
  cmake -DSOURCE_ROOT="$PWD" -DTEXT_TRANSACTION_SELF_TEST=ON \
    -DINJECT_PUBLISH_FAILURE_AT=3 -P cmake/AcquireTextRuntimeData.cmake
  cmake -DSOURCE_ROOT="$PWD" -DTEXT_TRANSACTION_SELF_TEST=ON \
    -DINJECT_PUBLISH_FAILURE_AT=3 -P cmake/AcquireTextRuntimeData.cmake
  cmake -DSOURCE_ROOT="$PWD" -DTEXT_TRANSACTION_SELF_TEST=ON \
    -DINJECT_PUBLISH_FAILURE_AT=-3 -P cmake/AcquireTextRuntimeData.cmake
  cmake -DSOURCE_ROOT="$PWD" -DTEXT_TRANSACTION_SELF_TEST=ON \
    -DINJECT_PUBLISH_FAILURE_AT=-3 -P cmake/AcquireTextRuntimeData.cmake
  ```

  Expected: all four runs report `rollback verified; clean publish verified`; before-rename and crash-window failures each preserve all five old destination SHA values, their clean rerun publishes all five new values, and no transaction artifact remains.

- [x] **Step 14: Add the exact-destination fast path.** After journal recovery, validate the complete runtime destination set and return before network access only when every expected size/hash is already exact.

- [x] **Step 14a: Publish the exact real runtime set.** Pass these eight staged/destination pairs to one publisher call: ICU data → `resources/text/icudt78l.dat`; rasterizer header → `external/text/rasterizer/imstb_truetype.h`; extracted stb license → both `external/text/rasterizer/LICENSE.txt` and `resources/licenses/StbTrueType.txt`; HarfBuzz license → `resources/licenses/HarfBuzz.txt`; ICU license → `resources/licenses/ICU.txt`; Inter license → `assets/fonts/Inter-v4.0-OFL.txt`; notice base → `resources/licenses/ThirdPartyNotices.md`.

- [x] **Step 14b: Run and verify the real runtime transaction.** Run `cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextRuntimeData.cmake`, then require the ICU size and all eight destination hashes.

- [x] **Step 14c: Prove the fast path is offline and non-replacing.** Add a script-test flag `TEXT_FAIL_ON_NETWORK` whose download wrapper raises `FATAL_ERROR` if called. Record all eight destination SHA/mtime values, run `cmake -DSOURCE_ROOT="$PWD" -DTEXT_FAIL_ON_NETWORK=ON -P cmake/AcquireTextRuntimeData.cmake`, and require identical values plus log marker `all runtime text artifacts already verified`.

- [x] **Step 15: Encode the base fixture download rows.** Add these three exact logical destination/source/size/SHA rows to `AcquireTextFixtures.cmake`:

  | File | Source commit/path | Bytes | SHA-256 |
  |---|---|---:|---|
  | `NotoSans-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-Regular.ttf` | 569208 | `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` |
  | `NotoSansArabic-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansArabic/NotoSansArabic-Regular.ttf` | 240456 | `ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b` |
  | `NotoSansHebrew-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansHebrew/NotoSansHebrew-Regular.ttf` | 26900 | `a7fa16fffb27bedb060a0866267c29e9859aeb9c21cc33f5b3aaf6eb062eca85` |

- [x] **Step 15a: Encode the Indic/CJK fixture download rows.** Append these three exact rows:

  | File | Source commit/path | Bytes | SHA-256 |
  |---|---|---:|---|
  | `NotoSansDevanagari-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansDevanagari/NotoSansDevanagari-Regular.ttf` | 219212 | `385e78e6359a9d88a0f243d53b1209d7548361ba2194e2b9ec779bcaa7e8949d` |
  | `NotoSansThai-Regular.ttf` | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSansThai/NotoSansThai-Regular.ttf` | 37752 | `404ddfb5ed0aaa6b6ec8a85700d682978992062d67da93903967b56cbd9a4acc` |
  | `NotoSansKR-Regular.otf` | `https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf` | 4644748 | `69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68` |

- [x] **Step 15b: Encode the immutable fixture-license rows.** Append these two logical destination/source/SHA rows:

  | File | Source commit/path | Bytes | SHA-256 |
  |---|---|---:|---|
  | Noto Fonts license | `https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/LICENSE` | manifest | `0dab92d0544f7b233403f14b84a663bdbfa746982eda629e7f4f9ffe1b036feb` |
  | Noto CJK license | `https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/LICENSE` | manifest | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` |

- [x] **Step 15c: Download the complete fixture set into one unique root.** For each table row call `file(DOWNLOAD "${url}" "${temporary_path}" EXPECTED_HASH "SHA256=${sha256}" STATUS status)` and fail before publication if any status is nonzero.

- [x] **Step 15d: Validate and publish the fixture set once.** Validate every declared size/hash in that unique root, then pass all eight staged/destination pairs to one `text_publish_artifact_set` call using the same rollback journal; never publish a partial table.

- [x] **Step 16: Run the fixture script and verify its transaction.**

  Run: `cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextFixtures.cmake && shasum -a 256 tests/fixtures/text/fonts/* tests/fixtures/text/licenses/*`

  Expected: the eight output hashes equal the table, and a second run changes no tracked bytes.

- [x] **Step 17: Write the portable dependency input manifest.** `dependency-contract.input.json` schema `1` records exact commits, permitted/forbidden options, archive/data/rasterizer/license hashes, stable logical archive names, and repo-relative source identities. It contains neither compiler/build results nor absolute paths.

- [x] **Step 18: Write the fixture corpus manifest.** `font_manifest.json` schema `1` records the table, Inter font SHA `64f8be6e55c37e32ef03da99714bf3aa58b8f2099bfe4f759a7578e3b8291123`, and strings `ffi`, `AV`, `x\u0301`, `سلام`, `ن\u200Dن`, `abc שלום 123!`, `क्षि`, `क्\u200Dष`, `กำลัง เก่ง`, `한글 日本語 中文 漢字（、。）`, `！\uFE00`, and `👩\u200D🚀`.

- [x] **Step 19: Lock the variable-font rejection fixture.** Add a manifest/test assertion that existing `tests/fixtures/fonts/NotoSansKR-Regular.ttf` has SHA `194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252` and tables `fvar/gvar/avar/HVAR`; never use its modified `OFL.txt` as provenance.

- [x] **Step 20: Run the immutable-input green gate.**

  Run: `cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextRuntimeData.cmake && cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextFixtures.cmake && cmake --build --preset debug --target test_text_dependencies -j && ctest --test-dir build/debug -R '^test_text_dependencies$' --output-on-failure`

  Expected: PASS; every acquisition hash matches and repeated execution leaves identical destination bytes.

- [x] **Step 21: Commit the immutable inputs.**

  ```bash
  git add .gitmodules .gitattributes .gitignore tests/CMakeLists.txt \
    cmake/AcquireTextRuntimeData.cmake cmake/AcquireTextFixtures.cmake \
    cmake/TextArtifactTransaction.cmake \
    external/harfbuzz external/icu external/text resources/text \
    resources/licenses assets/fonts/Inter-v4.0-OFL.txt tests/fixtures/text \
    tests/test_text_dependencies.cpp
  git commit -m "build: lock text dependency inputs"
  ```

**Exit:** All immutable bytes and source gitlinks are reviewable without running a normal build; acquisition is explicit and transactionally fail-closed.

---

### Task 1.2: Build vendored static dependencies and emit separate provenance records

**Prerequisite:** Task 1.1 is committed; the acquisition scripts are not invoked by configure or build.

**Files:**

- Create: `cmake/TextDependencies.cmake`
- Create: `cmake/MergeIcuStubdata.cmake`
- Create: `cmake/MergeHarfBuzzIcu.cmake`
- Create: `cmake/RepairIcuRawInstall.cmake`
- Create: `cmake/RepairHarfBuzzRawInstall.cmake`
- Create: `cmake/ProbeIcuStubdataLink.cmake`
- Create: `cmake/ProbeHarfBuzzIcuLink.cmake`
- Create: `cmake/VerifyTextDependencies.cmake`
- Modify: `cmake/TextArtifactTransaction.cmake`
- Modify: `CMakeLists.txt`
- Modify: `src/Rendering/FontFace.cpp`
- Modify: `tests/test_text_dependencies.cpp`
- Create: `tests/test_text_harfbuzz_link.cpp`
- Create: `tests/test_text_icu_link.cpp`
- Create: `tests/probes/icu_stub_link.cpp`
- Create: `tests/probes/harfbuzz_icu_link.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 1.1 gitlinks, input contract, data/licenses/rasterizer, and existing `molga_core`.
- Produces: `molga_text_icuuc` backed by one composite common archive containing the pinned `icudt78_dat` stub symbol, `molga_text_icui18n`, `molga_text_harfbuzz` backed by one deterministic composite of upstream raw `harfbuzz` plus raw `harfbuzz-icu`, `molga_text_rasterizer`, `molga_text_dependencies_ready`, `${CMAKE_BINARY_DIR}/generated/text_dependency_build_lock.json`, `${CMAKE_BINARY_DIR}/generated/text_dependency_contract.json`, and three separate attachment seams: barrier-only, portable-contract C++ consumer, and provenance-test-only. `MOLGA_TEXT_DEPENDENCY_CONTRACT` is defined only for an explicitly registered C++ reader; `MOLGA_TEXT_DEPENDENCY_BUILD_LOCK`, `MOLGA_SOURCE_DIR`, and `MOLGA_BINARY_DIR` are defined only for `test_text_dependencies`. The logical/public ICU archive set remains exactly `icui18n` plus `icuuc`; stubdata is provenance within `icuuc`. The logical/public HarfBuzz set remains exactly one `harfbuzz` archive; the upstream adapter archive is composite provenance only, never a second public target or manifest archive.

- [x] **Step 1: Add dependency-record JSON helpers.**

  ```cpp
  #include <fstream>
  #include <nlohmann/json.hpp>

  static nlohmann::json ReadJson(const std::filesystem::path& path) {
      std::ifstream input(path);
      REQUIRE_MESSAGE(input.good(), path.string());
      return nlohmann::json::parse(input);
  }

  static void CheckPortable(const nlohmann::json& node) {
      if (node.is_string()) {
          const std::string value = node.get<std::string>();
          CHECK_FALSE(std::filesystem::path(value).is_absolute());
          CHECK(value.find(MOLGA_SOURCE_DIR) == std::string::npos);
          CHECK(value.find(MOLGA_BINARY_DIR) == std::string::npos);
      } else if (node.is_array() || node.is_object()) {
          for (const auto& child : node) CheckPortable(child);
      }
  }
  ```

- [x] **Step 1a: Add canonical containment/hash helpers.** Include `Common/Sha256.h`. Define `CheckCanonicalPathEquals`, `CheckCanonicalPathUnder`, `CheckEveryIncludeAndArchiveUnderNestedPrefix`, `CheckEveryDependencyArchiveIsStatic`, and `CheckSharedDependencyFieldsEqual` with `std::filesystem::weakly_canonical` plus path-component equality/containment—never a string-prefix test—and an explicit shared-field allowlist matching `dependency-contract.input.json`. `BuildPath(relative)` rejects absolute/empty/escaping input, returns `weakly_canonical(path(MOLGA_BINARY_DIR)/relative)`, and reuses the component containment check. `RequiredSha256File(path)` calls `molga::Sha256File(path,&error)` and throws on a nonempty error/invalid 64-hex digest. `JsonContainsSubstring(node,token)` recursively performs a case-sensitive substring search in every object key and string value; arrays recurse by element, so `libharfbuzz-icu.a` cannot evade a check for `harfbuzz-icu`. `CheckExactObjectKeys(actual,expectedKeys)` rejects every missing/extra key. `CheckExactPortableObjectKeys(actual,input,generatedKeys)` delegates to it and requires the portable object's key set to equal the committed input object's key set plus the explicitly supplied generated keys.

  `CheckHarfBuzzCompositeProvenanceMatchesFiles(lock)` creates one unique caller-owned temporary directory and a nonexisting `harfbuzz-read-only-result.json`, then invokes the absolute `MOLGA_CMAKE_COMMAND` without a shell using this literal argument vector: `-DMODE=HARFBUZZ_READ_ONLY`, `-DBUILD_LOCK=<MOLGA_TEXT_DEPENDENCY_BUILD_LOCK>`, `-DSOURCE_ROOT=<MOLGA_SOURCE_DIR>`, `-DBINARY_ROOT=<MOLGA_BINARY_DIR>`, `-DRESULT_ROOT=<unique-directory>`, `-DRESULT_FILE=<unique-directory>/harfbuzz-read-only-result.json`, `-P`, `<MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT>`. Require exact exit code `0`, a newly created regular result file, and exact schema-1 object keys `{schemaVersion,mode,rawCoreSha256,rawAdapterSha256,adapterObjectSha256,finalCompositeSha256,filteredMemberDigest,archiverFamily,arPath,ranlibPath,nmPath,arAppendFlags,ranlibFlags,zeroArDate,rawCoreDefinedAdapterSymbols,rawIcuAdapterDefinedAdapterSymbols,compositeDefinedAdapterSymbols}` with integer `schemaVersion:1` and string `mode:"HARFBUZZ_READ_ONLY"`; reject unknown/missing keys or wrong types. Validate those two discriminator fields independently and compare every remaining composite result field with `lock.harfbuzz.icuComposite`. The script canonicalizes the recorded raw-core/raw-adapter/final paths below `${BINARY_ROOT}/text-dependencies`, validates the configured ar/ranlib/nm paths and exact deterministic flags, recomputes all three archive hashes, re-enumerates and exact-name-extracts the sole adapter object inside `RESULT_ROOT`, recomputes its hash and the filtered ordered-member digest, and reruns the raw-zero/adapter-one/composite-one symbol probes. It rejects a preexisting/escaping result, production publication arguments, or any mode other than the closed read-only mode and never publishes JSON or mutates a dependency archive. These helpers and the argv subprocess shim exist only in `test_text_dependencies`.

- [x] **Step 1b: Add the failing portable/build separation test.**

  ```cpp
  TEST_CASE("build provenance and portable dependency contract agree") {
      const auto lock = ReadJson(MOLGA_TEXT_DEPENDENCY_BUILD_LOCK);
      const auto portable = ReadJson(MOLGA_TEXT_DEPENDENCY_CONTRACT);
      const auto input = ReadJson(std::filesystem::path(MOLGA_SOURCE_DIR) /
                                  "resources/text/dependency-contract.input.json");
      CHECK(lock.at("harfbuzz").at("commit") == portable.at("harfbuzz").at("commit"));
      CHECK(lock.at("icu").at("commit") == portable.at("icu").at("commit"));
      CHECK(lock.at("icuData").at("sha256") == portable.at("icuData").at("sha256"));
      CHECK(lock.at("harfbuzz").at("options").at("icu") == true);
      CHECK(lock.at("harfbuzz").at("options").at("coretext") == false);
      CHECK(lock.at("icu").at("libraries") ==
            nlohmann::json::array({"icui18n", "icuuc"}));
      CHECK(portable.at("icu").at("libraries") ==
            nlohmann::json::array({"icui18n", "icuuc"}));
      CheckExactPortableObjectKeys(portable.at("icu"), input.at("icu"),
                                   {"archiveSha256"});
      CheckExactObjectKeys(portable.at("icu").at("archiveSha256"),
                           {"icui18n", "icuuc"});
      CHECK(portable.at("icu").at("archiveSha256") ==
            lock.at("icu").at("archiveSha256"));
      CHECK(portable.at("icu").at("archiveSha256").at("icuuc") ==
            lock.at("icu").at("commonComposite").at("finalCompositeSha256"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "commonComposite"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "rawCommon"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "rawI18n"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "stubSource"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "stubObject"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "libicudata"));
      CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), "icudt78_dat"));
      CHECK(lock.at("harfbuzz").at("libraries") ==
            nlohmann::json::array({"harfbuzz"}));
      CHECK(portable.at("harfbuzz").at("libraries") ==
            nlohmann::json::array({"harfbuzz"}));
      CHECK(lock.at("harfbuzz").at("compositeSha256") ==
            portable.at("harfbuzz").at("compositeSha256"));
      CHECK(lock.at("harfbuzz").at("compositeSha256") ==
            RequiredSha256File(BuildPath(
                "text-dependencies/harfbuzz/lib/libharfbuzz.a")));
      const auto& hbComposite = lock.at("harfbuzz").at("icuComposite");
      CHECK(hbComposite.at("finalCompositeSha256") ==
            lock.at("harfbuzz").at("compositeSha256"));
      CHECK(hbComposite.at("rawCoreDefinedAdapterSymbols") == 0);
      CHECK(hbComposite.at("rawIcuAdapterDefinedAdapterSymbols") == 1);
      CHECK(hbComposite.at("compositeDefinedAdapterSymbols") == 1);
      CheckHarfBuzzCompositeProvenanceMatchesFiles(lock);
      CheckExactPortableObjectKeys(portable.at("harfbuzz"),
                                   input.at("harfbuzz"),
                                   {"compositeSha256"});
      CHECK_FALSE(JsonContainsSubstring(portable, "harfbuzz-icu"));
      CHECK_FALSE(JsonContainsSubstring(portable, "icuComposite"));
      CHECK_FALSE(JsonContainsSubstring(portable, "rawCore"));
      CHECK_FALSE(JsonContainsSubstring(portable, "rawAdapter"));
      CHECK_FALSE(JsonContainsSubstring(portable, "adapterObject"));
      CHECK(std::filesystem::path(lock.at("harfbuzz").at("sourcePath").get<std::string>()).is_absolute());
      CheckCanonicalPathEquals(lock.at("harfbuzz").at("sourcePath"),
                               std::filesystem::path(MOLGA_SOURCE_DIR) / "external/harfbuzz");
      CheckCanonicalPathEquals(lock.at("icu").at("sourcePath"),
                               std::filesystem::path(MOLGA_SOURCE_DIR) / "external/icu");
      CheckEveryIncludeAndArchiveUnderNestedPrefix(
          lock, std::filesystem::path(MOLGA_BINARY_DIR) / "text-dependencies");
      CheckEveryDependencyArchiveIsStatic(lock);
      CheckSharedDependencyFieldsEqual(lock, portable);
      CheckPortable(portable);
  }
  ```

- [x] **Step 1c: Add failing HarfBuzz and direct-ICU symbol probes.**

  ```cpp
  // tests/test_text_harfbuzz_link.cpp
  #include "doctest.h"
  #include <hb.h>
  #include <hb-icu.h>
  #include <string_view>
  TEST_CASE("pinned static HarfBuzz symbols are reachable") {
      CHECK(hb_version_atleast(14, 3, 1));
      CHECK(std::string_view(hb_version_string()).find("14.3.1") == 0);
      CHECK(hb_icu_get_unicode_funcs() != nullptr);
  }

  // tests/test_text_icu_link.cpp
  #include "doctest.h"
  #include <cstdint>
  #include <unicode/ubrk.h>
  #include <unicode/uversion.h>
  extern "C" const std::uint8_t icudt78_dat[];
  TEST_CASE("pinned static ICU symbols are reachable directly") {
      UVersionInfo version{};
      u_getVersion(version);
      CHECK(version[0] == 78);
      CHECK(version[1] == 3);
      auto* i18nSymbol = &ubrk_open;
      CHECK(i18nSymbol != nullptr);
      CHECK(icudt78_dat[2] == 0xda);
      CHECK(icudt78_dat[3] == 0x27);
  }
  ```

- [x] **Step 2: Run the dependency-record red gate.**

  Run: `cmake --preset debug && cmake --build --preset debug --target test_text_dependencies -j`

  Expected: FAIL because the imported targets and two generated record paths do not exist; configure must not find `/opt/homebrew`, `/usr/local`, or a framework substitute.

- [x] **Step 3: Define the ICU nested static build.** Before configure, require superproject gitlink OID == submodule `HEAD` == approved ICU commit and an empty `git -C external/icu status --porcelain=v1 --untracked-files=all`; a dirty tracked/untracked submodule fails rather than self-recording new bytes under a pinned HEAD. Configure vendored `external/icu/icu4c/source` into `${CMAKE_BINARY_DIR}/text-dependencies/icu-build` with exact flags `--disable-shared --enable-static --disable-tools --disable-tests --disable-samples --disable-extras --disable-icuio --disable-layoutex --prefix=${CMAKE_BINARY_DIR}/text-dependencies/icu-raw`; reject an unknown/ignored flag in configure output. Build only the exact common, i18n, and pinned stubdata targets, pass `ZERO_AR_DATE=1` to every archive-producing nested command, and bind stubdata to `external/icu/icu4c/source/stubdata/stubdata.cpp`. Invoke configure through `cmake -E env ZERO_AR_DATE=1 PKG_CONFIG=false ac_cv_prog_PYTHON=`. At this pinned revision configure otherwise probes host `pkg-config`/`icu-le-hb` even with layout disabled and spawns Python to generate rules for disabled data/tests. Require the resulting `config.log`, `config.status`, and generated makefiles to carry no resolved host `pkg-config`, `python`, or `icu-le-hb` executable/flags; require configured `PKG_CONFIG`, `PYTHON`, `ICULEHB_CFLAGS`, and `ICULEHB_LIBS` to be exactly empty; require no `Spawning Python` line; and require `data/rules.mk` plus `test/testdata/rules.mk` to contain exactly one terminal newline and no rule. A literal `PKG_CONFIG=false` value, a host executable path, or nonempty layout flags is a failure rather than an accepted disabled state. Do not build ICU's full data archive: external `icudt78l.dat` remains the only runtime data payload.

- [ ] **Step 3.1: Declare every raw ICU output to the build graph.** Give the
  nested ICU `ExternalProject_Add` exact `BUILD_BYPRODUCTS` entries for
  `${icu_build}/lib/libicuuc.a`, `${icu_build}/lib/libicui18n.a`,
  `${icu_build}/stubdata/libicudata.a`, and
  `${icu_build}/stubdata/stubdata.ao`. At
  superproject configure time, enumerate only the clean pinned
  `icu4c/source/{common,i18n}/unicode/*.h` regular files relative to
  `icu4c/source`, sort them bytewise, reject duplicate installed basenames or
  an escaping/symlink entry, require exactly `203` names, and require SHA-256
  `fe6c48d6b56a735a1434c0aabd47c4dbbb2a1f71df26c7fccc98cf74dec8947b`
  for their LF-joined relative-name manifest with one terminal LF. Transform
  every name to its exact `${icu_raw_prefix}/include/unicode/<basename>` path
  and list all 203 paths plus installed raw `libicuuc.a` and `libicui18n.a` in
  `INSTALL_BYPRODUCTS`. If one custom build command itself performs install and
  leaves `INSTALL_COMMAND` empty, list the same 205 installed paths in that
  command's `BUILD_BYPRODUCTS` instead and name the exception explicitly. If
  the implementation uses `add_custom_command`, list archives as `OUTPUT` and
  installed headers as `BYPRODUCTS`; no consumed archive/header may be an
  undeclared side effect.

- [x] **Step 3.1a: Make the ICU raw install a Make-safe repair boundary.**
  `BUILD_BYPRODUCTS`/`INSTALL_BYPRODUCTS` supply Ninja's file rules but a
  missing byproduct does not uniformly rerun an ExternalProject build/install
  with Make. Therefore make `molga_text_icu_raw_install` an always-checked
  target after the nested install order dependency whose sole command invokes
  `cmake/RepairIcuRawInstall.cmake` with exact canonical ICU source/build/raw
  prefix paths, configured absolute Make program, expected 203-header manifest,
  and closed `MODE=RAW_INSTALL_WRAPPER`; reject missing/unknown arguments or a
  path escape. The script first requires the four Step 3.1 build-tree outputs;
  if any is missing, rerun the exact pinned nested Make targets for
  `lib/libicuuc.a`, `lib/libicui18n.a`, and `stubdata/libicudata.a` under
  `ZERO_AR_DATE=1` using the configured absolute Make program, then require all
  four outputs including `stubdata/stubdata.ao`. Next compare the two installed raw
  archives byte-for-byte with their build-tree authorities and compare all 203
  installed headers byte-for-byte with their corresponding files under the
  clean pinned source. A missing/mismatch reruns the exact nested install under
  `ZERO_AR_DATE=1 PKG_CONFIG=false ac_cv_prog_PYTHON=`, then the wrapper
  revalidates the complete 205-file consumed set. It never baselines an
  installed copy, and the installed stubdata archive/config/pkg-config files
  remain unconsumed incidental outputs rather than repair authorities.

- [ ] **Step 3.1b: Prove ICU install repair with the actual Make generator.**
  Configure a dedicated temporary build tree with CMake 3.27 and exact
  generator `Unix Makefiles` (plus the approved preset's toolchain/cache
  values), assert its `CMAKE_GENERATOR`, then build
  `molga_text_icu_raw_install`. Delete each validated build-tree output
  `lib/libicuuc.a`, `lib/libicui18n.a`, `stubdata/libicudata.a`, and
  `stubdata/stubdata.ao` one at a time, rebuild only the wrapper, and require it
  to recreate the complete four-file build set before comparison/install.
  Then delete the validated installed `include/unicode/utypes.h`, installed raw
  `libicuuc.a`, and installed raw `libicui18n.a` one at a time; rebuild only the
  wrapper and require each to match its pinned source/build-tree authority.
  Resolve and component-check every deletion below that temporary tree first;
  no source/workspace path is removed.

  Run: `cmake --build --preset debug --target molga_text_icu_raw_install molga_text_icu_stubdata -j`

  Expected: raw `libicuuc.a`, `libicui18n.a`, pinned `stubdata.cpp`, and exactly one stubdata object/archive exist below the nested build; no system ICU or full `libicudata.a` is selected as a public dependency.

- [x] **Step 3a: Add the deterministic composite-archive script.** `MergeIcuStubdata.cmake` requires `RAW_ICUUC`, `STUBDATA_SOURCE`, `STUBDATA_ARCHIVE`, `OUTPUT_ICUUC`, `CMAKE_AR_TOOL`, and `CMAKE_RANLIB_TOOL`; canonicalize each input and reject paths outside the nested source/build prefixes. Enumerate the stub archive, filter only `__.SYMDEF`, `__.SYMDEF SORTED`, GNU `/`, and GNU `//`, record the filtered table member, require exactly one remaining regular object, and extract that exact named object into a unique child rather than blanket-running `ar -x`. Reject every unknown non-object and duplicate/colliding regular member before publication.

- [x] **Step 3b: Merge the stub object into logical `icuuc`.** Copy raw `libicuuc.a` to a same-directory unique staged output, then append the one stub object after all raw common members. For GNU/LLVM ar invoke `cmake -E env ZERO_AR_DATE=1 <ar> qcsD <stage> <object>`; for Apple ar (whose usage probe has no `D`) invoke `cmake -E env ZERO_AR_DATE=1 <ar> qcs <stage> <object>`. Invoke `<ranlib> -D <stage>` in both cases, fail any unknown archiver family, and atomically rename the staged archive to `${CMAKE_BINARY_DIR}/text-dependencies/icu/lib/libicuuc.a`. Never modify the nested raw archive in place.

- [x] **Step 3c: Verify the composite before exposing it.** Hash `stubdata.cpp`, the extracted object, raw common archive, and composite common archive; enumerate composite members twice and require byte-identical order. Run `${CMAKE_NM} -g` on the composite and require exactly one definition line matching ` [DRS] _?icudt78_dat$`; ignore—but record—the raw common archive's expected `U` reference, and reject duplicate definitions, a version-mismatched name, or no definition.

- [x] **Step 3d: Add a target-independent raw-archive red probe.** Do not use the imported targets or `test_text_icu_link`, which are intentionally registered only in Step 11b. Add `tests/probes/icu_stub_link.cpp` as a minimal `main` that directly reads `extern "C" const std::uint8_t icudt78_dat[]`. Add `cmake/ProbeIcuStubdataLink.cmake`, which requires explicit compiler/source/archive/output/expected-success arguments, invokes the C++ compiler driver, and validates both status and captured output. Define `molga_text_icu_raw_link_probe` alongside the nested-build targets; it depends only on `molga_text_icu_raw_install` plus the probe source/script and passes the raw common archive with `EXPECT_SUCCESS=OFF`. The wrapper target succeeds only when the nested link itself fails and its diagnostic names `icudt78_dat`; an unexpected link success or unrelated failure is fatal.

  Run: `cmake --build --preset debug --target molga_text_icu_raw_link_probe -j`

  Expected red: the wrapper records the expected nested link failure with undefined `icudt78_dat`; no imported dependency/test target is needed yet.

- [x] **Step 3e: Build and verify the composite common archive.** Make `molga_text_icu_composite` depend on raw ICU plus stubdata and run `MergeIcuStubdata.cmake`; `molga_text_icu_install` then depends on the composite and installed i18n archive. Define `molga_text_icu_composite_link_probe` with the same minimal source/script and `EXPECT_SUCCESS=ON`, depending on the composite rather than any imported target.

  Declare `${CMAKE_BINARY_DIR}/text-dependencies/icu/lib/libicuuc.a` as the
  composite command's exact `OUTPUT` and make the target depend on that file;
  declare the installed/final `libicui18n.a` as an explicit copied output (or
  verified byproduct of its install target). A missing final archive therefore
  has a producer rule even when the generated JSON records still exist.

  Run: `cmake --build --preset debug --target molga_text_icu_composite_link_probe -j && nm -g build/debug/text-dependencies/icu/lib/libicuuc.a | rg ' [DRS] _?icudt78_dat$'`

  Expected green: build PASS and exactly one symbol line; a second clean rebuild produces the same composite SHA-256 and member listing.

- [x] **Step 3f: Reject dirty HarfBuzz source authority.** Before configure,
  require superproject gitlink OID == `external/harfbuzz` `HEAD` == the approved
  commit and require `git -C external/harfbuzz status --porcelain=v1
  --untracked-files=all` to be empty. A tracked or untracked submodule change
  fails before compilation rather than recording different bytes under a
  pinned HEAD.

- [x] **Step 4: Define the HarfBuzz nested static build.** Configure vendored `external/harfbuzz` only after `molga_text_icu_composite`, use exact binary root `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build`, install beneath `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-raw`, and pass this pinned cache matrix: `BUILD_SHARED_LIBS=OFF`, `BUILD_FRAMEWORK=OFF`, `HB_HAVE_ICU=ON`, `HB_HAVE_CORETEXT=OFF`, `HB_HAVE_CAIRO=OFF`, `HB_HAVE_FREETYPE=OFF`, `HB_HAVE_GRAPHITE2=OFF`, `HB_HAVE_GLIB=OFF`, `HB_HAVE_GOBJECT=OFF`, `HB_HAVE_INTROSPECTION=OFF`, `HB_BUILD_UTILS=OFF`, `HB_BUILD_SUBSET=OFF`, `HB_BUILD_RASTER=OFF`, `HB_BUILD_VECTOR=OFF`, `HB_BUILD_GPU=OFF`, and string `HB_BUILD_GPU_DEMO=OFF`; require every entry to retain that exact type/value in the nested cache. At the pinned CMake commit, `HB_HAVE_ICU=ON` deliberately builds core `harfbuzz` from `harfbuzz.cc` and a separate `harfbuzz-icu` from `hb-icu.cc`; do not assume the ICU adapter is inside raw `libharfbuzz.a`. Pin `ICU_INCLUDE_DIR` to the nested ICU install; pin both `ICU_UC_LIBRARY_RELEASE` and `ICU_UC_LIBRARY_DEBUG` to logical composite `libicuuc.a`. Do not rely on singular `ICU_UC_LIBRARY`, which FindICU may overwrite during configuration selection. After configure, verify the exact nested `CMakeCache.txt` include/release/debug selections, verify the raw `hb-icu.cc` compile command carries the nested include root plus `U_STATIC_IMPLEMENTATION`, and verify the standalone driver probe uses the exact static common archive. Upstream install may also emit `lib/cmake/harfbuzz/harfbuzzConfig*.cmake` and `lib/pkgconfig/harfbuzz*.pc` inside the raw prefix, but no Molga step loads or parses them as an input, links through them, stages them, or packages them: they describe the raw upstream split and are intentionally outside the declared/repairable byproduct set. Raw static archive `link.txt` need only be free of dependency include/library/backend host paths because it contains configured host archiver commands. Verify top-level `molga_text_icui18n` separately against nested `libicui18n.a`; pinned HarfBuzz requests only ICU `uc`. Add the nested ICU include root plus `U_STATIC_IMPLEMENTATION` to HarfBuzz C++ compilation because the pinned target does not propagate it to `harfbuzz-icu`. Set `CMAKE_DISABLE_FIND_PACKAGE_Python3=ON` because every Python-using optional branch is disabled, then reject host dependency/backend resolution while permitting and recording the configured host toolchain executables. Wrap every raw HarfBuzz configure/build/install archive-producing command in `cmake -E env ZERO_AR_DATE=1`, including upstream `ar` and `ranlib`; deterministic flags on the later merge alone are insufficient.

- [ ] **Step 4a: Declare and repair every raw HarfBuzz output.** Give the nested ExternalProject exact `BUILD_BYPRODUCTS` entries `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build/libharfbuzz.a` and `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build/libharfbuzz-icu.a`. Do not misdeclare a configure output as a build-command byproduct. Instead add an explicit `ExternalProject_Add_Step` named `molga_harfbuzz_generated_header` with `DEPENDEES configure`, `DEPENDERS build`, `DEPENDS` on the clean pinned `src/hb-features.h.in` plus `cmake/RepairHarfBuzzRawInstall.cmake`, and exact `BYPRODUCTS ${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-build/src/hb-features.h`. Its command invokes that script in exact `MODE=GENERATED_HEADER_PREBUILD` with the canonical source/binary/prefix/toolchain/nested-ICU paths and complete typed Step 4 cache matrix; this gives Ninja a real pre-build producer while the later always-checked wrapper covers Make. Give the ExternalProject exact `INSTALL_BYPRODUCTS` entries `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-raw/lib/libharfbuzz.a`, `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz-raw/lib/libharfbuzz-icu.a`, and these pinned installed headers beneath `include/harfbuzz`: `hb-aat-layout.h`, `hb-aat.h`, `hb-blob.h`, `hb-buffer.h`, `hb-common.h`, `hb-cplusplus.hh`, `hb-deprecated.h`, `hb-draw.h`, `hb-face.h`, `hb-features.h`, `hb-font.h`, `hb-icu.h`, `hb-map.h`, `hb-ot-color.h`, `hb-ot-deprecated.h`, `hb-ot-fetch.h`, `hb-ot-font.h`, `hb-ot-layout.h`, `hb-ot-math.h`, `hb-ot-meta.h`, `hb-ot-metrics.h`, `hb-ot-name.h`, `hb-ot-shape.h`, `hb-ot-var.h`, `hb-ot.h`, `hb-paint.h`, `hb-script-list.h`, `hb-set.h`, `hb-shape-plan.h`, `hb-shape.h`, `hb-style.h`, `hb-unicode.h`, `hb-version.h`, and `hb.h`. Verify this exact allowlist after install and reject a missing, extra, duplicate, or escaping public header.

  Because Make does not uniformly rerun an ExternalProject configure/build/install step when only a byproduct is deleted, make `molga_text_harfbuzz_raw_install` an always-checked target whose sole command invokes the same script in exact `MODE=RAW_INSTALL_WRAPPER`. Both modes reject missing/unknown arguments and path escapes. The script first reasserts the complete typed Step 4 cache matrix and requires build-tree `src/hb-features.h` SHA-256 `b9f5b0184edfab48fa3953f3b5fe8f72f72db0c08ebf6ccfc2541451c8bbc597`, the exact output of pinned `src/hb-features.h.in` under that macOS matrix. A missing/wrong generated header or cache mismatch makes it rerun the exact pinned HarfBuzz configure command under `ZERO_AR_DATE=1` with the same canonical source root, binary root, install prefix, compiler/toolchain, nested ICU include/common paths, Python disable, and every typed Step 4 cache argument, then reassert the complete cache plus generated-header SHA. `GENERATED_HEADER_PREBUILD` returns here because its declared build step follows; `RAW_INSTALL_WRAPPER` immediately invokes the exact nested `harfbuzz` and `harfbuzz-icu` targets after such a repair, before trusting either raw archive. Neither mode derives expected bytes from an installed or build-tree copy. Wrapper mode next requires both build-tree archives and, if either is missing, reruns those same two nested targets under `ZERO_AR_DATE=1` before rechecking both. It then verifies all 36 consumed installed outputs: each installed raw archive against its build-tree archive, each of the 33 source headers against the corresponding file under the clean pinned source, and installed `hb-features.h` against the independently pinned build-tree generated header. On any installed missing/mismatch it reruns the exact nested install under `ZERO_AR_DATE=1`, then revalidates the full set against those authorities before succeeding. Bind every consumer/composite to this boundary rather than treating an archive/header as an incidental install side effect. Upstream's installed CMake export/pkg-config files are deliberately unconsumed incidental outputs, not members of this 36-file repair set. If the project instead uses one grouped custom install command with all installed paths as outputs, document and test that equivalent owner explicitly.

- [ ] **Step 4a.1: Prove generated-header repair with the actual Make generator.** In a dedicated temporary CMake 3.27 `Unix Makefiles` tree, build `molga_text_harfbuzz_raw_install` and record the generated-header plus both raw-archive hashes. Delete only canonical build-tree `src/hb-features.h`, rebuild only the wrapper, require the log to show the exact pinned configure and both nested archive targets, and require the header SHA above, both installed-header copies, and both original archive hashes. Then change the same byte in both build-tree and installed `hb-features.h`, rebuild only the wrapper, and require the independent SHA check to reject that false agreement, regenerate both copies, reassert the complete cache, and retain the original archive hashes. Resolve and component-check every deletion/mutation below that temporary tree first; never modify the source template, workspace, or another build root.

  Run: `cmake --build --preset debug --target molga_text_harfbuzz_raw_install -j`

  Expected: both raw archives and all 34 headers exist below the nested prefix, and no dependency/include/library/optional-backend cache value or compile/link argument resolves Homebrew, `/usr/local`, CoreText, FreeType, or system ICU. The configured CMake/compiler/SDK/ar/ranlib/nm executable paths are host toolchain provenance, not dependency-resolution failures; the offline gate forbids network/acquisition during this build rather than requiring those tools below the nested prefix. Delete one transitive header such as `hb-blob.h`, rebuild this target under the preset's actual generator, and require the boundary to restore it before any consumer compiles.

- [x] **Step 4b: Prove the upstream split before merging.** Run `${CMAKE_NM} -g` on both raw archives. Require zero defined lines matching ` [TDS] _?hb_icu_get_unicode_funcs$` in raw core and exactly one in raw `harfbuzz-icu`. When enumerating archive members, filter only the known archive-table pseudo-members `__.SYMDEF`, `__.SYMDEF SORTED`, GNU `/`, and GNU `//`; require exactly one remaining regular adapter object and reject a remaining member-name collision with raw core. Record every filtered pseudo-member and fail on any other non-object entry. This check is tied to the pinned upstream commit and fails closed if its archive layout changes.

- [x] **Step 4c: Add a target-independent raw HarfBuzz adapter probe.** Create `tests/probes/harfbuzz_icu_link.cpp` as a minimal `main` that includes `hb-icu.h` and returns failure when `hb_icu_get_unicode_funcs()` is null. `ProbeHarfBuzzIcuLink.cmake` requires explicit C++ compiler, both HarfBuzz and ICU include roots, ordered archives, output, expected success, and expected diagnostic symbol; it adds `U_STATIC_IMPLEMENTATION`, invokes the C++ compiler driver, and validates status/output. Define `molga_text_harfbuzz_raw_link_probe` against only raw `libharfbuzz.a` plus ordered ICU archives with `EXPECT_SUCCESS=OFF`; the wrapper succeeds only when the nested link fails for `hb_icu_get_unicode_funcs`.

- [x] **Step 4d: Build one deterministic logical HarfBuzz archive.** `MergeHarfBuzzIcu.cmake` requires raw core, raw adapter, output path, configured ar/ranlib/nm, and canonical allowed roots. Enumerate members, filter only the Step 4b archive-table pseudo-members, and extract the single verified adapter object by its exact name into a unique build child; never use blanket `ar -x`. Reject symlinks/duplicate or colliding remaining member names, copy raw core to a same-directory staged output, append the adapter object after the raw members with the same probed GNU/LLVM-versus-Apple deterministic ar flags and `ZERO_AR_DATE=1` used by the ICU composite, run deterministic ranlib, then atomically publish `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz/lib/libharfbuzz.a`. Never modify either upstream raw archive in place.

- [ ] **Step 4e: Verify and own the HarfBuzz composite output.** Declare the final composite archive as the exact `OUTPUT` of its custom command and make `molga_text_harfbuzz_composite` depend on it. Hash raw core, raw adapter, extracted adapter object, filtered ordered member-name list, and final composite; enumerate twice and require byte-identical order. Require exactly one composite definition of `hb_icu_get_unicode_funcs` and no duplicate regular members. In the same canonical build tree, save the raw-core/raw-adapter/final hashes and filtered listings, delete the two build-tree raw archives, their two installed raw copies, and the final composite while leaving the already-built `.o` files, then build only the always-checked raw-install boundary followed by the composite producer. Require the boundary log to show that it invoked the exact nested `harfbuzz` and `harfbuzz-icu` targets under `ZERO_AR_DATE=1`, and require identical raw-core, raw-adapter, adapter-object, filtered-member, and final-composite values. Reinstalling unchanged build-tree archives is not a determinism test; this sequence must re-execute upstream `ar`/`ranlib`. Cross-root Debug archive identity is not claimed because compiler debug strings can embed the build root.

- [x] **Step 4f: Link the composite without an imported target.** Define `molga_text_harfbuzz_composite_link_probe` with the same minimal source/script, ordered final composite then `libicui18n.a` then composite `libicuuc.a`, and `EXPECT_SUCCESS=ON`. `molga_text_harfbuzz_install` depends on this probe plus the raw installed headers; no public CMake target exists yet.

  Run: `cmake --build --preset debug --target molga_text_harfbuzz_raw_link_probe molga_text_harfbuzz_composite_link_probe -j && nm -g build/debug/text-dependencies/harfbuzz/lib/libharfbuzz.a | rg ' [TDS] _?hb_icu_get_unicode_funcs$'`

  Expected: the raw wrapper records the exact missing-adapter failure, the composite link passes, and the final command emits exactly one definition line.

- [x] **Step 5: Expose the four CMake dependency targets.** Add the four declarations below and encode only the dependency order `ICU install -> HarfBuzz configure/install -> verification barrier`:

  ```cmake
  # ICU composite/install -> HarfBuzz raw install/composite -> verification barrier.
  # Every find_path/find_library uses NO_DEFAULT_PATH and the nested prefix only.
  add_library(molga_text_icuuc STATIC IMPORTED GLOBAL)
  add_library(molga_text_icui18n STATIC IMPORTED GLOBAL)
  add_library(molga_text_harfbuzz STATIC IMPORTED GLOBAL)
  add_library(molga_text_rasterizer INTERFACE)
  ```

  Include this module once in the existing external-dependency section, where it defines dependency targets/functions but never references `molga_core`.

- [x] **Step 5a: Bind deterministic not-yet-built import paths.** Create only the nested install include directories needed for CMake's generate-time existence check. Bind `molga_text_icuuc` exclusively to `${CMAKE_BINARY_DIR}/text-dependencies/icu/lib/libicuuc.a` (the composite, never `icu-raw` or stubdata), bind `molga_text_harfbuzz` exclusively to `${CMAKE_BINARY_DIR}/text-dependencies/harfbuzz/lib/libharfbuzz.a` (the composite, never either `harfbuzz-raw` archive), and use the raw install's verified header directory as HarfBuzz's include root. Set every other imported target property to its exact anticipated path below `${CMAKE_BINARY_DIR}/text-dependencies`; do not call `find_path`, `find_library`, `file(REAL_PATH)`, or hash an archive during configure. Any build-time lookup after install uses `NO_DEFAULT_PATH` and that one nested prefix. Add target dependencies from each imported target to the producer that declares its `IMPORTED_LOCATION`, so both target ordering and file-level link edges are known on a clean Ninja/Make build.

- [x] **Step 5b: Encode static transitive archive order.** Add the exact properties below. Therefore a normal consumer's link line is HarfBuzz first, then ICU i18n, then ICU common; the repeated common dependency CMake may emit after the i18n transitive is intentional for one-pass static archive resolution, and no consumer lists ICU before the archive that needs it.

  ```cmake
  set_property(TARGET molga_text_icui18n PROPERTY
    INTERFACE_LINK_LIBRARIES "molga_text_icuuc")
  set_property(TARGET molga_text_harfbuzz PROPERTY
    INTERFACE_LINK_LIBRARIES "molga_text_icui18n;molga_text_icuuc")
  ```

- [x] **Step 5c: Add the idempotent verification-barrier helper.** Append this exact helper after the imported targets. It is the only helper that adds the build-order edge, so composing normal/direct/portable helpers cannot duplicate it:

  ```cmake
  function(molga_attach_text_verification_barrier target_name)
    if(NOT TARGET "${target_name}")
      message(FATAL_ERROR "text verification consumer target does not exist: ${target_name}")
    endif()
    get_property(text_barrier_attached TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_VERIFICATION_BARRIER_ATTACHED)
    if(text_barrier_attached)
      return()
    endif()
    add_dependencies("${target_name}" molga_text_dependencies_ready)
    set_property(TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_VERIFICATION_BARRIER_ATTACHED TRUE)
  endfunction()
  ```

- [x] **Step 5c.1: Add the portable-contract product helper.** This helper
  exposes only the portable path to a C++ target that actually reads it; CMake
  staging functions continue to use the CMake variable directly. It never
  exposes the machine-local build lock or source/binary roots:

  ```cmake
  function(molga_attach_text_portable_contract_consumer target_name)
    if(NOT TARGET "${target_name}")
      message(FATAL_ERROR "portable text contract target does not exist: ${target_name}")
    endif()
    get_property(portable_contract_attached TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_PORTABLE_CONTRACT_ATTACHED)
    if(portable_contract_attached)
      return()
    endif()
    target_compile_definitions("${target_name}" PRIVATE
      MOLGA_TEXT_DEPENDENCY_CONTRACT="${MOLGA_TEXT_DEPENDENCY_CONTRACT}")
    molga_attach_text_verification_barrier("${target_name}")
    set_property(TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_PORTABLE_CONTRACT_ATTACHED TRUE)
  endfunction()
  ```

- [x] **Step 5c.2: Add the provenance-test-only helper.** Reject every target
  except `test_text_dependencies`; this is the sole compile-definition path for
  the machine-local lock, checkout/build roots, configured CMake executable,
  and verifier script:

  ```cmake
  function(molga_attach_text_provenance_test target_name)
    if(NOT target_name STREQUAL "test_text_dependencies" OR
       NOT TARGET "${target_name}")
      message(FATAL_ERROR
        "text provenance macros are test_text_dependencies-only: ${target_name}")
    endif()
    get_property(provenance_test_attached TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_PROVENANCE_TEST_ATTACHED)
    if(provenance_test_attached)
      return()
    endif()
    molga_attach_text_portable_contract_consumer("${target_name}")
    target_compile_definitions("${target_name}" PRIVATE
      MOLGA_TEXT_DEPENDENCY_BUILD_LOCK="${MOLGA_TEXT_DEPENDENCY_BUILD_LOCK}"
      MOLGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
      MOLGA_BINARY_DIR="${CMAKE_BINARY_DIR}"
      MOLGA_CMAKE_COMMAND="${CMAKE_COMMAND}"
      MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT="${CMAKE_SOURCE_DIR}/cmake/VerifyTextDependencies.cmake")
    set_property(TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_PROVENANCE_TEST_ATTACHED TRUE)
  endfunction()
  ```

- [x] **Step 5d: Add the idempotent normal text-dependency consumer helper.** Append this exact helper; HarfBuzz supplies its ICU transitives in the archive-safe order from Step 5b. `MOLGA_TEXT_DEPENDENCIES_ATTACHED` is the single target-local attachment authority: a second normal attachment is a strict no-op and may not append another library, compile definition, or barrier dependency.

  ```cmake
  function(molga_attach_text_dependencies target_name)
    if(NOT TARGET "${target_name}")
      message(FATAL_ERROR "text dependency consumer target does not exist: ${target_name}")
    endif()
    get_property(text_dependencies_attached TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_DEPENDENCIES_ATTACHED)
    if(text_dependencies_attached)
      return()
    endif()
    target_link_libraries("${target_name}" PRIVATE
      molga_text_harfbuzz molga_text_rasterizer)
    target_compile_definitions("${target_name}" PRIVATE U_STATIC_IMPLEMENTATION)
    molga_attach_text_verification_barrier("${target_name}")
    set_property(TARGET "${target_name}"
      PROPERTY MOLGA_TEXT_DEPENDENCIES_ATTACHED TRUE)
  endfunction()
  ```

- [x] **Step 5e: Add the direct-ICU test/tool helper.** Append this exact helper; it links ICU i18n before common, adds the static consumer definition/barrier, and does not attach HarfBuzz or the rasterizer:

  ```cmake
  function(molga_attach_direct_icu_consumer target_name)
    if(NOT TARGET "${target_name}")
      message(FATAL_ERROR "direct ICU consumer target does not exist: ${target_name}")
    endif()
    target_link_libraries("${target_name}" PRIVATE
      molga_text_icui18n molga_text_icuuc)
    target_compile_definitions("${target_name}" PRIVATE U_STATIC_IMPLEMENTATION)
    molga_attach_text_verification_barrier("${target_name}")
  endfunction()
  ```

- [x] **Step 5f: Recheck immutable submodule authority at the barrier.** In
  `VerifyTextDependencies.cmake`, immediately before provenance hashing and
  again before JSON publication, repeat for ICU and HarfBuzz the exact superproject
  gitlink == submodule `HEAD` == approved commit comparison and require empty
  `git status --porcelain=v1 --untracked-files=all`. A checkout that changed
  after nested configure fails before either generated JSON is staged.

- [x] **Step 6: Implement canonical provenance validation.** `VerifyTextDependencies.cmake` resolves source/include/archive paths with `file(REAL_PATH)`, verifies both git commits, requires each recorded source root to equal canonical `external/icu` or `external/harfbuzz`, and rejects an individual source outside its matching root or any output outside `${CMAKE_BINARY_DIR}/text-dependencies` before writing either JSON. Re-run both exact final-composite probes: one and only one `icudt78_dat` definition in logical `icuuc`, and one and only one `hb_icu_get_unicode_funcs` definition in logical `harfbuzz`. Revalidate raw-core zero/raw-adapter one HarfBuzz definitions and fail before publication if any result differs from Steps 3c or 4b–4f.

  Add the closed `HARFBUZZ_READ_ONLY` entrypoint from Step 1a. It requires all and only `MODE`, `BUILD_LOCK`, `SOURCE_ROOT`, `BINARY_ROOT`, `RESULT_ROOT`, and `RESULT_FILE`; requires canonical source/binary roots equal the caller arguments; requires `RESULT_ROOT` to be an existing caller-owned directory and `RESULT_FILE` to be its direct nonexisting child; rejects either production JSON destination and any publication/journal/injection argument; and writes only the exact schema-1 result object after every recomputation succeeds. It returns nonzero and leaves no result on an argument, containment, hash, member, tool, flag, or symbol failure. It never calls `text_publish_artifact_set`, never replaces a dependency archive or generated dependency record, and removes only its exact extraction child before returning.

  Of the composite-specific HarfBuzz fields, both JSON files share only `libraries:["harfbuzz"]` and final `compositeSha256`. The build lock additionally carries `harfbuzz.icuComposite.{rawCorePath,rawAdapterPath,finalCompositePath,rawCoreSha256,rawAdapterSha256,adapterObjectSha256,finalCompositeSha256,filteredMemberDigest,archiverFamily,arPath,ranlibPath,nmPath,arAppendFlags,ranlibFlags,zeroArDate,rawCoreDefinedAdapterSymbols:0,rawIcuAdapterDefinedAdapterSymbols:1,compositeDefinedAdapterSymbols:1}`. `archiverFamily` is exactly `gnu`, `llvm`, or `apple`; `arAppendFlags` is respectively `qcsD` for GNU/LLVM or `qcs` for Apple; `ranlibFlags` is `-D`; and `zeroArDate` is string `"1"`. Every path is canonical and nested where applicable, the verifier uses the recorded configured tools to recompute every archive-derived value, nested final SHA must equal top-level SHA and the actual archive, and no recorded installed CMake/pkg-config metadata is accepted as provenance. The portable record contains neither `icuComposite`, a `harfbuzz-icu` substring, nor any raw-core/raw-adapter/adapter-object field.

- [x] **Step 7: Stage the machine-local build lock.** Hash actual static archives and write schema, source/input values, full option matrix, compiler/architecture, canonical absolute source/include/archive paths, logical archive names, and archive SHA values to a unique staged file; do not replace the generated destination yet. Set exact `icu.archiveSha256` keys to `{icui18n,icuuc}`, with `icui18n` equal the installed/final i18n archive and `icuuc` equal the logical common composite. Under `icu.commonComposite`, record raw common SHA, pinned stub source SHA, extracted stub object SHA, final composite SHA, configured ar/ranlib paths and deterministic flags, ordered member-name digest, and the exact successful `icudt78_dat` symbol-probe result; require its final SHA equal `icu.archiveSha256.icuuc`. Under `harfbuzz.icuComposite`, record raw core SHA, raw adapter SHA, extracted adapter-object SHA, final composite SHA, the same configured tool/flag identities, ordered member-name digest, raw-zero/adapter-one/final-one symbol results, and the successful target-independent composite link probe.

- [x] **Step 8: Stage the portable dependency contract.** Build it from an explicit allowlist of shared fields plus repo-relative source identities; recursively reject absolute paths and source/build prefixes. The portable `harfbuzz` object has exactly the keys in the committed schema-1 input `harfbuzz` object plus generated `compositeSha256`; reject every missing/extra key and every key or string-value substring `harfbuzz-icu`, `icuComposite`, `rawCore`, `rawAdapter`, or `adapterObject`. The portable `icu` object has exactly the committed input `icu` keys plus generated `archiveSha256`; its `libraries` is exactly `["icui18n","icuuc"]`, and `archiveSha256` has exactly those two keys and values copied from the verified build lock. Require `archiveSha256.icuuc == commonComposite.finalCompositeSha256`, but keep `commonComposite`, raw common/i18n, stub source/object, `libicudata`, and `icudt78_dat` names/fields build-lock-only. Represent HarfBuzz with exactly `libraries:["harfbuzz"]` plus top-level `compositeSha256`; raw core/adapter/object hashes, member/tool provenance, and `icuComposite` remain build-lock-only. Do not derive the contract by deleting path-looking fields from the build lock, and do not replace the generated destination yet.

- [x] **Step 9: Publish the mutually consistent JSON pair.** Compare every shared staged field, then call `text_publish_artifact_set` once with the two generated destinations and a build-directory journal. A mismatch fails before either destination is touched.

- [x] **Step 9a: Test build-record rollback at both crash windows.** Add before-rename `2` and after-rename `-2` injection cases to `test_text_dependencies`; each records the previous pair, invokes the real publisher, and proves both previous JSON bytes are restored with no journal/new/backup sibling left.

- [x] **Step 9b: Test clean publication after each injected failure.** In each case rerun the same publisher with injection disabled and assert both generated JSON documents contain the new mutually consistent shared fields.

- [x] **Step 10: Add the verification barrier.** `molga_text_dependencies_ready` depends on the ICU install/composite and HarfBuzz raw install/composite/link probe, runs the verifier, and declares both JSON paths as `BYPRODUCTS`. The composed attach helpers add this barrier exactly once and only after their named consumer target exists. A product C++ target that actually reads the portable contract calls `molga_attach_text_portable_contract_consumer` immediately after creation; CMake-only staging uses `${MOLGA_TEXT_DEPENDENCY_CONTRACT}` without adding a compile definition. No product/normal/direct consumer receives the build-lock, source/binary-root, configured-CMake, or verifier-script macro.

- [x] **Step 10a: Add the missing-byproduct rebuild regression.** Configure a
  fresh build tree, build both standalone link probes, then remove one explicit
  build-tree archive at a time (final composite `libharfbuzz.a`, final `libicui18n.a`, composite
  `libicuuc.a`) while leaving both generated JSON records in place. Rebuild the
  affected probe and require the owning producer to recreate and reverify the
  archive before linking. The test uses only its dedicated temporary build
  tree, validates each resolved deletion path is below that tree, and never
  removes source or workspace artifacts.

- [x] **Step 11: Attach the core only after its target exists.** Immediately after root `add_library(molga_core STATIC ${ENGINE_SOURCES})`, call `molga_attach_text_dependencies(molga_core)`; do not place that call in the earlier dependency section.

- [x] **Step 11a: Attach the dependency-record test after creation.** Replace Task 1.1's temporary direct `MOLGA_SOURCE_DIR`/`MOLGA_BINARY_DIR` compile definitions: immediately after `molga_add_test(test_text_dependencies test_text_dependencies.cpp)`, call `molga_attach_text_provenance_test(test_text_dependencies)` so this helper is the final sole definition path for the portable/build records, source/binary roots, configured CMake executable, and verifier script. Require `MOLGA_TEXT_PROVENANCE_TEST_ATTACHED=TRUE`; a second helper call is a strict no-op, so each of these six test-private compile definitions occurs exactly once.

- [x] **Step 11b: Register and attach standalone static symbol probes.** Do not use `molga_add_test`, because its implicit `molga_core` link would mask a broken direct dependency helper. Add these exact standalone targets after `doctest_main` and `molga_warnings` exist; each attach call occurs only after its target exists:

  ```cmake
  add_executable(test_text_harfbuzz_link test_text_harfbuzz_link.cpp)
  target_link_libraries(test_text_harfbuzz_link PRIVATE doctest_main molga_warnings)
  molga_attach_text_dependencies(test_text_harfbuzz_link)
  set_target_properties(test_text_harfbuzz_link PROPERTIES ENABLE_EXPORTS ON)
  add_test(NAME test_text_harfbuzz_link COMMAND test_text_harfbuzz_link)

  add_executable(test_text_icu_link test_text_icu_link.cpp)
  target_link_libraries(test_text_icu_link PRIVATE doctest_main molga_warnings)
  molga_attach_direct_icu_consumer(test_text_icu_link)
  set_target_properties(test_text_icu_link PROPERTIES ENABLE_EXPORTS ON)
  add_test(NAME test_text_icu_link COMMAND test_text_icu_link)
  set_tests_properties(test_text_harfbuzz_link test_text_icu_link
                       PROPERTIES LABELS "unit")
  ```

- [x] **Step 11c: Attach each consumer after creation and audit macro scope.**
  `molga_attach_text_provenance_test(test_text_dependencies)` is the only call
  that supplies `MOLGA_TEXT_DEPENDENCY_BUILD_LOCK`, `MOLGA_SOURCE_DIR`,
  `MOLGA_BINARY_DIR`, `MOLGA_CMAKE_COMMAND`, or
  `MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT`. Attach
  `test_text_runtime_dependencies` through
  `molga_attach_text_portable_contract_consumer` because it launches children
  and calls no ICU/HarfBuzz API. Task 2.2 attaches normal text dependencies to
  `molga_text_runtime_probe`, and Task 3.3 attaches the direct ICU helper to
  ready `test_unicode_text` only after each target exists. Every later direct
  ICU/HarfBuzz source follows its own target creation. At configure time inspect
  `COMPILE_DEFINITIONS`: require all six test-private macros—the portable path
  plus the five just named—exactly once on `test_text_dependencies`; require
  the five machine-local test-private macros to
  occur zero times on `molga_core`, normal/direct link probes, ready text tests,
  editor, runtime, and every package target; and require the portable macro only
  on an explicitly registered portable-contract C++ consumer.

- [x] **Step 11d: Isolate the rasterizer include seam.** Give `molga_text_rasterizer` the interface include root `${CMAKE_SOURCE_DIR}/external` and change only `FontFace.cpp` to `#include "text/rasterizer/imstb_truetype.h"`; verify it no longer reaches the ImGui-private header path and that `U_STATIC_IMPLEMENTATION` is absent from the rasterizer target. Keep the existing `${IMGUI_DIR}` private include on `molga_core` because unrelated engine component sources still include public ImGui headers; this task does not migrate or relink those consumers.

- [x] **Step 12: Run the static/portable green gate.**

  ```bash
  cmake --preset debug
  cmake --build --preset debug --target test_text_dependencies \
    test_text_harfbuzz_link test_text_icu_link --clean-first -j
  ctest --test-dir build/debug \
    -R '^(test_text_dependencies|test_text_harfbuzz_link|test_text_icu_link)$' \
    --output-on-failure
  nm -g build/debug/text-dependencies/icu/lib/libicuuc.a | \
    rg -c ' [DRS] _?icudt78_dat$' | rg '^1$'
  nm -g build/debug/text-dependencies/harfbuzz/lib/libharfbuzz.a | \
    rg -c ' [TDS] _?hb_icu_get_unicode_funcs$' | rg '^1$'
  otool -L build/debug/tests/test_text_harfbuzz_link | \
    rg -i 'harfbuzz|icu|homebrew' && exit 1 || true
  otool -L build/debug/tests/test_text_icu_link | \
    rg -i 'harfbuzz|icu|homebrew' && exit 1 || true
  ```

  Expected: PASS from a clean link, exactly one defined stub-data symbol, exactly one defined HarfBuzz ICU-adapter symbol, the link order is composite HarfBuzz -> i18n -> composite common, the build lock contains both canonical composite provenances, and the portable contract contains no machine path, second HarfBuzz library, or third ICU archive.

- [x] **Step 13: Commit the build/record boundary.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt cmake/TextDependencies.cmake \
    cmake/MergeIcuStubdata.cmake cmake/MergeHarfBuzzIcu.cmake \
    cmake/RepairIcuRawInstall.cmake cmake/RepairHarfBuzzRawInstall.cmake \
    cmake/ProbeIcuStubdataLink.cmake cmake/ProbeHarfBuzzIcuLink.cmake \
    cmake/VerifyTextDependencies.cmake cmake/TextArtifactTransaction.cmake \
    tests/test_text_dependencies.cpp tests/test_text_harfbuzz_link.cpp \
    tests/test_text_icu_link.cpp tests/probes/icu_stub_link.cpp \
    tests/probes/harfbuzz_icu_link.cpp \
    src/Rendering/FontFace.cpp
  git commit -m "build: compile pinned text dependencies"
  ```

**Exit:** Offline normal builds resolve only pinned static archives; the machine-local lock and portable contract agree field-for-field without leaking local paths.

---

### Task 2.1: Add stable typed diagnostics and rate limiting

**Prerequisite:** Task 1.2 provides the build barrier and portable contract path.

**Files:**

- Create: `src/Text/TextDiagnostic.h`
- Create: `src/Text/TextDiagnostic.cpp`
- Create: `tests/test_text_runtime_dependencies.cpp`
- Modify: `src/Common/Log.h`
- Modify: `src/Common/Log.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: the existing human-readable logger.
- Produces: `TextSeverity`, `TextDiagnosticCode`, `StableTextDiagnosticCode`, `ParseStableTextDiagnosticCode`, `SourceByteRange`, `TextDiagnostic`, `TextDiagnosticSink`, `VectorTextDiagnosticSink`, and `LoggerTextDiagnosticSink` keyed by the complete diagnostic context.

- [ ] **Step 1: Add the failing stable-code round-trip test.**

  ```cpp
  TEST_CASE("text diagnostic codes and context keys are stable") {
      using namespace molga::text;
      const std::pair<TextDiagnosticCode, std::string_view> expected[] = {
          {TextDiagnosticCode::DependencyInvalid, "TEXT_DEPENDENCY_INVALID"},
          {TextDiagnosticCode::Utf8Invalid, "TEXT_UTF8_INVALID"},
          {TextDiagnosticCode::FontInvalid, "TEXT_FONT_INVALID"},
          {TextDiagnosticCode::FontFamilyInvalid, "TEXT_FONT_FAMILY_INVALID"},
          {TextDiagnosticCode::MissingGlyph, "TEXT_MISSING_GLYPH"},
          {TextDiagnosticCode::AtlasExhausted, "TEXT_ATLAS_EXHAUSTED"},
          {TextDiagnosticCode::LayoutInvalid, "UI_LAYOUT_INVALID"},
          {TextDiagnosticCode::LayoutCycle, "UI_LAYOUT_CYCLE"},
          {TextDiagnosticCode::ReferenceInvalid, "UI_REFERENCE_INVALID"},
          {TextDiagnosticCode::TextInputUnavailable, "TEXT_INPUT_UNAVAILABLE"},
          {TextDiagnosticCode::TextInputRangeClamped, "TEXT_INPUT_RANGE_CLAMPED"},
          {TextDiagnosticCode::ReflowDeferred, "UI_REFLOW_DEFERRED"},
          {TextDiagnosticCode::PackageValidationFailed, "PACKAGE_VALIDATION_FAILED"},
      };
      for (const auto& [code, stable] : expected) {
          CHECK(std::string_view(StableTextDiagnosticCode(code)) == stable);
          CHECK(ParseStableTextDiagnosticCode(stable) == code);
      }
      CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_UNKNOWN_FROM_FUTURE"));
      CHECK_FALSE(ParseStableTextDiagnosticCode("0"));
  }
  ```

- [ ] **Step 1a: Add the failing collecting-sink/context-key test.**

  ```cpp
  TEST_CASE("vector sink retains records and source range changes identity") {
      using namespace molga::text;
      VectorTextDiagnosticSink sink;
      TextDiagnostic first{TextDiagnosticCode::Utf8Invalid, TextSeverity::Error,
          "unicode", "invalid sequence", "replace authored bytes", "font-a", 7,
          "UILabel", {3, 4}};
      sink.Report(first);
      sink.Report(first);
      first.sourceByteRange = {4, 5};
      sink.Report(first);
      CHECK(sink.Diagnostics().size() == 3);
      CHECK(TextDiagnosticRateLimitKey(sink.Diagnostics()[0]) !=
            TextDiagnosticRateLimitKey(sink.Diagnostics()[2]));
  }
  ```

- [ ] **Step 1b: Add the failing bounded logger-rate test.**

  ```cpp
  TEST_CASE("logger sink suppresses only a remembered complete context key") {
      using namespace molga::text;
      TextDiagnostic first{TextDiagnosticCode::Utf8Invalid, TextSeverity::Error,
          "unicode", "invalid sequence", "replace authored bytes", "font-a", 7,
          "UILabel", {3, 4}};
      ScopedRingLogSink log;
      LoggerTextDiagnosticSink logger(/*maxRememberedKeys=*/256);
      logger.Report(first);
      logger.Report(first);
      first.sourceByteRange = {4, 5};
      logger.Report(first);
      CHECK(log.Messages().size() == 2);
  }
  ```

- [ ] **Step 1c: Add the failing logger-capacity test.**

  ```cpp
  TEST_CASE("logger sink evicts oldest keys and zero capacity never suppresses") {
      using namespace molga::text;
      const auto a = DiagnosticAtRange({1, 2});
      const auto b = DiagnosticAtRange({2, 3});
      {
          ScopedRingLogSink log;
          LoggerTextDiagnosticSink one(/*maxRememberedKeys=*/1);
          one.Report(a); one.Report(b); one.Report(a);
          CHECK(log.Messages().size() == 3);
      }
      {
          ScopedRingLogSink log;
          LoggerTextDiagnosticSink zero(/*maxRememberedKeys=*/0);
          zero.Report(a); zero.Report(a);
          CHECK(log.Messages().size() == 2);
      }
  }
  ```

- [ ] **Step 2: Run the diagnostic red gate.**

  Run: `cmake --build --preset debug --target test_text_runtime_dependencies -j`

  Expected: compile FAIL because `Text/TextDiagnostic.h` and the stable mapping do not exist.

- [ ] **Step 3: Add the stable severity/code declarations.** Add only these closed enums and forward/reverse function declarations:

  ```cpp
  enum class TextSeverity : std::uint8_t { Info, Warning, Error, Blocker };
  enum class TextDiagnosticCode : std::uint16_t {
      DependencyInvalid, Utf8Invalid, FontInvalid, FontFamilyInvalid,
      MissingGlyph, AtlasExhausted, LayoutInvalid, LayoutCycle,
      ReferenceInvalid, TextInputUnavailable, TextInputRangeClamped,
      ReflowDeferred, PackageValidationFailed,
  };
  const char* StableTextDiagnosticCode(TextDiagnosticCode) noexcept;
  std::optional<TextDiagnosticCode> ParseStableTextDiagnosticCode(
      std::string_view) noexcept;
  ```

- [ ] **Step 3a: Implement the closed stable-code mapping.** Map the enum exhaustively to the 13 exact strings in Step 1; an invalid enum returns `TEXT_DIAGNOSTIC_UNKNOWN` without throwing. The reverse parser accepts exactly those 13 strings and returns `nullopt` for numeric values, aliases, or unknown future codes.

- [ ] **Step 3b: Add the diagnostic value and base-sink records.** Append these exact declarations:

  ```cpp
  struct SourceByteRange {
      std::uint32_t begin = 0, end = 0;
      friend constexpr bool operator==(SourceByteRange a,
                                       SourceByteRange b) noexcept {
          return a.begin == b.begin && a.end == b.end;
      }
  };
  struct TextDiagnostic {
      TextDiagnosticCode code = TextDiagnosticCode::DependencyInvalid;
      TextSeverity severity = TextSeverity::Error;
      std::string subsystem, message, remediation, assetGuid;
      unsigned int sceneObjectId = 0;
      std::string componentType;
      SourceByteRange sourceByteRange;
  };
  std::string TextDiagnosticRateLimitKey(const TextDiagnostic&);
  class TextDiagnosticSink {
  public:
      virtual ~TextDiagnosticSink() = default;
      virtual void Report(TextDiagnostic diagnostic) = 0;
  };
  ```

- [ ] **Step 3c: Add the collecting sink declaration.** Append this exact declaration:

  ```cpp
  class VectorTextDiagnosticSink final : public TextDiagnosticSink {
  public:
      void Report(TextDiagnostic diagnostic) override;
      const std::vector<TextDiagnostic>& Diagnostics() const noexcept;
  private:
      std::vector<TextDiagnostic> diagnostics_;
  };
  ```

- [ ] **Step 3d: Add the bounded logger-sink declaration.** Append this exact declaration:

  ```cpp
  class LoggerTextDiagnosticSink final : public TextDiagnosticSink {
  public:
      explicit LoggerTextDiagnosticSink(std::size_t maxRememberedKeys);
      void Report(TextDiagnostic diagnostic) override;
      void ResetRateLimit();
  private:
      std::size_t maxRememberedKeys_ = 0;
      std::deque<std::string> insertionOrder_;
      std::unordered_set<std::string> rememberedKeys_;
  };
  ```

- [ ] **Step 4: Implement the full context key.**

  ```cpp
  std::string TextDiagnosticRateLimitKey(const TextDiagnostic& d) {
      return std::string(StableTextDiagnosticCode(d.code)) + "\x1f" + d.assetGuid +
          "\x1f" + std::to_string(d.sceneObjectId) + "\x1f" + d.componentType +
          "\x1f" + std::to_string(d.sourceByteRange.begin) + ":" +
          std::to_string(d.sourceByteRange.end);
  }
  ```

- [ ] **Step 4a: Implement the collecting sink.** `VectorTextDiagnosticSink::Report` moves every record into `diagnostics_`; `Diagnostics()` returns that exact vector by const reference without deduplication.

- [ ] **Step 4b: Implement the bounded logger sink.** Emit through `Log::Emit`, suppress only a currently remembered identical key, and evict the oldest key before inserting beyond `maxRememberedKeys`; zero capacity emits every record.

- [ ] **Step 4c: Add the scoped log-test adapter.** `ScopedRingLogSink` installs the existing `RingBufferSink` in its constructor and restores the prior sink in its destructor so the test leaves global logging unchanged.

- [ ] **Step 5: Run the diagnostic green gate.**

  Run: `cmake --build --preset debug --target test_text_runtime_dependencies -j && ctest --test-dir build/debug -R '^test_text_runtime_dependencies$' --output-on-failure`

  Expected: PASS for exhaustive code mapping and rate-limit context identity.

- [ ] **Step 6: Commit typed diagnostics.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Text/TextDiagnostic.* \
    src/Common/Log.* tests/test_text_runtime_dependencies.cpp
  git commit -m "feat: add typed text diagnostics"
  ```

**Exit:** Every later text/UI/package failure can carry one stable code plus full context without collapsing distinct source ranges.

---

### Task 2.2: Own and verify the ICU runtime lifetime

**Prerequisite:** Tasks 1.2 and 2.1 are committed; ICU initialization is one-way within each process, while independent test processes remain parallel-safe unless they mutate the same staged on-disk root.

**Files:**

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
- Modify: `cmake/TextDependencies.cmake`
- Modify: `tests/test_text_runtime_dependencies.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: verified portable contract, `icudt78l.dat`, ICU static targets, and `TextDiagnosticSink`.
- Produces: `TextDependencyConfig::{FromEngineTextRoot}`, terminal single-lifetime `TextRuntimeDependencies::{Get,Initialize,Shutdown,IsReady,DependencyContractSha256,OutstandingClientHandleCount}`, move-only `TextRuntimeLifetimeGuard::Create`, process-wide test fixture `TextRuntimeTestSession`, fresh-process not-ready probes, `molga_define_text_runtime_resource_stage(stage_name,destination_root)`, `molga_stage_text_runtime_resources(target_name)`, `molga_add_text_test(target_name,source_name)`, and verified development files `Engine/Text/{text_dependency_contract.json,icudt78l.dat}`; all later ICU/HarfBuzz objects require `IsReady()==true`, and no successful process lifetime is restarted after terminal cleanup.

- [ ] **Step 1: Add the failing fresh-process lifecycle and tamper cases.** The doctest parent never initializes ICU. Create `molga_text_runtime_probe` from `tests/text_runtime_probe_main.cpp` plus the test-only ICU-call recorder; the child receives no compiled source/development root and never derives one from cwd. Its exact CLI is `--mode <closed-value> --fixture-root <canonical-root> --development-root <canonical-root> --report <new-direct-child-of-caller-temp-root>`. Allowed modes are `tampered-data`, `nonportable-contract`, `terminal-nonrestart`, `staged-valid`, `dev-missing-contract`, and `dev-tampered-data`; reject duplicates, unknown switches/modes, noncanonical/symlink roots, an existing report, a report whose parent is not an existing canonical directory, or a report that is not a direct child of that caller-owned root before initialization. Each child executes one mode, atomically writes schema-1 `{mode,firstInitialize,readyBeforeShutdown,harfbuzzIcuProbe,secondInitializeAttempted,secondInitialize,icuCallsBeforePublish,icuCallsAfterTerminal,terminallyCleaned,diagnosticCodes}`, performs at most one successful lifetime, and returns immediately after terminal cleanup.

  The parent target alone receives exact `MOLGA_TEXT_RUNTIME_PROBE`, `MOLGA_TEXT_RUNTIME_FIXTURE_ROOT`, and `MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT` definitions. As its one portable-contract C++ read, `RunTextRuntimeProbe(mode)` canonicalizes `MOLGA_TEXT_DEPENDENCY_CONTRACT`, requires its bytes to equal `fixtureRoot/text_dependency_contract.json`, then rejects an unknown mode before spawning, canonicalizes both compiled roots, creates one unique canonical caller temp root plus nonexisting report child, and invokes the probe without a shell using the literal argv `[probe,"--mode",mode,"--fixture-root",fixtureRoot,"--development-root",developmentRoot,"--report",report]`. It captures exit/stdout/stderr, requires exit `0`, parses the exact report schema, and removes only its temp root through RAII; the parent never initializes ICU/HarfBuzz. Add the cases without helper implementations so the target first exposes the missing runtime API:

  ```cpp
  TEST_CASE("ICU tamper fails before publishing ready state") {
      const auto report = RunTextRuntimeProbe("tampered-data");
      CHECK_FALSE(report.firstInitialize);
      CHECK_FALSE(report.readyBeforeShutdown);
      CHECK(report.icuCallsBeforePublish == 0);
      CHECK(report.diagnosticCodes == std::vector<std::string>{
          "TEXT_DEPENDENCY_INVALID"});
  }
  ```

- [ ] **Step 1a: Add the failing nonportable-contract/zero-ICU-call test.**

  ```cpp
  TEST_CASE("nonportable dependency contract fails before every ICU call") {
      const auto report = RunTextRuntimeProbe("nonportable-contract");
      CHECK_FALSE(report.firstInitialize);
      CHECK(report.icuCallsBeforePublish == 0);
      CHECK(report.diagnosticCodes == std::vector<std::string>{
          "TEXT_DEPENDENCY_INVALID"});
  }
  ```

- [ ] **Step 1b: Add the failing terminal non-restart probe.** Run this entire body in one dedicated child and return immediately afterward:

  ```cpp
  TEST_CASE("successful ICU and HarfBuzz lifetime cannot restart after cleanup") {
      const auto report = RunTextRuntimeProbe("terminal-nonrestart");
      CHECK(report.firstInitialize);
      CHECK(report.readyBeforeShutdown);
      CHECK(report.harfbuzzIcuProbe);
      CHECK(report.secondInitializeAttempted);
      CHECK_FALSE(report.secondInitialize);
      CHECK(report.icuCallsAfterTerminal == 0);
      CHECK(report.terminallyCleaned);
      CHECK(report.diagnosticCodes.back() == "TEXT_DEPENDENCY_INVALID");
  }
  ```

  The second failure is `DependencyInvalid` with remediation to start a fresh
  process. It never calls the cached HarfBuzz ICU funcs after cleanup.

- [ ] **Step 1c: Implement isolated dependency-tree test helpers in the child.** Define `CopyValidTextDependencyTree`, `FlipOneByte`, and `RewriteContractField` in `text_runtime_probe_main.cpp` using a uniquely named canonical child of the report's validated parent root, binary read/write, and RAII cleanup; never alter committed data. The parent passes the two immutable read-only roots as literal CLI values and never passes a mutation path.

- [ ] **Step 1d: Add the ICU call-count seam.** Route `udata_setCommonData`, `udata_setFileAccess`, `u_init`, and `u_cleanup` through the production-private runtime API table. A test-only companion TU used only by `molga_text_runtime_probe` installs counting forwarding functions before the first initialization; production retains the default table. The private hook rejects replacement after any lifecycle transition and the test companion cannot be linked into editor/runtime. Count calls before ready publication separately from calls attempted after terminal cleanup.

- [ ] **Step 1e: Add the failing staged-development-root test.** Define `MOLGA_TEXT_RUNTIME_FIXTURE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/Engine/Text"` and `MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT="$<TARGET_FILE_DIR:molga_engine>/Engine/Text"` only on the existing parent `test_text_runtime_dependencies` target now (without staging dependencies yet), then add:

  ```cpp
  TEST_CASE("built development Engine Text root initializes exact staged pair") {
      const auto report = RunTextRuntimeProbe("staged-valid");
      CHECK(report.firstInitialize);
      CHECK(report.readyBeforeShutdown);
      CHECK(report.terminallyCleaned);
  }
  ```

- [ ] **Step 1f: Add the failing missing/tampered development-root test.**

  ```cpp
  TEST_CASE("development Engine Text root fails closed when missing or tampered") {
      for (const std::string mode :
           {"dev-missing-contract", "dev-tampered-data"}) {
          const auto report = RunTextRuntimeProbe(mode);
          CHECK_FALSE(report.firstInitialize);
          CHECK(report.icuCallsBeforePublish == 0);
          CHECK(report.diagnosticCodes == std::vector<std::string>{
              "TEXT_DEPENDENCY_INVALID"});
      }
  }
  ```

- [ ] **Step 1g: Implement isolated staged-root mutations in the child.** Add `enum class DevRootMutation { RemoveContract, TamperIcuData };`. `CopyStagedEngineTextRoot()` creates a unique RAII temporary directory beneath the validated caller temp root, copies only the passed immutable development root's `text_dependency_contract.json` and `icudt78l.dat`, and returns paths to those copies. `ApplyDevRootMutation(RemoveContract)` removes only the copied contract; `ApplyDevRootMutation(TamperIcuData)` opens only the copied data file in binary read/write mode, seeks to byte `4096`, writes `oldByte ^ 0x80`, flushes, and requires unchanged size.

- [ ] **Step 1h: Add the failing process-session lifetime test.** Put this case in the new session-backed `test_text_runtime_session.cpp`; it observes the one process lifetime without stopping it:

  ```cpp
  TEST_CASE("text test session owns one exact staged runtime") {
      auto& session = TextRuntimeTestSession::Current();
      CHECK(session.EngineTextRoot() ==
            std::filesystem::path(MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT));
      CHECK(molga::text::TextRuntimeDependencies::Get().IsReady());
      CHECK_FALSE(session.ShutdownWasRequested());
  }
  ```

- [ ] **Step 1i: Add the failing app-unwind order test.** Run editor/runtime startup seams in subprocess mode for `--text-test-return-after-services=17` and `--text-test-return-after-services=18`, record lifetime events, and require `last_text_handle_destroyed < runtime_guard_shutdown < u_cleanup < process_return` for both nonzero early returns. The seam returns through the real scoped startup function and may not call `std::exit`, `_Exit`, `quick_exit`, or terminate the process before destructors.

- [ ] **Step 2: Run the lifetime red gate.**

  Run: `cmake --build --preset debug --target test_text_runtime_dependencies -j`

  Expected: compile FAIL because `TextRuntimeDependencies` does not exist.

- [ ] **Step 3: Add the exact public lifetime API.**

  ```cpp
  struct TextDependencyConfig {
      std::filesystem::path dependencyContract;
      std::filesystem::path icuData;
      bool packagedRuntime = false;
      static TextDependencyConfig FromEngineTextRoot(
          const std::filesystem::path& root, bool packagedRuntime);
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
  ```

- [ ] **Step 3a: Implement the exact root-to-config mapping.** `FromEngineTextRoot(root, packaged)` returns an all-empty-path config carrying `packaged` when `root.empty()`, otherwise returns `{root / "text_dependency_contract.json", root / "icudt78l.dat", packaged}` without probing a source tree or current working directory. `Initialize` reports either empty path as one `DependencyInvalid` before ICU calls.

- [ ] **Step 4: Implement portable-contract verification.** Parse schema `1`, reject unknown/missing locked dependency fields and any absolute path, hash the exact contract bytes, and keep the SHA in an unpublished local `State` candidate.

- [ ] **Step 5: Implement exact ICU-data validation.** Read the configured file without mutating it, require size `33107232`, compute SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b`, and return one `DependencyInvalid` before any ICU call when either differs.

- [ ] **Step 6: Implement aligned ICU initialization in this exact order.**

  ```text
  allocate 16-byte-aligned immutable storage and copy the data
  udata_setCommonData(alignedBytes, status)
  udata_setFileAccess(UDATA_ONLY_PACKAGES, status)
  u_init(&status)
  publish state_ only if every status is U_SUCCESS
  ```

  Publish `state_` only after `U_SUCCESS(status)` from every call.

- [ ] **Step 7: Implement partial-failure unwind.** Validation/hash/path failures that occur before the first ICU call leave `Lifecycle::NeverInitialized` and may be retried deterministically. Track whether common data was accepted; if a later ICU call fails, invoke `u_cleanup`, release aligned storage, set `Lifecycle::TerminallyCleaned`, emit one `DependencyInvalid`, and leave `state_ == nullptr`. Once `Ready` has been published, `Shutdown` is the only transition and ends in `TerminallyCleaned`. Every `Initialize` from `Ready` or `TerminallyCleaned` returns one `DependencyInvalid` before any ICU/HarfBuzz call; remediation is to start a fresh process.

- [ ] **Step 8: Separate client leases from runtime-owned handles.** Every service/face/analyzer/shaper handle gets a weak client-lifetime token registered in `State`; `OutstandingClientHandleCount` counts live external tokens but not `State`'s own common-data/internal ICU handles. `Shutdown()` requires zero live client tokens, then clears runtime-owned ICU/HarfBuzz handles while the common-data buffer and ICU runtime remain alive.

- [ ] **Step 8a: Release ICU state in the required order.** After handles are gone, call terminal `u_cleanup`, release aligned common-data storage, reset `state_`, and set `Lifecycle::TerminallyCleaned`. No code in that process may subsequently call an ICU API or a HarfBuzz object backed by `hb_icu_get_unicode_funcs`; normal application/test mains return immediately after this cleanup.

- [ ] **Step 8b: Make shutdown terminal and idempotent.** `Shutdown` returns immediately from `NeverInitialized` or `TerminallyCleaned`; from `Ready` it performs Step 8a exactly once. The `terminal-nonrestart` child checks not-ready state, an extra no-op `Shutdown`, and a second `Initialize` rejection with zero ICU calls. Do not add a repeat-cycle test.

- [ ] **Step 8c: Implement the move-only app lifetime guard.** `Create` calls `Initialize` and returns `nullopt` on failure; a live guard's destructor first fail-fasts if `OutstandingClientHandleCount()!=0`, then calls `Shutdown`, which clears only runtime-owned handles before `u_cleanup`. Moving transfers `active_` and deactivates the source so exactly one guard performs cleanup.

- [ ] **Step 8d: Add the exact common test-session API.** Put this test-only interface in `tests/TextRuntimeTestSession.h`; it deliberately has no stop/restore seam:

  ```cpp
  class TextRuntimeTestSession {
  public:
      explicit TextRuntimeTestSession(std::filesystem::path engineTextRoot);
      ~TextRuntimeTestSession();
      bool Initialize();
      bool ShutdownAfterTests();
      const std::filesystem::path& EngineTextRoot() const noexcept;
      bool ShutdownWasRequested() const noexcept;
      static TextRuntimeTestSession& Current();
      static void Install(TextRuntimeTestSession*) noexcept;
  private:
      std::filesystem::path engineTextRoot_;
      bool installed_ = false;
      bool shutdownRequested_ = false;
  };
  ```

- [ ] **Step 8e: Implement session initialization and checked terminal shutdown.** `Initialize` calls `TextDependencyConfig::FromEngineTextRoot(engineTextRoot_, false)` exactly once and returns success only after ready state; the executable main then calls `Install(&session)` exactly once. `ShutdownAfterTests` first sets `shutdownRequested_`, fails if any handle remains, otherwise performs terminal shutdown, clears the installed pointer, and requires not-ready/terminal state immediately before main returns. The destructor invokes the same checked terminal shutdown when still installed so an exception cannot bypass `u_cleanup`; a previously successful explicit shutdown makes it a no-op. No test case may call shutdown directly or recreate the session; not-ready/post-cleanup behavior belongs to fresh child executables.

- [ ] **Step 8f: Implement the dedicated doctest executable main.** `tests/text_doctest_main.cpp` owns the only main for text-runtime tests:

  ```cpp
  #define DOCTEST_CONFIG_IMPLEMENT
  #include "doctest.h"
  #include "TextRuntimeTestSession.h"
  int main(int argc, char** argv) {
      doctest::Context context(argc, argv);
      TextRuntimeTestSession session(MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT);
      if (!session.Initialize()) return 4;
      TextRuntimeTestSession::Install(&session);
      const int result = context.run();
      if (!session.ShutdownAfterTests()) return 5;
      return result;
  }
  ```

- [ ] **Step 9: Isolate lifecycle mutation by process.** `test_text_runtime_dependencies` launches one child per probe mode; `test_text_runtime_session` and every `molga_add_text_test` executable initialize once in main and clean up once immediately before process return. Set `RUN_SERIAL TRUE` only on a test whose parent manipulates a shared on-disk staged fixture or app subprocess artifact; ordinary session-backed Unicode/font/shaping/layout/cache cases are independent processes and remain parallel-safe. No target contains an in-process shutdown/restore helper.

- [ ] **Step 9a: Validate staging inputs before touching the development root.** In `StageTextRuntimeResources.cmake`, require `CONTRACT_SOURCE`, `ICU_DATA_SOURCE`, and `DESTINATION_ROOT`; require both sources to be regular files, then require ICU size `33107232` and SHA-256 `d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b`. Compute `contract_source_sha` only after those checks.

- [ ] **Step 9b: Stage and transactionally publish the exact pair.** Make `DESTINATION_ROOT`, take the exact process lock below, include `TextArtifactTransaction.cmake`, and require successful recovery of `${DESTINATION_ROOT}/.molga-text-runtime-journal.json` before any fast path. If both destination hashes already match, return; otherwise copy both sources into one random 128-bit child staging directory and verify their hashes.

  ```cmake
  file(MAKE_DIRECTORY "${DESTINATION_ROOT}")
  file(LOCK "${DESTINATION_ROOT}.molga-text-runtime.lock"
       GUARD PROCESS TIMEOUT 60 RESULT_VARIABLE lock_result)
  if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "could not lock text runtime root: ${lock_result}")
  endif()
  include("${CMAKE_CURRENT_LIST_DIR}/TextArtifactTransaction.cmake")
  set(journal "${DESTINATION_ROOT}/.molga-text-runtime-journal.json")
  text_recover_artifact_journal(
    recovered "${journal}" "${DESTINATION_ROOT}")
  if(NOT recovered)
    message(FATAL_ERROR "could not recover text runtime staging journal")
  endif()
  string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef stage_id)
  set(stage_root "${DESTINATION_ROOT}/.molga-stage-${stage_id}")
  ```

- [ ] **Step 9c: Publish the verified staged pair.** After the exact-hash fast path or staged-copy checks, call:

  ```cmake
  text_publish_artifact_set(published
    "${journal}" 0
    "${DESTINATION_ROOT}"
    "${stage_root}/text_dependency_contract.json"
    "${DESTINATION_ROOT}/text_dependency_contract.json"
    "${stage_root}/icudt78l.dat"
    "${DESTINATION_ROOT}/icudt78l.dat")
  ```

  Fail unless `published` is true and both destination hashes still match; remove only that exact `stage_root` after success. A failed recovery/publish retains its journal/rollback evidence and fails the build.

- [ ] **Step 9d: Add the reusable explicit-root staging helper.** Append this function to `TextDependencies.cmake`; the always-run target repairs deletion/tampering even when a consumer does not relink:

  ```cmake
  function(molga_define_text_runtime_resource_stage stage_name destination_root)
    if(TARGET "${stage_name}")
      message(FATAL_ERROR "text runtime stage already exists: ${stage_name}")
    endif()
    if("${destination_root}" STREQUAL "")
      message(FATAL_ERROR "text runtime destination is empty: ${stage_name}")
    endif()
    add_custom_target("${stage_name}"
      COMMAND "${CMAKE_COMMAND}"
        "-DCONTRACT_SOURCE=${MOLGA_TEXT_DEPENDENCY_CONTRACT}"
        "-DICU_DATA_SOURCE=${CMAKE_SOURCE_DIR}/resources/text/icudt78l.dat"
        "-DDESTINATION_ROOT=${destination_root}"
        -P "${CMAKE_SOURCE_DIR}/cmake/StageTextRuntimeResources.cmake"
      DEPENDS molga_text_dependencies_ready
              "${CMAKE_SOURCE_DIR}/resources/text/icudt78l.dat"
      VERBATIM)
  endfunction()
  ```

- [ ] **Step 9e: Add the exact target-scoped staging wrapper.** Append this function after the explicit-root helper:

  ```cmake
  function(molga_stage_text_runtime_resources target_name)
    if(NOT TARGET "${target_name}")
      message(FATAL_ERROR "text runtime resource target does not exist: ${target_name}")
    endif()
    set(stage_target "${target_name}_text_runtime_resources")
    if(TARGET "${stage_target}")
      message(FATAL_ERROR "text runtime resources already attached: ${target_name}")
    endif()
    molga_define_text_runtime_resource_stage("${stage_target}"
      "$<TARGET_FILE_DIR:${target_name}>/Engine/Text")
    add_dependencies("${target_name}" "${stage_target}")
  endfunction()
  ```

- [ ] **Step 9f: Create one exact text-test resource root.** In `tests/CMakeLists.txt`, call `molga_define_text_runtime_resource_stage(molga_text_test_runtime_resources "${CMAKE_CURRENT_BINARY_DIR}/Engine/Text")` once and define `MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/Engine/Text"` only on session-backed test executables. Never derive it from the current working directory or an application target directory.

- [ ] **Step 9g: Add the exact session-backed test helper.** Define this beside `molga_add_test`; it links the header-only `doctest` interface, never `doctest_main`, so the executable has exactly one main. Every session-backed ready test is a normal HarfBuzz/ICU consumer, so this helper itself must call `molga_attach_text_dependencies` after creating the target. A later legacy/explicit call to the same normal helper is permitted but is the Step 5d property-guarded no-op; a direct-ICU-only or not-ready process must use a different generic helper instead of `molga_add_text_test`.

  ```cmake
  function(molga_add_text_test target_name source_name)
    add_executable("${target_name}"
      "${source_name}"
      "${CMAKE_CURRENT_SOURCE_DIR}/text_doctest_main.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/TextRuntimeTestSession.cpp")
    target_link_libraries("${target_name}" PRIVATE
      molga_core doctest molga_warnings)
    target_include_directories("${target_name}" PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions("${target_name}" PRIVATE
      MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/Engine/Text")
    add_dependencies("${target_name}" molga_text_test_runtime_resources)
    molga_attach_text_dependencies("${target_name}")
    set_target_properties("${target_name}" PROPERTIES ENABLE_EXPORTS ON)
    add_test(NAME "${target_name}" COMMAND "${target_name}")
    set_tests_properties("${target_name}" PROPERTIES LABELS "unit;text-runtime")
  endfunction()
  ```

- [ ] **Step 9g.1: Prove normal attachment is exactly once.** After registering `test_text_runtime_session` through `molga_add_text_test`, deliberately call `molga_attach_text_dependencies(test_text_runtime_session)` once more. At configure time require target properties `MOLGA_TEXT_DEPENDENCIES_ATTACHED` and `MOLGA_TEXT_VERIFICATION_BARRIER_ATTACHED` to equal `TRUE`; count exact list items and require one `molga_text_harfbuzz` and one `molga_text_rasterizer` in `LINK_LIBRARIES`, one `U_STATIC_IMPLEMENTATION` in `COMPILE_DEFINITIONS`, and one `molga_text_dependencies_ready` in `MANUALLY_ADDED_DEPENDENCIES`. Require `MOLGA_TEXT_PORTABLE_CONTRACT_ATTACHED` to be unset/false and require zero `MOLGA_TEXT_DEPENDENCY_CONTRACT`, `MOLGA_TEXT_DEPENDENCY_BUILD_LOCK`, `MOLGA_SOURCE_DIR`, `MOLGA_BINARY_DIR`, `MOLGA_CMAKE_COMMAND`, or `MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT` definitions on this target. Any absent/duplicate/forbidden item is `FATAL_ERROR`. This regression proves the common helper supplies only the ready-test link/barrier boundary and that an existing explicit re-attachment cannot perturb static link order, definitions, or the barrier.

- [ ] **Step 9h: Register the child probe and session self-test.** Create `molga_text_runtime_probe` from `text_runtime_probe_main.cpp` and `TextRuntimeDependenciesTestAccess.cpp`, link `molga_core`, attach normal HarfBuzz/ICU text dependencies, give it no root/path compile definition, and do not register it as a CTest. Immediately after the existing parent `test_text_runtime_dependencies` exists, call `molga_attach_text_portable_contract_consumer(test_text_runtime_dependencies)` and define exact `MOLGA_TEXT_RUNTIME_PROBE="$<TARGET_FILE:molga_text_runtime_probe>"`, `MOLGA_TEXT_RUNTIME_FIXTURE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/Engine/Text"`, and `MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT="$<TARGET_FILE_DIR:molga_engine>/Engine/Text"` there. Add parent target dependencies on the probe, `molga_text_test_runtime_resources`, and `molga_engine_text_runtime_resources`; keep the parent on its existing doctest main without text-session initialization. This parent owns the executable and both immutable roots and receives the portable path/barrier but no build-lock/source/binary macro. Register `test_text_runtime_session` through `molga_add_text_test`; Tasks 3.1–7.2 register their ready Unicode/font/shaping/atlas/cache/layout targets through the same helper and therefore already receive normal text dependencies. Any retained explicit normal attach after those registrations is the verified idempotent no-op. Standalone link probes and explicit not-ready child executables retain dedicated mains.

- [ ] **Step 9i: Attach existing development executables after creation.** Call `molga_stage_text_runtime_resources(molga_engine)` immediately after the editor target exists and `molga_stage_text_runtime_resources(molga_runtime)` immediately after the runtime target exists. The parent test's dependency on `molga_engine_text_runtime_resources` owns the exact development root named by its generator-expression compile definition; its dependency on `molga_text_test_runtime_resources` owns the fixture root. The child receives both only as literal argv. Never derive a root from the working directory or from the probe executable path.

- [ ] **Step 10: Wire editor lifetime and failure behavior.** After `PathService::InitFromExecutable`, enter a scoped `RunEditorAfterPaths` function, create `TextRuntimeLifetimeGuard` from `TextDependencyConfig::FromEngineTextRoot(PathService::Get().EngineResource("Engine/Text"), false)` before constructing Game View text services, and declare every text service/renderer/host handle after the guard. On failure retain the ImGui shell, expose the typed diagnostic, and mark Game View text unavailable without attempting a fallback renderer.

- [ ] **Step 10a: Route every editor return through stack unwinding.** Keep all post-initialization early returns inside `RunEditorAfterPaths`; destroy scene/UI/text services and all ICU/HarfBuzz-owning handles before the earlier-declared guard. Ban `std::exit`, `_Exit`, `quick_exit`, and static text-service owners in this scope so the guard's `u_cleanup` is last.

- [ ] **Step 11: Wire development-runtime lifetime and exit behavior.** After `PathService::InitFromExecutable`, enter `RunRuntimeAfterPaths`, create the guard from `PathService::Get().EngineResource("Engine/Text")` with `packagedRuntime=false` before SDL/window/renderer/scripts/assets/scenes, and declare all of those owners afterward; on guard-creation failure print stable code, failed path/hash and remediation and return `4`.

- [ ] **Step 11a: Route every runtime return through stack unwinding.** Every return after guard creation remains inside `RunRuntimeAfterPaths`, including argument, window, renderer, script, asset, and scene failures. Reverse destruction releases all text handles before guard shutdown and `u_cleanup`; no cleanup callback may retain a text resource past the function.

- [ ] **Step 11b: Preserve the packaging handoff.** Document beside the helper that Milestone 16 must invoke `molga_stage_text_runtime_resources` for its `molga_runtime_dev` target and copy the same verified `Engine/Text` pair into the final runtime bundle before switching packaged initialization to `packagedRuntime=true`; package staging may not bypass `StageTextRuntimeResources.cmake` or substitute source-tree paths. Its `TextRuntimeManifest` must contain exactly one logical `harfbuzz` record whose SHA is the portable `harfbuzz.compositeSha256`, exactly one `icui18n` record, and exactly one logical `icuuc` record whose SHA is the composite common SHA. Raw `harfbuzz-icu`, raw core/adapter/object provenance, and ICU stub source/object provenance remain build-lock-only and may not become additional manifest libraries.

- [ ] **Step 12: Run lifetime, startup, and regression green gates.**

  ```bash
  cmake --build --preset debug --target molga_engine_text_runtime_resources \
    molga_runtime_text_runtime_resources test_text_runtime_dependencies \
    test_text_runtime_session molga_engine molga_runtime -j
  ctest --test-dir build/debug -R '^(test_text_dependencies|test_text_runtime_dependencies|test_text_runtime_session|runtime_smoke)$' --output-on-failure
  ctest --preset debug
  ```

  Expected: focused tests and the complete Debug suite pass; a tampered temporary data copy never reaches ready state or window creation.

- [ ] **Step 13: Commit the verified lifetime.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt cmake/TextDependencies.cmake \
    cmake/StageTextRuntimeResources.cmake src/Text/TextRuntimeDependencies.* \
    src/main.cpp src/runtime_main.cpp tests/test_text_runtime_dependencies.cpp \
    tests/TextRuntimeTestSession.* tests/text_doctest_main.cpp \
    tests/TextRuntimeDependenciesTestAccess.* tests/text_runtime_probe_main.cpp \
    tests/test_text_runtime_session.cpp
  git commit -m "feat: verify text runtime dependencies"
  ```

**Exit:** ICU has one verified process lifetime, both development executables consume the build-verified `Engine/Text` pair, aligned data outlives `u_cleanup`, and startup failures follow the approved editor/runtime terminal actions.

---

### Task 3.1: Add checked signed 26.6 logical units

**Prerequisite:** Typed diagnostics exist; this task has no UI dependency.

**Files:**

- Create: `src/Common/Fixed26_6.h`
- Create: `src/Common/Fixed26_6.cpp`
- Create: `tests/test_unicode_text.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: finite `float` authored/logical values.
- Produces: `molga::Fixed26_6`, `FixedPoint`, `FixedSize`, and `FixedRect`; later text/UI algorithms compare only exact raw values.

- [ ] **Step 1: Add failing conversion/arithmetic tests.**

  ```cpp
  TEST_CASE("Fixed26_6 rejects invalid input and rounds exact ties away from zero") {
      using molga::Fixed26_6;
      CHECK_FALSE(Fixed26_6::FromFloat(std::numeric_limits<float>::infinity()));
      CHECK(Fixed26_6::FromFloat(-0.0f)->Raw() == 0);
      CHECK(Fixed26_6::FromFloat(1.0f / 128.0f)->Raw() == 1);
      CHECK(Fixed26_6::FromFloat(-1.0f / 128.0f)->Raw() == -1);
      CHECK_FALSE(Fixed26_6::CheckedAdd(
          Fixed26_6::FromRaw(INT32_MAX), Fixed26_6::FromRaw(1)));
      CHECK_FALSE(Fixed26_6::CheckedMulDiv(
          Fixed26_6::FromRaw(64), 1, 0));
  }
  ```

- [ ] **Step 1a: Register the Unicode test with the common runtime main.** Call `molga_add_text_test(test_unicode_text test_unicode_text.cpp)`; do not link `doctest_main` or add another staged-root macro/dependency by hand.

- [ ] **Step 2: Run the fixed-point red gate.**

  Run: `cmake --build --preset debug --target test_unicode_text -j`

  Expected: compile FAIL because `Common/Fixed26_6.h` does not exist.

- [ ] **Step 3: Add the master-plan fixed-point value types.**

  ```cpp
  class Fixed26_6 {
  public:
      static constexpr std::int32_t Scale = 64;
      static std::optional<Fixed26_6> FromFloat(float value);
      static constexpr Fixed26_6 FromRaw(std::int32_t value) {
          return Fixed26_6(value);
      }
      constexpr std::int32_t Raw() const { return raw_; }
      float ToFloat() const;
      static std::optional<Fixed26_6> CheckedAdd(Fixed26_6, Fixed26_6) noexcept;
      static std::optional<Fixed26_6> CheckedSub(Fixed26_6, Fixed26_6) noexcept;
      static std::optional<Fixed26_6> CheckedMulDiv(
          Fixed26_6, std::int64_t numerator, std::int64_t denominator) noexcept;
      friend constexpr bool operator==(Fixed26_6 a, Fixed26_6 b) noexcept {
          return a.raw_ == b.raw_;
      }
      friend constexpr bool operator!=(Fixed26_6 a, Fixed26_6 b) noexcept {
          return !(a == b);
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
      Fixed26_6 x = Fixed26_6::FromRaw(0), y = Fixed26_6::FromRaw(0);
      Fixed26_6 width = Fixed26_6::FromRaw(0), height = Fixed26_6::FromRaw(0);
      friend constexpr bool operator==(FixedRect a, FixedRect b) noexcept {
          return a.x == b.x && a.y == b.y && a.width == b.width &&
                 a.height == b.height;
      }
      friend constexpr bool operator!=(FixedRect a, FixedRect b) noexcept {
          return !(a == b);
      }
  };
  ```

- [ ] **Step 4: Implement `FromFloat`.** Reject non-finite/out-of-range input, canonicalize signed zero, multiply by 64 in a wider finite representation, and round exact half ties away from zero before the final `int32_t` check.

- [ ] **Step 4a: Implement `ToFloat`.** Convert the signed raw value with an exact division by `Scale`, returning canonical positive zero when `raw_ == 0`.

- [ ] **Step 5: Implement checked add and subtract.** Convert both raw operands to `int64_t`, perform one operation, return `nullopt` outside `int32_t`, and never saturate or wrap.

- [ ] **Step 6: Implement checked multiply/divide.** Reject zero denominator and intermediate overflow, reduce sign explicitly, round nearest with half ties away from zero, and validate the final raw `int32_t`.

- [ ] **Step 7: Run the fixed-point green gate.**

  Run: `cmake --build --preset debug --target test_unicode_text -j && ctest --test-dir build/debug -R '^test_unicode_text$' --output-on-failure`

  Expected: PASS for finite conversion, signed zero, tie rounding, and checked overflow.

- [ ] **Step 8: Commit fixed-point primitives.**

  ```bash
  git add CMakeLists.txt tests/CMakeLists.txt src/Common/Fixed26_6.* \
    tests/test_unicode_text.cpp
  git commit -m "feat: add checked 26.6 logical units"
  ```

**Exit:** Text and UI can share a deterministic logical unit without float comparison or overflow ambiguity.

---

### Task 3.2: Build a byte-preserving Unicode text buffer

**Prerequisite:** Task 2.2 ICU lifetime and Task 3.1 fixed point are green.

**Files:**

- Create: `src/Text/UnicodeTextBuffer.h`
- Create: `src/Text/UnicodeTextBuffer.cpp`
- Modify: `tests/test_unicode_text.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: arbitrary owned UTF-8 and `TextDiagnosticSink`.
- Produces: `Utf16Range`, `ScalarRange`, `GraphemeRange`, `DecodedScalar`, and `UnicodeTextBuffer::{Build,OriginalUtf8,SanitizedUtf16,Scalars,SourceBytesForUtf16,Utf16ForSourceBytes,HadDecodeErrors}`.

- [ ] **Step 1: Add the failing invalid UTF-8 source-range test.**

  ```cpp
  TEST_CASE("invalid UTF-8 keeps the original maximal-subpart byte range") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build(
          std::string("A\xF0\x28\x8C\x28Z", 6), sink);
      REQUIRE(buffer.has_value());
      CHECK(buffer->OriginalUtf8().size() == 6);
      CHECK(buffer->Scalars().at(1).value == U'\uFFFD');
      CHECK(buffer->Scalars().at(1).sourceBytes.begin == 1);
      CHECK(buffer->Scalars().at(1).sourceBytes.end == 2);
      const auto mapped =
          buffer->SourceBytesForUtf16(buffer->Scalars().at(1).utf16Units);
      REQUIRE(mapped);
      CHECK(mapped->begin == 1);
      CHECK(buffer->HadDecodeErrors());
  }
  ```

- [ ] **Step 1a: Add the failing supplementary-scalar test.**

  ```cpp
  TEST_CASE("supplementary scalar maps through two UTF-16 units") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build(u8"A😀Z", sink);
      REQUIRE(buffer);
      CHECK(buffer->Scalars().at(1).utf16Units.end -
            buffer->Scalars().at(1).utf16Units.begin == 2);
  }
  ```

- [ ] **Step 1b: Add the failing exact-boundary rejection test.**

  ```cpp
  TEST_CASE("range mapping accepts exact empty end and rejects partial scalars") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build(u8"A😀Z", sink);
      REQUIRE(buffer);
      const auto utf16End = static_cast<std::uint32_t>(buffer->SanitizedUtf16().size());
      const auto byteEnd = static_cast<std::uint32_t>(buffer->OriginalUtf8().size());
      const auto endRange = buffer->SourceBytesForUtf16({utf16End, utf16End});
      REQUIRE(endRange);
      CHECK(endRange->begin == byteEnd);
      CHECK(endRange->end == byteEnd);
      const auto reverseEnd = buffer->Utf16ForSourceBytes({byteEnd, byteEnd});
      REQUIRE(reverseEnd);
      CHECK(reverseEnd->begin == utf16End);
      CHECK(reverseEnd->end == utf16End);
      CHECK_FALSE(buffer->SourceBytesForUtf16({3, 2}));
      CHECK_FALSE(buffer->SourceBytesForUtf16({2, 2})); // inside 😀 surrogate pair
      CHECK_FALSE(buffer->SourceBytesForUtf16({0, utf16End + 1}));
      CHECK_FALSE(buffer->Utf16ForSourceBytes({5, 4}));
      CHECK_FALSE(buffer->Utf16ForSourceBytes({2, 2})); // inside 😀 UTF-8 bytes
      CHECK_FALSE(buffer->Utf16ForSourceBytes({1, 2})); // ends inside 😀 bytes
      CHECK_FALSE(buffer->Utf16ForSourceBytes({byteEnd + 1, byteEnd + 1}));
  }
  ```

- [ ] **Step 1c: Add the failing valid UTF-8 round-trip table.**

  ```cpp
  TEST_CASE("valid UTF-8 table round-trips scalar UTF-16 and source bytes") {
      const std::vector<DecodeCase> cases{
          {"A", {U'A'}, {{0, 1}}},
          {u8"é", {U'é'}, {{0, 2}}},
          {u8"😀", {U'😀'}, {{0, 4}}},
      };
      for (const auto& c : cases) {
          molga::text::VectorTextDiagnosticSink sink;
          const auto buffer = molga::text::UnicodeTextBuffer::Build(c.bytes, sink);
          REQUIRE(buffer);
          CHECK_FALSE(buffer->HadDecodeErrors());
          CHECK(ScalarValues(*buffer) == c.scalars);
          CHECK(ScalarSourceRanges(*buffer) == c.sourceRanges);
          CHECK(EveryScalarRoundTripsBothMappings(*buffer));
      }
  }
  ```

- [ ] **Step 1d: Add the failing maximal-subpart invalid UTF-8 table.**

  ```cpp
  TEST_CASE("invalid UTF-8 table preserves exact maximal-subpart ranges") {
      const std::vector<DecodeCase> cases{
          {std::string("\xC0\xAF", 2), {U'\uFFFD', U'\uFFFD'},
           {{0, 1}, {1, 2}}},
          {std::string("\xF0\x9F\x92", 3), {U'\uFFFD'}, {{0, 3}}},
          {std::string("\x80", 1), {U'\uFFFD'}, {{0, 1}}},
      };
      for (const auto& c : cases) {
          molga::text::VectorTextDiagnosticSink sink;
          const auto buffer = molga::text::UnicodeTextBuffer::Build(c.bytes, sink);
          REQUIRE(buffer);
          CHECK(buffer->HadDecodeErrors());
          CHECK(ScalarValues(*buffer) == c.scalars);
          CHECK(ScalarSourceRanges(*buffer) == c.sourceRanges);
          CHECK(EveryScalarRoundTripsBothMappings(*buffer));
          CHECK(InvalidDiagnosticRanges(sink) == c.sourceRanges);
      }
  }
  ```

- [ ] **Step 1e: Implement the decode-table helpers.** Define `DecodeCase { std::string bytes; std::vector<char32_t> scalars; std::vector<SourceByteRange> sourceRanges; }`. `ScalarValues` and `ScalarSourceRanges` project `DecodedScalar` fields in order; `EveryScalarRoundTripsBothMappings` requires `SourceBytesForUtf16(s.utf16Units)==s.sourceBytes` and `Utf16ForSourceBytes(s.sourceBytes)==s.utf16Units` for every scalar; `InvalidDiagnosticRanges` returns only `Utf8Invalid` ranges in report order.

- [ ] **Step 2: Run the buffer red gate.**

  Run: `cmake --build --preset debug --target test_unicode_text -j`

  Expected: compile FAIL because `UnicodeTextBuffer` and mapping ranges do not exist.

- [ ] **Step 3: Add exact public mapping records.**

  ```cpp
  struct Utf16Range {
      std::uint32_t begin = 0, end = 0;
      friend constexpr bool operator==(Utf16Range a, Utf16Range b) noexcept {
          return a.begin == b.begin && a.end == b.end;
      }
  };
  struct ScalarRange {
      std::uint32_t begin = 0, end = 0;
      friend constexpr bool operator==(ScalarRange a, ScalarRange b) noexcept {
          return a.begin == b.begin && a.end == b.end;
      }
  };
  struct GraphemeRange {
      std::uint32_t begin = 0, end = 0;
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
  ```

- [ ] **Step 4: Add exact `UnicodeTextBuffer` storage/accessors.**

  ```cpp
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
  private:
      std::string originalUtf8_;
      std::u16string sanitizedUtf16_;
      std::vector<DecodedScalar> scalars_;
      std::vector<SourceByteRange> utf16SourceRanges_;
      bool hadDecodeErrors_ = false;
  };
  ```

  Reject input whose byte/scalar/unit count cannot fit the declared 32-bit offsets.

- [ ] **Step 5: Implement valid UTF-8 decoding.** For each accepted scalar record original byte start/end, scalar index, and one/two UTF-16 units; reject overlong, surrogate, and out-of-range encodings before appending.

- [ ] **Step 6: Implement maximal-subpart replacement.** Consume exactly one Unicode maximal subpart for each ill-formed sequence, append `U+FFFD`, preserve its original byte range, set `hadDecodeErrors_`, and report one `Utf8Invalid` with that range. Never normalize or rewrite `originalUtf8_`.

- [ ] **Step 7: Implement `SourceBytesForUtf16`.** Accept an empty range at any exact scalar boundary, including end-to-end; return `nullopt` for reversed/out-of-bounds endpoints or one inside a surrogate pair, and translate valid endpoints without widening.

- [ ] **Step 7a: Implement `Utf16ForSourceBytes`.** Apply the symmetric rule to original byte endpoints: exact empty scalar boundaries are valid, while reversed/out-of-bounds ranges and endpoints inside a multibyte scalar return `nullopt`.

  Consumer contract: Task 3.3 and shaping/layout check each optional before dereference and propagate a typed invalid-range/UTF-8 failure. The later SDL IME task may clamp only under its separately specified `TEXT_INPUT_RANGE_CLAMPED` policy; no caller silently widens a partial scalar.

- [ ] **Step 8: Run the mapping green gate.**

  Run: `cmake --build --preset debug --target test_unicode_text -j && ctest --test-dir build/debug -R '^test_unicode_text$' --output-on-failure`

  Expected: PASS for ASCII, multibyte BMP, surrogate pairs, overlong/truncated/stray sequences, and both mapping directions.

- [ ] **Step 9: Commit the byte-preserving buffer.**

  ```bash
  git add CMakeLists.txt src/Text/UnicodeTextBuffer.* tests/test_unicode_text.cpp
  git commit -m "feat: preserve Unicode source byte mappings"
  ```

**Exit:** Sanitized display text can never erase the authored byte range needed by clusters, diagnostics, or later cache collision checks.

---

### Task 3.3: Analyze graphemes, breaks, scripts, and BiDi with ICU

**Prerequisite:** Task 3.2 produces byte/UTF-16 maps and Task 2.2 reports `IsReady()==true`.

**Files:**

- Create: `src/Text/UnicodeAnalysis.h`
- Create: `src/Text/UnicodeAnalysis.cpp`
- Create: `tests/UnicodeTextAnalyzerTestAccess.h`
- Create: `tests/UnicodeTextAnalyzerTestAccess.cpp`
- Create: `tests/test_unicode_not_ready.cpp`
- Modify: `src/Rendering/Utf8.h`
- Modify: `src/Rendering/Utf8.cpp`
- Modify: `tests/test_unicode_text.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `UnicodeTextBuffer`, ready ICU lifetime, locale default `und`, and base direction default `Auto`.
- Produces: `BaseDirection`, `TextAnalysisOptions`, `AnalysisItem`, immutable `UnicodeAnalysis`, and `UnicodeTextAnalyzer::Analyze`; `DecodeUtf8` remains a non-authoritative wrapper over the new decoder.

- [ ] **Step 1: Add the failing BiDi/source-boundary analysis test.**

  ```cpp
  TEST_CASE("analysis preserves grapheme boundaries and exact BiDi levels") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build(u8"abc שלום 123!", sink);
      REQUIRE(buffer);
      auto analysis = molga::text::UnicodeTextAnalyzer::Analyze(
          *buffer, {"und", molga::text::BaseDirection::Auto}, sink);
      REQUIRE(analysis);
      CHECK(analysis->GraphemeBoundaries().front() == 0);
      CHECK(analysis->GraphemeBoundaries().back() == buffer->OriginalUtf8().size());
      CHECK(EmbeddingLevelsByGrapheme(*analysis) ==
            std::vector<std::uint8_t>{0,0,0,0,1,1,1,1,1,2,2,2,0});
      CHECK(AllItemBoundariesMapToOriginalBytes(*buffer, *analysis));
  }
  ```

- [ ] **Step 1a: Add the failing extended-grapheme tests.**

  ```cpp
  TEST_CASE("combining ZWJ and variation sequences remain one grapheme") {
      const auto combining = AnalyzeFixture(u8"x\u0301");
      const auto zwj = AnalyzeFixture(u8"👩\u200D🚀");
      const auto variation = AnalyzeFixture(u8"❤️");
      const auto variationSupplement = AnalyzeFixture(u8"！\uFE00");
      CHECK(combining.GraphemeBoundaries().size() - 1 == 1);
      CHECK(zwj.GraphemeBoundaries().size() - 1 == 1);
      CHECK(variation.GraphemeBoundaries().size() - 1 == 1);
      CHECK(variationSupplement.GraphemeBoundaries().size() - 1 == 1);
  }
  ```

  Define `EmbeddingLevelsByGrapheme`, `AllItemBoundariesMapToOriginalBytes`, and the small `AnalyzeFixture` in the same test file using only the public APIs above.

- [ ] **Step 1b: Add the failing CRLF/paragraph-boundary test.**

  ```cpp
  TEST_CASE("CRLF is one grapheme and one explicit paragraph separator") {
      const auto analysis = AnalyzeFixture("A\r\nB\n");
      CHECK(analysis.GraphemeBoundaries() ==
            std::vector<std::uint32_t>{0, 1, 3, 4, 5});
      CHECK(ContainsBoundary(analysis.LineBreakBoundaries(), 3));
      CHECK(ContainsBoundary(analysis.LineBreakBoundaries(), 5));
      CHECK_FALSE(ContainsBoundary(analysis.LineBreakBoundaries(), 2));
      CHECK(ParagraphStartBytes(analysis) ==
            std::vector<std::uint32_t>{0, 3});
      CHECK(ParagraphEndBytes(analysis) ==
            std::vector<std::uint32_t>{3, 5});
  }
  ```

- [ ] **Step 1c: Add the failing script-resolution test.** Include `<unicode/uscript.h>` and verify resolved item scripts rather than scalar-name heuristics:

  ```cpp
  TEST_CASE("Latin Arabic common and inherited scalars resolve by context") {
      const auto analysis = AnalyzeFixture(u8"A·x\u0301 سـ");
      CHECK(ScriptAtGrapheme(analysis, 0) == USCRIPT_LATIN);
      CHECK(ScriptAtGrapheme(analysis, 1) == USCRIPT_LATIN);
      CHECK(ScriptAtGrapheme(analysis, 2) == USCRIPT_LATIN);
      CHECK(ScriptAtGrapheme(analysis, 4) == USCRIPT_ARABIC);
      CHECK(ScriptAtGrapheme(analysis, 5) == USCRIPT_ARABIC);
  }
  ```

- [ ] **Step 1d: Add the failing Thai/CJK line-opportunity test.**

  ```cpp
  TEST_CASE("ICU supplies Thai opportunities and CJK punctuation prohibitions") {
      const auto thai = AnalyzeFixture(
          u8"ภาษาไทย", {"th", molga::text::BaseDirection::Auto});
      CHECK(ContainsBoundary(thai.LineBreakBoundaries(), 12));
      CHECK(ContainsBoundary(thai.LineBreakBoundaries(), 21));
      const auto cjk = AnalyzeFixture(
          u8"漢字（、。）", {"ja", molga::text::BaseDirection::Auto});
      CHECK(ContainsBoundary(cjk.LineBreakBoundaries(), 3));
      CHECK_FALSE(ContainsBoundary(cjk.LineBreakBoundaries(), 9));
      CHECK_FALSE(ContainsBoundary(cjk.LineBreakBoundaries(), 12));
      CHECK_FALSE(ContainsBoundary(cjk.LineBreakBoundaries(), 15));
      CHECK(ContainsBoundary(cjk.LineBreakBoundaries(), 18));
      CHECK(AllLineBreaksAreGraphemeAligned(thai));
      CHECK(AllLineBreaksAreGraphemeAligned(cjk));
  }
  ```

- [ ] **Step 1e: Add the failing invalid-locale test.**

  ```cpp
  TEST_CASE("invalid ICU locale fails without ad hoc analysis") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
      REQUIRE(buffer);
      CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
          *buffer, {"en--US", molga::text::BaseDirection::Auto}, sink));
      CHECK(HasDiagnostic(sink,
            molga::text::TextDiagnosticCode::LayoutInvalid));
  }
  ```

- [ ] **Step 1f: Add the failing fresh-process not-ready construction test.** Put this case in new `tests/test_unicode_not_ready.cpp`, registered through generic `molga_add_test` rather than `molga_add_text_test`; the executable has no staged-root definition/session main and therefore begins in `NeverInitialized`:

  ```cpp
  TEST_CASE("Unicode analyzer creates no ICU handle before runtime ready") {
      molga::text::VectorTextDiagnosticSink sink;
      auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
      REQUIRE(buffer);
      ResetIcuObjectCreationCountForTest();
      CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
          *buffer, {}, sink));
      CHECK(IcuObjectCreationCountForTest() == 0);
      CHECK(HasDiagnostic(sink,
            molga::text::TextDiagnosticCode::DependencyInvalid));
  }
  ```

  `UnicodeTextAnalyzerTestAccess` counts attempted ICU iterator/object creation;
  its test companion is linked only to this target. The target must not call
  `Initialize`, `Shutdown`, or `u_cleanup`, and process exit is its isolation.

- [ ] **Step 1g: Add the failing resolved-analysis identity test.** Analyze the same Thai paragraph twice and assert immutable, actual iterator identities rather than requested strings:

  ```cpp
  TEST_CASE("analysis records resolved locales rules and unique generation") {
      const auto first = AnalyzeFixture(u8"ภาษาไทย", {"th-TH", BaseDirection::Auto});
      const auto second = AnalyzeFixture(u8"ภาษาไทย", {"th-TH", BaseDirection::Auto});
      CHECK_FALSE(first.Identity().resolvedGraphemeLocale.empty());
      CHECK_FALSE(first.Identity().resolvedLineBreakLocale.empty());
      CHECK(first.Identity().graphemeRuleIdentity.size() == 64);
      CHECK(first.Identity().lineBreakRuleIdentity.size() == 64);
      CHECK(first.Identity().resolvedGraphemeLocale ==
            second.Identity().resolvedGraphemeLocale);
      CHECK(first.Identity().resolvedLineBreakLocale ==
            second.Identity().resolvedLineBreakLocale);
      CHECK(first.Identity().graphemeRuleIdentity ==
            second.Identity().graphemeRuleIdentity);
      CHECK(first.Identity().lineBreakRuleIdentity ==
            second.Identity().lineBreakRuleIdentity);
      CHECK(first.Identity().analysisGeneration !=
            second.Identity().analysisGeneration);
  }
  ```

- [ ] **Step 1h: Add the failing nonwrapping-generation test.** Through `UnicodeTextAnalyzerTestAccess::SetNextGeneration(UINT64_MAX)`, require one successful analysis with generation `UINT64_MAX`; the next `Analyze` returns `nullopt`, emits exactly one `LayoutInvalid`, and creates zero ICU objects. Restore the allocator to a fresh monotonic value through the same RAII test access before leaving the serialized case.

- [ ] **Step 1i: Implement exact item-expansion test helpers.** `EmbeddingLevelsByGrapheme` allocates one slot per adjacent grapheme-boundary pair and fills each slot from the single covering `AnalysisItem`, failing on overlap/gap. `ScriptAtGrapheme` returns that covering item's `scriptCode`. `ParagraphStartBytes` and `ParagraphEndBytes` collect the source begin/end from flagged items in logical order and remove only adjacent duplicates.

- [ ] **Step 1j: Implement exact boundary test helpers.** `ContainsBoundary` uses `std::binary_search`; `AllLineBreaksAreGraphemeAligned` requires every line boundary in the grapheme-boundary vector. `AnalyzeFixture(utf8, options={})` builds the public buffer, calls the public analyzer with a local collecting sink, requires both optionals, and returns the immutable analysis without calling ICU directly.

- [ ] **Step 1k: Attach direct ICU and register the isolated not-ready process.** Implement `UnicodeTextAnalyzerTestAccess` only in `tests/UnicodeTextAnalyzerTestAccess.{h,cpp}`; its private production seam counts attempted ICU-object creation and returns an RAII generation override that restores the prior test state after Step 1h. Add that companion source explicitly to both `test_unicode_text` and `test_unicode_not_ready`, and to no production/editor/runtime target. After the already-created session-backed `test_unicode_text` target, call `molga_attach_direct_icu_consumer(test_unicode_text)`. Register `test_unicode_not_ready` with generic `molga_add_test`, the same test-access companion, and no text-runtime session/root; do not mark either target serial merely for lifecycle, because each process owns an independent one-way state. Add configure assertions that the companion occurs exactly once in each test's `SOURCES` and zero times in every product target.

- [ ] **Step 2: Run the analysis red gate.**

  Run: `cmake --build --preset debug --target test_unicode_text test_unicode_not_ready -j`

  Expected: compile FAIL because `UnicodeAnalysis` and `UnicodeTextAnalyzer` do not exist.

- [ ] **Step 3: Add the exact analysis contracts.**

  ```cpp
  enum class BaseDirection : std::uint8_t { Auto, LeftToRight, RightToLeft };
  struct TextAnalysisOptions {
      std::string locale = "und";
      BaseDirection baseDirection = BaseDirection::Auto;
  };
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
          const UnicodeTextBuffer&, const TextAnalysisOptions&,
          TextDiagnosticSink&);
  };
  ```

- [ ] **Step 4: Enforce the analyzer ready gate.** Before constructing an ICU object, check `TextRuntimeDependencies::Get().IsReady()`; on false emit one `DependencyInvalid` and return `nullopt` so Step 1b records zero ICU object creations.

- [ ] **Step 4a: Implement ICU extended-grapheme boundaries.** Open the character `BreakIterator` for the canonical locale, check every `SourceBytesForUtf16` optional before dereference, and store monotonically increasing original-byte boundaries including `0` and byte length; a failed map emits typed invalid-input diagnostics and returns `nullopt`.

- [ ] **Step 4b: Capture resolved iterator locales.** After each `ubrk_open`, call `ubrk_getLocaleByType(iterator, ULOC_ACTUAL_LOCALE, &status)`, canonicalize the returned BCP-47 identity, normalize ICU's empty root result to `root`, and store the grapheme and line values separately. Any status/canonicalization failure emits `LayoutInvalid` and discards the candidate analysis.

- [ ] **Step 4c: Hash exact break-rule identities.** For each live iterator call the pinned C API `ubrk_getBinaryRules` first with `binaryRules=nullptr` and capacity `0`; require `U_SUCCESS` and a positive exact required byte count, allocate that count, then call it again and require `U_SUCCESS` plus the identical returned count. Hash the canonical byte stream `"molga-icu-break-rule-v2\0" + kind + "\0" + resolvedLocale + "\0" + decimal U_ICU_VERSION_MAJOR_NUM + ":" + decimal U_IS_BIG_ENDIAN + ":" + decimal U_CHARSET_FAMILY + "\0" + compiled rule bytes` with SHA-256 and store the lowercase 64-hex digest. This binds ICU 78's compiled binary-rule format, platform tuple, locale tailoring, and character-versus-line kind; do not switch to the C++ textual-rules API, identify rules by requested locale/version alone, or expose the binary bytes from `UnicodeAnalysis`.

- [ ] **Step 5: Implement ICU line opportunities.** Open the line `BreakIterator` for the same resolved locale, keep only opportunities aligned to extended-grapheme boundaries, preserve explicit separators, and source Thai/CJK opportunities only from ICU.

- [ ] **Step 6: Resolve scripts and script extensions.** Use `UScript` plus surrounding context for common/inherited scalars, preserve isolate/control boundaries, and never force neutral content to LTR merely because its script is common.

- [ ] **Step 7: Analyze paragraph BiDi levels.** Run `UBiDi` separately for each explicit paragraph using requested `Auto/LTR/RTL`; record every logical run's exact embedding level and base direction.

- [ ] **Step 8: Materialize deterministic `AnalysisItem`s.** Split at paragraph, logical BiDi, isolate/control, exact level, resolved script, language/style boundary; assign stable logical run IDs and never merge equal direction parity when levels differ.

- [ ] **Step 9: Implement ICU failure behavior.** Canonicalize the authored locale with `uloc_forLanguageTag`, require `parsedLength == locale.size()` and `U_SUCCESS`, then use ICU's returned locale for both BreakIterators. Malformed locale or any later ICU failure emits one `LayoutInvalid` and returns `nullopt`; no ASCII/ad-hoc analysis fallback is allowed.

- [ ] **Step 9a: Allocate a nonwrapping analysis generation.** Protect a process-wide `{next=1,exhausted=false}` allocator with a mutex. Reserve the generation before any ICU construction; issue `UINT64_MAX` exactly once, then latch exhaustion so all later analyses emit one `LayoutInvalid` and return `nullopt` before ICU calls. Never wrap to `0`, reuse a value, or reset in production; expose `UnicodeTextAnalyzerTestAccess::SetNextGeneration` only under the test build seam used by Step 1h.

- [ ] **Step 9b: Publish identity atomically with analysis.** Construct `UnicodeAnalysisIdentity` only after both actual locales and both rule digests are valid, and move it with boundaries/items into the immutable `UnicodeAnalysis`; no accessor derives mutable process locale or calls ICU later.

- [ ] **Step 10: Route the legacy decoder through the authoritative buffer.** `DecodeUtf8(std::string_view)` builds a `UnicodeTextBuffer` with a local sink and returns its sanitized scalar values. Mark it compatibility-only in `Utf8.h`; no production consumer migration occurs before Milestone 8.

- [ ] **Step 11: Run Debug and sanitizer green gates.**

  ```bash
  cmake --build --preset debug --target test_unicode_text test_unicode_not_ready test_text -j
  ctest --test-dir build/debug -R '^(test_unicode_text|test_unicode_not_ready|test_text)$' --output-on-failure
  cmake --preset asan && cmake --build --preset asan --target test_unicode_text -j
  ctest --test-dir build/asan -R '^test_unicode_text$' --output-on-failure
  cmake --preset ubsan && cmake --build --preset ubsan --target test_unicode_text -j
  ctest --test-dir build/ubsan -R '^test_unicode_text$' --output-on-failure
  ```

  Expected: all commands pass; output boundaries remain original UTF-8 byte offsets.

- [ ] **Step 12: Commit the Unicode/ICU analysis boundary.**

  ```bash
  git add CMakeLists.txt src/Text/UnicodeAnalysis.* src/Rendering/Utf8.* \
    tests/CMakeLists.txt tests/UnicodeTextAnalyzerTestAccess.* \
    tests/test_unicode_text.cpp tests/test_unicode_not_ready.cpp
  git commit -m "feat: analyze Unicode text with ICU"
  ```

**Exit:** Milestones 1–3 are independently reviewable and green. The next plan may assume exact source-byte clusters, ICU analysis items, portable dependency SHA, immutable ICU lifetime, and checked signed 26.6 units—nothing more.

## Subplan Completion Gate

- [ ] Run `git diff --check` and scan this plan without self-matching the pattern: `rg -n 'T[B]D|T[O]DO|F[I]XME|implement l[a]ter|similar t[o]' docs/superpowers/plans/2026-08-20-ui-text/01-dependencies-unicode.md`.
- [ ] Run the complete Exit Contract commands from a fresh configure and record the actual output; earlier baseline results are not completion evidence.
- [ ] Request code review for the full Milestones 1–3 range and resolve every blocker/high finding before starting Milestone 4.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-20-ui-text/01-dependencies-unicode.md`. Execute it with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`, then continue to `02-font-shaping-layout.md` only after the exit gate is green.
