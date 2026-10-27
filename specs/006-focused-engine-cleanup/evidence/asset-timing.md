# Matched final asset timing

Five independent processes per cell, shuffled seed 6, CPU 0, dummy SDL, identical fixture overlay, Release builds, 100 warmup hits (two warmup churn cycles). Values are loop milliseconds, mean ± sample standard deviation. Raw schedule and output: `timings/asset-warmed-processes.json`. The final candidate includes the internal PathManager signature cleanup. The baseline was built with a temporary matching probe overlay, then the source was restored. Debug-on Release rendering timing remains unverified. This isolates loads and does not establish a whole-engine speedup.

- fixture: `84ffa16b2e233f8ebe0978788b49ee92afe1700b02224c8d51c8b5a721728760`
- baseline_binary: `caf4467ba193e27a3fb6b43f3fce4762c864acb01f813ec5976a53bf7faf46d1`
- candidate_binary: `4b46451fa0bbe7be745d0de8b478774790bd0d262ca7d28040702c595452daa8`

| mode | requests | baseline ms | candidate ms |
| --- | ---: | ---: | ---: |
| asset-hit | 100 | 1.1183 ± 0.0801 | 0.0026 ± 0.0006 |
| asset-hit | 1000 | 10.7880 ± 0.2751 | 0.0246 ± 0.0039 |
| asset-alias | 100 | 1.0906 ± 0.0511 | 0.0024 ± 0.0000 |
| asset-alias | 1000 | 10.7690 ± 0.1667 | 0.0233 ± 0.0008 |
| asset-churn | 10 | 0.4684 ± 0.0331 | 0.5183 ± 0.0390 |
| asset-churn | 100 | 4.5929 ± 0.0931 | 4.8878 ± 0.0830 |
