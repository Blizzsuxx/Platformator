# Quickstart: Complete Editor Audit Remediation Validation

**Feature**: `014-editor-audit-remediation`  
**Date**: 2026-11-12  
**Status**: Ready for Implementation  

---

## Prerequisites

1. Linux build environment with CMake 3.28+, Ninja, and C++20 compiler.
2. Build directory under `bin/editor-debug` (kept outside report/spec directories per `AGENTS.md`).

---

## Build & Automated Test Commands

From the repository root:

```sh
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4
ctest --preset editor-debug -R "editor_" --output-on-failure
```

---

## Compact Performance & Idle Wait Verification (`AGENTS.md` Compliant)

Run a small 15-frame headless probe against `examples/mario/level1.scene`:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  bin/editor-debug/platformator_editor_gui_probe \
  --frames 15 --warmup-frames 2 \
  --project-root /home/user/Projects/Platformator \
  --scene /home/user/Projects/Platformator/examples/mario/level1.scene
```

**Expected Outcome**:
- `GUI_ACTIVE_FRAME_US` median is $< 16,600\text{ \mu s}$ (down from ~60,000–67,000 µs).
- Idle wait duration (`GUI_WAIT_INCLUSIVE_US - GUI_ACTIVE_FRAME_US`) when no immediate work is scheduled is $> 50,000\text{ \mu s}$ (up from ~15–35 µs).
- `inotify` watcher stays within `MaxWatchedDirectories` with zero `ENOSPC` fallback full-repo scans.

---

## Interactive Workflow Validation Scenarios

### 1. Hierarchy $\rightarrow$ Scene Focus & Navigation (US2)
1. Launch the editor on `examples/mario/level1.scene`.
2. Click any GameObject in the **Hierarchy** panel.
3. Without clicking the Scene view first:
   - Press `F`: verify the Scene camera immediately frames the selected GameObject.
   - Press `Q`, `W`, `E`, `R`: verify the Scene tool switches immediately.
   - Move the mouse over the Scene canvas and drag with the **Middle** or **Right** mouse button: verify the Scene camera pans on the very first drag gesture.
4. Double-click another GameObject in the **Hierarchy** panel: verify the Scene camera frames that object.

### 2. Embedded Game Play Mode & Input Forwarding (US3)
1. Ensure **Embed Game view in editor** is enabled in Run Settings.
2. Click the centered **Play (`▶`)** button in the top toolbar.
3. Verify the editor toolbar/chrome displays the Play Mode visual tint and the embedded Game view updates smoothly at up to 60 FPS.
4. Click inside the **Game** view panel and press movement/jump keys (`A`/`D` or `Left`/`Right`/`Space`): verify the player character moves and jumps inside the embedded Game view, and pressing `Q`/`W`/`E`/`R`/`F` does not trigger Scene tools while the Game view is focused.
5. Press `Escape` (or click the Inspector) to release Game input focus, and use the centered **Pause (`⏸`)**, **Step (`⏭`)**, and **Stop (`■`)** toolbar buttons.

### 3. Inspector Header Menus, Compact Vector2s & Scrubbing (US4)
1. Select a GameObject with `Transform`, `Sprite`, `Collider`, and `Rigidbody` components.
2. Verify the GameObject identity header (`[☑ Active] [Icon] [Name]` and `Tag`) is at the top of the Inspector above `Object Template`.
3. Verify each component title appears once in its collapsible header (no duplicate `SeparatorText` beneath it) with a clean three-dot menu icon on the right.
4. Left-click the three-dot menu button: verify the popup (*Reset*, *Copy Settings*, *Paste Settings*, *Remove Component*) opens **without** collapsing the component header.
5. Verify `Position`, `Scale`, Collider `Offset`, Rigidbody `Velocity`, and Rigidbody `Force` all display horizontal `X`/`Y` columns, and dragging horizontally over numeric labels scrubs values smoothly with single-step Undo (`Ctrl+Z`).

### 4. Scene Marquee Selection, Drag-to-Scene & Project/Console Ergonomics (US5, US6, US7)
1. In the Scene view with Move (`W`) active, left-click and drag across empty space to draw a marquee box over multiple objects; verify all enclosed unlocked objects are selected.
2. Drag a sprite or template from the **Project** panel into the **Scene** view (and into the **Hierarchy** panel); verify a new GameObject is instantiated at the drop location.
3. Right-click in the **Project** panel to test *Create*, *Rename*, *Duplicate*, and *Delete*, and use the two-column folder tree, breadcrumbs, and thumbnail slider.
4. In the **Console** panel, toggle the independent `Info`, `Warnings`, and `Errors` count badges and select a row to inspect its details in the lower pane.
