# Implementation Plan: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Branch**: `branch-without-bvh` | **Date**: 2026-11-18 | **Spec**: [spec.md](spec.md)

**Input**: `specs/020-sprite-tint-raycast/spec.md`; user guidance: *"try to stick to KISS where possible. cleaner code is usually better but optimize for performance"*.

**Feature identifier**: `020-sprite-tint-raycast`. The setup script reports this identifier as `BRANCH`; the actual Git branch above is unchanged.

## Summary

Add three focused, zero-dependency engine and editor capabilities following a strict KISS + performance-first design:

1. **Per-Sprite Color & Opacity Tinting**: Store an 8-bit `SDL_Color color{255, 255, 255, 255}` on `Sprite` (fitting into existing tail padding), expose `getColor` / `getAlpha` / `setColor` / `setAlpha`, serialize `"color": {"r", "g", "b", "a"}` with legacy omission defaulting to opaque white, support full editor inspection/validation/copy-reset/template/preview workflows, and apply modulation per draw inside `platformator_detail::submitSpriteRender` with a zero-overhead fast path when `(255, 255, 255, 255)` and clean state restoration afterward. Color edits never dirty culling bounds or sprite draw order. [Research](research.md#1-per-sprite-rgba-modulation-sdl_settexturecolormod--sdl_settexturealphamod)
2. **Configurable Frame Clear Color**: Add `SDL_Color clearColor{255, 255, 255, 255}` to `WindowSettings` and forward `Runtime::getClearColor()` / `Runtime::setClearColor(...)` directly to `SDLWindow`'s `windowSettings.clearColor`, replacing the hardcoded white clear in `SDLWindow::render()` with zero duplicate state and safe headless storage. [Research](research.md#3-window--runtime-clear-color-override)
3. **2D Spatial Raycast Queries**: Add `platformator::RaycastHit` (`collisiontypes.h`) and `Runtime::raycast` / `Runtime::raycastAll` (`runtime.h`), backed by `PhysicsManager` (`physicsmanager.{h,cpp}`) reusing the existing `Grid` + `AABB` (`SegmentedIntervalList` / `LocalSortArray` + `BoundingRadiusProjectionAxis`) infrastructure directly on the synchronized physics world. Queries clip `[0, maxDistance]` against the occupied `Grid` cell bounds (`grid.getActiveCellBounds()`) via `rayIntersectsAABBInterval`, step 2D Amanatides & Woo DDA across occupied `GridCell`s, query `cell.getAABB()->forEachOverlappingAABB(...)` on the cell's X/Y projections with stack multi-cell deduplication, apply ray-vs-AABB slab pruning before exact 2D circle / oriented-box slab intersection against `cachedWorldPosition`, perform zero heap allocations on `raycast`, and break ties deterministically by `(distance, collider->getId())`. [Research](research.md#4-kiss-high-performance-2d-raycast-architecture-runtimeraycast--runtimeraycastall)

## Technical Context

**Language/Version**: C++20 (`CMAKE_CXX_STANDARD 20`, no extensions).

**Primary Dependencies**: Existing bundled SDL3 3.4.18, SDL3_image 3.4.8, SDL3_mixer 3.2.4, SDL3_ttf 3.2.2, Eigen 5.0.1, oneTBB 2022.3.0, nlohmann JSON, and Dear ImGui 1.92.9b. Zero added dependencies.

**Storage**: Existing `.scene` / `.pttemplate` JSON; optional `"color"` object on Sprite (`type == 6`), emitted on save and defaulting to `{255, 255, 255, 255}` when omitted. Clear color and raycast results are runtime-only.

**Testing**: Existing CMake/CTest engine and editor integration test suites (`ctest --preset test`), deterministic software rendering (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`), work-probe instrumentation (`runtime_work_probe.h`), plus focused tests for sprite tinting/clear color and 2D raycasting.

**Target Platform**: Cross-platform desktop runtime (`Linux`, `macOS`, `Windows`) with Linux editor and deterministic software-rendering CI coverage.

**Project Type**: C++20 runtime library (`Platformator::platformator`), scene runner, and native ImGui scene editor.

**Performance Goals**:
- Zero added SDL state calls on untinted `(255, 255, 255, 255)` sprites.
- Zero culling bounds refreshes, visibility re-scans, or draw-order re-sorts when only sprite colors or clear color change.
- Zero recurring heap allocations during closest-hit `Runtime::raycast` queries; synchronized `Grid` + `AABB` spatial traversal (2D DDA + `SegmentedIntervalList` AABB overlap query + ray-AABB slab pruning + shrinking closest-hit bound).

**Constraints**: Preserve 100% backward compatibility with legacy scenes/templates, `cached-linear` render culling, ImGui editor texture rendering (no leaked texture modulation), headless `headless = true` behavior, and existing public header standalone compilation checks (`cmake/PlatformatorPublicHeaders.cmake`).

**Scale/Scope**: Single-scene rendering up to 10,000 sprites (matching `019-sprite-z-ordering` scales) and dynamic per-frame tinting / multi-hundred-ray casting (e.g., 2.5D raycasters, AI LOS, hitscan combat).

## Constitution Check

*Pre-research gate: PASS. Post-design check: PASS. No exceptions or complexity violations.*

| Principle | Before research | After design |
|---|---|---|
| I. Performance and Scalability First | Avoid allocations, culling/order invalidation, and texture duplication on tinting; avoid full-list collider scans or allocations on raycast. | Untinted sprites incur 0 extra SDL calls; tinted sprites preserve SDL batching and trigger 0 culling/draw-order invalidations; `raycast` reuses `Grid` + `AABB` (`SegmentedIntervalList` + ray-AABB slab pruning) with shrinking `limit` and 0 heap allocations. |
| II. Multicore Design with oneTBB | Evaluate whether rendering tint/clear or raycast should parallelize. | Rendering runs on the owner thread in SDL3; spawning oneTBB tasks inside a single microsecond-scale `raycast` query adds task-schedule overhead exceeding the work itself. Making `raycast` `const` with purely stack-local state and zero writes to shared counters enables caller-side `tbb::parallel_for` ray batching cleanly when needed. |
| III. Research-Backed Collision Architecture | Reuse `Grid` + `AABB` safely for raycasts without mutating broad-phase state. | `PhysicsManager::grid` broad-phase storage, pair witness counts, and solver pairs remain completely untouched (read-only). Queries traverse the synchronized physics world directly via `Grid` + `AABB::forEachOverlappingAABB` and `cachedWorldPosition`, designed for callbacks after physics sync (`Update` / `LateUpdate` / stepped scenes). |
| IV. Simple Code and KISS | Reuse existing `Sprite`, `WindowSettings`, `Runtime`, `submitSpriteRender`, and `PhysicsManager` owners with minimal fields and helpers. | `SDL_Color` on `Sprite` fits existing padding; `clearColor` lives directly on `SDLWindow::windowSettings` (zero duplicate fields); raycast adds two small pure-geometry helpers (~100 lines total) without a spatial tree or extra state structures. |
| V. Unity-Inspired Developer Experience | Provide familiar `Sprite` color/alpha, camera/window clear color, and `raycast` / `raycastAll` semantics. | Exposes intuitive `Sprite::setColor` / `setAlpha`, `Runtime::setClearColor`, and `Runtime::raycast` / `raycastAll` returning `RaycastHit` (`collider`, `gameObject`, `point`, `normal`, `distance`, `fraction`), while documenting the deterministic inside-origin (`distance = 0`, `normal = -dir`) contract. |
| VI. Evidence Before Performance Claims | Verify behavior and work counts via existing deterministic tests and probes before claiming zero overhead. | `quickstart.md` defines targeted work-probe checks (`0` culling/order refreshes on color edits), pixel verification under software rendering, allocation-free closest-hit raycast tests, and full `ctest --preset test` validation. |
| VII. Correctness and Architectural Integrity | Prevent shared texture state leaks across sprites/UI; ensure exact rotated/scaled/offset collider math and safe validation. | `submitSpriteRender` restores `(255, 255, 255, 255)` + original blend mode whenever non-default; JSON/editor boundaries validate `[0, 255]` integers atomically; raycast validates inputs, filters inactive/deleted/trigger/masked colliders, and tie-breaks by `(distance, colliderId)`. |

## Project Structure

### Documentation (this feature)

```text
specs/020-sprite-tint-raycast/
├── spec.md
├── checklists/requirements.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    └── rendering-and-raycast.md
```

`tasks.md` will be generated later by `$speckit-tasks`.

### Source Code (repository root)

```text
engine/
├── include/platformator/
│   ├── collisiontypes.h         # platformator::RaycastHit struct
│   ├── runtime.h                # getClearColor/setClearColor, raycast/raycastAll declarations
│   ├── sprite.h                 # SDL_Color color field, getColor/getAlpha/setColor/setAlpha
│   └── windowsettings.h         # SDL_Color clearColor{255, 255, 255, 255}
├── src/
│   ├── camera.cpp               # pass sprite->getColor() to submitSpriteRender
│   ├── spriterendergeometry.h   # apply & restore SDL_SetTextureColorMod / AlphaMod / BlendMode
│   ├── sprite.cpp               # constructors, clone(), color/alpha getters & setters
│   ├── spriteserializer.cpp     # "color": {"r","g","b","a"} JSON serialization & validation
│   ├── sdlwindow.h / sdlwindow.cpp # clearColor accessors & clear in SDLWindow::render()
│   ├── runtime.cpp              # clearColor and raycast/raycastAll forwarding
│   └── physicsmanager.h / physicsmanager.cpp # raycast / raycastAll + circle/OBB slab math
└── tests/
    ├── CMakeLists.txt           # add platformator_sprite_tinting_test & platformator_raycast_test
    ├── sprite_tinting_test.cpp  # shared-texture tint/alpha isolation, clearColor, culling invariance
    ├── raycast_test.cpp         # circle, AABB/OBB, scale/offset, mask/trigger, inside/tie/alloc tests
    ├── scene_io_contract_test.cpp # Sprite color serialization, legacy default & error guards
    └── runtime_template_test.cpp  # Sprite color retention across template instantiation & clone

editor/
├── src/
│   ├── componenttypes.h         # add "color" default to default_component_record(6)
│   ├── validation.cpp           # validate optional Sprite "color" & copy_validation_component
│   ├── preview.h / preview.cpp  # cache SpriteInputs color and apply to preview proxy Sprite
│   └── panels/inspector.cpp     # compact ColorEdit4 RGBA control, semantic node & multi-edit
└── tests/
    ├── inspector_interaction_test.cpp # Sprite Color single/multi-edit, undo/redo, invalid repair
    ├── diagnostics_test.cpp           # Sprite color validation diagnostics
    ├── component_operations_test.cpp  # Sprite color copy/paste/reset
    ├── templates_test.cpp             # Sprite color template override/revert/duplication
    └── preview_test.cpp               # Preview pixel & resource invariance on color edits
```

**Structure Decision**: Extend the existing engine and editor owners in place (`Sprite`, `submitSpriteRender`, `WindowSettings`, `SDLWindow`, `Runtime`, `PhysicsManager`, and existing editor validation/preview/inspector modules). No new runtime subsystems, background workers, shader wrappers, or scene envelope changes are introduced.

## Phase 0 & Phase 1 Design Summary

- **Research**: Resolved texture modulation batching/state safety, structured `"color"` JSON & atomic editor editing, `WindowSettings::clearColor` storage, and contiguous immediate-transform raycasting in [research.md](research.md).
- **Data Model**: Defined invariants, field layouts, JSON schemas, and preview/culling non-invalidation rules in [data-model.md](data-model.md).
- **Contracts**: Specified exact C++ public signatures, JSON schemas, validation errors, tie-breaking, and edge-case rules in [contracts/rendering-and-raycast.md](contracts/rendering-and-raycast.md).
- **Quickstart**: Documented build, test, and targeted verification commands in [quickstart.md](quickstart.md).
- **Implementation Alignment**: Refactored `PhysicsManager` raycast traversal to query `Grid` + `AABB` (`SegmentedIntervalList` + ray-AABB slab pruning) exclusively (eliminating full `colliderComponents` scans and redundant circumcircle checks), reflected above and in [research.md](research.md), [data-model.md](data-model.md), and [evidence/performance-summary.md](evidence/performance-summary.md).

## Complexity Tracking

No Constitution Check violations or exceptions; no extra abstraction layers, caches, or background threading pools added.
