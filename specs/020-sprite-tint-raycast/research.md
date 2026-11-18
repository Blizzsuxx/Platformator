# Research & Architectural Decisions: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Feature**: `020-sprite-tint-raycast`  
**Date**: 2026-11-18  
**Spec**: [spec.md](spec.md)

This document resolves all technical design choices following the KISS and performance-first engineering goals.

---

## 1. Per-Sprite RGBA Modulation (`SDL_SetTextureColorMod` + `SDL_SetTextureAlphaMod`)

- **Decision**:
  - Store `SDL_Color color{255, 255, 255, 255}` (4 bytes) on `Sprite` (`engine/include/platformator/sprite.h`). Because `Sprite` currently ends with `std::int32_t zOrder`, `bool isRegisteredInWindow`, and `size_t windowIndex`, adding 4 bytes (`SDL_Color`) fits cleanly into existing padding/tail alignment without expanding object footprint unnecessarily.
  - Pass `sprite->getColor()` into `platformator_detail::submitSpriteRender(...)` in `engine/src/spriterendergeometry.h` (invoked by `Camera::renderWithView`, which backs `SDLWindow::render()`, `Camera::render()`, and `EditorPreview::render()`).
  - Inside `submitSpriteRender`:
    - When `color.r == 255 && color.g == 255 && color.b == 255 && color.a == 255`, submit `SDL_RenderTextureRotated` directly with zero extra SDL state calls (fast path for legacy / untinted sprites).
    - Otherwise, query and save `SDL_GetTextureColorMod` (`previousR/G/B`) and `SDL_GetTextureAlphaMod` (`previousA`), apply `SDL_SetTextureColorMod(texture, color.r, color.g, color.b)` and `SDL_SetTextureAlphaMod(texture, color.a)` before `SDL_RenderTextureRotated`. If `color.a < 255`, check `SDL_GetTextureBlendMode(texture, &previousBlend)` and temporarily set `SDL_BLENDMODE_BLEND` when `previousBlend == SDL_BLENDMODE_NONE` (so RGB-only textures still alpha-fade properly). Immediately after `SDL_RenderTextureRotated`, restore the saved `previousR, previousG, previousB`, `previousA` (and `previousBlend` if promoted) so no shared `SDL_Texture*` consumer—including pre-modulated textures, subsequent untinted sprites, or ImGui editor panels—ever sees leaked state.
  - `Sprite::setColor(...)` and `Sprite::setAlpha(...)` never call `notifySpriteGeometryChanged` or `notifySpriteOrderChanged`, because color/opacity does not affect spatial bounding boxes, visibility classification, or Z-order sorting.

- **Rationale**:
  - Verified in `vendored/SDL3-3.4.18/src/render/SDL_render.c`: `PrepQueueCmdDraw` copies `texture->color` and `texture->blendMode` by value into each queued `SDL_RenderCommand` (and `QueueCmdGeometry` passes `&texture->color` directly when building vertices) at the moment `SDL_RenderTextureRotated` runs. Restoring modulation on the `SDL_Texture` right after `SDL_RenderTextureRotated` never affects the already-queued draw command, keeps consecutive draws using the same `SDL_Texture*` batchable on hardware backends, and works deterministically on `SDL_RENDER_DRIVER=software` (`PrepTextureForCopy` reads per-command color/blend state in `SDL_render_sw.c`).
  - Zero allocations, zero custom shader maintenance, zero GPU texture duplication, and zero culling or draw-order invalidation even when hundreds of sprites modulate color every frame (e.g., distance fog on raycast wall columns).

- **Alternatives considered**:
  - *Custom GPU shader pipeline*: Rejected because Platformator uses SDL3's `SDL_Renderer` 2D API (which has no portable custom user fragment shader contract across software/OpenGL/Metal/D3D11) and CI relies on `SDL_RENDER_DRIVER=software`.
  - *Duplicating `SDL_Texture` per tinted sprite*: Rejected due to linear VRAM/RAM explosion and slow texture creation on dynamic per-frame color updates.
  - *Pre-baking brightness rows into texture sheets*: Useful as a specialized asset trick, but cannot support arbitrary runtime RGB tints, damage flashes, or smooth opacity fades as an engine feature.

---

## 2. Sprite Color Serialization & Editor Authoring Contract

