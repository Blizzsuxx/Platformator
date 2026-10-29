# Linear Render Culling Results — T044 Regeneration

## Method and provenance

This report uses `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`. It contains 17 bounded cases in pre-feature submit-all, candidate-disabled, and candidate-linear modes; five fresh Release processes per case/mode (255 focused timing processes), plus 60 stock-engine controls. Every process uses seed 0, 120 warmup steps, 600 measured steps, and `dt=0.008333333333333333`. Comparison order is shuffled within each repetition. CPU affinity is 0–11 on an AMD Ryzen 5 7535HS with six physical cores; worker counts are one and six.

Focused probes queried SDL and recorded the software renderer, 320×240 output, scale 1×1, default viewport, no target/logical presentation, and VSync off. Headless probes have no renderer or render samples. Stock-runner manifests omit renderer identity, so the renderer is explicitly unverified for those individual controls.

The pinned baseline is commit `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75` plus common fixture/probe/observation support only. Baseline and candidate source fingerprints are `536cd40de0dea634250c56ffca20e68231ded659cef7257c9b8b9d60ee42529f` and `4a5ce52e10fc81f903c3687879ee9d883836ec13668e18185f967fc6fa20dfba`. Both use fixture `caff033f8a7577eb7d3f9eafe5a897bb09a70db78b385a7d010d4c174d84e65d` and harness `83f7119f120a7a3fd401028553ee29595b23e93a87f101b58ecb3611bc1abbd7`. Timing probes are Release with debug tools, work probes, and sanitizers off. Validation and resource runs use separate work-probe binaries.

The report SHA-256 is `ed7e25acbcaf21f88ef569ec70d4a8d5685d88988a470cd6ca47e0c76bba0a10`; the raw-index SHA-256 is `7ffc42a7c93ed1057536b4d55e1866ad7e4c33e195010b417b0c6b3f3bb7a3e3`. All 51 rendering proofs, 51 resource proofs, 255 focused raw captures, and 60 control captures passed independent verification. See [capture-index.md](capture-index.md) and [baseline-provenance.md](baseline-provenance.md).

## Complete render

Values are means ± standard deviation of five independent process means, milliseconds. The visible fraction is from the checked validation traces, not inferred from timing.

| Case | Visible fraction | Pre-feature | Candidate disabled | Linear culling |
|---|---:|---:|---:|---:|
| visible-n1-w1 | 1.000 | 0.008597 ± 0.000600 | 0.008271 ± 0.000850 | 0.008924 ± 0.000555 |
| visible-n1000-w1 | 1.000 | 0.985113 ± 0.022941 | 0.976644 ± 0.043765 | 1.003287 ± 0.027674 |
| sparse-n100-w1 | 0.100 | 0.095838 ± 0.009348 | 0.093230 ± 0.001835 | 0.025006 ± 0.000659 |
| sparse-n1000-w1 | 0.100 | 0.879333 ± 0.046346 | 0.866183 ± 0.036766 | 0.176263 ± 0.015954 |
| sparse-n10000-w1 | 0.100 | 8.980451 ± 0.451433 | 8.330235 ± 0.149823 | 1.346039 ± 0.036869 |
| offscreen-n1000-w1 | 0.000 | 0.813896 ± 0.017942 | 0.795877 ± 0.015708 | 0.042656 ± 0.001003 |
| clustered-n1000-w1 | 0.100 | 0.844778 ± 0.017640 | 0.838996 ± 0.036130 | 0.140065 ± 0.003589 |
| camera-pan-n1000-w1 | 0.024 | 0.815259 ± 0.012726 | 0.803069 ± 0.023091 | 0.086354 ± 0.003041 |
| churn-n1000-w1 | 0.500 | 0.915670 ± 0.043514 | 0.919325 ± 0.033550 | 0.541393 ± 0.013114 |
| rotated-n1000-w1 | 0.239 | 8.958735 ± 0.274064 | 8.959084 ± 0.258031 | 2.428779 ± 0.064162 |
| crossing-n1000-w1 | 0.312 | 0.885008 ± 0.025334 | 0.885760 ± 0.042795 | 0.364547 ± 0.012033 |
| crossing-n10000-w1 | 0.310 | 8.868755 ± 0.136919 | 8.791972 ± 0.304106 | 3.420734 ± 0.138800 |
| visible-n1000-w6 | 1.000 | 0.982426 ± 0.025550 | 0.983129 ± 0.022299 | 0.987479 ± 0.009235 |
| sparse-n10000-w6 | 0.100 | 8.828176 ± 0.225061 | 8.284874 ± 0.282503 | 1.353016 ± 0.052178 |
| crossing-n10000-w6 | 0.310 | 8.706040 ± 0.034526 | 8.629485 ± 0.305589 | 3.401173 ± 0.148473 |

