# Spatial-index evaluation results (T076)

## Scope and provenance

The final source fingerprint for the validation, timing, and resource runs below is
`42d3e64f95876cb9fa0284bbf36b95a2e7e755692a1158ecc072e7f38ecec9dc`. The primary
BVH profile is `native-partial`, zero padding, serial queries. The replay sweep also
retains parallel-query variants as separately reported configurations. Each reported
timing configuration has five fresh processes. Replay runs use 10 warmup and 30
measured operations; actual engine runs use 120 warmup and 600 measured frames.
Process means are aggregated without pooling frame samples. `none` is the no-index
control and is excluded from indexed-method winner counts.

The machine, compiler, SDL/oneTBB versions, worker allowances, renderer, and affinity
limitations are in [`evaluation-timing/MACHINE.md`](evaluation-timing/MACHINE.md).
Raw reports and manifests are retained in the [replay timing bundle](evaluation-timing/replay-full-serial-final/)
and [engine timing bundle](evaluation-timing/engine-serial-final/). The 248 replay
and 28 engine validation and resource records are linked in the corresponding
[`evaluation-validation/`](evaluation-validation/) and
[`evaluation-resources/`](evaluation-resources/) trees. All six final bundle roots
have verified checksums and use the fingerprint above.

## Indexed-method results

For collision replay, the ratio is BVH `spatial_replay` time divided by segmented
time for the same case and worker allowance. Across 20 paired cases, the median ratio
is 2.371 at one worker and 2.397 at six workers: segmented is faster in 19/20 pairs
at each allowance. The one BVH win in each worker set is the 10,000-proxy
uniform case with all bounds changing. Dense, mixed-size, skewed, churn,
migration, teleport, and filter-change cases remain in the raw matrix, so this
count includes both simple density settings and dynamic operations.

For visibility replay, each indexed method is compared with linear on the same case
and worker allowance. BVH/linear median ratios are 1.079 at one worker (BVH faster
in 3/21 cases) and 1.086 at six workers (5/21). Segmented/linear ratios are 1.033
at one worker (segmented faster in 4/21) and 1.049 at six (7/21). These aggregates
include static, moving, panning, resizing, aspect-change, churn, and 0/1/10/100%
change workloads. The result is a consistent small-workload advantage for linear
in this replay matrix, with BVH/segmented wins in individual configurations. It
does not justify replacing linear universally.

Separating no-change (`changed_count == 0`) from changing workloads
(`changed_count > 0`) makes the update tradeoff clearer. Ratios below 1 favor
the method named first; static and dynamic rows are paired by case and worker:

| Replay comparison | Workers | Static median ratio (wins / cases) | Dynamic median ratio (wins / cases) |
|---|---:|---:|---:|
| Collision BVH / segmented | 1 | 54.221 (0/3) | 2.137 (1/17) |
| Collision BVH / segmented | 6 | 50.142 (0/3) | 2.156 (1/17) |
| Visibility BVH / linear | 1 | 1.036 (3/13) | 1.533 (0/8) |
| Visibility BVH / linear | 6 | 1.024 (5/13) | 1.551 (0/8) |
| Visibility segmented / linear | 1 | 1.016 (3/13) | 1.605 (1/8) |
| Visibility segmented / linear | 6 | 0.984 (7/13) | 1.678 (0/8) |

The large static collision ratios come from the near-no-op segmented update,
not a large absolute frame cost: for the 10,000-proxy `collision-n10000-p0`
trace at one worker, segmented averaged 0.000076 ms (about 76 ns) while BVH
averaged 0.041292 ms. This is the expected cost of maintaining/querying a tree
when no bounds change. BVH initialization is recorded separately in the same
run manifests. Dynamic changes make the relative BVH cost smaller, but
segmented still wins most paired replay cases.

The visibility replay's `spatial_replay` boundary includes selected-index work,
exact candidate refinement, result updates, and order restoration. It excludes
trace parsing and validation oracles. It is therefore useful for the selected
consumer cost but is not a draw-call or full SDL rendering measurement. The
[`foundation-control.md`](foundation-control.md) report separately documents
same-method overhead against T002/T017.

## Actual engine and rendering

The selected sprite fixture contains 1,000 sprites with 10% visible. At one worker,
all three indexed methods submitted 100 sprites: render time averaged 3.951 ms for
linear, 3.922 ms for BVH, and 3.881 ms for segmented, versus 4.679 ms with no
culling and 1,000 submissions. At six workers the same 100 submissions were
preserved, while measured render times were 3.938 ms linear, 3.980 ms BVH, and
4.118 ms segmented, versus 4.732 ms with no culling. Culling reduced work sent to
SDL in both worker configurations; the indexed-method timing order changed with
worker allowance. The offscreen software renderer dominates these absolute render
times and does not qualify a hardware renderer.

In the no-render `engine-broad-n100` case, segmented complete-step time was
0.05090 ms at one worker and 0.03109 ms at six. BVH measured 0.04151 ms and
0.04969 ms, respectively. This reversal illustrates that worker allowance and
the chosen update/reduction path matter; it is not evidence for a universal
physics winner. The replay matrix is the broader algorithm comparison, while the
engine captures verify integration on a bounded selection of live fixtures.

