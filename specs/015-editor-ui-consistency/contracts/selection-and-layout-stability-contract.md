# Contract: Selection and Layout Stability

**Feature**: `015-editor-ui-consistency`  
**Covers**: `FR-001`, `FR-002`, `FR-003`, `FR-004`, `SC-001`

---

## 1. Scene-to-Inspector and Scene-to-Hierarchy Selection Contract (`FR-001`)

1. **Scene Picking**:
   - When the user clicks a pickable target in `SceneViewport` (`viewport.cpp`), the resulting `Selection` MUST select the owning GameObject (`objectId` and `selectedObjectIds`).
   - `InspectorPanel::draw` MUST render **all** components belonging to the selected `objectId` (subject only to the user's text search query in the Inspector search bar), never hiding sibling components when `selection.current().componentId` is non-empty.
   - `HierarchyPanel::draw` MUST highlight the selected GameObject's row (`ImGuiTreeNodeFlags_Selected`) and allow `F2` inline rename whenever `selection.current().objectId` matches the row's `id` or `selection.is_selected(row.id)` is true, even if `selection.current().componentId` is set.

## 2. Project Browser Stationary Layout Contract (`FR-002`)

1. **Zero Vertical Shift on Asset Selection**:
   - In `ProjectPanel::draw` (`project.cpp`), the top-left Y coordinate of the `"Asset folders"` and `"Asset contents"` child windows MUST remain identical whether:
     - No entry is selected (`selectedEntry_ == std::nullopt`),
     - A directory is selected,
     - A texture, audio, animation, script, or scene asset is selected, or
     - A `.pttemplate` asset is selected.
2. **Relocation of Interactive Controls**:
   - Interactive `Sprite Source Region` dragging and pixel-bound editing for a `Sprite` component MUST be rendered inside the `Sprite` component in `InspectorPanel`, not above the folder/asset split in `ProjectPanel`.
   - Template instantiation and explicit asset assignment remain accessible via double-click, drag-and-drop, right-click context menu in `ProjectPanel`, and `ProjectPanel::semantic_nodes` / `dispatch_semantic_action`.

## 3. Hierarchy Row Icon & Gutter Contract (`FR-003`)

1. **Left Icon Placement**:
   - Every GameObject row in `HierarchyPanel::draw` MUST draw `EditorIcon::GameObject` at `{iconX, iconY}` (immediately to the right of the tree expand arrow and to the left of the object name text).
2. **Right Gutter Stability**:
   - The right edge of a Hierarchy row MUST ONLY draw the visibility (`EyeOpen`/`EyeClosed`) and lock (`LockClosed`/`LockOpen`) icons when the row is hovered or when the object is currently hidden or locked.
   - `EditorIcon::GameObject` MUST NEVER be drawn on the right edge of a Hierarchy row.

## 4. Chrome, Toolbar, and Tab Bar Geometry Contract (`FR-004`)

1. **Main Toolbar Fixed Single-Row Height**:
   - `MainToolbar::draw` (`toolbar.cpp`) MUST maintain a constant single-row height (`40.0f * uiScale`) regardless of window width or status message length.
2. **Scene Viewport Stable Top Boundary**:
   - `SceneViewport::draw` (`viewport.cpp`) MUST NOT conditionally insert or remove text lines between the Scene toolbar and the viewport canvas when `document.sourcePath` changes from `std::nullopt` to a saved path.
3. **Hierarchy Stable Search Bar Position**:
   - `HierarchyPanel::draw` (`hierarchy.cpp`) MUST NOT insert conditional status lines between the header bar and the search bar.
4. **Animation Tab Bar Width Stability**:
   - `Animation Assets` tabs in `editorapp.cpp` MUST reserve fixed width for dirty/diagnostic markers (`" * !"`) and use `"×"` for the close button, matching Scene tabs.
