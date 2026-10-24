# Research: Simplify Contact Events

**Date**: 2026-10-25
**Source baseline**: `0ca09758f5148426139a923a1e7b9404471eac67`
**Inputs**: [Specification](spec.md), project constitution, and the user's focus on `physicscontacts.cpp`, `physicsgeometry.cpp`, `physicsbodyindexes.cpp`, and `GameManager::triggerStartedBehaviors`.

Research used the repository implementation, existing tests, and the included sweep-and-prune paper. Two research agents independently traced contact lifetime and behavior startup; their findings were checked against body indexing and solver consumers. No runtime code or tests were changed or executed during planning.

## R1. Generate events where confirmed contact state changes

**Decision**: Compare the pre-worker `resultHadContact` flag with the final presence of the pair's `Collision` in the serial reduction after narrow-phase workers join. Enqueue enter for absent-to-present and exit for present-to-absent. Generate no event for either unchanged state.

**Evidence**: [physicsmanager.cpp](../../src/physicsmanager.cpp), `narrowPhase` and `processNarrowPhasePair`, already preserve the required before/after state. [physicscontacts.cpp](../../src/physicscontacts.cpp), `observed` and `refreshPairLists`, restrict `stayPairs` to objects with runnable scripts; `narrowPhase` currently derives both enter and Stay from that observer list. This is the dependency to eliminate.

**Rationale**: Workers retain pair-local geometry writes; transition production and shared collection changes remain on the runtime thread. The existing pending-pair queue already identifies affected geometry. No recurring contact traversal or event subscription index is necessary.

Scriptless contacts now require enter/exit records that the old observer-dependent path omitted. Initial-contact and churn workloads can therefore perform more legitimate event work; the expected reduction concerns recurring Stay processing and observer invalidation. Measure those workload differences explicitly rather than assuming every workload becomes faster.

**Alternatives considered**:

- Keep an observer list for enter/exit: rejected because script changes would still affect contact notification history and require subscription invalidation.
- Traverse all confirmed contacts each step: rejected because unchanged contacts carry no transition information.
- Emit from shape routines or workers: rejected because it couples geometry to gameplay and requires concurrent event collection.

## R2. Make PhysicsManager own transition production and ordering

**Decision**: Keep one retained-capacity pending event vector. Ordinary transitions are produced by narrow-phase reduction; pair removal produces its single exit in `PhysicsManager::removePair` before Grid erases the pair. Make `ColliderPair` destruction release its collision only. Remove `queueCollisionExit`, eventful `clearCollision`, and the pair's dependency on RuntimeAccess/GameManager.

**Evidence**: [grid.cpp](../../src/grid.cpp), `applyPairDelta`, already calls `owner->removePair(pair)` immediately before pair erasure. [colliderpair.cpp](../../src/colliderpair.cpp) currently queues an exit from its destructor through the runtime singleton. This hides event generation and suppresses generation when both endpoints have retired.

**Rationale**: A confirmed contact has one terminal transition even when neither endpoint can receive callbacks. Recipient eligibility belongs to delivery. A pair that already lost its collision during narrow phase cannot produce another exit when later erased. Temporary lookup pairs and teardown destructors have no gameplay side effects.

Before each delivery batch, sort only queued events by exit before enter, then canonical endpoint handles; deliver endpoint A before B. This preserves the current ordinary-step exits-before-enters convention and gives cleanup/idle exit batches the same deterministic order. Use allocation-free in-place sorting; each reconciliation boundary dispatches its own batch. Do not coalesce distinct transitions from distinct completed reconciliation boundaries.

Borrowed `Collision*` for enter and borrowed endpoint pointers keep their existing lifetime: dispatch finishes before pair/collider storage is reclaimed. Exit carries no collision pointer. Collision callbacks may retire objects logically but cannot flush structural deletion while the batch is executing. On exceptional cleanup, drain or clear pending records before reclaiming their borrowed storage; verify this explicitly rather than adding per-event ownership or a general event bus.

