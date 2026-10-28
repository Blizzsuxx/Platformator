# Spatial calibration

Calibration inputs are the separate deterministic preset in
[`calibration/cases/`](calibration/cases/), generated with seed 2026, five warmup
steps and 40 measured steps. The four cases use 1,000 endpoints at 1 and 10
percent changed bounds in each domain. They have distinct calibration IDs and
seeds from evaluation.

The validation bundle tested native-partial/full-rebuild, 0/2 world-unit
padding, serial/parallel query execution and worker allowances 1/6. All 80
requested validation captures passed. The performance bundle contains 400
fresh-process captures (five per requested case/method/worker/profile); all
400 were eligible and paired to validation with identical case, profile,
worker allowance and source fingerprint. Both bundles retain raw samples,
initialization, tails, logs, manifests, pairing and checksums:

- [`calibration/validation/`](calibration/validation/)
- [`calibration/timing/`](calibration/timing/)

Across 32 matched profile pairs, full rebuild averaged 22.8% more replay time
than native-partial (median paired difference 9.9%). Padding 2 averaged 2.1%
more time than padding 0 (median 0.2%). Parallel query execution averaged 4.2%
more time than serial (median 4.0%). These are descriptive differences in
short calibration workloads, not confidence bounds or a universal setting
optimum. Initialization and p95/p99/max data are retained in the per-process
summaries and should be reviewed alongside these averages.

The per-case query comparison was mixed. For collision 1%/10% changed bounds,
parallel was slower in both worker allowances. For visibility 1%/10%, parallel
won two of four worker/case pairs and lost the other two. The tested sizes do
not establish a serial/parallel crossover; they establish that the current
small-work profiles do not show a consistent parallel advantage. Keep
`native-partial`, zero padding, and serial query execution as the frozen
primary evaluation BVH profile. The runtime's preserved public defaults remain
segmented collision broad phase and no render culling; these calibration runs
do not justify changing those defaults.
