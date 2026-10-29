# Tasks: Linear Rendering Culling

**Input**: Design documents from `specs/008-linear-render-culling/`
**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [rendering contract](contracts/rendering.md), [validation contract](contracts/validation-evidence.md), [quickstart.md](quickstart.md), and `.specify/memory/constitution.md`.
**Branch**: `branch-without-bvh`
**Tests**: Explicitly required by FR-011/FR-012 and SC-001–SC-005. Add independent geometry, actual-image/order, lifecycle, and capture-integrity coverage; do not substitute implementation-mirroring tests for these checks.
**Organization**: Setup → shared prerequisites → US1 (P1, rendering MVP) → US2 (P1, dynamic correctness) → US3 (P2, evidence) → convergence review. T001–T044 are complete, including the final provenance and evidence gate.

## Format: `[ID] [P?] [Story] Description`

- `[P]` identifies independent work in different files after the named prerequisite checkpoint. It never authorizes simultaneous edits to shared files or timing runs concurrent with builds/tests.
- `[US1]`, `[US2]`, and `[US3]` map to the specification's three user stories. Setup, foundation, and final review tasks have no story label.
- Paths are relative to the repository root unless explicitly absolute. Capture directories and baseline source/builds live under the Git-ignored benchmark artifacts directory; concise evidence reports live under this feature's `evidence/` directory.
- Existing source, assets, scene formats, dependencies, physics scheduling, and broad-phase implementation remain outside this rendering change. Do not reintroduce BVH infrastructure, dirty-transform notifications, caches, backend enums, or worker pools.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Preserve an honest pre-feature reference and record the environment before changing engine code. No branch switching, rollback, or unrelated cleanup is needed.

- [X] T001 Preserve an isolated source export of commit `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75` before engine edits and record its source/archive hashes, external location, toolchain/dependency versions, and current dirty-state inventory in `specs/008-linear-render-culling/evidence/baseline-provenance.md`; leave the user's branch and unrelated files intact. Later baseline captures may use this pinned source as permitted by `contracts/validation-evidence.md`.
- [X] T002 [P] Record available CPU topology/affinity, physical-core worker count, SDL renderer availability, output/VSync policy, build configurations, and fresh external validation/resource/timing directories in `specs/008-linear-render-culling/evidence/environment.md`; use `benchmark/README.md` methodology, avoid assuming the historical six-core affinity, and identify a software renderer for deterministic image checks.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Provide only the shared fixture and observation support required by all stories. Finish T001–T002 before this phase; finish this phase before story implementation.

- [X] T003 [P] Add baseline-compatible static scene construction in `tests/render_culling_fixture.h`: real deterministic textures, fixed camera/output, visible/offscreen/mixed/translucent sprites, inactive/empty variants for correctness, and logical slot/generation identities. Preserve the data-model constraint "raw object addresses are not cross-run identifiers." Keep fixture setup/upload outside measured work and collider-free; do not require the new settings field to compile against the pinned old runtime.
- [X] T004 [P] Add work-probe-only view/check/submission counters, pre-reserved ordered submission observation, and scoped pre-presentation readback support in `tests/runtime_work_probe.h`, `src/workprobe.h`, `src/camera.cpp`, and `src/sdlwindow.cpp`. Preserve "The observer only reads output; it does not mutate the runtime." Use RAII attachment/removal before runtime destruction; compile all observation code/storage/callback branches out when work probes are off. Keep original submission behavior unchanged and structure the observation support so it can also be applied to the pinned baseline.
- [X] T005 Build and check the shared fixture/observation support with the existing debug/work conventions in `CMakePresets.json` and `CMakeLists.txt`, recording results and the common-support diff in `specs/008-linear-render-culling/evidence/foundation.md`; verify ordinary builds contain no observation seam and guarded observation support compiles in work builds without changing submit-all behavior; actual frame observations are exercised by T007/T013. Do not claim culling correctness or performance yet; the complete common baseline harness is finalized in T026.

**Checkpoint**: Pinned baseline source is retained; deterministic static fixture and compile-time-only observations work without any culling implementation.

---

## Phase 3: User Story 1 — Skip sprites outside the view without changing the image (Priority: P1, MVP)

**Goal**: Deliver default-enabled linear filtering in the existing ordered window render path, with safe bounds and an opt-out.

**Independent Test**: Compare enabled/disabled images and actual submission traces for static visible/offscreen/partial/transparent/rotated/scaled scenes. Require identical output, no missing visible sprite or order inversion, and zero submitted sprites for the classifiable all-offscreen case.

### Tests for User Story 1

Write the assertions before the corresponding feature implementation. Demonstrate the missing-culling all-offscreen failure against the unchanged reference path; add minimal internal declarations as needed to build focused geometry tests, without claiming absent helpers have passed.

