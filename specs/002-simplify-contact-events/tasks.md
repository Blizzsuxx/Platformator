# Tasks: Simplify Contact Events

**Input**: Design artifacts in `specs/002-simplify-contact-events/`: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contact contract](contracts/contact-events.md), [benchmark contract](contracts/benchmark-schema.md), and [quickstart.md](quickstart.md).

**Prerequisites**: Read these artifacts and `.specify/memory/constitution.md` before implementation. Feature selection is `specs/002-simplify-contact-events`, independent of the existing Git branch. Implementation and scoped validation are complete; see [validation.md](validation.md) for results and unresolved desktop sanitizer findings.

**Tests**: Included because the specification explicitly requires acceptance scenarios and measurable correctness/affected-work evidence. Add changed-contract tests before implementation and record their relevant failures; retain passing geometry/startup/lifetime guardrails before mechanical refactors. A compile failure caused solely by a removed callback is not sufficient evidence of correct transition semantics.

**Organization**: Setup → shared foundation → US1 (P1) → US2 (P1) → US3 (P2) → cross-cutting acceptance. The stories have explicit integration dependencies because they edit the same runtime files; each has a separate observable acceptance checkpoint.

## Format: `[ID] [P?] [Story] Description`

- Every task has a completion box, sequential ID, and concrete repository-relative file paths.
- `[P]` denotes a task that can run alongside its listed peer after the stated prerequisite. It does not override phase gates, file ownership, or resource isolation for measurements.
- `[US1]`, `[US2]`, and `[US3]` refer to the three stories in `spec.md`; setup, foundation and polish tasks have no story label.
- Evidence paths below record outputs produced during implementation. Large raw captures may live in a fresh directory outside Git with a durable path, checksums and provenance recorded in feature evidence.

## Phase 1: Setup (Shared Infrastructure and Baseline)

**Purpose**: Establish an equivalent measurable workload before changing runtime behavior. No event, geometry, startup, solver or body implementation edits may precede T006.

- [X] T001 Record source revision, dirty patch, relevant callers and baseline build/worker/CPU configuration in `specs/002-simplify-contact-events/evidence/baseline-provenance.md`; recheck the current paths in `src/physicsmanager.cpp`, `src/physicscontacts.cpp`, `src/physicsgeometry.cpp`, `src/physicsbodyindexes.cpp`, `src/colliderpair.cpp`, `src/gamemanager.cpp` and `src/benchmark_main.cpp`, including the all-contact invariant consumer; verify preset availability in `CMakePresets.json` and preserve existing user changes.
- [X] T002 [P] After T001, add the common `persistent_contacts` fixture and runner options `--contact-pair-count` and `--scripted-endpoints 0|1|2` in `src/benchmark_main.cpp`; create disjoint stationary confirmed static/trigger pairs with no-op behaviors overriding only Enter/Exit, validate requested contact/recipient populations, and record resolved parameters without changing existing runtime behavior, scope boundaries or schema 2.
- [X] T003 [P] After T001, support `persistent_contacts`, sweep `--counts` and `--scripted-endpoints 0|1|2` in `benchmark/sweep_scaling.py`, mapping counts to `--contact-pair-count` and preserving existing scenario flags, independent-process repetitions, output safeguards and schema-2 readers; use the exact runner interface specified by T002 and `plan.md`.
- [X] T004 After T002 and T003, add fixture/argument/provenance coverage in `benchmark/tests/test_sweep_scaling.py` and verify actual pair counts through `src/benchmark_main.cpp` invariant captures; ensure zero/one/two scripted endpoints and invalid values behave as specified, and save the common fixture patch and source fingerprint in `specs/002-simplify-contact-events/evidence/baseline-provenance.md` for reuse unchanged by the candidate.
- [X] T005 Build and validate the unmodified runtime plus common fixture using `CMakePresets.json` cleanup-debug and cleanup-benchmark configurations and `benchmark/sweep_scaling.py --validate-invariants`; capture baseline test results, one/multiworker contact/support/body snapshots and numerical tolerances in `specs/002-simplify-contact-events/evidence/baseline-correctness.md`, resolving observed correctness failures before using any timings.
- [X] T006 Capture the pre-change Release timing matrix from `specs/002-simplify-contact-events/quickstart.md` using `benchmark/sweep_scaling.py` with its schema-2 runner/readers: persistent pairs 10/100/1000 with zero/one/two scripted endpoints plus all six existing workload families and their small/large/churn sizes, one and appropriate multiple workers, matching seed/dt/affinity, and at least five independent repetitions; retain raw samples, manifests, logs, baseline allocation/retained-storage observations, fixture patch and capture checksums in `specs/002-simplify-contact-events/evidence/baseline-results.md` without overwriting historical results.

