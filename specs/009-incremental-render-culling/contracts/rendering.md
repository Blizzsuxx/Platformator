# Rendering Contract: Incremental Culling

## Public compatibility

Keep `WindowSettings::enableRenderCulling`, its default, existing `--no-render-culling`, scene formats, and all current public setter signatures. Users do not add colliders or call a manual invalidation API. Keep public `Camera::render(Sprite*, SDL_Renderer*, int, int)` as direct submit-all rendering; it does not consume window visibility state. Private declarations/friends may change, requiring normal C++ consumer rebuilds; no stable binary layout is promised.

Normal enabled window rendering uses the accepted indexed strategy. Benchmark strategies are internal and selected before scene population; there is no new installed backend enum, serialized setting, virtual interface, or public runtime switching API. Existing submit-all opt-out remains distinct from current-linear culling.

## Internal owner interface

`SDLWindow` owns optional concrete `RenderCullingState`. Its narrow operations are registration, synchronous unregister, mark-geometry-changed, prepare ordered selection for the current view, and explicit scene reset. Exact C++ names can follow repository style. A notification must not reach a missing state or staged/unregistered component. Maintain record/queue/map consistency if capacity growth throws; failed preparation never leaves a valid partial draw list.

Dense slots match authoritative `spriteComponents` order. Unregister invalidates cached selection and cancels every reference before retirement. Moving the last slot repairs only that record's bounded references. No full vector pass is needed to discover dirty state or reconstruct sparse selection order. Compare handles in correctness checks; raw address reuse is not identity.

## Bound geometry

1. Sample `w = Sprite::getWidth()`, `h = getHeight()` and synchronized world center. These are already affected by GameObject scale.
2. Preserve the exact float scale factors and angle conversion/trig used by current `Camera::renderWithView`/SDL. Do not substitute GameObject's radians trig without proving equivalence.
3. Form ideal projected-then-rotated half extents in double: `ex=(abs(c)*w*sx + abs(s)*h*sy)/2`, `ey=(abs(s)*w*sx + abs(c)*h*sy)/2`. Inverse-map them per axis and center on the synchronized world position.
4. Expand outward for a certified envelope of the actual float destination/corner arithmetic **and** the existing classifier's pixel/error padding. A proxy is eligible for grid insertion only after that enclosure is certified.
5. Query the double camera rectangle with extent `outputWidth/sx`, `outputHeight/sy`, expanding computed endpoints outward as required. Query-bound arithmetic/cell conversion must also be conservative.
6. Run the existing output-space classifier on candidates before selecting them. Unclassifiable classification keeps the sprite. Selected drawing uses current SDL arguments with no second visibility decision.

## Numeric certificate and fallback

Use a deliberately bounded sufficient domain initially. The baseline certificate uses `P = 2^14` output pixels as a numeric range, not a performance threshold:

- Per sprite: finite positive dimensions/scales and finite angle/trig; `abs(worldX)*sx`, `abs(worldY)*sy`, `w*sx`, and `h*sy` bounded by P using conservative double checks.
- Per camera/view: `abs(cameraX)*sx`, `abs(cameraY)*sy`, output width and height bounded by P, with valid ordinary mapping.
- Guard actual pre-scale world subtraction against overflow over the certified camera domain; bound every subsequent float intermediate. Include absolute subnormal rounding error, or fail certification for arithmetic regimes the error model does not support. Do not enable unsafe fast-math/reassociation assumptions.
- Start with a two-output-pixel broad envelope, converted to world units as `2/sx`, `2/sy` with outward rounding. Before enabling it, document a conservative bound for the exact expression sequence showing float geometry deviation plus the existing `1 + 8*epsilon*maximumMagnitude` padding fits inside that envelope. Research indicates a loose intermediate bound near `8P` leaves substantial margin, but operation counts/error constants are **not accepted without the implementation audit**.

The certificate helper must return failure rather than assume enclosure when arithmetic/rounding/SDL-path assumptions cannot be established. Failing sprites enter the unconditional candidate list; they still receive exact current-view classification. Failing camera/view certification chooses original current-linear selection over current sprites on a cache miss; an unchanged valid fallback result may be reused. This fallback never requires refreshing all sprite bounds just because the camera translated. Instrument the regime and include large common offsets/extreme values in validation and reporting.

Widening the domain or changing the envelope requires the same derivation/oracle/image coverage, not a tuned epsilon. If a two-pixel proof fails for any admitted input, tighten the certificate or conservatively enlarge the rigorously bounded envelope and update cells/tests/docs. Rejection with an unproved margin is forbidden. This is a defined implementation proof gate, not an unresolved API decision.

## Grid and ordering

Use cell dimensions equal to the current effective world view extent, independent of camera translation. A **padded** proxy with width/height no larger than the respective cell dimension intersects at most two cells per axis. Floor-based inclusive coverage handles negative positions and edge contact. Larger proxies, unsafe coordinate conversions, and uncertifiable entries go to the side list. Retain/refine all such candidates; no clipped cell range may silently drop them.

Hash values hold intrusive list heads. Four per-record links, recycled empty-cell node handles, and capacity prepared for at most four cells per reserved record bound storage and remove per-migration allocation. Reserve actual nodes as well as buckets. Do not keep empty cells keyed by every historical position.

Queries deduplicate by record stamp, refine current geometry, and sort only retained records by authoritative window rank. Cached-linear misses traverse ranks in their existing order. Never draw hash order or scan all N ranks merely to order a sparse indexed result. A valid unchanged result needs zero query, refinement, bounds work, or order reconstruction. Drawing remains O(V).

## Invalidation and phase contract

- Notify changed positions at synchronized world-position publication, including affected descendants.
- Notify dimensions, actual texture null/non-null eligibility, scale and rotation on real changes. Cover direct physics writes by preserving the translation/rotation mask through the existing owner-thread merge and synchronous `Rigidbody::move` path.
- Index mutations and notifications happen after workers complete, on the existing owning thread. No locks, worker observers, competing pool, or concurrent erase/traversal.
- Compare camera identity/position and projection/output values once per render. Identity/translation invalidates selection; changed projection refreshes dependent bounds/grid globally and explicitly.
- Activation, replacement, unregister, swap-removal, and scene operations retain existing authoritative membership/order and retirement semantics.
- Flip/source rectangle/texture pixels remain current draw inputs even with unchanged cached selection. Do not invalidate bounds merely because those contents change.

## Fallback, inactive modes, and reset

Unsupported SDL render target/viewport/scale/logical presentation or failed mapping queries retain current submit-all behavior; do not alter consumer SDL state. Keep bounded dirty notifications pending while cached preparation is suspended. Remove references immediately on unregister. Resume by comparing projection and consuming affected changes before selection, without a gratuitous rebuild for mapping fallback alone.

No camera/no renderer, clear/present, paused rendering, debug overlays, and clipping retain existing behavior. Headless, disabled, and current-linear comparison modes have no optional cache/index allocation, per-sprite cache initialization, queue insertion, revision increments, maintenance, or queries. Small constant absent-state branches may remain at changed setters/publication points; measure them against frozen controls.

Explicit successful scene-clear/replacement resets old storage at the existing commit boundary; failed scene preparation does not disturb the live cache. Shutdown destroys all optional state. Do not force capacity release on transient empty ordinary churn. Stable warmed capacity has zero recurring visibility allocations and no growth with historical positions, retired handles, or scene count.
