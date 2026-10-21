# Pre-cleanup benchmark reference: e01526b

This captures the engine before the C++ cleanup. Engine, public headers, dependencies and root build files are unchanged from `e01526b`. The baseline harness and results are additional files. The staged review/specification documents do not affect the measured engine.

The engine still has the correctness defects documented in [CODEBASE_REVIEW.md](../../../CODEBASE_REVIEW.md). These measurements preserve its current behavior; they do not certify correct physics or lifecycle handling.

See [RESULTS.md](RESULTS.md) for the recorded numbers and observed correctness failures.

## Measurements

- `host_step`: an outer `steady_clock` interval around one public `Runtime::simulateFrame` or `simulateFrameWithCustomCallback` call. It includes scenario callbacks, all simulation work, existing benchmark instrumentation, post-step transient cleanup and deferred object/component destruction. Startup, scenario construction, warmup, shutdown, rendering, state inspection and output are excluded. A preallocated sample array stores every measured step in nanoseconds.
- `legacy_frame`: the existing recorder's `frame` scope. It ends before post-step transient cleanup and deferred destruction. Keep this name/boundary when comparing historical results.
- The original broad-phase, narrow-phase and collision-resolution scopes and all original counters are also retained. These scopes are nested within the step; adding them to the complete step would double-count work.

`capture.cpp` reuses the existing `src/benchmark_main.cpp` builders, placement/velocity patterns and argument parser. It adds an outer timer, a TBB concurrency cap and final-state output. It does not modify engine code. The six serial pilot cases were also run through the unmodified stock benchmark runner: every counter and frame count matched the harness exactly. Short pilot and stock-comparison runs are validation only, excluded from the performance aggregates.

## Configuration

- Linux, AMD Ryzen 5 7535HS, six physical cores/twelve logical CPUs.
- GCC 16.2.1, C++20, Release `-O3 -DNDEBUG`; existing benchmark instrumentation enabled, debug tools disabled, no sanitizers or LTO.
- TBB maximum parallelism of **1** or **6**, including the calling thread. Each process is pinned to CPU `0` or CPUs `0,2,4,6,8,10`, respectively. Actual worker participation may be lower for small workloads.
- Headless operation with SDL dummy audio/video drivers; the current runtime still initializes audio. No rendering or VSync.
- Fixed explicit timestep `1/120` second, **120 warmup steps**, **600 measured steps**, **five fresh-process repetitions** per condition.
- Six workloads, three sizes each, two concurrency profiles: **180 measured processes**, **108,000 measured steps**. Processes run sequentially in a recorded shuffled order with shuffle seed 1. Workload generation uses existing deterministic patterns, not a configurable random seed.
- The observed CPU governor is `performance` and boost is enabled. Clock speed is not locked, other desktop activity is not controlled, and no global power/affinity settings are changed. Per-run load and process wall time are recorded.

| Scenario | Small / medium / large parameter | Expected live objects/colliders |
| --- | --- | --- |
| `broad_phase` | 100 / 500 / 1,000 lanes | Parameter |
| `broad_phase_stress` | 50 / 150 / 300 moving dense boxes | Parameter |
| `narrow_phase` | 10 / 100 / 200 elements per group | Four times the parameter |
| `rigid_body_container` | 100 / 500 / 1,000 dynamic bodies, equal boxes/circles | Parameter plus four walls |
| `add_and_remove` | 10 / 100 / 1,000 removals and creations per step | 1,000 steady bodies |
| `moving_add_and_remove` | 10 / 100 / 1,000 removals and creations per step | 1,000 moving steady bodies |

The runner checks every measured process for successful exit, output completeness, exact recorded step/population counts, finite final transforms/velocities, and containment of the legacy timer inside the outer timer. It separately records support-range failures, preserving those measurements as defective-engine references. It saves final named-body snapshots and support/sleeping totals. These checks do not establish correct contact manifolds, supports, sleeping, or serial/parallel equivalence; those remain cleanup tasks.

## Reproduce

Run from the repository root. Preserve this original result directory and select a new output directory for every subsequent capture.

```sh
cmake -S benchmark/baselines/pre-cleanup-e01526b \
  -B bin/pre-cleanup-baseline -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
  -DSDLMIXER_TESTS=OFF -DSDLMIXER_EXAMPLES=OFF -DSDLIMAGE_TESTS=OFF
cmake --build bin/pre-cleanup-baseline \
  --target platformator_baseline_capture platformator_benchmark_runner --parallel 4
python3 benchmark/baselines/pre-cleanup-e01526b/run.py \
  --output benchmark/baselines/checkpoint-N
```

`run.py --help` exposes smaller smoke runs; the recorded baseline uses its defaults. The original configure invocation also passed unused `SDLTTF_TESTS=OFF`, reported in `logs/configure.log`; it has no effect. Build/configure logs are retained in `logs/`.

Use the same machine, compiler, flags, affinity, concurrency, timestep, step counts and scenario definitions for checkpoint comparisons. Inspect population/contact/support/sleeping changes before attributing timing changes to optimizations. Correctness fixes can change the amount of legitimate work. The source fingerprints identify workload changes; the harness targets the old runner's internal builder interface and may need a small adaptation as that interface changes. Preserve the original workload definitions when doing so.

## Recorded files

- `results/manifest.json`: source/binary/harness fingerprints, machine/build settings, timing definitions and run settings.
- `results/source-sha256.json`, `source.patch`, `CMakeCache.txt`, `compile_commands.json`, `build-commands.txt`, `ldd.txt`, `lscpu.txt`: provenance. An empty source patch confirms the measured engine matches the original commit.
- `results/schedule.json`: exact sequential execution order.
- `results/runs.csv` and `runs.json`: per-process statistics/counters, including the mean/median/p95/min/max of the full-step samples and final-state fingerprint.
- `results/summary.csv` and `summary.json`: medians, ranges and standard deviations **across the five per-process means**, plus final-state variation. The mean of per-step p95s is not substituted for run-to-run variation.
- Per-process raw records contain `steps.csv` (all outer samples), `legacy.csv` (original scope distributions/counters), `state.csv` (final body snapshot), `manifest.json`, stdout and stderr.

Raw records are preserved in `results/raw-runs.tar.gz`; pilot/stock-runner checks are in `validation-runs.tar.gz`. Every archived file was checked against its original SHA-256 before removing the loose copy. Extract with `tar -xzf results/raw-runs.tar.gz -C /tmp/chosen-directory` after creating that destination. `results/state-comparison.csv` compares final states against the corresponding first serial repetition. `SHA256SUMS` covers the retained artifacts. The final `run.py` revision explicitly records a failed support-range check while continuing capture; initial and resumed orchestration logs remain separate. The resume operation retained completed processes rather than replacing failed physics observations.

Use `host_step_mean_ms_run_median` as the main checkpoint metric. Its run minimum/maximum show observed spread; frame p95 and run-to-run spread describe different distributions. Process wall time includes startup/teardown/output and is not a substitute for step time. Original engine scopes/counters are retained for diagnosis. No Godot speed ratio is inferred from this capture.
