# Research: Simplify Contact Events

**Date**: 2026-10-25
**Scope**: [spec.md](spec.md), including the requested audit of `physicscontacts.cpp`, `physicsgeometry.cpp`, `physicsbodyindexes.cpp`, and `GameManager::triggerStartedBehaviors`.

Initial constitution gate passed before selecting designs: preserve confirmed-contact and lifecycle semantics, remove recurring Stay work, retain oneTBB ownership boundaries, investigate alternatives and measure performance during implementation. Three research agents independently examined contacts/events, geometry/body bookkeeping, and script startup; findings below were checked against source. No unresolved design questions remain.

## 1. Generate transitions at the existing reduction boundary

**Evidence**: `PhysicsManager::narrowPhase` in `src/physicsmanager.cpp` records each pending pair's old contact presence in `resultHadContact`. Geometry runs independently per pair, using oneTBB above the existing threshold. A serial pass already reduces support and list membership after workers finish. Enter is currently delayed until a separate traversal of script-observed `stayPairs`; Exit is already detected from old/new contact presence.

**Decision**: Compare old and new confirmed contact presence in that serial reduction. Append Enter on absent→present and Exit on present→absent. Append nothing for the other two states. Keep script lookup exclusively in callback delivery. Remove `stayPairs`, `stayIndex`, `stayOrderDirty`, `observed`, `enteredStep`, and `simulationStep` when their remaining uses have been removed.

**Rationale**: The existing changed-pair pass supplies the information without a new queue, script subscription index, full-contact scan, or cross-worker event synchronization.

**Alternatives considered**: Scanning all contacts preserves recurring O(contact count) work; retaining an observer list keeps script-dependent contact production; worker-side appends require coordination and introduce nondeterministic shared mutation. None is necessary.

## 2. Make pair-removal Exit explicit

**Evidence**: `Grid::applyPairWitnessDelta` in `src/grid.cpp` calls `PhysicsManager::removePair` before erasing the last witnessed candidate. The current `ColliderPair` destructor calls `clearCollision`, which queues Exit through the active runtime. Geometry uses `clearCollisionNoEvent`, making destruction an otherwise hidden second event producer. `queueCollisionExit` currently also skips pairs whose endpoints are both retired.

**Decision**: `PhysicsManager::removePair` queues Exit if the pair still owns a confirmed contact, then removes support and memberships before pair erasure. Narrow-phase reduction owns geometry-driven endings. `ColliderPair` destruction only releases contact memory; remove its runtime lookup and event-queue methods. Queue pair-removal Exit even if neither endpoint can receive it; recipient eligibility belongs to delivery, including the both-retired case. Destruction of already-drained manager storage emits no additional events.

**Rationale**: There are two explicit, mutually exclusive end paths: geometry already cleared the contact, or removal still sees it. This avoids both missed removals and destructor-driven duplicates, and makes generated-event counts independent of listeners and retirement delivery eligibility.

**Alternatives considered**: Keeping destructor notifications hides lifetime and runtime dependencies. Emitting only in narrow phase misses pair erasure. Separate contact-history maps and duplicate suppression sets repeat state already available on the pair.

## 3. Retain physical indexes; remove a redundant solver projection

**Evidence**: `src/physicscontacts.cpp` maintains `contactPairs`, `solverPairs`, and `stayPairs`. `src/benchmark_main.cpp::validatePhysicalState` enumerates `getContactPairs()` for an independent support/manifold oracle, so the all-contact index is not just an unused metric. `activeCollisions` is rebuilt solely by projecting ordered `solverPairs` and is read only by resolution and debug drawing. `dynamicCollisionScratch` selects dynamic contacts for repeated solver iterations and has a different role.

**Decision**: Retain indexed all-contact membership, ordered solver membership, adjacency, witness counts, cached support contributions and body delta reduction. Delete Stay membership and the `activeCollisions` projection; solver/debug consumers dereference `pair->getCollision()` directly from `solverPairs`. Retain reusable `dynamicCollisionScratch`. Remove the unused `NarrowPhaseLocalResults` declaration after confirming repository-wide absence of users.

**Rationale**: Each remaining collection has a distinct consumer and invariant. Deleting `activeCollisions` removes a full rebuild and an extra invalidation relationship. Keeping `contactPairs` preserves validation without scanning every candidate, including broad-phase false positives.

**Alternatives considered**: Rebuilding all physical subsets each step loses affected-work scaling. Replacing the all-contact list with a count breaks the existing oracle; replacing it with candidate scans adds unrelated work. A generic indexed-container framework adds machinery to two small domain-specific helpers. Retaining the solver projection would require measured benefit rather than convenience.

## 4. Rename solver refresh and remove script coupling

