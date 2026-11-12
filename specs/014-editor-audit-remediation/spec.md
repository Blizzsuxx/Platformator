# Feature Specification: Complete Editor Performance, Focus, Playability & Unity Ergonomics Audit Remediation

**Feature Branch**: `branch-without-bvh`

**Created**: 2026-11-12

**Status**: Draft

**Input**: User description: "create a specification that would include the entirety of the audit"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Quiet Idle Operation & Responsive Authoring Performance (Priority: P1)

A game developer opens a project in the editor and leaves it open while inspecting objects, editing properties, or reading documentation without the system fan spinning up from continuous background CPU consumption, and experiences smooth, immediate visual feedback during active editing.

**Why this priority**: An editor that consumes 100% of a CPU core while completely idle and takes over 60 ms per active frame drains battery, causes severe thermal/fan noise, and makes basic cursor movement and viewport panning feel sluggish. Restoring proper idle sleep and low-latency active frames is foundational to every other editor workflow.

**Independent Test**: Open a project scene in the editor and leave the window untouched for 30 seconds; verify that the editor enters low-power idle waiting between scheduled background checks and does not continuously spin CPU cores. Pan the viewport and scrub Inspector values; verify that active frames complete within a 60 FPS interactive budget without periodic multi-second stalls from scanning non-project directories.

**Acceptance Scenarios**:

1. **Given** an open project and scene with no user input or active animations occurring, **When** the editor sits idle, **Then** the application waits passively for input events and reduces idle frame redraws to background heartbeat intervals rather than spinning continuously.
2. **Given** a project workspace containing non-runtime directories (such as virtual environments, specification archives, documentation, or hidden tool folders), **When** project monitoring initializes and runs, **Then** only relevant project source and asset directories are monitored and scanned, avoiding watcher exhaustion and repetitive full-repository file hashing.
3. **Given** an open scene with the Hierarchy, Inspector, and Scene panels visible, **When** the user moves the pointer or pans the Scene viewport, **Then** unchanged project metadata, template schemas, and accessibility snapshots are not redundantly recomputed on every frame, maintaining smooth interactive frame rates.
4. **Given** any viewport tool (including the Hand tool) is active, **When** moving the mouse or panning across the Scene viewport, **Then** no diagnostic spam is written to standard output.

---

### User Story 2 - Seamless Cross-Panel Focus & Scene Navigation (Priority: P1)

A level designer selects a GameObject in the Hierarchy panel and immediately frames it in the Scene viewport with `F`, switches manipulation tools with `Q`/`W`/`E`/`R`, or pans the Scene camera using middle-click or right-click drag without having to click inside the Scene canvas first.

**Why this priority**: Selecting an object in the Hierarchy and immediately framing or navigating to it in the Scene view is the single most frequent multi-panel interaction in a Unity-style editor. Requiring an extra left-click inside the Scene viewport before panning or pressing `F` breaks muscle memory and risks accidentally clearing or altering the current selection.

**Independent Test**: Click a GameObject in the Hierarchy panel. Without clicking the Scene panel first, press `F` and verify the Scene camera centers on the selected object. Press `W`, `E`, `R`, or `Q` and verify the active Scene tool switches. Move the cursor over the Scene canvas and drag with the middle or right mouse button; verify the camera pans immediately on the first drag gesture. Double-click any GameObject in the Hierarchy and verify the Scene viewport frames that object.

**Acceptance Scenarios**:

1. **Given** the user has just clicked a GameObject in the Hierarchy panel (so the Hierarchy panel holds focus), **When** the user moves the pointer over the Scene viewport and drags with the middle mouse button or right mouse button, **Then** the Scene viewport immediately pans on that drag and acquires panel focus without requiring a prior left-click.
2. **Given** the user has selected one or more GameObjects in the Hierarchy panel, **When** the user presses `F` (or `Shift+F`), **Then** the Scene viewport frames the selected objects (or all scene objects for `Shift+F`) immediately even while the Hierarchy panel holds focus.
3. **Given** the Hierarchy panel or Scene viewport holds focus (and no text input field or modal dialog is active), **When** the user presses `Q`, `W`, `E`, or `R`, **Then** the active Scene tool switches to Hand, Move, Rotate, or Scale respectively.
4. **Given** any GameObject row in the Hierarchy panel, **When** the user double-clicks the row, **Then** the Scene viewport smoothly frames and centers on that GameObject.
5. **Given** the Hand tool (`Q`) is active, **When** the user left-clicks and drags inside the Scene viewport, **Then** the Scene viewport pans and acquires active panel focus.

