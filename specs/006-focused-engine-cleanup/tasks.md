---
description: "Dependency-ordered implementation tasks for focused engine and example cleanup"
---

# Tasks: Focused Engine and Example Cleanup

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md), [data-model.md](data-model.md), and [contracts/](contracts/).

**Scope**: S01–S05, P01, P04, E01–E02, and only the two physics `membership` helpers from S06. P02/P03/P05/P06/P07, editor findings E03–E05 and other S06 refactors are excluded. All nonempty asset requests are required. Preserve existing working-tree edits, including `src/gamemanager.cpp`, and do not switch branches merely to match the feature directory.

**Format**: `- [ ] Tnnn [P?] [USn?] Action with exact paths`. `[P]` means independent files and no dependency on an unfinished task in the same batch. A task is complete when its stated work and evidence exist, not merely when a command was attempted. Distinguish skipped/unavailable checks from passes.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Record the actual input and recheck the audit before changing production code.

- [X] T001 Create `specs/006-focused-engine-cleanup/evidence/` and record branch, HEAD, staged/unstaged/nonignored untracked status, binary patches, CMake/compiler/dependency versions, hardware, allowed CPU affinity and worker settings in `specs/006-focused-engine-cleanup/evidence/provenance.md` following `specs/006-focused-engine-cleanup/quickstart.md`; explicitly preserve the existing `src/gamemanager.cpp` edits.
- [X] T002 [P] Recheck S01–S05, P01, P04, E01–E02 and both physics `membership` helpers against `src/`, `include/platformator/`, `examples/mario/`, `tests/` and `docs/CODE_CLEANUP_AUDIT.md`; record callers, public compatibility, candidate removals and serialized `cameraLead` uses in `specs/006-focused-engine-cleanup/evidence/inventory.md`.
- [X] T003 [P] Review `paper/paper.txt` §§3.1–3.3 with `src/grid.cpp`, `src/gridcell.cpp`, `src/aabb.cpp` and `src/segmentedintervallist.cpp`; document primary-axis checkpoints, endpoint and chunk repair, fast-motion cancellation, cell witnesses, worker completion and serial reduction in `specs/006-focused-engine-cleanup/evidence/physics-invariants.md`.

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish common fixtures and reproducible baseline evidence before production edits.

- [X] T004 Inspect `tests/asset_root_contract_test.cpp`, `tests/scene_io_contract_test.cpp`, `tests/physics_bookkeeping_test.cpp`, `tests/collider_pair_cleanup_test.cpp`, `tests/contact_events_test.cpp` and `tests/regression_tests.cpp`; record current coverage, needed baseline-compatible fixture additions and intentional differences in `specs/006-focused-engine-cleanup/evidence/coverage.md`.
- [X] T005 Create the smallest shared measurement fixtures needed for repeated retained asset requests, validated aliases, load/eviction churn and debug-object growth under `benchmark/` and register them in `CMakeLists.txt` only where necessary; provide separate correctness/work and uninstrumented timing modes, actual populations/operation counts, fixed inputs, and no production hooks solely for timing. If a fixture cannot be constructed, record the affected claim as unverified in `specs/006-focused-engine-cleanup/evidence/coverage.md`.
- [X] T006 Freeze the actual working-tree source, nonignored untracked inputs and common T005 fixtures before production edits using `specs/006-focused-engine-cleanup/quickstart.md`; capture content hashes, manifest and source/build fingerprints in `specs/006-focused-engine-cleanup/evidence/baseline-provenance.md`, retain the snapshot and logs, and do not attribute a dirty snapshot to clean HEAD.
- [X] T007 Build the frozen baseline with `cleanup-debug`, `production` and `cleanup-benchmark` presets and run applicable suites, public-header/package/consumer and Mario smoke checks from `specs/006-focused-engine-cleanup/quickstart.md`; record counts, commands, skips, failures, old optional-fallback behavior and fixture hashes in `specs/006-focused-engine-cleanup/evidence/baseline.md`.
- [X] T008 Run baseline correctness/work fixtures for repeated asset hits, debug growth, one/multiple-worker physics, multicell witness and endpoint/chunk controls; characterize source-visible filesystem calls, copies/moves, populations and limitations in `specs/006-focused-engine-cleanup/evidence/baseline-work.md` without presenting instrumented durations as performance results.

**Checkpoint**: Baseline identity, input, fixture equivalence and observable behavior are recorded. All stories can proceed subject to shared-file dependencies below.

