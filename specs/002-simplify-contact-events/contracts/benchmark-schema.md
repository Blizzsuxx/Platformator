# Benchmark Diagnostic Contract: Schema v3

**Status**: Design contract for this feature.
**Compatibility**: New captures and current tooling move together from schema v2 to v3. Archived v2 artifacts retain their original data and meaning.

## Version and shape

Set `schema_version` to `3` in run manifests, summary CSV rows, raw sample rows, console version text, and sweep aggregates. New raw headers and summary counter rows omit `collision_stay_event_count`.

The existing eight timing scopes remain:

~~~text
complete_step, physics, cleanup, scene_transition, render,
broad_phase, narrow_phase, resolve_collisions
~~~

The eleven counters are:

~~~text
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
~~~

Scope timing boundaries, units, metadata/provenance requirements, validation-mode labels, and independent-run spread calculations remain as documented in [benchmark/README.md](../../../benchmark/README.md).

## Event counter meaning

| Counter | Meaning |
| --- | --- |
| collision_enter_event_count | Generated absent-to-present confirmed-contact transitions enqueued during this recorded complete step. |
| collision_exit_event_count | Generated present-to-absent confirmed-contact transitions enqueued during this recorded complete step, including cleanup/removal exits. |

A pair transition counts once even if no script receives it, several behaviors receive it, or a delivery callback throws. A retired recipient does not suppress generation. Delivery and queue clearing do not increment or decrement counts.

Frame start initializes counters to zero. Count at the manager's single enqueue path, while a recording frame is active. Do not rescan/reset the event totals in narrow phase. Events generated during idle mutation outside a recorded step do not get attributed to the next recorded sample.

One completed contact interval contributes one enter and one exit over its measured lifetime; intervals that start before warmup/measurement or end after capture naturally need not be balanced within a single capture window. Do not reject those captures solely for unequal aggregate enter/exit counts.

Unchanged contacts contribute zero events in later samples even if awake, scripted, stationary, sleeping, or triggers. Active contact counts continue describing all confirmed contacts rather than only solver contacts.

## Writer and reader migration

Update these together:

- `src/benchmark.h/.cpp`: counter enum/name arrays, manifest and CSV versions, console text, and recorded metric descriptions.
- `src/physicsmanager.cpp`: transition counters at enqueue; remove the narrow-phase recount and Stay branch.
- `benchmark/sweep_scaling.py`: exact counter set, accepted version, raw/summary validation, aggregate version, and error wording.
- `benchmark/plot_scaling.py`: accepted aggregate version and diagnostic wording.
- `benchmark/tests/test_sweep_scaling.py` and `tests/benchmark_contract_test.cpp`: schema fixtures, complete metric expectations, generation-count coverage, and rejection cases.
- `benchmark/capture_cleanup.py`: verify integration with the migrated imported parser and aggregation helpers; change only assumptions actually affected.
- Current benchmark documentation and feature validation instructions.

Current tools reject v2 and mixed-version input with a clear unsupported-version message; this feature does not add a general multi-version parser. Never fill a removed Stay field with zero or silently interpret old v2 enters as script-independent counts.

## Historical comparison

Leave `benchmark/baselines/`, archived `benchmark/results/`, historical specifications, captured manifests/CSVs, and historical source snapshots intact. Existing references to those results retain their schema/version labels.

For before/after timing evidence, use the baseline source and its original v2 tools for the baseline, and v3 tools for the new source, with equivalent workloads, workers, seed, dt, steps, instrumentation, and timing boundaries. Preserve both artifact sets and provenance. Compare unchanged timing scopes explicitly; event-counter values may differ by the intended semantics and must not be pooled as one series.

Do not claim a measured speedup until correctness and comparable captures establish it.

## Required contract checks

1. New summary/raw/manifest/aggregate outputs consistently identify version 3, eight scopes, and eleven counters.
2. No Stay counter is emitted; missing required metrics, duplicate/unknown metrics, v2 input, mixed versions, and truncated captures fail validation.
3. A scriptless contact generates a counted enter and exit; adding behaviors does not multiply either count.
4. An exit generated during final cleanup appears in the same complete-step sample.
5. An idle exit outside recording does not affect the next frame's counts.
6. Multiple dispatch/cleanup batches in one step accumulate rather than overwrite event totals.
7. Existing completeness, rendering, metadata, validation-mode, and sample-capacity checks continue to pass.
