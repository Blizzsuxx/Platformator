# US3 Diagnostics, Console, and saved-state evidence

**Status:** In progress; this is not a US3 acceptance record.

## Focused headless verification

- Source baseline: `b45bdff923aed012f96e2de1b5554541679636f1`; working tree contains changes.
- Host: Linux `6.12.112-1-MANJARO x86_64`; reused Debug build `bin/editor-debug`.
- Build command: `cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_tabs_test platformator_editor_diagnostics_test platformator_editor_console_test platformator_editor_inspector_interaction_test -j2` — passed.
- Test command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_workspace_test|platformator_editor_tabs_test|platformator_editor_diagnostics_test|platformator_editor_console_test|platformator_editor_inspector_interaction_test)$'` — 5/5 passed on 2026-11-06.
- `git diff --check` — passed.
- Coverage includes stale validation request rejection, deleted targets, bounded validation input, asset-only invalidation, three-second validation settlement, source identity conflicts, dirty-tab preservation, explicit overwrite/reload/Save As, Console raw-byte retention and truncation, search/filter/repeat projection, Unicode source locations, numeric one-command commits, precision-preserving unchanged commits, Escape, selection/tab/host draft invalidation, and Inspector field reveal.
- Incremental validation now retains the last complete result for the same document structure, projects bounded validation-relevant fields for changed object IDs, merges only those object issues, and falls back to a full snapshot for structural/stale/oversized requests. Asset events use ResourceIndex reverse consumers to request only affected object IDs.
- Focused incremental verification: `cmake --build bin/editor-debug --target platformator_editor platformator_editor_diagnostics_test -j2` — passed; `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_diagnostics_test$'` — 1/1 passed on 2026-11-06. Regressions cover nested-object revalidation, preserving an unrelated missing-asset issue, clearing a repaired local issue, and asset-only invalidation at unchanged authored revisions.
- Latest revision / Run gate follow-up: the same diagnostics target rebuild passed; the same CTest filter passed 1/1. Regressions now also cover deleting an issue-bearing field, stale versus current Run authorization, blocking a current invalid result, releasing Run after a valid local revision, and explicit F6 cancellation/revalidation. Validation waits remain bounded to three seconds in the tests.
- The Run path now uses the shared latest-result gate, and the Validation panel recomputes Current after processing its F6 button so a same-frame explicit revalidation cannot leave stale issues actionable. Rebuilt `platformator_editor`, diagnostics and Inspector interaction targets; both focused tests passed (2/2), and `git diff --check` passed. Native full-app F6 → issue selection → reveal → Run remains unwalked.

## Remaining acceptance work

- The app has not been driven through the full F6 → issue selection → field reveal → Run barrier walkthrough.
- Source conflict polling/resolution is wired to the current active application workspace. The `DocumentManager` model tests cover multiple tabs, but tab activation and routing a conflict decision to a non-active app tab remain unintegrated.
- Full validation still performs a bounded scene snapshot for initial, structural, explicit F6, and fallback requests. Local edits and known asset consumers use bounded affected-object inputs. A full F6 → issue selection → field reveal → Run barrier walkthrough is still unverified.
- No native desktop walkthrough or benchmark was run.

## Source conflict decision continuation

The tab model now stores Keep Work acknowledgements with the owning DocumentTab and exact observed disk identity. Run checks, Reload, overwrite, Save As and conflict cancellation clear or consult that tab's acknowledgement; a later disk identity invalidates it. Save As cancellation returns to the external conflict decision, and a failed Save As or cancelled overwrite leaves any pending workspace action waiting instead of continuing. A cancelled Save As for another pending dirty action restores the unsaved decision prompt.

Focused verification:

    cmake --build bin/editor-debug --target platformator_editor platformator_editor_tabs_test platformator_editor_workspace_test -j2
    ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_tabs_test|platformator_editor_workspace_test)$' -j2
    git diff --check

Outcome: build passed and both tab-scoped source identity and workspace lifecycle suites passed (2/2). The regression verifies one tab's acknowledgement does not authorize Run for another tab and that a later disk version invalidates the earlier acknowledgement. The native dialog cancel and overwrite-popup routes were not automated. The conflict modal still operates on the active document only because the app tab-switching UI is tracked by T117; T051 remains open until non-active tabs can be selected and resolved in the app. This continuation is not a full US3 acceptance record.

