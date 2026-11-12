# Research: Complete Editor Performance, Focus, Playability & Unity Ergonomics Audit Remediation

**Feature**: `014-editor-audit-remediation`  
**Date**: 2026-11-12  
**Status**: Completed  

---

## Decision 1: Idle Event-Loop Scheduling & Zero-Spin Wake Deadlines

### Decision
1. In `editorapp.cpp` (`idleWait`), only include `nextReferenceFallbackCheck` in the wake deadline calculation when `projectSession.project_changes().coverage() != ProjectCoverageState::Current && !assetWatchScan`. When `coverage() == ProjectCoverageState::Current`, advance or disarm `nextReferenceFallbackCheck` so it never sits in the past.
2. Only include `assetWatchSchedule.deadline()` in `idleWait` when `!assetWatchScan && assetWatchSchedule.pending()`. While an `assetWatchScan` background task is already running, `idleWait` uses `activeBackgroundWait` (e.g., 16–50 ms poll or event wake) instead of a 0 ms expired deadline.
3. Enable vertical refresh synchronization (`SDL_SetRenderVSync(renderer, 1)`) on the main window renderer (`src/sdlwindow.cpp`) and detached viewport host renderers (`editor/src/windowhost.cpp`) when not running under the `dummy` headless driver.
4. Remove the leftover per-frame `printf("Hand tool pan check: ...")` in `SceneViewport::handle_canvas_navigation` (`editor/src/viewport.cpp:2091-2095`).

### Rationale
- Profiling with `platformator_editor_gui_probe` showed `GUI_WAIT_INCLUSIVE_US - GUI_ACTIVE_FRAME_US` was ~15–35 µs because `nextReferenceFallbackCheck` expired 2 seconds after startup and was never updated once `coverage()` became `Current`, causing `editor_wait_duration` to return `0ms` and skip `SDL_WaitEventTimeout` forever.
- Fixing both deadline guards immediately restores event-driven sleeping (`SDL_WaitEventTimeout` up to 250 ms when idle), dropping idle CPU usage from 100% to < 1%.

### Alternatives Considered
- *Unconditional `SDL_Delay(1)` in the main loop*: Rejected because a fixed sleep adds unnecessary latency to active frames without fixing the broken 0 ms `SDL_WaitEventTimeout` bypass when idle.

---

## Decision 2: Project Watcher Directory Exclusions & Incremental Source Stamp Caching

### Decision
1. Unify directory skip predicates across `editor/src/platform/watch_linux.cpp` (`should_skip_watched_directory`), `editor/src/project.cpp` (`should_skip_directory`), and `editor/src/projectsession.cpp` (`should_skip_project_directory`) to skip:
   - Any directory whose filename starts with `'.'` (`.git`, `.venv`, `.agents`, `.codex`, `.specify`, `.cache`, etc.)
   - Build, third-party, and non-runtime artifact directories: `bin`, `build`, `dist`, `vendored`, `vendor`, `node_modules`, `specs`, `benchmark`, `diplomski`, `paper`, `docs`.
2. In `project.cpp` (`perform_scan`), maintain an in-memory cache mapping canonical source file path $\rightarrow$ `(file_size, last_write_time, content_hash)` so fallback or initial scans do not re-read and FNV-1a hash unchanged source files.

### Rationale
- Without skipping `.venv` (6,340 entries) and `specs` (22,738 entries), the repository has 8,303 directories, exceeding `MaxWatchedDirectories = 4096` (`ENOSPC`). That forced `source_scan_interval()` down to 5 seconds and hashed 35,000+ entries every 5 seconds.
- With the unified exclusion list, watched directories drop from 8,303 to ~95 directories—well within `MaxWatchedDirectories`—and `inotify` notifications stay active continuously.

### Alternatives Considered
- *Increasing `MaxWatchedDirectories` to 65,536*: Rejected because watching `.venv` and historical `specs/` build trees wastes kernel `inotify` watches (which are capped per-user by `fs.inotify.max_user_watches` on Linux) on files irrelevant to the game project.

---

## Decision 3: Per-Frame Caching for Executable Identity, Template Schemas & Semantic Snapshots

### Decision
1. **Executable Identity & Behavior Catalog Configuration**: In `editorapp.cpp:3051-3069`, only query `executable_identity(selectedExecutable)` and call `behaviorCatalog.configure(...)` when `selectedExecutable` changes, a build/scan completes, or at a low-frequency check (e.g., every 1–2 seconds), rather than calling `std::filesystem::canonical`/`stat` on every single frame.
2. **Template Author Bindings & Schemas**: Cache the result of `inspectorPanel.template_author_bindings(document)` and `inspectorPanel.template_author_schemas(document)` keyed by `(document.projectGeneration, document.tabId.value, document.documentGeneration, catalogRevision)` so drawing the Hierarchy panel does not re-parse the scene JSON tree on every frame.
3. **Accessibility / Semantic Snapshot Publishing**: Only rebuild and partition the full `SemanticSnapshot` when the AT-SPI accessibility bridge has active listeners/requests, when a test harness queries semantic nodes, or when a state revision tuple `(documentGeneration, selectionRevision, consoleRevision, focusedPanel, activeMenuState)` changes, rather than allocating and deep-comparing thousands of `SemanticNode` strings on every rendered frame.