**Alternatives considered**:

- Keep destructor-driven exits: rejected because ownership, diagnostics, and both-retired behavior remain implicit.
- Copy every manifold into events or introduce persistent event handles: unnecessary under the existing dispatch/reclamation barrier; would add data and lifetime policy.
- Sort the full contact set: rejected; ordering concerns only the event batch and the solver's own membership.

## R3. Preserve necessary physics indexes while removing observer state

**Decision**: Remove `stayPairs`, `stayIndex`, `stayOrderDirty`, `observed`, `simulationStep`, `enteredStep`, and all script-subscription refreshes. Keep indexed `contactPairs`, `solverPairs`, body work lists, and coalesced support/body queues. Replace the body-side use of `refreshSubscriptions` with an adjacency-local `refreshSolverPairs(Collider*)` that updates solver eligibility only.

**Evidence**: [physicsbodyindexes.cpp](../../src/physicsbodyindexes.cpp), `reconcileBody`, calls `refreshSubscriptions`; that function also updates solver membership through `refreshPairLists`. Deleting this call without replacement would prevent stationary sleeping/waking contacts from entering or leaving the solver correctly. [physicscontacts.cpp](../../src/physicscontacts.cpp) also reduces support by canonical pair endpoints, handles body replacement, and wakes a body on loss of final support.

**Rationale**: Contact membership describes geometry; solver membership depends on response and awake body state; script eligibility determines callback delivery only. The remaining indexes have actual consumers and provide constant-time insertion/removal. An awake or response change revisits only the affected collider's adjacency. Script-only changes revisit no physics adjacency.

Remove the mirrored `activeCollisions` vector and have solver/debug consumers read each collision from the canonically ordered `solverPairs`. Retain `dynamicCollisionScratch`, which is the dynamic subset reused across solver iterations. Remove the unused `NarrowPhaseLocalResults` declaration. Do not replace these removals with a generic indexed-container framework.

**Alternatives considered**:

- Remove every maintained list to reduce field count: rejected because it reintroduces whole-body/contact scans.
- Keep `activeCollisions` as a mirror: rejected because every element already belongs to `solverPairs` and its collision is directly available there.
- Use one list for geometry, response, and subscribers: rejected because their membership rules differ.

## R4. Replace fragmented manager implementation with cohesive responsibilities

**Decision**:

- Fold retained contact membership/support reduction and body membership/sleep-island helpers from `physicscontacts.cpp` and `physicsbodyindexes.cpp` into `physicsmanager.cpp`, beside the phases that own their state; delete the two fragment files.
- Replace `physicsgeometry.cpp` with internal `collisiongeometry.h/.cpp`, containing all shape detection currently split between it and `physicsmanager.cpp`: circle/circle, circle/box, box SAT, clipping, and fallback-point construction.
- Expose one internal function, `platformator_detail::updateContactGeometry(const ColliderPair&)`. It reads synchronized collider geometry and updates only that pair's collision/manifold. It has no manager, script, event queue, body-list, or runtime-singleton dependency.
- Compute the collision's support flags once in the worker wrapper after successful geometry update; response-only updates retain their existing controlled support refresh.
- Move fallback-point construction into the geometry implementation using existing public collision accessors, removing the now-unused private `Collision::generateFallbackContactPoint` member. Preserve its existing calculation and clipping acceptance policy in this refactor.
- Make the clipping helper box-only after all shape routing is consolidated. Remove its circle branches only after the call graph and geometry regressions prove those branches unreachable.

**Evidence**: [physicsgeometry.cpp](../../src/physicsgeometry.cpp) contains 83 lines of circle-specific PhysicsManager members, while its dispatcher, box SAT, and clipping remain in [physicsmanager.cpp](../../src/physicsmanager.cpp). The other two fragment files add 279 lines of members manipulating the same manager fields. The geometry move offsets the consolidation rather than merely enlarging the manager.

