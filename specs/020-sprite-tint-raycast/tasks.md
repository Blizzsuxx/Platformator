---

description: "Task list template for feature implementation"
---

# Tasks: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Input**: Design documents from `/specs/020-sprite-tint-raycast/`

**Prerequisites**: `plan.md` (required), `spec.md` (required for user stories), `research.md`, `data-model.md`, `contracts/rendering-and-raycast.md`, `quickstart.md`

**Organization**: Tasks are grouped by user story (`US1`, `US2`, `US3`) to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (`[US1]`, `[US2]`, `[US3]`; Setup, Foundational, and Polish phases omit story labels)
- Every task specifies exact file paths

## Path Conventions

- Public C++ API headers: `engine/include/platformator/`
- Core runtime & rendering/physics implementation: `engine/src/`
- Engine tests & CMake build: `engine/tests/`, `CMakeLists.txt`, `CMakePresets.json`
- Native scene editor & tests: `editor/src/`, `editor/tests/`
- Documentation: `docs/`

---

## Phase 1: Setup (Baseline Verification)

**Purpose**: Establish clean pre-change build/test baseline before modifying engine or editor files.

- [X] T001 Configure and build the `test` preset baseline (`cmake --preset test && cmake --build --preset test --parallel 4`) using `CMakePresets.json` and `CMakeLists.txt`

---

## Phase 2: Foundational (Public Data Types & Window Settings)

**Purpose**: Define shared public structs and configuration fields that block downstream runtime, window, and raycast tasks.

**⚠️ CRITICAL**: No user story work on `US2` (`T013`+) or `US3` (`T016`+) can begin until its foundational struct is in place. (`US1` depends only on `T001`.)

- [X] T002 [P] Add `SDL_Color clearColor{255, 255, 255, 255}` ("8-bit unsigned RGBA backdrop clear color", defaulting to opaque white `(255, 255, 255, 255)`) to `WindowSettings` in `engine/include/platformator/windowsettings.h`
- [X] T003 [P] Define `platformator::RaycastHit` (`Collider *collider = nullptr;`, `GameObject *gameObject = nullptr;`, `Eigen::Vector2f point = Eigen::Vector2f::Zero();`, `Eigen::Vector2f normal = Eigen::Vector2f::Zero();`, `float distance = 0.0f;` in `[0.0f, maxDistance]`, `float fraction = 0.0f;` as `distance / maxDistance` or `0.0f` if `maxDistance == 0.0f`) in `engine/include/platformator/collisiontypes.h`

**Checkpoint**: Foundational public types and settings fields are ready — user story implementation can proceed in parallel (`US1`, `US2`, `US3`).

---

## Phase 3: User Story 1 - Tint and fade individual sprites independently (Priority: P1) 🎯 MVP

**Goal**: Enable per-sprite RGBA color tinting and alpha opacity across runtime rendering, scene/template persistence, and editor authoring without invalidating spatial culling bounds or sprite Z draw order.

**Independent Test**: Run `ctest --test-dir build/test -R "platformator_sprite_tinting_test|platformator_scene_io_contract_test|platformator_runtime_template_test|platformator_editor_(inspector_interaction|diagnostics|component_operations|template|preview)_test" --output-on-failure`.

### Implementation & Verification for User Story 1

