# Contract: Inspector Panel (Compact Transform & Header Action Menus)

**Contract Target**: `editor/src/panels/inspector.h`, `editor/src/panels/inspector.cpp`  
**Feature**: `013-editor-unity-ergonomics`  

---

## 1. Top GameObject Header Layout

The top section of the Inspector MUST be laid out as follows:

```text
Row 1: [☑ Active] [Icon] [Name InputText (fills available width)]
Row 2: Tag: [Tag InputText]
```

- **Active Checkbox**: Direct binding to `{ "active" }` (boolean).
- **Name Field**: Direct binding to `{ "name" }` (string).
- **Tag Field**: Direct binding to `{ "tag" }` (string).

---

## 2. Compact Transform Section

Transform properties MUST be grouped into a dedicated `Transform` section:

### 2.1 Position Row
- Label: `"Position"` (width ~ 60-80 px)
- Sub-columns:
  - `"X"` axis label (styled with red accent) + float input
  - `"Y"` axis label (styled with green accent) + float input
- Both fields share remaining width equally on the same line (`ImGui::SameLine()`).

### 2.2 Rotation Row
- Label: `"Rotation"` (width ~ 60-80 px)
- Float input displaying degrees, formatted with `"%.2f deg"`.

### 2.3 Scale Row
- Label: `"Scale"` (width ~ 60-80 px)
- Sub-columns:
  - `"X"` axis label (styled with red accent) + float input ($\ge 0.0001$)
  - `"Y"` axis label (styled with green accent) + float input ($\ge 0.0001$)
- Both fields share remaining width equally on the same line (`ImGui::SameLine()`).

---

## 3. Component Header Action Menu

### 3.1 Header Context Menu Trigger
- Each component collapsible header MUST render a trailing action button (`⋮` or ellipsis).
- The action menu MUST open when:
  1. The user left-clicks the `⋮` button.
  2. The user right-clicks anywhere on the component header rect (`ImGui::BeginPopupContextItem()`).

### 3.2 Action Menu Contents
The popup menu contains:
- **Reset**: Resets component fields to default values via `reset_component_settings()`.
- **Copy Settings**: Serializes component to clipboard buffer via `copy_component_settings()`.
- **Paste Settings**: Enabled only if clipboard matches current component type; applies via `paste_component_settings()`.
- **Remove Component**: Invokes `commands.remove_component(document, objectId, componentId)`.

### 3.3 Body Button Removal
The buttons `[Copy Settings]`, `[Paste Settings]`, and `[Reset Settings]` MUST be completely removed from the component body to conserve vertical space.

---

## 4. Numeric Label Drag Scrubbing

### 4.1 Activation
- When hovering over an axis label (e.g. `"X"`, `"Y"`), the cursor changes to `ImGuiMouseCursor_ResizeEW`.
- While left-mouse button is held down over the label, scrubbing is active.

### 4.2 Modifier Keys
- **Normal**: Mouse Delta $\times$ 0.1
- **Shift**: Mouse Delta $\times$ 1.0 (10× speed)
- **Alt**: Mouse Delta $\times$ 0.01 (0.1× precision)
- **Ctrl**: Snap to nearest step increment (`snapPreferences.position` / `rotationDegrees` / `scale`).

### 4.3 Undo Consolidation
- Scrubbing during mouse motion updates local preview coordinates immediately.
- On mouse button release, a single consolidated command is dispatched to `EditCommandService`.
