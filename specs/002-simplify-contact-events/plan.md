# Implementation Plan: Simplify Contact Events

**Branch**: `001-cpp-engine-cleanup` (actual Git branch) | **Date**: 2026-10-25 | **Spec**: [spec.md](spec.md)

**Input**: `specs/002-simplify-contact-events/spec.md`. Setup resolved the workflow identifier `002-simplify-contact-events` from the feature directory; it did not change the actual Git branch. Keep feature selection independent of Git branch naming.

## Summary

Generate Enter and Exit directly from confirmed-contact transitions, including explicit pair removal, and remove Stay callbacks, observer tracking and reporting. Keep physical contact, solver, body and support indexes where they serve distinct consumers. Remove the redundant `activeCollisions` solver projection and unused narrow-phase aggregation declaration. Make geometry one cohesive worker-owned operation and let ScriptComponent own pending behavior initialization while GameManager retains queue scheduling.

The requested smell audit covers all four named areas and follows their dependencies into ColliderPair destruction, Grid pair erasure, public callbacks, debug consumers, benchmark producers/readers and mutation tests. The design removes unnecessary state and misplaced responsibilities without changing collision calculations, sleep algorithms or startup scheduling semantics. [research.md](research.md) records evidence and rejected alternatives; [data-model.md](data-model.md) defines ownership and invariants.

## Technical Context

**Language/Version**: C++20 runtime and tests; Python 3 standard-library benchmark tooling, optional matplotlib plotting.

**Primary Dependencies**: Existing vendored SDL3 family, Eigen 5.0.1, oneTBB 2022.3.0 and nlohmann JSON; CMake/Ninja. No new runtime dependency, scheduler, service or framework.

**Storage**: In-memory candidate pairs, optional confirmed contacts, indexed physical work lists, deferred lifecycle queues and reusable event/scratch vectors; JSON scene serialization unchanged. Benchmark CSV/JSON moves to schema 3.

**Testing**: Existing CTest executable suite and Python unittest benchmark suite; focused internal contact-event test; existing cleanup-debug, cleanup-work, cleanup-asan, cleanup-tsan and cleanup-benchmark presets; public-header and package checks.

**Target Platform**: Existing native desktop library/runner targets; primary validation on Linux/GCC with headless SDL. Do not claim unrun platform or sanitizer validation.

**Project Type**: Native game-engine library with runner/examples and benchmark CLI; Python editor changes only if a direct compatibility consumer is found.

**Performance Goals**: Eliminate the event-only O(observed contacts) traversal on persistent contacts and all Stay subscription maintenance. Event generation uses existing changed-pair reduction plus actual removal transitions; order only generated events. Preserve warmed idle allocation reuse, affected-body work and existing multicore geometry/cell processing. Establish runtime impact through equivalent baseline measurements, not an invented speedup or latency target.

**Constraints**: Confirmed shape contact differs from broad-phase overlap; preserve witness deduplication, support totals, solver order, sleeping/waking, raw-pointer lifetime boundaries, start-before-dispatch, callback exceptions and safe mutation. Stay removal deliberately breaks source/binary callback compatibility; schema 3 deliberately breaks reporting compatibility. No per-script contact histories, new public event-observation API or recurring replacement for Stay.

**Scale/Scope**: Small and large populations, stationary/sleeping and moving contacts, script-heavy persistent contacts, insertion/removal churn, migration and multi-cell pairs, uniform/skewed layouts, one and multiple workers. Existing reference populations are broad 100/500/1000, dense broad 50/150/300, narrow 10/100/200 elements, rigid 100/500/1000 bodies and churn 10/100/1000 with 1000 steady bodies. Add a bounded persistent-pair fixture at 10/100/1000 pairs with zero/one/two scripted endpoints. These are validation workloads, not product capacity promises.

## Constitution Check

The initial gate passed before selecting the research decisions. Post-design results below assess the design and its required verification; they do not claim implementation or benchmark completion.

