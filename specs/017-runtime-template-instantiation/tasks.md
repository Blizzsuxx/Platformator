---

description: "Task list for Runtime Template Instantiation implementation"
---

# Tasks: Runtime Template Instantiation

**Input**: Design documents from `/specs/017-runtime-template-instantiation/`  
**Prerequisites**: `plan.md` (required), `spec.md` (required), `research.md`, `data-model.md`, `contracts/runtime-template-api.md`, `quickstart.md`

**Tests**: Validation scenarios from `quickstart.md` are included per user story so each increment can be verified independently under CTest (`debug` and `editor-debug` presets).

**Organization**: Tasks are grouped by user story (`US1`..`US4`) in priority order. Keep code **KISS**: validate external/public boundaries, assert established internal invariants, and avoid unnecessary internal null checks or duplicate code paths.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: User story label (`[US1]`, `[US2]`, `[US3]`, `[US4]`) — required ONLY for user-story phases
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Declarations & Build Wiring)

**Purpose**: Wire new runtime header/source/test files and core template type metadata so runtime and editor modules share one contract.

- [X] T001 Declare `class Template;`, `AssetTargetName<Template>` (`"Template"`), `TemplateAssetRef` alias, `Runtime::loadTemplate(...)`, and `ReferenceResolver::loadTemplate(...)` across `include/platformator/scriptmetadata.h`, `include/platformator/assetreference.h`, `include/platformator/referenceresolver.h`, and `include/platformator/runtime.h`
- [X] T002 [P] Register `src/template.cpp` in the `platformator` library target and register `platformator_runtime_template_test` (`tests/runtime_template_test.cpp`) in `CMakeLists.txt`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core runtime cloning, shared JSON subtree loading, and `GameManager` template asset lifecycle infrastructure required before user stories.

**⚠️ CRITICAL**: No user story work begins until Phase 2 is complete.

- [X] T003 Add `friend class Template;` and minimal `virtual Component *clone() const = 0;` declaration cleanly in `include/platformator/baseobject.h`, `include/platformator/gameobject.h`, `include/platformator/behavior.h`, and `include/platformator/scriptcomponent.h`
- [X] T004 [P] Implement concrete `clone()` overrides copying authored properties and retained asset refs (with fresh `id = -1`, empty `handle`, and unregistered defaults — no runtime registration flags copied) in `src/animator.cpp`, `src/audio.cpp`, `src/boxcollider.cpp`, `src/circlecollider.cpp`, `src/camera.cpp`, `src/rigidbody.cpp`, `src/sprite.cpp`, and `src/scriptcomponent.cpp` (plus corresponding headers in `include/platformator/`)
- [X] T005 [P] Generate `Behavior *clone() const override` (resetting `scriptComponent = nullptr` and `gameObject = nullptr` before attachment in `ScriptComponent::clone()`) directly inside `SERIALIZABLE_SCRIPT` macros in `include/platformator/scriptregistration.h`
- [X] T006 Extract shared JSON object/component subtree builder helper (keeping single source of truth for `name`, `tag`, `position`, `scale`, `rotation`, `active`, component slots, and child recursion) in `src/scenebuilder.h` and `src/scenebuilder.cpp` so both `SceneBuildContext` and `Template` reuse it without duplication
- [X] T007 Implement `GameManager` template cache (`templateCache`, `templateAliases`), cycle-safe `loadTemplate(filePath, resolver, required)` using an active canonical-path stack (keeping cyclic nested `AssetReference<Template>` fields path-backed on dormant prototypes while validating file readability/JSON), `findTemplate`, `reloadTemplate`/`freeTemplate`/`freeAllTemplates`, and `Scene` asset retention support in `src/gamemanager.h`, `src/gamemanager.cpp`, `src/runtime.cpp`, and `src/assetreference.cpp`

**Checkpoint**: Foundation ready — `Template`, `AssetReference<Template>`, cloning hooks, and cache lifecycle compile and are ready for story implementation.

---

## Phase 3: User Story 1 - Reference and Spawn Saved Templates in Gameplay Scripts (Priority: P1) 🎯 MVP

**Goal**: Load `.pttemplate` files into an inert unregistered prototype (`std::vector<std::unique_ptr<GameObject, ObjectDeleter>>` with `objects[0]` = root and pre-retained `Asset::Reference`s) and spawn independent registered instances with default, parented, or explicit world placement.

**Independent Test**: Load a scene with a spawner referencing a `.pttemplate` (sprite + collider + rigidbody + behavior), confirm `0` prototype objects in `Runtime::getAllGameObjects()` and saved scenes before spawning, trigger spawns with default, parented, and explicit world `(x, y)` + `rotation`, and verify independent simulation and cleanup.

