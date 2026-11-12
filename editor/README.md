# Platformator Editor

The optional editor is a native C++20/SDL3 application built with the engine. It edits the same `.scene` data used by the runner; runtime-only consumers do not link ImGui or editor code.

## Build and launch

The editor is currently supported on Linux. From the repository root:

```sh
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4 --target platformator_editor
bin/editor-debug/platformator_editor --project-root /path/to/project --asset-root /path/to/assets
```

The project root must contain `CMakeLists.txt`; the selected asset root must already exist. If `--project-root` is omitted, the editor restores the last opened workspace; if `--asset-root` is omitted, it uses `<project-root>/assets`. The workspace's last scene is opened with it. Relative CLI paths resolve against the invocation directory. `--scene FILE` overrides the restored scene, `--no-restore` skips scene restoration, and `--ui-scale 1.5` starts at 150% style scale. Run `platformator_editor --help` for the complete CLI.

To install the Linux editor with its SDK runtime libraries and resources:

```sh
cmake --preset editor-debug -DPLATFORMATOR_INSTALL=ON
cmake --build bin/editor-debug --parallel 4
cmake --install bin/editor-debug --prefix /path/to/platformator-editor
```

The installed editor resolves SDL and oneTBB from the prefix with a relative RUNPATH. Its default docking layout is compiled into the executable; the optional copy under `share/platformator/editor` is also installed. The installed `--help` command was verified from an unrelated working directory. A live GUI launch and Windows/macOS packaging remain unverified.

## Workspace

Hierarchy, Scene, Inspector, Project and Assets, and Console use stable IDs independent of their visible titles. Drag their tabs to rearrange them. Build and Run Settings and Validation open as tabs beside Console in the bottom-right dock; opening either from View focuses its tab. View exposes visibility toggles, Reset Layout, frame commands, and Fullscreen. Layout, panel visibility, run settings, editor window size/fullscreen state, scale, and last adopted scene are stored separately from scene files and keyed by the canonical project root. The last workspace path is stored globally so launching from another directory still restores that workspace.

The editor uses a neutral dark charcoal theme (`#282828` base with 1px input frame borders), compact aligned property controls, collapsible component sections, keyboard focus styling, and original vector toolbar icons. High-DPI scalable TrueType typography (e.g., DejaVuSans / Inter) is loaded with fallback to the built-in font. Numeric fields display two decimal places. Game objects use a distinct Hierarchy icon. The Hierarchy tree displays GameObjects exclusively—component rows are omitted from the tree to eliminate row clutter and parenting ambiguity. Selecting any GameObject in the Hierarchy displays all of its attached components in the Inspector. Dragging an object in the Hierarchy cleanly reparents it under the targeted GameObject (or drags to empty space to unparent to root). The Hierarchy header includes a `+` creation preset menu (*Create Empty*, *Create Empty Child*, *2D Sprite*, *Camera*, *Audio Object*), and each row features right-aligned inline gutter toggles for visibility (`EyeOpen`/`EyeClosed`) and scene picking lock (`LockOpen`/`LockClosed`).

At the top of the Inspector, a streamlined GameObject header displays the Active checkbox, GameObject icon, and editable Name input on row 1, followed by Tag on row 2, and Object Template below Tag. The Transform component features a compact layout: Position (X and Y side-by-side with colored axis tags) and Scale (X and Y side-by-side) are organized into dual-column rows, and Rotation is displayed in degrees on a single row. Collider `Offset` and Rigidbody `Velocity` and `Force` also use compact horizontal dual-column vector rows. Dragging horizontally across numeric property labels smoothly scrubs the value (`Shift` for 10× speed, `Alt` for 0.1× precision, `Ctrl` for snapping increments), committing a single atomic undo transaction upon mouse release. When multiple GameObjects are selected, common properties display `" — "` placeholders when values differ and edit simultaneously in one undo step. Component headers use non-collapsing vector menus (`⋮`), accessible via both a trailing button and right-clicking anywhere on the header, providing *Reset*, *Copy Settings*, *Paste Settings*, and *Remove Component* (with lossless Undo restoration).

Project and Assets provides a two-column browser featuring a resizable left folder tree, top clickable path breadcrumbs, bottom-right thumbnail scale slider (switching between compact 1-line list view at `0.0f` and scalable grid tiles in `(0.0f, 1.0f]`), and full right-click context menus (*Create* $\rightarrow$ *Folder*, *Behavior Script*, *Scene*, *Object Template*; *Open*; *Reveal in File Manager*; *Rename* `F2`; *Duplicate* `Ctrl+D`; *Delete* `Delete`). Full scans are capped at 50,000 files and 100,000 entries; targeted directory listings are capped at 10,000 files, 2,000 directories and 20,000 entries, skipping virtual environments (`.venv`), spec directories, and hidden dot-directories. Assets can be dragged directly onto the Scene viewport canvas or Hierarchy rows to instantiate Sprite, Camera, Audio, or Template GameObjects.

The Scene panel features a compact single-row tool and view overlay bar using procedural icon buttons for Hand (`Q`), Move (`W`), Rotate (`E`), Scale (`R`), and Rect tools, alongside Grid, Snap, Step, Zoom, and Frame controls. Left-clicking an object in the Hierarchy focuses Scene shortcut routing; pressing `F` immediately frames the selected object, while double-clicking a Hierarchy row frames it in the Scene view. Middle-click and right-click drag pan the Scene camera across all tool modes. When in Move, Rotate, or Scale modes, left-click dragging on empty canvas space initiates marquee box-selection, selecting all enclosed visible and unlocked GameObjects on release (`Shift`/`Ctrl` for additive selection).

