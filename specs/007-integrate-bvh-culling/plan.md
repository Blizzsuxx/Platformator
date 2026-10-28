# Implementation Plan: Selectable BVH and Segmented Spatial Culling

**Feature**: `007-integrate-bvh-culling` | **Date**: 2026-10-28 | **Spec**: [spec.md](spec.md)

**Branch**: Current checkout `001-cpp-engine-cleanup`. The feature-directory override selects `specs/007-integrate-bvh-culling` independently; planning does not create or switch a Git branch.

**Input**: `specs/007-integrate-bvh-culling/spec.md` and the agreement to continue with the extracted Box2D tree. Source baseline: `2ad8caa466b1c0217b7911d3bca1a53160150456`.

## Summary

Add independently selectable collision broad phases (current partitioned segmented sweep-and-prune or dynamic BVH) and render culling (none, linear, dynamic BVH or a separate segmented sprite/camera context). Preserve segmented collision and submit-all rendering as defaults. Both collision choices feed the same persistent pair/contact/support/solver path. Rendering uses actual sprite geometry and independent membership, including sprites without colliders.

Use the already extracted Box2D 3.2.0 tree through a private adapter, extending its bridge for marked dynamic updates and partial maintenance. Compare complete dynamic costs rather than a prebuilt-query shortcut. Reuse the actual segmented implementation through a narrow owner/context adaptation, preserving endpoint/chunk/checkpoint invariants and public source spellings. Validate correctness, measure refactoring overhead, and deliver matched-input replay plus real-engine and actual-render evidence. A slower BVH is a valid result; this feature does not choose a new default or claim Box2D is universally best.

## Technical Context

**Language/Version**: C++20 runtime/SDK, C17 vendored tree/bridge; Python 3 standard-library benchmark orchestration, optional matplotlib plots; CMake >=3.22/Ninja.

**Primary Dependencies**: Existing SDL3 3.4.18 renderer/platform stack, Eigen, oneTBB, nlohmann JSON; private extracted Box2D 3.2.0 dynamic-tree subset. No new library/package required. Preserve vendor checksums, license and upstream tests.

**Storage**: Runtime-owned in-memory indexes, stable pair/proxy pools and reusable task/result buffers. Versioned JSONL traces, CSV samples/summary, JSON manifests and retained evidence bundles on disk. No scene-format migration or database.

**Testing**: Existing CTest C++/C integration and package consumers, retained upstream tree tests, new independent overlap/replay and same-renderer image tests, Python schema/sweep tests, affected-work probes, ASan/UBSan and supported TSan configurations. Separate correctness/resource instrumentation from timing builds.

**Target Platform**: Existing desktop native/SDK targets. Establish reproducible Linux CPU and SDL software-renderer evidence first; actual supported hardware-renderer evidence is separately labeled. Other platform/hardware results remain unverified until measured.

**Project Type**: Native runtime library and installed SDK with stock CLI runner, benchmark executables and Python tooling.

**Performance Goals**: Quantify complete-step/render/replay mean and tail latency, initialization, work/output counts, resource cost and worker scaling. Avoid unchanged-state recomputation and unused backend work. No invented FPS, speedup or memory target; compare representative sparse/dense/dynamic workloads and investigate regressions.

**Constraints**: All eight selections; preserved defaults/source consumers; exact collision coverage and retained render order; independent physics/visibility indexes; shared downstream physics; safe worker publication/reclamation; no timing oracles/shadow indexes; truthful fallback/provenance; supported Box2D proxy extents strictly below 100,000 world units by pinned default. Startup-only configuration.

**Scale/Scope**: Sparse N=100/1,000/10,000, changed bounds 0/1/10/100%, visible 0/1/10/100%, bounded dense/closed-loop cases, churn/migration/skew/mixed sizes and camera changes. One worker and an available multiple-worker allowance; >=5 fresh-process repetitions per reported performance case. Bounded representative matrix, not an exhaustive Cartesian product.

## Constitution Check

Gate authority: `.specify/memory/constitution.md` v1.1.0. Pre-research assessment passed subject to resolving pair ownership, generic segmented reuse, dynamic tree maintenance, render geometry and benchmark equivalence. Four read-only research tracks resolved those questions in [research.md](research.md). Post-design assessment follows; there are no requested exemptions or unresolved clarification items.