- [X] T008 [US1] Create `Template : public Asset` class header defining `objects` (`std::vector<std::unique_ptr<GameObject, ObjectDeleter>>`), retained asset dependency vectors, `getName()`, `instantiate(parent)` / `instantiate(worldPosition, worldRotation, parent)` / `instantiate(worldPosition, parent)`, and `platformator::instantiate(const AssetReference<Template> &, ...)` boundary helpers in `include/platformator/template.h`
- [X] T009 [US1] Implement `.pttemplate` v1 envelope parsing (`format == "platformator.object-template"`, `version == 1`, valid `"root"`, ID/slot validation) and `Asset::Reference` dependency retention into an unregistered `Staged` prototype subtree (`handle == {}`) in `src/template.cpp`
- [X] T010 [US1] Implement prototype cloning (`id = -1`, RAII clone cleanup guard on exception), placement logic (preserve authored local transform when default/parent-only; set world `position`/`rotation` when explicit coordinates passed even with `parent`), live scene registration via `GameManager::registerGameObjectSubtree`, and scene-level dependency retention + unset-ref boundary rejection in `src/template.cpp` and `include/platformator/assetreference.h`
- [X] T011 [P] [US1] Add US1 validation scenarios (unregistered prototype isolation before/after spawn, multi-spawn independent IDs/mutation/destruction, default vs parented vs explicit world placement, preloaded media retention, and fail-fast on missing/corrupt/unset `.pttemplate`) in `tests/runtime_template_test.cpp`

**Checkpoint**: MVP complete — US1 works and tests pass independently.

---

## Phase 4: User Story 2 - Remap Internal Object and Component References per Spawned Instance (Priority: P1)

**Goal**: Ensure every spawned instance remaps its internal `ObjectReference<T>` fields (`GameObject`, `Component`, and concrete subtypes `Animator`, `Audio`, `Camera`, `Collider`, `Rigidbody`, `ScriptComponent`, `Sprite`) strictly to that instance's own newly registered objects/components before `start()` executes.

**Independent Test**: Spawn two instances from a composite `.pttemplate` containing parent + child nodes, sibling components, and behavior `ObjectReference<T>` fields; confirm Instance A and Instance B each hold distinct handles and new scene IDs pointing strictly to their own components/children.

- [X] T012 [US2] Update `ObjectReference<T>::resolve(ReferenceResolver &)` and add typed resolution/clearing support cleanly in `include/platformator/objectreference.h` and `include/platformator/referenceresolver.h` so resolved targets sync both live `handle` and cloned scene `objectId`
- [X] T013 [US2] Build `protoIdToClone` map during prototype subtree cloning in `src/template.cpp`, register cloned tree before resolving script references, and run a `CloneReferenceResolver` over cloned behaviors before `flushIfIdle()` / `start()` executes (throwing cleanly + rolling back on wrong internal target kind)
- [X] T014 [P] [US2] Add US2 composite multi-child and typed-component (`Animator`, `Audio`, `Camera`, `Collider`, `Rigidbody`, `Sprite`, `ScriptComponent`) remapping test cases across repeated spawns in `tests/runtime_template_test.cpp`

**Checkpoint**: US1 and US2 both work independently with full internal reference integrity.

---

## Phase 5: User Story 3 - Assign and Inspect Template Reference Fields in the Editor (Priority: P2)

**Goal**: Make `TemplateAssetRef` / `AssetReference<Template>` fields discoverable without building first, inspectable/draggable/assignable as `.pttemplate` assets in the Inspector, validated in diagnostics, and preserved cleanly by editor template operations.

**Independent Test**: Scan a behavior header containing `TemplateAssetRef`, verify `sourceScanner` emits `kind: "asset_reference"`, `target: "Template"`, `nullable: true`; assign/validate `.pttemplate` paths and missing-file diagnostics; confirm export/duplicate/apply/revert preserve template asset paths as authored asset references.

- [X] T015 [P] [US3] Update behavior source scanner to emit `kind = "asset_reference"`, `target = "Template"`, `nullable = true` for `TemplateAssetRef`, `TemplateRef`, and `AssetReference<Template>` / `AssetRef<Template>` in `editor/src/sourcescanner.cpp`
- [X] T016 [P] [US3] Add `ResourceKind::Template` and `.pttemplate` matching (`expectedType == "Template"`) in `editor/src/resourceindex.h`, `editor/src/resourceindex.cpp`, `editor/src/assetformats.h`, `editor/src/filedialogs.cpp`, and `editor/src/panels/referencefields.cpp`
- [X] T017 [P] [US3] Add editor test coverage for source-scanned `TemplateAssetRef` discovery, `.pttemplate` `ResourceKind::Template` resolution/assignment validation, missing-template diagnostics, and export/duplicate/revert asset-path preservation across `editor/tests/discovery_test.cpp`, `editor/tests/resources_test.cpp`, `editor/tests/diagnostics_test.cpp`, and `editor/tests/templates_test.cpp`

