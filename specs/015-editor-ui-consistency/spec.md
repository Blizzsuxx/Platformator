# Feature Specification: Editor UI Consistency and Unity Usability Cleanup

**Feature Branch**: `015-editor-ui-consistency`

**Created**: 2026-11-12

**Status**: Draft

**Input**: User description: "create a specification that would include a cleanup of the found issues"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Stable Layout and Unified Selection Behavior (Priority: P1)

A game developer selecting objects in the Scene view, browsing rows in the Hierarchy, or clicking assets in the Project browser experiences a rock-steady interface where clicking an item never shifts surrounding panels, hides unrelated components, or un-highlights the selected object in other panels.

**Why this priority**: Layout shifts on click (such as the Project asset grid jumping downward when selecting an asset, or the Scene canvas shifting when saving a scene) and broken selection synchronization (clicking an object in the Scene view hiding all its other components in the Inspector and deselecting its Hierarchy row) directly break core authoring interactions like double-clicking, inspecting objects, and renaming.

**Independent Test**: Can be fully tested by clicking objects in both the Scene view and Hierarchy, hovering across Hierarchy rows, clicking assets in the Project browser, and saving an untitled scene while verifying that panel regions do not jump vertically or horizontally and that the Inspector and Hierarchy display identical selection state regardless of where the object was clicked.

**Acceptance Scenarios**:

1. **Given** a GameObject with multiple components (e.g., Transform, Sprite, Collider, Script), **When** the user clicks that object in the Scene viewport, **Then** the Hierarchy highlights that GameObject's row, `F2` rename remains available in the Hierarchy, and the Inspector displays all components on that GameObject (with the clicked component optionally scrolled into view or highlighted, never hiding sibling components).
2. **Given** the Project panel is displaying a folder tree and asset grid/list, **When** the user clicks or double-clicks any asset file (texture, audio, animation, script, scene, or template), **Then** the top boundary of the folder tree and asset grid remains at the exact same vertical position without injecting assignment or region-editing panels above the browser.
3. **Given** the Hierarchy panel is visible with multiple GameObjects, **When** the user moves the pointer across rows, **Then** each GameObject's primary icon remains fixed on the left side of its label and only the visibility/lock action controls appear in the right-hand gutter on hover.
4. **Given** an unsaved scene or a narrow editor window, **When** the user saves the scene, triggers a hierarchy status message, or edits an animation tab, **Then** the Scene viewport canvas, Hierarchy search bar, main dockspace, and Animation tab widths do not jump or shift adjacent UI elements.

---

### User Story 2 - Elimination of Duplicate Controls and Redundant Feedback (Priority: P1)

A developer navigating the editor's toolbars, panel headers, menus, and Inspector encounters a single, unambiguous control for each action within any visual region and sees status or error feedback rendered once.

**Why this priority**: Duplicate side-by-side buttons and doubled error messages create visual confusion, waste toolbar space, and make the editor feel unpolished compared to Unity.

**Independent Test**: Can be fully tested by inspecting the Hierarchy header, Scene toolbar, Main Menu bar, Main Toolbar, and Inspector panel after triggering an edit/validation message, confirming that no action or message is duplicated within the same region.

**Acceptance Scenarios**:

1. **Given** the Hierarchy panel header, **When** the user views the creation controls, **Then** there is a single creation button (`+`) that opens the GameObject creation menu rather than two adjacent creation buttons.
2. **Given** the Scene viewport toolbar and canvas, **When** the user views snapping and zoom controls, **Then** snapping settings are opened by a single control (combining or separating toggle vs. settings cleanly without two adjacent buttons opening the identical popup) and zoom percentage is shown only once in the Scene toolbar without a duplicate zoom badge in the canvas corner.
3. **Given** the Inspector panel, **When** an action produces a feedback or validation message, **Then** the message is displayed once in the Inspector rather than twice.
4. **Given** the Main Menu bar, **When** the user opens `Edit`, `Window`, and `Help`, **Then** `Preferences...` appears only under `Edit`, and the `Window` menu contains only window, panel, and layout management items rather than duplicating build, playback, or scene framing commands.

---

### User Story 3 - Clean, Unity-Style Information Density and Inline Workflows (Priority: P2)

A developer reading the Hierarchy, Inspector, Project browser, Console, and Game view sees clean, human-readable names, accurate tooltips, and inline editing workflows without internal numeric IDs, raw data dumps, or unnecessary modal popups.