---

### User Story 3 - Interactive Embedded Game Play Mode & Centered Playback Controls (Priority: P1)

A developer clicks Play from the centered main toolbar to test their game inside the embedded Game view, controls the player character directly using keyboard and mouse inputs when the Game view is focused, observes smooth 60 FPS gameplay, and uses Pause and Step controls to debug frame-by-frame behavior.

**Why this priority**: An embedded Game view that hides the runtime window without forwarding keyboard or mouse input makes it impossible to actually play or test gameplay mechanics inside the editor. Combined with a 15 FPS capture cap and buried playback controls, Play Mode fails its primary purpose of rapid interactive iteration.

**Independent Test**: Enable embedded Game view and start Play Mode from the centered main toolbar controls. Click inside the Game panel and press movement/jump keys; verify the player character responds immediately in the embedded view and that editor shortcuts (`Q`, `W`, `E`, `R`, `F`, `Delete`) do not fire while the running Game view is focused. Verify the embedded display updates smoothly at up to 60 FPS and that the editor chrome displays a distinct Play Mode visual indicator.

**Acceptance Scenarios**:

1. **Given** an embedded game session is running and the Game view panel is focused, **When** the user presses or releases keyboard keys or performs mouse actions inside the Game view, **Then** those inputs are delivered directly to the running game session so gameplay controls register immediately.
2. **Given** an embedded game session is running and the Game view panel has active input focus, **When** the user presses keys that normally trigger single-key editor shortcuts (such as `Q`, `W`, `E`, `R`, `F`, or `Delete`), **Then** those keys are consumed by the running game rather than triggering editor scene tools or edits.
3. **Given** an embedded game session is running, **When** viewing the Game panel, **Then** rendered frames update at an interactive rate of up to 60 FPS and the status bar accurately reflects whether the Game view currently captures gameplay input.
4. **Given** the main application toolbar, **When** viewing the top of the editor window, **Then** Play (`▶`), Pause (`⏸`), and Step (`⏭`) controls are prominently centered, and active Play Mode applies a clear visual tint/indicator to signal runtime execution.
5. **Given** the Game panel header, **When** inspecting the running or previewed game, **Then** users can select aspect ratio presets (such as Free Aspect, 16:9, or 4:3) and adjust preview zoom scale.

---

### User Story 4 - Clean, Glitch-Free Inspector & Component Authoring (Priority: P1)

An author selects single or multiple GameObjects and edits their Transform, Collider, Rigidbody, Sprite, Animator, Audio, and Script Behavior properties in a compact, visually consistent Inspector where component headers, context menus, 2D vectors, and numeric scrubbing work reliably.

**Why this priority**: The Inspector is the primary authoring panel for object and component data. Visual bugs such as missing-glyph menu buttons (`?`), header buttons that accidentally collapse the component when clicked, duplicated component titles, and inconsistent vertical X/Y fields create constant friction during property tuning.

**Independent Test**: Select a GameObject with Transform, Sprite, Collider, and Rigidbody components. Verify that every component title appears once in its collapsible header with a legible context menu button that opens the action menu without collapsing the section. Verify that Transform Position/Scale, Collider Offset, and Rigidbody Velocity/Force all render as compact horizontal X/Y rows with label drag-scrubbing.

**Acceptance Scenarios**:

1. **Given** any component attached to a GameObject in the Inspector, **When** viewing its collapsible header, **Then** the component name is displayed once in the header (without a duplicate separator title immediately beneath it) alongside a clearly rendered context menu button.
2. **Given** a component header's trailing menu button in the Inspector, **When** the user left-clicks the menu button, **Then** the component context menu (*Reset*, *Copy Settings*, *Paste Settings*, *Remove Component*) opens without toggling the component's expanded/collapsed state, and collapsing the component leaves clean vertical spacing before the next section.
3. **Given** a GameObject with Transform, Collider, and Rigidbody components, **When** viewing their 2D vector properties (`Position`, `Scale`, Collider `Offset`, Rigidbody `Velocity`, and Rigidbody `Force`), **Then** each X/Y pair is arranged horizontally on a single compact row.
4. **Given** any numeric property label in the Inspector (across both Transform and component fields), **When** the user clicks and drags horizontally on the label, **Then** the value scrubs continuously (with `Shift` for 10× speed, `Alt` for 0.1× precision, and `Ctrl` for snap increments) and commits as a single undoable edit on release.
5. **Given** a selected GameObject in the Inspector, **When** viewing the top of the Inspector panel, **Then** the GameObject identity header (Active checkbox, icon, Name, and Tag) appears at the very top above the Object Template controls and component sections.

