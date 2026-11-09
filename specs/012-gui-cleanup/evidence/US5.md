# US5 implementation evidence

## Hierarchy move and selection foundation (2026-11-06)

Implemented and checked T069, T071, and T072. `EditCommandService::move_object` validates source and destination identities, parent cycles, destination bounds, same-parent index normalization, and finite translation-only world positions. It retains the exact before/after subtree payloads and parent/index locations in a two-operation batch. `move_objects` normalizes ancestor/descendant selections to ordered top-level roots, removes siblings in descending-index order, preserves the selected block order, adjusts local positions across parents, and commits all removals/insertions as one structural revision and one Undo/Redo entry. The selection model exposes ordered unique IDs, repaired primary/anchor/component/behavior addresses, focused panel/host, and selected top-level-root normalization.

The hierarchy move regression covers nested-to-nested and both root↔nested directions, exact Undo/Redo, unknown subtree payload preservation, world translation, same-parent reordering, multi-root block order, ancestor/descendant normalization, no-op placement, cycle rejection, duplicate/index rejection, and atomic rollback. The selection regression covers ordered range/additive selection, focus preservation, stale-member and component/behavior repair, and group-root normalization.

Command run:

```text
cmake --build bin/editor-debug --target platformator_editor_hierarchy_move_test platformator_editor_selection_test platformator_editor_history_test platformator_editor_batchcommand_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_(hierarchy_move|selection|history|batchcommand)_test' --output-on-failure
```

Outcome: focused history/selection/move/batch suites passed. The hierarchy move suite also passed under ASan/UBSan. An initial root-to-nested assertion used an out-of-range child insertion index; the fixture was corrected to index 0. This did not complete discovery/assignment cases, mixed Inspector edits, Scene group transforms, hidden/locked controls, or the independent US5 GUI walkthrough.

T073 is complete. The Hierarchy UI applies ordered additive/range selection from Ctrl/Shift row clicks, supports object drag-and-drop to a sibling insertion point or parent row, and routes selected blocks through `move_objects`. F2 opens an inline name field; Delete uses an atomic block deletion; Ctrl+D remaps and duplicates selected top-level subtrees in one history step. Selection changes expand ancestors and scroll the selected object into view. The action dispatcher blocks these shortcuts when text, modal, gesture, application-focus, or panel-focus capture disallows them. The UI sources compile in the editor test build. Current regressions cover action dispatch and command transactions, but do not yet simulate ImGui mouse/key delivery for the panel gestures.

T074 is complete. Multi-selection Inspector fields now intersect compatible fields shared by every selected object, matching component type/occurrence, and matching Script behavior type/ordinal. Mixed checkboxes, numeric inputs, enum values, and compatible component strings expose a common edit; numeric, boolean, enum, and component edits commit with one batch history entry. Missing, incompatible, opaque, and unsupported behavior-schema fields are omitted without creating defaults. The ImGui interaction regression edits common object, component, and behavior fields, checks one-step Undo restores exact data and selection, and verifies missing/incompatible values remain untouched.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_inspector_interaction_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_inspector_interaction_test$' --output-on-failure
```

Outcome: the multi-selection object/component/behavior interaction suite passed. Discovery/search/assignment, hidden/locked controls, and the independent US5 GUI walkthrough remained open at that checkpoint.

T075 is complete. Scene Move/Rotate/Scale gestures now apply one batch over the selected top-level roots, use the primary object as the pivot, and cancel if the root selection changes mid-gesture. Move preserves each root's local scale and rotation; Rotate and Scale apply the matching transform to each root while retaining other local pose fields. Rectangle gestures match the primary component type and occurrence across roots, update each size from the same pointer delta in its own local frame, and update opposite-edge collider offsets in the same batch. Descendant selections are normalized out of the transformed root set. The viewport regression checks top-level-root normalization, preservation of a selected descendant's local pose during group movement, exact group move Undo/Redo, and one-step group rectangle size/offset Undo.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_preview_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_preview_test$' --output-on-failure
```

