# Tasks: Simplify Runtime Lifecycle

**Input**: Design documents from `specs/004-simplify-runtime-lifecycle/`.

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [runtime contract](contracts/runtime-lifecycle.md), [evidence contract](contracts/validation-evidence.md), [quickstart.md](quickstart.md), and constitution 1.1.0.

**Tests**: Included because FR-005–FR-011 and SC-003–SC-006 require behavior, safety, work, and cost validation. This is a behavior-preserving refactor: characterization must pass on the baseline before production edits; do not intentionally make correct existing behavior fail to satisfy the template's generic TDD example. Add coverage for actual gaps, not formatting or already-covered implementation details.

**Organization**: US1 (P1) covers J1–J3: GameManager/frame/lifecycle/transforms. US2 (P1) covers J4–J5: sleeping/waking. US3 (P2) covers J6: runtime/scene lifetime. Every task is initially incomplete. This file generates work; it does not authorize publishing or claim implementation success.

## Format: `[ID] [P?] [Story] Description`

- `[P]` identifies independent file edits or isolated validation after stated prerequisites; it does not authorize overlapping timings with other work.
- Story tasks carry `[US1]`, `[US2]`, or `[US3]`. Setup, foundation, and final tasks have no story label.
- Paths are relative to the repository root. Feature evidence paths are written in full for execution clarity. Bulky captures live in a fresh ignored directory or `/tmp`, with durable locations/checksums recorded in the feature.
- Apply FR-004 to every new/changed function, including tests and lambdas: brace every control-flow body, put statements on separate lines, and separate logical stages with sensible blank lines. Do not reformat unrelated files.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish the actual input and the human-audit baseline without editing runtime code. Preserve the existing `001-cpp-engine-cleanup` checkout; feature directory selection does not imply a branch switch.

- [X] T001 Record the current commit, staged/unstaged changes, relevant untracked inputs, toolchain/build settings, dependency versions, available physical CPUs/affinity and selected workers in `specs/004-simplify-runtime-lifecycle/validation.md`; create a fresh capture root using `specs/004-simplify-runtime-lifecycle/quickstart.md`, preserve original input patches/status, and distinguish prior unresolved performance/sanitizer findings from this feature's new baseline.
- [X] T002 [P] Create `specs/004-simplify-runtime-lifecycle/inventory.md` and `specs/004-simplify-runtime-lifecycle/audit-before.md` from current GameManager, RuntimeAccess, transform, sleep and support paths; map J1–J6 to owners/producers/consumers/visibility/free boundaries, give every reviewed redundancy a remove/simplify/retain disposition with purpose/cost/alternatives, and record why hierarchyDepth, clock accounting, public support/timer methods, phase guards, indexed work and support batching remain.

**Checkpoint**: Input identity and before-state audit exist. No production refactor has begun.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Build only the common measurement fixture and capture the current engine before any production cleanup. Complete this phase before story implementation.

