# Contract: Viewport Navigation & Shortcuts

**Contract Target**: `editor/src/viewport.h`, `editor/src/viewport.cpp`, `editor/src/actions.h`, `editor/src/actions.cpp`  
**Feature**: `013-editor-unity-ergonomics`  

---

## 1. Action & Shortcut Registry

### 1.1 Action Mapping
In `editor/src/actions.cpp`:
- `EditorAction::FrameSelected`: Shortcut = `"F"`, `sceneOnly = true`.
- `EditorAction::FrameAll`: Shortcut = `"Shift+F"`, `sceneOnly = true`.
- `EditorAction::SelectHandTool`: Shortcut = `"Q"`, `sceneOnly = true`.
- `EditorAction::SelectMoveTool`: Shortcut = `"W"`, `sceneOnly = true`.
- `EditorAction::SelectRotateTool`: Shortcut = `"E"`, `sceneOnly = true`.
- `EditorAction::SelectScaleTool`: Shortcut = `"R"`, `sceneOnly = true`.

### 1.2 Key Dispatching
In `dispatch_editor_key`:
```cpp
case EditorKey::F:
    if (!capture.sceneFocused || control)
        return std::nullopt;
    return shift ? EditorAction::FrameAll : EditorAction::FrameSelected;

case EditorKey::Q:
    if (!capture.sceneFocused || shift || control)
        return std::nullopt;
    return EditorAction::SelectHandTool;
```

---

## 2. Framing Execution Logic

### 2.1 Frame Selected (`F`)
When `request_frame_selected()` is processed:
1. Check if `selection.current().objectId` is non-empty.
2. If an object is selected:
   - Compute bounding box of selected object (via Sprite, Collider, or position point).
   - Frame and center viewport on that bounding box.
3. If **no** object is selected:
   - Fall back to `request_frame_all()`, framing the collective bounds of all scene objects.

### 2.2 Frame All (`Shift+F`)
When `request_frame_all()` is processed:
- Compute collective bounding box of all objects in the scene.
- Frame and center viewport on that bounding box.

---

## 3. Hand / Pan Tool (`Q`)

### 3.1 Tool State
- `SceneTool::Hand` is added to `SceneTool` enum in `editor/src/viewport.h`.
- Selecting the tool sets `tool_ = SceneTool::Hand`.

### 3.2 Viewport Interaction
While `tool_ == SceneTool::Hand`:
- On `ImGui::IsMouseClicked(ImGuiMouseButton_Left)` inside the viewport bounds:
  - Set `panning_ = true`.
  - Record initial pan coordinates.
- While `ImGui::IsMouseDown(ImGuiMouseButton_Left)` and `panning_ == true`:
  - Camera pans smoothly with mouse motion via `transform_.pan(deltaX, deltaY)`.
- On mouse release:
  - Set `panning_ = false`.
- Middle-click and right-click panning remain available in all tool modes.