- [X] T006 [P] [US1] Add `tests/render_culling_geometry_test.cpp` with an independent double-precision transformed-corner oracle: strict disjointness, boundary contact, partial/oversized/elongated sprites, zero/quadrant/arbitrary/near-quadrant angles, fractional and adjacent-float edge cases, unequal camera scales, fixed-seed randomized valid inputs, extreme magnitudes, and invalid/degenerate fallback cases. Check conservative rejection safety and the documented padded bounds rather than duplicating the production algorithm.
- [X] T007 [P] [US1] Add static image/order and public-option tests in `tests/render_culling_test.cpp` using `tests/render_culling_fixture.h`: enabled/disabled modes, all-visible/all-offscreen/mixed/inactive/empty membership, translucent overlap, source rectangles/flips, scale/rotation, missing textures, and uncertain-input submission. Read pixels before presentation, normalize format, compare exact pixels and ordered slot identities, and report work-probe-only portions explicitly as unavailable when instrumentation is absent.
- [X] T008 [US1] Register `platformator_render_culling_geometry_test` and `platformator_render_culling_test` in `CMakeLists.txt` using `cmake/PlatformatorTests.cmake`, with appropriate internal includes, rendering labels, and existing dummy/software test environment. Keep geometry checks runnable without work probes and make the work-probe validation requirement visible rather than silently passing skipped image/trace coverage.

### Implementation for User Story 1

- [X] T009 [P] [US1] Append the field "`bool enableRenderCulling = true`" to `include/platformator/windowsettings.h` and map the idempotent no-value `--no-render-culling` flag to false in `src/runtimeoptions.cpp`; preserve existing aggregate defaults and unknown-argument behavior. Honor "It is not serialized into scenes and has no runtime mutation API." Use this single setting without a second mode state or changes to scene data.
- [X] T010 [P] [US1] Implement the internal value view and allocation-free conservative classifier in `src/spriterendergeometry.h`. Honor "Store values rather than references." and "A reject is allowed only when these bounds are strictly disjoint from `[0, outputWidth] × [0, outputHeight]`." Use the prepared destination rectangle, a zero-angle fast path, SDL-compatible degree-derived float corner rotation, outward rounding, and the tested one-output-pixel guard; retain unclassifiable/non-finite/overflowing/negative/degenerate inputs. Keep the helper independent of engine-object mutation and draw calls.
- [X] T011 [US1] Add private view preparation and `renderWithView` shared drawing in `include/platformator/camera.h` and `src/camera.cpp`, with narrow `SDLWindow` friendship and the internal view forward declaration. Preserve the public four-argument `Camera::render` signature and direct submit-all semantics. Honor "Prepare the same rectangle once for classification and drawing." and "Culling requires positive finite mapping/output and the default full-output SDL state." Snapshot current camera values/scales once, preserve original drawing arithmetic and arguments, and keep the direct-call view unculled.
- [X] T012 [US1] Integrate the prepared view and inline rejection into the existing ordered sprite traversal in `src/sdlwindow.cpp`, after renderer/camera early exits. Preserve active filtering, relative order, overlays, clear/present, and membership rules. Check target/viewport/scale/logical presentation/output compatibility once per enabled frame and fall back to submit-all on custom mapping/query failure without altering SDL state; disabled mode skips eligibility queries. Add no visible list, sorting, persistent state, transform hooks, or headless work.
- [X] T013 [US1] Run US1 geometry/static image/order and parser checks in debug/work builds and record fixture identities, passing image hashes/representative images, submitted counts/order, and guard/fallback findings in `specs/008-linear-render-culling/evidence/static-rendering.md`; resolve missing-pixel/order failures in the rendering files before advancing. Confirm all-offscreen rejection and all-visible retention use the production submission path.

**Checkpoint**: US1 is a usable, independently tested rendering MVP; it does not yet establish the full dynamic or performance acceptance criteria.

---

## Phase 4: User Story 2 — Keep moving and changing scenes visually correct (Priority: P1)

**Goal**: Prove that reading current values each render handles continuous motion, camera changes, and existing lifecycle transitions without stale state or added invalidation machinery.

**Independent Test**: Compare corresponding enabled/disabled frames over repeated continuous crossings, then mutate camera/output/geometry/membership. Require zero frames of stale visibility, identical output, valid retained order, and zero headless visibility work.

### Tests and fixture implementation for User Story 2

