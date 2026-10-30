---
description: "Dependency-ordered work for transform reads, same-value setters, and compact comparative evidence"
---

# Tasks: Efficient Transform Reads and Setters

**Input**: `specs/010-optimize-transform-setters/{spec,plan,research,data-model,quickstart}.md` and `contracts/runtime.md`

**Status**: Prior code changes are provisional. An unchecked task may require reviewing or replacing existing work, not necessarily implementing it from scratch. Preserve unrelated working changes. Do not claim acceptance or a speedup before the comparative evidence and decision gate.

**Execution note**: The first timing capture stopped at preflight before its first sample because the cache parser mishandled typed CMake keys. The parser is fixed. The user's new implementation request authorizes the single bounded capture in T025 after its fixture and timing-boundary prerequisites pass; no automatic retry or campaign expansion is authorized.

**Format**: `[ID] [P?] [Story] Description`; `[P]` means safe to perform concurrently with other marked tasks in the same phase after their prerequisites, using different files.

## Phase 1: Setup

**Purpose**: Preserve the working state and establish reproducible comparison identities.

- [X] T001 Record the actual branch, HEAD, tracked/untracked changes and candidate source/diff identity in `specs/010-optimize-transform-setters/evidence/RESULTS.md`; verify that baseline `a2f587d1c1281277e1b5a1d33d6a47b62fdb5a0c` represents the pre-change state without discarding unrelated work.
- [X] T002 Prepare isolated baseline and candidate source inputs and matched Release build directories under `bin/` or `build/`, recording compiler, flags, hardware, worker limits and exact build commands in `specs/010-optimize-transform-setters/evidence/RESULTS.md`; do not put source copies or builds in evidence directories.

## Phase 2: Foundational comparison infrastructure

**Purpose**: Provide a single compact, comparable pilot for both stories. Both story phases depend on this setup.

- [X] T003 Create a minimal deterministic shared fixture in `benchmark/transform_setter_probe.cpp` and wire it through `CMakeLists.txt`; support small/large flat and deep/broad read-heavy hierarchies, ancestor edits, moving parent/child bodies, and repeated same-value setters with changed-value controls, recording counts, shape, ratios, seed, workers and timing boundaries.
- [X] T004 Implement bounded capture orchestration in `benchmark/capture_transform_setter.py` using the same fixture on baseline and candidate, three independent repetitions, five warmup and 30 measured frames for step cases, and bounded direct-operation loops; enforce a cumulative 600-second and 50 MiB output budget, stop on failure/budget, and retain only commands, identities, raw samples and compact summaries.
- [X] T005 Build identical uninstrumented Release baseline and candidate fixtures in the T002 build directories and verify matching workload identities and timing boundaries; record commands and verification in `specs/010-optimize-transform-setters/evidence/RESULTS.md` before capture.

**Checkpoint**: Comparable fixtures and bounded capture are ready; baseline reconstruction has not changed the working tree.

## Phase 3: User Story 1 — Read current positions efficiently (Priority: P1, MVP)

**Goal**: Make repeated position reads efficient while retaining immediate public positions, reparenting behavior, lifecycle safety and synchronized physics/rendering snapshots.

**Independent test**: Nested reads immediately reflect ancestor and local edits and reparent/detach, including parent/child bodies moving together; compare baseline and candidate read and ancestor-edit workloads independently.

### Tests for User Story 1

- [X] T006 [P] [US1] Extend `tests/transform_hierarchy_test.cpp` for repeated nested edits, immediate reads before frame synchronization, detach/reparent preserving descendant positions, changed ancestor after reparent and parent/child motion at one and multiple workers.
- [X] T007 [P] [US1] Extend targeted `tests/destruction_contract_test.cpp` and `tests/scene_io_contract_test.cpp` coverage for hierarchy lifetime and serialized current positions after unsynchronized edits; preserve validation constraints: "finite translations, acyclic hierarchy, existing runtime ownership and mutation rules."

### Implementation and assessment for User Story 1

- [X] T008 [US1] Review and, where needed, simplify or replace provisional immediate-position handling in `include/platformator/gameobject.h` and `src/gameobject.cpp`; establish ownership, invalidate/publish on all position and parenting paths, preserve immediate reads, and document O(depth) baseline reads versus candidate read/edit costs and per-object storage.
- [X] T009 [US1] Review worker join, parent-first batch publication and stable concurrent reads in `src/physicsmanager.cpp`, `src/gamemanager.cpp` and `src/gamemanager.h`; preserve independent worker writes, existing synchronized snapshots, lifetime safety and deterministic ordering without a new worker pool.
- [X] T010 [US1] Run the relevant hierarchy, lifetime, scene and worker tests from `specs/010-optimize-transform-setters/quickstart.md`; record results, sanitizer or worker limitations and any failures in `specs/010-optimize-transform-setters/evidence/RESULTS.md`.
- [X] T011 [US1] Define and verify the P02 pilot cases in `benchmark/transform_setter_probe.cpp`: read-heavy deep/broad and flat controls, frequent ancestor edits and parent/child movement, including small/large cases and one/multiple workers; validate expected positions separately from timing.

