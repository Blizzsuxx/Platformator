# Runtime and Sleep/Wake Compatibility Contract

This is a native-library contract, not a new public interface. Preserve the existing [Runtime](../../../include/platformator/runtime.h), [Rigidbody](../../../include/platformator/rigidbody.h), authoring, and reference interfaces. Exact current code behavior is characterized before refactoring; no scene migration or deliberate gameplay change is authorized.

## J1: Frame entry points

- Explicit simulation accepts the current finite positive timestep contract and preserves caller-supplied dt. Reentrant/wrong-thread stepping rejects at the existing boundary.
- Starts precede the caller hook; the hook precedes fixed updates, physics, collision dispatch, updates, animation, and late updates. Final cleanup follows, including on callback failure according to existing error precedence.
- No-callback and callback rendered entry points share event handling, quit checks, simulation/pause choice, debug clearing/advance handling, and render phase behavior. An empty callback has no user-visible effect and must not add warmed per-step allocation.
- Rendering remains outside complete-step simulation timing. A quit that skips simulation/rendering creates neither a fake step nor a fake render sample. Do not eagerly validate dt on a paused/skipped path if the current path does not do so.
- Interactive accumulation, catch-up/drop accounting, pause, manual advance, and unpause behavior stay as implemented by SimulationClock. Explicit stepping does not use that accumulator.

## J2: Mutation, callbacks, and identity

- Preserve main-thread and forbidden-phase mutation checks. Logical retirement immediately invalidates handles/eligibility as currently specified; system membership and memory release follow their established commit boundaries.
- Components queued several times reconcile their final valid state without duplicate membership. A component created/removed before start does not receive a late start or update.
- Start/destruction delivery, callback recipient eligibility, and first-error/remaining-cleanup behavior remain unchanged. Destroy callbacks can read still-valid attached data but cannot structurally mutate when the existing restriction forbids it.
- Support recipients and callback/event references survive their required drain. No freeing occurs concurrently with workers or before a batch's required callbacks finish.
- `Runtime::getAllGameObjects()` retains its const collection contract and validity limits; internal callers receive no new mutation bypass.

## J3: Transforms

- Preserve immediate public world-position reads and world-preserving reparent behavior.
- Synchronize parents before affected descendants; overlapping dirty ancestors/children do not duplicate publication. Retirement cancels pending transform references.
- Preserve collider refresh and wake conditions for explicit local edits, ancestor motion, and integrated movement. Keep local worker writes separate from main-thread publication.

## J4–J5: Sleeping, waking, and support

- Preserve existing eligibility, motion thresholds, quiet delay, group connection/merge rules, and the precise boundary at which membership reflects a change.
- Unchanged `setIsSleeping` returns without resetting accrued time or changing motion. Real transitions reset the timer. Sleep entry clears velocity, force, angular velocity, and torque; waking does not fabricate motion.
- Group evaluation consumes all candidate motion before applying decisions. One moving member prevents group quiet accumulation; minimum prior member time controls the shared timer. Keep candidate initialization for bodies admitted after integration.
- Preserve explicit/setter-triggered waking and impact-wake predicates, including tiny moving-kinematic handling. Do not conflate solver impact thresholds, sleep thresholds, and explicit setter wake thresholds.
- Final support loss wakes at the committed net-delta boundary. Removing one of several supports or a net-zero same-batch transfer preserves current semantics. Component replacement transfers credit to the correct recipient before old storage is released.
- Preserve contact completeness and pair-level Enter/Exit generation independent of scripts. No recurring Stay or replay behavior is introduced. Required event batch ordering remains; canonical solver ordering is not restored. Cross-worker unconstrained trajectory identity is not a contract.

## J6: Runtime and scene lifetime

- Runtime remains the single public lifetime owner; a second simultaneous runtime and access without a current runtime reject as before. Privileged manager access checks owner availability at its boundary and throws the existing inactive-runtime `logic_error` if unavailable. Initialization failure must not publish a partial runtime.
- The last pending scene request wins; superseded paths are not opened. Failed preparation preserves the old scene. Loading/clearing and request-only calls retain their distinct immediate/deferred semantics.
- Current scene IDs, handle generations, default save path, publication, and next-step script startup retain existing behavior. Queued callbacks cannot observe half-published replacement scenes.
- Explicit shutdown runs while Runtime and its manager are accessible to permitted callbacks/resource release. Manager reset follows completed shutdown; clearing active Runtime follows reset. Repeated shutdown remains safe.
- A recreated runtime cannot revive old bound references. Asset cache ownership/release and physics/window teardown ordering remain unchanged.

## Acceptance mapping

| Journey | Existing validation hosts | Targeted additions |
| --- | --- | --- |
| J1 | runtime_step, benchmark_contract | Both rendered paths with hook/pause/quit and relevant exception cleanup |
| J2 | runtime_foundation, callback_mutation, destruction_contract, queued_deletion_safety | Only missing boundary assertions discovered during cleanup |
| J3 | transform_hierarchy, runtime_work | Overlapping dirty subtree/retirement guard if absent |
| J4 | regression, physics_bookkeeping | No-op wake timer and unequal-timer connected group |
| J5 | physics_support, physics_bookkeeping, contact_events | Controlled impact wake followed by resumed participation |
| J6 | scene_transition_contract/stress, runtime_recreation, object_reference_contract | RuntimeAccess during shutdown and no-runtime/recreation access |

Names in this table abbreviate `platformator_*_test` registrations; see [quickstart.md](../quickstart.md) for executable commands. Existing public-header and package-consumer checks cover source compatibility. Human audit acceptance stays separate from automated behavior checks.