- [X] T014 [P] [US2] Extend `tests/render_culling_fixture.h` with the contract's deterministic `crossing`, `camera-pan`, `churn`, and mixed `rotated` trajectories plus `sparse`/`clustered` layouts. Make continuous positions functions of seed, slot, and fixed step; at reference dt use 240-step periodic crossings with varied phases and both horizontal/vertical entries/exits, with no teleport/wrap updates. Keep churn population fixed, replace 1% of slots per step (minimum one), and track generations. Keep all geometry/updates baseline-compatible and identical between compared modes.
- [X] T015 [P] [US2] Extend `tests/render_culling_test.cpp` with focused lifecycle/drawing-input regressions for parent movement, size/scale/rotation/source/texture changes, activation, insertion, swap-removal order, component replacement, destruction, scene clear/load, and repeated runtime creation. Compare the current authoritative order and same-frame output; avoid retaining stale Sprite pointers in observations or adding invalidation hooks to production to satisfy tests.
- [X] T016 [US2] Add repeated continuous-crossing and camera-pan comparisons in `tests/render_culling_test.cpp` using T014's trajectories; cover at least the contract's 600-step measured interval after matching warmup, assert actual entry and exit events, and compare matching frame pixels/traces. Add camera dimension/replacement/output resize and repeated unchanged-frame cases, checking current camera/world/output values with no stale exclusions.
- [X] T017 [US2] Add custom renderer target/viewport/scale/logical presentation, query/invalid-mapping fallback, restoration-next-frame, and direct public Camera compatibility checks in `tests/render_culling_test.cpp`; also cover no-camera/no-renderer and classifier zero-output paths. Match existing rendering/error handling rather than add support for inputs the engine already rejects; verify default mapping regains filtering without notifications.
- [X] T018 [P] [US2] Add headless culling-work assertions and debug overlay/pause-path compatibility coverage in `tests/runtime_work_test.cpp` and `tests/runtime_step_test.cpp`, reusing T004 counters. Require zero view/eligibility/check/submission work during headless simulation and unchanged simulation results; keep debug-tools-on/off expectations explicit and avoid changing physics or synchronization code.
- [X] T019 [US2] Run the combined dynamic/lifecycle/rendering checks in debug, work-probe, and ASan/UBSan configurations from `quickstart.md`, plus relevant existing hierarchy/runtime lifecycle tests, and record results, frame/crossing evidence, and any supported sanitizer limitations in `specs/008-linear-render-culling/evidence/dynamic-rendering.md`. Fix only evidenced rendering defects in `src/camera.cpp`, `src/spriterendergeometry.h`, or `src/sdlwindow.cpp`, then repeat affected checks.

**Checkpoint**: US1 and US2 prove static and dynamic correctness on the tested renderer. Performance claims still require US3.

---

## Phase 5: User Story 3 — Verify that the focused optimization pays for itself (Priority: P2)

**Goal**: Compare pinned pre-feature submit-all, candidate disabled, and candidate enabled using real rendering and matched workloads, while exposing added all-visible/headless cost.

**Independent Test**: Complete the bounded matrix in `contracts/validation-evidence.md` with matched correctness/resource proof and independent timing captures; show whole-render benefit distinguishable from variation in a representative mostly-offscreen case, and report/investigate unfavorable results.

### Tests for User Story 3

- [X] T020 [P] [US3] Add meaningful rejection/comparison tests in `benchmark/tests/test_linear_render.py` for the planned driver: corrupt/incomplete raw outputs, wrong mode/profile/count/trajectory/seed/dt/warmup/workers, mismatched harness/renderer/source, missing or stale validation/resource proof, forbidden timing instrumentation, insufficient repetitions, sample-count/quit failures, reused paths, and write errors. Reuse schema-v3 test fixtures/parser behavior; do not invent a replacement numeric schema or mirror aggregation internals.
- [X] T021 [P] [US3] Add `--no-render-culling` parsing and additive `linear_render_culling` requested-mode/effective-policy metadata to the existing runner in `src/benchmark_main.cpp`, with focused parser/metadata regressions in `tests/benchmark_contract_test.cpp` or the existing benchmark CLI test registration in `CMakeLists.txt`. Keep the eight numeric scopes, eleven counters, generic sweep scenarios, and headless workload semantics unchanged; the stock pinned runner continues to work without the new flag.

### Implementation and evidence for User Story 3

