# Editor Undo and Dirty State Plan

> Date: 2026-06-28
> Track: P0 authoring correctness.

## Problem

Editor authoring changes are not consistently routed through command history. Some workflows are undoable, but many Inspector and component edits mutate objects directly and sometimes mark dirty manually or not at all.

Current evidence:

- `Transform` has a command path through `Editor::SubmitTransformEdit`: `src/ECS/Components/Transform.cpp:88-109`.
- `TransformCommand` can mark the editor scene modified: `src/Editor/Commands/TransformCommand.cpp:19-35`.
- `InspectorWindow` directly mutates name, tag, layer, and active state: `src/Editor/Windows/InspectorWindow.cpp:67-129`.
- `InspectorWindow` directly adds built-in components and scripts: `src/Editor/Windows/InspectorWindow.cpp:208-268`.
- `SpriteRenderer::OnInspectorGUI` directly mutates texture path, GUID, size, color, sorting, and material fields: `src/ECS/Components/SpriteRenderer.cpp:144-285`.
- Sprite object creation executes `CreateObjectCommand` then directly adds `SpriteRenderer`: `src/Editor/Windows/HierarchyWindow.cpp:222-229`.
- `DuplicateObjectCommand` only copies selected Transform and SpriteRenderer fields: `src/Editor/Commands/ObjectCommands.cpp:121-149`.

## Target Architecture

All save-affecting editor changes should go through either:

- `CommandHistory`, when undoable.
- An explicit non-undoable editor operation that documents why it cannot be undone.

Key model:

```text
CommandHistory
  Execute(command)
  Undo()
  Redo()
  MarkClean()
  IsDirty()

EditorChangeService
  BeginLiveEdit()
  PreviewMutate()
  CommitAlreadyApplied()
  CancelRollback()
```

For component properties, prefer snapshot commands:

```text
before = component.Serialize() + enabled flag
after  = component.Serialize() + enabled flag
ComponentSnapshotCommand(objectId, componentType, before, after)
```

This avoids hand-writing one command for every field and works for scripts once their type is registered.

## Phases

### Phase 1. Dirty/Clean Marker

- Extend `CommandHistory` with clean marker semantics.
- `Save`, `Open`, and `New` reset the clean marker.
- Undo/redo returning to clean marker means dirty false.
- Redo branch truncation invalidates clean marker if it discards the saved state.

### Phase 2. Snapshot Utilities

- Add `Editor/Commands/SceneSnapshots.{h,cpp}`.
- Capture and restore GameObject/component JSON using existing serializers and `ComponentFactory`/`ScriptManager` fallback.
- Include component `enabled` state.
- Add safe object lookup by ID.

### Phase 3. Property and Component Commands

- Add `GameObjectPropertyCommand` for name/tag/layer/active.
- Add `ComponentSnapshotCommand` for Transform, SpriteRenderer, script fields, and enabled state.
- Add `ComponentAddCommand` and `ComponentRemoveCommand`.
- Add `CreateObjectWithComponentsCommand` for Sprite/Tilemap creation workflows.

### Phase 4. Inspector Migration

- Move `InspectorWindow` GameObject header edits to commands.
- Move component add/script add to commands.
- For drag widgets, emit a single command on edit commit rather than a command every frame.
- Keep current UI behavior, but change mutation ownership.

### Phase 5. SpriteRenderer and Material Migration

- Wrap `SpriteRenderer::OnInspectorGUI` edits in component snapshot commits.
- On apply, restore `textureGuid`/`texturePath` and force asset resolution where needed.
- Treat GPU pointers as derived cache, not serialized command state.

### Phase 6. Deep Duplicate

- Replace manual Transform/SpriteRenderer copy with subtree serialization plus new-ID remapping.
- Preserve scripts, material fields, texture GUIDs, children, prefab data, and external object refs.
- Add regression tests for duplicate independence.

### Phase 7. Play/Edit Policy

- Define whether Inspector can edit play-world state.
- If allowed, edits are runtime preview and not scene dirty.
- If disallowed, UI should block or clearly route edits to edit-world only.

## Subagent Plan

| Worker | Owns | Deliverables | Must avoid |
|---|---|---|---|
| A. Dirty/History | `CommandHistory.h`, `SceneOperations.*`, `Editor.*` | Clean marker API and save/open/new integration | Inspector migration |
| B. Snapshot Commands | `Editor/Commands/*`, serializer helpers | Snapshot capture/restore, component add/remove commands | Large serializer rewrites |
| C. Inspector Migration | `InspectorWindow.*`, `Transform.cpp` | Header edits, component add, script field changes through commands | `SpriteRenderer.cpp` |
| D. Sprite/Material Properties | `SpriteRenderer.*`, `Material.*` | Undoable SpriteRenderer and material property edits | `InspectorWindow.*` |
| E. Object Workflow | `ObjectCommands.*`, `HierarchyWindow.cpp`, `SceneViewWindow.cpp`, `CreateSpriteFromAssetCommand.*` | Deep duplicate and create-with-components | Component property UI |
| F. Tests | command, serializer, scene operation tests | Dirty/undo regression suite | Product behavior except fixtures |

The coordinator should merge A and B first. C, D, and E should not run before command APIs stabilize.

## Tests

- `CommandHistory`: execute, undo, redo, redo truncation, mark clean, undo-to-clean.
- `GameObjectPropertyCommand`: name/tag/layer/active undo and dirty state.
- `ComponentSnapshotCommand`: Transform, SpriteRenderer size/color/flip/sorting/material, component enabled.
- `ComponentAddCommand`: built-in component and dynamic script add/remove.
- `DuplicateObjectCommand`: subtree, scripts, material, texture GUID, object reference remap.
- Manual smoke: edit Inspector fields, undo/redo, save, confirm dirty marker clears.

## Done Criteria

- Save-affecting authoring actions are undoable or explicitly non-undoable.
- Save makes dirty false; edit makes dirty true; undo back to saved state makes dirty false.
- Sprite/Tilemap creation is a single undoable operation.
- Duplicate preserves all serializable object state.
- Existing command/history/serializer tests and new dirty tests pass.

## Risks

- ImGui live edits require preview mutation, so commit/cancel boundaries must be explicit.
- Texture and GPU resources must be rebuilt from GUID/path after snapshot restore.
- Dynamic script component restore fails if the script type is not registered.
- Prefab override tracking can conflict with snapshot restore and needs separate regression coverage.
