# Quickstart: Validating and Comparing Spatial Methods

Run commands from the repository root with a C++20/C17 toolchain, CMake >=3.22, Ninja and Python 3. Collision/visibility replay and the actual `sprite_visibility`/`spatial_combined` engine fixtures write schema 4 with separate step/render/replay samples and metric applicability. The primary retained replay evaluation covers the 41-workload sparse/dynamic preset at 100, 1,000 and 10,000 objects; selected actual-engine cases cover physics, sprite visibility and all eight combined selections. Resource captures use a separate executable and are linked to matching timing records. See [`evidence/RESULTS.md`](evidence/RESULTS.md) and [`evidence/us3-acceptance.md`](evidence/us3-acceptance.md) for the measured scope and remaining limits. SDL image tests use a software renderer; hardware-renderer performance remains unverified. Use fresh output directories and retain raw files.

Selection details: [runtime contract](contracts/runtime-selection.md). Input and output formats: [replay](contracts/spatial-replay.md), [benchmark/results](contracts/benchmark-results.md). Ownership and timing assumptions: [data model](data-model.md).

For headless Linux runs that request actual rendering, set `SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy` for validation, timing and resource commands. Keep these renderer settings identical across paired captures. On a machine with a display, record and preserve its renderer/output signature instead.

## 1. Preserve the existing-method baseline

Before pair/core refactoring, retain source/build fingerprints at `2ad8caa466b1c0217b7911d3bca1a53160150456`. Use that revision's runner and Python tooling to capture the existing default segmented/none cases with their existing flags. For example, in the baseline checkout:

```sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
python3 benchmark/sweep_scaling.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100,500,1000 --workers 1 --seed 0 \
  --warmup-frames 120 --measure-frames 600 --dt 0.008333333333333333 \
  --repetitions 5 --raw-output-dir /tmp/spatial-baseline-raw --output /tmp/spatial-baseline.csv
```

Pair timing with separate `--validate-invariants` runs and an available multiworker allowance. Repeat relevant existing workload families from the benchmark contract, not just this example. Capture the same legacy commands after refactoring with default settings, the same toolchain/hardware/affinity, and a new destination. Compare common equivalent scopes through explicit version-aware reporting; keep schema-3 raw records intact. Investigate regressions before attributing any BVH advantage to the data structure.

## 2. Build and run correctness/package gates

```sh
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure

cmake --preset cleanup-work
cmake --build --preset cleanup-work --parallel 4
ctest --test-dir bin/cleanup-work --output-on-failure -L spatial

cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ctest --test-dir bin/cleanup-asan --output-on-failure -L spatial
```

Implementation registers the new tests with label `spatial`; image fixtures also have `rendering`, and benchmark/parser tests have `benchmark`. Run supported cleanup-tsan checks separately and retain any environment limitation. Work probes verify that inactive indexes do no work, unchanged batches do not scan all physics pairs, and dirty/order caches invalidate correctly; never use that build for timings. Preserve package smoke/relocation and source-embedded consumer checks under defaults and new selections; the installed SDK must require no Box2D package.

Expected gates: retained upstream tree tests plus marked update/partial/full maintenance tests; exact symmetric-mask pair coverage; segmented ties/checkpoints/chunk repair/migration/multi-cell witnesses; stale identity/node reuse; geometry/filter/response-only updates; support/sleep/contact event parity; callback destruction and scene replacement. Renderer tests cover conservative projected quads, transparent/current order, swap removal, sprite-only objects, animation/source/flip, parent changes, aspect/output changes and invalid-state fallback. All must pass before accepting timings.

## 3. Exercise all eight actual-engine combinations

```sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
mkdir -p /tmp/spatial-combinations
export SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
for broad in segmented bvh; do
  for culling in none linear bvh segmented; do
    bin/cleanup-benchmark/platformator_benchmark_runner \
      --scenario spatial_combined --sprite-count 1000 --visible-percent 1 \
      --changed-percent 10 --spatial-variant moving --render \
      --broad-phase "$broad" --render-culling "$culling" \
      --workers 1 --seed 0 --warmup-frames 10 --measure-frames 30 \
      --capture-mode validate \
      --csv-output "/tmp/spatial-combinations/$broad-$culling.csv"
  done
done
```

Use the same SDL renderer/output for comparisons and repeat with a supported multiworker allowance. Validation captures are timing-ineligible. Actual rendered validation now performs its own same-frame submit-all pixel/order comparison and retains image hashes, source/case identity and the renderer signature; physics-only validation cannot qualify rendered timing. Image fixtures compare each culled mode with submit-all under identical state; the dedicated static 1,000-sprite fixture has exactly 10 safely visible sprites. Expect 1,000 submissions for none and 10 once each, in the same retained order and with the same image, for the three culling choices. Motion/camera variants have their own per-frame expected counts rather than assuming the static count forever.

