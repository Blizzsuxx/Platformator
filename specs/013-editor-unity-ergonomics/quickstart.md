# Quickstart: Unity-Inspired Editor Ergonomics (Phase 1) Validation

**Feature**: `013-editor-unity-ergonomics`  
**Date**: 2026-11-12  
**Status**: Ready for Implementation  

---

## Prerequisites

1. Active build environment configured for Linux (Debian/Ubuntu or Arch Linux).
2. Existing build preset: `editor-debug`.
3. CMake 3.28+ and Ninja.

---

## Build Commands

From the repository root:

```sh
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4 --target platformator_editor
```

To run editor automated test suites:

```sh
ctest --preset editor-debug -R "editor_" --output-on-failure
```

---

## Validation Scenarios

### Scenario 1: Hierarchy Tree Simplification
1. **Launch**:
   ```sh
   bin/editor-debug/platformator_editor --project-root . --asset-root assets --scene assets/scenes/test.scene
   ```
2. **Observe**:
   - Verify that the Hierarchy window displays only GameObject entries (e.g. `Main Camera`, `Player`, `Floor`).
   - Verify that there are no expandable `Components` nodes beneath GameObjects in the tree.
3. **Select**:
   - Click `Player` in the Hierarchy.
   - Verify that all components on `Player` (Sprite, Rigidbody, Collider, Scripts) appear sequentially in the Inspector.
4. **Reparent**:
   - Drag `Player` onto `Floor`.
   - Verify that `Player` becomes a child of `Floor` with clean drop highlighting.

---

### Scenario 2: Compact Transform & Header Layout
1. **Inspect Selection**:
   - Select `Player`.
2. **Header Layout**:
   - Check that `Active` checkbox, GameObject icon, and `Name` input are on the top row.
   - Check that `Tag` input is directly beneath.
3. **Compact Transform**:
   - Verify `Position` displays `X` and `Y` side-by-side on one row with red/green axis accents.
   - Verify `Rotation` displays a single row with degrees.
   - Verify `Scale` displays `X` and `Y` side-by-side on one row.
4. **Numeric Scrubbing**:
   - Hover over the `X` label in Position: mouse cursor changes to `↔`.
   - Click and drag right: Position X increases smoothly.
   - Hold `Shift` while dragging: values increase 10× faster.
   - Hold `Alt` while dragging: values increase 10× slower (fine precision).
   - Release mouse: press `Ctrl+Z` to verify the entire scrub reverts in one single undo step.

---

### Scenario 3: Component Header Action Menu
1. **Header Action Menu**:
   - On the `Sprite` component in the Inspector, click the `⋮` icon on the right edge (or right-click the header bar).
   - Verify popup menu opens with: *Reset*, *Copy Settings*, *Paste Settings*, *Remove Component*.
2. **Remove Component**:
   - Select *Remove Component*.
   - Verify that the `Sprite` component is removed from the GameObject.
   - Verify that no duplicate text buttons (`[Copy Settings] [Paste Settings] [Reset Settings]`) exist in the body.
3. **Undo**:
   - Press `Ctrl+Z`.
   - Verify that the `Sprite` component and all its properties are restored.

---

### Scenario 4: Navigation Shortcuts (`F`, `Q`)
1. **Frame Selected (`F`)**:
   - Select `Floor` in the Hierarchy.
   - Move or zoom the viewport away from `Floor`.
   - Press `F`.
   - Verify the viewport immediately centers and frames `Floor`.
2. **Frame All Fallback**:
   - Deselect all objects (click empty space in Hierarchy).
   - Press `F`.
   - Verify the viewport frames the entire scene.
3. **Hand Tool (`Q`)**:
   - Press `Q`.
   - Verify the Hand tool activates.
   - Left-click and drag in the Scene viewport: the camera pans smoothly following the mouse.
   - Press `W`: the Move tool activates and gizmos appear.
