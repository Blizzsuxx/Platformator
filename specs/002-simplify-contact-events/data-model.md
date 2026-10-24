# Data Model: Simplify Contact Events

This feature changes in-memory responsibilities and observable callback/diagnostic contracts. Scene serialization is unchanged. See [the plan](plan.md) and [contact contract](contracts/contact-events.md).

## ColliderPair and confirmed contact

| State / field | Owner and meaning |
| --- | --- |
| Canonical collider A/B | Grid's unique candidate pair; endpoint order follows runtime handles, independent of SAT reference/incident selection. |
| Witness count and adjacency/queue indices | Existing Grid bookkeeping for duplicate spatial witnesses and affected narrow-phase work. |
| Optional owned Collision | Pair-owned confirmed manifold; absence means no confirmed contact. Geometry may create, update, or clear it. |
| Cached collider versions | Determine whether this pair needs geometry work. |
| resultHadContact | Pair-local pre-worker presence needed for main-thread before/after reconciliation. |
| resultTestedGeometry | Existing pair-local instrumentation result; no event semantics. |
| activeIndex / solverIndex | O(1) membership repair for confirmed-contact and solver collections. |
| Prior supportBodyA/B and supportsA/B | Canonical endpoint support contribution used to reduce only actual changes, including body replacement. |

Remove `stayIndex` and `enteredStep`. Pair destruction releases its manifold and never accesses the runtime or queues gameplay work.

### Contact state transitions

| Before | After | Event | Membership/support action |
| --- | --- | --- | --- |
| Absent | Absent | None | No confirmed contact; no support contribution. |
| Absent | Present | Enter | Add contact membership; reconcile solver eligibility and support. |
| Present | Present | None | Retain contact; update support/solver eligibility only as physical state requires. |
| Present | Absent | Exit | Remove contact/solver membership and prior support contribution. |
| Present | Pair removal | Exit before erasure | Remove memberships/support through manager, then free pair/manifold. |
| Absent | Pair removal | None | Remove candidate bookkeeping only. |

Neither script count nor callback eligibility participates in this table. Pair removal cannot repeat a geometry-generated exit because that path already cleared the collision. Net spatial witness deltas are consolidated by Grid before removal; temporary witnesses do not independently create transitions.

## Collision geometry

A Collision holds the normal, reference/incident colliders, up to two feature-identified manifold points, warm-start impulses, support flags, and solver data already used by the engine.

The internal geometry operation reads synchronized collider geometry and mutates only the owning pair/manifold. Shape helpers do not emit events or mutate shared body collections. The worker wrapper computes support flags after geometry; shared body support counts change only in the serial reduction.

Keep existing box/circle math, feature identities, clipping acceptance, and fallback policy. Moving fallback construction to the internal geometry module does not introduce a new public Collision operation.

## PhysicsEvent

| Field | Contract |
| --- | --- |
| type | Enter or exit only. No Stay value. |
| colliderA / colliderB | Borrowed canonical endpoints, readable until the current dispatch/retirement barrier finishes. |
| collision | Borrowed current pair manifold for enter; null for exit. Never dereferenced by exit handling. |

PhysicsManager owns the retained-capacity pending event vector. Main-thread contact reduction and pair removal use one enqueue function. That function counts the generated transition once, independent of whether any callback can run.

Each batch is ordered by exit before enter, then canonical A/B handles. Delivery visits A then B, checks each recipient's current lifecycle eligibility, aggregates exceptions under the existing policy, and clears the batch before storage reclamation.

No replay log, observer index, new event identity, subscription entity, manifold snapshot, or replacement Stay mechanism is added. No event survives into a replacement scene.

## Physics collections

| Collection | Membership / purpose | Update trigger |
| --- | --- | --- |
| contactPairs | Confirmed live contacts; diagnostics and existing invariant inspection. Unsorted indexed membership. | Confirmed-contact reconciliation/removal. |
| solverPairs | Confirmed contacts with physical response and a moving/awake eligible body; canonical constraint order. | Geometry, response, body wake/sleep or membership changes. |
| dynamicCollisionScratch | Dynamic subset reused across solver iterations. | Rebuilt by the existing solve phase. |
| awakeBodies | Live awake dynamic bodies for force/integration. | Coalesced body-state changes. |
| kinematicBodies | Live moving kinematic bodies. | Coalesced body-state changes. |
| sleepEligibleBodies | Existing supported or gravity-free awake dynamic bodies eligible for sleeping. | Body/support changes. |
| changedBodies / supportDeltaBodies | Deduplicated affected-body work with reusable capacity. | Actual body-state/support changes. |

Remove `stayPairs`, `activeCollisions`, `stayOrderDirty`, `simulationStep`, and the unused local-results struct. Retain solver-order dirtiness and existing body membership indices, support-delta fields, and sleep-island state. Body adjacency refresh updates solver membership only; script changes never touch these collections.

Solver membership must be canonically prepared before its next solver/debug consumer after membership changes. Do not mutate a collection while its consumers iterate it; body changes discovered in solver callbacks are queued and drained at the existing boundary.

## ScriptComponent startup state

| State | Owner / rule |
| --- | --- |
| pendingBehaviors | Script-owned entries awaiting their one initialization attempt. Startup may append entries. |
| behaviors | Script-owned runnable entries whose initialization was attempted, including logged failures. |
| pendingStartScripts plus queue index | GameManager's cancellable indexed startup queue; cancellation leaves a null slot during iteration. |
| Attempted prefix count | Local startup variable; only this prefix transfers from pending to runnable. |

`startPendingBehaviors` retains no iterators across user callbacks. New entries added during startup may be attempted in the same pass. Deactivation/retirement interrupts the loop and preserves the untouched suffix. Resolve failure skips that entry's start call; every attempted entry is promoted once and is not retried. Ordinary-dispatch additions wait for the next eligible startup pass.

Remove the local `firstBehaviors` condition and all startup observer refresh. These changes introduce no new behavior state or ownership list.

## Diagnostics

Per recorded complete step, enter/exit counters accumulate at transition enqueue, including cleanup events and zero-recipient contacts. Frame start resets them; an idle event outside a recorded frame does not contaminate a future sample.

Schema v3 removes Stay and retains eleven counters and eight timing scopes. Historical v2 artifacts remain immutable with their original semantics. See [benchmark schema contract](contracts/benchmark-schema.md).

## Invariants and validation rules

1. Every collider pair has at most one confirmed Collision, independent of spatial witness count.
2. Every completed absent/present change contributes one generated transition; callback multiplicity never changes this count.
3. An unchanged contact, including stationary/sleeping/trigger contact, emits no recurring event.
4. Each list insertion/removal repairs the moved element's index in O(1).
5. Support flags map through canonical endpoints even if SAT reference/incident roles swap.
6. Wake/sleep and response changes refresh affected solver membership even without geometry changes.
7. Scripts pending initialization receive no callbacks before their existing initialization boundary; adding them creates no replay.
8. Logical retirement suppresses later callbacks immediately; pending records are drained or cleared before referenced allocations are reclaimed, including exceptional cleanup.
9. Serial and parallel processing produce the same canonical transitions and physical state within established test tolerances.