**Checkpoint**: US1 has independent correctness and workload coverage. Acceptance still requires the shared pilot and decision gate in Phase 5.

## Phase 4: User Story 2 — Avoid redundant updates (Priority: P2)

**Goal**: Skip unnecessary collider/body work for identical assignments while retaining validation, threshold wakes, explicit wake and body-type resets.

**Independent test**: Settled colliders and sleeping bodies receive unchanged and changed properties; assert geometry/participation work and wake/reset outcomes without US1-specific behavior.

### Tests for User Story 2

- [X] T012 [P] [US2] Extend `tests/physics_geometry_test.cpp` to cover unchanged and changed box/circle dimensions and offsets, geometry/inertia effects and invalid inputs; preserve constraints: "Dimensions are positive and offset is finite."
- [X] T013 [P] [US2] Extend `tests/physics_bookkeeping_test.cpp` for unchanged mass/material/gravity, kinematic velocity participation, threshold-based wake checks, explicit sleep/wake and repeated body-type reset/response behavior; preserve constraints: "finite vectors and scalars; positive dynamic mass, nonnegative other mass/friction/restitution and restitution no greater than one."

### Implementation and assessment for User Story 2

- [X] T014 [US2] Review and correct validation-first exact-equality guards in `src/collider.cpp`, `src/boxcollider.cpp` and `src/circlecollider.cpp`, avoiding unchanged geometry and inertia work while preserving changed-value effects.
- [X] T015 [US2] Review and correct no-op guards and wake/reset ordering in `src/rigidbody.cpp`, preserving threshold wake checks, force/torque semantics and intentional same-type initialization; avoid unnecessary kinematic participation updates.
- [X] T016 [US2] Run targeted geometry and physics bookkeeping/support checks in `tests/physics_geometry_test.cpp`, `tests/physics_bookkeeping_test.cpp` and `tests/physics_support_test.cpp`; record outcomes and failures in `specs/010-optimize-transform-setters/evidence/RESULTS.md`.
- [X] T017 [US2] Define and verify the P05 pilot cases in `benchmark/transform_setter_probe.cpp` for settled repeated shape, offset, body and motion assignments, with changed-value controls and separate work counters; validate outcomes without including instrumentation in timed runs.

**Checkpoint**: US2 has independent correctness and workload coverage. Acceptance still requires the shared pilot and decision gate in Phase 5.

## Phase 5: Polish and cross-cutting evidence

**Purpose**: Compare the complete provisional change against the verified baseline, make an evidence-based KISS decision and document limitations.

- [X] T018 Run the single bounded pilot with `benchmark/capture_transform_setter.py` for the verified baseline and final candidate, including direct read/edit/setter samples and complete-step/physics scopes with 1 and multiple workers; retain independent raw samples, commands and identities under `specs/010-optimize-transform-setters/evidence/` and never sum nested scopes.
- [X] T019 Collect separate bounded work, memory/allocation/growth and relevant latency observations for the same cases using `benchmark/transform_setter_probe.cpp`; record small/large, static/dynamic and selected churn/spatial coverage or explicit gaps in `specs/010-optimize-transform-setters/evidence/RESULTS.md`.
- [X] T020 Analyze gains, regressions, run variability, read/edit tradeoffs and worker effects in `specs/010-optimize-transform-setters/evidence/RESULTS.md`; investigate unexpected outcomes, record partial/budget failures and do not retry or expand automatically. Use `benchmark/capture_linear_render.py` defaults only if a rendering comparison is actually needed.
- [X] T021 Decide whether to retain, simplify, replace or revert the provisional implementation based on correctness, representative results and KISS, documenting alternatives and remaining risks in `specs/010-optimize-transform-setters/design.md`; if a known inefficient path must remain, add the constitution-required local `EFFICIENCY:` comment with cost, reason and concrete replacement work to the relevant `src/` file.
- [X] T022 Update caller-facing wake/reset compatibility and measured limitations in `specs/010-optimize-transform-setters/contracts/runtime.md` and `specs/010-optimize-transform-setters/quickstart.md`, including explicit `wakeUp()` guidance and no claims unsupported by the pilot.
- [X] T023 Run final relevant CTest, sanitizer and serial/multiworker checks in `bin/` per `specs/010-optimize-transform-setters/quickstart.md`; document skips, unavailable tooling and the pre-existing all-target render-culling index build issue in `specs/010-optimize-transform-setters/evidence/RESULTS.md`.
- [X] T024 Recheck `git diff --check`, benchmark output budget and artifact contents against `specs/010-optimize-transform-setters/plan.md`; retain no executable, build tree, source bundle, frame images or sprite identity traces, and report unmeasured dimensions and whether the constitution evidence gate is satisfied in `specs/010-optimize-transform-setters/evidence/RESULTS.md`.

