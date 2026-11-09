# US6 implementation evidence

Status: partially implemented. Portable onboarding, keyboard navigation, semantic snapshots, renderer-local Scene/Project/Game images, and Linux/Windows/macOS provider implementations are present. Native Windows/macOS compilation, screen-reader walkthroughs, and live desktop/platform acceptance remain open.

## T094 projectless startup and adoption (implementation delivered; task dependency remains open)

- Startup restores a valid last workspace first, then accepts a valid project in the invocation directory. If neither is available, it creates an isolated temporary workspace and asset directory and opens the editor's welcome panel instead of failing before the window appears.
- The welcome panel can open a project folder. Cancelling or rejecting a folder leaves the current untitled document active. Build and Run remain unavailable until a real project is adopted.
- Runtime adoption uses the existing `ProjectSession::set_project_roots` path. The startup test covers a manifest-free bootstrap, rejected roots preserving the draft/history, and successful adoption preserving the draft/history.
- Temporary workspace cleanup removes only an unused bootstrap. If the user saves files inside it, the directory is retained so shutdown cannot delete authored work.
- Focused verification: `cmake --build build --target platformator_editor_startup_test platformator_editor -j8`; `ctest --test-dir build -R '^platformator_editor_startup_test$' --output-on-failure` (1/1 passed). This was headless verification; no native welcome-screen walkthrough was performed.
- T094 remains unchecked until prerequisite T092 (host/context test suite) is complete. Target discovery and run-choice onboarding in T095 remain open.

## T095 project target discovery (portable implementation delivered; integration dependencies remain open)

- Run Settings reads visible configure/build presets from `CMakePresets.json` and already-generated CMake File API codemodel replies. It never runs CMake or writes a query into a project.
- Discovery bounds the preset manifest to 1 MiB, File API reads to 4 MiB total, preset lists to 64 entries, build roots to 16, reply directory entries to 1,024, and executable targets to 256. It ignores hidden presets, unresolved macros and build roots outside the project, and accepts only built executable artifacts inside a discovered build directory.
- Run Settings now offers preset, target and executable choices; manual values remain available in an advanced override section. Refreshing discovery is a separate action from refreshing source identity. The existing compiled behavior catalog remains authoritative for automatic Run.
- The project regression covers hidden presets, external build roots, File API executable artifacts and an oversized preset manifest. Focused verification: `cmake --build build --target platformator_editor_workspace_test platformator_editor -j8`; `ctest --test-dir build -R '^platformator_editor_workspace_test$' --output-on-failure` (1/1 passed).
- No desktop onboarding or executable-selection walkthrough was performed. T095 remains unchecked until T094 and T107 prerequisites are complete; no Windows/macOS verification was performed.

## T100 keyboard navigation and action reference

- Enabled ImGui keyboard navigation for the editor context.
- Hierarchy object, component-group, and component controls now carry readable labels; Enter selects focused object and component-group rows.
- Added Ctrl+, to open Editor Preferences and a Help menu action reference generated from the shared editor action descriptors.
- The action reference lists each command, shortcut, and panel scope. Input dispatch rejects text, modal, gesture, application-focus, and host-focus capture.
- Added 100%, 150%, and 200% interface scale choices in Editor Preferences and persist changes through the existing deferred preference writer. Full scale and overflow walkthroughs are still open under T101/T107.

Build and focused regression command:

```sh
cmake --build build --target platformator_editor_workspace_test platformator_editor -j8
ctest --test-dir build -R '^(platformator_editor_workspace_test|platformator_editor_discovery_test|platformator_editor_hierarchy_move_test)$' --output-on-failure
```

Result: build succeeded; 3/3 focused tests passed. The shortcut regression covers descriptor discovery, Ctrl+, routing, and suppression while another host owns focus. No Windows/macOS or screen-reader verification was performed.

## Logical semantic snapshot contract tests (T093)

Expanded `editor/tests/accessibility_test.cpp` to assert accessible roles, meaningful names and values, enabled/disabled state, selected/expanded/focused/error state, parent relationships, focused reveal paths, immutable snapshots held across publication changes, stale-incarnation rejection, bounded queued actions, and owner-thread-only publication/dispatch. Added a 1,000-frame animation semantic case proving a selected frame at index 500 remains addressable in the bounded neighboring-frame window.

```text
cmake --build build --target platformator_editor_accessibility_test -j8
ctest --test-dir build -R '^platformator_editor_accessibility_test$' --output-on-failure
git diff --check
```

Outcome: the logical accessibility suite passed 1/1. This covers the test contract only; AT-SPI, UI Automation, NSAccessibility, native reader checks, and Windows/macOS verification remain open under T102–T107.

## Scaled toolbar wrapping (T101 partial)

