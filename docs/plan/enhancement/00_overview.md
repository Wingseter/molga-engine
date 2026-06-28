# Enhancement Plan Overview

> Date: 2026-06-28
> Scope: production-readiness gaps required for Molga Engine to become practical engine infrastructure, not only a playable editor slice.

## Goal

This directory tracks engine-level improvements that are larger than single UX features. The target is a production-usable 2D engine loop:

```text
author scripts/assets/scenes
-> build deterministic packages
-> run the same content in editor/runtime/package
-> observe performance and failures
-> ship across supported platforms with repeatable gates
```

The plans were split with subagent-sized ownership boundaries. Each track should be implementable by one or more workers without multiple agents editing the same files at the same time.

## Baseline

- Recent local verification before these planning docs: `ctest --preset debug` and `ctest --preset ubsan` passed after rebuild, 61 tests each.
- Build warnings still remain, including unused parameters/variables and reorder warnings. Production quality should not treat this as acceptable permanent noise.
- Existing user changes in `docs/plan/user_experience/*` are unrelated to this directory and should not be reverted while implementing these tracks.

## Tracks

| Priority | Document | Why it matters |
|---|---|---|
| P0 | `01_runtime_script_packaging.md` | Packaged games cannot depend only on built-in scripts. User script libraries must be loaded before scene deserialization. |
| P0 | `02_build_profile_package_layout.md` | `BuildProfile`, copied scene paths, `game.json`, and package validation must describe the same artifact. |
| P0 | `03_editor_undo_dirty.md` | A production editor cannot let Inspector/property edits bypass undo/redo and dirty state. |
| P1 | `04_render_queue_batching_profiler.md` | Per-sprite immediate rendering will not scale. Rendering needs a queue, batching, and useful frame statistics. |
| P1 | `05_box2d_physics_backend.md` | The custom physics solver is a prototype. Production behavior needs a proven backend, stable contacts, queries, and materials. |
| P1 | `06_asset_guid_runtime_catalog.md` | Runtime packages need GUID-based asset resolution without editor scans or source metadata mutation. |
| P2 | `07_platform_quality_ci.md` | CI, warnings, platform targets, package smoke tests, and release gates must be explicit and repeatable. |

## Recommended Order

1. Stabilize package identity first: runtime scripts and build layout.
2. Stabilize authoring correctness: undo/dirty and asset GUID catalog.
3. Replace scaling bottlenecks: render queue/batching and Box2D physics.
4. Lock quality gates: warning budget, platform matrix, package validation, release criteria.

The order is intentional. Rendering, physics, and platform work become harder to verify if packages and asset references are still ambiguous.

## Subagent Execution Model

Use subagents as implementation workers with disjoint ownership. The coordinator owns interface decisions, integration order, and final verification.

| Role | Responsibility | Rule |
|---|---|---|
| Interface worker | Adds shared structs/APIs and focused tests first. | Finishes before workers that consume the API. |
| Module worker | Owns one subsystem or small file set. | Does not edit unrelated windows, tests, or build scripts. |
| Integration worker | Replaces call sites after APIs exist. | Touches runtime/editor entry points only after module tests pass. |
| Verification worker | Adds regression tests, smoke fixtures, CI checks. | Avoids feature implementation except minimal failing fixtures. |
| Coordinator | Resolves naming, schema, feature flags, merge order. | Reviews for cross-track conflicts before final test runs. |

## Cross-Track Contracts

- `game.json` should become the runtime package contract for scenes, scripts, asset catalog, and development flags.
- `PackageLayout::Validate` should validate the same package contract that `GameBuilder` emits.
- `AssetDatabase` should have separate editor/build scan mode and runtime catalog load mode.
- Editor authoring changes should pass through `CommandHistory` or an explicit non-undoable policy.
- Runtime and Scene View rendering should share one render system instead of diverging loops.
- Physics should preserve public script callback semantics while replacing the solver internally.

## Verification Baseline

Every implementation track should end with at least:

```bash
cmake --build --preset debug
ctest --preset debug --output-on-failure
ctest --preset asan --output-on-failure
ctest --preset ubsan --output-on-failure
```

Track-specific smoke or CI checks are listed in each document.