**Evidence**: `refreshSubscriptions` currently refreshes every touching pair's physical and observer membership. Calls from first behavior startup and SCRIPT reconciliation serve observers. The call from `PhysicsManager::reconcileBody` is physically necessary: solver membership depends on body type and sleeping state. Collider activation and `refreshResponse` also have physical consequences.

**Decision**: Replace the subscription concept with an explicitly physical adjacency refresh, named `refreshSolverPairs(Collider*)`. Limit it to solver participation and retain it for relevant body/collider state changes. Remove all calls caused solely by script addition, startup, replacement or activation. Keep response/support updates in their existing owner. Audit COLLIDER reconciliation separately from SCRIPT reconciliation instead of deleting the combined branch blindly.

**Rationale**: Removing every function named subscription would leave stale solver membership even after Stay is gone. A physical name and narrower operation expose the real dependency.

**Alternatives considered**: Retaining the old name obscures ownership. Waking or revisiting all bodies/contacts on script changes defeats the feature. Moving all membership work into narrow phase misses sleeping/body transitions that do not change geometry.

## 5. Consolidate geometry by responsibility

**Evidence**: `src/physicsgeometry.cpp` contains direct circle-circle and circle-box routines. Shape dispatch, box SAT projection tests and contact clipping remain in `src/physicsmanager.cpp`. The function named `satCreateCollision` also dispatches the non-SAT circle routines. These functions mutate only the current pair/contact and use immutable published collider geometry and gravity direction; they must never enqueue events.

**Decision**: Move shape dispatch, `checkProjections`, box SAT and `calculateContactPoint` into `physicsgeometry.cpp` alongside the circle routines. Rename `satCreateCollision` to `updateContactGeometry`, reflecting updates, separation and multiple algorithms. Keep the existing pair-owned geometry contract, calculations, feature IDs, tolerances, orientation and cached versions unchanged. Move required includes with the functions. Do not introduce a geometry service or result-object hierarchy.

**Rationale**: A single cohesive geometry translation unit makes the worker mutation boundary reviewable and reduces manager responsibilities. Moving functions is justified by ownership, not a filename preference. Mathematical or clipping changes would increase correctness scope without helping transition semantics.

**Alternatives considered**: Merging everything back into the manager hides responsibilities. Introducing polymorphic shape-pair handlers adds indirection. Rewriting the circle routines or SAT is unsupported by a defect in this feature. Unreachable-branch cleanup can wait rather than alter geometry during this move.

## 6. Keep body indexes and sleeping invariants explicit

**Evidence**: `src/physicsbodyindexes.cpp` is a small implementation of O(1) swap membership for awake, moving kinematic and sleep-eligible lists, deduplicated body-change queueing, and sleep-island operations. `reconcileBody` initializes newly sleep-eligible nodes and currently reaches the physical solver refresh through the misleading subscription name.

**Decision**: Retain these body lists, indexes, change queue and island behavior. Keep the file as the body membership/sleep bookkeeping owner, with the solver refresh named by its actual physical responsibility. Remove no body state merely because it looks like contact-event state. Avoid cosmetic file splitting or merging into the manager.

**Rationale**: Replacing the indexes with scans would regress idle/static/sleeping scenes. A new container abstraction or body manager would create more ownership boundaries, while the actual cross-system smell is removed by decision 4.

**Alternatives considered**: One general body list requires filtering all bodies repeatedly; a global sleep redesign is unrelated. Renaming files alone offers no behavioral or ownership improvement.

## 7. Give ScriptComponent ownership of per-script startup

**Evidence**: `GameManager::triggerStartedBehaviors` drains a queue by index because callbacks can append/cancel scripts. Its inner loop checks `canDispatch` for each behavior, resolves references, logs failures independently, and promotes the attempted pending prefix even if an individual initialization failed. Additions during startup can participate in that pass; additions during ordinary callbacks await the next startup phase. Existing `callback_mutation_test.cpp` covers these distinctions.

**Decision**: Keep the indexed `pendingStartScripts` queue, its cancellation tombstones and all phase semantics. Rename the manager operation to `startPendingScripts` and move the per-script initialization loop, failure handling and attempted-prefix transfer into a private `ScriptComponent::startPendingBehaviors(platformator::ReferenceResolver&)`. The manager owns the resolver, queue drain and cancellation; the component owns its behavior collections. Delete `firstBehaviors` and physics-subscription refresh. Make this a mechanical responsibility extraction after contact decoupling, preserving failure promotion and dynamic append behavior exactly.

**Rationale**: This addresses the flagged manager smell without changing scheduling or adding a lifecycle framework. It is one private method on the existing owner, with no extra queue or persistent state.

**Alternatives considered**: Only deleting the subscription branch leaves the manager manipulating component-owned pending ranges. Iterator traversal, swapping away the start queue, one fixed starting size, or failing the whole batch on an exception changes documented behavior. A generic lifecycle dispatcher is unnecessary.

