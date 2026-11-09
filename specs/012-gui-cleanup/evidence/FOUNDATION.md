# Foundation verification

Captured 2026-11-05 after T017. Build artifacts remain under `bin/editor-debug`; no build tree or executable was copied into evidence.

## Build and focused checks

- Source: branch `branch-without-bvh`, HEAD `b45bdff923aed012f96e2de1b5554541679636f1` plus the current feature worktree.
- Build: CMake Debug, Ninja, `/usr/bin/c++`, editor enabled. Targets built: `platformator_editor`, `platformator_editor_batchcommand_test`, `platformator_editor_session_context_test`, `platformator_editor_workspace_test`, `platformator_editor_process_test`, `platformator_editor_history_test`, `platformator_editor_catalog_test`, and `platformator_editor_startup_test`.
- Build command: `cmake --build bin/editor-debug --target platformator_editor_batchcommand_test platformator_editor_session_context_test platformator_editor_workspace_test platformator_editor_process_test platformator_editor_history_test platformator_editor_catalog_test platformator_editor_startup_test platformator_editor -j2`.
- Test command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(batchcommand|session_context|workspace|process|history|catalog|startup|capture_budget)_test$' -j2`.
- Result: 8/8 passed in 2.10 seconds. This includes the new batch and session-context suites, affected workspace/process tests, the formerly passing history/catalog/startup suites, and the capture-budget guard test. The guard test did not run a benchmark capture.
- Session-context execution found an initialization-order defect: the workspace read its next document generation before that member was initialized, so the default document had empty indexes. The generation member was moved before the document member; the test now requires a nonzero generation and populated indexes. A stale-tab assertion was corrected to inspect the created object after the default camera row.

## Open acceptance items

- The baseline audit's hierarchy-row failure at `editor/tests/document_test.cpp:435` remains open for T028. It was not rerun in this Foundation checkpoint; see [BASELINE.md](BASELINE.md) for its historical diagnosis.
- The baseline audit's viewport projection failure at `editor/tests/viewport_cases.cpp:59` remains open for T020/T022. It was not rerun in this Foundation checkpoint; see [BASELINE.md](BASELINE.md) for its historical diagnosis.
- Neither historical failure is reported as a passing check here. Windows/macOS adapters and native desktop acceptance remain unverified.
- No GUI probe or benchmark capture was run. The capture-budget test verifies only the guard behavior.
