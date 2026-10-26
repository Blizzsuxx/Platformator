# Contract: Validation and Human Review Evidence

## Required evidence, without a new reporting framework

Implementation produces concise `evidence/` reports and a feature `validation.md`. Preserve machine-readable raw benchmark outputs in a new ignored capture directory, recording its exact path, source snapshots/patches and checksums. Avoid duplicating the same prose across reports; the final review links to raw evidence and the eight walkthroughs. This plan does not create tasks.md or claim implementation checks have run.

## Fresh baseline

Planning source: `f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba`. At implementation start, record actual HEAD, dirty patches and source fingerprints before runtime edits. If source changed since planning, review the delta and update the inventory before freezing the baseline. Use a separate complete source/build tree so candidate rebuilds cannot overwrite baseline libraries. Record vendored dependency versions and any common fixture patch. Do not copy only a dynamically linked runner.

Both stages use current schema 3 with the same eight scopes, eleven counters, validation labeling and resolved fixture parameters. Preserve strict current readers and archived captures. Feature 002's old schema-2 baseline cannot substitute for this baseline. Its candidate timings are historical context, not automatically a current control. Keep benchmark instrumentation enabled in both timing builds, work/debug/sanitizer instrumentation disabled, and do not alter measured boundaries in this refactor.

## Correctness and queue/support oracle

Existing public and INTERNAL tests remain. Add targeted regressions before refactoring where current assertions do not cover:

- Duplicate enqueue, first/middle/last swap-remove cancellation, removed-before-evaluation, consumed-then-requeued pair, and zero pending batch; include ≥128 changed pairs to exercise the parallel path and selected worker counts.
- Support begin/end with unchanged recipient, replacement by a different body, reference/endpoint support swaps, geometry deletion, multi-pair net loss/gain cancellation, and final-witness pair deletion. Check committed totals, sleeping/waking and exact generated event histories, not just absence of crashes.
- Static/kinematic/dynamic changes, kinematic zero/nonzero linear/angular velocity, repeated cancelling changes before drain, sleep/wake and lifecycle removal while queued. Exercise supported and unsupported bodies and old-body retirement with pending support changes.
- Preserve geometry, warm-start, lifecycle, callback-exception, script-isolation and component/scene tests; extending one real path is preferable to duplicating a suite that mirrors helpers.

Use independent physical/oracle comparisons, not the candidate's own membership predicate as expected output. Keep getContactPairs and the existing all-contact oracle. Position/velocity/separation tolerance is 1e-4; rotation/angular velocity/contact normals 1e-5, matching benchmark/capture_cleanup.py. Identity, support counts, sleeping state, contact feature identity and ordered transition histories compare exactly. Do not widen tolerances merely to pass a refactor.

The 36-group correctness matrix is separate from timing. Compare all stage/worker snapshots: 18 one/multiworker pairs per stage and 36 baseline/candidate groups. Per-step validation remains `timing_eligible=false`. Correct boundary snapshots alone do not prove every intermediate state; preserve the per-step oracle and focused histories.

## Work, storage and timing

The exact matrix and runnable commands are in quickstart.md. For each stage: 36 groups × five independent timing processes = 180 timing processes, plus 36 validation-only processes. Keep 120 warmup, 600 measured, seed 0 and dt 1/120; record resolved counts, bodies, contacts and supports. Do not silently lengthen only one stage. If warmup does not exercise expected supported/sleeping populations, extend matched captures for both stages and label them.

Use one worker and a physically appropriate multiworker count (reference six) with matching CPU affinity. Retain the same scenario/count job list; use recorded deterministic random ordering, preferably interleave immutable baseline/candidate runs to limit drift. No competing deliberate builds, tests or probes during timing. A matrix harness may orchestrate existing runner/readers; do not fork their measurement semantics.

Retain per-run complete-step and nested-scope mean, median, p95/p99/max, raw samples and run spread; report independent run variation and worker ratios. Never sum nested timings, treat sample count as independent process count, or infer significance from overlapping run ranges. Investigate every positive delta, including tail/allocation/storage/scaling changes. Reproduce important uncertain regressions with matched alternating runs. Correct faster/scaling designs take precedence under the constitution; comparable alternatives favor simplicity. A known retained inefficiency requires the constitution's local EFFICIENCY explanation and concrete replacement work. Missing evidence is unverified, not a pass.

Work evidence: warmed idle 100-update check with zero force/movement/sleep/geometry/transition/event/adjacency visits when no work is eligible, zero C++ new/new[] allocations, and script-only changes causing no physical refresh. Nonquiet body changes may require local neighbor refresh. Extend existing fixed-peak churn to queue cancellation and body replacement; record actual contact/body membership, support restoration, relevant vector capacities and layout bytes. Distinguish C++ allocations from all allocator activity; measure per-child RSS without inherited compiler high-water marks. No assumed byte or RSS benefit from deleting booleans.

## Safety and compatibility

Run focused and full normal suites, Production/public-header and package consumers, work/benchmark configurations, changed-path ASan/UBSan and TSan, and the appropriate full selections. Record commands, registrations, passes, skips and failures. Leak-disabled checks are memory-access checks only. Previously observed FluidSynth leaks and SDL/libdbus lock-order findings remain visible; re-evaluate stacks if reproduced and never label an unresolved failure a pass. Sandbox detector restrictions are environment limitations. No blanket suppression or disabling of race/deadlock detection to obtain acceptance. This refactor makes no new allocation-failure recovery guarantee.

## Human acceptance

Before/after inventory rows include source links, independent state facts, update triggers, validity boundaries, consumers and retention/removal rationale. For J4/J5 and J6, enumerate the old and new independently maintained facts and mutation sites; account for replacements elsewhere so wrapper code cannot hide equivalent coordination. At least one real journey must show a concrete reduction; the chosen design intends both support and queue reductions.

Provide all eight concise cause→owner→pending work→consumer→cleanup walkthroughs, with final code links and local invariant comments for non-obvious boundaries. Ask the maintainer to walk the final paths and record understood/unresolved findings for each. The implementing agent can prepare and check the evidence but cannot self-certify the maintainer's comprehension. Keep SC-001 human review pending until the maintainer actually performs it. Documentation volume, class/file count and automatic test passes do not stand in for that result. No changes to the feature requirements checklist are authorized by implementation.

## Current solver-order amendment — 2026-10-26

The maintainer explicitly waived deterministic physical solving and requested removal of solver sorting. Old sorted-runtime captures and exact solved-trajectory comparison commands remain historical reproductions. For the amended runtime, validate geometry/contact/support/lifetime invariants and controlled event order without requiring old/new or cross-worker solved-trajectory equality. No geometry tolerance is widened, no historical result is relabeled, and no performance benefit is inferred from removal. Current scoped checks are recorded in ../evidence/solver-order-removal.md.
