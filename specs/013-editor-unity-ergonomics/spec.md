# Feature Specification: Unity-Inspired Editor Ergonomics (Phase 1)

**Feature Branch**: `branch-without-bvh`

**Created**: 2026-11-12

**Status**: Draft

**Input**: User description: "create a specification for the Phase one, as per the review"

## Clarifications

### Session 2026-11-12

- Q: Should the Hierarchy window permanently display GameObjects only, or should an optional toggle be provided in the View menu to show components in the tree? → A: Option A (Strictly GameObjects-only; no toggle or legacy mode).
- Q: When dragging property labels to scrub numeric values in the Inspector, which modifier key behaviors should be supported? → A: Option A (Unity standard modifiers: Shift for 10× speed, Alt for 0.1× fine precision, Ctrl for snapping to configured increments).
- Q: When pressing F (Frame Selected) in the Scene viewport with no object selected, what should the viewport do? → A: Option A (Fallback to Frame All, framing the full scene bounds).
- Q: Should the component context menu in the Inspector be accessible by both clicking the `⋮` icon and right-clicking anywhere on the component header? → A: Option A (Both: left-clicking `⋮` or right-clicking anywhere on the component header opens the action menu).
- Q: When multiple GameObjects are selected and a Transform property (such as Position X) is edited in the Inspector, how should the change apply across the selection? → A: Option A (Set absolute value across all selected objects; show dash placeholder '—' when values differ).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Clean GameObject-Only Hierarchy Navigation (Priority: P1)

A level designer or game developer opens a complex scene containing multiple nested GameObjects with numerous attached components and navigates the scene structure through the Hierarchy window without visual clutter.

**Why this priority**: In Unity, the Hierarchy window represents the scene graph of GameObjects and their parent-child relationships exclusively. Mixing components into the tree expands row counts exponentially, obfuscates scene hierarchy, and causes misclicks and parenting errors during drag-and-drop operations.

**Independent Test**: Open a scene with multi-level nested GameObjects and multiple components per object. The Hierarchy displays only GameObject names. Selecting any GameObject immediately reveals all of its attached components in the Inspector. Dragging an object between or onto other objects reparents it cleanly.

**Acceptance Scenarios**:

1. **Given** a scene with GameObjects owning Sprite, Collider, Rigidbody, and Script components, **When** viewing the Hierarchy panel, **Then** only GameObject rows are visible in the tree; component sub-nodes are not displayed.
2. **Given** a GameObject selected in the Hierarchy, **When** examining the Inspector, **Then** all attached components for that GameObject are displayed sequentially.
3. **Given** a parent GameObject with children, **When** expanding or collapsing the parent in the Hierarchy, **Then** only child GameObjects toggle visibility.
4. **Given** a GameObject being dragged in the Hierarchy, **When** hovering over target rows, **Then** drop indicators identify parenting or reordering between GameObjects without ambiguous component targets.
5. **Given** the Hierarchy empty space, **When** right-clicking or activating the header creation menu, **Then** users can create an empty GameObject or child GameObject directly.

---

### User Story 2 - Compact Transform and Streamlined Component Headers (Priority: P1)

An author inspects and configures GameObjects and components in the Inspector using compact, familiar property layouts and header action menus.

**Why this priority**: The Inspector is the primary property authoring surface. Laying out X/Y coordinates across multiple stacked vertical lines and placing bulky utility buttons inside component bodies wastes vertical screen space and requires excessive scrolling. Grouping axis coordinates into horizontal rows and moving component management actions into header context menus dramatically improves authoring speed.

**Independent Test**: Select a GameObject with Transform and Sprite components. Verify that Position X/Y and Scale X/Y are presented side-by-side in compact horizontal rows. Verify that the GameObject header provides Active, Name, and Tag fields at the top. Open the component header `⋮` menu to execute Reset, Copy, Paste Values, and Remove Component actions.

**Acceptance Scenarios**:

