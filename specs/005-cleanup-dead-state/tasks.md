---

description: "Dependency-ordered implementation tasks for the dead state and internal code cleanup"
---

# Tasks: Clean Up Dead State and Internal Code

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/) and [quickstart.md](quickstart.md).

**Scope**: Audit findings C01–C14. Findings S01–S06, P01–P07 and E01–E05 are outside scope unless strictly necessary to remove a selected C finding. Recheck current usage and record evidence before each removal. Preserve uncommitted user work.

**Tests**: The specification requests behavior preservation, worker comparisons, independent consumers and performance evidence. Characterization tests added here should pass on the baseline: this is a behavior-preserving cleanup, so a failing test is a finding to investigate, not a required red step. Tests that only inspect removed internal state are not useful; migrate C03's field-specific assertions without weakening normal/tangent warm-start coverage.

**Format**: `- [ ] Tnnn [P?] [USn?] Action with exact paths`. `[P]` marks work with distinct files and no dependency on another unfinished task in the same parallel batch. Setup, foundation and polish tasks have no story labels. The repository's constitution and [validation contract](contracts/validation-evidence.md) remain binding.

## Phase 1: Setup

**Purpose**: Establish the execution environment and the source of truth for evidence before changing production code.

- [X] T001 Create `specs/005-cleanup-dead-state/evidence/` and record actual Git branch, HEAD, tracked/staged/untracked status and patches, CMake/compiler/dependency versions, hardware, available CPUs and allowed affinity in `specs/005-cleanup-dead-state/evidence/provenance.md` using `specs/005-cleanup-dead-state/quickstart.md`.
- [X] T002 Review `paper/paper.txt` §§3.1–3.3 together with `src/grid.cpp`, `src/aabb.cpp` and `src/segmentedintervallist.cpp`; record witness deduplication, endpoint/checkpoint/split/merge, event-delta and worker/reclamation boundaries in `specs/005-cleanup-dead-state/evidence/broad-phase-invariants.md` without changing algorithms.
- [X] T003 Re-audit C01–C14 against production, tests, examples, benchmark code and installed headers in `docs/CODE_CLEANUP_AUDIT.md`; record declarations, callers, evidence-backed proposed removal or required retention, and public compatibility in `specs/005-cleanup-dead-state/evidence/dispositions.md`. Flag unexpected live consumers before edits.

## Phase 2: Foundational — common characterization and baseline

**Purpose**: Establish identical behavior fixtures and a frozen baseline for all three stories. These tasks block production edits. Tasks T004–T007 may run concurrently after T003 because they touch distinct files; integrate them before freezing.

- [X] T004 [P] Review `tests/collider_pair_cleanup_test.cpp`, `tests/physics_bookkeeping_test.cpp` and `tests/contact_events_test.cpp`; if the documented gap remains, extend `tests/collider_pair_cleanup_test.cpp` with a behavior-oriented one/multiple-worker multi-cell witness survival, final-witness removal and re-entry check that passes on the baseline and does not inspect removed slots.
- [X] T005 [P] Review coverage in `tests/runtime_foundation_test.cpp`, `tests/transform_hierarchy_test.cpp`, `tests/runtime_step_test.cpp` and `tests/regression_tests.cpp`; add only missing baseline-passing lifecycle/audio ownership characterization to `tests/runtime_foundation_test.cpp`, including fire-and-forget surviving component removal where not already covered.
- [X] T006 [P] Review existing asset containment, fallback and diagnostics coverage in `tests/asset_root_contract_test.cpp`; add only missing baseline-passing checks for preservation of the original request in required-error/fallback diagnostics, keeping supported symlink/root behavior.
- [X] T007 [P] Inspect `benchmark/sweep_scaling.py`, `src/benchmark_main.cpp` and `benchmark/runtime_cleanup_probe.cpp` for a common workload that proves actual first-witness pair creation and final-witness removal. If existing scenarios do not demonstrate these counts, create the smallest common pair-churn fixture in `benchmark/dead_state_pair_churn_probe.cpp` and register it in `CMakeLists.txt`; validate expected counts and snapshots outside timing. If no valid fixture can be built, record C01 timing as unverified in `specs/005-cleanup-dead-state/evidence/baseline.md` rather than treating object churn as pair churn.
- [X] T008 Freeze a separate baseline source tree after T004–T007 and before production edits, including tracked changes and nonignored untracked common fixtures; record hashes and snapshot provenance in `specs/005-cleanup-dead-state/evidence/baseline.md` following `specs/005-cleanup-dead-state/quickstart.md`. Never attribute a metadata-free snapshot to a clean commit without evidence.
- [X] T009 Build and run the baseline debug-tools-on full applicable CTest suite and debug-tools-off production/consumer checks from the frozen tree; record commands, test counts, skips and failures in `specs/005-cleanup-dead-state/evidence/baseline.md` using `specs/005-cleanup-dead-state/quickstart.md`.
- [X] T010 Configure and build the frozen tree’s `cleanup-benchmark` runner, then establish baseline one/multiple-worker correctness traces and invariant snapshots for the workload matrix in `specs/005-cleanup-dead-state/contracts/validation-evidence.md`; record actual pair/contact populations, fixture hashes, worker counts and any missing coverage in `specs/005-cleanup-dead-state/evidence/baseline-invariants.md` before production edits.