### Rationale
- Eliminates ~45–50 ms of redundant per-frame CPU allocations and filesystem syscalls, bringing active authoring frame times from ~60–67 ms down to < 10 ms.

### Alternatives Considered
- *Removing `SemanticSnapshot` entirely*: Rejected because headless integration tests and Linux accessibility (`accessibility_linux.cpp`) rely on semantic node queries and actions. Caching by state revision preserves 100% of test and accessibility behavior at near-zero steady-state frame cost.

---

## Decision 4: Unified Panel Focus & Cross-Panel Scene Navigation (`Hierarchy` $\leftrightarrow$ `Scene`)

### Decision
1. **Immediate Middle/Right-Click & Hand-Tool Panning**:
   - In `SceneViewport::draw` (`editor/src/viewport.cpp:1286`), pass `ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight` to `ImGui::InvisibleButton("##scene-viewport-input", ...)` (or call `ImGui::SetWindowFocus()` when middle/right/Hand-left mouse button is pressed while `hovered` is true).
   - When `panning_` starts on the canvas, set `focused = true` and update `selection.focusedPanel = FocusedPanel::Scene`.
   - At `viewport.cpp:2260`, replace `if (!focused || capture) panning_ = false;` with `if (capture || !panningHeld) panning_ = false;` so an active pan gesture is never aborted on the frame it starts or if the cursor temporarily leaves the window rect while the button remains held.
2. **Scene Tool & Framing Shortcuts After Clicking in Hierarchy**:
   - In `editor/src/actions.cpp`, allow `EditorAction::FrameSelected` (`F`), `EditorAction::FrameAll` (`Shift+F`), `EditorAction::SelectHandTool` (`Q`), `EditorAction::SelectMoveTool` (`W`), `EditorAction::SelectRotateTool` (`E`), and `EditorAction::SelectScaleTool` (`R`) when `capture.sceneFocused || capture.hierarchyFocused` (provided `!capture.wantTextInput && !capture.anyPopupOpen && !capture.embeddedGameFocused`).
   - Unify focus checks in `editorapp.cpp` and `viewport.cpp` so both `Q` and `W`/`E`/`R`/`F`/`Shift+F` use the same focus predicate.
3. **Double-Click Hierarchy Row to Frame in Scene**:
   - In `HierarchyPanel::draw` (`editor/src/panels/hierarchy.cpp`), detect double-click on a GameObject row (`ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)`) and invoke a callback/flag that triggers `sceneViewport.frame_selected(document, selection)` on that frame.

### Rationale
- Matches Unity's exact interaction model: selecting an object in the Hierarchy and immediately pressing `F`, `W`, `E`, `R`, `Q`, or middle/right-dragging in the Scene view works on the first input without requiring a separate focus-transfer click.

### Alternatives Considered
- *Auto-focusing Scene view whenever the mouse cursor merely hovers over it*: Rejected because hover-stealing focus would break keyboard navigation (`Up`/`Down`/`F2`/`Delete`) in the Hierarchy if the user's mouse happens to rest over the Scene view. Allowing `F`/`Q`/`W`/`E`/`R` from `Hierarchy` or `Scene` focus and auto-focusing `Scene` on any mouse button press (Left/Middle/Right) inside Scene preserves both Hierarchy keyboard navigation and instant Scene interaction.

---

## Decision 5: Embedded Game Input Forwarding & 60 FPS Session Capture

### Decision
1. **Extend Editor Session Protocol for Input Forwarding**:
   - Enable `capabilities.inputForwarding = true` in `src/editorsession.cpp` and accept it in `editor/src/playsession.cpp`.
   - Add control pipe commands in `EditorSessionClient::poll_control` (`src/editorsession.cpp`) and sender helpers in `PlaySession` (`editor/src/playsession.cpp`):
     - `key_down <scancode> <keycode> <mod>` / `key_up <scancode> <keycode> <mod>`
     - `mouse_move <x> <y>` (normalized to runtime logical coordinates)
     - `mouse_button <button> <down|up> <x> <y>`
     - `release_all_inputs` (sent automatically when `GamePanel` loses focus or stops)
   - In `EditorSessionClient::poll_control`, synthesize the corresponding `SDL_Event` (`SDL_EVENT_KEY_DOWN`, `SDL_EVENT_KEY_UP`, `SDL_EVENT_MOUSE_MOTION`, `SDL_EVENT_MOUSE_BUTTON_DOWN`, `SDL_EVENT_MOUSE_BUTTON_UP`) and inject it via `SDL_PushEvent(&event)`.
