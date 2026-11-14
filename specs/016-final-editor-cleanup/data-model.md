# Data Model: Final Editor Cleanup (`016-final-editor-cleanup`)

**Date**: 2026-11-13
**Feature**: [spec.md](spec.md)

## 1. Global & Project Session Entities

### `GlobalPreferences` (stored in user config dir, e.g. `global.json`)
- `lastProjectRoot`: `optional<path>` — Most recently active project directory.
- `recentProjects`: `vector<RecentProjectEntry>` — Ordered MRU list of recently opened or created projects (deduplicated by canonical path, capped at e.g. 10 entries).
- `rememberedPlatformatorRoot`: `optional<path>` — Path to local Platformator engine source tree used as `add_subdirectory` fallback when scaffolding `CMakeLists.txt`.

### `RecentProjectEntry`
- `name`: `string` — Display project name (derived from CMake `project(...)` or directory basename).
- `rootPath`: `path` — Canonical root directory path.
- `lastOpenedUtc`: `int64` / ISO timestamp string — Used for MRU ordering in Welcome Screen and `File → Open Recent`.
- `exists`: `bool` (transient UI state) — Indicates whether directory still exists on disk.

### `ProjectScaffoldRequest`
- `parentDirectory`: `path` — Existing writable directory chosen by user.
- `projectName`: `string` — Non-empty identifier valid as directory name and CMake project/target identifier (`[A-Za-z_][A-Za-z0-9_-]*`).
- `platformatorSourceRoot`: `optional<path>` — Local engine source tree embedded as fallback in generated `CMakeLists.txt`.
- **Validation**: Rejects empty name, invalid path characters, or pre-existing non-empty target directory `<parentDirectory>/<projectName>`.

### `ProjectSwitchTransition` (State Machine)
1. **PromptingDirtyDocuments**: If any open Scene, Animation, Template tab, or unsaved Project Settings is dirty, prompt `Save` / `Don't Save` / `Cancel` (`Cancel` aborts switch cleanly).
2. **TearingDownWorkspace**: Flush current project `WorkspacePreferences`, stop active `PlaySession` (if running), stop `AudioPreviewService`, clear all `DocumentTab` instances + undo histories, exit Template Editing mode, reset validation & source-scanner state.
3. **OpeningTargetProject**: Validate directory; detect `CMakeLists.txt` (`missingCmakePromptPending = true` if absent); load target `WorkspacePreferences`, `LayerSettings`, and recent scene/tab; update `recentProjects`; trigger initial `SourceBehaviorScanner` pass + asset validation.

---

## 2. Physics Layers & Matrix Entities

### `LayerSettings` (per-project persistent entity in `.platformator/layers.json` + loaded by `Runtime`)
- `version`: `int` (`1`)
- `names`: `array<string, 32>`
  - `names[0]` MUST always equal `"Default"` (immutable in UI and normalized on load).
  - `names[1..31]`: Optional user-assigned layer names (trimmed; duplicates among non-empty names rejected in UI/validation).
- `collisionMatrix`: `array<uint32_t, 32>`
  - Row bitmask representation: bit `j` (`0..31`) of `collisionMatrix[i]` equals `1` iff layer `i` and layer `j` collide.
  - **Symmetry invariant**: `((collisionMatrix[i] >> j) & 1u) == ((collisionMatrix[j] >> i) & 1u)` for all `i, j in 0..31`. Normalized on load using lower/upper mirror (`OR` or canonical mirror write via `set_collision(i, j, enabled)` mutating both bits).
  - **Default value**: All 32 entries `0xFFFFFFFFu` (every layer collides with every layer) so legacy projects without `.platformator/layers.json` maintain exact existing collision behavior.

### `GameObject::layer` (Engine & Scene Entity Extension)
- **Type**: `std::uint8_t` (valid domain `0..31`; setters clamp/mask invalid values cleanly or reject out-of-range in serializer validation).
- **Default**: `0` (`"Default"`).
- **Serialization** (`.scene`, `.pttemplate`, `GameObjectSerializer`):
  - Property `"layer"`: integer `0..31`. Omitted or missing key on legacy files deserializes as `0`.
- **Relationship**: Purely governs `PhysicsManager` narrow-phase collision eligibility (`canLayersCollide(a.layer, b.layer)`) and optional layer-filtered physics queries; has **no** effect on sprite rendering order or visibility.

---

## 3. Source Behavior & Field Scanner Entities

### `SourceBehaviorDescriptor` (authoritative behavior metadata extracted directly from C++/H sources)
- `typeName`: `string` — Exact C++ class/struct identifier passed as first arg to `SERIALIZABLE_SCRIPT(TypeName, ...)`.
- `qualifiedName`: `string` — Optional namespace-qualified identifier if declared inside a namespace.
- `headerPath`: `path` — Project-relative path to header file declaring the behavior.
- `sourcePath`: `optional<path>` — Project-relative path to `.cpp` file containing `SERIALIZABLE_SCRIPT(...)`.
- `declarationLine`: `int` — 1-based line number for `Edit Script` / diagnostic navigation.
- `baseClass`: `string` — Verified base class chain (`ScriptComponent` or derived).
- `fields`: `vector<SourceBehaviorFieldDescriptor>` — Preserves exact declaration order from `SERIALIZABLE_SCRIPT(...)`.
- `diagnostics`: `vector<ScannerDiagnostic>` — Syntax/type-resolution warnings scoped to this behavior.

