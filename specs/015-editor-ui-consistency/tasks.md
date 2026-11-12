---

description: "Task list for Editor UI Consistency and Unity Usability Cleanup"
---

# Tasks: Editor UI Consistency and Unity Usability Cleanup

**Input**: Design documents from `/specs/015-editor-ui-consistency/`

**Prerequisites**: [plan.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/plan.md), [spec.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/spec.md), [research.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/research.md), [data-model.md](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/data-model.md), [contracts/](file:///home/user/Projects/Platformator/specs/015-editor-ui-consistency/contracts/)

**Tests**: Targeted headless CTest regression tasks are included per user story to validate layout stability, selection invariants, and semantic backward compatibility in `editor/tests/`.

**Organization**: Tasks are grouped by user story (`US1`–`US5`) in priority order (`P1` → `P2` → `P3`) so each story can be implemented, tested, and verified independently.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (`[US1]` through `[US5]`)
- Exact repository-relative file paths are included in every task description

---

## Phase 1: Setup (Baseline Build & Test Verification)

**Purpose**: Verify the existing `editor-debug` build and headless CTest suite before applying UI consistency changes.

- [X] T001 Verify baseline build and headless editor test suite via `cmake --preset editor-debug && cmake --build --preset editor-debug --parallel 4 && ctest --preset editor-debug --output-on-failure -R "editor_"` from `CMakeLists.txt`
- [X] T002 [P] Run compact 15-frame baseline GUI probe using `bin/editor-debug/editor/platformator_editor_gui_probe` on `examples/mario/level1.scene` in accordance with `AGENTS.md`

---

## Phase 2: Foundational (Shared Display-Label, Formatting & View-State Primitives)

**Purpose**: Shared helper functions and view-state struct fields required across multiple panels and user stories.

**⚠️ CRITICAL**: Complete these foundational additions before starting the dependent user story phases.

- [X] T003 [P] Add a clean display-name helper `strip_identity_suffix(std::string_view label)` and `format_clean_float(double value, int maxDecimals = 3)` in `editor/src/style.h` and `editor/src/style.cpp` so visual canvas text can strip trailing `" (#<id>)"` suffixes and format floats without raw `%.9g`/`%.17g` precision noise while preserving programmatic labels
- [X] T004 [P] Extend `ProjectViewState` in `editor/src/panels/project.h` with `bool renameFocusPending = false`, `bool confirmDeletePending = false`, and `std::filesystem::path pendingDeletePath` to support inline tile/row renaming and confirmation-guarded deletion

**Checkpoint**: Foundational helpers and state structures ready — User Story phases can now begin.

---

## Phase 3: User Story 1 - Stable Layout and Unified Selection Behavior (Priority: P1) 🎯 MVP

**Goal**: Ensure clicking a GameObject in the Scene viewport selects the GameObject identically to the Hierarchy (highlighting its Hierarchy row, keeping `F2` rename enabled, and showing 100% of its components in the Inspector), eliminate the Project panel vertical jump when selecting assets (`0 px` shift), fix the Hierarchy row icon so `EditorIcon::GameObject` stays on the left at `iconX` instead of swapping on the right gutter on hover, and prevent conditional status banners or tab markers from shifting the Scene canvas, Hierarchy tree, Main Toolbar, or Animation tabs.

**Independent Test**: Click a multi-component GameObject in the Scene viewport and verify its Hierarchy row is highlighted, `F2` rename works, and all components remain visible in the Inspector; click any asset in the Project browser and verify the top Y coordinate of `"Asset folders"` and `"Asset contents"` shifts by `0 px`; hover across Hierarchy rows and verify the GameObject cube stays on the left.

### Implementation for User Story 1

- [X] T005 [P] [US1] Remove the `!currentSelection.componentId` guard on row selection (`const bool selected = selection.is_selected(row.id)`) and `F2` inline rename eligibility, draw `EditorIcon::GameObject` on the left at `{iconX, iconY}`, remove the `else if (!rowHovered && !isVisible && !isLocked)` right-gutter `EditorIcon::GameObject` draw call, and render `duplicatePolicyStatus_` in the footer/tooltip instead of above the search bar in `editor/src/panels/hierarchy.cpp`
- [X] T006 [P] [US1] Remove the `if (selection.current().componentId)` filter on `visibleComponentIds` in `InspectorPanel::draw` so all components belonging to the selected `objectId` remain visible when picked from the Scene view, and include multi-object selection IDs in `objectSelectionKey` so scroll position only resets (`ImGui::SetScrollY(0.0f)`) when the selected object set actually changes in `editor/src/panels/inspector.cpp`
- [X] T007 [P] [US1] Remove `draw_assignment_controls(document, commands, selection)` from above the `"Asset folders"` and `"Asset contents"` child windows in `ProjectPanel::draw` (`editor/src/panels/project.cpp`) so selecting any asset causes `0 px` change to the top Y coordinate of `"Asset folders"` and `"Asset contents"`, while preserving `asset_assignment_targets`, `load_selected_template`, and `semantic_nodes` / `dispatch_semantic_action`
- [X] T008 [P] [US1] Remove the conditional `if (!document.sourcePath) ImGui::TextDisabled(...)` banner above the viewport canvas in `SceneViewport::draw` (`editor/src/viewport.cpp`) so saving an untitled scene never shifts the Scene canvas vertically
- [X] T009 [P] [US1] Lock `draw_editor_toolbar` in `editor/src/toolbar.cpp` to a fixed single-row height (`40.0f * uiScale`) and reserve fixed dirty/error marker width (`ImGui::CalcTextSize(" * !").x`) with `"×"` close buttons on Animation tabs in `editor/src/editorapp.cpp`
- [X] T010 [US1] Add regression assertions in `editor/tests/selection_test.cpp` and `editor/tests/project_test.cpp` verifying that Scene-picked selection (`componentId` set) highlights Hierarchy rows and shows all Inspector components, and that selecting an asset in `ProjectPanel` produces `0 px` vertical shift in the folder/content child windows

**Checkpoint**: User Story 1 complete — Scene/Hierarchy/Inspector selection is unified and all panels maintain stationary layouts (`0 px` jump on click).

---

## Phase 4: User Story 2 - Elimination of Duplicate Controls and Redundant Feedback (Priority: P1)

**Goal**: Remove every duplicate button, redundant popup trigger, doubled feedback message, and duplicate menu item across the Hierarchy header, Scene toolbar, Inspector, and Main Menu bar.

**Independent Test**: Inspect the Hierarchy header (only one `+` button), Scene viewport toolbar and canvas (one Snap popup trigger and one Zoom percentage indicator), Inspector panel (feedback rendered once), and Main Menu bar (`Preferences...` only in `Edit`; `Window` menu contains only window/layout items).

### Implementation for User Story 2

- [X] T011 [P] [US2] Remove the duplicate `ImGui::Button("+ Object")` button and duplicate tooltip next to `icon_button(EditorIcon::Plus, "##hierarchy-create-button", ...)` in `HierarchyPanel::draw` (`editor/src/panels/hierarchy.cpp`)
- [X] T012 [P] [US2] Consolidate the adjacent `ImGui::SmallButton("Snap")` and `ImGui::SmallButton("Step: ...")` buttons in `SceneViewport::draw` (`editor/src/viewport.cpp`) so only one button opens `##scene-snap-settings`, and remove the duplicate bottom-left `"Zoom 100%"` canvas overlay badge in `SceneViewport::draw` (`editor/src/viewport.cpp`)
- [X] T013 [P] [US2] Remove the duplicate `if (!inspectorPanel.feedback().empty()) ImGui::TextColored(...)` rendering in `editor/src/editorapp.cpp` and style the single `feedback_` output inside `InspectorPanel::draw` (`editor/src/panels/inspector.cpp`)
- [X] T014 [US2] Remove the duplicate `Help -> Editor Preferences...` menu item and remove the duplicate `Build`, `Build and Run`, `Stop`, `Zoom In`, `Zoom Out`, `Frame Selected`, and `Frame All` items from the `Window` menu in `editor/src/editorapp.cpp`
- [X] T015 [US2] Update `editor/tests/viewport_cases.cpp` and `editor/tests/inspector_interaction_test.cpp` to verify single snap popup control, single zoom indicator, and single Inspector feedback rendering

**Checkpoint**: User Story 2 complete — zero duplicate buttons, popups, menu items, or doubled feedback strings remain.

---

## Phase 5: User Story 3 - Clean, Unity-Style Information Density and Inline Workflows (Priority: P2)

**Goal**: Display clean GameObject and component names without `(#id)` numeric suffixes in the Hierarchy and closed reference selector buttons, place `Transform` immediately below the GameObject identity header in the Inspector, remove wrong/redundant tooltips, and replace the Project panel's modal `F2` rename with inline renaming and confirmation-guarded asset deletion.

**Independent Test**: View Hierarchy rows and Inspector reference fields (no `(#id)` suffix in visible labels while unit tests on `HierarchyRow::label` and `SceneReferenceInfo::label` still pass); verify `Transform` is immediately below the GameObject header with no behavior drag-and-drop tooltip; press `F2` on an asset in the Project browser to rename inline and press `Delete` to trigger confirmation.

### Implementation for User Story 3

- [X] T016 [P] [US3] Render `strip_identity_suffix(row.label)` for visible row text in `HierarchyPanel::draw` (`editor/src/panels/hierarchy.cpp`) while preserving `HierarchyRow::label` (`"<name> (#<id>)"`) for programmatic queries (`visible_rows()`) and search matching
- [X] T017 [P] [US3] Render `strip_identity_suffix(info.label)` on closed reference selector combo previews in `draw_scene_reference_field` (`editor/src/panels/referencefields.cpp`) and remove `"Primary object: #<id>"` from multi-object Inspector in `editor/src/panels/inspector.cpp`
- [X] T018 [P] [US3] Move the `Transform` section in `InspectorPanel::draw` (`editor/src/panels/inspector.cpp`) immediately below the GameObject identity header (`Active`, `Name`, `Tag`, `Layer`), compact the `Object Template` section below `Transform`, and remove the unrelated `"Drop a behavior header anywhere in the Inspector to add it to this GameObject."` tooltip from the `Transform` header
- [X] T019 [P] [US3] Replace `"Rename Asset##modal"` in `ProjectPanel` (`editor/src/panels/project.cpp`) with inline `ImGui::InputText` editing inside `draw_asset_tile` and `draw_asset_list_row` when `viewState_.renaming && viewState_.renamingPath == path` (using `viewState_.renameFocusPending`), and add a `"Delete Asset##modal"` confirmation popup when interactive deletion is triggered via `Delete` key or context menu (`viewState_.confirmDeletePending` / `viewState_.pendingDeletePath`)
- [X] T020 [P] [US3] Remove redundant folder-name tooltips in `ProjectPanel::draw_folder_tree`, change asset tile hover tooltips to display relative project paths (`assets/...`) in `editor/src/panels/project.cpp`, replace inline `"draft; game default retained until edited"` text in `editor/src/panels/behaviorfields.cpp` with field tooltips, and update the `"Embed Game view in editor"` tooltip in `editor/src/panels/runsettings.cpp` to accurately reflect gameplay input capture
- [X] T021 [US3] Add test coverage in `editor/tests/project_test.cpp` and `editor/tests/inspector_interaction_test.cpp` verifying inline asset rename state transitions, confirmation-guarded interactive delete, and `Transform`-first Inspector ordering

**Checkpoint**: User Story 3 complete — Hierarchy, Inspector, Project browser, and tooltips present clean Unity-style information and inline workflows.

---

## Phase 6: User Story 4 - Consistent Widget Styling, Multi-Object Editing, and UI Scaling (Priority: P2)

**Goal**: Unify float formatting and label drag-scrubbing across Inspector and Behavior fields, render multi-object shared components inside collapsible headers with `⋮` menus matching single-object mode, compact asset reference fields into a single line while embedding Sprite source-region editing inside the Inspector's Sprite component, scale all hardcoded pixel widths by `uiScale`, and add a consistent clear (`×`) button to all four panel search bars.

**Independent Test**: Scrub float values in single-object, multi-object, and behavior fields and verify clean decimal formatting without `0.300000012` noise; select 2 GameObjects and verify shared components use `CollapsingHeader` + `⋮` menus; edit a Sprite's source region directly in the Inspector; test UI scale at 150% and 200%; and verify the `×` clear button in Hierarchy, Project, Inspector, and Console search bars.

### Implementation for User Story 4

- [X] T022 [P] [US4] Update `draw_number` and `draw_multi_object_fields` in `editor/src/panels/inspector.cpp` and float/double fields in `editor/src/panels/behaviorfields.cpp` to use clean decimal formatting (`format_clean_float` / `"%.3f"`) instead of raw `"%.9g"` / `"%.17g"`, and add horizontal label drag-scrubbing (`ImGuiMouseCursor_ResizeEW`) to `begin_field` in `editor/src/panels/behaviorfields.cpp` and multi-object numeric rows in `editor/src/panels/inspector.cpp`
- [X] T023 [US4] Refactor `InspectorPanel::draw_multi_object_fields` in `editor/src/panels/inspector.cpp` to render each shared component inside an `ImGui::CollapsingHeader` with `ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap`, component vector icon, and right-aligned `⋮` (`EditorIcon::MoreVertical`) popup menu containing `Reset Settings for Selection`, `Copy Settings`, and `Paste Settings to Selection`
- [X] T024 [P] [US4] Compact `draw_asset_reference_field` in `editor/src/panels/referencefields.cpp` into a single-row slot by removing the collapsible `"Edit asset path"` tree node and moving `Copy Path` and `Edit Path` into a right-click context menu (`ImGui::BeginPopupContextItem`) on the asset reference button
- [X] T025 [US4] Embed interactive Sprite source-region preview and pixel-bound editing inside the `Sprite` component section of `InspectorPanel::draw` (`editor/src/panels/inspector.h` and `editor/src/panels/inspector.cpp`) when `sourceRectEnabled` is enabled, committing via `commit_sprite_source_region`
- [X] T026 [P] [US4] Multiply all hardcoded pixel widths and offsets by `uiScale` in `editor/src/panels/inspector.cpp` (`labelWidth`, `axisTagWidth`, `iconSize`, `buttonWidth`), `editor/src/panels/behaviorfields.cpp` (`begin_field` offset `124.0f * uiScale`), `editor/src/panels/referencefields.cpp` (`124.0f * uiScale`, `clearWidth = 46.0f * uiScale`), and `editor/src/viewport.cpp` (`rightControlsWidth = 240.0f * uiScale`)
- [X] T027 [P] [US4] Standardize search bars across `editor/src/panels/hierarchy.cpp`, `editor/src/panels/project.cpp`, `editor/src/panels/inspector.cpp`, and `editor/src/panels/console.cpp` so all four panels render an inline/adjacent `×` clear button when the search query is non-empty
- [X] T028 [US4] Update `editor/tests/inspector_interaction_test.cpp` to verify multi-object collapsible component headers and `⋮` menu actions, clean float formatting, and UI-scaled layout bounds

**Checkpoint**: User Story 4 complete — widgets, multi-object headers, reference slots, search bars, and DPI scaling are uniform across the entire editor.

---

## Phase 7: User Story 5 - Unity-Aligned Toolbar, Console, and Game View Ergonomics (Priority: P3)

**Goal**: Align the Main Toolbar, Game view, and Console panel with Unity's layout conventions by removing redundant toolbar status text, consolidating Game view controls into a single top bar with formatted runtime object inspection (no raw `component.dump()` JSON strings), and rendering vector severity icons in the Console panel.

**Independent Test**: Inspect the Main Toolbar (single fixed row with left tools and centered Play/Pause/Step/Stop), run or inspect the Game panel (single top control bar, formatted runtime object fields instead of raw JSON), and check the Console panel (vector Info/Warning/Error icons on badges and rows).

### Implementation for User Story 5

- [X] T029 [P] [US5] Update `draw_editor_toolbar` in `editor/src/toolbar.cpp` to omit the redundant `"Project / Scene"` and `"Saved | Complete"` text that duplicates the Scene tab bar, keeping left scene/build actions and centered playback controls (`Play / Build and Run`, `Pause / Resume`, `Step`, `Stop`) on a single row
- [X] T030 [P] [US5] Streamline `GamePanel::draw` and `GamePanel::draw_runtime_inspection` in `editor/src/panels/game.cpp` into a single compact top control bar (`Pause`, `Resume`, `Step`, `Stop`, `Aspect`, `Scale`, `Refresh Runtime Objects`) and format inspected runtime object components as readable key-value properties instead of raw `component.dump()` JSON strings
- [X] T031 [P] [US5] Update `ConsolePanel::draw` in `editor/src/panels/console.cpp` to render vector severity icons (`EditorIcon::Info`, `EditorIcon::Warning`, `EditorIcon::Error`) on severity filter badges and diagnostic rows while preserving semantic node IDs (`console-badge-info`, `console-badge-warning`, `console-badge-error`, `row/<n>`)
- [X] T032 [US5] Verify Console and Game panel semantic and interaction tests in `editor/tests/console_test.cpp` and `editor/tests/playsession_test.cpp`

**Checkpoint**: User Story 5 complete — Main Toolbar, Game view, and Console match Unity's clean visual hierarchy.

---

## Phase 8: Polish & Cross-Cutting Verification

**Purpose**: End-to-end build, full CTest regression verification, and compact GUI probe validation per `quickstart.md` and `AGENTS.md`.

- [X] T033 Build all editor and test targets cleanly with `cmake --build --preset editor-debug --parallel 4` from `CMakeLists.txt`
- [X] T034 Run the complete headless editor CTest suite via `ctest --preset editor-debug --output-on-failure -R "editor_"` and confirm 100% pass rate
- [X] T035 Run the compact 15-frame GUI probe in `specs/015-editor-ui-consistency/quickstart.md` (`SDL_VIDEODRIVER=dummy ./bin/editor-debug/editor/platformator_editor_gui_probe --frames 15 --warmup-frames 2 --project-root . --scene examples/mario/level1.scene`) within `AGENTS.md` size and time budgets

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — can start immediately.
- **Phase 2 (Foundational)**: Depends on Phase 1 (`T001`–`T002`) — blocks user story phases that use `strip_identity_suffix`, `format_clean_float`, or `ProjectViewState` fields (`T003`–`T004`).
- **Phase 3 (US1 - P1 MVP)**: Depends on Phase 2 (`T003`–`T004`).
- **Phase 4 (US2 - P1)**: Depends on Phase 2; can run in parallel with or immediately after Phase 3.
- **Phase 5 (US3 - P2)**: Depends on Phase 3 & Phase 4 for shared panel files (`hierarchy.cpp`, `inspector.cpp`, `project.cpp`).
- **Phase 6 (US4 - P2)**: Depends on Phase 5 for `inspector.cpp` and `referencefields.cpp`.
- **Phase 7 (US5 - P3)**: Depends on Phase 2; `console.cpp`, `game.cpp`, and `toolbar.cpp` tasks (`T029`–`T031`) can run in parallel.
- **Phase 8 (Polish)**: Depends on completion of Phases 3–7.

### User Story Dependencies

- **User Story 1 (P1)**: Independent after Foundational Phase 2.
- **User Story 2 (P1)**: Independent after Foundational Phase 2 (touches header/toolbar/menu regions).
- **User Story 3 (P2)**: Builds cleanly on US1/US2 panel layouts while remaining independently testable.
- **User Story 4 (P2)**: Builds on US3 Inspector/Reference field updates while remaining independently testable.
- **User Story 5 (P3)**: Independent after Foundational Phase 2 (`toolbar.cpp`, `game.cpp`, `console.cpp`).

### Parallel Opportunities

- **Phase 2**: `T003` (`style.h`/`style.cpp`) and `T004` (`project.h`) can run in parallel.
- **Phase 3 (US1)**: `T005` (`hierarchy.cpp`), `T006` (`inspector.cpp`), `T007` (`project.cpp`), `T008` (`viewport.cpp`), and `T009` (`toolbar.cpp`/`editorapp.cpp`) operate on distinct files and can run in parallel.
- **Phase 4 (US2)**: `T011` (`hierarchy.cpp`), `T012` (`viewport.cpp`), and `T013`/`T014` (`editorapp.cpp`/`inspector.cpp`) can run in parallel across distinct files.
- **Phase 5 (US3)**: `T016` (`hierarchy.cpp`), `T017` (`referencefields.cpp`), `T018` (`inspector.cpp`), `T019` (`project.cpp`), and `T020` (`behaviorfields.cpp`/`runsettings.cpp`) can run in parallel across distinct files.
- **Phase 7 (US5)**: `T029` (`toolbar.cpp`), `T030` (`game.cpp`), and `T031` (`console.cpp`) can run in parallel.

---

## Parallel Example: User Story 1 (MVP)

```bash
# Execute all panel stability fixes in User Story 1 concurrently across separate files:
Task T005: "Fix Hierarchy selection highlight, F2 guard, and left-aligned GameObject icon in editor/src/panels/hierarchy.cpp"
Task T006: "Remove visibleComponentIds filter on Scene-picked selection in editor/src/panels/inspector.cpp"
Task T007: "Remove draw_assignment_controls from above folder/asset split in editor/src/panels/project.cpp"
Task T008: "Remove conditional unsaved-scene banner above canvas in editor/src/viewport.cpp"
Task T009: "Lock MainToolbar to single-row height in editor/src/toolbar.cpp and stabilize Animation tab markers in editor/src/editorapp.cpp"
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2 — Priority P1)

1. Complete Phase 1: Setup (`T001`–`T002`)
2. Complete Phase 2: Foundational (`T003`–`T004`)
3. Complete Phase 3: User Story 1 (`T005`–`T010`) — fixes all layout jumps and Scene-to-Inspector/Hierarchy selection bugs
4. Complete Phase 4: User Story 2 (`T011`–`T015`) — removes all duplicate buttons, popups, menus, and doubled feedback
5. **STOP and VALIDATE**: Run CTest (`editor_selection_test`, `editor_project_test`, `editor_viewport_cases`, `editor_inspector_interaction_test`)

### Incremental Delivery

1. **P1 Foundation & Stability (`US1` + `US2`)**: Stationary panels, unified selection, zero duplicate controls.
2. **P2 Visual Clarity & Widget Consistency (`US3` + `US4`)**: Clean names without `(#id)`, `Transform`-first Inspector, inline `F2` asset rename, confirmed delete, unified multi-object `CollapsingHeader` + `⋮` menus, clean float formatting, compact reference slots, and `100%–200%` UI scale support.
3. **P3 Workspace Polish (`US5` + Phase 8)**: Single-row Main Toolbar, streamlined Game view without raw JSON dumps, vector Console severity icons, and full regression verification.

---

## Phase 9: Convergence

- [X] T036 Fix modal popup stuck states and support discarding unsaved changes on Build and Run by calling `ImGui::CloseCurrentPopup()` when closing `"Unsaved scene changes"`, `"Scene changed on disk"`, `"Destination already exists"`, and `"Close Scene Tab"`, resetting `closePromptTab` on close/failure, and reloading the saved scene from disk (`workspace->reload_source_file()`) when choosing `Discard` during `PendingAction::BuildAndRun` in `editor/src/editorapp.cpp` per FR-004 (contradicts)
- [X] T037 [P] Fix Hierarchy row icon and label text overlap by passing an empty format string to `ImGui::TreeNodeEx(treeNodeId.c_str(), flags, "")` and rendering the stripped GameObject name via `ImDrawList::AddText` at `textX = iconX + iconSize + 5.0f * uiScale` (clipped before the lock icon) in `editor/src/panels/hierarchy.cpp` per FR-003 (contradicts)
- [X] T038 [P] Fix Main Toolbar button icon and text overlap by reserving left icon padding in toolbar button widths and rendering button labels offset to the right of the vector icon in `editor/src/toolbar.cpp` per FR-016 (contradicts)
- [X] T039 Embed the tab close (`×`) button inside the right edge of each Scene tab (`###editor.scene-tabs`) and Animation Assets tab (`Animation Assets###animation-editor`) instead of rendering a separate adjacent `ImGui::SmallButton("×")` outside the tab, while preserving `sceneTabBounds` and `sceneTabCloseBounds` hit areas in `editor/src/editorapp.cpp` per FR-004 (partial)
- [X] T040 [P] Expand `ConsoleRows` to fill the full available Console panel height when no log row is selected in `editor/src/panels/console.cpp`, and dock `Console###panel.console`, `Animation Assets###animation-editor`, `Build and Run###panel.run`, and `Validation###panel.validation` as tabs in the same bottom dock node (`0x00001001`) alongside `Project and Assets###panel.project` in `editor/resources/default-layout.ini` and `editor/src/editorapp.cpp` per FR-018 (partial)
- [X] T041 [P] Attach the asset reference drag-and-drop target (`ImGui::BeginDragDropTarget()`) directly to the `"##asset-selector"` button before rendering the nullable `"Clear"` button in `editor/src/panels/referencefields.cpp` per FR-015 (partial)
- [X] T042 [P] Replace remaining raw `"%.9g"` and `"%.17g"` float format strings in multi-object Inspector numeric fields (`draw_common_number` and shared component float properties) and behavior vector fields (`draw_vector`) with clean decimal formatting (`"%.3f"` / `format_clean_float`) in `editor/src/panels/inspector.cpp` and `editor/src/panels/behaviorfields.cpp` per FR-013 (partial)
- [X] T043 Rebuild `editor-debug` and verify modal discard flows, Hierarchy/Toolbar non-overlapping text, embedded tab close buttons, full-height/tabbed Console layout, and CTest suite in `editor/tests/` per SC-002 (partial)

---

## Phase 10: Convergence

- [X] T044 Call `ImGui::SetNextItemAllowOverlap()` and left-align tab text (`ImGuiStyleVar_ButtonTextAlign` = `{0.0f, 0.5f}`) on the outer tab `ImGui::Button` in both `###editor.scene-tabs` and `Animation Assets###animation-editor` (and capture `tabHovered` before drawing the embedded `×` button) so the embedded `×` close button receives mouse hover/click input without triggering tab activation or overlapping centered tab text in `editor/src/editorapp.cpp` per FR-004 (partial)
- [X] T045 [P] Update `migrate_scene_window_identity` in `editor/src/preferences.cpp` (and add migration test coverage in `editor/tests/startup_test.cpp`) to migrate saved project layouts (`layout.ini`) that still have the legacy `0x00004000` / `0x00004001` bottom split so `panel.project`, `panel.console`, `animation-editor`, `panel.run`, and `panel.validation` dock as tabs in `0x00001001` without requiring a manual Reset Layout per FR-018 (partial)
- [X] T046 [P] Clear `pendingAction`, `pendingOpenPath`, `pendingProjectRoots`, and `showUnsavedPrompt` when `execute_pending` fails on `UnsavedDecision::Discard` (or non-conflict `PendingAction::BuildAndRun` failure) so `projectLocked` and `documentEditingLocked` do not stay locked, and reset `closeAnimationPromptTab` with `ImGui::CloseCurrentPopup()` when `Save` fails in `"Close Animation Tab"` in `editor/src/editorapp.cpp` per FR-004 (partial)
- [X] T047 Rebuild `editor-debug` and run the headless editor test binaries (`platformator_editor_startup_test`, `platformator_editor_tabs_test`, `platformator_editor_workspace_test`, `platformator_editor_console_test`, `platformator_editor_selection_test`, `platformator_editor_inspector_interaction_test`) to verify Phase 10 convergence fixes per SC-002 (partial)

---

## Phase 11: Convergence

- [X] T048 [US3] Stop `Animator` and `EditorPreview` from overwriting authored `Sprite` dimensions with `AnimationClip` `width`/`height` or `frame.sourceRect`, and remove redundant `width`/`height` inputs from `AnimationPanel` per FR-009 (contradicts): in `src/animator.cpp` (`Animator::applyCurrentFrame`), only fall back to `clip->getWidth()`/`clip->getHeight()` or `frame.sourceRect` / texture dimensions when `sprite->getWidth() <= 0.0f` or `sprite->getHeight() <= 0.0f` so the `Sprite` component owns its world display size; in `editor/src/preview.cpp` (`EditorPreview` proxy sync around lines 1640–1644), keep `spriteInputs.width` and `spriteInputs.height` when positive instead of overriding them with `proxy.clip->getWidth()`/`getHeight()` or `frame.sourceRect`; in `editor/src/panels/animation.cpp` (`AnimationPanel::draw` and `AnimationPanel::semantic_nodes`), remove the `width` and `height` numeric inputs from the UI while keeping `/width` and `/height` optional (defaulting to `0.0f` when omitted) in `src/animationclipserializer.cpp` and `editor/src/animationeditor.cpp` (`validate_animation_clip_document`).
- [X] T049 [P] [US1] Polish the `Animation Assets` panel header, toolbar actions, Undo/Redo routing, and live Scene preview sync in `editor/src/editorapp.cpp` per FR-004 (partial): (1) replace the raw absolute OS filesystem path in the `Animation Assets` header (`editor/src/editorapp.cpp` around line 5250) with a compact project-relative path (e.g., `assets/animations/flag.animset`) in muted text alongside `Save`, `Undo`, and `Redo` buttons; (2) route `Ctrl+Z` / `Ctrl+Y` (`undo_edit` / `redo_edit`) and `Edit -> Undo` / `Redo` to the active `AnimationDocumentTab::document().undo()` / `redo()` when the `Animation Assets` window is focused, and enable `File -> Save All` whenever any Scene or Animation tab is dirty (`hasDirtySceneTab || hasDirtyAnimationTab`); and (3) immediately notify `scenePreview` (`apply_project_resource_change`) and `projectPanel` when an `.animset` tab is saved via `Save` or `Save All` so Scene and Project previews update without waiting for background file polling.
- [X] T050 [P] [US3] Redesign `AnimationPanel::draw` (`editor/src/panels/animation.cpp` and `editor/src/panels/spriteeditor.cpp`) for consistent Unity-style layout and asset assignment per FR-015 (partial): (1) arrange the panel into a responsive two-column split when width permits (`>= 480px` — Left column: compact `FPS` and `Loop` header row, bordered scrollable Frame list with `Add Frame` / `Move Up` / `Move Down` / `Remove Frame` toolbar, and selected frame `Duration` + `Texture` reference field; Right column: `Play`/`Pause` + `Scrub` timeline bar above the interactive `AnimationFrameRegionEditor` source-rect preview canvas) so wide bottom-docked panels do not clip the preview canvas vertically; and (2) add `PLATFORMATOR_ASSET_PATH` (`EditorAssetKind::Image`) drag-and-drop target support (plus immediate commit on drop or Enter) to the frame Texture field in `AnimationPanel::draw` so textures can be dragged directly from `Project and Assets`.
- [X] T051 [P] [US2] Add `Animation Clip` (`.animset`) creation to `ProjectPanel` and the `Assets -> Create` menu, and add a quick-open action in the `InspectorPanel` `Animator` component per FR-014 (unbuilt): (1) in `editor/src/panels/project.cpp` (`execute_context_create`, item/folder context menus) and `editor/src/editorapp.cpp` (`Assets -> Create` menu), add an `Animation Clip` option that creates `New Animation.animset` (with collision suffixing) initialized with a valid runtime `.animset` JSON payload (`frames: []`, `framesPerSecond: 8.0`, `loop: true`, `width: 0.0`, `height: 0.0`, `name`, `filePath`) and opens it in `Animation Assets`; and (2) in `editor/src/panels/inspector.cpp` (`type == 0` / `Animator`), order `Animation Clip` (`animationClipFilePath`) as the first field of the `Animator` component (matching `Sprite` and `Audio`) and add an inline `EditClip` / open action when a valid `.animset` is assigned.
- [X] T052 [US3] Rebuild `editor-debug` and add regression tests in `editor/tests/animation_test.cpp` verifying: (1) `Animator::applyCurrentFrame` and `EditorPreview` preserve positive authored `Sprite` `width` and `height` across animation frames with different `AnimationClip` dimensions or `sourceRect` crops; (2) `.animset` documents without explicit `width`/`height` fields validate and deserialize cleanly while still rejecting negative/non-finite `width`/`height` if present; and (3) `ProjectPanel::execute_context_create("Animation Clip", ...)` creates a valid `.animset` asset that opens cleanly in `DocumentManager::open_animation` per SC-002 (partial).

---

## Phase 12: Convergence

- [X] T053 Fix Inspector `Animator` `"Edit Clip"` path resolution and row placement in `editor/src/panels/inspector.cpp` (and add test coverage in `editor/tests/animation_test.cpp`) by stripping the leading `"assets/"` prefix from `animationClipFilePath` before joining with `document.assetRoot` (preventing broken `<assetRoot>/assets/assets/...` paths) and rendering the `"Edit Clip..."` button on its own aligned row below `draw_asset_reference_field` instead of calling `ImGui::SameLine()` after a full-width row per FR-015 (contradicts)
- [X] T054 [P] Bring `Animation Assets###animation-editor` to the front of its bottom DockNode (`ImGui::SetNextWindowFocus()`) whenever an `.animset` is opened from `ProjectPanel` or `InspectorPanel`, and route `Ctrl+S` / `File -> Save` (`save_current`) to save the active `AnimationDocumentTab` (and refresh `scenePreview` / `projectPanel`) when `Animation Assets` is focused in `editor/src/editorapp.cpp` per FR-004 (partial)
- [X] T055 Move reference counting (`referenceCount`, `getReferenceCount()`, `addReference()`, `removeReferenceAndFreeIfNoReferences()`) into `Asset` (`include/platformator/baseobject.h`) with a protected virtual `freeFromRuntime()` hook overridden by `TextureWrapper` (`include/platformator/texturewrapper.h`, `src/texturewrapper.cpp`), `AudioWrapper` (`include/platformator/audiowrapper.h`, `src/audiowrapper.cpp`), and `AnimationClip` (`include/platformator/animationclip.h`, `src/animationclip.cpp`), and generalize `AudioWrapper::Reference` into `AssetReferenceHandle<T>` / `Asset::Reference<T>` so all three asset types share one reference-counting and RAII retention implementation
- [X] T056 Extract a shared `TextureRegion` struct holding `{ TextureWrapper *textureWrapper` (via RAII reference handle), `SDL_FRect sourceRect`, `bool hasSourceRect }` used by both `Sprite` (`include/platformator/sprite.h`, `src/sprite.cpp`) and `AnimationFrame` (`include/platformator/animationclip.h`, `src/animationclip.cpp`) so `AnimationFrame` follows the Rule of Zero (removing the manual Rule-of-5 copy/move/destructor boilerplate in `src/animationclip.cpp` and `Sprite::freeTexture()`), and collapse redundant `Sprite` constructor overloads via default arguments
- [X] T057 Simplify `AnimationClip` and `Animator`: remove `width`, `height`, `name`, and `framesPerSecond` from `AnimationClip` (`include/platformator/animationclip.h`, `src/animationclip.cpp`), remove `Animator::getCurrentClipName()` (`include/platformator/animator.h`, `src/animator.cpp`, `src/editorsession.cpp`), require positive finite `duration > 0.0f` on every `AnimationFrame` (default `0.1f`, removing `resolve_animation_frame_timing()`'s FPS fallback), and reduce `Animator::applyCurrentFrame()` (`src/animator.cpp`) to `sprite->setTextureRegion(frame.region)` without touching `Sprite` `width`/`height`
- [X] T058 Update `AnimationClip` / `Sprite` serialization, `examples/mario/assets/animations/*.animset` (converting `"duration": 0.0` + `"framesPerSecond"` to explicit per-frame `"duration"` values and dropping `framesPerSecond`, `width`, `height`, `name`), and editor animation tooling (`editor/src/animationeditor.h`, `editor/src/animationeditor.cpp`, `editor/src/panels/animation.h`, `editor/src/panels/animation.cpp`, `editor/src/panels/project.cpp`, `editor/src/preview.cpp`) to remove `framesPerSecond`, `width`, `height`, and `name` and apply `AnimationFrame::region` uniformly
- [X] T059 Update affected engine and editor test fixtures (`tests/regression_tests.cpp`, `tests/runtime_foundation_test.cpp`, `tests/scene_script_roundtrip_test.cpp`, `editor/tests/animation_test.cpp`, `editor/tests/numericdraft_interaction_test.cpp`, `editor/tests/project_test.cpp`), rebuild both engine and `editor-debug` targets, and run the engine and headless editor test suites to verify zero regressions




## Phase 13: Convergence

- [X] T060 CRITICAL Replace the per-frame filesystem checks and `SDL_LoadBMP` allocation/decoding in the Inspector Sprite source-region section with revision-invalidated prepared thumbnails and true source dimensions owned by the existing Project/resource preparation path; render the assigned texture and implement bounded mouse region selection plus reliable numeric draft commits through `commit_sprite_source_region`, removing fabricated 16384-pixel bounds and preserving one Undo step on release and Escape cancellation. Update `editor/src/panels/inspector.h`, `editor/src/panels/inspector.cpp`, resource callback wiring in `editor/src/editorapp.cpp` / `editor/src/panels/project.h`, and reuse region logic in `editor/src/panels/spriteeditor.cpp` where practical; add focused PNG/BMP, real-image-boundary, drag/cancel, and repeated-unchanged-frame preparation checks in `editor/tests/sprite_region_test.cpp` / `editor/tests/inspector_interaction_test.cpp` per Constitution I/IV, FR-015, and T025 (contradicts)
- [X] T061 CRITICAL Validate that every serialized animation frame duration converts to a finite, strictly positive `float` before mutating or loading an `AnimationClip` in `src/animationclipserializer.cpp`, matching `editor/src/animationeditor.cpp`; reject positive finite doubles that overflow to infinity or underflow to zero, including the standalone `AnimationFrame` deserialization boundary, and add runtime deserialization regressions for both extremes and ordinary valid durations in `editor/tests/animation_test.cpp` / `tests/regression_tests.cpp` per Constitution IV and T057 (contradicts)
- [X] T062 CRITICAL Document the public asset and animation contract changes from T055-T058 in `docs/CPP_API_MIGRATION.md` and relevant current editor guidance in `editor/README.md`: identify affected C++ callers and rebuild requirements, explain shared `Asset` retention and `TextureRegion` ownership/lifetimes, map removed AnimationClip members/constructors and `Animator::getCurrentClipName()` to the supported APIs, and describe conversion of legacy FPS-fallback clips to explicit positive per-frame durations with Sprite-owned display dimensions; keep historical reports explicitly historical per Constitution V and T055-T058 (missing)
- [X] T063 CRITICAL Make focused Animation Assets Undo/Redo routing terminal in `undo_edit` and `redo_edit` in `editor/src/editorapp.cpp` even when the active animation cannot undo/redo, so Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, and semantic actions never fall through into the Scene history; add focused-document routing regressions covering empty/exhausted animation history alongside a nonempty Scene history in `editor/tests/tabs_test.cpp` / `editor/tests/animation_test.cpp`, preserving both documents and histories on a no-op per Constitution VII, plan: atomic undo/redo ownership, and T049 (contradicts)
- [X] T064 Separate clean inactive numeric display from precision-preserving edit drafts in `editor/src/panels/inspector.cpp` and `editor/src/panels/behaviorfields.cpp`, removing the remaining `%.9g` / `%.17g` fallback displays while retaining exact authored values and avoiding history changes on unchanged Enter/blur; extend `editor/tests/inspector_interaction_test.cpp` to exercise rendered high-precision single-object and behavior fields as well as existing precision/Undo regressions per FR-013, US4/AC1, and T022/T042 (partial)
- [X] T065 Implement actual horizontal numeric label scrubbing for behavior scalar/vector fields and multi-object built-in/behavior numeric fields in `editor/src/panels/behaviorfields.cpp` and `editor/src/panels/inspector.cpp`; replace cursor-only label affordances with address-scoped drag state, preserve applicable bounds and modifier behavior, commit one atomic Undo step on release, and restore exact per-target values on Escape or selection change. Add mouse-driven single/multi-object drag, cancellation, and Undo regressions in `editor/tests/inspector_interaction_test.cpp` per FR-013, US4/AC1, and T022 (partial)
- [X] T066 Build human-readable visual scene-reference labels in `editor/src/panels/referencefields.cpp` independently of identity-qualified `SceneReferenceInfo::label`, so closed component selectors remove both embedded owner IDs and component IDs even when an asset filename follows them; preserve hierarchy-path disambiguation, component names, programmatic labels, and stable widget identities, with coverage for GameObject, component, assigned-asset component, and duplicate-name cases in `editor/tests/inspector_interaction_test.cpp` per FR-009, SC-003, and T017 (partial)
- [X] T067 Replace generic string inputs for shared component asset-reference properties in `InspectorPanel::draw_multi_object_fields` in `editor/src/panels/inspector.cpp` with compact typed reference slots supporting mixed values, validated drag-and-drop, browse/picker, and clear; adapt shared reference helpers/callback wiring in `editor/src/panels/referencefields.cpp` / `editor/src/editorapp.cpp` as needed and commit compatible assignments to all selected targets as one atomic undoable batch. Add mixed and uniform Sprite/Audio/Animator assignment, invalid-drop, clear, and Undo coverage in `editor/tests/inspector_interaction_test.cpp` per FR-015 and US4/AC3 (partial)
- [X] T068 Add the specified scene/transform tool access cluster to the left side of the fixed-height Main Toolbar in `editor/src/toolbar.h` and `editor/src/toolbar.cpp`, wiring selected tool state and actions to the existing Scene viewport tools in `editor/src/editorapp.cpp`; preserve centered playback, keyboard/tool-state synchronization, and usable narrow/high-scale layouts, and extend toolbar interaction and bounds checks in `editor/tests/startup_test.cpp` per US5/AC1 and FR-018 (missing)
- [X] T069 Complete the Game view presentation in `editor/src/panels/game.h` and `editor/src/panels/game.cpp`: retain a compact Aspect/Scale control row in idle and active states, place runtime hierarchy/details behind an optional drawer/toggle that reserves its own space, center the game canvas within the remaining viewport, and keep restart/capability status from adding unintended toolbar rows. Preserve existing session/input/semantic actions and add idle/running/paused, aspect/zoom centering, and drawer layout coverage in `editor/tests/playsession_test.cpp` per US5/AC2, FR-018, and T030 (partial)
- [X] T070 Finish proportional layout sizing and width-dependent control fitting in `editor/src/panels/inspector.cpp` (including Tag/Layer and narrow component/property rows), `editor/src/panels/hierarchy.cpp` (tree indentation), `editor/src/panels/console.cpp` (search/toolbar widths), and `editor/src/toolbar.cpp`; choose compact labels/icon mode before measuring and placing toolbar groups instead of setting `result.compact` after label selection, and ensure adjacent controls fit or clip/elide within stable row heights. Replace render-only scaling checks with actual non-overlap, label/control bounds, and row-height assertions at 100%, 150%, and 200% plus widths around toolbar mode transitions in `editor/tests/inspector_interaction_test.cpp`, `editor/tests/startup_test.cpp`, and `editor/tests/console_test.cpp` per FR-016, SC-004, and T026/T028 (partial)
- [X] T071 Give the shared Transform section in `InspectorPanel::draw_common_compact_transform` in `editor/src/panels/inspector.cpp` the specified component icon and collapsible-header settings menu, matching single-object presentation where applicable; provide Copy Settings, Paste Settings to Selection, and Reset Settings for Selection for the object-level transform fields with one atomic Undo transaction and exact mixed-value restoration. Add actual menu activation and Copy/Paste/Reset/Undo regressions in `editor/tests/inspector_interaction_test.cpp` per FR-014 and US4/AC2 (partial)
- [X] T072 Notify `scenePreview` and refresh Project previews for every successfully saved animation in `save_all_documents` in `editor/src/editorapp.cpp` before handling a later failed/conflicted tab, so partial Save All results process `result.savedTabs` without waiting for background polling; preserve failure focus/status and add a regression with an animation saved successfully before a later tab fails in `editor/tests/tabs_test.cpp` / `editor/tests/animation_test.cpp` per T049 and plan: immediate saved-animation preview synchronization (partial)


## Phase 14: Convergence

- [X] T073 CRITICAL Restore thumbnail lifetime and renderer ownership in the Inspector Sprite region path in `editor/src/panels/inspector.h`, `editor/src/panels/inspector.cpp`, and `editor/src/editorapp.cpp`: do not retain an unowned `SDL_Texture *` across frames solely under directory/uncertainty revision checks; obtain the current Inspector host's renderer-specific prepared replica through `ProjectPanel` / `ThumbnailCache` each visible frame so it is pinned until rendering completes, while retaining existing revision-cached decoding. Handle eviction, replacement uploads, renderer recreation/release, and detach/redock without stale or cross-renderer textures. Add focused cache-pressure and two-renderer Inspector integration regressions in `editor/tests/sprite_region_test.cpp` / `editor/tests/windowhost_test.cpp` per Constitution VII, plan: explicit resource lifetimes, and T060 (contradicts)
- [X] T074 CRITICAL Preserve exact typed per-target values throughout numeric scrubbing in `editor/src/panels/inspector.h`, `editor/src/panels/inspector.cpp`, `editor/src/panels/behaviorfields.h`, and `editor/src/panels/behaviorfields.cpp`: replace lossy float/double rollback snapshots and unchecked integer conversions with exact original JSON/typed values and representable arithmetic; a click/release without movement, Escape, rejection, and Undo must restore original values and numeric types without extra history. Keep deltas and each participant within supported bounds. Add mouse-driven regressions with high-precision doubles, integers above 2^24 and 2^53, signed/unsigned limits, mixed values, no movement, cancellation, rejection, and exact Undo restoration in `editor/tests/inspector_interaction_test.cpp` per Constitution VII, plan: lossless JSON and atomic history, and T065 (contradicts)
- [X] T075 CRITICAL Bind numeric scrub previews and Sprite source-region interactions to their complete originating document/property context in `editor/src/panels/inspector.h`, `editor/src/panels/inspector.cpp`, `editor/src/panels/behaviorfields.h`, `editor/src/panels/behaviorfields.cpp`, `editor/src/panels/spriteeditor.h`, `editor/src/panels/spriteeditor.cpp`, and lifecycle wiring in `editor/src/editorapp.cpp`: capture document/project generation, tab/host context, target object/component/behavior identities, ordered selection, and property/texture identity as applicable; restore pending numeric previews and cancel drag/text state before selection/tab/project/host changes or when a field is hidden, filtered, collapsed, or removed. Never carry a Sprite drag into another component's current commit callback, and include document identity in Inspector source-region numeric draft addresses. Add regressions that switch selection/documents with reused IDs and the same texture during active drags, then release, verifying no leaked edits, cross-target commits, or spurious history per Constitution VII and T060/T065 (contradicts)
- [X] T076 CRITICAL Replace the per-popup-frame `recursive_directory_iterator` fallback in `draw_multi_asset_reference_field` in `editor/src/panels/referencefields.cpp` with the existing bounded Project/resource discovery and cached candidate presentation, keeping filesystem traversal and expensive resolution out of unchanged UI frames. Preserve a usable typed picker, handle scan/access failures at the discovery boundary, and reuse the existing UI-thread/background preparation ownership rather than adding another worker system. Add a focused large-listing/error regression showing bounded preparation and no repeated tree walk while an unchanged picker remains open in `editor/tests/project_test.cpp` / `editor/tests/inspector_interaction_test.cpp` per Constitution I, plan: revision-cached UI lookups and single UI owner, and T067 (contradicts)
- [X] T077 Correct shared component asset-reference routing in `InspectorPanel::draw_multi_object_fields` in `editor/src/panels/inspector.cpp` to use the actual Sprite (6), Audio (1), and Animator (0) component identities and canonical expected asset kinds (`Texture`, `Audio`, `AnimationClip`); reserve human-readable strings for labels. Replace the incorrect Rigidbody-as-Sprite fixture in `run_multi_object_asset_reference_slot_tests` in `editor/tests/inspector_interaction_test.cpp` with real default component records and drive the full Inspector path for mixed/uniform slots, valid and wrong-kind drops, Clear, and exact batch Undo rather than bypassing dispatch through the helper per FR-015, US4/AC3, and T067 (partial)
- [X] T078 Carry all selected property addresses and the originating selection/document identity through the multi-object asset Browse/picker workflow in `editor/src/panels/referencefields.h`, `editor/src/panels/referencefields.cpp`, and `editor/src/editorapp.cpp`; replace `browse(targets.front(), ...)` plus single-property dialog completion with validated group assignment using one `apply_batch` transaction, preserving existing single-object browsing. Reject stale/incompatible groups atomically and retain the original mixed values on cancellation or failure. Add a dialog-completion integration regression covering assignment to two or more real Sprite/Audio/Animator targets, one Undo step, cancellation, and stale-document rejection in `editor/tests/inspector_interaction_test.cpp` / `editor/tests/nativefile_test.cpp` per FR-015, US4/AC3, and T067 (partial)
- [X] T079 Constrain property-label rendering to its allocated column and make numeric/reference/header layouts fit narrow panels at 100%, 150%, and 200% UI scale in `editor/src/panels/inspector.cpp`, `editor/src/panels/behaviorfields.cpp`, and `editor/src/panels/referencefields.cpp`; clip/elide long labels with a useful tooltip, keep input/axis/menu/clear controls within available bounds, and preserve intended row heights. Extend `editor/tests/inspector_interaction_test.cpp` with actual rendered label/control non-overlap and bounds checks for long built-in/behavior labels, single/multi-object components, Transform axes, and asset slots at fixed narrow physical widths; do not substitute monotonic total-content-height checks or component-free fixtures for these assertions per FR-016, SC-004, and T070 (partial)
- [X] T080 Add the missing Game panel GUI coverage promised by T069 in `editor/tests/playsession_test.cpp`, using the existing headless ImGui/SDL session fixtures to call `GamePanel::draw` with idle, running, and paused sessions and actual frame data. Assert one-row Aspect/Scale controls, aspect/zoom canvas centering and bounds, optional runtime drawer visibility/reserved space, and restart/capability-state geometry at representative narrow and scaled sizes, while preserving existing input/session/semantic behavior; resolve any demonstrated layout failures within `editor/src/panels/game.h` and `editor/src/panels/game.cpp` per US5/AC2, SC-005, and T069 (missing)


## Phase 15: Convergence

- [X] T081 CRITICAL Remove the remaining unconditional integer-to-double-to-`llround`/`lround` conversions from the signed/unsigned shared-component input branches in `InspectorPanel::draw_multi_object_fields` in `editor/src/panels/inspector.cpp`; keep original and edited values as `int64_t`/`uint64_t`, obtain active scrub results as typed JSON from `apply_numeric_delta`, and never round an unchanged integer for display. Ensure representable arithmetic in `editor/src/panels/numericscrub.h` rejects or safely clamps out-of-range deltas before any rounding/conversion. Add full-Inspector multi-object regressions for exact displayed and committed integers above 2^53, signed/unsigned extrema, unchanged Enter/blur, no-movement label clicks, drag/cancel, and exact Undo; test actual shared-component rows rather than only behavior/helper paths in `editor/tests/inspector_interaction_test.cpp` per Constitution VII, plan: lossless JSON and atomic history, and T074 (contradicts)
- [X] T082 CRITICAL Complete application-level Inspector interaction cancellation in `editor/src/editorapp.cpp` and `editor/src/panels/inspector.cpp` / `editor/src/panels/behaviorfields.cpp`: restore pending previews against their originating document before Inspector hide/collapse, active-tab close/rebinding, project replacement, and host move/release, including frames where `InspectorPanel::draw` is skipped; propagate the actual host generation and discard Sprite region numeric drafts as well as drag state when their owner becomes unavailable. Correct `run_lossless_scrub_and_context_binding_tests` in `editor/tests/inspector_interaction_test.cpp` to use Script type 5 instead of Camera type 2, locate/assert real widget identities, and require a changed live preview before testing restoration. Add application/host lifecycle regressions proving hide/detach/close and subsequent release or Save cannot retain an untracked edit, transfer a draft, or create spurious history per Constitution VII and T075 (contradicts)
- [X] T083 Make fallback multi-object asset picker cache invalidation cover nested file additions, deletions, renames, and replacements in `editor/src/panels/referencefields.cpp`, using current project identity and a revision/dependency mechanism covering the scanned subtree rather than only `directory_revision(assetRoot)`; revalidate the selected cached candidate and all targets immediately before an atomic assignment, reporting stale/missing/wrong-kind failures without changing any selected object. Preserve T076's bounded discovery and absence of repeated tree walks on unchanged frames, adapting `editor/src/resourceindex.h` / `editor/src/resourceindex.cpp` only as needed. Add a nested-folder picker regression in `editor/tests/inspector_interaction_test.cpp` / `editor/tests/project_test.cpp` that opens the picker, delivers normal file invalidations, verifies refreshed candidates, and rejects a file deleted after listing without an Undo entry per FR-015, plan: cache invalidation correctness, and T076/T078 (partial)
- [X] T084 Keep the Game view toolbar on one fixed-height row at narrow widths and 100%-200% UI scale in `editor/src/panels/game.cpp`: replace the Details/Restart `NewLine()` fallbacks with a measured compact/elided/overflow presentation that fits the row while retaining the actions and avoiding canvas-top shifts when Details or restart availability changes. Extend `test_game_panel_gui_draw_and_layout` in `editor/tests/playsession_test.cpp` to assert actual Aspect, Scale, Details, and Restart control bounds and shared row positions at narrow/scaled sizes, stable toolbar/canvas-top geometry across state transitions, and vertical as well as horizontal viewport/drawer placement per US5/AC2, FR-018, and T069/T080 (partial)