## Phase 3: User Story 1 — Predictable, reusable asset loading (Priority: P1) 🎯 MVP

**Goal**: Load every nonempty requested asset or fail, and reuse previously validated retained resources without repeat filesystem resolution.

**Independent Test**: Exercise texture/audio/animation via runtime, component, scene and behavior/nested reference paths; verify missing/decode failures, exact retained hits after filesystem changes, multiple aliases, eviction and revalidation.

### Tests and characterization

- [X] T009 [US1] Extend `tests/asset_root_contract_test.cpp` to cover required missing/nonempty texture, audio and animation requests, original-path diagnostics, intentionally empty/no-asset values, invalid/traversal paths, first-time symlink escape and explicit loading of formerly fallback-named files; retain baseline characterizations separately and mark new behavior expectations as intentionally failing before implementation.
- [X] T010 [P] [US1] Extend `tests/scene_io_contract_test.cpp` and `tests/scene_script_roundtrip_test.cpp` only for uncovered required scene, behavior and nested animation-frame missing/unreadable/undecodable paths, original request context and failed-scene rollback; verify baseline failure behavior where already required and preserve scene ownership/reference tests.
- [X] T011 [US1] Add behavior-oriented retained-hit, valid distinct-alias, file deletion/replacement, symlink-change, final-reference eviction, all-alias invalidation, reload and runtime recreation checks in `tests/asset_root_contract_test.cpp` and `tests/runtime_recreation_test.cpp`; verify the first unseen alias still validates and assert that no request maps to freed storage. Use T005 instrumentation to check zero filesystem resolutions for an exact retained hit, outside timing.
- [X] T012 [US1] Run T009–T011 against the frozen baseline in `specs/006-focused-engine-cleanup/quickstart.md`, preserving baseline-compatible results and separately recording expected failures for the intentional required-loading/cache changes in `specs/006-focused-engine-cleanup/evidence/us1-characterization.md`.

### Implementation and validation

- [X] T013 [US1] Replace the optional/fallback policy in `src/pathmanager.h` and `src/pathmanager.cpp` with required resolution for nonempty requests: preserve syntax, configured-root containment and first-time symlink checks; report the original request on missing/unreadable failures; remove unused fallback selection/warning state only after searching callers; keep intentionally requested `missing.*` files ordinary assets.
- [X] T014 [US1] Update texture/audio/animation loading and error propagation in `src/gamemanager.h`, `src/gamemanager.cpp`, `src/scenebuilder.cpp`, `src/referenceresolver.cpp` and relevant nested serializers so all nonempty requests fail rather than return a fallback or silent decode failure; remove unreachable `required && resolved.usedFallback` checks (S02), preserve entry-point empty-path semantics and scene rollback, and update every affected call site.
- [X] T015 [US1] Implement per-type runtime-owned request shortcuts in `src/gamemanager.h` and `src/gamemanager.cpp` with these constraints from `specs/006-focused-engine-cleanup/data-model.md`: "Identity: exact nonempty original request string within an asset type and runtime"; "Creation: only after validation, resolution, successful decode/deserialization and association with a live canonical resource"; "every alias points to a currently live resource in the same kind/runtime". Keep first-time aliases validated and canonical resource deduplication intact; do not retain an owning reference merely for an alias.
- [X] T016 [US1] Complete shortcut eviction in `src/gamemanager.cpp` and the ownership paths in `src/texturewrapper.cpp`, `src/audiowrapper.cpp`, `src/animationclip.cpp` and `src/scenebuilder.cpp` as needed: "before associated resource is freed, remove every alias pointing to it"; "clear all aliases during cache teardown"; ensure failed loads and aborted scene preparation cannot leave stale mappings, including nested animation destruction and reentrant release. Record alias-storage and eviction complexity in `specs/006-focused-engine-cleanup/evidence/us1-design.md`.
- [X] T017 [US1] Rebuild and run T009–T011, relevant `tests/runtime_foundation_test.cpp`, `tests/scene_transition_contract_test.cpp` and `tests/scene_script_roundtrip_test.cpp` against the candidate; record actual outcomes, filesystem-call counts, first-load/hit behavior, ownership and baseline differences in `specs/006-focused-engine-cleanup/evidence/us1-validation.md`.
- [X] T018 [US1] Document required-only asset behavior, no-asset cases, cache-hit stability, explicit `missing.*` requests, changed failure diagnostics and consumer migration in `docs/CPP_API_MIGRATION.md` and relevant public asset-loading documentation in `include/platformator/runtime.h`.

