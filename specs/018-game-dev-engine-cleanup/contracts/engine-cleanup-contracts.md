# Phase 1 Contracts: Engine Usability & Packaging Cleanup (`018-game-dev-engine-cleanup`)

## 1. Runtime & Window Configuration Contract

- **`platformator::Runtime::Runtime(const RuntimeOptions &runtimeOptions)`** (`src/runtime.cpp`):
  - If `!runtimeOptions.sceneFilePath.empty()`, loads `runtimeOptions.sceneFilePath` during construction.
  - On exception during startup scene loading, cleans up `gameManager` and clears `Runtime::tryGetCurrent()` before rethrowing.
  - `src/runner.cpp` and `src/benchmark_main.cpp` do not double-load `sceneFilePath` after constructing `Runtime`; synthetic benchmark scenarios (`--scenario`) clear `sceneFilePath` prior to constructing `Runtime`.
- **`RuntimeOptions::assetRoot`** (`include/platformator/runtimeoptions.h`):
  - Documented as the `assets/` directory itself (empty string auto-resolves `<exe_dir>/assets` or `<exe_dir>/../assets`).
- **`WindowSettings::headless`** (`include/platformator/windowsettings.h`):
  - Documented as physics-only execution (never initializes SDL video or audio subsystems). Automated full-scene tests with sprites/audio use `hidden = true` (`headless = false`) alongside `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and `SDL_RENDER_DRIVER=software`.

## 2. Debug Overlay & Release Build Defaults Contract

- **`DebugSettings`** (`include/platformator/debugsettings.h`):
  - All 4 visual overlay flags default to `false` (`showColliders`, `showCollisionPoints`, `showCollisionNormals`, `showGridCells`).
  - Explicit `--debug-draw <tokens>` CLI parsing (`src/runtimeoptions.cpp`) or programmatic struct field assignment overrides these defaults cleanly.
- **`CMakeLists.txt` Option Default**:
  - `PLATFORMATOR_ENABLE_DEBUG_TOOLS` defaults to `OFF` if `CMAKE_BUILD_TYPE` is `Release` or `MinSizeRel`, and `ON` otherwise, unless explicitly passed on configure (`-DPLATFORMATOR_ENABLE_DEBUG_TOOLS=ON|OFF` or via `CMakePresets.json`).

## 3. Optional `AssetReference<T>` & `SERIALIZABLE_SCRIPT` Contract

- **`AssetReference<T>`** (`include/platformator/assetreference.h`):
  - `AssetReference("")` and `setFilePath("")` clear the reference (`hasFilePath() == false`), matching `null` JSON values.
  - Non-empty strings still preserve path validation and strict asset resolution (throwing with field context if missing, invalid, or escaping `assetRoot`).
- **`SERIALIZABLE_SCRIPT(Type, ...)`** (`include/platformator/scriptregistration.h`):
  - Supports **0..32** fields (`SERIALIZABLE_SCRIPT(Type)` up to `SERIALIZABLE_SCRIPT(Type, f1, ..., f32)`).
  - Exceeding 32 fields must fail compilation with a clear preprocessor error.

## 4. Free-Function Behavior Query Contract

- **`include/platformator/behaviorquery.h`**:
  - Provides `template <typename T> T *getBehavior(GameObject *gameObject)` in the global namespace and `using ::getBehavior;` in `namespace platformator`.
  - Safe for null `gameObject` pointers (`returns nullptr`); checks both active and pending behaviors on `ScriptComponent` via `ScriptComponent::getBehavior<T>()`.
  - `Behavior::getBehavior<T>(GameObject *gameObject) const` forwards to `::getBehavior<T>(gameObject)`.

## 5. Scene & Template Path Resolution Contract

- **Scene Format**: Remains a top-level JSON array `[...]`.
- **Template Loading Normalization** (`GameManager::loadTemplate` in `src/gamemanager.cpp`):
  - Input `"templates/<path>"` normalizes to `"assets/templates/<path>"` before alias lookup and `PathManager::resolveAssetPath(...)`.
  - Canonical asset identity stored on loaded `Template::getFilePath()` remains `"assets/templates/<path>"`.
  - Editor scene provenance (`"template": "templates/<Name>.pttemplate"`) remains unchanged and resolves cleanly in both Editor and Runtime.

## 6. Generated Project `CMakeLists.txt` Contract

- **`generate_project_cmakelists`** (`editor/src/project.cpp`):
  - Retains the literal `set(PLATFORMATOR_ENGINE_SOURCE_DIR "<path>" CACHE PATH ...)` directive required by `inspect_project_engine_path` / `repair_project_engine_path`.
  - Includes `find_package(Platformator CONFIG QUIET)` with `add_subdirectory(... EXCLUDE_FROM_ALL)` fallback.
  - Uses `GLOB_RECURSE` (`CONFIGURE_DEPENDS`) for `assets/behaviors/*.cpp|h|hpp`.
  - On `WIN32`, copies `$<TARGET_RUNTIME_DLLS:<target>>` post-build.
  - Copies project `assets/` (if present) post-build into `$<TARGET_FILE_DIR:<target>>/assets`, removing copied `behaviors/` source files from the runtime output directory.
  - Sets `INSTALL_RPATH` (`$ORIGIN/../lib` on Linux/Unix, `@loader_path/../lib` on macOS).
  - Defines `install()` rules installing `<target>` into `bin`, in-tree built runtime libraries (`SDL3-shared`, `SDL3_image-shared`, `SDL3_mixer-shared`, `SDL3_ttf-shared`, `tbb`) into `lib` (Unix) / `bin` (Windows), and project `assets/` (excluding `behaviors`) into `assets`.
- **`examples/mario/CMakeLists.txt` & `examples/crystal_citadel/CMakeLists.txt`**:
  - `examples/crystal_citadel/CMakeLists.txt` uses the Editor-generated structure cleanly.
  - `examples/mario/CMakeLists.txt` drops the obsolete engine `missing.png`/`missing.wav`/`missing.animset` requirement and full engine `assets/` copy while keeping its `INSTALL_RPATH`/`install()` and `level1.scene` staging working for `tests/package_smoke.cmake`.