1. **Given** a selected GameObject, **When** viewing the Inspector top header, **Then** the Active state checkbox, GameObject icon, and Name text field are displayed prominently in the header area, with Tag configured below.
2. **Given** the Transform component section, **When** inspecting Position and Scale, **Then** Position X and Y appear side-by-side on a single horizontal row with distinct axis labels, and Scale X and Y appear side-by-side on a single horizontal row.
3. **Given** any numeric field in the Transform or built-in components, **When** dragging the axis label horizontally, **Then** the numeric value scrubs smoothly with undoable changes, supporting `Shift` for 10× speed, `Alt` for 0.1× precision, and `Ctrl` for snapping to configured increments.
4. **Given** a component in the Inspector, **When** opening its context menu by clicking the trailing `⋮` icon or right-clicking anywhere on the component header, **Then** options to *Reset*, *Copy Settings*, *Paste Settings*, and *Remove Component* are available.
5. **Given** a component header with the new context menu, **When** viewing the component body, **Then** redundant body buttons ("Copy Settings", "Paste Settings", "Reset Settings") are removed, freeing vertical space for properties.
6. **Given** the *Remove Component* action selected from the header menu, **When** executed, **Then** the component is removed from the GameObject, the Inspector updates immediately, and a single Undo operation restores the component and its exact configuration.

---

### User Story 3 - Familiar Navigation & Framing Shortcuts (Priority: P1)

An author navigates the 2D Scene viewport using standard game engine shortcuts to frame selections and pan the camera smoothly.

**Why this priority**: Keyboard muscle memory is critical for workflow fluidity. Inverting the standard `F` shortcut (which Unity uses for Frame Selected) forces users to unlearn standard engine habits. Providing `Q` for Hand/Pan tool navigation enables one-handed trackpad or mouse navigation without forcing middle-mouse or right-mouse chord gestures.

**Independent Test**: Select an object in the Hierarchy or Scene. Press `F` to center and zoom the viewport directly to that selected object. Press `Shift+F` to frame the entire scene. Press `Q` to switch to the Hand/Pan tool and left-click drag to pan the viewport smoothly.

**Acceptance Scenarios**:

1. **Given** one or more selected objects in the scene, **When** pressing `F`, **Then** the Scene viewport frames and centers the selected objects.
2. **Given** an open scene with objects, **When** pressing `Shift+F`, **Then** the Scene viewport frames all active objects across the entire scene bounds.
3. **Given** the Scene viewport focused, **When** pressing `Q`, **Then** the viewport switches to the Hand/Pan tool mode.
4. **Given** the Hand/Pan tool active (`Q`), **When** left-clicking and dragging anywhere within the Scene viewport, **Then** the camera pans smoothly following the pointer.
5. **Given** any active text input field (e.g. typing a GameObject name or numeric value), **When** typing characters including `f`, `q`, `w`, `e`, or `r`, **Then** the keystrokes are captured solely by the text input and do NOT trigger framing or tool mode switches.
6. **Given** no object is selected in the scene, **When** pressing `F`, **Then** the Scene viewport falls back to framing all visible objects across the entire scene bounds.

---

### Edge Cases