**Rationale**: This gives geometry a bounded input/output and places mutable scheduling state with its owner. No new subsystem class, virtual interface, public API, or independent contact manager is needed. Existing shape math, warm-start feature identities, parallel-read assumptions, support reductions, and sleep-island algorithms remain intact.

**Alternatives considered**:

- Concatenate all three files into PhysicsManager: loses a useful separation between pure pair geometry and shared phase state.
- Add ContactManager, BodyIndexManager, and startup services: duplicates ownership and expands coordination for this small change.
- Rename files without changing responsibilities: leaves the geometry split and hidden manager coupling intact.
- Rewrite solver or sleep-island algorithms: outside the evidence and scope of this feature.

## R5. Simplify startup by removing its physics side effect

**Decision**: Remove `firstBehaviors`, its collider lookup and subscription refresh from `triggerStartedBehaviors`, and remove subscription calls from GameManager reconciliation. Rename the method to `startPendingBehaviors` and name its processed prefix clearly. Keep the current queue and promotion algorithm.

**Evidence**: [gamemanager.cpp](../../src/gamemanager.cpp), `triggerStartedBehaviors`, `queueScriptStart`, `cancelScriptStart`, and `reconcilePending`; [scriptcomponent.cpp](../../src/scriptcomponent.cpp), `addBehavior`; [CPP_API_MIGRATION.md](../../docs/CPP_API_MIGRATION.md), lifecycle section; [callback_mutation_test.cpp](../../tests/callback_mutation_test.cpp).

**Rationale**: Index-based iteration and nullable cancellation slots are required because startup can append or cancel queue entries. A growing pending-behavior list permits additions during start in the same startup pass. Only the attempted prefix is promoted; an interrupted suffix remains pending. Resolve/start exceptions are logged per behavior; attempted entries, including failures, become runnable and are not retried. Additions during ordinary dispatch wait until the next startup pass. These are existing contracts, not incidental complexity.

Do not extract a new startup service or change ownership, resolver timing, failure handling, promotion visibility, or `canDispatch()` gates.

**Alternatives considered**:

- Range iteration, a copied queue, or replacing the loop with a drain-until-empty loop: risks iterator invalidation, delayed additions, or infinite retries of interrupted entries.
- Promoting each behavior before its start returns: changes which behaviors callbacks and lookups observe.
- Excluding failed behaviors from ordinary dispatch: contradicts the documented one-attempt initialization policy.

## R6. Count generation across the complete step and version changed output

**Decision**: Increment enter/exit benchmark counters at the single manager enqueue function. Remove the narrow-phase event recount and Stay enum/name/column. Record transitions during cleanup as well as ordinary physics; idle transitions outside an active recorded step do not get attributed to a later step.

Publish benchmark schema version 3 consistently in manifests, summary/raw CSV, console output, sweep aggregates, current readers/plotting, fixtures, and documentation. New output has the existing eight timing scopes and eleven counters. Keep the existing enter/exit counter names, with their clarified meaning: generated pair transitions, including zero-recipient transitions. Do not add callback-count reporting. Preserve archived v2 results and specifications unchanged; current tools reject v2 or mixed-version captures with a clear migration error.

**Evidence**: [benchmark.cpp](../../src/benchmark.cpp), `addCounter` ignores calls outside an active frame. Existing event counters are set by recounting pending events in `narrowPhase`, so cleanup-generated exits are omitted. [sweep_scaling.py](../../benchmark/sweep_scaling.py) validates the exact v2 counter set; [plot_scaling.py](../../benchmark/plot_scaling.py) validates version 2; [test_sweep_scaling.py](../../benchmark/tests/test_sweep_scaling.py) builds corresponding fixtures. [capture_cleanup.py](../../benchmark/capture_cleanup.py) imports the sweep parser and needs an integration check.