- [X] T003 [P] Create the benchmark-only host `benchmark/runtime_cleanup_probe.cpp` and register `platformator_runtime_cleanup_probe` in `CMakeLists.txt` only when tests and benchmarks are enabled; implement the evidence contract's CLI, physics-only Runtime, worker control, schema-3 recorder/output metadata, separate invariant/timing modes, sample/population checks, and quiet/mixed modes with verified sleeping/active counts. Keep the probe outside engine sources, reserve samples after warmup, and preserve existing timing boundaries and public APIs.
- [X] T004 Add wake_burst and connected modes to `benchmark/runtime_cleanup_probe.cpp` after T003: deterministic 1/10 wake subsets with actual movement at recorded intervals; gravity-free dynamic contact groups with verified graph/population, all-quiet and moving-member controls, actual timers and transitions. Reject an invalid or disconnected fixture instead of assuming warmup proves it; record all resolved parameters and perform expensive checks outside timed scopes.
- [X] T005 Add hierarchy and lifecycle modes to `benchmark/runtime_cleanup_probe.cpp` after T004: repeated root edits plus an overlapping child in a fixed hierarchy; fixed-peak callback-time create/remove/replacement plus periodic scene replacement. Create scene files before capture, keep requested/commit/destruction work inside the existing step, record replacement samples separately, and verify resulting populations and reference lifetimes without adding production hooks.
- [X] T006 [P] Create a small sequential capture driver in `specs/004-simplify-runtime-lifecycle/validation/capture.py` using the evidence-contract matrix and existing runner/sweep/recorder interfaces. Accept explicit baseline/candidate runner and probe paths, output root, worker/affinity configuration, and baseline/candidate/both stages with validate/time modes; provide a list-jobs mode, retain a reproducibly shuffled alternating stage schedule, refuse overwrites, keep subprocess logs/statuses/manifests/checksums, and reject wrong schema, source identity, fixture parameters, sample counts or timing eligibility. Use controlled contract comparisons and general physical invariants appropriately; do not reuse old exact-trajectory comparisons or build a general benchmark framework.
- [X] T007 Freeze an isolated current-engine source snapshot and baseline binaries after T003–T006, using `specs/004-simplify-runtime-lifecycle/quickstart.md`; record source/fixture/build fingerprints and capture paths in `specs/004-simplify-runtime-lifecycle/evidence/baseline-provenance.md`. Include all common probe inputs and relevant untracked files; preserve actual dirty-tree provenance and leave historical captures unchanged. Exercise the capture driver's job listing and failure handling before trusting captures.
- [X] T008 Build/run the baseline ordinary tests, applicable work/benchmark contracts, all controlled modes and spatial/churn/contact correctness groups, then collect preliminary baseline cost samples before production edits using `specs/004-simplify-runtime-lifecycle/validation/capture.py`; record results, active/sleeping populations, allocations/storage and any failures in `specs/004-simplify-runtime-lifecycle/evidence/baseline-results.md`. Validate counts 100/1000, dense controls 50/300, existing churn controls, and workers 1/2/available maximum as applicable. Preliminary timing is characterization, not the final five-process comparative result; unresolved baseline issues must be classified explicitly.

**Checkpoint**: A valid current baseline and common fixtures exist. No production source has changed. Reject bad fixtures before proceeding; an unrelated baseline finding remains visible rather than becoming an implicit gameplay fix.

Later story-specific test additions run against both candidate and a fresh validation copy of the frozen baseline production source. Never edit the frozen snapshot/binaries or overwrite existing captures. Keep test-only overlays and their hashes separate from the common timing fixture; if the probe, build configuration or measured code changes, version and recapture the affected matched baseline groups.

---

## Phase 3: User Story 1 — Follow GameManager's execution flow (Priority: P1, MVP)

**Goal**: Remove duplicated frame/private-access paths and make J1–J3 readable while preserving lifecycle and transform barriers.

**Independent Test**: Existing and extended runtime-step, benchmark-contract, hierarchy, mutation/destruction and work hosts pass; J1–J3 have code-backed before/after explanations and a concrete maintenance reduction. No US2 or US3 production change is required for this increment.

### Characterization before production changes

- [X] T009 [P] [US1] Extend only uncovered entry-path cases in `tests/runtime_step_test.cpp` and `tests/benchmark_contract_test.cpp`: callback versus empty-callback rendering, pause/manual advance/quit, exact hook order, skipped-path dt handling, exception cleanup, and independent render/complete-step sampling. Preserve existing clock tests and verify the same baseline behavior for both entry paths without introducing new callback abstractions.
- [X] T010 [P] [US1] Add missing overlap/cancellation assertions in `tests/transform_hierarchy_test.cpp` and `tests/runtime_work_test.cpp`: parent and child queued together, repeated root edits and retirement before synchronization. Preserve the model rules "At most one queued entry; swap-removal repairs the moved slot; cancellation precedes free." and "A subtree reached through overlapping queued ancestors/descendants is published once per pass." Also preserve "Preserve current immediate read semantics and physics/render publication points. Workers integrate only their own local fields." and "Rebuild order on hierarchy changes; sort dirty nodes parent-first only when required." Reuse existing work probes rather than add runtime state or duplicate covered cases.
- [X] T011 [US1] Run T009–T010 and the existing callback/destruction/queued-deletion hosts against the unchanged candidate and a fresh baseline validation copy; retain outputs and fixture hashes in `specs/004-simplify-runtime-lifecycle/evidence/us1-characterization.md`. Establish expected traces, work counts and tolerances before T012; keep unrelated failures and unavailable checks distinct from passes.

