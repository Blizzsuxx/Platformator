# Constitution and feature acceptance review (T082)

Reviewed against Platformator Constitution v1.1.0, FR-001–027 and SC-001–009.
The final source fingerprint for the reviewed build and new evidence bundles is
`42d3e64f95876cb9fa0284bbf36b95a2e7e755692a1158ecc072e7f38ecec9dc`.

## Functional requirements

| Requirements | Evidence and review |
|---|---|
| FR-001–007: independent runtime choices, preserved defaults, private BVH integration and unchanged physics contracts | [Runtime selection contract](../contracts/runtime-selection.md), [US1 acceptance](us1-acceptance.md), [US2 acceptance](us2-acceptance.md), collision/physics integration and all-eight selection tests in the final CTest log. The installed API remains independent of the private Box2D subset. |
| FR-008–012: conservative overlap/visibility, deduplication, lifecycle publication, ordering and reclamation | Independent replay references cover candidate sets and transitions; render readback and lifecycle suites cover output/order and invalidation. Ownership, worker-join, reduction and retirement boundaries are reviewed in [implementation-review.md](implementation-review.md) and [data-model.md](../data-model.md). |
| FR-013–017: no hidden unselected work, explicit validation, runtime/package compatibility | Performance, validation and resource executables are separated; the resource runner contract verifies the timing executable rejects resource mode. The final Release CTest log includes package install/relocation and source-consumer smoke. Probe-enabled work tests are retained in the cleanup-work spatial log. |
| FR-018–025: deterministic replay, representative input matrix, complete costs/resources and eligibility | [Benchmark results contract](../contracts/benchmark-results.md), the 41-case replay inputs, final validation/timing/resource bundles and their eligibility ledgers. Timing bundles retain five fresh processes per reported configuration. Mismatches and failed runs stay in their ledgers. |
| FR-026–027: segmented control, measured integration cost and bounded conclusions | [Foundation control](foundation-control.md) compares T002/T017 with the final default on aligned `complete_step` boundaries. [RESULTS.md](RESULTS.md) reports wins, regressions and limits without adopting a new default or claiming a universal Box2D optimum. |

## Success criteria

| Criterion | Acceptance evidence |
|---|---|
| SC-001: all eight combinations and preserved defaults | Current-source all-eight actual-engine captures are recorded in [quickstart-validation.md](quickstart-validation.md); selection and CLI checks are in the final CTest log. |
| SC-002: matched collision coverage and transition behavior at supported workers | 248/248 final replay validation records pass across applicable methods and workers 1 and 6; collision, pair-state, support, callback and lifecycle tests pass. |
| SC-003: controlled render parity and order | Submit-all image/readback comparisons and lifecycle checkpoints are retained in [render-images](render-images/) and [render-lifecycle.md](render-lifecycle.md); the final CTest run passes those tests. Hardware renderer parity remains unverified. |
| SC-004: 10 of 1,000 safely visible sprites | A fresh software-rendered capture records 1,000 submissions for `none` and 10 for each culling method; see [quickstart-validation.md](quickstart-validation.md). |
| SC-005: reproducible repeated comparisons | Final replay timing has 41 declared workloads and five processes for each eligible method/worker configuration. Selected engine timing has five processes for every eligible selected case/method/worker configuration. Inputs, commands, raw samples, source fingerprints, renderer signatures, checksums and validation pairing are retained. |
| SC-006: population and fraction coverage | Replay inputs include 100, 1,000 and 10,000 endpoints; changing bounds at 0%, 1%, 10% and 100%; visible populations at 0%, 1%, 10% and 100%; plus churn, migration, teleport, skew, mixed sizes, dense overlaps and camera/view changes. The case set is representative, not a full Cartesian product. |
| SC-007: initialization, maintenance, ordering and resources | Replay and engine records expose initialization and subsystem scopes. Resource captures include initial/peak/final owner capacities, tagged allocation totals, available process RSS and exclusions. Replay capacities are sampled after initialization and each complete step outside replay timing. |
| SC-008: regression, lifecycle, worker and package checks | Final Release CTest: 55/55 passed; seven probe-only checks are skipped in that probes-disabled build and pass in the probes-enabled spatial run. ASan/UBSan and available TSan results/limitations are recorded in [final-validation.md](final-validation.md). Installed/relocated and source consumers pass package smoke. |
| SC-009: documented basis for each choice | [RESULTS.md](RESULTS.md) and [us3-acceptance.md](us3-acceptance.md) separate measured help, regressions and unverified claims. Production defaults remain segmented collision and submit-all rendering. |

## Ownership, invariants and reclamation

- `PhysicsManager` owns the selected physics index and `PhysicsPairState`; the latter owns canonical pair records, collider adjacency, the pending narrow-phase batch and reusable reduction scratch. Segmented physics retains cell-local endpoint membership and applies worker-local deltas only after cell workers join.
- BVH proxy records preserve generation-aware endpoint identity; recycled tree node IDs are not object identity. Dirty index writes and maintenance complete before immutable queries. Candidate deltas reduce before callbacks and reclamation.
- `RenderCulling` owns visibility records, its independent segmented/BVH state, dirty/revision caches, candidates and retained output. `SDLWindow` supplies the current draw order/view revisions and consumes the ordered output. Camera and sprite visibility membership never enters gameplay physics.
- The replay executor owns preloaded trace fixture records and its selected replay context. Resource snapshots query the selected owner after initialization and complete steps; they do not change step timing. Fixture storage, allocator metadata, direct C/SDL/TBB allocation paths and process-wide RSS are separately described in the resource contract.
- Queued mutations are published at the existing safe simulation/render boundaries. Worker-produced deltas are reduced after join; callback destruction and empty-cell/index retirement occur only after readers finish. The relevant queue/cache ownership and invalidation decisions are detailed in [implementation-review.md](implementation-review.md) and [data-model.md](../data-model.md).

## Constitution performance obligations and limits

The implementation preserves independent cell-level oneTBB work and measures one and six workers. Pair-store writes and reductions remain serial because the canonical pair map, dual adjacency references, pending narrow-phase queue and callback retirement order are shared mutable state. Draw-order restoration is serial because spatial query enumeration is not transparent submission order. Local `EFFICIENCY:` comments in [physicspairstate.cpp](../../../src/physicspairstate.cpp) and [renderculling.cpp](../../../src/renderculling.cpp) state their cost/frequency, why they remain, and concrete sharded-reduction and generation-stamped draw-order replacement work.

The evidence does not establish hardware-renderer timing, pinned-core results, external host contention, or a full engine population matrix through 10,000 objects. Replay collision timing excludes trace loading and bounds/transform formation; resource durations are ineligible for timing. Allocation totals exclude direct C, SDL and oneTBB allocator paths; capacity estimates exclude allocator metadata. These limits remain explicit in [MACHINE.md](evaluation-timing/MACHINE.md), [RESULTS.md](RESULTS.md) and [us3-acceptance.md](us3-acceptance.md).
