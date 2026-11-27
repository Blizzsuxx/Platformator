# C++ benchmark measurements

Rendering comparisons default to a compact exploratory pilot with a **50 MiB output / 600 second capture budget**. See the [current rendering quickstart](../../specs/019-sprite-z-ordering/quickstart.md) (and the historical [feature-009 incremental culling quickstart](../../specs/009-incremental-render-culling/quickstart.md) for archived multi-strategy methodology). Retain reports, identities, commands and raw timings; keep builds outside artifacts. Full frame traces and exhaustive campaigns require an explicit user request. Other benchmark tools below do **not** inherit these guards: use equivalent external monitoring when running them. See [AGENTS.md](../../AGENTS.md) for the repository-wide retention policy.


Configure `cmake --preset benchmark`, then build `cmake --build --preset benchmark --parallel 4`. Captures require Release without sanitizer, work probes or debug tools. Benchmark scopes themselves are timing instrumentation. Python tooling uses only the standard library except optional matplotlib plotting.

```sh
python3 engine/benchmarks/sweep_scaling.py --runner build/benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100 --workers 1 --seed 0 \
  --warmup-frames 5 --measure-frames 30 --dt 0.008333333333333333 \
  --repetitions 3 --raw-output-dir /tmp/broad-runs --output /tmp/broad.csv
```

Each repetition starts a new process. On Linux pin the sweep with `taskset -c <CPUs>` so children inherit affinity; record CPU topology/governor, contention and the chosen logical CPUs. Worker control is set once before runtime creation. Seed zero preserves deterministic original layouts. `--warmup-frames` and `--measure-frames` mean simulation **steps**; explicit caller dt is used. `--render` adds independent rendering samples and stops immediately on quit; incomplete captures cannot enter an aggregate.

Use `--validate-invariants --repetitions 1` for separate correctness runs. This mode recomputes contact/support invariants after each step and records final body/contact snapshots. It is explicitly labeled `per-step`, `timing_eligible=false`; its durations must never be presented as uninstrumented performance. Ordinary captures check finite final state, possible support ranges, population and per-step population counters outside measured scopes. Compare serial/multiworker validation snapshots with declared tolerances before accepting timings. General lifecycle/geometry/sleep tests remain separate correctness gates.

## Schema v3 and boundaries

`run.csv` contains scope and counter summaries, units and sample counts. `run.samples.csv` preserves every step duration/counter plus independent rendering rows. `run.manifest.json` records resolved workload, seed/workers/dt, warmup/measurement/repetition parameters, source fingerprint/commit/dirty state, compiler/flags/configuration, instrumentation, machine/OS/dependencies, invariants and scope boundaries. Logs and raw files are retained beside sweep aggregates. Output errors are fatal. Sweeps refuse overwriting prior captures, missing/unknown metrics, invalid invariants, non-Release instrumentation and truncated runs. Plotting accepts only schema-v3 performance aggregates and displays standard deviation across independent run means.

Current captures report eleven scopes and eleven counters. Readers also accept the original eight-scope schema-v3 layout. `plot_scaling.py --include-cleanup` includes the lifecycle scopes when present in the input.

Schema 3 removes the recurring contact metric and counts Enter/Exit **pair events generated within the complete step**, including cleanup, independently of script availability and delivered callback count. Current readers reject schema-2 or mixed captures; historical files remain unchanged and require their recorded tooling revision. Timing comparisons across versions require equivalent workloads/boundaries; the event counters have intentionally changed meaning.

The `persistent_contacts` scenario accepts `--contact-pair-count N --scripted-endpoints 0|1|2` (sweep: `--counts` and `--scripted-endpoints`). It validates disjoint stationary trigger contacts and isolates observer-related event cost. Script-free and scripted runs use identical geometry; the no-op observer overrides only retained callbacks, so the common fixture can be applied to the baseline revision.

- `complete_step`: ordered runtime step through final retirement/transient cleanup, including object destruction. Recorder storage and outer host-call overhead are outside it. Rendering is outside it.
- `physics`: transform synchronization, broad/narrow work, forces/solver/movement and cache publication within the step.
- `cleanup`: final scene/lifecycle reconciliation and retirement; nested in complete_step.
- `step_hook`: the custom simulation-step callback, including object creation and retirement requests in churn workloads. This scope is part of `complete_step`.
- `retirement`: the retirement drain, including destruction callbacks, registry erasure, and deallocation. This scope is part of `cleanup`.
- `deferred_grid_flush`: wall time for grid and cell reconciliation during mutation commit. This scope is part of `cleanup` and covers repeated flushes within a step.
- `scene_transition`: selected scene request preparation/commit, nested in cleanup.
- `render`: actual renderer work measured separately after simulation/cleanup. Headless runs have zero render samples.
- `broad_phase`, `narrow_phase`, `resolve_collisions`: named physics subscopes. Nested scopes must not be added together as total wall time.

