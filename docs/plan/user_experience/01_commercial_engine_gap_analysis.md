# Commercial Engine UX Gap Analysis

> Date: 2026-06-21
> Focus: development convenience, editor authoring flow, and diagnostic visibility.

## Summary

Commercial engines converge on the same practical authoring loop:

```text
Viewport + Outliner/Hierarchy + Inspector/Details + Content Browser
+ Undo/Redo + Console + Profiler + Preferences + Build/Play controls
```

Molga already has most of these surfaces, but several surfaces are not backed by shared editor services. The next UX pass should avoid adding isolated panels. It should introduce the services that make panels behave consistently:

- `SelectionService`
- `EditorCommand` coverage for inspector/component/project edits
- structured `Log` with sinks
- background `TaskService`
- asset identity/index service
- profiler event capture

## 1. Scene View, Selection, and Transform Tools

### Commercial Pattern

Unity and Unreal treat the viewport as the primary authoring surface. The user can select objects directly, use transform handles, switch move/rotate/scale tools, snap to grid or increments, and see Hierarchy/Outliner and Inspector/Details update from the same selection.

Unreal makes this especially explicit: the viewport toolbar groups transform, snapping, camera, visualization, performance, and viewport options; clicking an object in the viewport selects it; the selected object is highlighted in the Outliner and shown in the Details panel.

### Current Molga State

Implemented:

- Scene View FBO rendering: `src/Editor/Windows/SceneViewWindow.{h,cpp}`
- editor camera pan/zoom/frame-all
- grid rendering
- context creation at right-click world position
- object rendering sorted by `sortingOrder`

Gaps:

- No central viewport picking path is declared in `SceneViewWindow.h`.
- No transform gizmo state or tool mode is present in `SceneViewWindow.h`.
- Selection lives in `HierarchyWindow` and `Editor::GetSelectedObject()` reads it back from that panel.
- There is no shared multi-selection model.
- There is no selection outline, locked Inspector target, local/world mode, or snap preference.

### Required Direction

Create `SelectionService` as an editor-level model:

```text
selected object IDs
primary selected object ID
selection change event
selection source
lockable inspector target
```

Move Scene View picking into a tested service boundary:

```text
viewport position -> world position -> candidates -> ordered hit result -> SelectionService
```

Add a 2D `TransformGizmo`:

```text
tool: select / move / rotate / scale
space: local / world
snap: off / grid / increment
drag lifecycle: begin snapshot -> live preview -> commit one command
```

### Completion Criteria

- Clicking a sprite in Scene View selects it and updates Hierarchy and Inspector.
- Dragging a transform handle creates one undo step, not one per frame.
- Inspector numeric edits and gizmo edits use the same command semantics.
- Selection survives Play/Stop when the edit world object still exists.
- Multi-selection can be introduced without changing every panel API again.

## 2. Undo/Redo and Dirty State

### Commercial Pattern

Unity's Undo API models edits at object/property/hierarchy/component levels and groups edits based on editor events. The key UX behavior is not the exact implementation; it is that every visible edit can be reverted and has a useful action name.

### Current Molga State

Implemented:

- `CommandHistory`: `src/Editor/Commands/CommandHistory.h`
- object create/delete/rename/reparent/duplicate commands
- prefab create/instantiate/apply/revert/unpack commands
- `SceneOperations` dirty flag

Gaps:

- `Transform::OnInspectorGUI()` changes position/rotation/scale directly.
- Component add operations in Inspector are direct `AddComponent` calls.
- Component reset/remove/copy/paste is not a command surface.
- Project Browser file deletion bypasses command history and confirmation.
- Dirty state is manually marked in scattered UI paths.
- Command names are not exposed in menu labels or history UI.

### Required Direction

Make edits enter a command layer before they mutate project state:

```text
TransformCommand
ComponentAddCommand
ComponentRemoveCommand
ComponentResetCommand
ComponentPasteCommand
ProjectFileCreateCommand
ProjectFileRenameCommand
ProjectFileMoveCommand
ProjectFileDeleteCommand
BuildProfileEditCommand
ProjectSettingsEditCommand
```