**Checkpoint**: The inventory, shared characterization, immutable baseline and baseline correctness outcomes are available. Retain baseline binaries and fixtures until final comparison. Timing runs are performed after all builds and correctness checks so baseline and candidate use the same final capture schedule.

## Phase 3: User Story 1 — Remove obsolete collision bookkeeping (Priority: P1) 🎯 MVP

**Goal**: Remove C01/C03/C09–C12 when confirmed unused while preserving collision, support, solver, worker and lifetime behavior.

**Independent test**: Compare common baseline/candidate traces for multicell creation, intermediate and final witness loss, re-entry, dynamic one-/two-contact response and support under one and multiple workers. Run the relevant physics/geometry/contact suites; preserve feature remapping and warm starting.

### Tests and characterization for User Story 1

- [X] T011 [US1] Run the baseline fixtures from `tests/collider_pair_cleanup_test.cpp`, `tests/physics_bookkeeping_test.cpp`, `tests/physics_support_test.cpp`, `tests/physics_geometry_test.cpp`, `tests/contact_events_test.cpp` and `tests/regression_tests.cpp`; record baseline event ordering, physical tolerances and results in `specs/005-cleanup-dead-state/evidence/us1-baseline.md` before the US1 removals.

### Implementation for User Story 1

- [X] T012 [P] [US1] Remove C01's `pairSlot`, `pairSlots`, `freePairSlots` and their insertion/recycling writes from `src/colliderpair.h`, `src/grid.h` and `src/grid.cpp`; retain `candidateCollisions`, canonical identity, net witnesses, adjacency/queue/contact/solver indexes and pair-local worker results.
- [X] T013 [P] [US1] Remove C03's `accumulatedNormalImpulseBias` initialization and copy from `include/platformator/collisiontypes.h` and `src/collision.cpp`, and remove only field-specific setup/assertions in `tests/regression_tests.cpp`; preserve feature remapping, normal/tangent impulses, `bias`, scalar/block solver paths and other tests.
- [X] T014 [P] [US1] Remove C12's unused `ColliderBinding::bind`, projection-axis `bind` and proxy `setBoundingProjection` chain from `src/aabb.h`, `include/platformator/collider.h`, `src/boundingradiusprojectionaxis.cpp` and `src/boundingradiousprojectionproxy.cpp`; preserve binding construction, projection cache, chunk reassignment and repair.
- [X] T015 [US1] After T013, remove only C09's four unused support getter/setter declarations and definitions from `include/platformator/collision.h` and `src/collision.cpp`; preserve support flags, `updateSupportState`, contribution credits and wake/sleep behavior.
- [X] T016 [US1] After T012, remove only C10's `Grid::finishColliderSync` declaration and definition from `src/grid.h` and `src/grid.cpp`; preserve the live `queuePairsForCollider` caller and existing flush/reduction order.
- [X] T017 [US1] After T012, remove C11's unused `ColliderPair::setObjectA/B` and `increment/decrementWitnessCount` declarations/definitions from `src/colliderpair.h` and `src/colliderpair.cpp`; preserve constructor endpoint ordering, version caches and `addToWitnessCount` net transitions.
- [X] T018 [US1] Record the actual C01/C03/C09–C12 dispositions and C03/C12 public removals in `specs/005-cleanup-dead-state/evidence/us1-dispositions.md`; review `src/grid.cpp` and `src/physicsmanager.cpp` against `specs/005-cleanup-dead-state/contracts/runtime-physics.md`, especially worker completion, borrowed-pair lifetimes and serial reduction.
- [X] T019 [US1] Rebuild the candidate and run the same US1 fixtures and tests as T011 from `tests/`, comparing controlled baseline/candidate and single/multiworker event traces and physical snapshots while honoring the existing unconstrained-solver-order waiver; record results and any limitations in `specs/005-cleanup-dead-state/evidence/us1-validation.md`.