- [X] T022 [US3] Implement the baseline-compatible `benchmark/linear_render_probe.cpp` and register its benchmark-only target in `CMakeLists.txt`, reusing T014's shared fixture and `BenchmarkRecorder` schema-v3 summaries/raw/manifest. Implement the documented profile/population/workers/seed/dt/warmup/measurement/output/repetition options and strict boundary validation, default rendered mode plus `--headless`, and a templated settings-field capability check: old runtime accepts reference only, candidate modes share one binary. Record actual renderer/output/VSync/mapping, source and harness hashes, resolved parameters, and no accepted incomplete capture; include probe/shared fixture contents in fingerprint coverage and avoid per-sprite telemetry in timing builds.
- [X] T023 [US3] Implement `--validate-rendering`, `--evidence-output`, and `--validation-proof` in `benchmark/linear_render_probe.cpp`, using T004 observations and fixture slot/generation identities. Require work probes for readback/traces, retain matching-frame hashes/representative images and mismatch images, and record submission/crossing counts and evidence checksums. Validation is `validation_mode=per-step`, `timing_eligible=false`; timing proof lookup/checks occur outside measured scopes and must match the complete fixture identity.
- [X] T024 [US3] Implement mutually exclusive work-probe-only `--validate-resources` in `benchmark/linear_render_probe.cpp` with scoped validation-only allocation observation, warmed repeated-frame checks, and retained-memory observations for population growth/churn. Keep traces/readback disabled during these captures; record coverage limits for C++ versus C/driver allocations and distinguish fixture/SDL costs from culling-owned work. Resource output uses `validation_mode=per-step`, `timing_eligible=false`, and resource interception must compile out of the timing probe.
- [X] T025 [US3] Implement `benchmark/capture_linear_render.py` against T020 tests, reusing `benchmark/sweep_scaling.py::parse_benchmark_csv`. Accept explicit baseline/candidate probe and stock-runner paths, validation/resource roots, workers, dt/seed/frame counts/repetitions, and a fresh output directory. Execute the contract's non-Cartesian matrix and engine controls in shuffled fresh processes, verify case/proof/binary identities and final-source consistency, retain logs/raw outputs/checksums, and report complete render separately from complete_step with run-mean spread, p95/p99/max and validated submission fractions; never aggregate correctness/resource runs as timing.
- [X] T026 [US3] Build the pinned baseline and candidate timing/validation binaries in isolated directories using the finalized identical fixture/probe harness from T022–T024; apply only common fixture/probe/build/provenance/work-only observation support to the pinned source, and record exact patches, file/binary hashes, compiler flags, dependency versions, and actual fingerprints in `specs/008-linear-render-culling/evidence/baseline-provenance.md`. Baseline must contain no camera preparation, settings field, state queries, or culling; do not reuse an altered binary with a stale embedded source fingerprint. Complete all builds/tests before timing.
- [X] T027 [US3] Capture and compare full required per-case/per-worker pre-feature/disabled/linear image/order/headless validation using `benchmark/linear_render_probe.cpp` and `benchmark/capture_linear_render.py`; record matched proof locations/checksums and rejection-safety/crossing results in `specs/008-linear-render-culling/evidence/matrix-validation.md`. Require zero missing visible sprites, duplicates, order inversions, or stale frames; never accept a timing case lacking corresponding final-source correctness proof. Keep raw capture data outside tracked source.
- [X] T028 [US3] Capture stable-population and churn resource observations for matched modes and workload sizes using `benchmark/linear_render_probe.cpp`; retain external outputs and summarize warmed allocations, O(1) additional culling storage versus N, headless isolation, and allocation-tool coverage limits in `specs/008-linear-render-culling/evidence/resources.md`. Do not count image/trace/fixture/driver allocation as culling cost or claim unobserved allocation categories are measured.
- [X] T029 [US3] Run the complete bounded comparison through `benchmark/capture_linear_render.py` with matched T027/T028 evidence, five independent fresh-process repetitions per case/mode, seed zero, 120 warmup/600 measured steps at `0.008333333333333333`, recorded affinity and actual renderer, one worker and selected physical-core-worker cases. Include contract engine rendered/headless controls using the explicit stock-runner paths; retain commands, logs, raw samples, manifests, binaries/source/harness identities and aggregate output externally, with an index at `specs/008-linear-render-culling/evidence/capture-index.md`. Do not overlap timing with builds, tests, resource probes, or other timing captures.
- [X] T030 [US3] Investigate any regression beyond observed variation or missing mostly-offscreen benefit using the actual complete-render/complete_step results and profile `src/camera.cpp`, `src/spriterendergeometry.h`, and `src/sdlwindow.cpp` as warranted; record causes, uncertainty, and any narrow corrective change in `specs/008-linear-render-culling/evidence/performance-investigation.md`. Do not add caching/parallel classification/generalized machinery without concrete evidence. After a source/harness change, rebuild and regenerate affected correctness/resource proof plus the required final-source timing set; never mix fingerprints or omit unfavorable cases. If no acceptance benefit can be established, leave performance acceptance incomplete and record why.
- [X] T031 [US3] Write `specs/008-linear-render-culling/evidence/RESULTS.md` from final-source captures: compare enabled versus disabled, disabled versus pre-feature, and enabled versus pre-feature; show whole-render and separate simulation-step means/tails/spread, validated submitted fractions, allocations/memory, visible/rotated/tiny/headless costs, and worker/renderer limits. Link external raw data/checksums and common baseline patches; accept SC-005 only when the representative benefit exceeds observed variation and regressions have been investigated. Do not infer hardware-renderer performance from software-only or CPU submission measurements.

**Checkpoint**: All correctness, resource, and performance acceptance criteria have corresponding reproducible evidence, or a remaining failure is explicitly unfinished. A finished harness alone is not a completed performance story.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Finish consumer compatibility, documentation, and final correctness/performance review after all story gates pass.

