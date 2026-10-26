# Validation Guide: Simplify Physics Bookkeeping

This feature is implemented and independently executable checks are recorded in [validation.md](validation.md). The bookkeeping target exists. Commands use the current presets, tests and benchmark schema 3. Performance acceptance remains unverified under T038; see [profiling disposition](evidence/spatial-investigation.md). Actual maintainer readability review remains pending; see [human-review.md](evidence/human-review.md). See [state ownership](data-model.md), [behavior contract](contracts/physics-lifecycle.md) and [evidence contract](contracts/validation-evidence.md).

## Current solver-order amendment

The maintainer removed canonical physics-solver ordering on 2026-10-26; see spec.md and [current scoped checks](evidence/solver-order-removal.md). The baseline/capture commands below reproduce the earlier sorted implementation and its historical comparisons. Do not require matching old/new or cross-worker solved trajectories for the amended runtime. Retain independent geometry/contact/support/lifetime checks and canonical event ordering for equivalent transitions; do not widen geometry tolerances or relabel old timings as current.

## Baseline provenance and reproduction

The retained baseline is `bin/physics-bookkeeping-20261025/baseline-source`, derived from f6d1af7 plus the test/CMake-only [common characterization patch](evidence/common-characterization-final.patch). Current working runtime is the final candidate. Final raw results are under `bin/physics-bookkeeping-20261025/captures-v2`; original candidate captures/follow-ups/source archive are retained separately. Preserve/archive ignored bin/ data before cleaning builds.

For exact reproduction, create an isolated checkout at `f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba` and apply only common-characterization-final.patch to the baseline; use the final candidate source/archive for the other stage. Do not apply the current runtime diff and call it the old baseline. The generic preservation block below describes how the initial pre-edit baseline was made, or how to start a new future feature comparison. It must not be rerun against the implemented working tree as a reproduction of this feature’s old runtime.

Run from the repository root with the current C++20/CMake/Ninja/Python toolchain. Baseline HEAD at planning was `f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba`. Record a newer HEAD/patch if the source changed. Keep baseline and candidate full source/build trees separate; rebuilding the candidate must not overwrite baseline shared libraries. The historical `7906959` and feature 002's schema-2 baseline are not equivalent controls.

```bash
export BOOKKEEPING_PROJECT="$PWD"
mkdir -p "$BOOKKEEPING_PROJECT/bin"
export BOOKKEEPING_CAPTURE_ROOT="$(mktemp -d "$BOOKKEEPING_PROJECT/bin/physics-bookkeeping.XXXXXX")"
export BOOKKEEPING_BASELINE_SOURCE="$BOOKKEEPING_CAPTURE_ROOT/baseline-source"
export BOOKKEEPING_MULTI_WORKERS=6
export BOOKKEEPING_ONE_CPUS=0
export BOOKKEEPING_MULTI_CPUS=0,2,4,6,8,10

git rev-parse HEAD > "$BOOKKEEPING_CAPTURE_ROOT/baseline-revision.txt"
git status --short > "$BOOKKEEPING_CAPTURE_ROOT/baseline-status.txt"
git diff --binary HEAD > "$BOOKKEEPING_CAPTURE_ROOT/baseline-tracked.patch"
git clone --local --no-hardlinks "$BOOKKEEPING_PROJECT" "$BOOKKEEPING_BASELINE_SOURCE"
if [ -s "$BOOKKEEPING_CAPTURE_ROOT/baseline-tracked.patch" ]; then
  git -C "$BOOKKEEPING_BASELINE_SOURCE" apply "$BOOKKEEPING_CAPTURE_ROOT/baseline-tracked.patch"
fi
cd "$BOOKKEEPING_BASELINE_SOURCE"
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
export BOOKKEEPING_BASELINE_RUNNER="$BOOKKEEPING_BASELINE_SOURCE/bin/cleanup-benchmark/platformator_benchmark_runner"
cd "$BOOKKEEPING_PROJECT"
```

This copies tracked committed and dirty content. Inspect baseline-status.txt and separately preserve/apply any untracked runtime/test/fixture files; do not mistake their omission for an equivalent baseline. The new planning documents need not be copied into the baseline runner. Record source fingerprints, actual flags, vendored dependency versions, CPU topology/governor and supported affinity. Choose appropriate CPUs/workers for the host rather than assuming the reference topology exists. Pin both stages identically. Baseline retains its own .git metadata, sources and built dependencies. No current checkout branch switch or commit is required.

Run baseline characterization and the validation matrix before runtime changes. Complete both builds before interleaved final timing. If a new common fixture or observation surface is needed, apply identical fixture changes to both sources before baseline captures and retain that patch. Do not alter physical algorithms to create the baseline. Archive the capture directory before cleaning ignored bin/ outputs.

## Focused characterization, then refactor

