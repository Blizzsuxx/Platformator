# Validation and Evidence Contract

## Strategies and compatibility

Extend the existing focused probe with internal `--render-strategy current-linear|cached-linear|indexed`, selected before scene creation. Unknown values fail. Default follows the accepted enabled production path. Existing `--no-render-culling` always selects effective submit-all; headless always has no render state. Record requested strategy and effective mode separately.

- **current-linear:** exact feature-008 ordered scan/prepared view/classifier, no cache/bounds/index notifications or storage.
- **cached-linear:** shared dirty publication, maintained proxies, fallback, and unchanged-result reuse; linear maintained-record traversal on a miss, without grid construction/maintenance. Process in rank order, with no unnecessary sort.
- **indexed:** same cached semantics, plus maintained render grid, side list, candidate refinement, and sorting retained ranks.

Keep all three concrete paths in the current source for fair comparisons; no public runtime strategy framework. Freeze/hash the pre-engine-edit feature-008 runtime, archive the common harness changes separately, and run selected parity/performance controls against it. A small capability adapter may let that baseline accept only `current-linear` and existing submit-all; reject unsupported strategy requests. Do not patch its engine with cache/index features. Historical T044 timings are contextual only, never fresh candidate evidence.

`capture_linear_render.py` supports `--comparison incremental` with `--phase validate|resources|time|pilot`. Preserve its existing comparison/default behavior and schema-v3 recorder readers. For the incremental comparison:

- `validate` invokes matching work-probe binaries and fills checked corresponding-frame output/order/oracle proofs below `--validation-root`.
- `resources` invokes separate resource captures and fills `--resource-root`, including cold/growth/reclaim and stable-capacity observations.
- `time` requires both completed roots and starts no builds/validation concurrently. Reject mismatched/missing/failed proofs before accepting timing.
- `pilot` is an explicitly exploratory, focused-first entry point. It requires `--exploratory`, `--strategy all`, a candidate work-probe validation executable, an uninstrumented candidate timing executable, and a fresh output directory. Cases are selected with `--focused-cases`; the default selection is the five core single-worker cases. It does not require full phase roots, frozen-reference controls, or per-frame image/order dumps. It runs resource captures for all three candidate strategies plus disabled mode, then shuffles and times current-linear/cached-linear/indexed with matching resource-proof checksums. A completed pilot report states its selected scope and cannot claim feature acceptance. Cancellation stops the owned process group and cannot write a success report.

Retain existing baseline/candidate probe, validation-probe, runner, asset-root, workers, dt, warmup, measured-frame, repetition, and output arguments. Phase outputs are fresh directories; refuse overwrites. A proof-kind/version and comparison identifier distinguish new evidence from feature 008 without silently changing old meaning.

## Correctness gate

Use actual corresponding-frame SDL software images, ordered fixture identities (slot plus generation), and an independent double transformed-corner oracle. Do not prove coverage only by comparing two calls to the same proxy/classifier helper. Current-linear and submit-all are complementary references: compare pixels to submit-all and expected conservative selection/order to linear/oracle, allowing explicitly documented conservative extras. All potentially visible sprites must be present, live, duplicate-free, and in authoritative relative order.

Required scenarios:

1. Prepared unchanged visible, empty, sparse, and all-offscreen frames; repeated identical edits; many edits/steps between render calls; paused repeated rendering.
2. Continuous edge/corner crossings and camera pans, including moving into view after earlier rejection. Exercise fractional coordinates, exact edges, near-quadrant rotations, elongated/large sprites, unequal projection and nonuniform object scale.
3. Independent width, height, scale, rotation, texture eligibility changes; animation/source/flip/pixel changes that reuse visibility while drawing current content.
4. Parent/overlapping-subtree movement, reparenting, late callbacks; physics translation-only, rotation-only, combined movement and rounded-away translation, at one and multiple workers; synchronous `Rigidbody::move`.
5. Add/activate/deactivate/remove/destroy/replace; swap-remove first/middle/last; remove queued changes; address/registry-slot reuse; last texture-user destruction; scene clear/load/failure; runtime recreation. Images and order must remain current on each frame.
6. Camera replacement/removal/size changes and actual output resize after an earlier prepared frame; supported mapping to custom target/viewport/scale/logical presentation and back, with mutations/removals during fallback.
7. Numeric certificate boundaries, large common offsets, extreme finite values, non-finite/negative/degenerate dimensions, overflow/subnormal regimes, negative cell coordinates, cell-edge ties, oversized proxies, hash collisions, node reuse, and forced query-stamp wrap.
8. Allocation/growth/reset failure where injectable at the owner boundary: no valid partial selection, stale reference, or broken link. Applicable public header/consumer/package tests and existing physics/lifecycle tests remain passing.