---

### User Story 5 - Scene Viewport Overlay Controls, Marquee Selection & Asset Drag-Instantiation (Priority: P2)

A level designer arranges a 2D level in the Scene viewport using a compact single-bar/overlay tool strip, drags a selection box across empty space to select multiple objects at once, and drags sprite or template assets from the Project panel directly into the Scene or Hierarchy to instantiate new GameObjects.

**Why this priority**: Level design requires rapid spatial manipulation of many objects (platforms, coins, hazards) and fast placement of assets into the world. Wrapping text buttons across multiple rows wastes viewport canvas space, lack of marquee selection makes multi-object selection tedious, and lack of drag-and-drop asset instantiation forces repetitive manual object and component creation.

**Independent Test**: In the Scene viewport, verify tools and view options fit cleanly in a compact bar/overlay without multi-row text wrapping. Left-click and drag across empty canvas space in Move/Rotate/Scale mode to draw a marquee rectangle and verify all enclosed/intersected objects are selected. Drag an image or template from the Project panel onto the Scene canvas or Hierarchy panel and verify a configured GameObject is created at that location.

**Acceptance Scenarios**:

1. **Given** the Scene viewport with Move, Rotate, or Scale tool active, **When** the user left-clicks on empty space and drags a rectangle across multiple scene objects, **Then** a visible marquee selection box is drawn and all intersecting unlocked visible GameObjects become selected (with `Shift` or `Ctrl` adding/toggling selection).
2. **Given** the Scene viewport header/overlay, **When** viewing tool and view controls (`Hand`, `Move`, `Rotate`, `Scale`, `Grid`, `Snap`, `Zoom`, `Frame`), **Then** they are presented as compact icon/pill controls that preserve maximum canvas area without wrapping into multiple text rows.
3. **Given** a sprite texture or object template in the Project panel, **When** the user drags and drops it onto the Scene viewport canvas, **Then** a new GameObject configured with that asset is instantiated at the drop world coordinates in a single undoable action.
4. **Given** a sprite texture, audio clip, behavior script, or object template in the Project panel, **When** the user drags and drops it onto the Hierarchy panel, **Then** a new GameObject (or child of the targeted Hierarchy row) is instantiated with the corresponding component configured.

---

### User Story 6 - Project Browser Context Menus, Two-Column Navigation & Hierarchy Quick-Controls (Priority: P2)

A developer organizes project assets and scene objects using right-click context menus in the Project browser (to create folders, scripts, scenes, or templates, and to rename, duplicate, delete, or reveal files), navigates folders via a two-column tree and breadcrumbs with adjustable thumbnail sizes, and uses Hierarchy header creation presets and row visibility/lock icons.

**Why this priority**: Users expect the Project browser and Hierarchy panel to support self-contained authoring without switching to external file managers or memorizing hidden context options.

**Independent Test**: Right-click in the Project panel to create a new folder and rename or delete an asset. Use the Project panel's left folder tree, top breadcrumbs, and bottom thumbnail slider to browse assets at different sizes. In the Hierarchy panel, click the `+` button to create a preset 2D Sprite or Camera object, and click the inline visibility/lock gutter icons on a row to hide or lock an object in the Scene view.

**Acceptance Scenarios**:

1. **Given** the Project panel, **When** the user right-clicks empty space or an asset item, **Then** a context menu provides actions to *Create* (Folder, Behavior Script, Scene, Object Template), *Open*, *Reveal in File Manager*, *Rename* (`F2`), *Duplicate* (`Ctrl+D`), and *Delete* (`Delete`).
2. **Given** the Project panel layout, **When** browsing a project directory, **Then** a folder tree sidebar, clickable path breadcrumbs, and a footer size slider allow switching smoothly between a compact list view and scalable asset tiles.
3. **Given** the Hierarchy panel header, **When** clicking the `+` creation menu, **Then** the user can create an Empty GameObject, Empty Child, 2D Sprite, Camera, or Audio Object directly.
4. **Given** any GameObject row in the Hierarchy panel, **When** hovering the row or viewing an object with non-default scene visibility/lock state, **Then** clickable visibility (eye) and scene-picking lock icons in the row gutter toggle that object's visibility and pick-lock state directly.