```bash
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -R 'platformator_(contact_events|physics_support|physics_geometry|callback_mutation|scene_transition_stress|queued_deletion_safety|collider_pair_cleanup)_test|platformator_regression_tests'
```

Preserve existing passing assertions. The INTERNAL `tests/physics_bookkeeping_test.cpp` target covers queue cancellation/deduplication, type/kinematic changes and pending replacement gaps. Its baseline characterization and final results are retained; run it with:

```bash
cmake --build --preset cleanup-debug --target platformator_physics_bookkeeping_test --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure -R '^platformator_physics_bookkeeping_test$'
```

Expected outcomes are in the contracts. Test observable membership/results, committed support and exact event histories, not private helper call sequences. Queue tests need first/middle/last cancellation, moved-index repair, duplicate enqueue, removed-before-evaluation and consumed-then-requeued entries. Include 128 changed pairs under workers one and the selected multiworker count (current pair threshold 64). Keep worker control local to fixtures.

Support tests must distinguish old and replacement recipients, current and deleted geometry, reference/endpoint changes, and net loss/gain cancellation within one flush. Body tests must cover static/kinematic/dynamic changes, zero/nonzero linear and angular velocity, sleep/wake, gravity eligibility, repeated changes and retirement before drain. Use real safe runtime boundaries; no new public queue inspection interface.

## Work and storage

```bash
cmake --preset cleanup-work
cmake --build --preset cleanup-work --parallel 4
ctest --test-dir bin/cleanup-work --output-on-failure \
  -R 'platformator_(runtime_work|physics_bookkeeping|contact_events|physics_support)_test'
```

The expanded runtime_work_test covers 100 warmed unchanged updates, pending replacement/cancellation and fixed-peak churn. CTest uses one worker; run `bin/cleanup-work/platformator_runtime_work_test 6` for the retained six-worker control. Require zero eligible-work counters and zero C++ new/new[] allocations in the warmed idle control. Verify that script-only changes do not refresh physics. For churn, assert real contact/body counts and support restoration as well as capacities; allocation/storage assertions without a populated fixture are insufficient. Measure pair layout and per-child peak RSS, reporting noise and allocator scope. Do not require padding to yield a specific byte reduction.

## Full checks and sanitizers

```bash
ctest --test-dir bin/cleanup-debug --output-on-failure -L platformator
cmake --preset production
cmake --build --preset production --parallel 4
cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(physics_bookkeeping|contact_events|physics_support|physics_geometry|transform_hierarchy)_test'
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure -L platformator -E platformator_package_smoke
cmake --preset cleanup-tsan
cmake --build --preset cleanup-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir bin/cleanup-tsan --output-on-failure \
  -L 'physics|mutation|lifecycle'
```

The full normal suite includes installed/relocated SDK consumers; package smoke's independently rebuilt consumers are not a sanitized-engine check. The second ASan command is explicitly a memory-access/UB check with leaks disabled. Also attempt the full non-package leak-enabled selection where supported, recording actual findings. Prior [feature 002 sanitizer results](../002-simplify-contact-events/evidence/sanitizers.md) include FluidSynth initialization leaks, LeakSanitizer thread-inspection restrictions inside the tool sandbox and two SDL/libdbus lock-order warnings. Record and compare actual stacks; unresolved findings and unavailable detectors do not become passes. No TSan suppression or deadlock-detector disabling is part of this plan.

## Representative matrix and reporting compatibility

```bash
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
ctest --test-dir bin/cleanup-benchmark --output-on-failure -L benchmark
python3 -m unittest discover -s benchmark/tests -v
export BOOKKEEPING_CANDIDATE_RUNNER="$BOOKKEEPING_PROJECT/bin/cleanup-benchmark/platformator_benchmark_runner"
```

Both stages must retain strict schema 3: eight scopes, eleven counters, identical boundaries and event meanings. Use each source tree's matching sweep/runner and inspect physical populations in manifests. Persistent contacts are a quiet/notification guard, not a support-ledger timing fixture. The rigid-body container supplies moving response/support work; explicit sleeping/quiet controls are established by focused/work tests. Record actual sleeping/support populations rather than assuming the container settles by warmup.

| Scenario | Counts | Additional options | Purpose |
| --- | --- | --- | --- |
| broad_phase | 100,1000 | none | Uniform placement/movement guard |
| broad_phase_stress | 50,300 | none | Uneven/dense spatial work |
| narrow_phase | 10,200 | none | Small/large changed-pair batches and geometry boundary |
| rigid_body_container | 100,1000 | none | Mixed shapes, dynamic response and support |
| add_and_remove | 10,1000 | --steady-count 1000 | Registration/retirement and queue churn |
| moving_add_and_remove | 10,1000 | --steady-count 1000 | Churn plus movement/cell migration |
| persistent_contacts | 10,1000 | Repeat --scripted-endpoints 0,1,2 | Quiet contacts and script independence |