**Checkpoint**: A validated, reproducible baseline exists before any runtime edit. Record actual environment limitations rather than silently substituting unrelated historical measurements.

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Supply internal inspection and real affected-work measurement shared by all stories, without creating a new public event API.

- [X] T007 Create the reusable internal fixture in `tests/contact_events_test.cpp` and register `platformator_contact_events_test` as INTERNAL with physics/determinism labels in `CMakeLists.txt`; use existing RuntimeAccess/PhysicsManager inspection at valid phase boundaries to capture generated records separately from delivered callbacks, support one/multiworker runs and zero/one/two recipients, and preserve the endpoint constraint "Canonical A/B handle order; allocations remain valid through pending notification delivery."
- [X] T008 Add test-only counters for actual pending-pair transition comparisons and appended event records in `tests/runtime_work_probe.h`, wiring the real operations through `src/workprobe.h` and `src/physicsmanager.cpp`; route existing event appends through the manager enqueue operation as needed without changing baseline semantics yet, retain legacy Stay instrumentation until US2, preserve `resultTestedGeometry` as "Existing test-work result, reduced on the main thread.", and compile all probes out of performance builds.

**Checkpoint**: Baseline evidence is protected, internal event inspection works without recipient scripts, and probes measure executed operations rather than infer work from delivered callbacks. T007 and T008 are sequential because they establish shared test/runtime interfaces.

## Phase 3: User Story 1 — Produce Events from Contact Transitions (Priority: P1) — MVP

**Goal**: Confirmed-contact beginnings/endings produce exactly one Enter/Exit per pair independently of scripts, including explicit pair removal, with deterministic and safe delivery.

**Independent Test**: Observe generated Enter/Exit records and recipient callbacks for separate→touching→separated→re-contact sequences with zero/one/two scripted endpoints. Verify false-positive candidates, multiple cell witnesses, physical removals and one/multiworker transition ordering. This checkpoint validates the Enter/Exit subsequence; complete Stay removal belongs to US2.

### Tests for User Story 1

- [X] T009 [P] [US1] Add generation-versus-delivery assertions in `tests/contact_events_test.cpp` for zero/one/two eligible recipients, entry, geometric separation, re-contact and independently gated endpoints; assert one pair record versus per-recipient/per-behavior callbacks and record the no-script Enter failure before implementing transitions.
- [X] T010 [P] [US1] Extend `tests/collider_pair_cleanup_test.cpp` with last-witness removal, partial multi-cell witness loss, duplicate synchronization, filter exclusion/re-enable, deactivation and one/both endpoint retirement; prove one ending per confirmed contact and no Exit for an unconfirmed candidate, preserving "losing one of several witnesses cannot erase the pair."
- [X] T011 [P] [US1] Extend `tests/physics_geometry_test.cpp` with confirmed-contact versus bounding-region false-positive cases across box-box, circle-circle and circle-box interactions, including rotated/touching/near-separated cases; retain manifold/support validity, feature stability and numerical tolerances as guardrails for geometry consolidation.

### Implementation for User Story 1

