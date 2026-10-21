# Validation Guide: C++ Engine Cleanup

Run commands from the repository root. This is the acceptance guide for the future implementation, not a report that these checks currently pass. `PLATFORMATOR_SANITIZER`, CTest labels/new cases, `--asset-root`, `--smoke-steps`, and benchmark worker/repetition/schema controls below are planned outputs of the cleanup. Existing baseline failures are recorded in [CODEBASE_REVIEW.md](../../CODEBASE_REVIEW.md).

## Prerequisites

Use a C++20 compiler, Ninja, CMake supporting the checked-in presets, existing bundled dependencies/system build prerequisites, and Python 3 for sweeps. No Python GUI installation is needed for C++ validation. On a headless machine use SDL dummy/software drivers for desktop smoke tests; physics-only tests should not need SDL video/audio devices at all.

See [runtime lifecycle](contracts/runtime-lifecycle.md), [physics/steps](contracts/physics-step.md), and [library/benchmarks](contracts/library-benchmarks.md) for expected semantics.

## Normal C++ build and project tests

```sh
cmake -S . -B bin/cleanup-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPLATFORMATOR_BUILD_TESTS=ON \
  -DPLATFORMATOR_BUILD_EXAMPLES=ON \
  -DPLATFORMATOR_SANITIZER=none
cmake --build bin/cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug -N
ctest --test-dir bin/cleanup-debug --output-on-failure
```

Expected: all project tests pass; ordinary CTest does not list missing/unbuilt TBB tests. Header checks compile public headers individually and at least one consumer uses no private src headers. Build a second configuration with examples OFF to expose accidental dependencies on example assets.

During development run the relevant labels, then the whole suite only when warranted by broader changes:

```sh
ctest --test-dir bin/cleanup-debug -L lifecycle --output-on-failure
ctest --test-dir bin/cleanup-debug -L physics --output-on-failure
```

## Sanitizers

```sh
cmake -S . -B bin/cleanup-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPLATFORMATOR_BUILD_TESTS=ON \
  -DPLATFORMATOR_SANITIZER=address-undefined
cmake --build bin/cleanup-asan --parallel 4
ctest --test-dir bin/cleanup-asan --output-on-failure

cmake -S . -B bin/cleanup-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPLATFORMATOR_BUILD_TESTS=ON \
  -DPLATFORMATOR_SANITIZER=thread \
  -DTBB_SANITIZE=thread
cmake --build bin/cleanup-tsan --parallel 4
ctest --test-dir bin/cleanup-tsan -L physics --output-on-failure
```

Expected: engine and tests are instrumented; TSan also uses the compatible TBB build. Do not suppress all dependency reports or call an uninstrumented dependency run clean. If the host cannot execute TSan, record the exact error and continue unaffected checks without claiming it passed. LeakSanitizer failed under ptrace in the review environment; if this restriction persists, use `ASAN_OPTIONS=detect_leaks=0` for memory-access validation and record that leak checking still requires a supported environment.

## Required discriminating scenarios

| Scenario | Expected evidence |
| --- | --- |
| 1,000 scripted load/clear/reload cycles from start/fixed/update/late/collision/animation | Current callback returns alive; last ordinary request wins; no recursive frame; exactly-once starts/destruction; no old-scene registrations/references remain |
| Grow scripts, destroy another script, remove/replace executing component | No invalidated iteration; survivors update once; no later callbacks on logical retirement |
| Parent/child destruction in either allocation order | One owner/free per object; onDestroy sees its own required live components |
| Handle target destroyed/recycled, scene ID reused, runtime recreated, wrong subtype | Reference is empty or binding fails; no stale dereference/accidental revival |
| Failed load after partial staging and failed save | Active scene/path and prior saved file survive; staged resources reclaim |
| Parent moves; child collider has no Rigidbody | Public world position and synchronized geometry/cells agree without prepareSync calls in the test |
| Both parent/child move in parallel | Correct summed displacement; no ancestor race |
| Activation/filter/trigger/body changes | Final desired registration; one exit/enter where required; no loss or duplicate events |
| One body with 128 supports, one vs several workers | 128 stable supports, balanced changes, consistent canonical event/contact traces |
| Corner/face/inside/rotated/offset circle-box cases | Correct separation/manifold/features; reported outside-corner case does not collide |
| Tall sleeping stack, wide pile, last support removed | Existing physical assertions pass; wake/sleep behavior is meaningful |
| Time partition/pause/manual step/stall/quit | Fixed tick count/state matches contract, bounded recovery, no fake benchmark samples after quit |

