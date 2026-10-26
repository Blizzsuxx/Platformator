# Data Model and Ownership: Simplify Physics Bookkeeping

This records the inspected baseline and implemented design. Actual source dispositions and before/after walkthroughs are in [state-inventory.md](evidence/state-inventory.md) and [audit-after.md](evidence/audit-after.md). Source baseline: `f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba`. The implementation review records the final disposition and concrete source links for every row. Physical properties such as mass, velocity, shapes and transforms remain authoritative authoring/simulation inputs; this inventory concerns information maintained around them.

## Authority and derived state

- Body type, velocities, gravity, activation/retirement and committed sleeping state determine participation.
- A pair's optional Collision is confirmed-contact presence. Cell witness counts describe spatial coverage, not contact.
- Collision support bits describe current geometry/response eligibility. Pair contributor identities describe which exact bodies have been credited or scheduled. Committed body support count is updated only at the net-delta boundary.
- Lists and indexes accelerate consumption/removal; they do not redefine physical facts. Pending queues encode work not yet reconciled. Sleep-island fields are temporary aggregation state, rebuilt for the current step.

## Baseline inventory and implemented disposition

In the boundary column, “serial” means the existing main-thread phase after relevant worker joins. No new flags or global scans were introduced.

| Mechanism: baseline → implemented | Owner and meaning | Triggers, validity and consumers | Disposition and reason |
| --- | --- | --- | --- |
| Registered body vector, `physicsManagerIndex`, registration boolean → retained | PhysicsManager registration; Rigidbody stores its membership metadata | Add/remove during lifecycle commit; valid after registration reconciliation; removal repairs moved index before cancellation/participation reconciliation | Public registration accessors and lifetime participation have existing consumers; do not silently derive one public setter from another |
| Collider vector, physics index/registration flag → retained | PhysicsManager collider registration | Lifecycle commit → Grid operations; enumeration and destruction consumers | Independent of cell placement; preserve public behavior |
| `awakeBodies/awakeIndex` → descriptively named awake dynamic membership | PhysicsManager list, per-body removal index | Body participation reconciliation; force and movement consumers | Avoid all-static/sleeping scans; only live awake dynamic bodies |
| `kinematicBodies/kinematicIndex` → descriptively named moving kinematic membership | PhysicsManager list, per-body removal index | Kinematic velocity/angular/type/lifecycle changes; movement consumer | Only nonzero linear/angular motion participates; retain existing predicate |
| `sleepEligibleBodies/sleepIndex` → descriptively named sleep candidates | PhysicsManager list, per-body removal index | Awake dynamic and (support or gravity disabled); sleeping aggregation consumer | Physical subset, not a redundant awake list |
| `changedBodies/bodyQueueIndex` → retained, explicit participation-change naming | PhysicsManager pending bodies, index deduplicates and cancels | Rigidbody setters, GameManager lifecycle, support and sleep; drained at existing boundaries | Coalesces repeated changes, O(1) cancellation; not the lifecycle reconciliation queue |
| `sleepParent/sleepRank` → retained | Per-body union-find scratch for connected awake bodies | Reset by current integration/reconciliation paths; merge in collision pre-step; root lookup in sleep update | Keep group sleep behavior and path compression; name/reset operation explicitly |
| `islandQuiet/islandTimer` → retained | Per-root quiet conjunction and minimum timer | Initialized with body timer; aggregate in sleep update, used before sleep decisions | Different meaning from each body's timer; no new persistent island registry |
| `supportContactCount` → retained | Body's committed support total | Net flush and existing public support methods; `hasSupportContact` and sleep eligibility consume | Last committed value remains public contract |
| `supportDelta/supportDeltaIndex/supportDeltaBodies` → retained | PhysicsManager net pending support change per touched body | Pair reconciliation queues; serial flush commits/wakes and queues body participation | Queue membership cannot derive from delta: cancelling changes may leave a queued zero delta |
| `supportBodyA/supportsA` → nullable `contributedSupportBodyA` | Pair's contribution identity for canonical endpoint A | Serial support reconciliation after geometry/response; final-witness removal clears; body flush consumes deltas | Removes separate flag and unsupported current-body snapshot |
| `supportBodyB/supportsB` → nullable `contributedSupportBodyB` | Same for canonical endpoint B | Same rules; A/B independent even if contributors happen to be equal | Preserve endpoint identity without assuming only one can contribute |
| `Collision::supportState` → retained | Geometry/response-derived reference/incident support bits | Geometry and response refresh; serial pair ledger reads | Not interchangeable with old applied/scheduled contributor after deletion or body replacement |
| `Collision*` → retained | Pair-owned optional contact including manifold/warm-start data | Distinct-pair geometry mutates/reuses/deletes; serial reduction and solver consume; destructor frees | Sole confirmed-contact presence; no destructor event side effect |
| Pair `objectA/objectB` → retained immutable canonical identity while stored | Grid set key | Construction orders handles; hashing, adjacency, canonical ordering and events consume | Non-key fields may mutate through const pair pointers; do not mutate set identity |
| `objectAStateVersion/objectBStateVersion` → retained | Pair's last geometry-evaluated collider versions | Contact create/clear updates stamps; `shouldUpdate` consumes | Avoid redundant geometry; no body eligibility decision inferred solely from these stamps |
| `isQueuedForNarrowPhase + narrowPhaseQueueIndex` → index only | Grid pending queue; worker consumes own entry | Enqueue/dedup/cancel before worker launch; sentinel at worker entry; batch clear after reduction | Removes independent boolean and all dual writes; query derives from index |
| `adjacencyIndexA/B` and `pairAdjacency` → retained | Grid endpoint-local pair lookup and O(1) removal | First witness adds; final witness/removal repairs swapped endpoint's correct index | Localize response and solver refresh to affected neighbors |
| `witnessCount` → retained | Grid net number of spatial observations | Joined cell deltas; 0→positive publishes pair, positive→0 removes | Losing one cell does not remove a still-covered pair |
| `pairSlot/pairSlots/freePairSlots` → retained | Grid stable pair enumeration and reusable holes | Pair insertion/removal serially; oracle/candidate enumeration consumers | Lifetime/index purpose separate from contact/solver lists; measure fixed-peak retention |
| `contactPairs/activeIndex` → retained | All live confirmed-contact membership | Serial contact reconciliation/removal; benchmark count and independent oracle | Do not remove oracle merely to simplify candidate implementation |
| `solverPairs/solverIndex` → retained | Physical response-eligible membership | Contact/body/response reconciliation; unsorted solver/debug consumers | Subset differs from all contacts; no mirrored Collision pointer cache added |
| `solverOrderDirty` → removed | Former canonical solver-order invalidation | No current writer or consumer; sorting and reindexing are removed | Maintainer explicitly waived deterministic solving on 2026-10-26; no replacement ordering state |
| `resultHadContact` → retained, explicitly transient | Per-pair worker result | Capture before geometry; serial transition reduction after join | No event vector in workers; previous presence still needed after Collision deletion |
| `resultTestedGeometry` → work-probe builds only | Probe-only per-pair evaluation result | Worker sets; serial reduction adds work counters after join | Field and writes compile out without PLATFORMATOR_ENABLE_WORK_PROBES; the disabled work macro also removes its read |
| `pendingPhysicsEvents` → retained | PhysicsManager transition batch | Serial transition/removal append; canonical delivery then clear | No unchanged-contact scan, Stay, or script subscription list |
| `dynamicCollisionScratch` → retained | Solver subset reused across iterations | Pre-step fills; iterative solve consumes | Distinct purpose; preserve capacity reuse |
| `movedObjects` → retained | oneTBB thread-local movement results | Independent integration appends locally; serial transform/collider synchronization drains | Worker ownership and main-thread structural boundary retained |
| Collider dirty flags, state versions, pending-operation index, cached cell ranges → retained | Collider/Grid synchronization | Transform, filter, shape, response, activation and registration changes; current Grid flush consumes | Trace as external boundary to this review; no duplicate dirty-reason system added |
| Grid cell operation/empty queues, pair delta collector/scratch → retained | Grid cell workers and serial consolidation | Independent cell work produces; joins precede reduction and erasure | Spatial algorithms unchanged; targeted multi-cell/migration regressions required |
| GameManager lifecycle queues, eligibility and retirement state → retained | Runtime lifecycle owner | Public structural changes → reconciliation → physics flush → notifications → free | Different contract from body participation and support queues; do not merge them |

