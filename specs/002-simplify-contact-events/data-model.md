# Data Model: Simplify Contact Events

This is an in-memory runtime design; there is no scene-format migration. [Contact contracts](contracts/contact-events.md) define public behavior; [research.md](research.md) explains retained and removed state.

## ColliderPair and confirmed contact

**Owner**: Grid candidate storage. The pair owns its optional `Collision`; stable ordered collider handles identify its endpoints.

| State | Meaning and invariant |
| --- | --- |
| Endpoint pointers | Canonical A/B handle order; allocations remain valid through pending notification delivery. |
| Witness count and adjacency indexes | Candidate membership across spatial cells; losing one of several witnesses cannot erase the pair. |
| Optional `Collision*` | Sole source of confirmed-contact presence; a bounding-region overlap alone is insufficient. |
| Cached collider versions | Determine whether pair geometry needs re-evaluation. |
| Narrow-phase queue membership | At most one pending entry per pair; workers own distinct pairs. |
| `resultHadContact` | Previous contact presence captured for the current evaluation, consumed after workers finish. |
| `resultTestedGeometry` | Existing test-work result, reduced on the main thread. |
| Active-contact and solver indexes | Membership in two distinct physical collections; absent membership uses the existing sentinel. |
| Support body pointers and contribution flags | Previous per-endpoint support contributions; changes reduce once into body totals. |

Remove `stayIndex` and `enteredStep`. Remove event-emitting pair methods (`queueCollisionExit`, event-emitting `clearCollision`) and runtime-service lookup from the pair. Pair-local creation/clearing may update contact memory and cached versions but never enqueue events. The destructor releases any remaining collision without notifying the runtime.

### Transition table

| Previous confirmed contact | Current confirmed contact | Result |
| --- | --- | --- |
| Absent | Absent | No event; no contact membership. |
| Absent | Present | Queue one Enter; add physical memberships and support as applicable. |
| Present | Present | No event; refresh geometry/response/support only when affected. |
| Present | Absent | Queue one Exit; remove contact/solver membership and prior support. |
| Present | Pair removed | Queue one Exit before erasure; remove memberships/support; destroy contact storage. |
| Absent | Pair removed | No event; remove candidate storage only. |

Geometry transitions are committed in the serial narrow-phase reduction; removal transitions are committed by `PhysicsManager::removePair` after witness reduction. A geometry-ending pair has no `Collision` when later removed, so removal cannot emit a duplicate Exit. No transition condition reads scripts.

## PhysicsManager physical collections

| Collection | Owner and purpose | Update cost and ordering |
| --- | --- | --- |
| `contactPairs` | All live confirmed contacts, including non-solving contacts; benchmark invariant oracle and active-contact count. | O(1) indexed insert/swap removal; consumers requiring order canonicalize their own output. |
| `solverPairs` | Confirmed contacts eligible for dynamic/kinematic response with an applicable moving body. | O(1) membership changes; sort only when membership changes, O(S log S). Resolution/debug obtain the current collision directly from each pair. |
| `dynamicCollisionScratch` | Reusable per-step subset used for iterative dynamic solving. | Rebuilt during the required solver pass, preserving canonical selection order. |
| `supportDeltaBodies` | Bodies with accumulated support-count changes. | Deduplicated indexed queue; reduced after workers, before affected-body reconciliation. |
| Body work collections | Registered, awake dynamic, moving kinematic and sleep-eligible bodies, plus changed-body queue. | Existing indexed membership and sleep-island invariants; no script dependency. |

Delete `stayPairs`, `stayOrderDirty`, `simulationStep`, and the redundant `activeCollisions` projection. Retain solver dirty state. Delete unused `NarrowPhaseLocalResults` after a whole-repository use check.

`refreshPairLists` becomes physical membership reconciliation only. `refreshSolverPairs(Collider*)` visits touching adjacency for relevant body/collider state changes and refreshes solver eligibility without a geometry query or script lookup. `refreshResponse` remains responsible for response/support changes. Script reconciliation and startup must reach none of these operations solely because a script changed.

## PhysicsEvent batch

**Owner**: PhysicsManager's existing reusable pending-event vector. No new public event bus, observer table, persistent history or script-specific contact state is introduced.

| Field | Contract |
| --- | --- |
| Type | Enter or Exit only. |
| `colliderA`, `colliderB` | Canonical endpoint pointers; full stable handles supply deterministic sorting, including generations. |
| `collision` | Borrowed confirmed contact for Enter; null for Exit. It remains valid until Enter dispatch completes. |

The manager's enqueue operation is the sole append/count boundary for both producers. Each actual transition creates one pair event even with no recipients, including removal of both endpoints. Each nonempty dispatch batch orders Exit before Enter, then canonical endpoint handles; delivery visits A before B. The phase model admits at most one event of a given kind for a given pair in a batch; enforce this through transition ownership, not a per-frame deduplication set. Ending an old contact and beginning a new one may legitimately produce Exit and Enter for the same pair.

No supported structural mutation is applied while the notification vector is being iterated. Complete ordinary physics dispatch before cleanup can clear geometry. Cleanup-generated Exits dispatch before retired component/object allocation reclamation. Finish old-scene reconciliation and event drains before publishing replacement objects. Inactive/retired recipients are skipped at delivery; borrowed `other` pointers follow the existing callback-duration contract and must not be retained as durable references.

Catch callback exceptions per endpoint, continue delivery under current rules, clear the consumed queue, then propagate the first exception. Cleanup still executes under GameManager's existing exception handling.

## Script startup ownership

GameManager owns `pendingStartScripts`, per-script queue indexes, cancellation tombstones and the startup phase. `startPendingScripts` keeps index-based traversal and one active reference resolver for the pass.

ScriptComponent owns `pendingBehaviors`, runnable `behaviors`, dispatch eligibility and private `startPendingBehaviors(platformator::ReferenceResolver&)`:

1. Recheck eligibility before each pending behavior and read the current pending size, allowing same-pass additions during startup.
2. Resolve references and attempt start; report failures per behavior without aborting the remaining eligible batch.
3. Promote exactly the attempted prefix in order, including failed attempts as the existing contract requires.
4. Leave an unattempted suffix pending after disable/retirement; queue scheduling remains the manager's responsibility.

After the component operation returns, GameManager cancels that script's start-queue membership as today. No `firstBehaviors` state or contact refresh follows. Public callback and serialization contracts do not change beyond Stay removal.

## Benchmark and test-only data

Schema 3 has the same eight timing scopes and eleven counters; see [benchmark-schema.md](contracts/benchmark-schema.md). Each enqueue increments Enter or Exit using frame-scoped recorder accumulation. Generation outside an active measured frame does not create an artificial sample. Main-phase and cleanup events both contribute when they occur within a frame. Do not count recipients, rescan pending events, or carry an out-of-frame event into a later sample.

Replace the Stay work probe with test-only counters for actual transition comparisons in the pending-pair reduction and event records appended. Quiet persistent-contact tests must show both remain zero after warmup with and without scripts. Pending geometry updates may perform a transition comparison even when contact remains present; this is changed-pair work, not a dedicated traversal of persistent contacts. Probe support is compiled out of performance builds.
