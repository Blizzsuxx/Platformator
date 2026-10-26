# Final audit journeys

Technical paths below reflect the actual candidate code. Human comprehension is still pending; these are review aids, not maintainer approval. The preserved baseline is bin/physics-bookkeeping-20261025/baseline-source, with before routes in audit-before.md.

| Journey | Trigger/owner → consumer/cleanup | Rule to audit |
| --- | --- | --- |
| J1 | [queueParticipationChange](../../../src/rigidbody.cpp#L400) → [reconcileBodyParticipation](../../../src/physicsbodyindexes.cpp#L46) | Velocity/angle changes that affect participation queue once; current state derives the active list before movement. Awake dynamic impulses do not add redundant queue work. |
| J2 | [setBodyType](../../../src/rigidbody.cpp#L254) → [refreshResponse](../../../src/physicscontacts.cpp#L129) | Type changes queue body participation and invalidate collider response; local neighbors derive support and solver eligibility without inventing contact events. |
| J3 | [resetSleepIsland](../../../src/physicsmanager.h#L85) → [updateSleepingStates](../../../src/physicsmanager.cpp#L555) | Step-local roots initialize before solver merges; quiet/minimum timer aggregates precede sleep decisions; membership is drained after fixed-list iteration. |
| J4 | [reconcilePairSupport](../../../src/physicscontacts.cpp#L52) → [flushSupportDeltas](../../../src/physicscontacts.cpp#L78) | Canonical endpoint support maps to exact contributor identity; net per-body deltas commit once, waking only on actual final loss. |
| J5 | [removeComponent](../../../src/gameobject.cpp#L292) → [drainRetirements](../../../src/gamemanager.cpp#L1024) | Response invalidation transfers old credit to the replacement; old recipients remain allocated through grid/support flush and destruction callbacks before batch freeing. |
| J6 | [queuePairForNarrowPhase](../../../src/grid.cpp#L129) → [narrowPhase](../../../src/physicsmanager.cpp#L178) | One queue index controls dedup/cancellation; distinct workers consume it and update only pair geometry, then serial transitions/support/participation are reduced. Event dispatch remains ordered; physical solving follows membership order. |
| J7 | [isLiveContact](../../../src/physicscontacts.cpp#L32) → [flushPendingCellUpdates](../../../src/grid.cpp#L429) | Dirty filter/activation/response work changes only affected spatial coverage and physical participation; one live predicate is used, with final-witness removal preserving exact Exit. |
| J8 | [handlePendingPhysicsEvents](../../../src/physicsmanager.cpp#L598) → [commitMutations](../../../src/gamemanager.cpp#L1065) | Exit-before-Enter and endpoint order remain; failures are aggregated, required cleanup flushes pending support/events before retirement/free and replacement scene publication. |

## Objective state/coordination comparison

J4/J5: four support fields became two nullable contributor identities. The old reconcile compared and updated body+flag, while removal separately checked flags and read old bodies. The new reconcileSupportContribution compares one identity and handles both updates and release. Null means no scheduled/applied credit. No independently maintained unsupported current-body cache or support flag remains. Committed count, pending signed delta and touched-body index remain because their validity boundaries differ.

J6: flag+index became one queue index. Enqueue, cancellation and worker consumption each previously wrote both; now each writes only the index. O(1) duplicate rejection, swap repair and consumed-batch lifetime are unchanged. The fixed pointer batch is not an additional queued-status cache. Three owned booleans and their synchronization obligations are removed overall; no replacement facts or scans are introduced.

Live contact is one uncached predicate and response classification is evaluated once per solver eligibility update. Body list names state awake dynamic/moving kinematic/sleep candidate roles; one reset helper states the lifetime of island scratch. Geometry has no PhysicsManager state access and receives gravity explicitly. Removing unreachable circle branches and moving fallback math completes that actual boundary without a new owner class, friend or public accessor. These clarity changes supplement, rather than replace, the ledger/queue reductions.
