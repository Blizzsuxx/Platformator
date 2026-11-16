# Recorded pre-cleanup results

Captured 2026-10-21T17:06:12.958426+00:00 through 2026-10-21T17:14:53.960051+00:00 on AMD Ryzen 5 7535HS with Radeon Graphics. Source commit `e01526b00f0b3552cccc884887f9281fbaa1a104`.

**180 runs / 36 conditions / 108,000 measured steps.** Every table entry is the median of five per-process mean full-step times. Brackets give the minimum and maximum of those five means, in milliseconds. Each process used 120 warmup steps, 600 measured steps and dt=1/120 second. See [README.md](README.md) for timing boundaries and reproduction commands.

| Workload | Parameter | One thread: median [min, max] ms | Six threads: median [min, max] ms |
| --- | ---: | ---: | ---: |
| `broad_phase` | 100 | 0.0492 [0.0460, 0.0735] | 0.0355 [0.0319, 0.0384] |
| `broad_phase` | 500 | 0.2589 [0.2505, 0.2680] | 0.1220 [0.1158, 0.1234] |
| `broad_phase` | 1000 | 0.5632 [0.5520, 0.5922] | 0.2158 [0.2001, 0.2211] |
| `broad_phase_stress` | 50 | 0.1410 [0.1349, 0.1471] | 0.1247 [0.1204, 0.1290] |
| `broad_phase_stress` | 150 | 0.8703 [0.8603, 0.9222] | 0.8002 [0.7735, 0.8170] |
| `broad_phase_stress` | 300 | 2.6537 [2.5678, 2.7091] | 2.5493 [2.4425, 2.6339] |
| `narrow_phase` | 10 | 0.0722 [0.0703, 0.0778] | 0.0416 [0.0378, 0.0446] |
| `narrow_phase` | 100 | 10.3531 [10.1447, 11.3304] | 3.1362 [2.7535, 3.2971] |
| `narrow_phase` | 200 | 50.3892 [47.3092, 51.9079] | 17.8075 [16.7884, 18.4816] |
| `rigid_body_container` | 100 | 0.0770 [0.0763, 0.0838] | 0.0670 [0.0662, 0.0711] |
| `rigid_body_container` | 500 | 0.6654 [0.6333, 0.7008] | 0.4816 [0.4516, 0.5067] |
| `rigid_body_container` | 1000 | 1.7451 [1.7189, 2.0300] | 1.2601 [1.2400, 1.3395] |
| `add_and_remove` | 10 | 0.0437 [0.0420, 0.0466] | 0.0382 [0.0360, 0.0413] |
| `add_and_remove` | 100 | 0.3142 [0.3047, 0.3461] | 0.1597 [0.1533, 0.2039] |
| `add_and_remove` | 1000 | 2.7268 [2.4543, 2.9696] | 1.1381 [1.1205, 1.1606] |
| `moving_add_and_remove` | 10 | 0.5334 [0.5254, 0.5527] | 0.2283 [0.2252, 0.2604] |
| `moving_add_and_remove` | 100 | 0.7344 [0.7310, 0.7854] | 0.3003 [0.2894, 0.3156] |
| `moving_add_and_remove` | 1000 | 3.3578 [3.1078, 3.5871] | 1.3225 [1.2952, 1.4099] |

For churn workloads, the parameter is removals plus the same number of creations per step, with 1,000 live bodies. Narrow-phase populations contain four times the parameter. Rigid-body populations also include four walls.

## Correctness observations

All 180 processes completed successfully, retained their expected populations, produced complete timing outputs and ended with finite transforms/velocities. This verifies capture integrity, not engine correctness.

**Support accounting failed its range check in 3 measured run(s).** These results are retained and flagged, without rerunning them to obtain passing values.

- `narrow_phase-small-n10-t6-r5`: maximum reported support count 51, 1 invalid body/bodies. See its archived final snapshot and manifest.
- `narrow_phase-small-n10-t6-r4`: maximum reported support count 61, 2 invalid body/bodies. See its archived final snapshot and manifest.
- `narrow_phase-small-n10-t6-r1`: maximum reported support count 43, 1 invalid body/bodies. See its archived final snapshot and manifest.

The initial range check stopped orchestration after the first such capture. The same binary/settings and original completed captures were retained; execution resumed at the next job. Both orchestration logs and script revision fingerprints are preserved. The range check remains an explicit failure.

9 of 36 conditions had different exact final-state fingerprints across repetitions. Variation occurred in all six rigid-body conditions (including serial repetitions) and all three six-thread narrow-phase conditions. The largest final-position difference from the corresponding first serial repetition was 333.249 scene units in the 500-body container; this is substantial state divergence. `results/state-comparison.csv` reports position/velocity, support and sleeping differences per run. These observations do not isolate the cause of divergence. Correctness changes may legitimately change benchmark work, so timings alone cannot establish serial/parallel equivalence.

## Original timer versus complete public step

The original `frame` scope omits post-step collider/grid reconciliation, transient cleanup and deferred destruction. The following large-case values are medians of the five six-thread run means; these are different timing boundaries, not interchangeable performance claims.

| Workload (large case, six threads) | Existing frame ms | Full public step ms |
| --- | ---: | ---: |
| `broad_phase` | 0.0391 | 0.2158 |
| `broad_phase_stress` | 2.5483 | 2.5493 |
| `narrow_phase` | 13.1574 | 17.8075 |
| `rigid_body_container` | 1.0870 | 1.2601 |
| `add_and_remove` | 1.0220 | 1.1381 |
| `moving_add_and_remove` | 1.0629 | 1.3225 |

Raw per-step samples, original recorder CSVs, final-state snapshots, per-run manifests and logs are preserved in `results/raw-runs.tar.gz`. [summary.csv](results/summary.csv) contains all condition aggregates; [runs.csv](results/runs.csv) contains all per-process results.
