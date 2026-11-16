# Feature Specification: Engine Usability and Packaging Cleanup from Game Development Findings

**Feature Branch**: `018-game-dev-engine-cleanup`

**Created**: 2026-11-16

**Status**: Draft

**Input**: User description: "perform a \"engine cleanup\" given the findings from developing a game"

## Clarifications

### Session 2026-11-16

- Q: Should scene files keep their existing bare JSON array format while we normalize template paths, or should we also introduce a versioned JSON object wrapper for scenes? (FR-007) → A: Keep `.scene` files as a top-level JSON array `[...]` and only normalize `"assets/templates/..."` vs `"templates/..."` template paths across runtime and editor.
- Q: How should headless execution handle scenes that load textures, animations, and audio while preserving the existing physics-only `headless = true` contract test? (FR-004) → A: Leave `headless` behavior unchanged (`headless = true` remains physics-only without video/audio subsystem initialization) and document the `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software` (`hidden = true`) pattern for full-scene tests.
- Q: How should we change the defaults for `DebugSettings` visual overlays and the `PLATFORMATOR_ENABLE_DEBUG_TOOLS` build option so games don't launch looking like debug builds? (FR-008) → A: Default `DebugSettings` overlay booleans (`showColliders`, `showCollisionPoints`, `showCollisionNormals`, `showGridCells`) to `false` AND default `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` in `Release`/`MinSizeRel` builds.
- Q: What should the Editor-generated `CMakeLists.txt` include for project `assets/` and `cmake --install` packaging? (FR-009, FR-010) → A: Add both a `POST_BUILD` copy of `assets/` (excluding `assets/behaviors/`) to `<target_dir>/assets` AND cross-platform `install()` + RPATH rules for standalone distribution.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Predictable Runtime Initialization, Optional Asset Slots, and Documented Test Modes (Priority: P1)

A game developer or automated test harness configures a game session with a startup scene, optional asset slots on reusable scripts, and clear physics-only vs dummy-driver test modes, getting predictable startup and asset validation without trial-and-error workarounds.

**Why this priority**: Runtime startup surprises, mandatory-only script asset fields, and unclear headless vs dummy-driver behavior create immediate friction on every multi-scene game project.

**Independent Test**: Initialize a session directly with a configured startup scene path; load reusable objects with both populated and intentionally empty optional asset slots; verify validation of non-empty missing paths and run full-scene tests cleanly with documented hidden/dummy driver settings while preserving physics-only `headless = true` behavior.

**Acceptance Scenarios**:

1. **Given** a runtime configuration with a non-empty startup scene path, **When** a session is initialized directly from that configuration, **Then** the configured startup scene is loaded and ready for simulation without requiring a separate manual load step (while an empty startup scene path still initializes an empty session).
2. **Given** an explicit asset root path is omitted in runtime configuration, **When** the session resolves logical `assets/...` requests, **Then** standard build-tree (`<executable-dir>/assets`), sibling (`<executable-dir>/../assets`), and installed layouts are discovered automatically and documented clearly so callers know custom overrides target the `assets/` folder itself.
3. **Given** a script behavior declares texture, audio, animation, or template reference fields and a scene or template sets an optional slot to an empty value (`""`), **When** the scene or template loads, **Then** loading succeeds with that reference remaining unset/empty rather than rejecting the empty string.
4. **Given** a non-empty asset reference points to a missing, invalid, or root-escaping path, **When** the scene or template loads, **Then** loading still fails immediately with an actionable error naming the field and requested path.
5. **Given** a developer setting up automated tests, **When** they consult runtime configuration docs/headers, **Then** `headless = true` is clearly documented as strict physics-only mode (no video/audio subsystem initialization) and `hidden = true` with dummy SDL video/audio and software renderer drivers is documented for full sprite/audio scene tests.

---

### User Story 2 - Consistent Behavior Authoring, Queries, and Scene/Template Conventions (Priority: P1)

A game developer writes complex gameplay scripts, inspects sibling or external scripts, and authors `.scene` and `.pttemplate` files using sufficient field limits, uniform query helpers, and normalized template path references without hitting unexpected macro caps or path mismatches.

**Why this priority**: Behavior serialization limits, inconsistent lookup visibility, and `"assets/templates/..."` vs `"templates/..."` path discrepancies caused repeated friction while authoring Crystal Citadel.

**Independent Test**: Define and serialize a behavior with more than 16 persistent fields (up to at least 32 fields); query behaviors from both script subclasses and external application entry points; save/load bare-array `.scene` files and `.pttemplate` files while normalizing template path references across runtime and editor.

**Acceptance Scenarios**:

1. **Given** a complex character or boss behavior with between 17 and 32 serialized fields, **When** the behavior is registered, built, serialized, and loaded from scene or template data, **Then** compilation and serialization succeed cleanly for all fields without requiring manual field trimming, and exceeding any supported limit emits a clear compile-time diagnostic.
2. **Given** an application entry point or external harness holding an object reference outside a behavior subclass, **When** it invokes the documented behavior lookup helper, **Then** the lookup resolves identically regardless of whether it is called inside a behavior class or from free application code.
3. **Given** `.scene` files (formatted as top-level JSON arrays `[...]`) and `.pttemplate` files, **When** template links are authored, resolved, or saved across runtime and editor, **Then** `"assets/templates/..."` and `"templates/..."` template paths normalize consistently without breaking existing projects or requiring a scene envelope format change.

