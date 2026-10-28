# Foundation control (T017)

## Capture design

T017 repeats the segmented physics / no-render control from T002. Both builds report commit
`2ad8caa466b1c0217b7911d3bca1a53160150456`; the T017 build is a dirty Release tree with source
fingerprint `49622ef319492251744a5c9501f374cfabc6686ff32d27519d787a9995cc44fd`. It used GNU
16.2.1, `-O3 -DNDEBUG`, no sanitizer, no debug tools, and work probes disabled. The T017 runner,
benchmark tool, machine, and vendored dependency versions match the pinned T002 environment.

The capture helper replays all eight T002 sweep configurations for worker allowances 1 and 6:
the seven legacy workload families, with persistent contacts split into zero and two scripted
endpoints. Counts, seed 0, dt 1/120, 120 warmup frames, 600 measured frames, and five fresh timing
processes per population match T002. Renderer submission is disabled. A separate validation matrix
used 10 warmup and 30 measured steps with the per-step invariant oracle: 40 validation processes
passed. The sweep tool validated every schema-3 summary, manifest, and raw sample file.

The default settings were used: segmented physics and no render culling. The captured timing
boundaries retain schema-3 meanings; nested scopes are not added together. Per-process means and
their standard deviation are retained rather than pooling frame samples across processes.

## Matched timing summary

Across the 40 matched workload/population/worker rows, the median `complete_step` change is
−0.03% at one worker and −0.66% at six workers. The row ranges are −22.87% to +6.39% and −18.44%
to +14.03%, respectively; the extremes largely come from very small baseline scopes where a few
nanoseconds create a large percentage. The full per-scope comparison is in
[`foundation-control-delta.csv`](foundation-control-delta.csv).

Selected larger physics cases:

| Workload | Workers | T002 complete step | T017 complete step | Change |
|---|---:|---:|---:|---:|
| Broad-phase stress, 50 | 1 | 0.139 ms | 0.148 ms | +6.39% |
| Broad-phase stress, 150 | 1 | 0.910 ms | 0.939 ms | +3.15% |
| Broad-phase stress, 300 | 1 | 2.697 ms | 2.823 ms | +4.64% |
| Broad-phase stress, 50 | 6 | 0.131 ms | 0.126 ms | −3.27% |
| Broad-phase stress, 150 | 6 | 0.847 ms | 0.891 ms | +5.15% |
| Broad-phase stress, 300 | 6 | 2.726 ms | 2.720 ms | −0.23% |
| Moving add/remove, 1,000 | 6 | 1.458 ms | 1.539 ms | +5.56% |

The 300-element stress case remains about 0.125 ms per step slower in the one-worker run and is
within the pinned control at six workers. This single-worker difference is retained as a residual
same-method cost; this capture does not claim parity for every case. Moving add/remove at 1,000
objects is another residual: `complete_step` is +5.56% and its `cleanup` scope is +7.44%. In the
six-worker broad-phase case at 1,000 objects, `complete_step` is +5.89%, while its `broad_phase`
scope is −0.84% and `cleanup` is +6.35%, so that complete-step difference is not evidence of a
broad-phase regression. The host had no external contention monitor, as recorded for T002.

## Regression investigation

The first matched matrix, retained under `foundation-control-captures/timing-preoptimization/`,
showed the 300-element broad-phase stress case at +12.66% with one worker and +11.57% with six.
Inspection found two avoidable costs on the generalized candidate path: physics overlap checks
looked collider projections up through the cell binding map, and physics pair changes went through
generic policy callbacks. The generalized owner representation also enlarged several keys and
records.

The physics path now reads collider projections directly after checking the endpoint kind, uses
direct physics pair dispatch, and keeps pointer-only keys inside homogeneous interval/cell
contexts. Generic visibility contexts still use their owner policy. Field ordering keeps the
projection compact. In the final matched matrix, the 300-element stress change fell to +4.64% at
one worker and −0.23% at six. A separate retained hot-path check showed the same recovery before
the final full matrix. Thus the initial double-digit increase was mostly avoidable abstraction
overhead; the smaller single-worker residual remains visible and is not attributed to an unmeasured
cause.

Original and final layouts are the same size on this 64-bit build:

| Record | T002 layout | Final layout |
|---|---:|---:|
| Projection | 16 B | 16 B |
| Projection proxy | 32 B | 32 B |
| Axis binding | 64 B | 64 B |
| Cell collider binding | 136 B | 136 B |

`platformator_spatial_segmented_test` also measured zero allocations over warmed updates. The
probes-enabled runtime work test processed 616 independent cell operations with maximum concurrency
1 at one worker and 6 at six workers; its warmed idle step allocated zero bytes. This confirms the
segmented default retains independent oneTBB cell work after the common-core changes.

## Verification and retained files

The final probes-disabled Release build completed. Full CTest passed 33 tests; the work-probe test
was skipped because probes are disabled in the timing build. The probes-enabled executable then
passed directly at one and six workers. Regression, physics bookkeeping, pair-state, segmented,
and dynamic-tree tests passed after the hot-path changes. The installed/relocated package smoke
passed in CTest.

Raw final timing and validation captures, manifests, logs, command lists, and the initial
preoptimization investigation are retained under
[`foundation-control-captures/`](foundation-control-captures/). The capture tool is
[`capture_foundation_control.py`](capture_foundation_control.py), and the comparison is reproduced
by [`compare_foundation_control.py`](compare_foundation_control.py). Checksums for the retained
capture tree are in [`foundation-control-captures/SHA256SUMS`](foundation-control-captures/SHA256SUMS).
The baseline files remain unchanged under [`baseline/`](baseline/).
