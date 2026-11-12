# Research: Unity-Inspired Editor Ergonomics (Phase 1)

**Feature**: `013-editor-unity-ergonomics`  
**Date**: 2026-11-12  
**Status**: Completed  

---

## Decision 1: Pure GameObject Tree in Hierarchy

### Decision
Modify `HierarchyPanel::rebuild()` to emit exclusively `HierarchyRowKind::Object` rows. Remove the intermediate `HierarchyRowKind::ComponentsNode` and `HierarchyRowKind::Component` row generation, as well as `collapsedComponentGroups_`. Selecting an object continues to populate the selection model with `objectId`, leaving `componentId` unset so the Inspector displays all components attached to that GameObject.

### Rationale
- **Unity Parity**: In Unity, the Hierarchy window is strictly a scene graph of GameObjects and their parent-child relationships. Components only ever appear in the Inspector.
- **Visual Clarity**: Eliminating component rows reduces row clutter by >65% in typical scenes, allowing developers to see significantly more of their scene structure without scrolling.
- **Drag-and-Drop Safety**: Dragging a GameObject to re-parent or re-order no longer suffers from ambiguous drop targets (e.g. accidentally dropping onto a component row).
- **Simplicity (KISS)**: Simplifies `hierarchy.cpp` by removing row-type branching, component group collapsing state, and redundant connector line drawing passes.

### Alternatives Considered
- *Toggleable component display in View menu*: Rejected during specification clarification (Option A chosen). Retaining legacy component rows would preserve dead code, require new preference serialization, and maintain visual confusion.
- *Inline component badges on GameObject rows*: Showing small component icons on the right edge of the GameObject row. Kept as a potential future polish item, but omitted from Phase 1 to keep row rendering minimal and clean.

---

## Decision 2: Compact Multi-Column Transform Layout

### Decision
Replace the vertical stack of 8 separate property rows (Name, Tag, Active, Position X, Position Y, Scale X, Scale Y, Rotation) with a standardized Unity layout:
1. **Header Row**: Active checkbox + GameObject icon + Name text box.
2. **Sub-header Row**: Tag label + Tag text box.
3. **Dedicated Transform Section**:
   - `Position`: Label on left, followed by horizontal columns for `X` and `Y` with subtle colored axis badges (Red for `X`, Green for `Y`).
   - `Rotation`: Label on left, followed by single float input displaying degrees.
   - `Scale`: Label on left, followed by horizontal columns for `X` and `Y` (Red for `X`, Green for `Y`).

### Rationale
- **Vertical Density**: Reduces the height consumed by Transform from 5 lines down to 3 compact rows, saving ~45% vertical space in the Inspector.
- **Muscle Memory**: Matches the exact visual representation Unity developers expect.
- **Precision**: Uses standard floating-point inputs with 2 decimal places ("%.2f" format), preserving exact serialized float values under the hood.

### Alternatives Considered
- *Single-line Transform table with XYZ columns*: Unity uses 3D transforms with X, Y, Z. Since Platformator is a 2D engine, a 2-column layout (X, Y) matches 2D dimensions naturally without wasting space on unused Z coordinates.
- *Accordion / dropdown for Position*: Hiding X and Y under a foldout. Rejected because Position is the single most frequently edited property in game development and must always remain immediately visible and editable.

---

## Decision 3: Component Header Context Menu (`⋮`) & Body Cleanup

### Decision
Add a trailing `⋮` button on every component collapsible header in `InspectorPanel::draw` and attach an `ImGui::BeginPopupContextItem` to the header. Move the *Reset Settings*, *Copy Settings*, and *Paste Settings* logic into this menu, and add *Remove Component* directly to this menu. Delete the three in-body text buttons (`[Copy Settings] [Paste Settings] [Reset Settings]`).

### Rationale
- **Vertical Screen Space**: Removing three wide in-body buttons saves 28-36 pixels of height per component. On an object with 4 components, this saves over 120 pixels of vertical space.
- **Workflow Completeness**: Currently, removing a component can only be done by right-clicking the component in the Hierarchy window. Since components will no longer appear in the Hierarchy, the Inspector header menu becomes the natural, canonical place for component removal.
- **Dual Invocation**: Supports both left-clicking the `⋮` icon and right-clicking anywhere on the component header, accommodating both visual discoverability and rapid muscle memory.

### Alternatives Considered
- *Inline "X" button on the header*: An explicit delete button on the header bar. Rejected because accidental clicks could inadvertently delete components, whereas a context menu item with confirmation or standard undo is safer and matches Unity's gear/ellipsis menu.
- *Floating toolbar above component*: Hovering over a component reveals a floating action strip. Rejected as unidiomatic for Dear ImGui and prone to visual glitching.

---

## Decision 4: Numeric Label Scrubbing with Standard Modifiers

### Decision
Implement horizontal dragging on numeric property labels (such as "X", "Y", "Rotation", "Scale X", "Scale Y"). Calculate horizontal mouse delta when `ImGui::IsItemActive()` is true on the label. Support modifier keys:
- Unmodified: Base step (0.1 for position, 1.0 for rotation, 0.05 for scale).
- `Shift`: 10× speed for coarse adjustments.
- `Alt`: 0.1× speed for fine sub-pixel precision.
- `Ctrl`: Snap to nearest configured increment (from `SnapPreferences`).
Group the continuous drag into a single atomic undo command upon mouse button release.

### Rationale
- **Speed & Tactile Feel**: Allows rapid tuning of values without clicking to select text, backspacing, typing, and pressing Enter.
- **Unity Standardization**: Unity's virtual sliders work identically with `Shift` (fast) and `Alt` (slow).
- **History Protection**: Recording intermediate values on every frame would flood the undo history. Committing only on mouse release ensures `Ctrl+Z` cleanly reverses the entire adjustment in one step.

### Alternatives Considered
- *Dear ImGui DragFloat widgets directly*: Using `ImGui::DragFloat` directly instead of custom label scrubbing. While `DragFloat` supports dragging, it embeds the drag handle in the input box itself, which conflicts with custom axis prefix labels (`X`, `Y`) and full-width alignment. Custom label scrubbing gives complete control over axis styling and modifier speeds.

---

## Decision 5: Viewport Shortcuts & Hand/Pan Tool (`Q`, `F`)

### Decision
1. **Shortcut Remap**:
   - `F` → `EditorAction::FrameSelected` (fallback to `FrameAll` if no selection exists).
   - `Shift+F` → `EditorAction::FrameAll`.
2. **Hand/Pan Tool (`Q`)**:
   - Add `SceneTool::Hand` to `SceneTool` enum in `viewport.h`.
   - Add `EditorAction::SelectHandTool` and `EditorKey::Q` to `actions.h` / `actions.cpp`.
   - When `tool_ == SceneTool::Hand`, left-mouse button down and drag initiates panning identically to middle/right-mouse button panning.
   - Text input capture checks in `actions.cpp` ensure typing `q` or `f` in text fields never triggers tools or camera framing.

### Rationale
- **Eliminates Muscle Memory Traps**: Inverting `F` and `Shift+F` is one of the most frustrating friction points for developers transitioning from Unity.
- **Accessibility & Trackpad Usability**: Middle-click drag is difficult or impossible on many laptop trackpads. `Q` enables one-handed left-click dragging to pan anywhere in the scene.

### Alternatives Considered
- *Spacebar temporary pan*: Holding `Space` temporarily switches to Hand tool. Spacebar panning is common in Photoshop and Unity; it can be added additively alongside `Q`, but `Q` is the formal persistent tool mode in Unity's QWERT layout.
