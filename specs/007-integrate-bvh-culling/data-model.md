# Data Model: Selectable Spatial Culling

This is a design contract for future implementation. Names below describe private types unless explicitly identified as SDK API. See [research](research.md) for source evidence and alternatives.

## Selection and ownership

`RuntimeOptions` gains one trailing `SpatialSettings` value, copied at runtime construction and immutable thereafter. It contains independent physics/render modes and the BVH maintenance, padding and query-execution controls defined in [runtime-selection.md](contracts/runtime-selection.md). A runtime owns one selected collision context; an SDLWindow owns at most one selected visibility context. No context shares proxies, pending operations, pair witnesses or tree nodes with the other domain. `None` rendering owns no visibility index.

| Entity | Fields / relationships | Invariant |
| --- | --- | --- |
| `SpatialBounds` | Finite min/max X/Y, inclusive overlap predicate | Min <= max on both axes; exact current bounds are distinct from optional padded tree envelopes |
| `SpatialIdentity` | Full component ObjectHandle, including runtime context, slot and generation | A replacement component is a different endpoint even if its GameObject survives |
| `SpatialProxy` | Identity, owner kind, stable owner reference, exact bounds, selected-backend membership, dirty bits, operation-queue index | One live record per registered endpoint in its context; pending references cannot outlive retirement |
| `SpatialPairOps` | Context-owned bounds, eligibility and overlap-transition operations | Dispatch occurs at candidate changes, not every endpoint comparison; owner kind is checked before casting |
| `BvhProxyRecord` | Stable proxy reference, reusable Box2D node ID, cached envelope, category bits | Node ID is never logical identity; user data refers to a stable record until all readers finish |
| `SpatialDirtyOperation` | Endpoint identity, final desired membership, accumulated Geometry/Filter/Response flags | Coalesces repeated writes; removal cancels stale updates, recreation uses a new generation |
| `SpatialBatchStats` | Changed endpoints, actual mutations, queries, discoveries, reductions, maintenance and capacities | Records actual selected work; inactive backends contribute no hidden work |

Pools may retain reusable high-water capacity; they must not retain an unbounded history of retired identities. Preparing a batch validates all potentially unsupported physics tree bounds before mutation. Invalid input terminates that BVH run explicitly. Callback failures must not unwind across the C ABI: callback trampolines capture an exception/failure, stop the query and report it after returning to C++.

## Shared segmented core

The existing projection, endpoint proxy, chunk, checkpoint and per-cell AABB structures remain the one segmented implementation. Preserve installed collider-facing type names and operations. A master projection stores an opaque owner and compact kind tag in place of its collider-only owner representation; the collider accessor returns the actual Collider for physics owners and null for internal visibility owners. Endpoint indirection, chunk links and indexes retain their current meaning.

Each segmented context owns its own cell map, buffered membership operations and thread-local overlap deltas. Bindings connect a spatial owner to that cell's two axes and four endpoints. Endpoint ties retain min-before-max ordering for inclusive touching. Checkpoints describe intervals crossing chunk boundaries; fast-motion cancellation, split/merge repair and migration remain mandatory invariants. Neither the generic owner adaptation nor visibility adds a heap wrapper per endpoint.

The initial visibility partition uses the existing 200-unit cell size and 32-endpoint chunk capacity, recorded from the effective constants rather than assumed in reports. Only camera/sprite pairs pass visibility eligibility. Multi-cell discoveries reduce to one camera/sprite relationship; sprite/sprite relationships never enter physics. Grid-specific collider ranges remain invalid for BVH membership; neutral registration reports the selected backend truthfully.

## Physics pair state

`PhysicsManager` owns a `PhysicsPairState` independent of its selected spatial backend. It contains the current stable candidate records, collider adjacency, pending narrow-phase records and queue indexes. Existing downstream contact/support state, solver and event paths consume this same owner for both choices.

| Record | Fields | Rules |
| --- | --- | --- |
| `ColliderPairKey` | Canonically ordered full collider identities | One key per pair; both mask directions and current exact overlap decide eligibility |
| Candidate record | Existing ColliderPair data, positive spatial support count, pending index | Segmented support counts cell witnesses; deduplicated BVH membership contributes 0 or 1 |
| Endpoint adjacency | Incident candidate keys / stable records | Updated together with pair insertion/removal; dirty endpoints can reconcile old pairs without scanning all pairs |
| Narrow-phase queue | Stable candidate references, coalesced membership | Geometry/filter changes queue affected retained pairs; queue repair precedes record reclamation |

Transitions occur after net delta reduction: support 0 -> positive inserts one candidate; positive -> positive retains it; positive -> 0 runs existing final contact/support removal, removes queued references and adjacency, then reclaims it. A response-only change uses existing response/support refresh without manufacturing a spatial Enter/Exit. Sleeping does not invalidate a collider's spatial membership. Camera visibility never gates these transitions.