### Implementation and independent validation

- [X] T012 [US1] Consolidate rendered-frame entry points in `src/gamemanager.cpp` by delegating the no-callback call to the existing callback-capable path with an empty callback; format simulation stages/lambdas with braces and logical spacing. Retain PhaseGuard and the exact constraint "Previous phase is restored on normal or exceptional exit; structural mutation follows existing thread/phase restrictions." Preserve event/quit/debug/advance behavior, phase order, error precedence and recorder boundaries without new type erasure or warmed allocations.
- [X] T013 [US1] Recheck callers and remove dead private `removeGameObject(std::string)` and `deleteMarkedGameObjects` declarations/definitions in `src/gamemanager.h` and `src/gamemanager.cpp`; make the internal `getGameObjects` accessor a const method returning a const reference compatible with `Runtime::getAllGameObjects`. Retain pointer-based removal and current ownership; do not remove public Runtime methods or change hierarchyDepth/clock accounting.
- [X] T014 [US1] Clean the touched lifecycle/transform orchestration functions in `src/gamemanager.cpp` and declarations in `src/gamemanager.h`: brace and space branches/loops, clarify domain actions and concise non-obvious barriers, and remove only newly proven redundancy. Preserve "Pending/draining work are distinct during reentrant callbacks. One pending entry per queued component; clear/requeue at the established boundary.", "Registration records applied system membership; callback eligibility can change before registration catches up. They are not interchangeable.", and "Eligible behavior starts once; inactive or retired work does not start; later reactivation does not repeat initialization." Keep the retirement rule "Identity is invalidated immediately; allocations survive required support/event/destruction work. All callbacks in the batch finish before its storage is freed." Retain hierarchy rebuild/order, queue repair, stamps and existing public transform semantics; do not introduce global scans or a replacement coordinator.
- [X] T015 [US1] Rebuild and run J1–J3 affected runtime-step, benchmark-contract, lifecycle/mutation/destruction, hierarchy and work checks after T012–T014, including the ordinary and instrumented builds needed for each host; write actual results and scoped before/after cost observations to `specs/004-simplify-runtime-lifecycle/evidence/us1-results.md`. Verify no empty-callback allocation, no duplicate subtree publication, preserved callback/retirement behavior and debug-enabled/disabled entry paths; investigate introduced failures before accepting the increment.
- [X] T016 [US1] Write `specs/004-simplify-runtime-lifecycle/audit-us1.md` with J1–J3 before/after source links, removed maintenance paths, retained owners/barriers and reasons, and actual T015 evidence. Demonstrate actual simplification rather than only formatting or file movement; prepare review questions without recording maintainer comprehension as passed.

**Checkpoint**: US1 is an independently reviewable MVP. Its technical checks and review material are available; actual maintainer readability acceptance remains separate.

---

## Phase 4: User Story 2 — Explain sleeping and waking (Priority: P1)

**Goal**: Simplify transition logic and make retained group/participation work readable without changing sleep, wake, support or contact semantics.

**Independent Test**: Baseline/candidate controls preserve no-op timer behavior, transition-only resets, connected-group minimum quiet time, impact resumption and committed support transfer; existing sleeping pile/contact/work tests remain valid with one and multiple workers.

### Characterization before production changes

