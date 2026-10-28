# Corrected calibration and frozen profile

Current source: `f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1`.

The separate seed-2026 preset supplies four 1,000-endpoint cases: collision and visibility at 1%/10% changed bounds. Runs use five warmup/40 measured steps, workers 1/6, native-partial/full-rebuild, padding 0/2 and serial/parallel query policy. All [80 validation captures](convergence/calibration/validation/) and [400 timing processes](convergence/calibration/timing/) are eligible and source/case/profile matched, with five fresh processes per timing configuration.

The [frozen profile](convergence/calibration/frozen-profile.json) was recorded before primary evaluation: **native-partial, zero padding, serial queries**. Across 32 matched control pairs, median native/full elapsed replay ratio is 0.706. Padding-2/zero median is 0.995, which does not establish a useful padding improvement. Serial/parallel median is 0.978 overall and 0.944 in the 16 collision-only pairs. Visibility has one camera query, so changing its allowed query policy does not create parallel work; differences there are not evidence of parallel speedup.

These small-batch observations support a reproducible primary control, not a universal optimum, a tuned query crossover or an automatic runtime threshold. Public defaults remain segmented physics and none rendering. Other profiles remain selectable and separately keyed.

Previous calibration reports are preserved in [historical reports](convergence/historical-reports/calibration.md); interim raw captures preserve their source identities under [historical captures](convergence/historical-captures/). Current conclusions use only the matching f1 bundles above.