**Checkpoint**: US1 can be reviewed and behavior-tested independently. Release timing and full feature acceptance remain in Phase 6.

## Phase 4: User Story 2 — Simplify object and component lifecycle state (Priority: P1)

**Goal**: Remove C02/C04–C08 while preserving hierarchy, clock, scene, lifecycle and audio behavior.

**Independent test**: Create/load/reparent/move/destroy hierarchies, replace and retire components, exercise the fixed-step cap, and verify component and fire-and-forget Audio ownership with the baseline fixtures.

### Tests and characterization for User Story 2

- [X] T020 [US2] Run the baseline characterization in `tests/transform_hierarchy_test.cpp`, `tests/runtime_step_test.cpp`, `tests/runtime_foundation_test.cpp`, `tests/callback_mutation_test.cpp`, `tests/destruction_contract_test.cpp` and `tests/scene_transition_stress_test.cpp`; record lifecycle/audio traces and baseline results in `specs/005-cleanup-dead-state/evidence/us2-baseline.md`.

### Implementation for User Story 2

- [X] T021 [P] [US2] Remove C04's unused `Audio::gameManagerIndex` and public accessors from `include/platformator/audio.h` and `src/audio.cpp`; preserve autoplay, deactivation/stop, wrapper references, track destruction and the independent transient-audio collection in `src/sdlwindow.cpp`.
- [X] T022 [P] [US2] Remove C02's `hierarchyDepth` member/assignments and `rebuildHierarchyOrder` depth argument from `include/platformator/gameobject.h`, `src/gamemanager.h` and `src/gamemanager.cpp`; retain depth-first ordering, `hierarchyOrder`, transform queue/stamps and immediate position semantics.
- [X] T023 [US2] After T022, remove C05's unobserved `droppedSimulationSteps` manager accumulator/update from `src/gamemanager.h` and `src/gamemanager.cpp`; preserve `src/simulationclock.h` drop accounting, cap and `tests/runtime_step_test.cpp` expectations.
- [X] T024 [US2] After T022, remove C06's unused parameterized private GameObject constructor from `include/platformator/gameobject.h` and `src/gameobject.cpp`; retain default construction and setter-based scene/object creation.
- [X] T025 [US2] After T023 and T024, remove C07's unused `GameManager::addGameObject` wrapper and C08's unreachable `removeComponentsFromGameManager` / `notifyComponentRemoved` chain from `src/gamemanager.h`, `src/gamemanager.cpp`, `include/platformator/gameobject.h` and `src/gameobject.cpp`; preserve live registration, scene publication, retirement and deferred reconciliation paths.
- [X] T026 [US2] Record actual C02/C04–C08 dispositions and the C02 GameObject binary-layout impact plus C04 source/layout changes in `specs/005-cleanup-dead-state/evidence/us2-dispositions.md`; document why remaining clock, lifecycle and audio state is still needed.
- [X] T027 [US2] Rebuild the candidate and run the same US2 characterization as T020 plus relevant scene/audio round trips from `tests/scene_script_roundtrip_test.cpp`; compare against the frozen baseline and record results in `specs/005-cleanup-dead-state/evidence/us2-validation.md`.

**Checkpoint**: US2 can be reviewed and behavior-tested independently of physics cleanup. External consumers must rebuild after C02/C04 layout changes; the combined migration guide is integrated in US3.

## Phase 5: User Story 3 — Remove unused support paths and document compatibility (Priority: P2)

**Goal**: Remove C13/C14, preserve asset/debug behavior, and provide migration guidance for all selected public/layout changes.

**Independent test**: Compare valid/invalid/missing/fallback asset paths and diagnostics, validate debug-on/off builds and available rendered overlays, and rebuild independent consumers against the retained public interfaces.

### Tests and characterization for User Story 3

- [X] T028 [US3] Run the baseline asset and serialization fixtures in `tests/asset_root_contract_test.cpp`, `tests/scene_script_roundtrip_test.cpp` and `tests/regression_tests.cpp`; inspect active buffered debug paths in `src/debugdraw.cpp` and record the available baseline graphical/capture method or its absence in `specs/005-cleanup-dead-state/evidence/us3-baseline.md`.

### Implementation for User Story 3

