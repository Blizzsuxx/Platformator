# Implementation Plan: Unity-Inspired Editor Ergonomics (Phase 1)

**Branch**: `branch-without-bvh` | **Date**: 2026-11-12 | **Spec**: [spec.md](spec.md)

**Feature directory**: `specs/013-editor-unity-ergonomics`.

## Summary

Implement Phase 1 high-impact usability and styling enhancements across the Platformator editor to achieve Unity-like ergonomics:
1. **Hierarchy Streamlining**: Remove component sub-nodes from the tree, presenting GameObjects exclusively to eliminate row clutter and parenting ambiguity.
2. **Compact Transform Layout**: Redesign the top of the Inspector into a standard GameObject header (Active, Name, Tag) and group Transform properties (Position X/Y, Rotation, Scale X/Y) into compact horizontal rows with colored axis indicators.
3. **Component Header Action Menus**: Replace in-body buttons with a trailing `⋮` button and header right-click context menu (*Reset*, *Copy Settings*, *Paste Settings*, *Remove Component*).
4. **Numeric Label Scrubbing**: Drag-to-scrub values over numeric property labels with `Shift` (10×), `Alt` (0.1×), and `Ctrl` (step snapping) modifiers, recording a single consolidated undo command upon release.
5. **Shortcut & Navigation Alignment**: Remap `F` to Frame Selected (with fallback to Frame All), `Shift+F` to Frame All, and add `Q` for the Hand/Pan tool.

All changes preserve existing `.scene` JSON serialization contracts, atomic undo transactions, and headless test harness compatibility.

---

## Technical Context

**Language/Version**: C++20 (GCC 13+ / Clang 16+ on Linux).  
**Primary Dependencies**: Dear ImGui 1.92.9b, SDL 3.4.18, nlohmann JSON, oneTBB.  
**Storage**: Lossless `.scene` JSON array format (no schema migrations or format modifications).  
**Testing**: Existing CTest test suites (`editor/tests/`) including `hierarchy_move_test`, `inspector_interaction_test`, `selection_test`, `viewport_cases`.  
**Target Platform**: Linux (x86_64).  
**Project Type**: Desktop game engine editor application (`platformator_editor`).  
**Performance Goals**: Maintain 60+ FPS UI refresh, reduce hierarchy row traversal overhead by >65%, zero per-frame heap allocations during numeric scrubbing or label hover.  
**Constraints**: All state changes must generate valid `EditCommand` instances for undo/redo; text input focus must completely suppress editor shortcuts.

---

## Constitution Check

| Principle | Initial Gate | Post-Design Gate |
| :--- | :--- | :--- |
| **I. Performance and Scalability** | **PASS**: Hierarchy row reduction removes redundant ImGui widget submission and connector traversal passes. | **PASS**: O(1) label scrubbing with zero heap allocation during drag; consolidated undo commits. |
| **II. Multicore Design** | **PASS**: Editor UI mutations remain strictly on the main owner thread. | **PASS**: UI rendering and command dispatching preserve single-threaded ImGui/SDL contracts. |
| **III. Collision Architecture** | **PASS**: No modifications to broad-phase or narrow-phase collision routines. | **PASS**: Collider outline visualization in viewport remains untouched. |
| **IV. Simple Code and KISS** | **PASS**: Removing component sub-nodes from the hierarchy tree dramatically simplifies `hierarchy.cpp`. | **PASS**: Streamlines `HierarchyPanel::rebuild()` by removing component branching and collapsed group state. |
| **V. Unity-Inspired Developer Experience** | **PASS**: Directly satisfies Principle V by aligning hierarchy tree, transform layouts, header menus, and shortcuts with Unity. | **PASS**: Standardizes `F`, `Q`, `Shift+F`, `X`/`Y` dual-column rows, and component `⋮` action menus. |
| **VI. Evidence Before Claims** | **PASS**: Verified through automated headless test fixtures and quickstart manual verification scenarios. | **PASS**: Acceptance tests and ctest regression suites validate every user journey. |
| **VII. Correctness and Lifetimes** | **PASS**: Document IDs and selection model stay synchronized; component removal respects command rollback. | **PASS**: Undo transactions restore removed components and original transform values losslessly. |

---

## Project Structure

### Documentation (this feature)

```text
specs/013-editor-unity-ergonomics/
├── spec.md                  # Feature specification with clarifications
├── plan.md                  # This implementation plan
├── research.md              # Phase 0 technical decisions and rationales
├── data-model.md            # Phase 1 data structures and entities
├── quickstart.md            # Phase 1 verification scenarios and run guide
├── checklists/
│   └── requirements.md     # Spec quality checklist
└── contracts/
    ├── hierarchy-contract.md
    ├── inspector-contract.md
    └── viewport-shortcuts-contract.md
```

### Source Code Impact

```text
editor/src/
├── actions.h                        # Add SelectHandTool to EditorAction, Q to EditorKey
├── actions.cpp                      # Remap F to FrameSelected, Shift+F to FrameAll, route Q
├── viewport.h                       # Add SceneTool::Hand
├── viewport.cpp                     # Implement left-click drag panning in Hand tool mode
├── panels/
│   ├── hierarchy.h                  # Remove ComponentsNode & Component from HierarchyRowKind
│   ├── hierarchy.cpp                # Rebuild GameObject-only rows, clean up drag-and-drop
│   ├── inspector.h                  # Declare compact transform and header context menu helpers
│   └── inspector.cpp                # Compact X/Y rows, header Active/Name/Tag, ⋮ context menu
```

---

## Complexity Tracking

No constitutional violations or unjustified complexity introduced.
