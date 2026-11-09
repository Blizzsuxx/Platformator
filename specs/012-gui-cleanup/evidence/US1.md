# User Story 1 verification

Captured 2026-11-05 from the feature worktree on `branch-without-bvh`, based on `b45bdff923aed012f96e2de1b5554541679636f1`. Debug builds remain under `bin/editor-debug`; no executable or build tree was copied into evidence.

## Build and focused checks

- Build command: `cmake --build bin/editor-debug --target platformator_editor platformator_editor_document_test platformator_editor_history_test platformator_editor_batchcommand_test platformator_editor_workspace_test platformator_editor_preview_test platformator_editor_authoringindex_test platformator_scene_script_roundtrip_test -j2`.
- Test command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(document|history|batchcommand|workspace|preview|authoringindex)_test|platformator_scene_script_roundtrip_test)$'`.
- Result: 7/7 passed in 0.50 seconds: document/save compatibility, workspace, preview/viewport, history, batch commands, authoring index, and the ordinary runtime scene round trip.
- The document test now builds expected hierarchy rows by independently traversing saved object/component order and checks row kind, identity, owner, depth, parent row, expansion, and labels. It also adds a populated nested subtree and verifies the cached rows rebuild after structural edits.
- Authoring/viewport cases cover empty, collider-only, camera, missing-asset and inactive objects; legacy/conflicting camera dimensions; translation-only parents; component overlap cycling; full-panel coordinates; pointer-anchored zoom; W/E/R capture; snapping; finite rectangle dimensions; and rectangle batch update/cancel/Undo. Box collider width and local offset preview atomically and Undo together while preserving the opposite edge.
- Save/game-consumer checks in the document suite save and reopen editor-authored scene data, load it through `Runtime`, and compare camera semantics while retaining editor-only unfamiliar data. The runtime scene round-trip suite also passed.

## Limits

- These checks use the SDL software/headless test lane; no interactive desktop pointer walkthrough or physical-GPU session was run.
- Windows and macOS hierarchy/input behavior remain unverified. No benchmark capture was run.

## Hierarchy selection must not invoke Delete (2026-11-07)

The main app now routes Hierarchy Delete only from a non-repeat SDL Delete keydown for its window. Any mouse-button press in the same app frame suppresses that shortcut, and the panel independently rejects Delete on an ImGui mouse-click frame. F2 and Ctrl+D remain one-shot ImGui key actions.

The ImGui panel regression clicks each visible GameObject row and verifies selection without deleting authored data or changing history. A Delete key event coincident with mouse interaction is suppressed; Delete pressed on its own still creates exactly one undoable removal.

Focused command:

```text
cmake --build build --target platformator_editor_hierarchy_move_test -j8
ctest --test-dir build -R '^platformator_editor_hierarchy_move_test$' --output-on-failure
git diff --check
```

Outcome: the hierarchy interaction and move suite passed 1/1. This was exercised through ImGui's headless interaction lane; a native desktop pointer walkthrough was unavailable because the configured X display could not be reached.

## Follow-up: use ImGui's Delete key event directly (2026-11-07)

This intermediate ImGui-only routing was superseded after the click still reproduced in the running editor. The current app accepts Delete only from a non-repeat `SDL_EVENT_KEY_DOWN` with `SDL_SCANCODE_DELETE` targeted at the editor window, then passes that one-frame edge to `HierarchyPanel`. The panel still suppresses mouse press/hold/release frames and requires Hierarchy keyboard focus. The headless regression verifies ordinary clicks and ImGui Delete state alone cannot delete, coincident mouse plus explicit Delete is suppressed, and a standalone Delete remains undoable.

Commands: `cmake --build bin/editor-debug --target platformator_editor platformator_editor_hierarchy_move_test -j2`; `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_hierarchy_move_test$'`; `git diff --check && git diff --cached --check`.

Outcome: the current debug editor rebuilt and the Hierarchy regression passed 1/1. The Linux display server is unreachable here, so a native pointer walkthrough remains unavailable.

Both runnable editor targets were rebuilt again after the thumbnail-cache changes: `build/platformator_editor` and `bin/editor-debug/platformator_editor`. The current executables include the Hierarchy Delete guard. The focused Hierarchy and resource suites passed in both build trees (2/2 each); a native pointer walkthrough is still unavailable because no X display is reachable.

## Hierarchy click/Delete frame-split regression (2026-11-07)

The Hierarchy Delete shortcut now ignores mouse button press, held, and release frames. This prevents an input event arriving in a later ImGui frame of the same mouse gesture from deleting the newly clicked GameObject. Explicit Delete on a keyboard-only frame still uses the existing undoable command path.

Added a regression that sends Delete on the mouse-release frame after selecting a GameObject and verifies the object and undo history remain unchanged. The existing cases still click each visible object row, preserve selection/data, and allow standalone Delete.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_hierarchy_move_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_hierarchy_move_test$'
```

Outcome: debug editor build succeeded and the focused Hierarchy suite passed 1/1. Native pointer walkthrough remains unavailable in the current environment.

## SDL mouse-button suppression (2026-11-07)

The application now cancels the Delete shortcut at the SDL event boundary whenever the editor window receives a mouse-button press or release in the same frame. `HierarchyPanel` also receives that raw mouse-event fact in addition to its ImGui press/hold/release guard. This covers delayed or missed ImGui mouse state while preserving standalone, undoable Delete.

The headless regression explicitly supplies a Delete edge together with a raw SDL mouse-button event while ImGui reports no active press; the object and history remain unchanged. The test then confirms a standalone Delete still removes the selected object as one history step.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_hierarchy_move_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_hierarchy_move_test$' --output-on-failure --timeout 60
```

Outcome: the current debug editor and Hierarchy test rebuilt; the focused test passed 1/1. No native display server is available here for a pointer walkthrough. Restart any already-running editor process to load the rebuilt executable at `bin/editor-debug/platformator_editor`.

## Linux launch variant refresh (2026-11-07)

Refreshed the existing RelWithDebInfo, Release, and Release-with-session editor executables from the current source, in addition to the documented `bin/editor-debug/platformator_editor`. The Hierarchy click/Delete regression passed in each build tree:

```text
cmake --build build --target platformator_editor platformator_editor_hierarchy_move_test --parallel 4
ctest --test-dir build -R '^platformator_editor_hierarchy_move_test$' --output-on-failure
cmake --build bin/editor-release --target platformator_editor platformator_editor_hierarchy_move_test --parallel 3
ctest --test-dir bin/editor-release -R '^platformator_editor_hierarchy_move_test$' --output-on-failure
cmake --build bin/editor-release-session --target platformator_editor platformator_editor_hierarchy_move_test --parallel 3
ctest --test-dir bin/editor-release-session -R '^platformator_editor_hierarchy_move_test$' --output-on-failure
```

Outcome: the test passed 1/1 in all four Linux build trees (Debug, RelWithDebInfo, Release, and Release with editor session). Each executable also printed `--help` successfully. These headless checks do not verify a live pointer event; the display server remains unavailable here. Launch the freshly built `bin/editor-debug/platformator_editor` for the documented debug path.
