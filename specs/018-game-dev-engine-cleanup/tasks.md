# Tasks: Engine Usability and Packaging Cleanup from Game Development Findings

**Input**: Design documents from `/specs/018-game-dev-engine-cleanup/`  
**Prerequisites**: `plan.md` (required), `spec.md` (required for user stories), `research.md`, `data-model.md`, `contracts/engine-cleanup-contracts.md`, `quickstart.md`

**Organization**: Tasks are grouped by user story (`US1`, `US2`, `US3`) so each story is independently implementable and testable with minimal, KISS-focused changes.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (`[US1]`, `[US2]`, `[US3]`)
- Every task specifies exact file paths

## Path Conventions

- Core public API headers: `include/platformator/`
- Core runtime & runner implementation: `src/`
- Root build & options: `CMakeLists.txt`
- Editor scaffold & tests: `editor/src/`, `editor/tests/`
- Standalone examples & engine tests: `examples/`, `tests/`

---

## Phase 1: Setup (Baseline Verification)

**Purpose**: Establish clean pre-change build/test baselines before applying cleanup edits.

- [X] T001 Build debug/editor baselines (`cmake --preset cleanup-debug && cmake --build bin/cleanup-debug --parallel 4`) and verify current tests run cleanly in `CMakeLists.txt`

---

## Phase 2: Foundational (Release & Visual Debug Defaults)

**Purpose**: Establish clean `DebugSettings` and `Release`/`MinSizeRel` CMake defaults that affect all user stories and verification presets.

**CRITICAL**: Complete before verifying `US1`–`US3` under debug and release configurations.

- [X] T002 [P] Default visual overlay fields (`showColliders`, `showCollisionPoints`, `showCollisionNormals`, `showGridCells`) to `false` (`startPaused = false`) in `include/platformator/debugsettings.h`
- [X] T003 [P] Default `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` when `CMAKE_BUILD_TYPE` matches `^(Release|MinSizeRel)$` (and `ON` otherwise) in `CMakeLists.txt`

**Checkpoint**: Visual debug overlays start disabled and `Release`/`MinSizeRel` CLI builds suppress debug tools and `PLATFORMATOR_LOG` output automatically.

---

## Phase 3: User Story 1 - Predictable Runtime Initialization, Optional Asset Slots, and Documented Test Modes (Priority: P1) 🎯 MVP

**Goal**: Make `Runtime(const RuntimeOptions &)` load non-empty `sceneFilePath` reliably with exception rollback, allow empty `""` optional `AssetReference<T>` slots while rejecting invalid non-empty paths, and clearly document `assetRoot` and physics-only `headless` vs `hidden` dummy-driver testing.

**Independent Test**: Construct `platformator::Runtime(options)` directly with valid and invalid `sceneFilePath`; load scenes/templates containing `""` empty `AssetReference<T>` slots alongside non-empty missing paths; confirm `runner_failure_test` physics-only `headless` invariants pass and `crystal_citadel --self-test` loads without manual post-construct `loadScene()`.

### Implementation for User Story 1

- [X] T004 [P] [US1] Document `assetRoot` (expects the `assets/` folder; empty defaults to `<exe_dir>/assets` or `<exe_dir>/../assets`) and `sceneFilePath` auto-loading in `include/platformator/runtimeoptions.h`, and document `headless = true` (strict physics-only, no SDL video/audio init) vs `hidden = true` + SDL dummy drivers in `include/platformator/windowsettings.h`
- [X] T005 [P] [US1] Treat empty string `""` identically to `clear()` (`filePath = std::nullopt`, `hasFilePath() == false`, `resolve()` returns `nullptr`) in `AssetReference(const std::string &)` and `setFilePath(const std::string &)` while keeping non-empty validation strict in `include/platformator/assetreference.h`
- [X] T006 [US1] Load non-empty `runtimeOptions.sceneFilePath` inside `Runtime::Runtime(const RuntimeOptions &)` with rollback cleanup (`gameManager->shutdown()`, `gameManager.reset()`, `activeRuntime() = nullptr` on throw) in `src/runtime.cpp`
- [X] T007 [US1] Remove redundant post-construct `loadScene()` calls in `src/runner.cpp` and `src/benchmark_main.cpp`, and clear `options.runtimeOptions.sceneFilePath` when `--scenario` selects a synthetic benchmark in `src/benchmark_main.cpp` (depends on T006)
- [X] T008 [US1] Add regression assertions for `Runtime(options)` startup scene loading, rollback on invalid startup scene (`tryGetCurrent() == nullptr`), and empty `""` vs non-empty missing `AssetReference<T>` behavior in `tests/runtime_foundation_test.cpp` and `tests/runtime_template_test.cpp` (depends on T005, T006)

