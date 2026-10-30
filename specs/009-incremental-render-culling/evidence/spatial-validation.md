# Spatial Index Validation

## Test-first results

`tests/render_culling_index_test.cpp` covers negative fractional cell coordinates, inclusive ties, four-cell proxy coverage, oversized and uncertifiable side-list entries, move/unlink/swap-relocation, distinct keys with colliding hashes, query-stamp wrap, actual node-pool bounds/reuse, and the cached-linear no-grid gate. Its independent reference checks each query for duplicate ranks and verifies that all exactly intersecting indexed proxies and all conservative side-list entries are present.

`tests/render_culling_test.cpp` now compares corresponding current-linear and indexed images/order during repeated camera pans over a 403-Sprite spread/overlap fixture, then changes the projection. It requires actual spatial queries, selective candidate visits, zero camera-only bounds refresh/relink/full-population scans, bounded ordered selection work, and explicit projection refresh.

With the grid not yet implemented, the test-first checks failed as intended:

```sh
cmake --build build/009-incremental-validation --target platformator_render_culling_index_test platformator_render_culling_test --parallel 4
```

The index test compiled and failed to link because the spatial-index operations do not exist yet. The rendering test compiled; running it failed at “A camera pan must execute an actual spatial query.” The cached-linear tests preceding the new pan case continued to pass. These failures define the implementation work; no spatial correctness or performance acceptance is claimed.

## Implemented grid and selection result

The indexed strategy now maintains floor-based cells, four fixed intrusive link IDs per record, a conservative side list, reusable map node handles, bounded link/candidate/side capacities, and per-query deduplication stamps. Cell-node and link-pool growth follows geometric record-capacity growth; empty cells are extracted and reused. Cached-linear retains no allocated grid object or grid pools. Projection changes regrid after refreshing bounds; camera translation queries existing bounds. Indexed misses sort only refined retained ranks, and hits reuse them.

The independent index tests passed after implementation. They cover negative/fractional cells, inclusive edge ties, four-cell membership and duplicate suppression, oversized/uncertifiable side-list coverage, movement, unlink/swap-relocation, record/node-pool growth, repeated migration without node-pool growth, distinct hash-colliding cell keys, and forced query-stamp wrap. The two side-list entries were visited once per query: 3 queries, 6 cell visits, 6 side-list visits, 6 active cells, and a 48-node supply for 12 reserved records. Brute-force AABB reference checks found no missing candidate, and all returned ranks were unique and live.

The rendered 403-Sprite spread/overlap trajectory passed current-linear image, submission-order, and independent-oracle comparisons across four camera pans and one projection change. A representative pan performed 1 query over 9 cells, refined 3 candidates, ordered 3 retained ranks, refreshed 0 bounds, relinked 0 memberships, and visited 0 full-population records. It retained 404 active cells from a 2048-node pool (512 reserved record slots, at most 2048 nodes). The projection-change frame refreshed and relinked all 403 records, then queried once and ordered one visible rank. Static indexed scenes verified that subsequent unchanged frames perform no query, cell visits, candidates, bounds refresh, population scan, or order work, and record one cache hit.

Focused validation commands:

```sh
cmake --build build/009-incremental-validation --target platformator_render_culling_index_test platformator_render_culling_test platformator_render_culling_geometry_test platformator_render_culling_contract_test platformator_runtime_work_test --parallel 4
ctest --test-dir build/009-incremental-validation --output-on-failure -R '^(platformator_runtime_work_test|platformator_render_culling_geometry_test|platformator_render_culling_test|platformator_render_culling_contract_test|platformator_render_culling_index_test)$'
```

All five selected entries passed. These are work and correctness observations only; they do not establish timing improvement or SC-006 acceptance.
