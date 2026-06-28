# Render Queue, Batching, and Profiler Plan

> Date: 2026-06-28
> Track: P1 rendering scalability.

## Problem

Rendering is still mostly component-immediate. Runtime and Scene View collect components into a local vector, sort by `sortingOrder`, and call `RenderSprite` on every component. This makes batching, material sorting, culling, and profiling difficult.

Current evidence:

- Runtime builds a `drawList` every frame and sorts only by sorting order: `src/runtime_main.cpp:237-259`.
- Runtime calls `comp->RenderSprite(renderer.get())` per component: `src/runtime_main.cpp:274-289`.
- Scene View has the same local `drawList` pattern: `src/Editor/Windows/SceneViewWindow.cpp:351-411`.
- `SpriteRenderer::RenderSprite` applies material, creates a temporary `Sprite`, and calls `Renderer::DrawSprite`: `src/ECS/Components/SpriteRenderer.cpp:28-65`.
- `Material::Apply` sets shader, uniforms, textures, and blend state per sprite: `src/Rendering/Material.cpp:14-69`.
- `Renderer::DrawSprite` sets uniforms, binds texture, and issues `glDrawArrays` per sprite: `src/Rendering/Renderer.cpp:101-128`.
- `Renderer::SetShader` increments `shaderSwitches` on every call, not only real switches: `src/Rendering/Renderer.cpp:69-78`.
- `RenderStats::batches` exists but Scene View currently sets it equal to draw calls: `src/Core/Profiling/FrameProfile.h:32-40`, `src/Editor/Windows/SceneViewWindow.cpp:414-417`.

## Target Architecture

```text
World / Components
  -> CollectRender(RenderQueue&)
  -> RenderQueue: frame-local commands
  -> SortKey: visual order
  -> BatchKey: compatible GPU state
  -> RenderSystem2D
       -> SpriteBatcher for batchable sprites
       -> legacy Renderer::DrawSprite fallback
  -> ProfilerService + RenderStats
```

Separate the two keys:

- `SortKey`: camera pass, sorting layer, sorting order, depth/y-sort, submission index.
- `BatchKey`: shader, texture, blend mode, material layout, sampler state.

Do not let batching reorder transparent sprites across visual-order boundaries.

## Phases

### Phase 0. Statistics Baseline

- Define `drawCalls`, `batches`, `textureBinds`, `shaderSwitches`, and `submittedSprites`.
- Make `Renderer::SetShader` count only real shader switches.
- Add future fields: `submittedCommands`, `batchFlushes`, `batchBreaks`, `maxSpritesPerBatch`, `verticesUploadedBytes`, `queueSortNanos`.

### Phase 1. RenderQueue

- Add `RenderCommand`, `RenderQueue`, `SortKey`, and `BatchKey`.
- Keep first integration as queue plus legacy draw fallback.
- Replace runtime and Scene View local `drawList` with queue submit/sort.

### Phase 2. SpriteRenderer Submission

- Add `SpriteRenderer::BuildRenderData` or `CollectRender(RenderQueue&)`.
- Move temporary sprite data construction out of GL draw path.
- Add material APIs such as `ResolveShader`, `GetBatchKey`, and `ApplyForBatchStart`.
- Mark custom per-sprite uniform materials as non-batchable until vertex data support exists.

### Phase 3. SpriteBatcher

- Add dynamic VBO/static EBO sprite batcher.
- First target: same texture, same shader, same blend mode.
- Flush on batch key change or buffer full.
- Keep texture atlas as a later optimization, not a required first step.

### Phase 4. RenderSystem2D

- Add `RenderSystem2D::Render(queue, renderer, camera)`.
- Runtime and Scene View both call this shared system.
- Preserve `RenderPass` RAII boundaries.
- Keep legacy `Renderer::DrawSprite` for fallback and debug.

### Phase 5. Profiling

- Add profiling scopes around queue collection, sorting, and batch flushing.
- Expose batch efficiency in `ProfilerWindow`.
- Add runtime smoke report fields for render stats where possible.

## Subagent Plan

| Worker | Owns | Deliverables | Must avoid |
|---|---|---|---|
| A. RenderQueue | `RenderCommand.h`, `RenderQueue.*`, queue tests | Deterministic sort and batch key model | GL code |
| B. Sprite Data/Material | `SpriteRenderer.*`, `Material.*` | Render data construction and material batch key | Runtime loop edits |
| C. SpriteBatcher | `SpriteBatcher.*`, batch shaders, renderer stats | VBO/EBO batching and flush stats | Scene View/runtime integration |
| D. Integration | `runtime_main.cpp`, `SceneViewWindow.cpp`, `RenderSystem2D.*` | Shared render system and local drawList removal | Material internals |
| E. Profiling UI | `FrameProfile.h`, `ProfilerService.*`, `ProfilerWindow.cpp`, `main.cpp` | Batch efficiency and runtime/editor frame stats | Queue ownership |
| F. Tests/Bench | tests and smoke fixtures | queue/batch/material tests and 1k sprite baseline | Feature code except fixtures |

## Tests

- `RenderQueue` sort is deterministic.
- Same `sortingOrder` preserves submission order.
- Batch key groups only compatible sprites.
- Non-batchable material falls back without visual failure.
- Scene View and runtime render the same sprite order.
- 1,000 identical texture/material sprites produce a small number of draw calls.

Suggested commands:

```bash
ctest --preset debug -R "renderer|frame_counters|profiler" --output-on-failure
ctest --preset asan --output-on-failure
ctest --preset ubsan --output-on-failure
```

## Done Criteria

- Runtime and Scene View no longer own separate local draw-list render loops.
- `SpriteRenderer` no longer directly applies material and draws in the normal render path.
- Profiler shows draw calls, batches, batch efficiency, texture binds, shader switches, and sort time.
- Batch off/on modes produce matching visuals.
- Non-batchable materials still render through fallback.

## Risks

- Sorting by material or texture can break transparent rendering if visual order is not preserved.
- `Material::Apply` currently owns too much GL state; batchable and fallback paths must be explicit.
- GL-dependent batching is harder to unit-test; keep sort/grouping pure C++ and verify GL through smoke tests.
- Editor and runtime can diverge again unless both use `RenderSystem2D`.
