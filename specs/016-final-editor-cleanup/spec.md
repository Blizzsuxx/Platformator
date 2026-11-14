# Feature Specification: Final Editor Cleanup

**Feature Branch**: `016-final-editor-cleanup`  
**Created**: 2026-11-13  
**Status**: Draft  
**Input**: User description: "create a specification for the final editor cleanup. investigate all the things that need a cleanup - like the following: having projects and opening projects, creating a project generates a CMake file if needed (for Platformator); scanner for behaviors and fields; cleaning up the UI (like build and run settings, redundant text / buttons in the UI like the refresh assets button -> right click into refresh); Layer system / window like in Unity, having the ability to create layers, name them and manipulate the bitmap; editing templates like prefabs in Unity; double clicking an audio asset should play the sound; and any other cleanups or issues or proposed features needed for a final editor version."

## Clarifications

### Session 2026-11-13

- Q: Should the new Layer system govern physics collision filtering only, or should it also control rendering order (like sorting layers)? → A: Physics collision filtering only (symmetric 32×32 collision matrix bitmap + GameObject layer assignment); sprite rendering order remains independent.
- Q: How should auto-generated project build files locate and link the Platformator engine? → A: Support an installed Platformator package first, with automatic fallback to the local Platformator engine source tree path.
- Q: When a saved Object Template is edited and saved, what changes should propagate to existing instances in scenes, and should nested templates be supported? → A: Single-level templates (no nested template-in-template hierarchies) with full structural (added/removed components and child objects) and non-overridden property propagation to linked instances.
- Q: With source scanning replacing `--editor-catalog` for behaviors and fields, how should the editor handle `--editor-catalog` and detect embedded Game view support? → A: Remove `--editor-catalog` completely (no pre-launch catalog subprocess) and verify embedded Game view support directly during the play-session launch handshake.
- Q: How should Build and Run Settings and the new Layers & Physics Matrix UI be organized in the editor workspace? → A: Group Build & Run and Layers & Physics Matrix into a unified Project Settings window with category tabs, freeing the bottom Console dock from permanent settings tabs.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Seamless Project Creation, Opening, and Switching (Priority: P1)

A game developer launches the editor to start a new game or switch between existing projects without manually authoring build files or losing context. From the Welcome screen or File menu, they can create a new project in a chosen folder (which automatically generates the required asset layout, starter scene, entry point, and build configuration linking the engine when missing) or open an existing folder (generating missing build configuration on confirmation if it lacks one). Switching projects cleanly handles unsaved work in the current project and restores the newly opened project's own scenes, settings, and recent-project history.

**Why this priority**: Project onboarding and switching are the entry point to every editor workflow; requiring manual build-file authoring or carrying stale scenes across projects blocks new users immediately.

**Independent Test**: Can be tested by creating a brand-new project in an empty directory from the Welcome screen, adding a scene edit, building and running it without editing any build file manually, and then opening a second project from a recent-projects list and verifying each project retains its own open scenes and settings.

**Acceptance Scenarios**:

1. **Given** the editor is launched with no active project or from the File menu, **When** the user chooses **New Project...** and selects an empty or target directory, **Then** the editor creates the standard project structure (assets folder, starter scene, game entry point, and build configuration linking Platformator), opens the project, and leaves it immediately ready to edit and Build & Run.
2. **Given** a user opens an existing folder that contains an assets folder (or existing source/scenes) but no build configuration, **When** they choose to open it, **Then** the editor offers to generate a compatible build configuration automatically so the project can be opened and built without manual setup.
3. **Given** a user switches from Project A (with unsaved scene changes) to Project B, **When** they confirm Save or Discard for Project A, **Then** Project A's scenes close cleanly, Project B loads its own previously open scenes and run settings, and both projects appear in a **Recent Projects** list.

---

### User Story 2 - Immediate Source-Driven Behavior and Field Discovery (Priority: P1)

A developer writes or edits gameplay behavior scripts and inspects objects in a scene before ever compiling the game. As soon as a project opens or a script file is saved, the editor scans the project's behavior source files as the single source of truth for available behavior types and their serialized fields. All behavior types appear in the **Add Component** menu, and every declared field—primitives, vectors, enums, asset references, and object/component references—is immediately visible and editable in the Inspector without building first and without duplicate behavior entries.