| Principle | Pre-design gate | Post-design gate / required implementation evidence |
| --- | --- | --- |
| I. Performance/scalability | PASS: compare existing segmented, dynamic tree and linear rendering; evaluate coherence/locality/allocation | PASS: dirty coalescing, stable reusable records, sparse-hit ordering, unchanged-result caching, complete costs/resources and same-method control; no per-endpoint virtual/hash dispatch |
| II. oneTBB/multicore | PASS: assess feasible parallel work and serial dependencies | PASS: preserve cell concurrency/TLS deltas; exclusive tree writer/maintenance, immutable task-local collider queries, canonical reduction and deferred reclaim. Single-camera/SDL/callback/solver dependencies remain sequential; measure small-batch overhead and serial limits with explicit query controls |
| III. Research-backed collision architecture | PASS: study paper §§3.1–3.3 and current source | PASS: retain inclusive ties, chunk bindings, checkpoints, fast-motion cancellation, split/merge/migration and multi-cell witnesses. Assess local/global/batch/adaptive alternatives below; independent oracles cover invariants |
| IV. KISS | PASS: bounded backend seam, no new physics framework | PASS: one shared physics pair owner, one segmented core, context-level policy, selected-only indexes. Every added queue/cache has an owner and revision/lifetime invariant in data-model.md |
| V. Developer experience | PASS: startup configuration, existing scene/component contracts | PASS: appended settings preserve aggregate initializers, all parser/SDK paths documented; truthful Grid-specific diagnostics. Experimental selection needs no scene migration; no claims of identical Unity internals/API |
| VI. Evidence before claims | PASS: no static-query/dynamic inference or mandatory winner | PASS: schema-4 explicit boundaries/provenance, immutable replay, real-engine/render tests, frozen baseline, >=5 process repetitions, resources separate, unavailable results named |
| VII. Correctness/integrity | PASS: preserve contact/support/event/lifetime contracts | PASS: full-generation identities, adjacency reconciliation, symmetric masks, frame publication, independent oracles/image parity, worker barriers and package/sanitizer checks |

A PASS here accepts the design, not unperformed runtime tests or benchmark results. Implementation completion remains conditional on the evidence below. Any unavoidable known inefficient implementation must meet the constitution's local `EFFICIENCY:` explanation and concrete replacement obligation; none is authorized by this plan as an unexplained exception.

## Project Structure

### Documentation (this feature)

```text
specs/007-integrate-bvh-culling/
├── spec.md
├── checklists/requirements.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    ├── runtime-selection.md
    ├── spatial-replay.md
    └── benchmark-results.md
```

`tasks.md` is future output of speckit-tasks and is deliberately not generated by planning. Implementation evidence will live in this feature's `evidence/` or a checksummed benchmark bundle referenced from it.

### Source Code (repository root)

Proposed additions are marked `(new)`; this plan does not create these source files.

```text
include/platformator/
├── spatialsettings.h                 (new public settings)
├── runtimeoptions.h                  (append settings)
└── collider.h                        (compatible projections + neutral membership)
src/
├── spatialindex.h/.cpp               (new private bounds/context/backend coordination)
├── physicspairstate.h/.cpp           (new extracted common pair owner)
├── renderculling.h/.cpp              (new private visibility owner)
├── dynamicaabbtree.h/.cpp            (existing adapter, extend policy/metrics)
├── box2d_tree_bridge.h/.c            (private marked-update/partial-rebuild bridge)
├── grid.h/.cpp, gridcell.h/.cpp       (selected segmented spatial context)
├── aabb.*, segmentedintervallist.*, localsortarray.*
├── collider.cpp, physicsmanager.*, physicscontacts.*
├── gameobject.cpp, gamemanager.cpp, sprite.cpp, animator.cpp
├── sdlwindow.*, camera.cpp, runtimeoptions.cpp
├── benchmark.h/.cpp, benchmark_main.cpp
└── spatialreplay.h/.cpp              (new benchmark-only trace runner)
benchmark/
├── generate_spatial_traces.py        (new bounded deterministic cases)
├── sweep_spatial.py                  (new independent-process workflow)
├── spatial_resource_hooks.cpp        (new resource-executable-only hooks)
├── sweep_scaling.py, plot_scaling.py  (version-aware readers, preserve v3)
└── README.md
tests/
├── spatial_selection_test.cpp        (new SDK/parser/selected-only tests)
├── spatial_pair_state_test.cpp       (new common pair/retirement tests)
├── spatial_replay_test.cpp           (new independent dynamic oracles)
├── render_culling_test.cpp           (new geometry/lifecycle/order tests)
├── render_culling_image_test.cpp     (new SDL parity fixtures)
├── test_spatial_benchmarks.py        (new format/sweep/eligibility tests)
└── existing physics/lifecycle/package/segmented/tree tests
vendored/box2d-3.2.0/                  (retain source subset and provenance)
CMakeLists.txt, relevant SDK usage documentation
```

**Structure Decision**: Keep product code in existing SDK/runtime directories. Introduce only ownership boundaries needed by both consumers. Benchmark traces/hooks belong to benchmark-enabled targets, not installed public interfaces. Avoid a plugin framework, second worker pool or duplicated segmented algorithm. The resource target links hooks separately from the normal timing executable.

