# Solver ordering removed — 2026-10-26

The maintainer explicitly requested removal of canonical physics-solver ordering and waived deterministic solving. This is an intentional change to the earlier feature acceptance contract, recorded in spec.md and the physics-lifecycle contract. It does not waive contact completeness, support/lifetime safety or canonical event-batch/endpoint order for equivalent transitions.

## Implementation

- Deleted `canonicalLess`, the sort/reindex helper, `orderSolverPairs` declaration/definition/call, `solverOrderDirty` and its mutation sites.
- `solverPairs` uses append/swap-remove membership order. Per-pair `solverIndex` and local repair of the moved entry remain necessary for O(1) removal; no sorting or full reindex pass remains.
- Membership returns void because its former change result only drove the removed dirty flag. No replacement ordering algorithm, queue or cache was introduced.
- Support regression still checks 128 contributions, filtering/trigger/activity/body replacement, exact controlled event order across workers, finite physical state, net transfer and before-free lifetime. It no longer compares solved position/velocity/rotation/angular snapshots across workers. Other geometry/physical regressions remain.

## Executed checks

- Full cleanup-debug build succeeds. Full normal/package suite: 26 registered, 24 passed, two expected instrumentation-disabled skips. Installed/relocated SDK and example consumers are included.
- Focused physics/contact/geometry/bookkeeping/support/cleanup/deletion selection: seven passed before the test-contract adjustment; final full suite covers the adjusted support test.
- cleanup-work rebuild and selected support/work tests: 2/2 passed. Direct six-worker runtime-work control passes: zero idle allocations, geometry/transition/event/solver-refresh work; fixed-peak retained event/contact capacities 256/512.
- The final explicit `<cmath>` include was followed by rebuilding and rerunning the affected support host in both Debug and work configurations; results are retained beside the full run.
- Source audit finds no live `orderSolverPairs`, `solverOrderDirty` or sorting comparator in runtime/tests. The remaining event sorting is unchanged.

Current runtime source fingerprint: `cc6ce0456970f439e78ae6e664a7ab90066a12b8c2155c14a47483b68d6c1588`. Logs/JUnit and index-preservation snapshot: `bin/solver-order-removal-20261026-bxgfe2oe`. Per-file raw checksums/provenance are retained there.

Subsequent [matched Release comparisons](solver-order-performance.md) measure this change: heavier/longer 1,000-body cases improve about 6–8%, while the short one-worker gain is uncertain and a small one-worker case has a higher mean. These are workload-specific end-to-end results, not isolated sort savings; no memory saving is claimed. Earlier sorted-runtime timings and exact trajectory comparisons remain historical. No new full sanitizer or original 36-group timing matrix was run for this scoped deletion. Previously documented FluidSynth leaks and SDL/libdbus warnings remain unresolved, not passes. T038's prior zero-solver-contact broad-phase downside is not resolved by deleting solver sorting. T043/T045 still need actual maintainer findings.