- **Decision**:
  - Persist sprite color in `engine/src/spriteserializer.cpp` under key `"color"` as an object `{"r": <uint8>, "g": <uint8>, "b": <uint8>, "a": <uint8>}`.
  - Emit `"color"` in `to_json` alongside `"zOrder"` and `"sourceRectEnabled"`.
  - On `from_json`, if `"color"` is omitted, reset `sprite.setColor({255, 255, 255, 255})` (preventing stale color retention when deserializing into an existing tinted `Sprite`, matching `zOrder` contract). If `"color"` is present, validate that it is a JSON object containing integer (non-boolean) channels `r`, `g`, `b`, `a` each in `[0, 255]`, throwing `std::invalid_argument` or `std::out_of_range` before mutating `sprite` on any error.
  - In the editor:
    - Add `"color": {"r": 255, "g": 255, "b": 255, "a": 255}` to `default_component_record(6)` in `editor/src/componenttypes.h`.
    - Validate optional `"color"` in `validate_sprite` and include `"color"` in `copy_validation_component` in `editor/src/validation.cpp`.
    - Cache `SDL_Color color{255, 255, 255, 255}` inside `EditorPreview::Proxy::SpriteInputs` (`editor/src/preview.h` & `editor/src/preview.cpp`). Color-only changes trigger preview re-rendering without setting `drawOrderDirty_`, reloading textures, or renewing registration sequence.
    - Expose `"Color"` in `InspectorPanel` (`editor/src/panels/inspector.cpp`) via a compact `draw_color` helper using `ImGui::ColorEdit4` (with `ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf`) plus semantic node registration (`Color`) and multi-selection support. Property edits commit the entire `{"r", "g", "b", "a"}` JSON object atomically at path `{std::string("color")}`.

- **Rationale**:
  - Committing at the single component-level path `{std::string("color")}` avoids `resolve_target` intermediate-key failures when editing legacy `.scene`/`.pttemplate` sprites that omit `"color"`, keeps undo/redo single-step even from legacy state, and works natively with `EditCommandService`, `component_operations` copy/paste/reset, and `revert_template_property` / `template_field_is_overridden`.

- **Alternatives considered**:
  - *Separate keys `colorR`, `colorG`, `colorB`, `colorA`*: Rejected because four separate scalar rows clutter the Inspector and diverge from Platformator's structured object pattern (`offset`, `sourceRect`).
  - *Editing nested `{ "color", "r" }` paths directly*: Rejected because legacy Sprite components omit `"color"` until edited, which would cause `EditCommandService::resolve_target` to fail with `"intermediate object key is missing"`.

---

## 3. Window & Runtime Clear Color Override

- **Decision**:
  - Add `SDL_Color clearColor{255, 255, 255, 255}` to `WindowSettings` (`engine/include/platformator/windowsettings.h`).
  - Forward `Runtime::getClearColor()` and `Runtime::setClearColor(...)` (`engine/include/platformator/runtime.h`, `engine/src/runtime.cpp`) to `SDLWindow::getClearColor()` and `SDLWindow::setClearColor(...)` (`engine/src/sdlwindow.h`, `engine/src/sdlwindow.cpp`), reading and mutating `windowSettings.clearColor` directly.
  - Replace the hardcoded `SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF)` in `SDLWindow::render()` with `windowSettings.clearColor.{r, g, b, a}` right before `SDL_RenderClear(renderer)`.

- **Rationale**:
  - `SDLWindow` already owns `WindowSettings windowSettings`, so zero extra state or duplicate bookkeeping is needed.
  - Clearing happens before `if (!mainCamera)`, so custom clear colors apply consistently both with and without an active main camera.
  - In headless mode (`renderer == nullptr`), `setClearColor`/`getClearColor` safely store and return `windowSettings.clearColor` without requiring SDL video initialization.
  - Keeping default `{255, 255, 255, 255}` ensures 100% visual parity for all existing games and tests.

- **Alternatives considered**:
  - *Storing clear color on `Camera`*: Rejected because clearing occurs even when `mainCamera == nullptr`, and adding a serialized field to `Camera` alters `.scene` schemas unnecessarily when a window/runtime setting covers both startup configuration and dynamic screen flashes cleanly.

---

## 4. `Grid` + `AABB` 2D Raycast Architecture (`Runtime::raycast` & `Runtime::raycastAll`)