## 8. Define ordering, lifetime and exception boundaries

**Evidence**: Current narrow phase sorts pending Exits by canonical handles, then appends canonical observed Enter/Stay records. Delivery visits endpoint A before B, catches each endpoint failure, clears the queue and rethrows the first exception. `GameManager::reconcilePending` flushes removal work and dispatches cleanup events before `drainRetirements` frees allocations. Supported callback mutation is deferred; it does not synchronously append physics events while their vector is being iterated.

**Decision**: Canonicalize each nonempty batch immediately before delivery, including cleanup batches: Exit family first, then Enter family, each by the full ordered collider handles. Keep A-before-B delivery and exception aggregation. Do not deduplicate distinct transitions by pair. Enforce the current phase invariant that a contact with an undelivered Enter cannot be destroyed before that batch is dispatched. Main physics delivery precedes structural cleanup; cleanup delivery precedes physical reclamation and replacement-scene publication.

**Rationale**: Sorting only changed events covers both producers and avoids sorting persistent contacts. Existing deferred mutation keeps borrowed pointers valid without a new persistent event bus or payload copies.

**Alternatives considered**: Worker completion order is not a contract. Global pair-first ordering changes the existing Exit-before-Enter behavior. New handle-resolution layers or per-step event snapshots are unnecessary without a supported path violating the established lifetime boundary; any such discovered path is a correctness defect to fix before acceptance.

## 9. Version benchmark changes and count at generation

**Evidence**: `src/benchmark.cpp`, `benchmark/sweep_scaling.py`, `benchmark/plot_scaling.py`, `benchmark/tests/test_sweep_scaling.py` and `tests/benchmark_contract_test.cpp` use schema 2. The live format requires twelve counters including `collision_stay_event_count`. Current event counts are obtained by scanning the queue in narrow phase, before later cleanup Exits exist.

**Decision**: Emit schema 3 consistently across summary/raw CSV, manifests, aggregates and console summaries; remove Stay to leave eight scopes and eleven counters. Keep the Enter/Exit names but define their values as pair events generated during the complete step, including cleanup. Increment the recorder at the single manager-owned event enqueue operation using existing frame-scoped accumulation. Remove the queue-count scan. Update all live readers, fixtures, validation and plotting code together. Current tools reject schema-2/mixed input clearly; historical captures remain unchanged and can be read with their corresponding tooling revision.

**Rationale**: Both the metric set and event-count meaning change. A version boundary prevents silent interpretation of old values as new semantics. Counting where events originate includes listener-free and cleanup cases without a second history.

**Alternatives considered**: Keeping schema 2 silently changes a strict format. A permanently zero Stay field contradicts removal. Silently converting old counters loses provenance and cannot reconstruct missing listener-free or cleanup events.

## 10. Validate correctness before making performance claims

**Decision**: Record a pre-change source fingerprint and baseline before modifying runtime code. Use existing Debug, ASan/UBSan, TSan, affected-work and Release benchmark configurations. Extend transition/lifecycle/geometry/support tests and current benchmark contract tests. Add focused internal event tests and test-only event-work counters; no new public event-inspection interface. Compare zero/one/two recipients, 100 unchanged updates, removal paths, shape families, cell witnesses and one/multiple workers. Include observer-heavy persistent scenes as well as the repository's existing representative broad/narrow/body/churn workloads. Retain raw timing/allocation data and independent repetitions.

**Rationale**: Removing O(observed contacts) event traversal is a complexity conclusion; runtime improvement and the extra solver-pair dereference require measurement. Existing headless benchmark cases largely lack scripts and alone cannot demonstrate the removed observer cost.

**Alternatives considered**: Reusing old Stay assertions no longer tests the contract. Testing callbacks alone cannot prove events exist without scripts. Running correctness instrumentation in timing captures distorts comparisons. Invented latency/speedup targets have no project basis.

## Primary references checked

- Repository source and tests cited above; `.specify/memory/constitution.md` version 1.1.0; `benchmark/README.md`.
- [Tracy, Buss and Woods, local paper](../../paper/paper.txt), §§3.1–3.3: retain spatial witness deduplication and segmented sweep invariants; use overlap changes without confusing candidates with confirmed contacts. The broad-phase algorithm itself is not redesigned.
- [Unity collision-event documentation](https://docs.unity.com/en-us/engine/6000.7/manual/physics-section/physics-overview/collision-section/collider-interactions/oncollision): Unity exposes Enter, Stay and Exit. Removing Stay is an explicit product choice for this feature; this plan claims no other Unity callback-eligibility equivalence.
- [oneAPI parallel_for specification](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.0-rev-3/elements/onetbb/source/algorithms/functions/parallel_for_func): iteration order is not deterministic. Keep pair-local work parallel and event/support reduction ordered after completion rather than deriving event order from worker completion.
