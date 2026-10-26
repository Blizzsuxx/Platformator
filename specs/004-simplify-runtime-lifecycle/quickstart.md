# Validation Guide: Simplify Runtime Lifecycle

The technical implementation and validation are complete; actual maintainer readability review remains pending. The `platformator_runtime_cleanup_probe` target exists in benchmark-enabled test builds. See [validation.md](validation.md) for executed results and limitations, and [capture locations](evidence/capture-locations.md) for actual baseline/candidate artifacts. The baseline commands below also serve as a reproduction guide; this invocation retained its source snapshots under the ignored capture root.

Use [runtime contracts](contracts/runtime-lifecycle.md) for expected behavior and [evidence rules](contracts/validation-evidence.md) for workloads, boundaries, and reporting. Keep the specification-quality checklist unchanged during implementation.

## Prerequisites and baseline

Use the current C++20/CMake/Ninja toolchain and bundled dependencies. On Linux use dummy SDL backends for desktop tests and physics-only settings for controlled headless workloads. Record available physical CPUs and worker limits; reference six-worker commands below must be adjusted if unavailable.

```bash
cd /home/user/Projects/Platformator
export RUNTIME_CLEANUP_PROJECT="$PWD"
export RUNTIME_CLEANUP_CAPTURE="$(mktemp -d /tmp/platformator-runtime-cleanup.XXXXXX)"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export SDL_RENDER_DRIVER=software

git rev-parse HEAD > "$RUNTIME_CLEANUP_CAPTURE/origin-commit.txt"
git status --porcelain=v1 > "$RUNTIME_CLEANUP_CAPTURE/input-status.txt"
git diff --binary HEAD > "$RUNTIME_CLEANUP_CAPTURE/input-changes.patch"
lscpu > "$RUNTIME_CLEANUP_CAPTURE/cpu.txt"
taskset -pc $$ > "$RUNTIME_CLEANUP_CAPTURE/allowed-cpus.txt"
```

Before production edits, add characterization and the common benchmark probe. Snapshot the current source, including those fixtures and relevant untracked inputs, into an isolated directory outside the checkout. The following copies current source inputs rather than assuming HEAD contains the working tree. Keep the resulting snapshot and binaries until the comparison is archived.

```bash
python3 - <<'PY'
import os
import shutil
import subprocess
from pathlib import Path

source = Path(os.environ['RUNTIME_CLEANUP_PROJECT'])
target = Path(os.environ['RUNTIME_CLEANUP_CAPTURE']) / 'baseline-source'
target.mkdir()
inputs = ['CMakeLists.txt', 'CMakePresets.json', 'cmake', 'include', 'src',
          'vendored', 'assets', 'tests', 'benchmark', 'examples']
paths = subprocess.check_output(
    ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '--deduplicate', '-z', '--', *inputs],
    cwd=source).decode().split('\0')
for name in filter(None, paths):
    current = source / name
    if not current.is_file() and not current.is_symlink():
        continue
    destination = target / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(current, destination, follow_symlinks=False)
print(target)
PY
export RUNTIME_CLEANUP_BASELINE="$RUNTIME_CLEANUP_CAPTURE/baseline-source"
```

Record checksums of the fixture and source inputs before changing production code. Since the snapshot has no Git metadata, preserve `origin-commit.txt`, the input patch/status and source fingerprint alongside its captures; do not fabricate a clean commit identity. Preserve any additional local build inputs if the checkout uses them.

## Correctness and public compatibility

Configure/build and run the applicable current hosts on both source trees. Commands below apply to the baseline first; repeat with `RUNTIME_CLEANUP_SOURCE=$RUNTIME_CLEANUP_PROJECT` for the candidate after implementation.

```bash
export RUNTIME_CLEANUP_SOURCE="$RUNTIME_CLEANUP_BASELINE"
cd "$RUNTIME_CLEANUP_SOURCE"
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -L 'lifecycle|mutation|physics|transform|scene|reference'
ctest --test-dir bin/cleanup-debug --output-on-failure
```

Expected: J1–J6 contracts pass. The full ordinary run also covers source headers, runner failures, benchmark tooling, and the package consumer; instrumentation-dependent tests can intentionally skip in this build and must be run in their applicable configurations below. Do not count skips as executed evidence.

Add focused assertions to existing hosts for no-op awake waking preserving its timer, unequal timers in a connected group, controlled impact wake/resumed movement, both rendered entry paths, and RuntimeAccess during shutdown/no-runtime/recreation. Establish baseline behavior before refactoring. Existing callback, destruction, support transfer, hierarchy, and scene failure checks remain required. An unrelated baseline defect is a recorded finding, not permission to change gameplay silently.

## Work and allocation checks

```bash
cmake --preset cleanup-work
cmake --build --preset cleanup-work --parallel 4
bin/cleanup-work/platformator_runtime_work_test 1
bin/cleanup-work/platformator_runtime_work_test 2
bin/cleanup-work/platformator_runtime_work_test 6
```

