# Validation Quickstart: Clean Up Dead State and Internal Code

This guide describes checks to run **during implementation**, not results obtained by planning. See [runtime/physics contract](contracts/runtime-physics.md), [public compatibility](contracts/public-compatibility.md) and the complete [validation evidence contract](contracts/validation-evidence.md). Preserve all output/statuses and explicitly distinguish skips, baseline failures and unavailable checks.

## 1. Record inputs and freeze the baseline

Prerequisites: project C++20/CMake/Ninja toolchain, bundled dependencies, Python 3 and Linux tools for affinity/RSS when available. Identify available logical CPUs and at least one representative multiple-worker setting. Finish any necessary shared behavioral test fixture before freezing; execute that same fixture for baseline and candidate.

```bash
cd /home/user/Projects/Platformator
export DEAD_STATE_PROJECT="$PWD"
export DEAD_STATE_CAPTURE="$(mktemp -d /tmp/platformator-dead-state.XXXXXX)"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export SDL_RENDER_DRIVER=software

git rev-parse HEAD > "$DEAD_STATE_CAPTURE/head.txt"
git branch --show-current > "$DEAD_STATE_CAPTURE/branch.txt"
git status --porcelain=v1 --untracked-files=all > "$DEAD_STATE_CAPTURE/status.txt"
git diff --binary HEAD > "$DEAD_STATE_CAPTURE/tracked-changes.patch"
lscpu > "$DEAD_STATE_CAPTURE/cpu.txt"
taskset -pc $$ > "$DEAD_STATE_CAPTURE/allowed-cpus.txt"
```

Freeze before editing production code. This copies existing tracked **and nonignored untracked** inputs rather than assuming the source equals HEAD. Check the recorded status for deleted files and add any required ignored local inputs explicitly; record checksums of copied sources and fixtures. The snapshot has no Git metadata; use the captured revision/status/patch and content hashes as provenance, not a fabricated clean commit.

```bash
mkdir -p "$DEAD_STATE_CAPTURE/baseline-source"
git ls-files --cached --others --exclude-standard --deduplicate -z -- \
  CMakeLists.txt CMakePresets.json cmake include src vendored assets tests benchmark examples \
  | tar --null -T - -cf - | tar -xf - -C "$DEAD_STATE_CAPTURE/baseline-source"
export DEAD_STATE_BASELINE="$DEAD_STATE_CAPTURE/baseline-source"
```

Record a C01–C14 inventory and baseline test outcomes before removal. If a requested item has a current supported consumer/invariant, retain it with concrete evidence. Keep the snapshot, baseline binaries, common fixture and logs until candidate comparison and final evidence archiving are complete.

## 2. Correctness and compatibility

Run the following in the baseline first and repeat against the candidate after implementation. Set `DEAD_STATE_SOURCE` accordingly before each run. The complete normal suite includes public-header and independent package tests; do not report skipped tests as passed.

```bash
export DEAD_STATE_SOURCE="$DEAD_STATE_BASELINE"
cd "$DEAD_STATE_SOURCE"
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -R 'platformator_(contact_events|physics_bookkeeping|physics_support|physics_geometry|transform_hierarchy|callback_mutation|destruction_contract|runtime_foundation|runtime_recreation|runtime_step|scene_transition_contract|scene_transition_stress|queued_deletion_safety|collider_pair_cleanup|asset_root_contract)_test|platformator_regression_tests'
ctest --test-dir bin/cleanup-debug --output-on-failure -L platformator

cmake --preset production
cmake --build --preset production --parallel 4
ctest --test-dir bin/production --output-on-failure -L platformator
```

Repeat by setting `DEAD_STATE_SOURCE="$DEAD_STATE_PROJECT"` and `cd "$DEAD_STATE_SOURCE"`. Expected: relevant baseline/candidate test outcomes and observable traces agree; any baseline failure is documented and any new failure is investigated. C03's field-only test assertions are removed on the candidate, while normal/tangent remapping and physical tests remain. For worker equivalence and multicell final-witness removal/re-entry, run the common behavior fixture described in the evidence contract if existing coverage is insufficient.

Review `docs/CPP_API_MIGRATION.md` and package-smoke/public-header results for C02/C03/C04/C12. Build every consumer afresh with its corresponding library; a successful rebuild does not establish compatibility for previously compiled binaries. Exercise asset validation/fallback and existing scene/audio round trips.

## 3. Work, memory and safety checks

