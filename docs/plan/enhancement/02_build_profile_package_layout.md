# Build Profile and Package Layout Plan

> Date: 2026-06-28
> Track: P0 package contract consistency.

## Problem

`BuildProfile`, copied scene files, generated `game.json`, and `PackageLayout::Validate` do not share one source of truth.

Current evidence:

- `BuildProfile` defaults to `Scenes/main.json`: `src/Core/BuildProfile.h:20-21`.
- `BuildProfile::Validate` checks `startupScene in scenes`, but not package path mapping: `src/Core/BuildProfile.cpp:35-42`.
- `BuildProfile::Validate` rejects every target except `host`: `src/Core/BuildProfile.cpp:47-50`.
- `GameBuilder::CopyScenes` flattens every scene to `Scenes/<filename>`: `src/Editor/GameBuilder.cpp:259-272`.
- `GameBuilder::GenerateGameConfig` records startup scene by filename: `src/Editor/GameBuilder.cpp:281-290`.
- `PackageLayout::Validate` hardcodes `Scenes/main.json`: `src/Core/PackageLayout.cpp:9-15`.
- Runtime loads the scene path from `game.json.mainScene`: `src/runtime_main.cpp:196-199`.

## Target Architecture

Introduce one package plan object:

```text
BuildProfile + projectRoot + target
  -> BuildPlan
       executableName
       sceneEntries[]
         sourceProfilePath
         sourceAbsolutePath
         packagePath
       requiredDirectories
       optionalScriptLibrary
       assetCatalogPath
```

Rules:

- `BuildProfile` remains the user-facing declaration.
- `BuildPlan` resolves paths, checks project-root containment, detects collisions, and defines package paths.
- `CopyScenes`, `GenerateGameConfig`, `BuildManifest`, and `PackageLayout` consume the same `BuildPlan`.
- Scene subdirectories are preserved under `Scenes/...`; filename flattening is removed.
- `target = "host"` may remain the only supported target, but its runtime executable resolution should be centralized.

## Phases

### Phase 1. Regression Tests First

- Non-default `startupScene = "Scenes/levels/start.json"` must produce matching file and `game.json.mainScene`.
- Two scenes with same filename in different folders must either preserve structure or fail with a collision error before copying.
- `PackageLayout::Validate` must validate the actual configured startup scene, not `Scenes/main.json`.

### Phase 2. BuildPlan Builder

- Add `BuildPlan` and `BuildPlanBuilder` in `src/Core` or `src/Editor`.
- Normalize paths with `/`.
- Reject absolute or project-root escaping paths unless a deliberate external-asset policy is created.
- Detect duplicate package paths before copying.

### Phase 3. GameBuilder Consumption

- Build `BuildPlan` at the start of `GameBuilder::Build`.
- Replace `CopyScenes` filename logic with `sceneEntry.packagePath`.
- Generate `game.json.mainScene` and `game.json.scenes[]` from the same mapping.
- Populate `BuildManifest` from the plan.

### Phase 4. PackageLayout Contract

- Add `PackageLayoutSpec` or make `Validate` read `game.json` after basic file existence.
- Validate executable, `game.json`, required directories, and every scene listed by the package contract.
- Keep the existing overload temporarily if tests or older call sites still use it, but mark it as legacy.

### Phase 5. Editor and Runtime Alignment

- Project open should use configured startup scene where appropriate instead of assuming `Scenes/main.json`.
- Runtime default `mainScene` fallback should use `Scenes/main.json` casing for consistency.
- Missing `game.json` or missing configured main scene in a package should be a startup error, not silent fallback.

## Subagent Plan

| Worker | Owns | Deliverables | Integration dependency |
|---|---|---|---|
| A. Profile/Layout API | `BuildProfile.*`, `PackageLayout.*`, `PathConstants.h` | `BuildPlan`/`PackageLayoutSpec`, unit tests | Finishes public structs first |
| B. Builder | `GameBuilder.*`, `BuildManifest.*` | Plan-driven copying, config generation, manifest | Depends on A headers |
| C. Editor/Runtime | `src/main.cpp`, `runtime_main.cpp`, `BuildManager.cpp` | Startup scene alignment and clear errors | Depends on plan semantics |
| D. Verification | `tests/*`, `tests/smoke/*`, CI smoke commands | Non-main scene smoke, collision cases | Runs after A/B/C |

No worker should reformat all build-related files. Path mapping is the shared interface.

## Tests

```bash
ctest --preset debug -R "test_build_profile|test_game_builder|build_smoke|runtime_smoke|smoke_end_to_end" --output-on-failure
ctest --preset asan -LE e2e --output-on-failure
ctest --preset ubsan -LE e2e --output-on-failure
```

Additional static check:

```bash
rg -n "Scenes/main\\.json|filename\\(\\).*scene|PackageLayout::Validate" src tests
```

Any remaining `Scenes/main.json` should be a documented default, not a validation shortcut.

## Done Criteria

- `PackageLayout::Validate` no longer hardcodes `Scenes/main.json`.
- `CopyScenes`, `GenerateGameConfig`, and `BuildManifest` use the same scene mapping.
- Package files, `game.json.mainScene`, `game.json.scenes[]`, and `BuildProfile.scenes` match.
- Duplicate package scene paths are detected before partial package output.
- `target = "host"` remains explicit and its unsupported-target errors are clear.

## Risks

- Existing tests and fixtures are likely tied to `Scenes/main.json`.
- Case sensitivity can pass on macOS and fail on Linux.
- Over-generalizing future targets can slow down host package stabilization.
- Runtime script and asset catalog plans also need package layout fields; coordinate schema names before implementation.
