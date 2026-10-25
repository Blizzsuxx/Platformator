# Validation: Simplify Contact Events

The implementation derives Enter/Exit from confirmed-contact transitions independently of scripts and removes Stay callbacks, observer tracking and reporting. Geometry and startup responsibilities were consolidated; physical contact, solver, body and support indexes retain their distinct invariants. The task list records implementation and verification completion.

## Behavior and ownership

- Geometry workers mutate their own pair/contact only. The existing serial reduction emits Enter/Exit after joining workers; explicit pair removal emits Exit before erasure. Pair destructors release memory without runtime lookup or notifications.
- Event generation has no script predicate. Delivery checks each endpoint independently, orders Exit before Enter and canonical endpoint handles, and preserves callback exceptions, deferred mutation and dispatch-before-free boundaries. Late scripts receive no replay and may receive the real Exit without a prior Enter.
- Stay event/callback declarations, dispatch, observer vector/index/dirty state, step markers and metrics are removed. The duplicate solver collision projection and unused narrow-phase aggregate are removed; solver consumers read current contacts through ordered solver pairs.
- Body-state changes retain affected-adjacency solver refresh. Script changes do not refresh physical neighbors. Support counts, sleeping, wake-up, cell witnesses and the independent all-contact oracle remain intact.
- `physicsgeometry.cpp` contains shape dispatch, circle routines, SAT and clipping. Relocated SAT/clipping tokens match the baseline apart from naming/whitespace; no math or tolerance changed. ScriptComponent owns per-script startup; GameManager retains the mutable indexed scheduling queue.

## Executed checks

| Check | Result and scope |
| --- | --- |
| Final Debug build and complete normal CTest | 25 registered: 23 passed, two expected instrumentation-disabled skips. Includes installed/relocated SDK consumers and Mario. |
| Production build | Successful full build, including runner, Mario, vendor libraries, tests and independent public headers. |
| Benchmark contracts | 3/3 passed: C++ recorder/generation, real-runner zero-warmup fixture, and ten Python schema/reader/plot/fixture tests. |
| Work/contact/support configuration | 3/3 passed; expanded churn cardinality/storage test also passes. |
| Generated event histories | 128 pairs, zero/one/two recipients, workers one/six: matching Enter/Exit histories; no-script generation tested separately from callbacks. |
| Mixed transition delivery | Entering pair has lower handles, but observed trace is `exit:oldA exit:oldB enter:newA enter:newB`. Passes Debug, leak-enabled ASan/UBSan and TSan. |
| Physical capture validation | All 54 candidate captures pass; all 27 worker comparisons and all 54 baseline/candidate snapshot comparisons pass recorded tolerances. |
| ASan/UBSan memory access | Full non-package suite: 22 passed, two instrumentation skips; leak detection disabled for this explicitly separate access/UB check. |
| Leak-enabled ASan/UBSan | Changed physics-only hosts pass. Full suite: eight passed, fourteen failed on FluidSynth-path leaks, two skips. This is not a clean full leak check. |
| TSan physics/lifecycle/mutation selection | Twelve passed, two desktop tests report SDL/libdbus lock-order warnings, one work-probe skip. Changed contacts, geometry, support, callback mutation and worker histories pass without race reports. |

Detailed commands, counts and original output: [final correctness](evidence/final-correctness.md), [ordering trace](evidence/final-order-trace.txt), [US1](evidence/us1-validation.md), [US2](evidence/us2-validation.md), [US3](evidence/us3-validation.md), [physical comparisons](evidence/parallel-validation.md), [sanitizers](evidence/sanitizers.md), and [leak attribution](evidence/leak-attribution.md).

The two TSan desktop warnings reproduce with detailed stacks in `scene_transition_contract_test` and `runtime_step_test`, through SDL initialization/shutdown and system libdbus locks. Backend source is unchanged, but a small SDL-only probe did not reproduce the cycle; exclusive dependency attribution is not established. These checks remain unresolved. No TSan suppression or deadlock-detector disabling was introduced.

LeakSanitizer requires outside-sandbox thread inspection in this environment. Once allowed, all reported allocation stacks in the full run pass through `LoadModule_fluidsynth` and `MIX_Init`: 8,800 bytes in 176 allocations. This reproduces the previously documented initialization issue and its engine-free probe; it was not suppressed or fixed by this contact feature. No engine-owned allocation stack or memory-access/UB error was observed in that run. These results do not certify universal leak or race freedom.

## Affected work and storage

The warmed idle fixture includes scripted and unobserved confirmed contacts. Recorded values are zero C++ `new/new[]` allocations, geometry visits, transition comparisons, event records and physical adjacency refresh visits. Script startup/replacement remains independent of physical neighbor work; actual body sleep changes still trigger the required refresh.

Five fixed-peak churn cycles assert sixteen real added contacts and restoration of original membership after removal. Event storage stabilizes at 256 slots and contact storage at 512 slots. `ColliderPair` measures 152 bytes in the baseline declaration and 136 bytes in the candidate: 16 bytes removed per pair on the tested platform. Process RSS samples overlap the single baseline observation, so no net RSS improvement is claimed. See [affected work and memory](evidence/affected-work-memory.md).

## Measurement provenance