For BVH dirty endpoints, union discoveries with previous adjacency, sort/deduplicate canonical keys, then evaluate exact current bounds and symmetric 64-bit mask eligibility. This also removes stale relationships and handles filter-only changes. With padding, contained geometry updates still query/reconcile and dirty existing narrow-phase pairs even if the tree envelope stays unchanged. The Box2D one-direction category test is only an early rejection.

## Visibility state

`RenderCulling` is a private SDLWindow-owned coordinator with the selected linear, BVH or segmented representation, stable sprite records, pending dirty IDs, an uncullable collection and reusable hit/order buffers. Linear uses the same cached prepared bounds as the indexes. None retains today's direct sprite-vector submission path.

| Record | Fields / relationships | Invariant |
| --- | --- | --- |
| `RenderProxy` | Sprite identity/reference, cached world center, dimensions/angle revision, conservative bounds, current windowIndex, backend membership | Bounds follow the actual rendered quad, independently of collider presence |
| `PreparedView` | Camera identity/position/dimensions, output size, sx/sy, query rectangle/guard, SDL state signature, support status/reason | Validated for every rendered frame; translation/guard changes need not rebuild all sprite geometry |
| Visibility relationship | Camera identity + sprite identity | Unique, camera/sprite only; overlap is conservative and distinct from a draw |
| Ordered result | Unique valid sprite references sorted by current windowIndex | Same retained order as the current sprite vector, including swap-with-last removal |
| Result cache | Geometry/index revision, prepared-view revision, draw-order revision, ordered result | Reusable only when all three match, for every culling choice including linear |

Bounds match existing SDL destination-space rotation, including anisotropic camera/output scale. For post-object-scale dimensions w/h and angle theta, world half-extents are `(abs(cos(theta))*abs(w) + abs(sin(theta))*abs(h)*sy/sx)/2` and `(abs(sin(theta))*abs(w)*sx/sy + abs(cos(theta))*abs(h))/2`. Use the actual submitted angle and outward-rounded computation; expand the camera query by at least one output pixel per axis in world units for raster conservatism. Bound float draw-expression rounding error at the current coordinate/dimension magnitudes as well: one pixel alone is not proof at extreme coordinates. Include sprite-dependent error in cached bounds and camera-dependent error in the view guard; if finite conservative bounds cannot be established, use the explicit uncullable/bypass path. A changed scale ratio invalidates all shape extents. Preserve current local rotation/scale and parent-position semantics.

Dirty producers: published world-position changes (including parents/reparenting), object scale/rotation, sprite dimensions (including Animator), registration/removal and view aspect changes. UV source rectangle, texture and flip changes alone do not change the destination quad. They remain covered by output parity fixtures. Coalesced geometry and indexes flush immediately before rendering after normal transform publication, including paused/zero-step frames.

Unsupported view state or invalid camera uses the original submit-all path with a reason. Individual invalid/unrepresentable sprites go through the uncullable collection, merged by current draw order. Detect non-default SDL render target, viewport, scale and logical presentation; culling may resume only after supported state is restored and pending revisions reconciled. Bypass/hybrid frames are identified in metrics. Headless/no-camera paths do no visibility maintenance; resuming rendering reconciles all pending changes before querying.

## Publication and retirement

1. Main-thread mutations publish transforms/geometry and coalesce final desired membership.
2. Prepare dirty bounds and preflight the selected index. Apply exclusive BVH writes and its maintenance, or existing partitioned segmented cell work.
3. Complete writers. Publish immutable query state. Parallel BVH collider queries use oneTBB task-local outputs; a single camera query stays sequential.
4. Complete readers. Canonically reduce deltas/results and update common physics state or ordered visibility results.
5. Consume stable pairs in existing narrow phase/solver phases, or submit ordered sprites on the renderer thread. Existing callback/cleanup ordering remains authoritative.
6. Remove pending references, await all readers and reductions, then retire proxy/component/cell storage. Scene clear resets both domains independently.

No C callback mutates the queried tree. No worker invokes gameplay callbacks or SDL draw calls. Removal during callbacks is deferred through the existing lifecycle path and must be reconciled before the next consumer. Zero-change batches do no all-body BVH requery or full-pair scan.

## Trace and capture entities

`SpatialTrace` has a versioned domain header, initial state and ordered publish steps; stable trace IDs map to generation-aware live identities. Parsing/validation occurs outside timing. Collision input supplies exact AABBs; visibility input supplies transforms/dimensions and views so formation is included. See [spatial-replay.md](contracts/spatial-replay.md).

`BenchmarkRun` identifies one process, input hash, source/build, effective settings, worker allowance, capture mode and applicability. Step, render and replay samples are separate sequences. Initialization and resource records are not inferred from steady-state samples. Aggregation rejects invalid/incomplete/non-equivalent runs and retains raw files. See [benchmark-results.md](contracts/benchmark-results.md).