The toolbar now chooses compact labels from measured content width, wraps each action group with explicit row positioning, and sizes the reserved toolbar area to the actual wrapped rows. This keeps Build/Run/Stop controls from falling into the Scene dockspace at narrow widths and avoids ImGui's extra line advance when wrapping at high scale.

`platformator_editor_startup_test` verifies all toolbar action bounds and clicks Open Scene at 1280×800/100%, 720×500/100%, 480×320/150%, 1280×800/200%, and 320×280/200%.

```text
cmake --build build --target platformator_editor_startup_test platformator_editor -j8
ctest --test-dir build -R '^platformator_editor_startup_test$' --output-on-failure
```

Outcome at that stage: build succeeded; 1/1 test passed. No live monitor-change or native desktop walkthrough was possible in this headless session; later entries complete portable scale and overflow work, while native checks remain under T107/T145.

The same continuation also renders the toolbar's document/operation status as clipped text and identifies the project and active scene filename in the SDL window title, retaining a textual Modified marker. `platformator_editor_startup_test` uses a deliberately long status in narrow layouts and checks the status hit rectangle stays within the toolbar/viewport. This closes the source omission where `EditorToolbarState::status` and scene identity fields were populated/available but not displayed. Later entries finish the portable toolbar and Project/viewport scaling; live monitor-change acceptance remains under T107/T145.

## Scaled Scene and Project geometry (T101/T242 partial)

The Scene viewport now scales transform/rectangle gizmo drawing and hit radii together with the active UI scale, sizes Scene toolbar controls from their labels, and wraps those controls within the available panel width. Project folder icons/indentation, clipped folder labels, asset tile dimensions/icons/insets, and the folder/content split use the same scale.

```text
cmake --build build --target platformator_editor_startup_test platformator_editor_workspace_test platformator_editor_resources_test platformator_editor_preview_test platformator_editor -j8
ctest --test-dir build -R '^(platformator_editor_startup_test|platformator_editor_workspace_test|platformator_editor_resources_test|platformator_editor_preview_test)$' --output-on-failure
git diff --check
```

Outcome at that stage: build succeeded; 4/4 tests passed. Startup coverage directly checks 100/150/200% toolbar wrapping and hit bounds, while the preview and Project suites retain their existing Scene interaction, Project layout, and resource regressions. The native scale/monitor walkthrough remains unavailable under T107/T145; later entries complete portable T101/T199/T242 implementation.

## Primary panel semantic snapshots (T106 complete; T231 partial)

The main application snapshot includes the current visible Hierarchy rows, Project asset root, Inspector selection summary, Console status/output summary and Clear/Copy actions, Run status and controls, and Validation status with a bounded page of up to 64 current issues. Hierarchy rows use tab/document-scoped IDs, readable object/component labels, selected/focused/expanded state and ImGui bounds. Parent links fall back to the Hierarchy root when virtualization leaves an ancestor offscreen; stale rows are withheld after document revision changes. Validated owner-thread activation and reveal actions select/reveal only IDs in the still-active document. Validation issue activation is tied to the active tab/document generation and latest current validation result; Console and Run controls route through existing owner-thread services.

The main snapshot now also includes Welcome, menu groups/actions, Scene tabs and close/save controls, Project asset tiles and assignment actions, built-in/custom reference fields, and focused reveal paths for Hierarchy, Project, Inspector, Run and Validation. Run Settings records focus for its build, discovery, browse and stop actions. Unchanged producer frames retain the existing immutable snapshot and revision, avoiding provider invalidation for an identical semantic tree.

The focused accessibility, Hierarchy and startup suites previously passed 3/3, and the latest logical accessibility plus Linux AT-SPI bridge tests passed 2/2. These tests cover the semantic model and bridge but do not drive a native screen reader. The portable node publishing/action work in T106 is complete. Windows UI Automation, macOS NSAccessibility, native reader walkthroughs and the multiple-host provider integration remain open under T104/T105/T107/T231.

```text
cmake --build build --target platformator_editor platformator_editor_accessibility_test platformator_editor_hierarchy_move_test platformator_editor_startup_test -j8
ctest --test-dir build -R '^(platformator_editor_accessibility_test|platformator_editor_hierarchy_move_test|platformator_editor_startup_test)$' --output-on-failure
git diff --check
```

Outcome: build succeeded, CTest passed 3/3, and `git diff --check` passed on 2026-11-07.

## Persistent host registry foundation (T092 partial)

Added a portable `WindowHostRegistry` that distinguishes persistent `HostLayoutId` values from monotonic window and renderer generations, caps active hosts at eight, restores a saved layout with fresh native generations, and redocks a closing or failed host's panels to the main host. Renderer recovery preserves the layout identity and invalidates stale window/renderer work. The main accessibility provider now gets its host identity from this registry, and `main_host_layout_id` is defined once in `sessionids.h`.