## Derived participation rules

One body reconciliation operation derives the three existing memberships from current authoritative state. It does not integrate the body or regenerate geometry.

| Membership | Established rule | Consumer |
| --- | --- | --- |
| Awake dynamic | Registered, not retired, owner active, DYNAMIC, not sleeping | Forces, movement, island reset |
| Moving kinematic | Registered, not retired, owner active, KINEMATIC, nonzero velocity or angular velocity | Movement |
| Sleep candidate | Awake dynamic and (positive committed support or gravity disabled) | Sleep aggregation |
| Live contact | Collision exists; neither collider retired; both owners active | All-contact and solver derivation |
| Solver contact | Live contact; resolution is DYNAMIC or KINEMATIC; at least one nonstatic, nonsleeping body under the existing predicate | Physical response in membership order |

Do not add a new cached eligibility field. `reconcileBodyParticipation` is the single body derivation point; a shared live-contact helper and solver derivation replace the current duplicate predicates. Public setter and runtime lifecycle triggers remain distinct callers with explicit reasons.

## Support state transitions

For endpoint X, desired contributor is its current Rigidbody if the current contact supports X, otherwise null. Map the support bit through the current reference/incident orientation, not the canonical handle order.

| Old contributor | Desired contributor | Queued changes | New ledger value |
| --- | --- | --- | --- |
| null | null | None | null |
| A | A | None | A |
| null | B | B +1 | B |
| A | null | A -1 | null |
| A | B, A ≠ B | A -1, B +1 | B |