- [X] T012 [US1] Generate Enter on absent→present and Exit on present→absent in the existing serial post-worker reduction in `src/physicsmanager.cpp`, using `src/colliderpair.h::resultHadContact` as "Previous contact presence captured for the current evaluation, consumed after workers finish."; retain "At most one pending entry per pair; workers own distinct pairs." and optional Collision as "Sole source of confirmed-contact presence; a bounding-region overlap alone is insufficient."; never inspect scripts or append shared events in geometry workers, and remove Enter synthesis from the legacy Stay traversal so transitional US1 behavior cannot double-emit Enter.
- [X] T013 [US1] Move pair-removal Exit enqueue into `PhysicsManager::removePair` in `src/physicscontacts.cpp` before the erasure boundary in `src/grid.cpp`; make `src/colliderpair.cpp`/`src/colliderpair.h` destruction release memory only and remove event-emitting pair methods/runtime lookup, emit removal even when both endpoints are retired, and preserve "Previous per-endpoint support contributions; changes reduce once into body totals." so a geometry-ended pair cannot emit another removal Exit.
- [X] T014 [US1] Canonicalize generated transition batches in `src/physicsmanager.cpp::handlePendingPhysicsEvents`, including cleanup drains from `src/gamemanager.cpp`, by Exit-before-Enter then full endpoint handles with A-before-B delivery; enforce "Canonical endpoint pointers; full stable handles supply deterministic sorting, including generations." and payload constraint "Borrowed confirmed contact for Enter; null for Exit. It remains valid until Enter dispatch completes."; preserve per-endpoint exception aggregation, drain-before-free and old-scene-before-publication boundaries, leaving any temporary legacy Stay delivery separate from Enter/Exit generation until US2.
- [X] T015 [US1] Move shape dispatch, `checkProjections`, box SAT and `calculateContactPoint` from `src/physicsmanager.cpp` into `src/physicsgeometry.cpp`, renaming `satCreateCollision` to `updateContactGeometry` in `src/physicsmanager.h` and its caller; preserve calculations, feature IDs, orientation, tolerances, warm-start behavior and version semantics ("Determine whether pair geometry needs re-evaluation."), keep pair-local geometry free of event side effects, and move only required includes with the functions.
- [X] T016 [US1] Run the focused internal event, cleanup, geometry and existing physics/lifecycle regression checks from `specs/002-simplify-contact-events/quickstart.md` with one and multiple workers; retain exact canonical transition traces, supported callback-exception/teardown outcomes and preserved geometry/support results in `specs/002-simplify-contact-events/evidence/us1-validation.md`, fixing any US1 failure before continuing.

**Checkpoint**: US1 is a demonstrable MVP for script-independent Enter/Exit generation. Legacy Stay surfaces may still exist until US2; this checkpoint is not completion of the full requested feature.

## Phase 4: User Story 2 — Remove Stay from the Contact Lifecycle (Priority: P1)

**Goal**: Remove Stay callbacks and all Stay-only recurring work, simplify physical bookkeeping, and publish a consistent schema-3 reporting contract.

**Independent Test**: One Enter, 100 quiet updates with no additional events/work, then one Exit; no supported Stay callback/event/metric; listener-free and cleanup Enter/Exit counts agree across recorder output and live readers. Physical solver/support behavior remains correct.

### Tests for User Story 2