This is 18 scenario/count/recipient combinations × two worker choices = 36 groups per stage. For correctness use one process per group, 120 warmup, 600 measured, seed 0 and dt 1/120, with --validate-invariants. For timing use five independent processes per group without the per-step oracle: 180 processes per stage, 360 total. Preserve exact job order/affinity and source/build metadata; add workloads only for a demonstrated gap, applying them to both stages.

The following runnable example covers one no-script persistent group at one worker. Repeat using the table, recipient choices and the selected multiworker count/affinity. Give every stage/mode/scenario/worker/recipient an unused raw/output path.

```bash
export BOOKKEEPING_STAGE=baseline
export BOOKKEEPING_STAGE_SOURCE="$BOOKKEEPING_BASELINE_SOURCE"
export BOOKKEEPING_RUNNER="$BOOKKEEPING_BASELINE_RUNNER"
mkdir -p "$BOOKKEEPING_CAPTURE_ROOT/$BOOKKEEPING_STAGE"
taskset -c "$BOOKKEEPING_ONE_CPUS" python3 "$BOOKKEEPING_STAGE_SOURCE/benchmark/sweep_scaling.py" \
  --runner "$BOOKKEEPING_RUNNER" --scenario persistent_contacts --counts 10,1000 \
  --scripted-endpoints 0 --workers 1 --seed 0 --dt 0.008333333333333333 \
  --warmup-frames 120 --measure-frames 600 --repetitions 1 --validate-invariants \
  --raw-output-dir "$BOOKKEEPING_CAPTURE_ROOT/$BOOKKEEPING_STAGE/persistent-r0-w1-validate" \
  --output "$BOOKKEEPING_CAPTURE_ROOT/$BOOKKEEPING_STAGE/persistent-r0-w1-validate.csv"
```

For candidate use BOOKKEEPING_STAGE=candidate, BOOKKEEPING_STAGE_SOURCE=$BOOKKEEPING_PROJECT and BOOKKEEPING_RUNNER=$BOOKKEEPING_CANDIDATE_RUNNER. Compare all 36 groups baseline/candidate and 18 worker pairs per stage with benchmark/capture_cleanup.py's existing compare function. Never aggregate oracle timings as performance.

Only after all correctness checks and builds finish, execute matched timing jobs. This example uses a fresh timing destination:

```bash
taskset -c "$BOOKKEEPING_ONE_CPUS" python3 "$BOOKKEEPING_STAGE_SOURCE/benchmark/sweep_scaling.py" \
  --runner "$BOOKKEEPING_RUNNER" --scenario persistent_contacts --counts 10,1000 \
  --scripted-endpoints 0 --workers 1 --seed 0 --dt 0.008333333333333333 \
  --warmup-frames 120 --measure-frames 600 --repetitions 5 \
  --raw-output-dir "$BOOKKEEPING_CAPTURE_ROOT/$BOOKKEEPING_STAGE/persistent-r0-w1-time" \
  --output "$BOOKKEEPING_CAPTURE_ROOT/$BOOKKEEPING_STAGE/persistent-r0-w1-time.csv"
```

The implemented [capture_matrix.py](evidence/capture_matrix.py) records a deterministic matched job order (shuffle seed 1), preferably alternate immutable baseline/candidate jobs, preserve command logs and checksums, and refuse overwrites. Adapt the previous feature's small capture script to this matrix/runner paths during implementation if needed; do not reuse its hardcoded paths, schema-2 control, counts or CPU masks without review. The examples above validate the runner interface; they do not implement the final scheduling harness.

Report complete-step and relevant nested-scope means/median/p95/p99/max and independent-run spread, worker ratios, work/allocation and retained-storage results. Investigate every regression; use matched follow-up captures for important uncertainty. Do not sum nested scopes, declare equivalence from overlapping ranges, or claim speedups from removed field counts. Freeze runtime source before final timing; a later fix requires affected correctness and comparable recaptures.

## Human review and completion evidence

Produce concise inventory and code-linked walkthroughs for J1–J8 from data-model.md. For support replacement and queue cancellation, show old/new independent facts and maintenance sites, including any complexity moved elsewhere. The intended changes remove three independent booleans across these paths; actual reduction and behavior must be verified in the final code.

Record execution evidence and limitations in validation.md. Prepare the final code for the maintainer to walk each route and identify who changes a fact and when it becomes valid. Leave the human-readability outcome pending until that review happens. This is separate from the already completed specification-quality checklist; implementation must not change its markers.

Final implementation measurements and remaining uncertainty are in [performance-analysis.md](evidence/performance-analysis.md), including initial regression follow-ups, the inline reset correction and all final positive deltas. No speedup/equivalence claim follows from overlap alone. Current leak/lock-order findings were rechecked after the correction; they remain unresolved exactly as labeled in the sanitizer evidence.