---

### User Story 3 - Clean Production Defaults and Standalone Project Build/Distribution Scaffolding (Priority: P2)

A game developer builds a standalone game project using the engine's generated `CMakeLists.txt` and gets clean default visuals (no unintended debug overlays) plus both local multi-level out-of-tree runs and relocatable `cmake --install` distributions free of console log spam, missing assets, or obsolete engine fallback requirements.

**Why this priority**: Standalone games built with the generated project build file should run cleanly out of the box across build layouts and ship as self-contained release builds without manual build-script rewrites.

**Independent Test**: Generate a standalone project `CMakeLists.txt`, configure/build both `Debug` and `Release` builds from CLI and Editor in multi-level out-of-tree build directories, install to a staging prefix, verify RPATH/bundled assets (excluding `assets/behaviors/`), and inspect default visuals and console output.

**Acceptance Scenarios**:

1. **Given** default `DebugSettings` without explicit CLI flags or overrides, **When** a game runs in either development or release mode, **Then** `showColliders`, `showCollisionPoints`, `showCollisionNormals`, and `showGridCells` all default to `false` (off unless explicitly requested), and configuring `Release` or `MinSizeRel` builds defaults `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` so release builds suppress debug tools and per-collider/per-asset console log chatter automatically.
2. **Given** a standalone project built with the Editor-generated `CMakeLists.txt` in any build directory (single-level or multi-level out-of-tree), **When** compilation completes, **Then** a `POST_BUILD` step copies the project's `assets/` (excluding `assets/behaviors/`) next to the target executable so the binary runs immediately without extra CLI overrides.
3. **Given** a standalone project built with the Editor-generated `CMakeLists.txt` and installed to a staging prefix (`cmake --install`), **When** installation completes, **Then** cross-platform `install()` rules and relocatable RPATH (`$ORIGIN/../lib` on Linux, `@loader_path/../lib` on macOS, plus runtime DLL handling on Windows) produce a self-contained runnable game bundle containing the executable, required runtime shared libraries, and game runtime `assets/` (excluding `assets/behaviors/`), with no unneeded `missing.*` or internal engine test-scene copies.

---

### Edge Cases

