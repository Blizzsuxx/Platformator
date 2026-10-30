# Data Model: Incremental Render Culling

All runtime state below is private, transient, and owned by one `SDLWindow`. Existing Sprite/GameObject ownership, serialization, and registry semantics remain authoritative. Names describe the implementation contract; they do not require a public type hierarchy.

## RenderCullingState

Created only for a renderer-backed cached strategy. Absent in headless, explicitly disabled, and current-linear comparison modes. Contains:

| State | Purpose and invariant |
|-------|-----------------------|
| Dense `records` | One per existing `spriteComponents` slot; same index/rank. No second Sprite membership registry. |
| `pending` rank vector | At most one queued change per live record; indexed cancellation. |
| Projection key/certificate | Dependencies of maintained bounds/grid and a safe numeric camera domain. |
| View key and validity flag | Conditions for reusing ordered visibility. Invalidation does not scan records. |
| Candidate and visible-rank buffers | Capacity reused; ordered visible ranks contain no duplicates. No cached texture/content. |
| Sparse cell map/node pool | Indexed strategy only; at most four cell memberships per normal record. |
| Side-list ranks | Oversized/uncertifiable entries, each with an O(1) removal backlink. |
| Query stamp | Deduplication across up to four cell hits; only increments on a query. |

No independent global revision counter is necessary: an invalid flag, compared view/projection values, and a nonempty affected queue cover validity. Do not add redundant dirty maps or append-only history.

## Renderable record

| Field | Rule |
|-------|------|
| Borrowed Sprite pointer and existing `ObjectHandle` | Valid only while registered in the owning window; handle carries generation/runtime identity. Unregister cancels/unlinks before destruction. |
| Current rank | Implicit dense slot matching `Sprite::windowIndex`; relocation repairs references. |
| Conservative world proxy | Double min/max; projection-dependent, not camera-translation-dependent. Includes certified numeric/raster enclosure. |
| Classification | `indexed`, `oversized`, or `uncertifiable`; non-indexed cases remain candidates. Cached-linear records need no cell links. |
| Pending queue index | Sentinel when absent. Repeated geometry changes do not enqueue again. |
| Four membership links | Cell reference and previous/next link IDs; only valid for indexed records. Fixed storage, no per-record membership vector. |
| Side-list index | Sentinel unless oversized/uncertifiable; swap-removal repairs the moved entry. |
| Last query stamp | Skips duplicate cell hits; irrelevant on a cache hit. |

Live Sprite width/height, scale, angle, synchronized position, and texture eligibility are sampled on refresh. Do not retain an additional snapshot of every property solely to poll for dirty state. Source rectangle/flip/pixel content are consumed at drawing, not retained in this record.

## Projection and view keys

**Projection key:** camera dimensions, actual prepared float X/Y scales, output dimensions, and the default-mapping policy version/eligibility needed to interpret bounds. Cells use effective world extents `output / preparedScale`. Camera position is explicitly excluded. Camera replacement with an identical projection invalidates selection identity but need not rebuild bounds.

**View key:** existing main-camera handle, synchronized camera X/Y, projection key, and selection/fallback regime. Compare finite values directly; invalid/NaN geometry never becomes an accidentally reusable key. Invalid/unclassifiable views may conservatively use a non-reusable fallback unless a stable bitwise key is explicitly supported. Ordinary supported unchanged frames must reuse visibility.

**Projection certificate:** a numeric safe domain and enclosure parameters derived from the actual float operation sequence. It has O(1) per-view camera checks and per-affected-sprite checks. The initial conservative domain and failure behavior are specified in [rendering.md](contracts/rendering.md). It does not accumulate coordinate history or scan records to compute a global maximum.

## Grid cells and storage