### `SourceBehaviorFieldDescriptor`
- `name`: `string` — Exact member identifier listed in `SERIALIZABLE_SCRIPT(...)`.
- `kind`: `ScriptFieldKind` (`Boolean`, `SignedInteger`, `UnsignedInteger`, `Number`, `String`, `Enum`, `Vector2`, `Rectangle`, `Array`, `Optional`, `AssetReference`, `ObjectReference`, `OpaqueJson`, `Unresolved`).
- `cppType`: `string` — Parsed normalized C++ type string (e.g. `float`, `Eigen::Vector2f`, `AnimationClipRef`, `std::vector<int>`).
- `targetType`: `optional<string>` — Referenced object/asset target type (`"AnimationClip"`, `"Audio"`, `"MarioPlayer"`, etc.) or enum type name.
- `enumValues`: `vector<string>` — Enumerator identifiers when `kind == Enum`.
- `defaultValueJson`: `nlohmann::json` — Parsed literal/aggregate default initializer if present, or type default (`false`, `0`, `0.0`, `""`, `[0, 0]`, `null`, etc.).
- `elementDescriptor`: `unique_ptr<SourceBehaviorFieldDescriptor>` (for `Array` / `Optional`).
- `lineNumber`: `int` — 1-based line number of the member declaration.
- `resolved`: `bool` — `false` only if type syntax or target field declaration could not be matched; carries diagnostic reason.

### `BehaviorCatalogSnapshot` (in-memory index replacing `BehaviorCatalogQuery` subprocess catalog)
- `projectGeneration`: `ProjectGeneration`
- `descriptorsByType`: `map<string, SourceBehaviorDescriptor>`
- `enumDefinitions`: `map<string, vector<string>>` — Project-defined `enum` / `enum class` + built-in engine enums (`BodyType`, `SDL_FlipMode`).
- `fileCache`: `unordered_map<path, CachedScannedFile>` — Keyed by `{mtime, byteSize}` for fast incremental re-scans on watcher events.
- `scanInProgress`: `bool`

---

## 4. Template (Prefab) Authoring & Instance Linkage Entities

### `TemplateAssetV1` (`.pttemplate` on-disk schema — retained version `1`)
- `format`: `"platformator.object-template"`
- `version`: `1`
- `name`: `string`
- `root`: `Json` — Complete GameObject JSON subtree using template-local node IDs (`"id"` integers within the template). **Single-level invariant**: `root` and all descendants contain zero nested `"template"` linkage blocks; creating a template from an object containing nested template instances strips nested `"template"` metadata automatically before writing.
- `reference_bindings`: `vector<TemplateReferenceBinding>` — Existing external scene-object binding declarations.

### `TemplateInstanceLink` (stored as optional `"template"` object on a linked scene root GameObject)
- `source`: `string` — Project-relative path to `.pttemplate` file (e.g. `"assets/templates/Coin.pttemplate"`).
- `nodeId`: `int` — Matching node `"id"` inside `TemplateAssetV1.root` for the instance root (and optional `"templateNodeId"` integer on linked descendant children).
- `overrides`: `vector<string>` — Canonical field paths overridden on this scene instance relative to the template root/node (examples: `"name"`, `"tag"`, `"layer"`, `"position"`, `"rotation"`, `"scale"`, `"active"`, `"components[id=1002].gain"`, `"children[nodeId=105].position"`).
- **Rules**:
  - Deleting an instance removes it from the scene without modifying `.pttemplate`.
  - Editing a non-overridden property on an instance automatically appends its canonical path to `overrides`.
  - Reverting a single property removes its path from `overrides` and copies the template value back; reverting all clears `overrides` and restores structure/values from `.pttemplate`.
  - Unpacking deletes `"template"` from root and `"templateNodeId"` from descendants cleanly.

### `DocumentTabKind` (Extended)
- `Scene`: Standard `.scene` editing tab.
- `Animation`: `.animset` clip/frame editing tab.
- `Template`: `.pttemplate` isolated **Template Editing Mode** tab displaying the template subtree in Scene Viewport, Hierarchy, and Inspector with header bar actions (`< Back to Scene`, `Auto-Save` toggle, `Save`, `Revert`).

---

## 5. Project Settings & UI Layout Entities

### `ProjectSettingsTab`
- `BuildAndRun`: Configure/build preset, target executable, Launch Mode (`Embedded Game View` / `External Game Window`), window dimensions/flags, overlays, Build & Rebuild actions, collapsed Advanced overrides.
- `LayersAndPhysics`: 32-slot editable layer name table + symmetric 32×32 physics collision matrix bitmap.

### `WorkspacePreferences` (Extended)
- Migrates `"panel.run"` -> `"panel.project_settings"` (closed by default in fresh/reset layouts; opens cleanly via `Edit → Project Settings...` or `Window → Project Settings`).
- Preserves per-project open tabs (`Scene`, `Animation`, `Template`), active tab, view camera, hierarchy expansion, snap preferences, and `RunPreferences` (with `catalogProtocol` removed).

---

## 6. Editor Audio Preview Entities

### `AudioPreviewState`
- `activeAssetPath`: `optional<path>` — Currently loaded/playing audio asset path.
- `playbackState`: `enum { Stopped, Playing, Paused }`
- `mixerAvailable`: `bool` — False only if `SDL3_mixer` device initialization fails (logs warning without throwing).
- `lastError`: `string`
- **Transitions**:
  - `doubleClick(path)`: If `path == activeAssetPath && playbackState == Playing`, stop/pause cleanly; otherwise unload previous `MIX_Audio`, load `path`, play on shared `MIX_Track` once (`loops = 0`).
  - `stop()` / project switch / editor shutdown: Stop track immediately and release `MIX_Audio`.