- [X] T032 [P] Document default-enabled window culling, `--no-render-culling`, unchanged direct Camera semantics, custom-mapping fallback, and consumer rebuild implications in `docs/CPP_API_MIGRATION.md` and `benchmark/README.md`; exercise the appended setting in `tests/consumer/main.cpp` without changing scene formats or public method signatures. Keep performance statements tied to T031 evidence and explain the O(N)/O(1), no-cache tradeoff plainly.
- [X] T033 [P] Reconcile `specs/008-linear-render-culling/quickstart.md`, `plan.md`, `data-model.md`, and `contracts/` with final names/commands/numerical guard/provenance and actual renderer/tool limitations; add an FR/SC/acceptance-scenario-to-evidence map in `specs/008-linear-render-culling/evidence/coverage.md`. Replace planned-only descriptions only where implemented and verified; leave unmet obligations visible.
- [X] T034 Run the required combined rendering/geometry/dynamic/work/sanitizer checks, existing benchmark C++/Python contracts, debug-tools-on/off builds, and public-header/consumer/package checks affected by `include/platformator/windowsettings.h` and `include/platformator/camera.h`; follow final `quickstart.md` and record exact results/limitations in `specs/008-linear-render-culling/evidence/final-validation.md`. Confirm ordinary/timing binaries contain no work-only observers or allocation interception; repeat broader checks only for new changes/failures, not to replace the final-source evidence gates.
- [X] T035 Review final changes in `include/platformator/windowsettings.h`, `include/platformator/camera.h`, `src/camera.cpp`, `src/spriterendergeometry.h`, `src/sdlwindow.cpp`, `src/runtimeoptions.cpp`, and benchmark/test integrations against all seven constitution principles and record signoff in `specs/008-linear-render-culling/evidence/final-review.md`: ordered O(N) work, O(1) extra runtime state, no warmed culling allocations/headless work, correct lifetimes, sequential rationale, no physics/dirty-hook/dependency changes, complete unfavorable-results reporting, and final artifact consistency. Any known retained inefficiency needs the constitution's local `EFFICIENCY:` rationale and concrete follow-up; do not mark the feature complete with unmet SC outcomes.

---

## Dependencies & Execution Order

### Phase and story graph

```text
Setup: T001 + T002
        |
Foundation: (T003 || T004) -> T005
        |
US1: (T006 || T007) -> T008 -> (T009 || T010) -> T011 -> T012 -> T013
        |
US2: (T014 || T015 || T018) -> T016 -> T017 -> T019
        |
US3: (T020 || T021) -> T022 -> T023 -> T024 -> T025 -> T026
                                                    -> T027 -> T028 -> T029 -> T030 -> T031
        |
Polish: (T032 || T033) -> T034 -> T035
```

- T001 must preserve the pinned runtime before any engine edit, including observation seams. T002 is independent inventory work.
- T003 and T004 edit separate files; both precede T005. The fixture is baseline-compatible throughout its later extensions.
- T006/T007 can be authored independently after foundation. T008 registers both tests; T009 and T010 then edit disjoint configuration/helper files. T011 depends on T009/T010; T012 depends on T011; T013 closes US1.
- US2 extends US1. T014 modifies the shared fixture; T015 modifies lifecycle integration assertions; T018 modifies separate existing work/step tests. T016 uses T014 and T015, T017 follows it in the same test file, and T019 waits for T018 and all dynamic assertions.
- US3 depends on static/dynamic correctness. T020 and T021 are independent contract/CLI work. Later probe changes share `benchmark/linear_render_probe.cpp` and must be sequential. T025 uses the final T022–T024 interfaces. T026 freezes/builds the complete common harness; T027/T028 precede T029, and T030/T031 assess the complete final-source set.
- Baseline timing capture is intentionally after the shared harness exists, from T001's isolated pinned source, as explicitly allowed by the evidence contract. It is not a comparison against a previous BVH binary or a candidate with filtering merely disabled.
- Final documentation/consumer tasks can proceed together; verification and final signoff follow. If T030 or final checks change source, invalidate stale evidence and rebuild/recapture before closing the affected gates.

### Parallel examples

These are scheduling examples, not a request to launch agents during task generation.

| Story | After checkpoint | Independent tasks | Shared-file work that stays sequential |
|-------|------------------|-------------------|---------------------------------------|
| US1 | T005 | T006 geometry tests and T007 image/order tests | T008 registration, then T011/T012 production integrations |
| US1 | T008 | T009 setting/parser and T010 stateless helper | T011 consumes both interfaces |
| US2 | T013 | T014 motion fixture, T015 lifecycle assertions, T018 work/step assertions | T016/T017 extend the same integration test file |
| US3 | T019 | T020 Python capture rejection tests and T021 stock runner adapter | T022/T023/T024 share probe code; T026 build freeze precedes all captures |

