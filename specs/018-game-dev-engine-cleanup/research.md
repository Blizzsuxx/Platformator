# Phase 0 Research: Engine Usability & Packaging Cleanup (`018-game-dev-engine-cleanup`)

This document records the KISS design decisions, technical rationale, and rejected alternatives for each finding in [`examples/crystal_citadel/ENGINE_ISSUES.md`](../../examples/crystal_citadel/ENGINE_ISSUES.md) and [`specs/018-game-dev-engine-cleanup/spec.md`](./spec.md).

---

## 1. Startup Scene Loading in `Runtime(const RuntimeOptions &)` (`FR-001`)

- **Decision**: Load `runtimeOptions.sceneFilePath` inside `platformator::Runtime::Runtime(const RuntimeOptions &)` (`src/runtime.cpp`) when `!runtimeOptions.sceneFilePath.empty()`, with rollback (`gameManager->shutdown()`, `gameManager.reset()`, `activeRuntime() = nullptr`) if scene loading throws. Remove the redundant post-construct `loadScene()` call from `src/runner.cpp` and `src/benchmark_main.cpp`, and clear `options.runtimeOptions.sceneFilePath` when `--scenario` selects a synthetic benchmark in `src/benchmark_main.cpp`.
- **Rationale**: Constructing `Runtime(options)` with a populated `sceneFilePath` previously left the runtime empty while `platformator::run(options)` loaded it. Loading in the constructor makes `Runtime(options)` self-consistent (~10 lines changed) and preserves strong exception-safety (`runner_failure_test`). Notice clearing `sceneFilePath` in `--scenario` parsing prevents synthetic benchmark modes (`makeDefaultBenchmarkRuntimeOptions()` sets `DEFAULT_SCENE_PATH`) from loading `default.scene` unnecessarily.
- **Alternatives Considered**: Keeping `Runtime(options)` empty and documenting manual `loadScene()`. Rejected because callers reasonably expect `RuntimeOptions::sceneFilePath` passed to `Runtime(options)` to load on construction.

---

## 2. Asset Root & Physics-Only Headless Documentation (`FR-002`, `FR-004`)