`platformator_editor_windowhost_test` covers the main-host invariant, ID/generation replacement, panel reassignment, redocking, renderer recovery, duplicate restore rejection, and capacity without ID reuse. The focused Linux windowhost, accessibility, AT-SPI bridge and startup set passed 4/4. This is the registry model only; SDL window/renderer/ImGui ownership, multi-context event routing, focus/IME handling and per-host docking persistence remain open under T092/T096–T099/T187/T230.

## Hierarchy icon and rename geometry scale follow-up T101/T242 (2026-11-07)

Hierarchy object icons, icon offsets, rename spacing, and minimum rename-input width now use the active `FontGlobalScale`, matching the existing Project and Scene geometry. This removes the remaining fixed 14-pixel object icon at 150% and 200% scale.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_startup_test platformator_editor_hierarchy_move_test -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_startup_test|platformator_editor_hierarchy_move_test)$' --output-on-failure
git diff --check
```

Outcome at that stage: build passed and both focused Linux headless suites passed 2/2. Live monitor changes and narrow desktop overflow were still unverified then; follow-up deterministic DPI and Project-label coverage below completes portable T101/T199/T242 implementation. Native desktop walkthroughs remain under T107/T145.

## Per-frame DPI scaling and Project label overflow T101 (2026-11-07)

Extracted effective UI scale resolution into `resolve_editor_ui_scale`, which combines the saved/user-selected scale with the current window's logical-to-pixel dimensions. The editor calls it every frame, so updated pixel dimensions after a monitor transition reapply the style without restarting. Invalid user scales fall back to 100%, invalid/minimized pixel metrics use a 1× DPI fallback, and the effective total stays bounded. The startup regression checks 100%, 150%, and 200% settings across simulated 1×, 1.5×, and 2× host pixel densities, plus invalid and bounded cases.

Project folder and current-location labels remain clipped to their panel bounds at narrow widths and expose their complete label/path in a hover tooltip. Asset tiles already expose their full paths and preview states in the same way. Style spacing/fonts, Scene gizmo drawing/hit radii, and Project folder/tile geometry use the effective `FontGlobalScale`.

```text
cmake --build bin/editor-debug --target platformator_editor_startup_test platformator_editor_resources_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_startup_test|platformator_editor_resources_test)$' --output-on-failure
git diff --check && git diff --cached --check
```

Outcome: build succeeded; 2/2 focused Linux headless suites passed. The session has no reachable X display (`xrandr --query` reports `Can't open display :0.0`), so no physical monitor transition was exercised; per-frame scale recomputation and monitor-density changes are covered with deterministic dimension inputs. Native desktop scale walkthroughs remain part of T107/T145 acceptance.

## Semantic snapshot store and owner-thread actions T102

`AccessibilitySnapshots` publishes validated immutable `shared_ptr<const SemanticSnapshot>` objects keyed by persistent `HostLayoutId` and `WindowHostGeneration`. Native providers can retain an acquired snapshot across callbacks. Provider actions carry host identity, generation, snapshot revision, node ID, and advertised action; enqueue rejects stale, hidden, disabled, or unadvertised targets. A bounded queue is dispatched only on the constructing owner thread, and publication changes or host retirement invalidate queued work before handlers run. The main editor publishes visible panel/Hierarchy semantics and drains actions on its UI owner thread; it retires the host snapshot before ImGui/Runtime teardown.

Focused contract coverage is in `editor/tests/accessibility_test.cpp` and `editor/tests/hierarchy_move_test.cpp`: immutable lifetime across replacement, current/stale action validation, queue bounds, generation retirement, owner-thread enforcement, stable Hierarchy IDs/bounds/reveal, and selection without authored mutations.

```text
cmake --build bin/editor-debug --target platformator_editor_accessibility_test platformator_editor_hierarchy_move_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_accessibility_test|platformator_editor_hierarchy_move_test)$' --output-on-failure
git diff --check && git diff --cached --check
```

Outcome: the existing focused Linux contract suites passed 2/2. Platform UI inspection and teardown walkthroughs remained open at that stage; the Linux provider implementation is recorded below and native desktop acceptance remains in T107.

## Linux AT-SPI provider T103

Added an editor-private `dbus-1` dependency on Linux and an AT-SPI provider that registers the editor root with `org.a11y.atspi.Registry`. Its Accessible, Application, Component, Action, and numeric Value methods read copied semantic data through `AccessibilitySnapshots::acquire`; each callback retains the immutable snapshot for the response. Stable semantic IDs map to bounded D-Bus object paths. Names, descriptions/values, roles, selected/focused/expanded/error states, bounds, and advertised actions are exposed. Action callbacks only enqueue host-generation and snapshot-revision checked work; the editor dispatches it on the owner thread. Provider shutdown joins/drains its D-Bus dispatch thread and disconnects before retiring the host snapshot.