ASan/UBSan covers lifecycle/churn/reset and numeric/index cases. Targeted TSan covers worker-produced motion/owner publication and existing allocation observer. Do not suppress engine races; document tool/platform inability separately and do not call unrun checks passing.

## Affected-work and resource gate

Add validation-only observations for dirty enqueue/cancellation, bounds refreshes, index insertion/removal/relink, grid queries/cell visits/candidate visits, full-population selection visits, order rebuild/sort input, cache reuse, projection rebuild, side-list and numeric/mapping fallback, and capacity/retained bytes. Observe publication/maintenance during simulation as well as render; do not reset away its counts before inspection.

Assertions:

- Unchanged prepared cached frames: zero bounds/index/query/full-population/order work, including unchanged empty/all-offscreen cases. Drawing/submission still occurs normally.
- M affected sprites: only those M refresh once per preparation batch (plus explicit new memberships); no scan to find them. Cover overlapping affected subtrees and simultaneous setter/physics edits.
- Camera-only translation within supported domain: zero bounds refresh/full-index rebuild; candidates/order are derived from spatial selection. Leaving/reentering the certificate domain must not force a bounds rebuild merely for translation.
- Pure order/membership changes invalidate selection and repair only necessary records; no mandatory population-wide order pass.
- Headless/disabled/current-linear modes: zero cache/index creation, initialization, dirty enqueue, revision bookkeeping, maintenance, query, or retained culling capacity. Existing independent engine work is recorded separately.
- Warmed stable-capacity cache/index preparation: zero recurring temporary C++ allocations. Exercise movement into new coordinates, clustered-to-spread layouts, all-change motion, churn and reuse. Reserve actual cell nodes, not just map buckets.
- Stable-population churn and repeated scene/runtime transitions: no continuing retained-state increase, stale handles, retired pointers, or historical cell keys. Explicit scene reset releases old high-water storage. Report cold/growth/reset allocation, bytes and latency separately.

Use existing thread-safe scoped allocation instrumentation and its self-tests. Track culling-owned capacity/allocations separately from fixture/engine allocation and process RSS. Disclose observer limits (aligned/C/SDL/driver allocations); do not claim process-wide zero allocation from a partial observer.

## Focused pilot before acceptance timing

Start with mostly-offscreen static, camera motion, a small moving subset, fully moving crossing, and all-visible workloads at representative populations. Add a selected multiworker crossing/control only when the comparison requires it. Keep warmup and measured steps short initially, retain full schema-v3 raw samples and resource cold/growth/reset observations, and label the result exploratory. Run more repetitions or add cases only to resolve an identified uncertainty. The pilot does not relax corresponding-frame correctness, acceptance repetition counts, or the full matrix below.

## Full acceptance timing matrix

Use seed 0, dt `0.008333333333333333`, 120 warmup steps and 600 measured steps, with at least five independent process repetitions per case/strategy. Shuffle strategy order reproducibly. Record affinity, CPU topology/governor, compiler/flags/dependencies, exact queried renderer/mapping/output/VSync, and workers. Complete all builds/checks first. SDL software output is 320×240 by default; rotated cases retain the unequal 640×160 world-view setup. Output-resize trajectories record their full schedule.

The changed fraction refers to actual unique geometry changes after warmup, verified by work proofs. Extend deterministic fixtures so static layouts really remain unchanged and moving subsets follow continuous trajectories, not teleports. Ensure growth from 10k to 50k includes a spread layout at controlled spatial density, in addition to fixed-area density stress. A larger count piled into the same cells does not alone test sparse-query scaling.

| Profile/layout | Population | Changed fraction / variation | Workers |
|----------------|------------|------------------------------|---------|
| All-visible static | 0, 1, 1,000, 10,000 | 0% | 1 |
| All-visible continuous motion within view | 1, 1,000, 10,000 | 100% | 1 |
| All-offscreen static | 1,000, 10,000, 50,000 | 0% | 1 |
| Sparse static, spread | 1,000, 10,000, 50,000 | 0% | 1 |
| Sparse moving, spread | 1,000, 10,000, 50,000 | 1%, 10%, 100% | 1 |
| Camera pan, stationary spread sprites | 1,000, 10,000, 50,000 | 0% sprites, continuously moving camera | 1 |
| Continuous boundary crossing | 1,000, 10,000, 50,000 | 100% | 1 |
| Clustered/skewed, fixed-area density | 10,000, 50,000 | 10% | 1 |
| Stable-population churn | 1,000, 10,000 | Existing deterministic 1% replacement schedule | 1 |
| Rotated/unequal projection | 1,000, 10,000 | 10% size/rotation or motion changes | 1 |
| Oversized/giant sprite stress | 1,000 | 10%, including all entries in side list | 1 |
| Output/projection changes | 1,000 | Fixed resize schedule after earlier render | 1 |
| Large common coordinate offset | 1,000 | 1% plus pan through certificate exit/resumption | 1 |
| Shipped default/stacking scenes | Resolved loaded populations | Natural deterministic fixed-step motion, recorded change counts | 1 and physical-core count |
| Selected multiworker repeats | As above | visible-1k; sparse-10k/1%; pan-50k; crossing-50k; churn-10k | Physical-core count |
| Headless / explicitly disabled controls | 1,000 | Crossing trajectory; measure each inactive mode | 1 and physical-core count |

