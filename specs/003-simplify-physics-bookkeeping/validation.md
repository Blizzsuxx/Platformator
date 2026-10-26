# Validation: Simplify Physics Bookkeeping

The implementation removes three independently maintained flags, clarifies body/contact participation and isolates geometry with explicit inputs. The implementation and measured comparisons are available. **Performance acceptance (T038 / FR-011 / SC-005) remains unverified**, and **maintainer readability acceptance (T043/T045 / SC-001) remains pending**; see the [eight-path walkthrough](evidence/human-review.md). Test passes and prepared documentation do not certify human comprehension.

## Current runtime amendment — 2026-10-26

The maintainer explicitly removed the deterministic-solving requirement. Solver-pair sorting, comparator/helper/call, dirty flag and sort-time index repair are now deleted. The solver consumes append/swap-remove membership order; canonical event dispatch is unchanged. See [current scoped validation](evidence/solver-order-removal.md) and the [subsequent sorted/unsorted performance comparison](evidence/solver-order-performance.md). The latter retains 70 primary timing processes, 20 targeted follow-ups and four per-step-oracle checks; benefits are workload-dependent and the small one-worker downside is reported. The original 36-group equivalence captures, source fingerprint, sanitizer results and timings below describe the prior sorted implementation; they are retained as historical evidence and do not certify the amended runtime or a speedup. Exact solved-trajectory agreement across workers is no longer required. Readability findings and the earlier broad-phase regression disposition remain unverified; removing a sort from zero-solver-contact controls cannot resolve that issue.

## What changed and why

- Pair support credit now has two nullable exact recipient identities, replacing two body pointers plus two support flags. One reconciliation operation handles transfer and release. Per-body net deltas remain: losing and gaining support within the same boundary must not cause an interim wake, and old recipients survive until the delta flush and retirement barrier.
- Narrow-phase queued status derives from its index sentinel. Enqueue, cancellation and worker consumption no longer coordinate an independent boolean. O(1) deduplication/swap repair and fixed-batch pointer lifetime are preserved.
- One uncached live-contact predicate and one solver-participation derivation replace repeated decisions. All-contact, solver, adjacency/witness, body participation and sleep indexes retain their distinct physical/oracle consumers.
- Body collections/functions name awake dynamic, moving kinematic, sleep candidates, pending participation and reconciliation. The shared island reset has one private-header definition so Release integration can inline it. An initially out-of-line definition was corrected after measurement identified an avoidable per-awake-body call.
- Geometry has one internal pair/gravity entry. Exact shape, box clipping and unchanged fallback midpoint logic are implementation-local. Proven-unreachable circle branches in box-only clipping were removed; every reachable geometry/fallback operation compares to baseline after explicit interface normalization. No new class hierarchy, result allocation, public accessor/friend or recurring global scan was introduced.

Read [state inventory](evidence/state-inventory.md), [before routes](evidence/audit-before.md), [final routes](evidence/audit-after.md), [geometry comparison](evidence/geometry-comparison.json) and [reset investigation](evidence/reset-helper-investigation.md). Source changes extend beyond the historical commit where the actual dependency required it, including Collision's private fallback implementation; unrelated systems were not rewritten.

## Actual checks on the final candidate

| Check | Result and scope |
| --- | --- |
| Full Debug build / normal CTest | 26 registered, 24 passed, two expected instrumentation-disabled skips. Includes installed/relocated SDK consumers and Mario package smoke; repeated after the helper correction. |
| Production and independent public headers | Successful final rebuild, including runner, Mario and test/public-header targets. |
| Benchmark producer/runner/readers | 3/3 benchmark CTest checks pass; separate Python suite has 10 passing cases. Schema 3, eight scopes and eleven counters remain unchanged. |
| New characterization before refactoring | Queue/support tests pass against both original runtime trees; body/geometry characterization also passes before cleanup. |
| Changed physics contracts | Bookkeeping/support/contact/geometry/regression hosts pass. Queue fixture covers 131 candidates, three cancellations, 128 changed pairs, duplicates/consumption/requeue/empty batches under one/six workers. |
| Support lifetime | Net-zero transfer does not wake; final support loss does. Real destruction callback observes old recipient count zero and replacement count one before allocation freeing. Existing 128-support, reference/filter/trigger/multi-cell cases remain correct. |
| Work and retention | Baseline/final candidate selections pass 4/4 plus direct six-worker work execution. 100 warmed unchanged updates have zero C++ new/new[] allocations and zero eligible physics/event/adjacency work. Five populated fixed-peak churn/replacement/cancellation cycles restore membership/support and stable capacities. |
| Physical/oracle captures | All 36 candidate groups pass; all 18 worker comparisons per stage and all 36 cross-stage comparisons pass existing exact identities/support/sleep/features and numerical tolerances. Repeated for final candidate after correction. |
| Leak-enabled ASan/UBSan, changed physics hosts | 5/5 passed, including new before-free, net-transfer, body and worker paths. |
| Full memory-access/UB check | 25 non-package registrations, 23 passed, two instrumentation skips, with leak detection explicitly disabled for this separate check. |
| Full leak-enabled check | 9 passed, 14 failed, two skips; 8,800 bytes / 176 allocations on FluidSynth initialization paths. **Not a clean full leak check.** |
| TSan physics/mutation/lifecycle | 13 passed, two failed, one work skip. Changed physics/worker tests have no data-race reports. Two desktop SDL/libdbus lock-order warnings remain unresolved. |

