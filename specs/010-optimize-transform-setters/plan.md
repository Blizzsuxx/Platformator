# Implementation Plan: Efficient Transform Reads and Setters

**Branch**: `branch-without-bvh` | **Date**: 2026-10-30 | **Spec**: [spec.md](spec.md)

**Input**: `specs/010-optimize-transform-setters/spec.md`; user requires KISS, performance-first decisions, and compact before/after evidence.

## Summary

P02 must make repeated hierarchy position reads efficient while preserving immediate public reads and the existing synchronized rendering/physics snapshot. P05 must avoid redundant collider and body work without losing explicit wake/reset behavior. Implementation was started before this planning phase and is **provisional**: retain, simplify, replace, or revert it according to correctness and comparable measurements. Do not present it as accepted or claim a speedup until the evidence gate below passes.

## Technical Context

**Language/Version**: C++20; Python for existing benchmark orchestration.

**Primary Dependencies**: Eigen, oneTBB, SDL3; CMake/CTest.

**Storage**: In-memory object state; compact, local benchmark report and raw samples only.

**Testing**: Targeted CTest hierarchy, geometry, physics bookkeeping, lifecycle and rendering tests; separate correctness/work-probe and uninstrumented Release timing builds; sanitizer and worker comparisons where supported.

**Target Platform**: Existing supported desktop platforms; collect reproducible measurements on the current Linux host.

**Project Type**: C++ runtime/library.

**Performance Goals**: Remove repeated ancestor calculations and unnecessary updates when this produces a meaningful net benefit; investigate measured regressions, especially frequent ancestor edits. No invented speedup target.

**Constraints**: Immediate position reads; existing synchronized snapshots, validation, lifecycle, numeric and wake/reset contracts; compact artifact policy and no automatic full historical campaign.

**Scale/Scope**: Small and larger flat, broad and deep hierarchies; static/read-heavy and mutation-heavy cases; same-value versus changed-value setters; one and multiple workers. Exact bounded pilot cases are in [quickstart.md](quickstart.md).

## Constitution Check

*Pre-research and post-design review.*

- **I / IV — performance and KISS**: PASS for the plan, conditional for implementation. Compare recursive baseline, provisional eager cache, and feasible simpler alternatives. Track O(depth) read versus O(subtree) edits, additional per-object storage, allocation and dynamic hierarchy regressions; no speculative framework. A known inefficient path that remains requires the constitution's local `EFFICIENCY:` explanation and concrete replacement work; do not use it as a substitute for a feasible fix.
- **II — multicore**: PASS for the design. Physics integrates per-body local state in oneTBB workers, joins before ordered main-thread synchronization, and collision readers use stable data. Main-thread subtree publication is sequential to preserve immediate reads and avoid overlapping writes; benchmark 1 and multiple workers and investigate synchronization cost. Any alternative must specify ownership and visibility and avoid unsynchronized cache writes by readers.
- **III — collision architecture**: PASS / not in scope. No foundational broad-phase data structure change is planned; geometry/participation changes require existing collision correctness tests, not a broad-phase redesign.
- **V — developer experience**: PASS: public names and immediate reads remain; changed same-value waking is explicitly documented in [contract](contracts/runtime.md). No unverified Unity behavior claims.
- **VI — evidence**: PASS for plan; **NOT YET SATISFIED for acceptance**. The existing code has no comparable before/after timing. Establish and record a source-identifiable baseline and candidate, equal builds and fixtures, raw samples, work counts and complete-step controls. Separate instrumented correctness/work from uninstrumented timing. Measure read-heavy and mutation-heavy hierarchy shapes and no-op/changed setter controls, small/large populations, serial/multiworker, cold/growth/RSS and mean/tail where meaningful within budget. Report unmeasured dimensions and avoid claims beyond the pilot.
- **VII — correctness**: PASS conditional on new and existing tests, including concurrent movement, reparenting, lifetime and same-value/reset edge cases. Catch and diagnose failures before performance acceptance.

Post-design: no architectural exception approved. The candidate cannot be considered constitutionally accepted until the pending correctness and evidence work completes. If the budget prevents a required measurement, report the partial result and limitation; do not silently waive it or expand the campaign.

## Project Structure

### Documentation (this feature)

```text
specs/010-optimize-transform-setters/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── contracts/runtime.md
├── quickstart.md
└── tasks.md                 # generated only by the tasks phase
```

### Source Code (repository root)

```text
include/platformator/        # public object, collider and body interfaces
src/                         # transforms, physics, synchronization, setters
benchmark/                   # existing runners and compact capture tools
tests/                       # hierarchy, geometry, physics and runtime regressions
bin/                         # separate build trees, not evidence artifacts
```

**Structure Decision**: Extend existing runtime and focused test/benchmark infrastructure only as required. Keep benchmark source small and builds outside reports. Preserve the user's working changes; baseline reconstruction must not reset or overwrite the workspace.

## Measurement and decision gate

1. Identify the pre-change baseline (`a2f587d1c1281277e1b5a1d33d6a47b62fdb5a0c` at planning time) and candidate from actual source contents; record revision, candidate diff hash, compiler, flags, hardware, worker settings and fixture version. Verify baseline equivalence rather than assuming HEAD always captures unrelated changes. Do not commit or copy a source bundle into reports.
2. Use equivalent optimized builds and the same deterministic fixture for both revisions. Add the minimal shared fixture if necessary. Run three independent repetitions with 5 warmup and 30 measured frames for focused complete-step cases, plus bounded direct read/edit/setter samples. Use one worker and a practical multicore count. Match input, seeds and timing boundaries; do not sum nested scopes. Work/allocations are measured in separate instrumented runs.
3. Include a small flat control, read-heavy deep/broad hierarchy, repeatedly edited ancestor with descendants, parent/child moving bodies, and settled repeated setters with a changed-value control. Capture representative complete-step and physics scopes, direct operation timings, per-run samples and variability; counters verify intended work avoidance. Check memory overhead and allocation/growth observations. Select bounded cases rather than a Cartesian matrix; document uncovered dimensions.
4. Cap the entire focused capture at **600 seconds and 50 MiB** of output; monitor the non-render tools explicitly. Stop on budget/failure, retain the partial small report and raw timings, diagnose, and do not automatically retry or expand. Rendering comparisons, if actually needed, use `benchmark/capture_linear_render.py` defaults. No full campaign or historical acceptance restart.
5. Compare costs as well as gains, and investigate surprising differences and regressions before deciding. If repeated subtree edits dominate or cache complexity is unjustified, evaluate a simpler or more efficient alternative; preserve functional requirements and make any spec conflict explicit. Three short runs are exploratory evidence, not broad statistical acceptance or a multicore scaling claim.

## Complexity Tracking

No constitution exception is approved. The provisional extra cached position and deferred batch flag are justified only if correctness and focused measurements support their net value; remove or redesign them if they do not.