**Checkpoint**: US1 provides required-only loading and safe retained-resource reuse with its independent contract checks.

## Phase 4: User Story 2 — Maintain focused engine logic (Priority: P1)

**Goal**: Simplify selected engine paths and merge only the two membership bookkeeping helpers without changing collision, lifecycle or worker behavior.

**Independent Test**: Exercise contact and body membership transitions, moved-index repair, overlap/witness invariants at one and multiple workers, collision correction and both animation getter names.

### Tests and characterization

- [X] T019 [P] [US2] Review `tests/physics_bookkeeping_test.cpp`, `tests/contact_events_test.cpp` and `tests/physics_support_test.cpp`; add only missing behavior-oriented body/pair membership cases for add, unchanged requests, middle/last removal, moved-index repair, contact/solver participation and retirement. Run unchanged logic on the baseline and record expected traces in `specs/006-focused-engine-cleanup/evidence/us2-membership-baseline.md`.
- [X] T020 [P] [US2] Review `tests/collider_pair_cleanup_test.cpp`, `tests/physics_geometry_test.cpp` and `tests/regression_tests.cpp`; add only uncovered baseline-passing controls for endpoint ties, long intervals, fast motion, split/merge, cell migration and first/final multicell witnesses; record coverage and one/multiple-worker expectations in `specs/006-focused-engine-cleanup/evidence/us2-overlap-baseline.md`.
- [X] T021 [P] [US2] Extend `tests/scene_script_roundtrip_test.cpp` and, if needed, `tests/consumer/` to verify both animation getter names agree for playing/stopped clips and serialization; identify any current public `Collision` constructor consumers in `specs/006-focused-engine-cleanup/evidence/us2-api-baseline.md` before the signature change.

### Implementation and validation

- [X] T022 [US2] Remove only unused axis storage and arguments from `src/segmentedintervallist.h`, `src/segmentedintervallist.cpp`, `src/aabb.h`, `src/aabb.cpp` and their callers such as `src/gridcell.cpp` (S01); preserve `isPrimary`, endpoint tie/order, checkpoint/fast-motion/split/merge repair and worker/witness boundaries.
- [X] T023 [P] [US2] Remove the unused penetration parameter from the public `Collision` constructor in `include/platformator/collision.h` and `src/collision.cpp`, update actual callers, and preserve separation-based penetration bias and normal/tangent solver paths (S03); record the old/new signatures and rebuild implications in `specs/006-focused-engine-cleanup/evidence/us2-api-baseline.md`.
- [X] T024 [P] [US2] Remove the unused free behavior lookup from `src/helpers.h` after confirming no live callers; retain `include/platformator/behaviorquery.h` and the public member query and its required includes (S05).
- [X] T025 [P] [US2] Make `getCurrentClip()` the preferred getter in `include/platformator/animator.h` and `src/animator.cpp`, and keep `getCurrentAnimationClip()` as a documented forwarding compatibility alias returning the same pointer (S04); preserve playback and `src/animatorserializer.cpp` behavior.
- [X] T026 [US2] Merge only the two existing `membership` helpers from `src/physicsbodyindexes.cpp` and `src/physicscontacts.cpp` into a small private typed primitive, for example `src/physicsmembership.h`; preserve "Desired present + absent -> append and record index", "Desired absent + present -> move last entry into removed position, repair moved index, pop, set removed index absent" and "Unchanged request -> no change". Keep list ownership, eligibility, current ordering and serial reconciliation boundaries; use existing mutable index access where valid and do not alter `GameManager::cancelScriptStart`.
- [X] T027 [US2] Rebuild and run T019–T021 and existing geometry/contact/support/transform and lifecycle tests against baseline and candidate with one and available multiple workers; compare contract traces and declared tolerances, and document checkpoint/witness/worker/retirement results plus any limitations in `specs/006-focused-engine-cleanup/evidence/us2-validation.md`.
- [X] T028 [US2] Update `docs/CPP_API_MIGRATION.md` after T018 and T023–T025 with collision signature changes, source/rebuild implications and the preferred animation getter/alias; verify public headers and `tests/consumer/` use the supported API.

**Checkpoint**: US2 preserves physical behavior and externally supported animation access while removing the requested duplication.