## Design and integration sequence

### 1. Freeze the control and expose startup selection

Preserve baseline source/build/raw captures before genericization. Repeat existing headless segmented/none scenarios after each material structural change using common equivalent timing boundaries. Do not presume the old runner accepts new trace flags. Add settings, validation, parser parity and manifests without activating unused indexes. Preserve existing aggregate initializers and installed dependency closure. See [runtime selection contract](contracts/runtime-selection.md).

### 2. Separate pair state from segmented indexing

Move canonical pair storage, adjacency, pending narrow-phase queue/index repairs and final removal callbacks from Grid into PhysicsManager-owned PhysicsPairState. Retain existing cell-witness count semantics; BVH contributes deduplicated 0/1 support. Preserve queueing of retained dirty pairs and response/support refresh independently of overlap transitions. Do not duplicate solver/contact paths or retain a dormant Grid as a BVH database.

Then adapt private projections/cells to spatial owner/kind plus context-level pair operations, preserving public collider-facing names. Check layouts/allocation counts and same-method timing before building conclusions on top. Reuse the actual grid-plus-segmented method for visibility in a separate owned context; never create fake gameplay Collider components. Initial constants match the existing 200-unit grid and 32-endpoint chunks and appear in provenance.

### 3. Integrate incremental collision BVH

Extend the private bridge with marked insert/move and partial rebuild support, retaining upstream files unchanged. Default native-partial performs initial full construction, then one partial maintenance pass per changed batch; full-rebuild is an explicitly recorded control. Preflight bounds before mutation. Store generation-aware identity in stable backend records; Box2D reusable node IDs remain private implementation handles.

Coalesce desired membership and geometry/filter/response flags. Apply selected writes and maintenance exclusively; then query added/geometry/filter-dirty endpoints against immutable tree state. Union discoveries with old adjacency, canonicalize/deduplicate, exact-postfilter and check both mask directions; reduce through common pair state. Default padding is zero; optional envelope padding never suppresses exact dirty queries/reevaluation. Unchanged steps do no global all-pair rescan. Filter-only changes and sleeping/removal/scene paths are first-class tests.

### 4. Add current-frame rendering culling

Keep none's direct sprite-vector walk. Culling methods share dirty-bound preparation and valid-view handling; linear caches the same bounds so caching is not misattributed to indexing. Match actual destination-space rotation and anisotropic scaling, outward rounding/raster guard, current parent-position semantics and signed draw dimensions. Aspect ratio changes invalidate shape extents; camera motion updates its query. Invalid/unsupported state conservatively falls back with recorded reasons.

Publish dirty visibility immediately before each real rendered frame, after current mutations/transforms, including paused/zero-step frames. Maintain only the chosen method; headless/no-camera does no hidden index work. Unique refined hits plus uncullable sprites sort by current windowIndex, preserving swap-removal and transparent order. Reusable result caches depend on geometry/index, view and draw-order revisions; all culled choices can reuse them. Retire proxies before their components disappear and after readers complete.

### 5. Deliver the experiment and comparison

Implement versioned replay through the actual selected cores, with explicit collision precomputed-bounds exclusion and timed visibility bounds formation. Separate replay samples from real steps and actual rendered frames. Add schema 4 without rewriting schema 3; time cleanup-initiated physics reconciliation as well as the existing named broad-phase scope. Use resource-only hooks/capacity accounting outside timing binaries.

Generate the bounded matrix, validate fixed inputs and real engine behavior, then collect >=5 independent processes per reported case/configuration. Freeze tuning on separate calibration inputs. Report initialization, latency tails, memory/allocation coverage, worker limits, fallback, regressions and unavailable cases. No automatic adoption or speedup criterion follows from completing the prototype.

## Complexity, hot paths and alternatives

For N proxies, D dirty endpoints and K output discoveries, BVH updates generally follow affected tree paths plus explicitly charged maintenance. A query can degrade to O(N) under overlapping/skewed geometry; pair output itself can be O(N²). Do not promise balanced O(log N) behavior for every dynamic tree state. Exact postfilter/adjacency reconciliation, canonical reduction and returned output remain real work. Stable records and task-local reusable buffers avoid pointer invalidation and recurring per-hit allocations.

Segmented work depends on affected cells, endpoint movement/chunk repairs, checkpoint crossings and witness output, rather than just global N. Large bounds/camera ranges amplify cell memberships and duplicates. Global sort can be simpler/faster for heavily changed lists; batch merge can reduce insertion movement; adaptive local/global repair adds thresholds and state. Preserve the current local segmented algorithm as the required control first, measure skew/dynamic/churn costs, and discuss these alternatives in the report rather than silently bundling a second SAP optimization into the BVH comparison. No paper constant or single-thread result is adopted as a modern performance guarantee.

