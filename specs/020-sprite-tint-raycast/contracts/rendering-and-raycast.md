# Interface Contracts: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Feature**: `020-sprite-tint-raycast`  
**Date**: 2026-11-18  
**Spec**: [spec.md](spec.md)

---

## 1. Sprite Color & Alpha API (`engine/include/platformator/sprite.h`)

### Public Methods
```cpp
SDL_Color getColor() const;
std::uint8_t getAlpha() const;

void setColor(const SDL_Color &color);
void setColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
void setAlpha(std::uint8_t alpha);
```

### Behavioral Contract
1. Default value for all `Sprite` constructors is `SDL_Color{255, 255, 255, 255}`.
2. `Sprite::clone()` preserves `color`.
3. Calling `setColor` or `setAlpha` never invalidates `RenderCullingState` bounds, visibility selection, or `SpriteDrawOrder`.
4. Rendering (`SDLWindow::render()`, `Camera::render()`, and `EditorPreview::render()`):
   - Default `(255, 255, 255, 255)` sprites take a zero-modulation-call fast path without querying or setting texture modulation or blend state.
   - Non-default sprites query and save the texture's prior RGB color modulation (`SDL_GetTextureColorMod`) and prior alpha modulation (`SDL_GetTextureAlphaMod`), apply the sprite's `r, g, b, a`, and—when `color.a < 255` and `SDL_GetTextureBlendMode` reports `SDL_BLENDMODE_NONE`—temporarily promote to `SDL_BLENDMODE_BLEND`.
   - Immediately after `SDL_RenderTextureRotated`, non-default submissions restore the saved prior RGB modulation, prior alpha modulation, and prior blend mode (if promoted) so shared `SDL_Texture` instances never leak modulation or alter pre-modulated texture state across sprites or UI passes.

### Serialization Schema (`engine/src/spriteserializer.cpp`)
- Serialized JSON field inside Sprite (`"type": 6`):
  ```json
  "color": { "r": 255, "g": 255, "b": 255, "a": 255 }
  ```
- Deserialization rules:
  - Missing `"color"` resets `Sprite` to `{255, 255, 255, 255}`.
  - Present `"color"` MUST be a JSON object containing keys `"r"`, `"g"`, `"b"`, `"a"`, each a non-boolean JSON integer in `[0, 255]`.
  - Any violation throws `std::invalid_argument` or `std::out_of_range` without mutating the target `Sprite`.

### Editor Contract
- `default_component_record(6)` includes `"color": {{"r", 255}, {"g", 255}, {"b", 255}, {"a", 255}}`.
- `InspectorPanel` displays `"Color"` (`ColorEdit4`, uint8 RGBA with alpha bar) and semantic node `"Color"`, supporting single-object edits, multi-object edits, undo/redo, component copy/paste/reset, duplication, and template override/revert.
- `validate_sprite` treats omitted `"color"` as valid (legacy compatibility) and reports a blocking `ValidationSeverity::Error` on field `"color"` if present and malformed or out of `[0, 255]`.
- `EditorPreview` updates the proxy sprite's color on document edits without dirtying `drawOrderDirty_`, reloading textures, or renewing registration sequence.

---

## 2. Clear Color Override API (`windowsettings.h` & `runtime.h`)

### Public Fields & Methods
In `WindowSettings` (`engine/include/platformator/windowsettings.h`):
```cpp
SDL_Color clearColor{255, 255, 255, 255};
```

In `platformator::Runtime` (`engine/include/platformator/runtime.h`):
```cpp
SDL_Color getClearColor() const;
void setClearColor(const SDL_Color &color);
void setClearColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
```

### Behavioral Contract
1. Default `clearColor` is `{255, 255, 255, 255}` (opaque white).
2. `SDLWindow::render()` clears the renderer with `clearColor.{r, g, b, a}` on every rendered frame, regardless of whether `mainCamera` is set or null.
3. Calling `setClearColor` in headless mode (`headless = true`) stores the value safely without error and performs no rendering work.

---

## 3. 2D Raycast API (`collisiontypes.h` & `runtime.h`)

### Public Data Type (`engine/include/platformator/collisiontypes.h`)
```cpp
class Collider;
class GameObject;

namespace platformator
{
struct RaycastHit
{
    Collider *collider = nullptr;
    GameObject *gameObject = nullptr;
    Eigen::Vector2f point = Eigen::Vector2f::Zero();
    Eigen::Vector2f normal = Eigen::Vector2f::Zero();
    float distance = 0.0f;
    float fraction = 0.0f;
};
} // namespace platformator
```

### Public Methods (`engine/include/platformator/runtime.h` & `engine/include/platformator/behavior.h`)
```cpp
// platformator::Runtime
std::optional<RaycastHit> raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                  float maxDistance, uint64_t collisionMask = ~uint64_t{0},
                                  bool includeTriggers = false) const;

bool raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance,
             RaycastHit &outHit, uint64_t collisionMask = ~uint64_t{0},
             bool includeTriggers = false) const;

std::vector<RaycastHit> raycastAll(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                   float maxDistance, uint64_t collisionMask = ~uint64_t{0},
                                   bool includeTriggers = false) const;

// Behavior::afterPhysicsSync
virtual void afterPhysicsSync(double timeDelta); // Optional default-no-op lifecycle hook
```

