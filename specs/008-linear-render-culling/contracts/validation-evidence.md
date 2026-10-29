# Validation and Evidence Contract

## Correctness before timing

New CTest executables are `platformator_render_culling_geometry_test` and `platformator_render_culling_test`. Use existing dummy video/audio and software rendering setup; output readback occurs before presentation. The geometry executable runs without work probes. Integration image comparisons run in ordinary test builds where possible; submission traces, readback through the window's presentation seam, and headless-work assertions require a work-probe build and must never silently pass when that instrumentation is absent. Report skipped instrumentation portions explicitly, with a separate work-probe run required for acceptance.

Use a compile-time-only observation point before presentation and at actual sprite submission, with trace storage reserved by validation code. Ordinary production/timing builds contain no trace buffer, readback callback, observer branch, or per-sprite telemetry. A scoped observer only reads output and is removed before runtime teardown. Frame failures must propagate as test/capture failures.

The validation-only allocation observer is process-wide and atomic. Its scoped epoch includes ordinary `new`/`new[]` calls from any thread during a measured step, including oneTBB workers; it excludes aligned new, C allocation, SDL, and driver allocations. Overlapping observation scopes are unsupported and terminate rather than mixing snapshots. An exception leaving a scope must still close observation. The concurrency and exceptional-exit check runs only in a work-probe build.

Compare two independently constructed runtimes with identical fixture slot IDs, state updates, camera/output, and renderer: one submit-all, one linear. Normalize readback format and compare pixels exactly on the same deterministic backend. Store per-frame image hashes and retain mismatch images; keep representative passing raw images as audit evidence. Compare ordered submitted identities to the reference sequence filtered by the independently calculated conservative visibility oracle, allowing documented numerical false positives but no missing visible sprite, duplicates, or order inversions. Raw pointers/IDs allocated differently between runs cannot be used as equivalence keys.

Mandatory cases:

| Dimension | Coverage and observable result |
|-----------|--------------------------------|
| Membership | Empty, inactive, all-visible, all-offscreen, mixed; expected reference membership and zero all-offscreen draws for classifiable padded bounds. |
| Image/order | Overlapping translucent textured sprites; preserve compositing and retained sequence, not just sprite counts. |
| Bounds | Every edge/corner, partial visibility, oversized and elongated sprites, outside centers with visible corners, fractional coordinates and near-boundary float values. |
| Transforms | Zero/quarter/arbitrary/near-quadrant rotation, nonuniform scale, unequal camera/output scale, hierarchy movement, supported negative scale/degenerate input fallbacks. |
| Drawing inputs | Source rectangles, flips, texture/dimension changes and missing textures; match current behavior. |
| Dynamics | All fixture sprites move continuously across view boundaries with varied phases; verify crossings and exact corresponding-frame output. Camera-only pan, camera size/replacement, output resize, and repeated unchanged frames. |
| Lifecycle | Activation, insert/remove, swap-removal order, component replacement, destruction, scene clear/load, repeated runtime creation. |
| Renderer state | Missing camera/renderer, zero/invalid classifier output, custom viewport/target/scale/logical mapping; conservative fallback and next-frame restoration. |
| Isolation | Zero headless view preparations/checks/submissions; unchanged simulation results and existing debug overlay/control behavior. |

Use an independent double-precision transformed-corner oracle for geometric cases. Do not call the production bounds helper to calculate expected answers. Signed/invalid cases verify fallback instead of assigning new SDL semantics. Include fixed-seed randomized valid geometry and extreme magnitudes. The oracle's exact geometric rectangle need not equal the production padded bounds: compare containment/rejection safety and declared false positives.

## Shared fixture and focused probe

`tests/render_culling_fixture.h` owns a bounded deterministic sprite-only workload used by tests and `benchmark/linear_render_probe.cpp`. It uses real textures, current Runtime APIs, and no colliders. Positions/rotations are deterministic functions of seed, slot, and fixed step; do not depend on wall time or CPU completion order. Updates happen through the normal pre-render/simulation callback path and are identically included in complete_step for every mode.

Focused probe command shape (new commands to implement, not existing capabilities):