Outcome: the viewport and preview suite passed. The current test exercises normalized batch transactions and geometry oracles; it does not yet synthesize pointer events through the Scene panel.

Current focused batch after block moves and Hierarchy wiring:

```text
cmake --build bin/editor-debug --target platformator_editor_preview_test platformator_editor_resources_test platformator_editor_workspace_test platformator_editor_hierarchy_move_test platformator_editor_selection_test platformator_editor_history_test platformator_editor_batchcommand_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_(preview|resources|workspace|hierarchy_move|selection|history|batchcommand)_test' --output-on-failure
```

Outcome: the earlier preview/resource/workspace/history/selection/move batch passed 7/7; the latest Hierarchy action/transaction batch passed 5/5. The latest `platformator_editor_hierarchy_move_test` also passed in the ASan/UBSan build with `ASAN_OPTIONS=abort_on_error=1:detect_leaks=0` and `UBSAN_OPTIONS=print_stacktrace=1`.

```text
cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_hierarchy_move_test platformator_editor_selection_test platformator_editor_history_test platformator_editor_batchcommand_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_(workspace|hierarchy_move|selection|history|batchcommand)_test' --output-on-failure
cmake --build bin/editor-asan --target platformator_editor_hierarchy_move_test -j2
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software bin/editor-asan/platformator_editor_hierarchy_move_test
```

## Discovery and editor visibility (2026-11-06)

T070 and T076 are complete. Hierarchy search now matches names, tags, full ancestry paths, component types, and object/component IDs. It reveals matching descendants and their ancestors, filters unrelated rows while preserving document order, and restores the saved expansion state when cleared. Project search caches filtered listing indices by query and directory-entry revision and searches filename, relative path, extension, and asset type. Inspector field and behavior searches include object/component/behavior context, and Add Component/Add Behavior pickers filter by component type and the project behavior catalog. Scene reference choices include stable identity labels and a searchable name/type/ID filter.

T077 is complete. Hidden and locked object IDs are stored in workspace preferences under the canonical scene path (or a tab-scoped key for unsaved scenes), kept sorted/unique, pruned when objects disappear, and cleared when the same tab replaces a document at the same path. Hierarchy context actions and Scene controls change only these preferences. Scene preview rendering omits hidden sprites and editor overlays without changing the document's `active` value; the render cache includes the hidden set. Scene picking filters hidden and locked IDs while preserving authoring order. Preference helper tests verify sorted/idempotent state, stale-ID pruning, and unchanged scene data/revision/history; preview pixel tests verify hidden render output and restoration.

T078 is complete. Asset fields display the full authored path on hover and in the path editor, offer path copy, preserve inline repair, and route Browse, drag/drop, and Project Assign through the same assignment validator. Project Assign recognizes compatible Sprite/Animator/Audio targets from the current selection, labels each target with object and component IDs, requires an explicit choice for ambiguous targets, and assigns only the chosen address. Double-click assigns directly only when one compatible target exists; otherwise the user gets the target selector and Assign action. Invalid roots or asset types leave the prior reference and history unchanged.

Focused commands:

```text
cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_preview_test platformator_editor_inspector_interaction_test platformator_editor_discovery_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_(workspace|preview|inspector_interaction|discovery|document|hierarchy_move|selection)_test' --output-on-failure
ctest --test-dir bin/editor-debug -R 'platformator_editor_(workspace|inspector_interaction)_test' --output-on-failure
git diff --check
```

Outcome: the listed focused suites passed 7/7; the workspace/preview pair passed 2/2 after hidden rendering and preference changes; the latest workspace/Inspector pair passed 2/2 after Project Assign integration. The workspace suite includes real Project panel listing-revision search and target assignment cases. No native interactive walkthrough has been recorded. T079 remains open for the independent US5 GUI walkthrough.

## Focused acceptance recheck (2026-11-06)

Rebuilt and ran the hierarchy move, discovery, history, preview, and resource suites. CTest passed 5/5. These cover transactional cycle rejection and exact Undo/Redo, duplicate-name/search context, mixed-property edits, and editor-only visibility without scene/history dirtiness.