```bash
cd "$DEAD_STATE_SOURCE"
cmake --preset cleanup-work
cmake --build --preset cleanup-work --parallel 4
bin/cleanup-work/platformator_runtime_work_test 1
bin/cleanup-work/platformator_runtime_work_test 2

cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(physics_bookkeeping|contact_events|physics_support|physics_geometry|transform_hierarchy|callback_mutation|destruction_contract|runtime_foundation|queued_deletion_safety)_test'

cmake --preset cleanup-tsan
cmake --build --preset cleanup-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir bin/cleanup-tsan --output-on-failure \
  -L 'physics|mutation|lifecycle' -E '^platformator_package_smoke$'
```

Use additional worker counts available on the host. Record sanitizer build/run exit status and full diagnostics for baseline and candidate. Existing dependency leaks or SDL/libdbus TSan reports must be distinguished from affected engine findings; do not suppress them or claim unavailable runs passed. If LeakSanitizer is unavailable, a separate `detect_leaks=0` diagnostic does not replace the required enabled attempt.

Measure object sizes with each build's own compile flags, and measure retained capacity/allocation and process peak RSS separately as described in the [evidence contract](contracts/validation-evidence.md). The historical `specs/003-simplify-physics-bookkeeping/evidence/measure_state.py` illustrates isolated child RSS measurement but hardcodes CPU 0 and only some types; adapt a new measurement to allowed CPUs and all affected types. Do not overwrite the historical script or its results.

For visual verification, use a real graphical environment and the same scene/settings/build on baseline and candidate. For example, unset the dummy video/audio overrides, run `bin/cleanup-debug/main --scene assets/scenes/default.scene --asset-root assets --debug-draw all`, and compare collider, contact and grid overlays and controls under the same reproducible state. Capture screenshots or record the method. If a real renderer is unavailable, report the visual check as unverified; debug-on/off builds are separate evidence.

## 4. Separate invariant and timing captures

Build the Release benchmark configuration in each tree after correctness checks. Each sweep refuses overwriting a previous output; choose fresh names and record allowed affinity. The following is a runnable example for one baseline validation group, not the whole required matrix:

```bash
cd "$DEAD_STATE_SOURCE"
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
ctest --test-dir bin/cleanup-benchmark --output-on-failure -L benchmark

export DEAD_STATE_STAGE=baseline
python3 benchmark/sweep_scaling.py \
  --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario moving_add_and_remove --counts 10,100,1000 --steady-count 1000 \
  --workers 1 --seed 0 --dt 0.008333333333333333 \
  --warmup-frames 120 --measure-frames 600 --repetitions 1 --validate-invariants \
  --raw-output-dir "$DEAD_STATE_CAPTURE/$DEAD_STATE_STAGE/moving-w1-validation" \
  --output "$DEAD_STATE_CAPTURE/$DEAD_STATE_STAGE/moving-w1-validation.csv"
```

For candidate, select its source and set `DEAD_STATE_STAGE=candidate`. Repeat the matrix in the evidence contract with equivalent settings and supported worker counts. Inspect raw manifests and pair/contact counts; `moving_add_and_remove` alone does not establish actual pair churn. Use a common targeted pair-creation/final-removal fixture if needed and verify its operation counts and snapshots.

For timing, use new destinations, omit `--validate-invariants`, use at least `--repetitions 5`, and run matched baseline/candidate groups under recorded CPU affinity with builds and correctness work stopped. Retain schedule, manifests, raw per-step samples, source fingerprints and process-level measurements. Compare complete-step and relevant subsystem means/tails and run variation; do not add nested scopes. Report unverified claims when a workload, visual or memory measurement cannot be established.

## Execution notes for this feature

The completed capture uses `evidence/capture_matrix.py` and `evidence/capture_paired.py` to run the full prescribed groups, plus `evidence/confirm_persistent.py` for an interleaved timing follow-up. Full raw invariant, timing and sanitizer archives are under `evidence/`. The original temporary baseline and ordinary-suite logs disappeared between turns; the Release baseline was reconstructed from the recorded HEAD and shared fixtures, then its controlled invariants were compared against the archived original before timing. The original complete source-manifest hash cannot be rechecked. TSan's full label selection was run on both builds; the leak-disabled ASan/UBSan retry is separate from the failed leak-enabled attempt. See `evidence/sanitizers.md` for failures and `evidence/debug-overlays.md` for unavailable visual verification.
