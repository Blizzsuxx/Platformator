# Implementation Plan: Incremental Render Culling

**Branch**: `branch-without-bvh` | **Date**: 2026-10-29 | **Spec**: [spec.md](spec.md)

**Input**: `specs/009-incremental-render-culling/spec.md`

The setup script reports feature identifier `009-incremental-render-culling` as `BRANCH`, resolving `.specify/feature.json`. The actual Git branch remains `branch-without-bvh`; planning does not switch branches.

## Summary

Add one optional renderer-owned cache/index component. Reuse feature 001's affected-item publication and indexed queue conventions to maintain conservative sprite bounds, query a small independent spatial grid when inputs change, and reuse an ordered visible list otherwise. Keep the existing authoritative sprite vector/order and current SDL drawing geometry. Camera translation reuses bounds; actual projection changes refresh them.

Choose a render-only sparse grid as the simplest first maintained spatial index, with view-sized cells, at most four memberships per ordinary sprite, and an explicit oversized/uncertifiable candidate list. No dependency, collider broad-phase refactor, gameplay camera collider, public backend framework, or new worker pool. A grid is a hypothesis to validate, not a claim that it beats BVH or cached linear everywhere.

Compare `current-linear`, `cached-linear`, and `indexed` through internal benchmark selection. Gate acceptance on image/lifetime correctness, affected-work/resource checks, and fresh complete-cost evidence, including added simulation notification costs. Preserve feature 008 and its durable evidence.

## Technical Context

**Language/Version**: C++20 runtime/tests; Python 3 standard-library benchmark tooling.

**Primary Dependencies**: Existing SDL 3.4.18, SDL_image 3.4.8, SDL_ttf 3.2.2, SDL_mixer 3.2.4, Eigen 5.0.1, oneTBB 2022.3.0, nlohmann JSON, and C++ standard containers. No new library or vendored changes.

**Storage**: O(N) optional in-memory records, memberships, pooled cells, queues, and visible/candidate buffers. No serialized cache or scene-format change. Durable ignored source/build/proof/raw timing bundles under `benchmark/artifacts/linear-render-culling/009-<run-id>/`.

**Testing**: CTest geometry/index/work/lifecycle/render integration tests; independent double corner oracle and corresponding-frame image/order comparisons; ASan/UBSan; targeted TSan worker-publication checks; public-consumer/package and capture-contract tests; separate work-probe and Release timing builds.

**Target Platform**: Existing SDL desktop targets. Mandatory deterministic SDL software validation on the available Linux host. Record queried backend/output/mapping/VSync; hardware/GPU performance remains unverified unless separately captured.

**Project Type**: Native engine/library with runner, examples, tests, and benchmark tools.

**Performance Goals**: Unchanged visibility preparation O(1), plus O(V) current drawing. Changed frames update only M affected bounds, query local candidates, and sort only retained ranks. Zero warmed-capacity recurring visibility allocation and zero added bookkeeping in headless/disabled modes. Demonstrate static gains over current linear and additional indexed gains over cached linear beyond independent-run variation; no invented percentage target.

**Constraints**: Same-frame state, conservative projected-then-rotated geometry, current order and lifetime boundaries, direct Camera API, opt-out/custom-mapping semantics, no physics behavior/scheduling changes. Numeric safety may choose explicit fallback rather than unsafe rejection. No full population scan to discover dirty entries or restore sparse-query order.

**Scale/Scope**: One main camera; tiny/empty scenes, 1,000, 10,000, and 50,000 sprites; 0%, 1%, 10%, and 100% changed populations; view motion, giant/skewed/dense populations, output changes, churn, scene replacement, representative engine controls; one and available physical-core workers. These fractions/counts define validation workloads, not runtime tuning thresholds.

## Constitution Check

*Pre-research gate: passed with research obligations identified. Post-design gate: passed as a design; implementation and performance acceptance remain unverified.*

