# Box2D Physics Backend Plan

> Date: 2026-06-28
> Track: P1 production physics.

## Problem

The current physics system is a custom prototype solver. It is useful for learning and simple scenes, but it does not have the broadphase, iterative solver, materials, joints, sleep, CCD, robust rotation handling, or query performance expected from a production engine.

Current evidence:

- `PhysicsWorld` is custom state, not a proven backend world: `src/Physics/PhysicsWorld.h:10-30`.
- Gravity and integration are hand-coded with `981 px/s^2`: `src/Physics/PhysicsWorld.cpp:65-95`.
- Active colliders are gathered and checked with an `O(n^2)` pair loop: `src/Physics/PhysicsWorld.cpp:103-245`.
- Penetration and velocity response are basic single-pass logic: `src/Physics/PhysicsWorld.cpp:261-307`.
- `Rigidbody2D` lacks angular velocity, torque, sleep, bullet/CCD, and constraints: `src/ECS/Components/Rigidbody2D.h`.
- `PhysicsMaterial2D` is only forward-declared in collider code.
- Box colliders use AABB-style bounds and do not handle rotated shapes robustly: `src/ECS/Components/BoxCollider2D.cpp:21`.
- `Physics2D` queries scan objects directly instead of using a spatial structure: `src/Physics/Physics2D.cpp`.

## Target Architecture

Use a Box2D backend behind Molga's public component API:

```text
World
  -> PhysicsWorld
       -> Box2DWorldBackend
       -> PhysicsConversions
       -> ContactBridge
       -> QueryBridge

Rigidbody2D / Collider2D / Joint2D
  -> serializable engine components
  -> backend handles owned by PhysicsWorld
```

Design constraints:

- Keep one `PhysicsWorld` per `World`.
- Hide Box2D headers from public component headers where practical.
- Fix a pixel-to-meter conversion early. Default can start at `100 px = 1 m`.
- Preserve existing script callback meanings: `OnCollision*` and `OnTrigger*`.
- Preserve current fixed-step ordering where scripts can apply forces before physics step.

## Phases

### Phase 1. Dependency and Backend Boundary

- Add a pinned Box2D dependency through vendoring or configured fetch.
- Add `MOLGA_PHYSICS_BACKEND=legacy|box2d` while migrating.
- Create `Box2DBackend.*` and `PhysicsConversions.*`.
- Add license and version notes.

### Phase 2. Physics Settings

- Add `PhysicsSettings2D`: gravity, pixels-per-meter, velocity iterations, position iterations, substep policy.
- Store settings in project/package configuration where appropriate.
- Keep compatibility with current fixed update loop.

### Phase 3. Body and Shape Lifetime

- Do not create backend bodies only from component attach events, because objects may not be in a `World` yet.
- Add `PhysicsWorld::SyncComponents()` to lazily create/destroy bodies and shapes based on world ownership, active state, enabled state, and component changes.
- Ensure object destruction queues backend deletion safely.

### Phase 4. Simulation Replacement

- Replace custom gravity/integration/penetration code with Box2D stepping.
- Define authority:
  - Dynamic: physics writes Transform after step.
  - Static: Transform writes backend pose when changed.
  - Kinematic: velocity/transform sync follows explicit policy.
- Convert Transform degrees to backend radians.

### Phase 5. Contacts, Filters, Events

- Compile `ProjectSettings` layer collision matrix into backend category/mask filters.
- Convert backend begin/end contacts into existing collision/trigger callbacks and events.
- Synthesize stay events from active contact state if needed.

### Phase 6. Queries, Tilemap, Debug Draw

- Replace `Physics2D` raycast/overlap scans with backend queries.
- Convert tilemap collision from per-frame AABB checks to static shapes, preferably merged by chunk or run length.
- Add Scene View physics debug draw toggles.

### Phase 7. Production Features

- Implement `PhysicsMaterial2D` with density, friction, restitution, and sensor behavior.
- Add CCD/bullet and sleep options.
- Add at least two joint components with serialization and Inspector support.

## Subagent Plan

| Worker | Owns | Deliverables | Must avoid |
|---|---|---|---|
| A. Dependency/Backend | `CMakeLists.txt`, `Box2DBackend.*`, `PhysicsConversions.*` | Linkable backend wrapper and conversion tests | Component schema edits |
| B. Component Schema | `Rigidbody2D.*`, `Collider2D.*`, collider types, `PhysicsMaterial2D.*`, joints | Serializable API and Inspector fields | `PhysicsWorld.cpp` logic |
| C. World/Step/Events | `PhysicsWorld.*`, `World.*`, physics events | Sync, step, contact bridge, profiler counters | Public component redesign |
| D. Query/Tilemap/Debug | `Physics2D.*`, `TilemapRenderer.*`, debug draw | Backend queries and tile static shapes | Contact bridge |
| E. Tests/Migration | physics/query/tilemap/serializer tests and smoke fixtures | Legacy parity tests and production cases | Broad implementation changes |

Dependency worker A should land first. C should not begin deep integration until B's component handles and serialization contract are stable.

## Tests

- Existing: `test_physics`, `test_physics_query`, `test_tilemap`, `test_scene_serializer`, `test_lifecycle`, `test_time`, `runtime_smoke`.
- New: unit conversion, body/shape create-destroy, play/stop leak checks, layer filter, multiple colliders per object, trigger events, material friction/restitution, CCD tunneling, sleeping wake, joint constraints.
- Performance: 1,000 static colliders plus 100 dynamic bodies should keep fixed-step time stable compared with legacy `O(n^2)` behavior.

Suggested commands:

```bash
ctest --preset debug -R "physics|tilemap|scene_serializer|lifecycle|runtime_smoke" --output-on-failure
ctest --preset asan --output-on-failure
ctest --preset ubsan --output-on-failure
```

## Done Criteria

- Box2D backend is the default physics backend.
- Existing script collision/trigger callback semantics are preserved.
- Gravity, collision matrix, triggers, raycasts/overlaps, and tilemap collision work in editor, runtime, and package.
- CCD, sleeping, materials, and at least two joints have tests and serialization.
- Play/stop, clone/load, and object destroy leave no backend body/shape/contact leaks.

## Risks

- Current `BoxCollider2D` positioning appears top-left/AABB-oriented; backend center/origin conversion can shift existing scenes.
- Eager body creation breaks when components exist before world insertion.
- Transform editing and physics authority can fight each other without a clear sync policy.
- Pixel units directly in Box2D can destabilize simulation; conversion must be fixed early.
- Event ordering changes can break scripts and tests.