---

### User Story 7 - Console Severity Badges, Standard Menu Bar & Neutral Charcoal Theme (Priority: P3)

A developer diagnoses build or runtime issues using independent Error/Warning/Info toggle badges and a split detail pane in the Console, navigates a standardized Unity-style top menu bar (`File | Edit | Assets | GameObject | Component | Window | Help`), and works in a neutral dark charcoal UI with crisp, scalable typography.

**Why this priority**: Visual consistency, readable typography at any display scale, standard menu organization, and at-a-glance diagnostic counts complete the Unity-inspired authoring experience and reduce cognitive load during long sessions.

**Independent Test**: Trigger Info, Warning, and Error messages in the Console; verify the three severity badge toggles display live counts, filter independently when clicked, show full details in the bottom pane when a row is selected, and open the source file location when double-clicked. Inspect the top menu bar and editor theme to verify standard menu categories, neutral charcoal palette, and crisp scalable font rendering.

**Acceptance Scenarios**:

1. **Given** the Console panel, **When** log messages are present, **Then** three independent toggle buttons for Info, Warnings, and Errors display live message counts and filter the log list independently when toggled.
2. **Given** a log entry in the Console panel, **When** clicked or double-clicked, **Then** a single click displays the full multi-line message and source details in a dedicated lower detail pane, and a double-click on an entry with a source file location opens that file in the external editor or file handler.
3. **Given** the main application menu bar, **When** browsing top-level menus, **Then** commands are organized under `File | Edit | Assets | GameObject | Component | Window | Help`.
4. **Given** the editor interface on standard or high-DPI displays, **When** viewing panels, input fields, and text, **Then** the theme uses a neutral dark charcoal palette with distinct input field borders, crisp scalable proportional typography, and a visible Play Mode tint when a game session is running.

---

### Edge Cases

- **Scene navigation when no object is selected**: Pressing `F` when no object is selected in the Hierarchy or Scene falls back to framing all visible scene objects; if the scene is completely empty, the camera resets to the world origin $(0, 0)$ at `100%` zoom.
- **Panning or marquee drag leaving viewport bounds**: If a user starts a middle-click/right-click pan, Hand-tool pan, or marquee box-selection inside the Scene canvas and drags the cursor outside the Scene panel bounds while holding the mouse button, the gesture continues smoothly until the button is released.
- **Shortcut suppression during text editing or active gameplay**: Single-key editor shortcuts (`Q`, `W`, `E`, `R`, `F`, `Delete`) MUST NOT fire when any text input field is active, when a popup/modal is open, or when the running embedded Game view panel holds focus.
- **Hidden or locked objects during marquee selection**: Objects hidden in the Scene view or marked pick-locked in the Hierarchy MUST be excluded from marquee box-selection.
- **Embedded game focus loss**: If the user clicks outside the Game panel (e.g., into the Inspector or Console) while the embedded game is running, any held gameplay keys are cleanly released so the character does not remain stuck moving in one direction.
- **Large repository workspaces**: Opening the editor in a repository containing tens of thousands of files inside virtual environments, build artifacts, or specification folders MUST NOT exceed watcher limits or trigger multi-second background CPU spikes.

---

## Requirements *(mandatory)*

### Functional Requirements

#### Performance & Idle Lifecycle
- **FR-001**: When no user input, active drag/animation, or due background task is present, the editor main loop MUST block waiting for input events up to the idle heartbeat interval instead of executing zero-timeout busy-spin iterations.
- **FR-002**: Background project and fallback reference schedules MUST only register wake-up deadlines when a check is genuinely pending and not already executing on a background worker.
- **FR-003**: Project directory watching and source/asset scanning MUST exclude non-project directories (including hidden dot-directories, virtual environments, specification/benchmark archives, and build outputs) so directory watch limits are not exhausted on standard development workspaces.
- **FR-004**: Unchanged executable metadata, template bindings/schemas, and accessibility semantic trees MUST be cached across frames and recomputed only when underlying document, selection, layout, or project state changes.
- **FR-005**: Display presentation MUST synchronize with vertical refresh (VSync) during normal interactive execution, and no diagnostic debug text may be printed per-frame during viewport navigation.