## Implementation Strategy

### MVP first

Complete setup/foundation and US1 through T013: safe static linear culling, one prepared camera view, existing order, default setting/opt-out, and image/trace proof. This is the smallest usable rendering increment. It is a checkpoint, not permission to stop the fully authorized implementation or declare the complete performance feature done.

### Incremental delivery

Add US2 to demonstrate continuously moving sprites/camera and lifecycle safety without cross-system invalidation. Then complete US3 using the pinned baseline and final common harness; prove whole-render benefit and expose all-visible/headless overhead. Finish documentation/public-consumer verification and final review. Do not restore archived experiment code to simplify benchmarking.

### Scope and acceptance discipline

Static and dynamic tests need independent expected geometry and actual rendered output. Instrumented validation/resource data never becomes timing evidence. Runtime configuration stays one boolean; no new dependency or worker infrastructure is needed. Keep measurement tooling feature-specific and reuse the existing recorder/parser. Correctness, final-source provenance, and SC-005 benefit remain acceptance gates even if the code itself is small.

## Coverage and Task Summary

| Obligation | Tasks |
|------------|-------|
| FR-001–FR-003: ordered conservative sprite geometry | T006–T007, T010–T013 |
| FR-004: same-frame dynamic correctness | T014–T017, T019, T027 |
| FR-005–FR-006: compatibility and conservative fallbacks | T007, T011–T013, T015–T019, T032, T034 |
| FR-007: default and explicit opt-out | T007, T009, T021–T022, T032, T034 |
| FR-008–FR-010: isolation, KISS, complexity, allocation | T004–T005, T010–T012, T018, T024, T028–T030, T035 |
| FR-011: actual images/order and independent oracle | T003–T008, T013–T019, T023, T027 |
| FR-012: honest baseline and representative evidence | T001–T002, T014, T020–T031 |
| SC-001–SC-003: image/order/visibility and zero stale frames | T013, T019, T027 |
| SC-004: allocations and headless isolation | T018–T019, T024, T028–T029 |
| SC-005: whole-render benefit and regression assessment | T025–T031 |
| Public compatibility, reproducibility, final constitution review | T032–T035 |

35 tasks total: 2 setup, 3 foundational, 8 US1, 6 US2, 12 US3, and 4 final-review tasks. 14 tasks are marked `[P]`. All checkbox items are implementation or verification work; no task is complete merely because this list exists.


## Phase 7: Convergence

**Assessment date**: 2026-10-29. All T001–T035 were assessed independently of their checked state, with emphasis on correctness, code quality, and evidence integrity. Checked 12 functional requirements, 5 success criteria, 10 acceptance scenarios, 7 edge-case groups, 16 grouped plan obligations, and all 7 constitution principles. No application/design files were changed during this assessment.

**Verification performed**: Existing geometry executable passed 5,018 samples; six current Python in-memory contract checks passed. Read-only audit validated the retained 51 rendered proofs, 51 resource proofs, 255 focused timings, and 60 stock engine controls for internal consistency. Independent in-memory rejection probes demonstrated acceptance of 40 invalid stock-control records and a negative linear submission count. Current CMake fingerprint reconstruction does not match the claimed final-source capture fingerprint. These checks do not establish a clean convergence result or substitute for the corrective checks below.

**Execution order**: T036–T037 and T039–T043 were completed before the T038 final rebuild/provenance/evidence gate. T041 was reassessed against regenerated run-level comparisons. Historical captures and unfavorable measurements remain archived and are labeled superseded where their source inputs cannot be independently reconstructed.

**Final closeout**: T036–T043 are complete. The final manifest-backed baseline/candidate rebuilds passed their independent per-input fingerprint audit; 51 render proofs, 51 resource proofs, 255 focused timing runs, and 60 stock controls were regenerated and validated. The all-visible positive cost at six workers is documented as an accepted tradeoff alongside sparse gains. See [final-validation.md](evidence/final-validation.md), [baseline-provenance.md](evidence/baseline-provenance.md), and [RESULTS.md](evidence/RESULTS.md).

- [X] T036 CRITICAL — Fix F1's texture lifetime violation in `tests/render_culling_fixture.h` per Constitution VII, T003, T015, and FR-011 (contradicts). The borrowed `textures_` cache at lines 365/415–417 can outlive the last Sprite reference: idle `replaceSprite()` destroys the prior object before requesting its texture again, and remove-last-user followed by a later add also reuses a freed wrapper. Remove the redundant borrowed cache and rely on the runtime's authoritative cache, or use the existing asset ownership mechanism with explicit release before scene clear/runtime teardown. Add ASan/UBSan regressions in `tests/render_culling_test.cpp` for single-user idle replacement, remove-last-user then render/re-add, texture switches, and scene reload. Keep the fix fixture-only, baseline-compatible, and free of new ownership infrastructure; update corresponding lifecycle evidence only after those cases pass.