2. **Capture Input in `GamePanel` & Suppress Editor Shortcuts**:
   - When the embedded game is running and the user clicks inside the Game viewport image (or the `Game` window is focused), `GamePanel` captures keyboard state transitions and mouse coordinates/buttons relative to the rendered game rect and forwards them to `PlaySession`.
   - While `GamePanel` is actively capturing gameplay input, `ShortcutCaptureState` suppresses single-key editor shortcuts (`Q`, `W`, `E`, `R`, `F`, `Delete`, etc.), while keeping `Shift+F5` (Stop) and `Escape` (unfocus Game capture) available so the user can always exit or unfocus.
   - Update the status label in `GamePanel` to clearly show `"Embedded game running · Click Game view to capture input (Esc to release)"` or `"Capturing gameplay input (Esc to release to editor)"`.
3. **60 FPS Frame Capture**:
   - Reduce `MinimumCaptureInterval` in `src/editorsession.cpp:45` from `66,667 µs` (15 FPS) to `16,667 µs` (60 FPS).

### Rationale
- Because embedded mode intentionally hides the child SDL window (`windowSettings.hidden = true`), OS window focus never reaches the child process. Forwarding input over the existing non-blocking session control pipe into `SDL_PushEvent` requires zero changes to game scripts or `InputManager`, works identically on X11, Wayland, and headless test runners, and makes the embedded game immediately playable at 60 FPS.

### Alternatives Considered
- *X11 `XReparentWindow` embedding*: Rejected because it breaks on Wayland and headless `dummy` video drivers and causes cross-process window manager focus deadlocks.

---

## Decision 6: Inspector Component Header, Compact 2D Vectors & Label Scrubbing Fixes

### Decision
1. **Fix Component Header Menu (`⋮`) & Spacing**:
   - Pass `ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap` to `ImGui::CollapsingHeader` in `InspectorPanel::draw` (`editor/src/panels/inspector.cpp:2202`).
   - Place the trailing context menu button on the same line (`ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttonWidth)`) and draw three crisp vertical dots using `ImDrawList::AddCircleFilled` (instead of UTF-8 `"⋮"`, which renders as `?` in ProggyClean).
   - Remove the duplicate `ImGui::SeparatorText("Camera" / "Collider" / "Rigidbody" / "Sprite" / "Animator" / "Audio")` calls immediately inside each component body.
2. **Extend Compact Horizontal `X`/`Y` Rows**:
   - Use `draw_compact_vector2` for Collider `Offset` (`{"offset"}`) and Rigidbody `Velocity` (`{"velocity"}`) and `Force` (`{"force"}`).
3. **Top-of-Inspector Ordering & General Label Scrubbing**:
   - Move the GameObject identity header (`[☑ Active] [Icon] [Name]` and `Tag`) above the `Object Template` section in `InspectorPanel::draw`.
   - Add horizontal drag-to-scrub on the left-hand property label inside `begin_field` / `draw_number` / `draw_integer` so all numeric fields in the Inspector support label scrubbing with `Shift` (10×), `Alt` (0.1×), and `Ctrl` (snap) modifiers and single-command undo commit on release.

### Rationale
- Eliminates all four visual/interaction regressions introduced in Phase 1 while bringing full Unity Inspector consistency across all components.

### Alternatives Considered
- *Keeping `ImGui::SeparatorText` inside components*: Rejected because `CollapsingHeader` already displays the component title; repeating it immediately below wastes vertical space and looks broken.

---

## Decision 7: Scene Overlay Toolbar, Marquee Selection & Asset Drag-Instantiation

### Decision
1. **Compact Scene Toolbar / Overlay**:
   - Replace the wrapping text `Selectable` list in `SceneViewport::draw` with a compact single-row toolbar containing icon/short-label buttons for tools (`[Q Hand] [W Move] [E Rotate] [R Scale] [Rect]`) on the left and view toggles (`[Grid] [Snap] [Step] [Zoom] [Frame]`) on the right.
2. **Marquee Box-Selection**:
   - When `tool_ != SceneTool::Hand` and the user left-clicks on empty canvas space (where `pick_candidates` is empty or the drag exceeds a 4 px threshold without hitting a gizmo handle) and drags, set `marqueeSelecting_ = true` and track `marqueeStartScreen_` / `marqueeCurrentScreen_`.
   - Render a translucent selection rectangle (`AddRectFilled` + `AddRect` border) over the Scene canvas.
   - On mouse release, convert the screen box to world AABB bounds, query all visible unlocked scene objects intersecting the box, and update `SelectionModel` (`Shift`/`Ctrl` adds to existing selection; unmodified replaces selection).