The main menu bar is organized into standard categories: `File | Edit | Assets | GameObject | Component | Window | Help`. The main application toolbar features centered Play, Pause, and Step buttons with visual Play Mode emerald tinting when active.

The Console panel features three independent right-aligned toggle badges for `Info (N)`, `Warning (N)`, and `Error (N)` with live message counts, filtering the log list independently. Selecting a row displays its complete details and source link in the lower detail pane, and double-clicking any source-mapped log row opens the file in the system default editor.

Multiple scene documents and animation assets can be open in tabs with independent selection, history, and dirty state. Multi-object selection and group edits are supported. Panels can be assigned to detached SDL window/renderer hosts and moved between hosts; registry, lifecycle, and renderer-local behavior have Linux headless coverage. Physical two-host, monitor-transition, and cross-window input walkthroughs remain unverified. The menu bar and responsive toolbar occupy separate rows. Long status text is clipped in the toolbar and remains available in its tooltip and the Build and Run Settings and Validation panels. Save is disabled while the active scene is clean.

Use **File → Open Scene**, the **Open Scene** toolbar button, or **Ctrl+O** for the same scene-opening flow. The native picker starts at the active project root. A cancelled picker leaves the current scene alone. If the platform dialog provider fails, enter the scene path in the file-dialog prompt; spaces and Unicode characters are accepted. The candidate is checked before adoption, and an invalid path leaves the previous document and its edits active. Opening a scene never changes the project or asset roots.

Use **File → Open Project Folder** to select another project. The folder must contain `CMakeLists.txt` and `assets/`. Save, Discard, and Cancel resolve dirty work before the change. Replacement cancels and joins old scans, discards stale dialog results, reloads that project's preferences, and recreates the preview Runtime at a frame boundary. The open document and its undo history are retained. A failed or cancelled change keeps the previous project context usable.

Duplicate operations allocate new object/component IDs but preserve serialized numeric references to their existing targets, including targets in the duplicated subtree. These compatibility choices are deliberate and are recorded in the [parity inventory](../specs/011-add-imgui-editor/parity.md).

Build and Run Settings discover configure/build presets, built executable targets, and executable artifacts from an existing CMake File API reply. Select a discovered executable or browse to one; advanced manual overrides remain available. The panel also configures runtime window options and debug overlays. The Production profile uses Release settings, disables debug tools, benchmark/work-probe instrumentation, tests, and unrelated Platformator targets, while keeping the selected target enabled. Runtime overlay and start-paused options are disabled in that profile. Scene collider components remain part of the game and continue to affect physics; only their debug outlines are omitted. Run launches a separate game process. The editor stops only processes it owns.

## Behaviors and catalog boundary

Behavior choices and typed fields are shown in the Inspector's Script component and come from the selected executable's `--editor-catalog` response. The Inspector's single **Add Component** menu adds built-in components and attaches catalog behaviors; dragging a behavior header still works. Asset and scene-reference controls identify their expected target type and display the selected asset or scene target. Stock targets using the shared `platformator::run(argc, argv, defaultScene)` runner can use the common-runner protocol after rebuilding; game-source edits are not required. Custom entry points remain editable and can be built, but automatic catalog query and Build and Run stay unavailable until the target explicitly adopts the supported protocol. The editor does not infer protocol support from source text.

## Game panel and process ownership

Embedded Game view is opt-in: configure with `-DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON` and use a compatible shared-runner target. The child process advertises its session capability; unsupported targets retain the external game window path. Embedded capture uses two bounded 640×360 RGBA8 slots and requests frames only while the Game view is visible, up to 15 captures per second. Pause, Resume, one-step, read-only built-in inspection and Build & Restart do not edit the authored scene. Embedded v1 does not forward arbitrary gameplay mouse, keyboard or controller input; use the external game window for native gameplay input. The editor owns the foreground child it launches and stops/reaps that process tree before releasing session resources. It does not claim ownership of unrelated or daemonized processes.

Linux is the only exercised editor platform. Semantic accessibility nodes and owner-thread actions have headless coverage, but a physical screen-reader walkthrough has not been completed. Windows UI Automation and macOS Accessibility provider sources are authored but have not been compiled or verified on their native platforms; keyboard navigation alone is not a screen-reader support claim.

## Scene and asset paths

Scene files remain ordinary JSON arrays and unknown fields are preserved. Scene opening paths are separate from asset paths. Nonempty asset references use the engine's `assets/...` form beneath the selected asset root; path escape, missing-file, and wrong-type assignments are rejected. Scene data does not contain editor layout or run preferences.

## Dependencies and platform scope

The editor uses the vendored Dear ImGui 1.92.9b implementation and SDL3 renderer/platform backends; the ImGui MIT license is in `vendored/imgui-1.92.9b/LICENSE.txt`. It adds no raster icon or font package. Engine dependencies and their licenses remain as listed with their vendored sources. Linux/GCC is the exercised platform; other SDL platforms and actual desktop-provider behavior need their own verification.

For build, test, desktop review and evidence commands, see [the GUI cleanup quickstart](../specs/012-gui-cleanup/quickstart.md). Native tests do not replace visual review of monitor DPI, detached-host workflows, text focus, or OS file dialogs. The legacy Python/Qt interface is not part of this repository checkout; it can coexist with the editor executable as a separate tool and its results should be compared semantically through shared scene files.