## Phase 5: User Story 3 — Keep debug rendering and Mario clear (Priority: P2)

**Goal**: Reduce debug-object copying and remove verified dead Mario state without changing visible output or scene compatibility.

**Independent Test**: Build debug-on/off and Mario, compare overlay output on the same scene when graphical access is available, and load/round-trip the existing Mario scene including `cameraLead`.

### Tests and characterization

- [X] T029 [P] [US3] Use T005's common debug-growth fixture to record baseline copies/moves, allocation behavior, populations and available overlay-capture method in `specs/006-focused-engine-cleanup/evidence/us3-debug-baseline.md`; mark graphical comparison unavailable if no renderer is accessible.
- [X] T030 [P] [US3] Inspect `examples/mario/level1.scene`, all Mario scene files and `tests/package_smoke.cmake`; establish baseline example build/smoke and `cameraLead` load/round-trip coverage, adding only missing behavior-oriented checks in `tests/scene_script_roundtrip_test.cpp` or the Mario consumer host, and record results in `specs/006-focused-engine-cleanup/evidence/us3-mario-baseline.md`.

### Implementation and validation

- [X] T031 [P] [US3] Remove the empty move-suppressing `DebugObject` destructor or otherwise enable safe implicit moves in `src/debugdraw.h` and `src/debugdraw.cpp` (P01); preserve member ownership, required copying, primitive output, draw order, debug controls and debug-off compilation.
- [X] T032 [P] [US3] Remove verified unused camera/enemy pointers and assignments in `examples/mario/mario_camera_rig.h`, `examples/mario/mario_camera_rig.cpp`, `examples/mario/mario_patrol_enemy.h` and `examples/mario/mario_patrol_enemy.cpp`, and unused constants in `examples/mario/mario_constants.h` (E01); retain the serialized `cameraLead` field and its current inert behavior.
- [X] T033 [P] [US3] Remove only the unimplemented unused `getPositionBeforePhysics` and `getVelocityBeforePhysics` declarations from `examples/mario/mario_player.h` after rechecking callers (E02).
- [X] T034 [US3] Rebuild debug-on/off and Mario with examples enabled, run T029–T030, package smoke and matching baseline/candidate overlay comparisons using `specs/006-focused-engine-cleanup/quickstart.md`; record debug copy/move counts, scene compatibility and visual availability in `specs/006-focused-engine-cleanup/evidence/us3-validation.md`.

**Checkpoint**: US3 is independently validated, including explicit visual verification status.

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T035 Run full applicable debug-on and production CTest suites, `platformator_public_headers`, package/relocated consumer, Mario and benchmark contract checks as described in `specs/006-focused-engine-cleanup/quickstart.md`; compare with T007 and record exact passes, skips and failures in `specs/006-focused-engine-cleanup/evidence/final-correctness.md`.
- [X] T036 Run applicable `cleanup-asan` ASan/UBSan including leak-enabled attempts and `cleanup-tsan` physics/lifecycle selections for baseline and candidate using `specs/006-focused-engine-cleanup/quickstart.md`; retain logs and diagnose baseline versus candidate issues in `specs/006-focused-engine-cleanup/evidence/sanitizers.md`; leak-disabled retries and unavailable hosts are not passes.
- [X] T037 Run separate baseline/candidate operation and allocation checks for exact retained hits, initial aliases, multiple-alias eviction/reload and debug-object growth using T005 fixtures; record call counts, copies/moves, allocations, alias retained memory and eviction cost in `specs/006-focused-engine-cleanup/evidence/work-memory.md`, excluding these instrumented runs from timing aggregates.
- [X] T038 Capture matched baseline/candidate Release timing for small/large retained repeated loads, first-load and churn controls and debug-on growth/rendering where available; use equivalent fixtures, at least five independent processes per cell, recorded affinity/warmup/inputs, interleaved schedule and retained raw results under `specs/006-focused-engine-cleanup/evidence/timings/`; record unavailable fixture or rendering groups as unverified.
- [X] T039 Run one/multiple-worker baseline/candidate engine invariant and Release timing controls using `benchmark/README.md`, `benchmark/sweep_scaling.py` and `specs/006-focused-engine-cleanup/contracts/validation.md`, including small/large, quiet/dynamic, sparse/dense and migration/churn cases; retain manifests, snapshots and raw outputs under `specs/006-focused-engine-cleanup/evidence/engine-benchmarks/` and compare complete-step and relevant nested scopes without summing them.
- [X] T040 Analyze T037–T039 for average/tail timing, run variation, scaling, allocations, retained memory and eviction cost; investigate regressions or unequal workloads and record bounded conclusions and unavailable claims in `specs/006-focused-engine-cleanup/evidence/performance-analysis.md` without inventing speedup thresholds.
- [X] T041 Review final code and documentation against `specs/006-focused-engine-cleanup/spec.md`, `specs/006-focused-engine-cleanup/contracts/` and `.specify/memory/constitution.md`; reconcile S01–S05, P01, P04, E01–E02 and narrow S06 dispositions, public migration, user changes, multicore/lifetime boundaries and actual checks in `specs/006-focused-engine-cleanup/validation.md`. Update `specs/006-focused-engine-cleanup/quickstart.md` only for commands actually used or documented justified differences.

