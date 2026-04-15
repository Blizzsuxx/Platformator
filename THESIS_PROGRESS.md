# Platformator Thesis Progress Matrix

This matrix maps the current codebase to the major implementation areas covered by the Tracy, Buss, and Woods 2009 paper, plus the engine work that sits on top of the paper.

The paper PDF tracked by this repository is `paper/Efficient_Large-Scale_Sweep_and_Prune_Methods_with.pdf`.

## Paper-Aligned Matrix

| Paper Topic | Status | Code | Notes |
| --- | --- | --- | --- |
| Segmented sweep-and-prune interval structure | Done | `src/segmentedintervallist.h`, `src/localsortarray.h` | Chunked interval storage is implemented with linked `LocalSortArray` chunks and incremental projection repair. |
| Interval insertion and removal without global re-sort | Done | `src/segmentedintervallist.cpp` | New projection axes are inserted by chunk-level binary search and repaired locally instead of rebuilding the entire list. |
| Cross-chunk checkpoint tracking | Done, with correctness risk | `src/localsortarray.cpp`, `src/segmentedintervallist.cpp` | Checkpoints exist and are used during insertion, removal, and swap processing. The current checkpoint cache logic is still an open correctness risk. |
| Overlap begin/end event generation from endpoint ordering | Done | `src/segmentedintervallist.cpp`, `src/aabb.cpp` | Axis overlap begin/end is emitted from endpoint swaps and combined into per-cell AABB overlap state. |
| Two-axis AABB filtering | Done | `src/aabb.cpp` | X and Y projections are both maintained and combined before narrow phase. |
| Spatial subdivision over the broad phase | Done | `src/grid.cpp`, `src/gridcell.cpp` | The engine adds a grid layer on top of the paper-inspired interval lists. This is beyond the bare paper structure but is implemented and active. |
| Persistent broad-phase pair tracking | Done | `src/grid.cpp`, `src/colliderpair.cpp` | Pairs are witness-counted across cells and queued into narrow phase only when needed. |
| Temporal coherence through local repair | Done | `src/collider.cpp`, `src/segmentedintervallist.cpp` | Collider sync updates projections in place and repairs only the affected proxies. |
| Fast-moving object conservative buffering | Partial | `src/segmentedintervallist.cpp` | The code comments say the checkpoint buffer work is done, but there is no separate conservative motion expansion or dedicated fast-body path. |
| Continuous collision detection for tunneling prevention | Missing | N/A | There is no swept test or time-of-impact solver for fast projectiles. |

## Physics Layer Above The Paper

| Engine Topic | Status | Code | Notes |
| --- | --- | --- | --- |
| SAT narrow phase | Done | `src/physicsmanager.cpp`, `src/boxcollider.cpp`, `src/circlecollider.cpp` | Normals are collected from both colliders and tested for separating axes. |
| Contact manifold generation | Done, approximate for circles | `src/physicsmanager.cpp`, `src/helpers.h` | Polygon clipping is in place. Circle contact generation is present but still less feature-accurate than a dedicated closest-feature manifold builder. |
| Sequential impulse solver | Done | `src/physicsmanager.cpp` | Warm-started normal and tangent impulses, effective mass terms, and Baumgarte bias are all implemented. |
| Automatic inertia from collider geometry | Done | `src/rigidbody.cpp` | Box and circle inertia are derived from shape and cached. |
| Sleeping and support contacts | Done | `src/rigidbody.cpp`, `src/collision.cpp` | Support contacts are persistent and drive sleep state. |
| Collision filtering by group and mask | Done in broad phase | `src/aabb.cpp`, `src/collider.cpp` | Group and mask checks already gate pair creation. |
| Trigger non-resolution | Done | `src/collision.cpp` | Trigger pairs can still be detected but are skipped by the solver. |
| Restitution / bounce response | Done | `src/rigidbody.cpp`, `src/collision.cpp`, `src/physicsmanager.cpp` | Restitution is combined per collision and injected into the normal solver bias when the closing velocity crosses a bounce threshold. |
| Kinematic body semantics | Done | `src/rigidbody.cpp`, `src/collision.cpp` | Kinematic bodies now move through scripted velocity, ignore force and gravity integration, contribute zero inverse mass/inertia to the solver, and still block dynamic bodies. |

## Engine Scope Outside The Paper

| Engine Area | Status | Code | Notes |
| --- | --- | --- | --- |
| Component-based entity model | Done | `src/gameobject.h`, `src/gameobject.cpp` | Core `GameObject` and `Component` architecture is working. |
| SDL3 rendering path | Done | `src/sdlwindow.cpp`, `src/sprite.cpp`, `src/camera.cpp` | Rendering, camera, and debug overlays are functional. |
| Debug visualization | Done | `src/debugdraw.cpp` | Colliders, normals, contact points, and grid cells can be drawn live. |
| Audio component | Partial | `src/audio.h`, `src/audio.cpp` | Resource ownership exists, but there is no playback API or scene integration. |
| Animator component | Missing | `src/animator.h`, `src/animator.cpp` | Animator is only a stub. |
| Light component | Missing | `src/gameobject.h` | `LIGHT` exists in the enum but has no implementation. |
| Input abstraction | Missing | `src/main.cpp`, `src/sdlwindow.cpp` | Input is still direct SDL event handling, not an engine input layer. |
| Serialization / scene loading | Missing | N/A | No save/load pipeline is implemented. |
| Automated tests | Done, minimal | `tests/regression_tests.cpp` | A small regression harness now covers stability, sleeping, and broad-phase pair tracking. |

## Open Risks And Performance Debt

| Area | Status | Code | Notes |
| --- | --- | --- | --- |
| Checkpoint cache correctness | Open risk | `src/localsortarray.cpp` | The cancellation cache can still suppress valid checkpoint state transitions across frames. |
| Renderer startup failure handling | Open risk | `src/sdlwindow.cpp` | Renderer creation is still assumed to succeed. |
| Parallel physics loops | Not started | `src/physicsmanager.cpp` | The main rigidbody loops and contact solver still have explicit parallelization TODOs. |
| Name-based object lookup | Not started | `src/gamemanager.cpp` | Object lookup and removal by name are linear scans over a list. |
| Headless / CI proof beyond regressions | Partial | `tests/regression_tests.cpp` | Regression coverage exists now, but there is still no large scene benchmark or stress suite. |

## Suggested Thesis Framing

- The thesis implementation already demonstrates the core research contribution: a working chunked sweep-and-prune broad phase with checkpoint-assisted cross-chunk overlap tracking and spatial subdivision.
- The strongest completed areas are broad phase, SAT narrow phase, sequential impulses, inertia handling, and sleep/support management.
- The clearest remaining work items are CCD, restitution, fully distinct kinematic behavior, and broader automation around testing and benchmarking.