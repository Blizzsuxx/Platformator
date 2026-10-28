> Historical pre-convergence record. Current implementation, qualification and completion evidence are in [convergence/IMPLEMENTATION.md](convergence/IMPLEMENTATION.md), [final-validation.md](final-validation.md) and [RESULTS.md](RESULTS.md). Earlier raw data/source identities remain unchanged.

# Render lifecycle validation

`platformator_render_culling_lifecycle_test` passed under the software renderer. The generated [lifecycle CSV](render-lifecycle.csv) contains candidate, visible, submitted, proxy, query, maintenance, revision, order-hash, cache, and fallback fields for each checkpoint.

The run covers all eight physics/render selections. Parent/reparent, `LateUpdate`, camera pan/jump/return/aspect changes, activation, Sprite removal/replacement, GameObject destruction, camera removal/replacement, and a paused zero-step frame run with one and six allowed workers. The same selections also run headless checks. For each selected render method, the test sets and restores SDL scale, viewport, target, and logical presentation; every custom state submits all sprites with a reason, and restoration resumes indexed visibility. The test confirms that these render operations create no gameplay contact pairs.

The `none` mode remains the direct sprite-vector path and reports visibility as unknown. Selected methods report post-refinement guarded-bounds counts when every sprite is representable; a one-pixel raster guard can retain a sprite just outside the mathematical camera rectangle. When an individual sprite cannot be safely indexed, the frame remains conservative and reports a hybrid fallback. No-camera and headless frames report no visibility query or maintenance work.

The segmented renderer reuses the private segmented spatial context with 200-unit cells and the existing 32-endpoint interval chunks. Visibility endpoints are camera/Sprite records only; the pair policy rejects Sprite/Sprite candidates. The Dynamic BVH path uses the shared tree adapter, exact bound refinement, and an ordered uncullable path. Queries and SDL submissions remain sequential.

This validates software rendering and lifecycle behavior. Hardware renderer behavior remains unverified.
