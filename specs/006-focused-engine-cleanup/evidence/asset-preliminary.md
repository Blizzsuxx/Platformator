# Preliminary asset timings

Five independently launched processes per cell, shuffled seed 6, CPU affinity 0, dummy SDL, matched Release probes; samples and exact commands in `timings/asset-processes.json`. Values are whole-loop milliseconds (mean ± sample standard deviation). Each loop includes the initial asset load and has no separate warmup; these are preliminary and do not satisfy the complete timing contract. No whole-engine speedup is implied.

| mode | requests | baseline ms | candidate ms |
| --- | ---: | ---: | ---: |
| asset-hit | 100 | 1.377 ± 0.264 | 0.210 ± 0.017 |
| asset-hit | 1000 | 11.469 ± 1.536 | 0.244 ± 0.042 |
| asset-alias | 100 | 1.404 ± 0.272 | 0.249 ± 0.028 |
| asset-alias | 1000 | 11.074 ± 0.476 | 0.234 ± 0.032 |
| asset-churn | 10 | 0.648 ± 0.083 | 0.712 ± 0.082 |
| asset-churn | 100 | 4.824 ± 0.342 | 6.266 ± 1.419 |
