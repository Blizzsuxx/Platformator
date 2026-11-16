# Implementation Plan: Engine Usability and Packaging Cleanup from Game Development Findings

**Branch**: `018-game-dev-engine-cleanup` | **Date**: 2026-11-16 | **Spec**: [spec.md](./spec.md)  
**Input**: Feature specification from `/specs/018-game-dev-engine-cleanup/spec.md`

## Summary

Resolve the usability, authoring, default-setting, and packaging friction uncovered while building **Crystal Citadel** ([`examples/crystal_citadel/ENGINE_ISSUES.md`](../../examples/crystal_citadel/ENGINE_ISSUES.md)) using targeted, minimal edits grounded in **KISS**:
1. Load `options.sceneFilePath` inside `platformator::Runtime::Runtime(const RuntimeOptions &)` with exception rollback (`src/runtime.cpp`), eliminating duplicate runner loading and clearing `sceneFilePath` when benchmark `--scenario` overrides it (`src/benchmark_main.cpp`).
2. Treat `""` in `AssetReference<T>` (`include/platformator/assetreference.h`) identically to `clear()`/`null` so reusable behaviors support unset optional asset slots without weakening required component checks.
3. Raise `SERIALIZABLE_SCRIPT` field limit from 16 to 32 (`include/platformator/scriptregistration.h`) while simplifying its macro expansion to reuse `PLATFORMATOR_FOR_EACH`.
4. Add free-function `getBehavior<T>(GameObject *)` in global and `platformator::` scopes (`include/platformator/behaviorquery.h`).
5. Keep `.scene` as bare JSON array `[...]` and normalize `"templates/..."` -> `"assets/templates/..."` inside `GameManager::loadTemplate` (`src/gamemanager.cpp`).
6. Document `assetRoot` and physics-only `headless` vs `hidden` + dummy SDL driver usage (`runtimeoptions.h`, `windowsettings.h`), default `DebugSettings` overlay booleans to `false` (`debugsettings.h`), and default `PLATFORMATOR_ENABLE_DEBUG_TOOLS` to `OFF` for `Release`/`MinSizeRel` builds (`CMakeLists.txt`).
7. Update the Editor's generated `CMakeLists.txt` (`editor/src/project.cpp`) to copy project `assets/` (excluding `behaviors/`) post-build and include cross-platform `install()` + relocatable RPATH rules, sync `examples/crystal_citadel/CMakeLists.txt`, and remove obsolete engine `missing.*` copies from `examples/mario/CMakeLists.txt`.

## Technical Context

**Language/Version**: C++20 (runtime library, runner, editor) and CMake 3.16+/3.20+  
**Primary Dependencies**: SDL3, SDL3_mixer, SDL3_image, SDL3_ttf, Eigen 5, nlohmann/json, oneTBB, Dear ImGui (editor)  
**Storage**: Local filesystem `.scene` JSON arrays, `.pttemplate` v1 envelopes, `.animset` JSON, PNG/WAV assets  
**Testing**: CMake + CTest (`cleanup-debug` preset for runtime tests, `editor-debug` build for editor tests, `package_smoke`, and `crystal_citadel --self-test`)  
**Target Platform**: Linux desktop (cross-platform packaging parity with macOS & Windows)  
**Project Type**: C++20 2D game engine library (`platformator`), CLI runner, C++/ImGui Editor, and standalone example projects  
**Performance Goals**: Zero runtime allocation or per-frame overhead; `Release` builds suppress debug overlay work and `PLATFORMATOR_LOG` stdout overhead by default (`SC-002`)  
**Constraints**: Strict KISS scope — clean, minimal edits without extra abstraction layers or defensive blanket null checks; preserve `headless = true` strict physics-only invariant (`runner_failure_test`), bare-array `.scene` format, and all `ctest`/`mario`/`crystal_citadel` contracts (`FR-012`)  
**Scale/Scope**: ~10 focused files across `include/platformator/`, `src/`, `editor/src/`, `CMakeLists.txt`, and `examples/` plus targeted regression tests

## Constitution Check

*GATE: Evaluated before Phase 0 research and re-checked after Phase 1 design.*