Expected existing checks include 100 warmed idle steps without added C++ allocations or unrelated work, exactly 21 transform visits for the existing dirty-root fixture, cancellation before terminal-body integration, no physics refresh from unrelated script replacement, and fixed-peak storage reuse. Extend with controlled waking/hierarchy/lifecycle probes only where needed. Keep work-build durations out of performance aggregates.

## Safety checks

Run from each source tree as applicable, preserving full outputs and statuses:

```bash
cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(physics_bookkeeping|physics_support|transform_hierarchy|callback_mutation|destruction_contract|runtime_foundation|runtime_recreation|scene_transition_contract|scene_transition_stress|queued_deletion_safety)_test'

cmake --preset cleanup-tsan
cmake --build --preset cleanup-tsan --parallel 4
ctest --test-dir bin/cleanup-tsan --output-on-failure \
  -L 'physics|mutation|lifecycle|transform' -E '^platformator_package_smoke$'
```

Compare diagnostics with the [previous sanitizer findings](../003-simplify-physics-bookkeeping/evidence/sanitizers.md). Leak-inspection restrictions, FluidSynth leaks, and SDL/libdbus lock-order warnings remain explicit limitations unless reproduced and resolved. A separate detector-disabled diagnostic does not replace the required enabled run. Do not add suppressions or mark unavailable checks passed.

## Release captures

Finish all builds and correctness runs before final timing. In both baseline and candidate:

```bash
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
ctest --test-dir bin/cleanup-benchmark --output-on-failure -L benchmark
```

The implemented `platformator_runtime_cleanup_probe` is built with the Release benchmark configuration. Validate its population/transition claims before timing. This example captures a quiet workload; repeat both stages, all six modes, counts 100/1000 and selected worker counts according to the [matrix](contracts/validation-evidence.md).

```bash
export RUNTIME_CLEANUP_STAGE=baseline
export RUNTIME_CLEANUP_SOURCE="$RUNTIME_CLEANUP_BASELINE"
mkdir -p "$RUNTIME_CLEANUP_CAPTURE/$RUNTIME_CLEANUP_STAGE/validate"
"$RUNTIME_CLEANUP_SOURCE/bin/cleanup-benchmark/platformator_runtime_cleanup_probe" \
  --workload quiet --count 1000 --workers 1 --seed 0 \
  --warmup-frames 120 --measure-frames 600 --validate-invariants \
  --csv-output "$RUNTIME_CLEANUP_CAPTURE/$RUNTIME_CLEANUP_STAGE/validate/quiet-n1000-w1.csv"
```

Expected: exact resolved populations, valid invariants, complete sample count, schema 3, and correctness-mode timing marked ineligible. Connected-group mode must prove the contact graph; rigid-body warmup alone does not prove a sleeping population. Run the matching candidate invocation with `RUNTIME_CLEANUP_STAGE=candidate` and `RUNTIME_CLEANUP_SOURCE=$RUNTIME_CLEANUP_PROJECT`.

The existing sweep is runnable without a new scenario interface. This example captures a spatial correctness guard in a fresh directory:

```bash
python3 "$RUNTIME_CLEANUP_SOURCE/benchmark/sweep_scaling.py" \
  --runner "$RUNTIME_CLEANUP_SOURCE/bin/cleanup-benchmark/platformator_benchmark_runner" \
  --scenario broad_phase --counts 100,1000 --workers 1 --seed 0 \
  --warmup-frames 120 --measure-frames 600 --repetitions 1 --validate-invariants \
  --raw-output-dir "$RUNTIME_CLEANUP_CAPTURE/$RUNTIME_CLEANUP_STAGE/broad-w1-validate" \
  --output "$RUNTIME_CLEANUP_CAPTURE/$RUNTIME_CLEANUP_STAGE/broad-w1-validate.csv"
```

For timing use fresh destinations, omit `--validate-invariants`, and use five independent processes per group. The sweep accepts `--repetitions 5`; invoke the purpose-specific probe once per repetition with unique output files. Pin each command to recorded allowed CPU sets via `taskset -c`, using one CPU for one worker and appropriate sets for larger worker counts. Keep the same fixture/seed/work/affinity and alternate matched baseline/candidate jobs in a retained schedule. Do not copy historical hardcoded CPU masks from other capture scripts.

Repeat existing spatial/churn/persistent-contact scenarios in the matrix. Inspect manifest fingerprints, instrumentation, resolved populations, sample counts, and timing eligibility before aggregating. Report whole-step and relevant nested-scope means/tails/variation and worker ratios; retain raw samples, logs and checksums. Investigate every regression and preserve missing/inconclusive evidence as unverified.

## Final code review

Review every changed function, including lambdas/helpers, for braces, separate statements, and blank lines between logical stages. Do not mechanically reformat unrelated files or use regex matches as proof of complete syntax/style compliance.

Produce a compact remove/simplify/retain inventory and six before/after code-backed walkthroughs. Record actual checks and limitations in `validation.md` during implementation and link durable capture locations. Keep correctness, cost acceptance, safety limitations, and maintainer readability findings distinct. Human readability acceptance remains pending until the maintainer reviews the resulting code.
