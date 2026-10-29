# T044 Performance Investigation

## Sparse workloads

Across five fresh processes per mode, linear mode submits 10% of the sprites and lowers the complete-render mean by 73.9%, 80.0%, and 85.0% at 100, 1,000, and 10,000 sprites with one worker. At 10,000 sprites and six workers, the reduction is 84.7%. The process-mean Welch 95% intervals for sparse linear-minus-reference render time are below zero against both pre-feature and candidate-disabled modes in all four sparse cases. These differences are large relative to the independent-run spread.

The offscreen, clustered, camera-pan, churn, rotated, and crossing cases also reduce the submitted fraction and render mean while retaining identical pixels/order and the independent visibility result. [RESULTS.md](RESULTS.md) gives the per-case means, spread, and tails.

## All-visible estimate and uncertainty

The all-visible 1,000-sprite one-worker means were 0.985113 ms pre-feature, 0.976644 ms candidate-disabled, and 1.003287 ms linear. The linear estimate is +0.018174 ms (+1.85%) versus pre-feature and +0.026643 ms (+2.73%) versus disabled. Conservative Welch 95% intervals over the five independent process means are `[-0.019839, +0.056186]` ms and `[-0.030020, +0.083306]` ms.

At six workers, means were 0.982426 ms pre-feature, 0.983129 ms disabled, and 0.987479 ms linear. The linear estimates are +0.005053 ms (+0.51%) versus pre-feature and +0.004351 ms (+0.44%) versus disabled; the corresponding intervals are `[-0.026178, +0.036285]` ms and `[-0.023396, +0.032097]` ms. The single-visible-sprite intervals also include zero.

Every all-visible mean estimate is positive, but five process repetitions do not distinguish those small costs from zero at the 95% level. The ranges of independent-run means overlap in these cases; that overlap is not used as evidence that a regression is absent. The estimate remains a potential enabled-path cost and is reported alongside the sparse benefit, rather than being called either free or conclusively repeatable. The implementation prepares the render view once per frame and then performs per-sprite eligibility/bounds work in the ordered O(N) scan, which is the expected source of added work when all sprites remain visible.

The intervals use independent process means, not 600 frame samples. Degrees of freedom use a conservative lower-tabulated Student-t critical value. The driver shuffles modes within repetition, but five processes cannot eliminate host/thermal drift.

## Headless and stock-engine controls

The focused headless crossing controls have zero render samples and zero view/eligibility/visibility/submission work. At one worker, linear-minus-pre-feature complete-step mean was +0.000024 ms with 95% interval `[-0.004851, +0.004899]`; at six workers it was +0.000989 ms with interval `[-0.002117, +0.004095]`. Their candidate-disabled comparisons also include zero.

The stock `broad_phase` render means were 0.205683 ms pre-feature and 0.210932 ms linear; their +0.005249 ms mean difference has 95% interval `[-0.009633, +0.020131]`. `moving_add_and_remove` means were 1.230999 and 1.236350 ms; +0.005351 ms has interval `[-0.019459, +0.030161]`. Headless `persistent_contacts` and `broad_phase_stress` complete-step intervals include zero at one and six workers. These controls are integration checks, not independent evidence of culling speedup. Their stock-runner manifests omit renderer identity; focused probes confirm the SDL software renderer.

## Bounds of the result

Timing separates `render` from `complete_step`; nested scopes are not summed. Per-run p95/p99 and observed maxima are reported as means of each process's own tails, not pooled frame percentiles. Resource checks use a separate work-probe build, while timing is Release with work probes/debug tools/sanitizer off. The actual backend is SDL software rendering; these CPU-side results do not establish hardware GPU elapsed time, other host behavior, or unmeasured workloads. The strict capture and uncertainty calculations are retained under the ignored bundle's `timing/final/` and `provenance/performance-analysis.json`.
