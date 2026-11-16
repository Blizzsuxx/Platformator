# Phase 1 Data Model: Engine Usability & Packaging Cleanup (`018-game-dev-engine-cleanup`)

## 1. Runtime Session Configuration (`RuntimeOptions` & `WindowSettings`)

- **Entity**: `RuntimeOptions` ([`include/platformator/runtimeoptions.h`](../../include/platformator/runtimeoptions.h)) & `WindowSettings` ([`include/platformator/windowsettings.h`](../../include/platformator/windowsettings.h))
- **Fields & Invariants**:
  - `sceneFilePath` (`std::string`):
    - Empty (`""`): `Runtime(options)` initializes an empty unloaded world.
    - Non-empty: `Runtime(options)` loads `sceneFilePath` via `loadScene(sceneFilePath)` before constructor returns; if loading throws, runtime state rolls back (`activeRuntime()` remains `nullptr`).
  - `assetRoot` (`std::string`):
    - Expects the project's `assets/` directory itself (not the parent application directory).
    - Empty (`""`): `PathManager` auto-resolves `<exe_dir>/assets` if it exists, otherwise `<exe_dir>/../assets`.
  - `windowSettings.headless` (`bool`, default `false`):
    - `true`: Strict physics-only execution (`SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0`); rendering and audio/texture loading are intentionally unsupported.
    - `false` + `hidden = true`: Full asset/rendering/audio simulation; CI runs use SDL's `dummy` video/audio and `software` renderer drivers.

## 2. Visual Debug Overlay Defaults (`DebugSettings`)

- **Entity**: `DebugSettings` ([`include/platformator/debugsettings.h`](../../include/platformator/debugsettings.h))
- **Updated Default State**:
  - `startPaused = false`
  - `showColliders = false` *(changed from `true`)*
  - `showCollisionPoints = false` *(changed from `true`)*
  - `showCollisionNormals = false` *(changed from `true`)*
  - `showGridCells = false` *(changed from `true`)*
- **Compile-Time Gate**:
  - `PLATFORMATOR_ENABLE_DEBUG_TOOLS`: defaults to `OFF` when `CMAKE_BUILD_TYPE` is `Release` or `MinSizeRel`; `ON` otherwise unless overridden on configure/preset. When `0`, debug overlay drawing and `PLATFORMATOR_LOG(...)` compile to no-ops.

## 3. Optional Script Asset References (`AssetReference<T>`)

- **Entity**: `AssetReference<T>` (`TextureAssetRef`, `AudioAssetRef`, `AnimationClipRef`, `TemplateAssetRef` in [`include/platformator/assetreference.h`](../../include/platformator/assetreference.h))
- **State Machine**:
  - **Unset** (`object == nullptr && !filePath.has_value()`): Produced by default construction, `clear()`, JSON `null`, omitted JSON key (under `*_WITH_DEFAULT`), or empty string `""` passed to constructor / `setFilePath("")` / JSON deserialization.
    - `hasFilePath()` -> `false`
    - `resolve(runtimeOrResolver)` -> resets any prior object and returns `nullptr` without invoking `load*`.
    - Serialization (`to_json`) -> serializes cleanly as JSON `null`.
  - **Pending** (`object == nullptr && filePath.has_value()` where `!*filePath.empty()`):
    - `hasFilePath()` -> `true`
    - `resolve(runtimeOrResolver)` -> delegates to `GameManager::load*(path, required=true)`; throws with `"Field '<name>': ..."` if missing, malformed, or escaping `assetRoot`.
  - **Resolved** (`object != nullptr`): Retains refcounted pointer and canonical `filePath`.

## 4. Behavior Serialization Capacity (`SERIALIZABLE_SCRIPT`)

- **Entity**: `SERIALIZABLE_SCRIPT(Type, ...)` ([`include/platformator/scriptregistration.h`](../../include/platformator/scriptregistration.h))
- **Capacity Constraint**:
  - Supports **0 to 32** serialized fields (`PLATFORMATOR_FOR_EACH_1..32`, `PLATFORMATOR_ARGUMENT_COUNT` up to `32`, backed by `NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT`).
  - Field count exceeding 32 triggers a compile-time error rather than silent truncation.

## 5. Scene & Template Path Normalization (`FR-007`)

- **Scene Files (`*.scene`)**:
  - Top-level JSON array `[ ...GameObject... ]` (no wrapper object envelope).
- **Template Files (`*.pttemplate`) & Provenance**:
  - Editor scene provenance key `"template"` stores `"templates/<Name>.pttemplate"` (relative to `assetRoot`, matching existing editor tests).
  - Runtime `TemplateAssetRef` and `Runtime::loadTemplate(...)` accept both `"assets/templates/<Name>.pttemplate"` and `"templates/<Name>.pttemplate"` (normalized internally inside `GameManager::loadTemplate` to `"assets/templates/<Name>.pttemplate"` for canonical lookup and `PathManager` validation).
  - Other resource types (textures, audio, animation clips, scenes) retain strict `"assets/..."` prefix enforcement (`PathManager::canonicalizeAssetPath`).

## 6. Generated Project Build & Distribution Layout (`CMakeLists.txt`)

- **Entity**: Standalone Project Scaffold ([`editor/src/project.cpp`](../../editor/src/project.cpp))
- **Post-Build Layout** (`<build_dir>`):
  - Executable `<build_dir>/<target>`
  - Runtime assets copied post-build (when project `assets/` exists) to `<build_dir>/assets/` excluding `<build_dir>/assets/behaviors/`
  - Windows runtime DLLs copied post-build (`if(WIN32)`) next to executable
- **Install Layout** (`<prefix>` via `cmake --install`):
  - `<prefix>/bin/<target>` (with `INSTALL_RPATH` set to `$ORIGIN/../lib` on Unix or `@loader_path/../lib` on macOS)
  - `<prefix>/lib/` (shared SDL3/TBB targets when built from source tree) or `<prefix>/bin/` (DLLs on Windows)
  - `<prefix>/assets/` (when project `assets/` exists; excludes `behaviors/`)