### Behavioral & Edge-Case Contract
1. **Input validation**:
   - Throws `std::invalid_argument` if `!origin.allFinite()`, `!direction.allFinite()`, `direction.squaredNorm() <= 1e-12f`, or `!std::isfinite(maxDistance) || maxDistance < 0.0f`.
   - `direction` may have any positive length; it is normalized internally while `maxDistance` is always measured in world units.
2. **Filtering**:
   - Ignores colliders whose owning `GameObject` is null, inactive (`!obj->getActive()`), or marked for deletion (`obj->getIsMarkedForDeletion()`), plus inactive or retired collider components. Lifecycle/filter checks inspect live component and `GameObject` state on candidate hits.
   - Matches collider if `(collider->getCollisionGroup() & collisionMask) != 0`. (Default `~uint64_t{0}` matches all groups, including `collisionGroup == 1` default and bitmask layers).
   - Skips `collider->getIsTrigger()` unless `includeTriggers == true`.
3. **Synchronized Physics World & Transform Timing**:
   - Raycasts query the **synchronized physics world** via `Grid` + `AABB` (`SegmentedIntervalList` X/Y projections and conservative bounds `(cMinX, cMaxX, cMinY, cMaxY)`), `collider->getCachedWorldPosition()`, and published collider geometry (`BoxCollider::getPublishedHalfWidth()`, `getPublishedHalfHeight()`, `getPublishedXAxis()`; `CircleCollider::getPublishedRadius()`), updated atomically inside `Collider::prepareSyncFromCache()` (`PhysicsManager::broadPhase()` during `Phase::Physics`, `PhysicsManager::reconcilePending()` during `Phase::Cleanup`, and idle attachment/mutation `flushIfIdle()`).
   - Handles rotated `BoxCollider` (OBB slab intersection), scaled `BoxCollider`/`CircleCollider`, and non-zero collider `offset`.
   - Queued geometry, transform, scale, rotation, or collider-resize edits made during `update()`, `fixedUpdate()`, `lateUpdate()`, or inside `onCollisionEnter` / `onCollisionExit` cleanup callbacks executing in `reconcilePending()` after its final collider flush do **not** take effect in raycasts until the next physics/reconciliation sync point (`broadPhase()` or component reconciliation).
4. **`Behavior::afterPhysicsSync(double timeDelta)` & External Post-Step Entry Points**:
   - `Behavior::afterPhysicsSync(double timeDelta)` runs once per committed fixed step in `GameManager::Phase::AfterPhysicsSync` immediately after `commitMutations()` succeeds (with `simulationStepNumber` incremented right before dispatch) and before host `afterSimulationStep` callbacks or `SDLWindow::render()`.
   - Pending, inactive, or destroyed scripts are excluded via `ScriptComponent::canDispatch()`. Exceptions thrown from `afterPhysicsSync` are captured and propagated out of the step/loop cleanly.
   - Physics state MUST remain read-only during `afterPhysicsSync`; transform or collider mutations inside `afterPhysicsSync` are unsupported.
   - Intended host/external query points with synchronized physics state: `Behavior::afterPhysicsSync`, `Runtime::run_with_owner_service(..., afterSimulationStep)`, `preRenderCallback`, and owner-thread code after `simulateFrame*` / `simulateAndRenderFrame*` returns.
5. **Origin Inside or On Boundary**:
   - When `origin` is inside or on the boundary of an eligible collider, that collider hits with `distance = 0.0f`, `fraction = 0.0f`, `point = origin`, and `normal = -normalizedDirection`.
6. **Ordering, Rounding Consistency & Allocations**:
   - `raycast` returns the closest hit ordered by `(float(distance), collider->getId())` ascending, performing **zero** heap allocations.
   - To prevent double-precision spatial pruning from discarding an equal-`float`-distance candidate that rounds downward vs `best.distance`, closest-hit pruning tightens its `double limit` to `std::min(static_cast<double>(maxDistance), 0.5 * (double(best.distance) + double(std::nextafter(best.distance, INFINITY))))` rather than `double(best.distance)`.
   - `raycastAll` returns all intersecting eligible colliders within `[0, maxDistance]`, sorted by `(distance, collider->getId())` ascending.
   - `fraction` equals `maxDistance > 0.0f ? distance / maxDistance : 0.0f`.
7. **Thread Safety**:
   - Read-only `const` query using stack-local intermediates; safe for concurrent calls across worker threads (e.g., `tbb::parallel_for`) on a stable scene while no structural, lifecycle, or transform mutations occur. Any parallel raycast tasks spawned inside `afterPhysicsSync` MUST join before `afterPhysicsSync` returns.
