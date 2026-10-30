# Research: Incremental Render Culling

Date: 2026-10-29. This is source/design research, not new performance evidence. The two planning research streams examined index/geometry choices and mutation/lifecycle publication. All decisions below are resolved for implementation; measured acceptance remains future work.

## 1. Reuse affected-item publication, not an already consumed queue

**Decision:** Publish sprite geometry changes to an optional, window-owned coalescing queue at existing owning-thread publication points. Drain it during eligible render preparation.

**Rationale:** `GameManager::queueTransform`, `cancelTransform`, `synchronizeSubtree`, and `synchronizeTransforms` already provide deduplication, affected-descendant traversal, and parent-before-child publication. The queue is cleared during simulation. Notify the registered Sprite when `synchronizeSubtree` actually changes its synchronized world position. Do not rescan objects or inspect the emptied queue during rendering. Reuse indexed queue insertion/cancellation conventions rather than introduce a general observer system.

`GameObject::setRotation` and `setScale` bypass position synchronization; `Sprite::setWidth` and `setHeight` are plain writes. These need gated notifications on changed values. `setTextureWrapper` needs invalidation for changes in actual null/non-null SDL texture eligibility, after assignment. Source rectangle, flip, texture pixels, and animation content must remain live draw inputs; they do not independently change destination geometry. Constructor/deserialization changes before registration are picked up by initial registration, without notifying staged objects.

**Important additional path:** `Rigidbody::integrateLocal` writes position and rotation directly. Its return mask has translation and rotation bits, but `PhysicsManager::MovedObject` currently preserves only translation. Preserve the existing mask in the existing worker-local results, and publish rotation invalidation in the owning-thread merge after TBB completion. Cover both rotation-only and combined motion. The public synchronous `Rigidbody::move` needs the corresponding rotation publication. Do not mutate a render queue from physics workers, change integration, or introduce a second physics pass. The mask already exists; retaining it is not a new worker notification queue.

End-of-step `commitMutations`/`reconcilePending` synchronizes transforms after later callbacks too. Preserve that boundary and paused-frame behavior rather than force a new global transform sync in `render`.

**Alternatives considered:** Per-render revision polling still visits N sprites; reading `dirtyTransforms` after simulation misses changes; modifying setters alone misses physics rotation and hierarchy publication; a global event bus adds unnecessary ownership and dispatch machinery.

## 2. One render-only sparse grid; no new library

**Decision:** Implement a small internal 2D spatial hash grid for maintained conservative sprite AABBs. Give it separate membership from physics. Initially use rectangular cells equal to the effective view's world extent: `outputWidth / scaleX` by `outputHeight / scaleY`, using the actual prepared float scales promoted to double. Do not add a tuning API.

**Rationale:** This selects local sprite candidates for a single view, with straightforward local remove/reinsert and no balancing or overlap-pair bookkeeping. View-sized cells give a small camera cell range without choosing an unmeasured tuning constant. This is a testable first design, not a fastest-index claim.

For a proxy whose padded width and height do not exceed the respective cell dimensions, an inclusive AABB intersects at most two cells per axis, hence at most four memberships. Store four intrusive membership links in its render record. Larger proxies and numerically unclassifiable entries go into an indexed side list and are conservatively considered on every changed-view query. The exact existing classifier refines them. They are not lost or assigned a gameplay collider. Measure side-list size and cost, including pathological giant-sprite populations.

Use floor-based signed cell coordinates (including negative positions), checked range conversion, inclusive boundary coverage, and no unchecked integer multiplication/range iteration. Numeric camera fallback prevents enormous or unrepresentable coordinate traversal. A valid normal query visits a bounded cell rectangle; it does not enumerate every occupied cell.

**Alternatives considered:**

- Existing `AABB`/`SegmentedIntervalList`: designed around `Collider`, endpoint bindings, checkpoints, pair transitions, and owning `Grid`; no read-only rectangle-query interface. Factoring that system would expand this rendering feature into broad-phase architecture work.
- Dynamic AABB tree/BVH: attractive for size variation and range queries, but needs a correct maintained tree, rotations/reinsertion, and pooling, or a new dependency. No suitable tree is currently used in this branch. Retain it as the next alternative if grid density/oversized-list evidence defeats this design.
- Sorted intervals: maintaining order and two-dimensional queries requires additional machinery; a simple prefix walk can still visit N entries.
- Cached linear: required comparison and simpler than a grid, but every invalidated view still checks N bounds. It cannot satisfy the requested indexed-selection scope by itself.
- Several production backends or a new Box2D integration: unnecessary to answer this feature's cache-versus-index question. No library addition is planned.

