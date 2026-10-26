# Runtime State and Ownership

This refactor changes no serialized model. The tables describe retained in-memory facts and the few selected removals. See [research.md](research.md) for source evidence and alternatives.

## Runtime lifetime

| State | Owner and relationship | Validity rule / change |
| --- | --- | --- |
| `activeRuntime` | Runtime; non-owning pointer to the one public lifetime owner | Publish after successful manager construction; keep through explicit shutdown; clear after manager reset. Access without a live runtime rejects as before. |
| `gameManager`, `paths` | Runtime-owned resources | Manager owns scene/runtime machinery; paths supplies asset resolution. No worker or callback outlives this ownership interval. |
| GameManager static instance | Duplicate non-owning pointer today | Remove with its getter/setter/publication sites; RuntimeAccess resolves the existing Runtime-owned manager instead. |
| `shuttingDown`, `shutdownComplete` | GameManager | First prevents structural mutation during teardown; second makes repeated shutdown safe. Different facts; retain both. |
| Runtime serial / handle generations | ObjectRegistry | Bound references expire at logical retirement and cannot revive in a later runtime or reused slot. Preserve. |

Lifecycle: construct resources → publish Runtime → execute scenes/steps → explicit shutdown with Runtime and manager still accessible → destroy manager → clear active Runtime. Do not move callbacks or resource release into a window where the owning `unique_ptr` has already reset.

## Execution phase and lifecycle changes

| State | Producer / consumer | Invariant |
| --- | --- | --- |
| `phase`, `mainThread` | PhaseGuard / mutation and step boundaries | Previous phase is restored on normal or exceptional exit; structural mutation follows existing thread/phase restrictions. |
| `reconciliations`, `reconciliationDrain`, per-component queued marker | Object/component changes / commit | Pending/draining work are distinct during reentrant callbacks. One pending entry per queued component; clear/requeue at the established boundary. |
| `systemsRegistered`, `callbackEligible`, lifecycle state | Reconciliation and logical retirement / system lists and dispatch | Registration records applied system membership; callback eligibility can change before registration catches up. They are not interchangeable. |
| Pending starts and started state | Registration/behavior attachment / start phase | Eligible behavior starts once; inactive or retired work does not start; later reactivation does not repeat initialization. |
| `retiredComponents`, `gameObjectsToDelete` | Logical retirement / destruction drain | Identity is invalidated immediately; allocations survive required support/event/destruction work. All callbacks in the batch finish before its storage is freed. |
| `idToObjectMap`, registry | Publish/retire / file-ID and handle lookups | Scene IDs and stable handles serve different reference contracts. Preserve both and their existing update order. |

Reconciliation runs until permitted reentrant pending work is drained. Removed/retired participation is excluded before reclamation. Preserve first-error handling and remaining cleanup obligations; do not replace failed internal invariants with silent recovery.

## Scene request

`pendingSceneRequest` remains an optional request with an optional path:

| Value | Meaning |
| --- | --- |
| No request | No scene action selected |
| Request with no path | Clear the current scene |
| Request with a path | Prepare and replace with that scene |

New requests replace the pending request. Commit selects and clears that slot once; later requests belong to a subsequent commit. Preparation uses temporary ownership and leaves the current scene intact on failure. Publication follows old-scene reconciliation/retirement. Remove the unused `sequence` and `sceneRequestSequence`; no ordering information is lost because there are no readers. Preserve `nextFileId` bounds and collision checks.

## Transform synchronization

| State | Purpose and owner | Rule |
| --- | --- | --- |
| Local position and cached world position | GameObject; public reads versus synchronized consumers | Preserve current immediate read semantics and physics/render publication points. Workers integrate only their own local fields. |
| `dirtyTransforms`, `transformQueueIndex` | GameManager queue / GameObject slot | At most one queued entry; swap-removal repairs the moved slot; cancellation precedes free. |
| Hierarchy dirty/order and ordering-needed state | GameManager / per-object hierarchy order | Rebuild order on hierarchy changes; sort dirty nodes parent-first only when required. |
| Transform stamp | GameManager pass / per-object visit marker | A subtree reached through overlapping queued ancestors/descendants is published once per pass. |