- [X] T017 [P] [US2] Extend `tests/physics_bookkeeping_test.cpp` for already-awake no-op wake calls preserving a nonzero quiet timer, real transition resets/entry motion clearing, and connected candidates with unequal prior timers plus a moving-member control. Quote and enforce "A real sleep/wake transition resets the timer; an unchanged request does not." and "First pass aggregates every candidate's motion and minimum persistent timer. Second pass computes common elapsed quiet time or reset, then applies individual transitions." Verify actual contact connectivity and current thresholds without requiring arbitrary solver trajectories.
- [X] T018 [P] [US2] Add a controlled sleeping-body impact/wake/resumed-participation case to `tests/physics_support_test.cpp`, reusing existing support-transfer/replacement checks and covering admission after integration and relevant wake-threshold boundaries. Preserve "Commit net support once per batch. Wake on genuine committed final loss, not an intermediate zero during transfer. Old recipient survives through this flush." Preserve the contributor rule "Identify the exact old/new recipient even during replacement; no contributor means no applied-or-queued contribution." Keep final-support loss and net-zero transfer assertions distinct; do not add tests mirroring braces or private helper layout.
- [X] T019 [US2] Execute T017–T018 and existing regression/contact/support controls on a fresh frozen-baseline validation copy and the pre-US2 candidate, with workers 1 and available multiple counts; record expected transitions, declared tolerances, fixture hashes and results in `specs/004-simplify-runtime-lifecycle/evidence/us2-characterization.md` before editing production sleep code. Treat differing unconstrained solved trajectories separately from contract violations.

### Implementation and independent validation

- [X] T020 [P] [US2] Simplify `Rigidbody::setIsSleeping` in `src/rigidbody.cpp`: keep the unchanged-state return first, retain motion/force clearing only on entry, place one shared timer reset after that guard, and remove stale commented logging/empty branch scaffolding. Preserve public support/timer methods, setter wake effects and the rule "Sleep entry also clears linear/angular motion and accumulated force/torque." Brace and space every changed function; do not reset timers for already-awake no-op wake requests.
- [X] T021 [P] [US2] Clean the sleep-related paths in `src/physicsmanager.cpp` and retain the inline reset in `src/physicsmanager.h`: remove the unused impact-predicate receiver parameter and stale commented reconciliation call, expand cramped force/sleep lambdas and branches, and explain the two-pass decision/drain boundary. Keep thresholds, candidate admission, solver work/order and joins unchanged; do not merge distinct motion predicates or add per-step island allocation. Preserve the rule "Reset before awake integration and when newly entering candidates; solver preparation unions eligible bodies. Find uses root discovery plus path compression; merge uses rank."
- [X] T022 [P] [US2] Clarify and brace membership, queue, root-discovery/compression and union code in `src/physicsbodyindexes.cpp`, preserving both find passes, rank handling, indexed swap-repair and new-candidate reset. Preserve "Derived from current registration, activity, type, sleep, motion, support, and gravity at reconciliation boundaries. Keep indexed add/remove and bounded candidate iteration." and "Coalesce repeated changes. Cancel before removal/free. Do not mutate fixed traversal membership until its prescribed drain." Add no duplicate flags, global scans or generic indexed-container framework.
- [X] T023 [US2] Rebuild/run T017–T018 plus existing bookkeeping/support/contact/geometry/transform and stable sleeping-pile regressions, and work probes at workers 1/2/available maximum after T020–T022; record results, actual candidate/active populations and allocations/storage in `specs/004-simplify-runtime-lifecycle/evidence/us2-results.md`. Exercise repeated type/gravity/activation/sleep changes and pending retirement, verifying post-sleep list updates occur only after both traversals; preserve "Contact membership, solver participation, and callbacks are separate consumers. Sleep changes refresh affected physical adjacency; solver order stays append/swap-remove."
- [X] T024 [US2] Write J4–J5 code-backed before/after paths in `specs/004-simplify-runtime-lifecycle/audit-us2.md`, distinguishing persistent body timers, step-local island scratch, derived participation and net support batching. Record the actual removed duplication and purpose/cost of retained scratch, inline reset and sequential aggregation; link T023 evidence and leave maintainer readability findings pending.