- [X] T004 [P] [US1] Add `SDL_Color color{255, 255, 255, 255}` ("8-bit unsigned RGBA tint and opacity quadruple (`r, g, b, a` each in `[0, 255]`)") and `getColor()`, `getAlpha()`, `setColor(const SDL_Color &)`, `setColor(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t = 255)`, `setAlpha(std::uint8_t)` to `Sprite` (preserving `color` in `Sprite::clone()` without calling `notifySpriteGeometryChanged` or `notifySpriteOrderChanged`) in `engine/include/platformator/sprite.h` and `engine/src/sprite.cpp`
- [X] T005 [P] [US1] Apply per-sprite `SDL_Color` modulation in `platformator_detail::submitSpriteRender` (fast-pathing `(255, 255, 255, 255)`, applying `SDL_SetTextureColorMod` and `SDL_SetTextureAlphaMod`, temporarily promoting `SDL_BLENDMODE_NONE` to `SDL_BLENDMODE_BLEND` when `color.a < 255`, and restoring `(255, 255, 255, 255)` + original blend mode afterward) and pass `sprite->getColor()` in `Camera::renderWithView` in `engine/src/spriterendergeometry.h` and `engine/src/camera.cpp` (depends on T004)
- [X] T006 [US1] Serialize `"color": {"r": <0..255>, "g": <0..255>, "b": <0..255>, "a": <0..255>}` in `to_json` and deserialize in `from_json` (where "Omitted `\"color\"` in legacy `.scene`/`.pttemplate` documents loads as `{255, 255, 255, 255}` (even when deserializing into an already-tinted `Sprite`)" and "Invalid `\"color\"` payloads (non-object, missing channel, boolean, float, string, or integer outside `[0, 255]`) fail validation cleanly and leave the target `Sprite` unmodified") in `engine/src/spriteserializer.cpp` (depends on T004)
- [X] T007 [P] [US1] Add `"color": {{"r", 255}, {"g", 255}, {"b", 255}, {"a", 255}}` to `default_component_record(6)` in `editor/src/componenttypes.h` and validate optional `"color"` (reporting blocking `ValidationSeverity::Error` on field `"color"` for non-object, missing channel, boolean, float, string, or integer outside `[0, 255]`, and copying `"color"` in `copy_validation_component`) in `editor/src/validation.cpp`
- [X] T008 [P] [US1] Cache `SDL_Color color{255, 255, 255, 255}` in `EditorPreview::Proxy::SpriteInputs` and apply `proxy.sprite->setColor(...)` during reconciliation without setting `drawOrderDirty_`, reloading textures, or bumping `registrationSequence` in `editor/src/preview.h` and `editor/src/preview.cpp` (depends on T004)
- [X] T009 [US1] Add single-selection and multi-selection `"Color"` (`ColorEdit4` uint8 RGBA with alpha bar) editing at path `{std::string("color")}`, template override/revert integration, and `"Color"` semantic node discovery for `Sprite` (`type == 6`) in `editor/src/panels/inspector.cpp` (depends on T007)
- [X] T010 [P] [US1] Add engine rendering and culling-invariance test `engine/tests/sprite_tinting_test.cpp` (verifying shared-texture RGB/alpha isolation, RGB24 alpha blend promotion, Z-order alpha compositing, and `0` culling bounds refreshes / visibility re-scans / draw-order sorts on color-only edits) and register `platformator_sprite_tinting_test` in `engine/tests/CMakeLists.txt` (depends on T005)
- [X] T011 [P] [US1] Extend `engine/tests/scene_io_contract_test.cpp` and `engine/tests/runtime_template_test.cpp` to verify `"color"` round-trip persistence, omitted-field reset to `{255, 255, 255, 255}`, invalid `"color"` rejection atomicity, `Sprite::clone()`, and template instantiation color retention (depends on T006)
- [X] T012 [US1] Extend `editor/tests/inspector_interaction_test.cpp`, `editor/tests/diagnostics_test.cpp`, `editor/tests/component_operations_test.cpp`, `editor/tests/templates_test.cpp`, and `editor/tests/preview_test.cpp` to verify Sprite `"color"` Inspector single/multi-edit + undo/redo, validation diagnostics, copy/paste/reset, template override/revert/duplication, and preview rendering (depends on T007, T008, T009)

**Checkpoint**: User Story 1 is complete and independently verifiable across engine rendering, serialization, culling work probes, and editor workflows.

---

## Phase 4: User Story 2 - Query 2D line-of-sight and hitscan rays against colliders (Priority: P1)

**Goal**: Provide fast, allocation-free closest-hit (`raycast`) and sorted multi-hit (`raycastAll`) 2D ray queries against active box and circle colliders using immediate world transforms.

**Independent Test**: Run `ctest --test-dir build/test -R "platformator_raycast_test" --output-on-failure`.

### Implementation & Verification for User Story 2