The baseline is revision `f67f6ba47f88f08ee2dd2aae1ea8eee940aa07d8` plus the retained common fixture patch. Each stage uses 27 case/recipient/population combinations, workers one and six, 120 warmup and 600 measured steps, seed zero, timestep 1/120, fixed matching CPU affinity and five independent timing processes per group. Captures require Release without sanitizer, work probes or debug tools. Physical correctness captures are separate and labeled `timing_eligible=false`.

Raw files, per-run manifests, command logs, aggregates and checksums are retained under `bin/contact-events-20261025/{baseline,candidate}/{validate,time}`. The `bin/` tree is intentionally ignored by Git; retain it or archive it before cleaning build outputs. [Baseline provenance](evidence/baseline-provenance.md) and [capture_matrix.py](evidence/capture_matrix.py) document reproduction. The zero-warmup pre-evaluation fixture check was corrected outside the measured warmup=120 workload; the common scene builder and timing boundaries are unchanged.

Schema 3 removes the Stay metric and counts generated pair events through cleanup; historical schema-2 event counts have different meaning. Compare timing scopes only under equivalent workloads, never relabel old counters. Event sorting moved from narrow phase to delivery; complete-step time measures its net effect, while subsystem changes can reflect moved work.

Performance conclusions and regression investigation are recorded in [performance-analysis.md](evidence/performance-analysis.md). All 540 independent timing processes were verified. At 1,000 persistent scripted pairs, complete-step means improve by 33–48% for the tested one/six-worker configurations. Fifteen groups have higher means; all overlap observed baseline run ranges. The largest is +7.23% in the 500-population, six-worker broad-phase case, chiefly in cleanup. These downsides and uncertain repeatability remain reported; overlap does not prove equivalence. See the linked analysis for every positive delta, tails, worker limits and state-removal tradeoffs.

## Requirement coverage

| Requirements | Evidence |
| --- | --- |
| FR-001–FR-003, SC-001 | Internal generated records, recipient matrix, shape false positives, multi-cell removal tests; US1 and physical capture comparisons. |
| FR-004, SC-002 | 100 unchanged updates, real re-contact and quiet-work assertions; contact tests and affected-work report. |
| FR-005–FR-006, SC-003 | Late attachment/addition/replacement, delivery-time eligibility, Exit without Enter, independent recipient callbacks; US3. |
| FR-007–FR-008, SC-004 | Public callback removal concepts, current symbol audit, observer-state deletion, work probes and metric inventory. |
| FR-009 | Support reference changes, 128 supports, body replacement, sleeping support, final-support wake-up; physical snapshot oracle and support tests. |
| FR-010, SC-005 | Ordered worker traces, mixed batch, exception/retirement/scene tests, sanitizer results within stated scope. |
| FR-011 | Actual schema-3 producer/reader integration, eleven counters/eight scopes, cleanup/out-of-frame counting and compatibility rejection tests. |
| SC-006 | Recorded baseline/candidate workload matrix, raw timing and allocation/storage evidence, investigated results with limitations. |

## Compatibility and scope

Removing a virtual callback changes source/binary compatibility: rebuild the engine and consumers together and remove obsolete Stay overrides/calls. Enter/Exit payload meaning and callback-duration borrowing remain unchanged; no scene-format migration is required. Current benchmark readers require schema 3. Archives remain untouched and require their original tooling revision.

No broad-phase redesign, solver/sleep algorithm rewrite, new public contact-query API, asynchronous event bus or per-script contact history was introduced. Original staged specification/planning changes were preserved; no commit or publication is part of this work. Final constitution review is scoped to this feature and its explicitly recorded sanitizer limitations; untested platforms and unrestricted desktop dependency safety are not certified.

## Final constitution review

| Principle | Feature review |
| --- | --- |
| I. Performance and scalability | Incremental confirmed transitions replace repeated Stay work; warmed zero-work/allocation and fixed-peak churn probes pass. All positive timing deltas are investigated, including solver indirection; no known affected inefficiency is deferred under an undocumented exception. |
| II. oneTBB and ownership | Existing independent-pair workers and threshold retained. Reduction, callbacks and reclamation remain serial after workers join. One/six-worker traces and physical snapshots match; persistent contacts show no parallel scaling claim. |
| III. Collision architecture | No foundational broad-phase change; cell witness and candidate/contact distinction retained. Shape relocation is token-verified, and independent physical oracle captures match. |
| IV. Simplicity | Contact presence has one owner; no destructor notification or observer invalidation. Duplicate solver projection removed. Retained indexes have documented physical/oracle consumers. |
| V. Developer experience | Enter/Exit naming and endpoint semantics retained. Stay removal is deliberate; source/binary rebuild and callback migration documented. No new claim about Unity implementation. |
| VI. Evidence | Baseline/common fixture, 54 matched groups, five processes each, raw checksums, means/tails/spread, work/storage/RSS and limitations retained. Complete-step comparisons account for sorting moving scopes. |
| VII. Correctness | Focused contracts, mutation/lifecycle, normal/package/production, worker/oracle and changed-path sanitizer checks pass. Full leak and desktop lock-order findings remain explicitly unresolved; unrelated dependency rewrites were not folded into this feature. |

All 43 implementation/evidence tasks are recorded complete. This means the stated checks were executed and their limitations recorded; it does not turn failed desktop sanitizer checks into passes. `.specify/extensions.yml` is absent, so there are no after-implementation hooks to dispatch. The requirements checklist was read without changing its markers.
