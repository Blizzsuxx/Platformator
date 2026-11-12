# Implementation Plan: Editor UI Consistency and Unity Usability Cleanup

**Branch**: `015-editor-ui-consistency` | **Date**: 2026-11-12 | **Spec**: [spec.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/spec.md)

**Input**: Feature specification from `/specs/015-editor-ui-consistency/spec.md`

## Summary

Clean up all UI layout instabilities, selection synchronization bugs, duplicate controls, noisy/redundant popups, and widget inconsistencies identified in the Platformator Editor GUI audit so the editor behaves and looks consistently like Unity:
1. **Stable Layout & Unified Selection (US1)**: Fix Scene-view picking so clicking an object in the Scene viewport highlights its Hierarchy row, keeps `F2` rename active, and shows **all** components of the GameObject in the Inspector (never filtering out sibling components when `Selection::componentId` is set); remove `draw_assignment_controls()` from above the Project folder/asset split so clicking an asset causes `0 px` of vertical jump; draw `EditorIcon::GameObject` on the left of Hierarchy row labels (`iconX`) instead of swapping on the right gutter on hover; and eliminate conditional banner/tab/toolbar layout shifts in the Scene viewport, Hierarchy, Main Toolbar, and Animation tab bar.
2. **Elimination of Duplicate Controls & Redundant Feedback (US2)**: Remove the duplicate `+ Object` button in the Hierarchy header, merge the duplicate `Snap` / `Step` popup buttons and remove the duplicate bottom-left `"Zoom 100%"` badge in the Scene viewport, remove the duplicate `inspectorPanel.feedback()` text render in `editorapp.cpp`, and remove duplicate `Preferences...` and build/viewport items from the `Help` and `Window` menus.
3. **Clean Information Density & Inline Workflows (US3)**: Strip internal `(#id)` numeric suffixes from visible Hierarchy row text and Inspector reference field buttons (while preserving `HierarchyRow::label` and `SceneReferenceInfo::label` for programmatic test contracts and disambiguation dropdowns); place `Transform` immediately below the GameObject header in the Inspector and remove its erroneous tooltip; replace the Project panel's `"Rename Asset##modal"` with inline tile/row renaming (`F2`) and add a confirmation prompt for interactive asset deletion; and fix redundant/inaccurate tooltips across Project and Run Settings.
4. **Consistent Widget Styling, Multi-Object Headers & UI Scaling (US4)**: Unify floating-point formatting and label drag-scrubbing across single-object Inspector, multi-object Inspector, and Behavior fields; render shared components in multi-object Inspector using `ImGui::CollapsingHeader` and the `⋮` context menu (`Reset`, `Copy`, `Paste`); compact asset reference fields into a single line (moving `Copy Path` into a context menu and moving Sprite source-region editing into the Inspector's Sprite component); scale all hardcoded pixel widths by `uiScale`; and add a consistent clear (`×`) button to all four panel search bars.
5. **Unity-Aligned Toolbar, Console & Game View Ergonomics (US5)**: Keep the Main Toolbar at a fixed single-row height without redundant scene/save status text, streamline the Game view into a single top control bar without raw `component.dump()` JSON dumps in the viewport, and use vector severity icons (`EditorIcon::Info`, `Warning`, `Error`) in the Console panel.

## Technical Context

**Language/Version**: C++20 (GCC 13+ / Clang 16+ on Linux)  
**Primary Dependencies**: Dear ImGui 1.92.9b, SDL 3.4.18, nlohmann JSON, Eigen3, oneTBB  
**Storage**: Lossless `.scene`, `.pttemplate`, and animation JSON files; `.platformator-editor` preferences JSON  
**Testing**: CTest headless editor test suites (`editor/tests/editor_*`) and compact GUI probe (`platformator_editor_gui_probe`)  
**Target Platform**: Linux (x86_64, X11 / Wayland / headless SDL dummy video driver)  
**Project Type**: Native desktop game engine editor (`platformator_editor`)  
**Performance Goals**: Maintain $< 16.6\text{ ms}$ (60 FPS) active authoring frame time and $\ge 50\text{ ms}$ idle wait timeout; `0 px` layout shift on selection changes  
**Constraints**: Preserve lossless JSON round-tripping, atomic undo/redo command history, backward-compatible programmatic/semantic test contracts, and compact benchmark artifact policy (`AGENTS.md`)  
**Scale/Scope**: ~16 source/header files under `editor/src/` and corresponding tests under `editor/tests/`

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Initial Gate | Post-Design Gate |
| :--- | :--- | :--- |
| **I. Performance and Scalability First** | **PASS**: Eliminates redundant UI widgets, duplicate text rendering, and unnecessary Project panel assignment/region passes on every frame when browsing assets. | **PASS**: All UI changes are O(1) per visible widget and preserve existing revision-cached lookups in `HierarchyPanel`, `ProjectPanel`, and `InspectorPanel`. |
| **II. Multicore Design with oneTBB** | **PASS**: All ImGui/SDL rendering and selection state transitions execute on the single UI owner thread; background asset scan and preview tasks remain unchanged. | **PASS**: No new threads, locks, or cross-thread mutations introduced. |
| **III. Research-Backed Collision Architecture** | **PASS**: Runtime broad-phase and narrow-phase collision systems (`src/grid.cpp`, `src/segmentedintervallist.cpp`) are untouched. | **PASS**: Scene viewport picking continues to use editor preview bounds without affecting engine collision code. |
| **IV. Simple Code and KISS** | **PASS**: Removes duplicate UI code paths (duplicate buttons, duplicate feedback rendering, duplicate Sprite region editors, modal rename popup) and reuses existing component header and icon helpers. | **PASS**: Single-object and multi-object Inspector share the same collapsible header + `⋮` menu pattern; search bars share a consistent clear button pattern. |
| **V. Unity-Inspired Developer Experience** | **PASS**: Directly resolves Unity usability and visual consistency gaps across Hierarchy, Inspector, Scene view, Game view, Project browser, Console, Toolbar, and Menu bar. | **PASS**: Matches Unity's selection behavior, Hierarchy left-icon + right-gutter layout, `Transform`-first Inspector order, stationary Project browser, and compact ObjectField slots. |
| **VI. Evidence Before Performance Claims** | **PASS**: Verified via existing CTest suites and bounded 15-frame `platformator_editor_gui_probe` runs per `AGENTS.md`. | **PASS**: No heavy benchmark campaigns required; GUI probe and unit tests verify layout stability and frame budgets. |
| **VII. Correctness and Architectural Integrity** | **PASS**: Preserves `.scene` / `.pttemplate` serialization, undo/redo history invariants, and semantic accessibility action dispatch. | **PASS**: Programmatic labels (`HierarchyRow::label`, `SceneReferenceInfo::label`) and semantic node IDs remain intact while visual canvas labels are cleaned up. |

## Project Structure

### Documentation (this feature)

```text
specs/015-editor-ui-consistency/
├── spec.md                                                  # Feature specification (US1–US5, FR-001–FR-018)
├── plan.md                                                  # This implementation plan
├── research.md                                              # Phase 0 technical decisions and rationales
├── data-model.md                                            # Phase 1 data structures, states, and invariants
├── quickstart.md                                            # Phase 1 verification and compact probe guide
├── checklists/
│   └── requirements.md                                      # Spec quality validation checklist
└── contracts/
    ├── selection-and-layout-stability-contract.md           # Selection sync, stationary Project/Scene/Hierarchy/Tabs
    └── widget-and-panel-consistency-contract.md             # Deduplication, clean labels, widgets, scaling, polish
```

### Source Code (repository root)

```text
editor/src/
├── editorapp.cpp                            # Deduplicate menus (Window/Help), remove doubled Inspector feedback, fix Animation tab marker width & close glyph
├── toolbar.h                                # Fixed single-row height and unified playback control state
├── toolbar.cpp                              # Remove multi-row wrapping & redundant scene/save status text; keep single-row layout
├── viewport.h                               # Scaled toolbar layout & consolidated snap control state
├── viewport.cpp                             # Merge duplicate Snap/Step popup buttons, remove bottom-left Zoom badge, remove unsaved-scene layout shift banner, scale rightControlsWidth
└── panels/
    ├── hierarchy.h                          # Helper for clean display name vs programmatic label
    ├── hierarchy.cpp                        # Fix Scene-picked selection highlight & F2 rename when componentId set, draw GameObject icon on left (iconX), remove right-edge GameObject icon on non-hover, remove duplicate '+ Object' button, stabilize status display
    ├── inspector.h                          # Shared component header helper, Sprite source region preview state, UI-scaled metrics
    ├── inspector.cpp                        # Show all components even when componentId is set, move Transform directly below GameObject header, remove wrong Transform tooltip, unify single/multi-object CollapsingHeader + '⋮' menu, clean float formatting, add search '×' button, embed Sprite source region editor in Sprite component, scale hardcoded pixel widths
    ├── project.h                            # Inline rename focus state and delete confirmation modal state
    ├── project.cpp                          # Remove draw_assignment_controls from above folder/asset split, implement inline F2 rename on tiles/rows, add delete confirmation modal, add '+' Create toolbar button & search '×' button, fix folder/asset tooltips
    ├── referencefields.cpp                  # Strip '(#id)' from closed combo button preview, remove collapsible 'Edit asset path' sub-tree (move Copy Path to right-click menu), scale offsets by uiScale
    ├── behaviorfields.cpp                   # Clean float/double formatting, add label drag-scrubbing, remove noisy inline 'draft; game default...' text (move to tooltip), scale field offsets by uiScale
    ├── console.cpp                          # Render vector severity icons (EditorIcon::Info/Warning/Error) and add search '×' clear button
    ├── game.cpp                             # Consolidate top toolbar into a single row and format runtime object inspector without raw JSON dumps
    └── runsettings.cpp                      # Fix contradictory tooltip on 'Embed Game view in editor'

editor/tests/
├── selection_test.cpp                       # Verify Scene picking highlights Hierarchy and shows all Inspector components
├── inspector_interaction_test.cpp           # Verify Transform placement, multi-object CollapsingHeader menu, float formatting, single feedback output
├── project_test.cpp                         # Verify stationary folder/asset top Y on asset click, inline rename, and semantic compatibility
└── viewport_cases.cpp                       # Verify consolidated Snap button and single Zoom indicator
```

**Structure Decision**: Update existing C++20 editor components in-place under `editor/src/` and expand headless regression tests in `editor/tests/` without adding external dependencies or new libraries.

## Complexity Tracking

No constitutional violations or unjustified complexity introduced.
