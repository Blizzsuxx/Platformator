# C++ benchmark measurements

Configure `cmake --preset cleanup-benchmark`, then build `cmake --build --preset cleanup-benchmark --parallel 4`. Captures require Release without sanitizer, work probes or debug tools. Benchmark scopes themselves are timing instrumentation. Python tooling uses only the standard library except optional matplotlib plotting.

## Spatial replay sweeps (schema 4)

The trace generator, headless collision/visibility replay runner, schema-4 reader,
fresh-process sweep harness and schema-4 summary plot path are available. Replay
does not make SDL render samples and is not a substitute for physics/render
engine fixtures. `sweep_spatial.py` verifies each trace hash, runs only the
method choices that apply to its domain, retains every process artifact, and
requires a matching passed validation bundle before accepting performance runs.
Qualification binds the immutable case/domain and resolved fixture parameters to
the effective methods/profile, seed, dt, warmup and measured boundaries, worker
allowance, complete source fingerprint, compiler/version/flags, build
configuration and instrumentation, dependencies, machine record, and renderer
signature when rendering. Validation and timing are separate modes. Resource
captures link to timing with the same identity; their declared `resource_hooks`
setting is the only permitted build difference and must be enabled only for the
resource runner.

```sh
python3 benchmark/generate_spatial_traces.py --preset smoke --seed 0 \
  --warmup-frames 1 --measure-frames 2 --output-dir /tmp/spatial-smoke-cases
python3 benchmark/sweep_spatial.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --cases /tmp/spatial-smoke-cases/cases.json --broad-phases segmented,bvh \
  --render-cullings none,linear,bvh,segmented --worker-counts 1 \
  --available-workers 12 --repetitions 1 --capture-mode validate \
  --output-dir /tmp/spatial-smoke-validation
```

For performance, use at least five processes and supply `--validation-dir` with
the matching case/profile/worker/source captures. Use the measured CPU allowance
for `--available-workers`; the example value 12 is machine-specific. The
`summary.csv`, `report.md`, `eligibility.json`, `reason_counts.json` and
`checksums.sha256` are produced with the raw CSV, sample CSV, manifest and log
for every process. `plot_scaling.py` accepts schema-4 sweep summaries; filter a
single domain, method, worker count and workload fraction with `--domain`,
`--broad-phase` or `--render-culling`, `--workers`, `--changed-percent` or
`--visible-percent`, and `--variant`.

Generated replay headers distinguish requested workload knobs from observed
operations. `changed_percent` remains a requested fixture knob; when present,
`requested_changed_endpoint_count` is its rounded population-based label, not
an observed mutation count. `operation_counts` records per-step actual changed
endpoints and geometry endpoints, geometry writes, generation replacements,
filter writes (including no-op writes), effective filter changes, and writes
coalesced by endpoint/property. Step entries include warmup and follow trace
step order. Collision churn, filter and repeated-write companions retain their
intended operations even when their actual counts differ from requested labels.
The manifest and trace header must agree on all these fields.

The typed engine-case generator and actual `sprite_visibility` and
`spatial_combined` runner fixtures are available. Both require `--render`; the
combined fixture also includes 128 moving box colliders. The runner writes
schema-4 step and actual-render samples with separate applicability and the
renderer/output-view signature. Actual rendered validation compares every frame
against an ordered submit-all redraw with exact RGBA32 pixels, retains matching
reference/result hashes and source/case/renderer identity, and is timing-ineligible.
The sweep rejects physical-only or mismatched image proof for rendered timings.
Replay geometry validation remains renderer-independent. Schema-3 files remain readable as historical
captures. `platformator_spatial_resource_runner` links allocation hooks only
into its resource-mode executable; resource captures are timing-ineligible.
Use `--timing-dir` to link each resource process to a matching performance
capture with the same case, source, method profile, worker count and renderer.

```sh
python3 benchmark/sweep_spatial.py --runner bin/cleanup-benchmark/platformator_spatial_resource_runner \
  --cases /tmp/spatial-cases/cases.json --broad-phases bvh --render-cullings none \
  --worker-counts 1 --repetitions 5 --capture-mode resource \
  --timing-dir /tmp/spatial-timings --output-dir /tmp/spatial-resources
```