#### Focus & Scene Navigation
- **FR-006**: Initiating a middle-click drag, right-click drag, or Hand-tool left-click drag over the Scene viewport canvas MUST immediately pan the camera and assign focus to the Scene panel, regardless of which panel previously held focus.
- **FR-007**: Pressing `F` (Frame Selected), `Shift+F` (Frame All), or `Q`/`W`/`E`/`R` (Scene tool selection) MUST take effect when either the Scene panel or the Hierarchy panel holds focus, provided no text input or modal popup is capturing keyboard input.
- **FR-008**: Double-clicking a GameObject row in the Hierarchy panel MUST frame and center that GameObject in the Scene viewport.
- **FR-009**: Panel focus state MUST remain synchronized with window interaction across all panels so keyboard shortcuts always act on the panel the user most recently clicked or dragged in.

#### Embedded Game Playability & Main Toolbar
- **FR-010**: When an embedded game session is running and the Game view panel holds focus, keyboard and mouse events inside the Game view MUST be forwarded to the running game session in real time.
- **FR-011**: When the Game view loses focus while an embedded game session is running, any currently pressed forwarded inputs MUST be released cleanly, and when the Game view holds focus, single-key editor shortcuts (`Q`, `W`, `E`, `R`, `F`, `Delete`) MUST be suppressed in favor of gameplay input.
- **FR-012**: Embedded game frame capture and display MUST support interactive refresh rates of up to 60 FPS, and the Game view status text MUST accurately indicate whether gameplay input is active in the Game view or requires clicking the Game view to focus.
- **FR-013**: The main application toolbar MUST center the Play (`▶`), Pause (`⏸`), and Step (`⏭`) execution controls, and the editor chrome MUST display a distinct visual Play Mode tint while a game session is active.
- **FR-014**: The Game view header MUST provide aspect ratio presets (Free Aspect, 16:9, 4:3) and a zoom scale control.

#### Inspector Layout & Authoring Ergonomics
- **FR-015**: Component headers in the Inspector MUST render a single collapsible title bar with a legible context menu button (without missing-glyph placeholders) whose click area does not toggle the header's collapsed state, and collapsing a component MUST preserve normal vertical spacing to the next element.
- **FR-016**: Duplicate component title separators inside component bodies MUST be removed so each component's name appears only once in its header.
- **FR-017**: Built-in 2D vector properties across components (Transform `Position` and `Scale`, Collider `Offset`, and Rigidbody `Velocity` and `Force`) MUST be rendered as compact single-row horizontal `X` / `Y` field pairs.
- **FR-018**: Dragging horizontally on numeric property labels in the Inspector MUST scrub the numeric value continuously (with `Shift` for 10× speed, `Alt` for 0.1× precision, and `Ctrl` for snapping) and commit a single atomic undo command on mouse release.
- **FR-019**: The GameObject identity header (Active checkbox, icon, Name, and Tag) MUST be positioned at the top of the Inspector above the Object Template controls and component list.

#### Scene Viewport Overlay, Marquee Selection & Asset Drag-and-Drop
- **FR-020**: Left-clicking and dragging across empty space in the Scene viewport while in Move, Rotate, or Scale mode MUST draw a marquee selection rectangle and select all intersecting visible, unlocked GameObjects.
- **FR-021**: The Scene viewport toolbar MUST present tool selection (`Hand`, `Move`, `Rotate`, `Scale`) and view controls (`Grid`, `Snap`, `Zoom`, `Frame`) in a compact single-row or floating overlay layout that does not wrap into multi-row text strips.
- **FR-022**: Dragging a sprite image or object template from the Project panel onto the Scene viewport MUST instantiate a corresponding GameObject at the world coordinates under the cursor as an undoable command.
- **FR-023**: Dragging a sprite image, audio file, behavior script, or object template from the Project panel onto the Hierarchy panel MUST instantiate a configured GameObject (or attach the behavior to a targeted GameObject row) as an undoable command.

