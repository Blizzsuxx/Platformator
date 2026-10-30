# User story 3 lifecycle validation

This evidence covers T026–T031 and was collected with the work-probe build at
`build/009-incremental-validation`.

## Test-first findings

- The first expanded mapping run found that the test readback assumed every SDL
  mapping returned the full output surface. The fixture now reserves the output
  dimensions and captures a smaller active viewport into that storage. Fallback
  assertions compare the full ordered eligible submission trace and rendered
  pixels across strategies.
- The initial worker assertion expected an indexed camera query after physics
  moved objects. The implementation correctly used the dirty-rank path because
  the camera view was unchanged; the test now requires one refresh per affected
  record and no query for this case.
- Before T030, the scene-clear regression failed because record/grid capacity
  remained allocated after successful clear.
- Before T031, an injected failure during `reserveRecords(64)` followed by retry
  left the newly appended/query selection path allocating during warmed queries.

## Coverage and implementation results

- Consecutive current-linear, cached-linear, indexed, and submit-all frames cover
  source rectangles, flips, texture eligibility/content swaps, dimensions,
  scale, rotation, parent reparenting, movement, camera replacement/removal,
  actual output resize, and viewport/scale/logical-presentation/render-target
  fallback and return. Fallback frames retain no culling work; deferred changes
  are refreshed on resumption. Independent visibility membership, submission
  order, and image comparisons are retained.
- The worker fixture compares one worker and the machine-reported concurrency
  limit for indexed mode, plus current-linear at the same concurrency limit.
  Translation-only, rotation-only, combined movement, rounded-away translation,
  late-update edits, same-frame output, work coalescing, and collider vertices
  agree. Observer identity resolution is confirmed on the owner thread.
- Grid lifecycle checks cover first/middle/last swap removal with dirty queue
  entries, same-slot new-generation reuse, clustered-to-spread migration,
  stable-capacity movement, transient empty membership, and injected capacity
  allocation failure/retry. Ten warmed query/visible-selection iterations
  allocate zero C++ heap blocks in the observer.
- Scene regressions verify malformed preparation preserves the live cache,
  ordinary empty churn retains capacity, successful clear/load releases old
  storage, repeated scene reset has no old cell keys, and new Runtime instances
  have distinct handle lifetimes.
- `RenderCullingState::releaseStorage()` drops high-water vectors and cell
  storage without reallocating at successful scene commit or shutdown.
  `reserveRecords()` now retries all coupled vector and grid reservations to a
  common target, so an earlier partial reserve cannot make later appends miss
  the prepared capacity.

## Commands

```sh
cmake --build build/009-incremental-validation --target \
  platformator_render_culling_test platformator_render_culling_index_test \
  platformator_render_culling_worker_test platformator_runtime_recreation_test -j4
ctest --test-dir build/009-incremental-validation \
  -R '^(platformator_render_culling_(test|index_test|worker_test)|platformator_runtime_recreation_test)$' \
  -j4 --output-on-failure
```

The render-culling and index tests passed after T030/T031. The worker test passed
after correcting its incremental-query expectation. The runtime-recreation
extension is included in the final US3 gate; sanitizer and broader CTest results
are tracked separately in `final-validation.md`.
