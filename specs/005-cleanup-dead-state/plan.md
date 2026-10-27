# Implementation Plan: Clean Up Dead State and Internal Code

**Branch**: `001-cpp-engine-cleanup` (actual Git branch) | **Date**: 2026-10-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/005-cleanup-dead-state/spec.md`. The setup script reported `BRANCH=005-cleanup-dead-state`, derived from the active feature directory; `git branch --show-current` reports the actual branch above. No branch was created or switched by planning.

## Summary

Re-audit all fourteen candidates in [CODE_CLEANUP_AUDIT.md](../../docs/CODE_CLEANUP_AUDIT.md) against the implementation and record an evidence-backed disposition for each. Remove only state and call chains confirmed unused: collision-pair slots, the unused bias-impulse accumulator, hierarchy depth, Audio's manager index, the unobserved dropped-step accumulator, unused lifecycle and physics helpers, obsolete rebinding and debug-rendering paths, and the unused resolved-path copy. Preserve every active pair/queue index, support and solver input, callback/lifetime boundary, resource rule and serialized format. Public removals C03/C04/C12 are intentional source and possible ABI breaks; C02 changes installed GameObject layout even though its field is private. All require explicit migration and rebuild guidance.

Use the existing pair-local parallel narrow phase and serial reduction; do not implement the abandoned slot-indexed result architecture or a split-impulse solver. Complete comparable correctness, work/memory and timing evaluation before making performance claims. The [research](research.md), [data model](data-model.md), [contracts](contracts/) and [quickstart](quickstart.md) specify the boundaries and evidence.

## Technical Context

**Language/Version**: C++20, CMake 3.16+, CTest; Python 3 standard library for existing benchmark tooling.

**Primary Dependencies**: bundled oneTBB, Eigen, SDL3, SDL3_image, SDL3_mixer, SDL3_ttf and nlohmann JSON; no new runtime dependencies.

**Storage**: In-memory Grid-owned candidate pairs, pair-local result/manifold state, indexed queues/lists, GameObject/component state and runtime asset caches. Scene and asset files retain their existing formats; benchmark artifacts are separate files.

**Testing**: CTest engine/consumer/benchmark tests; dedicated affected-work build; ASan+UBSan and TSan where available; schema-3 invariant and Release timing runs with retained raw evidence. Independent consumer and both debug-tool configurations are required.

**Target Platform**: existing supported desktop/native library and runner environments; Linux is the reproducible validation host when available. Dummy SDL drivers support automated tests; overlay appearance requires an actual graphical check when applicable.

**Project Type**: native 2D engine/library, runner and example consumer. Python editor and other audit findings are out of scope.

**Performance Goals**: eliminate identified unused maintenance and storage with no uninvestigated regression in affected representative complete-step workloads. No invented speedup threshold; claims require comparable measurements and uncertainty reporting.

**Constraints**: recheck C01–C14 before edits; preserve behavior and public contracts except expressly identified C03/C04/C12 removals; preserve lifecycle and broad-phase invariants; do not add a new result-slot or split-impulse system; preserve historical evidence; isolate benchmark timing from work probes/sanitizers.

**Scale/Scope**: 14 audit dispositions; broad/narrow physics and affected scene/object/resource/debug paths. Cover small and large populations, quiet/persistent and high churn/migration, uniform and skewed occupancy, one and multiple workers, memory/allocation and average/tail timing. Exact workload sizes are specified in [contracts/validation-evidence.md](contracts/validation-evidence.md).

## Constitution Check

*Pre-research gate: PASS; post-design recheck: PASS, conditional on implementation completing the evidence and validation described below. No exception requested.*

| Principle | Planned compliance and remaining implementation obligation |
| --- | --- |
| I. Performance and Scalability | C01 removes constant per-pair maintenance and peak-retained vectors, without changing algorithms. Compare meaningful growth dimensions and affected-work/allocation; investigate anomalies. Do not claim a measured gain in this plan. |
| II. Multicore Design | Preserve oneTBB cell and eligible-pair parallel work. Pair workers mutate distinct pair-owned records; serial reduction, pair erasure, callbacks and reclamation follow worker completion. Small batches remain sequential and shared-body solving remains serial. Test 1 and multiple available workers; measure scheduling/granularity effects rather than introducing a pool or threshold. |
| III. Research-Backed Collision Architecture | Reviewed the repository's paper §§3.1–3.3 and current grid/segmented paths: cell subdivision and witness deduplication, endpoint/chunk/checkpoint invariants and event-based delta output remain. No sort, batch or adaptive redesign is proposed. Maintain coverage for ties, large/multicell intervals, fast movement, split/merge, migration and duplicate witnesses; use an independent oracle for affected broad-phase cases. |
| IV. Simplicity/KISS | Delete unused ownership/indexes and transitive dead helpers without replacement abstraction. Recheck hidden/test/public consumers; retain any item shown necessary with evidence. Do not consolidate unrelated code. |
| V. Developer Experience | Keep GameObject/component lifecycle and scene/resource behavior familiar and unchanged. The three audited public removals and C02 class-layout change are explicitly described in migration docs; avoid unsupported claims about Unity internals. |
| VI. Evidence | Freeze equivalent pre-change and candidate source/build/fixtures; measure complete-step and relevant nested scopes, raw repetitions, worker count and retained memory. Preserve historical captures. Baseline failures and unavailable sanitizer/graphics checks are recorded, not silently passed. |
| VII. Correctness and Integrity | Maintain canonical pair identity, candidate/contact distinction, support counts, normal/tangent warm start, callback order, destruction safety, immediate transforms, resource lifetime, diagnostics and output. Compare baseline/candidate and serial/multiworker controlled traces with existing numerical tolerances. The prior maintainer waiver of unconstrained cross-worker solved-trajectory equality remains in force. |

## Project Structure

### Documentation (this feature)

```text
specs/005-cleanup-dead-state/
├── spec.md                     # Requested behavior and scope
├── plan.md                     # This plan
├── research.md                 # Phase 0 decisions and audited disposition candidates
├── data-model.md               # Ownership, state transitions and removed/retained fields
├── contracts/
│   ├── runtime-physics.md      # Physics, lifecycle, resource and debug contracts
│   ├── public-compatibility.md # Public removals and retained consumer contract
│   └── validation-evidence.md  # Correctness, workload and evidence requirements
├── quickstart.md               # Validation procedure and commands
└── tasks.md                    # Later $speckit-tasks output, not produced by planning
```

### Source Code (repository root)

```text
include/platformator/  # Public GameObject, collider, collision types and Audio declarations
src/                  # Grid, pair, collision, runtime/lifecycle, path and debug implementations
tests/                # Regression, physics, lifecycle, asset and consumer tests
benchmark/            # Existing schema-3 runner, sweep and results methodology
docs/                 # Audit, migration guide and current contract corrections
specs/                # Feature design and immutable historical execution records
```

**Structure Decision**: Keep the existing native engine/library and test structure. Make localized removals in existing files, update relevant current documentation and migration guidance, and add only necessary behavior-oriented characterization to existing test hosts. No new production subsystem or editor work.

## Implementation and Validation Sequence

1. **Inventory and freeze**: Verify all fourteen findings including tests, examples, benchmark probes and public declarations. Record each exact remaining consumer and a remove/retain disposition. Establish the actual source, dirty/untracked changes, toolchain, hardware, existing test status and benchmark fixtures. If an item has a live consumer or contract, retain it with evidence and scope the change accordingly.
2. **Characterize before removal**: Reuse meaningful existing tests. Add only missing independent observable-behavior characterization, especially pair witness lifetime/churn, one-/two-contact response, component/audio retirement and graphical/debug behavior where existing coverage is insufficient. The same common characterization must run against baseline and candidate; removing bias-field-only assertions must not weaken warm-start assertions.
3. **Physics slice (US1)**: Remove C01/C03/C09–C12 in dependency-safe groups. Keep pair-set ownership, canonical keys, adjacency and queue indexes, witness counts, pair-local result data, contact/solver indexes and body support credits. Maintain the normal/tangent cached impulses and both solver paths. Record C12 public removals; do not fold in S01 or other unrelated simplification.
4. **Lifecycle/resource slice (US2/US3)**: Remove C02/C04–C08/C13/C14 and their now-unreferenced local code. Preserve transform ordering and queue cancellation, delayed reclamation and callback eligibility, live Audio stop/track ownership, original-request errors, current buffered overlays and debug-off build. Keep clock accounting required by existing clock behavior/tests even though the manager aggregate is removed.
5. **Compatibility and documentation**: Update source/installed-consumer usage and migration guide for C02/C03/C04/C12. Correct present-tense claims about pair-slot consumers in current design documentation with an explicit supersession note; do not alter archived evidence or imply historical captures measured this change. Maintain a final C01–C14 inventory and disposition rationale.
6. **Gates and comparison**: Run relevant focused and full ordinary tests, consumer/install tests and production/debug builds; execute affected-work, ASan+UBSan and TSan checks when supported, and record unavailable/skipped runs. Compare serial/multiworker invariant snapshots and event traces before Release timing. Run matched baseline/candidate workloads with independent repetitions, captured metadata, memory/allocations and raw samples. Investigate regressions against natural variation and workload counters; distinguish no detectable effect from proven equivalence and retain inconclusive results honestly.

**Dependencies**: Inventory and baseline precede all production edits. Each slice is independently characterizable after the shared baseline. Both slices precede final source-consumer validation and candidate performance capture. No `$speckit-tasks` or implementation is performed by this planning phase.

## Complexity Tracking

No constitution violations or added abstractions are proposed. Existing sequential reduction/solver/callback work is retained because it owns shared state and preserves event/lifetime ordering; parallelizing it is outside scope and requires separate evidence.