## Complete simulation step

Values are means ± standard deviation of five independent process means, milliseconds. Headless cases have no render samples; their complete-step values remain comparable.

| Case | Pre-feature | Candidate disabled | Linear culling |
|---|---:|---:|---:|
| visible-n1-w1 | 0.000332 ± 0.000024 | 0.000316 ± 0.000023 | 0.000338 ± 0.000017 |
| visible-n1000-w1 | 0.000727 ± 0.000108 | 0.000766 ± 0.000319 | 0.000699 ± 0.000062 |
| sparse-n100-w1 | 0.000397 ± 0.000108 | 0.000359 ± 0.000018 | 0.000330 ± 0.000017 |
| sparse-n1000-w1 | 0.000790 ± 0.000324 | 0.000631 ± 0.000216 | 0.000451 ± 0.000082 |
| sparse-n10000-w1 | 0.003304 ± 0.000859 | 0.003039 ± 0.000586 | 0.000886 ± 0.000198 |
| offscreen-n1000-w1 | 0.000640 ± 0.000125 | 0.000530 ± 0.000039 | 0.000348 ± 0.000004 |
| clustered-n1000-w1 | 0.000762 ± 0.000114 | 0.000677 ± 0.000132 | 0.000406 ± 0.000052 |
| camera-pan-n1000-w1 | 0.000798 ± 0.000107 | 0.000673 ± 0.000111 | 0.000551 ± 0.000053 |
| churn-n1000-w1 | 0.007489 ± 0.001816 | 0.007334 ± 0.001088 | 0.005840 ± 0.000456 |
| rotated-n1000-w1 | 0.003052 ± 0.001187 | 0.003304 ± 0.000905 | 0.001447 ± 0.000504 |
| crossing-n1000-w1 | 0.064611 ± 0.003766 | 0.065100 ± 0.003754 | 0.063487 ± 0.002021 |
| crossing-n10000-w1 | 0.982614 ± 0.038134 | 0.799110 ± 0.033966 | 0.769284 ± 0.042840 |
| visible-n1000-w6 | 0.000719 ± 0.000180 | 0.000706 ± 0.000120 | 0.000641 ± 0.000052 |
| sparse-n10000-w6 | 0.003060 ± 0.000762 | 0.003123 ± 0.000843 | 0.000950 ± 0.000288 |
| crossing-n10000-w6 | 0.938568 ± 0.047916 | 0.788047 ± 0.034820 | 0.775450 ± 0.047972 |
| crossing-n1000-w1-headless | 0.062439 ± 0.002457 | 0.061089 ± 0.002137 | 0.062463 ± 0.003716 |
| crossing-n1000-w6-headless | 0.061574 ± 0.002095 | 0.062048 ± 0.002720 | 0.062564 ± 0.002059 |

## Sparse gains and all-visible estimate

| Sparse case | Linear submitted | Render reduction vs pre-feature | Render reduction vs disabled |
|---|---:|---:|---:|
| sparse-n100-w1 | 10% | 73.9% | 73.2% |
| sparse-n1000-w1 | 10% | 80.0% | 79.7% |
| sparse-n10000-w1 | 10% | 85.0% | 83.8% |
| sparse-n10000-w6 | 10% | 84.7% | 83.7% |

