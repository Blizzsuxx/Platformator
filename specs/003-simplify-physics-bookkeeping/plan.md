# Implementation Plan: Simplify Physics Bookkeeping

**Branch**: `001-cpp-engine-cleanup` (actual Git branch) | **Feature**: `003-simplify-physics-bookkeeping` | **Date**: 2026-10-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/003-simplify-physics-bookkeeping/spec.md`.

**Implementation status**: Technical implementation and required measurement captures are available. The 2026-10-26 bounded profiling investigation is complete, but T038 performance acceptance remains unverified because repeated spatial-control downsides lack an established cause/tradeoff; actual maintainer walkthrough T043/T045 also remains pending. See [validation.md](validation.md) and [human-review.md](evidence/human-review.md). The design text below records the chosen scope; it is not a substitute for executed evidence.

The setup script reports `BRANCH=003-simplify-physics-bookkeeping` from the selected feature path. It does not switch Git branches. Preserve the existing checkout, prior feature artifacts and user changes.

## Summary

Make physics state maintenance easier for the maintainer to trace and change. Review the bookkeeping introduced by `7906959` and current related paths; use the current post-contact-event engine, not the historical parent, as the behavioral and cost baseline.

The concrete design removes redundant queue membership and support flags, centralizes live-contact/solver eligibility, clarifies the existing body participation/sleep derivation, and isolates geometry behind one internal function with explicit inputs. Keep incremental body/contact indexes, per-body batched support deltas, deterministic event ordering and lifetime barriers because they serve different consumers. No new owning subsystem, generic indexed-container framework, global scan or synchronization layer is proposed.

A before-and-after inventory and eight code-backed walkthroughs accompany actual state reductions. Tests and measurements verify behavior and costs; only the maintainer can supply the human-readability finding. This command ends at design. Runtime changes, tests and captures were executed by speckit-implement; actual maintainer acceptance remains a separate pending outcome.

## Technical Context

**Language/Version**: C++20 runtime; Python 3 benchmark tooling. Available local tools inspected: GCC 16.2.1, CMake 4.4.3, Python 3.14.7. Repository minimum CMake 3.16; no toolchain upgrade is part of this feature.

**Primary Dependencies**: Existing vendored Eigen 5.0.1, oneTBB 2022.3.0, SDL3 3.4.18 and SDL image/mixer/font stack, nlohmann JSON, CMake/Ninja. No added package or framework.

**Storage**: In-memory bodies, Grid-owned stable pair keys and their contact objects, indexed lists and reusable queues/scratch; scene serialization unchanged. Evidence uses Markdown plus current schema-3 CSV/JSON/raw captures in a fresh retained directory.

**Testing**: Existing standalone CTest hosts, installed/relocated public consumers, independent geometry/support/contact oracle, work/allocation probes, Python benchmark contracts, ASan/UBSan and TSan. Add one focused INTERNAL bookkeeping test and extend existing support/work assertions for uncovered cases.

**Target Platform**: Existing supported desktop library/runner platforms; reference execution on Linux, headless SDL tests. Validate one and suitable multiple workers (reference six), recording real CPU affinity/topology. No untested-platform certification.

**Project Type**: Native C++ runtime/library with runner/example consumers and Python measurement tools; this feature changes internal physics and dependent lifecycle plumbing only.

**Performance Goals**: Fewer independent facts/update dependencies on real audit paths; retain affected-work behavior, warmed allocation reuse and meaningful runtime/scaling benefits. No arbitrary speedup or acceptable-slowdown percentage. Compare complete-step means/tails/run variation and worker scaling; investigate every regression.

**Constraints**: Preserve authoring/scene/reporting contracts, exact contact histories and established physical tolerances; no Stay, replay, script-dependent generation, new global scans or discarded oracle. Keep component lifetime/worker boundaries and net support reduction. Missing safety/performance evidence and pending maintainer review remain explicitly unverified.

**Scale/Scope**: Four discussed physics files plus ColliderPair, relevant Rigidbody/Grid/GameManager boundaries and tests. Eight audit journeys, ≥128 changed pairs for parallel-path coverage, existing 128-support and large idle/churn fixtures. Timing matrix: 36 groups/stage × five processes, plus separate correctness captures; populations small/large, static/quiet/moving, dense/uneven, churn/migration, recipients 0/1/2 and workers one/multiple.

## Constitution Check

Initial gate was evaluated after loading the spec and constitution 1.1.0, before selecting the design. All seven principles permit a focused internal simplification with explicit evidence. Historical pre-constitution origin is not an exemption or rollback mandate.

| Principle | Before research | Post-design check and implementation obligation |
| --- | --- | --- |
| I. Performance/scalability first | PASS: compare state reductions with scans, immediate updates and new ownership layers | PASS at design: O(1) indexed queue/body operations and local neighbor work retained; eliminate three independent booleans, no byte/RSS claim. Measure complete-step/mean/tails/work/storage/allocations; investigate any cost of helper boundaries. Known retained inefficiency, if found, needs the local EFFICIENCY cost/frequency/reason/alternative/replacement-work comment and evidence; no such exception is assumed now. |
| II. oneTBB/multicore | PASS: independent integration/cell/pair workers; serial shared consumers | PASS at design: no shared ledger/list writes in workers. Existing small-pair threshold 64 retained. Join precedes support/list/event reduction, solver, callbacks and erasure. Assess task granularity, skew, scheduling, cache/bandwidth and serial work with one/multiworker evidence; no unsupported load-balance or parallel speedup claim. |
| III. Research-backed collision architecture | PASS: no foundational broad-phase redesign | PASS at design: reviewed local paper §§3.1–3.3; only redundant consumer-queue flag changes in Grid. Keep cell witness deduplication, endpoint ties, checkpoint/fast-motion/split/merge behavior and conservative candidates. Existing foundational regressions remain; changing those algorithms would reopen research/scope gates. |
| IV. Simplicity/KISS | PASS: code must actually reduce coordination | PASS at design: two nullable support contributors replace pointer+flag pairs; one queue index replaces flag+index. One uncached live predicate, direct body derivation and geometry free function. No extra framework, dirty-reason flags, replacement state copies or owning classes. Human audit and inventory must verify complexity was not merely relocated. |
| V. Developer experience | PASS: authoring behavior preserved | PASS at design: no public method removal, scene migration, callback or reporting break. Geometry header remains internal. Package/header consumers test compatibility. No new claim about Unity behavior. |
| VI. Evidence before performance claims | PASS: fresh baseline required | PASS at design: current schema-3 baseline isolated before edits; fixed 36-group matrix, five processes, raw/provenance/checksums, separate oracle and timing, all regression investigations. Old schema-2 results remain historical. No measurements or speedup claimed by planning. |
| VII. Correctness/integrity | PASS: safety and determinism are gates | PASS at design: preserve net-support boundary and old recipient lifetime, queue cancellation before erasure, geometry math/warm-start, worker ownership and Exit-before-Enter delivery. Add changed-path characterization and reuse independent oracle/lifecycle tests. Actual sanitizer findings remain visible; pending maintainer review cannot be self-certified. |

Post-design result: no known unjustified constitutional violation or unresolved design clarification. These are design gates, not evidence that implementation checks passed. A discovered incompatibility, unsafe boundary, uninvestigated regression or unverified requirement blocks the corresponding acceptance claim and requires a revised decision.

## Project Structure

### Documentation (this feature)

```text
specs/003-simplify-physics-bookkeeping/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── physics-lifecycle.md
│   └── validation-evidence.md
├── checklists/requirements.md       # Existing specification-quality review; read-only in implement
├── tasks.md                         # Future speckit-tasks output; not generated by this command
├── evidence/                        # Future implementation inventory/traces/raw indexes/reports
└── validation.md                    # Future actual results and maintainer-review status
```

### Source Code (repository root)

```text
src/
├── physicsmanager.{h,cpp}            # Phase orchestration, solver and event delivery
├── physicsbodyindexes.cpp           # Body participation, changed-body queue, sleep-island helpers
├── physicscontacts.cpp              # Pair participation and net support contribution reconciliation
├── physicsgeometry.{h,cpp}           # New internal header; explicit pair + gravity geometry entry
├── colliderpair.{h,cpp}              # Remove redundant queue/support facts; preserve stable identity
├── grid.{h,cpp}                      # Queue index transitions only; witnesses/spatial algorithms retained
├── rigidbody.cpp                    # Participation notification callers; preserve setter semantics
└── gamemanager.cpp                  # Existing registration, reconciliation and retirement boundaries
include/platformator/
├── rigidbody.h                      # Private helper/name adjustments; public API preserved
└── collision.h                      # Remove private fallback declaration after geometry relocation
tests/
├── physics_bookkeeping_test.cpp     # Planned INTERNAL target for uncovered queue/body state paths
├── physics_support_test.cpp         # Net support/replacement/reference assertions
├── runtime_work_test.cpp            # Quiet work, one/multiworker and fixed-peak storage
├── runtime_work_probe.h             # Existing counters, extend only for a demonstrated assertion gap
├── contact_events_test.cpp          # Existing generated histories/order and recipient controls
└── ...                             # Existing geometry/lifecycle/public-consumer guardrails retained
benchmark/
├── sweep_scaling.py                 # Existing strict schema-3 reader/runner interface
├── capture_cleanup.py               # Reuse oracle comparator/tolerances, not hardcoded topology/matrix
└── tests/                          # Existing contracts; no schema migration
```

**Structure Decision**: Keep small domain-local functions in the current files. File count is not a success criterion. Add only an internal geometry header and one focused test host where needed; no new state-owning physics hierarchy. Do not broaden a refactor simply to touch every listed file. Source dependencies justify neighboring edits.

## Phase 0: Research and decisions

Completed design research is in [research.md](research.md). Three read-only investigations covered body/sleep state, contact/support/geometry and validation. All technical unknowns are resolved for planning:

1. Queue bool duplicates index state, including cancellation and worker consumption.
2. Per-endpoint support pointer+flag can become one nullable contribution identity, while net per-body deltas remain essential.
3. Body and contact/solver lists have distinct physical/oracle consumers and avoid global scans.
4. Geometry needs explicit gravity input and a pair-owned contact boundary; it does not need a new owner class or manifold-result allocation.
5. Deferred body/support/lifecycle queues have different visibility/cancellation rules; retain and name them rather than merge them.
6. A fresh equivalent schema-3 baseline and actual maintainer review are required; old captures and automatic tests cannot substitute.

Review implementation-start source drift before treating this inspected baseline as current. No new network/dependency research is needed for these local design decisions.

## Phase 1: Design

### A. Support ledger: four fields to two contribution identities

Use nullable `contributedSupportBodyA/B` in ColliderPair. A pointer means this pair has one applied-or-scheduled contribution to that exact body. Derive the desired pointer from the current Collision support bit mapped to canonical endpoint and its current body. Compare identity, queue removal/addition only on a change, then update the ledger. Use the same operation to release both contributions on final pair removal.

Preserve per-body net delta queue and the existing flush/wake rule. A replaced old body must survive through decrement and flush even though component lookup already returns the new body. Geometry can delete Collision before reduction; old contribution identity remains available. Do not read current components to infer who received old credit. See the transition table and lifetime rules in data-model.md and physics-lifecycle.md.

### B. Narrow-phase queue: one mutable membership fact

Remove the queue flag and its setter; derive the read-only query from the index sentinel. Keep Grid's O(1) enqueue/dedup/swap-remove cancellation and moved-entry repair. At existing worker entry clear only the owned index, then evaluate. The batch pointers remain fixed and live through serial reduction; structural queue changes and erasure are forbidden until after join. Clear the batch without a new scan. Remove obsolete commented dual-clear code and add boundary assertions in test/debug contexts where useful.

### C. Body and pair participation: direct derivation, explicit reasons

Rename internal collections/functions where necessary to state their actual predicates: awake dynamic bodies, moving kinematic bodies, sleep candidates and `reconcileBodyParticipation`. Preserve indexed membership and change-queue semantics. Put the trigger/visibility table beside the relevant logic or in concise linked audit material; local non-obvious boundaries need local comments. A shared sleep-island reset helper removes duplicated initialization without new state; its one definition is in the private header so Release integration can inline it. An initially out-of-line definition was corrected after regression investigation.

Use one uncached live-contact predicate and one solver derivation. Keep geometry-completion reduction, response-only reconciliation and final-witness removal separate and clearly named. Share support/list maintenance only where semantics match. Do not emit transitions from response refresh, refresh physical neighbors for script changes, restore a duplicate Collision vector. Preserve public registration flags/accessors despite their apparent duplication; changing their contract is outside this feature.

### D. Geometry boundary

Expose one internal namespace function in physicsgeometry.h with explicit pair and gravity-direction inputs. Move shape helpers out of PhysicsManager's declaration into implementation-local functions. Preserve operation order, math constants, Collision reuse, cache version updates, feature IDs and warm-start impulse transfer. No new result container, heap layer, virtual dispatch or runtime lookup. The existing worker calls this function; serial support/event/list ownership is unchanged. Verify mechanical math preservation and public geometry/physical oracle outputs. Implementation also removed proven-unreachable circle branches from box-only clipping and moved unchanged fallback midpoint logic out of Collision into geometry; no public accessor/friend was added.

### E. Complexity and mutation boundaries

For P pending pairs, S solver contacts, E generated transitions, B changed bodies and A affected adjacency visits: preserve parallel per-pair geometry + serial O(P) reduction; O(1) solver membership insertion/swap removal without sorting; O(E log E) event ordering; O(B) body drain plus O(A) neighbor refresh; O(touched support bodies) net flush. Queue/index changes do not add an all-body or all-contact pass. Cost constants and cache/layout effects require measurement. Compound operations stay in existing ownership phases; const-key pair storage is not a license for concurrent arbitrary mutation.

### F. Compatibility, validation and human audit

Contracts are in [physics-lifecycle.md](contracts/physics-lifecycle.md) and [validation-evidence.md](contracts/validation-evidence.md). Entities, fields, consumers, proposed dispositions, state transitions and eight audit routes are in [data-model.md](data-model.md). [quickstart.md](quickstart.md) supplies existing runnable checks, planned target prerequisites, baseline isolation, the exact matrix and capture examples.

At implementation, first freeze baseline/provenance and characterize gaps, then make support and queue reductions in bounded steps, then eligibility/body/geometry boundary changes. Check affected paths between steps. Final correctness precedes timing, and any later runtime fix requires affected revalidation/recapture. The plan does not prescribe breaking passing tests before an observationally equivalent refactor; targeted characterization must pass against the baseline before code changes.

## Acceptance and handoff to task generation

Generate dependency-ordered tasks for: baseline/provenance; inventory and characterization; support reduction; queue reduction; body/eligibility clarification; geometry boundary; focused/full/package/sanitizer/work validation; separate physical captures and comparable timing; regression investigation; final inventory/eight walkthroughs and actual maintainer review. Preserve requirements checklist markers. Tasks must keep the independent oracle, raw captures, worker/lifetime assertions, and conditional investigation work rather than declaring checks complete on limited evidence.

| Requirement group | Design/evidence responsibility |
| --- | --- |
| FR-001–FR-003, SC-001 | Scope rationale, inventory, eight code-linked journeys, local invariants, actual maintainer result |
| FR-004–FR-005, SC-002 | Support/queue facts and mutation-site comparison; retained structure rationale; no relocated equivalent machinery |
| FR-006, FR-008, SC-003/SC-006 | Physical oracle, net support/replacement/lifecycle tests, fixed-peak work/storage and safety checks |
| FR-007, SC-004 | Existing contact/recipient/order tests, 100-update quiet controls, unchanged public contact contract |
| FR-009 | Worker ownership and one/multiworker physical/ordered-history comparison; TSan and actual limitations |
| FR-010–FR-011, SC-005 | Fresh source/build baseline, 36-group raw matrix, work/allocations/layout/RSS, regression investigations |
| FR-012 | Compatible authoring/scenes/reporting, package/header tests, concise final review and pending/actual human findings |

The implementation agent prepares concrete reviewable code and evidence before requesting the maintainer walkthrough. It may finish independently authorized checks while review is pending, but cannot mark SC-001 human acceptance passed on the maintainer's behalf.

## Complexity Tracking

No constitutional exception is proposed. Existing body indexes, support batching, sleep union-find, pair adjacency/witnesses, separate contact/solver membership and deterministic event sorting retain explicit purposes in the inventory. Revisit these decisions if implementation evidence identifies a simpler comparable or faster design. A discovered known inefficiency cannot be silently grandfathered; apply the constitution's evidence and local-comment requirements or fix it within scope.

## Implemented user amendment — 2026-10-26

Canonical physics-solver ordering has been removed at the maintainer's explicit request; see the scope amendment in spec.md. `solverPairs` retains only membership and O(1) indexed removal. There is no sorting comparator/helper/call, dirty-order flag or sort-time index repair. Event ordering remains independent and unchanged. The support test keeps exact per-worker accounting and event-order checks while replacing solved-trajectory equality with finite-state validation; other geometry/physical regressions remain. Prior recorded timings and physical-equivalence captures are historical, not validation of the amended runtime. Current scoped results: [solver-order-removal.md](evidence/solver-order-removal.md).
