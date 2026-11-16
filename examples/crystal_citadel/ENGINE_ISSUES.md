# Platformator Engine Findings & Issues

Issues, sharp edges, and friction points directly observed while building, testing, and packaging the **Crystal Citadel** example project (`examples/crystal_citadel`).

> **Update (`018-game-dev-engine-cleanup`)**: All 9 findings below have been resolved or explicitly codified in Platformator under feature `specs/018-game-dev-engine-cleanup/`. Each section documents both the original finding and its resolution.

---

## 1. `platformator::Runtime(const RuntimeOptions &)` Skips Initial Scene Loading

- **Status**: **Resolved** (`FR-001`)
- **Affected area**: `src/runtime.cpp`, `src/runner.cpp`, `src/benchmark_main.cpp`
- **Observed behavior**:
  Constructing `platformator::Runtime runtime(options)` directly (e.g., for automated `--self-test` harnesses or custom embedding outside `platformator::run()`) previously did **not** load `options.sceneFilePath`. Stepping frames immediately after construction produced an empty world until `runtime.loadScene(options.sceneFilePath)` was called explicitly.
- **Resolution**:
  `Runtime::Runtime(const RuntimeOptions &)` (`src/runtime.cpp`) now loads `runtimeOptions.sceneFilePath` automatically when non-empty, rolling back `gameManager` and `activeRuntime()` if scene loading throws. `src/runner.cpp`, `src/benchmark_main.cpp`, and `examples/crystal_citadel/main.cpp` no longer call a redundant post-construct `loadScene()`.

---

## 2. `RuntimeOptions::assetRoot` Contract (`assets/` Folder vs Application Root)

- **Status**: **Resolved — Documented Contract** (`FR-002`)
- **Affected area**: `src/pathmanager.cpp`, `include/platformator/runtimeoptions.h`
- **Observed behavior**:
  Serialized paths in `.scene`, `.pttemplate`, `.animset`, and C++ `AssetReference` values begin with `"assets/..."`, which `PathManager` strips before joining with `root` — meaning `options.assetRoot` (and `--asset-root`) expects the `assets/` directory itself rather than the application directory. Passing `<exe_dir>/` manually breaks resolution, whereas leaving `options.assetRoot` empty auto-discovers `<exe_dir>/assets` or `<exe_dir>/../assets`.
- **Resolution**:
  `include/platformator/runtimeoptions.h` now documents explicitly that `assetRoot` expects the `assets/` folder itself and auto-discovers `<exe_dir>/assets` or `<exe_dir>/../assets` when empty.

---

## 3. `AssetReference` Fields in `SERIALIZABLE_SCRIPT` Cannot Be Optional (`""` Throws at Load)

- **Status**: **Resolved** (`FR-003`)
- **Affected area**: `include/platformator/assetreference.h`
- **Observed behavior**:
  Leaving an `AnimationClipRef`, `TemplateAssetRef`, `TextureAssetRef`, or `AudioAssetRef` field as `""` in JSON previously threw during scene/template deserialization, preventing shared behaviors from leaving optional asset slots unset.
- **Resolution**:
  `AssetReference(const std::string &)` and `AssetReference::setFilePath(const std::string &)` (`include/platformator/assetreference.h`) now treat an empty string `""` identically to `clear()` (`filePath = std::nullopt`, `hasFilePath() == false`, `resolve()` returns `nullptr`), while non-empty missing, malformed, or root-escaping paths still fail with diagnostics.

---

## 4. `SERIALIZABLE_SCRIPT` Is Hard-Capped at 16 Fields

- **Status**: **Resolved** (`FR-005`)
- **Affected area**: `include/platformator/scriptregistration.h`
- **Observed behavior**:
  `PLATFORMATOR_FOR_EACH` previously supported only 1–16 fields, causing both `CitadelPlayer` and `CitadelArchonBoss` to hit the 16-field ceiling.
- **Resolution**:
  `include/platformator/scriptregistration.h` expanded `PLATFORMATOR_FOR_EACH_1`..`32` and `PLATFORMATOR_HAS_FIELDS_FLAG` to support up to **32 fields** per behavior and unified the field/metadata expansion ladders onto `PLATFORMATOR_FOR_EACH`.

---

## 5. Schema & Path-Prefix Asymmetry Between `.scene` and `.pttemplate`

- **Status**: **Resolved** (`FR-007`)
- **Affected area**: `src/gamemanager.cpp`, `include/platformator/gameobject.h`
- **Observed behavior**:
  `.pttemplate` files use a versioned JSON object envelope, whereas `.scene` files are bare top-level JSON arrays (`[...]`). Meanwhile, runtime `TemplateAssetRef` paths used `"assets/templates/<Name>.pttemplate"`, while editor scene-object `"template"` provenance omitted the prefix (`"templates/<Name>.pttemplate"`).