- **Decision**:
  - Define `platformator::RaycastHit` in `engine/include/platformator/collisiontypes.h` (with `collider`, `gameObject`, `point`, `normal`, `distance`, `fraction`).
  - Expose on `platformator::Runtime`:
    - `std::optional<RaycastHit> raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance, uint64_t collisionMask = ~uint64_t{0}, bool includeTriggers = false) const;`
    - `bool raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance, RaycastHit &outHit, uint64_t collisionMask = ~uint64_t{0}, bool includeTriggers = false) const;`
    - `std::vector<RaycastHit> raycastAll(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance, uint64_t collisionMask = ~uint64_t{0}, bool includeTriggers = false) const;`
  - Implement spatial ray traversal and collider filtering in `PhysicsManager` (`engine/src/physicsmanager.{h,cpp}`), reusing `Grid`, `AABB`, and `SegmentedIntervalList` (`engine/src/grid.{h,cpp}`, `engine/src/aabb.h`, `engine/src/segmentedintervallist.h`):
    1. Validate inputs at entry (`validateAndNormalizeRay`): finite `origin`, finite `direction` with `maxAbs`-scaled normalization rejecting zero/near-zero lengths (`<= 1e-6` without overflowing `(1e30f, 0)`), and finite `maxDistance >= 0.0f` (`maxDistance == 0.0f` tests point containment at `origin`).
    2. **`Grid` + `AABB` spatial traversal on the synchronized physics world**:
       - Clip `[0, limit]` against the occupied `Grid` cell bounds (`grid.getActiveCellBounds()`) via `rayIntersectsAABBInterval` in double precision; rays missing all occupied cells terminate immediately with zero cell lookups.
       - Walk occupied cells along `[tGridStart, tEnd]` via 2D Amanatides & Woo DDA (or occupied cell AABB filtering for `> 65536`-cell extreme spans), shrinking the DDA cutoff dynamically as `limit = best.distance` tightens on closest-hit queries.
       - Inside each visited `GridCell`, compute the ray sub-segment's padded AABB over `[tCellEnter, tCellExit]` and query `cell.getAABB()->forEachOverlappingAABB(segMinX, segMaxX, segMinY, segMaxY, ...)` (`SegmentedIntervalList::forEachOverlappingInterval` binary-searches chunks in X + checks left checkpoints, then filters `yProjections`), deduplicating multi-cell colliders in a stack-allocated 64-entry array (zero heap allocations in `raycast`).
    3. **Ray-vs-AABB coarse slab reject & narrow-phase exact intersection (`intersectColliderRay`)**:
       - Filter null, retired, inactive, deleted, masked, or excluded trigger colliders using live lifecycle/filter state.
       - Read synchronized `collider->getCachedWorldPosition()`, published `BoxCollider` (`getPublishedHalfWidth()`, `getPublishedHalfHeight()`, `getPublishedXAxis()`) / `CircleCollider` (`getPublishedRadius()`) geometry updated during `Collider::prepareSyncFromCache()`, and prune candidate colliders in double precision against their synced world AABB bounds `(cMinX, cMaxX, cMinY, cMaxY)` from `AABB::forEachOverlappingAABB`.
       - Run cancellation-safe double-precision narrow-phase circle (`proj`, `perp = toCenter - proj * dir`) or rotated box OBB slab (`publishedXAxis`, `d == 0.0` exact parallel check) tests.
    4. For `raycast`, tighten `double limit = std::min(static_cast<double>(maxDistance), 0.5 * (double(best.distance) + double(std::nextafter(best.distance, INFINITY))))` so equal-float-distance candidates that round downward never get pruned before deterministic `(distance, collider->getId())` tie-breaking; for `raycastAll`, deduplicate multi-cell entries and sort by `(distance, collider->getId())` ascending.

- **Rationale**:
  - **Reuses existing broad-phase structures cleanly**: `Grid` (`grid.cells`) and each cell's `AABB` (`SegmentedIntervalList` + `BoundingRadiusProjectionAxis`) already partition colliders in space and maintain exact sorted world X/Y AABB projections, `cachedWorldPosition`, and published raycast extents/axes per synced step/reconciliation. Querying them read-only eliminates full `colliderComponents` scans, avoids dirty-transform workarounds, prunes unrelated colliders per cell via interval binary search + AABB slab checks, and skips completely empty distant segments before stepping a single cell.
  - **Synced physics-world contract & `Behavior::afterPhysicsSync`**: Raycasts reflect the synchronized physics world (published at component reconciliation, `Phase::Physics` `broadPhase()`, or `Phase::Cleanup` `commitMutations()` / `reconcilePending()`, with cleanup collision-callback writes publishing on the next sync point). `Behavior::afterPhysicsSync(double)` runs once per committed fixed step right after `commitMutations()` succeeds (keeping physics read-only and joining any parallel reader tasks before returning), alongside host `afterSimulationStep` and post-`simulateFrame*` owner-thread entry points.
  - **Zero allocations & concurrent read safety**: `raycast` allocates no heap memory, never mutates `Grid`, never increments non-atomic `positionReadVisits`, and uses atomic work-probe counters in instrumented builds, allowing safe parallel `tbb::parallel_for` ray batches on a stable scene.

- **Alternatives considered**:
  - *Contiguous linear scan over `colliderComponents`*: Rejected as primary strategy because every ray inspects every collider in the scene (`O(R * N)`), scaling poorly as scenes grow or ray batches widen.
  - *Mutating `Grid` / flushing transforms inside `raycast`*: Rejected because `const` queries (and concurrent `tbb::parallel_for` query batches) must remain strictly side-effect-free with zero shared mutable state writes.