- What happens when a runtime session is constructed with an empty `sceneFilePath`? It starts in an unloaded/empty state without error and supports subsequent explicit scene loading.
- What happens when an empty (`""`) optional asset reference is dereferenced by gameplay code at runtime? It safely reports unset/null without crashing, remaining clearly distinguishable from a non-empty missing-file error during load.
- What happens when existing projects pass `--debug-draw colliders,grid` in a debug-enabled build? Explicit debug overlay flags continue to enable the requested overlays even though `DebugSettings` booleans default to `false`.
- What happens when a physics-only `headless = true` session attempts to initialize display/audio vs when a full-scene dummy-driver test runs with `hidden = true`? `headless = true` continues to reject video/audio initialization (`SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0` preserved for `runner_failure_test`), while `hidden = true` with dummy SDL drivers validates texture/audio assets on disk normally.
- What happens with existing scenes using bare top-level JSON arrays and `"templates/<Name>.pttemplate"` provenance links, or existing `Mario` scenes? Full backward compatibility is preserved so existing example projects continue to build and pass without manual scene rewrites.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Initializing a runtime session with a non-empty startup scene configuration MUST load that scene during session initialization, while preserving empty-session initialization when no startup scene is specified.
- **FR-002**: Runtime configuration documentation and error messages for asset root resolution MUST clearly specify that custom asset root paths target the `assets/` directory itself (while default discovery when omitted handles both executable-adjacent and parent-relative `assets/` directories across build and install layouts).
- **FR-003**: Script asset reference fields (texture, audio, animation clip, and object template references) MUST allow empty values (`""`) to represent intentionally unset optional slots while continuing to reject any non-empty missing, malformed, or root-escaping path with a diagnostic identifying the field and requested path.
- **FR-004**: Keep `windowSettings.headless = true` as strict physics-only mode without SDL video/audio initialization, and document the `windowSettings.hidden = true` + dummy SDL drivers (`SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `SDL_RENDER_DRIVER=software`) pattern for automated tests on scenes with textures and audio.
- **FR-005**: Behavior field serialization registration MUST support at least 32 fields per behavior (doubling the current 16-field limit) and emit a clear diagnostic if a declared limit is exceeded.
- **FR-006**: Public behavior query utilities and headers MUST expose a consistent, self-contained lookup interface usable from both behavior subclasses and non-behavior application code without forcing callers to open-code component traversal.
- **FR-007**: Keep `.scene` files as bare top-level JSON arrays `[...]` (no wrapper format change) while reconciling `"assets/templates/..."` vs `"templates/..."` template path normalization across editor provenance metadata and runtime template loading, preserving compatibility with existing scenes.
- **FR-008**: Default all four `DebugSettings` visual overlay booleans (`showColliders`, `showCollisionPoints`, `showCollisionNormals`, `showGridCells`) to `false` unless explicitly requested via configuration or CLI flags, and default `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` when `CMAKE_BUILD_TYPE` is `Release` or `MinSizeRel` (suppressing debug overlays and per-collider/per-asset stdout chatter in release builds automatically).
- **FR-009**: Update the Editor-generated `CMakeLists.txt` (`generate_project_cmakelists`) to include a `POST_BUILD` step copying project `assets/` (excluding `assets/behaviors/`) to `$<TARGET_FILE_DIR:...>/assets` so single-level and multi-level build directories run without manual asset copying or extra CLI overrides.
- **FR-010**: Update the Editor-generated `CMakeLists.txt` (`generate_project_cmakelists`) to include cross-platform `install()` rules (executable, bundled runtime shared libraries, and project `assets/` excluding `assets/behaviors/`) plus relocatable `INSTALL_RPATH` (`$ORIGIN/../lib` on Linux, `@loader_path/../lib` on macOS) so `cmake --install` produces a self-contained redistributable game folder.
- **FR-011**: Clean up obsolete engine-asset fallback requirements in standalone example scaffolding (removing unneeded engine `missing.png`/`missing.wav`/`missing.animset` checks and test-scene copies from `examples/mario` where not required, while preserving SDK packaging contract test coverage where appropriate).
- **FR-012**: All existing engine invariants — physics collision detection and broad-phase behavior, runtime template instantiation (parented, composite reference-remapping, and recursive templates), deferred scene transitions, and existing examples/test suites — MUST remain intact.

### Key Entities

- **Runtime Session Configuration**: Startup scene selection, asset root path specification, physics-only `headless` vs `hidden` presentation flags, and default-off debug-overlay settings governing session initialization.
- **Optional Asset Reference**: A typed resource link attached to a script field distinguishing an intentionally unset empty slot (`""`) from a non-empty asset request that must resolve and validate under the configured asset root.
- **Behavior Field Registration**: Compile-time declaration connecting C++ behavior member variables to scene/template serialization and editor inspection supporting at least 32 fields.
- **Template Provenance Path**: Canonicalized template reference connecting editor scene-instance metadata and runtime template instantiation without breaking legacy `"templates/..."` scene entries.
- **Standalone Project Distribution**: A self-contained installed or post-build game folder containing the relocatable executable, bundled runtime shared libraries, and game runtime assets (excluding `assets/behaviors/` source files and engine test fixtures).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of the 9 engine usability and packaging findings documented during game development are resolved (or have documented intentional contracts per Clarifications) with dedicated regression coverage passing in automated tests.
- **SC-002**: Release builds run multi-scene tests cleanly using documented hidden/dummy SDL drivers (`SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `SDL_RENDER_DRIVER=software`) with zero per-collider or per-asset console log lines and while preserving physics-only `headless = true` contract tests (`runner_failure_test`).
- **SC-003**: A behavior declaring more than 16 serialized fields (up to 32) builds, saves, reloads from scene/template JSON, and executes cleanly in automated tests, and objects with intentionally empty optional asset slots load and run without errors.
- **SC-004**: Running a single clean CLI `Release` configure, build, and `cmake --install` sequence on a project using the Editor-generated `CMakeLists.txt` produces both a runnable out-of-tree build directory and a relocatable, self-contained installed game directory that pass multi-scene verification from any working directory with 0 manually copied files, 0 extra CMake debug flags, and 0 behavior `.cpp`/`.h` files leaked into runtime `assets/`.
- **SC-005**: Default interactive game launches in both development and release builds render game visuals cleanly with debug overlays disabled unless explicitly requested via CLI or settings.
- **SC-006**: All existing engine, editor, and example regression suites (`ctest` suite plus both `mario` and `crystal_citadel` verification runs) pass 100% with zero regressions.

## Assumptions

- Empty string (`""`) or omitted/null JSON value is the canonical representation for an intentionally unset optional script asset reference, matching existing animator/audio component behavior.
- Supporting up to 32 serialized fields per behavior covers complex character and boss scripts without requiring an unbounded variadic reflection rewrite; future needs beyond 32 can group fields into sub-structs if necessary.
- `.scene` files continue using their existing bare top-level JSON array `[...]` format indefinitely (no scene envelope wrapper will be added), while template path references normalize consistently across runtime and editor metadata.
- `windowSettings.headless = true` stays strict physics-only (no SDL video/audio init); full-scene headless CI runs use `windowSettings.hidden = true` with dummy SDL video/audio drivers.
- Windows runtime DLL bundling continues alongside Unix/macOS `$ORIGIN/../lib` / `@loader_path/../lib` RPATH packaging and cross-platform `install()` rules inside generated project build configurations so desktop distribution is handled uniformly.
