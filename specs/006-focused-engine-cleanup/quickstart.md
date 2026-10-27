# Validation Quickstart: Focused Engine and Example Cleanup

This is a guide for implementation, not a claim that checks have run. See [asset contract](contracts/asset-loading.md), [physics and compatibility](contracts/physics-and-compatibility.md), and [validation evidence](contracts/validation.md). Use the actual current source and preserve all existing user edits. Paths below are absolute or anchored to the project root.

## 1. Capture and freeze input

Requires the project's C++20 toolchain, CMake/Ninja, bundled dependencies, Python 3 and Linux affinity utilities where available. Prepare any common test-only fixtures first; keep them identical in baseline and candidate, but use separate expected results for the intentionally changed missing-asset policy.

```bash
cd /home/user/Projects/Platformator
export FOCUSED_PROJECT="$PWD"
export FOCUSED_CAPTURE="$(mktemp -d /tmp/platformator-focused-cleanup.XXXXXX)"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export SDL_RENDER_DRIVER=software

git rev-parse HEAD > "$FOCUSED_CAPTURE/head.txt"
git branch --show-current > "$FOCUSED_CAPTURE/branch.txt"
git status --porcelain=v1 --untracked-files=all > "$FOCUSED_CAPTURE/status.txt"
git diff --binary HEAD > "$FOCUSED_CAPTURE/tracked-changes.patch"
lscpu > "$FOCUSED_CAPTURE/cpu.txt"
taskset -pc $$ > "$FOCUSED_CAPTURE/allowed-cpus.txt"
mkdir -p "$FOCUSED_CAPTURE/baseline-source"
git ls-files --cached --others --exclude-standard --deduplicate -z -- \
  CMakeLists.txt CMakePresets.json cmake include src vendored assets tests benchmark examples \
  | tar --null -T - -cf - | tar -xf - -C "$FOCUSED_CAPTURE/baseline-source"
export FOCUSED_BASELINE="$FOCUSED_CAPTURE/baseline-source"
```

Check the status for deletions and include any required ignored inputs; do not assume the snapshot is a clean commit. Save compiler/CMake/dependency versions, build settings, fixture and source hashes and a snapshot manifest. Keep all captured files until comparisons and evidence are archived. Capture baseline behavior, including fallback results, before production edits.

## 2. Correctness, consumers and example

In each tree run the following, starting with `FOCUSED_SOURCE="$FOCUSED_BASELINE"` and repeating with `FOCUSED_SOURCE="$FOCUSED_PROJECT"`. Preserve logs and exit codes. The old optional-fallback assertions need explicitly different candidate expectations; do not discard unrelated baseline failures or silently count skips as passes.

```bash
export FOCUSED_SOURCE="$FOCUSED_BASELINE"
cd "$FOCUSED_SOURCE"
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -R 'platformator_(asset_root_contract|scene_io_contract|scene_script_roundtrip|runtime_foundation|runtime_recreation|scene_transition_contract|callback_mutation|collider_pair_cleanup|contact_events|physics_bookkeeping|physics_support|physics_geometry|transform_hierarchy)_test|platformator_regression_tests'
ctest --test-dir bin/cleanup-debug --output-on-failure -L platformator
cmake --build bin/cleanup-debug --target platformator_public_headers --parallel 4
cmake --preset production
cmake --build --preset production --parallel 4
ctest --test-dir bin/production --output-on-failure -L platformator
```

Run and document the added focused alias/eviction/scene-rollback and body/pair membership tests, including one and available multiple workers; use existing worker controls or a small controlled host as needed. Package smoke includes the installed/relocated consumer and Mario smoke; verify that it ran, and explicitly build Mario with `PLATFORMATOR_BUILD_EXAMPLES=ON` where the selected preset disables examples. Compare debug-on/off builds. Use the same scene/settings on a real renderer to compare debug overlays and Mario behavior; save captures or record why unavailable.

## 3. Safety checks

Repeat in both trees; record full diagnostics, skips and exit codes. Adjust parallelism to the host.

```bash
cd "$FOCUSED_SOURCE"
cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(asset_root_contract|scene_io_contract|runtime_foundation|runtime_recreation|collider_pair_cleanup|physics_bookkeeping|contact_events)_test'
cmake --preset cleanup-tsan
cmake --build --preset cleanup-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir bin/cleanup-tsan --output-on-failure \
  -L 'physics|mutation|lifecycle' -E '^platformator_package_smoke$'
```

If leak detection cannot run, report it unavailable; a separate `detect_leaks=0` diagnostic is not a leak check. Classify any previously observed SDL audio reports using the actual baseline and candidate diagnostics.

## 4. Separate operation checks and timing

Measure filesystem-resolution calls for first request, exact retained hit, independently resolved aliases, multiple-alias eviction and reload; assert zero further resolution calls on a retained exact hit. Measure debug-object copies/moves and allocations during vector growth in debug-on mode. Record fixture identities, population, memory, operation counts and any inability to observe. Perform these checks in instrumented runs, not in Release timing aggregates.

Build matched Release baselines/candidates after correctness work. Use the same asset and overlay workloads, hold references throughout the timed portion, use recorded CPU affinity, warmup and at least five independent processes per cell, preserve raw samples and interleave versions. Compare small/large repeated hits and asset churn; include relevant one/multiple-worker quiet, dense, movement and migration/churn engine controls. The repository sweep tool provides reproducible existing engine workloads:

```bash
cd "$FOCUSED_SOURCE"
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
ctest --test-dir bin/cleanup-benchmark --output-on-failure -L benchmark
python3 benchmark/sweep_scaling.py \
  --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100,500,1000 --workers 1 --seed 0 \
  --warmup-frames 120 --measure-frames 600 --dt 0.008333333333333333 \
  --repetitions 1 --validate-invariants \
  --raw-output-dir "$FOCUSED_CAPTURE/baseline-broad-validation" \
  --output "$FOCUSED_CAPTURE/baseline-broad-validation.csv"
```

Use unique destinations for each stage. This example is an invariant check, not timing or the entire matrix; follow `benchmark/README.md` and [validation contract](contracts/validation.md) for matched timing, raw manifests, worker counts, variance and renderer limitations. Existing benchmark scenarios do not automatically prove asset-hit or debug-copy behavior; add a small common fixture if needed. Do not claim a speedup without comparable evidence.

## Recorded execution differences

The completed feature used a preserved `/tmp/platformator-006-baseline` snapshot and test-only overlays, rather than the quickstart's generated temporary directory name. The matched engine capture used 20 warmup and 60 measured steps per process instead of the illustrative 120/600 command above; five timing processes per cell and raw manifests were retained, but this shorter capture limits tail conclusions. Asset and debug work probes and their source are under `evidence/work/`. Real X11 rendering and leak checking were unavailable. The exact commands, fixture fingerprints, data, failures and limitations are recorded in `evidence/validation-logs.tar.xzdata`, `evidence/engine-analysis.md`, `evidence/asset-timing.md`, and `evidence/sanitizers.md`.