```text
platformator_linear_render_probe
  --profile visible|offscreen|sparse|clustered|crossing|camera-pan|churn|rotated
  --sprite-count N --workers N --seed N
  --warmup-frames N --measure-frames N --dt SECONDS
  --csv-output PATH [--repetitions N --run-index N]
  [--no-render-culling] [--headless] [--asset-root PATH]
  [--validation-proof PATH] [--resource-proof PATH]
  [--validate-rendering | --validate-resources] [--evidence-output PATH]
```

Positive population/workers/measurement/dt, nonnegative warmup/index/seed, finite dt, known profile, and a fresh output path are required. `run-index` must be less than repetitions. Correctness tests handle zero population; the timing probe need not accept it. `--validate-rendering` requires work probes, captures images/traces/counters for comparison, and labels output `validation_mode=per-step`, `timing_eligible=false`. `--validate-resources` also requires work probes, uses the same fixture/mode parameters, and records warmed allocation counts and retained-memory observations under `--evidence-output`; its output is ineligible for timing. It is mutually exclusive with `--validate-rendering` so trace/readback allocation cannot contaminate resource observations. Scoped C++ allocation interception is compiled into validation probes only; explicitly report its C/driver coverage limits. Readback and trace work are never accepted as timing evidence. `--validation-proof PATH` identifies already checked matching validation metadata, whose checksum is added to timing provenance outside measured work; the capture driver supplies it for accepted comparisons. Smoke captures without it are not acceptance evidence. In headless mode no textures need be loaded; create equivalent non-rendering fixture objects and apply the same deterministic transforms.

`--resource-proof PATH` similarly identifies a separately checked resource manifest for an accepted timing case and stores its checksum outside measured scopes. It is required by the capture driver; it does not make a resource capture itself timing-eligible. Headless timing manifests have zero render samples and retain the `complete_step` samples.

Profiles have fixed documented parameters, recorded in manifests:

- `visible`: all sprites entirely within the camera; unchanged frames.
- `offscreen`: every sprite's padded bounds fully outside; unchanged frames.
- `sparse`: approximately 10% inside, others clearly outside, distributed uniformly across a larger area.
- `clustered`: same approximate visible fraction concentrated in a few groups to exercise skew without changing algorithmic work.
- `crossing`: all sprites move smoothly with different phases across horizontal and vertical boundaries; use continuous periodic trajectories with a 240-step period at the reference dt, so 600 measured steps include repeated entries/exits. No teleport/wrap updates. Record crossing counts during separate validation and require both entry and exit events.
- `camera-pan`: stationary sprites, continuous camera trajectory across a larger scene, with repeated entries/exits.
- `churn`: fixed population with 1% of slots replaced each step; reuse logical slot identities with generation labels for trace safety. Use a minimum of one replacement for tiny fixtures.
- `rotated`: mostly-offscreen mixed angles and unequal camera X/Y output scales; include zero and nonzero rotations and elongated sprites.

Use the same initial state, total warmup, phase, texture, output dimensions, and update sequence for every compared mode. Setup/texture upload is outside timing; all-visible geometry remains fixed as population changes.

## Bounded comparison matrix

Run this non-Cartesian set first; add cases only to resolve a failure or unexplained result:

| Workload | Sprite counts | Workers |
|----------|---------------|---------|
| visible | 1, 1,000 | 1 |
| sparse | 100, 1,000, 10,000 | 1 |
| offscreen, clustered, camera-pan, churn, rotated | 1,000 each | 1 |
| crossing | 1,000, 10,000 | 1 |
| visible 1,000; sparse 10,000; crossing 10,000 | as shown | available physical-core count |
| headless crossing fixture | 1,000 | 1 and available physical-core count |
| existing headless persistent_contacts and broad_phase_stress | 100 pairs, 300 bodies respectively | 1 and available physical-core count |

If only one physical core is available, record that limit instead of claiming multiworker coverage. Record actual allowed affinity/core topology and do not hardcode the old six-core CPU list. The sprite-only worker comparison checks integration, not culling parallel speedup.

