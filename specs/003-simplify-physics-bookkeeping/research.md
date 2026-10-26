# Research: Simplify Physics Bookkeeping

## Scope and evidence

Planning inspected current source at `f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba` and the physics changes in `79069591488a68223f8e71ce5f24ddf2d37fb609`. The historical commit introduced body indexes, changed-body reconciliation, support deltas, sleeping-island state and contact participation lists. It is review context, not a rollback target. The current post-Stay-removal engine is the baseline.

Three read-only research investigations covered body/sleep participation, contact/support/geometry ownership, and existing correctness/measurement coverage. Findings below resolve the design questions. No runtime edits, new measurements, or readability approval occurred during planning.

## 1. Reduce redundant representations before adding subsystems

**Decision:** Implement two concrete state reductions: derive narrow-phase queued status from its existing queue index, and replace each support-body-plus-flag combination with a nullable contributing-body pointer. Consolidate repeated live-contact eligibility and make body reconciliation responsibilities explicit. Isolate geometry behind one internal function with explicit gravity input, without a new state-owning class.

**Rationale:** These changes remove independently coordinated facts and clarify real ownership boundaries. They do not require replacing proven incremental structures or disguising them behind a framework. Queue cancellation and support replacement are concrete before/after audit examples for FR-004 and SC-002.

**Alternatives considered:** File moves or naming alone cannot satisfy the spec. A PhysicsWorld/BodyRegistry/ContactManager hierarchy would add ownership and interfaces before demonstrating a need. Deleting every cache in favor of per-frame scans sacrifices quiet-scene behavior. Keeping all current state with more comments would leave the main request unsatisfied.

## 2. One source for narrow-phase queue membership

**Decision:** Remove `ColliderPair::isQueuedForNarrowPhase` and its writable setter. The existing query derives from `narrowPhaseQueueIndex != SIZE_MAX`. Grid owns enqueue, deduplication, swap-remove cancellation and moved-entry index repair. The pair worker marks its own entry consumed by clearing the index at the current worker-entry boundary. Grid clears the consumed pointer batch after the join/reduction.

**Evidence:** `src/grid.cpp::{queuePairForNarrowPhase,dequeuePairFromNarrowPhase}` currently update both flag and index. `src/physicsmanager.cpp::processNarrowPhasePair` clears both; no independent flag meaning was found. Grid's old commented clearing loop is obsolete. No public installed API exposes this internal pair queue.

**Important distinction:** Before worker launch, a non-sentinel index is a live queue slot. During evaluation, the vector still holds the fixed batch, but a consumed pair's index is already sentinel. Enqueue, cancellation, pair erasure and structural mutation cannot overlap that worker batch. Do not assert a bijection between all batch slots and live indices during processing, or add another phase flag to recreate the deleted state.

**Alternatives considered:** Linear duplicate/cancellation search changes O(1) operations to O(P). A new queue container or generation stamp adds machinery without a need. Clearing all indexes in a new post-worker scan adds work; retain the existing worker-local consumption boundary.

## 3. Nullable support contribution per canonical endpoint

**Decision:** Replace `supportBodyA/supportsA` and `supportBodyB/supportsB` with `contributedSupportBodyA/B`. Null means this pair owns no contribution for that endpoint. A nonnull pointer identifies the exact body for which one contribution has been applied or scheduled. Compute desired contributors using the current Collision support bits mapped from reference/incident to canonical A/B and each endpoint's current Rigidbody. Reconcile old versus desired identity, enqueueing a decrement/increment only when identity changes. Pair removal reconciles both to null before erasure.

**Evidence:** Only `src/physicscontacts.cpp::{reducePairSupport,removePair}` consumes the four old fields. The old pointer is stored even when its support flag is false. `Collision::updateSupportState` produces no bits for triggers or missing bodies. Reference/incident ownership can change independently of canonical pair order. `GameObject` body replacement invalidates response, while `GameManager::reconcilePending` flushes collider operations and support changes before `drainRetirements` frees the old body.

**Rationale:** This reduces four fields and a two-variable consistency rule to two contribution identities. It preserves old-body identity when a new body replaces it, without assuming the current component lookup identifies the recipient of the old decrement. No structure-size or RSS saving is claimed before measuring padding and process noise.