Aggregate `*_avg_ms` is the mean of independent run means; `*_run_median_ms`, `*_run_stdev_ms`, `*_run_min_ms`, `*_run_max_ms` describe their spread. `*_p95_ms` is the mean of per-run p95 values, not a pooled percentile or confidence interval. Counters describe workloads, not timings.

## Workloads and historical results

Reference populations are broad lanes 100/500/1000, dense broad boxes 50/150/300, narrow elements 10/100/200 (four colliders per element), rigid bodies 100/500/1000 (equal boxes/circles plus four walls), and both static/moving churn 10/100/1000 per step with 1,000 steady bodies. Headless mode has no camera; rendered mode adds one. Use the resolved parameters in manifests rather than guessing geometry from labels. Odd rigid populations split floor/remaining without dropping a body.

The original [pre-cleanup capture](baselines/pre-cleanup-e01526b/RESULTS.md) is preserved unchanged. Its `legacy_frame` is a partial boundary; `host_step` includes outer call/recording overhead. Neither is identical to schema-v2 `complete_step`. It also flags support-range failures and worker nondeterminism. Corrected captures under `results/cpp-cleanup` establish a new reference after correctness changes; they cannot establish a causal optimization ratio against buggy physics. Existing graphs remain historical artifacts.

The Godot host in [benchmark_case.gd](godot/benchmark_case.gd) samples `Performance.TIME_PROCESS` and `Performance.TIME_PHYSICS_PROCESS` inside its physics-driven capture path, plus engine object/node/collision-pair monitors. These are Godot engine monitor values, not clocks bracketing the C++ complete-step/cleanup boundary; Godot object/node counts also differ from GameObject counts. No equivalent complete-step boundary or synchronized sampling has been established. Present these measurements separately; do not infer a cross-engine speed ratio from similarly named scopes.

The 18-case Linux reference is a historical full campaign, not the default procedure. Do not restart it automatically or retain new source/build bundles. For a current question, choose one relevant workload and a small worker/count comparison, retain compact provenance and raw timing samples, and stop on failure or the time/output budget. Deleted historical raw artifacts remain historical observations and cannot be reverified from report summaries alone.

## Linear render culling evidence

The focused `platformator_linear_render_probe` reuses schema-v3 summaries, raw samples and manifests for deterministic sprite profiles. Build it in Release with debug tools and work probes disabled for timing. Build a separate work-probe configuration for image/order and resource captures; validation data is marked `per-step` and is never timing evidence. Keep the pinned pre-feature source, candidate, fixture, output size, asset root, renderer and trajectory parameters matched.

### Focused exploratory pilot

Start with a fresh ignored output directory and selected cases under the sole production culler (`cached-linear`). A pilot needs a candidate work-probe validation executable and a separate uninstrumented timing executable; it does not need complete phase roots, frozen-reference runs, or per-frame identity dumps. (Retired `indexed` / `current-linear` selectors are rejected by current probes and may only be passed to archived frozen baseline binaries.)

```sh
RUN_ID=replace-with-unique-run-id
BUILD_ROOT="$PWD/build/linear-render-$RUN_ID"
REPORT_ROOT="$PWD/specs/009-incremental-render-culling/evidence/pilots/$RUN_ID"
test ! -e "$REPORT_ROOT/captures/pilot"
SDL_RENDER_DRIVER=software python3 engine/benchmarks/capture_linear_render.py \
  --comparison incremental --phase pilot --strategy cached-linear --exploratory \
  --focused-cases mostly-offscreen \
  --candidate-validation-probe "$BUILD_ROOT/candidate-validation/platformator_linear_render_probe" \
  --candidate-probe "$BUILD_ROOT/candidate-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/engine/benchmarks/assets" --workers 1 --multiworker-count 6 --seed 0 \
  --repetitions 3 --warmup-frames 5 --measure-frames 30 \
  --dt 0.008333333333333333 --output "$REPORT_ROOT/captures/pilot"
```