**Checkpoint**: User Story 1 is independently complete and testable via `ctest --test-dir bin/cleanup-debug -R "runner_failure_test|runtime_foundation_test|asset_root_contract_test|runtime_template_test"`.

---

## Phase 4: User Story 2 - Consistent Behavior Authoring, Queries, and Scene/Template Conventions (Priority: P1)

**Goal**: Double `SERIALIZABLE_SCRIPT` field limit from 16 to 32 cleanly, export free-function `getBehavior<T>(GameObject *)` in global and `platformator::` scopes, and normalize `"templates/..."` -> `"assets/templates/..."` inside `GameManager::loadTemplate` while keeping `.scene` files as top-level JSON arrays `[...]`.

**Independent Test**: Register, serialize, inspect, and deserialize a behavior with 17..32 fields; query behaviors from external/free C++ functions via `getBehavior<T>` and `platformator::getBehavior<T>`; load templates via both `"templates/..."` and `"assets/templates/..."` without breaking existing `.scene` arrays or Editor tests.

### Implementation for User Story 2

- [X] T009 [P] [US2] Expand `PLATFORMATOR_FOR_EACH_1..32` and `PLATFORMATOR_ARGUMENT_COUNT` to 32 fields and eliminate duplicate `PLATFORMATOR_METADATA_FIELDS_*` / `PLATFORMATOR_SERIALIZABLE_SCRIPT_1..16` ladders by reusing `PLATFORMATOR_FOR_EACH` in `include/platformator/scriptregistration.h`
- [X] T010 [P] [US2] Add free function `template <typename T> T *getBehavior(GameObject *gameObject)` (and `namespace platformator { using ::getBehavior; }`) returning `nullptr` for null `gameObject` and delegating `Behavior::getBehavior(GameObject *) const` in `include/platformator/behaviorquery.h`
- [X] T011 [P] [US2] Normalize leading `"templates/"` template paths to `"assets/templates/..."` before alias lookup and `PathManager::resolveAssetPath` (preserving `"assets/templates/..."` canonical identity and bare-array `[...]` `.scene` format) in `GameManager::loadTemplate` in `src/gamemanager.cpp`
- [X] T012 [US2] Add regression checks covering a >16-field (e.g., 20–32 fields) `SERIALIZABLE_SCRIPT` behavior round-trip, free-function `::getBehavior<T>` and `platformator::getBehavior<T>` queries, and `"templates/..."` loading/ref-resolution in `tests/runtime_template_test.cpp` and `tests/runtime_foundation_test.cpp` (depends on T009, T010, T011)

**Checkpoint**: User Story 2 is independently complete and testable alongside existing `editor/tests/templates_test.cpp` assertions.

---

## Phase 5: User Story 3 - Clean Production Defaults and Standalone Project Build/Distribution Scaffolding (Priority: P2)

**Goal**: Make Editor-generated `CMakeLists.txt` copy `assets/` (excluding `behaviors/`) post-build and support relocatable cross-platform `cmake --install` packages (`$ORIGIN/../lib`, `@loader_path/../lib`, `WIN32` DLLs), sync Crystal Citadel, and remove obsolete `missing.*` engine fallback requirements from `examples/mario`.

**Independent Test**: Generate or sync `examples/crystal_citadel/CMakeLists.txt`, build from CLI with `-DCMAKE_BUILD_TYPE=Release` into a multi-level directory, verify post-build `assets/` excludes `behaviors/`, install with `cmake --install --strip`, confirm RPATH via `ldd`, run `--self-test` from `/tmp` with zero debug log chatter, and run `ctest -R package_smoke`.

### Implementation for User Story 3

- [X] T013 [US3] Update `generate_project_cmakelists` to add guarded `POST_BUILD` copy of `assets/` removing `assets/behaviors`, Unix/macOS `INSTALL_RPATH` (`$ORIGIN/../lib` / `@loader_path/../lib`), and `install()` rules (target binary, shared SDL3/TBB targets when built in-tree, and `assets/` with `PATTERN "behaviors" EXCLUDE`) while retaining `PLATFORMATOR_ENGINE_SOURCE_DIR` in `editor/src/project.cpp`
- [X] T014 [P] [US3] Update `editor/tests/project_test.cpp` and `editor/tests/workspace_lifecycle_test.cpp` to assert `POST_BUILD` asset copy (without `behaviors`), `INSTALL_RPATH`, and `install()` rules in generated `CMakeLists.txt` files (depends on T013)
- [X] T015 [P] [US3] Remove obsolete `textures/missing.png`, `audio/missing.wav`, and `animations/missing.animset` fallback check and full engine `assets/` copy while keeping `level1.scene` staging and `install()`/`INSTALL_RPATH` working for `tests/package_smoke.cmake` in `examples/mario/CMakeLists.txt`
- [X] T016 [US3] Sync `examples/crystal_citadel/CMakeLists.txt` to the updated Editor-generated template and simplify `examples/crystal_citadel/main.cpp` to rely on constructor `sceneFilePath` auto-loading, `getBehavior<T>`, and optional `""` cleanup improvements (depends on T006, T010, T013)