Add an editor mutation contract:

```text
UI may request edits.
Commands perform edits.
Commands mark dirty.
Commands update selection when needed.
Undo/redo runs through CommandHistory.
```

### Completion Criteria

- Every visible Inspector edit can be undone.
- Undo/redo state is shown in menu text with the next command name.
- Scene dirty state changes only through document/command boundaries.
- New/Open/Close/Exit/Project switch cannot lose unsaved edits silently.
- File delete/rename/move has confirmation and undo or trash behavior.

## 3. Console, Logs, and Diagnostics

### Commercial Pattern

Unity's Console groups errors, warnings, and messages; provides search and severity filters; supports collapse; clears on play/build/recompile; shows details and stack/source links. Unreal exposes Output Log, command entry, tracing, and Live Coding status from its editor bottom toolbar.

### Current Molga State

Implemented:

- `Log::Info/Warn/Error` functions.
- Most systems already report through `Log`.
- Script compile output is retained in `ScriptCompiler` and displayed inside `ScriptWindow`.
- Build errors are visible in the build window and stdout/stderr.

Gaps:

- `Log` writes directly to stdout/stderr with no sink model.
- No Console window exists.
- No severity/category/time/source metadata.
- Script compiler output, build output, runtime package smoke failures, missing asset warnings, and renderer warnings are not unified.
- No clear-on-play/build/recompile settings.
- No source navigation for compiler diagnostics.

### Required Direction

Introduce a structured logger:

```text
LogMessage {
  sequence, timestamp, threadId,
  severity, category,
  message,
  sourceFile, sourceLine,
  externalPath, externalLine,
  context: Editor | Runtime | Build | ScriptCompiler | Importer
}
```

Add sinks:

```text
StdoutSink
RingBufferSink
FileSink
EditorConsoleSink
SmokeReportSink
```

Add `ConsoleWindow`:

```text
toolbar: clear, collapse, clear on play/build/recompile, error pause
filters: search text, severity, category, context
list: virtualized rows, repeated-message count
detail: full message, stack/source lines, copy/open file actions
```

### Completion Criteria

- Build, script, runtime, renderer, asset, and package errors appear in one Console.
- 100k log messages do not exceed a configured memory cap.
- Compiler errors open the corresponding source file and line.
- CI/smoke failures write the same structured messages to stdout and report files.
- A log from a background task cannot mutate ImGui state directly.

## 4. Background Tasks and Script Iteration

### Commercial Pattern

Unreal Live Coding allows C++ rebuilds while the editor or Play-In-Editor keeps running, with a dedicated status console and notification. The practical lesson for Molga is not to match Live++ immediately; it is to make slow operations non-blocking and observable.

### Current Molga State

Implemented:

- `ScriptWindow` can create templates, call compile, and hot reload.
- VS Code setup and file opening exist.
- `ScriptCompiler` captures compile output.

Gaps:

- UI calls `compiler.Compile()` synchronously.
- No background task progress model.
- No cancellation.
- No last-good dynamic library policy documented at the UI level.
- No automatic source diagnostics in Console.
- Play-mode compile/reload policy is unclear.

### Required Direction

Create `EditorTaskService`:

```text
TaskId
name
category: Build | ScriptCompile | ScriptReload | Import | Package | ShaderReload
state: Queued | Running | Succeeded | Failed | Cancelled
progress
log stream
result payload
```

Script compile flow:

```text
request compile -> task runs process -> stdout/stderr to Console
-> success validates library -> unload/reload at safe point
-> preserve serialized script fields
-> failure keeps last-good library active
```

### Completion Criteria

- Script compile does not block editor rendering.
- Compile status is visible in Console and task status UI.
- Failed compile leaves the current script runtime usable.
- Reload invalidates stale script pointers at a documented safe point.
- Play-mode compile behavior is explicit: blocked, queued until Stop, or allowed with reload.

## 5. Project Browser and Asset Identity

### Commercial Pattern

Unity separates source assets from imported data through the Asset Database. Unreal's Content Browser is the primary place to create, import, organize, filter, diagnose, and migrate project assets. Godot keeps project paths stable with project-local resource paths and separate writable user paths.

