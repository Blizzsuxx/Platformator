# Contract: Physics State and External Compatibility

## Existing authoring interface

This is an internal refactor. Preserve installed Rigidbody/Collider/Behavior/Runtime operations, return values, validation errors, scene serialization, object handles and callback payload lifetimes. Do not remove public registration/support accessors while simplifying internal metadata. Public object layout may change only with the existing engine/consumer rebuild model; no new ABI guarantee is created. Run installed/relocated consumer checks.

Body type, velocities, gravity, activation and component replacement take effect at the same established runtime/physics boundaries. Preserve collision math/tolerances, feature IDs, warm-start impulse reuse and reference-face behavior. Existing physical behavior is the baseline, not the pre-7906959 engine. No new trigger-event family, contact-query surface, or recurring callback is introduced.

## Body and contact participation

- Exactly one registered instance participates for each current body/collider under existing lifecycle rules. Pending work must not keep a retired instance alive beyond the designated runtime retention boundary or use it after free.
- Awake dynamic, moving kinematic and sleep candidate memberships obey data-model.md. An internal name or file change cannot change the predicates or their visibility timing.
- Several cell witnesses identify one candidate pair. Candidate presence is not confirmed contact. Losing one witness cannot end a still-covered contact; final removal cancels queued evaluation before pair erasure.
- The optional Collision remains confirmed-contact presence. Workers own distinct pairs; support counts, lists and events are mutated only after joining.
- Body/response changes may refresh physical eligibility without recomputing geometry. A response-only refresh must not synthesize Enter/Exit, change notification history or rely on scripts.

## Support ownership

Each nonnull `contributedSupportBodyA/B` records one applied-or-scheduled contribution to that exact Rigidbody. It is not an assertion that the body remains the collider's current component. Reference/incident support bits must map to canonical endpoints on every reconcile.

Queue -1 for the previous recipient and +1 for the desired recipient only when identity differs. Pair removal queues both old contributions away and clears the ledger. Net per-body support changes commit at the existing batch boundary, with no negative totals and no spurious wake from intermediate zero. Final committed support loss wakes under the existing contract. Retired recipients remain allocated until their deltas have been consumed.

## Events and callbacks

Retain the [contact transition contract](../../002-simplify-contact-events/contracts/contact-events.md): one pair-level Enter for absent→present, one Exit for present→absent, no Stay or repeated event for unchanged contact, generation independent of scripts, no replay, delivery-time recipient eligibility. Exit before Enter, canonical pair order, then canonical endpoint A/B delivery remain unchanged. Callback exceptions must not prevent cleanup or independent eligible deliveries required by the established contract.

Scene replacement flushes/cancels old work under the existing lifecycle contract before its storage is freed and new-scene work is published. No event may leak to the replacement scene.

## Internal geometry interface

Add only an internal header, not an installed API:

```cpp
namespace physics_geometry {
void updateContactGeometry(const ColliderPair &pair,
                           const Eigen::Vector2f &gravityDirection);
}
```

The call can mutate/reuse/delete only this pair's contact, its geometry version stamps, manifold and derived Collision support bits. Collider identity is unchanged. No event append, Grid mutation, body-counter write, structural mutation or runtime lookup is allowed in the geometry module. Inputs are the stable collider state and explicit normalized gravity direction. Preserve current operation order and contact allocation/reuse behavior; do not return a newly allocated manifold or introduce an owning geometry object. Shape/SAT/clipping helpers remain private to the implementation. Worker completion precedes shared reduction and every reader of updated contact state.

The const pair reference reflects the stable key in Grid's unordered set; intentionally mutable non-key state does not permit changing hash/equality fields while stored. This feature does not redesign that storage.

## Narrow-phase queue interface

Grid's queue index is the sole mutable membership fact. A read-only queued query derives from it; remove the boolean setter. Enqueue, duplicate rejection and cancellation remain O(1), with moved-entry repair after swap removal. Only the worker owning an entry consumes its index during the fixed batch. Do not enqueue, cancel, erase pairs or mutate queue storage until the worker batch and serial reduction finish.

No new publicly supported interface, scene version or benchmark schema change is required.

Implementation note: fallback midpoint generation moved unchanged from a private Collision method into the geometry implementation. The box-only clipping helper no longer contains circle branches with no caller. Exact circle routes and all reachable operations remain unchanged; comparison is recorded in evidence/geometry-comparison.json. The sleep reset’s single private-header definition preserves the same stores/phase while avoiding a Release cross-file call.

## Solver-order amendment — 2026-10-26

The maintainer explicitly waived deterministic physics solving. Solver iteration now follows append/swap-remove membership order with no sort, dirty-order flag or reindex pass. Correct membership, support and lifetime boundaries remain required. Earlier requirements for matching solved physical trajectories across workers or the old sorted runtime are superseded for this change. Event batches still use Exit-before-Enter, canonical pair order and endpoint A/B order; equivalence applies when the transition sets are equivalent, not as a promise that differing physical trajectories create identical future contacts.
