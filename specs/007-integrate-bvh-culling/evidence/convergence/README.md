# Corrected BVH/segmented comparison evidence

Historical primary source: `f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1`. Primary BVH profile: native-partial, zero padding, serial query policy. Defaults remain segmented physics and none rendering. The separate final-source linear correction is documented in [linear-correction.md](../linear-correction.md).

Start with [RESULTS.md](../RESULTS.md), [implementation corrections](IMPLEMENTATION.md), [coverage.csv](coverage.csv), [derived comparisons](DERIVED.md), [cold startup](cold-startup.csv), [resources](resources.csv) and [integrity](INTEGRITY.json). Figures are provided as PNG and editable SVG. [Machine context](MACHINE.md) and [final validation](../final-validation.md) state the limits.

The original bundles retain 720 validation, 3,600 timing and 464 resource processes. Every original timing configuration uses five fresh processes. `calibration/`, `engine/`, `replay/`, `controls/` and `startup/` retain immutable cases, command files, logs, per-stage eligibility, raw samples/manifests and checksums. Startup has zero warmup/one measured frame and no resource comparison; calibration is separate from evaluation. Their current qualification limits are listed in [qualification-audit.md](../qualification-audit.md).

`capture.py`, `run_remaining.py`, `make_cold_cases.py`, `verify_edge_cases.py`, `summarize.py`, `plot_results.py` and `verify_integrity.py` retain the capture/derivation procedures. Captures refuse an existing nonempty output directory. For new experiments use the underlying `benchmark/generate_spatial_traces.py` and `benchmark/sweep_spatial.py` with fresh output paths and matching validation; do not rewrite these recorded artifacts or their source identities. The summarizer/integrity scripts intentionally require this frozen source.

Historical report copies and interim captures are explicitly separate in `historical-reports/` and `historical-captures/`. Original pre-convergence evaluation/baseline inputs remain unchanged outside this directory. Current qualification does not borrow their source/profile/renderer identities.