Original commands, counts and output: [final correctness](evidence/final-correctness.md), [US2](evidence/us2-validation.md), [US1](evidence/us1-validation.md), [US3](evidence/us3-validation.md), [work/memory](evidence/affected-work-memory.md), [physical comparisons](evidence/physical-validation.md), [contract mapping](evidence/contract-coverage.md), and [sanitizers](evidence/sanitizers.md). Final JUnit counts are also retained in [v2-check-counts.json](evidence/v2-check-counts.json).

Sanitizers ran outside the process sandbox to allow thread inspection, based on the prior demonstrated restriction. Full leak reports reproduce the previously recorded LoadModule_fluidsynth/MIX_Init finding; no new leak suppression or dependency workaround was applied. The same scene_transition_contract_test and runtime_step_test report lock-order inversions through SDL initialization/shutdown and system libdbus. The earlier engine-free SDL probe did not reproduce that cycle, so exclusive dependency attribution is unproven. No TSan suppression or deadlock-detector disabling was added. These results do not certify universal leak/race/deadlock freedom or new allocation-failure recovery guarantees.

## Storage and performance

Compiled ColliderPair layout is **136 → 120 bytes** on this toolchain; Rigidbody remains 224 bytes. Final per-child RSS samples overlap the baseline, so no process-memory saving is claimed. Event/contact storage stabilizes at 256/512 slots in the fixed-peak fixture. Allocation claims concern the instrumented C++ new/new[] calls, not every allocator.

The final matrix retains 36 groups × five independent processes per stage, 360 processes total, with 120 warmup / 600 measured steps, seed zero, timestep 1/120, matching CPU affinity, compiler/flags, dependencies, workloads and timing boundaries. Correctness captures are separate and not timing-eligible. Both stages use schema 3. Initial candidate measurements and two sets of 60-process focused follow-ups are retained, rather than overwritten.

Results are mixed. Dense narrow phase at population 200 has lower means: 30.939 → 29.354 ms with one worker (-5.12%), and 14.446 → 13.443 ms with six (-6.95%). This is an observed workload result, not an algorithm-only or universal speedup claim. Every higher final mean has overlapping observed run ranges, which does not establish equivalence. Larger quiet-contact and 1,000-body increases reverse in fresh follow-ups; some small spatial-control means remain higher, including controls with no candidate pairs or awake dynamic bodies. Those downsides and causal uncertainty remain explicit. No general performance-equivalence certification is made.

The discovered cross-file reset call was removed while retaining a single helper definition; the final Release object confirms no out-of-line reference. All affected builds, normal/package/work/safety/physical checks and the matched matrix were repeated. Further cache, padding, scheduling or SAP changes would require evidence beyond the present control deltas. The [2026-10-26 perf investigation](evidence/spatial-investigation.md) adds 192 fresh processes with separate profiles/counters, extended physical controls and unprofiled timings. Broad100/w6 remains +1.45% over 600 steps and +3.19% over 6,000; stress50/w6 remains +4.64% / +2.85%. Whole-process counter and cache observations do not establish a cause or justified tradeoff. T044 investigation is complete, but T038 is reopened; the no-unexplained-regressions acceptance condition has not passed. No engine change was justified by the profiling, and the previously validated runtime fingerprint is unchanged. See [all results](evidence/performance-table.md), [means/tails/spread/worker CSV](evidence/performance-comparison.csv), [run/scoped data](evidence/performance-data.json) and [every regression and disposition](evidence/performance-analysis.md).

## Provenance and reproduction

Behavioral baseline is current post-contact-simplification revision `f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba` plus the retained CMake/test-only characterization patch. `7906959` is review context, not a rollback or old-schema control.

