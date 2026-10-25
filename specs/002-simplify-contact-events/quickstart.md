# Validation Guide: Simplify Contact Events

This guide validates the implemented feature. The internal contact test, persistent-contact fixture and schema-3 tools are available. Run commands from the repository root; retained results and known sanitizer limitations are linked from [validation.md](validation.md). See [data-model.md](data-model.md) for ownership and [contracts/contact-events.md](contracts/contact-events.md) for expected behavior.

## Prerequisites and baseline

Use the existing C++20/CMake/Ninja/Python 3 toolchain and vendored dependencies. SDL tests run headless through CTest's configured environment. Record the source revision and dirty patch, compiler/build options, CPU topology, worker count/affinity, seed and timestep. Use one worker and an appropriate multiworker count for the machine; six below is the repository reference example, not a hardware assumption.

For a new baseline comparison, reproduce the recorded baseline revision plus [common-fixture.patch](evidence/common-fixture.patch) in a separate checkout before changing runtime behavior. The recorded baseline provenance is in [baseline-provenance.md](evidence/baseline-provenance.md). The current checkout implements the candidate. Its Enter/Exit-only no-op observer compiles against both versions. Preserve the exact fixture patch in both baseline and candidate source fingerprints. The fixture adds runner `--contact-pair-count`, sweep `--counts`, and `--scripted-endpoints 0|1|2`; it must report and verify the resolved number of real confirmed contacts.

```sh
export CONTACT_CAPTURE_ROOT="$(mktemp -d /tmp/platformator-contact-captures.XXXXXX)"
export CONTACT_MULTI_WORKERS=6
export CONTACT_STAGE=candidate
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
```

Capture the baseline matrix in its baseline checkout with `CONTACT_STAGE=baseline`, using its schema-2 producer and matching tooling before runtime changes. Retain all captures; do not copy a lone dynamically linked runner and assume it preserves the baseline after dependencies are rebuilt. After implementation, rebuild the same preset, set `CONTACT_STAGE=candidate`, and capture again with schema-3 tooling. Keep workload, fixture, compiler flags, seed, timestep and worker/affinity choices equivalent. Differences in schema/event counter meaning must remain labeled.

## Focused correctness and public consumers

The INTERNAL target `platformator_contact_events_test` is registered with physics/determinism labels and implemented in `tests/contact_events_test.cpp`. Its fixture observes pair-event generation before delivery without adding a public event-inspection API.

```sh
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -R 'platformator_(contact_events|physics_support|physics_geometry|callback_mutation|scene_transition_stress|queued_deletion_safety|collider_pair_cleanup)_test|platformator_regression_tests'
```

Expected outcomes:

- Zero, one and two scripted endpoints produce the same generated Enter/Exit sequence; callback totals reflect only current eligible recipients.
- Entry followed by 100 unchanged updates produces one Enter and no further contact event; separation produces one Exit; re-contact produces one new Enter.
- Adding, replacing or enabling a script midcontact creates no Enter replay. An eligible late recipient receives the actual Exit.
- False-positive candidates produce no events. Multiple cell witnesses never duplicate transitions or end a still-covered contact.
- Geometry separation, filter exclusion, deactivation and collider/object removal each close real contacts once, including when both recipients retire.
- One and several workers produce identical canonical Exit-before-Enter traces and A-before-B delivery, with normal and cleanup batches tested separately.
- Contact/support totals, trigger response changes, 128-support cases, resting stacks, sleeping/waking and geometry/manifold regressions remain correct.
- Callback mutation, exception aggregation, scene replacement/shutdown and detailed start-order/failure/queue-mutation tests retain their previous guarantees after startup extraction.

Run the full normal suite once the focused checks pass; this includes public-header builds, package consumers, scene serialization and lifecycle integration:

```sh
ctest --test-dir bin/cleanup-debug --output-on-failure -L platformator
```

Any external behavior consumer must rebuild against the new library and remove Stay overrides. No scene serialization migration is expected.

## Affected work, allocation and memory lifetime

```sh
cmake --preset cleanup-work
cmake --build --preset cleanup-work --parallel 4
ctest --test-dir bin/cleanup-work --output-on-failure \
  -R 'platformator_(runtime_work|contact_events|physics_support)_test'
cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure -L platformator -E platformator_package_smoke
cmake --preset cleanup-tsan
cmake --build --preset cleanup-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir bin/cleanup-tsan --output-on-failure \
  -L 'physics|mutation|lifecycle'
```

The work fixture includes quiet confirmed contacts with and without scripts, and five fixed-peak churn cycles that assert sixteen real contacts per cycle and retained capacity reuse. After warmup, transition comparisons and queued event records remain zero, and existing idle allocation checks remain zero. Moving pairs may require geometry/transition checks but must not cause a separate persistent-contact event scan. Record retained vector capacity/memory and allocation behavior under contact churn as well as idle. Keep probes and allocation instrumentation out of timing runs.

The ASan command above checks memory access and UB; it does not certify leak freedom. A separate leak-enabled run outside the tool sandbox reproduces the documented FluidSynth initialization leak in desktop hosts. The sandbox itself prevents LeakSanitizer thread inspection. Leak-enabled contact/support/geometry/transform hosts pass outside it. Broad TSan currently reports SDL/libdbus lock-order warnings in two desktop tests; those remain unresolved. No TSan suppression or deadlock-detector disabling was added. See [sanitizers.md](evidence/sanitizers.md) for complete counts and reports.

