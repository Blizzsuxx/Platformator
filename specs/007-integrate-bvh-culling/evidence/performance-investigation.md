# Default-path performance regression investigation

**Recorded**: 2026-10-28. **Status**: investigation only; no engine fixes applied.

**Investigated control source**: `a8795fcece092b3dd47b0ddc4b736015b2c4215500f85472ec5ccba309c54025`. Temporary diagnostic variants changed only the suspected paths; they are not qualified feature benchmark builds.

The investigation left application sources, existing builds and retained benchmark captures unchanged. This note preserves the findings for future work, including the option of keeping the full spatial-index experiment on a research branch and carrying forward only linear render culling. It does not authorize or implement a rollback, branch change or optimization.

Historical comparison context: [same-method control data](convergence/same-method-controls.csv), [results](RESULTS.md#same-method-controls-and-integration-cost), and [foundation control investigation](foundation-control.md). Source line numbers below identify the investigated implementation and may move later.

## Main finding

The added `SpatialUpdate` timer in `src/grid.cpp:248` executes before the empty-work return. An idle simulation reaches the grid from broad phase and cleanup, so this introduces two extra timer scopes per step despite no spatial updates.

Five fresh-process runs per variant, randomized with seed 93421, workers=1, segmented broad phase, render culling=none, no actual rendering, seed=0, dt=1/120:

| Idle case | Complete step | Broad phase | Cleanup |
|---|---:|---:|---:|
| Current control | 409.8 ns | 79.2 ns | 89.8 ns |
| Move only the new scope below the empty-work return | 311.4 ns | 32.0 ns | 42.2 ns |

This removes 98.4 ns (24.0%) of the measured idle step. Control process means ranged from 382 to 452 ns; the changed variant ranged from 293 to 329 ns. The reduction occurs in both broad phase and cleanup, matching the two call sites and historical scope deltas. The historical T017-to-f1 idle control was approximately 319 to 389 ns. These new measurements are an isolated current-source experiment, not a direct comparison against a newly rebuilt historical source.

This timer is benchmark-only. It substantially explains the large *percentage* idle regression in the instrumented controls; it does not establish a 24% production-runtime regression.

## Other concrete additions to the default path

These are confirmed code-path costs, but their individual share of the historical regression has not been established. The filter-call experiment below provides suggestive workload-specific evidence; the other small timing differences were inconclusive.

1. `src/spatialresourcehooks.h:27` always sets/restores a thread-local resource tag, through out-of-line functions in `src/spatialresourcehooks.cpp:28`. Grid flushes, common pair publication and pair transitions use these scopes even when resource capture is inactive. The ordinary timing runner has no allocation interceptors, but still pays for tagging. This is distinct from expensive allocation interception.
2. `src/grid.cpp:251` and `:299` unconditionally read `steady_clock::now()` on nonempty work. Those timestamps are used only when `preparedBoundsMode` is true. These calls are not guarded by the benchmark build macro and are unnecessary for the ordinary live segmented path.
3. `src/gamemanager.cpp:939` notifies `SDLWindow::markGameObjectDirty` for every changed world position. `src/sdlwindow.cpp:624` checks for a Sprite and camera before checking whether any culling coordinator exists. This also happens for headless physics and submit-all rendering. Rotation/scale setters add notifications as well.
4. `src/aabb.cpp:141` adds an out-of-line `Grid::eligiblePair` call, whose `src/grid.cpp:397` implementation branches between prepared replay and live activation. The original live path checked activation/filtering directly. Generalized owner/kind checks are additional hot-path work, though earlier foundation optimization already removed the worse map lookups and generic physics callbacks.
5. `src/physicspairstate.cpp:191` adds an extra serial pass over the gathered pair-delta buffer to count raw discoveries, in addition to gathering, sorting and reduction. Returned publication statistics are computed for ordinary live calls as well as replay; the counting pass can plausibly be fused into gathering. No isolated timing attribution was established for this pass.

## Experiments and uncertainty

The first experiment compiled seven temporary variants and ran 21 separate controlled validation processes and 105 timing processes: five fresh processes per variant/workload. Workloads were idle persistent contacts (1,000 warmup/20,000 measured steps), moving masked-overlap stress with 150 bodies, and moving add/remove with 1,000 steady objects and 1,000 churn (120/600 steps). The scope-only idle effect was clear. Small busy-scene effects overlapped and were not additive; removing raw replay clocks cannot plausibly explain a multi-percent millisecond-scale effect on its own. Do not treat all observed mean differences as causal improvements.

A second experiment kept explicit object link order matched, restricted execution to allowed CPU 0 (not exclusive), randomized variants within each repetition, and ran six separate validation processes and 30 timing processes. All used workers=1, seed=0, dt=1/120, 120/600 frames. Temporary variants preserved the relevant live workload behavior; the inline-filter diagnostic deliberately does not support replay endpoints and is not a proposed production patch.

| Workload | Variant | Mean complete step | Process standard deviation | Change vs control |
|---|---|---:|---:|---:|
| Moving stress, 300 bodies | Control | 2.922256 ms | 0.055477 ms | — |
| Moving stress, 300 bodies | Original direct live filter | 2.848898 ms | 0.039238 ms | -2.51% |
| Moving stress, 300 bodies | Combined telemetry/tag/clock/dirty experiment | 2.889303 ms | 0.035138 ms | -1.13% |
| Moving churn, 1,000 | Control | 2.877636 ms | 0.090434 ms | — |
| Moving churn, 1,000 | Original direct live filter | 2.918654 ms | 0.163179 ms | +1.43% |
| Moving churn, 1,000 | Combined telemetry/tag/clock/dirty experiment | 2.892685 ms | 0.097798 ms | +0.52% |

The filter result is suggestive for the moving stress case, with overlapping process ranges, not a universal measured recovery. Churn differences were noisy and inconsistent. The historical aggregate regression is not fully attributed by these experiments. Historical controls were not pinned and were captured at different times, so host drift remains a confounder.

The earlier approximately 5% figure was f1 versus the intermediate T017 foundation control. The retained CSV's median f1/T002 complete-step ratios against the original control are 1.03458 at one worker and 1.04900 at six workers. These are historical measurements, not new final-source ratios.

## Recommended order

1. Put the idle return ahead of both profiling and resource-tag scopes. Preserve zero applicable work counters and full complete-step boundaries.
2. Read replay clocks only for replay; remove inactive resource-only tagging from ordinary builds/paths while retaining the resource runner's coverage.
3. Skip culling-dirty notifications when the immutable selected configuration has no culling coordinator.
4. Evaluate direct live collision filtering and fused pair-delta accounting separately on matched larger workloads. Preserve replay semantics and validation.
5. Re-run matched controls for any real fix, including a build with benchmark instrumentation disabled, so measurement overhead and product overhead are reported separately.

## Diagnostic artifacts and retention limits

The investigation ran **135 timing processes and 27 separate validation processes**. Exact temporary sources, build/link commands, process summaries and validation logs were stored under `/tmp/platformator-regression-investigation-GRPeoy/`:

- `investigate.py`: variants, commands, separate validation and randomized capture procedure.
- `variants.json`: original source digests and diagnostic variant definitions.
- `runs.json`, `summary.json`: first experiment's per-process results and spread.
- `refine.py`, `refined-runs.json`, `refined-summary.json`: matched-order, single-CPU follow-up.
- Variant directories: exact temporary sources, compiled objects, link/build logs, stdout timing summaries and validation logs.

These are diagnostic experiments, not eligible feature comparison bundles: some deliberately remove metric instrumentation, and the linked runner's embedded source metadata belongs to the original build, not these temporary variants. No standard raw feature capture was emitted or relabeled. Measured values were parsed from retained stdout summaries; displayed ns figures have approximately 1 ns input rounding.

The temporary directory may be cleared by the system. The numerical findings, methods and limitations are retained in this repository note; archive the temporary directory separately if the diagnostic raw logs are needed for later reproduction or publication. The existing feature comparison bundles remain independent and unchanged.