### Current Molga State

Implemented:

- Project Browser displays an Assets tree and file grid.
- Drag sources exist for textures and audio.
- Components accept texture/audio drag targets.
- Script, folder, and scene creation exist.
- Prefab instantiation from `.prefab` exists.

Gaps:

- Asset references are still primarily file paths.
- No general GUID/meta/importer database.
- No incremental file watcher/index refresh.
- No asset dependency graph for scenes, prefabs, materials, audio, tilemaps, scripts.
- Rename/move/delete do not update references.
- Direct delete uses `std::filesystem::remove`.
- No missing reference UI or import error badges.

### Required Direction

Add asset identity before expanding asset-heavy tools:

```text
GUID
.meta sidecar
AssetRecord { guid, sourcePath, importer, importerVersion, artifactPath, dependencies }
AssetIndex { guid -> record, sourcePath -> guid }
Importer interface
MissingAsset placeholder
```

Project Browser should become a safe asset operation surface:

```text
create/import/rename/move/delete commands
reference check before destructive actions
trash/recycle or reversible delete
badges for missing/import-failed/dirty/generated
search and type filters
drag-to-scene to create Sprite/Audio/etc.
```

### Completion Criteria

- Moving or renaming a texture does not break saved Sprite references.
- Missing assets show a visible placeholder and Console warning.
- Build can collect only assets referenced by selected scenes and prefabs.
- Asset delete warns about references and is reversible or goes through trash.
- Importer version changes reimport only affected assets.

## 6. Inspector and Authoring UX

### Commercial Pattern

Unity Inspector and Unreal Details expose object properties, components, filtering, multi-object editing, locked inspection, reset/copy/paste, prefab override state, and direct connection to undo.

### Current Molga State

Implemented:

- Inspector edits object name, active, tag, layer.
- Component sections call `OnInspectorGUI`.
- Add Component popup exists.
- Prefab instance apply/revert/unpack controls exist.

Gaps:

- Component UI owns mutation details, which makes undo/dirty consistency hard.
- No component context menu for remove, reset, copy, paste, move up/down.
- No property mixed-value state for multi-selection.
- No inspector search/filter.
- No locked Inspector target.
- Prefab overrides are listed, but property-level override editing is not yet a full authoring workflow.

### Required Direction

Split component editor UI from data mutation:

```text
ComponentEditor draws fields.
PropertyAdapter reads/writes serializable fields.
EditorCommand commits value changes.
Inspector owns context menus and common component operations.
PrefabOverrideTracker marks changed values.
```

### Completion Criteria

- Component fields can be edited, undone, copied, pasted, reset, and removed consistently.
- Multi-selection displays shared fields and mixed values.
- Inspector can lock to one object while selection changes elsewhere.
- Prefab instance overrides are visible at property level and can be reverted individually.

## 7. Profiler and Performance Diagnostics

### Commercial Pattern

Unity Profiler and Unreal Insights both treat performance as captured data, not as a single FPS label. They provide frame navigation, module/timeline views, and live or saved captures. Unreal also separates trace recording from analysis.

### Current Molga State

Implemented:

- Stats window exists.
- Renderer and build tests provide some correctness coverage.

Gaps:

- No named CPU scopes.
- No frame capture ring buffer.
- No renderer stats panel for draw calls, batches, texture binds, shader switches, FBO resizes.
- No asset load timing.
- No build/package timing breakdown.
- No saved performance capture format.

### Required Direction

Start small with in-process profiling:

```text
ProfileScope(name, category)
FrameProfile { frameIndex, dt, scopes, counters }
ProfilerService ring buffer
ProfilerWindow timeline + selected frame details
Counters: draw calls, sprites, particles, text, tile chunks, asset loads, scripts, physics
```

Later, evolve into trace capture files if needed.

### Completion Criteria

- A slow frame can be expanded into named CPU sections.
- Renderer stats are visible while editing and in Play mode.
- Build/package steps report durations in Console/task details.
- Asset load warnings identify the asset and calling subsystem.

## 8. Preferences, Shortcuts, and Layout

### Commercial Pattern

