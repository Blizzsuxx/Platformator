# Quickstart Validation Guide: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Feature**: `020-sprite-tint-raycast`  
**Date**: 2026-11-18  
**Contracts**: [rendering-and-raycast.md](contracts/rendering-and-raycast.md) | **Data Model**: [data-model.md](data-model.md)

---

## 1. Configure & Build the Test Suite

Configure and build both engine and editor tests using the repository's `test` preset:

```sh
cmake --preset test
cmake --build --preset test --parallel 4
```

Run the full regression test suite (uses SDL dummy video/audio and software renderer for rendering and editor UI tests):

```sh
ctest --preset test --output-on-failure
```

---

## 2. Targeted Feature Verification Scenarios

### A. Sprite Color Tinting, Opacity & Render-Culling Isolation
- Run the engine sprite tint / clear color / ordering / culling tests:
  ```sh
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    ctest --test-dir build/test -R "platformator_sprite_tinting_test|platformator_sprite_ordering_test|platformator_render_culling" --output-on-failure
  ```
- **Expected outcomes**:
  - Shared-texture sprites render independently with distinct RGB tints and alpha opacity without leaking modulation onto subsequent sprites.
  - Translucent (`alpha < 255`) RGB textures blend smoothly against background sprites and the clear color.
  - Editing `Sprite::setColor` / `setAlpha` on a warmed scene produces `0` culling bounds refreshes, `0` visibility re-scans, and `0` draw-order sorts.
  - Default `clearColor` renders `(255, 255, 255, 255)`, while `WindowSettings::clearColor` and `Runtime::setClearColor` alter rendered frame pixels immediately (with and without a main camera).

### B. Serialization & Template Round-Trip
- Run scene I/O and template contract tests:
  ```sh
  ctest --test-dir build/test -R "platformator_scene_io_contract_test|platformator_runtime_template_test|platformator_scene_script_roundtrip_test" --output-on-failure
  ```
- **Expected outcomes**:
  - Legacy scenes/templates omitting `"color"` reset sprites to `{255, 255, 255, 255}`.
  - Authored `"color"` objects round-trip through scene save/load, template instantiation, and `Sprite::clone()`.
  - Out-of-range (`< 0`, `> 255`), boolean, float, or malformed `"color"` values fail deserialization without mutating existing sprites.

### C. 2D Raycasting Geometry, Synchronization & Filtering
- Run raycast, runtime-step, and contact-event integration tests:
  ```sh
  ctest --test-dir build/test -R "platformator_raycast_test|platformator_runtime_step_test|platformator_contact_events_test" --output-on-failure
  ```
- **Expected outcomes**:
  - Exact hit point, outward unit normal, distance, and fraction match analytical expectations within `1e-4` across axis-aligned, rotated, scaled, and offset `BoxCollider`s and `CircleCollider`s.
  - Published raycast geometry synchronizes deterministically with `cachedWorldPosition` and `Grid` + `AABB` bounds (queued resizes/rotations/scales/moves before sync stay isolated; `Behavior::afterPhysicsSync` sees post-`commitMutations` synced state; cleanup collision-callback writes publish on the next sync point).
  - Inactive objects, excluded triggers, and non-matching collision masks are filtered out.
  - Inside-collider origin hits at distance `0.0`, equal-float-distance rounding ties respect lowest `collider->getId()` regardless of storage order, zero/non-finite/negative inputs throw `std::invalid_argument`, `raycastAll` returns sorted results, and `raycast` executes with zero heap allocations.

### D. Editor Authoring, Validation, Copy/Reset, Templates & Preview
- Run editor tests:
  ```sh
  ctest --test-dir build/test -R "platformator_editor_(inspector_interaction|diagnostics|component_operations|template|preview|document)_test" --output-on-failure
  ```
- **Expected outcomes**:
  - Inspector edits RGBA color cleanly (single & multi-select) with undo/redo, semantic discovery, component copy/paste/reset, and template override/revert.
  - Invalid `"color"` JSON triggers blocking `ValidationSeverity::Error`; omitted `"color"` passes cleanly.
  - `EditorPreview` reflects `"color"` updates without dirtying draw order or reloading textures.

### E. Public Header Hygiene & Relocated Consumer Check
- Verify public headers compile standalone (`platformator_public_headers` check in `cmake/PlatformatorPublicHeaders.cmake`) and run the package smoke test:
  ```sh
  ctest --test-dir build/test -R "platformator_package_smoke" --output-on-failure
  ```