- [X] T017 [P] [US2] Add 100-update persistent stationary/sleeping/moving contact cases and supported-callback-surface checks in `tests/contact_events_test.cpp`; require no Stay or repeated Enter and exactly one later Exit, distinguishing required changed-geometry comparisons from a forbidden separate persistent-contact event traversal; record the legacy Stay failure before removal.
- [X] T018 [P] [US2] Extend `tests/benchmark_contract_test.cpp` for schema 3 throughout output, eight scopes and eleven counters without Stay, zero/one/two recipient count equivalence, cleanup Exit in the same complete-step sample, both-endpoint retirement, callback exceptions and out-of-frame events not leaking into the next sample; retain provenance, rendering separation, sample storage and output-error checks.
- [X] T019 [P] [US2] Add schema-3 reader/aggregate/plot expectations in `benchmark/tests/test_sweep_scaling.py`, including rejection of schema 2, mixed versions, missing/unknown/duplicate metrics, removed Stay fields, incomplete captures and validation-only performance input; preserve independent-process and common-fixture coverage established in T004.
- [X] T020 [P] [US2] Extend `tests/runtime_work_test.cpp` to warm quiet confirmed contacts with and without scripts, then assert zero transition comparisons, zero appended event records and zero warmed idle allocations using T008 probes; add moving-contact/churn observations that distinguish physical work from Stay-only work without imposing fabricated timing thresholds.

### Implementation for User Story 2

- [X] T021 [US2] Remove `Behavior::onCollisionStay`, `ScriptComponent::dispatchCollisionStay` and `Collider::triggerCollisionStay` declarations/definitions from `include/platformator/behavior.h`, `include/platformator/scriptcomponent.h`, `include/platformator/collider.h`, `src/behavior.cpp`, `src/scriptcomponent.cpp` and `src/collider.cpp`; remove the Stay event kind/dispatch in `src/physicsmanager.h`/`src/physicsmanager.cpp`, enforcing event type "Enter or Exit only." with no deprecated shim or replacement recurring callback.
- [X] T022 [US2] Remove `observed`, `stayPairs`, `stayIndex`, `stayOrderDirty`, `enteredStep`, `simulationStep` and the persistent event loop from `src/physicscontacts.cpp`, `src/physicsmanager.cpp`, `src/physicsmanager.h` and `src/colliderpair.h`; replace `refreshSubscriptions` with solver-only `refreshSolverPairs(Collider*)`, retain its physical body/collider callers in `src/physicsbodyindexes.cpp`/`src/gamemanager.cpp`, remove script/startup-only callers and `firstBehaviors`, and preserve body lists/change queues with "Existing indexed membership and sleep-island invariants; no script dependency." and response/support refresh ownership.
- [X] T023 [US2] Delete the redundant `activeCollisions` projection/rebuild in `src/physicscontacts.cpp`, `src/physicsmanager.cpp` and `src/physicsmanager.h`; resolve/debug directly through canonical `solverPairs`, retain reusable `dynamicCollisionScratch` and all-contact `getContactPairs()` for the independent `src/benchmark_main.cpp` manifold/support oracle, preserve "Membership in two distinct physical collections; absent membership uses the existing sentinel.", retain solver dirty ordering and O(1) physical membership, and remove `NarrowPhaseLocalResults` after a whole-repository use check.
- [X] T024 [US2] Remove obsolete Stay overrides/counters/phases from `tests/callback_mutation_test.cpp`, `tests/scene_transition_stress_test.cpp`, `tests/physics_support_test.cpp`, `tests/regression_tests.cpp` and `tests/queued_deletion_safety_test.cpp`; replace Stay-specific expectations with silence/transition expectations while retaining Enter/Exit mutation, scene, support, idle and deletion coverage, and remove the old Stay-only probe in `tests/runtime_work_probe.h`/`tests/runtime_work_test.cpp` once T020 uses real event-work counters.
- [X] T025 [P] [US2] After T021–T024, migrate the producer in `src/benchmark.h`/`src/benchmark.cpp` to schema 3 in summary/raw CSV, JSON manifests and console output, removing the Stay enum/name/column and retaining eight scopes/eleven counters; in `src/physicsmanager.cpp` accumulate Enter/Exit counters at the single enqueue boundary for both transition producers and delete the narrow-phase queue-count scan, counting generated pairs through cleanup rather than delivered recipients while preserving active-contact sampling and all other metric definitions.
- [X] T026 [P] [US2] After T021–T024, migrate live parsers/aggregates/plotting in `benchmark/sweep_scaling.py` and `benchmark/plot_scaling.py`, audit `benchmark/capture_cleanup.py` for affected assumptions, and update fixtures in `benchmark/tests/test_sweep_scaling.py` to schema 3 with no Stay metric; reject incompatible/mixed captures clearly, preserve provenance/completeness and five-repetition plot guards, and leave archived captures unchanged.
- [X] T027 [US2] Document callback source/binary rebuild requirements, generated-pair metrics including cleanup, schema-3 reader compatibility and deliberate Stay removal in `docs/CPP_CLEANUP_WALKTHROUGH.md` and `benchmark/README.md`; audit actual current callers/templates under `examples/mario/` and `ui/src/platformator_ui/`, update only affected consumers, and preserve scene serialization, archived specifications and historical benchmark files.
- [X] T028 [US2] After T025 and T026 converge, build normal and benchmark configurations, run the US2 contact/work/recorder/Python contract checks and public-header/package compatibility checks from `specs/002-simplify-contact-events/quickstart.md`; retain output inventories, quiet-work/allocator observations, schema rejection evidence and unchanged support/solver results in `specs/002-simplify-contact-events/evidence/us2-validation.md` before marking this story complete.