The combined-engine case records `complete_step` and actual `render` as separate
boundaries. Complete-step time excludes independent SDL rendering; nested scopes
must not be added to their parents. Render preparation and submission breakdowns
are retained in the summary. On this host the software renderer's millisecond-scale
work is much larger than the microsecond-scale culling preparation, so small
preparation differences are not a reliable proxy for GPU-backed workloads.

Initialization is material for short-lived scenes. In the one-worker 1,000-sprite
engine case, total recorded initialization averaged 87.4 ms with no culling,
88.3 ms linear, 87.9 ms BVH, and 90.5 ms segmented; this includes engine fixture
and renderer setup, not only the spatial index. Mean-of-process render p95 was
5.60 ms with no culling, 5.09 ms linear, 4.86 ms BVH, and 4.79 ms segmented.
For the one-worker moving 1,000-proxy visibility replay, initialization averaged
0.572 ms with no index, 0.733 ms linear, 1.114 ms BVH, and 2.014 ms segmented;
mean-of-process replay p95 was 0.040, 0.143, 0.209, and 0.185 ms respectively.
These tails are means of per-process p95s, not a pooled percentile.

## Same-method baseline and worker scaling

The following `complete_step` values compare the legacy T002 control, the
same-method T017 control, and final default segmented/none capture for the
100-body `broad_phase` case. These are aligned on workload count, seed, worker
allowance, and the common complete-step boundary; rows remain separate by source
revision and schema. T002/T017 raw captures and their comparison are retained in
[`baseline/`](baseline/) and [`foundation-control.md`](foundation-control.md).

| Workers | T002 | T017 | Final segmented/none | Final vs T002 | Final vs T017 |
|---:|---:|---:|---:|---:|---:|
| 1 | 0.05284 ms | 0.05229 ms | 0.05090 ms | −3.66% | −2.66% |
| 6 | 0.03370 ms | 0.03083 ms | 0.03109 ms | −7.75% | +0.84% |

The six-worker final result is within 1% of T017 for this row. T017's broader
40-row comparison still has a slower 300-element one-worker case (+4.64%) and
the final evaluation does not replace that separate regression analysis. The
current engine benchmark records `spatial_update`, `index_update`, `broad_phase`,
and `result_reduce`; on the 100-body example segmented `result_reduce` averaged
about 0.000036 ms at one worker and 0.000040 ms at six, while BVH reduction was
about 0.000431 ms and 0.000536 ms. This reflects the current deterministic
serial result publication path and helps explain why more workers do not improve
every configuration.

## Initialization, tails, and resources

Initialization, per-process means, per-process p95/p99/max summaries, bounds
preparation, index update, maintenance, query, reduction, and complete-step
boundaries are present in the raw timing files and aggregate reports. Tails are
means of each process's tail statistic; the report does not pool frame samples
or add nested scopes. Replay traces include provided collision AABBs, so they do
not charge transform/AABB formation. Initialization remains a separate cost and
should be included when a workload frequently creates or replaces indices.

Resource instrumentation is isolated in separate timing-ineligible runs. It
tracks scalar/array/sized/aligned/nothrow C++ allocation families and Box2D
allocator callbacks, plus owner-reported retained capacities for selected
indexes, common pairs, and scratch containers. For example, the engine
1,000-sprite visibility fixture reports a peak owned visibility-index capacity
of 154,880 bytes for segmented, 58,672 bytes for linear, and 363,016 bytes for
BVH in the selected one-worker run. The corresponding process peak RSS samples
were about 295.5 MiB (linear), 295.4 MiB (BVH), and 296.8 MiB (segmented).
These whole-process readings include SDL and unrelated runtime allocations,
so they should not be interpreted as index memory. The capacities are estimates
from container/tree snapshots, not a process-wide memory total. Other
allocations are classified into tracked Box2D or unclassified domains; trace
storage, SDL, oneTBB internals, stacks, allocator metadata, and external process
memory are not all attributable to an index. Resource-run durations are not
timing evidence.

## Algorithm and implementation implications

The outcomes support keeping selection explicit. Linear visibility has a small
and reliable query path for the tested populations; BVH and segmented indices
can win on selected cases but cost more in other distributions and update
patterns. Collision segmented wins the replay set broadly, while the live engine
small-case result depends on workers and whole-step overhead. Nothing here
supports automatic backend switching or a universal Box2D optimum.

The current segmented implementation maintains shared memberships and performs
deterministic pair/result publication serially where mutation and callback
ordering require it. The `EFFICIENCY:` comments beside pair reduction and
render ordering explain the safety constraints, complexity, and concrete
replacement experiments: sharded pair/result staging with deterministic merge,
and generation-stamped visible-order output with 0/1/10/100% visibility
benchmarks. Further options worth evaluating separately are local versus global
SAP, batched writes, and adaptive policies. Any such change should retain the
same workload, image/order, lifecycle, and resource contracts, and measure its
initialization and tails against these retained controls.

## Limits

The complete replay matrix covers 41 workloads across collision and visibility,
with one and six workers. The actual-engine matrix is intentionally selected:
100-body broad phase, 1,000-sprite/10%-visible, and combined fixtures. It is not
a full live-engine population matrix through 10,000 objects. Actual rendering
was validated and measured with SDL's offscreen software renderer only; no
hardware-renderer performance result is available. The host was not CPU-pinned,
and no external contention monitor was recorded. These results describe this
build and host, not cross-machine confidence intervals.
