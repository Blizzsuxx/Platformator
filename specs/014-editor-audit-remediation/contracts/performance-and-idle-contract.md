# Contract: Performance, Watcher Exclusions & Idle Lifecycle

**Feature**: `014-editor-audit-remediation`  
**Covers**: `FR-001`, `FR-002`, `FR-003`, `FR-004`, `FR-005`, `SC-001`, `SC-002`, `SC-003`

---

## 1. Idle Wait Contract (`editorapp.cpp`)

1. When the editor event queue is empty, no UI widget/gizmo is actively dragged, and no immediate redraw is requested:
   - `idleWait()` MUST block in `SDL_WaitEventTimeout` for up to `idleHeartbeat` (`250 ms`).
   - `nextReferenceFallbackCheck` MUST NOT be passed to `include_deadline(...)` when `projectSession.project_changes().coverage() == ProjectCoverageState::Current` or while `assetWatchScan` is in flight.
   - `assetWatchSchedule.deadline()` MUST NOT be passed to `include_deadline(...)` while `assetWatchScan` is in flight or when `!assetWatchSchedule.pending()`.
2. Both the main window `SDL_Renderer` (`src/sdlwindow.cpp`) and any secondary viewport host `SDL_Renderer` (`editor/src/windowhost.cpp`) MUST attempt to enable vertical sync (`SDL_SetRenderVSync(renderer, 1)`) on creation.

## 2. Directory Exclusion Contract (`watch_linux.cpp`, `project.cpp`, `projectsession.cpp`)

All directory traversal and `inotify` registration routines MUST skip any directory entry where:
- `name.starts_with('.')` (e.g., `.git`, `.venv`, `.agents`, `.codex`, `.specify`, `.cache`)
- `name` is one of: `"bin"`, `"build"`, `"dist"`, `"vendored"`, `"vendor"`, `"node_modules"`, `"specs"`, `"benchmark"`, `"diplomski"`, `"paper"`, `"docs"`

## 3. Per-Frame Caching Contract (`editorapp.cpp`, `project.cpp`)

1. `executable_identity(selectedExecutable)` and `behaviorCatalog.configure(...)` MUST only execute when `selectedExecutable` changes, a build/scan completes, or at a throttled interval ($\ge 1\text{ s}$), never unconditionally on every frame.
2. `inspectorPanel.template_author_bindings(document)` and `inspectorPanel.template_author_schemas(document)` MUST be cached by `(projectGeneration, tabId, documentGeneration, catalogRevision)`.
3. `SemanticSnapshot` construction and `accessibilitySnapshots.publish(...)` MUST be skipped on frames where `(projectGeneration, documentGeneration, selectionRevision, consoleRevision, focusedPanel, operationActive)` is unchanged and no pending semantic action/query requires a fresh layout bounds capture.
4. Source file content hashing in `perform_scan` (`project.cpp`) MUST reuse the cached FNV-1a hash when a file's canonical path, `file_size`, and `last_write_time` match the previous scan.