- Baseline source fingerprint: `5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031`.
- Final candidate fingerprint: `b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db`.
- Baseline full source/build/dependency tree: `bin/physics-bookkeeping-20261025/baseline-source`.
- Final raw matrix: `bin/physics-bookkeeping-20261025/captures-v2/{baseline,candidate}/{validate,time}`; final targeted runs: `followup-final` beside it.
- Initial candidate raw matrix: `captures`, targeted runs: `followup-time`, and source archive: `candidate-v1-source.tar.gz` beside the final data. Its fingerprint is explicitly labeled in the initial reports.

Independent ldd checks confirm each runner loads its own vendored libraries. Metadata/raw checksums and actual source fingerprints were verified by the capture/analysis tools. Exact initial staged/unstaged patches, environment and common characterization inputs are retained in [baseline provenance](evidence/baseline-provenance.md). The large bin/ tree is intentionally ignored by Git: retain or archive it before cleaning builds. [Quickstart](quickstart.md) distinguishes exact baseline reproduction from starting a new future comparison.

## Requirement coverage and remaining acceptance

| Requirement | Evidence / outcome |
| --- | --- |
| FR-001 | Current/historical source audit and scope dependencies; only related physics/body/collider/lifecycle and private fallback plumbing changed. |
| FR-002 | Complete baseline/implemented state inventory, source owners, triggers, validity, consumers and disposition rationale. |
| FR-003, SC-001 | Eight final code-backed routes prepared. **Actual maintainer comprehension pending T043.** |
| FR-004, SC-002 | Support ledger four fields → two identities; queue flag+index → index; three owned booleans and dual-write obligations removed without replacement coordination. |
| FR-005 | Retained body/contact/solver/adjacency/support/island queues and indexes each have a distinct purpose; simpler alternatives and the avoided extra call are documented. |
| FR-006, SC-003 | Characterization, shape/warm-start/regression and independent physical/worker comparisons preserve current behavior and exact/tolerant rules. |
| FR-007, SC-004 | Existing and expanded generation/recipient/order/no-replay controls, quiet100, and zero recurring event work; Stay remains absent. |
| FR-008, SC-006 | Before-free recipient transfer, queue cancellation, mutation/retirement/scene tests, populated fixed-peak restoration and actual sanitizer scope. |
| FR-009 | Existing worker joins and serial mutation/reclamation boundaries retained; ordered histories and one/six-worker physical evidence; known desktop detector findings remain visible. |
| FR-010–FR-011, SC-005 | Required comparisons/reporting and bounded regression investigation are present, including perf follow-ups. **FR-011 / SC-005 performance acceptance remains unverified; T038 is reopened** because repeated six-worker spatial-control downsides lack an established cause/tradeoff. T044 completion certifies investigation, not equivalence or acceptance. |
| FR-012 | Public authoring/scene/reporting and installed/relocated consumers preserved; final code/evidence review prepared; human judgment separate. |

## Constitution review

| Principle | Final scoped finding |
| --- | --- |
| I. Performance/scalability | Incremental/local work retained; three state facts removed, measured layout reduction. The avoidable hot helper call was fixed; all higher means investigated and retained with uncertainty. No arbitrary target or unmeasured speed claim. |
| II. oneTBB/ownership | Existing independent cell/body/pair workers and threshold retained. Shared support/lists/events, solving, callbacks and freeing remain at established serial/join boundaries. One/six-worker evidence recorded; no linear scaling claim. |
| III. Collision architecture | Local paper principles reviewed; endpoint/checkpoint/witness/SAP algorithms unchanged. Consumer queue simplification preserves final-witness cancellation and conservative candidate/contact distinction. |
| IV. KISS | Concrete nullable/index-only reductions; explicit names and one geometry boundary without a new owner framework. Remaining state has identified consumers; the inventory and actual human review challenge readability. |
| V. Developer experience | Public operations, return/validation behavior, callback/scene contracts and consumers preserved. No new claim about Unity behavior or new public API. |
| VI. Evidence | Isolated current baseline, raw checksums/fingerprints, matched independent runs, affected work/memory and initial/final investigations retained. Untested platforms and inconclusive causal claims remain unverified. |
| VII. Correctness | New paths characterized before edits; physical/order/lifetime/safety evidence preserved and repeated after corrective work. Existing desktop leaks/lock warnings remain unresolved, not waived or recast as passes. |

No commit, staging or publication was performed by the agent. The read-only requirements checklist is unchanged and initial staged bytes are retained. The final index differs from that snapshot and includes implementation/evidence files; the observed index is left intact (see [index observation](evidence/index-observation.json)). `.specify/extensions.yml` is absent, so no implementation extension hooks are registered. Feature acceptance still requires the supported performance disposition under T038 and actual maintainer findings under T043/T045. Neither is certified by this report.