For all-visible 1,000 sprites at one worker, the linear mean is +0.018174 ms (+1.85%) vs pre-feature and +0.026643 ms (+2.73%) vs disabled. The independent-process Welch 95% intervals are `[-0.019839, +0.056186]` and `[-0.030020, +0.083306]` ms. At six workers, the estimates are +0.005053 ms (+0.51%) and +0.004351 ms (+0.44%), with intervals `[-0.026178, +0.036285]` and `[-0.023396, +0.032097]` ms. All four estimates are positive; all four intervals include zero. They are potential costs with uncertain magnitude. Overlapping min/max ranges do not establish absence of a regression.

Intervals use five independent process means per mode, not 600 frame samples. Welch degrees of freedom are rounded down to a conservative tabulated 95% Student-t critical value. The driver randomizes mode order; five repetitions still leave host/thermal uncertainty.

## Tail observations

These are means of each process's own p95, p99, and observed maximum, not pooled frame percentiles. The raw per-process arrays are in `timing/final/report.json`.

| Linear case | Render p95 / p99 / max (ms) | Complete-step p95 / p99 / max (ms) |
|---|---:|---:|
| visible-n1000-w1 | 1.1624 / 1.4002 / 1.6908 | 0.001579 / 0.002673 / 0.009109 |
| sparse-n100-w1 | 0.0353 / 0.0425 / 0.0619 | 0.000539 / 0.000827 / 0.002094 |
| sparse-n1000-w1 | 0.2270 / 0.2576 / 0.3259 | 0.000838 / 0.001178 / 0.003354 |
| sparse-n10000-w1 | 1.5355 / 1.6815 / 1.9374 | 0.002160 / 0.003549 / 0.009392 |
| rotated-n1000-w1 | 2.7072 / 2.8775 / 3.5883 | 0.003455 / 0.004919 / 0.007482 |
| crossing-n10000-w6 | 3.9243 / 4.3849 / 5.2477 | 0.921096 / 1.036727 / 1.250405 |
| crossing-n1000-w6-headless | 0 / 0 / 0 | 0.081203 / 0.093126 / 0.129775 |

## Stock-engine controls

Means below are across five process means. Headless controls have no render samples. Stock-runner renderer identity is unverified because its manifest does not query SDL renderer configuration.

| Scenario | Workers | Render mean pre-feature / linear (ms) | Complete-step mean pre-feature / linear (ms) | Linear−baseline render 95% CI (ms) |
|---|---:|---:|---:|---:|
| broad_phase | 1 | 0.205683 / 0.210932 | 0.071707 / 0.069547 | `[-0.009633, +0.020131]` |
| moving_add_and_remove | 1 | 1.230999 / 1.236350 | 0.555970 / 0.538355 | `[-0.019459, +0.030161]` |
| persistent_contacts | 1 | no render samples | 0.006541 / 0.006573 | n/a |
| broad_phase_stress | 1 | no render samples | 2.605907 / 2.628454 | n/a |
| persistent_contacts | 6 | no render samples | 0.006579 / 0.006683 | n/a |
| broad_phase_stress | 6 | no render samples | 2.540149 / 2.544753 | n/a |

The headless focused crossing and stock controls retain `complete_step` separately; their five-run Welch intervals include zero. These controls check integration and physical invariants, not renderer-specific culling speedup.

## Resource behavior and limits

Stable visible/sparse/offscreen/clustered/rotated profiles observed zero C++ `new`/`new[]` calls and bytes. Camera-pan/crossing observed 600 calls and 19,200 bytes per mode from common fixture motion. Churn observed 48,600 calls and 3,369,192 bytes per mode from fixture replacement. No mode-specific allocation difference was observed. Headless render-work counters were zero. The observer excludes aligned new, C allocation, SDL, and driver allocations; RSS is process-wide and cannot attribute culling memory. See [resources.md](resources.md).

## Scope and decision

Sparse complete-render improvements are clearly larger than independent-run variation in this bounded SDL software-renderer matrix. All-visible linear means are positive, but the intervals do not resolve the small effect; this report treats it as potential overhead and does not claim no cost. Culling retains order and uses an O(N) scan with O(1) state. `complete_step` is reported separately from rendering. Results do not establish hardware-renderer/GPU elapsed-time behavior, other machines/platforms, or unmeasured worker/population combinations. SC-005 is supported for the tested mostly-offscreen workload, with the all-visible uncertainty and control limits retained as tradeoffs.
