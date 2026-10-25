# Benchmark Schema 3: Contact Transitions

## Version and boundaries

Set `schema_version` to **3** in manifest JSON, summary CSV rows, raw CSV rows, aggregate CSV rows and console reporting. Update producer, sweep parser, capture tooling where applicable, plotting checks, C++ contract tests, Python fixtures/tests and current benchmark documentation as one compatibility change.

Preserve the existing file roles and timing boundaries: summary `<run>.csv`, raw `<run>.samples.csv`, provenance `<run>.manifest.json`; complete-step includes final reconciliation and retirement, physics and cleanup remain nested scopes, and rendering remains separately sampled. Scope times must not be summed as total wall time. Preserve all source/build/hardware/worker metadata, capture completeness checks, physical invariants and timing-eligibility labels.

## Metric inventory

The eight timing scopes remain:

```text
complete_step, physics, cleanup, scene_transition,
render, broad_phase, narrow_phase, resolve_collisions
```

The eleven counters are:

```text
object_count
occupied_cell_count
candidate_pair_count
pending_narrow_phase_pair_count
active_collision_count
awake_dynamic_body_count
queued_add_count
queued_sync_count
queued_remove_count
collision_enter_event_count
collision_exit_event_count
```

Remove `collision_stay_event_count` from the recorder enum/name arrays, summary rows, raw columns, aggregate columns, console output, parser required sets and current fixtures. Do not preserve it as zero or silently synthesize it.

`collision_enter_event_count` and `collision_exit_event_count` count **generated collider-pair events in the sampled complete step**, not eligible scripts, behaviors, delivered callbacks or contacts still active at sample end. Increment at manager-owned enqueue for both geometry and removal transitions, including cleanup and both-endpoint retirement inside the measured step. Frame begin resets counters; out-of-frame mutations do not leak into the next step or create extra samples. Delivery failure does not undo a generated count. `active_collision_count` retains its existing confirmed-contact snapshot meaning; the other nine counters retain their existing definitions and sampling points.

## Reader and history rules

- Current tools accept schema 3 consistently across all files in a run and its aggregates. Reject schema 2, missing metrics, unknown metrics, duplicate summary metrics, mixed versions, missing required raw columns, incomplete captures and contradictory instrumentation labels with an actionable message.
- Keep archival schema-2 and earlier captures unchanged. Historical readers are available at their recorded source/tool revision. Do not fabricate transition totals from historical observed-contact counts or rewrite old files under a new schema number.
- Cross-version baseline comparisons may compare timing scopes only after confirming unchanged workload and timing boundaries. Do not equate old Enter/Exit counter semantics with new generated-transition semantics; label this distinction in results.
- Plotting accepts only complete schema-3 timing-eligible aggregates and retains the requirement for at least five independent repetitions. Validation-only results stay excluded from performance plots.

## Contract checks

1. A no-script pair entering and later exiting contributes one count to each relevant measured step.
2. The same physical transition with zero, one or two scripted endpoints has the same pair count despite differing callback totals.
3. Quiet persistent contacts contribute zero Enter/Exit counts after entry, including sleeping contacts.
4. Exit produced during cleanup contributes to that same complete step; out-of-frame removal contributes to no later sample.
5. Both endpoints retiring, script replacement and callback exceptions do not convert pair counts to callback counts.
6. Producer output has eight scopes, eleven counters, schema 3 throughout and no Stay field. Reader fixtures verify rejection of schema 2, mixed versions and removed Stay metrics.
7. Recorder storage reuse, raw sample counts, invariants, build provenance, rendering separation and output error behavior continue to pass their existing contract tests.
