# Research: Selectable BVH and Segmented Spatial Culling

Date: 2026-10-28. Source baseline: `2ad8caa466b1c0217b7911d3bca1a53160150456`.
Four read-only research tracks covered physics/pair ownership, rendering, Box2D maintenance, and configuration/benchmarks. This document records design decisions, not new performance measurements. Phase 0 has no unresolved clarification items.

## R1 — Keep Box2D as the initial reference, not a proven optimum

**Decision:** Use the already extracted Box2D 3.2.0 dynamic tree, behind the existing private adapter. Keep the backend boundary narrow enough to permit another implementation later. Do not add another library in this feature.

**Rationale:** The dependency already supports the required 2D insertion, updates, removal, overlap queries and metrics and has retained upstream tests. The user explicitly continued the original plan after discussing alternatives. No evidence establishes it as universally fastest. The earlier static Eigen experiment cannot answer the dynamic question.

**Alternatives considered:** Bullet `btDbvt` offers dynamic updates and incremental optimization but introduces 3D types/adaptation; `aabbcc` is standalone and 2D-capable but derives from Box2D and adds general-dimensional/periodic machinery; build/refit-oriented libraries offer different update tradeoffs. These remain possible later comparators, not additional integration scope. See [Box2D tree API](https://box2d.org/documentation/group__tree.html), [Bullet source](https://github.com/bulletphysics/bullet3/blob/master/src/BulletCollision/BroadphaseCollision/btDbvt.h), and [aabbcc](https://github.com/lohedges/aabbcc).

**Evidence:** [Subset provenance](../../vendored/box2d-3.2.0/README.platformator.md), [adapter](../../src/dynamicaabbtree.cpp), and [adapter tests](../../tests/dynamic_aabb_tree_test.cpp).

## R2 — Extract common physics pair state; preserve the Grid algorithm

**Decision:** Introduce a private `PhysicsPairState` owned by `PhysicsManager`. Move the existing canonical `ColliderPair` storage, adjacency, narrow-phase pending vector/index repair and final-pair-removal callback into it. The selected segmented or BVH backend feeds spatial support changes and changed-endpoint notifications into that same store.

**Rationale:** `Grid` currently owns both indexing and all of that downstream state. Merely returning BVH overlaps would lose persistent contacts, geometry-version reevaluation, support accounting and teardown behavior. Reusing a dormant Grid as the BVH's pair database would obscure ownership and risk retaining unused grid work.

Retain the existing witness-count operation on stable pair records: grid deltas can have multiplicity from several cells; a deduplicated BVH relationship contributes exactly one support. Both use net reduction before applying transitions. This avoids inventing another per-pair witness map. Zero-to-positive creates one logical pair; positive-to-zero dequeues and invokes existing contact/support removal before reclaiming the pair. A changed collider queues its retained adjacent pairs even when membership is unchanged. Response-only changes preserve candidacy and use the existing response/support refresh.

**Alternatives considered:** Duplicating physics pair/contact handling per backend was rejected; a new general-purpose event bus or virtual object hierarchy is unnecessary. Retaining the pair algorithms inside a renamed backend-neutral coordinator would be equivalent, but the plan uses an explicit owner rather than a Grid-owned hidden dependency.

**Evidence:** [Grid ownership](../../src/grid.h), [pair/witness reduction and dirty flush](../../src/grid.cpp), [contact/support teardown](../../src/physicscontacts.cpp), [stable narrow-phase worker batch](../../src/physicsmanager.cpp).

## R3 — Reuse segmented mechanics without fake colliders or per-endpoint dispatch

**Decision:** Generalize the existing private AABB/cell operations to opaque spatial owners and one context-level policy. Preserve the public projection/axis/proxy class names, constructors and collider-facing methods in `collider.h`. Their owner slot becomes an opaque pointer with a compact owner-kind tag; a private friend/factory creates sprite/camera projections. `getCollider()` retains its meaning for collider-backed projections; internal generic code uses the spatial owner. Existing chunk pointers, indexes, cached coordinates and endpoint-to-projection indirection stay in place.

`SpatialPairOps` is one context-level set of direct bounds, eligibility and transition operations. It is called on candidate transitions, not during every endpoint comparison. Physics policy casts to an actual Collider and reads existing projections. Visibility policy casts to an owned render proxy. There is no owner registry lookup or virtual object per endpoint. Preserve the same geometry/tie, primary-axis, checkpoint cancellation, split/merge and migration algorithms.

**Rationale:** The installed collider header exposes `BoundingRadiusProjection`, `BoundingRadiusProjectionProxy` and `LocalSortArray*` signatures. Wholesale renaming/templating them would unnecessarily break source consumers. A context policy allows one implementation to serve physics and an entirely separate visibility grid.

**Cost assessment:** On the current 64-bit layout, the owner-kind tag is expected to fit master-projection padding; this is an estimate to verify with size/allocation checks. Per-cell bindings must not gain four wrapper allocations or per-comparison lookups. Source compatibility is required; binary ABI identity is not promised. The SDK must be rebuilt with its clients. Capture unchanged-segmented before/after measurements before attributing performance to BVH.

**Alternatives considered:** Fake Collider components were rejected. Copying the interval algorithm for sprites would create two correctness/maintenance paths. Type-erased per-endpoint callbacks and wrapper objects add unnecessary hot-path overhead. A deeper template extraction is reserved for evidence that the narrow owner adaptation cannot preserve required contracts efficiently.

**Evidence:** [Public projection types](../../include/platformator/collider.h), [AABB coupling](../../src/aabb.cpp), [interval repair/checkpoints](../../src/segmentedintervallist.cpp), [chunks](../../src/localsortarray.cpp), and [paper §§3.1–3.3](../../paper/paper.txt).

## R4 — Give rendering its own partitioned segmented context

**Decision:** The `segmented` renderer uses its own grid/cells, camera and sprite projections, operation buffers and multi-cell witness reduction, using R3's core. Accept only camera/sprite relationships; exclude sprite/sprite relationships at the earliest context-policy check. No visibility proxy belongs to `PhysicsManager` or its pair store.

**Rationale:** This implements the user's separate-AABB proposal while retaining the actual partitioned method, not substituting one large global list. Report the cell size and chunk capacity. Camera jumps incur real cell/membership costs and must be measured. Physics and visibility can run at different rates and use different geometry.

**Alternatives considered:** One global SAP is a useful separate research variant, but would not be labeled the existing grid-plus-segmented backend. Sharing the physics index would miss sprite-only objects and artwork outside collider bounds. Traversing the entire sprite vector just to reorder sparse index hits would discard much of the indexing benefit.

## R5 — Use an explicit, credible BVH maintenance policy

**Decision:** Default to `native-partial`: exact proxy bounds, internal create/move operations marking changed leaves, and one `Rebuild(false)` maintenance call per changed published batch. Perform an initial full build. A `full-rebuild` control uses the same updates but performs a full rebuild for each changed batch. Native-partial is a source-backed starting policy, not a measured optimum. No unrelated library or static prebuilt hierarchy substitutes for this path.

Expand the private C bridge and adapter rather than modifying upstream tree files. Upstream `MoveProxyInternal(..., true)` marks changes; public `MoveProxy` currently passes false. The current full-only bridge is insufficient for a partial-build comparison. Structural insertions must also be marked; do not combine unmarked public inserts with assumed partial-build readiness.

The engine owns coalesced dirty IDs and does not depend on Box2D's moved-leaf enumeration. Therefore maintenance may finish before immutable queries are published, even though upstream world scheduling can rebuild after pair collection. Document this scheduling difference. Record update, maintenance and query costs, actual rebuild calls and returned build-item counts; retained subtrees mean that count is not simply the number of dirty proxies.

**Rationale:** Omitting maintenance or using full rebuilds as the only policy can create an unnecessarily weak comparator. The extracted source contains the native partial-build implementation and tests, so this requires a bounded bridge extension.

**Alternatives considered:** Never rebuilding is a diagnostic control, not the default claimed representative. Topology-preserving refitting can be useful, but Box2D's `MarkProxyMoved` has containment/DFS prerequisites and its refit does not itself balance the tree; adding a separate refit profile is deferred rather than silently equating it with native movement. No guessed quality threshold is labeled optimal. Tuning happens on separate calibration cases; tested settings are then frozen in the reported bundle.

**Evidence:** [Create/move and rebuild implementation](../../vendored/box2d-3.2.0/src/dynamic_tree.c), [internal maintenance declarations](../../vendored/box2d-3.2.0/src/dynamic_tree.h), [existing bridge](../../src/box2d_tree_bridge.c). The original vendored import `0e51c1cffeed514e306409b8b4dfe69b8afb61f1:vendored/box2d-3.2.0/src/broad_phase.c` shows upstream marked moves and partial rebuild scheduling; that full-world source is intentionally not compiled or restored.

## R6 — Keep exact bounds and persistent pair reconciliation explicit

**Decision:** Default padding is zero. Support an explicitly recorded nonnegative world-unit envelope padding as a controlled setting. Cache exact bounds separately from optional tree envelopes. A contained dirty move may skip structural mutation, but must still trigger queries/old-adjacency reconciliation and narrow-phase geometry refresh.

For a collider batch: prepare current geometry; remove/add/update selected proxies; publish immutable tree state; query each added/geometry-dirty/filter-dirty eligible collider; union discovered pair keys with that endpoint's previous adjacency; canonicalize/deduplicate; evaluate current exact overlap and both collision-mask directions; apply net membership transitions through R2; queue retained changed pairs and response refresh. Unchanged colliders are not all re-queried, nor is the whole persistent pair graph scanned every step. This reproduces the current event-driven consumer instead of comparing SAP deltas against unnecessary BVH all-pair rescans.

The BVH may store each collider's 64-bit collision group and query with the querying collider's mask: every eligible pair necessarily passes that one direction. The shared final test still checks the reverse direction. Never truncate masks or treat Box2D's one-sided leaf filter as the complete engine predicate. The resulting canonical pair store is exact even when envelope queries return extra candidates; count those extra discoveries separately.

**Identity:** Box2D node IDs are reusable implementation handles. Canonical physics keys use complete generation-aware collider ObjectHandles. Tree user data identifies a stable backend-owned record (or its stable address), whose lifetime spans readers and reduction; it is never interpreted as a packed full ObjectHandle or used after reclamation.

**Limits:** Preflight finite/ordered proxy bounds and padded extents before committing an index batch. The pinned default Box2D proxy extent limit is strictly below 100,000 world units; larger query rectangles are allowed. Physics BVH selection reports unsupported scene/batch input explicitly and marks the run failed, rather than silently switching backend or dropping a collider. Renderer-specific fail-open handling is R8. Reusable record pools retain bounded high-water capacity, not an ever-growing history of destroyed proxy IDs.

## R7 — Make physics membership diagnostics truthful

**Decision:** Add a backend-neutral registration accessor to Collider. Retain the current Grid-named accessor and ranges as literal Grid diagnostics: BVH members are not Grid members and their ranges remain invalid unless explicitly calculated. Existing queue-status accessors reflect the common selected-backend queue. Rename private desired-membership/queue state to backend-neutral terms.

Split internal shape/projection synchronization from grid-range calculation. SAP computes ranges when adding/updating; BVH reads the current four bounds without paying for unused cell-coordinate calculations. Preserve the behavior of the public explicit `prepareSync()` and existing Grid calculation methods. Existing segmented defaults and public source spellings remain compatible; document Grid-specific reads under the new BVH choice.

**Evidence:** [Collider registration and queue APIs](../../include/platformator/collider.h), [geometry/range preparation](../../src/collider.cpp).

## R8 — Bound the quad that is actually rendered

**Decision:** All culling choices use the same prepared camera and cached conservative sprite bounds. The unmodified submit-all path remains the control and allocates no visibility index. The linear baseline also caches dirty bounds, so its comparison with an index does not conflate bounds caching with query acceleration.

The camera position is the current view's top-left. The current renderer scales destination width/height independently and then rotates in destination space. For post-object-scale dimensions `w,h`, angle `theta`, and camera scales `sx,sy`, inverse-scaled world half extents are `hx=(abs(c)*abs(w)+abs(s)*abs(h)*sy/sx)/2` and `hy=(abs(s)*abs(w)*sx/sy+abs(c)*abs(h))/2`. Preserve signed dimensions in the actual draw. Compute conservative/outward bounds matching the submitted rotation, and expand the query for raster/rounding guard; charge the work to bounds preparation. A scale-ratio change invalidates all cached shape extents; camera translation and uniform scale changes update the query/guard. Do not add inherited scale/rotation semantics absent from today's renderer.

Per-render view validation checks output size, render target, viewport, renderer scale and logical presentation. With non-default unsupported SDL view state or an invalid camera transform, draw through the original submit-all path and record the bypass reason. A restrictive clip need not tighten conservative bounds. An individual invalid or BVH-unrepresentable sprite remains in an ordered uncullable list rather than disappearing. Benchmarks report such hybrid/bypass paths explicitly and cannot claim an unqualified indexed result from them.

**Rationale:** The public renderer handle permits external SDL state changes. A nominal camera rectangle alone can otherwise produce false negatives. Source rectangles, flips and texture-only changes affect content but do not change the centered destination quad; animated width/height changes do.

**Evidence:** [Camera transform](../../src/camera.cpp), [sprite dimensions](../../src/sprite.cpp), [animation](../../src/animator.cpp), [SDL view state and rotated copy](../../vendored/SDL3-3.4.18/src/render/SDL_render.c), [public renderer handle](../../include/platformator/runtime.h).

## R9 — Publish visibility per rendered frame and preserve mutable draw order

**Decision:** Dirty notifications come from actual published world-position changes, rotation/scale changes, sprite dimension changes and lifecycle registration/removal. Coalesce them and flush only the selected visibility method immediately before rendering, after current transforms/mutations are synchronized. Preserve the existing paused/zero-step behavior. Headless/no-camera paths do not maintain a hidden visibility index; when a usable camera returns, bring selected state current before querying.

Render proxies are owned by SDLWindow's private visibility manager and removed before component/object retirement. Collect unique hits and the uncullable list, refine against current conservative bounds, and restore the current `windowIndex` order using reusable storage. Removal is swap-with-last, so creation order is not a valid substitute. Cache the ordered list using geometry/index, camera/view and draw-order revisions; reuse is allowed for every culling method when its inputs are unchanged. Any mutation invalidates the appropriate revision before a consumer can read it. Record whether a query/order pass was performed or reused.

**Alternatives considered:** Per-step-only updates fail while paused; full-scene geometry scans every frame waste temporal coherence; forcing an O(N) sprite walk to order every sparse indexed result is avoidable. Per-object callbacks into worker-owned SDL rendering are unsafe.

**Evidence:** [Frame/commit ordering](../../src/gamemanager.cpp), [transform setters](../../src/gameobject.cpp), [registration, swap removal and render early returns](../../src/sdlwindow.cpp).

## R10 — Preserve worker ownership boundaries

**Decision:** Keep segmented cell placement/repair partitioned through existing oneTBB facilities with thread-local deltas and a post-worker serial reduction. BVH insertion, removal, category updates and maintenance use an exclusive writer phase. Dirty-collider read-only queries may use oneTBB with task-local results after publication; no query races a tree writer. One camera query and ordered SDL submission remain sequential. Reclamation and gameplay callbacks follow completed readers/reduction.

Provide an explicit serial/parallel BVH query setting. Parallel uses the existing worker allowance and at least two independent queries, without a new pool or a claim that that minimum is a tuned performance crossover. The serial setting is a calibration/control profile. Evaluate latency across small/large batches and worker counts before selecting any measured cutoff; frozen settings identify the result. Existing narrow-phase/solver ordering rules and required numerical tolerances are unchanged.

**Rationale:** Atomics in internal moved-node marking are not permission to race queries, rebuilding or lifetime changes. Parallelize independent work rather than attaching a lock to every proxy operation. Canonical reductions keep pair output independent of worker completion order. Record remaining serial sorting/pair reduction and single-camera costs as scalability limits.

## R11 — Use startup settings and an explicit schema transition

**Decision:** Append `SpatialSettings` to RuntimeOptions; expose typed independent broad-phase/render choices and BVH policy/padding/query controls. Defaults are segmented physics, submit-all rendering, native-partial maintenance, zero envelope padding and parallel independent collider queries. The shared runtime parser, stock runner and the benchmark runner's separate parser all enforce the same tokens. See [runtime selection contract](contracts/runtime-selection.md).

Introduce benchmark output schema 4 for the added samples/counters/provenance. Preserve schema-3 artifacts and their meanings; readers dispatch by explicit version, reject mixed-version aggregates and require exact versioned field sets. No optional BVH columns are silently inserted into schema 3. A dedicated spatial sweep orchestrates new cases using the existing fresh-process/eligibility methodology; existing scenario commands remain usable.

**Evidence:** [RuntimeOptions aggregate](../../include/platformator/runtimeoptions.h), [runtime parser](../../src/runtimeoptions.cpp), [independent benchmark parser](../../src/benchmark_main.cpp), [recorder's strict schema 3](../../src/benchmark.cpp), [sweep validators](../../benchmark/sweep_scaling.py).

## R12 — Distinguish replay, full-engine, validation and resources

**Decision:** Use versioned deterministic JSONL traces with collision and visibility domains. Collision traces supply exact AABBs and eligibility changes, exercising persistent pair deltas and changed adjacency; their precomputed bounds are explicitly not object-transform cost. Visibility traces supply sprite world transforms/dimensions and camera/output changes, so bounds formation is timed. Loading/generation and expensive oracle/image checks occur outside timing. Full-engine cases include real shape preparation, lifecycle, narrow phase/solver, visibility and drawing.

Keep complete-step and render boundaries distinct. Add an all-flush spatial reconciliation measurement because current cleanup can also flush collider operations outside PhysicsManager's named broad-phase scope. Count that nested work without adding it twice to complete-step cost. Renderer samples own their own counters, independently of simulation steps. Empty/headless rendering emits no actual render samples.

Use separate validation runs and a resource-only executable for allocation hooks. Timed captures do not link allocation interceptors or run full oracles. Report retained/peak accounted index/common/scratch capacities with declared exclusions, plus process peak RSS where available. Resource-only C++ allocation interception and Box2D allocator callbacks declare coverage and cannot enter timing aggregates. There is no claim that capacity estimates include allocator metadata or that whole-process RSS is tree-only memory.

**Workload decision:** Use the bounded case matrix in [benchmark contract](contracts/benchmark-results.md), at least five fresh processes per reported method/case/worker configuration. Freeze calibration settings separately from reported evaluation traces. Same-method pre/post-refactor controls use the same common timing boundary and preserve historical schema-3 records. Static/query-only, software-renderer and closed-loop measurements remain explicitly distinct.

## Resolution and gate summary

| Research question | Resolution |
| --- | --- |
| Shared pair ownership and public projection compatibility | R2, R3, R7 |
| Renderer-independent sprite/camera SAP | R3, R4, R8, R9 |
| Dynamic Box2D baseline, tuning and supported bounds | R1, R5, R6 |
| Safe publication and multicore execution | R9, R10 |
| SDK/CLI settings and backward-compatible evidence | R11, R12 and contracts |

The pre-design constitution gate passes with these bounded research obligations; the post-design gate is recorded in [plan.md](plan.md). No default-backend replacement, new solver, new worker pool or unsupported performance claim is part of the design.