## 3. Translation-invariant bounds must model screen-space rotation

**Decision:** Maintain projection-dependent world-coordinate proxies, then refine returned candidates with the current output-space classifier. Camera translation changes the query, not all proxies. Projection changes refresh all bounds explicitly.

**Rationale:** `Camera::renderWithView` independently scales X/Y before SDL rotates in output coordinates. For positive scaled sprite dimensions `w,h`, prepared scales `sx,sy`, and the same degree-derived float sine/cosine used by SDL, ideal screen half extents are:

```text
ex = (abs(cos) * w * sx + abs(sin) * h * sy) / 2
ey = (abs(sin) * w * sx + abs(cos) * h * sy) / 2
world half extents = ex / sx, ey / sy
```

Compute the ideal enclosure in double, centered at the synchronized float world position, with outward expansion. Rotating the unprojected world rectangle is incorrect when scales differ. The query rectangle is `[cameraX, cameraX + outputWidth/sx] × [cameraY, cameraY + outputHeight/sy]`; using the actual scale avoids assuming float division exactly reproduces the camera's configured extent.

**Numerical safety decision:** An ideal radius plus a fixed pixel is insufficient. The existing classifier pads by `1 + 8 * float_epsilon * maximumMagnitude`, and camera-relative float subtraction/rotation can differ from ideal geometry. Add a small projection certificate and a conservative broad envelope, as specified in [rendering.md](contracts/rendering.md). It bounds float-operation error over a declared camera coordinate domain without depending on the current camera translation. The certificate is checked once per frame; each sprite is certified when its bounds refresh. Entries outside that proof domain remain unconditional candidates. A camera outside it uses the unchanged current-linear classifier for the whole frame, with no index rebuild. Unsupported SDL mappings retain the existing submit-all fallback.

The implementation must derive and test the enclosure against the exact operation sequence, including overflow, subnormal/error bounds, and SDL-compatible trig. This is a blocking correctness obligation, not permission to use an assumed epsilon. Do not assert a larger safe domain or remove fallback without proof. Record fallback frequency so an index gain cannot conceal an impractically narrow domain.

**Alternatives considered:** Caching screen bounds at the previous camera position becomes stale on pan; shifting them alone ignores changing cancellation/error. World-space rotation is geometrically wrong under unequal scales. Recomputing all bounds on every pan violates the intended incremental work. A fully general arbitrary-coordinate proof/geometry framework would add unnecessary complexity; explicit conservative fallback is already an established renderer contract.

## 4. Cache ordered identities and reuse existing ranks

**Decision:** The optional render state contains dense records aligned with `SDLWindow::spriteComponents`, a dirty-index queue, spatial memberships, reusable candidate/visible vectors, and one valid-view key. Reuse `Sprite::windowIndex` as authoritative order. Deduplicate query hits with a per-record query stamp, refine candidates, and sort only retained ranks. Repeated unchanged frames reuse this vector without querying or sorting.

**Rationale:** Membership already enters/leaves through `SDLWindow::addSpriteComponent`/`removeSpriteComponent`. Removal swaps the last Sprite into the removed slot and repairs its window index. Repair only that moved record's links/queued index/side-list index too. Invalidate the visible vector before any retirement; synchronous unlink/cancellation happens before GameManager destroys the component. Store existing `ObjectHandle` identity with each record and validate identity in correctness builds. No new global object registry or address-based identity is needed.

The draw list contains membership ranks/identities, not cached textures or prepared animation content. Draw current sprite inputs with the existing SDL argument formulas. Candidate refinement and selected drawing share geometry helpers; any repeated preparation cost stays inside render timing. Avoid a persistent second cache of drawing packets unless measurements justify it.

**Alternatives considered:** Scanning all sprites to restore order defeats sparse-query work; rendering traversal order changes transparency; append-only IDs/maps retain dead scenes; per-frame lists with fresh allocation violate the warmed-capacity requirement.

## 5. Bounded reuse without a generic allocator

**Decision:** Use contiguous reusable record/scratch/queue storage and fixed four-link memberships. The sparse cell map stores only cell heads. Extract empty hash nodes into a bounded reusable `node_handle` pool and rekey/reinsert them for newly occupied cells. Reserve buckets, node supply, queue, side-list, and scratch capacity during cold construction/population growth, before the warmed measurement window. Cells do not retain historical coordinates.

**Rationale:** Plain `unordered_map::erase`/`emplace` and fresh cell vectors allocate during continuous cell migration. Standard node extraction permits recycling without adding an allocator library. Cell supply is bounded by four times reserved record capacity, not elapsed movement or visited coordinates. Prewarming the selected capacity is charged to cold construction; stable-capacity movement/churn must then reuse it. Clear/release high-water storage on scene reset and window destruction, outside a live traversal, and measure that cost. No shrinking or allocation is required each ordinary frame.