## Focused continuation (2026-11-06)

- Rebuilt the Preview, Workspace, Resources, Diagnostics, Console, Document, History, BatchCommand, InspectorInteraction, Changes, Tabs, Startup, Catalog and AssetWatchIntegration targets in the reused `bin/editor-debug` build as needed.
- Focused CTest groups passed: Preview/Resources/Workspace/Document/History/Console/Diagnostics (7/7); BatchCommand/InspectorInteraction/Changes/Tabs (4/4); Startup/Catalog/AssetWatchIntegration/Changes (4/4).
- The Preview startup path now waits in tests for bounded asynchronous asset preparation. Full Refresh explicitly invalidates and requeues failed paths while preserving last-good content until owner-thread adoption. The regression verifies missing-resource repair after Refresh.
- ASan/UBSan found and helped remove a test-only lifetime error: one regression declared `ResourceIndex` after its borrowing `EditorPreview`, and an async assertion failure unwound in the wrong order. The fixture now gives the index the longer lifetime. With work counters enabled, the rebuilt `bin/editor-asan/platformator_editor_preview_test` passed with `ASAN_OPTIONS=abort_on_error=1:detect_leaks=0` and `UBSAN_OPTIONS=print_stacktrace=1`.
- The app has not been driven through the full F6 → issue selection → field reveal → Run barrier walkthrough. Non-active tab conflict decisions and the native desktop walkthrough remain open; this evidence does not close T051 or T052.

## Tab routing follow-up (2026-11-06)

The earlier note that tab activation was absent is superseded by the T119 implementation recorded in `US9.md`. Scene tabs can now be activated, external-change status is shown on each tab, and the conflict prompt resolves against the active tab's own acknowledgement and source identity. Save/Save As cancellation paths keep the tab open and retain pending decisions; Open Scene results defer while a conflict or other modal decision is active. `DocumentManager::save_all()` stops at a conflict or unsaved untitled tab and reports any tabs already saved.

The editor, workspace lifecycle and tab tests rebuilt successfully; the focused workspace and tabs suites passed 2/2. The native Save As cancel and overwrite-popup routes still have no automated UI driver, and the full F6 → issue selection → field reveal → Run walkthrough remains open under T052. No Windows/macOS verification was performed.

## Diagnostics and command regression follow-up

Rebuilt the editor preview, history, diagnostics, Console, Inspector interaction, tabs and workspace targets. Focused CTest completed 7/7: `platformator_editor_workspace_test`, `platformator_editor_preview_test`, `platformator_editor_history_test`, `platformator_editor_tabs_test`, `platformator_editor_console_test`, `platformator_editor_diagnostics_test`, and `platformator_editor_inspector_interaction_test`.

The Linux desktop walkthrough could not start. Command: `XDG_CONFIG_HOME=/tmp/platformator-012-us3.HcRWzw/config SDL_VIDEODRIVER=x11 bin/editor-debug/platformator_editor --project-root /tmp/platformator-012-us3.HcRWzw/project --asset-root /tmp/platformator-012-us3.HcRWzw/project/assets --scene /tmp/platformator-012-us3.HcRWzw/project/blocked.scene --no-restore`. It exited with `Platformator Editor: Cannot initialize Platformator backends: x11 not available`. `Xvfb` and Weston are unavailable in this environment. The UI walkthrough remains unverified and T052 stays open; no Windows/macOS verification was attempted.

## All-open-tab source identity follow-up (2026-11-06)