**Checkpoint**: US2 can be validated independently against the common baseline without US1/US3 production changes. Integrating it preserves the already-validated US1 contracts.

---

## Phase 5: User Story 3 — Trust the surrounding runtime (Priority: P2)

**Goal**: Remove dead scene sequencing and duplicate active-manager state while preserving scene transactions, runtime access, resource ownership and teardown order.

**Independent Test**: Runtime/scene/reference hosts preserve last-request-wins, failed preparation, permitted callback access during explicit shutdown, no-runtime rejection, initialization failure and recreation; public consumers remain compatible.

### Characterization before production changes

- [X] T025 [P] [US3] Extend `tests/runtime_foundation_test.cpp` with privileged RuntimeAccess checks before runtime creation, during permitted shutdown callbacks and after destruction, alongside failed initialization coverage where absent; make only that host INTERNAL in `CMakeLists.txt` if private header access is necessary. Preserve existing reference and callback-failure cases, test the current inactive-runtime `logic_error`, and avoid new public testing hooks or invoking forbidden mutation from destruction callbacks.
- [X] T026 [P] [US3] Extend only missing lifetime/access assertions in `tests/runtime_recreation_test.cpp` across two runtime lifetimes, proving old references cannot revive and access binds to the new Runtime; enforce "Bound references expire at logical retirement and cannot revive in a later runtime or reused slot. Preserve." Reuse the existing scene transition contract/stress and object-reference cases for last-wins/failed-load behavior instead of duplicating those tests.
- [X] T027 [US3] Run T025–T026 and existing scene transition/failure, destruction, runner-startup and reference hosts against a fresh baseline validation copy and the pre-US3 candidate; record fixtures, test-only build overlays, no-runtime error behavior and callback/resource access order in `specs/004-simplify-runtime-lifecycle/evidence/us3-characterization.md`. Trace manager/physics/window destructors to ensure no required access moves into reset; resolve contrary evidence by revising the design before removing the fallback.

### Implementation and independent validation

- [X] T028 [US3] Remove `SceneRequest::sequence`, `sceneRequestSequence` and their increment from `src/gamemanager.h` and `src/gamemanager.cpp`; preserve the outer pending marker and optional payload with exact meanings "No scene action selected", "Clear the current scene", and "Prepare and replace with that scene". Keep last-wins selection, preparation failure and later-request deferral; enforce "Preserve `nextFileId` bounds and collision checks." and "Scene IDs and stable handles serve different reference contracts. Preserve both and their existing update order." Brace/space the changed request/commit functions.
- [X] T029 [US3] Consolidate active-runtime authority in `src/runtime.cpp`, `src/runtimeaccess.cpp`, `src/gamemanager.h` and `src/gamemanager.cpp`: remove GameManager static instance storage/getters/setter/publication, route privileged access through Runtime's manager accessor, and reject unavailable owner/manager with the existing inactive-runtime `logic_error` at that boundary. Preserve "Publish after successful manager construction; keep through explicit shutdown; clear after manager reset. Access without a live runtime rejects as before." and "No worker or callback outlives this ownership interval." Keep explicit Runtime shutdown before reset, idempotent manager destruction, resource teardown order and the intentional RuntimeAccess boundary; remove no public API.
- [X] T030 [US3] Rebuild/run focused runtime foundation/recreation, scene transition contract/stress, object-reference, destruction, asset/runner startup and failure checks after T028–T029; document actual results in `specs/004-simplify-runtime-lifecycle/evidence/us3-results.md`. Verify mixed load/clear request semantics, failed preparation retaining the old scene, next-step starts, saving/identity contracts and shutdown callback access; distinguish pre-existing backend findings from introduced failures.
- [X] T031 [US3] Write J6 before/after paths in `specs/004-simplify-runtime-lifecycle/audit-us3.md`, showing one runtime authority, eliminated sequence state, request selection/preparation/publication and shutdown-access/free boundaries. Link actual T030 results and identify fewer independently coordinated facts without claiming binary layout or speed improvements that were not measured.