## Dependencies & Execution Order

### Phase dependencies

- Setup T001–T003 precedes foundational work; T004–T005 precede frozen baseline T006, and T007–T008 establish characterization before production edits.
- US1, US2 and US3 can be independently delivered after the foundation. Shared files require coordination: T018 precedes T028 because both edit `docs/CPP_API_MIGRATION.md`; T021 and T030 must not edit `tests/scene_script_roundtrip_test.cpp` concurrently; T009 and T011 must be sequential because both edit `tests/asset_root_contract_test.cpp`.
- US1: T009–T011 and T012 precede T013–T016; T013 precedes T014, T014 precedes T015, T015 precedes T016, and T017 validates the integrated changes. T018 documents the actual implemented behavior.
- US2: T019–T021 precede T022–T026; T027 follows the implementation; T028 follows the integrated API changes and T018. T023 and T025 are parallel with T022/T024/T026 only when their target files remain distinct and no shared build/test mutation occurs.
- US3: T029–T030 precede T031–T033; T034 follows those changes. T029 relies on the common T005 fixture.
- T035–T041 follow all stories. T036 and T037 can be scheduled independently once builds and source are fixed, but sanitizer/work runs must finish before matched timing in T038–T039. T040 requires measurements; T041 integrates the evidence. Do not run timing concurrently with builds or other active captures.

### Parallel opportunities

- Setup: T002 and T003 inspect different areas and write separate records.
- US1: T010 can proceed independently of T009; T011 follows T009 due to its shared asset test file.
- US2: T019 and T020 use separate physics test hosts; T021 uses scene/consumer tests. After characterization, T023, T024 and T025 edit different production files; T022 and T026 may also be separate if their verified caller edits do not overlap.
- US3: T029 and T030 characterize separate fixtures; T031, T032 and T033 edit separate debug, Mario camera/enemy and Mario player files.
- Parallel labels indicate available work, not a requirement to use multiple agents. Preserve all dependent edits and evidence capture order.

### Requirement coverage

| Requirement | Tasks |
| --- | --- |
| FR-001–FR-005 / SC-001–SC-002 | T004–T018, T035–T038, T040–T041 |
| FR-006 / SC-003 | T003, T020, T022, T027, T035–T036, T039 |
| FR-007 | T013–T014 |
| FR-008–FR-010 | T021, T023–T025, T027–T028, T035 |
| FR-011 | T019, T026–T027, T035–T036 |
| FR-012 / SC-004 | T005, T008, T029, T031, T034, T037–T040 |
| FR-013 / SC-004 | T002, T030, T032–T034, T035 |
| FR-014–FR-015 / SC-005–SC-006 | T001–T008, T017–T018, T027–T028, T034–T041 |

## Implementation Strategy

1. Establish the dirty-input baseline and common probes (T001–T008).
2. Deliver US1 (T009–T018) as the MVP and independently verify required-only loading, reuse and eviction.
3. Deliver US2 and US3 independently, respecting shared-file dependencies and preserving public/scene compatibility.
4. Finish cross-cutting correctness, safety, measurement and evidence review. No speedup, graphical equivalence or sanitizer pass is claimed without corresponding evidence.

## Phase 7: Convergence

