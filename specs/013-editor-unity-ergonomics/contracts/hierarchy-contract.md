# Contract: Hierarchy Panel (GameObject-Only Tree)

**Contract Target**: `editor/src/panels/hierarchy.h`, `editor/src/panels/hierarchy.cpp`  
**Feature**: `013-editor-unity-ergonomics`  

---

## 1. Tree Row Structure

### 1.1 Row Kinds
- `HierarchyPanel::visible_rows(const DocumentSession &document)` MUST return a list containing exclusively `HierarchyRowKind::Object` entries.
- Component groupings (`ComponentsNode`) and individual component entries (`Component`) are completely omitted from the tree traversal.
- The `collapsedComponentGroups_` member is removed or unused.

### 1.2 Selection Behavior
- Clicking a GameObject row invokes:
  ```cpp
  selection.set(document, selected_object(row.id));
  ```
- Because `componentId` is `std::nullopt`, the Inspector automatically displays all components belonging to `row.id`.

### 1.3 Drag-and-Drop Parenting
- Drag Source:
  - Payload type: `PLATFORMATOR_HIERARCHY_OBJECT`
  - Payload data: `int objectId`
- Drop Target:
  - Valid drop targets are strictly other GameObject rows (or the empty window space to unparent to scene root).
  - Component drop targets do not exist.
  - Hovering over a target GameObject displays insertion lines (before/after) or hover highlight (parenting into target).

---

## 2. Empty Space Context Actions

- Right-clicking empty space in the Hierarchy window displays:
  ```cpp
  if (ImGui::BeginPopupContextWindow("Hierarchy empty-space actions", ...))
  {
      if (ImGui::MenuItem("Create Empty Object"))
          create_object(std::nullopt);
      ImGui::EndPopup();
  }
  ```
- Right-clicking a GameObject row continues to display:
  - *Create Child*
  - *Show / Hide in Scene*
  - *Lock / Unlock Scene Picking*
  - *Add Component*
  - *Duplicate (`Ctrl+D`)*
  - *Delete (`Delete`)*