**Checkpoint**: J6 is independently testable. US1 and US3 touch GameManager/CMake, so integrate their production changes sequentially; US3 needs no US2 algorithm change.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Review the integrated code, freeze it for final validation, and report human/correctness/performance findings separately.

- [X] T032 Review integrated changes in `src/gamemanager.h`, `src/gamemanager.cpp`, `src/runtime.cpp`, `src/runtimeaccess.cpp`, `src/rigidbody.cpp`, `src/physicsmanager.h`, `src/physicsmanager.cpp`, `src/physicsbodyindexes.cpp`, changed tests and `benchmark/runtime_cleanup_probe.cpp`; verify all FR-004 braces/spacing, actual maintenance reduction, preserved scope and concurrency/lifetime barriers, and reconcile `specs/004-simplify-runtime-lifecycle/inventory.md`. Retain "First prevents structural mutation during teardown; second makes repeated shutdown safe. Different facts; retain both." for shutdown flags; explain any genuinely retained inefficient path using the constitution's required local `EFFICIENCY:` treatment. Record Constitution Check, ownership, complexity and parallel/sequential rationale before freezing candidate code.
- [X] T033 [P] Build the integrated ordinary debug and production configurations, run the full ordinary CTest suite including package/runner/source-header/public-consumer checks, and follow applicable commands in `specs/004-simplify-runtime-lifecycle/quickstart.md`; retain complete statuses/logs in `specs/004-simplify-runtime-lifecycle/evidence/final-compatibility.md`. Confirm supported Runtime/Rigidbody authoring calls, scene/asset formats, clock/debug behavior and consumer compatibility; identify instrumentation skips rather than count them as passes.
- [X] T034 [P] Run final baseline/candidate work/allocation and fixed-peak lifecycle/scene/recreation checks from `tests/runtime_work_test.cpp` and the affected hosts with workers 1/2/available maximum; record warmed idle, subtree visits, participation, vector-capacity and memory trends in `specs/004-simplify-runtime-lifecycle/evidence/final-work-memory.md`. Verify expected populations return without stale work or continuing unexplained growth; label C++ allocation-probe scope and allocator/process-memory limitations, keeping these durations out of performance results.
- [X] T035 [P] Build/run the final ASan/UBSan lifecycle, scene, transform and sleep/support selections with leak detection enabled according to `specs/004-simplify-runtime-lifecycle/quickstart.md`; retain actual statuses, stacks and baseline/candidate comparisons in `specs/004-simplify-runtime-lifecycle/evidence/asan-ubsan.md`. Distinguish new access/UB/lifetime issues from reproduced FluidSynth or environment findings; keep unresolved/unsupported checks visible and introduce no detector suppression.
- [X] T036 [P] Build/run relevant final TSan worker/physics/mutation/lifecycle selections according to `specs/004-simplify-runtime-lifecycle/quickstart.md`; retain statuses, stacks, worker configuration and attribution limits in `specs/004-simplify-runtime-lifecycle/evidence/tsan.md`. Investigate new races/deadlocks; do not declare SDL/libdbus warnings resolved from historical evidence or disable detection to pass.
- [X] T037 Build final baseline/candidate release benchmark runners/probes and validate every applicable controlled and existing matrix group using `specs/004-simplify-runtime-lifecycle/validation/capture.py`; record fingerprints, populations, connectivity, oracle results and schema/boundary checks in `specs/004-simplify-runtime-lifecycle/evidence/final-matrix-validation.md`. For the final baseline use a fresh validation copy containing all common test/probe/build-only overlays; prove its production sources still match the frozen baseline, and keep the original snapshot/binaries unchanged. Confirm common fixture identity, one/multiple workers and exact controlled contracts versus invariant-based unconstrained checks before timings. Resolve introduced correctness failures; record reproduced safety/environment limitations from T033–T036 without relabeling them as success.
- [X] T038 After all builds/checks finish, capture matched final baseline/candidate timings through `specs/004-simplify-runtime-lifecycle/validation/capture.py` for the full applicable evidence-contract matrix: counts 100/1000, dense 50/300, churn/persistent-contact controls, all six probe modes, workers 1/2/available maximum, seed 0, 120 warmup/600 measured steps and five independent processes per group. Pin recorded CPU sets, alternate reproducibly shuffled matched jobs, retain raw outputs/logs/manifests/checksums and reference them from `specs/004-simplify-runtime-lifecycle/evidence/final-timings.md`; run no builds, safety jobs or other captures concurrently.
- [X] T039 Analyze T034–T038 in `specs/004-simplify-runtime-lifecycle/evidence/performance-analysis.md`: complete-step and relevant subsystem means/median/p95/p99/max, independent-run variation, active/work/allocation/retained-memory costs and worker scaling. Investigate every regression and document its cause/tradeoff/disposition under the constitution; use matched follow-ups where necessary, never sum nested scopes or infer equivalence from overlapping ranges. Any code/fixture fix reopens affected correctness and comparable captures; unresolved evidence stays unverified and this task remains incomplete where acceptance is not supported.
- [X] T040 Finalize `specs/004-simplify-runtime-lifecycle/validation.md`, `specs/004-simplify-runtime-lifecycle/inventory.md`, `specs/004-simplify-runtime-lifecycle/audit-us1.md`, `specs/004-simplify-runtime-lifecycle/audit-us2.md`, `specs/004-simplify-runtime-lifecycle/audit-us3.md` and `specs/004-simplify-runtime-lifecycle/quickstart.md` with actual paths/commands/results, six final code-backed walkthroughs, concrete reductions and remaining limitations. Check FR-001–FR-012/SC-001–SC-006 traceability, archive durable baseline/capture provenance, and present a concise review-ready result. Report pending readability separately; do not modify specification-quality checklist markers to represent implementation progress.
- [ ] T041 Present the completed J1–J6 code walkthroughs and final changed functions to the maintainer, then record their actual readability findings and resolutions in `specs/004-simplify-runtime-lifecycle/maintainer-review.md`. Only mark this task complete after the maintainer can explain each path's order/ownership/visibility/cleanup with no unresolved questions and confirms improved auditability; absent feedback remains pending. Address in-scope feedback concretely and rerun affected checks/captures after code changes rather than self-certifying human comprehension.

