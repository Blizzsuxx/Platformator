# C++ benchmark measurements

Configure `cmake --preset cleanup-benchmark`, then build `cmake --build --preset cleanup-benchmark --parallel 4`. Captures require Release without sanitizer, work probes or debug tools. Benchmark scopes themselves are timing instrumentation. Python tooling uses only the standard library except optional matplotlib plotting.

```sh
python3 benchmark/sweep_scaling.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100,500,1000 --workers 1 --seed 0 \
  --warmup-frames 120 --measure-frames 600 --dt 0.008333333333333333 \
  --repetitions 5 --raw-output-dir /tmp/broad-runs --output /tmp/broad.csv
python3 benchmark/plot_scaling.py --input /tmp/broad.csv --output /tmp/broad.png
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

To reproduce the full Linux reference on the recorded six-core host, use `capture_cleanup.py --mode validate --runner bin/cleanup-benchmark/platformator_benchmark_runner --output <new-directory>`, then the same command with `--mode time` after all builds/tests finish. It pins one worker to CPU 0 and six workers to CPUs 0,2,4,6,8,10, compares all 18 serial/parallel workload sizes, shuffles 180 timing processes with seed 1, retains compressed raw runs/validation snapshots, and writes summary, provenance, a source snapshot and checksums. It refuses existing captures. Choose a new output directory for future task/reference comparisons.

## Linear render culling evidence

The focused `platformator_linear_render_probe` reuses schema-v3 summaries, raw samples and manifests for deterministic sprite profiles. Build it in Release with debug tools and work probes disabled for timing. Build a separate work-probe configuration for image/order and resource captures; validation data is marked `per-step` and is never timing evidence. Keep the pinned pre-feature source, candidate, fixture, output size, asset root, renderer and trajectory parameters matched.

### Focused exploratory pilot

Start with a fresh ignored output directory and selected cases. A pilot needs a candidate work-probe validation executable and a separate uninstrumented timing executable; it does not need complete phase roots, frozen-reference runs, or per-frame identity dumps.

```sh
RUN_ROOT=benchmark/artifacts/linear-render-culling/<new-run-id>
test ! -e "$RUN_ROOT/captures/pilot"
SDL_RENDER_DRIVER=software python3 benchmark/capture_linear_render.py \
  --comparison incremental --phase pilot --strategy all --exploratory \
  --focused-cases mostly-offscreen,camera-motion,small-moving-subset,fully-moving-crossing,all-visible,crossing-multiworker \
  --candidate-validation-probe "$RUN_ROOT/candidate/build-validation/platformator_linear_render_probe" \
  --candidate-probe "$RUN_ROOT/candidate/build-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/assets" --workers 1 --multiworker-count 6 --seed 0 \
  --repetitions 3 --warmup-frames 5 --measure-frames 30 \
  --dt 0.008333333333333333 --output "$RUN_ROOT/captures/pilot"
```

The driver writes a report only after every selected resource/timing run validates. It retains raw step/render/paired samples and cold/growth/reset observations. Three short repetitions screen large effects but do not support an acceptance claim. The current pilot summary is `specs/009-incremental-render-culling/evidence/RESULTS.md`.

### Full acceptance campaign

The full acceptance path first populates separate validation and resource roots with each required `(profile, population, workers, headless, mode)` case. Render validation runs use `--validate-rendering --evidence-output`; resource runs use the mutually exclusive `--validate-resources --evidence-output`. The probe writes per-step traces and oracle visibility, image hashes and a representative image where rendering is enabled, or allocation/work/RSS observations for resource checks. C++ allocation interception does not attribute C allocations or SDL/driver allocations. The inventory below remains the feature acceptance scope; the pilot does not complete it.

After those roots pass comparison, run the bounded matrix with a fresh output directory:

```sh
RUN_ROOT=benchmark/artifacts/linear-render-culling/<new-run-id>
mkdir -p "$RUN_ROOT"
git check-ignore -v "$RUN_ROOT"  # verify the durable artifact root is ignored before capture

python3 benchmark/capture_linear_render.py \
  --baseline-probe /path/to/pinned/platformator_linear_render_probe \
  --candidate-probe bin/cleanup-benchmark/platformator_linear_render_probe \
  --baseline-validation-probe /path/to/pinned/work-probe/platformator_linear_render_probe \
  --candidate-validation-probe /path/to/candidate/work-probe/platformator_linear_render_probe \
  --baseline-runner /path/to/pinned/platformator_benchmark_runner \
  --candidate-runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --validation-root /path/to/final-validation \
  --resource-root /path/to/final-resources \
  --asset-root /path/to/shared/assets \
  --workers 1 --multiworker-count 6 \
  --repetitions 5 --warmup-frames 120 --measure-frames 600 \
  --dt 0.008333333333333333 \
  --output "$RUN_ROOT/timing/final"
```

Keep the source snapshots, builds, validation/resource roots, timing outputs, commands, logs, and checksums below this persistent ignored root. Do not use a temporary directory for any input or artifact required to verify or reproduce the comparison.

The driver shuffles comparison modes for each fresh process, retains commands/logs/manifests/raw samples and validates each timing run against its matching proof checksum and source fingerprint. Reports keep complete rendering separate from `complete_step`, include independent-run spread and per-run tails, and record observed submission fractions and resource observations. A mostly-offscreen win is accepted only when the complete-render improvement exceeds run variation and visible, rotated, headless and engine-control regressions have been investigated. Software-renderer results describe CPU-side behavior on that backend; they do not establish hardware-renderer or GPU elapsed-time performance.

The focused sprite profiles include selected `--multiworker-count` cases. The existing rendered `broad_phase` and `moving_add_and_remove` engine controls run at one worker; the headless `persistent_contacts` and `broad_phase_stress` controls run at one worker and at `--multiworker-count`.
