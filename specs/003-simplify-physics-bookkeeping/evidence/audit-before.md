# Baseline audit journeys

Before source is retained under bin/physics-bookkeeping-20261025/baseline-source; links below name the original functions also present in the working source. No maintainer-comprehension outcome is claimed.

The actual step is start → hook → fixedUpdate → transform sync → collision detection/reduction → forces → solve/sleep → movement → transform sync → collision delivery → updates/animation → cleanup commit. Do not reorganize these phases.

| Journey | Cause → owner/pending work → consumer → cleanup | Hidden coordination to review |
| --- | --- | --- |
| J1 Motion | Rigidbody::setVelocity/setAngularVelocity → bodyStateChanged/PhysicsManager::queueBodyChange → drainBodyChanges/reconcileBody → active movement and GameManager transform sync | Kinematic motion requires membership refresh; awake dynamic impulses do not. |
| J2 Type | Rigidbody::setBodyType → body queue plus Collider::invalidateResponse → Grid::flushPendingCellUpdates/refreshResponse → support and solver membership → net flush | Body participation and response are separate derived consequences. |
| J3 Sleep/wake | setIsSleeping/wakeUp → body queue → reconcileBody plus applyPhysics island reset → preStepCollision merges roots → updateSleepingStates aggregates quiet/min timer and drains changes | Island scratch is reset at two source sites; active/sleep candidate membership is not interchangeable. |
| J4 Supports | geometry or response bits → reducePairSupport old body+bool per endpoint → queueSupportDelta → flushSupportDeltas commits net totals and wakes on positive→zero | Four independently synchronized pair fields and a different deferred per-body total. |
| J5 Replacement | GameObject remove/add body → retireComponent/queueReconciliation and invalidateResponse → old participation cancellation/new registration → response ledger transfers old credit → flush before drainRetirements | Old body must remain alive although current component lookup already returns its replacement. |
| J6 Contact | joined cell witnesses → Grid::queuePairForNarrowPhase flag+index → distinct workers mutate Collision → serial Enter/Exit/support/lists → delivery → final-witness cancellation before erase | Three sites dual-write queue fields; destructors are notification-free. |
| J7 Filter/activity | Collider dirty filter or GameObject::setActive → Grid ops/runtime reconciliation → final witness/contact response/support/membership → ordinary or cleanup Exit | One live predicate is duplicated across contact and solver-neighbor refresh. |
| J8 Callback/scene | handlePendingPhysicsEvents aggregates delivery errors → commitMutations/reconcilePending → physics flush and cleanup delivery → drainRetirements → replacement scene publication | All required callbacks/old support recipients precede batch freeing; old events cannot reach new scene. |

Primary source: [Rigidbody](../../../src/rigidbody.cpp), [body participation](../../../src/physicsbodyindexes.cpp), [contacts](../../../src/physicscontacts.cpp), [pair](../../../src/colliderpair.h), [Grid](../../../src/grid.cpp), [phase/lifecycle manager](../../../src/gamemanager.cpp). Inventory maps every retained queue/index/cache family to its consumer.