**Rationale**: Removing a required column and changing scriptless enter counts is a data-contract change. A version bump prevents old and new workload/event counters from being silently treated as equivalent. Timing scopes and equivalent workloads remain comparable with recorded provenance; event-count series require their respective semantics.

**Alternatives considered**:

- Keep a permanently zero Stay column: contradicts removal of Stay as a supported diagnostic category.
- Remove the column under v2: makes the same version identify incompatible required metric sets.
- Count callbacks: loses contacts without recipients and multiplies events with behavior count.

## R7. Validate changed behavior and scaling at the affected boundaries

**Decision**: Extend the existing physics/support, mutation/startup, retirement, benchmark, and work-probe tests, and add one focused internal contact-transition contract test. Inspect generated events directly with the existing internal PhysicsManager pending-event access at controlled test boundaries; no production observer API is needed. Keep tests of callback delivery separate.

Cover scriptless/one-sided/two-sided recipients, empty/unstarted scripts, late attachment, persistent stationary/sleeping/trigger contacts, separation/re-entry, duplicate cell witnesses, coalesced toggles, removal/replacement, both-retired endpoints, callback exceptions, and serial/parallel canonical ordering. Preserve existing geometry, 128-support, tall-stack/wide-pile, startup queue, and scene-change assertions. Remove obsolete Stay cases; retain their mutation/lifetime intent through the remaining callback families.

Replace the obsolete `stayPairs` work probe with a small transition count/visit probe at the actual event producer, extending the warmed-contact scenario to include eligible scripts. Script-only changes must not visit contact adjacency; idle contacts must not incur event work or new allocations.

Use separate correctness and Release captures for existing broad, dense, narrow, rigid-body, and churn workloads; compare serial/multiworker snapshots before timings. Run comparable before/after captures at the recorded source baseline with the original v2 tooling and the new v3 tooling respectively. Retain manifests/raw samples; do not invent a universal speedup threshold or run the full historical 180-process study as a planning prerequisite.

**Evidence**: [benchmark methodology](../../benchmark/README.md), [runtime_work_test.cpp](../../tests/runtime_work_test.cpp), existing CTest registrations, and the included [paper, section 3.3](../../paper/paper.txt). The paper motivates overlap-change output without traversal of all unchanged overlaps; broad-phase witness changes must still remain distinct from confirmed narrow-phase contacts.

**Rationale**: The expected benefit is elimination of Stay traversal, observer invalidation, and mirrored solver storage. Necessary solver/support work remains proportional to affected physics state. Geometry comparisons and body-state regressions prevent a bookkeeping cleanup from weakening correctness.

**Alternatives considered**:

- Rely only on existing callback traces: cannot prove that scriptless contacts generate events, or that cleanup exits are included in diagnostics.
- Treat a large timing study as the first gate: expensive and unable to establish lifecycle correctness; focused invariants and work probes come first.
- Add production observation hooks solely for tests: unnecessary because existing internal physics entry points and diagnostic recording expose the relevant boundaries.

## Resolved research questions

| Question | Resolution |
| --- | --- |
| Where do script-independent enters come from? | Post-worker before/after contact reconciliation. |
| Who owns removal exits? | PhysicsManager, before pair erasure; pair destruction has no event side effect. |
| Can all subscription call sites simply disappear? | GameManager calls disappear; body changes retain a solver-only adjacency refresh. |
| Are body/support indexes obsolete with Stay? | No; preserve their physics consumers and constant-time membership. |
| What happens to the three source fragments? | Two fold into the owner; all geometry becomes one independent internal module. |
| Does startup need a new lifecycle algorithm? | No; remove physics coupling and preserve queue/initialization semantics. |
| How do event reports stay trustworthy? | Count enqueue operations across the recorded step and migrate output/readers to v3. |

All planning unknowns are resolved. No user clarification, new dependency, constitution exception, or broad-phase algorithm change is required.
