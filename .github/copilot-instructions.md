# Platformator - Copilot Instructions

## Project Overview
A 2D game engine/platformer in C++20 using SDL2 (rendering/audio) and Eigen3 (linear algebra). Unity-style component-based architecture. All source files are flat in the project root (may be reorganized into folders later). No test framework exists. Input handling will be added via user scripts.

## Build & Run
```bash
make        # Build with g++ -std=c++20, links SDL2, SDL2_image, SDL2_ttf, SDL2_mixer
make clean  # Remove .o files and binary
./main      # Run the engine
./tidy.sh   # Run clang-tidy auto-fixes (readability, modernize checks)
```
All `.cpp` files in the root are compiled via wildcard — new `.cpp` files are automatically included in the build.

## Architecture

### Game Loop ([gamemanager.cpp](gamemanager.cpp))
`GameManager::loop()` → `updateDeltaTime()` → `handleEvents()` → `applyPhysics()` → `resolveCollisions()` → `render()` → `delay()`

`GameManager` owns the `SDLWindow` (events + rendering) and `PhysicsManager`. It auto-creates a `MainCamera` GameObject on construction. Adding/removing GameObjects registers their components with the appropriate manager (`PhysicsManager` for colliders/rigidbodies, `SDLWindow` for sprites).

### Component System ([gameobject.h](gameobject.h))
- `GameObject` holds a fixed-size `Component*` array indexed by `ComponentType` enum
- Components: `ANIMATOR`, `AUDIO`, `CAMERA`, `COLLIDER`, `LIGHT`, `RIGID_BODY`, `SPRITE`
- `COMPONENT_TYPE_COUNT` is the last enum value (standard auto-increment) — add new types before it
- Access pattern: `(Collider *)gameObject->getComponent(ComponentType::COLLIDER)` (C-style cast)
- Each `Component` constructor takes `(GameObject*, ComponentType)` and registers itself
- `GameObject` rotation is stored in **radians**; `getRotationInDegrees()` converts for SDL rendering
- `GameObject::setPosition()` triggers `updateCollider()` which marks colliders dirty for broad-phase re-sort

### Physics Pipeline ([physicsmanager.cpp](physicsmanager.cpp))
Based on Tracy, Buss & Woods 2009 "Efficient Large-Scale Sweep and Prune Methods with AABB Insertion and Removal" (see [paper/](paper/)).

1. **Broad Phase**: `AABB` class manages a `SegmentedIntervalList` on X-axis only (Y-axis is commented out). Sorts intervals, then sweeps to find candidate pairs.
2. **Narrow Phase**: SAT via `checkProjections()` tests normals from both colliders. Computes penetration depth, contact normal, and contact point.
3. **Collision Response**: `resolveCollisions()` is **currently empty** — this is the main unimplemented feature.

### Segmented Interval List ([segmentedintervallist.h](segmentedintervallist.h))
Implements the paper's novel data structure — a linked list of small sorted arrays ("chunks") that allows O(1) AABB insertion/removal without sorting the entire axis.

- `LocalSortArray`: Chunks of max 32 sorted `BoundingRadiusProjection` elements. When full, a chunk splits in half.
- `checkpoint`: `unordered_set<Collider*>` per chunk, tracking AABBs whose minima is in this or an earlier chunk but whose maxima is in a later chunk. This provides local knowledge of arbitrarily distant extrema.
- `BoundingRadiusProjection`: Stores collider pointer, projected position, and `isEnd` flag (false=minima, true=maxima)

**Checkpoint maintenance**: During sort, a minima swapping left into a chunk adds to checkpoint; a maxima swapping right out adds to checkpoint. The reverse removes from checkpoint. On insertion across multiple chunks, every chunk from minima (inclusive) to maxima (exclusive) adds the object to its checkpoint.

**Sweep logic**: When encountering a maxima (`isEnd()==true`), all colliders with extrema between it and its paired minima — plus those in the checkpoint set — are candidate collision pairs.

### Collider Types
`Collider` (abstract) → `BoxCollider` (4 vertices, cached normals), `CircleCollider` (radius-based)
- `projectOntoAxis(axis)` — SAT projection returning `Vector2f(min, max)`
- `getNormals(other)` — returns separating axes to test
- Each collider holds `xProjections`/`yProjections` (`BoundingRadiusProjectionAxis`) for broad phase
- Collider has `layer` (int) and `isTrigger` (bool) fields — not yet wired into collision filtering

## Code Conventions
- Headers: `#pragma once`, no include guards
- Naming: `getX()`/`setX()` accessors, `SCREAMING_CASE` enums, `camelCase` members (no prefix)
- Memory: raw `new`/`delete` for GameObjects and Components (no smart pointers for entities)
- `Collision` uses `mutable` fields so narrow phase can update data on `const Collision&` references from the broad-phase set
- Vectors: `Eigen::Vector2f` throughout; access via `.x()`, `.y()`
- Frame timing: 60 FPS target via `FRAME_TIME` constant in [constants.h](constants.h)
- `BodyType::DYNAMIC` receives gravity/forces; `STATIC`/`KINEMATIC` do not

## Key Files
| File | Purpose |
|------|---------|
| [gamemanager.cpp](gamemanager.cpp) | Main loop, object lifecycle, manager wiring |
| [gameobject.h](gameobject.h) | Entity + component system definitions |
| [physicsmanager.cpp](physicsmanager.cpp) | Broad phase, narrow phase (SAT), collision resolution (stub) |
| [aabb.h](aabb.h) | AABB broad-phase orchestrator using segmented interval list |
| [segmentedintervallist.h](segmentedintervallist.h) | Paper's sweep-and-prune data structure |
| [localsortarray.h](localsortarray.h) | Chunk with sorted array + checkpoint set |
| [collider.h](collider.h) | Collider base class + BoundingRadiusProjection types |
| [constants.h](constants.h) | Screen dimensions, frame timing, axis constants |