Run each rendered row with all three strategies and matching proof/resource keys. Headless/disabled requested-strategy controls verify no strategy accidentally enables state. Preserve selected frozen-008 controls for visible-1k, sparse-10k, crossing-10k, headless and disabled to identify new constant notification/dispatch overhead, with fresh matching source/harness and proofs.

Load `assets/scenes/default.scene` and `assets/scenes/stacking.scene` through the existing runtime loader in bounded focused-harness profiles, using live handles for initial identities and corresponding frames for proof. Record resolved populations and actual changed counts rather than assigning synthetic fractions. No new scene serialization or public loader is needed.

Also run existing rendered `broad_phase` and `moving_add_and_remove`, and headless `persistent_contacts` and `broad_phase_stress`, using existing representative sizes and one/physical-core workers. Compare frozen 008 and normal candidate under equivalent settings. The old stock runner does not expose cached-linear; do not label its default result as that mode. Any added test-only stock strategy adapter must remain internal and explicitly identified.

## Timing boundaries and decision rule

- Retain schema-v3 `render` and independent `complete_step`; do not sum nested physics/cleanup scopes. Render includes view checks, dirty draining, proxy/global refresh, index maintenance done there, query/refinement, sorting, submission, flush/present. Simulation includes any setter/merge/membership notifications or unregister work done there.
- Additionally retain directly measured paired host step-plus-render intervals for this comparison, without replacing/relabeling recorder scopes. Use those intervals and separately reported step/render deltas to judge complete impact; do not hide maintenance by moving it outside a timer.
- Record cold first eligible render, capacity growth and explicit projection rebuild/reclamation separately from warmed samples. Report means, p95/p99/worst frame, per-process means/spread, retained capacity/bytes and allocation observations. Attribute query/candidate/order work through separate proofs, never instrumented timing.
- Use process-level difference intervals (for example Welch 95% intervals on independent run means), not overlapping min/max ranges, to assess variation. Positive differences remain potential regressions even when uncertain. Increase repetitions only to resolve a stated ambiguity; retain prior results.
- SC-006 requires a complete-cost static/cache win over current-linear and an additional indexed win over cached-linear on at least one large sparse-change/pan case. Investigate all-visible, fully dynamic, small, giant, fallback, tail, cold and memory costs before accepting the normal path. Missing index benefit blocks acceptance and prompts design revision; no universal winner claim.

## Provenance and durable bundle

Use a new ignored root `benchmark/artifacts/linear-render-culling/009-<run-id>/`; the existing ignore rule covers it. No required artifact may live only in `/tmp`. Preserve the complete bundle and unfavorable/failed evidence. Do not edit T044 or replace it with new captures.

Every accepted timing run links matching output/resource proofs with the full case key, checksums, fixture/harness/source/input-manifest and binary identities. Add namespaced incremental-comparison metadata; preserve existing schema-v3 scopes/columns and feature-008 metadata meaning. Record build flags/capabilities and reject timing binaries with work probes, sanitizers, or debug tools. Fingerprint all new runtime/fixture/driver/configuration inputs and loaded scene/assets used by proofs. Reject mixed modes, changed fractions, layouts, output trajectories, truncated samples, negative counts, stale proofs and substituted binaries. Mismatch recovery uses matching validation binaries and retains the original error even if image recovery fails.

Retain source snapshots/archives, support-only baseline patch inventory, builds or reconstructable binaries, exact commands, logs, all raw step/render/paired samples, proof images/order/resource observations, independent-run analysis, and checksums. Provide a bundle-relative index, reproduction script, and verification entry point. Verify a copied/relocated bundle with original paths unavailable; merely rewriting absolute links is insufficient. Track only concise evidence summary/index/checksums in the spec directory, not bulk JSON/CSV/images/binaries.