| Principle | Before research | After design |
|-----------|-----------------|--------------|
| I. Performance/scalability | Assess cache-only, grid, dynamic tree, and existing SAP; locate repeated work. | Affected bounds, local four-link memberships, cached ordered results, bounded pooled storage. Explicit O(N) dense/oversized/fallback limits and O(V log V) changed-result ordering; include cold cost, churn, density, 50k, tails, memory. |
| II. Multicore | Trace worker writes and owning-thread publication; assess parallel maintenance. | Reuse existing TBB motion result merge; preserve rotation mask; no worker index mutation. Sequential owner-side maintenance/query justified by small batches/dependencies/SDL order. Test 1/physical-core workers and profile fully dynamic serial cost; evaluate TBB bounds computation if material. |
| III. Collision architecture | Render membership must remain independent. | No collider/index/pair/solver change. Physics merge retains an already produced change mask solely for render publication; integration/scheduling remain unchanged. Foundational SAP modification and paper-specific broad-phase work are out of scope. |
| IV. KISS | Reuse existing queues, ranks, registry identities, geometry, capture tools. | One private state owner; fixed four memberships; standard map-node reuse; explicit oversized list. No event bus, public strategy framework, duplicated scene registry, general allocator, or new library. Every field/invalidation rule is listed in the data model. |
| V. Developer experience | Existing scenes/API should work without manual invalidation/colliders. | Same setters/configuration/scene formats and direct Camera semantics. Internal hooks/friend declarations may change private layouts; consumers rebuild. No Unity behavior claims or new transform model. |
| VI. Evidence | Preserve 008 baseline; old headless/submit-all results cannot prove index value. | Fresh three-strategy proofs/timings, frozen pre-edit controls, complete render and simulation costs, paired end-to-end intervals, independent repetitions/uncertainty, durable relocated verification. SC-006 gates normal-path acceptance. |
| VII. Correctness/lifetimes | Audit all mutations, actual numeric geometry, retirement/order. | Owner-thread publication, queue cancellation and synchronous unlink before free, existing handles/ranks, projection certificate plus conservative fallback, mutation/worker/scene/order/sanitizer coverage. |

No constitution exception or permission request is needed. Correctness/provenance failures block performance acceptance; a missing index benefit requires revisiting the design rather than declaring caching alone complete.

## Project Structure

### Documentation

```text
specs/009-incremental-render-culling/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── checklists/requirements.md
└── contracts/
    ├── rendering.md
    └── validation-evidence.md
```

`tasks.md` belongs to the subsequent tasks command and is not created by this plan.

### Planned source areas

```text
src/
├── renderculling.h/.cpp          # internal records, queue, grid, reuse, selection
├── spriterendergeometry.h        # maintained proxy/certificate + exact refinement
├── camera.cpp                   # shared geometry/draw helpers; selected draw skips re-culling
├── sdlwindow.h/.cpp              # optional owner, membership repair, view/cache dispatch
├── gamemanager.cpp              # affected transform publication, scene-boundary reset
├── gameobject.cpp               # gated changed rotation/scale publication
├── sprite.cpp                   # gated dimensions/texture-eligibility publication
├── physicsmanager.h/.cpp        # retain motion mask; publish after worker join
└── rigidbody.cpp                # synchronous move rotation publication
include/platformator/
├── gameobject.h                 # private helper/friend access only as needed
├── sprite.h                    # private helper/friend access only as needed
└── camera.h                    # preserve public API; private shared-helper declarations
tests/
├── render_culling_geometry_test.cpp
├── render_culling_index_test.cpp # numeric/cell/link/pool/order invariants
├── render_culling_test.cpp       # actual frames, mutations, fallback, lifetime
├── render_culling_fixture.h      # changed fractions, spread, giant/resize trajectories
└── runtime_work_probe.h          # validation-only affected work/resource observations
benchmark/
├── linear_render_probe.cpp      # internal modes, complete-cost and cold/resource evidence
├── capture_linear_render.py     # explicit incremental matrix/version, strict proof matching
├── tests/test_linear_render.py
└── README.md
CMakeLists.txt                   # sources/tests and complete fingerprint inputs
.gitignore                       # existing durable artifact rule already covers this feature
```