Unreal and Unity expose editor preferences, keyboard shortcuts, snapping values, layout save/reset, external tool settings, play/build behavior, and project settings separately.

### Current Molga State

Implemented:

- ImGui docking default layout.
- Project Settings window for tags/layers/collision/sorting layers.
- Build Settings window for build profile fields.
- Some keyboard shortcuts are displayed in menus.

Gaps:

- Shortcuts are menu labels more than a central command binding system.
- Layout reset exists, but persistent layout save/restore is unclear.
- No Editor Preferences distinction from Project Settings.
- Snap, selection, console, script compile, external editor, logging, and task behavior have no persistent preference model.

### Required Direction

Create:

```text
EditorPreferences
EditorShortcutRegistry
EditorLayoutService
CommandPalette
```

Keep project-facing data in `ProjectSettings` and machine/user-facing data in editor preferences.

### Completion Criteria

- Shortcuts invoke the same commands as menus and toolbar buttons.
- User layout survives restart and can be reset safely.
- Snap increments, external editor, console behavior, and script compile policy are editable and persistent.
- Command Palette can search and execute registered editor commands.

## 9. Proposed Milestones

### UX-1: Authoring Control Backbone

Includes:

- `SelectionService`
- Scene View picking
- selection outline
- transform gizmo
- snap settings
- `TransformCommand`
- inspector transform edits through commands

Exit scenario:

```text
User clicks a Sprite in Scene View, moves it with the gizmo, edits its position in Inspector,
undoes both changes, redoes both changes, saves, and sees dirty state clear.
```

### UX-2: Console and Task Status

Includes:

- structured log messages
- sink model
- `ConsoleWindow`
- background task model
- script/build task output routed to Console

Exit scenario:

```text
User triggers a script compile error, sees it in Console, filters to errors,
opens the source line, fixes it, recompiles in the background, and continues editing.
```

### UX-3: Asset Browser and Identity

Includes:

- GUID/meta/index
- importer interface
- safe Project Browser operations
- missing reference UX
- drag texture to Scene View to create Sprite

Exit scenario:

```text
User drags a texture into Scene View, saves, renames the texture in Project Browser,
reloads the scene, and the Sprite still resolves.
```

### UX-4: Script Iteration

Includes:

- async script compile
- last-good library
- field preservation
- reload-safe pointer invalidation
- play-mode compile policy

Exit scenario:

```text
User changes gameplay script while editor is open, compiles without blocking UI,
reloads successfully, and field values on scene objects remain intact.
```

### UX-5: Profiler and Trace Lite

Includes:

- `ProfileScope`
- frame ring buffer
- profiler panel
- renderer/build/asset counters

Exit scenario:

```text
User notices low FPS, opens Profiler, selects a frame, and sees whether time is spent
in scripts, rendering, asset loading, physics, or editor UI.
```

### UX-6: Advanced Production UX

Includes:

- multi-object editing
- component copy/paste/reset/remove
- prefab isolation/edit mode
- command palette
- animation and tilemap authoring panels

Exit scenario:

```text
User edits a group of objects, copies component settings, applies prefab overrides,
and returns to normal scene editing without data loss.
```

## 10. Near-Term Non-Goals

- Do not build an advanced animation editor before selection, undo, console, and asset identity are stable.
- Do not add large-scale profiler trace files before an in-process ring buffer proves the event model.
- Do not implement deep prefab variants before property IDs, asset GUIDs, and inspector command edits are stable.
- Do not add editor plugins before command, selection, logging, and task APIs are documented.

## 11. Alignment With Existing Roadmap

This UX plan does not replace `docs/plan/playable-editor-vertical-slice/phase-1-3_roadmap.md`. It reframes its Phase 1 items around commercial-engine user experience:

- Milestone 1-1 and 1-2 map to UX-1.
- Milestone 1-4 maps to UX-2.
- Milestone 1-5 maps to UX-4.
- Phase 2-1 maps to UX-3.
- Phase 3-2 maps to UX-5.

The sequencing recommendation is stricter than the older roadmap: **selection + undo + console + asset identity** should precede new high-level authoring panels.