The Linux UI walkthrough is unavailable in this environment: SDL initialization reports `x11 not available`, and neither Xvfb nor Weston is installed. T079 remains open for the visual US5 walkthrough; Windows/macOS checks were skipped as requested.

## Search, reveal, identity, and move regressions (2026-11-06)

T177/T220 and T178/T221 are implemented. Hierarchy object labels include stable IDs, reference candidate labels include full ancestry and IDs, and selecting an object from another panel reveals its ancestors after search expansion is restored. Search remains cached by the existing document/listing revisions and does not reorder authored objects. F2/Delete/Ctrl+D dispatch requires Hierarchy focus and is blocked by text entry, modal, gesture, or application-focus capture. Validated root/nested and same-parent block moves retain world translation and exact Undo/Redo ordering.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_discovery_test platformator_editor_hierarchy_move_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_discovery_test|platformator_editor_hierarchy_move_test|platformator_editor_resources_test|platformator_editor_selection_test)$' --output-on-failure
```

Outcome: build succeeded and CTest passed 4/4. The regressions cover duplicate target path/identity, hierarchy ID labels through rename, selected-object reveal during a nonmatching search, focused shortcut capture, move ordering, cycle rejection, and exact transactional Undo/Redo. Native ImGui pointer/key walkthrough remains unavailable under the Linux display constraints above; T079 remains open.

## Multi-selection, visibility, and reference routes (2026-11-06)

T179/T222, T180/T223, and T181/T224 are implemented. Ordered additive/range selection repairs stale object/component/behavior addresses; compatible Inspector edits and Scene group transforms use one batch transaction around the primary pivot, with descendants normalized out. Per-tab hidden and pick-locked IDs stay in editor preferences, are pruned on replacement/deletion, and do not modify runtime Active or authored history. Asset references share root/type validation across inline edits, paste/copy, Browse, drag/drop, and Project Assign/double-click; ambiguous compatible targets require a specific selection and rejected paths preserve the previous authored value.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_preview_test platformator_editor_workspace_test platformator_editor_selection_test platformator_editor_inspector_interaction_test platformator_editor_discovery_test -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_preview_test|platformator_editor_workspace_test|platformator_editor_selection_test|platformator_editor_inspector_interaction_test|platformator_editor_discovery_test)$' --output-on-failure
```

Outcome: the first run found outdated label assertions after stable IDs were added to hierarchy labels. Updated the independent hierarchy oracle and rename expectations. The rerun passed 8/8 across document, workspace, preview, resources, Inspector interaction, selection, hierarchy move, and discovery suites. T079 remains open because a native visual walkthrough cannot run without an X11 display; Windows/macOS checks were skipped as requested.

## Focused Hierarchy shortcut regression (2026-11-07)

The main app now routes Hierarchy Delete only from a non-repeat SDL Delete keydown for its window. Any mouse-button press in the same app frame suppresses that shortcut, and the panel independently rejects Delete on an ImGui mouse-click frame. F2 and Ctrl+D remain one-shot ImGui key actions. The regression clicks every visible fixture GameObject row and checks selection/document/history, verifies Delete alone removes the selected object in one undoable transaction, and checks that a mouse click plus Delete event does not remove the previous selection. The interaction is also summarized in `evidence/US1.md`.

```text
cmake --build build --target platformator_editor_hierarchy_move_test -j8
ctest --test-dir build -R '^platformator_editor_hierarchy_move_test$' --output-on-failure
git diff --check
```

Outcome: the focused Hierarchy move/interaction suite passed 1/1 in both `build/` and `bin/editor-debug`. The Linux editor app was rebuilt in `bin/editor-debug` with the explicit SDL Delete routing. T079 remains open because the native desktop pointer walkthrough cannot run: `DISPLAY=:0.0` is unreachable in this environment. Windows/macOS checks were skipped as requested.

## US5 regression recheck (2026-11-07)

