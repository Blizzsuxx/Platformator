# Contact Events and Startup Contract

## Public callbacks

The retained callback signatures are:

```cpp
virtual void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
virtual void onCollisionExit(Collider *other, double timeDelta);
```

Remove `Behavior::onCollisionStay`, `ScriptComponent::dispatchCollisionStay`, `Collider::triggerCollisionStay`, their definitions and internal Stay event kind. No deprecated shims or zero-work replacement callback remains. Removing a virtual callback changes the library's source and binary contract: rebuild the engine and external consumers together, remove Stay overrides/calls, and update generated or example behavior code if affected. Scene files and behavior-field serialization are unchanged.

Enter carries the current confirmed contact; Exit supplies the other collider without a contact payload. Payloads are borrowed for callback use under existing lifetime rules. Ordinary events retain the simulation timestep; lifecycle-removal callbacks dispatched during reconciliation retain the existing zero timestep.

## Pair events versus callback delivery

1. Absent→confirmed contact creates one Enter per pair. Confirmed→absent, including removal, creates one Exit. Unchanged contact creates no event.
2. Generation does not inspect script presence, behavior count, startup state, activation or recipient eligibility. Shape/contact eligibility remains a physical concern.
3. Delivery checks each endpoint independently under existing ScriptComponent/Collider eligibility rules. A pair with two eligible endpoints has two endpoint notifications for one event; an endpoint may contain multiple runnable behaviors as today. A missing or ineligible endpoint does not suppress the other.
4. Script changes alone create no transition. Adding a script during an existing contact never replays Enter; if still eligible, that script receives the actual future Exit. No per-script balanced-history guarantee is added.
5. Bounding-region candidates without a confirmed shape contact create no contact events. Multiple spatial witnesses describe one pair/contact lifetime.
6. Removal records an actual ending even when both endpoints are retired and neither receives a callback. Old-scene notifications never reach replacement-scene scripts.

## Ordering and mutation

- Preserve Exit-before-Enter ordering within each dispatch batch. Within a family, order by canonical endpoint A handle then B handle, using complete stable handles. Deliver A before B; keep behavior insertion order within each recipient.
- Main physics and cleanup are separate batches. Finish a contact's queued Enter delivery before clearing its storage; finish cleanup notifications before freeing endpoints. Do not sort or retain unchanged contacts solely for event delivery.
- Maintain current deferred mutation and scene replacement boundaries. Callback-driven script/collider/object removal is safe, suppresses later ineligible delivery, and cannot invalidate the active event batch.
- Catch failures independently per endpoint, continue the batch under existing semantics, clear consumed records, and then rethrow the first failure. Preserve GameManager cleanup on errors.
- Do not collapse Exit and subsequent Enter for a re-contact into one notification or replay Enter after script activation.

## Startup responsibility contract

`GameManager::startPendingScripts` schedules scripts; private `ScriptComponent::startPendingBehaviors` initializes the component's own behaviors. This is a responsibility extraction, with these existing behaviors preserved:

- Resolve references immediately before each start attempt, once per attempted initialization.
- Check current dispatch eligibility before every attempt; keep unattempted suffixes for later activation.
- Allow additions to the current script, later queued scripts and already-processed scripts during startup according to the existing dynamic queue pass: an already-processed script can be requeued and started again in that same pass. Additions during ordinary callbacks begin at the next startup phase.
- Log individual initialization failures and continue; attempted behaviors become runnable even on failure, matching current tests.
- Retain index-based queue traversal and O(1) tombstone cancellation during mutation. No iterator may survive a callback that can append/cancel.
- Startup performs no contact observation/subscription refresh. A script becoming runnable cannot manufacture a physical transition.

## Required acceptance coverage

The implementation must demonstrate no-script generation separately from callback traces; zero/one/two recipient equivalence; late attachment/activation and replacement; 100 quiet updates; re-contact; shape false positives; multi-cell witnesses; filtering/deactivation/removal; both-endpoint retirement; callback exceptions/mutation; scene replacement and shutdown; unchanged support/sleep/wake behavior; and deterministic serial/multiworker ordered histories. Retain existing detailed startup mutation tests while extracting ownership. A private/internal test fixture may inspect pending records; no public inspection API is required.

The absence of Stay is a deliberate deviation from Unity's collision-event family, which includes Stay; see [Unity documentation](https://docs.unity.com/en-us/engine/6000.7/manual/physics-section/physics-overview/collision-section/collider-interactions/oncollision). Enter/Exit naming remains familiar; no claim is made that Platformator's eligibility, payload or lifecycle rules match Unity's.