| Principle/gate | Before research | Post-design evidence and implementation obligation |
| --- | --- | --- |
| I. Performance and scalability | Pass: remove recurring listener-contact work and assess physical indexes. | Pass: changed-pair reduction replaces observer enumeration; retain affected-body/adjacency indexes; remove duplicate solver projection; record allocation and timing comparisons across populations/churn/workers. |
| II. Multicore design | Pass: preserve oneTBB and establish worker/reduction boundaries. | Pass: independent cell, geometry and body integration stay parallel; queue/list/support mutations stay after joins; shared-body solver and arbitrary callbacks stay sequential. Measure granularity, serial reduction/sort cost, contention/load imbalance and end-to-end scaling. No worker-pool addition. |
| III. Collision architecture | Pass: inspect existing pair transitions/witnesses and local paper §§3.1–3.3. | Pass: no segmented sweep redesign; preserve ties, chunk/checkpoint and migration invariants. Explicit removal runs after consolidated witness reduction; candidate overlaps never imply confirmed contact. Keep existing broad-phase regressions, including duplicate witnesses. |
| IV. KISS and responsibility | Pass: investigate each flagged area, retain only justified state. | Pass: remove Stay state and destructor queueing; eliminate `activeCollisions`; consolidate geometry; move per-script startup into its owner. Retain physical indexes and indexed queue semantics with explicit invariants. No generic container/lifecycle framework. |
| V. Developer experience | Pass: define public compatibility and intentional deviation. | Pass: retain Enter/Exit naming and payloads; remove Stay without shim; update consumers and document migration. Official Unity collision docs confirm Stay exists there; its removal is a deliberate product choice. |
| VI. Evidence | Pass: require a pre-change baseline and representative workloads. | Pass: common fixture first, baseline before runtime changes, then correctness, separate affected-work/allocation probes and independent Release repetitions. Keep historical captures and timing boundaries intact; label schema/counter semantics. Actual speedup remains unverified until measured. |
| VII. Correctness/integrity | Pass: inspect teardown, dispatch, startup and response dependencies. | Pass: explicit two-source transition ownership, deterministic Exit-before-Enter batches, safe dispatch-before-free, preserved support and start semantics; focused regressions and sanitizer/worker comparisons are acceptance gates. |

No constitution exception is required. If implementation reveals a correctness gap, unsupported mutation path or retained known inefficiency, resolve it or document the constitution-required local `EFFICIENCY:` rationale and concrete replacement work; do not silently waive the gate.

## Project Structure

### Documentation (this feature)

```text
specs/002-simplify-contact-events/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── checklists/requirements.md
└── contracts/
    ├── contact-events.md
    └── benchmark-schema.md
```

`tasks.md` is intentionally left to the next `$speckit-tasks` phase.

### Source Code (repository root)

```text
include/platformator/{behavior,collider,scriptcomponent}.h
src/
├── physicsmanager.{h,cpp}        # phases, transition reduction, solver/dispatch
├── physicscontacts.cpp          # physical contact/solver/support membership
├── physicsgeometry.cpp          # all supported shape dispatch/manifold work
├── physicsbodyindexes.cpp       # body membership, change queue, sleep islands
├── colliderpair.{h,cpp}          # pair/contact ownership, no event side effects
├── grid.cpp                     # explicit removePair call before pair erasure
├── gamemanager.{h,cpp}           # startup scheduling and lifecycle boundaries
├── scriptcomponent.cpp          # per-script initialization and callback delivery
├── {behavior,collider}.cpp       # supported Enter/Exit callbacks only
├── benchmark.{h,cpp}            # schema 3 and generation counters
└── benchmark_main.cpp           # invariant consumer and common baseline fixture
tests/
├── contact_events_test.cpp      # planned internal transition/recipient contract
├── callback_mutation_test.cpp
├── scene_transition_stress_test.cpp
├── physics_support_test.cpp
├── physics_geometry_test.cpp
├── runtime_work_{probe.h,test.cpp}
├── regression_tests.cpp
├── queued_deletion_safety_test.cpp
└── benchmark_contract_test.cpp
benchmark/{sweep_scaling,plot_scaling,capture_cleanup}.py
benchmark/tests/test_sweep_scaling.py
benchmark/README.md
docs/CPP_CLEANUP_WALKTHROUGH.md
CMakeLists.txt
```

**Structure Decision**: Retain the existing library and domain-oriented translation units. Geometry moves into its existing owner file. Startup adds one private method to its existing component owner. CMake discovers engine source files through the existing source setup; add explicit registration for the planned test and adjust affected declarations/includes. Search the runner/examples/editor for actual consumers instead of assuming none. Preserve archived specs, benchmark baselines and historical results.

## Phase 0: Research Outcome

Research is complete in [research.md](research.md). The selected design resolves:

- Which pair state defines Enter/Exit, and how removal differs from geometry separation.
- Which contact/body indexes remain necessary, and why `activeCollisions` can be deleted.
- Why body-driven solver refresh must survive while script subscription work disappears.
- How geometry consolidation and per-script initialization ownership address the user's four named smells.
- How event ordering, teardown, metric semantics and public compatibility remain explicit.

All technical unknowns have evidence-backed decisions. No clarification is pending.

## Phase 1: Design and Implementation Sequence

### 1. Establish common workload and baseline

Before runtime edits, add a minimal `persistent_contacts` benchmark fixture and matching sweep options: `--contact-pair-count` in the runner, sweep `--counts`, and `--scripted-endpoints 0|1|2`. Use disjoint persistent static/trigger pairs and a no-op behavior implementing only retained callbacks so the identical fixture compiles before/after Stay removal. Include the existing independent contact/support checks and recorded resolved workload fields. The fixture must actually retain confirmed contacts and produce no recurring movement. Record its source patch/fingerprint with the baseline; do not time an empty-contact scene.

Capture existing workloads and this fixture using schema-2 producer/readers before changing runtime/event behavior. Keep the source revision, fixture patch, build, worker topology/affinity, seed, timestep, warmup and independent-run files. A historical capture is reference material rather than a substitute for a comparable current baseline.

### 2. Make transitions and ownership explicit

In the existing serial post-geometry reduction, enqueue Enter/Exit from `resultHadContact` and current contact presence, with no script query. In `removePair`, enqueue Exit before erasure if contact still exists; eliminate pair-destructor event emission and retirement-based generation suppression. Use the manager enqueue operation for both paths. Remove Stay event kind/callbacks, observer vector/index/dirty state, step markers and the persistent event traversal. Remove unused narrow-phase aggregation state after confirming its absence of consumers.

Keep complete-contact and solver indexes and support delta bookkeeping. Delete `activeCollisions`; resolution and debug access collision through canonical `solverPairs`, retaining dynamic scratch. Narrow `canonicalizePairs` to solver ordering, without rebuilding a second projection. Replace `refreshSubscriptions` with `refreshSolverPairs` for relevant physical state changes; remove all script-only callers and preserve response/support refresh. Test body type changes, sleeping/waking, component replacement, filtering and multi-cell removal before accepting this simplification.

### 3. Consolidate geometry and startup responsibilities

Move shape dispatch, projection testing, box SAT and clipping into `physicsgeometry.cpp`; rename `satCreateCollision` to `updateContactGeometry`. Preserve math, feature IDs, tolerances, warm-start/manifold behavior and version updates exactly. Geometry creates/updates/clears only its own pair contact and never enqueues events.

Extract the per-script startup loop/prefix transfer/logging into private `ScriptComponent::startPendingBehaviors(platformator::ReferenceResolver&)`. Rename the manager entry point `startPendingScripts`, preserving its dynamic indexed queue drain, cancellation, resolver lifetime and phase timing. Do not move failure handling across the batch or change which attempted behaviors become runnable. Keep body membership and sleep islands in `physicsbodyindexes.cpp`; its physical solver dependency now has a correct name. These bounded refactors must retain the existing mutation/startup and geometry tests before further cleanup.

### 4. Finalize event delivery and reporting contracts

Sort nonempty generated-event batches at dispatch by Exit-first family and canonical complete handles, covering both regular and cleanup drains. Keep endpoint A-before-B delivery, per-endpoint exception aggregation and borrowed-payload lifetime boundaries. Do not add per-script history, duplicate suppression maps or an asynchronous event bus.

Advance all live benchmark output/readers/fixtures to schema 3; remove Stay metric and increment Enter/Exit at enqueue with frame-scoped accumulation, including cleanup. Preserve other metric definitions, scope boundaries, provenance, capture validation and plot safeguards. Keep old captures untouched and reject incompatible/mixed current input clearly. See [contact-events.md](contracts/contact-events.md) and [benchmark-schema.md](contracts/benchmark-schema.md).

### 5. Carry verification and migration into tasks

Add internal contact-event coverage that observes generated records before delivery, independently of callbacks. Cover zero/one/two eligible endpoints, 100 quiet updates, late attachment/no replay, re-contact, rejected candidates, both endpoint removals, filter/deactivation, callback exceptions/mutation, scene replacement and multi-cell witnesses. Assert actual ordered traces across one and several workers. A newly eligible script may receive Exit without Enter; this must be an explicit regression.

