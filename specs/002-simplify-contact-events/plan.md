# Implementation Plan: Simplify Contact Events

**Branch**: `001-cpp-engine-cleanup` | **Date**: 2026-10-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-simplify-contact-events/spec.md`.

**Feature context**: `002-simplify-contact-events`. Setup reports this feature identifier in its `BRANCH` field; the actual Git branch remains `001-cpp-engine-cleanup`. No branch switch is part of this plan.

## Summary

Produce enter/exit events directly from confirmed contact transitions, independently of scripts. Remove Stay callbacks, recurring event traversal, observer membership, and script-to-physics subscription invalidation. Keep callback eligibility checks at delivery and preserve existing mutation/reclamation boundaries.

Address the user's four concrete concerns through responsibility changes: fold contact/body scheduling members into PhysicsManager; consolidate all narrow-phase shape geometry into an internal module with one pair-local entry point; remove startup's physics side effect and rename it `startPendingBehaviors`. Preserve the body/support indexes with actual solver consumers. Remove the mirrored active-collision list. See [research.md](research.md) for source evidence, alternatives, and decisions.

## Technical Context

**Language/Version**: C++20 runtime; Python 3 standard-library benchmark tooling; CMake presets/Ninja.

**Primary Dependencies**: Existing vendored SDL 3.4.18, SDL_image 3.4.8, SDL_mixer 3.2.4, SDL_ttf 3.2.2, Eigen 5.0.1, oneTBB 2022.3.0, and nlohmann/json. No dependency changes.

**Storage**: In-memory collider pairs, borrowed callback data, retained-capacity event/scratch vectors, existing JSON scene files, and benchmark CSV/JSON artifacts. No scene-format migration.

**Testing**: Existing CTest C++ executables and Python unittest suite; focused internal transition-contract coverage; work probes and allocation counting; ASan/UBSan; serial/multiworker comparisons. TSan is a supplemental check on a supported instrumented host.

**Target Platform**: Current Linux/GCC development and validation host. Preserve portable C++/SDL library and runner behavior; no claim of new platform validation.

**Project Type**: Embeddable native library with desktop/headless runners and an example game.

**Performance Goals**: Zero per-contact event work for unchanged contacts, including contacts with eligible scripts; no physics adjacency traversal due solely to script changes; O(E log E) ordering of E queued transitions; O(1) indexed contact/body membership updates; retain warmed-up idle allocation reuse. Validate timing differences on declared workloads, without an arbitrary speedup requirement.

**Constraints**: Preserve pair-local parallel geometry, serial shared-state reduction, support-loss waking, awake/kinematic/sleep indexes, canonical solver order, single-attempt initialization, callback safety, and all existing public enter/exit arguments. Stay removal is an intentional source/ABI break requiring consumer rebuilds. Benchmark output/readers migrate together to schema v3.

**Scale/Scope**: Contact/event producer, geometry organization, body membership integration, startup decoupling, affected public callbacks, tests, current docs, and benchmark tools. Exercise 128 shared supports, long persistent contacts, duplicate cell witnesses, large resting populations, and existing sparse/dense/churn scenarios. Exclude broad-phase algorithm replacement, editor changes, a general event bus, and new ownership frameworks.

## Constitution Check

*GATE: Reviewed before research and re-evaluated after design.*

| Principle / gate | Pre-research assessment | Post-design result |
| --- | --- | --- |
| I. Performance and scalability | Existing observer traversal and script invalidation are identifiable hot paths. Retain affected-work indexes until their consumers are traced. | PASS: remove observer work; process only pending geometry, physics adjacency changes, and event batches. Record complexity and validate representative workloads. |
| II. Simple code and ownership | Audit the three implementation fragments and startup side effects before proposing abstractions. | PASS: one physics owner, one internal geometry function, one event queue; delete obsolete lists and singleton-driven destructor events. |
| III. Architectural integrity | Preserve candidate/contact distinction and cell-witness consolidation; consult included paper section 3.3. | PASS: Grid, segmented lists, endpoint/checkpoint ordering, and witness reduction are unchanged. Confirmed contacts alone produce gameplay events. |
| IV. Developer experience | Removing Stay changes the public behavior contract; startup semantics already have explicit documentation. | PASS: preserve enter/exit signatures, document the deliberate enter/exit-only choice, late-attachment exits, and source/ABI migration. |
| V. Evidence before performance claims | Existing CTest, work probes, and benchmark methodology provide the appropriate evidence. | PASS: separate correctness from timings; version changed output; preserve v2 history and retain comparable raw captures/provenance. No speedup claimed in planning. |
| VI. Correctness and evolution | Trace pair removal, shared-body support, startup mutation, and queued callback lifetime. | PASS: manager-owned removal transitions, solver-only wake/sleep refresh, preserved startup prefix algorithm, and focused lifecycle/parallel regressions. |

No constitution exceptions are required. All research questions are resolved in [research.md](research.md).

## Project Structure

### Documentation (this feature)

~~~text
specs/002-simplify-contact-events/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── contact-events.md
│   └── benchmark-schema.md
└── checklists/
    └── requirements.md
~~~

`tasks.md` is generated later by `$speckit-tasks`, not during this planning command.

### Source Code (repository root)

~~~text
include/platformator/
    behavior.h, collider.h, scriptcomponent.h   # Remove Stay declarations.
    collision.h                               # Remove relocated private geometry helper.
src/
    physicsmanager.h/.cpp                      # Physics phases, contacts/support/body indexes, events.
    collisiongeometry.h/.cpp                  # New internal home for ALL shape geometry.
    colliderpair.h/.cpp                        # Pair/manifold ownership; no eventful destructor.
    behavior.cpp, collider.cpp, scriptcomponent.cpp
    gamemanager.h/.cpp                         # Startup and lifecycle without observer refresh.
    benchmark.h/.cpp                           # Two event counters, schema v3.
    physicscontacts.cpp                       # Removed after retaining required member logic.
    physicsbodyindexes.cpp                    # Removed after retaining required member logic.
    physicsgeometry.cpp                       # Replaced by complete geometry module.
tests/
    contact_transition_contract_test.cpp       # New internal pair-event contract coverage.
    physics_support_test.cpp, physics_geometry_test.cpp
    runtime_work_probe.h, runtime_work_test.cpp
    callback_mutation_test.cpp, scene_transition_stress_test.cpp
    queued_deletion_safety_test.cpp, regression_tests.cpp
    collider_pair_cleanup_test.cpp, benchmark_contract_test.cpp
benchmark/
    sweep_scaling.py, plot_scaling.py, capture_cleanup.py
    tests/test_sweep_scaling.py, README.md
docs/
    CPP_API_MIGRATION.md, CPP_CLEANUP_WALKTHROUGH.md
CMakeLists.txt                                 # Register focused test; glob picks up file changes.
~~~

**Structure Decision**: Keep mutable phase state with PhysicsManager and remove the two files that merely scatter its implementation. Geometry becomes a complete internal operation over a pair, with no manager access. Retain small concrete membership helpers locally; do not create a generic collection framework. The existing CMake recursive source glob already discovers/removes translation units on reconfiguration.

## Phase 0: Research Decisions

Research is complete. [research.md](research.md) records event origin, destructor removal, required solver/body indexes, geometry extraction, startup invariants, diagnostics versioning, and verification alternatives. No external service or new technology choice is needed.

## Phase 1: Design

### Contact reconciliation and queue ownership

The worker wrapper saves whether a collision existed, updates pair-local geometry if versions changed, and refreshes pair-local support flags when geometry was updated. After the join, the runtime thread reconciles transitions, support deltas, and contact/solver membership. Unchanged contacts produce no queued event. Response-only synchronization preserves contacts while updating support/solver state.

When Grid removes a candidate pair, PhysicsManager records an exit only if that pair still has a confirmed collision, removes indexed memberships, and reduces prior support before Grid erases storage. The pair destructor never queues work. Both-retired endpoints still contribute a generated exit; delivery suppresses their callbacks.

The manager enqueue function increments transition diagnostics and appends the event. Count at generation exactly once, including cleanup-generated exits; delivery does not modify those counts. Sort queued batches by exit before enter and canonical pair handles at every dispatch boundary, including idle/cleanup. Preserve A-then-B delivery and the existing exception aggregation policy.

### Required collections and refresh paths

Keep `contactPairs` as unsorted indexed membership for confirmed contacts and diagnostics. Keep canonically ordered `solverPairs` for response involving a moving/awake eligible body. Remove `activeCollisions` and read the manifold from each solver pair directly. Retain `dynamicCollisionScratch` for the repeated dynamic solver iterations. Prepare canonical solver order after pending body/response changes and before its next solver/debug consumer; do not mutate that membership while iterating it.

Replace `refreshSubscriptions` with a private `refreshSolverPairs(Collider*)`: body wake/sleep/membership changes use the collider's existing adjacency to update solver eligibility. Response synchronization updates support and solver state. Geometry reduction updates contact and solver membership. GameManager and script startup have no subscription or solver refresh calls.

Remove Stay indexes/order flags, enter-step stamps, observer checks, the unused `NarrowPhaseLocalResults`, and source-level remnants of obsolete Stay delivery.

### Geometry and body scheduling

The internal geometry function accepts a pair with synchronized cached collider geometry. Its shape helpers update/create/clear only the pair's manifold and preserve feature identities/warm-start behavior. All shape selection, SAT, clipping and fallback construction live together. Geometry does not queue events, change body support counts, or obtain the runtime singleton.

Keep support flags/reduction, body work-list membership, and sleep-island helpers under PhysicsManager's ownership. Merge their retained definitions into the manager beside force, response, solve, and sleeping phases. Preserve enqueue/drain ordering and O(1) swap-removal repair; the file move is not a sleeping or solver algorithm rewrite.

### Behavior startup

Rename `triggerStartedBehaviors` to `startPendingBehaviors`. Delete only the observer-specific branch and clarify the attempted-prefix variable. Keep nullable cancellation slots, index-based queue iteration, growth during startup, pending/runnable ownership, deferred ordinary-dispatch additions, per-behavior resolve/start exception logging, and promotion of every attempted entry including failed initialization. No change to when pending entries become visible.

### Lifetime and compatibility

Enter borrows the pair's current `Collision` until delivery; exit has a null collision field. Events borrow endpoints through the existing retirement barrier. Replaced/retired recipients are ineligible immediately; allocations remain readable until all pending callback work is finished. Explicitly verify error cleanup cannot reclaim borrowed storage while records remain pending.

Public enter/exit signatures remain as documented in [the contact contract](contracts/contact-events.md). Stay declarations/definitions disappear across Behavior, Collider and ScriptComponent. Existing binaries must be rebuilt. Current examples use enter callbacks already; rebuild them and change only affected consumers. Migrate current explanatory docs while preserving clearly historical evidence.

### Diagnostic contract

New benchmark artifacts use version 3, eight unchanged scopes, and eleven counters; Stay is absent. Update writer, parsers, aggregates, plotting, fixtures, console strings, current docs, and the capture helper integration together. [The diagnostic contract](contracts/benchmark-schema.md) defines generation counts, recording boundaries, and historical separation.

### Complexity and retained state

| Operation | Intended cost / invariant |
| --- | --- |
| Unchanged contacts, no body/response changes | Zero contact-event traversal or event allocation, regardless of script presence. |
| Affected narrow-phase pairs P | O(P) transition/support/membership reduction after existing geometry work. |
| E queued transitions | O(E) enqueue/delivery and O(E log E) deterministic batch ordering. |
| Script-only changes | No contact adjacency walk or physics-list invalidation. |
| Changed body/collider response | O(degree of affected collider) solver/support refresh; coalesced body queue. |
| Contact/body membership insertion/removal | O(1), including moved element's index repair. |
| Changed solver membership S | Existing O(S log S) canonicalization only when dirty; no mirrored collision vector. |
| Retained memory | O(contacts + solver pairs + affected bodies + peak transition batch); observer set and stamps removed. |

If implementation discovers a retained inefficiency, evaluate the feasible simpler/faster design first. Any unavoidable retention requires the constitution's local `EFFICIENCY:` explanation with replacement prerequisites; a comment does not excuse an available fix.

### Risks and tradeoffs

- Scriptless contacts now generate required transitions that the old observer path omitted. Initial-contact and churn work may increase even as recurring Stay work disappears; preserve peak queue capacity and report workload/event-count differences with timings.
- Public Stay overrides and existing binaries require migration/rebuilds. Schema v2 readers also require the coordinated v3 migration; archived data remains valid under its original contract.
- Geometry relocation and removal of the mirrored solver vector can expose ordering or private-access assumptions. Preserve shape math and prepare canonical solver membership before each consumer; use existing geometry/support/sleep assertions as gates.
- Borrowed event data depends on the dispatch/reclamation barrier. Cover exceptional cleanup and callback-driven retirement explicitly before accepting destructor-side-effect removal.

## Implementation Sequence for Task Generation

1. Capture the source baseline and focused correctness/work evidence. Protect current startup and physical invariants; identify all Stay consumers and v2 writers/readers.
2. Consolidate geometry behind the pair-local internal boundary; fold retained scheduling helpers into the manager. Keep physical results unchanged while changing ownership and file organization.
3. Implement transition-driven enqueue, explicit removal exits, solver-only body refresh, deterministic batches, and removal of observer/mirror/stamp state. Remove GameManager subscription calls and simplify startup without changing its algorithm.
4. Remove Stay from public/internal callback surfaces and migrate affected tests/consumers. Add pair-event generation coverage independent of callback counts.
5. Migrate benchmark writer/readers/docs to v3; prove cleanup exits and zero-recipient transitions are counted.
6. Run focused/full correctness, work/allocation, consumer, and sanitizer checks, then comparable Release measurements. Record actual evidence and limitations.

These are planning packages for the next command, not a generated task checklist or completed implementation.

## Validation and Readiness

[quickstart.md](quickstart.md) provides commands and expected outcomes. Acceptance requires all FR-001 through FR-012 and SC-001 through SC-005, including script-independent transition traces, zero recurring notifications for 100 persistent steps, retained startup mutation behavior, exact support counts, and serial/parallel equivalence.

Post-design constitution review passes all six gates. Deliverables are this plan, research, data model, two contracts, and the validation guide. Runtime implementation, task generation, benchmark captures, and behavioral test execution are not claimed as completed by this planning phase.

## Complexity Tracking

No constitution violations or exceptions. Existing solver/body indexes and support reductions are retained for named consumers and scaling invariants; no new subsystem or ownership layer is proposed.