- **Decision**: Leave `PathManager` (`src/pathmanager.cpp`) and `WindowSettings::headless` logic unchanged. Clarify in `include/platformator/runtimeoptions.h` and `include/platformator/windowsettings.h` comments that:
  - `RuntimeOptions::assetRoot` points to the `assets/` directory itself (and empty defaults to `<exe_dir>/assets` or `<exe_dir>/../assets` automatically).
  - `WindowSettings::headless = true` is strict physics-only mode (no SDL video/audio init), whereas full sprite/audio scene tests should use `windowSettings.hidden = true` with `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and `SDL_RENDER_DRIVER=software`.
- **Rationale**: Preserves `runner_failure_test` (`SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0` when `headless = true`), preserves `PathManager`'s dual build/install layout discovery, and avoids speculative stub layers across `TextureWrapper`, `AudioWrapper`, and `Audio` (aligned with Clarifications & Constitution `§IV`).
- **Alternatives Considered**: Stubbing textures and audio when `headless = true`, or probing `<configuredRoot>/assets`. Rejected as unnecessary complexity (`headless` stubs risk hiding real PNG/WAV asset bugs and break the physics-only guarantee).

---

## 3. Optional Empty `""` `AssetReference<T>` Slots (`FR-003`)

- **Decision**: Treat an empty string (`""`) in `AssetReference<T>(const std::string &)` and `AssetReference<T>::setFilePath(const std::string &)` (`include/platformator/assetreference.h`) identically to `clear()` (`filePath = std::nullopt`, `object = nullptr`).
- **Rationale**: Currently `from_json` maps `null` to `clear()`, whereas `""` stores `filePath = ""` (`hasFilePath() == true`), causing `AssetReference::resolve` to invoke `load*(..., required = true)` and throw `"Required ... path is empty"`. Treating `""` as unset directly in `AssetReference` is a ~6-line KISS change that covers all 4 asset types (`TextureWrapper`, `AudioWrapper`, `AnimationClip`, `Template`) without weakening `GameManager::load*` checks on required engine components or duplicating reference wrapper types.
- **Alternatives Considered**: Changing `GameManager::load*` `required` parameter defaults or introducing an `OptionalAssetReference<T>` template. Rejected as either over-permissive for required components or redundant API surface.

---

## 4. Raising `SERIALIZABLE_SCRIPT` Capacity to 32 Fields (`FR-005`)

- **Decision**: Expand `PLATFORMATOR_FOR_EACH_1..32` and `PLATFORMATOR_ARGUMENT_COUNT` to 32 fields in `include/platformator/scriptregistration.h`, and remove the separate `PLATFORMATOR_METADATA_FIELDS_*` and `PLATFORMATOR_SERIALIZABLE_SCRIPT_1..16` macro ladders by forwarding directly to `PLATFORMATOR_FOR_EACH` and a uniform non-zero branch (`PLATFORMATOR_SERIALIZABLE_SCRIPT_HAS_FIELDS`).
- **Rationale**: `vendored/json/json.hpp`'s `NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT` already supports 64 fields (`NLOHMANN_JSON_PASTE64`), so `scriptregistration.h`'s 16-field preprocessor cap was the sole bottleneck. Simplifying the `0` vs `N >= 1` dispatch eliminates 30+ redundant macro lines while doubling capacity from 16 to 32 fields.
- **Alternatives Considered**: Replacing macros with nested config structs in game code or C++20 reflection libraries. Rejected; doubling the macro arity to 32 is zero-overhead and keeps game scripts ergonomic.

---

## 5. Consistent Behavior Lookup in `behaviorquery.h` (`FR-006`)

- **Decision**: Define free function `template <typename T> T *getBehavior(GameObject *gameObject)` in `include/platformator/behaviorquery.h`, expose `using ::getBehavior;` in `namespace platformator`, and delegate `Behavior::getBehavior(GameObject *) const` directly to `::getBehavior<T>(gameObject)`.
- **Rationale**: Non-behavior C++ code (e.g., `main.cpp` or test helpers) including `behaviorquery.h` previously got a compile error calling `platformator::getBehavior<T>` or `::getBehavior<T>` because only `Behavior::getBehavior` existed. Adding the free function and aliasing it in `namespace platformator` provides one canonical helper across both global and `platformator::` scopes with minimal lines changed.
- **Alternatives Considered**: Documenting manual `obj->getComponent<ScriptComponent>()->getBehavior<T>()` everywhere. Rejected because keeping `behaviorquery.h` usable outside `Behavior` subclasses is much cleaner.

---

## 6. `.scene` Format Stability & `"templates/"` Path Normalization (`FR-007`)

- **Decision**: Keep `.scene` files as bare top-level JSON arrays `[...]` (per Clarification Q1). In `GameManager::loadTemplate` (`src/gamemanager.cpp`), normalize a leading `"templates/"` path prefix (when no `"assets/"` prefix is present) to `"assets/templates/..."` before delegating to `PathManager::resolveAssetPath`. Preserve editor `"templates/<Name>.pttemplate"` provenance normalization in `editor/src/templates.cpp`.
- **Rationale**: `editor/src/templates.cpp` (`resolve_template_asset_path`) already accepts both `"assets/templates/..."` and `"templates/..."`, while `editor/tests/templates_test.cpp` expects `"templates/..."` in scene metadata. Normalizing `"templates/"` -> `"assets/templates/"` inside `GameManager::loadTemplate` (3 lines) lets scene provenance links and `TemplateAssetRef` runtime paths work interchangeably with zero breaking schema or test changes.
- **Alternatives Considered**: Wrapping `.scene` files in a JSON object or rewriting all editor tests to emit `"assets/templates/..."`. Explicitly rejected in Clarifications (Q1) and violates KISS.

---

## 7. Default-Off Visual Overlays & `Release` `PLATFORMATOR_ENABLE_DEBUG_TOOLS` (`FR-008`)

- **Decision**: Set `showColliders = false`, `showCollisionPoints = false`, `showCollisionNormals = false`, and `showGridCells = false` by default in `include/platformator/debugsettings.h`. In root `CMakeLists.txt`, default `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` when `CMAKE_BUILD_TYPE` is `Release` or `MinSizeRel` (and `ON` otherwise). Check `tests/runtime_step_test.cpp` and `tests/runtime_foundation_test.cpp`: `runtime_step_test.cpp` already sets `showColliders = true` / `showGridCells = true` explicitly (`lines 94–95`), while `runtime_foundation_test.cpp` only sets `startPaused`.
- **Rationale**: Fixes games launching with debug wireframes and `Release` CLI builds printing ~46 KB of `PLATFORMATOR_LOG` collider/asset chatter by default, while `--debug-draw` CLI flags and explicit `DebugSettings` structs continue to work identically.
- **Alternatives Considered**: Leaving `DebugSettings` all `true` or relying solely on Editor `production_cmake_options()`. Rejected per Clarification Q3.

---

## 8. Editor-Generated `CMakeLists.txt` Packaging & `examples/mario` Cleanup (`FR-009`, `FR-010`, `FR-011`)

- **Decision**: Update `generate_project_cmakelists` in `editor/src/project.cpp` so generated `CMakeLists.txt` files include:
  1. `POST_BUILD` copy of `${CMAKE_CURRENT_SOURCE_DIR}/assets` to `$<TARGET_FILE_DIR:<target>>/assets` (gated by `if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")` so source-only projects still build cleanly, followed by `remove_directory "$<TARGET_FILE_DIR:<target>>/assets/behaviors"` so `.cpp`/`.h` sources aren't duplicated into runtime `assets/`).
  2. Unix/macOS relocatable RPATH (`$ORIGIN/../lib` / `@loader_path/../lib`) on the game target alongside the existing `WIN32` `$<TARGET_RUNTIME_DLLS:...>` copy.
  3. Clean `install()` rules for `<target>` (`RUNTIME DESTINATION bin`, `BUNDLE DESTINATION bin`), runtime shared library targets (`SDL3-shared`, `SDL3_image-shared`, `SDL3_mixer-shared`, `SDL3_ttf-shared`, `tbb` installed only when built in-tree, matching `mario`), and `assets/` (`PATTERN "behaviors" EXCLUDE`, if `EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets"`).
  Also update `examples/crystal_citadel/CMakeLists.txt` to match the improved generator output, and remove the obsolete `missing.png`/`missing.wav`/`missing.animset` fallback checks and engine `assets/` copy from `examples/mario/CMakeLists.txt` (updating `tests/package_smoke.cmake` if `mario_example` no longer copies `textures/missing.png`—note we verified `tests/package_smoke.cmake` only runs `mario_example --smoke-steps 5`, while `tests/consumer/main.cpp` checks `textures/missing.png` against the installed SDK `assets` directory!).
- **Rationale**: Makes CLI `Release` builds, multi-level out-of-tree builds (`bin/<project>`), and `cmake --install` distributions work out of the box across Linux, macOS, and Windows without manual build script rewrites. Notice `package_smoke.cmake` lines 34–44 still test `examples/mario` with `-DPLATFORMATOR_MARIO_USE_INSTALLED=ON`, so keeping `PLATFORMATOR_MARIO_USE_INSTALLED` support while dropping the fake `missing.*` error check makes `examples/mario/CMakeLists.txt` accurate and clean!
- **Alternatives Considered**: Generating a separate packaging CMake file or keeping `install()` manual. Rejected per Clarification Q4.