- [X] T037 CRITICAL — Make F2's validation allocation observer safe under concurrent allocation and exceptions in `benchmark/linear_render_probe.cpp` per Constitution VII, T024, and SC-004 (contradicts). The process-global plain `enabled`, `calls`, and `bytes` at lines 40–51 can race with allocation on other runtime threads, and lines 744–748 toggle observation without scoped restoration. Use a simple scoped observer with a documented ownership model: synchronized process-wide collection, or thread-local collection explicitly labeled as main-thread-only rather than global coverage. Ensure all observer operations are allocation-free and compile out of timing/production builds. Add focused concurrent-allocation and exceptional-exit checks, verify supported race/sanitizer behavior, and regenerate matched resource proofs after updating the common baseline harness. Document actual aligned/C/SDL/driver and worker-thread coverage in `specs/008-linear-render-culling/evidence/resources.md`.

- [X] T038 CRITICAL — Reconcile F3's final-source provenance and rebuild the acceptance evidence after T036–T037 and T039–T043 per Constitution VI, FR-012, T026–T031, T034, and SC-005 (partial). Read-only reproduction of the current `CMakeLists.txt` fingerprint algorithm over its cached/current input set yields `a1a3fc3af20731f4fdb0fe4f9dac26638d4749c2e3afabafa66a32efd7a11999`, whereas generated headers, final-review claims, and retained captures use `02283cad1140781bf977b35868abc21cc7127abf7bbb11ec0d06846578e3f256`. Identify and explain the discrepancy without rewriting old evidence or stamping old binaries with a new hash. Retain a per-input hash manifest, rebuild pinned baseline/candidate and separate validation/timing binaries with identical finalized common harness content, independently verify embedded fingerprints against current inputs, then regenerate the required matched correctness/resource/timing/control sets and interpret unfavorable cases. Update `specs/008-linear-render-culling/evidence/baseline-provenance.md`, `capture-index.md`, `coverage.md`, `RESULTS.md`, `final-validation.md`, and `final-review.md` only from the verified new artifacts. Do not keep a final-source acceptance claim if this gate remains unresolved.

- [X] T039 HIGH — Complete F4's actual dynamic rendering regression coverage in `tests/render_culling_test.cpp` per FR-004, FR-011, US2/AC1–AC4, T016–T017, and SC-003 (partial). `checkTrajectoryContracts()` at line 518 only evaluates trajectory arithmetic; it does not render its claimed 720-frame enabled/reference sequences. Add real corresponding-frame image/order comparisons over matching warmup plus at least 600 continuously moving frames, and exercise sprite/parent geometry changes, camera dimension/replacement, activation/removal/replacement, unchanged repeated frames, and output resize after an initial rendered frame. Adapt readback storage/expected dimensions to actual resized output instead of hardcoded 320×240; include no-camera behavior and retain existing custom-mapping/headless coverage. Reuse the shared fixture and production render path, keep the independent oracle, and explicitly distinguish these regression checks from the already retained focused-probe motion matrix. Correct the overstated claims in `specs/008-linear-render-culling/evidence/dynamic-rendering.md`, `coverage.md`, and `final-validation.md` to name checks actually executed.

- [X] T040 HIGH — Close F5's capture-validation holes in `benchmark/capture_linear_render.py` and `benchmark/tests/test_linear_render.py` per FR-012, T020, T025, and SC-005 (partial). `run_engine_controls()` at lines 417–453 verifies build flags and per-mode fingerprint consistency but accepts wrong scenario/workers/seed/dt/warmup/measurement/repetition/render policy, validation-mode data, and identical or unrelated baseline/candidate sources. Validate the full requested control identity, numeric/raw completeness, timing eligibility, expected render count, candidate enabled policy, and source/build relationship to the already checked focused binaries; verify actual renderer identity or explicitly retain its unverified status. Record required control tail values and independently checked membership/visibility evidence where applicable. Resource validators must reject negative/fractional work counts, impossible submissions, and malformed identities instead of accepting a negative linear submission count. Add targeted rejection tests and re-audit control/resource artifacts without silently substituting zeros or changing schema-v3 scope semantics.

- [X] T041 HIGH — Correct F6's performance inference and investigate the all-visible overhead in `specs/008-linear-render-culling/evidence/performance-investigation.md`, `RESULTS.md`, and `final-review.md` per FR-012, SC-005, T030–T031, and plan: regression assessment (partial). Overlapping observed min/max ranges do not establish absence of a repeatable mean regression. The retained ten-run diagnostic has linear render increases of about 0.00455 ms (2.15%) versus pre-feature and 0.00554 ms (2.62%) versus disabled; a Welch comparison of independent process means gives approximate 95% intervals excluding zero, subject to run-order/environment assumptions. Analyze independent-run uncertainty and confounders using the recorded ordering rather than treating frame samples as independent; report the small positive cost honestly alongside sparse gains. Profile the enabled all-visible path as needed, including repeated already-established view validation and padded-bounds arithmetic, then either make a narrowly evidenced improvement or document the accepted tradeoff. Do not require eliminating every cost, invent a percentage threshold, or dismiss a measured effect merely because ranges overlap; apply the same interpretation to T038's final regenerated data.

