> Historical pre-convergence record. Current implementation, qualification and completion evidence are in [convergence/IMPLEMENTATION.md](convergence/IMPLEMENTATION.md), [final-validation.md](final-validation.md) and [RESULTS.md](RESULTS.md). Earlier raw data/source identities remain unchanged.

# Spatial implementation review

Source baseline: `2ad8caa466b1c0217b7911d3bca1a53160150456` (the current `HEAD` when reviewed).
The review follows the feature's research decisions R1–R12 and Platformator Constitution v1.1.0.
This is a source and algorithm review, not benchmark evidence.

## Existing ownership and work boundaries

- `PhysicsManager` owns the `Grid`, but `Grid` currently owns more than spatial indexing: canonical
  `ColliderPair` records, per-collider adjacency, the pending narrow-phase pointer vector and its
  swap-removal indices, response refresh dispatch, and final pair-removal callbacks. Pair storage
  outlives cell-local candidate tracking and therefore cannot be retired while worker batches or
  adjacency/queue references still point at it.
- `Grid` owns the concurrent cell map and collider/cell operation queues. Collider additions,
  removals and syncs are first coalesced into the queued collider array. Cell operations are then
  applied with oneTBB; the current implementation queues independent cell processing with
  `parallel_for`. Each `AABB` owns the x/y interval lists, endpoint bindings and current two-axis
  overlap set. Worker-local pair witness deltas are collected through enumerable thread-local
  vectors, sorted by full collider handles, reduced canonically, and applied to the persistent pair
  owner after cell work. Candidate removals update both adjacency lists and dequeue pending narrow
  phase before reclaiming the pair record. Potentially empty cells are erased only after workers
  have joined.
- A collider can be copied into multiple grid cells. Its cached range maps it back to those cells;
  each cell has an `AABB` binding with x/y endpoint pairs. `ColliderPair` identity is canonicalized
  by full `ObjectHandle`, while witness counts track how many cells independently report an
  overlap. A net 0-to-positive witness transition inserts one pair and adjacency entries; positive
  changes retain it; a transition to zero removes it once.
- The private Box2D adapter owns a `b2DynamicTree` and represents a proxy ID as a tree-local
  integer. User data is an opaque 64-bit value. Existing create/move APIs are unmarked for partial
  rebuild purposes; the current bridge calls a full rebuild. `DynamicAabbTree` validates finite,
  ordered bounds and rejects proxy extents at or above Box2D's `B2_HUGE`, while larger query bounds
  remain permitted. Query callbacks cross a C boundary and currently have no exception capture.
- The current segmented structure is collider-specific. `AABB` creates cell-local bindings and
  forwards candidate changes to `Grid`; `GridCell` owns deferred add/sync/remove bindings. Public
  collider projection names are present in `include/platformator/collider.h` and must remain source
  compatible under the planned generic owner seam.

## Segmented interval and overlap invariants

- X and Y extrema are maintained in sorted order. Comparator tie order puts minima before maxima,
  so touching inclusive intervals overlap. Full AABB eligibility checks both axes and both
  directions of the 64-bit group/mask predicate; active state is checked before adding an overlap.
- Each `SegmentedIntervalList` is a binary-searchable vector of pointers to linked small chunks.
  Projections cache their chunk and in-chunk index. Local movement swaps neighboring projections
  and repairs those bindings whenever a boundary is crossed. Inserts split full chunks; removals
  may merge neighboring chunks. Those paths must keep the ordered chunk index, links, sizes and
  every moved projection's cached binding consistent.
- Only the primary interval axis maintains per-chunk checkpoint coverage. A checkpoint is an
  object whose interval enters at or before the chunk and exits to its right. The paired
  `checkpointCache` records a removal that happened before a matching add during fast endpoint
  motion; the later add cancels the pending removal instead of manufacturing a checkpoint. Split,
  merge, multi-chunk insertion/removal and endpoint swaps must preserve coverage. Secondary-axis
  checkpoint sets remain empty.
- A pair found in more than one cell contributes multiple witnesses but only one canonical pair and
  one set of adjacency references. Removing one cell witness cannot emit an exit while another
  remains. Removal and migration can produce opposing deltas in the same batch, so reduction must
  apply their net value before deciding whether support crossed zero.
- Broad-phase AABB candidates are not confirmed narrow-phase contacts, collision support, or
  gameplay events. Retained dirty pairs still require narrow-phase refresh; response changes
  refresh pair response even if overlap membership is unchanged.

## Publication, readers and reclamation

The current safe boundary is: main-thread lifecycle/sync requests are coalesced; workers mutate
distinct cells and produce thread-local deltas; all cell readers/writers join; deltas reduce in
canonical handle order; physics receives the stable pending pair batch; workers process that fixed
batch; serial reduction and callbacks complete; only then may pair/collider/cell storage be
reclaimed. Grid cell erasure follows worker completion. A future BVH writer phase must be exclusive
for proxy edits and maintenance, followed by immutable parallel queries, join, canonical
reconciliation, callbacks and deferred retirement. Box2D node IDs cannot serve as logical identity
because the tree recycles them; stable backend records must retain generation-aware endpoint
identity through all queries and reductions.

## Alternatives required by the paper and constitution

- **Subdivision plus per-cell sweep-and-prune** reduces unrelated extrema density and bounds
  insertion/removal movement by cell population, preserves independent cell parallelism, and
  deduplicates multi-cell candidates through witness counts. Skew and large bounds can still create
  crowded cells and many memberships.
- **Global sweep-and-prune** removes cell-placement and duplicate-witness work, but lets distant
  extrema increase swap and insertion/removal costs; it is not equivalent to the current
  grid-plus-segmented control.
- **Local sorting** uses temporal coherence and only repairs changed endpoints. It remains a good
  control for sparse motion, but heavily changed endpoints can generate many swaps.
- **Batch insertion/removal** sorts queued extrema and merges them with retained lists in a pass;
  this can suit large churn or mostly moving populations, at the cost of touching the whole list and
  extra buffers. It is not implemented by the current per-endpoint segmented insertion path.
- **Adaptive local/global or local/batch repair** could use local repair at low change fractions
  and bulk work at high change fractions, but adds thresholds, state and additional correctness
  paths. No threshold should be chosen without representative measurements; it is outside the
  current comparison until evidence justifies it.
- **Dynamic BVH** is a separate reference with path-based updates and query pruning, but may degrade
  toward linear traversal for overlapping or skewed bounds. Its full or partial maintenance,
  exact refinement, adjacency reconciliation and rebuild costs must all be charged. The pinned
  Box2D implementation is an evaluated comparator, not a proven optimum.

The paper's asymptotic arguments and historical tuning measurements motivate cases and invariants;
they are not modern CPU speedup guarantees. This review establishes implementation obligations
only. T002 remains the required immutable schema-3 baseline and is not satisfied by this document.