Check omitted settings preserve segmented/none. Unknown mode tokens, negative/nonfinite padding and unsupported physics BVH bounds must fail clearly. Verify render fallback produces reference output and records its reason; it must not masquerade as a pure indexed frame. A no-camera/headless run should produce no actual render samples, and paused/zero-step changes must appear in the next rendered frame.

## 4. Validate identical recorded inputs

```sh
python3 benchmark/generate_spatial_traces.py --preset smoke --seed 0 \
  --warmup-frames 120 --measure-frames 600 --output-dir /tmp/spatial-smoke-traces

python3 benchmark/sweep_spatial.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --cases /tmp/spatial-smoke-traces/cases.json --broad-phases segmented,bvh \
  --render-cullings none,linear,bvh,segmented --worker-counts 1,6 \
  --repetitions 1 --capture-mode validate --output-dir /tmp/spatial-smoke-validation
```

Replace 6 with an available multiple-worker allowance and record CPU affinity/topology. The collision trace runs both broad phases; the visibility trace runs all four render choices. These headless traces produce replay rows, never SDL render rows. Independent references must find no missed eligible pair/visible quad, duplicate logical identity or invalid retained ordering. Collision references cover persistent transitions; actual contact/solver integration remains covered by step 2/3.

To diagnose one case:

```sh
bin/cleanup-benchmark/platformator_benchmark_runner \
  --spatial-trace /tmp/spatial-smoke-traces/smoke.collision.jsonl \
  --broad-phase bvh --bvh-maintenance native-partial --bvh-padding 0 \
  --bvh-query-execution serial --workers 1 --capture-mode validate \
  --csv-output /tmp/spatial-smoke-bvh-validation.csv
```

Do not combine a replay with --scenario/--scene/--render or contradictory header parameters. Changing maintenance/padding/query profile requires matching validation and a distinct comparison key.

## 5. Capture complete dynamic timings

After smoke passes, generate `--preset evaluation` in a fresh directory and validate it with the same sweep command/settings. The retained bounded matrix uses 10 warmup and 30 measured replay steps to cover every preset dimension at every applicable method and worker count; engine cases use their declared 120 warmup and 600 measured steps. Use separate `--preset calibration` inputs to assess maintenance/padding/query-execution choices, then freeze the profile before evaluation. Native-partial/zero-padding with serial queries is the selected primary profile; it is a measured policy, not a proven universal optimum.

Run actual-render engine validation and timing sweeps with the same SDL renderer environment shown above. For example:

```sh
python3 benchmark/sweep_spatial.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --cases /tmp/spatial-smoke-traces/cases.json --broad-phases segmented,bvh \
  --render-cullings none,linear,bvh,segmented --worker-counts 1,6 \
  --repetitions 5 --capture-mode performance \
  --validation-dir /tmp/spatial-smoke-validation --output-dir /tmp/spatial-smoke-timing
```

Use matching evaluation cases/validation paths for the complete paper bundle. The schema-4 sweep retains run summaries, raw samples, manifests, logs, checksums, eligibility reasons, summary.csv and report.md. Expect five fresh processes per applicable case/method/worker/profile, not five loops in one warmed process. Boundary-matched cases must identify every initialization/update/rebuild/query/reduction/order cost; static reuse is visible through counters, not mistaken for dynamic update throughput.

Also capture actual sprite_visibility and spatial_combined engine cases using typed engine-case manifests, plus the existing physics scenarios. Render claims require actual --render frames with renderer/presentation state recorded. Replay excludes drawing, and precomputed collision traces exclude transform-to-AABB construction; neither replaces full-engine measurements. Resource and validation captures cannot enter timing aggregates.

## 6. Measure resources separately and review the result

```sh
python3 benchmark/sweep_spatial.py --runner bin/cleanup-benchmark/platformator_spatial_resource_runner \
  --cases /tmp/spatial-smoke-traces/cases.json --broad-phases segmented,bvh \
  --render-cullings none,linear,bvh,segmented --worker-counts 1,6 \
  --repetitions 1 --capture-mode resource --timing-dir /tmp/spatial-smoke-timing \
  --output-dir /tmp/spatial-smoke-resources
```

Link matching case/profile/source keys to timing results. Inspect accounted retained/peak index/common/scratch capacity, C++/Box2D allocation coverage and exclusions, plus process RSS where available. Confirm the timing executable does not link allocation hooks.

For actual-render engine resource cases, keep the renderer environment identical to the paired validation and timing bundles. Replay resources sample selected owner capacities after initialization and every complete step, outside the measured replay interval; resource runs remain timing-ineligible.

Report startup with separate zero-warmup, one-frame fixture variants and five fresh processes. Runtime/fixture setup plus first complete-step and actual-render durations are disjoint; include first-frame index publication instead of letting it disappear into warmup. Keep these cold results separate from steady-state means/tails.

The final comparison must cover the required population/change/visibility fractions and companions, report mean/tails/initialization/resources and one/multiworker limits, quantify same-method refactor overhead, and explain regressions and unavailable hardware evidence. Preserve raw historical P03 results separately. Completion supplies a reproducible choice between methods; it does not require a speedup or changing production defaults.