Adapt Stay-specific tests without deleting their unrelated lifetime/physics coverage. Extend work probes to count transition comparisons and appended records; quiet observed and unobserved contacts must both show zero after warmup. Keep zero-allocation warmed idle validation. Cover moving persistent contacts too: changed-pair geometry/reduction may run, but no separate unchanged-contact event traversal may remain. Preserve detailed startup queue/failure tests and support/sleep/wake/oracle tests. Verify schema inventory and cleanup/out-of-frame event counting with the recorder.

Build public headers and independent package consumers after the callback contract break; update examples/current docs and explain consumer rebuilds. Search all current source/tooling for Stay-specific symbols/columns, distinguishing historical files and ordinary uses of the English word “stay.”

## Hot Paths, Complexity and Parallel Strategy

Let P be pending evaluated pairs, E generated events in a dispatch batch, S solver contacts, C confirmed contacts, D affected collider adjacency and B affected bodies.

| Work | Chosen strategy and cost |
| --- | --- |
| Cell updates / pair witnesses | Keep existing oneTBB cell processing and task-local results; serial consolidated witness changes after joins. No sweep-and-prune algorithm change. |
| Pair geometry | Existing thresholded oneTBB over P, pair-local mutable results and immutable published geometry. No shared event appends or support/list mutations in workers. |
| Transition/support reduction | Existing serial O(P) pass plus O(removed confirmed pairs) explicit removal work; no O(C) or O(observed C) event-only scan. |
| Event order/delivery | O(E log E) ordering, then recipient/behavior delivery; no work over persistent contacts when E=0. Callbacks remain serial because arbitrary game state/lifetimes are shared. |
| Solver membership | O(1) indexed updates and O(D) affected adjacency refresh. Canonical O(S log S) sorting only on membership change; remove the projection rebuild. |
| Solving and body integration | Preserve serial shared-body solver, dynamic iterative scratch and parallel independent force/movement work. Do not assume adjacent contacts are independent. |
| Support/body reconciliation | Preserve deduplicated deltas/change queues and O(B) affected body membership work; keep physical adjacency updates independent of scripts. |
| Startup | O(queued scripts + attempted behaviors) using existing vector capacity and tombstones; no contact-adjacency traversal caused by script startup. |

Removing the solver projection adds a pair-to-contact dereference for required solver/debug visits while removing a vector and its rebuild. Measure that tradeoff, event sorting under churn, allocator reuse, serial reductions, task granularity, load balance and worker scaling. Retain existing thresholds unless measurements justify a change. No new lock contention or task scheduling is introduced for events/startup; false-sharing and bandwidth effects remain part of representative profiling if results warrant it.

## Validation and Acceptance

[quickstart.md](quickstart.md) is the runnable validation guide. Runtime correctness and timing checks are planned work, not results from this planning command.

| Requirement coverage | Evidence to retain during implementation |
| --- | --- |
| FR-001–FR-004 / SC-001–SC-002 | Internal generated-event traces and shape/witness/re-contact cases; quiet 100-update history. |
| FR-005–FR-006 / SC-003 | Recipient matrix, mid-contact script changes, independent endpoint delivery and no replay. |
| FR-007–FR-008 / SC-004, SC-006 | Current symbol/metric audit, affected-work counters, idle allocations, equivalent persistent-contact timing fixture. |
| FR-009–FR-010 / SC-005 | Support/sleep/geometry regressions, startup mutation/failure tests, teardown/scene integration, ordered serial/parallel traces, ASan/UBSan and TSan where runnable. |
| FR-011 | Schema-3 C++/Python contract results including cleanup generation, no-script counts, out-of-frame behavior and historical-version rejection. |

Performance acceptance requires correctness first, independent repetitions (at least five for reported timing aggregates), complete-step and relevant nested-scope means/tails, retained raw samples/provenance, allocation/retained-storage observations, and investigated regressions. Include observer-heavy and observer-free persistent scenes, dynamic scenes, churn, skewed/multi-cell layouts and worker sweeps. Do not infer total time by summing scopes, mix instrumented validation into timings, relabel old event counts, or assert speedup without comparable measurements.

## Post-Design Gate Result

All seven constitution gates pass for this design; no unresolved clarification or justified exception remains. Phase 1 outputs are complete. Next phase is `$speckit-tasks`; implementation and measured validation remain outstanding.