**Checkpoint**: Full editor authoring loop for template reference fields is functional and tested.

---

## Phase 6: User Story 4 - Safe Spawning During Simulation, External Reference Handling, and Resource Cleanup (Priority: P2)

**Goal**: Guarantee mid-step and collision-callback spawning safety, deterministic clearing of cross-scene external bindings, cycle-safe nested template resolution on spawn, and leak-free unload/shutdown teardown.

**Independent Test**: Spawn 100 instances during active `update()` and collision callbacks (`SC-003`), spawn a `.pttemplate` containing an `external` ID colliding with a destination scene object ID (`FR-009`), spawn self-referential `Slime -> Slime` and mutual `A -> B -> A` cyclic templates (`FR-003`), and cycle scene load/unload repeatedly verifying `0` leaked objects/asset refs (`SC-005`).

- [X] T018 [US4] Clear cross-scene `external` `ObjectReference` targets (prototype-external IDs absent from `protoIdToClone`) via `reference.clear()` inside `CloneReferenceResolver` in `src/template.cpp` so coincidental destination scene IDs are never aliased
- [X] T019 [US4] Verify and handle mid-step / collision-callback spawning deferral (`assertCanMutate()`, `flushIfIdle()`, deferred `ScriptComponent` start order with remapped refs) and cycle-safe runtime binding of path-backed nested `AssetReference<Template>` fields on spawn in `src/template.cpp` and `src/gamemanager.cpp`
- [X] T020 [P] [US4] Add US4 stress and edge-case tests in `tests/runtime_template_test.cpp` covering 100 mid-update/collision spawns, external ID collision clearing, direct self-referential + mutual cyclic templates, partial-clone failure rollback, and repeated scene reload cleanup verification

**Checkpoint**: All 4 user stories and edge cases are complete and verified.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Verify KISS code quality, eliminate any duplication or unnecessary checks, and execute full `quickstart.md` validation across runtime and editor suites.

- [X] T021 Audit modified headers/sources across `include/platformator/`, `src/`, and `editor/src/` to confirm no duplicated subtree deserialization logic, no speculative/blanket null checks inside internal helpers, clean public-boundary checks, and preserved existing comments/docstrings
- [X] T022 Build and execute runtime and editor test suites matching `specs/017-runtime-template-instantiation/quickstart.md` (`cmake --build --preset debug --parallel 4 && ctest --test-dir bin/debug --output-on-failure` and `cmake --build --preset editor-debug --parallel 4 && ctest --test-dir bin/editor-debug --output-on-failure -L editor`)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: Starts immediately — T001, T002 can run in parallel (`CMakeLists.txt` vs headers).
- **Foundational (Phase 2)**: Depends on Phase 1 (`T001`). Inside Phase 2: `T003` precedes `T004`/`T005`; `T006` can run in parallel with `T004`/`T005`; `T007` integrates cache & resolver hooks (`T001`, `T006`). **BLOCKS Phases 3–6**.
- **User Story 1 (Phase 3, P1 — MVP)**: Depends on Phase 2. Sequentially: `T008` -> `T009` -> `T010`, then `T011`.
- **User Story 2 (Phase 4, P1)**: Depends on US1 (`T010`) and Phase 2. (`T012` -> `T013` -> `T014`).
- **User Story 3 (Phase 5, P2)**: Can run in parallel with Phases 3–4 once `T001` (`AssetTargetName<Template>` / `TemplateAssetRef` type naming) is done! `T015`, `T016` run cleanly in parallel across separate files, followed by `T017`.
- **User Story 4 (Phase 6, P2)**: Depends on US2 (`T013`) and Phase 2 (`T007`). (`T018` + `T019` -> `T020`).
- **Polish (Phase 7)**: Runs after desired stories complete (`T021` -> `T022`).

### Parallel Opportunities

- Phase 1: `T001` and `T002`
- Phase 2: `T004`, `T005`, `T006` once `T003` lands
- Phase 5 (`US3`): `T015` (`sourcescanner.cpp`) and `T016` (`resourceindex`/`assetformats`/`referencefields`) in parallel with `US1`/`US2` runtime work

---

## Parallel Example: Foundational & Editor Stories

