# C++ benchmark measurements

Rendering comparisons default to a compact exploratory pilot with a **50 MiB output / 600 second capture budget**. See the [rendering quickstart](../specs/009-incremental-render-culling/quickstart.md). Retain reports, identities, commands and raw timings; keep builds outside artifacts. Full frame traces and exhaustive campaigns require an explicit user request. Other benchmark tools below do **not** inherit these guards: use equivalent external monitoring when running them. See [AGENTS.md](../AGENTS.md) for the repository-wide retention policy.


Configure `cmake --preset cleanup-benchmark`, then build `cmake --build --preset cleanup-benchmark --parallel 4`. Captures require Release without sanitizer, work probes or debug tools. Benchmark scopes themselves are timing instrumentation. Python tooling uses only the standard library except optional matplotlib plotting.

```sh
python3 benchmark/sweep_scaling.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100 --workers 1 --seed 0 \
  --warmup-frames 5 --measure-frames 30 --dt 0.008333333333333333 \
  --repetitions 3 --raw-output-dir /tmp/broad-runs --output /tmp/broad.csv
```

Each repetition starts a new process. On Linux pin the sweep with `taskset -c <CPUs>` so children inherit affinity; record CPU topology/governor, contention and the chosen logical CPUs. Worker control is set once before runtime creation. Seed zero preserves deterministic original layouts. `--warmup-frames` and `--measure-frames` mean simulation **steps**; explicit caller dt is used. `--render` adds independent rendering samples and stops immediately on quit; incomplete captures cannot enter an aggregate.

Use `--validate-invariants --repetitions 1` for separate correctness runs. This mode recomputes contact/support invariants after each step and records final body/contact snapshots. It is explicitly labeled `per-step`, `timing_eligible=false`; its durations must never be presented as uninstrumented performance. Ordinary captures check finite final state, possible support ranges, population and per-step population counters outside measured scopes. Compare serial/multiworker validation snapshots with declared tolerances before accepting timings. General lifecycle/geometry/sleep tests remain separate correctness gates.

## Schema v3 and boundaries

`run.csv` contains all eight scope and eleven counter summaries, units and sample counts. `run.samples.csv` preserves every step duration/counter plus independent rendering rows. `run.manifest.json` records resolved workload, seed/workers/dt, warmup/measurement/repetition parameters, source fingerprint/commit/dirty state, compiler/flags/configuration, instrumentation, machine/OS/dependencies, invariants and scope boundaries. Logs and raw files are retained beside sweep aggregates. Output errors are fatal. Sweeps refuse overwriting prior captures, missing/unknown metrics, invalid invariants, non-Release instrumentation and truncated runs. Plotting accepts only schema-v3 performance aggregates and displays standard deviation across independent run means.

Schema 3 removes the recurring contact metric and counts Enter/Exit **pair events generated within the complete step**, including cleanup, independently of script availability and delivered callback count. Current readers reject schema-2 or mixed captures; historical files remain unchanged and require their recorded tooling revision. Timing comparisons across versions require equivalent workloads/boundaries; the event counters have intentionally changed meaning.

The `persistent_contacts` scenario accepts `--contact-pair-count N --scripted-endpoints 0|1|2` (sweep: `--counts` and `--scripted-endpoints`). It validates disjoint stationary trigger contacts and isolates observer-related event cost. Script-free and scripted runs use identical geometry; the no-op observer overrides only retained callbacks, so the common fixture can be applied to the baseline revision.

- `complete_step`: ordered runtime step through final retirement/transient cleanup, including object destruction. Recorder storage and outer host-call overhead are outside it. Rendering is outside it.
- `physics`: transform synchronization, broad/narrow work, forces/solver/movement and cache publication within the step.
- `cleanup`: final scene/lifecycle reconciliation and retirement; nested in complete_step.
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

Start with a fresh ignored output directory and selected cases. A pilot needs a candidate work-probe validation executable and a separate uninstrumented timing executable; it does not need complete phase roots, frozen-reference runs, or per-frame identity dumps.

```sh
RUN_ID=replace-with-unique-run-id
BUILD_ROOT="$PWD/bin/linear-render-$RUN_ID"
REPORT_ROOT="$PWD/specs/009-incremental-render-culling/evidence/pilots/$RUN_ID"
test ! -e "$REPORT_ROOT/captures/pilot"
SDL_RENDER_DRIVER=software python3 benchmark/capture_linear_render.py \
  --comparison incremental --phase pilot --strategy all --exploratory \
  --focused-cases mostly-offscreen \
  --candidate-validation-probe "$BUILD_ROOT/candidate-validation/platformator_linear_render_probe" \
  --candidate-probe "$BUILD_ROOT/candidate-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/assets" --workers 1 --multiworker-count 6 --seed 0 \
  --repetitions 3 --warmup-frames 5 --measure-frames 30 \
  --dt 0.008333333333333333 --output "$REPORT_ROOT/captures/pilot"
```

Build trees stay under `bin/`; the report path holds only the compact pilot report and timing samples. Use one fresh run ID and do not reuse or replace an output directory after a failure.

The driver writes a report only after every selected resource/timing run validates. It retains raw step/render/paired samples and cold/growth/reset observations. Three short repetitions screen large effects but do not support an acceptance claim. The current pilot summary is `specs/009-incremental-render-culling/evidence/RESULTS.md`.

## Capture limits and retention

The focused exploratory pilot is the default. It uses 3 repetitions, 5 warmup frames, 30 measured frames, a 50 MiB output budget and a 600 second time budget. Stop on a budget or correctness failure and report the partial result; do not automatically retry, expand the workload matrix or retain a full source/build bundle. Larger acceptance campaigns require an explicit request and a separate bounded plan.

Keep builds under `build/` or `bin/`, outside report directories. Retain a compact report, workload/build identities, commands and raw timing samples. Do not retain per-frame sprite identity lists, full-frame images, source archives, executables, shared libraries or build trees by default. Use targeted correctness tests for rendering or resource checks; capture a small bounded diagnostic only for a specific failure.

Historical reports remain historical observations when raw artifacts were deleted. Do not claim their old samples or checksums have been reverified.

## GUI editor capture

The editor probe target is `platformator_editor_gui_probe`; the capture driver is `benchmark/capture_gui.py`. It supports one matched `idle` or `hover` workload and defaults to 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB and 600 seconds. See the current command template and limitations in [GUI Cleanup Quickstart](../specs/012-gui-cleanup/quickstart.md#7-focused-gui-evidence-after-correctness). Do not run a comparison without a behavior-equivalent baseline. The previous GUI binary and deleted raw evidence are not available for re-verification, so comparative acceptance remains open until a valid baseline exists.

Software-renderer results describe CPU-side behavior on that backend; they do not establish hardware-renderer or GPU elapsed-time performance.

The focused sprite profiles include selected `--multiworker-count` cases. The existing rendered `broad_phase` and `moving_add_and_remove` engine controls run at one worker; the headless `persistent_contacts` and `broad_phase_stress` controls run at one worker and at `--multiworker-count`.