| Principle | Assessment | Status |
|-----------|------------|--------|
| **I. Performance & Scalability First** | Disabling `PLATFORMATOR_ENABLE_DEBUG_TOOLS` in `Release`/`MinSizeRel` builds and defaulting `DebugSettings` overlays to `false` eliminates debug overlay collection and console logging overhead on default game runs. Normalized template loading adds only a single prefix check on cache miss. | **PASS** |
| **II. Multicore Design with oneTBB** | No changes to task partitioning, `Grid`, `PhysicsManager`, or `tbb::parallel_for` execution paths or thread safety boundaries. | **PASS** |
| **III. Research-Backed Collision Architecture** | Sweep-and-prune intervals, witnesses, collision pairs, and solver behavior remain completely untouched. | **PASS** |
| **IV. Simple Code & KISS** | Every fix targets the exact source: ~6 lines for empty `AssetReference`, ~10 lines for `Runtime(options)` scene load + rollback, ~3 lines for `loadTemplate` prefix normalization, unified `PLATFORMATOR_FOR_EACH` instead of duplicate 32-entry ladders, zero speculative `headless` resource stubs, and no unnecessary defensive checks. | **PASS** |
| **V. Unity-Inspired Developer Experience** | Matches developer expectations cleanly (`Runtime(options)` loads configured startup scene, unset asset refs stay `null`/empty cleanly, games boot without debug wireframes visible, standalone projects package `assets/` and libs automatically). Deliberate deviations (`headless = true` physics-only, `assetRoot` = `assets/` folder) are documented clearly in headers. | **PASS** |
| **VI. Evidence Before Performance Claims** | No speculative speedup claims made; Quickstart validates functional fixes, zero log chatter in `Release`, and full CTest/example suites. | **PASS** |
| **VII. Correctness & Architectural Integrity** | Constructor exception rollback preserves `Runtime::tryGetCurrent() == nullptr` on invalid startup scenes (`runner_failure_test`); non-empty missing paths continue failing fast with field context; bare-array `.scene` and editor `"templates/..."` contracts remain 100% intact. | **PASS** |

*Post-Phase-1 Re-check*: Reviewed `research.md`, `data-model.md`, `contracts/engine-cleanup-contracts.md`, and `quickstart.md`. All contracts align cleanly with `Clarifications`, contain zero Constitution violations, and introduce zero unnecessary complexity.

## Project Structure

### Documentation (this feature)

```text
specs/018-game-dev-engine-cleanup/
├── plan.md                                      # This file
├── research.md                                  # Phase 0 KISS decisions & alternatives
├── data-model.md                                # Entities, state transitions & layouts
├── quickstart.md                                # End-to-end validation commands
├── contracts/
│   └── engine-cleanup-contracts.md              # Public header, JSON & CMake contracts
└── checklists/
    └── requirements.md                          # Spec quality checklist
```

### Source Code (repository root)

```text
CMakeLists.txt                                   # Default PLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF in Release/MinSizeRel

include/platformator/
├── assetreference.h                             # Treat "" path as clear() in ctor / setFilePath()
├── behaviorquery.h                              # Add free ::getBehavior<T> + platformator::getBehavior<T>
├── debugsettings.h                              # Default 4 visual overlay booleans to false
├── runtimeoptions.h                             # Clarify doc comments on assetRoot & sceneFilePath
├── windowsettings.h                             # Clarify doc comments on headless (physics-only) vs hidden
└── scriptregistration.h                         # Expand capacity 16 -> 32 via unified PLATFORMATOR_FOR_EACH

src/
├── runtime.cpp                                  # Load non-empty runtimeOptions.sceneFilePath in Runtime() w/ rollback
├── runner.cpp                                   # Remove redundant post-construct loadScene call
├── benchmark_main.cpp                           # Remove duplicate loadScene; clear sceneFilePath when --scenario set
└── gamemanager.cpp                              # Normalize "templates/..." -> "assets/templates/..." in loadTemplate

editor/src/
└── project.cpp                                  # Update generate_project_cmakelists w/ POST_BUILD assets, RPATH & install()

examples/
├── crystal_citadel/CMakeLists.txt               # Sync with updated Editor-generated CMakeLists.txt
└── mario/CMakeLists.txt                         # Drop obsolete missing.* checks and full engine assets/ copy

tests/ & editor/tests/
├── runtime_foundation_test.cpp / asset_root_contract_test.cpp / runtime_template_test.cpp
│                                                # Add tests for Runtime(options) scene load, "" optional refs,
│                                                # "templates/..." loadTemplate, free getBehavior<T>, DebugSettings defaults,
│                                                # and 32-field SERIALIZABLE_SCRIPT behavior
└── project_test.cpp / workspace_lifecycle_test.cpp
                                                 # Verify POST_BUILD assets copy, RPATH & install() in generated CMakeLists.txt
```

**Structure Decision**: Modify existing engine headers, runtime sources, root build script, editor project scaffold, and existing test suites directly in place — zero new libraries, abstraction layers, or speculative helper classes.

## Complexity Tracking

*No Constitution violations — design strictly follows KISS (`§IV`).*