- [X] T042 [US2] CRITICAL — Make additions in `src/physicsmembership.h` exception-safe per FR-011 and Constitution VII (contradicts): do not publish an item's membership index until its vector insertion succeeds, and preserve existing no-op, removal, moved-index repair and serial update boundaries. Add a focused allocation-failure/retry regression in `tests/physics_bookkeeping_test.cpp` or an appropriate internal test host for both body and pair memberships, then rerun affected one/multiple-worker physics and lifetime checks.
- [X] T043 [US1] CRITICAL — Make resource and request-alias publication transactional in `src/gamemanager.h` and `src/gamemanager.cpp` per FR-004, FR-015 and Constitution VII (contradicts): if reverse-list allocation or forward-map insertion fails, leave no orphan reverse entry, stale alias or newly cached zero-reference resource from the failed request; preserve any previously existing resource and its aliases. Exercise controlled allocation failures and retries for texture, audio and animation, including scene preparation rollback and nested-resource cleanup, in appropriate `tests/asset_root_contract_test.cpp` and `tests/scene_io_contract_test.cpp` hosts; rerun affected lifetime and sanitizer checks and update `specs/006-focused-engine-cleanup/validation.md` with actual results and limitations.
- [X] T044 [US1] Bound or reclaim retained alias bookkeeping in `src/gamemanager.h` and `src/gamemanager.cpp` per the plan's live-alias-proportional storage decision and FR-004 (partial): current hash-table bucket capacity survives removal of the entries that caused its high-water mark. Demonstrate bounded post-eviction storage for both forward and reverse mappings, avoid an unconditional full rehash on every removal, and test many aliases followed by final release and reuse in `tests/asset_root_contract_test.cpp` or an internal host. Measure retained memory, allocation and representative eviction/churn costs against the existing baseline in `specs/006-focused-engine-cleanup/evidence/work-memory.md` and `specs/006-focused-engine-cleanup/evidence/performance-analysis.md`; rerun relevant correctness and timing checks after changes.
- [X] T045 [US1] Reconcile `docs/CPP_API_MIGRATION.md` per FR-005 and T018 (contradicts): its existing asset-reference and asset-root sections still say missing optional assets select packaged fallbacks, while the later section documents required-only loading. Update current guidance consistently for runtime, component and scene consumers, distinguish empty/unset fields from missing nonempty requests, and identify any historical fallback descriptions as historical without rewriting historical evidence.

## Phase 8: Convergence

- [X] T046 [US1] CRITICAL — Resolve the observed allocation-failure termination during animation JSON cleanup per FR-015, T043 and Constitution VII (contradicts): reproduce `PLATFORMATOR_TEST_ALL_ALLOCATIONS=1` from `tests/asset_runtime_exception_test.cpp` in an isolated subprocess with core dumps disabled, and trace the failure through `src/gamemanager.cpp`, `src/animationclipserializer.cpp` and the JSON destruction path in `vendored/json/json.hpp`. Implement an allocation-safe loading/cleanup approach or a justified compatible dependency-level remedy that preserves nested texture ownership, scene rollback, malformed-input diagnostics and safe handling of deeply nested input; do not silently restrict the test to selected allocation sizes. Add meaningful failure-and-retry coverage for animation and staged scene preparation, including nested texture release, rerun affected one/multiple-worker, lifetime and sanitizer checks, and update `specs/006-focused-engine-cleanup/validation.md` and `specs/006-focused-engine-cleanup/evidence/convergence/results.md` with actual outcomes and any remaining host limitations.
- [X] T047 [US1] Preserve the original request in filesystem-exception diagnostics per FR-001 and the invalid-path edge case (partial): in `src/pathmanager.cpp`, handle failures from first-time canonicalization and file-status operations so an error retains the exact supplied path spelling as well as useful underlying context, without weakening traversal/symlink containment checks or changing cached-hit behavior. Extend `tests/asset_root_contract_test.cpp` with a deterministic filesystem-error case using an original spelling that differs from its normalized path, exercise texture/audio/animation and scene/reference propagation as applicable, and rerun affected asset and scene tests.

## Phase 9: Convergence

- [X] T048 [US1] CRITICAL — Make `AssetReference<T>` ownership changes exception-safe per FR-004, FR-015, T043, T046 and Constitution VII (contradicts): in `include/platformator/assetreference.h`, ensure `set`, construction, copy assignment and resolution cannot publish an unretained resource or lose ownership when copying the asset path throws. Preserve existing owners and aliases, clean up newly loaded zero-reference resources if adoption fails, and preserve empty/unset semantics and runtime lifetime checks. Add controlled allocation-failure and retry tests using long canonical paths for texture, audio and animation references, including an already retained resource and scene/reference resolution, and verify final release and cache eviction under ASan/UBSan; record actual results and limitations.
