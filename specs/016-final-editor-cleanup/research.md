# Research: Final Editor Cleanup (`016-final-editor-cleanup`)

**Date**: 2026-11-13
**Feature**: [spec.md](spec.md)

## 1. Project Scaffolding, Generated `CMakeLists.txt`, and Workspace Isolation

### Decision
Provide explicit **Open Project...** and **New Project...** workflows (menu bar + Welcome Screen + Recent Projects list capped cleanly in global preferences). **New Project...** scaffolds a minimal runnable C++ Platformator project containing `CMakeLists.txt`, `main.cpp`, `assets/scenes/main.scene`, and empty `assets/behaviors/`, `assets/templates/`, and `assets/audio/` directories. **Open Project...** validates the directory and checks for `CMakeLists.txt`; if `CMakeLists.txt` is absent (for example an asset/scene-only folder), it offers a one-click **"Generate CMakeLists.txt"** action rather than failing or forcing manual authoring.

The generated `CMakeLists.txt` mirrors `examples/mario/CMakeLists.txt`: it calls `find_package(Platformator CONFIG QUIET)` first, and if `Platformator_FOUND` is false, falls back via `add_subdirectory(...)` to the local Platformator engine source tree path detected by the editor (overridable via `-DPLATFORMATOR_ROOT=<path>`). Generated build targets automatically collect project `.cpp`/`.h` behavior files (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` over `main.cpp` and `assets/behaviors/`) and link `Platformator::platformator` (or `platformator_engine`), copying/resolving runtime shared libraries cleanly via CMake generator expressions or `PLATFORMATOR_ROOT`.

Opening another project flushes workspace preferences, closes all open document tabs cleanly (prompting via dirty-state save confirmation when modified), resets undo/redo stacks, clears template editing state, clears active audio preview, reloads per-project `RunPreferences` / `LayerSettings` / layout, and re-runs `SourceBehaviorScanner` and asset validation for the new project root.

### Rationale
- Reusing the dual `find_package` + `add_subdirectory` pattern keeps generated projects portable between installed SDKs and local repo checkouts without requiring manual CMake edits (`examples/mario/CMakeLists.txt` already validates this pattern).
- `CONFIGURE_DEPENDS` globbing over `assets/behaviors/` eliminates manual `CMakeLists.txt` edits whenever users add or delete behavior scripts.
- Strict workspace state teardown on project switch prevents stale undo commands, old tabs, or stale behavior catalogs from corrupting the newly opened project.

### Alternatives Considered
- **Installed-only `find_package(Platformator CONFIG REQUIRED)`**: Breaks developers running the editor directly from a local source checkout without `cmake --install`.
- **Subprocess template generator / Python script**: Unnecessary external dependency when the C++ editor already owns project filesystem validation (`validate_project_folder` in `editor/src/project.cpp`).

---

## 2. Static C++ Behavior & Field Scanner (`SourceBehaviorScanner`) and `--editor-catalog` Removal

### Decision
Promote static C++ source scanning into the single authoritative source of truth for behavior type discovery and Inspector field metadata across `assets/behaviors/` (and project header/source files outside `bin/`/`build/`), and completely remove `--editor-catalog` from both the engine (`src/scriptregistration.cpp`, runtime CLI parsing) and the editor (`editor/src/behaviorcatalog.cpp` subprocess invocation, `editor/src/process.cpp` catalog check stage, and `editor/src/panels/runsettings.cpp` protocol status/controls).

`SourceBehaviorScanner` tokenizes C++ headers and sources using a lightweight lexer (stripping `//` and `/* ... */` comments and string literals accurately before parsing) to extract:
1. Class/struct declarations inheriting `ScriptComponent` (directly or transitively within scanned headers).
2. `SERIALIZABLE_SCRIPT(TypeName, field1, field2, ...)` macro invocations, extracting exact `TypeName` and the ordered list of field member names (supporting multi-line macro calls and comments inside argument lists).
3. Member declarations matching each listed field inside the class body, resolving C++ type + default member initializer (`= value`, `{value}`):
   - Primitives: `bool`, signed/unsigned integers (`int`, `int32_t`, `int64_t`, `size_t`, etc.), floating-point (`float`, `double`), `std::string`.
   - Math/geometry: `Eigen::Vector2f` -> `Vector2`, `SDL_FRect` -> `Rectangle`.
   - References: `ObjectRef<T>` -> `ObjectReference`, `AnimationClipRef` / `AudioAssetRef` / `AssetRef<T>` -> `AssetReference` (with target kind `"AnimationClip"`, `"Audio"`, etc.).
   - Containers: `std::vector<T>` -> `Array`, `std::optional<T>` -> `Optional`.
   - Enums: scanned `enum` / `enum class` definitions in project headers + built-in engine enums (`BodyType`, `SDL_FlipMode`) -> `Enum` with concrete option names.
   - Any unrecognized C++ type or unresolved field produces an explicit scanner diagnostic (shown cleanly in Validation / Inspector tooltip while leaving safe editing/JSON fallback intact) rather than falling back to `"Build to load field"`.

Scanning runs off the UI thread (reusing/extending existing asynchronous project scanning or `oneTBB` task dispatch triggered on project open and file-watcher deltas) so the UI remains responsive at 60 FPS, and caches per-file parse results keyed by file modification timestamp + size for incremental re-parsing.

### Rationale
- Compiling the game binary to query `--editor-catalog` previously duplicated truth and blocked Inspector field editing whenever a project wasn't freshly built.
- Static parsing matched to `SERIALIZABLE_SCRIPT(...)` works instantaneously on source save—including for newly created or edited scripts before compiling.
- Removing `--editor-catalog` simplifies `PlaySession` launch states by eliminating the pre-launch `CatalogCheck` subprocess completely.

### Alternatives Considered
- **Full `libclang` AST dependency**: Heavy external toolchain dependency (`LLVM`/`Clang` dev packages) that complicates builds across platforms; Platformator behaviors declare serialized fields explicitly via `SERIALIZABLE_SCRIPT(...)`, making a focused C++ tokenizer + class-member matcher fast, zero-dependency, and deterministic.
- **Keeping `--editor-catalog` as optional verification**: Explicitly rejected by user clarification to avoid duplicate sources of truth and stale protocol UI.

---

## 3. Embedded PlaySession Handshake Without `--editor-catalog`

### Decision
Remove the pre-launch `--editor-catalog` capability check from `editor/src/process.cpp` and `editor/src/playsession.cpp`. Instead, when Play Mode is launched in Embedded Game View mode (`--editor-session ...` environment/IPC args), `PlaySession` transitions directly to `Starting` and negotiates capabilities via the existing runtime `"capabilities"` IPC event emitted by `src/editorsession.cpp` upon startup.

Rules:
- Remove `RunPreferences::catalogProtocol` and catalog protocol UI text from `RunSettings` / Project Settings.
- Remove `PlaySession::start_process`'s requirement for pre-validated `SessionCapabilities` from `--editor-catalog`; `PlaySession` enters `Starting` and waits up to the launch handshake timeout (reuse existing handshake timeout budget) for `"capabilities"` followed by `"ready"`.
- If `"capabilities"` reports `!supported || !frameStream`, or if the child exits cleanly/with non-zero status before sending `"capabilities"`, or if handshake times out, transition `PlaySession` cleanly to `Failed` with an actionable error recommending External Game Window mode.
- Keep `"capabilities"` structure inside `src/editorprotocol.cpp` and emitted by `src/editorsession.cpp` so runtime inspection, play control, live asset reload, and frame stream capability checks continue working identically without `--editor-catalog`.

### Rationale
- `src/editorsession.cpp` already sends `"capabilities"` over the live IPC channel on startup, and `editor/src/playsession.cpp` already parses it; removing the pre-check eliminates a redundant process spawn on every Play click while preserving backward-compatible detection of older/custom targets.

### Alternatives Considered
- **Removing capability negotiation altogether**: Would lose clear error reporting if a user runs a custom target executable that doesn't initialize `EditorSession`.

---

## 4. Physics Layer System (32 Named Layers + Collision Matrix Bitmap)

### Decision
Implement Unity-style physics-only layers (`0..31`) shared seamlessly between the editor (`ProjectSettings` -> Layers & Physics Matrix, Inspector `Layer` dropdown on `GameObject`) and runtime engine (`GameObject`, `PhysicsManager`, `Grid`, `.scene` serializer):

1. **Layer Table & Collision Matrix Storage (`LayerSettings`)**:
   - Stored per project in project settings (`.platformator/layers.json` or project settings file alongside `assets/`, plus loaded automatically by `Runtime` from `assetRoot` / project root so standalone builds also honor it).
   - Fixed 32 slots (`0..31`): slot `0` locked to `"Default"`; slots `1..31` editable non-empty unique-trimmed names (empty strings mean unassigned/unnamed slot selectable only after naming or shown by index in UI if referenced).
   - Collision matrix stored as a symmetric 32-entry `std::array<std::uint32_t, 32>` bitmap: bit `j` of `matrix[i]` is `1` iff layer `i` and layer `j` collide; symmetry `((matrix[i] >> j) & 1) == ((matrix[j] >> i) & 1)` is enforced on read/write.
   - Default matrix: all bits `1` (`0xFFFFFFFFu`), preserving 100% compatibility for existing scenes/projects where every `GameObject` defaults to layer `0` (`"Default"`).

2. **`GameObject` & `.scene` / `.pttemplate` Serialization**:
   - Add `std::uint8_t layer = 0;` (clamped `0..31`) to `platformator::GameObject` (`getLayer()` / `setLayer(uint8_t)`).
   - Serialize `"layer": <0..31>` in `src/gameobjectserializer.cpp` and deserialize in `src/scenebuilder.cpp` defaulting missing `"layer"` to `0` (backward-compatible with all existing `.scene` and `.pttemplate` files).
   - Also replicate `"layer"` changes live over `EditorSession` inspection / command path if displayed in runtime inspector.

3. **Runtime Collision Filtering & Constitution Architecture Compliance (`Grid` + `PhysicsManager`)**:
   - **Invariant preservation**: Do **not** skip cell witness tracking (`Grid::applyPairWitnessDelta`) based on layer bitmasks inside `Grid::processColliderOperations()`, because layer masks do not mutate grid cell membership, and filtering inside `Grid` witness bookkeeping risks corrupting `pairAdjacency` / `candidateCollisions` reference counts or leaving stale contacts if a layer is changed dynamically at runtime (`setLayer()`).
   - **Where to filter**: Filter in `PhysicsManager::processNarrowPhasePair(const ColliderPair *pair)` right alongside the existing `!active || retired` guard (line 266 of `src/physicsmanager.cpp`):
     ```cpp
     const auto layerA = pair->getObjectA()->getGameObject()->getLayer();
     const auto layerB = pair->getObjectB()->getGameObject()->getLayer();
     if (!canLayersCollide(layerA, layerB))
     {
         pair->clearCollisionNoEvent();
         return;
     }
     ```
     AND when `GameObject::setLayer(...)` changes a GameObject's layer at runtime while colliders are registered, mark its attached colliders dirty (or queue their existing pairs via `Grid::queuePendingNarrowPhasePair`) so previously active contacts separate cleanly without needing a transform move.
   - **Concurrency safety**: `PhysicsManager::canLayersCollide(uint8_t a, uint8_t b)` reads the immutable-during-step `std::array<std::uint32_t, 32> collisionMatrix_` via `(collisionMatrix_[a & 31u] >> (b & 31u)) & 1u` — pure O(1) lock-free bitwise read safe inside `tbb::parallel_for` narrow-phase workers.
   - **Queries/raycasts**: If physics queries/raycasts accept a layer mask or filter colliders, expose the same bitmask helper (`uint32_t layerMask`).

### Rationale
- Filtering at `processNarrowPhasePair` alongside `getActive()`/`isRetired()` respects Platformator's strict Separation of Broad-Phase Cell Sweep-and-Prune Witnesses from Narrow-Phase Contact State (Constitution Principle III), causes zero allocations or atomic contention in `tbb::parallel_for`, and avoids `O(pairs)` SAT/manifold geometry work for non-colliding layers while naturally emitting `COLLISION_EXIT` / clearing contacts cleanly if layers change.

### Alternatives Considered
- **Skipping pair creation in `Grid::applyPairWitnessDelta`**: Tightly couples `Grid` structural cell witness invariants to mutable GameObject layer state; changing `setLayer()` at runtime would require tearing down and re-inserting colliders across cells rather than just re-queuing narrow-phase evaluation.
- **Layer names/order affecting sprite rendering**: Explicitly rejected in clarification Q1 (`A — Physics collision filtering only`).

---

## 5. Single-Level Template (Prefab) Authoring, Flattening, and Instance Synchronization

### Decision
Retain the existing `TemplateAssetV1` format (`.pttemplate`, version `1`, `format: "platformator.object-template"`) as the on-disk template definition while upgrading how scene GameObjects link to templates and how users edit templates:

1. **Template Creation & Single-Level Constraint**:
   - **Save as Template** on any GameObject writes `<assets>/templates/<Name>.pttemplate` (or user-chosen path in `assets/templates/`), serializing the GameObject subtree with template-local stable IDs (`templateNodeId` / existing `"id"` within the template root).
   - **Single-Level Flattening**: If the source hierarchy being saved as a template contains nested template instances, strip their `templateSource` / `templateOverrides` metadata during serialization so the saved `.pttemplate` is strictly self-contained (one template level deep — no nested template references).
   - **Clean Inspector**: Remove the permanent `Object Template` collapsible section (`templateName_`, manual `Load Template`, separate `Assign` clutter) for regular non-template GameObjects. Instead:
     - Expose `"Save as Template..."` in Hierarchy context menu (and compact Inspector header `⋮` / menu when appropriate).
     - Show a compact **Template Instance Header Bar** (`Open`, `Apply`, `Revert`, `Unpack`) **only** on GameObjects linked to a template (`templateSource` present).

2. **Instance Linkage & Overrides in Scene JSON (`.scene`)**:
   - Linked root GameObjects in editor/saved `.scene` files store lightweight additive metadata ignored harmlessly by runtime if unused or stripped cleanly during scene build:
     - `"template": { "source": "assets/templates/Enemy.pttemplate", "nodeId": 101, "overrides": ["position", "components[1].speed", ...] }` (Exact schema specified in `contracts/template-instance-format.md`).
   - Direct template child instances created by instantiation carry `"templateNodeId"` mapping them back to their corresponding child inside the `.pttemplate` hierarchy so structural diffs (component/child additions & removals) and property diffs apply deterministically.
   - Dragging `.pttemplate` onto Hierarchy / Scene (or Hierarchy context menu -> Instantiate Template) creates a linked instance preserving template defaults. Editing a field on an instance records that field path in `overrides` and renders it visually distinct (bold label + right-click `Revert Property`). Clicking `Revert` resets all overrides to template values; `Unpack` removes `"template"` linkage metadata leaving an independent GameObject.

3. **Unity-Style Template Editing Mode & Synchronization**:
   - Double-clicking a `.pttemplate` asset in the Project browser or clicking **Open** on a linked instance opens **Template Editing Mode** as an isolated document tab (`DocumentTabKind::Template`) using the exact same Scene Viewport + Hierarchy + Inspector tools on the template subtree (with an Exit/Back breadcrumb bar showing unsaved status and **Auto-Save** toggle or explicit Save/Revert).
   - Saving an edited `.pttemplate` updates all loaded scenes in the open workspace (and via optional project-wide scene pass on open/active scenes) for every linked instance whose matching property/component/child is **not** marked in `overrides`:
     - Non-overridden component/transform/layer/tag/name properties update to match the template.
     - Newly added components or child nodes in the template are instantiated into linked instances with fresh unique scene IDs mapped via `templateNodeId`.
     - Removed components or child nodes in the template are removed from linked instances unless specifically preserved/overridden.
     - All updates execute as atomic undoable commands or clean save transactions without leaving dangling object references.

### Rationale
- Single-level templates avoid exponential complexity of multi-level nested overrides and match the user's Q3 answer (`A — Single-level templates only`).
- Editing templates inside a dedicated `DocumentTabKind::Template` reuses the existing Scene/Hierarchy/Inspector editing machinery cleanly with full undo/redo and validation rather than building a second bespoke template editor UI.

### Alternatives Considered
- **Separate modal popup editor for templates**: Duplicates inspector/hierarchy rendering and lacks full viewport manipulation (dragging child colliders/sprites visually).

---

## 6. Project Settings Window & UI / Dock Cleanup

### Decision
Consolidate all project-level configuration out of the bottom dock and into a unified **Project Settings** window opened via **Edit → Project Settings...** and **Window → Project Settings**, matching Unity's layout:

1. **Unified `ProjectSettingsPanel`**:
   - Left category list:
     - **Build & Run**: Configure/Build presets, target selector, executable path, Launch Mode (`Embedded Game View` vs `External Game Window`), Game Window size/fullscreen/maximized/preserve aspect, Start Paused, Overlays, and clean **Build** / **Rebuild** / **Save Settings** status—removing `Catalog protocol`, `Recheck Sources`, `Rediscover Targets` buttons, and duplicate `Build & Run` / `Stop` transport buttons already in the main toolbar. Advanced manual overrides sit inside a collapsed `Advanced` section.
     - **Layers & Physics Matrix**: 32-slot layer list (`0: Default` read-only, `1..31` inline editable names with validation) + symmetric 32×32 interactive checkbox collision matrix grid (rows/columns labeled cleanly with layer names and index tooltips, filtering out unnamed empty rows/columns optionally via a toggle `"Show Empty Layers"` defaulting to collapsible/compact so populated layers are effortless to read and edit).
   - Bottom dock keeps **Console** and **Validation** clean; `"panel.run"` is migrated to `"panel.project_settings"` (opened via menu on demand rather than pinned in the bottom dock in default layout reset, with legacy layout migration moving `"panel.run"` cleanly out of the default bottom tab bar).

2. **Panel & Context Menu Cleanup**:
   - **Project Panel**: Remove the top standalone `Refresh Assets` button, `Load Template` / `Instantiate Template` button bar, and redundant `Assign` button clutter; expose `Refresh` (plus `Create -> Folder / Scene / Behavior Script / Animation`, `Show in File Manager`, `Rename`, `Delete`) via right-click context menu on the asset tree/grid.
   - **Inspector**: Remove permanent `Object Template` box on regular non-template objects; keep compact Transform (`position`, `rotation`, `scale`), `Tag`, `Layer` dropdown next to `Tag`, `Add Component` button, and behavior fields clean with zero dead/redundant buttons.
   - **Console / Validation**: Keep compact filter pills + Clear button; validation runs automatically when files change or scenes/templates are edited (retaining errors/warnings live without forcing manual button clicks).
   - **Menu & Toolbars**: Maintain crisp single Play/Pause/Step/Stop transport bar and **Window → Layouts** presets (Default / Tall / Compact / Reset Layout).

### Rationale
- Moving infrequently changed project settings out of the bottom dock reclaims permanent vertical screen space for the Scene/Game viewports and Console/Project panels while following Unity's exact muscle memory (`Edit → Project Settings...`).

### Alternatives Considered
- **Separate modal dialogs per settings page**: Prevents side-by-side inspection while editing layers on Scene GameObjects (for example clicking a GameObject while tweaking layer names or collision toggles in Project Settings). Dockable/floatable non-modal `ProjectSettingsPanel` allows live side-by-side editing.

---

## 7. Editor Audio Asset Preview (`SDL3_mixer`)

### Decision
Implement a lightweight `AudioPreviewService` in the editor reusing the already-linked `SDL3_mixer` (`MIX_Mixer`, `MIX_Audio`, `MIX_Track`) APIs (matching `src/audio.cpp` / `src/sdlwindow.cpp` usage):

- Trigger: Double-clicking a supported audio asset (`.wav`, `.ogg`, `.mp3`, `.flac`) in the Project panel calls `AudioPreviewService::toggle_or_play(assetPath)`.
- Behavior:
  - Lazy-initializes a shared editor `MIX_Mixer*` + single reusable `MIX_Track*` on first preview request (gracefully logging an informative non-fatal Console message if no audio output device is available in headless/CI environments).
  - Double-clicking an audio file loads and plays it immediately from the beginning; double-clicking the **same** currently playing audio asset toggles pause/stop cleanly; double-clicking a **different** audio asset stops the current track cleanly and starts the new one.
  - Project panel subtly displays a playing indicator + mini stop control / tooltip next to the active audio asset while playback is active, and cleans up tracks deterministically on project switch or editor shutdown.

### Rationale
- `platformator_editor` already links `platformator_engine`, which links `SDL3_mixer` (`SDL3_mixer-3.2.4` vendored in `third_party/`). Zero new build dependencies are required, and a single shared `MIX_Track` ensures previews never stack or leak tracks.

### Alternatives Considered
- **External audio CLI player (`aplay`/`ffplay`)**: Non-portable across OS/desktop setups and uncoordinated with editor lifecycle.

---

## 8. Automatic Validation & Dirty-State Protection

### Decision
Make asset/behavior/scene validation reactive rather than button-driven, and unify unsaved-change protection across all destructive transitions:

1. **Automatic Validation**:
   - Trigger validation automatically after: project open, scene/template open or edit command completion, file watcher change event (new/modified/deleted asset or behavior header/source), and `LayerSettings` updates (e.g., if a scene object references an unnamed layer or missing script/asset).
   - Deduplicate/debounce background validation (<= 150 ms coalescing) so rapid typing or drags never cause validation spikes.
2. **Unified Unsaved Changes Prompt**:
   - Standardize a three-choice modal dialog (`Save`, `Don't Save`, `Cancel`) across **Close Tab**, **Open Project**, **New Project**, **Exit Template Edit Mode**, and **Quit Application** whenever any open Scene, Animation, Template, or Project Settings has unsaved dirty state (`WorkspacePreferencePersistence` or `DocumentTab::history().is_dirty()`).

### Rationale
- Eliminates silent data loss on project switching or window close and removes guesswork about whether validation findings are stale.