---

## Dependencies & Execution Order

### Phase dependencies

```text
Setup T001–T002
  ├─ T003 → T004 → T005 (common probe)
  └─ T006 (independent capture driver)
Both foundation branches → T007 → T008

T008 → US1: T009 + T010 → T011 → T012 → T013 → T014 → T015 → T016
T008 → US2: T017 + T018 → T019 → T020 + T021 + T022 → T023 → T024
T008 → US3: T025 + T026 → T027 → T028 → T029 → T030 → T031

All stories integrated → T032 → T033 + T034 + T035 + T036
                              → T037 → T038 → T039 → T040 → T041
```

T037 waits for T033–T036 to finish and for introduced correctness issues to be resolved; T038 waits for all build/check activity to stop. T039 consumes work/memory and safety results as well as timing. Known unresolved findings remain explicitly limited; they are never silently converted to passes.

### Story independence and shared files

- US1 and US2 are independently executable after foundation; US3 also has independent behavioral criteria. The default sequence is US1 → US2 → US3 for review clarity.
- US1 and US3 edit `src/gamemanager.h`/`src/gamemanager.cpp`; T025 also touches `CMakeLists.txt`, so integrate those edits sequentially. No dependency on a US2 implementation is needed for US3.
- Tests precede each story's production changes and run against frozen-baseline validation copies. Keep timing fixtures identical; never silently rebase the original production baseline to an intermediate cleanup.
- Readability walkthrough files are separate by story. Reconcile shared inventory/validation summaries after integration rather than overwrite each other's evidence.
- A technical checkpoint can be reviewed while human readability remains pending. Complete all unaffected technical work before requesting the actual maintainer assessment; waiting for feedback does not justify skipping validation.

