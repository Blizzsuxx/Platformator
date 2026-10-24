# Validation Guide: Simplify Contact Events

These are implementation acceptance instructions. The plan itself does not implement runtime changes or run the behavioral checks. The new contact-transition test target becomes available when its implementation is added.

Run commands from the repository root. Use a C++20 compiler, Ninja, CMake 3.21+ for the existing presets, Python 3, and the repository's SDL system development prerequisites. Dependencies are vendored. Headless CTest entries configure dummy SDL drivers.

Read [the contact contract](contracts/contact-events.md), [diagnostic contract](contracts/benchmark-schema.md), and [data model](data-model.md) for expected semantics.

## 1. Preserve baseline evidence

Before source edits, record `git rev-parse HEAD` and `git status --short`. The planning baseline is `0ca09758f5148426139a923a1e7b9404471eac67`. Keep current source/tooling available for equivalent pre-change captures. Existing historical results are not a substitute for a matching before/after workload.

Run the existing focused tests when establishing the baseline. New behavior tests should demonstrate the script-dependent enter defect before its fix; existing Stay assertions are expected to change with the deliberate contract removal. Do not weaken support, geometry, startup, or lifetime assertions.

## 2. Build and run focused correctness checks

~~~sh
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure \
  -R 'platformator_(contact_transition_contract_test|physics_support_test|physics_geometry_test|collider_pair_cleanup_test|queued_deletion_safety_test|callback_mutation_test|scene_transition_contract_test|scene_transition_stress_test|destruction_contract_test|regression_tests)$'
~~~

Expected results after implementation:

| Area / requirements | Required observation |
| --- | --- |
| FR-001–003, FR-010 | Identical generated pair-event traces with zero, one-sided, and two-sided script availability; absent/present changes only; canonical serial/parallel order. |
| FR-004–005 | Each eligible behavior receives one callback; empty/pending scripts receive none; late attachment gets no replay and can receive the later exit. |
| FR-006, FR-012 | Stay declarations/definitions/dispatch and active consumer overrides are removed. Maintained test callback families use the remaining contract. |
| FR-007–008 | 100 unchanged steps produce no further notifications for moving, stationary, sleeping, and trigger contacts. Filter/coalesced toggle, support/body replacement, response-change, and re-entry expectations hold. |
| FR-009 | Survivor retirement exits keep the other collider readable; both-retired callbacks are suppressed; no stale callback after replacement/clear or exceptions. |
| Startup cleanup | One-attempt resolve/start, failed-entry promotion, additions during startup, next-pass ordinary additions, cancellation, and interrupted pending suffix retain their exact existing assertions. |
| Geometry/body consolidation | Circle/circle, circle/box corner/containment/offset, rotated boxes, rounded-SAT gap, manifold features, 128 support counts, support-loss waking, tall stacks, and wide piles remain correct. |

The new focused test is an INTERNAL CTest target using existing PhysicsManager access. At controlled physics test boundaries, inspect queued pair events before delivery. This proves generation for scriptless contacts without adding production observer APIs. Public/runtime callback traces separately prove eligibility and lifecycle delivery.

## 3. Run the complete runtime suite and consumers

~~~sh
ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke
cmake --preset cleanup-debug -DPLATFORMATOR_BUILD_EXAMPLES=ON
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure -R platformator_package_smoke
~~~

The example game and installed/public-header consumer must compile against the enter/exit-only surface. Remove obsolete Stay overrides where they exist; the current Mario example already uses enter callbacks.

## 4. Verify affected work and allocation reuse

~~~sh
cmake --preset cleanup-work
cmake --build --preset cleanup-work --target platformator_runtime_work_test --parallel 4
ctest --test-dir bin/cleanup-work --output-on-failure -R platformator_runtime_work_test
~~~

Extend the existing warmed-up test with stable confirmed contacts that have eligible scripts, empty scripts, and sleeping bodies. Verify:

- Unchanged contacts generate zero events and cause zero event-pair visits.
- Script-only attachment/removal/replacement causes zero physics adjacency refresh visits; actual startup/reconciliation work is allowed.
- A real wake/sleep or response change still refreshes the affected solver adjacency and retains correct support/sleep behavior.
- Warming and reusing buffers preserves zero allocations in a mutation-free idle step.
- Existing transform-subtree, force/movement, body sleeping, and lifecycle affected-work assertions remain intact.

Remove the old `stayPairs` probe. Add only small test-only counts at actual transition and solver-adjacency work boundaries as needed to distinguish the behavior above. Work probes remain disabled for timing runs.

## 5. Verify sanitizer and failure paths

~~~sh
cmake --preset cleanup-asan
cmake --build --preset cleanup-asan --parallel 4
ctest --test-dir bin/cleanup-asan --output-on-failure \
  -R 'platformator_(contact_transition_contract_test|physics_support_test|physics_geometry_test|callback_mutation_test|queued_deletion_safety_test|scene_transition_stress_test|collider_pair_cleanup_test)$'