The pair ledger changes when the deltas are queued. Body totals change later at the same existing flush. Net zero support change must not transiently wake a body. Old recipients survive through that flush even if their component was replaced. Remove a pair by reconciling both contributions to null, removing contact/solver membership, and only then erasing it. A deleted Collision must not be needed to undo the old ledger.

## Narrow-phase queue transitions

1. **Absent:** index sentinel. First enqueue stores the queue position; duplicate enqueue does nothing.
2. **Queued:** before launch, index identifies a slot containing that pair. Cancellation swap-removes and repairs the moved pair's index.
3. **Consumed in a fixed batch:** the worker clears its own index at entry. The batch vector still holds the pointer while the worker and serial reduction use it. Structural enqueue/cancel/erase cannot overlap this period.
4. **Drained:** after join and reduction, the batch vector is cleared. A later change can enqueue the surviving pair again.

These are explanatory stages, not a new enum/flag. Preserve current exception/lifecycle boundaries; do not claim new transactional recovery from allocation failure.

## Trigger and visibility table

| Trigger | Pending work | Required boundary before consumer |
| --- | --- | --- |
| Kinematic linear/angular velocity changes | Body participation | Before movement membership is used |
| Dynamic wake/sleep, body type or gravity change | Body participation; body type also invalidates collider response | Existing integration/support/sleep and response flush boundaries |
| Registration or lifecycle activity/retirement | GameManager reconciliation and affected body work | Registration update/cancellation before physical work or free |
| Geometry changes | Affected pair evaluation | After collider/cell flush; workers joined before shared reduction |
| Trigger/body response changes | Affected adjacency response and membership | Current response flush without synthetic contact events |
| Support zero/nonzero transition | Net support commit then affected body participation | Before sleep/solver membership consumers at the established boundary |
| Final witness or collider removal | Queue cancellation, contribution release, memberships, Exit | Before pair erase; endpoint allocations retained through delivery |
| Scene replacement/callback retirement | Lifecycle reconciliation and physics cleanup | Old-scene flush/dispatch before retirement free and replacement publication |

## Eight review journeys and implemented simplification

| Journey | Code-backed route to record before and after | Candidate effect |
| --- | --- | --- |
| J1 Motion start/stop | Rigidbody setters → body change queue → body reconciliation → active movement → transform sync | Name and document one derivation; preserve queue coalescing |
| J2 Body type | setBodyType → participation + response invalidation → neighbor refresh → solver/sleep consumers | Shared live/solver rules; no extra dirty flags |
| J3 Sleep/wake | body state → participation → island initialization/merge → aggregation → sleep state | Explicit island scratch lifetime and common reset operation |
| J4 One/final support loss | geometry/response → pair contribution identities → net body delta → wake/participation | Four ledger fields → two; one identity comparison per endpoint |
| J5 Component replacement/removal | retirement/invalidateResponse → registration/cancel → old contribution removal/new credit → flush → free | No independently synchronized support flags; old recipient explicit |
| J6 Contact lifetime | Grid witness → queue index → pair geometry → serial transition/support/membership → delivery | Queue flag removed; geometry boundary has explicit input/side effect |
| J7 Filter/activation | dirty collider/runtime reconciliation → Grid pair/response refresh → support/membership/transition boundaries | One live-contact predicate; no script-driven physical refresh |
| J8 Callback/scene | dispatch/exception capture → commit/reconcile → physics flush/Exit → retirement → new scene | Preserve ordering; add precise links, not a new lifecycle abstraction |

Implementation evidence counts independent state facts and maintenance edges on J4/J5 and J6 before/after. Deleted bools are demonstrable reductions; compiler padding may hide byte savings. Supporting naming/extraction work does not satisfy that reduction by itself. SC-001 remains pending until the maintainer walks these routes in the final code.

The final resetSleepIsland definition is in PhysicsManager’s private header, allowing Release inlining without a second definition. Geometry fallback midpoint logic is implementation-local in physicsgeometry.cpp; only unreachable circle branches in box-only clipping were removed. See [geometry comparison](evidence/geometry-comparison.json), [reset investigation](evidence/reset-helper-investigation.md) and [validation](validation.md). Human readability remains pending actual maintainer review.

Current solver-order disposition (2026-10-26): `orderSolverPairs` and its comparator/sort/reindex helper/call plus `solverOrderDirty` are removed. Membership only appends or swap-removes, repairing the moved entry locally. The membership helper no longer returns an unused change flag. This removes one additional owned boolean in PhysicsManager; it does not imply a further measured object-layout saving. Physics solving is not required to reproduce across worker counts. Event ordering and all support/queue/lifetime mechanisms retain their existing contracts.
