# Data Model: Unity-Inspired Editor Ergonomics (Phase 1)

**Feature**: `013-editor-unity-ergonomics`  
**Date**: 2026-11-12  
**Status**: Completed  

---

## 1. Entities & Data Structures

### 1.1 Hierarchy Row (`HierarchyRow`)

Represents a single entry in the Hierarchy tree. In Phase 1, `HierarchyRowKind` is simplified to remove component rows from the visible tree.

```cpp
enum class HierarchyRowKind
{
    Object // Only GameObject rows are emitted in the tree
};

struct HierarchyRow
{
    HierarchyRowKind kind = HierarchyRowKind::Object;
    int id = 0;                     // Unique GameObject ID
    std::size_t depth = 0;          // Tree depth for indentation
    std::string label;              // Display label (GameObject name)
    bool hasChildren = false;       // Whether this object has child GameObjects
    int parentRowIndex = -1;        // Index of parent GameObject in visible row list
};
```

#### Validation Rules & Invariants
- `id` must correspond to a valid GameObject in `document.indexes.objectsById`.
- Depth must be $\ge 0$.
- Component IDs are no longer stored in `HierarchyRow`.

---

### 1.2 Transform Properties (`TransformDescriptor`)

The fundamental spatial descriptor of every GameObject, extracted from the serialized JSON GameObject dictionary.

```text
{
  "id": <int>,
  "name": <string>,
  "tag": <string>,
  "active": <bool>,
  "position": { "x": <float>, "y": <float> },
  "rotation": <float>,   // Serialized in degrees
  "scale": { "x": <float>, "y": <float> }
}
```

#### Property Addresses
- `Position X`: `{ "position", "x" }` (Type: Float)
- `Position Y`: `{ "position", "y" }` (Type: Float)
- `Rotation`: `{ "rotation" }` (Type: Float, displayed in degrees)
- `Scale X`: `{ "scale", "x" }` (Type: Float, minimum $\gt 0.0$)
- `Scale Y`: `{ "scale", "y" }` (Type: Float, minimum $\gt 0.0$)

#### Multi-Selection Multi-Value State
When $N > 1$ GameObjects are selected:
- If all $N$ objects share the same value $V$, the input displays $V$.
- If values differ, the input displays a null/mixed placeholder: `" — "`.
- Committing a value $V_{new}$ creates a `BatchCommand` assigning $V_{new}$ to all $N$ objects.

---

### 1.3 Component Action Context (`ComponentActionContext`)

Represents the state required to render and dispatch component header actions.

```cpp
struct ComponentActionContext
{
    int objectId;                   // Owner GameObject ID
    int componentId;                // Target component ID
    int componentType;              // Component type enum (0..6)
    bool canReset = true;
    bool canCopy = true;
    bool canPaste = false;          // True if clipboard matches componentType
    bool canRemove = true;
};
```

#### Component Action State Transitions
- **Copy**: Serializes current component JSON into `componentSettingsClipboard_`.
- **Paste**: Validates `componentSettingsClipboard_->type == componentType`. Applies settings via `paste_component_settings()`.
- **Reset**: Replaces component JSON with `default_component_record(type)` via `reset_component_settings()`.
- **Remove**: Invokes `commands.remove_component(document, objectId, componentId)`, repointing selection and emitting an undoable command.

---

### 1.4 Viewport Tool State (`SceneTool`)

Expanded tool enum encompassing standard QWERT tools.

```cpp
enum class SceneTool
{
    Hand,       // Q: Camera pan
    Move,       // W: Translation gizmo
    Rotate,     // E: Rotation ring gizmo
    Scale,      // R: Scaling handles gizmo
    Rectangle   // Sizing handles for Sprite/Collider bounds
};
```

#### Tool Activation Transitions
- Pressing `Q` (when viewport focused, no text input active) $\rightarrow$ `SceneTool::Hand`.
- Pressing `W` $\rightarrow$ `SceneTool::Move`.
- Pressing `E` $\rightarrow$ `SceneTool::Rotate`.
- Pressing `R` $\rightarrow$ `SceneTool::Scale`.
- In `SceneTool::Hand` mode:
  - `MouseDown(Left)`: Records `panStart_` and sets `panning_ = true`.
  - `MouseMove`: Computes screen delta, invokes `transform_.pan(deltaX, deltaY)`.
  - `MouseUp(Left)`: Clears `panning_ = false`.

---

### 1.5 Numeric Scrubbing Gesture State (`NumericScrubState`)

Tracks interactive dragging over numeric property labels.

```cpp
struct NumericScrubState
{
    bool active = false;
    float startValue = 0.0f;
    float accumulator = 0.0f;
    PropertyAddress address;
    int objectId = 0;
    std::optional<int> componentId;
    std::vector<int> targetObjectIds; // For multi-selection
};
```

#### Scrubbing Calculation
$$\Delta = \text{mouseDeltaX} \times \text{baseStep} \times \text{multiplier}$$
where:
$$\text{multiplier} = \begin{cases} 10.0 & \text{if Shift held} \\ 0.1 & \text{if Alt held} \\ 1.0 & \text{otherwise} \end{cases}$$
If `Ctrl` is held, the scrubbed value snaps to the nearest step increment:
$$V_{\text{snapped}} = \text{round}\left(\frac{V}{\text{step}}\right) \times \text{step}$$
On mouse button release, a single consolidated command is dispatched to `EditCommandService`.