3. **Drag-and-Drop Asset Instantiation**:
   - Emit a unified `PLATFORMATOR_ASSET_PATH` drag payload from `ProjectPanel` for draggable assets (images/sprites, `.pttemplate` object templates, audio files, and behavior headers, while keeping `PLATFORMATOR_BEHAVIOR_TYPE` compatibility).
   - In `SceneViewport::draw`, accept `PLATFORMATOR_ASSET_PATH` on the canvas drop target: convert the drop cursor position via `screen_to_world` (snapped if grid snap is active) and instantiate a new GameObject (from template if `.pttemplate`, or with a `Sprite` component configured to the dropped texture path) via `EditCommandService`.
   - In `HierarchyPanel::draw`, accept `PLATFORMATOR_ASSET_PATH` on row and empty-space drop targets to instantiate a new GameObject (or attach a behavior/component when dropped onto an existing GameObject).

### Rationale
- Completes the core 2D level-authoring loop: drag sprites/templates into the Scene or Hierarchy, box-select groups of objects, and manipulate them with compact tools.

### Alternatives Considered
- *Instantiating assets on drag-enter before mouse release*: Rejected because previewing transient objects in the document before drop delivery complicates undo history if the user cancels the drag outside the window. Committing on payload delivery is clean, fast, and atomic.

---

## Decision 8: Project Context Menus, Two-Column Browser, Console Badges, Menus & Theme

### Decision
1. **Project Panel Context Menus & Two-Column Layout**:
   - Add `ImGui::BeginPopupContextItem` on asset/folder rows and `ImGui::BeginPopupContextWindow` on empty space in `ProjectPanel::draw` (`editor/src/panels/project.cpp`) providing:
     - *Create* $\rightarrow$ *Folder*, *Behavior Script*, *Scene*, *Object Template*
     - *Open*, *Reveal in File Manager*, *Rename* (`F2`), *Duplicate* (`Ctrl+D`), *Delete* (`Delete`)
   - Add a resizable left-hand folder tree child pane, a clickable breadcrumb path bar at the top of the right-hand content pane, and a bottom-right thumbnail scale slider (`0.0f` = compact list row, `> 0.0f` = grid tiles).
2. **Hierarchy Header `+` Preset Menu & Gutter Icons**:
   - Add a `+` button in the Hierarchy search/header row opening a preset creation popup (*Create Empty*, *Create Empty Child*, *2D Sprite*, *Camera*, *Audio Object*).
   - Render clickable right-aligned gutter toggles on each Hierarchy row for scene visibility (`Eye`) and scene-picking lock (`Lock`), shown persistently when hidden/locked or when the row is hovered.
3. **Console Severity Badges & Detail Pane**:
   - Replace the single severity combo box in `ConsolePanel::draw` (`editor/src/panels/console.cpp`) with three right-aligned toggle buttons displaying live counts (`Info (N)`, `Warn (N)`, `Error (N)`), allowing independent filtering.
   - Keep the lower detail pane showing full multi-line text and source location for the selected row, and wire double-click on a row with a valid `DiagnosticSourceLocation` to open the file.
4. **Main Toolbar, Menu Bar & Neutral Charcoal Style**:
   - Center `[ ▶ Play ] [ ⏸ Pause ] [ ⏭ Step ]` in `draw_editor_toolbar` (`editor/src/toolbar.cpp`), keeping `Open Scene`, `Save`, and `Build` on the left and project/status identity on the right.
   - Reorganize the main menu bar in `editorapp.cpp` into `File | Edit | Assets | GameObject | Component | Window | Help`.
   - Update `make_base_style()` in `editor/src/style.cpp` to a Unity-inspired neutral dark charcoal palette (`WindowBg = #282828`, `ChildBg = #222222`, `TitleBg = #1E1E1E`, `FrameBg = #1A1A1A`, `Header = #383838`, `FrameBorderSize = 1.0f`), apply a subtle Play Mode tint when `gamePipeline.busy()`, and load a clean proportional TrueType UI font from system/bundled paths at native DPI scale when available.

### Rationale
- Aligns every remaining panel (`Project`, `Hierarchy`, `Console`, `Toolbar`, `Menu Bar`, `Style`) with the audit's Unity usability and visual benchmarks.

### Alternatives Considered
- *Requiring external icon image assets for toolbar/gutter icons*: Rejected in favor of extending the procedural vector icon renderer in `editor/src/style.cpp` (`draw_icon`), which is resolution-independent, requires zero disk I/O, and works reliably in headless tests.