Added a Linux provider lifecycle test for graceful unavailable-registry behavior and repeatable stop, alongside the semantic snapshot and Hierarchy action suites. A successful registry handshake/native screen-reader test could not run here: no session AT-SPI service is installed, and sandbox restrictions prevented `dbus-run-session` from creating its temporary socket. Native AT-SPI inspection remains open under T107.

```text
cmake --build bin/editor-debug --target platformator_editor_accessibility_linux_test platformator_editor_accessibility_test platformator_editor_hierarchy_move_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_accessibility_linux_test|platformator_editor_accessibility_test|platformator_editor_hierarchy_move_test)$' --output-on-failure
git diff --check && git diff --cached --check
```

Outcome: build succeeded and all 3 focused Linux headless suites passed. Windows UI Automation and macOS NSAccessibility implementation/verification remain separate under T104–T107.

## Inspector and Project semantic actions (2026-11-07)

Inspector snapshots now include stable selected-object fields, supported component fields, nested vector fields, and Script behavior items. Reveal actions revalidate current object/component identity, select the Inspector owner, and reveal the matching field. Component and object field groups report selected/focused state when the action is still current.

Project snapshots include the current folder, refresh/status, and only the asset tiles submitted for the visible grid rows, including their screen bounds and asset kind. Activating a tile checks the current project generation and listing before changing Project selection. Compatible asset assignment actions include the explicit object/component target; dispatch recomputes current compatible targets and validates the address before committing one ordinary history command.

Regressions cover Inspector reveal focus and removed components, visible Project tile selection and stale project generations, explicit accessible assignment targets, and the existing Hierarchy click-without-delete contract. Commands:

```text
cmake --build build --target platformator_editor_workspace_test platformator_editor_inspector_interaction_test platformator_editor_resources_test platformator_editor_hierarchy_move_test platformator_editor_accessibility_test platformator_editor -j2
ctest --test-dir build -R '^(platformator_editor_workspace_test|platformator_editor_inspector_interaction_test|platformator_editor_resources_test|platformator_editor_hierarchy_move_test|platformator_editor_accessibility_test)$' --output-on-failure
```

Outcome: the workspace, Inspector, resources, Hierarchy, and logical accessibility suites passed 5/5 in the focused run. The Linux headless semantic model now includes Scene tabs, menus, Welcome, reference states/actions and focused reveal paths. Native UI Automation/NSAccessibility providers and reader walkthroughs remain open under T104/T105/T107/T231.

## Executable picker implementation slice (2026-11-07)

Run Settings now has a Browse executable action backed by the existing asynchronous file-dialog service. The request is scoped to the active document identity; cancellation preserves the current executable, and a late selection cannot be applied to a different active tab. Returned dialog paths and stored executable paths use UTF-8 conversions. Linux/macOS offer an unrestricted file picker; Windows narrows the native filter to `.exe`. Selecting a valid regular file updates the configured path and shows the target/catalog next steps.

The async request suite now covers executable filter validation, request identity, a Unicode executable path, and project-folder cancellation bound to the originating document identity. Focused verification:

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_workspace_test$' --output-on-failure
```

Outcome: the editor and workspace suite built successfully; the workspace suite passed 1/1. No native file-picker walkthrough was available in this session. T186/T229 remain open for live onboarding/run-choice/cancel evidence and their outstanding acceptance dependencies; no Windows/macOS verification is claimed.

The File API discovery regression now uses a Unicode project directory and a Unicode executable filename, while resolving `${sourceDir}` and the artifact path from the generated reply. The Linux Debug editor, workspace and startup targets built successfully; after adding document-scoped project-folder cancellation coverage, `platformator_editor_workspace_test` and `platformator_editor_startup_test` passed 2/2. This covers UTF-8 conversion in preset expansion, File API path decoding and the persisted picker path. It does not replace a native chooser/welcome walkthrough or Windows/macOS verification.

The startup suite now also verifies that a deleted restored project is rejected, a corrupt last-workspace preference is ignored with a message, and projectless adoption still preserves the draft/history. The updated `platformator_editor_startup_test` rebuilt and passed 1/1.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test platformator_editor_startup_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_(workspace|startup)_test$' --output-on-failure --timeout 60
```

## Welcome and Scene-tab semantics T106 (2026-11-07)

The main semantic snapshot now includes the visible Welcome window and bounded Open Project Folder action, plus the Scene-tab group, Create Scene, Save All, each current Scene tab and its close action. Nodes carry current screen bounds where captured, active/modified/source status, focused/selected state and lock state. Actions route to the existing owner-thread project, tab activation and close-prompt paths after validating current tab identities.