Retain hierarchyDepth and the accumulated dropped-step statistic in this pass as documented dispositions; neither is part of the selected reduction. Keep transform wake/collider consequences unchanged. Returning the internal all-object vector as const prevents mutation outside these invariants.

## Sleep, support, and participation

| State | Owner / lifetime | Producer and consumer |
| --- | --- | --- |
| `sleeping`, `sleepTimer`, motion/force values | Rigidbody; persistent | Explicit setters and sleep decisions / participation, integration, solver. A real sleep/wake transition resets the timer; an unchanged request does not. Sleep entry also clears linear/angular motion and accumulated force/torque. |
| `awakeDynamicBodies`, `movingKinematicBodies`, `sleepCandidates` and corresponding slot indexes | PhysicsManager / Rigidbody | Derived from current registration, activity, type, sleep, motion, support, and gravity at reconciliation boundaries. Keep indexed add/remove and bounded candidate iteration. |
| `pendingBodyChanges`, `participationQueueIndex` | PhysicsManager / Rigidbody | Coalesce repeated changes. Cancel before removal/free. Do not mutate fixed traversal membership until its prescribed drain. |
| `sleepParent`, `sleepRank` | Rigidbody; step-local group scratch | Reset before awake integration and when newly entering candidates; solver preparation unions eligible bodies. Find uses root discovery plus path compression; merge uses rank. |
| `islandQuiet`, `islandTimer` | Root scratch; one decision pass | First pass aggregates every candidate's motion and minimum persistent timer. Second pass computes common elapsed quiet time or reset, then applies individual transitions. |
| Pair support contributor pointers | Contact pair | Identify the exact old/new recipient even during replacement; no contributor means no applied-or-queued contribution. |
| Body support total, delta and queue slot | Rigidbody plus PhysicsManager reduction queue | Commit net support once per batch. Wake on genuine committed final loss, not an intermediate zero during transfer. Old recipient survives through this flush. |
| Contact and solver lists | PhysicsManager | Contact membership, solver participation, and callbacks are separate consumers. Sleep changes refresh affected physical adjacency; solver order stays append/swap-remove. |

Retained transition order:

1. Publish pending transforms and perform the existing collider/contact/support flush.
2. Derive body participation at existing boundaries; workers reset/integrate awake bodies.
3. Solver preparation can wake sleepers and merge eligible candidates; solve shared-body work sequentially.
4. Reconcile queued body changes, including newly awake candidates whose scratch needs initialization.
5. Aggregate all candidate groups, then apply timers/sleep decisions without list mutation during traversal.
6. Drain participation changes after both sleep passes; movement uses the resulting lists. Complete later transform/callback/lifecycle phases in their established order.

Public timer and support methods remain available. Existing methods do not gain new validation, wake effects, or synchronization merely because some repository callers are sparse.

## Selected maintenance reductions

| Before | After | Journey |
| --- | --- | --- |
| Unused scene sequence fields and increments | Pending payload alone expresses last-wins | J6 |
| Two private methods with no callers | Deleted declarations and definitions | J2/J6 |
| Duplicate ordinary/callback rendered bodies | One callback-capable body and empty-callback wrapper | J1 |
| Runtime and GameManager active pointers plus fallback | One Runtime authority; preserved access boundary | J6 |
| Mutable internal object collection accessor | Const reference matching the public contract | J2/J6 |
| Timer reset repeated across sleep/wake branches | One transition-only reset after the no-op guard | J4/J5 |
| Unused impact receiver argument and stale commented operations | Predicate describes the body actually examined | J5 |

Implementation must verify these reductions in the final source and record any changed disposition. No line-count or object-size reduction is claimed by this table.