- [X] T029 [P] [US3] Remove C13's unused `ResolvedAssetPath::requestedPath` member and aggregate initializers from `src/pathmanager.h` and `src/pathmanager.cpp`; keep raw request text for validation/errors/fallback warning keys and preserve canonical/absolute paths and fallback status.
- [X] T030 [P] [US3] Remove C14's uncalled direct `drawBoxCollider`, `drawCircleCollider`, `drawContactPoint`, `drawNormal`, `drawCircle` and `drawCross` declarations/definitions from `src/debugdraw.h` and `src/debugdraw.cpp`; preserve buffered `add*DebugObject` rendering, settings and toggles for debug-on/off builds.
- [X] T031 [US3] After T029–T030, rerun the common asset/scene tests and compare debug-on/off builds and available graphical/capture results with T028; record output and any visual verification limitation in `specs/005-cleanup-dead-state/evidence/us3-validation.md`.
- [X] T032 [US3] After T018 and T026 for the full feature, update `docs/CPP_API_MIGRATION.md` with exact actual C02/C03/C04/C12 declaration/layout changes, consumer actions and rebuild requirement; preserve serialized formats and do not promise ABI compatibility or a replacement rebinding API. For a US3-only increment, document only changes already landed and complete this task after the other story dispositions exist.
- [X] T033 [US3] After T012, if C01 is removed, annotate the present-tense pair-slot consumer claim in `specs/003-simplify-physics-bookkeeping/data-model.md` as superseded by C01 and append a dated supersession note to `docs/CODE_CLEANUP_AUDIT.md` while retaining its baseline findings; if C01 is retained with evidence, document that disposition instead of claiming removal. Preserve historical `specs/003-simplify-physics-bookkeeping/evidence/` and benchmark captures as snapshots.
- [X] T034 [US3] Build the individual public-header target defined in `cmake/PlatformatorPublicHeaders.cmake` and run the retained-interface consumer through `tests/package_smoke.cmake` and `tests/consumer/`; check the source and relocated installed package plus Mario consumer and record actual outcomes in `specs/005-cleanup-dead-state/evidence/us3-consumer.md`.
- [X] T035 [US3] Record final C13/C14 dispositions and compare all public/layout impacts with `specs/005-cleanup-dead-state/contracts/public-compatibility.md` in `specs/005-cleanup-dead-state/evidence/us3-dispositions.md`.

**Checkpoint**: Asset/debug work can be tested separately after the foundation. Completion of this story's combined migration requirement depends on the actual US1 and US2 dispositions.

## Phase 6: Polish and Cross-Cutting Validation

**Purpose**: Validate the integrated feature, safety, scaling and evidence; preserve limitations honestly. These tasks depend on all story implementation and are sequential where they share builds, evidence or resource-intensive execution.

- [X] T036 Run the complete debug-tools-on CTest suite, debug-tools-off production build/tests, public-header checks, package/consumer smoke and existing benchmark tooling using `specs/005-cleanup-dead-state/quickstart.md`; compare against T009 and record failures/skips separately in `specs/005-cleanup-dead-state/evidence/final-correctness.md`.
- [X] T037 Run `tests/runtime_work_test.cpp` in the `cleanup-work` build with one and available multiple workers, including quiet and fixed-peak controls; record affected-work/allocation results and compare with equivalent baseline in `specs/005-cleanup-dead-state/evidence/work-memory.md` without using probe timings as performance data.
- [X] T038 Run the `cleanup-asan` ASan+UBSan and `cleanup-tsan` configurations for applicable physics and lifecycle tests per `specs/005-cleanup-dead-state/quickstart.md`; record enabled attempts, baseline comparisons, full diagnostics, skips and host/dependency limitations in `specs/005-cleanup-dead-state/evidence/sanitizers.md` without treating unavailable runs as passes.
- [X] T039 Compare supported debug overlays and controls using the same scene, settings and reproducible state for baseline/candidate as described in `specs/005-cleanup-dead-state/quickstart.md`; retain captures/method or explicitly record unavailable visual verification in `specs/005-cleanup-dead-state/evidence/debug-overlays.md`.
- [X] T040 Measure affected structure layouts, retained pair-table capacity/high-water population, allocations and, where available, per-child peak RSS for baseline and candidate using each build's own flags; save methods, raw measurements and limitations in `specs/005-cleanup-dead-state/evidence/work-memory.md`, keeping measurement probes out of Release timing.
- [X] T041 Configure/build the `cleanup-benchmark` runner in both frozen baseline and candidate trees, then run the complete baseline/candidate one/multiple-worker invariant matrix from `specs/005-cleanup-dead-state/contracts/validation-evidence.md`, including actual pair creation/final-removal proof and multicell cases; retain manifests, snapshots, raw output and baseline comparisons under `specs/005-cleanup-dead-state/evidence/invariants/`, and mark missing groups unverified.
- [X] T042 After all builds, correctness and safety runs finish, capture the matched Release timing matrix specified in `specs/005-cleanup-dead-state/contracts/validation-evidence.md` with at least five independent processes per group, recorded affinity, seed, warmup/steps, counters, schedule and raw schema-3 samples under `specs/005-cleanup-dead-state/evidence/timings/`; keep validation/work/sanitizer runs out of timing aggregates.
- [X] T043 Compare baseline/candidate complete-step and relevant nested-scope average/p95, run variation, worker scaling and memory/allocation measurements from `specs/005-cleanup-dead-state/evidence/`; investigate regressions and missing pair-churn evidence, and write bounded conclusions and unverified claims in `specs/005-cleanup-dead-state/evidence/performance-analysis.md` without inventing a speedup threshold.
- [X] T044 Reconcile all C01–C14 removals/retentions, source/ABI migration notes, applicable tests, historical documentation boundaries and actual limitations in `specs/005-cleanup-dead-state/evidence/dispositions.md` and `specs/005-cleanup-dead-state/validation.md`; confirm `specs/005-cleanup-dead-state/quickstart.md` describes commands actually used or records justified differences.
- [X] T045 Review the integrated diff in `src/`, `include/platformator/`, `tests/` and `docs/` against `specs/005-cleanup-dead-state/spec.md` and the constitution; ensure active state/parallel boundaries remain, unrelated audit items are excluded, performance claims link to evidence, and all reported completion gates are honestly satisfied in `specs/005-cleanup-dead-state/validation.md`.