- [X] T013 [US2] Implement `PhysicsManager::raycast` and `PhysicsManager::raycastAll` (validating finite `origin`, finite non-zero `direction` with `direction.squaredNorm() > 1e-12f`, and finite `maxDistance >= 0.0f`; filtering inactive/deleted objects, `(collider->getCollisionGroup() & collisionMask) == 0`, and excluded triggers; applying bounding-circle coarse pruning with shrinking closest-hit bound; computing exact analytical 2D circle and oriented-box slab intersections on immediate world transforms; returning `distance = 0.0f`, `fraction = 0.0f`, `point = origin`, `normal = -normalizedDirection` when starting inside; and tie-breaking by `(distance, collider->getId())` ascending with zero heap allocations on `raycast`) in `engine/src/physicsmanager.h` and `engine/src/physicsmanager.cpp` (depends on T003)
- [X] T014 [US2] Declare and forward `Runtime::raycast` (`std::optional<RaycastHit>` and `bool(..., RaycastHit &outHit, ...)` overloads) and `Runtime::raycastAll` (`std::vector<RaycastHit>`) in `engine/include/platformator/runtime.h` and `engine/src/runtime.cpp` (depends on T013)
- [X] T015 [US2] Add comprehensive 2D raycast test suite `engine/tests/raycast_test.cpp` (covering axis-aligned/rotated/scaled/offset `BoxCollider` and `CircleCollider` accuracy within `1e-4`, immediate same-frame transform updates before `simulateFrame`, collision mask & trigger filtering, inactive/deleted object exclusion, inside-collider origin handling, deterministic tie-breaking, `raycastAll` sorting, invalid-argument exceptions, and zero-heap-allocation verification on `raycast`) and register `platformator_raycast_test` in `engine/tests/CMakeLists.txt` (depends on T014)

**Checkpoint**: User Story 2 is complete and independently verifiable via `platformator_raycast_test`.

---

## Phase 5: User Story 3 - Override the frame background clear color (Priority: P2)

**Goal**: Allow configuring the initial window background clear color via `WindowSettings` and updating it dynamically at runtime via `Runtime::setClearColor`.

**Independent Test**: Run `ctest --test-dir build/test -R "platformator_sprite_tinting_test|platformator_runner_failure_test" --output-on-failure`.

### Implementation & Verification for User Story 3

- [X] T016 [US3] Add `getClearColor()` and `setClearColor(...)` to `SDLWindow` and replace hardcoded `SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF)` in `SDLWindow::render()` with `windowSettings.clearColor.{r, g, b, a}` in `engine/src/sdlwindow.h` and `engine/src/sdlwindow.cpp` (depends on T002)
- [X] T017 [US3] Declare and forward `Runtime::getClearColor()`, `Runtime::setClearColor(const SDL_Color &)`, and `Runtime::setClearColor(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t = 255)` in `engine/include/platformator/runtime.h` and `engine/src/runtime.cpp` (depends on T016)
- [X] T018 [US3] Add clear color verification cases (default `(255, 255, 255, 255)` backdrop, startup `WindowSettings::clearColor`, dynamic `Runtime::setClearColor` with and without a main camera, culling-cache invariance, and headless `headless = true` storage no-op) to `engine/tests/sprite_tinting_test.cpp` (depends on T010, T017)

**Checkpoint**: All three user stories (`US1`, `US2`, `US3`) work independently and preserve existing defaults.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Documentation alignment and full end-to-end validation across all engine, editor, and packaging targets.

- [X] T019 [P] Document `Sprite` color/alpha modulation, `WindowSettings::clearColor` / `Runtime::setClearColor`, and `Runtime::raycast` / `Runtime::raycastAll` contracts in `docs/CPP_API_MIGRATION.md`
- [X] T020 Run full validation per `specs/020-sprite-tint-raycast/quickstart.md` (`ctest --preset test --output-on-failure` and `ctest --test-dir build/test -R platformator_package_smoke --output-on-failure`) across `engine/tests/` and `editor/tests/` (depends on T001–T019)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1: T001)**: No dependencies — verifies baseline build/test health first.
- **Foundational (Phase 2: T002–T003)**: Can proceed in parallel after T001; blocks `US2` (`T013`) and `US3` (`T016`).
- **User Stories (Phase 3–5)**:
  - **User Story 1 (P1: T004–T012)**: Depends only on T001; independent of `US2` and `US3`.
  - **User Story 2 (P1: T013–T015)**: Depends on T003 (`RaycastHit`); independent of `US1` and `US3`.
  - **User Story 3 (P2: T016–T018)**: Core implementation (`T016–T017`) depends on T002 (`WindowSettings::clearColor`); `T018` appends clear-color assertions to `engine/tests/sprite_tinting_test.cpp` after T010.
- **Polish (Phase 6: T019–T020)**: `T019` can be written in parallel once public signatures stabilize; `T020` runs final verification after all tasks complete.

### File Serialization Notes (Same-File Coordination)

- `engine/include/platformator/runtime.h` and `engine/src/runtime.cpp` are edited by `T014` (`US2`) and `T017` (`US3`); run `T014` and `T017` sequentially when modifying `runtime.{h,cpp}`.
- `engine/tests/CMakeLists.txt` is edited by `T010` (`US1`) and `T015` (`US2`); `engine/tests/sprite_tinting_test.cpp` is created by `T010` (`US1`) and extended by `T018` (`US3`).