Build trees stay under `build/`; the report path holds only the compact pilot report and timing samples. Use one fresh run ID and do not reuse or replace an output directory after a failure.

The driver writes a report only after every selected resource/timing run validates. It retains raw step/render/paired samples and cold/growth/reset observations. Three short repetitions screen large effects but do not support an acceptance claim. Historical feature-009 multi-strategy summaries live in `specs/009-incremental-render-culling/evidence/RESULTS.md`; current feature-019 evidence lives under `specs/019-sprite-z-ordering/evidence/`.

### Render Z ordering & culling comparison (`render-order-culling`)

Use `--comparison render-order-culling` with `--focused-cases authored-static,sparse-camera-pan,dense-z-edit` to run the combined compact pilot under `cached-linear` (defaults: 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB output limit, 600 s wall-clock limit). `--baseline-probe` is optional: supply it when comparing `authored-static` and `sparse-camera-pan` against the recorded frozen pre-removal `indexed` baseline, or omit it for candidate-only `cached-linear` runs:

```sh
SDL_RENDER_DRIVER=software python3 engine/benchmarks/capture_linear_render.py \
  --comparison render-order-culling \
  --focused-cases authored-static,sparse-camera-pan,dense-z-edit \
  --baseline-probe "$PWD/build/render-order-baseline/platformator_linear_render_probe" \
  --candidate-validation-probe "$PWD/build/render-order-validation/platformator_linear_render_probe" \
  --candidate-probe "$PWD/build/render-order-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/engine/benchmarks/assets" \
  --workers 1 --multiworker-count 4 --seed 0 \
  --output "$PWD/specs/019-sprite-z-ordering/evidence/<run-id>"
```

- **Effective-policy metadata**: `linear_render_culling` reports `requested_selector`, `resolved_startup_strategy`, `requested_mode`, `headless`, `disabled`, `effective_policy`, `observed_mapping_fallbacks`, `observed_numeric_fallbacks`, and `observed_frame_fallbacks` separately while preserving reader compatibility for schema-3 historical manifests.
- **Timed cases vs. work/resource controls**: `authored-static` (64 visible, Z=0) and `sparse-camera-pan` (10,000 sparse pan, Z=0) run `candidate-cached-linear` (and optional `baseline-indexed` passed *only* to the frozen pre-removal baseline binary and labeled as a combined implementation + policy change; historical pre-removal same-build sparse-pan tradeoff of `indexed` vs `cached-linear` is preserved in `specs/019-sprite-z-ordering/evidence/pre-removal-grid-vs-cached-20261118/report.md`). `dense-z-edit` (10,000 retained 1x1 mixed-Z sprites, alternating clean and peer-crossing 1-edit frames) times `rebuild`, `forced-sort`, `repair` at 1 worker and `rebuild` at `--multiworker-count` under `cached-linear`, separating `clean_frames`, `edit_frames`, and `combined` samples. `rejected-z-edit` and 10,000-sprite `fully-moving-crossing` (`cached-linear`) run only under `--validate-resources` as work/resource controls—never infer timing speedups from work-only controls.
- **Bounded outputs**: Per-frame `.frames.jsonl` traces and `.ppm` images are deleted immediately after validation parity checks so only compact reports (`report.json`, `report.md`), build/workload identities, commands, resource summaries, and raw timing CSVs/samples remain. Historical reports whose raw artifacts were deleted remain historical observations.

## Capture limits and retention

The focused exploratory pilot is the default. It uses 3 repetitions, 5 warmup frames, 30 measured frames, a 50 MiB output budget and a 600 second time budget. Stop on a budget or correctness failure and report the partial result; do not automatically retry, expand the workload matrix or retain a full source/build bundle. Larger acceptance campaigns require an explicit request and a separate bounded plan.

Keep builds under `build/`, outside report directories. Retain a compact report, workload/build identities, commands and raw timing samples. Do not retain per-frame sprite identity lists, full-frame images, source archives, executables, shared libraries or build trees by default. Use targeted correctness tests for rendering or resource checks; capture a small bounded diagnostic only for a specific failure.

Historical reports remain historical observations when raw artifacts were deleted. Do not claim their old samples or checksums have been reverified.

## Box2D v3.2.0 upstream workloads