- [X] T042 MEDIUM — Repair F7's mismatch-image recovery in `benchmark/capture_linear_render.py::capture_mismatch_images` per T023, T025, and plan: retained validation evidence (partial). It currently invokes `args.baseline_probe`/`args.candidate_probe`, which are required to be work-probes-off timing binaries, with `--validate-rendering`, which those binaries reject. Supply explicitly identified validation-capable binaries or another bounded recovery mechanism, verify source/harness/fixture compatibility, retain the actual failing-frame images, and preserve the original mismatch diagnosis if recovery fails. Add a targeted failure-path test in `benchmark/tests/test_linear_render.py` and update `specs/008-linear-render-culling/quickstart.md` and `contracts/validation-evidence.md` to match the working interface; do not enable readback instrumentation in timing binaries.

- [X] T043 LOW — Remove F8's unused/redundant harness scaffolding per T035 and plan: focused KISS implementation in `benchmark/linear_render_probe.cpp`, `benchmark/capture_linear_render.py`, and `CMakeLists.txt` (unrequested). Remove the unused `setCulling` template, no-op rotated/pre-feature branch, ignored `step` lambda parameters, redundant resource-support guard and aliases where unnecessary, unused `_visibility_fraction`/imports, and duplicate capture-driver fingerprint input. Replace `new std::vector` plus custom deletion cleanup with a scoped local vector whose address is borrowed by the observation. Keep the independent oracle and required evidence checks; do not expand this into a framework or unrelated parser rewrite. Verify the complete input list is unique and baseline-compatible, then let T038 rebuild and recapture for the resulting actual source/harness identities.


## Phase 8: Convergence

**Assessment date**: 2026-10-29. Assessed all T001–T044 and the resulting corrective behavior, plus 12 functional requirements, 5 success criteria, 10 acceptance scenarios, 7 edge-case groups, 16 grouped plan obligations, and all 7 constitution principles. The earlier F1–F8 code/report findings are addressed. No new production-code correctness defect was found in this pass.

**Verification**: All 132 candidate and 131 baseline source-manifest inputs were rehashed, both validation/timing binary identities were checked, and the pinned baseline diff contains only the nine declared common-support files. Baseline CTest passed 29/29 (package-smoke excluded); candidate CTest passed 34/34 including package smoke. The persistent bundle's 43,509-entry checksum inventory passed, as did its final `verify_bundle.sh` run: 51 rendering proofs, 51 resource proofs, 255 focused timing processes, 60 engine controls, all numeric raw samples, proof/source/harness/binary identities, and the relocation views. The capture report/raw-index hashes are recorded in [capture-index.md](evidence/capture-index.md). Branch `branch-without-bvh` and HEAD `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75` remain unchanged; T044 made no engine implementation edits.

**Evidence history**: The previously cited `/tmp` capture roots were absent when this task began, including the reported final captures. Their pre-T044 Markdown summaries and claimed hashes are preserved as explicitly superseded historical snapshots. Because the raw evidence could not be recovered, T044 regenerated the bounded run in `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`; this new run is independently verified and is the current evidence. It reports sparse gains, positive but uncertain all-visible mean costs, headless behavior, tails, stock controls, and allocation/renderer limits without treating overlapping ranges as proof of no regression.

- [X] T044 CRITICAL — Resolve F9's unavailable measurement evidence per Constitution VI, FR-012, T038, T027–T031, and SC-005. The prior final and earlier `/tmp` raw roots were absent, so their summaries are retained as superseded snapshots and the required matrix was regenerated into the Git-ignored `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/` bundle. The bundle retains the pinned pre-feature commit plus only common fixture/benchmark/build/observation support, candidate snapshot, separate work-probe and uninstrumented Release builds, source-input manifests, binary identities, commands/logs, 51 matching image/order proofs, 51 resource proofs, and 255 focused plus 60 control runs with numeric per-step samples. `scripts/verify_bundle.sh` passed the 43,509-file checksum inventory, independent source/build/proof/timing audits, and relocation checks. The relocatable archive index and reproduction/verification workflow are documented in `evidence/capture-index.md` and `quickstart.md`; `baseline-provenance.md`, `final-validation.md`, `coverage.md`, and `RESULTS.md` contain current results. T044 introduced no culling/runtime implementation changes. The accepted performance evidence records large sparse gains, positive all-visible estimates with intervals including zero, stock/headless controls, tails, and resource measurement limits.
