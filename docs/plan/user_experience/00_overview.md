# User Experience Plan Overview

> Date: 2026-06-21
> Scope: development convenience and editor workflow gaps, using commercial game engines as reference points.

## Goal

Molga Engine already has meaningful editor pieces: docked ImGui windows, Scene View FBO rendering, Hierarchy, Inspector, Project Browser, build profiles, script compilation, prefabs, and command objects for some hierarchy operations. The remaining UX problem is not only missing panels. The larger gap is that common authoring actions do not yet form a commercial-engine-style loop:

```text
Find asset -> place object -> select object -> manipulate in viewport
-> edit properties -> undo/redo safely -> save/play/build
-> inspect errors and performance without leaving the editor
```

This directory tracks that loop from a user-experience point of view. It should stay practical: each item must explain what a user can do after it is complete, what current Molga code blocks that experience, and what engineering boundary should own the fix.

## Reference Engines

Primary references checked on 2026-06-21:

- Unity Scene View: selecting, manipulating, and modifying GameObjects in the editor viewport.
  <https://docs.unity3d.com/Manual/UsingTheSceneView.html>
- Unity Undo API: property, object, hierarchy, component, and grouped undo behavior.
  <https://docs.unity3d.com/ScriptReference/Undo.html>
- Unity Console: severity filters, search, collapse, stack traces, clear-on-play/build/recompile, player logs.
  <https://docs.unity3d.com/Manual/Console.html>
- Unity Profiler: frame navigation and CPU, GPU, rendering, memory, audio, physics, file access, and asset loading modules.
  <https://docs.unity3d.com/Manual/ProfilerWindow.html>
- Unity Asset Database: source assets, imported counterparts, and refresh/import synchronization.
  <https://docs.unity3d.com/Manual/AssetDatabase.html>
- Unity Prefabs: reusable GameObject assets, nested prefabs, variants, overrides, unpacking, runtime instantiation.
  <https://docs.unity3d.com/Manual/Prefabs.html>
- Unreal Editor Interface: Level Viewport, Outliner, Details, Content Browser/Drawer, bottom toolbar, source control, tracing, Live Coding.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface>
- Unreal Viewport Controls: click selection, marquee selection, transform gizmos, keyboard shortcuts, snapping.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine>
- Unreal Content Browser: creating, importing, organizing, filtering, diagnosing, and migrating project assets.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine>
- Unreal Live Coding: background C++ rebuilds while editing, Play-In-Editor, or attached desktop builds continue.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime>
- Unreal Insights: trace capture, live sessions, CPU/GPU timing, memory, networking, UI, asset loading, and cooking analysis.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine>
- Godot editor/debug/file-path docs: editor docks, output/debugger/profiler panels, project-local `res://` and writable `user://` path model.
  <https://docs.godotengine.org/en/stable/tutorials/editor/index.html>
  <https://docs.godotengine.org/en/stable/tutorials/scripting/debug/overview_of_debugging_tools.html>
  <https://docs.godotengine.org/en/stable/tutorials/io/data_paths.html>

## Commercial-Engine UX Principles

1. **Viewport-first authoring**
   Users should be able to place, select, move, rotate, scale, frame, and inspect objects from the Scene View without switching mental modes.

2. **Every edit is recoverable**
   Object creation, deletion, parenting, component changes, inspector property edits, asset operations, prefab operations, and batch edits must enter one undo/redo system and mark dirty state consistently.

3. **One selection model**
   Hierarchy, Scene View, Inspector, Project Browser, Console source navigation, and context menus should read from one selection service instead of owning separate pointers.

4. **Diagnostics stay inside the editor**
   Build errors, script compiler output, missing assets, runtime exceptions, renderer warnings, and package validation failures should appear in a searchable Console with source links and severity filters.

5. **Assets are identities, not paths**
   Scenes, prefabs, materials, scripts, and build manifests should reference assets through stable IDs and importer metadata. Moving a texture should not silently break a Sprite.

6. **Slow work becomes a task**
   Build, package, import, script compile, asset scan, and shader reload should report progress and completion state without blocking the editor UI.

7. **Performance is observable**
   Developers need frame timing, CPU sections, renderer stats, asset load timing, memory growth, and build/package timings before they can improve complex scenes.

8. **Project and editor preferences are explicit**
   Shortcuts, snapping, layout, external tools, default build behavior, logging, and script compile policy should be discoverable and persistent.

## Current Molga Strengths

- Scene View already renders into an offscreen framebuffer and supports editor camera pan, zoom, frame-all, grid, and context creation.
- Hierarchy operations have command objects for create, delete, rename, reparent, and duplicate.
- Prefab commands exist for create, instantiate, apply, revert, and unpack.
- Project Browser supports folders, file grids, texture/audio drag sources, scene/script/folder creation, and prefab instantiation.
- Build settings have project build profiles and packaging validation work already started.
- Project Settings includes tags, layers, collision matrix, and sorting layers.
- Script Window supports template creation, VS Code integration, compile, and hot reload entry points.

## Current UX Risks

- Inspector component fields can mutate runtime data directly, bypassing undo/redo and dirty tracking.
- Selection is owned by Hierarchy plus ad hoc callbacks, not by a central editor service.
- Scene View has camera navigation and rendering, but no central picking, selection outline, transform gizmo, local/world tool mode, or snap command path.
- Logging writes to stdout/stderr only. There is no structured log sink, Console window, source navigation, clear-on-play/build/recompile, or editor/player log split.
- Project Browser deletes files directly and does not yet own safe rename/move/delete workflows, reference checks, or trash/recycle semantics.
- Asset references are still path-heavy; there is no general AssetDatabase with GUID/meta/importer state.
- Script compilation is invoked synchronously from UI code and is not connected to a unified task/console model.
- Performance visibility is limited to basic stats; there is no capture timeline or per-subsystem profiler.

## Recommended Order

```text
UX-1 Authoring control backbone
  -> SelectionService, transform gizmo, snap, inspector command edits, dirty guard

UX-2 Console and background tasks
  -> structured log sinks, Console window, source links, build/script/import task status

UX-3 Asset identity and safe Project Browser operations
  -> GUID/meta, incremental scan, importers, safe rename/move/delete, missing-reference UX

UX-4 Script iteration loop
  -> async compile, last-good library, field preservation, source diagnostics

UX-5 Profiler and trace captures
  -> CPU scopes, frame timeline, renderer stats, asset load/build timings

UX-6 Advanced production UX
  -> multi-object editing, prefab isolation mode, animation/tilemap authoring, command palette
```

The first three items should be treated as foundations. Adding advanced editors before these are in place will create new UI that repeats the same selection, undo, diagnostics, and asset-reference problems.

## Documents

- `01_commercial_engine_gap_analysis.md`: detailed workflow-by-workflow research and Molga gap analysis.