Platformator's standard runner includes three scenarios based on the [Box2D v3.2.0 Release scene layouts](https://github.com/erincatto/box2d/blob/v3.2.0/shared/benchmarks.c). The Box2D reference executable calls the original scene builders. Each paired capture selects one scenario and inherits CPU affinity. The original Box2D CLI is available as a separate build target called `benchmark`.

| Scenario | Dynamic boxes | Scene details |
| --- | ---: | --- |
| `large_pyramid` | 5,050 | A 100-box-wide pyramid on a single box floor. Sleeping is disabled. |
| `many_pyramids` | 22,000 | A 20 × 20 array of 10-box-wide pyramids. Platformator uses 20 thin box floors with aligned top surfaces in place of Box2D's 20 segments on one body. Sleeping is disabled. |
| `smash` | 9,601 | A heavy projectile moves toward a packed wall of 9,600 initially sleeping boxes in zero gravity. The default 65-step warmup puts the first impact around step 69 inside the measured window. |

Run a scenario in Platformator from the repository root. These three scenarios default to 60 Hz, one worker, and 30 measured steps. Use the runner's standard `--dt`, `--workers`, `--warmup-frames`, and `--measure-frames` options to override them.

```sh
cmake --preset benchmark
cmake --build --preset benchmark --parallel 4 --target platformator_benchmark_runner
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  build/benchmark/platformator_benchmark_runner --scenario large_pyramid \
  --csv-output build/large-pyramid.csv
```

For a paired capture, reuse the pinned Box2D checkout if it already exists.

```sh
git clone --depth 1 --branch v3.2.0 https://github.com/erincatto/box2d.git build/box2d-v3.2.0-src
cmake -S engine/benchmarks/box2d/upstream -B build/box2d-upstream-reference -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/box2d-upstream-reference --parallel 4 --target box2d_upstream_reference_runner
python3 engine/benchmarks/capture_box2d_upstream.py \
  --platformator build/benchmark/platformator_benchmark_runner \
  --box2d build/box2d-upstream-reference/box2d_upstream_reference_runner \
  --scenario large_pyramid --output build/box2d-large-pyramid-pilot
```

The driver uses 3 fresh processes per engine, 5 warmup steps (65 for `smash`), 30 measured steps, a 600-second timeout and a 50 MiB output budget. `report.json` records run medians, executable hashes and commands. The Box2D run JSON files hold raw nanosecond samples. Platformator writes its usual summary CSV, raw samples CSV, and manifest. Both include body, shape and awake counts at measurement boundaries. The coordinator stops on the first failed run and leaves its partial files in place.

Both runners advance at 60 Hz. Platformator uses 9.81 world units per meter and reflects the Y axis to match the upstream gravity and geometry. The driver compares Platformator's standard `complete_step` scope with elapsed time around `b2World_Step`. Box2D uses its native scheduler, four solver substeps and continuous collision. Platformator uses its native discrete solver and includes runtime bookkeeping. Interpret timings alongside the scene state and solver settings because contact and sleep behavior can diverge. The [original single-worker pilot](results/box2d-v3.2.0-large-pyramid/report.json) used an outer timer around Platformator's `Runtime::simulateFrame` call.

The other upstream scenes depend on features beyond these ports. `joint_grid`, `rain`, `spinner`, `tumbler`, `washer` and `sleep` exercise joints or motors. `compounds` and `junkyard` require polygon compounds or additional shapes. `queries`, `tree_cast` and `tile_world` exercise shape queries or Box2D's dynamic tree directly. The native `benchmark --list` lists these workloads.

## GUI editor capture

The editor probe target is `platformator_editor_gui_probe`; the capture driver is `editor/benchmarks/capture_gui.py`. It supports one matched `idle` or `hover` workload and defaults to 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB and 600 seconds. See the current command template and limitations in [GUI Cleanup Quickstart](../../specs/012-gui-cleanup/quickstart.md#7-focused-gui-evidence-after-correctness). Do not run a comparison without a behavior-equivalent baseline. The previous GUI binary and deleted raw evidence are not available for re-verification, so comparative acceptance remains open until a valid baseline exists.

Software-renderer results describe CPU-side behavior on that backend; they do not establish hardware-renderer or GPU elapsed-time performance.

The focused sprite profiles include selected `--multiworker-count` cases. The existing rendered `broad_phase` and `moving_add_and_remove` engine controls run at one worker; the headless `persistent_contacts` and `broad_phase_stress` controls run at one worker and at `--multiworker-count`.