**Structure Decision:** Keep the existing public/private split. `RenderCullingState` is a concrete private component, not a polymorphic service. Its private grid may be factored locally for invariant tests; do not expose an installed spatial API. Extend current tooling rather than create a second recorder/capture framework.

## Phase 0: Research Outcome

[research.md](research.md) resolves mutation publication, physics rotation, index alternatives, geometry coordinates/numeric fallback, order/lifetimes, memory reuse, parallelism, and benchmark attribution. Local primary source is sufficient; no external dependency or unresolved clarification remains.

## Phase 1: Design

### Runtime flow

1. Preserve renderer-null, clear/present, no-camera, debug, opt-out, and direct Camera behavior. Create optional state only for renderer-backed cached strategies. Registration before first render may create records/queue entries; bounds/grid construction waits for a valid projection.
2. Publish changed synchronized positions, independent geometry setters, and merged physics rotation through a gated `SDLWindow` notification. Only registered affected sprites enter the deduplicated queue. Membership/removal uses existing window registration hooks.
3. At render preparation, check mapping once and compare value-based camera/projection keys. Mapping fallback draws current membership through submit-all and leaves bounded dirty work pending. No camera does not query/build bounds; destruction still cancels references.
4. For a genuine projection change, refresh live bounds and rebuild grid explicitly. Otherwise drain only queued entries. Repeated edits before preparation produce one final bounds refresh. Index relinking may be skipped when cell coverage is unchanged, but stored geometry must be current.
5. If the ordered result and view key remain valid, reuse it directly. Camera position changes invalidate only selection. Numeric-unsafe camera selection uses the original classifier over current membership on a cache miss and may cache that ordered result; it does not rebuild bounds simply because the camera moved.
6. On an ordinary indexed miss, query current view cells plus the oversized/uncertifiable side list; deduplicate, refine with the existing current-view classifier, then sort only retained window ranks. On a cached-linear miss, scan maintained records without constructing/querying a grid. Cache ordered ranks and the effective view key.
7. Draw selected Sprites from current content using existing geometry/SDL formulas with repeated culling disabled. Cached visibility never freezes texture/source/flip/animation data. Candidate refinement's extra geometry work is timed; do not introduce a second persistent drawing-packet cache by default.
8. Preserve overlays/readback/presentation. Validation counters/traces compile out of timing/normal builds.

### Publication and lifetime boundaries

See [data-model.md](data-model.md) and [rendering contract](contracts/rendering.md). Dense records align with existing `windowIndex`. Queue cancellation, intrusive unlink, side-list repair, and last-record relocation are bounded operations per membership mutation (at most four links). Invalidate selection before a pointer can retire. Existing `ObjectHandle` includes runtime and generation; do not derive identity from addresses or file IDs.

An actual scene-clear/replacement boundary in `GameManager::commitMutations`, after old membership is removed and before new membership is published, resets/releases optional render state. Do not release capacity whenever ordinary churn transiently empties membership. Runtime/window shutdown releases all state. Failed scene preparation leaves the live scene/cache consistent. Reset/reclamation time belongs to complete-step/scene-transition or teardown measurements.

### Bounds and spatial representation

Use projection-dependent world proxies for the actual scale-then-rotate geometry. Cells are view-sized and contain only Sprite ranks, independently of gameplay cells. A normal proxy spans at most four cells; all other cases have explicit candidate fallback. A projection certificate bounds camera-relative floating error without needing a camera-pan bounds pass. The numeric contract, safe-domain exit behavior, and proof obligations are in [contracts/rendering.md](contracts/rendering.md).

Reserve reusable cell **nodes as well as buckets**. At growth, prepare enough nodes for at most four memberships per reserved record, plus scratch/queue/side-list capacities. Recycle extracted empty nodes; no per-cell dynamic vectors or retained empty-coordinate history. Charge memory and cold allocation cost. If this cost defeats the candidate, report it and revisit the representation.

### Complexity and limitations

Let N be registered sprites, M coalesced affected sprites (including descendants), C unique candidates, V retained sprites, and F oversized/uncertifiable entries.