## Dependencies & Execution Order

### Phase and story dependencies

```text
Setup T001–T003
       ↓
Common characterization T004–T007 → freeze T008 → baseline T009–T010
       ├── US1 T011–T019: collision (MVP)
       ├── US2 T020–T027: lifecycle/audio
       └── US3 T028–T031, T034–T035: assets/debug/consumer
                           ↑
       US1 T012 → US3 T033: pair-slot documentation
       US1 T018 + US2 T026 → US3 T032: combined migration guide
       ↓ all story work and common compatibility integrated
Phase 6 T036–T045: full correctness → work/sanitizers/visual/memory → invariants → timing → final review
```

- All stories depend on the completed common baseline. Their production-code slices use separate paths and can be developed independently after the foundation. Individual checkpoints validate that slice against the same baseline.
- US3's **combined** migration guide requires the actual removals/dispositions from US1 and US2. US3 asset/debug work can start independently; T032 cannot represent the whole feature until those dispositions exist.
- T013 and T015 both touch `src/collision.cpp`; T012 precedes T016/T017 because they share Grid/ColliderPair files; T022 precedes T023/T024/T025 because they share GameObject/manager files. Integrate shared evidence and migration documents serially.
- If re-audit proves a finding is required by a current supported consumer or invariant, retain it, document the evidence, and adjust that finding's expected removal; do not force a behavioral regression to mark a task complete.
- Final timing waits for completed correctness/build/sanitizer work; baseline source, fixture and build remain available throughout. Report unavailable visual, sanitizer, memory or pair-churn measurements as unverified rather than blocking unrelated implementation or claiming a pass.

### Parallel opportunities

- **Foundation**: T004, T005, T006 and T007 may proceed in parallel after T003; they write separate test/benchmark paths. T008 waits for all four.
- **US1**: T012, T013 and T014 can proceed in parallel after T011; their production/test files do not overlap. T015 follows T013; T016 and T017 follow T012 and are serialized where shared files or review order require it.
- **US2**: T021 and T022 can proceed in parallel after T020; Audio files are separate from GameObject/manager files. T023–T025 follow the stated shared-file dependencies.
- **US3**: T029 and T030 can proceed in parallel after T028; PathManager and DebugDraw use different files. T033 waits for T012; T032 waits for US1/US2 disposition records.
- Cross-story code slices may be worked concurrently only with coordinated isolated changes and the frozen common baseline; benchmark captures and shared documentation integration should not run concurrently with builds or conflicting edits.

## Implementation Strategy

1. Complete setup and the common evidence/characterization foundation; keep the baseline immutable.
2. Deliver US1 first as the MVP and validate collision/solver behavior independently before moving on.
3. Deliver US2 and the US3 asset/debug slice independently, then integrate migration guidance after actual C02/C03/C04/C12 dispositions are known.
4. Run integrated correctness, work, safety, public-consumer and available graphical checks; then capture and analyze the complete matched performance matrix.
5. Finish with a complete fourteen-item disposition and a report that distinguishes verified behavior, measured performance, unavailable checks and compatibility breaks.
