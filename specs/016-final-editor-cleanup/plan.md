# Implementation Plan: Final Editor Cleanup

**Branch**: `016-final-editor-cleanup` | **Date**: 2026-11-13 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/016-final-editor-cleanup/spec.md`

## Summary

Consolidate and polish the Platformator editor into a release-grade, Unity-inspired workspace across both the editor and the runtime physics/serialization layers:
- **Project Lifecycle & CMake Scaffolding**: Complete **New Project...**, **Open Project...**, MRU **Recent Projects**, workspace state isolation on project switch, and non-blocking **Generate CMakeLists.txt** using installed `find_package(Platformator CONFIG)` with automatic fallback to local source `add_subdirectory`.
- **Single-Source C++ Behavior Scanner & `--editor-catalog` Removal**: Replace the `--editor-catalog` subprocess check with a static C++ header/source scanner (`SourceBehaviorScanner`) extracting `SERIALIZABLE_SCRIPT(...)` classes, fields, defaults, references, enums, and containers; negotiate embedded Play Mode capabilities directly during the launch IPC handshake (`capabilities` -> `ready`).
- **Project Settings Window & Dock Cleanup**: Consolidate **Build & Run** and **Layers & Physics Matrix** into **Edit → Project Settings...** / **Window → Project Settings** with left-side category tabs; remove bottom dock settings tab, `Refresh Assets` button (moved to right-click context menu), `Catalog protocol`, `Recheck Sources`, `Rediscover Targets`, duplicate transport buttons, and permanent `Object Template` clutter on non-template GameObjects.
- **Physics Layers (32 Named Layers + Collision Matrix)**: Add `GameObject::layer` (`0..31`, `"Default"` locked at `0`), per-project `.platformator/layers.json`, symmetric `uint32_t[32]` collision matrix UI, and lock-free `O(1)` narrow-phase filtering inside `PhysicsManager::processNarrowPhasePair` preserving `Grid` cell witness bookkeeping invariants.
- **Single-Level Template (Prefab) Editing & Sync**: Isolated **Template Editing Mode** (`DocumentTabKind::Template`), linked scene instances (`template` metadata + property overrides + Revert/Apply/Unpack), automatic single-level flattening on save, and live propagation of non-overridden changes/structure across open scenes.
- **Audio Preview & Workspace Ergonomics**: Double-click audio preview (`AudioPreviewService` using linked `SDL3_mixer`), curated **Window → Layouts** presets, reactive debounced validation, and unified dirty-state save prompts.

## Technical Context

**Language/Version**: C++20 (`CMAKE_CXX_STANDARD 20`)
**Primary Dependencies**:
- Core Engine (`platformator`): `SDL3` (3.4.18), `SDL3_image` (3.4.8), `SDL3_mixer` (3.2.4), `SDL3_ttf` (3.2.2), `Eigen` (5.0.1), `oneTBB` (2022.3.0), `nlohmann_json`
- Editor (`platformator_editor`): `platformator_engine`, `Dear ImGui` (docking branch via `platformator_imgui`), POSIX/platform process & file watcher layers (`editor/src/*`)
**Storage**: Local filesystem JSON configs — per-project `.platformator/layers.json`, workspace settings/layout JSON (`preferences`), `.scene` / `.pttemplate` JSON, global preferences (`recentProjects`).
**Testing**: CTest + unit/integration test binaries (`bin/editor-debug`, including `platformator_editor_*_test` and engine physics/serializer tests)
**Target Platform**: Linux (primary exercised editor target; cross-platform C++ abstractions retained in `editor/src/`)
**Project Type**: C++20 2D game engine + native desktop ImGui/SDL3 editor (`src/`, `include/`, `editor/`, `tests/`)
**Performance Goals**:
- Narrow-phase layer filtering MUST add zero heap allocations, zero locks, and O(1) bitwise latency (`(matrix[a] >> b) & 1u`) inside `tbb::parallel_for` physics workers.
- Static behavior/field scanning runs incrementally off the UI thread (mtime/size cache) so typing/dragging stays responsive at 60 FPS without stalls.
- Audio preview toggles cleanly with single reusable `MIX_Track` without blocking frames.
**Constraints**:
- Preserve backward compatibility with existing `.scene` and `.pttemplate` files missing `"layer"` or `"template"` keys (default `layer = 0`, all collision bits `0xFFFFFFFFu`).
- Single-level templates only; saving nested instances flattens inner linkage metadata cleanly.
- No benchmark matrix expansion (`AGENTS.md` compliance).

## Constitution Check

*GATE: Evaluated before Phase 0 Research and re-evaluated after Phase 1 Design.*

| Principle | Pre-Design Assessment | Post-Design Verification |
|-----------|-----------------------|--------------------------|
| **I. Performance & Scalability First** | Layer check must avoid extra hash lookups/allocations per collider pair; behavior scanning must not stall UI frames. | **PASS**: Layer check is a fixed `std::array<std::uint32_t, 32>` indexed by `layer & 31` (`O(1)`, L1-cached, zero allocations). Behavior scanner caches `(mtime, size)` per file and runs asynchronously off UI thread. |
| **II. Multicore Architecture with oneTBB** | Narrow phase (`PhysicsManager::narrowPhase`) runs under `tbb::parallel_for`; collision matrix lookup MUST be thread-safe and lock-free. | **PASS**: `collisionMatrix_` is loaded/immutable during the physics step; worker lambdas in `processNarrowPhasePair` only perform read-only bitwise loads on `collisionMatrix_`, introducing zero contention or false sharing. |
| **III. Research-Backed Collision Architecture** | `Grid` separates cell Sweep-and-Prune witnesses (`applyPairWitnessDelta`) from narrow-phase contact generation. Filtering must not corrupt pair/cell witness counts. | **PASS**: Layer filtering executes in `PhysicsManager::processNarrowPhasePair` right alongside `getActive()` / `isRetired()` (`clearCollisionNoEvent(); return;`). `Grid` cell witness counts and `pairAdjacency` stay intact regardless of dynamic `GameObject::setLayer()` changes; `setLayer()` marks colliders dirty/queues pairs cleanly when mutated dynamically. |
| **IV. Simple Code & KISS** | Replacing dual catalog mechanisms and nested prefab complexity with unified simpler mechanisms. | **PASS**: Removes `--editor-catalog` subprocess path (`BehaviorCatalogQuery`, `CatalogCheck` stage), keeps single-level templates (auto-flattening nested ones), and reuses existing `SDL3_mixer` and `DocumentWorkspace` infrastructure instead of custom subsystems. |
| **V. Unity-Inspired Developer Experience** | User-requested DX parity: Project Settings, Layers/Matrix, Template Editing Mode, clean docks, double-click audio preview. | **PASS**: Designs map directly to familiar Unity flows (**Edit → Project Settings...**, Inspector `Layer` dropdown + Template `Open/Apply/Revert/Unpack` header bar, **Window → Layouts**, reactive Validation). |
| **VI. Evidence Before Performance Claims** | No speculative speedups or unverified benchmark claims. | **PASS**: Plan relies strictly on targeted functional/unit/integration tests (`ctest`); existing broad-phase/narrow-phase benchmarks remain untouched unless specifically requested. |
| **VII. Correctness & Architectural Integrity** | Backward compatibility for legacy `.scene`/`.pttemplate` assets; atomic undo/redo; dirty-state protection on project switch & tab close. | **PASS**: Default missing `"layer"` -> `0`, missing `layers.json` -> `0xFFFFFFFFu`, runtime `SceneBuildContext` ignores editor `"template"` metadata, and project switches serialize/prompt cleanly before tearing down workspace tabs/history. |

## Project Structure

### Documentation (this feature)

```text
specs/016-final-editor-cleanup/
├── spec.md                              # Feature specification + Q1..Q5 clarifications
├── plan.md                              # This implementation plan
├── research.md                          # Phase 0 design decisions & rationale
├── data-model.md                        # Entities, schemas, invariants, transitions
├── quickstart.md                        # Build/test commands & manual acceptance steps
└── contracts/
    ├── project-scaffold-and-cmake.md    # New project layout & find_package/fallback CMake
    ├── layer-and-scene-format.md        # .platformator/layers.json & GameObject layer semantics
    ├── template-instance-format.md      # Single-level .pttemplate & instance sync rules
    └── editor-session-handshake.md      # Embedded PlaySession capabilities without --editor-catalog
```

### Source Code (repository paths affected during implementation)

```text
include/platformator/
├── gameobject.h                         # Add uint8_t layer + getLayer()/setLayer()
├── physicsmanager.h                     # LayerSettings / collision matrix accessors & canLayersCollide()
└── scriptregistration.h                 # Remove obsolete --editor-catalog runtime catalog runner entrypoints

src/
├── gameobject.cpp                       # Layer getter/setter + dynamic collider pair re-queue hook
├── gameobjectserializer.cpp             # Serialize "layer" field
├── scenebuilder.cpp                     # Deserialize "layer" (default 0); safely ignore editor "template" metadata
├── physicsmanager.cpp                   # Lock-free O(1) layer collision check in processNarrowPhasePair()
├── runtime.cpp / gamemanager.cpp        # Load .platformator/layers.json on runtime startup; remove --editor-catalog CLI path
├── editorsession.cpp                    # Emit "capabilities" at launch cleanly without CLI catalog dependency
└── scriptregistration.cpp               # Retain runtime serialization registration; remove --editor-catalog printing path

editor/
├── CMakeLists.txt                       # Wire new/renamed sources (ProjectSettingsPanel, AudioPreviewService, ProjectScaffold, SourceScanner)
└── src/
    ├── project.h / project.cpp          # Missing-CMake classification, project creation scaffold, Generate CMakeLists.txt
    ├── preferences.h / preferences.cpp  # GlobalPreferences (recentProjects, engine root fallback), remove catalogProtocol, migrate panel.run -> panel.project_settings
    ├── layersettings.h / layersettings.cpp  # NEW: LayerSettings load/save/normalize/symmetry helpers
    ├── sourcescanner.h / sourcescanner.cpp  # NEW (or split from behaviorcatalog): Incremental static C++ SERIALIZABLE_SCRIPT + field/enum/default scanner
    ├── behaviorcatalog.h / behaviorcatalog.cpp  # Remove --editor-catalog subprocess catalog query; route catalog exclusively through source scanner
    ├── process.cpp                      # Remove PipelineStage::CatalogCheck / --editor-catalog execution
    ├── playsession.h / playsession.cpp  # Remove pre-launch catalog requirement; validate "capabilities" from child launch handshake
    ├── audiopreview.h / audiopreview.cpp # NEW: Lazy SDL3_mixer audio preview controller (play/toggle/stop/cleanup)
    ├── templates.h / templates.cpp      # Single-level template flattening, instance link/override diff & propagate-to-open-scenes
    ├── documentmanager.h / documentmanager.cpp  # DocumentTabKind::Template (Template Editing Mode), workspace isolation helpers
    ├── projectsession.cpp               # Reactive debounced validation on file/scene/layer updates
    ├── editorapp.h / editorapp.cpp      # Menu integration (New/Open/Recent Project, Edit->Project Settings, Window->Layouts), unified dirty prompt on project switch
    └── panels/
        ├── projectsettings.h / projectsettings.cpp  # NEW (replaces runsettings): Unified Project Settings (Build & Run + Layers & Physics Matrix)
        ├── runsettings.h / runsettings.cpp          # Removed/subsumed into ProjectSettingsPanel
        ├── project.cpp                  # Context-menu Refresh, remove Refresh Assets/Load Template/Assign buttons, double-click .pttemplate -> Template Mode, audio -> AudioPreviewService
        ├── inspector.cpp                # Layer dropdown next to Tag, compact Template Instance header bar (Open/Apply/Revert/Unpack) only on linked objects, bold overridden properties
        ├── hierarchy.cpp                # Template context actions ("Save as Template...", "Instantiate Template"), clean visual indicator on linked template roots
        ├── behaviorfields.cpp           # Always render source-scanned fields cleanly (remove "Build to load field" blocker)
        └── console.cpp                  # Clean compact console/validation dock without settings tabs

tests/
├── unit/ / integration/                 # New & updated CTest targets covering:
│                                        # - Layer collision matrix symmetry & PhysicsManager filtering + dynamic setLayer()
│                                        # - GameObject/Scene layer backward-compatible serialization
│                                        # - Project scaffold + Generate CMakeLists.txt (installed + fallback CMake)
│                                        # - SourceBehaviorScanner (primitives, Vector2, Rectangle, AssetRef, ObjectRef, std::vector, std::optional, enums, multi-line macros, comments)
│                                        # - Removal of --editor-catalog & PlaySession launch capability handshake
│                                        # - Single-level Template flattening, instance overrides, Revert/Apply/Unpack, sync propagation
│                                        # - Clean project switching & preferences migration
```

**Structure Decision**: Keep runtime layer filtering and serialization inside core `include/platformator/` & `src/` (so both standalone built games and embedded play sessions obey project layers identically) and keep editor-only UX/scanning/scaffolding/audio-preview cleanly decoupled inside `editor/src/`, reusing existing `SDL3_mixer` and `ImGui` targets with zero new third-party dependencies.

## Complexity Tracking

No Constitution violations require justification. All design choices simplify existing dual code paths (`--editor-catalog` vs source scanner, bottom dock Run Settings vs Project Settings) and respect Broad-Phase/Narrow-Phase/oneTBB architectural invariants.