**Checkpoint**: Stay is absent across the supported callback/event/reporting surfaces. T021–T026 form one coordinated contract cut: do not claim a buildable/releasable US2 increment until stale overrides, producer and readers have all migrated.

## Phase 5: User Story 3 — Change Scripts without Changing Contacts (Priority: P2)

**Goal**: Script churn never creates contacts/transitions or physical subscription work, and startup responsibilities live on their owners without changing scheduling, mutation or failure semantics.

**Independent Test**: Attach/replace/activate scripts during a contact, observe no replay and the real future Exit, and compare physical state with a control run. Exercise per-script startup additions/cancellation/failures and callback-driven removal/scene changes with valid event payloads.

### Tests for User Story 3

- [X] T029 [P] [US3] Extend `tests/contact_events_test.cpp` with late script attachment, addition to an existing ScriptComponent, activation, removal, replacement and generation-to-delivery eligibility changes; assert no synthetic transition/replay, legitimate Exit without a previously delivered Enter, independent endpoint delivery and unchanged physical contact histories at one/multiple worker counts.
- [X] T030 [P] [US3] Preserve and extend startup guardrails in `tests/callback_mutation_test.cpp` for same-script/later-script/already-processed-script additions, per-behavior resolve/start failures, failed-attempt promotion, self-disable/retirement, pending suffix reactivation, tombstone cancellation and start-before-ordinary-dispatch; establish passing pre-extraction traces so moving ownership cannot silently change scheduling or failure behavior.
- [X] T031 [P] [US3] Extend `tests/physics_support_test.cpp` with script-churn/control-run comparisons and physical body type, sleep/wake and component replacement cases that require `refreshSolverPairs` without changed geometry; retain 128-support/reference-flip/filter/trigger cases and prove script changes neither alter support totals nor prevent wake-up after final support loss.
- [X] T032 [P] [US3] Extend `tests/scene_transition_stress_test.cpp` with Enter/Exit-driven script/collider/object removal, callback exceptions, both-endpoint retirement and scene replacement/shutdown; assert recipient suppression, no old-scene notification reaching new objects and safe borrowed payloads through main and cleanup drains under the existing lifecycle phases.

### Implementation for User Story 3