**Alternatives considered:** Recompute all support counts from contacts every step: loses affected-work scaling. Read old body from the current collider at removal: wrong after replacement. Store only flags: loses old-body identity. Store only current Collision bits: wrong after geometry deletes or replaces a contact. A separately allocated support-record object adds allocation and indirection.

## 4. Preserve net support reduction and lifecycle barriers

**Decision:** Retain per-body `supportContactCount`, signed `supportDelta`, `supportDeltaIndex`, and the manager's touched-body queue. Apply the net delta once at the existing flush boundary; wake only on committed positive-to-zero support and reconcile participation on zero/nonzero changes. Do not move this decision into individual pair updates.

**Rationale:** Losing and gaining supports in the same batch must not cause a transient wake. `supportDeltaIndex` remains necessary even when a queued body's delta cancels to zero; deriving queued status from delta would lose deduplication. The pair ledger describes scheduled intent; the public count describes the last committed result. These are different facts, not duplication.

**Lifetime:** Geometry workers may delete the Collision; they do not dereference old contribution recipients or mutate body totals. The serial reduction removes old contributions after joining. Retirement drains grid operations, support deltas and notifications before freeing components. Normal callback exceptions preserve that ordering. This design does not promise new strong exception safety for allocation failure during delta-queue insertion; the current code already lacks transactional insertion there.

**Alternatives considered:** Immediate count edits cause order-dependent wakes. A whole-scene support pass adds scans. Combining body and support queues confuses different validity boundaries and cancellation rules.

## 5. Keep body indexes, make participation derivation auditable

**Decision:** Retain registered bodies, awake dynamic bodies, moving kinematic bodies and sleep candidates, their indexes, and the deduplicated changed-body queue. Use descriptive internal names and one `reconcileBodyParticipation` derivation point. Document a trigger table covering Rigidbody setters, registration/removal, GameManager lifecycle reconciliation, support reduction and sleep transitions. Keep registration/removal and queue cancellation ordering intact. Share the existing sleep-island initialization operation between reconciliation and force integration if it improves directness without new state.

**Evidence:** `src/physicsbodyindexes.cpp` is small but spans body participation and sleep union-find. Its lists have separate consumers in force integration, movement, sleeping and solver-neighbor refresh. `Rigidbody::bodyStateChanged` is gated by registration; lifecycle reconciliation may queue bodies through a different entry. Kinematic velocity/angular changes require reconciliation even when geometry is unchanged. Dynamic velocity writes do not always require it because awake dynamic membership is already valid. Removing that distinction could add per-solver-impulse queue work.

**Rationale:** These lists avoid visiting every static/sleeping body on each force/movement pass. A full scan is a comparison alternative, not the selected design. This plan does not relocate all physics fields into a new registry or remove publicly declared registration accessors. Naming and initialization cleanup supplement the concrete reductions in decisions 2–3.

**Alternatives considered:** Single scan for all body work, immediate per-setter physical-neighbor refresh, more dirty-reason flags, or separate owning classes. Each either adds quiet/churn work, weakens batching, or moves complexity without eliminating it. No sleep algorithm or threshold change is justified here.

## 6. Explicit pair reconciliation and geometry boundary

**Decision:** Use one uncached `isLiveContact` predicate and one solver-eligibility derivation. Retain narrow-phase transition reduction, response-only refresh and final-witness removal as distinct, explicitly named operations. Share support contribution reconciliation and contact/solver membership maintenance where their semantics match; response-only refresh must never emit events or replay geometry. Keep the two small body/pair membership operations local; no generic indexed-container framework.

**Geometry decision:** Add internal `src/physicsgeometry.h` with a single `physics_geometry::updateContactGeometry(const ColliderPair &, const Eigen::Vector2f &gravityDirection)` entry. Keep shape/SAT/clipping helpers implementation-local in physicsgeometry.cpp. Remove their declarations from PhysicsManager; the manager supplies gravity and calls the entry from its existing distinct-pair worker. Preserve Collision reuse, warm-start features, cached version updates and every math/tolerance operation. Geometry may mutate only its pair's contact and derived support bits, not queues, body counters or callbacks.

**Rationale:** PhysicsManager currently supplies only gravity to geometry. This function boundary makes allowed inputs and side effects visible without a heap object, virtual dispatch, result copying or new storage. The pair is held as a const set key because canonical collider identity is immutable; mutable contact/bookkeeping does not alter hashing. A global const/mutable rewrite is unnecessary.