```bash
# After T003 completes, run in parallel across independent files:
Task: "T004 [P] Implement concrete Component::clone() overrides across component .cpp/.h files"
Task: "T005 [P] Generate Behavior::clone() inside SERIALIZABLE_SCRIPT in include/platformator/scriptregistration.h"
Task: "T006 [P] Extract shared JSON subtree builder in src/scenebuilder.h and src/scenebuilder.cpp"

# While runtime US1/US2 is built or alongside it, run US3 editor wiring in parallel:
Task: "T015 [P] [US3] Update source scanner for TemplateAssetRef in editor/src/sourcescanner.cpp"
Task: "T016 [P] [US3] Add ResourceKind::Template and .pttemplate matching in editor/src/"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1 (Setup) + Phase 2 (Foundational).
2. Complete Phase 3 (`US1` — prototype loading + runtime spawning + world/parent transforms).
3. **STOP & VALIDATE**: Run `platformator_runtime_template_test` to confirm inert prototypes and multi-spawn isolation before moving on.

### Incremental Delivery

1. Phase 1 + Phase 2 → Runtime cloning & template cache foundation.
2. Phase 3 (`US1`) → MVP spawning usable in simple prefabs.
3. Phase 4 (`US2`) → Composite prefabs with internal `ObjectReference<T>` hierarchies.
4. Phase 5 (`US3`) → Full visual authoring workflow in the editor Inspector + diagnostics.
5. Phase 6 (`US4`) → Mid-step stress safety, external ref clearing, cyclic templates, leak checks.
6. Phase 7 → KISS audit and full `debug` + `editor-debug` test pass.


---

## Phase 8: Convergence

**Purpose**: Close implementation gaps, optimality bottlenecks, and code-quality issues identified by `/speckit-converge` against `spec.md`, `plan.md`, and the project constitution.

- [X] T023 [US1] Fix `Template::instantiateImpl` self-eviction bug (`C1`) in `src/template.cpp` so calling `instantiate()` on a cached `Template` with `referenceCount == 0` (loaded via `Runtime::loadTemplate`) does not increment and immediately drop `referenceCount` (`0 -> 1 -> 0`) via `Asset::retain`, preventing premature `freeTemplate(this)` and use-after-free segfaults on subsequent spawns
- [X] T024 [US4] Restructure `Template::instantiateImpl` in `src/template.cpp` (`C2`) to perform all fallible reference validation and deferred cyclic `TemplateAssetRef` loading while the cloned graph is still `Staged` (prior to `GameManager::registerGameObjectSubtree`), guaranteeing $O(k)$ RAII rollback without polluting `ObjectRegistry`, leaving mid-step pending retirements, or falsely invoking `Behavior::onDestroy()` on failed clones (`FR-011`)
- [X] T025 [US1] [US3] Optimize `AssetReference<T>::resolve` and `ScriptComponent::startPendingBehaviors` (`C3`) in `include/platformator/assetreference.h`, `include/platformator/scriptcomponent.h`, `src/scriptcomponent.cpp`, and `src/template.cpp` so pre-resolved prototype `AssetReference<T>` bindings are not re-queried on spawn and template-resolved behaviors are not redundantly re-resolved during `startPendingScripts()` (`FR-003`, `FR-006`, Principle I)
- [X] T026 [US2] [US3] [US4] Fix deferred `Behavior::start()` and object-retirement test sequencing (`C4`) in `tests/runtime_template_test.cpp` so `testUs2AndUs4CompositeAndRollbackAndExternalClearing` and `testUs3AssetAndCycleLifecycle` advance the simulation frame (`runtime.simulateFrame(dt)`) before asserting `start()` execution or post-removal template cache eviction, and verify `platformator_runtime_template_test` passes 100% under CTest
- [X] T027 [P] [US1] Defer `MIX_CreateTrack` allocation for dormant `Staged` `Audio` prototypes (`C5`) and preserve authored `gain` scalar directly in `include/platformator/audio.h` and `src/audio.cpp` so loaded `Template` prototypes do not consume live `SDL_mixer` tracks while inert (`FR-002`, `SC-001`, Principle I)
- [X] T028 [P] [US1] Optimize transform and hierarchy initialization order during `cloneSubtree` and `Template::instantiateImpl` (`C6`) in `src/template.cpp` so local transforms, components, parent attachment, and world transform overrides are applied without redundant $O(\text{subtree})$ `refreshImmediatePositions()` passes (Principle I)
- [X] T029 [P] [US3] Add missing editor unit test coverage (`C7`, `T017`) in `editor/tests/discovery_test.cpp` and `editor/tests/diagnostics_test.cpp` verifying `scan_project_sources` emits `kind: "asset_reference", target: "Template"` for `TemplateAssetRef` / `AssetReference<Template>` fields and `validate_document` reports blocking errors for missing or wrong-extension `.pttemplate` behavior references (`FR-014`, `SC-004`)
- [X] T030 [P] Clean up code quality and internal API surface (`C8`, Principle IV) by removing the unused `TemplateBuildResolver::owner` member in `src/template.cpp`, removing the uncontracted public `Template::getObjects()` accessor from `include/platformator/template.h`, and replacing `std::function` callbacks in `src/scenebuilder.h` / `src/scenebuilder.cpp` with zero-overhead callable templates or direct helpers