- [X] T033 [US3] Extract the current per-script initialization loop, per-attempt error logging and attempted-prefix transfer into private `ScriptComponent::startPendingBehaviors(platformator::ReferenceResolver&)` in `include/platformator/scriptcomponent.h`/`src/scriptcomponent.cpp`; rename the scheduler to `GameManager::startPendingScripts` in `src/gamemanager.h`/`src/gamemanager.cpp`, retaining dynamic indexed queue traversal, resolver lifetime and cancellation, rechecking eligibility before each attempt, promoting failed attempts exactly as before and leaving unattempted suffixes pending, with no new startup state or physics refresh.
- [X] T034 [US3] Instrument actual solver-adjacency refresh visits in `src/physicscontacts.cpp` using test-only fields in `tests/runtime_work_probe.h`, and add script-start/activation/replacement churn assertions in `tests/runtime_work_test.cpp` showing zero physical neighbor refresh, geometry and transition work for otherwise unchanged contacts; separately demonstrate required body-state changes still refresh solver membership so absence of script coupling cannot hide a broken physical refresh path.
- [X] T035 [US3] Run the contact, startup mutation/failure, support, work and scene stress acceptance checks from `specs/002-simplify-contact-events/quickstart.md`; compare one/multiworker traces and pre-extraction startup behavior, fix any observed eligibility/lifetime defect at its existing owner boundary, and retain results in `specs/002-simplify-contact-events/evidence/us3-validation.md` without adding a generic lifecycle or contact-history layer.

**Checkpoint**: Script/startup changes are isolated from physical contact state and neighbor work; all startup and physical-state-dependent solver guarantees remain observable and tested.

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Verify the full feature, measure costs/benefits, and leave reviewable evidence. All three stories are prerequisites. Run resource-heavy builds/tests and timing captures sequentially; do not time while other work competes for the same machine.

- [X] T036 Audit current runtime/headers/tests/tooling/docs in `src/`, `include/platformator/`, `tests/`, `benchmark/`, `docs/`, `examples/mario/` and `ui/src/platformator_ui/` for removed Stay symbols/columns, `refreshSubscriptions`, `triggerStartedBehaviors`, `satCreateCollision` and unused aggregation/projection state; fix live remnants and record exclusions for archival files/ordinary English text in `specs/002-simplify-contact-events/evidence/current-contract-audit.md`, preserving historical captures and unrelated user edits.
- [X] T037 Build the final cleanup-debug and production configurations from `CMakePresets.json`, including supported example consumers, independent public headers and `tests/package_smoke.cmake`; run the complete normal CTest suite and benchmark contract suite from `specs/002-simplify-contact-events/quickstart.md`, recording exact build/test commands and outcomes in `specs/002-simplify-contact-events/evidence/final-correctness.md` and resolving compatibility/scene/lifecycle regressions.
- [X] T038 Run cleanup-work instrumentation for quiet script-observed/unobserved contacts, moving contacts, body changes, startup churn and insertion/removal churn using `tests/runtime_work_test.cpp` and `tests/contact_events_test.cpp`; retain actual event/geometry/adjacency visit counts, allocation observations and retained vector/storage growth against T006 in `specs/002-simplify-contact-events/evidence/affected-work-memory.md`, proving zero event-only persistent scans and warmed idle allocation reuse while distinguishing required solver/body work.
- [X] T039 Run cleanup-asan (ASan/UBSan) and cleanup-tsan configurations from `CMakePresets.json` over the final physics/lifecycle/mutation/scene cases in `tests/`, using `specs/002-simplify-contact-events/quickstart.md`; investigate failures and record actual race/lifetime results or exact runtime/platform limitations in `specs/002-simplify-contact-events/evidence/sanitizers.md`, never treating a skip or unavailable runtime as a successful check.
- [X] T040 Capture final schema-3 validation-only runs using `benchmark/sweep_scaling.py --validate-invariants` for the common persistent fixture and all baseline workload sizes/worker counts; compare canonical contact/support/body snapshots with T005 tolerances and record ordered event-trace comparisons plus physical-state equivalence in `specs/002-simplify-contact-events/evidence/parallel-validation.md`, labeling correctness-instrumented captures `timing_eligible=false`.
- [X] T041 After all builds/correctness checks finish, capture the final Release matrix in `specs/002-simplify-contact-events/quickstart.md` using `benchmark/sweep_scaling.py` with at least five independent repetitions and exactly matching baseline fixture/workload/build/seed/dt/affinity/worker choices; retain raw samples/manifests/logs/checksums, complete-step and relevant nested-scope means/tails/run spread, and one/multiworker scaling under `specs/002-simplify-contact-events/evidence/candidate-results.md`, keeping work probes/sanitizers/debug tools and competing jobs out of timing captures.
- [X] T042 Analyze baseline/candidate comparisons in `specs/002-simplify-contact-events/evidence/performance-analysis.md`, investigating every timing/allocation regression and the solver dereference/event-sort/startup tradeoffs, serial reduction cost and worker load balance; fix feasible affected-path inefficiencies in `src/physicsmanager.cpp`, `src/physicscontacts.cpp`, `src/physicsbodyindexes.cpp` or `src/scriptcomponent.cpp` only when evidence warrants them, rerun affected correctness and comparable captures after fixes, and document any justified retained inefficiency with the constitution-required local `EFFICIENCY:` cost/frequency/reason/alternative/replacement-work comment instead of claiming unmeasured gains.
- [X] T043 Finalize `specs/002-simplify-contact-events/validation.md` with FR/SC-to-evidence links, actual commands/results, baseline/candidate provenance, compatibility migration, investigated regressions and remaining environment limitations; update `specs/002-simplify-contact-events/quickstart.md` and current `benchmark/README.md`/`docs/CPP_CLEANUP_WALKTHROUGH.md` to match implemented behavior and runnable commands, recheck all seven constitution gates and mark only tasks whose stated work/evidence is complete.

