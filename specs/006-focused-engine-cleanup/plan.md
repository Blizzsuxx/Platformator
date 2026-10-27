# Implementation Plan: Focused Engine and Example Cleanup

**Branch**: `001-cpp-engine-cleanup` (actual checkout) | **Date**: 2026-10-27 | **Spec**: [spec.md](spec.md)

**Input**: `specs/006-focused-engine-cleanup/spec.md`. The setup script reports the selected feature name as `BRANCH` because feature selection is directory-based; it did not switch the actual checkout.

## Summary

Unify all nonempty texture, audio and animation requests under required loading, and cache previously resolved requests for as long as their canonical resource is retained. Remove S01–S05's unused plumbing/duplicate paths, merge only the two physics membership helpers, enable inexpensive DebugObject moves, and remove verified dead Mario state/declarations while preserving the existing serialized camera field. Characterize the dirty input first and compare behavior, affected work and matched measurements against a frozen baseline. Public collision signature and missing-asset behavior change; preserve and document the existing animation alias.

## Technical Context

**Language/Version**: C++20; Python 3 only for existing benchmark tooling.

**Primary Dependencies**: CMake/Ninja, SDL3, SDL_image, SDL_mixer, Eigen, oneTBB; existing bundled dependencies and project test framework.

**Storage**: Configured asset root and serialized scenes; runtime-owned canonical asset caches plus bounded-lifetime request mappings.

**Testing**: CTest physics, asset, scene, reference, lifecycle, package/consumer and example tests; ASan/UBSan and TSan where available; reproducible controlled probes and matched Release benchmarks.

**Target Platform**: Existing supported desktop hosts, with Linux validation where available; debug-on and debug-off builds.

**Project Type**: Native game engine/library, runner and Mario example.

**Performance Goals**: Avoid filesystem resolution on repeated identical retained requests and avoid debug-object copies during growth; no invented speedup threshold. Assess full-step and relevant subsystem impact where applicable and report uncertainty.

**Constraints**: Preserve user edits; strict first-time path validation; no fallback substitution; no stale resource pointers; retain broad-phase/parallel and callback semantics; preserve empty-path meaning and old serialized camera field. Same-request hits deliberately ignore later file or symlink changes; previously unseen aliases still validate.

**Scale/Scope**: Repeated shared texture/audio/animation requests, different aliases, churn and nested loads; overlay growth; representative small/large, quiet/dynamic, dense/sparse and worker-count physics controls. Excludes P02/P03/P05/P06/P07, Python editor findings and S06 beyond the named helpers.

## Constitution Check

**Before research — pass subject to evidence during implementation:**

- **I, IV — Performance and simplicity**: Exact-request lookup adds owned bookkeeping proportional to live request aliases, eliminated on eviction; no global scans per load or unbounded lifetime cache. Compare reverse-index vs per-resource alias storage and record costs; do not add generic container frameworks. Profile and measure representative repeated loads, debug growth and relevant engine steps.
- **II, VII — Parallelism and integrity**: Preserve existing worker partition, per-cell ownership and joins. Asset caches retain their existing runtime-thread ownership; do not introduce shared mutable parallel loading. Body/contact membership reconciles at existing prescribed serial boundaries. Compare one/multiple workers and validate pair lifetimes, callbacks and deferred mutation.
- **III — Research-backed collision**: Reviewed `paper/paper.txt` §§3.1–3.3: cell subdivision duplicates witnesses, segmented chunks/checkpoints preserve large overlaps and fast-motion cancellation, and event output reduces net witness transitions after cell work. S01 removes only an unused axis value; preserve `isPrimary`, endpoint ordering, bindings, checkpoint/split/merge repair, witness counting, collision/event separation and worker completion. Validate ties, fast moves, large intervals, split/merge, migration and multi-cell final witness.
- **V — Developer experience**: Required asset loading changes runtime behavior deliberately; document diagnostics/migration. Remove the ignored public collision constructor argument with migration notes; preserve animation compatibility and existing scene fields. No unsupported claims about Unity behavior.
- **VI — Evidence**: Retain dirty baseline provenance, comparable fixtures and raw results. Separate correctness/work probes from timing. Benchmark per `benchmark/README.md` with matched builds/workloads, recorded affinity/workers and independent repetitions; report failures, skips and visual/sanitizer limits rather than asserting success.

**After design — pass with implementation evidence required:** The request map has a specific owner and eviction boundary; shared membership retains current type-specific eligibility and mutable-index access; physics algorithm and synchronization are unchanged. The following are not yet demonstrated: contract tests, performance measurements, visual equivalence and sanitizer results. They are execution gates, not planning results.

## Project Structure

### Documentation

```text
specs/006-focused-engine-cleanup/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── contracts/
│   ├── asset-loading.md
│   ├── physics-and-compatibility.md
│   └── validation.md
├── quickstart.md
└── tasks.md                 # generated later, not by this command
```

### Source Code

```text
src/                         # loaders, path manager, physics, debug and internal helpers
include/platformator/        # public collision, animation and asset interfaces
examples/mario/              # camera/enemy/player cleanup and existing scene
assets/                      # asset fixtures and scenes
cmake/ CMakeLists.txt        # builds and focused tests/probes if necessary
tests/                       # asset, scene, physics, lifecycle and consumer contracts
benchmark/                   # existing measurement methodology and tooling
docs/                        # API and behavior migration documentation
```

**Structure Decision**: Extend existing engine, test and documentation locations. Add a small private membership primitive only for the two existing helpers and narrow benchmark fixtures only if existing tooling cannot measure the named operation. Do not modify the Python editor or unrelated work.

## Complexity Tracking

No constitution exception proposed. Request aliases and eviction coordination add state specifically to remove repeat filesystem work; reverse associations must be proportional to live aliases and cleaned with their resource. Their time/memory cost and evidence are part of validation.
