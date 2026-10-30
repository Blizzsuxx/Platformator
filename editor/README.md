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

## Workspace

Hierarchy, Scene, Inspector, Project and Assets, and Console use stable IDs independent of their visible titles. Drag their tabs to rearrange them. View exposes visibility toggles, Reset Layout, frame commands, and Fullscreen. Layout, panel visibility, run settings, editor window size/fullscreen state, scale, and last adopted scene are stored separately from scene files and keyed by the canonical project root. The last workspace path is stored globally so launching from another directory still restores that workspace.

The editor uses a neutral dark theme, compact aligned property controls, collapsible component sections, keyboard focus styling, and original vector toolbar icons. Numeric fields display two decimal places. Game objects use a distinct Hierarchy icon, and each supported component type has a different icon. Component rows are indented beneath their owner and visible by default; collapse an object to hide its components and children. Project and Assets uses the real directories under the active asset root: select a folder in the tree to browse it, or double-click a folder in its contents to open it. In `assets/behaviors`, only behavior headers (`.h`) are shown; drag one into the Inspector to attach it to the selected GameObject. Other files can be dragged onto compatible asset fields. The editor polls the active asset tree every three seconds and refreshes the browser and Scene preview when files change; trees exceeding the scan bound report that automatic refresh may be incomplete. The Scene panel is a static editor preview: it does not instantiate script behaviors or simulate gameplay. Select an object in Scene or the Hierarchy, then use the Move, Rotate, and Scale tools or their `W`, `E`, and `R` shortcuts. Drag a gizmo handle to edit the selected object's transform; Move has X/Y axes, Rotate has a ring, and Scale has X/Y handles. In Move mode, dragging the object itself also moves it. `F` frames all and `Shift+F` frames the selection; `Ctrl` with `+` or `-` zooms the view. `Ctrl+B` builds, `F5` builds and runs, and `Shift+F5` stops the owned process. Standard scene commands are in File/Edit and the toolbar.

The workspace supports one scene and one current selection per editor window. Panels dock and tab inside the main window; detached OS windows, multiple scene tabs, and multi-selection are deferred. The menu bar and responsive toolbar occupy separate rows. The toolbar contains only Open Scene, Save, Build, Build and Run, and Stop; New, Undo, Redo, validation, and project-folder selection remain in File/Edit/Run. Long status text is clipped in the toolbar and remains available in its tooltip and the Validation/Console panels.

Use **File → Open Scene**, the **Open Scene** toolbar button, or **Ctrl+O** for the same scene-opening flow. The native picker starts at the active project root. A cancelled picker leaves the current scene alone. If the platform dialog provider fails, enter the scene path in the file-dialog prompt; spaces and Unicode characters are accepted. The candidate is checked before adoption, and an invalid path leaves the previous document and its edits active. Opening a scene never changes the project or asset roots.

Use **File → Open Project Folder** to select another project. The folder must contain `CMakeLists.txt` and `assets/`. Save, Discard, and Cancel resolve dirty work before the change. Replacement cancels and joins old scans, discards stale dialog results, reloads that project's preferences, and recreates the preview Runtime at a frame boundary. The open document and its undo history are retained. A failed or cancelled change keeps the previous project context usable.

Duplicate operations allocate new object/component IDs but preserve serialized numeric references to their existing targets, including targets in the duplicated subtree. These compatibility choices are deliberate and are recorded in the [parity inventory](../specs/011-add-imgui-editor/parity.md).

Build and Run Settings select configure/build presets or a direct CMake build directory, target, executable, runtime window options, and debug overlays. The Production profile uses Release settings, disables debug tools, benchmark/work-probe instrumentation, tests, and unrelated Platformator targets, while keeping the selected target enabled. Runtime overlay and start-paused options are disabled in that profile. Scene collider components remain part of the game and continue to affect physics; only their debug outlines are omitted. Run launches a separate game process. The editor stops only processes it owns.

## Behaviors and catalog boundary

Behavior choices and typed fields are shown in the Inspector's Script component and come from the selected executable's `--editor-catalog` response. Asset and scene-reference controls identify their expected target type and display the selected asset or scene target. Stock targets using the shared `platformator::run(argc, argv, defaultScene)` runner can use the common-runner protocol after rebuilding; game-source edits are not required. Custom entry points remain editable and can be built, but automatic catalog query and Build and Run stay unavailable until the target explicitly adopts the supported protocol. The editor does not infer protocol support from source text.

## Scene and asset paths

Scene files remain ordinary JSON arrays and unknown fields are preserved. Scene opening paths are separate from asset paths. Nonempty asset references use the engine's `assets/...` form beneath the selected asset root; path escape, missing-file, and wrong-type assignments are rejected. Scene data does not contain editor layout or run preferences.

## Dependencies and platform scope

The editor uses the vendored Dear ImGui 1.92.9b implementation and SDL3 renderer/platform backends; the ImGui MIT license is in `vendored/imgui-1.92.9b/LICENSE.txt`. It adds no raster icon or font package. Engine dependencies and their licenses remain as listed with their vendored sources. Linux/GCC is the exercised platform; other SDL platforms and actual desktop-provider behavior need their own verification.

For build, test, desktop review and evidence commands, see [the feature quickstart](../specs/011-add-imgui-editor/quickstart.md). Native tests do not replace visual review of monitor DPI, window docking, text focus, or OS file dialogs. The legacy Python/Qt interface is not part of this repository checkout; it can coexist with the editor executable as a separate tool and its results should be compared semantically through shared scene files.