~~~

Include callbacks that throw after requesting retirement and cases that destroy/replace the other endpoint during enter/exit delivery. Pending enter manifold pointers and exit endpoints must remain valid through dispatch; error cleanup must not free referenced storage while records remain pending.

Use the existing `cleanup-tsan` preset for supplemental serial/multiworker checks when the host and instrumented dependencies support it. Record exact environmental failures or existing sanitizer defects separately; do not report a clean sanitizer run if it did not complete.

## 6. Verify diagnostic schema and generation counts

~~~sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
ctest --test-dir bin/cleanup-benchmark --output-on-failure \
  -R 'platformator_benchmark_(contract|sweep)_test$'
python3 -m unittest discover -s benchmark/tests -p 'test_*.py'
~~~

The C++ contract test must check zero-recipient enters/exits, multi-behavior non-multiplication, cleanup-generated exits accumulated into the same step, multiple delivery batches, and idle transitions excluded from future recorded frames.

The Python fixtures must cover schema v3 summary/raw/manifest/aggregate consistency, absence of Stay, exact eight-scope/eleven-counter sets, and rejection of v2/mixed versions, missing/duplicate/unknown metrics, or incomplete captures. Existing metadata, rendering, timing-eligibility, and sample-capacity checks remain meaningful.

## 7. Validate physical equivalence before timing

Use a new output directory for each capture; the tools refuse overwriting earlier evidence. Example serial/multiworker narrow-phase correctness runs:

~~~sh
for contact_workers in 1 6; do
  python3 benchmark/sweep_scaling.py \
    --runner bin/cleanup-benchmark/platformator_benchmark_runner \
    --scenario narrow_phase --counts 10,100,200 --workers "$contact_workers" --seed 0 \
    --warmup-frames 120 --measure-frames 600 --dt 0.008333333333333333 \
    --validate-invariants --repetitions 1 \
    --output "/tmp/platformator-contact-v3-validation/narrow-w$contact_workers.csv"
done
~~~

Repeat applicable existing broad-phase, dense, rigid-body, and static/moving churn scenarios using the populations in [the benchmark methodology](../../benchmark/README.md). Compare serial/multiworker snapshots with its declared tolerances and the existing `capture_cleanup.py` comparison helper: support/sleep/identity/features match exactly; physical floats use the existing tolerances. Keep the exact 128-support regression's tighter 1e-5 comparison.

Validation captures must be marked `per-step` and `timing_eligible=false`. Do not use their durations for performance claims.

## 8. Capture comparable Release timings

After correctness passes and build/test work finishes, make fresh boundary-only captures with at least five independent repetitions. For example:

~~~sh
for contact_workers in 1 6; do
  python3 benchmark/sweep_scaling.py \
    --runner bin/cleanup-benchmark/platformator_benchmark_runner \
    --scenario narrow_phase --counts 10,100,200 --workers "$contact_workers" --seed 0 \
    --warmup-frames 120 --measure-frames 600 --dt 0.008333333333333333 \
    --repetitions 5 \
    --output "/tmp/platformator-contact-v3-timing/narrow-w$contact_workers.csv"
done
~~~

Also cover sparse/dense broad-phase, resting rigid-body populations, and static/moving churn. Compare equivalent unchanged timing scopes against baseline captures made with the original v2 tooling. Preserve each source fingerprint, configuration, worker/affinity setting, seed, dt, raw sample set, and manifest. Pin processes consistently when collecting publishable timings.

The expected structural improvement is no observer traversal/invalidation and no mirrored active-collision collection. Report measured results and workload differences; no universal speedup percentage is an acceptance requirement. A full reproduction of the historical 180-process study is optional unless needed to support a broader performance claim.

## 9. Final source and artifact review

~~~sh
rg -n 'onCollisionStay|triggerCollisionStay|dispatchCollisionStay|COLLISION_STAY|stayPairs|stayIndex|stayOrderDirty|enteredStep|refreshSubscriptions|triggerStartedBehaviors|NarrowPhaseLocalResults' \
  src include tests examples
rg -n 'collision_stay_event_count|CollisionStayEventCount' \
  src benchmark/sweep_scaling.py benchmark/plot_scaling.py benchmark/tests
git diff --check
~~~

Both searches should have no remaining active implementation/consumer matches; ripgrep's no-match exit status is 1. Negative contract assertions mentioning removed names are acceptable only where they explicitly verify absence. Feature docs and immutable historical results may still discuss Stay.

Verify the three old fragment files are gone, geometry has no manager/script/event dependency, startup has no subscription side effect, body wake/sleep retains solver refresh, and v3 diagnostic output is documented. Record actual test commands, outcomes, captures, and limitations before declaring implementation complete.
