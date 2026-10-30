# Validation and compact measurement guide

Run from the repository root. These commands cover the targeted correctness, sanitizer, and bounded comparison used for this feature.

## Correctness

```sh
cmake --preset cleanup-debug
cmake --build bin/cleanup-debug --parallel 4 --target \
  platformator_transform_hierarchy_test platformator_destruction_contract_test \
  platformator_scene_io_contract_test platformator_physics_geometry_test \
  platformator_physics_bookkeeping_test platformator_physics_support_test \
  platformator_runtime_step_test
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -R 'platformator_(transform_hierarchy|destruction_contract|scene_io_contract|physics_geometry|physics_bookkeeping|physics_support|runtime_step)_test$'
```

The focused all-target `cleanup-debug` build has a pre-existing render-culling index failure because that test expects hooks not enabled in this configuration. The selected targets above isolate the feature checks; this does not make the unrelated index test pass.

## Sanitizer

```sh
cmake --preset cleanup-asan
cmake --build bin/cleanup-asan --parallel 4 --target \
  platformator_transform_hierarchy_test platformator_destruction_contract_test \
  platformator_scene_io_contract_test platformator_physics_geometry_test \
  platformator_physics_bookkeeping_test platformator_physics_support_test \
  platformator_runtime_step_test
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(transform_hierarchy|destruction_contract|scene_io_contract|physics_geometry|physics_bookkeeping|physics_support|runtime_step)_test$'
```

LeakSanitizer cannot inspect threads in this environment's ptrace setup. The command disables leak detection while retaining AddressSanitizer and UBSan checks; it is not a leak validation.

## Bounded before/after pilot

Use the recorded baseline and candidate inputs and their matched `bin/transform-setter-*` builds:

```sh
python3 benchmark/capture_transform_setter.py \
  --baseline-build bin/transform-setter-baseline \
  --candidate-build bin/transform-setter-candidate \
  --baseline-work-build bin/transform-setter-work-baseline \
  --candidate-work-build bin/transform-setter-work-candidate
```

This command created the current [raw pilot](evidence/pilot.raw.json). The capture script refuses to overwrite that file; a future capture needs an explicitly designated output directory.

The script uses three repetitions, five warmup and 30 measured frames for step workloads, and enforces at most 600 seconds and 50 MiB of output. It writes compact raw samples and identities in `evidence/pilot.raw.json`. Do not increase those limits or run a full workload matrix without an explicit request. On failure or budget stop, retain and report the partial result; diagnose it without an automatic retry.

The fixture covers small/large flat, deep and broad reads, deep ancestor edits with eight reads per edit, unchanged and changed shape/body/motion setter controls, and complete steps with repeated velocity/angular-velocity assignments. A parent/child kinematic pair moves alongside dynamic and kinematic bodies. Complete-step and physics subsystem scopes run with one and six workers. The physics clock covers `applyPhysics` and `applyMovement`; the follow-up synchronization frame is outside that timer. Work counters and growth observations are separate from uninstrumented timing.

## Compatibility and current evidence limits

Repeated material or gravity assignments no longer wake a body. Code that intentionally needs a wake must call `Rigidbody::wakeUp()` explicitly. Velocity and angular-velocity assignments retain their threshold checks, and body-type assignment retains reset behavior.

The exploratory pilot found large direct gains for deep reads and unchanged setters, plus 11–14% lower medians for the host-call composite of repeated unchanged motion setters followed by a complete step. Those setters ran before the runtime step; an in-step callback workload remains planned. Plain complete-step medians were 2–6% slower and physics-scope medians 7–8% slower; those scopes are only a few microseconds and showed run variation. The physics-scope cause remains unresolved. Retain the candidate provisionally for its targeted workloads, with no overall game-speedup or multicore-scaling claim. See [evidence results](evidence/RESULTS.md) for per-case samples and uncovered workloads.