The earlier note that non-active app tabs could not be selected or resolved is superseded by the T119 tab-strip implementation. `DocumentManager::source_file_statuses()` checks each open tab, including external scenes outside the project asset root. The app refreshes that snapshot every 500 ms and reuses it for per-tab changed/missing markers; activating a flagged tab opens the conflict decision for that tab. Keep Work acknowledgements are scoped to the exact file identity, and save/reload/save-as only adopt identity after successful operations.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_diagnostics_test platformator_editor_tabs_test -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_diagnostics_test|platformator_editor_tabs_test)$' --output-on-failure
```

Outcome: build succeeded and CTest passed 2/2. Tests cover outside-root sources, independent per-tab status, dirty draft/history preservation, exact-version Keep Work and Run gating, later external edits, overwrite/reload/Save As, and close cancellation. Native conflict-modal interactions and the complete US3 walkthrough remain unverified; T052 stays open.

## Per-field numeric drafts T207 (2026-11-07)

Inspector integer drafts are keyed by widget identity, so drawing a neighboring integer field does not reset the active draft. The ImGui interaction regression covers two integer fields, a held increment gesture, invalid intermediate input, Escape, unchanged Enter/focus commits, and selection/tab/host changes. Behavior coverage includes high-precision doubles, signed and full-range unsigned integers, separate vector components, unchanged Enter, Escape, null/missing/unknown payload retention, and one Undo command per committed edit.

```text
cmake --build bin/editor-debug --target platformator_editor_inspector_interaction_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_inspector_interaction_test$' --output-on-failure
```

Outcome: build passed and the focused Linux headless interaction suite passed 1/1. Native desktop Inspector walkthroughs remain unverified under T052; no Windows/macOS verification was run.

## Diagnostics and command suites recheck (2026-11-07)

Rebuilt and reran the current Linux headless suites for Console, Diagnostics, history, batch commands, Preview/Viewport, Inspector interactions, Workspace, Hierarchy, and discovery. All 9/9 passed. The Inspector interaction suite includes one-command numeric draft commits and the diagnostic suite covers stale validation/navigation state. The X11 desktop walkthrough remains unavailable in this environment (`DISPLAY=:0.0` is unreachable); T052 stays open for F6-to-Run and native modal walkthrough evidence.

```text
cmake --build bin/editor-debug --target platformator_editor_console_test platformator_editor_diagnostics_test platformator_editor_history_test platformator_editor_batchcommand_test platformator_editor_preview_test platformator_editor_inspector_interaction_test platformator_editor_hierarchy_move_test platformator_editor_discovery_test platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(console|diagnostics|history|batchcommand|preview|inspector_interaction|hierarchy_move|discovery|workspace)_test)$'
```

## Process and document gate recheck (2026-11-07)

Rebuilt diagnostics, Console, Inspector interaction, history, document and process targets in `bin/editor-debug` and reran:

```text
cmake --build bin/editor-debug --target platformator_editor_diagnostics_test platformator_editor_console_test platformator_editor_inspector_interaction_test platformator_editor_history_test platformator_editor_document_test platformator_editor_process_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_(diagnostics|console|inspector_interaction|history|document|process)_test$' --output-on-failure --timeout 60
```

Outcome: 6/6 passed on Linux headless. These suites cover current-versus-stale Run authorization, local validation revision updates, explicit F6 revalidation, one-command committed numeric drafts, unchanged/invalid/cancelled draft behavior, history/save state, retained Console output and source-location parsing, and process/build failure barriers. This does not complete the human-driven F6 → select diagnostic → reveal field → Run walkthrough; no native desktop display is available here. T052 remains open for that independent walkthrough and any platform-specific review.

## Current diagnostics and authoring regression rerun (2026-11-07)

Rebuilt the current Workspace, Preview/Viewport, Resources, History, asset watcher, Console, Diagnostics, Inspector interaction, Selection, Hierarchy move, and Discovery targets. CTest passed 11/11. Numeric Inspector tests assert exactly one retained Undo for each committed draft; Diagnostics asserts that Run waits for current validation and blocks on current errors. The native F6 → issue reveal → Run walkthrough remains unavailable because this host has no reachable X11 display, so T052 stays open.

```text
cmake --build build --target platformator_editor_console_test platformator_editor_diagnostics_test platformator_editor_workspace_test platformator_editor_preview_test platformator_editor_resources_test platformator_editor_history_test platformator_editor_asset_watch_integration_test platformator_editor_inspector_interaction_test platformator_editor_selection_test platformator_editor_hierarchy_move_test platformator_editor_discovery_test -j2
ctest --test-dir build --output-on-failure -R '^platformator_editor_(console|diagnostics|workspace|preview|resources|history|asset_watch_integration|inspector_interaction|selection|hierarchy_move|discovery)_test$'
```

No Windows/macOS verification was performed.
