# Data Model: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Feature**: `020-sprite-tint-raycast`  
**Date**: 2026-11-18

---

## 1. Sprite Color Modulation (`Sprite`)

- **Owner**: `Sprite` (`engine/include/platformator/sprite.h`, `engine/src/sprite.cpp`)
- **Fields**:
  - `SDL_Color color{255, 255, 255, 255}` — 8-bit unsigned RGBA tint and opacity quadruple (`r, g, b, a` each in `[0, 255]`).
- **Invariants**:
  - Default-constructed `Sprite` instances (all constructors) initialize `color` to `{255, 255, 255, 255}`.
  - `Sprite::clone()` copies `color` to the cloned component.
  - Modifying `color` or `alpha` does **not** invoke `notifySpriteGeometryChanged` or `notifySpriteOrderChanged` on `SDLWindow`:
    - `RenderCullingState` spatial bounds remain valid.
    - `SpriteDrawOrder` remains clean (`0` extra order sorts or candidate re-scans).
  - During rendering (`platformator_detail::submitSpriteRender`), non-default modulation queries and saves the texture's prior RGB color mod, alpha mod, and (when `a < 255` and blend mode is `SDL_BLENDMODE_NONE`) blend mode, then restores those saved values right after drawing so shared `SDL_Texture*` state never leaks across consecutive draws or editor panels (while default `(255, 255, 255, 255)` draws take a zero-modulation-call fast path).
- **Persistence & Editor State**:
  - JSON representation on Sprite (`type == 6`): `"color": {"r": <0..255>, "g": <0..255>, "b": <0..255>, "a": <0..255>}`.
  - Omitted `"color"` in legacy `.scene`/`.pttemplate` documents loads as `{255, 255, 255, 255}` (even when deserializing into an already-tinted `Sprite`).
  - Invalid `"color"` payloads (non-object, missing channel, boolean, float, string, or integer outside `[0, 255]`) fail validation cleanly and leave the target `Sprite` unmodified.
  - `EditorPreview::Proxy::SpriteInputs` stores `SDL_Color color{255, 255, 255, 255}` (or `{r, g, b, a}` fields compared in `operator==`) so document edits to `"color"` trigger preview re-rendering cleanly without marking `drawOrderDirty_`, reloading textures, or bumping registration sequence.

---

## 2. Frame Clear Color (`WindowSettings` & `SDLWindow`)

- **Owner**: `WindowSettings` (`engine/include/platformator/windowsettings.h`), held by value in `SDLWindow::windowSettings` (`engine/src/sdlwindow.h`)
- **Fields**:
  - `SDL_Color clearColor{255, 255, 255, 255}` — 8-bit unsigned RGBA backdrop clear color.
- **Invariants**:
  - Default `WindowSettings` clears to opaque white `(255, 255, 255, 255)`, preserving existing visuals across all existing scenes and tests.
  - Mutating clear color via `Runtime::setClearColor(...)` takes effect on the very next `SDLWindow::render()` call (both when `mainCamera != nullptr` and when `mainCamera == nullptr`).
  - In headless mode (`renderer == nullptr`), `clearColor` is stored and returned cleanly with zero SDL video calls.

---

## 3. 2D Raycast Query & Hit Result (`RaycastHit`)

- **Owner**: Value type returned by `platformator::Runtime::raycast` / `raycastAll` (`engine/include/platformator/collisiontypes.h`, computed by `PhysicsManager` in `engine/src/physicsmanager.{h,cpp}`)
- **Fields (`RaycastHit`)**:
  - `Collider *collider = nullptr;` — Borrowed pointer to the intersected `Collider`.
  - `GameObject *gameObject = nullptr;` — Borrowed pointer to `collider->getGameObject()`.
  - `Eigen::Vector2f point = Eigen::Vector2f::Zero();` — World-space intersection point (`origin + distance * normalizedDirection`, or `origin` when starting inside).
  - `Eigen::Vector2f normal = Eigen::Vector2f::Zero();` — Outward unit surface normal at `point` (`-normalizedDirection` when ray origin lies inside/on the collider).
  - `float distance = 0.0f;` — Distance along the unit ray in world units (`[0.0f, maxDistance]`).
  - `float fraction = 0.0f;` — Normalized distance `distance / maxDistance` (`0.0f` if `maxDistance == 0.0f`).
- **Relationships & Transform Timing**:
  - Traverses colliders through the synchronized `PhysicsManager::grid` (`Grid::getActiveCellBounds()` + 2D DDA + each visited `GridCell`'s `AABB::forEachOverlappingAABB` query over `SegmentedIntervalList` X/Y projections).
  - Excludes colliders whose owning `GameObject` is null, inactive (`!getActive()`), or marked for deletion (`getIsMarkedForDeletion()`), plus inactive/retired components and filtered masks/triggers via live lifecycle/filter inspection.
  - Reads synchronized `collider->getCachedWorldPosition()`, published `BoxCollider` (`getPublishedHalfWidth()`, `getPublishedHalfHeight()`, `getPublishedXAxis()`) and `CircleCollider` (`getPublishedRadius()`) geometry updated together in `Collider::prepareSyncFromCache()`, and the cell `AABB` projections (`cMinX..cMaxY`), keeping `Grid` state read-only and concurrent-query safe without touching non-atomic read counters.
  - Edits queued before a sync point or inside cleanup collision callbacks after `reconcilePending()`'s final flush take effect in raycasts on the next sync (`Phase::Physics` `broadPhase()`, `Phase::Cleanup` `reconcilePending()`, or idle reconciliation), matching `Behavior::afterPhysicsSync(double)` and post-step caller timing.
- **Ordering & Tie-Breaking**:
  - Closest-hit (`raycast`) and multi-hit (`raycastAll`) order hits by `(distance, collider->getId())` ascending, using a float-rounding-safe `double` cutoff (`min(maxDistance, midpoint(best, nextafter(best, +inf)))`) to ensure equal-float-distance candidates with lower `collider->getId()` are never pruned prematurely independent of internal collider swap-remove storage order.