For startup cost, use separate zero-warmup, one-frame versions of the same engine
fixtures, matched validation and five fresh processes. Runtime/fixture setup is
recorded in `initialization.total_ns`; first-frame spatial publication can be
deferred, so report it separately. Setup + first `complete_step` + first actual
`render` are disjoint and capture cold operational startup without adding nested
children or mixing startup into steady-state samples. The retained feature
[results and reproducible capture drivers](../specs/007-integrate-bvh-culling/evidence/RESULTS.md)
include both views.

The resource report accounts C++ allocation requests by tagged domain and
Box2D allocator callbacks, plus process peak RSS where supported. C malloc,
SDL allocations and allocator metadata are excluded. These counts report
requested retained/peak bytes, not allocator usable size or total RSS by
index; the exclusions stay with the results.

```sh
python3 benchmark/sweep_scaling.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100,500,1000 --workers 1 --seed 0 \
  --warmup-frames 120 --measure-frames 600 --dt 0.008333333333333333 \
  --repetitions 5 --raw-output-dir /tmp/broad-runs --output /tmp/broad.csv
python3 benchmark/plot_scaling.py --input /tmp/broad.csv --output /tmp/broad.png
```

Each repetition starts a new process. On Linux pin the sweep with `taskset -c <CPUs>` so children inherit affinity; record CPU topology/governor, contention and the chosen logical CPUs. Worker control is set once before runtime creation. Seed zero preserves deterministic original layouts. `--warmup-frames` and `--measure-frames` mean simulation **steps**; explicit caller dt is used. `--render` adds independent rendering samples and stops immediately on quit; incomplete captures cannot enter an aggregate.

Use `--validate-invariants --repetitions 1` for separate correctness runs. This mode recomputes contact/support invariants after each step and records final body/contact snapshots. It is explicitly labeled `per-step`, `timing_eligible=false`; its durations must never be presented as uninstrumented performance. Ordinary captures check finite final state, possible support ranges, population and per-step population counters outside measured scopes. Compare serial/multiworker validation snapshots with declared tolerances before accepting timings. General lifecycle/geometry/sleep tests remain separate correctness gates.

## Schema versions and boundaries

Trace replay and new engine captures use schema 4. Readers dispatch explicitly
between historical schema-3 captures and schema-4 spatial bundles; mixed
versions are rejected. Schema 4 defines separate step, render and replay
sample domains, metric applicability, initialization and resource records.
Allocation instrumentation is linked only into the resource runner, so its
overhead does not enter the timing executable. Resource comparisons stay
separate from performance aggregates.

### Historical schema v3 engine captures

`run.csv` contains all eight scope and eleven counter summaries, units and sample counts. `run.samples.csv` preserves every step duration/counter plus independent rendering rows. `run.manifest.json` records resolved workload, seed/workers/dt, warmup/measurement/repetition parameters, source fingerprint/commit/dirty state, compiler/flags/configuration, instrumentation, machine/OS/dependencies, invariants and scope boundaries. Logs and raw files are retained beside sweep aggregates. Output errors are fatal. Sweeps refuse overwriting prior captures, missing/unknown metrics, invalid invariants, non-Release instrumentation and truncated runs. Plotting accepts only schema-v3 performance aggregates and displays standard deviation across independent run means.

Schema 3 files remove the recurring contact metric and count Enter/Exit **pair events generated within the complete step**, including cleanup, independently of script availability and delivered callback count. Historical files remain unchanged. Timing comparisons across versions require equivalent workloads/boundaries; the event counters have intentionally changed meaning.

### Schema v4 engine captures

New engine captures preserve independent step and actual-render rows. Applicability is checked against the spatial method and sample domain; blank fields mean inapplicable or unavailable, while applicable skipped work is zero. The manifest includes initialization time, source/build/dependency and worker provenance, renderer/output-view signature, boundaries, and validation/resource status. Physics and visibility instrumentation records selected index update, maintenance, query, reduction, and proxy work without constructing a validation oracle in timed frames. Resource captures report C++ new/delete and Box2D allocator requests by tag; C/SDL/TBB allocations and allocator metadata remain outside those counts.

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