**Checkpoint**: User Story 3 builds cleanly via CLI `-DCMAKE_BUILD_TYPE=Release`, produces self-contained post-build and `cmake --install` outputs, and preserves `package_smoke`.

---

## Phase 6: Polish & Cross-Cutting Verification

**Purpose**: End-to-end validation across all test targets, editor tests, examples, and cleanup documentation.

- [X] T017 [P] Update `examples/crystal_citadel/ENGINE_ISSUES.md` and `examples/crystal_citadel/README.md` with resolved status / simplified build instructions matching `specs/018-game-dev-engine-cleanup/quickstart.md`
- [X] T018 Execute full `quickstart.md` validation: debug `ctest --test-dir bin/cleanup-debug --output-on-failure` (including `package_smoke`), editor `ctest` (`project_test|templates_test|workspace_lifecycle_test|discovery_test`), CLI `Release` build + `cmake --install --strip` + `ldd` + `/tmp` run of `crystal_citadel --self-test` and all 4 scenes with dummy SDL drivers (depends on T001–T017)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1: T001)**: No dependencies — establishes baseline build/test health.
- **Foundational (Phase 2: T002–T003)**: Can proceed immediately in parallel after T001; blocks verification of clean `Release`/default overlay behavior across stories.
- **User Story 1 (Phase 3: T004–T008)**, **User Story 2 (Phase 4: T009–T012)**, **User Story 3 (Phase 5: T013–T016)**:
  - `US1` (T004–T008) and `US2` (T009–T012) touch disjoint headers/files (except appending separate test functions to `tests/runtime_foundation_test.cpp` and `tests/runtime_template_test.cpp` in T008 & T012) and can proceed independently.
  - `US3` (`T013`–`T015`) is independent of `US1`/`US2`, while `T016` (`examples/crystal_citadel/main.cpp` & `CMakeLists.txt`) integrates cleanups from `T006`, `T010`, and `T013`.
- **Polish (Phase 6: T017–T018)**: Final doc sync (`T017`) and full cross-cutting `quickstart.md` regression run (`T018`).

### Parallel Opportunities per Story

```text
# Foundational (Phase 2):
T002 (include/platformator/debugsettings.h)  ||  T003 (CMakeLists.txt)

# User Story 1 (Phase 3):
T004 (runtimeoptions.h, windowsettings.h)  ||  T005 (assetreference.h)  ||  T006 (src/runtime.cpp)
  -> then T007 (runner.cpp, benchmark_main.cpp) & T008 (runtime_foundation_test.cpp, runtime_template_test.cpp)

# User Story 2 (Phase 4):
T009 (scriptregistration.h)  ||  T010 (behaviorquery.h)  ||  T011 (src/gamemanager.cpp)
  -> then T012 (runtime_template_test.cpp, runtime_foundation_test.cpp)

# User Story 3 (Phase 5):
T013 (editor/src/project.cpp) -> then T014 (editor tests)
T015 (examples/mario/CMakeLists.txt) in parallel with T013
T016 (examples/crystal_citadel/CMakeLists.txt, main.cpp) after T006, T010, T013
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. Complete **Phase 1 (T001)** and **Phase 2 (T002–T003)** for clean baseline and overlay/Release defaults.
2. Complete **Phase 3: User Story 1 (T004–T008)** to deliver constructor startup scene loading, empty `""` optional `AssetReference` support, and clear asset-root / headless docs.
3. Validate independently via core CTest suite before expanding to US2/US3.

### Incremental Delivery

1. Add **User Story 2 (T009–T012)** -> verify 32-field serialization, free `getBehavior<T>`, and `"templates/..."` compatibility.
2. Add **User Story 3 (T013–T016)** -> verify Editor scaffold builds (`assets/` copied, `behaviors/` excluded, `cmake --install --strip` relocatable), `mario`, and `crystal_citadel`.
3. Run **Phase 6 (T017–T018)** -> verify clean `Release` self-test/smoke runs from `/tmp` and zero `ctest` regressions.

---

## Notes

- Keep changes minimal and KISS (`IV` of Constitution): avoid extra wrapper classes or unnecessary defensive checks on non-boundary internals.
- Remember [`AGENTS.md`](../../AGENTS.md): keep builds in `bin/`, reuse existing builds where appropriate, and do not copy build trees into artifacts.
- Re-check `.scene` bare-array compatibility (`tests/runtime_template_test.cpp`) and physics-only `headless = true` (`tests/runner_failure_test.cpp`) after edits.
