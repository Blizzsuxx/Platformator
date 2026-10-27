# Matched asset timing

Five independently launched processes per cell, shuffle seed 6, CPU 0, dummy SDL, identical fixture overlay, Release builds and 100 warmup hits (two warmup cycles for churn). Values are loop milliseconds, mean ± sample standard deviation. Raw schedule, output and exact commands are in `timings/asset-warmed-processes.json`; exploratory no-warmup data remains separate. This isolates loads and is not a whole-engine speedup.

- fixture: `84ffa16b2e233f8ebe0978788b49ee92afe1700b02224c8d51c8b5a721728760`
- baseline_binary: `caf4467ba193e27a3fb6b43f3fce4762c864acb01f813ec5976a53bf7faf46d1`
- candidate_binary: `8ffbdc5d1ba30a5b811e562e11133392d119e8cd817e0d8f1344c6be6277f7f5`

| mode | requests | baseline ms | candidate ms |
| --- | ---: | ---: | ---: |
| asset-hit | 100 | 1.0656 ± 0.0356 | 0.0023 ± 0.0000 |
| asset-hit | 1000 | 10.8593 ± 0.5239 | 0.0225 ± 0.0002 |
| asset-alias | 100 | 1.0531 ± 0.0175 | 0.0023 ± 0.0000 |
| asset-alias | 1000 | 10.6277 ± 0.1580 | 0.0230 ± 0.0015 |
| asset-churn | 10 | 0.4594 ± 0.0205 | 0.4836 ± 0.0130 |
| asset-churn | 100 | 4.6287 ± 0.1045 | 4.9159 ± 0.1865 |

The candidate release probe used the current fixture. The baseline binary was rebuilt with a temporary copy of the identical fixture in the frozen source, then the original baseline fixture source was restored; this is a test-only overlay, not a change to the historical snapshot. The debug-tools-on Release timing and real renderer groups were unavailable in these captures and remain unverified. The one-operation per-process timing summaries do not provide a reliable p95.
