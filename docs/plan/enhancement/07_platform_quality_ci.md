# Platform, Quality, and CI Plan

> Date: 2026-06-28
> Track: P2 release gates and productization.

## Problem

The project has useful CI and tests, but production use requires stricter gates around warnings, platform coverage, package validation, runtime smoke artifacts, and target support. This is not a replacement for feature tracks; it is the release discipline that keeps those tracks from regressing.

Current evidence:

- `BuildProfile::Validate` supports only `target == "host"`: `src/Core/BuildProfile.cpp:47-50`.
- CI currently covers macOS debug/release with smoke, Linux unit build/test, and macOS sanitizers: `.github/workflows/ci.yml`.
- Build warnings remain locally after rebuild, even though tests pass.
- Package layout validation is narrow and currently tied to hardcoded entries: `src/Core/PackageLayout.cpp:9-15`.
- Runtime smoke exists, but it should grow with scripts, asset catalogs, render stats, and package tree artifacts.

## Target Architecture

Quality gates should become explicit:

```text
pull request
  -> configure/build debug/release on macOS + Linux
  -> unit tests
  -> smoke tests where platform supports it
  -> sanitizers
  -> package validation
  -> warnings budget
  -> upload failure artifacts

release candidate
  -> host package build
  -> package runtime smoke
  -> asset catalog validation
  -> script package validation
  -> platform target manifest
```

Target support should be staged:

1. `host`: current machine packaging.
2. `macos-x64/arm64` and `linux-x64`: reproducible build artifacts.
3. `windows-x64`: build and script DLL packaging.
4. App bundle/signing/notarization or installer work after engine package contracts stabilize.

## Phases

### Phase 1. Warning Budget

- Add `MOLGA_WARNINGS_AS_ERRORS` option.
- Apply it only to owned targets, not external dependencies.
- Fix current warning baseline before enabling in CI.
- Add CI job that can run warning-as-error once baseline is clean.

### Phase 2. CI Matrix Expansion

- Keep macOS debug/release and sanitizer jobs.
- Add Windows configure/build/unit tests.
- Add Linux smoke through `xvfb` or a headless runtime profile.
- Keep smoke tests isolated from sanitizer E2E if OpenGL drivers make sanitizer smoke unstable.

### Phase 3. Package Artifacts

- On smoke failure, upload:
  - `game.json`
  - package tree listing
  - smoke reports
  - `asset_catalog.json`
  - logs
  - script manifest if present
- Add package validation as an explicit CI step, not only implicit in smoke.

### Phase 4. Target Model

- Replace free-form `BuildProfile::target` with an enum-like known target model.
- Keep unsupported targets as clear validation errors.
- Add target capabilities: executable extension, dynamic library extension, bundle style, signing requirement, smoke support.
- Keep actual cross-compilation out of the first pass unless toolchains are available.

### Phase 5. Release Checklist

- Add docs for required local commands before merge/release.
- Require debug/release/sanitizer pass.
- Require package runtime smoke for host.
- Require no new warnings in owned code.
- Require migration notes for schema changes such as `game.json`, asset catalog, and script manifest.

## Subagent Plan

| Worker | Owns | Deliverables | Must avoid |
|---|---|---|---|
| A. Warning Policy | `CMakeLists.txt`, warning helper targets | `MOLGA_WARNINGS_AS_ERRORS`, owned-code-only warning flags | External dependency changes |
| B. CI Matrix | `.github/workflows/ci.yml`, CMake presets if needed | macOS/Linux/Windows jobs and clear artifact naming | Product behavior |
| C. Package Gate | `PackageLayout.*`, smoke scripts, test fixtures | Explicit package validation and artifact upload | BuildProfile redesign |
| D. Target Model | `BuildProfile.*`, `GameBuilder.*`, path/platform helpers | Known target capabilities and validation errors | CI workflow edits |
| E. Release Docs | `docs/plan/enhancement/*` or release checklist doc | Merge/release checklist | Code changes |

Coordinate C with runtime script and asset catalog tracks so CI validates the current package contract instead of duplicating it.

## Tests and Checks

```bash
cmake --build --preset debug
cmake --build --preset release
ctest --preset debug --output-on-failure
ctest --preset release -L unit -LE smoke --output-on-failure
ctest --preset asan -LE e2e --output-on-failure
ctest --preset ubsan -LE e2e --output-on-failure
```

CI-specific checks:

- macOS: debug/release unit and smoke.
- Linux: debug/release unit, then smoke under `xvfb` or headless profile.
- Windows: configure/build/unit, then script DLL/package checks once script packaging is stable.
- Artifact upload on failure for package tree and smoke reports.

## Done Criteria

- Owned code builds warning-clean.
- Warning-as-error can be enabled in CI without external dependency noise.
- CI covers macOS, Linux, and Windows at least for configure/build/unit tests.
- Host package smoke validates package layout, scripts when enabled, asset catalog when enabled, and runtime startup.
- Failure artifacts are enough to diagnose package failures without reproducing locally first.
- `BuildProfile::target` has documented supported values and target capability behavior.

## Risks

- Enabling warning-as-error before baseline cleanup will block unrelated work.
- Linux smoke can fail due to display/OpenGL environment rather than engine behavior unless headless setup is deliberate.
- Windows support will expose DLL naming, path separator, and file locking issues.
- Expanding CI too much at once can slow iteration; add jobs behind clear gates and keep high-signal artifacts.
