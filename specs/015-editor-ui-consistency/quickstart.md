# Quickstart & Validation Guide: Editor UI Consistency and Unity Usability Cleanup

**Feature**: `015-editor-ui-consistency`  
**Date**: 2026-11-12  
**Spec**: [spec.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/spec.md)

---

## 1. Prerequisites & Build

Configure and build the editor and its headless test suite using the `editor-debug` preset (`bin/editor-debug`, reusing existing build artifacts per `AGENTS.md`):

```bash
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4
```

---

## 2. Automated Verification Suite

Run the editor CTest suite to verify all selection, layout stability, Inspector, Project, Hierarchy, Console, and Viewport contracts:

```bash
ctest --preset editor-debug --output-on-failure -R "editor_"
```

Key test targets verifying this feature:
- `editor_selection_test` & `editor_viewport_cases`: Verifies clicking an object in the Scene viewport highlights its Hierarchy row, keeps `F2` rename enabled, and preserves all sibling components in the Inspector (`FR-001`).
- `editor_project_test`: Verifies clicking any asset in the Project browser causes `0 px` vertical shift to the folder tree and asset grid, supports inline `F2` rename and delete confirmation, and preserves semantic template/assignment actions (`FR-002`, `FR-011`).
- `editor_inspector_interaction_test`: Verifies `Transform` is placed immediately below the GameObject header, single/multi-object component headers use `CollapsingHeader` + `⋮` menus, float formatting is clean, label drag-scrubbing works across Inspector and Behavior fields, and `feedback_` is rendered once (`FR-007`, `FR-010`, `FR-013`, `FR-014`, `FR-015`, `FR-016`).
- `editor_console_test`: Verifies vector severity icons, search clear (`×`) button, and badge filtering (`FR-017`, `FR-018`).

---

## 3. Compact GUI Probe & Visual Verification

In accordance with `AGENTS.md`, run the bounded headless GUI probe (15 frames) to confirm 0 layout jumps, clean visual rendering, and fast frame times:

```bash
SDL_VIDEODRIVER=dummy ./bin/editor-debug/editor/platformator_editor_gui_probe \
  --frames 15 \
  --warmup-frames 2 \
  --project-root . \
  --scene examples/mario/level1.scene
```

Expected outcome:
- `0 px` vertical jump when selecting objects or assets across frames.
- Clean Hierarchy labels without `(#id)` suffixes, left-aligned GameObject icons, and right-aligned hover Eye/Lock toggles.
- No duplicate buttons in Hierarchy (`+`), Scene toolbar (`Snap`), or Main Menu (`Window` / `Help`).
