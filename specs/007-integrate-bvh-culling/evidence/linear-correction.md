# Ordered linear visibility correction evidence

Final source fingerprint: `a8795fcece092b3dd47b0ddc4b736015b2c4215500f85472ec5ccba309c54025`.
All validation, timing, and resource captures share that fingerprint and qualification identity.
Timing entries are means of five fresh-process means; p95 values are means of per-process p95 values.
These measurements describe the corrected source and do not form a causal before/after ratio against the older source.

## Capture scope

Replay covers 1,000 sprites at 0% and 100% visibility, plus moving and panning 10%-changed traces (10 warmup and 30 measured steps).
Actual engine runs cover static 0% and 100% visibility as cache controls, and moving fixtures with 10% changed sprites at 1% and 100% initial visibility (120 warmup and 600 measured steps).
Every applicable case uses none, linear, BVH, and segmented culling at one and six workers. Each timing configuration has five fresh processes; every resource capture links to its timing identity.
All actual-render validation used the offscreen software renderer and passed exact same-frame pixel and order checks (720 frames per validation run).

| Bundle | Cases | Validation | Timing | Resources |
|---|---:|---:|---:|---:|
| Replay | 4 | 32 | 160 | 32 |
| Live static controls | 2 | 16 | 80 | 16 |
| Live invalidated frames | 2 | 16 | 80 | 16 |

## Actual live invalidated frames

The moving fixture invalidates visibility each measured frame. `index_query` is zero for linear because its single query is fused with the authoritative-order exact-refinement pass. That O(N) pass is charged once to `result_reduce`; no sample is a cache hit.

| Initial visible | Workers | Culling | Render ms | Culling ms | Index query ms | Result reduce ms | Visible candidates | Submitted | Cache hits |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1% | 1 | bvh | 4.222388 | 0.070518 | 0.001000 | 0.000501 | 10.000000 | 10.000000 | 0.000000 |
| 1% | 1 | linear | 4.061815 | 0.054492 | 0.000000 | 0.020171 | 10.000000 | 10.000000 | 0.000000 |
| 1% | 1 | none | 4.995772 | — | — | — | — | 1000.000000 | — |
| 1% | 1 | segmented | 4.388059 | 0.127147 | — | 0.000436 | 10.000000 | 10.000000 | 0.000000 |
| 1% | 6 | bvh | 4.188622 | 0.068936 | 0.001000 | 0.000485 | 10.000000 | 10.000000 | 0.000000 |
| 1% | 6 | linear | 4.148076 | 0.056398 | 0.000000 | 0.021041 | 10.000000 | 10.000000 | 0.000000 |
| 1% | 6 | none | 5.077080 | — | — | — | — | 1000.000000 | — |
| 1% | 6 | segmented | 4.226635 | 0.129870 | — | 0.000422 | 10.000000 | 10.000000 | 0.000000 |
| 100% | 1 | bvh | 5.497424 | 0.197159 | 0.070142 | 0.025451 | 1000.000000 | 1000.000000 | 0.000000 |
| 100% | 1 | linear | 5.387063 | 0.076514 | 0.000000 | 0.030700 | 1000.000000 | 1000.000000 | 0.000000 |
| 100% | 1 | none | 5.373373 | — | — | — | — | 1000.000000 | — |
| 100% | 1 | segmented | 5.599813 | 0.273218 | — | 0.028833 | 1000.000000 | 1000.000000 | 0.000000 |
| 100% | 6 | bvh | 5.445813 | 0.194139 | 0.068420 | 0.025245 | 1000.000000 | 1000.000000 | 0.000000 |
| 100% | 6 | linear | 5.586520 | 0.074675 | 0.000000 | 0.029815 | 1000.000000 | 1000.000000 | 0.000000 |
| 100% | 6 | none | 5.248445 | — | — | — | — | 1000.000000 | — |
| 100% | 6 | segmented | 5.740793 | 0.267975 | — | 0.028875 | 1000.000000 | 1000.000000 | 0.000000 |

## Replay mutation comparisons

The dynamic replay rows isolate recorded visibility maintenance and consumer publication. Unchanged 0% and 100% cases are retained as cache behavior controls.

| Trace | Workers | Culling | Spatial replay ms | Index query ms | Result reduce ms |
|---|---:|---|---:|---:|---:|
| change10-moving | 1 | bvh | 0.107072 | 0.001398 | 0.002142 |
| change10-moving | 1 | linear | 0.040374 | 0.000000 | 0.007464 |
| change10-moving | 1 | none | 0.018969 | — | — |
| change10-moving | 1 | segmented | 0.087584 | — | 0.001905 |
| change10-moving | 6 | bvh | 0.108204 | 0.001374 | 0.002271 |
| change10-moving | 6 | linear | 0.042298 | 0.000000 | 0.007958 |
| change10-moving | 6 | none | 0.019384 | — | — |
| change10-moving | 6 | segmented | 0.090053 | — | 0.002120 |
| change10-pan | 1 | bvh | 0.107050 | 0.001346 | 0.002203 |
| change10-pan | 1 | linear | 0.041445 | 0.000000 | 0.007396 |
| change10-pan | 1 | none | 0.024868 | — | — |
| change10-pan | 1 | segmented | 0.090443 | — | 0.001911 |
| change10-pan | 6 | bvh | 0.098107 | 0.001228 | 0.002144 |
| change10-pan | 6 | linear | 0.043448 | 0.000000 | 0.007618 |
| change10-pan | 6 | none | 0.023173 | — | — |
| change10-pan | 6 | segmented | 0.106196 | — | 0.002118 |

The work-probe regression test confirms invalidated linear frames perform no candidate sort at sparse or full visibility, preserve submission order, exact-check all 1,001 sprites in the full case, and retain unchanged-frame caching. Work probes are disabled in these timing binaries.

Raw process files, manifests, input traces, checksums, and eligibility records are retained locally under `convergence/linear-correction/`. Reproduce with [capture_linear_correction.py](capture_linear_correction.py); regenerate this report and its tables with [summarize_linear_correction.py](summarize_linear_correction.py). Compact ledgers are [linear-correction-coverage.csv](linear-correction-coverage.csv) and [linear-correction-comparisons.csv](linear-correction-comparisons.csv).

## Limits

This is a bounded correction check, not a full rerun of the 21-case replay or 11-case rendered visibility matrices. It covers one 1,000-sprite population, software rendering, and workers 1/6 on this host. No hardware-renderer or cross-machine conclusion follows.
