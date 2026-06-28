# Runtime Script Packaging Plan

> Date: 2026-06-28
> Track: P0 runtime user script integration and package support.

## Problem

The editor has a user script compiler and `ScriptManager` can load dynamic libraries, but packaged runtime startup does not load user scripts before scene deserialization. A scene with user script components can only deserialize those components if the script factories were registered before `World::LoadFromFile`.

Current evidence:

- Runtime startup only calls `RegisterBuiltinScripts()` before scene load: `src/runtime_main.cpp:190-199`.
- `GameConfig` has no script manifest fields: `src/runtime_main.cpp:41-74`.
- `SceneSerializer` creates user scripts only through already registered `ScriptManager` factories: `src/Core/SceneSerializer.cpp:194-208`.
- `ScriptManager` already has dynamic load and validation hooks: `src/Scripting/ScriptManager.cpp:62-91`, `src/Scripting/ScriptManager.cpp:114-132`.
- `GameBuilder` packages assets, shaders, scenes, config, and executable, but no user script library: `src/Editor/GameBuilder.cpp:221-373`.
- `PathConstants` has project `Scripts` constants, but build output lacks a `Scripts` directory: `src/Core/PathConstants.h:14-18`.
- `ScriptCompiler` generated CMake expects an engine source/SDK root, while the editor currently passes executable dir: `src/Editor/Editor.cpp:188-191`, `src/Scripting/ScriptCompiler.cpp:154-162`.

## Target Architecture

Package layout should support an optional script library:

```text
MyGame/
  MyGame
  game.json
  Scenes/...
  Assets/...
  Shaders/...
  Scripts/UserScripts.{dylib,dll,so}
```

`game.json` should contain a script manifest:

```json
{
  "scripts": {
    "enabled": true,
    "library": "Scripts/libUserScripts.dylib",
    "apiVersion": 1,
    "buildHash": "source-or-library-hash"
  }
}
```

Runtime order:

```text
load game.json
-> RegisterBuiltinScripts()
-> load and validate scripts manifest library
-> load scene
-> SceneSerializer creates built-in and user script components
```

`SceneSerializer` should not become responsible for library loading. It should keep the current contract: if a component type is registered, it can instantiate it.

## Phases

### Phase 1. Manifest Model

- Add `GameConfig::scripts` fields for `enabled`, `library`, `apiVersion`, and `buildHash`.
- Parse missing script manifest as disabled for backward compatibility.
- Add unit tests for no-script and script-enabled config parsing.

### Phase 2. Runtime Loader

- Add `src/Scripting/ScriptPackageLoader.{h,cpp}`.
- Resolve package-relative script path from executable/package root.
- Validate library existence, required symbols, and API version before scene load.
- On manifest absence: continue.
- On enabled manifest failure: fail runtime startup and smoke report clearly.

### Phase 3. Compiler/SDK Path

- Redefine `ScriptCompiler::SetEnginePath` as engine source root or exported SDK root, not executable directory.
- Add generated CMake checks for `${MOLGA_ENGINE_PATH}/src` and required include directories.
- Add an API version export to generated `ScriptExports.cpp`.
- Long-term direction: converge on a `ScriptSDK` or `molga_script_api` boundary so user scripts do not include arbitrary engine internals.

### Phase 4. GameBuilder Packaging

- Add a `CopyUserScripts` step.
- Copy the latest successful user script library into `Scripts/`.
- Emit `game.json.scripts.library` only when a script library is packaged.
- If scenes reference user script types and the library is missing, fail the build.
- Keep packages with no user scripts valid.

### Phase 5. Validation and Smoke

- Extend `BuildManifest` and `PackageLayout` so script libraries are required only when script manifest is enabled.
- Add a smoke fixture with one user script component.
- Runtime smoke should report script library load status and created script component count.

## Subagent Plan

| Worker | Owns | Deliverables | Must avoid |
|---|---|---|---|
| A. Runtime loader | `runtime_main.cpp`, new `ScriptPackageLoader.*`, public `ScriptManager` calls | Script manifest load before scene load, runtime error/report path | `GameBuilder` implementation |
| B. Packaging | `GameBuilder.*`, `BuildManifest.*`, `PackageLayout.*`, `PathConstants.h` | `Scripts/` copy, `game.json` script fields, conditional validation | `ScriptManager` internals |
| C. Compiler/SDK | `ScriptCompiler.*`, `VSCodeIntegration.*`, generated CMake/text | Correct engine SDK path, API version symbol, compiler tests | Runtime bootstrap |
| D. Tests/Smoke | `tests/*`, `tests/smoke/*`, CMake test wiring | Unit tests and script package smoke fixture | Product logic except minimal fixtures |

Subagents should agree on the manifest schema before code changes. Worker A can implement loader tests with a fake manifest while Worker B implements builder output against the same schema.

## Tests

- `test_script_compiler`: generated CMake points at a real source/SDK root and exports `RegisterScripts` plus API version.
- `test_runtime_script_package_loader`: valid library, missing file, missing symbol, API mismatch.
- `test_game_builder`: user library is copied to `Scripts/` and `game.json` records a package-relative path.
- `test_package_layout`: script disabled package stays valid; script enabled package fails when library is missing.
- `smoke_end_to_end`: build a user-script scene, run packaged runtime, assert script load/create/update succeeded.

Suggested command:

```bash
ctest --preset debug -R "script|build_smoke|runtime_smoke|smoke_end_to_end" --output-on-failure
```

## Done Criteria

- A packaged game with a user script component runs without editor context.
- Runtime loads user script library before scene deserialization.
- Missing or incompatible script libraries fail loudly.
- Script-free packages remain compatible.
- `ScriptCompiler` no longer treats executable directory as engine source root.

## Risks

- C++ ABI stability is fragile if user scripts include internal engine headers directly.
- Windows DLL locking can affect hot reload and package rebuild behavior.
- Static `molga_core` linkage may require explicit exported symbols or a script API shared boundary.
- Last-good editor reload artifacts must not be confused with current package build artifacts.