### Parallel Opportunities

- **Foundational**: `T002` (`windowsettings.h`) and `T003` (`collisiontypes.h`) touch different headers and run in parallel.
- **Across User Stories (after Phase 2)**: `T004` (`Sprite`), `T007` (`editor/src/componenttypes.h` + `validation.cpp`), `T013` (`PhysicsManager` raycast), and `T016` (`SDLWindow` clearColor) touch disjoint files and can all proceed simultaneously.
- **Within US1**: Once `T004` lands, `T005` (`spriterendergeometry.h`/`camera.cpp`), `T006` (`spriteserializer.cpp`), and `T008` (`editor/src/preview.{h,cpp}`) can proceed in parallel; once `T005`/`T006` land, `T010` (`sprite_tinting_test.cpp`) and `T011` (`scene_io_contract_test.cpp` + `runtime_template_test.cpp`) can proceed in parallel.

---

## Parallel Example: Foundational & Multi-Story Kickoff

```text
# After T001 (baseline verified), launch T002, T003, T004, T007 in parallel:
- T002: WindowSettings::clearColor in engine/include/platformator/windowsettings.h
- T003: platformator::RaycastHit in engine/include/platformator/collisiontypes.h
- T004: Sprite RGBA color/alpha API in engine/include/platformator/sprite.h + engine/src/sprite.cpp
- T007: Editor Sprite default + validation in editor/src/componenttypes.h + editor/src/validation.cpp

# Once T002, T003, T004 complete, parallelize across US1, US2, US3:
- T005 [US1]: submitSpriteRender + Camera::renderWithView modulation
- T006 [US1]: spriteserializer.cpp JSON persistence
- T008 [US1]: EditorPreview SpriteInputs color caching
- T013 [US2]: PhysicsManager::raycast / raycastAll
- T016 [US3]: SDLWindow::clearColor render clear
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete **Phase 1 (`T001`)** and **Phase 2 (`T002–T003`)**.
2. Complete **Phase 3: User Story 1 (`T004–T012`)** — delivers full per-sprite RGB/alpha tinting across engine rendering, scene/template serialization, and editor workflows.
3. **STOP and VALIDATE**: Run User Story 1's independent test command before expanding to `US2` / `US3`.

### Incremental Delivery

1. **Setup + Foundational (`T001–T003`)** → Shared headers ready.
2. **User Story 1 (`T004–T012`)** → Per-sprite tinting & alpha blending live and verified.
3. **User Story 2 (`T013–T015`)** → Fast 2D raycasting live and verified.
4. **User Story 3 (`T016–T018`)** → Custom window/runtime clear color live and verified.
5. **Polish (`T019–T020`)** → Migration docs updated and full `ctest --preset test` + `platformator_package_smoke` verified.

## Phase 7: Convergence

- [X] T021 CRITICAL Make concurrent raycast queries safe on a stable scene in instrumented builds. The `Collider::getWorldPosition()` path in `engine/src/collider.cpp` reaches the non-atomic `positionReadVisits` counter via `engine/src/gameobject.cpp`. Keep position reads free of shared writes during raycasts or make the instrumentation safe, document that scene and transform mutation must stay outside concurrent queries in `engine/include/platformator/runtime.h`, and compare serial and concurrent query results in `engine/tests/raycast_test.cpp` per Constitution II and VII and the plan’s concurrent read decision (contradicts)
- [X] T022 CRITICAL Establish comparable performance evidence for the feature’s hot paths. Measure pre-feature versus default and modulated sprite draws using the default pilot in `engine/benchmarks/capture_linear_render.py`. Compare ray candidate work and timing against the existing contiguous scan on representative small and large scenes and repeated query batches. Record workload and build identities, timing samples, and limitations under the repository’s compact artifact policy per Constitution VI and the plan’s performance and scale decisions (missing)
- [X] T023 CRITICAL Remove the unused `Proxy::cachedColor` state in `editor/src/preview.h` and `editor/src/preview.cpp`, using the proxy Sprite’s existing color as the applied value. Verify in `editor/tests/preview_test.cpp` that normal reconciliation refreshes preview pixels without a manual cache invalidation, renewed texture work, or a draw-order rebuild per Constitution IV and T008 (contradicts)
- [X] T024 Reconcile the plan’s contiguous scan with the spatial traversal requirement. Implement or reuse a bounded traversal in `engine/src/physicsmanager.cpp` that skips distant collider records where applicable, without querying stale physics placements. Preserve same-frame transforms, filters, deterministic ordering, and allocation-free valid closest-hit queries. Add a work-count regression and same-frame movement case in `engine/tests/raycast_test.cpp` per FR-011, SC-005, and T013 (partial)
- [X] T025 Normalize finite ray directions in `engine/src/physicsmanager.cpp` without allowing squared-length overflow to collapse the direction to zero. Preserve the near-zero rejection threshold and test equivalence of a unit direction and a large finite multiple such as `(1e30f, 0)` for closest-hit, all-hits, and inside-origin normals in `engine/tests/raycast_test.cpp` per FR-007, FR-010, and T013 (partial)
- [X] T026 Make the bounding-circle reject and exact circle and box intersections numerically conservative in `engine/src/physicsmanager.cpp`. Verify in `engine/tests/raycast_test.cpp` that a radius-one circle 8192 units away returns its entry hit and unit normal, a nearby ray beyond the radius misses, and nonzero near-parallel components can enter a box slab within the ray segment. Preserve inclusive endpoints, distance limits, and the documented zero-distance behavior per FR-008, FR-009, SC-004, US2/AC1–AC3, and T015 (partial)
- [X] T027 Preserve caller-supplied SDL texture tint and opacity across non-default Sprite draws in `engine/src/spriterendergeometry.h`. Reconcile the existing default-white restoration rule with shared texture compatibility, retain the no-SDL-modulation-call path for default Sprites, and test a pre-modulated texture shared by a tinted Sprite and a legacy default Sprite in `engine/tests/sprite_tinting_test.cpp` per FR-002, FR-012, US1/AC1, and T005 (partial)
- [X] T028 Provide an undoable way to repair malformed Sprite color to valid RGBA in the Inspector, including opaque white and a multi-selection containing malformed values, without resetting other Sprite fields. Exercise repair and undo/redo in `editor/tests/inspector_interaction_test.cpp` and retain blocking validation in `editor/src/validation.cpp` per FR-003, US1/AC4, T009, and the plan’s invalid-repair coverage (partial)
- [X] T029 Exercise shared-texture tint and opacity through cached visibility, culling opt-out, conservative fallback, and direct `Camera::render` in `engine/tests/sprite_tinting_test.cpp` and the existing `engine/tests/render_culling_test.cpp` fixtures. On a warm cached view, assert no view-query rebuilds or candidate visits alongside the existing bounds and draw-order checks per FR-002, SC-001, SC-003, and T010 (partial)
- [X] T030 Verify clear color on an alpha-capable SDL render target in `engine/tests/sprite_tinting_test.cpp`. Assert all four rendered channels for the white default and for non-opaque startup and runtime colors, including next-frame updates with and without a main camera per FR-006, US3/AC1–AC2, and T018 (partial)
- [X] T031 Check consumers of the global `RaycastHit` alias in `engine/include/platformator/collisiontypes.h`. Remove the alias if unused or document the compatibility reason for keeping it, and verify standalone public headers and the package consumer per T003 and the plan’s namespaced public API (unrequested)

## Phase 8: Convergence

- [X] T032 CRITICAL Publish the exact raycast geometry with the center and conservative bounds at the existing collider sync in `engine/src/collider.cpp`, `engine/src/boxcollider.cpp`, and `engine/src/circlecollider.cpp`. Have both query forms in `engine/src/physicsmanager.cpp` use the same published radius, box orientation and extents, and spatial bounds. Reuse existing synchronized geometry where it preserves precision. Keep query-time reads allocation-free for closest hits and safe for concurrent batches, preserve lifecycle filtering, and test queued resize, rotation, scale, offset, and movement before and after publication in `engine/tests/raycast_test.cpp` per Constitution II and VII, FR-008, SC-004, and T024 (contradicts)
- [X] T033 CRITICAL Define and document the user-approved synchronized raycast contract in `specs/020-sprite-tint-raycast/spec.md`, `plan.md`, the associated contract, data model, research, and quickstart, `engine/include/platformator/runtime.h`, and `docs/CPP_API_MIGRATION.md`. Explain when ordinary behavior edits become visible, distinguish the normal collision and end-of-step sync points from `afterPhysicsSync`, and specify that geometry writes queued by a cleanup collision callback after the final flush become visible at the next sync. Cover that case in `engine/tests/contact_events_test.cpp` and keep the existing enter/exit phase and ordering. Document physics writes as unsupported in the new hook, parallel readers joining before it returns, and the existing post-step entry points for external callers per Constitution V and VII, FR-008, T019, T024, and the requested callback contract (contradicts)
- [X] T034 CRITICAL Complete the scoped performance evidence in `specs/020-sprite-tint-raycast/evidence/performance-summary.md`. Link retained commands and raw timing samples for the raycast and direct-tint comparisons where available and identify summary-only observations as historical where raw data is unavailable. For the synchronized raycast and hook, use a focused, budgeted complete-step comparison that includes moving colliders, grid-edge churn, and the no-override case, alongside the existing allocation and candidate-count checks. Record workload, build, worker counts, measured samples, and limitations under `AGENTS.md` per Constitution VI and T022 (partial)
- [X] T035 Add the optional default-no-op `Behavior::afterPhysicsSync(double dt)` callback in `engine/include/platformator/behavior.h` and `engine/src/behavior.cpp`. Dispatch it through the existing ScriptComponent lifecycle checks in `engine/src/scriptcomponent.cpp` and a distinct GameManager phase in `engine/src/gamemanager.h` and `engine/src/gamemanager.cpp`. Run it once per successful fixed step after `commitMutations()` and before host post-step callbacks or rendering, through the common interactive, explicit, and headless step path. Keep the existing grid flush and collision dispatch points. Reuse deterministic behavior ordering and keep pending, inactive, and retired behaviors out of dispatch. Define callback error handling for an already committed step, document that callers keep physics read-only during dispatch, and cover ordering, step count, lifecycle, and raycasts of published physics movement and script edits in `engine/tests/runtime_step_test.cpp` and `engine/tests/raycast_test.cpp` per US2, FR-008, FR-012, and the requested post-sync behavior hook (missing)
- [X] T036 Keep the closest-hit pruning cutoff consistent with the documented distance and collider-ID ordering in `engine/src/physicsmanager.cpp`. Prevent conversion to the public float distance from discarding an equal-distance candidate, preserve the caller's maximum-distance bound, and verify that both candidate orders give the same closest hit as the first all-hits result in `engine/tests/raycast_test.cpp` using equal analytically known hits whose distance rounds down per FR-009, SC-005, T024, and T026 (partial)
- [X] T037 Update `docs/CPP_API_MIGRATION.md` and the feature rendering contract to describe restoring the texture's prior RGB, alpha, and blend state after a modulated draw, with the existing no-modulation-call path for default sprites, per FR-002, T019, and T027 (partial)

## Phase 9: Convergence

- [X] T038 CRITICAL Make grid ray traversal in `engine/src/physicsmanager.cpp` conservative at simultaneous cell-boundary crossings. Both closest-hit and all-hits must find a collider whose only intersection is the shared grid corner, regardless of cast direction. Preserve bounded cell traversal, closest-hit pruning, deduplication, and allocation-free closest hits. Add a regression in `engine/tests/raycast_test.cpp` for a box spanning `[200,240] x [200,240]` with opposing rays along `x + y = 400`, including a clipped ray endpoint at the boundary and casts across negative grid coordinates per Constitution III and VII, FR-011, US2/AC3–AC4, and T024 (contradicts)
- [X] T039 CRITICAL Correct the external raycast timing guidance in `engine/include/platformator/runtime.h`, `docs/CPP_API_MIGRATION.md`, and `specs/020-sprite-tint-raycast/contracts/rendering-and-raycast.md`. Document the custom callback in both explicit frame APIs as the pre-physics step hook and rename its misleading parameter where useful. Preserve its existing invocation order in `engine/src/runtime.cpp` and `engine/src/gamemanager.cpp`. Identify `Behavior::afterPhysicsSync`, the owner-service `afterSimulationStep` callback, and queries after explicit stepping returns as the post-sync entry points. Check the existing callback-order regression in `engine/tests/runtime_step_test.cpp` per Constitution V, FR-008, FR-012, and T033 (contradicts)
- [X] T040 Fix oblique tangent ray-circle intersection in `engine/src/physicsmanager.cpp` so the exact test uses a consistent direction length and preserves contact at tangency within the declared tolerance, including at the caller's maximum distance. Compute the surface normal before rounding the public hit point. Add analytical tests in `engine/tests/raycast_test.cpp` for direction `(3,4)` from the origin against radius-5 circles centered at `(26,43)` and `(34,37)`, each tangent at point `(30,40)` and distance `50`. Verify both query forms, the inclusive distance limit, and nearby misses per FR-008, FR-009, SC-004, US2/AC1–AC3, and T026 (partial)