The File, Edit, View/Tools, Run and Help menus now have semantic groups and direct owner-thread actions, including current undo/redo, save, panel visibility and run status. These menu actions are represented even when a submenu is closed. The debug editor, logical accessibility suite and Hierarchy interaction suite rebuilt successfully; the focused accessibility, Inspector and Hierarchy suites passed 3/3. These headless tests do not verify native screen-reader interoperability.

Inspector semantics now also report resolved, missing, wrong-kind, invalid and unset states for built-in Texture, Audio and Animation Clip references. Custom Script behavior schemas expose asset/object reference fields with stable IDs, current values, validation help/error state, and actions that select the correct component/behavior, focus Behaviors, and reveal the field. Inspector interaction coverage checks missing texture and dangling object references. The debug editor, Inspector interaction suite and logical accessibility suite rebuilt; the focused Inspector/accessibility run passed 2/2.

Hierarchy keyboard deletion is suppressed during mouse input and for the immediately following frame, preventing a delayed Delete edge from removing the row selected by a click while keeping an explicit Delete key press available. The click regression selects every visible fixture object without changing document data, checks same-frame and following-frame Delete edges, and verifies intentional keyboard deletion.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_hierarchy_move_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_hierarchy_move_test$'
git diff --check && git diff --cached --check
```

Outcome: build succeeded and the focused Hierarchy suite passed 1/1. These remain Linux headless checks. The portable semantic tree and owner-thread actions are integrated; Windows UI Automation/macOS NSAccessibility providers and native reader walkthroughs remain open under T104/T105/T107/T231.

## Semantic focus paths and stable unchanged publications (2026-11-07)

Run Settings now records keyboard focus for its refresh, executable picker, Build, Build & Run, and Stop controls. The main snapshot marks those nodes focused and adds the Run panel reveal path; focused Inspector fields and Validate Scene also add their panel reveal paths. `AccessibilitySnapshots::publish` now reuses the existing immutable snapshot when a newer producer frame has identical nodes and reveal path, retaining the prior revision so unchanged UI frames do not invalidate provider-held actions or trigger a new publication.

The logical regression verifies pointer/revision stability for an unchanged snapshot and that changed content still replaces it and invalidates stale queued actions. The Linux editor, logical accessibility, and AT-SPI bridge targets built; both accessibility tests passed 2/2. This does not replace actual screen-reader or keyboard-focus walkthroughs. The portable semantic publishing work in T106 is complete; provider implementation and native acceptance remain open under T104/T105/T107/T231.

The Console snapshot now exposes its current operation/status, retained-output size, copy/clear controls, repeat-collapse and follow states, search and severity controls, and only the rows currently drawn by the virtualized list. Visible rows carry bounded text, repeat/source help, selection state and screen bounds. Owner-thread actions select rows, cycle severity, copy, clear and return to newest output; row actions captured before a Console revision change are rejected. The semantic Console regression was added to `editor/tests/console_test.cpp`; `platformator_editor_console_test` passed 1/1 after rebuilding the editor.

The Scene viewport now publishes its canvas bounds, current selection, active Move/Rotate/Scale/Rectangle tool, zoom, snap increments and coordinate space. Its owner-thread semantic actions select tools and request Frame All/Frame Selected; Frame Selected revalidates that the current selection still exists. The viewport contract test validates the node tree through `AccessibilitySnapshots` and checks active-tool selection plus stale-selection rejection; `platformator_editor_preview_test` passed 1/1.

Run Settings now publishes discovery status, selected configure/build presets, target and executable, embedded-game/profile/window/overlay state, and a named error/help state when Automatic Run is unavailable because the target does not declare `common-runner-v1`. `platformator_editor_discovery_test` verifies these values and validates their parent tree through `AccessibilitySnapshots`; it passed 1/1. Screen-reader input editing and native reader acceptance remain open.

The responsive toolbar now displays the active project and scene identity in addition to explicit `Saved`/`Modified` and build/run state text, so document state is not communicated by color alone. The identity tooltip shows the full project and scene paths. The toolbar layout accounts for identity and status rows when it wraps, and clips long labels within the available width while retaining their tooltip text. The main semantic snapshot also publishes the project/scene identity and status as named text nodes with current screen bounds and full paths. The headless startup layout regression checks item bounds/clickability and semantic text/path values at 100%, 150%, and 200% scales across 1280px, 720px, 480px, and 320px window widths; it passed on Linux. The loop recalculates UI scale from current window and pixel dimensions each frame, and viewport handles/project tiles already scale their geometry with the active UI scale. The action-dispatch and DPI helper regressions cover focused-host/text/modal/gesture capture and the scale result when logical-to-pixel dimensions change. T199 and T242 are complete for portable implementation and headless verification. Physical monitor movement, IME and native keyboard/scale walkthroughs remain outstanding under T107/T145; they were not performed in this headless environment.

## Animation source-region semantics (2026-11-07)

The active animation frame now exposes accessible Source X, Source Y, Width, and Height pixel fields when its prepared image dimensions are available. The semantic values use the effective source crop, report image-bound ranges, and offer ordinary owner-thread `set_value` actions. Each action revalidates the selected frame and texture path, validates the complete crop against original image dimensions, and commits through the existing lossless asset history path. A missing or unprepared image is exposed as an error state with repair guidance.

`editor/tests/animation_test.cpp` verifies the named fields, pixel bounds, unknown frame-field preservation, exact one-step Undo, and rejection of an out-of-image edit. The focused animation test passed in the Release and ASan/UBSan builds, and the complete selected sanitizer lane passed 65/65. Native reader and image-crop walkthroughs remain open under T107/T145/T231.

## Run Settings choice and toggle actions (2026-11-07)

The Run Settings semantic tree now exposes discovered configure/build presets and built executables as named list items with owner-thread activation actions. Choosing a built executable updates both its target and UTF-8 path. Profile, embedded-game, window, and overlay checkboxes expose toggle actions when enabled; Production disables controls that the visible panel disables. Choice actions are cleared when discovery is refreshed or the project root changes, so queued actions from an old project cannot select a stale target.

`platformator_editor_discovery_test` now covers preset/target choices, selection updates, profile and overlay toggles, disabled controls during active work, stale choice invalidation on project replacement, and snapshot parent validity. Release build and focused test commands:

```text
cmake --build bin/editor-release --target platformator_editor_discovery_test platformator_editor --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 60 -R '^platformator_editor_discovery_test$'
```

Outcome: the Release editor and focused test built; the test passed 1/1. The focused test also passed 1/1 in the existing ASan/UBSan build with LeakSanitizer disabled for the recorded ptrace limitation. This remains logical/headless evidence and does not replace Linux AT-SPI reader acceptance or the skipped Windows/macOS providers and verification.

## AT-SPI numeric value action routing (2026-11-07)

The Linux AT-SPI `Value.SetCurrentValue` bridge used a hyphenated `set-value` action even though semantic controls advertise `set_value`; shared snapshot validation therefore rejected numeric edits. The bridge now sends the advertised action, rejects non-finite/out-of-range values before queueing, and formats doubles with round-trip precision. Snapshot tests verify exact action-name validation and owner-thread delivery.

Release and ASan/UBSan builds of `platformator_editor_accessibility_test` and `platformator_editor_accessibility_linux_test` succeeded; both focused suites passed 2/2 in each configuration. The Linux provider lifecycle test succeeds through its graceful no-registry path in this environment. No active AT-SPI registry or screen-reader interaction was verified; T107/T231 remain open for that evidence.

## Editable text and bounded dimension actions (2026-11-07)

Linux AT-SPI now exposes Text and EditableText for semantic text fields. Text reads use UTF-8 character offsets; whole-value replacements are capped at 4,096 bytes and enter the existing revision-checked owner-thread action queue. Console search accepts the same `set_value` action with its visible 255-byte limit, and Run Settings exposes bounded Game width/height fields through its ordinary preferences path.

The Console, Run Settings, logical accessibility, Linux provider, and Animation suites passed 5/5 in Release and 5/5 in ASan/UBSan. A private D-Bus session with a fake AT-SPI registry exercised Unicode Text offsets, EditableText replacement, numeric Value updates, oversize replacement rejection, and out-of-range rejection through provider dispatch. It caught a mid-codepoint end offset, which is now fixed and covered. No desktop AT-SPI registry or screen-reader walkthrough was available; Windows/macOS provider and walkthrough work remains open.

## Detached host surfaces and panel routing (T092/T096/T097/T099 partial, 2026-11-07)

Added owned SDL window/renderer/ImGui context surfaces for detached hosts. The View menu can detach, dock, and move Hierarchy, Scene, Inspector, Project, Console, Run Settings, Validation, and (when enabled) Game panels. The main Runtime and its authoring document remain shared. Native close and renderer-present failure redock the affected panels. Per-host context settings and geometry are persisted with project preferences, restored at launch, and reset by Reset Layout. Saved positions that overlap no current display are centered as a recovery measure. Project thumbnails and Game frames use renderer-local replicas; Scene uses the bounded transfer described below.

The editor-owned SDL3 backend adaptation is based on the vendored ImGui backend without modifying the vendored source. It routes SDL events to the context registered for the event window, keeps one shared cursor set, assigns one gamepad owner, and transfers IME ownership to the keyboard-focused window. Application shortcut dispatch selects the focused editor context. These paths compile on Linux; interactive focus, IME, clipboard, scale, and monitor behavior still need a real desktop walkthrough.

The host regression covers independent windows/renderers/contexts, per-context settings, event and keyboard isolation, shared IME owner handoff/release through the editor backend, display-event broadcast, registry capacity and generation behavior, missing-display geometry recovery, renderer-creation cleanup/redocking, and context/window unregister during teardown. The workspace lifecycle regression covers preference round trips and Reset Layout redocking.

```text
cmake --build bin/editor-release --target platformator_editor platformator_editor_windowhost_test platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-release -R '^(platformator_editor_windowhost_test|platformator_editor_workspace_test)$' --output-on-failure
```

Outcome: Linux Release build succeeded and the focused CTest set passed 2/2. This is headless SDL/software-renderer evidence; no live X11/Wayland detachment walkthrough was run. T092 logical routing, IME handoff, removed-display recovery, and transfer checks are complete; native clipboard, monitor-removal, and UI-driven walkthrough evidence remains under T107. T096/T097/T098/T099 portable implementation and logical checks are complete. Windows/macOS verification was not performed.

## Detached Scene transfer (2026-11-07)

Scene can now be assigned to a detached host. The main Runtime continues rendering the authoring preview; `SceneTextureReplicas` scales it into a fitting target on the source renderer, reads back only that target, converts through a reusable staging buffer, and uploads a distinct texture for the destination renderer. A successful preview render advances a revision; the transfer is repeated only when that revision or source target changes. The retained transfer target, staging pixels, transient readback allowance, and replica are conservatively planned within the 64 MiB limit. The Scene replica remains alive through the host renderer lifetime and is released before that renderer is destroyed. Project thumbnails already use renderer-local cached uploads. Game now retains a bounded 640×360 replica per renderer and updates them from each received frame, so switching host renderers does not drop the latest frame.

`windowhost_test.cpp` checks that the detached Scene texture is a distinct SDL texture, reads back expected rendered pixel colors, updates the same replica when the source revision changes, and releases the allocations with the destination renderer. It also validates IME ownership transfer and deterministic recovery for a saved host position outside the current display bounds. The host and workspace lifecycle tests passed 2/2 on Linux Release. Physical monitor/IME and UI-driven walkthroughs remain for T107; Windows/macOS verification was not performed.

## Explicit CMake File API target query (T186/T229 implementation partial, 2026-11-07)

Run Settings now registers the CMake `codemodel-v2` File API query when the user explicitly refreshes presets and targets. Opening or changing a project remains read-only. Registration is limited to existing build directories inside the canonical project root; path components that are symlinks or non-directories are rejected. The empty query file tells the next CMake configure to produce the reply consumed by bounded target discovery, and the panel reports that timing. The app does not create a build directory merely by opening Run Settings.

The Project regression covers automatic read-only discovery, explicit query registration, Unicode build paths, external build-root rejection, and a symlink escape where the host supports creating directory symlinks. Debug and ASan/UBSan commands:

```text
cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_discovery_test platformator_editor_startup_test platformator_editor --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_(workspace|discovery|startup)_test$' --output-on-failure
cmake --build bin/editor-asan --target platformator_editor_workspace_test platformator_editor_discovery_test platformator_editor_startup_test platformator_editor --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_(workspace|discovery|startup)_test$' --output-on-failure
```

Outcome: Debug and ASan/UBSan each passed 3/3. These headless results do not replace the independent welcome, native chooser, and Run Settings walkthrough; T186/T229 remain open for that acceptance.

A follow-up `platformator_editor_discovery_test` regression now exercises the `RunSettingsPanel` call path itself: project selection stays read-only, explicit refresh creates the query, and the semantic discovery node reports that it is queued for the next configure. The focused discovery test passed 1/1 in Debug and 1/1 in ASan/UBSan with `ASAN_OPTIONS=detect_leaks=0`.

## Bounded native semantic action metadata (2026-11-07)

The shared accessibility action queue now rejects node IDs over 1,024 bytes and action names over 64 bytes before snapshot scanning. It also catches lock/allocation failures within the provider-facing `noexcept` enqueue boundary and returns rejection instead of terminating the editor. Regression coverage advertises oversized IDs and action names, then verifies both are refused; existing queue/value-size and provider dispatch cases remain covered.

```text
cmake --build bin/editor-debug --target platformator_editor_accessibility_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_accessibility_test$' --output-on-failure
dbus-run-session -- bin/editor-debug/platformator_editor_accessibility_linux_test
cmake --build bin/editor-asan --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_accessibility_test$' --output-on-failure --timeout 120
ASAN_OPTIONS=detect_leaks=0 dbus-run-session -- bin/editor-asan/platformator_editor_accessibility_linux_test
```

The logical model test and private-session Linux provider test passed in Debug and ASan/UBSan. The provider test needed local Unix-socket access, which the ordinary sandbox denied. Debug test SHA-256: `420aca0b4d41025dad19776d65ae66367b0bbd65bdf549b13dfea5757fa9635b`; Debug Linux provider `f47b0d4ddf3641cd69963d7059925df624998c784b6ebbf4b9644d168d9c2c34`; ASan/UBSan logical test `a389dba37c6276ede0d4454807afb927f3950e67d7cbdfc03f4ec4a10c8a802e`; ASan/UBSan Linux provider `08f727586e6aae320e1188f1cb63719c4c50cc627723f011a2ca3251c42d2c09`. Detached-host publication is now implemented and has a focused Linux provider regression below; Windows/macOS providers and native reader walkthroughs remain open.

## Per-host semantic publication and Linux AT-SPI exposure (2026-11-07)

The editor now partitions its immutable accessibility publication by active `HostLayoutId` and generation, routing panels and their descendants according to saved host assignments. Detached partitions receive a named window root, valid in-host parent links, and a host-local focused reveal path. The Linux AT-SPI application root enumerates the main and detached windows; object paths include host identity and generation, actions dispatch against the owning snapshot, and retiring a detached host drains queued actions and invalidates its old object paths. Closing a detached host and editor shutdown retire their snapshots before renderer/context teardown.

The logical accessibility test covers partition parent/focus integrity, per-host publication, action ownership, and retirement. The private-session Linux provider test exercises a detached window and child action through D-Bus, then verifies that the stale path fails after retirement.

```text
cmake --build bin/editor-debug --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor -j4
ctest --test-dir bin/editor-debug -R '^platformator_editor_accessibility_test$' --output-on-failure
dbus-run-session -- bin/editor-debug/platformator_editor_accessibility_linux_test
```

Outcome: the full Linux Debug and ASan/UBSan CTest suites each passed 69/70 tests; the benchmark contract test skipped by configuration. The logical and Linux provider accessibility tests passed in both configurations. ASan/UBSan CTest completed in 597 seconds, including a 482-second package smoke; LeakSanitizer was disabled per the existing container limitation. Debug SHA-256: logical test `90299c6baa61c722c16ff278fc195865f18e3b05c959680e749006d97be5bac9`; Linux provider test `79bb6841423aa0c50521c87345630ae3242a442e7f7b9b20e218c71a5e7633f0`; editor `e004c6806757a186e80d87a30d2f2b6e151c94d23fb487384f23178f099eb524`. ASan/UBSan SHA-256: logical test `078bfbc5c27ebff87798db245419108c6ae462e27bbc04b5b3581fa2fe78e9e8`; Linux provider test `bbcada4a704fcca3ce12b80307485855e4a870dbd6bd5f56c6325fc609e5c6f3`; editor `3bc63eff43606c1638870ea159f03142e3b31cf788c2379f9d37fbd4caad26c7`. This is Linux provider integration evidence, not a screen-reader walkthrough. Native inspector/reader verification remains open, so T188/T231 are still incomplete.

## Windows UI Automation and macOS NSAccessibility providers (T104/T105, 2026-11-08)

Implemented editor-private providers for Windows and macOS using the same immutable, host-and-generation-scoped snapshots as Linux. The Windows fragment provider exposes semantic roles, names, values, help, focus, selection, bounds, and Invoke/Value/Toggle/SelectionItem patterns. The macOS provider exposes snapshot-backed AppKit elements, parent/child relationships, frame/value/focus state, and Press/value actions. Both validate actions against the current published node and enqueue them for owner-thread dispatch; the shared queue wakes the SDL event loop so an accepted action is processed while the editor is idle. Providers start for the main and restored/new detached hosts and retire before their native host is destroyed.

Initial Linux-focused verification after the shared wake-queue change: `platformator_editor_accessibility_test` passed 1/1; the private-session `platformator_editor_accessibility_linux_test` passed with local D-Bus socket access. At the initial wake-queue build, Debug executable SHA-256 was: logical test `9c48eeae49b2f44f700649b2f7bb9f81a910c5b5b315f41b3e628cd090ef10be`; Linux provider test `f2f64a0fb09c8459aaa78b77d61cc064e52f1cf0e4a6880ba5a8bf8f79efde4c`; editor `11c9f4494d966a6981064a325602f6da1caabc489704822ce36a89c6871f244c`.

The Windows and macOS sources were reviewed and formatted but not compiled or run, per the requested platform-verification exclusion. Their provider-facing state methods and action bridges now contain allocation/lock exceptions instead of allowing them to escape `noexcept` boundaries. No native UI Automation inspector, VoiceOver, or screen-reader walkthrough is claimed. T104/T105 implementation is complete; T107, T188, and T231 remain open for their broader native/reader acceptance requirements.
