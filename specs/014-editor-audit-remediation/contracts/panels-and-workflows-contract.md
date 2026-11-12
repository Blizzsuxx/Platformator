# Contract: Inspector, Project Browser, Hierarchy, Console, Menus & Theme

**Feature**: `014-editor-audit-remediation`  
**Covers**: `FR-015`–`FR-019`, `FR-023`–`FR-029`, `SC-006`, `SC-007`, `SC-008`

---

## 1. Inspector Component Headers, Vector2 Rows & Label Scrubbing (`inspector.cpp`)

1. **Top Header Order**: The GameObject identity header (`[☑ Active] [Icon] [Name]` and `Tag`) is drawn at the very top of the Inspector above `Object Template` and `Transform`.
2. **Component Header & Menu Button**:
   - `ImGui::CollapsingHeader` uses `ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap`.
   - The trailing menu button is placed on the same header line via `ImGui::SameLine` and draws three vertical dots procedurally (`ImDrawList::AddCircleFilled`) instead of UTF-8 `"⋮"`.
   - Left-clicking the menu button or right-clicking the header opens `ComponentHeaderContextMenu` (*Reset*, *Copy Settings*, *Paste Settings*, *Remove Component*) **without** toggling the `CollapsingHeader` open/closed state, and collapsed headers preserve standard vertical item spacing.
   - Redundant `ImGui::SeparatorText("<ComponentName>")` calls inside component bodies are removed.
3. **Compact 2D Vector Rows**:
   - Transform `Position`, Transform `Scale`, Collider `Offset`, Rigidbody `Velocity`, and Rigidbody `Force` all render using `draw_compact_vector2` (single-row horizontal `X` and `Y` columns).
4. **Universal Numeric Label Scrubbing**:
   - Horizontal dragging on numeric property labels in the Inspector scrubs the field value (`Shift` = 10×, `Alt` = 0.1×, `Ctrl` = snap increment) and commits a single undoable command on release.

## 2. Project Browser & Hierarchy Workflows (`project.cpp`, `hierarchy.cpp`)

1. **Project Context Menus**: Right-clicking folders, assets, or empty space in `ProjectPanel` opens a context menu with *Create* (*Folder*, *Behavior Script*, *Scene*, *Object Template*), *Open*, *Reveal in File Manager*, *Rename* (`F2`), *Duplicate* (`Ctrl+D`), and *Delete* (`Delete`).
2. **Project Two-Column Navigation**: `ProjectPanel` renders a left folder tree pane, top clickable path breadcrumbs, and a bottom thumbnail scale slider switching between compact list rows and grid tiles.
3. **Hierarchy `+` Menu, Gutter Icons & Asset Drops**:
   - `HierarchyPanel` includes a `+` preset dropdown (*Create Empty*, *Create Empty Child*, *2D Sprite*, *Camera*, *Audio Object*).
   - Each GameObject row renders clickable right-edge visibility and pick-lock icon toggles.
   - Dropping `PLATFORMATOR_ASSET_PATH` onto the Hierarchy instantiates a configured GameObject (or attaches a dropped behavior script to a target GameObject).

## 3. Console, Menu Bar & Theme (`console.cpp`, `editorapp.cpp`, `style.cpp`)

1. **Console Severity Badges**: `ConsolePanel` renders three independent right-aligned toggle buttons showing live counts for `Info`, `Warnings`, and `Errors`, a lower detail pane for the selected log row, and double-click navigation to source files.
2. **Menu Bar**: Top-level menus follow `File | Edit | Assets | GameObject | Component | Window | Help`.
3. **Theme & Typography**: `style.cpp` applies a neutral dark charcoal palette (`WindowBg = #282828`, `FrameBorderSize = 1.0f`) and loads a scalable TrueType proportional UI font at native DPI resolution when available.