Tests may use brute-force geometry/registry oracles, clear traces, and expensive assertions. They should exercise automatic runtime synchronization, not manually repair the state they are checking.

Add test-only work/allocation counters for idle mutations, unchanged hierarchy, one dirty subtree, warmed stable contacts, and sleeping-body populations. Expected: no whole-scene safety scan or active-list copy, one queue entry per changed item, no per-pair temporary axis allocation, no per-frame integration over sleeping/static populations. Keep this instrumentation out of uninstrumented performance runs. Review every remaining runtime `EFFICIENCY:` comment for all four required explanations; a regex match alone is not proof of compliance.

## Independent package and assets

```sh
cmake --install bin/cleanup-debug --prefix /tmp/platformator-cleanup-install
cmake -S examples/mario -B bin/cleanup-installed-mario -G Ninja \
  -DPLATFORMATOR_MARIO_USE_INSTALLED=ON \
  -DCMAKE_PREFIX_PATH=/tmp/platformator-cleanup-install
cmake --build bin/cleanup-installed-mario --parallel 4
cmake --install bin/cleanup-installed-mario --prefix /tmp/platformator-cleanup-game
```

The package-validation CTest cases must also create a minimal temporary public-header consumer, compile/link with find_package, relocate the clean prefix, and run outside the repository. An installed Mario smoke run from an unrelated directory should use the finite validation argument:

```sh
cd /tmp
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  /tmp/platformator-cleanup-game/bin/mario_example --smoke-steps 120
```

Expected: installed/package-default assets work without source-tree/cwd discovery. Repeat using `--asset-root` with a mixed-case asset fixture and after moving the install tree. Normalization must preserve filename case and reject root escape. Test invalid SDL driver, missing/malformed scene, unknown behavior, invalid required resource, and unwritable save destination: concise error, nonzero CLI status, safe cleanup. Physics-only tests run without dummy video/audio variables and without acquiring those devices.

## Corrected performance runs

The [pre-cleanup reference](../../benchmark/baselines/pre-cleanup-e01526b/RESULTS.md) records the original engine before task implementation, including both its historical partial-frame scope and an outer full-step timer. Preserve those artifacts and use a new output directory for each checkpoint. Keep workload/build/hardware settings comparable and inspect the recorded correctness failures and state changes before interpreting speed differences.

Return to the repository root before building/running these commands. These are measurement commands after correctness gates, not new performance claims.

```sh
cmake --preset benchmark-release -DPLATFORMATOR_SANITIZER=none
cmake --build --preset benchmark-release --target platformator_benchmark_runner --parallel 4
python3 benchmark/sweep_scaling.py \
  --runner bin/benchmark-release/platformator_benchmark_runner \
  --scenario add_and_remove \
  --starting-step 100 --step-size 100 --ending-step 1000 \
  --steady-count 1000 --warmup-frames 120 --measure-frames 600 \
  --dt 0.008333333333333333 --workers 1 --seed 1 --repetitions 5 \
  --raw-output-dir /tmp/platformator-cleanup-perf-raw \
  --output /tmp/platformator-cleanup-perf.csv
```

Repeat representative broad/narrow/rigid-body/moving-churn populations and a multiple-worker configuration on the same pinned machine. First run invariant-enabled validation separately; then use uninstrumented Release runs for timing. Confirm:

- Schema v2 identifies complete_step through actual destruction/final cleanup and separately names rendering/subscopes.
- Raw CSV and manifest exist for every population/repetition; source/build/workload metadata are complete.
- Population, finite-state, contact/support, and serial/multiworker invariants pass.
- Aggregates represent independent runs and retain spread, not only per-frame percentiles from one run.
- A controlled add/remove destructor workload changes complete_step/cleanup cost, proving cleanup is measured.
- Corrected baselines precede optimization comparisons. Investigate unexplained changes outside observed noise; fix feasible inefficiencies and document any necessary retained tradeoff locally.

Keep historical graphs/results labeled. A Godot comparison requires an explicit workload/metric mapping; if equivalent boundaries cannot be established, document separate scope results instead of publishing a speed ratio.