## Dependencies & Execution Order

### Phase and Story Dependencies

```text
T001 → (T002 || T003) → T004 → T005 → T006 [baseline gate]
  → T007 → T008 [foundation gate]
  → US1: (T009 || T010 || T011) → T012 → T013 → T014 → T015 → T016
  → US2: (T017 || T018 || T019 || T020)
       → T021 → T022 → T023 → T024 → (T025 || T026) → T027 → T028
  → US3: (T029 || T030 || T031 || T032) → T033 → T034 → T035
  → T036 → T037 → T038 → T039 → T040 → T041 → T042 → T043
```

The graph is the default executable order; parenthesized peers may run together on disjoint files. An implementation fix or newly discovered failure requires rerunning affected checks and refreshing affected captures before dependent completion, rather than declaring an old passing result current.

- **US1** depends only on setup/foundation and is independently demonstrable for Enter/Exit transition semantics. It deliberately precedes the full Stay compatibility cut.
- **US2** depends on US1's transition producers so removal of observer/Stay state cannot lose Enter generation. T021–T024 touch shared runtime/test surfaces and are sequential. T025 and T026 can proceed together only after those changes and the prewritten schema tests; integrate both before T028.
- **US3** depends on US2's script-free physical refresh boundary. Its new behavior scenarios are independently testable, but implementation is not concurrent with the prior edits to GameManager, ScriptComponent or physical refresh code.
- **Final acceptance** depends on all stories. T040 correctness precedes T041 timing; T042 analysis may require targeted fixes and repeated affected verification. Do not broaden unrelated subsystems merely to fill tasks.

### Within Each Story

Write changed-contract tests first and capture meaningful failures; retain existing passing guardrails for mechanical ownership changes. Implement pair/model ownership before transition consumers, update all affected callers before the story checkpoint, and validate the story's explicit observable outcome. Maintain working-file ownership even when test-authoring peers are parallelized. No task authorizes sending external messages, publishing, or merging.

## Parallel Example: User Story 1

After T008, T009 can author event/recipient tests in `tests/contact_events_test.cpp` while T010 authors witness/removal tests in `tests/collider_pair_cleanup_test.cpp` and T011 authors geometry guardrails in `tests/physics_geometry_test.cpp`. Join all three before the sequential runtime tasks. Do not run builds while these shared test interfaces are being revised.

