# Validation and Evidence Contract

Follow [repository methodology](../../../benchmark/README.md). This document specifies future execution evidence; planning has not run tests, sanitizers, or performance captures.

## Baseline and common fixtures

Record the current source commit, staged/unstaged runtime changes, relevant untracked inputs, build options/compiler, dependencies, hardware, CPU affinity, worker limits, and fixture hash. Preserve an isolated source snapshot and binaries before production edits. Do not use the historical `7906959` parent or a previous feature's results as this baseline.

Characterization and the small benchmark probe must be identical in baseline and candidate. Freeze baseline after adding those fixtures without production changes. Capture source/build fingerprints for both. A later code or fixture correction requires rebuilding and recapturing the affected matched groups; never replace old evidence silently.

## Required behavior and work evidence

Cover J1–J6 and applicable edge cases using the existing hosts plus focused gap coverage. Controlled timer/group/wake scenarios establish expected results on the current baseline. Compare exact callback/identity contracts and declared controlled numerical tolerances; validate finite/stable physical state and support/contact invariants in unconstrained simulations. Do not reject merely because multiworker trajectories differ after canonical solver ordering was removed.

Work-probe runs remain separate from timing. Preserve warmed idle zero-allocation/zero-unaffected-work checks, affected-subtree-only publication, fixed-peak storage reuse, queued retirement cancellation, and unrelated-script/physical-work separation. Allocation counts from the existing C++ operator-new probe are scoped evidence, not proof about all system/SDL allocations. Record retained vector capacities, fixed-peak trends, and applicable process memory observations separately.

## Controlled runtime probe (planned implementation interface)

Add `benchmark/runtime_cleanup_probe.cpp` and a `platformator_runtime_cleanup_probe` target only when benchmarks and tests are enabled. It links the current recorder and engine, without adding production APIs or entering the engine source list.

Proposed CLI: `--workload quiet|mixed|wake_burst|connected|hierarchy|lifecycle --count N --workers W --warmup-frames N --measure-frames N --seed N --csv-output PATH [--validate-invariants]`. Use the existing explicit dt of 1/120; include it in the manifest. Each invocation is one independent process. Counts and workload parameters must resolve identically for baseline and candidate.

| Mode | Controlled workload / validity requirement |
| --- | --- |
| quiet | Static objects plus gravity-free sleepers; verify sleeping/active populations before capture and unchanged idle work afterwards. |
| mixed | Fixed total count, 1/10 active gravity-free bodies and remainder sleepers, deterministic disjoint placement. Record actual active count rather than inferring it. |
| wake_burst | Deterministic fixed subset (1/10) transitions between sleeping and explicit waking at recorded intervals, with real movement/force work when awake. Apply operations inside the caller hook. |
| connected | Gravity-free contacting chain/group; verify actual connected contact graph, quiet timers, and sleep transitions. Include an all-quiet state and a moving-member control. A fixture that does not establish connectivity or validity fails rather than substituting an unrelated pile. |
| hierarchy | Fixed shallow hierarchy; edit a root twice per selected step and overlapping child state, preserving the same affected population and parent-first consequences. |
| lifecycle | Fixed-peak callback-time create/remove/component-replacement cycles and periodic prepared scene replacement. Precreate fixture files outside capture; preserve request/commit/destructor work inside the existing step boundary and report replacement steps separately. |

The probe uses existing schema-3 scopes/counters and additional fixture metadata within existing metadata fields. Reserve recorder capacity after warmup, before measured steps; write outputs and expensive checks outside timed scopes. In correctness mode run per-step invariants outside those scopes and mark timings ineligible. Validate sample counts, populations, connectivity where required, finite values, and expected transitions. Reuse existing physical-oracle logic where available; any probe-specific checks must be identical across stages.

For controlled modes use counts 100/1000, 120 warmup and 600 measured steps, matching current sampling practice. These are sampling choices, not performance targets. Connected-group setup must prove its stated population and graph on the baseline before timing; extend setup only with matched changes if it fails.

## Spatial and consumer guards

Use existing runner/sweep interfaces for these small/large controls:

| Scenario | Counts | Options / coverage |
| --- | --- | --- |
| broad_phase | 100,1000 | Uniform movement and collider synchronization |
| broad_phase_stress | 50,300 | Dense/skewed distribution |
| rigid_body_container | 100,1000 | Mixed shapes and physical response; report actual sleep state |
| add_and_remove | 10,1000 | `--steady-count 1000`; lifecycle/collider churn |
| moving_add_and_remove | 10,1000 | `--steady-count 1000`; migration plus churn |
| persistent_contacts | 10,1000 | Repeat scripted endpoints 0/1/2; quiet transition-production guard |

Preserve existing geometry/completeness tests. No foundational broad-phase algorithm change is planned, so new endpoint/chunk test infrastructure is unnecessary. Rendered entry behavior and package compatibility are validated separately from headless timing; do not infer render speed from these captures.

## Timing, worker scaling, and reporting

Use worker counts 1, 2, and a recorded larger available count (reference six where available); deduplicate choices on smaller machines. Pin appropriate physical CPUs and retain topology/affinity. Five independent processes per group, 120 warmup/600 measured steps and fixed seed 0 follow current methodology. Alternate matched baseline/candidate groups in a retained, reproducibly shuffled schedule; do not overlap captures with builds or other timing jobs.

Report complete-step and relevant nested-scope means, median, p95/p99/max from retained step samples, independent-run variation, and worker ratios. Label how tail statistics are aggregated; do not sum nested scopes or infer equivalence from overlapping ranges. Startup/shutdown wall-clock observations, if captured, have separate boundaries and cannot be labeled complete-step samples.

Investigate every timing/work/allocation/memory/scaling regression. Preserve raw CSV, samples, manifests, logs, checksums, and the disposition of follow-up captures. Missing or inconclusive evidence stays unverified. No speedup quota, automatic tolerance percentage, or retained complexity is justified by assertion alone.

## Safety and human review

Run focused ASan/UBSan with leak detection and relevant TSan workers/lifecycle selections. Prior [sanitizer findings](../../003-simplify-physics-bookkeeping/evidence/sanitizers.md) include FluidSynth initialization leaks, LeakSanitizer environment restrictions, and SDL/libdbus lock-order warnings. Compare actual new stacks/results; prior findings are context, not exemptions. Unavailable instrumentation and known unresolved errors are not passes. Do not suppress detectors to turn a check green.

Final evidence includes a compact remove/simplify/retain inventory and six code-backed before/after walkthroughs. Report separately: automated correctness, performance acceptance, safety limitations, and actual maintainer readability findings. Keep readability pending until the maintainer reviews the final code. Do not edit the specification-quality checklist to represent implementation progress.
