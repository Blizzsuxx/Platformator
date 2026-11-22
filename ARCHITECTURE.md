# Platformator Engine Architecture

Platformator is a C++20 2D component-based game engine built on SDL3, SDL3_image, SDL3_mixer, Eigen, nlohmann/json, and oneTBB, paired with an optional native Dear ImGui scene editor (`platformator_editor`).

## Public SDK vs. Internal Implementation

- **`engine/include/platformator/`** — Public C++20 SDK headers installed as CMake package `Platformator::platformator`. External games (`examples/crystal_citadel`, `examples/doom_2_5d`) and the editor depend only on these headers.
- **`engine/src/`** — Internal engine implementation organized by domain:

```text
engine/src/
  core/               Runtime orchestration, object/component lifecycle, pools, registries, flat containers
  physics/            Rigid-body dynamics, SAT/circle narrow phase, parallel impulse solver, DDA raycasting
    broadphase/       Uniform spatial grid + segmented interval lists (Tracy, Buss & Woods 2009)
  rendering/          SDL3 window/renderer, 2D camera, cached-linear sprite culling & Z-order, debug overlays
  audio/              SDL3_mixer AudioManager, Audio component, AudioWrapper asset
  animation/          Animator component and AnimationClip frames
  scripting/          Behavior lifecycle, ScriptComponent, compile-time override detection, script registry
  scene/              JSON scene/template I/O, atomic scene builder, ObjectReference resolution, serializers
  assets/             AssetStore caching, canonical path aliasing, hot-reload helpers
  editor/             In-process/IPC editor play-session protocol and live sync
  benchmark/          Schema-v3 scoped timing recorder, work probes, deterministic benchmark scenarios
```

## Top-Level Subsystem Ownership

`platformator::Runtime` exposes the public facade over `GameManager` (`engine/src/core/gamemanager.{h,cpp}`), which owns the core subsystems in deterministic initialization/teardown order:

| Subsystem | Location | Responsibility |
|---|---|---|
| **`SDLWindow`** | `rendering/sdlwindow.{h,cpp}` | SDL3 window, 2D renderer, input events, cached-linear sprite culling & incremental Z-order, debug draw triggers |
| **`AudioManager`** | `audio/audiomanager.{h,cpp}` | SDL3_mixer device lifecycle, pause/resume freeze, transient one-shot playbacks, deferred fade-out replays |
| **`PhysicsManager`** | `physics/physicsmanager.{h,cpp}`<br>`physics/physicssolver.cpp`<br>`physics/physicsraycast.cpp`<br>`physics/physicscontacts.cpp`<br>`physics/physicsgeometry.cpp`<br>`physics/physicsbodyindexes.cpp` | Integration, sleeping, narrow phase (parallel SAT/circle checks), parallel island & order-preserving batch solver, DDA raycasting, contact events |
| **`Grid`** *(owned by `PhysicsManager`)* | `physics/broadphase/grid.{h,cpp}`<br>`physics/broadphase/gridmaps.h`<br>`physics/broadphase/aabb.{h,cpp}`<br>`physics/broadphase/segmentedintervallist.{h,cpp}`<br>`physics/broadphase/localsortarray.{h,cpp}` | Spatial cell partitioning (`FlatCellMap`), pointer-stable candidate pair pool (`FlatCandidatePairPool`), per-cell segmented interval lists with flat open-addressing overlap/checkpoint sets |
| **`AssetStore`** | `assets/assetstore.{h,cpp}` | Canonicalized texture, audio, animation clip, and template caching with reference counting |
| **`ObjectRegistry`** | `core/objectregistry.{h,cpp}` | Generational `ObjectHandle` / `ObjectReference<T>` liveness tracking and `FlatIdMap` lookup |

## Fixed-Step Frame Pipeline (`GameManager::runSimulationStep`)

Each deterministic simulation step (`FIXED_TIME_STEP = 1/120 s`) runs in a strict stage order:

1. **Startup & Step Hook** — Resolve references and invoke `Behavior::start()` on newly attached scripts; run optional custom step hook.
2. **Fixed Script Update** — Dispatch `Behavior::fixedUpdate(dt)` over the sparse `fixedUpdateScripts` list (automatically filtered at compile time via `detectBehaviorUpdateMask<T>()`).
3. **Physics Step** (`PhysicsManager`)
   - Synchronize dirty transform subtrees.
   - `broadPhase()` — flush any queued cell/collider updates.
   - `narrowPhase()` — parallel geometry tests over `pendingNarrowPhasePairs`, fast path for unchanged trigger contacts, deterministic serial contact/support reconciliation.
   - `applyPhysics(dt)` — integrate gravity/forces on awake dynamic bodies.
   - `resolveCollisions(dt)` — union-find contact islands solved in parallel (or order-preserving batches for dominant islands), kinematic resolution, sleep-state updates.
   - `applyMovement(dt)` — deterministic parallel position/rotation integration into `movedBodyChanges`, followed by transform sync.
4. **Collision Event Dispatch** — Deliver queued `onCollisionEnter` / `onCollisionExit` callbacks.
5. **Frame Scripts & Animation** — Dispatch `update` (sparse `updateScripts`), `Animator::update`, and `lateUpdate` (sparse `lateUpdateScripts`).
6. **Cleanup & Mutation Commit**
   - `AudioManager::update()` — reap finished transient tracks and process pending replays.
   - `commitMutations()` — deferred spatial grid flush (`deferred_grid_flush`, including parallel order-preserving narrow-phase pair queueing), component reconciliation, `onDestroy` callbacks, object retirement, or atomic scene transition (`gamemanagerscene.cpp`).
7. **Render (separate from headless physics)** — `SDLWindow::render()` runs cached-linear frustum culling, incremental Z-order repair, sprite submission, and optional debug overlays (`F1`–`F4` colliders/points/normals/grid, `F5`/`F6` pause/step, `F9` FPS HUD).