**Why this priority**: Currently, users cannot inspect or edit behavior fields (or save templates containing reference fields) until the game executable is built, which breaks normal scene authoring and causes "Build to load field" dead ends.

**Independent Test**: Can be tested by opening an unbuilt project with behavior scripts, selecting a GameObject with a Script component, verifying all declared fields are editable with proper type controls (sliders, asset pickers, object references, enums), adding a new field in a script file, and verifying the new field appears in the Inspector without running a build.

**Acceptance Scenarios**:

1. **Given** a project that has never been built, **When** the user selects a GameObject with an attached behavior or opens **Add Component**, **Then** all behaviors declared in project source files are listed once (no duplicates) and all their serialized fields are immediately editable with type-appropriate controls.
2. **Given** a developer adds, renames, removes, or changes the type of a serialized field in a behavior source file, **When** the file is saved, **Then** the Inspector updates to reflect the revised field schema automatically without requiring a build.
3. **Given** an object has a behavior with object/component reference fields, **When** the user saves that object as an Object Template before building, **Then** template saving succeeds using the source-scanned schema rather than blocking due to missing compiled catalog metadata.

---

### User Story 3 - Unity-Style Layer System and Collision Matrix Window (Priority: P1)

A level designer or gameplay programmer organizes GameObjects into named layers (such as `Default`, `Player`, `Enemies`, `Ground`, `Triggers`) and controls which layers physically collide with each other using a visual collision matrix. Inside the **Project Settings** window's **Layers & Physics Matrix** category, they can create, name, and remove custom layers and toggle checkboxes in a symmetric 2D layer-vs-layer bitmap. Every GameObject's Inspector header includes a **Layer** dropdown next to **Tag**, and the simulation respects the configured collision matrix during play.

**Why this priority**: Complex 2D games require collision filtering between players, projectiles, platforms, and triggers; without named layers and a collision matrix, every collider interacts with everything or requires ad-hoc tag checks in every script.

**Independent Test**: Can be tested by creating two custom layers in Project Settings → Layers & Physics Matrix, disabling collisions between them in the collision matrix bitmap, assigning two overlapping colliders to those respective layers, and verifying in Play mode that they pass through each other while still colliding with layers whose matrix cells remain enabled.

**Acceptance Scenarios**:

1. **Given** the user opens **Project Settings → Layers & Physics Matrix** (via `Edit → Project Settings...`, `Window`, or the Inspector's Layer dropdown), **When** they add or rename custom layers (up to 32 total layers, with `Default` at index 0), **Then** the layer names persist in the project configuration and immediately populate every GameObject's Layer selector.
2. **Given** the user toggles a cell in the symmetric **Layer Collision Matrix** bitmap, **When** they save or run the scene, **Then** colliders on those two layers collide if and only if their matrix bit is enabled, and changing a parent GameObject's layer optionally offers to apply the layer to its children.
3. **Given** multiple GameObjects on different layers are selected, **When** inspected together, **Then** the Layer dropdown shows a mixed-value indicator and assigns a unified layer to all selected objects in a single undoable action.

---

### User Story 4 - Prefab-Style Object Template Editing and Overrides (Priority: P2)

A designer builds reusable game entities (enemies, coins, platforms, hazards) as Object Templates and iterates on them centrally like Unity Prefabs. Double-clicking a template asset (or choosing **Open** on a template instance in a scene) opens an isolated **Template Editing** workspace showing only that template's hierarchy, visual preview, and components. Modifying and saving the template updates all linked instances across scenes while preserving instance-specific property overrides, and instances in the Inspector expose clean **Open**, **Select Asset**, **Revert**, **Apply**, and **Unpack** actions.

**Why this priority**: Currently, templates are one-way snapshots: users must drop a template into a scene, edit it, overwrite/re-save manually, and re-instantiate copies everywhere—making level iteration slow and error-prone.

**Independent Test**: Can be tested by creating a template from a GameObject, placing two instances in a scene, overriding a property on one instance, opening and modifying another shared property in Template Editing mode, saving, and verifying both instances receive the template update while the overridden property remains intact on the customized instance.

**Acceptance Scenarios**:

1. **Given** a template asset exists in the Project browser or an instance exists in a scene, **When** the user double-clicks the template asset or clicks **Open** in the instance's Inspector header, **Then** the editor switches into isolated Template Editing mode with full Hierarchy, Scene preview, Inspector, Undo/Redo, and Save support, plus a clear breadcrumb/bar to exit back to the scene.
2. **Given** a template instance in a scene has local property modifications, **When** the user views it in the Inspector, **Then** modified fields are clearly indicated and the user can **Apply** changes back to the source template, **Revert** back to template defaults, **Select** the template asset, or **Unpack** the instance into independent scene objects.
3. **Given** an Object Template is edited and saved, **When** scenes containing linked instances are open or reopened, **Then** non-overridden properties update to match the saved template while preserving per-instance overrides and local child/reference bindings.

---

### User Story 5 - Clean, Compact Editor UI and Context-Driven Actions (Priority: P2)

A user works across the Project browser, Build & Run settings, Inspector, Console, and Validation panels without visual clutter, redundant text, or duplicate buttons. Repetitive inline buttons (such as **Refresh Assets**, **Load Template**, **Recheck Sources**, **Rediscover Targets**, and duplicate **Build / Build and Run / Stop** controls inside settings) are removed or moved into standard right-click context menus, compact panel header icons, or collapsed **Advanced** sections.

**Why this priority**: Cluttered panels waste screen space, repeat controls already present on the main toolbar or context menus, and expose internal build plumbing to users who only want to configure run options and build their game.

**Independent Test**: Can be tested by reviewing every panel at typical docked sizes and verifying that refresh/creation actions live in context menus or compact header controls, Build & Run settings fit cleanly without redundant build buttons or obsolete catalog-protocol dropdowns, and all actions remain discoverable via menus/shortcuts.

**Acceptance Scenarios**:

1. **Given** the user is in the **Project and Assets** panel, **When** they want to refresh assets or act on a folder/asset, **Then** no standalone full-row **Refresh Assets** button or verbose hint text occupies the top of the panel; instead, **Refresh** is available in the right-click context menu (and Assets menu / shortcut).
2. **Given** the user opens **Project Settings → Build & Run**, **When** viewing standard settings, **Then** obsolete catalog-protocol controls, **Recheck Sources**, **Rediscover Targets**, and duplicate **Build / Build and Run / Stop** buttons are removed, low-level build/executable overrides live inside a collapsed **Advanced** section, and standard options (Build Profile, Game View mode, Window Size/Mode, Debug Overlays) are concise and uncluttered without occupying a permanent Console dock tab.
3. **Given** the user selects a non-template GameObject in the Inspector, **When** viewing its components, **Then** no permanent empty **Object Template** creation form occupies space between Transform and Components; creating a template is accessible via drag-to-Project-browser or context/header action, while template instance controls appear only on actual template instances.

---

### User Story 6 - Instant Audio Asset Playback on Double-Click (Priority: P2)

A sound designer or developer browses audio assets (`.wav`, `.ogg`, `.mp3`, `.flac`) in the Project browser and wants to audition clips quickly while building a level. Double-clicking an audio asset plays the sound immediately inside the editor without hijacking Inspector component assignment; double-clicking it again (or clicking stop) stops playback, and double-clicking a different audio asset switches playback cleanly.

**Why this priority**: Auditioning sound effects is a core asset-browser workflow; currently, double-clicking an audio file tries to assign it to a selected component instead of playing the sound.

**Independent Test**: Can be tested by double-clicking two different audio assets in the Project browser, confirming audible playback starts immediately on the first double-click, switches cleanly on the second, and stops cleanly when toggled, deleted, or when switching projects.

**Acceptance Scenarios**:

1. **Given** an audio asset is shown in the Project and Assets browser, **When** the user double-clicks it, **Then** the editor plays the audio clip immediately and indicates active playback on the asset/preview without altering any component's assigned audio field.
2. **Given** an audio preview is currently playing, **When** the user double-clicks the same asset again, presses Stop in a compact audio preview bar, or switches/closes the project, **Then** playback stops cleanly without audio artifacts or resource leaks.

---

### User Story 7 - Final-Release Workspace Polish and Reliability (Priority: P3)

A developer uses the editor for extended daily sessions across multiple scenes, animations, templates, and play runs without encountering broken shortcuts, dead UI affordances, or inconsistent tab/dirty states. Unsaved changes are consistently indicated and guarded across scenes, templates, animations, and project layer settings; Console/Validation noise is reduced; and standard editor shortcuts and asset drag-and-drop workflows behave predictably across all panels.

**Why this priority**: Polish details determine whether the editor feels cohesive and reliable as a "final" v1 release rather than a collection of separate panels.

**Independent Test**: Can be tested via an end-to-end daily-workflow walkthrough covering multi-tab editing, undo/redo isolation per focused editor window, validation updates without manual clicks, and clean shutdown with mixed dirty assets.

**Acceptance Scenarios**:

1. **Given** scenes, templates, animations, or project layer settings have unsaved edits, **When** the user closes tabs, switches projects, or exits the editor, **Then** every dirty document/setting prompts consistently and preserves work on Cancel.
2. **Given** the user opens the **Validation** panel, **When** scene edits occur, **Then** validation refreshes automatically in the background without requiring a manual **Validate** button click for routine edits, and clicking a validation issue navigates directly to the offending object/component/field.
3. **Given** a user double-clicks or drags any supported asset type in the Project browser, **When** performing the standard action for that type (`.scene` opens scene, `.animset` opens Animation Assets, `.pttemplate` opens Template Editor, audio plays preview, script opens in external editor), **Then** every asset type has a consistent, predictable single-click (select/preview), double-click (primary open/play), and drag-and-drop (instantiate/assign) behavior.

---

### Edge Cases

- What happens when a user opens a folder whose generated or existing build configuration references an invalid or missing Platformator engine path? The editor reports a clear, actionable message and offers to regenerate/relink the build configuration without corrupting existing source or scene files.
- What happens when a behavior header has syntax errors or incomplete edits while the user is typing? The static behavior scanner preserves the last valid descriptor for unaffected behaviors, skips or marks only the unparseable behavior gracefully without crashing or clearing unrelated fields, and refreshes as soon as the file is valid again.
- What happens when a template asset referenced by a scene instance is renamed, moved, or deleted on disk? Renaming/moving via the Project browser updates references or resolves cleanly; deleting the source template marks affected scene instances as "Missing Template" with an option to **Unpack** or **Relink** without losing the instance's existing components and property values.
- What happens when a layer with assigned GameObjects is renamed or cleared in the **Layers & Physics Matrix** window? Renaming preserves all GameObjects' layer index assignments automatically; clearing a custom layer warns if objects in open scenes still reference it and falls back safely to `Default` (layer 0) upon confirmation or keeps the index readable until reassigned.
- What happens when an audio file is corrupt, unsupported, or missing when double-clicked? Playback fails gracefully with a non-blocking status/console message and never freezes the editor UI or crashes the audio subsystem.
- What happens when the user creates a project in a directory that already contains files? Project creation checks for collisions before overwriting existing files and warns or skips non-empty conflicts safely.

## Requirements *(mandatory)*

### Functional Requirements

#### Project Lifecycle & Build Setup
- **FR-001**: System MUST allow users to create a new Platformator project from the Welcome screen and the **File** menu, scaffolding the project directory (`assets/`, starter scene, game entry point, and build configuration that locates an installed Platformator package first with automatic fallback to the engine source tree path) without requiring manual CMake authoring.
- **FR-002**: System MUST allow opening an existing project folder that lacks a build configuration by offering to generate a compatible Platformator build configuration automatically.
- **FR-003**: System MUST maintain a persistent **Recent Projects** list accessible from the Welcome screen and **File → Open Recent** submenu.
- **FR-004**: System MUST cleanly isolate project workspaces when switching projects: prompting to save/discard dirty documents belonging to the outgoing project, closing the outgoing project's scenes/templates/animations, and restoring the incoming project's own open documents and preferences.

#### Source-Driven Behavior & Field Scanner
- **FR-005**: System MUST discover all project behavior types and their serialized fields directly from project C++/header source files as the single source of truth, completely removing `--editor-catalog` subprocess queries and verifying embedded Play-mode capabilities directly during launch handshake.
- **FR-006**: System MUST remove duplicate behavior discovery paths so every behavior appears at most once in **Add Component** and Inspector dropdowns, and behavior fields are immediately inspectable and editable before any build.
- **FR-007**: System MUST automatically re-scan changed behavior source files on project open and when source files are created, modified, or deleted, updating Inspector schemas and enabling Object Template saving without prior compilation.
- **FR-008**: System MUST gracefully tolerate temporary syntax errors in an edited behavior script by reporting a non-blocking diagnostic while keeping valid behaviors and previously known field values intact.

#### UI Cleanup & Decluttering
- **FR-009**: System MUST remove redundant inline action buttons and verbose boilerplate text across editor panels, including:
  - Removing the standalone **Refresh Assets** button in the Project browser in favor of **Refresh** in the folder/asset right-click context menu (and menu/shortcut).
  - Removing **Load Template** / separate **Assign** boilerplate clutter in the Project browser preview so selecting an asset previews it directly and double-clicking performs its primary open/play action.
  - Consolidating **Build and Run** into a unified **Project Settings** window (alongside **Layers & Physics Matrix**), removing obsolete catalog-protocol controls, **Recheck Sources**, **Rediscover Targets**, and duplicate **Build / Build and Run / Stop** buttons, while collapsing low-level CMake/executable overrides into an **Advanced** section and freeing the bottom Console dock.
  - Removing the permanent **Object Template** creation box from non-template GameObjects in the Inspector.
- **FR-010**: System MUST keep status notifications concise and consistent across the toolbar, panels, and Console without repeating identical status paragraphs in multiple panels.

#### Layer System & Collision Matrix
- **FR-011**: System MUST support up to 32 named physics collision layers per project (with layer `0` reserved as `Default`), persisted in project settings; layers govern physics collision filtering only and do not alter sprite rendering order.
- **FR-012**: System MUST provide a unified **Project Settings** window (`Edit → Project Settings...` and `Window` menu) containing category tabs for **Build & Run** and **Layers & Physics Matrix**, allowing users to view, create, rename, and clear custom layers and toggle collisions in a symmetric 2D layer-vs-layer matrix bitmap.
- **FR-013**: System MUST expose a **Layer** selector in the GameObject Inspector header (supporting single- and multi-object editing, undo/redo, and optional recursive application to child GameObjects).
- **FR-014**: The engine runtime and scene serialization MUST store each GameObject's layer index and apply the project's collision layer matrix bitmap during collision detection so disabled layer pairs do not generate physical collisions or trigger overlaps.

#### Prefab-Style Object Template Editing
- **FR-015**: System MUST support linking instantiated GameObjects in a scene to a single-level source `.pttemplate` asset (saving a template containing another template instance flattens/unpacks the inner instance rather than nesting templates) while preserving per-instance property overrides and reference bindings.
- **FR-016**: System MUST allow users to open any `.pttemplate` asset in an isolated **Template Editing** mode (via double-click in Project browser or **Open** on an instance in the Inspector) with full Hierarchy, Scene preview, Inspector, Undo/Redo, Save, and a clear exit action back to the active scene.
- **FR-017**: System MUST provide Inspector instance actions for template instances—including **Open**, **Select Asset**, **Revert**, **Apply**, and **Unpack**—plus visual differentiation for template instances and overridden fields.
- **FR-018**: System MUST propagate both structural changes (added or removed components and child objects) and non-overridden property defaults from an edited `.pttemplate` asset to linked instances in open and subsequently loaded scenes without overwriting explicit per-instance overrides.
- **FR-019**: Users MUST be able to create a new template cleanly by dragging a GameObject from the Hierarchy into the Project browser or using a context/header **Create Template** action.

#### Audio Preview Playback
- **FR-020**: Double-clicking a supported audio asset in the Project browser MUST play an audible preview inside the editor rather than attempting component field assignment.
- **FR-021**: System MUST allow stopping or toggling audio preview playback (by double-clicking the active audio asset again, clicking a compact Stop control in the asset preview, or starting another clip) and MUST stop playback cleanly on project switch, asset deletion, or editor exit.

#### Final-Release Polish
- **FR-022**: System MUST enforce uniform asset interaction semantics in the Project browser: single-click selects and previews; double-click opens/plays (`.scene`, `.animset`, `.pttemplate`, scripts, audio); drag-and-drop instantiates into Scene/Hierarchy or assigns to compatible Inspector fields.
- **FR-023**: System MUST automatically keep scene validation current during edits without requiring manual validation triggers for routine changes, and clicking any validation diagnostic MUST focus and reveal the offending object, component, or field.
- **FR-024**: System MUST track and prompt consistently for all unsaved editor state (scenes, templates, animation clips, and project layer/matrix settings) on close, project switch, or exit.

### Key Entities

- **Project Workspace**: The root directory of a user's game containing `assets/`, game source files, build configuration, project-level settings (including run preferences, layers, and collision matrix), and the workspace's own open-document session state.
- **Behavior Schema (Source-Scanned)**: The authoritative definition of a gameplay behavior class and its serialized member fields (name, data kind, reference target type, enum values), derived directly from project source files.
- **Layer Definition & Collision Matrix**: A project-wide registry of up to 32 named layers and a symmetric 32×32 boolean bitmap governing which layer pairs are permitted to collide at runtime.
- **Object Template (Prefab) & Instance Link**: A reusable `.pttemplate` GameObject hierarchy asset paired with scene-level instance metadata tracking the source template reference, internal/external object bindings, and per-field instance overrides.
- **Audio Preview State**: Transient editor playback state tracking the currently auditioned audio asset, playback status, and playback controls.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user can go from launching the editor with no project to creating a new project, placing an object with a behavior, editing its fields, and running the game in under 2 minutes without manually writing or editing any build configuration file.
- **SC-002**: 100% of serialized behavior types and supported field types declared in project source files appear once in the editor and are editable in the Inspector within 2 seconds of opening a project or saving a script edit—with zero prior game builds required.
- **SC-003**: Users can create, name, assign, and filter collisions between custom layers in **Project Settings → Layers & Physics Matrix** and verify altered collision behavior during Play mode in a single continuous editor session without restarting the editor.
- **SC-004**: Users can create a template, place multiple instances in a scene, edit the template in isolated Template Editing mode, save once, and observe all non-overridden instances update automatically while instance-specific overrides remain preserved.
- **SC-005**: Double-clicking any supported audio asset in the Project browser begins audible playback in under 250 ms without mutating any scene object or component assignment.
- **SC-006**: Redundant top-level action buttons (`Refresh Assets`, `Load Template`, `Recheck Sources`, `Rediscover Targets`, duplicate build controls, and obsolete catalog-protocol dropdowns) are eliminated from primary panel layouts while preserving 100% of underlying capabilities via compact context menus, toolbar actions, or collapsed advanced sections.

## Assumptions

- **Platform scope**: Editor cleanup and new features target the currently supported desktop editor platform (Linux) first while preserving cross-platform abstractions where they already exist in the codebase.
- **Project build configuration**: Auto-generated project build files attempt to locate an installed Platformator package first and fall back automatically to the local engine source tree path; users with custom CMake projects can still open and build them while keeping advanced overrides available in a collapsed section.
- **Static behavior scanning scope**: The behavior scanner targets Platformator's standard behavior registration/serialization declarations in project C++ headers and source files; complex preprocessor-generated fields outside standard declarations fall back gracefully to opaque preservation rather than blocking the Inspector.
- **Layer count**: A 32-layer limit (matching Unity's 32-bit layer mask) covers 2D game needs while allowing compact bitmask representation and a readable 32×32 (or active-layers-only) collision matrix UI.
- **Template file format compatibility**: Existing `.pttemplate` assets without instance-override metadata remain loadable and instantiable, upgrading cleanly when edited or saved in the new workflow.
- **Embedded session capabilities**: Removing `--editor-catalog` completely means embedded Play-mode capability negotiation happens directly during the play-session launch handshake, reporting a clear error if a target was built without embedded-session support.
