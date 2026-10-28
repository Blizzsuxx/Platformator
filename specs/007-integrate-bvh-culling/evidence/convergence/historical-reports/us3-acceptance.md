# User Story 3 evidence acceptance (T077)

The primary bundles below were generated with source fingerprint
`42d3e64f95876cb9fa0284bbf36b95a2e7e755692a1158ecc072e7f38ecec9dc`. Each sweep retains raw
CSV samples, per-run manifests and logs, the case/profile/method/worker keys, an eligibility ledger,
checksums and aggregate `summary.csv`/`report.md`.

## Minimum independent comparison

The compact independent collision and visibility replay smoke is retained in
[`spatial-replay-smoke/`](spatial-replay-smoke/). It pairs the same immutable inputs with separate
validation and timing runs, retains raw captures, measures selected index maintenance and result
reduction, and records machine/source/profile provenance. The larger evaluation is the primary
reported evidence:

| Domain | Workload and configurations | Validation | Timing | Resources |
|---|---|---:|---:|---:|
| Collision and visibility replay | 41 cases × applicable method choices × workers 1 and 6; 10 warmup and 30 measured steps per case; native-partial, zero padding, serial BVH query profile | 248/248 eligible | 1,240/1,240 eligible, five fresh processes per configuration | 248/248 linked, resource durations timing-ineligible |
| Actual engine | Physics broad phase (100 bodies); visibility (1,000 sprites, 10% visible); combined (1,000 sprites, 1% visible, 10% changed, 128 colliders), all eight broad-phase/culling choices; workers 1 and 6; 120 warmup and 600 measured frames | 28/28 eligible | 140/140 eligible, five fresh processes per configuration | 28/28 linked, resource durations timing-ineligible |

The validation bundles are [`replay-full-serial-final`](evaluation-validation/replay-full-serial-final/)
and [`engine-serial-final`](evaluation-validation/engine-serial-final/). Timing bundles are
[`replay-full-serial-final`](evaluation-timing/replay-full-serial-final/) and
[`engine-serial-final`](evaluation-timing/engine-serial-final/). Linked resource bundles are
[`replay-full-serial-final`](evaluation-resources/replay-full-serial-final/) and
[`engine-serial-final`](evaluation-resources/engine-serial-final/).

## Required dimensions and measured boundaries

The 41 replay cases cover populations 100, 1,000 and 10,000; changing bounds at 0%, 1%, 10% and
100%; visible fractions at 0%, 1%, 10% and 100%; static and moving scenes; churn, migration,
teleportation, skewed and mixed-size bounds, dense overlaps, filter changes, coalesced writes, and
camera pan/jump/resize/aspect changes. This is a representative case set, not a complete Cartesian
product. Collision replay supplies precomputed AABBs and excludes transform-to-bounds construction;
visibility replay measures its declared sprite-transform/view operations but does not call SDL draw
APIs.

Engine fixtures provide the required complete-step, physics and actual-render boundaries on
controlled workloads. Sprite-only cases have no gameplay colliders. Combined cases run each
physics/render choice; the visibility fixture and physics fixture keep their respective domains
independent. The actual renderer is SDL offscreen software at 1600×900. Hardware renderer
comparisons remain unverified.

Raw step/replay/render samples retain average and tail scopes/counters; aggregate reports preserve
per-process mean/stdev and the labeled mean of process tails. Initialization is recorded separately
from steady-state samples. Nested scopes are not added to `complete_step`. The contract records
`bounds_prepare`, index update/maintenance/query, `result_reduce`, actual `render_submit`, output
counts, proxy changes, rebuild work and per-frame pair/visibility changes.

Resource bundles are separate executions linked to timing keys. They record initial, peak and final
owned capacities; tagged C++ and Box2D allocation counts/bytes and peak live bytes; process RSS;
and explicit exclusions. Replay owner capacities are sampled at initialization and after each
complete step outside its measured interval. Resource captures are never interpreted as timing
results. Direct C, SDL and oneTBB allocation paths and allocator metadata remain excluded where
documented.

## Eligibility and limits

Validation is paired to timing by case hash, method, source fingerprint, worker count and frozen
profile. Actual-render validation additionally requires the same renderer/backend/output-view
signature as the paired capture; software validation does not qualify hardware timing. Each summary
retains all raw process records and rejection reasons; no frame filtering is used to salvage a run.

The machine details and affinity limits are in [`evaluation-timing/MACHINE.md`](evaluation-timing/MACHINE.md).
The CPU was not pinned and the host had no contention monitor. The selected engine suite is bounded;
the 10,000-object and percentage matrix is provided by replay rather than a full-engine cross
product. See [`RESULTS.md`](RESULTS.md) for observed method wins/regressions, T002/T017 controls,
tails, scaling and resource comparisons.
