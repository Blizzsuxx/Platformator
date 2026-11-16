# Validation Quickstart: Engine Usability & Packaging Cleanup (`018-game-dev-engine-cleanup`)

Use these commands after implementation to verify every requirement (`FR-001`–`FR-012`) and success criterion (`SC-001`–`SC-006`).

## 1. Debug Build & Full Engine/Editor Test Suite

```sh
cmake --preset cleanup-debug
cmake --build bin/cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke
```

Build and run the Editor test suite (including `project_test`, `templates_test`, `workspace_lifecycle_test`, `discovery_test`):

```sh
cmake -S . -B bin/editor-debug -GNinja -DCMAKE_BUILD_TYPE=Debug -DPLATFORMATOR_BUILD_EDITOR=ON
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug --output-on-failure -R "project_test|templates_test|workspace_lifecycle_test|discovery_test"
```

Verify the newly added / updated engine cleanup tests specifically cover:
- Direct `Runtime(options)` construction with non-empty `sceneFilePath` and constructor rollback on invalid scenes (`FR-001`).
- Empty `""` `AssetReference` slots across textures, audio, animations, and templates alongside strict failure on non-empty missing paths (`FR-003`).
- `SERIALIZABLE_SCRIPT` compilation, serialization, inspection, and deserialization with >16 fields up to 32 fields (`FR-005`).
- Free-function `getBehavior<T>` and `platformator::getBehavior<T>` calls outside `Behavior` subclasses (`FR-006`).
- Runtime `loadTemplate("templates/...")` and `TemplateAssetRef` resolution matching `"assets/templates/..."` (`FR-007`).
- Default-off `DebugSettings` overlay booleans (`FR-008`).

## 2. Standalone CLI `Release` Build, Post-Build Run & Redistributable Install

Verify that `crystal_citadel` configured with the Editor-generated `CMakeLists.txt` via CLI `-DCMAKE_BUILD_TYPE=Release` automatically disables debug tools, copies `assets/` (without `behaviors/`), runs out-of-tree without extra flags, and installs cleanly:

```sh
cmake -S examples/crystal_citadel -B bin/cc-release -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build bin/cc-release --parallel 4

# Verify post-build assets/ present and assets/behaviors excluded
test -d bin/cc-release/assets && test ! -d bin/cc-release/assets/behaviors

# Verify zero debug log chatter and self-test pass from another working directory
cd /tmp && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  /home/user/Projects/Platformator/bin/cc-release/crystal_citadel --self-test

# Verify relocatable install tree works from /tmp
cmake --install bin/cc-release --prefix bin/cc-install-test --strip
test ! -d bin/cc-install-test/assets/behaviors
ldd bin/cc-install-test/bin/crystal_citadel
cd /tmp && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  /home/user/Projects/Platformator/bin/cc-install-test/bin/crystal_citadel --self-test
```

## 3. SDK Packaging & Mario Smoke Check

Verify `tests/package_smoke.cmake` and `mario_example` still pass after removing obsolete `missing.*` checks from `examples/mario/CMakeLists.txt`:

```sh
ctest --test-dir bin/cleanup-debug --output-on-failure -R package_smoke
```