Each case compares pre-feature submit-all, candidate disabled, and candidate enabled with five fresh-process repetitions, shuffled order, fixed seed zero, 120 warmup/600 measured steps, and dt `0.008333333333333333`. Increase duration/repetitions only when variability prevents a reliable conclusion. Keep representative engine rendered controls using existing broad_phase and moving_add_and_remove scenarios at their existing small/default populations, one worker, to check integration outside the synthetic fixture; record actual visibility fraction separately.

## Pre-feature reference

Use source `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75` in an isolated source tree. Apply only common fixture/probe/build registration, provenance coverage, and work-probe-only observation support needed to make identical captures. Record this common harness patch and its hash, and the resulting actual source fingerprint. Do not carry culling, camera preparation, state queries, settings changes, or other candidate runtime changes into the reference. Instrumentation is compiled out for all timing builds.

The common probe uses a compile-time capability check for `WindowSettings::enableRenderCulling`: the old runtime supports reference mode only; requesting linear mode there is an error. Identical fixture/probe contents and equivalent compiler/dependency/options are required across builds. Candidate enabled and disabled use the same binary. Candidate-disabled versus pre-feature measures common-path cost; candidate-enabled versus disabled isolates filtering; enabled versus pre-feature measures total benefit.

Do not overwrite historical captures or treat the earlier BVH branch's numbers as this reference. Store baseline and candidate binaries/source/harness hashes with evidence. Baseline capture must precede candidate engine edits, or use the isolated pinned source when captured later.

## Artifact format and timing acceptance

Reuse existing schema-v3 summary CSV, raw samples CSV, and manifest. Keep numeric scopes/counters and their meanings unchanged. `linear_render_culling` manifest metadata includes:

- `mode`: `pre-feature`, `disabled`, or `linear`; requested setting and effective mapping policy;
- profile, population, fixture/harness SHA-256, resolved trajectory/layout/rotation/churn parameters;
- actual SDL renderer name, output/camera dimensions, render scale/mapping eligibility, VSync setting, and environment/affinity;
- validation evidence path/checksum for the same case parameters and separate resource-observation identity.

Use existing build/machine/dependency/source provenance and record any additions the current machine metadata lacks. The capture driver accepts explicit baseline/candidate stock-runner paths for the engine controls, separate baseline/candidate work-probe executables for mismatch-image recovery, and a resource-evidence root. It runs and records those controls rather than leaving them as an unstated manual step. Before recovery, it checks that the validation executable has work probes enabled and that its source, fixture, and harness identities match the checked proofs. On the old stock runner omit the new opt-out flag, which does not exist there; use it only for the candidate disabled mode. CMake's feature fingerprint must include the focused probe/shared fixture; baseline captures need equivalent coverage or explicit actual file hashes. The focused Python driver reuses `sweep_scaling.parse_benchmark_csv` and additionally rejects:

- wrong mode/profile/parameters/worker/seed/dt/warmup or missing feature/harness/renderer provenance;
- mixed candidate fingerprints, different harnesses, incompatible renderer/output, missing correctness proof, invalid resources, or insufficient independent repetitions;
- non-Release, debug/work/sanitizer instrumentation in timing data, incomplete/truncated/quit runs, and expected render-sample count mismatches;
- validation data labeled or aggregated as timing, reused output paths, and output write failures.

Submission counts/order come from matching validation captures; do not instrument every timed sprite to obtain them. Separate resource checks assess warmed repeated frames at stable population and churn-retained memory. Distinguish allocations attributable to culling from SDL/driver or fixture allocations; state whether probes cover C++ allocations, C allocations, or both. Culling's own retained state must remain constant with N.

Report complete rendering time (prepare/filter/draw/flush/present), complete_step separately, means/medians and spread across independent run means, per-run p95/p99, observed maximum, actual submitted fraction from validation, and memory/allocation observations. If VSync or queued GPU work limits interpretation, state the boundary rather than claim GPU elapsed time. Do not sum nested scopes.

SC-005 requires a representative mostly-offscreen complete-render improvement distinguishable from observed variation. Investigate regressions outside that variation, including all-visible and headless controls; retain unfavorable results. If the benefit cannot be established, record that outcome and revisit implementation before marking the feature accepted.
