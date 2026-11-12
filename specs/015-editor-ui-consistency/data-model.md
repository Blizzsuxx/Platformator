# Phase 1 Data Model: Editor UI Consistency and Unity Usability Cleanup

**Feature**: `015-editor-ui-consistency`  
**Date**: 2026-11-12  
**Spec**: [spec.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/spec.md)

---

## 1. Selection & Sub-Target Presentation Model

### `Selection` ([selection.h](file:///home/user/Projects/Platformator/editor/src/selection.h))
- **Fields**:
  - `std::optional<int> objectId`: Primary selected GameObject ID.
  - `std::optional<int> componentId`: Optional focused component ID (for standalone `BehaviorFieldsPanel` or targeted semantic actions).
  - `std::optional<std::size_t> behaviorOrdinal`: Optional focused behavior index within a `ScriptComponent`.
  - `FocusedPanel panel`: Panel that originated or holds logical focus (`Hierarchy`, `Scene`, `Inspector`, `Behaviors`, `Project`).
  - `std::vector<int> selectedObjectIds`: Ordered list of selected GameObject IDs for multi-selection.
- **Updated Presentation Invariants**:
  1. **Hierarchy Row Highlight**: A Hierarchy row for `objectId` is selected whenever `selection.is_selected(row.id)` is true (`selectedObjectIds` contains `row.id` or `selection.objectId == row.id`), **regardless** of whether `selection.componentId` has a value.
  2. **Hierarchy Inline Rename (`F2`)**: `F2` rename in the Hierarchy is eligible whenever a single `objectId` is selected, **regardless** of whether `selection.componentId` has a value.
  3. **Inspector Component Visibility**: When a single `objectId` is selected, `InspectorPanel` enumerates **all** components on that GameObject that match the active Inspector search filter, **regardless** of whether `selection.componentId` is set.
  4. **Inspector Scroll Stability**: `InspectorPanel` only resets vertical scroll (`SetScrollY(0.0f)`) when the set of selected `objectId`s actually changes (`objectSelectionKey` tracks both single and multi-object selection IDs rather than clearing on multi-select).

---

## 2. Hierarchy Row Display & Gutter State

### `HierarchyRow` ([hierarchy.h](file:///home/user/Projects/Platformator/editor/src/panels/hierarchy.h))
- **Fields**:
  - `HierarchyRowKind kind`: `HierarchyRowKind::Object`
  - `int id`: Scene-unique GameObject ID
  - `std::size_t depth`: Tree nesting depth
  - `std::string label`: Identity-qualified label (`"<name> (#<id>)"`) retained for programmatic/test queries and search indexing
  - `bool hasChildren`: Whether the object has child GameObjects
  - `int parentRowIndex`: Index of parent row in `rows_` (`-1` for root)
- **Visual Rendering Rules**:
  - **Left Column**: Expand/collapse arrow + `EditorIcon::GameObject` drawn at `iconX` + clean object name (stripped of trailing ` (#<id>)`).
  - **Right Gutter**: Reserved strictly for visibility (`EditorIcon::EyeOpen` / `EditorIcon::EyeClosed`) and lock (`EditorIcon::LockClosed` / `EditorIcon::LockOpen`) icons when hovered or when the object is hidden/locked. Never draws `EditorIcon::GameObject` on the right edge.

---

## 3. Project Browser View & Inline Rename State

### `ProjectViewState` ([project.h](file:///home/user/Projects/Platformator/editor/src/panels/project.h))
- **Fields**:
  - `float thumbnailScale`: `0.0f` for compact list view, `(0.0f, 1.0f]` for scalable tile grid
  - `float folderTreeWidth`: Width of the left folder tree pane (scaled with `uiScale`)
  - `std::filesystem::path contextTargetPath`: Target path for context menu operations
  - `bool renaming`: True while inline rename is active on a tile or list row
  - `bool renameFocusPending`: True on the first frame inline rename starts so `SetKeyboardFocusHere()` focuses the inline `InputText`
  - `std::filesystem::path renamingPath`: Path of the entry currently being renamed inline
  - `char renameBuffer[256]`: Editable filename buffer
  - `bool confirmDeletePending`: True when interactive deletion confirmation modal is open
  - `std::filesystem::path pendingDeletePath`: Path awaiting user delete confirmation
- **Layout Invariant**:
  - `ProjectPanel::draw` renders only the top bar (`Refresh Assets` + `+` Create menu) and the two-column `Asset folders` / `Asset contents` split. Selecting any asset file causes `0 px` change to the top Y coordinate of `Asset folders` and `Asset contents`.

---

## 4. Inspector Component Header & Property Row Model

### Component Header (`Single` & `Multi-Object`)
- **Structure**:
  - Collapsible header (`ImGui::CollapsingHeader` with `ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap`)
  - Component vector icon (`EditorIcon`) drawn on the header bar
  - Right-aligned `⋮` menu button (`20.0f * uiScale` width) opening the component context popup:
    - Single-object: `Reset`, `Copy Settings`, `Paste Settings`, `Move Up`, `Move Down`, `Remove Component`
    - Multi-object: `Reset Settings for Selection`, `Copy Settings`, `Paste Settings to Selection`

### Compact Sprite Source Region in Inspector
- **Location**: Rendered inside `InspectorPanel`'s `Sprite` component section when `sourceRectEnabled` is true (or under a collapsible `"Source Region Preview"` inside the `Sprite` component), using the assigned `textureFilePath` thumbnail and committing via `commit_sprite_source_region`.

### Numeric Formatting & Scrubbing
- **Display Format**: Clean decimal representation (`%.2f` for Transform vectors/rotation; trimmed float formatting up to 3–4 decimal places for general inspector/behavior numbers when not actively typing, avoiding raw `%.9g`/`%.17g` precision artifacts like `0.300000012`).
- **Label Scrubbing**: Horizontal mouse drag (`ImGuiMouseCursor_ResizeEW`) on any numeric field label in single-object Inspector, multi-object Inspector, and Behavior fields increments/decrements the value and commits a single undo step on release.
