# Contract: Cross-Panel Focus, Scene Navigation, Marquee & Overlay Toolbar

**Feature**: `014-editor-audit-remediation`  
**Covers**: `FR-006`, `FR-007`, `FR-008`, `FR-009`, `FR-020`, `FR-021`, `FR-022`, `SC-004`

---

## 1. Cross-Panel Focus & Mouse Panning (`viewport.cpp`)

1. Pressing Middle Mouse Button, Right Mouse Button, or Left Mouse Button (when `tool_ == SceneTool::Hand`) while hovering the Scene canvas MUST:
   - Immediately initiate camera panning (`panning_ = true`) on that same frame.
   - Focus the Scene window (`ImGui::SetWindowFocus()`) and set `selection.focusedPanel = FocusedPanel::Scene`.
   - Maintain `panning_ = true` until the initiating mouse button is released (even if `focused` was initially `false` at the start of `SceneViewport::draw` or if the cursor leaves the canvas rect during the drag).
2. No debug `printf` statements may execute during `handle_canvas_navigation`.

## 2. Keyboard Shortcut Focus Routing (`actions.cpp`, `editorapp.cpp`, `viewport.cpp`)

1. When `!capture.wantTextInput && !capture.anyPopupOpen && !capture.embeddedGameFocused` and `(capture.sceneFocused || capture.hierarchyFocused)`:
   - `Q` activates `SceneTool::Hand`
   - `W` activates `SceneTool::Move`
   - `E` activates `SceneTool::Rotate`
   - `R` activates `SceneTool::Scale`
   - `F` executes `FrameSelected` (falling back to `FrameAll` when selection is empty)
   - `Shift+F` executes `FrameAll`
2. Double-clicking any GameObject row in `HierarchyPanel` (`hierarchy.cpp`) MUST select that GameObject and trigger `FrameSelected` in `SceneViewport`.

## 3. Marquee Box-Selection & Compact Scene Toolbar (`viewport.cpp`)

1. Left-clicking and dragging on empty canvas space when `tool_ != SceneTool::Hand` MUST draw a translucent marquee rectangle and, on mouse release, select all visible unlocked GameObjects whose world bounds intersect the rectangle (`Shift`/`Ctrl` adds to current selection).
2. The Scene viewport toolbar MUST render tools (`Hand`, `Move`, `Rotate`, `Scale`, `Rect`) and view controls (`Grid`, `Snap`, `Step`, `Zoom`, `Frame`) in a compact single-row bar/overlay without multi-line text wrapping.
3. Dropping a `PLATFORMATOR_ASSET_PATH` payload onto the Scene canvas MUST instantiate the dropped sprite or `.pttemplate` at the world position under the cursor as a single undoable command.
