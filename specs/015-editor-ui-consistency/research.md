# Phase 0 Research: Editor UI Consistency and Unity Usability Cleanup

**Feature**: `015-editor-ui-consistency`  
**Date**: 2026-11-12  
**Spec**: [spec.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/spec.md)

---

## 1. Unified Scene-to-Inspector & Scene-to-Hierarchy Selection

- **Decision**: Keep `Selection::componentId` as an optional sub-target hint (used by `BehaviorFieldsPanel` or component-level semantic actions), but remove the filter in `InspectorPanel::draw` ([inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L2400-L2406)) that restricts `visibleComponentIds` to `*selection.current().componentId`, and remove the `!currentSelection.componentId` guard in `HierarchyPanel::draw` ([hierarchy.cpp](file:///home/user/Projects/Platformator/editor/src/panels/hierarchy.cpp#L829) and [L1152](file:///home/user/Projects/Platformator/editor/src/panels/hierarchy.cpp#L1152)). Additionally, in `SceneViewport` picking ([viewport.cpp](file:///home/user/Projects/Platformator/editor/src/viewport.cpp#L2378-L2400)), selecting a GameObject in the Scene view sets `Selection::objectId` (and `selectedObjectIds`) without hiding sibling components.
- **Rationale**: In Unity, clicking a GameObject's sprite or collider in the Scene view selects the GameObject: its row highlights in the Hierarchy, `F2` rename works in the Hierarchy, and the Inspector shows the GameObject's Transform and **all** of its components. Filtering `visibleComponentIds` to a single component when `componentId` was set caused other components on the GameObject to disappear whenever the user clicked the object in the Scene view.
- **Alternatives Considered**:
  - *Never set `componentId` anywhere*: Rejected because `BehaviorFieldsPanel` and specific unit tests (`selection_test`, `inspector_interaction_test`) use `Selection{objectId, componentId, behaviorOrdinal}` to address a specific Script component in the standalone Behavior Fields panel. Allowing `InspectorPanel` to always show all components of `objectId` (while still auto-selecting `componentId` for `BehaviorFieldsPanel` when needed) satisfies both Unity UX and existing tests.

---

## 2. Eliminating the Project Panel Vertical Layout Jump on Asset Selection

- **Decision**: Remove `draw_assignment_controls(document, commands, selection)` from above the two-column folder/asset split in `ProjectPanel::draw` ([project.cpp](file:///home/user/Projects/Platformator/editor/src/panels/project.cpp#L3212)). Keep `asset_assignment_targets`, `assign_asset_target`, `load_selected_template`, and `semantic_nodes` / `dispatch_semantic_action` intact for programmatic/accessibility actions and context-menu operations (*Instantiate Template* / *Assign to Selected*), and move interactive `Sprite Source Region` editing into the `Sprite` component section of `InspectorPanel` ([inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L2057-L2091)).
- **Rationale**: Rendering `draw_assignment_controls` and `draw_sprite_region_editor` above the `Asset folders` and `Asset contents` child windows caused the entire Project panel to jump downward by 80–300 px the moment a user clicked any file tile, moving the tile out from under the cursor and breaking double-click.
- **Alternatives Considered**:
  - *Render assignment controls at the very bottom of the Project panel*: Still resizes `Asset folders` and `Asset contents` height dynamically when selecting a texture vs. a folder. Moving Sprite region editing into the Inspector's Sprite component (where `sourceRectEnabled` and `sourceRect` already live) and keeping template instantiation in the context menu / double-click / semantic actions keeps the Project panel 100% stationary (`0 px` shift).

---

## 3. Hierarchy Row Left-Column Icon vs. Right-Gutter Hover Controls

- **Decision**: In `HierarchyPanel::draw` ([hierarchy.cpp](file:///home/user/Projects/Platformator/editor/src/panels/hierarchy.cpp#L1176-L1250)), always draw `EditorIcon::GameObject` at `{iconX, iconY}` on the left side of the row label (where `iconX` is already computed and reserved by `textX = iconX + iconSize + 5.0f * uiScale`), and remove the `else if (!rowHovered && !isVisible && !isLocked)` branch that drew `EditorIcon::GameObject` on the far right of the row.
- **Rationale**: Previously, `iconX` was calculated on the left and left blank, while the GameObject cube was drawn on the right edge and replaced by Eye/Lock buttons on hover—making the icon appear to jump from right to disappeared on hover. Drawing the cube at `iconX` on the left and reserving the right gutter strictly for Eye/Lock state & hover toggles matches Unity's Hierarchy.
- **Alternatives Considered**:
  - *Hide Eye/Lock gutter entirely*: Rejected because visibility/lock toggles are a core Unity scene authoring feature introduced in `013`/`014`.

---

## 4. Clean Display Names Without Breaking Identity-Qualified Test Contracts

- **Decision**:
  - Add a clean `display_name` (or strip the trailing ` (#id)` suffix when rendering visible canvas text in `HierarchyPanel::draw` and `draw_scene_reference_field` combo preview), while preserving `HierarchyRow::label` (`name + " (#" + id + ")"`) and `SceneReferenceInfo::label` for programmatic queries (`visible_rows()`, `inspect_scene_reference()`, and search matching).
  - Show the full disambiguated label (`Path / Name (#id)`) inside open reference combo dropdowns when multiple objects share a name or as a hover tooltip.
- **Rationale**: Unit tests in `workspace_lifecycle_test.cpp`, `discovery_test.cpp`, `document_test.cpp`, and `project_test.cpp` assert exact equality on `HierarchyRow::label` (e.g., `"Camera (#0)"`) and `inspect_scene_reference().label`. Separating the visual canvas text (`"Camera"`) from the programmatic/search label (`"Camera (#0)"`) delivers clean Unity-style UI with zero test regressions.
- **Alternatives Considered**:
  - *Modify `object_hierarchy_label` to omit `(#id)` globally*: Would break `workspace_lifecycle_test.cpp`, `discovery_test.cpp`, and `document_test.cpp` which explicitly test `(#id)` in data-layer labels.

---

## 5. Eliminating Duplicate Buttons, Menu Items, and Doubled Feedback

- **Decision**:
  1. **Hierarchy Header ([hierarchy.cpp](file:///home/user/Projects/Platformator/editor/src/panels/hierarchy.cpp#L976-L983))**: Keep a single `+` button (`icon_button(EditorIcon::Plus, "##hierarchy-create-button", ...)`), removing the redundant adjacent `ImGui::Button("+ Object")`.
  2. **Scene Viewport Toolbar ([viewport.cpp](file:///home/user/Projects/Platformator/editor/src/viewport.cpp#L1213-L1254) & [L1489-L1500](file:///home/user/Projects/Platformator/editor/src/viewport.cpp#L1489-L1500))**: Merge `[Snap]` and `[Step: N]` into a single compact snap dropdown button (`Snap: 1` or `Snap` toggle + `▾` settings) so two adjacent buttons don't open the identical `##scene-snap-settings` popup; remove the duplicate bottom-left `"Zoom 100%"` canvas overlay badge since the toolbar already displays the interactive `[100%]` zoom reset button.
  3. **Inspector Doubled Feedback ([editorapp.cpp](file:///home/user/Projects/Platformator/editor/src/editorapp.cpp#L5998-L5999) & [inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L2803-L2804))**: Render `feedback_` once inside `InspectorPanel::draw` (styled with warning/accent color) and remove the duplicate `inspectorPanel.feedback()` rendering in `editorapp.cpp`.
  4. **Main Menu Bar ([editorapp.cpp](file:///home/user/Projects/Platformator/editor/src/editorapp.cpp#L3789-L4048))**: Remove `Editor Preferences...` from `Help` (keeping `Edit -> Preferences...`), and remove `Build`, `Build and Run`, `Stop`, `Zoom In`, `Zoom Out`, `Frame Selected`, and `Frame All` from `Window` (keeping `Window` strictly for panels and workspace layouts).
- **Rationale**: Directly eliminates every duplicate button, popup trigger, menu item, and doubled error string identified in the audit.

---

## 6. Inspector Layout, Float Formatting, Multi-Object Headers, and UI Scaling

- **Decision**:
  1. **Transform Position in Inspector**: Move the `Transform` section in `InspectorPanel::draw` ([inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L2359-L2398)) immediately below the GameObject identity header (`Active`, `Name`, `Tag`, `Layer`). Move `Object Template` below `Transform` (or into a compact single-line bar) and remove the erroneous `"Drop a behavior header..."` tooltip from the `Transform` header.
  2. **Consistent Float Formatting**: Change `draw_number` ([inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L1323)), `draw_multi_object_fields` ([inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L3087)), and `BehaviorFieldsPanel` float/double inputs ([behaviorfields.cpp](file:///home/user/Projects/Platformator/editor/src/panels/behaviorfields.cpp#L318-L526)) from `"%.9g"` / `"%.17g"` to a clean human-readable format helper (formatting finite values without trailing float noise, e.g., `"%.3f"` with trailing zeros trimmed or `"%.2f"` when scrubbing) while preserving exact typed draft strings in `AddressNumericDraft` when keyboard editing.
  3. **Multi-Object Component Headers**: Replace `ImGui::Text("%s (Component %zu)")` + raw inline `SmallButton("Copy Settings")` / `SmallButton("Paste Settings to Selection")` / `SmallButton("Reset Settings for Selection")` in `InspectorPanel::draw_multi_object_fields` ([inspector.cpp](file:///home/user/Projects/Platformator/editor/src/panels/inspector.cpp#L2979-L3026)) with the same `ImGui::CollapsingHeader` + right-aligned `⋮` popup menu pattern used in single-object mode.
  4. **Compact Asset Reference Fields**: Remove the collapsible `"Edit asset path"` tree node under every asset reference field in `draw_asset_reference_field` ([referencefields.cpp](file:///home/user/Projects/Platformator/editor/src/panels/referencefields.cpp#L707-L738)); instead, add `Copy Path` and `Edit Path...` to a right-click context menu (`BeginPopupContextItem`) on the asset reference button itself.
  5. **Proportional `uiScale`**: Multiply all hardcoded pixel widths in `InspectorPanel` (`labelWidth`, `axisTagWidth`, `iconSize`, `buttonWidth`), `BehaviorFieldsPanel` (`begin_field` label offset `124.0f`), `ReferenceFields` (`124.0f`, `clearWidth = 46.0f`), and `SceneViewport` (`rightControlsWidth`) by `uiScale`.
- **Rationale**: Ensures visual and interactive parity across single-object, multi-object, and behavior inspectors at any DPI/UI scale from 100% to 200%.

---

## 7. Inline Asset Rename, Delete Confirmation, Search Clear Buttons, and Toolbar/Game/Console Polish

- **Decision**:
  1. **Project Inline Rename & Delete Confirmation**: Replace `"Rename Asset##modal"` in `ProjectPanel::draw_contents` ([project.cpp](file:///home/user/Projects/Platformator/editor/src/panels/project.cpp#L2489-L2515)) with inline `ImGui::InputText` directly inside `draw_asset_tile` / `draw_asset_list_row` when `viewState_.renaming && viewState_.renamingPath == path`, matching `HierarchyPanel`'s inline `F2` rename. Add a confirmation modal before executing interactive Delete in the Project panel (`Delete` key / context menu) while keeping direct `execute_context_delete()` callable by unit tests.
  2. **Search Clear (`×`) Affordance**: Standardize search bars in `HierarchyPanel`, `ProjectPanel`, `InspectorPanel`, and `ConsolePanel` so all four include a right-aligned clear (`×`) button when the search query is non-empty.
  3. **Fixed Single-Row Main Toolbar, Clean Game View, and Vector Console Icons**:
     - Keep `MainToolbar` ([toolbar.cpp](file:///home/user/Projects/Platformator/editor/src/toolbar.cpp)) at a fixed single-row height (`40.0f * uiScale`), removing the redundant `"Project / Scene"` and `"Saved | Complete"` right-hand text that caused 2–3 row wrapping.
     - Consolidate `GamePanel::draw` ([game.cpp](file:///home/user/Projects/Platformator/editor/src/panels/game.cpp#L390-L620)) into a single compact top toolbar row (`Pause`, `Resume`, `Step`, `Stop`, `Aspect`, `Scale`, and a collapsible/popover `Runtime Objects` inspector with formatted key-value fields instead of raw `component.dump()` JSON strings).
     - Draw vector severity icons (`EditorIcon::Info`, `EditorIcon::Warning`, `EditorIcon::Error`) in `ConsolePanel::draw` ([console.cpp](file:///home/user/Projects/Platformator/editor/src/panels/console.cpp#L219-L305)) alongside clean row labels.
- **Rationale**: Eliminates modal interruptions, accidental unconfirmed file deletion, toolbar height jumping, and raw JSON/ASCII clutter.