## Dependencies & Execution Order

- **Setup → Foundation**: T001 → T002 → T003 → T004 → T005. Capture controls apply across the entire pilot, not independently per story.
- **Stories**: After T005, US1 and US2 are independently implementable. In US1, T006/T007 can run together, then T008 → T009 → T010 → T011. In US2, T012/T013 can run together, then T014 → T015 → T016 → T017. Do not concurrently edit the shared fixture in T011 and T017.
- **Cross-cutting**: T018 starts only after T010, T011, T016 and T017; T019 → T020 → T021 → T022 → T023 → T024. If T021 changes source or fixture, repeat only the affected correctness/measurement checks when warranted, remaining within the original budget; never automatically retry after a budget or capture failure.
- **Measurements**: T018 and T019 form one bounded pilot, not an exhaustive matrix. A failure or budget stop leaves the result partial and the evidence gate unsatisfied unless separately justified by actual completed work.

## Parallel Examples

- **US1**: T006 and T007 touch separate test files and can proceed together after T005.
- **US2**: T012 and T013 touch separate test files and can proceed together after T005.
- US1 and US2 code reviews can proceed independently after T005 when file ownership is coordinated; serialize shared fixture edits and the combined capture.

## Implementation Strategy

Start with US1 as the MVP after shared setup. Preserve the existing provisional work until correctness and the comparative pilot support a decision. Add US2 as a separately testable increment, then run the one compact combined pilot and complete the cross-cutting review. No numeric performance claim follows from complexity alone or from three exploratory repetitions.

## Phase 6: Convergence

- [X] T025 CRITICAL: Once a new bounded capture is explicitly requested after the prior preflight failure, collect and retain the matched baseline/candidate timing samples and separate work/resource evidence, analyze read/edit and worker tradeoffs, investigate regressions, and decide whether to retain or revise the provisional implementation in `specs/010-optimize-transform-setters/evidence/RESULTS.md` and `specs/010-optimize-transform-setters/design.md`; enforce the existing 600-second/50 MiB limits, stop on failure, and do not automatically retry per Constitution VI and T018–T021 (missing).
- [X] T026 CRITICAL: Correct the mislabeled physics timing boundary in `benchmark/transform_setter_probe.cpp`: the timer currently includes the `Runtime::simulateFrame()` call described as untimed; either measure the intended integration/movement scope alone with synchronization outside the clock or accurately relabel the actual full boundary, and verify that reported boundaries match execution before any capture per Constitution VI and T018 (contradicts).
- [X] T027 CRITICAL: Extend the bounded comparison in `benchmark/transform_setter_probe.cpp` and `benchmark/capture_transform_setter.py` to exercise unchanged and changed velocity/angular-velocity setters and representative complete steps containing repeated setter activity; validate identical workloads and outcomes for baseline/candidate while keeping instrumentation out of timing runs and respecting the existing capture budget per Constitution VI and T017–T019 (partial).

T025 depends on completing T026 and T027 first; a new capture also requires an explicit user request under the benchmark policy.

## Phase 7: Convergence

- [X] T028 CRITICAL: Correct the setter-step timing description in `specs/010-optimize-transform-setters/evidence/RESULTS.md` and `benchmark/transform_setter_probe.cpp`: `assignStepMotionProperties` runs before `Runtime::simulateFrame` within a host-call composite timer, not inside the simulation call. Provide or explicitly plan a bounded true complete-step workload that executes repeated setters during the runtime step, and distinguish it from the existing raw samples without rewriting historical evidence; preserve the capture policy and do not automatically repeat or expand a campaign per Constitution VI and T027 (partial).
- [X] T029 CRITICAL: Reassess the plain-step and physics-scope regressions using the actual boundaries and existing raw samples in `specs/010-optimize-transform-setters/evidence/RESULTS.md` and `specs/010-optimize-transform-setters/design.md`: `synchronizeTransforms` and its immediate-position publication run in the untimed `simulateFrame` following the measured `applyPhysics`/`applyMovement` scope, so they cannot directly explain time measured within that scope. Investigate feasible causes using existing evidence, correct the attribution and keep the decision provisional if unresolved; respect the existing benchmark budget and do not automatically retry or expand the capture per Constitution VI and T020–T021 (partial).