| Case | Preparation cost | Drawing |
|------|------------------|---------|
| Unchanged prepared view | O(1), no query/order rebuild | O(V) |
| Changed M, supported projection | O(M) bounds and expected O(M) hash/link maintenance, then selection | O(V) |
| Indexed selection/camera pan | Expected O(visited cells + C + F + V log V); no mandatory N order pass | O(V) |
| Cached-linear miss | O(N) maintained-bound visits and exact candidate refinement; existing rank order needs no sort | O(V) |
| Cold/projection rebuild | O(N) bounds/membership plus allocation/growth and selection | O(V) |
| Dense, oversized, unsafe camera | Can approach O(N), plus indexed ordering where used | O(V) |

Storage is O(reserved N), including at most 4N membership links/cell-node capacity and O(N) scratch/visibility state. Hash operations have expected, not worst-case constant, cost. All-visible indexed selection can sort N ranks; do not claim it improves fully dynamic dense scenes. No automatic threshold switching or parallel-query framework is planned. Such adaptations require evidence and bounded follow-up.

### Validation and acceptance sequence

1. Freeze/hash the pre-engine-edit reference and common harness in a fresh ignored bundle. Confirm 008 source parity before using it as baseline.
2. Implement numeric enclosure and pure grid/queue/link invariants; independently validate them before enabling index rejection.
3. Integrate every notification/lifetime path, including physics rotation and late callbacks. Prove work counts and corresponding images/order across the mutation matrix.
4. Prove stable-capacity allocation/reclamation and disabled/headless inactivity; run applicable CTests, public-consumer/package checks, sanitizers and worker checks.
5. Capture matching output/resource proofs for each performance case in a separate work build. Then build uninstrumented Release and measure independently shuffled processes per [validation-evidence.md](contracts/validation-evidence.md).
6. Investigate gains/regressions using process-level uncertainty, tails, cold cost, retained bytes, fallback/candidate rates, simulation cost and paired complete-frame intervals. Require cache and additional indexed benefit (SC-006) before accepting indexed as the enabled normal path. If absent, preserve the unfavorable result and revisit the design; do not mark the feature complete or remove the comparison.
7. Retain source/build/commands/raw/proofs/checksums and verify a relocated bundle without original temporary paths. Update concise feature evidence and reproduction documentation.

### Focused-first execution update

The next measurement is the explicitly exploratory `pilot` phase, using the user's selected representative cases and short uninstrumented captures. It bypasses the full phase-root/frozen-control/per-frame-trace prerequisites but keeps matching workload, backend, resource-checksum, source/build, and raw-sample checks. Pilot output screens likely gains and regressions; it does not replace steps 5–7 or close SC-006/SC-007. Preserve the original full acceptance inventory and its unchecked tasks until those gates pass.

### Requirement coverage

| Requirements | Design location |
|--------------|-----------------|
| FR-001–005, SC-002–003 | Grid/bounds, affected publication, cache/view keys, selection/work probes |
| FR-006–008, SC-001/004 | Numeric certificate/refinement, live drawing, rank order, mutation/image oracle |
| FR-009–012, SC-004/005 | Lifecycle cancellation, independent membership, fallback, inactive-state gates |
| FR-013–014, SC-005 | Four-link/node budget, cold reservation/reset, one owner/no dependency |
| FR-015–018, SC-006/007 | Three strategies, pinned parity controls, complete-cost matrix, durable verification |

### Post-design gate recheck

All seven constitution principles are addressed without an exception. Numeric proof and comparative gains remain required implementation evidence. This planning run adds only documentation; engine code, benchmarks, and feature 008 artifacts remain unchanged.

## Complexity Tracking

No constitution violation. Each added state has a bounded purpose: record/dirty index avoids discovery scans; proxy avoids repeated geometry; four links/pooled cells enable local allocation-free migration; side list preserves oversized/uncertain coverage; query stamp prevents duplicates; ordered vector permits unchanged reuse; view/projection keys govern validity. Cold pool and all-visible sorting costs are explicit acceptance risks.