- **Object without visual bounds**: When pressing `F` on an empty GameObject or an object with no Sprite or Collider, the viewport centers on the GameObject's position coordinates without zooming in excessively or throwing division-by-zero errors.
- **Deeply nested GameObjects**: Collapsing an ancestor in the Hierarchy correctly hides all descendant GameObjects, and re-expanding restores the previous expansion state of its children.
- **Pasting component settings across mismatched types**: The Paste Settings action in the header context menu remains disabled or rejects pasting when the clipboard contains settings from an incompatible component type.
- **Multi-object selection in Inspector**: When multiple GameObjects are selected, shared Transform properties display their common value if identical across all selected objects, or a dash placeholder (`—`) if values differ. Editing a property assigns that specified absolute value across all selected objects within a single consolidated undo command.
- **Rapid tool switching during drag gestures**: Pressing `Q`, `W`, `E`, or `R` while actively dragging a transform handle or panning does not disrupt or corrupt the active gesture.

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The Hierarchy tree MUST strictly display only GameObject nodes, permanently excluding component sub-nodes and child rows from the tree hierarchy.
- **FR-002**: Selecting any GameObject row in the Hierarchy MUST display all attached components for that GameObject in the Inspector panel.
- **FR-003**: The Inspector header MUST display the GameObject's active checkbox, icon, and editable name field in a unified top header layout.
- **FR-004**: The Transform component MUST display 2D vector properties (`Position` X/Y and `Scale` X/Y) in horizontal two-column groupings with labeled axis indicators.
- **FR-005**: Every component header in the Inspector MUST provide a context action menu accessible by both left-clicking a trailing `⋮` button and right-clicking anywhere on the component header, containing *Reset*, *Copy Settings*, *Paste Settings*, and *Remove Component*.
- **FR-006**: Redundant in-body utility buttons (*Copy Settings*, *Paste Settings*, *Reset Settings*) MUST be removed from component body layouts in favor of the header menu.
- **FR-007**: Executing *Remove Component* from the Inspector header menu MUST remove the targeted component and register an atomic undo transaction that restores the component upon Undo.
- **FR-008**: Pressing the `F` key when an object is selected MUST frame the selection in the Scene viewport; pressing `F` with no selection active MUST fall back to framing all visible scene objects.
- **FR-009**: Pressing `Shift+F` MUST frame all visible objects in the scene.
- **FR-010**: Pressing `Q` when the Scene viewport has focus MUST activate the Hand/Pan tool mode.
- **FR-011**: While the Hand/Pan tool is active, left-clicking and dragging within the viewport MUST pan the camera.
- **FR-012**: Viewport shortcuts (`Q`, `W`, `E`, `R`, `F`) MUST be suppressed while any text input field or modal dialog has active input focus.
- **FR-013**: Dragging horizontally over numeric property labels in the Inspector MUST scrub the numeric value continuously (supporting `Shift` for 10× speed, `Alt` for 0.1× precision, and `Ctrl` for snapping increments) and record a single consolidated undo command upon mouse release.
- **FR-014**: Modifying a Transform property while multiple GameObjects are selected MUST assign the specified absolute value across all selected objects within a single atomic undo transaction.

---

### Key Entities

- **Hierarchy Row**: Represents an authorable GameObject in the scene tree, containing its name, active state, depth, parent-child links, and expansion state.
- **Transform Descriptor**: The fundamental spatial component of every GameObject, encompassing 2D Position $(x, y)$, Rotation $\theta$, and 2D Scale $(s_x, s_y)$.
- **Component Descriptor**: Modular engine logic or data attached to a GameObject (Animator, Audio, Camera, Collider, Rigidbody, Script, Sprite), equipped with type identification, property address paths, and lifecycle actions.
- **Viewport Tool State**: Active editor manipulation mode in the Scene viewport (Hand/Pan, Move, Rotate, Scale, Rect).

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Hierarchy vertical row count for a typical 20-object scene with 60 components is reduced by at least 65%, enabling developers to see twice as many GameObjects on screen without scrolling.
- **SC-002**: Vertical height consumed by the Transform component in the Inspector is reduced by at least 45% (from 5 stacked lines down to 3 compact rows).
- **SC-003**: 100% of component management actions (Reset, Copy, Paste, Remove) can be completed directly from the Inspector header without navigating to the Hierarchy panel.
- **SC-004**: Framing a selected object takes a single keypress (`F`), matching the muscle memory of 100% of Unity-trained game developers.
- **SC-005**: All existing headless and automated tests in `editor/tests/` continue to pass without regression.

---

## Assumptions

- **Scope Boundary**: This specification addresses Phase 1 (high-impact ergonomics and layout). Subsequent phases will address Project panel context menus, drag-to-scene instantiation, floating overlay toolbars, and Console message badges.
- **Engine Data Contract**: GameObject active state is serialized in scene files; individual component active/enabled flags are not currently part of the core engine JSON schema and are therefore not added to the scene serialization format in this phase.
- **Existing Document Persistence**: No changes to the underlying `.scene` JSON storage format are required; all edits continue to produce lossless round-trippable scene documents.
- **Keyboard Navigation**: Standard keyboard shortcut routing respect text focus checks already present in `editor/src/actions.cpp`.
