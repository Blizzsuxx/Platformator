# Contact Events and Behavior Startup Contract

**Applies to**: Platformator runtime/library consumers after implementation of this feature.
**Compatibility**: Intentional source and ABI break for Stay callbacks; rebuild the library and all consumers.

## Public callback surface

Retain the existing Behavior callbacks:

~~~cpp
virtual void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
virtual void onCollisionExit(Collider *other, double timeDelta);
~~~

Remove `Behavior::onCollisionStay`, `Collider::triggerCollisionStay`, and `ScriptComponent::dispatchCollisionStay`, including their declarations, definitions, and internal delivery paths. There is no deprecated no-op overload, separate trigger-Stay path, or substitute recurring notification.

Enter receives the existing Collision and other-collider data; exit receives the existing other-collider data. Existing argument meanings and ordinary-step/retirement timing remain unchanged. In particular, reconciliation exits outside the ordinary collision phase retain the existing zero timeDelta.

## Generated events

- A collider pair generates one enter when confirmed contact begins and one exit when that contact ends.
- An unchanged confirmed contact generates no event. Candidate bounding-region overlap alone is insufficient.
- Generation is identical with no scripts, one scripted endpoint, two scripted endpoints, empty script components, or scripts pending initialization.
- Body response and trigger changes that preserve confirmed contact do not generate transitions. Their physical effects still update.
- Duplicate cell witnesses and multiple behaviors cannot multiply a pair transition.
- Filtering, collider deactivation/removal, and object retirement generate the terminal exit once. Both-retired endpoints still have a generated transition but no eligible recipient.
- Final-state changes coalesced before an existing reconciliation boundary do not manufacture exit/enter when the confirmed contact remains uninterrupted. Distinct completed transitions are not merged.

## Delivery and ordering

Recipient eligibility is evaluated separately at delivery under the existing initialization, active-state, component-slot, and retirement rules. An event can deliver zero, one, or multiple behavior callbacks. Each eligible behavior on each endpoint receives one corresponding callback.

Within each dispatch batch, exits precede enters; each group is ordered by canonical pair handles. Endpoint A is visited before B. Ordering and event counts match for equivalent serial and parallel inputs. Existing per-callback exception handling continues attempting eligible delivery, clears the batch, and rethrows the first captured exception.

Adding a script during an existing contact produces no enter or replay. That script can receive a later exit despite not receiving an enter. Removing/replacing a script does not end the contact or suppress the other object's callback. A replacement script follows the same initialization and eligibility rules as any newly attached script.

## Callback lifetime and mutation

The Collision pointer is borrowed for the enter callback; the other collider is borrowed for each callback. Consumers must not retain either raw pointer as a lifetime guarantee. Existing typed object-reference facilities remain available for references spanning lifecycle changes.

Callbacks may request structural changes or scene replacement under the existing deferred mutation policy. Logical retirement/replacement suppresses later delivery immediately. An eligible survivor may inspect the other collider during a retirement exit, before physical allocation release. Both retired endpoints receive no later ordinary callbacks. Pending events are drained or cleared before their storage can be reclaimed; replacement scenes receive no stale callbacks.

## Startup semantics preserved

The internal method name becomes `GameManager::startPendingBehaviors`; this is not a new public lifecycle entry point.

- Reference resolution immediately precedes the first start attempt.
- Resolve/start exceptions are logged per behavior. Resolve failure skips that start call.
- Every attempted entry, including failure, becomes runnable under the existing one-attempt policy; there is no automatic retry.
- Pending additions during startup may be initialized during that startup pass.
- An interrupted pending suffix remains pending until an eligible startup pass.
- Additions during ordinary fixed/update/late/collision dispatch wait for the next startup pass.
- Startup and script reconciliation do not change physics observer or solver membership.

## Migration guidance to publish

Remove Stay overrides and their internal calls. For ongoing interaction, a behavior can maintain its own interaction state from enter/exit and use ordinary updates for recurring work, clearing that state on its normal lifecycle teardown. Because late attachment has no replay, an exit must tolerate an untracked contact; authors needing knowledge of pre-existing contacts must establish that through their own game state. This feature does not add a public contact-query facility.

Existing examples already use enter callbacks; rebuild them as public consumers. Update current API guidance, test callback-family matrices, and explanatory walkthroughs. Preserve historical specifications and recorded results as historical evidence.

## Acceptance references

The [specification](../spec.md) defines the user scenarios. The [data model](../data-model.md) defines transition ownership and fields; the [validation guide](../quickstart.md) covers generated events separately from callback delivery.