**Alternatives considered:** A new immutable manifold result per evaluation risks extra copies/allocations and warm-start behavior; an owning GeometrySystem is unnecessary. Merging response refresh, geometry and removal into a flag-driven universal reconcile function would hide their different event/lifetime meanings. Existing all-contact membership is retained for the independent oracle and benchmark counts; solver membership is separate physical work, not an interchangeable copy.

## 7. Parallelism and collision architecture

**Decision:** Retain oneTBB over independent cells, active body integration and sufficiently large distinct-pair batches. Keep current thresholds. Support/list/event reduction, shared-body solver, callback dispatch and reclamation remain serial at established join boundaries.

**Assessment:** Tiny quiet batches do not benefit from more tasks. Per-pair support reconciliation must not become a shared worker write. Larger pair batches retain parallel geometry but serial O(P) reduction; dirty solver ordering remains O(S log S), transition ordering O(E log E). Body membership updates remain O(1), neighbor refresh O(degree), and support flushing O(touched bodies). Existing scratch capacities are reused. No new atomics, pools, locks or parallel sorting are needed.

**Risks to evaluate:** Small-work task overhead, skewed cell/pair loads, shared-body solver dependency, allocation/cache effects and serial reduction limits. One/multiworker correctness and timings can expose effects, but do not by themselves identify false sharing, bandwidth or load imbalance as causes. No profiling-based causal claim without such evidence.

**Foundation:** Reviewed the repository's `paper/paper.txt` §§3.1–3.3: subdivision requires deduplicating cell witnesses; segmented lists/checkpoints localize changes, including fast-motion cancellation and split/merge repair; overlap-change output avoids enumerating stable overlaps. This work changes only the consumer queue's redundant membership representation. Endpoint ordering, chunk bindings/checkpoints, pair witnesses and grid algorithms remain unchanged. Paper constants are not adopted as modern tuning evidence.

## 8. Fresh baseline and human acceptance

**Decision:** Capture the current schema-3 engine before runtime changes, in an isolated source/build tree with dependencies preserved. Reuse existing fixtures and readers. Use the fixed 36-group matrix in quickstart.md, five independent timing processes per group, separate per-step correctness captures, and work/storage probes. If an additional fixture is needed, land its identical common patch in both stages before baseline capture.

**Rationale:** Feature 002's schema-2 baseline includes different event semantics. Its current candidate and sanitizer findings are context, not a fresh equivalent baseline. Preserve raw samples, fingerprints, build/hardware, affinity, resolved populations, ordering and checksums; investigate every regression. Existing core tests cover most contact/support behavior; add focused queue-cancellation, replacement/net-support and body-type/kinematic transition gaps before refactoring.

**Human decision:** Provide before/after inventory and eight code-backed walkthroughs, including a count of independent facts and maintenance dependencies for changed paths. A pending maintainer review stays pending. A machine-generated assertion, line count or large document is not readability evidence. Documentation and function moves cannot substitute for the selected queue/support reductions.

**Alternatives considered:** Reusing old raw captures uncritically, comparing only narrow-phase scope, claiming no regression from overlapping ranges, or treating passing tests as readability approval. All would leave required evidence missing.

## Resolved questions

The implementation language/dependencies, internal boundary, candidate state reductions, retained invariants, worker strategy, compatibility, baseline and validation approach are resolved. No blocking clarification remains. Actual correctness, performance, layout/RSS effects and maintainer readability remain implementation/acceptance work; they are not planning findings.

## Implementation inspection: dead clipping branches

The box/box clipping helper has no circle caller; both exact circle dispatch routes return first. Its unreachable circle branches include private cached world-position access. Remove only those branches during extraction and name the private helper calculateBoxContactPoints, avoiding new public accessors/friends or changed position math. Preserve and compare all reachable operations. Evidence: evidence/us1-geometry-before.md.

## Executed corrective decision

The shared reset helper’s first out-of-line definition introduced an avoidable per-awake-body call across translation units. Move its single definition to PhysicsManager’s private header so Release can inline it. The final object has no reset helper reference, full affected validation was repeated, and both initial and final matched captures were retained. This is a measured investigation outcome, not a new field/cache/threshold. See [performance analysis](evidence/performance-analysis.md) for all higher means, control regressions and uncertainty; human readability is still pending.