## Parallel Example: User Story 2

After T016, T017/T018/T019/T020 can author their disjoint contact/recorder/Python/work tests together. After T024, T025 may migrate the C++ producer while T026 migrates Python readers/fixtures against the already-defined schema-3 contract. Join both before building, capturing or claiming reporting compatibility. Do not parallelize T021–T024, which overlap runtime declarations and callers.

## Parallel Example: User Story 3

After T028, T029/T030/T031/T032 can author their disjoint contact/startup/support/scene cases together. Join before T033 startup extraction and T034 instrumentation/integration. ScriptComponent and GameManager changes remain one sequential responsibility extraction; the test tasks do not permit concurrent modification of those files.

## Requirement and Design Coverage

| Requirement/design obligation | Primary tasks | Acceptance evidence |
| --- | --- | --- |
| FR-001, FR-002: script-independent actual transitions/removals | T009, T010, T012, T013 | T016, T028, T035 |
| FR-003: confirmed shapes and spatial-witness deduplication | T010, T011, T013, T015 | T016, T040 |
| FR-004 / SC-002: silence during unchanged contact and real re-contact | T009, T017, T020, T022 | T028, T038 |
| FR-005, FR-006 / SC-003: delivery eligibility, script churn, no replay | T009, T014, T029, T033, T034 | T016, T035 |
| FR-007 / SC-004: callback/event/metric removal and consumer migration | T021, T024, T025, T026, T027 | T028, T036, T037 |
| FR-008 / SC-006: no Stay-only bookkeeping/work | T008, T020, T022, T023, T034 | T038, T041, T042 |
| FR-009: support, response, sleeping/waking and body indexes | T013, T015, T022, T023, T031 | T035, T037, T040 |
| FR-010 / SC-005: deterministic ordering and safe lifecycle | T010, T014, T030, T032, T033 | T016, T035, T039, T040 |
| FR-011: generated pair metrics and schema compatibility | T018, T019, T025, T026 | T028, T037 |
| SC-001: exact Enter/Exit lifetime counts across recipients | T009, T012, T013, T029 | T016, T035 |
| User-named smells and clearer ownership | T015 geometry; T022 physical refresh/body indexes; T023 contact/solver state; T033 startup | T016, T028, T035, T042 |
| Constitution: baseline, parallel strategy, allocation/tails, provenance | T001–T008, T038–T042 | T043 and linked evidence |

## Implementation Strategy

### MVP First

Complete setup/foundation and US1 through T016 to demonstrate transition-driven Enter/Exit without scripts. This is the smallest useful increment; it does not satisfy the full removal request while legacy Stay remains. Continue autonomously through US2, US3 and final acceptance for the complete feature.

### Incremental Delivery

1. Preserve the validated baseline and common fixture before modifying runtime behavior.
2. Establish explicit transition/removal ownership and validate US1.
3. Remove Stay and duplicate state, migrate all live reporting/consumers, then validate US2.
4. Complete startup ownership and script-isolation proof, then validate US3.
5. Run full correctness, sanitizer/work validation, comparable measurements and evidence review; rerun affected checks only when changes/failures justify it.

### Scope and Completion Rules

Retain physical contact/solver/body/support indexes with their documented purposes; retain the independent all-contact oracle. Do not redesign broad phase, collision math, sleep algorithms, phase scheduling or public contact queries. Preserve native lifetime/worker boundaries, archived captures and prior user edits. Measurement targets are evidence-based; no arbitrary speedup is required. Missing baseline or unresolved correctness/regression evidence cannot be converted into a completed performance task. Report genuine external/runtime limitations explicitly and retain the applicable work as unverified where necessary.

**Task inventory**: 43 total: setup 6; foundation 2; US1 8; US2 12; US3 7; cross-cutting 8. Fifteen `[P]` tasks form five explicit parallel groups. All 43 tasks are complete within the validation scope and limitations recorded in validation.md.