- Key: checked floor-based signed X/Y cell coordinates. Negative coordinates use mathematical floor, not integer truncation.
- Value: head of an intrusive membership list; no per-cell dynamically growing vector.
- Each normal padded proxy intersects at most four cells. Larger/unrepresentable proxies use the side list.
- Empty cells are extracted immediately; nodes are reused with a new key. No map of previously visited empty coordinates is retained.
- Let R be reserved record capacity. Node supply and map bucket capacity are prepared for at most 4R cells. `reserve` alone does not allocate element nodes: explicitly preallocate/recycle actual node handles during construction/growth. Pool-vector and scratch capacities are prepared too.
- Query candidates and selected ranks need at most R entries after stamp dedup. Dirty and side lists each need at most R. Grid link IDs/indices must remain valid across record-vector reallocations; do not store pointers into relocatable record arrays.
- Stable-capacity movement/churn adds no temporary visibility allocation. Growth, projection rebuild, scene reset, and destruction are observed separately. Scene reset releases old high-water storage; ordinary churn, even temporarily empty, retains bounded reusable capacity.

## Mutation publication table

| Source | Publication and result |
|--------|------------------------|
| Registered object position/hierarchy/affected descendants | `GameManager::synchronizeSubtree`, after actual world-position change: mark that object's registered Sprite. |
| `GameObject::setRotation`, `setScale` | After actual value change, gated geometry mark; do not alter independent collider/body behavior. |
| `Sprite::setWidth`, `setHeight` | After changed assignment, mark registered Sprite; retain existing invalid-input behavior. |
| `Sprite::setTextureWrapper` | Invalidate only if actual texture null/non-null eligibility changes; draw current texture/content regardless. |
| Physics worker integration | Keep translation/rotation mask in existing worker-local result. After join, existing position queue plus rotation mark on owning thread. |
| Synchronous `Rigidbody::move` | Same publication for both bits, without a new worker path. |
| Animation/late callbacks | Existing setters/publication; end-of-step reconciliation synchronizes pending transforms before render. |
| Registration/activation/component replacement | Existing window add/remove hooks create/cancel records and invalidate ordered result. |
| Swap-removal | Unlink removed record, cancel pending entry, relocate last record and repair its links/pending/side-list ranks; invalidate selection before free. |
| Camera translation/replacement/size and output mapping/resize | O(1) per-frame view/projection comparison; no per-sprite camera notifications. |
| Source rectangle, flip, texture pixels | No visibility invalidation when geometry/eligibility is unchanged; current values always used for drawing. |
| Committed scene clear/load; shutdown | After old memberships retire and before new publication, explicit safe reset/release. Failed preparation does not reset the live scene. |

Publication helper calls are gated before queue/revision/proxy work. A small inactive-state branch is allowed; absent-state modes do not initialize any per-sprite cache data or update a culling counter in production.

## State transitions

```text
Absent (headless/disabled/current-linear)

Cached state unprepared
  -> first eligible projection: construct bounds/grid -> selected result valid

Valid
  -> unchanged view and no affected work: reuse ordered ranks
  -> affected sprite: enqueue once, invalidate -> refresh affected -> select
  -> camera translation/identity: invalidate selection -> query/select, reuse bounds
  -> genuine projection change: refresh/regrid all -> select
  -> unsupported mapping/no camera: suspend selection, keep bounded dirty queue
  -> return to ordinary mapping: compare projection, drain dirty, select
  -> unsafe numeric camera: original linear selection on miss, reuse if valid/unchanged
  -> remove: cancel/unlink before retirement, repair rank, invalidate
  -> committed scene reset: release old capacity -> unprepared
  -> shutdown: release -> absent
```

Changing back to the old value before preparation may still cause one affected refresh; it must not cause repeated refreshes for intermediate values or refresh unrelated records. Query-stamp wrap resets live stamps once before a query, under explicit instrumentation; cache hits do not trigger it.

## Comparison evidence entity

A case is identified by strategy, profile, population, changed fraction, layout/size distribution, camera/output trajectory, seed, dt, warmup/measured steps, worker count, renderer/mapping, fixture/harness/source/build/binary identities, and proof IDs. Separate rendering/resource proofs must match that full key before timing is accepted. Source snapshots, raw samples, command logs, failure diagnostics, reports and checksums live in the persistent bundle. See [validation-evidence.md](contracts/validation-evidence.md).