**Why this priority**: Stripping internal IDs (`#14`), wrong tooltips, verbose technical prose, and blocking rename modals aligns the editor's day-to-day ergonomics and visual clarity with Unity.

**Independent Test**: Can be fully tested by inspecting object labels in the Hierarchy and reference pickers, hovering headers and folders for tooltips, renaming an asset with `F2`, and viewing the Inspector and Game panels during authoring and play sessions.

**Acceptance Scenarios**:

1. **Given** GameObjects in the Hierarchy and reference target selectors in the Inspector, **When** their labels are displayed, **Then** they show clean GameObject and component names without internal numeric ID suffixes (such as `(#14)`), exposing internal IDs only in tooltip details or diagnostic views when needed for disambiguation.
2. **Given** the Inspector for any selected GameObject, **When** the user views the top of the Inspector, **Then** `Transform` appears immediately below the GameObject identity header (`Active`, `Name`, `Tag`, `Layer`), and template/prefab controls appear compactly without an always-expanded instructional block pushing `Transform` down.
3. **Given** an asset selected in the Project panel, **When** the user presses `F2` or chooses `Rename`, **Then** the asset name becomes editable inline on the tile or list row (matching the Hierarchy's inline rename behavior) rather than opening a blocking modal dialog, and deleting an asset prompts for confirmation consistently with scene deletion.
4. **Given** the user hovers over the `Transform` header in the Inspector, a folder in the Project tree, or the embedded game option in Run Settings, **Then** tooltips are either omitted when redundant with visible text or accurately describe the hovered control (never showing unrelated behavior drag-and-drop instructions on `Transform` or contradicting embedded input support).

---

### User Story 4 - Consistent Widget Styling, Multi-Object Editing, and UI Scaling (Priority: P2)

A developer editing single or multiple objects, adjusting numeric properties, assigning asset references, or working at high display scales (e.g., 150%–200% UI scale) experiences uniform widget formatting, collapsible headers, search bars, and proportional layouts across every panel.

**Why this priority**: Consistent widget behavior (readable decimal formatting, uniform component headers in single and multi-object modes, compact object reference slots, and proper UI scaling) ensures predictable muscle memory across the entire editor.

**Independent Test**: Can be fully tested by scrubbing float values in the Inspector and Behavior fields, selecting multiple GameObjects to compare component headers against single-object selection, testing search bars across all four searchable panels, and setting UI scale to 150% and 200%.

**Acceptance Scenarios**:

1. **Given** numeric floating-point fields in the Inspector (Transform, components, behavior scripts, and multi-object selection), **When** values are displayed or scrubbed, **Then** they use clean, consistent decimal formatting without raw floating-point precision artifacts (such as `0.300000012`), and support drag-on-label scrubbing consistently.
2. **Given** multiple GameObjects are selected, **When** the user views shared components in the Inspector, **Then** each shared component is rendered inside a collapsible component header with a `⋮` context menu for `Copy Settings`, `Paste Settings`, and `Reset`—matching single-object Inspector presentation instead of rendering plain text headers and raw inline button rows.
3. **Given** an asset reference field in the Inspector, **When** the user views or edits it, **Then** it renders as a single compact Unity-style object slot (supporting drag-and-drop, click-to-reveal, a picker button, and clear action) without a separate collapsible `"Edit asset path"` sub-tree below every field.
4. **Given** the editor UI scale is set between 100% and 200%, **When** the user views the Inspector, Scene toolbar, Project browser, Hierarchy, and Console, **Then** all label columns, axis badges, icon buttons, and right-aligned toolbar groups scale proportionally without text clipping or control overlap.
5. **Given** the search bars in Hierarchy, Project, Inspector, and Console, **When** the user enters a filter query, **Then** all search bars provide a consistent inline clear (`×`) affordance and uniform placeholder style.

---

### User Story 5 - Unity-Aligned Toolbar, Console, and Game View Ergonomics (Priority: P3)

A developer switching tools, running play sessions, and inspecting diagnostics uses a unified top toolbar and clean Game and Console panels that mirror Unity's layout conventions.

**Why this priority**: Consolidating transform tools and playback controls into the main toolbar and decluttering the Game and Console panels completes the visual and functional convergence toward Unity's workspace design.

**Independent Test**: Can be fully tested by using the top toolbar to switch tools and control play mode (Play, Pause, Step, Stop), viewing the Game panel toolbar during play mode, and filtering logs in the Console panel.

**Acceptance Scenarios**:

1. **Given** the Main Toolbar at the top of the editor window, **When** the user views its layout, **Then** the left cluster provides quick scene/transform tool access, the center cluster provides unified playback controls (`Play` / `Build and Run`, `Pause`, `Step`, `Stop`), and redundant scene-name / save-status text already shown in the Scene tab bar is omitted so the toolbar stays a fixed single-row height.
2. **Given** the Game panel during idle or active play sessions, **When** the user views the panel, **Then** top controls are organized into a single compact toolbar row (`Aspect`, `Scale`, and optional runtime inspection drawer/toggle) above a centered game viewport canvas, without multi-line protocol explanations or raw JSON dumps beneath the frame.
3. **Given** the Console panel, **When** diagnostic rows and filter badges are rendered, **Then** rows and filter buttons use the editor's vector severity icons (Info, Warning, Error) and compact toolbar toggle buttons rather than ASCII prefixes (`[x] ERROR`, `[!] WARNING`, `[i] INFO`).

---

### Edge Cases

- **Scene Picking on Multi-Component Objects**: When an object has overlapping visual/physical representations (e.g., both a Sprite and a Collider) and is clicked in the Scene viewport, the GameObject is selected in both Hierarchy and Inspector with all its components visible; repeated clicks may cycle which object is picked without ever hiding sibling components on the selected GameObject.
- **Disambiguating Duplicate Object Names**: When two GameObjects in the scene share the same name (e.g., two objects named `"Enemy"`), reference pickers disambiguate them using hierarchy path context (or subtle disambiguation suffixes only when names collide) rather than appending `(#id)` to every object in the editor.
- **Narrow Panel and Window Widths at High UI Scale**: When a panel or the main window is resized narrower at 150%–200% UI scale, toolbars clip or elide gracefully within their fixed single-row height rather than wrapping into multi-row banners that shift the dockspace vertically.
- **Accidental Asset Deletion**: Pressing `Delete` on a folder or asset in the Project browser prompts for confirmation before permanently removing files from disk, matching the safety of Scene deletion.
- **Sprite Source Region Editing Without Project Panel Jump**: Editing a Sprite's source rectangle or an Animation frame's source region remains fully accessible from the Inspector (for Sprite components) and the Animation Asset editor (for animation frames) without injecting a canvas into the Project browser panel.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Selecting a GameObject from the Scene viewport MUST select the GameObject identically to selecting it in the Hierarchy—highlighting its Hierarchy row, keeping `F2` rename active, and displaying all of the GameObject's components in the Inspector.
- **FR-002**: Selecting or double-clicking any asset in the Project panel MUST NOT insert or expand assignment, template, or sprite-region controls above the folder tree or asset content view; the folder tree and asset grid/list top boundary MUST remain stationary.
- **FR-003**: Hierarchy rows MUST render the GameObject icon in the left label column before the object name, and MUST reserve the right-hand row gutter exclusively for visibility (eye) and pickability (lock) indicators/buttons so icons never swap sides on hover.
- **FR-004**: The Scene viewport, Hierarchy panel, Main Toolbar, and Animation tab bar MUST maintain stable layout geometry across state transitions (saving an untitled scene, displaying status feedback, resizing the window, or marking an animation tab dirty) without shifting the canvas, search bar, dockspace, or adjacent tabs.
- **FR-005**: The Hierarchy panel header MUST provide a single GameObject creation button (`+`), removing the duplicate adjacent `+ Object` button.
- **FR-006**: The Scene viewport toolbar MUST consolidate grid/snap controls so that no two adjacent buttons open the identical snap settings popup, and MUST display zoom percentage in a single location without a duplicate zoom badge on the canvas.
- **FR-007**: The Inspector panel MUST render validation and command feedback messages at most once per panel frame, removing the duplicate feedback text output in the host window wrapper.
- **FR-008**: The Main Menu bar MUST list `Preferences...` only under the `Edit` menu and MUST restrict the `Window` menu to window, panel, and layout management actions (removing duplicate build, run, stop, zoom, and frame commands from `Window`).
- **FR-009**: Hierarchy rows, Inspector headers, Project assignment targets, and reference fields MUST display human-readable GameObject and component names without appending internal numeric IDs (`(#id)`), using hierarchy paths or tooltips when disambiguation is required.
- **FR-010**: The Inspector MUST place the `Transform` component section immediately below the GameObject identity header (`Active`, `Name`, `Tag`, `Layer`), and MUST remove the unrelated behavior drag-and-drop tooltip from the `Transform` header.
- **FR-011**: The Project panel MUST support inline asset renaming on `F2` / `Rename` directly on the selected asset tile or list row (replacing the blocking rename modal dialog) and MUST require user confirmation before permanently deleting assets or folders from disk.
- **FR-012**: Tooltips across the Project folder tree, asset tiles, Inspector headers, and Run Settings MUST be accurate and non-redundant (omitting tooltips that merely repeat visible folder names, showing project-relative paths on assets, and accurately describing embedded Game view input capture).
- **FR-013**: All floating-point numeric inputs across the Inspector (Transform, built-in components, behavior fields, and multi-object selection) MUST use consistent human-readable decimal formatting (avoiding raw precision artifacts such as `%.9g` / `%.17g` noise) and consistent label-drag scrubbing.
- **FR-014**: Multi-object Inspector editing MUST render shared components using the same collapsible component headers and `⋮` context menu (`Copy Settings`, `Paste Settings`, `Reset`) as single-object editing, rather than plain text headings and inline button rows.
- **FR-015**: Asset reference fields in the Inspector MUST be presented as a single compact row (supporting drag-and-drop, click-to-select/browse, and clear) without rendering a separate collapsible `"Edit asset path"` sub-tree under each field, and Sprite source-region editing MUST reside in the Inspector's Sprite component rather than inside the Project panel.
- **FR-016**: All panel layouts, column offsets, icon buttons, axis badges, and toolbar alignments in the Inspector, Scene viewport, Hierarchy, Project browser, and Console MUST scale proportionally with the editor's UI scale factor (from 100% to 200%).
- **FR-017**: Search bars in the Hierarchy, Project, Inspector, and Console panels MUST share a consistent layout with an inline clear (`×`) button and consistent placeholder text.
- **FR-018**: The Main Toolbar, Game panel, and Console panel MUST follow Unity workspace conventions: a fixed single-row Main Toolbar with unified playback controls (`Play`/`Build and Run`, `Pause`, `Step`, `Stop`), a compact single-row Game view control bar without raw JSON dumps in the viewport, and vector severity icons in the Console panel.

### Key Entities

- **Selection State**: Represents the currently selected GameObject(s) and optional focused sub-target (component or behavior) without restricting which components of the selected GameObject are rendered in the Inspector or highlighted in the Hierarchy.
- **Panel Chrome & Toolbar Layout**: The fixed-height header bars, search bars, tab bars, and status areas that frame each editor panel without shifting content vertically during normal user interactions.
- **Component & Property Row**: The standardized visual unit in the Inspector consisting of a collapsible component header with a `⋮` action menu and proportional label/control rows supporting drag scrubbing, mixed-value state, and UI scaling.
- **Asset Browser Item**: A folder or asset entry in the Project panel supporting selection, drag-and-drop, context menu actions, inline rename, confirmation-guarded deletion, and non-shifting layout.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Clicking any GameObject in the Scene viewport, Hierarchy row, or asset tile in the Project panel causes `0 px` of vertical layout shift in the clicked panel's content region and preserves 100% of the selected GameObject's components in the Inspector.
- **SC-002**: 0 duplicate action buttons or duplicate feedback messages remain within the same visual bar or panel across the Hierarchy header, Scene toolbar, Main Menu bar, and Inspector.
- **SC-003**: 100% of Hierarchy rows and default Inspector reference labels display clean names without `(#id)` numeric suffixes, and 100% of searchable panels (Hierarchy, Project, Inspector, Console) provide a one-click search clear button.
- **SC-004**: Across UI scale settings from 100% to 200%, 100% of Inspector property labels, axis tags, component header icons, and Scene toolbar controls render without overlapping adjacent controls or wrapping into unintended multi-row jumps.
- **SC-005**: All automated editor GUI and semantic regression tests pass with updated expectations reflecting the unified selection, toolbar, and panel layouts.

## Assumptions

- Existing undo/redo command semantics, scene file serialization formats (`.scene`), template formats (`.pttemplate`), and animation clip formats remain unchanged; this feature strictly cleans up GUI presentation, layout stability, and interaction ergonomics.
- Where automated tests or semantic node queries currently rely on specific node IDs (such as console severity badges, hierarchy labels, or asset assignment actions), semantic node IDs will remain backward-compatible or have their corresponding GUI tests updated in lockstep.
- Moving interactive Sprite source-region editing and asset assignment out of the top of the Project panel and into the Inspector (plus drag-and-drop and the Inspector's asset picker popup) preserves all existing authoring capabilities while eliminating the Project panel layout jump.