A query-stamp wrap performs an explicitly observed reset of live stamps before the next query; test it through an instrumented seam. It never runs on a cache-hit frame. Exceptionally global work is not a recurring dirty-discovery algorithm.

**Alternatives considered:** Per-cell vectors need a separate capacity-retention policy and migration backlinks; retaining every visited empty cell grows with history; a custom generic slab/hash framework expands scope. A custom allocation layer is not required for this narrowly bounded map-node reuse.

## 6. Owning-thread maintenance and zero inactive bookkeeping

**Decision:** Create optional culling state only for a real renderer with enabled culling and a cached strategy. Notifications first test that state; disabled/current-linear/headless modes do not enqueue, increment revisions, or allocate cache metadata. Existing independent membership and physics work remains unchanged. Constant gate branches are not bookkeeping.

All index/queue mutations, candidate selection, sorting, and SDL calls occur on the existing owning thread. Worker-local motion results are merged after TBB completion. Sequential maintenance/query is the initial design: small coalesced batches, hash/link dependencies, ordered SDL drawing, and no per-frame scheduler cost. Benchmark one and physical-core worker configurations including fully dynamic 50,000-sprite cases. If profiling makes maintenance a material serial bottleneck, evaluate a bounded TBB bounds-computation stage before accepting it; do not add a competing worker pool.

Fallback frames do not consume a stale cache. Keep bounded notifications pending while preparation is suspended; remove retired entries immediately. On resumption, drain affected entries and compare projection keys before querying. A genuine changed projection may trigger global refresh; mapping fallback alone and camera translation must not silently force one.

## 7. Isolate cache and index benefit with current evidence tooling

**Decision:** Extend the focused probe/driver and fixtures for three explicit internal comparison strategies: `current-linear`, `cached-linear`, and `indexed`. Keep public `enableRenderCulling` and `--no-render-culling` unchanged. No public backend selector or virtual interface. See [validation-evidence.md](contracts/validation-evidence.md) for the bounded matrix, CLI contract, and gates.

`current-linear` keeps the current ordered scan/classifier and no cache/index bookkeeping. `cached-linear` uses identical dirty publication, maintained bounds, fallback, and visibility reuse, but scans maintained records on a cache miss without constructing a grid. `indexed` adds grid maintenance/query and candidate ordering. Thus indexed-versus-cached-linear counts the cost of the extra structure. Retain an immutable pre-engine-edit feature-008 source snapshot and verify current-linear parity/overhead against it on selected controls.

The T044 bundle is the durable source/evidence reference, not a substitute for fresh matching measurements: `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`. Its source fingerprint is `4a5ce52e10fc81f903c3687879ee9d883836ec13668e18185f967fc6fa20dfba`. Feature 001's headless timings do not establish rendering performance; feature 008's submit-all comparison does not establish an index winner.

Retain new evidence below the already ignored `benchmark/artifacts/linear-render-culling/009-<run-id>/`. Preserve schema-v3 timing boundaries and all old bundles. Report render and complete-step separately; also directly measure paired end-to-end step-plus-render intervals for this experiment, without summing nested scopes. Include notification work added during simulation. Software measurements support software-backend conclusions only. No speedup claim is made by this plan.

## Local primary-source basis

- `src/camera.cpp`, `src/spriterendergeometry.h`, `src/sdlwindow.cpp`, `src/sdlwindow.h`: draw geometry, numerical fallback, renderer eligibility, membership/order.
- `src/gamemanager.cpp`, `src/gameobject.cpp`, `src/sprite.cpp`, `include/platformator/objecthandle.h`, `src/objectregistry.h`: publication, setters, reconciliation, identity and retirement.
- `src/physicsmanager.cpp`, `src/physicsmanager.h`, `src/rigidbody.cpp`: worker-local changes, merge boundary, direct rotation writes and synchronous move.
- `src/grid.cpp`, `src/aabb.h`, `src/segmentedintervallist.h`: existing collider-specific spatial state and why it is not a ready render-query API.
- `benchmark/README.md`, `benchmark/capture_linear_render.py`, `benchmark/linear_render_probe.cpp`, `tests/render_culling_fixture.h`, and `specs/008-linear-render-culling/evidence/`: existing methodology, durable baseline, actual comparison limits.

No unresolved user clarification or dependency selection remains. Numeric enclosure, resource behavior, and performance are implementation/validation gates, not already measured findings.