### Parallel opportunities and examples

Parallelism here describes optional scheduling, not an instruction to spawn agents. Shared-source edits must settle before building; keep per-run outputs and baseline validation copies isolated. No timing capture overlaps another capture/build/check.

**US1**: After T008, T009 can update `tests/runtime_step_test.cpp`/`tests/benchmark_contract_test.cpp` while T010 updates `tests/transform_hierarchy_test.cpp`/`tests/runtime_work_test.cpp`. Join at T011; serialize GameManager edits T012–T014.

**US2**: After T008, T017 and T018 edit different test hosts. After T019, T020 (`src/rigidbody.cpp`), T021 (`src/physicsmanager.cpp`/`.h`) and T022 (`src/physicsbodyindexes.cpp`) can proceed as independent edits; join before T023 builds.

**US3**: After T008, T025 (`tests/runtime_foundation_test.cpp`/`CMakeLists.txt`) and T026 (`tests/runtime_recreation_test.cpp`) can prepare independent coverage. Join at T027, then serialize T028/T029 shared GameManager edits. Schedule around US1 if both stories are active.

**Shared work**: T001 and T002 write distinct setup artifacts. T006 can be prepared alongside T003–T005 against the fixed probe contract. After T032, T033–T036 use separate build/output directories and may run independently when host resources permit; they all finish before release timing.

## Requirement Coverage

| Requirement/outcome | Principal tasks |
| --- | --- |
| FR-001 bounded scope | T002, T012–T014, T020–T022, T028–T029, T032 |
| FR-002 / SC-002 concrete simplification | T002, T012–T013, T020–T021, T028–T029, T016/T024/T031, T040–T041 |
| FR-003 clear responsibilities | T002, T014, T021–T022, T016/T024/T031, T032 |
| FR-004 / SC-004 braces and readable spacing | All changed-code tasks, T032, T041 |
| FR-005 frame/mutation behavior | T009–T015, T033, T035–T037 |
| FR-006 sleep/wake behavior | T017–T023, T034–T037 |
| FR-007 support/contact integrity | T018–T019, T021–T023, T034–T037 |
| FR-008 / SC-003 compatibility/lifetime | T010–T015, T018–T019, T025–T030, T033–T037 |
| FR-009 performance first | T002–T008, T032, T034, T037–T039 |
| FR-010 concurrency | T019, T021–T023, T029–T030, T032, T034–T039 |
| FR-011 / SC-005 measured evidence | T001, T003–T008, T034–T039 |
| FR-012 / SC-001 human audit | T002, T016/T024/T031, T040–T041 |
| SC-006 fixed-peak lifetime/storage | T005, T008, T010/T018/T026, T030, T034–T037 |

## Implementation Strategy

### MVP first

Complete setup/foundation, then US1 through T016. Demonstrate one maintained rendered-frame path, dead-private-method removal, preserved lifecycle/transform semantics and readable functions. Validate this increment before proceeding; it is an MVP checkpoint, not completion of the full requested cleanup or permission to waive applicable performance evidence.

### Incremental delivery

Add US2, then US3, validating each against its independent criteria and preserving earlier results. Keep small, reviewable logical changes rather than mix unrelated rewrites into a cleanup commit. Do not automatically commit, publish, or change branches as part of this task list.

### Final acceptance

Complete the integrated safety/work/compatibility checks and matched cost comparison. Resolve or explicitly retain unverified limitations under the constitution. Prepare the final code-backed review before asking for the maintainer's readability finding; T041 remains incomplete until actual feedback. Documentation quantity, passing tests and deleted line counts cannot supply that human judgment.

## Maintainer update — 2026-10-26

The maintainer said the code "looks somewhat better" and will review it later in a separate prompt. Technical work is complete; T041 is deliberately deferred and unchecked, with no assumption that all six paths have been accepted. No further confirmation is requested in this implementation turn.