#### Project Browser, Hierarchy Quick-Controls, Console & Menus
- **FR-024**: The Project panel MUST provide right-click context menus on folders, assets, and empty space supporting *Create* (Folder, Behavior Script, Scene, Object Template), *Open*, *Reveal in File Manager*, *Rename* (`F2`), *Duplicate* (`Ctrl+D`), and *Delete* (`Delete`).
- **FR-025**: The Project panel MUST provide a two-column layout (folder tree on the left, folder contents on the right) with path breadcrumbs and a thumbnail size slider.
- **FR-026**: The Hierarchy panel MUST provide a header `+` creation dropdown for common GameObject presets (Empty, Empty Child, 2D Sprite, Camera, Audio Object) and inline per-row gutter icons to toggle scene visibility and pick-locking.
- **FR-027**: The Console panel MUST provide three independent right-aligned severity toggle badges (Info, Warnings, Errors) showing live counts, a lower detail pane for the selected log entry, and double-click navigation to source file locations.
- **FR-028**: The top application menu bar MUST organize editor commands under `File | Edit | Assets | GameObject | Component | Window | Help`.
- **FR-029**: The editor visual theme MUST use a neutral dark charcoal color palette with visible 1px input frame borders and crisp scalable proportional typography across standard and high-DPI displays.

---

### Key Entities

- **Idle Wake Schedule**: The consolidated set of pending timer deadlines and input events governing when the main loop sleeps versus renders an active frame.
- **Panel Focus State**: The authoritative record of which editor panel (`Hierarchy`, `Scene`, `Inspector`, `Project`, `Console`, `Game`) currently owns keyboard shortcut routing and pointer capture.
- **Embedded Input Event**: A serialized keyboard or mouse interaction captured from a focused Game view panel and delivered to the active embedded runtime session.
- **Marquee Selection Box**: A screen-space and world-space rectangle defined by an empty-canvas drag gesture used to query and select unlocked visible scene objects.
- **Asset Instantiation Payload**: A drag-and-drop descriptor linking a project asset path and asset kind to the default GameObject and component structure created when dropped into the Scene or Hierarchy.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: When the editor is open and idle (no user input or active game session), CPU utilization drops below 2% and median idle wait duration per loop iteration exceeds 50 ms (compared to the baseline 100% CPU spin with < 0.05 ms wait).
- **SC-002**: Active interactive frame time while panning or inspecting `examples/mario/level1.scene` is under 16.6 ms (60 FPS), a > 3.5× reduction from the 60–67 ms baseline.
- **SC-003**: Project directory watching on the repository completes within the watcher directory budget with zero fallback full-repository source scans triggered during idle operation.
- **SC-004**: 100% of Scene navigation actions (`F`, `Shift+F`, `Q`/`W`/`E`/`R`, middle-click pan, and right-click pan) succeed on the very first input immediately after clicking an object in the Hierarchy panel, without requiring an intermediate left-click in the Scene view.
- **SC-005**: In embedded Game view mode, keyboard and mouse gameplay inputs register within 1 frame of user input and embedded frame display updates at up to 60 FPS (4× higher than the 15 FPS baseline).
- **SC-006**: 100% of built-in 2D vector properties in the Inspector (Transform Position/Scale, Collider Offset, Rigidbody Velocity/Force) render on single horizontal rows, and 0 component headers display duplicate titles or missing-glyph (`?`) menu buttons.
- **SC-007**: Users can instantiate a sprite or template into a scene, multi-select objects via marquee drag, and create/rename/delete project assets entirely through direct mouse/context interactions without leaving the editor.
- **SC-008**: 100% of existing and newly added automated editor test suites pass in headless mode without regressions to lossless scene file persistence or undo/redo integrity.

---

## Assumptions

- **Lossless Scene Format Preservation**: All UI, layout, and workflow enhancements operate on the existing scene and object template file formats without breaking backward compatibility or round-trip fidelity.
- **Embedded Session Transport**: Forwarding keyboard and mouse events to the embedded game extends the existing local editor-session control channel between the editor and child runtime process; external (non-embedded) game windows continue to receive OS window input directly.
- **Typography Bundling**: A permissively licensed proportional UI font (or system UI font fallback) can be loaded by the editor with fallback glyphs for standard UI symbols so that headless and desktop environments render consistently.
- **Incremental Phased Delivery**: Although this specification encompasses the entirety of the audit across all panels and workflows, implementation tasks can be ordered so that P1 performance, focus, embedded playability, and Inspector fixes land first, followed by P2 workflow actions and P3 visual polish.