- **Resolution**:
  Per design clarification, `.scene` files intentionally remain bare top-level JSON arrays `[...]`, and `GameManager::loadTemplate` / `GameManager::findTemplate` (`src/gamemanager.cpp`) now normalize leading `"templates/"` (and `"templates\\"`) prefixes to `"assets/" + filePath` before alias lookup and asset resolution.

---

## 6. `WindowSettings::headless = true` Incompatible with Sprite/Audio Scenes

- **Status**: **Resolved — Documented Contract** (`FR-004`)
- **Affected area**: `include/platformator/windowsettings.h`, `tests/runner_failure_test.cpp`
- **Observed behavior**:
  Setting `options.windowSettings.headless = true` skips SDL video/audio initialization, causing texture/audio-backed scenes to fail during load.
- **Resolution**:
  `WindowSettings::headless = true` intentionally remains strict physics-only mode without SDL video/audio initialization (preserving `runner_failure_test`), and `include/platformator/windowsettings.h` documents using `hidden = true` combined with `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and `SDL_RENDER_DRIVER=software` for full-scene automated tests.

---

## 7. Mixed Namespaces & `behaviorquery.h` Lookup Ergonomics

- **Status**: **Resolved** (`FR-006`)
- **Affected area**: `include/platformator/behaviorquery.h`, `examples/crystal_citadel/main.cpp`
- **Observed behavior**:
  Core ECS types live in the global namespace while `Runtime` and template APIs live in `namespace platformator`, and calling `getBehavior<T>(obj)` from non-`Behavior` code previously failed compilation even after including `platformator/behaviorquery.h`.
- **Resolution**:
  `include/platformator/behaviorquery.h` now declares free function `template <typename T> T *getBehavior(GameObject *)` in the global namespace, exposes `using ::getBehavior;` in `namespace platformator`, and delegates `Behavior::getBehavior<T>` to it. `examples/crystal_citadel/main.cpp` now uses `platformator::getBehavior<T>` directly without a local helper.

---

## 8. `PLATFORMATOR_ENABLE_DEBUG_TOOLS` and `DebugSettings` Overlays Default to `ON` Even in `Release` Builds

- **Status**: **Resolved** (`FR-008`)
- **Affected area**: Root `CMakeLists.txt`, `include/platformator/debugsettings.h`, `src/runtimeoptions.cpp`
- **Observed behavior**:
  `PLATFORMATOR_ENABLE_DEBUG_TOOLS` previously defaulted to `ON` even in `Release` builds, and `DebugSettings` defaulted `showColliders`, `showCollisionPoints`, `showCollisionNormals`, and `showGridCells` to `true`, causing CLI `Release` builds to ship with visual debug overlays and verbose `PLATFORMATOR_LOG` collider/asset chatter.
- **Resolution**:
  `include/platformator/debugsettings.h` now defaults all four visual overlay booleans to `false` (while `parseDebugDrawSettings("--debug-draw all")` in `src/runtimeoptions.cpp` explicitly enables all four), and root `CMakeLists.txt` defaults `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` whenever `CMAKE_BUILD_TYPE` matches `Release` or `MinSizeRel`.

---

## 9. Gaps in the Editor-Generated `CMakeLists.txt` (and `examples/mario` Packaging Quirks)

- **Status**: **Resolved** (`FR-009`, `FR-010`, `FR-011`)
- **Affected area**: `editor/src/project.cpp`, `examples/crystal_citadel/CMakeLists.txt`, `examples/mario/CMakeLists.txt`
- **Observed behavior**:
  1. `generate_project_cmakelists` previously omitted a `POST_BUILD` copy of `assets/` next to the built executable, breaking multi-level out-of-tree builds such as `bin/crystal_citadel`.
  2. `generate_project_cmakelists` lacked `install()` rules and relocatable `$ORIGIN/../lib` (`@loader_path/../lib`) RPATH configuration.
  3. `examples/mario/CMakeLists.txt` carried an obsolete check for engine fallback files (`missing.png`/`missing.wav`/`missing.animset`) and copied the engine's entire `assets/` folder into Mario's build/install outputs.
- **Resolution**:
  1. `generate_project_cmakelists` (`editor/src/project.cpp`) now emits a guarded `POST_BUILD` copy of `${CMAKE_CURRENT_SOURCE_DIR}/assets` to `$<TARGET_FILE_DIR:<target>>/assets` (removing `assets/behaviors/`), cross-platform `install()` rules (target binary, runtime shared libraries, and `assets/` excluding `behaviors`), and `INSTALL_RPATH` (`$ORIGIN/../lib` on Unix, `@loader_path/../lib` on macOS).
  2. `examples/crystal_citadel/CMakeLists.txt` is synced with the updated Editor template and installs a standalone relocatable bundle cleanly via `cmake --install --strip`.
  3. `examples/mario/CMakeLists.txt` dropped the obsolete `missing.*` fallback check and full engine `assets/` copy while keeping `level1.scene` staging and `platformator_package_smoke` intact.