Rebuilt the affected Preview/Viewport, Project/Workspace, Hierarchy, selection, history, batch edit, discovery, Inspector interaction, and resource suites after the Hierarchy input-routing change.

```text
cmake --build build --target platformator_editor_preview_test platformator_editor_resources_test platformator_editor_workspace_test platformator_editor_hierarchy_move_test platformator_editor_selection_test platformator_editor_history_test platformator_editor_batchcommand_test platformator_editor_discovery_test platformator_editor_inspector_interaction_test -j8
ctest --test-dir build -R '^(platformator_editor_preview_test|platformator_editor_resources_test|platformator_editor_workspace_test|platformator_editor_hierarchy_move_test|platformator_editor_selection_test|platformator_editor_history_test|platformator_editor_batchcommand_test|platformator_editor_discovery_test|platformator_editor_inspector_interaction_test)$' --output-on-failure -j8
git diff --check
```

Outcome: the build passed and all nine focused suites passed. The test batch covers headless selection and authoring behavior; it does not replace the unavailable native visual walkthrough, so T079 remains open. No Windows/macOS verification was performed.

## Current debug-build regression recheck (2026-11-07)

Rebuilt and reran the current Linux headless Workspace, Preview/Viewport, History, BatchCommand, Console, Diagnostics, Inspector interaction, Hierarchy movement, and Discovery suites. All 9/9 passed, including the Hierarchy regression that clicks every visible object without changing document/history and confirms intentional keyboard Delete still works. A native GUI pointer walkthrough remains unavailable because `DISPLAY=:0.0` is unreachable; T079 remains open. Windows/macOS checks were skipped as requested.

```text
cmake --build bin/editor-debug --target platformator_editor_console_test platformator_editor_diagnostics_test platformator_editor_history_test platformator_editor_batchcommand_test platformator_editor_preview_test platformator_editor_inspector_interaction_test platformator_editor_hierarchy_move_test platformator_editor_discovery_test platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(console|diagnostics|history|batchcommand|preview|inspector_interaction|hierarchy_move|discovery|workspace)_test)$'
```

## Hierarchy, reference and viewport gate recheck (2026-11-07)

After adding raw SDL mouse-button suppression for Delete, rebuilt and ran the focused Hierarchy, discovery, history, Preview/Viewport, Inspector/reference, template and selection suites:

```text
cmake --build bin/editor-debug --target platformator_editor_hierarchy_move_test platformator_editor_discovery_test platformator_editor_history_test platformator_editor_preview_test platformator_editor_inspector_interaction_test platformator_editor_template_test platformator_editor_selection_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_(hierarchy_move|discovery|history|preview|inspector_interaction|template|selection)_test$' --output-on-failure --timeout 60
```

Outcome: 7/7 passed on Linux headless. The Hierarchy test covers normal row selection, SDL Delete coincident with a mouse event, and standalone undoable Delete; the other suites cover move/reorder, viewport transforms, selection repair, declared reference edits and duplication policy. The native pointer, mixed-property and project-visibility walkthroughs remain unavailable, so T079 is still open. No Windows/macOS verification was performed.

## Shared Project asset classification (2026-11-07)

Project search labels and asset icon/preview selection now use one extension classifier. WebP and QOI share the image category, and AIFF/AIF/M4A/Opus share the audio category, so the visible search type matches the tile treatment. The workspace regression now checks these image/audio query results; `platformator_editor_workspace_test` passed 1/1 after the refactor.

## Current US5 regression rerun (2026-11-07)

Rebuilt and reran the focused Workspace, Preview/Viewport, Resources, History, asset watcher, Console, Diagnostics, Inspector interaction, Selection, Hierarchy move, and Discovery suites. CTest passed 11/11. This covers transactional move ordering/cycle rejection, duplicate-name discovery, selection, history, mixed-property edits, and the Hierarchy click/delete regression. It does not replace the native pointer and Project visibility walkthrough; T079 remains open because this Linux environment has no reachable X11 display. No Windows/macOS verification was performed.