Linear visibility queries cost O(N) on changed views/bounds; unchanged-result caching avoids artificial repeated work. Indexed hits need exact refinement and O(V log V) ordering for V retained candidates, with reusable buffers, rather than an unconditional N-sprite order scan. For near-100% visibility the scan/control may win. None still pays N SDL submissions. Memory reporting separates index, common pair state, proxy/binding replication and scratch high-water retention; dense pair storage can dominate either index.

### Parallel assessment

- **Segmented**: keep existing independent cell tasks, buffered operations, TLS deltas, post-worker canonical reduction and delayed empty-cell erasure. Generic owner changes must not serialize cells.
- **BVH**: serial exclusive insertion/removal/category/move/maintenance, followed by oneTBB independent dirty-collider queries with task-local results. Parallel query setting uses at least two independent queries and the existing worker allowance; this is a capability condition, not a tuned crossover. Serial is an explicit calibration/control option.
- **Rendering**: one camera query and ordered SDL submissions stay sequential. Bound preparation uses coalesced dirty records; do not add guessed parallel thresholds or call SDL from workers. Independent bounds snapshots may be parallelized only if measured granularity justifies it and ownership remains disjoint.
- **Reduction/lifetime**: pair sorting/support/contact reduction, callbacks, solver and reclamation retain existing dependency order. Record serial write/rebuild/reduce/order costs as scaling limits. Test small/large dirty batches, skew, memory pressure and one/multiple workers before claiming scaling or setting a tuned cutoff.

## Validation and acceptance strategy

| Obligation | Required evidence |
| --- | --- |
| FR-001–003, FR-014 / SC-001 | Default compatibility, all eight SDK/parser combinations, invalid settings; source and installed/relocated consumers |
| FR-004–009 / SC-002 | Dynamic exact pair oracle, symmetric 64-bit masks, contained padded moves, stale generation/node reuse, cell witnesses; actual narrow/contact/support/response/sleep/event tests |
| FR-010–013 / SC-003–004 | Independent projected-quad and image parity, ten-of-1,000 submissions, transparent/current order, invalid view fallback, paused/zero-step/late changes and selected-only work probes |
| FR-015–017 / SC-008 | Paper invariants, controlled/randomized dynamic cases, existing physics/lifecycle/scene regressions, ASan/UBSan and available TSan, one/multiple workers; predeclared fixture tolerances |
| FR-018–020 / SC-005–006 | Immutable trace and engine fixture matrix, sparse scales/change/visible fractions, dense/skew/mixed/churn/camera companions |
| FR-021–025 / SC-007 | Complete boundaries/init/tails/work/resource coverage, strict schema4/legacy3 readers, >=5 process repetitions, provenance and eligibility ledger |
| FR-026–027 / SC-008–009 | Frozen same-method pre/post control, investigated regressions and reproducible comparison report with unverified limitations |

Independent references cannot call the implementation's own overlap/bounds helpers as their only oracle. Match fixed-input pair/contact transitions exactly; actual solver-driven trajectories additionally use physical invariants and fixture-specific predeclared numerical tolerances. Same-renderer software image fixtures require identical pixels, with hardware evidence separately qualified. All expensive references/readbacks/resource hooks are excluded from timing captures. Detailed runnable scenarios are in [quickstart.md](quickstart.md).

## Risks and bounded responses

- Public projection exposure makes segmented genericization the main compatibility/performance risk. Preserve source names, measure layouts/allocations and the same-method control; investigate overhead before proceeding to performance conclusions.
- Shared pair-state extraction can break support teardown or leave queued pointers. Preserve final-removal order and stable worker batches, test callback destruction/replacement and last-support transitions before BVH integration.
- Native partial maintenance requires marked changes and its own schedule. Add focused bridge tests and record actual rebuild work; do not infer moved-leaf enumeration semantics from a rebuild count.
- Cached sprite bounds depend on current rendering semantics and complete invalidation. Test every producer and custom SDL state; conservative recorded fallback protects output while exposing unsupported performance cases.
- Different solver order can change subsequent inputs. Use matched replay for index causality and actual engine fixtures for integration; do not turn chaotic trajectory divergence into an unqualified index speed claim.
- Hardware/sanitizer environments may be unavailable. Record exact limitation and keep those measurements unverified rather than substituting different timing boundaries or hiding failures.

## Complexity Tracking

No constitution violations or exemptions are requested. Added ownership, queues and revision caches have explicit correctness/scaling purposes in [data-model.md](data-model.md). No unresolved design clarification remains. Runtime implementation, tests, performance captures and `tasks.md` are future work.