To check the changed physics-only cases with leak detection enabled in an environment allowing sanitizer thread inspection:

```sh
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(contact_events|physics_support|physics_geometry|transform_hierarchy)_test'
```

Package smoke rebuilds separate unsanitized SDK consumers and is covered by the normal Debug/production package checks. Report unavailable runtimes and detector warnings as limitations; skipped or failing checks are not passes.

## Benchmark format contract

```sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
ctest --test-dir bin/cleanup-benchmark --output-on-failure -L benchmark
python3 -m unittest discover -s benchmark/tests -v
```

The candidate requires schema 3 in summaries, raw samples, manifests, aggregates and console output; eight scopes, eleven counters and no Stay field. Verify listener-free event counts, cleanup Exits inside the originating complete-step sample, both-endpoint retirement, callback exceptions and out-of-frame removals. Reject schema-2/mixed/unknown/missing metrics without rewriting historical captures. See [benchmark-schema.md](contracts/benchmark-schema.md).

## Separate physics validation captures

Use each stage's matching sweep tooling and runner. Choose unused output directories under `CONTACT_CAPTURE_ROOT`. This example validates observed persistent contacts; repeat for scripted endpoints 0 and 1, then the existing broad/dense/narrow/body/churn workloads listed below.

```sh
for contact_workers in 1 "$CONTACT_MULTI_WORKERS"; do
  python3 benchmark/sweep_scaling.py \
    --runner bin/cleanup-benchmark/platformator_benchmark_runner \
    --scenario persistent_contacts --counts 10,100,1000 --scripted-endpoints 2 \
    --workers "$contact_workers" --seed 0 --dt 0.008333333333333333 \
    --warmup-frames 120 --measure-frames 600 --repetitions 1 \
    --validate-invariants \
    --raw-output-dir "$CONTACT_CAPTURE_ROOT/$CONTACT_STAGE-validate-observed-w$contact_workers" \
    --output "$CONTACT_CAPTURE_ROOT/$CONTACT_STAGE-validate-observed-w$contact_workers.csv"
done
```

These results must be labeled `validation_mode=per-step`, `timing_eligible=false`. Compare canonical contacts, support totals and body snapshots between worker counts with declared existing numerical tolerances. The test trace checks event order separately. Keep equivalent physical state between baseline and candidate despite the intentional event contract change.

## Release timing matrix

After correctness passes, finish all builds/tests and avoid competing work during captures. Use matching stage tooling, identical affinity and at least five independent process repetitions. The persistent fixture demonstrates the removed observer cost; existing script-free workloads detect unrelated regressions.

```sh
for contact_recipients in 0 1 2; do
  for contact_workers in 1 "$CONTACT_MULTI_WORKERS"; do
    python3 benchmark/sweep_scaling.py \
      --runner bin/cleanup-benchmark/platformator_benchmark_runner \
      --scenario persistent_contacts --counts 10,100,1000 \
      --scripted-endpoints "$contact_recipients" \
      --workers "$contact_workers" --seed 0 --dt 0.008333333333333333 \
      --warmup-frames 120 --measure-frames 600 --repetitions 5 \
      --raw-output-dir "$CONTACT_CAPTURE_ROOT/$CONTACT_STAGE-persistent-r$contact_recipients-w$contact_workers" \
      --output "$CONTACT_CAPTURE_ROOT/$CONTACT_STAGE-persistent-r$contact_recipients-w$contact_workers.csv"
  done
done
```

Repeat the sweep command with these existing cases, omitting `--scripted-endpoints`, retaining both worker counts, and assigning each a new output/raw directory:

| Scenario | Counts | Extra options |
| --- | --- | --- |
| `broad_phase` | `100,500,1000` | Existing uniform broad layout. |
| `broad_phase_stress` | `50,150,300` | Existing dense/skewed broad layout. |
| `narrow_phase` | `10,100,200` | Existing moving narrow layout. |
| `rigid_body_container` | `100,500,1000` | Existing body/sleep/support workload. |
| `add_and_remove` | `10,100,1000` | `--steady-count 1000` |
| `moving_add_and_remove` | `10,100,1000` | `--steady-count 1000` |

Retain source fingerprints and patches, resolved workload fields, build/hardware/worker configuration, raw per-step samples, logs and independent-run aggregates. Review complete-step and relevant nested-scope mean/p95/p99/raw tails, run spread, worker scaling and allocation/retained-memory evidence. Investigate regressions, especially event sorting under churn and solver dereference cost after removing `activeCollisions`. Do not sum nested scopes, plot validation-only captures, compare incompatible counter meanings, or claim performance improvements from complexity arguments alone.

`benchmark/capture_cleanup.py` can reproduce its existing six-case reference matrix on the specifically documented six-core topology; inspect its fixed CPU affinity before using it on another machine. It does not replace the added observer fixture.

## Completion evidence

Retain test summaries, sanitizer results/limitations, current-symbol audit, schema contract results, serial/parallel traces, baseline/candidate provenance and performance analysis with investigated regressions. A search for `onCollisionStay`, `dispatchCollisionStay`, `triggerCollisionStay`, `COLLISION_STAY`, `stayPairs`, `stayIndex`, `stayOrderDirty`, `refreshSubscriptions`, `collision_stay_event_count` and the old startup/geometry names must find no supported current runtime/tooling path. Scope the search to current source/tests/tools/docs; historical captures/specifications and ordinary English “stay” text are not removal targets.
