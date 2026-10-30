# Implementation and validation results

## T001–T006: setup and build boundaries

- Dear ImGui `v1.92.9b-docking` resolved to `b48d1afbe8ee8b238e2961dc363a949dd7304e23`; retained source/license hashes are recorded in `vendored/imgui-1.92.9b/PLATFORMATOR_VENDOR.md`.
- `cmake --preset editor-debug` configured on Linux with GCC 16.2.1/CMake 4.4.4. SDL3 reported Dialog enabled, and `posix_spawn_file_actions_addchdir` was found. The configured cache has `PLATFORMATOR_BUILD_EDITOR=ON` and `PLATFORMATOR_BUILD_TESTS=ON`.
- `cmake --build --preset editor-debug --parallel 4 --target platformator_editor` succeeded. It built the four ImGui core translation units and only the SDL3/SDL_Renderer3 backends, followed by the engine, editor core and editor executable. `bin/editor-debug/platformator_editor` exited 0. This bootstrap only creates/destroys an ImGui context; it is not a functioning editor or UI acceptance result.
- `cmake --preset editor-release` configured successfully with `PLATFORMATOR_BUILD_EDITOR=ON`, `PLATFORMATOR_BUILD_TESTS=OFF`, and no `Python3_EXECUTABLE` in its cache. It is the editor-only configuration that avoids test-time Python discovery; no Qt/PySide package is used by the editor target.
- `cmake --preset cleanup-debug` configured with `PLATFORMATOR_BUILD_EDITOR=OFF`; `cmake --build --preset cleanup-debug --parallel 4 --target main` succeeded. The off configuration has no `platformator_editor*` build target.
- CMake keeps `editor/` outside the recursive engine `src/*.cpp` glob. `platformator_imgui` and `platformator_editor_core` are static private build targets; root install rules export/install only the engine and JSON interface target, and do not install ImGui or editor targets. A full installed/relocated consumer check remains T086.
- Commands used:

  ```sh
  cmake --preset editor-debug
  cmake --build --preset editor-debug --parallel 4 --target platformator_editor
  bin/editor-debug/platformator_editor
  cmake --preset editor-release
  cmake --preset cleanup-debug
  cmake --build --preset cleanup-debug --parallel 4 --target main
  ```

No feature test suites existed at this checkpoint, and no benchmark or desktop acceptance run was performed. Existing dependency CMake deprecation warnings from Eigen and oneTBB were observed during configuration; they did not prevent configuration or builds.

## T007–T016: foundational services

- `editor/src/async_requests.h` and `editor/src/filedialogs.cpp` provide generation-bound, one-shot asynchronous results. The queue distinguishes selected paths, cancellation and provider errors, filters superseded requests, rejects stale document generations, and safely ignores completion after shutdown. SDL open/save/folder calls retain their copied filter/default-location storage until callback completion; callbacks copy selected paths or the SDL error and never retain a document, panel, Runtime or service pointer. The bootstrap shell exposes a manual path field after provider failure and feeds the entered path through the same selected-path queue.
- `editor/src/project.cpp` canonicalizes project and asset roots transactionally. oneTBB task-group scans capture only immutable paths/options and shared cancellation/publication state, sort results, cap per-scan file output, bound pending publications, filter stale project generations, and join before service destruction. The workspace cases cover stale roots, cancellation, output bounds and owner teardown.
- `editor/src/editcommands.cpp` applies stable-ID property commands with expected before/after values and an explicit missing-key sentinel. It resolves each address at application time, rejects stale IDs/generations or mismatched preconditions before mutation, updates the value revision, and notifies affected object IDs. Gesture updates use the same command service and can commit a compact initial/final command or restore the initial value.
- `cmake --build --preset editor-debug --parallel 4 --target platformator_editor_workspace_test platformator_editor_process_test platformator_editor_startup_test platformator_editor` succeeded.
- `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(process|workspace|startup)_test$'` passed 3/3: process contracts (2.04 s), workspace/dialog/scan/command contracts (0.01 s), and one-frame Runtime/ImGui startup plus ordered teardown under SDL's dummy video/software renderer (0.08 s).
- `bin/editor-debug/platformator_editor --help` exited 0 before Runtime creation. `bin/editor-debug/platformator_editor --project-root /tmp` and `--ui-scale nan` each exited 1 with the expected validation message. `git diff --check` passed.
- The SDL provider's native dialog window was not invoked in this headless check. Real portal/provider behavior, document lifecycle adoption, and a user-driven close of the full event loop remain for later story integration and desktop review; these checks do not claim those workflows complete.

Commands used:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor_workspace_test platformator_editor_process_test platformator_editor_startup_test platformator_editor
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(process|workspace|startup)_test$'
bin/editor-debug/platformator_editor --help
bin/editor-debug/platformator_editor --project-root /tmp
bin/editor-debug/platformator_editor --ui-scale nan
git diff --check
```

## US2 authoring, metadata, and panels checkpoint (T026–T037)

- `platformator_editor_document_test` passed after adding built-in property edits, file ID zero support, explicit null/empty/missing distinctions, subtree/component/behavior commands, duplicate ID/name/order/offset behavior, exact preservation of self/internal/external/component reference numbers, compatibility rejection and selection repair.
- `platformator_editor_catalog_test` passed macro descriptors for private and zero-field classes; signed/unsigned ranges; vectors, rectangles, arrays, nested optionals, enums, asset/object targets and opaque JSON; factory-only registration; deterministic output; zero behavior construction; no Runtime/SDL initialization; duplicate-schema rejection; shared-runner argument rejection; immutable identity snapshots; stale identity invalidation; malformed/oversized/timeout failures; and unavailable versus declared protocol gating. It also exercised ProcessService-backed separate-stream query success and failed-refresh staleness.
- `platformator_editor_document_test`, `platformator_editor_catalog_test`, `platformator_editor_workspace_test`, and `platformator_editor_startup_test` passed 4/4 with `ctest --test-dir bin/editor-debug -R '^platformator_editor_(document|catalog|workspace|startup)_test$' --output-on-failure`.
- The stock shared runner and rebuilt `examples/mario` returned schema-version 1 JSON to `--editor-catalog`; mixed runtime arguments returned nonzero without stdout. Mario reported six linked behaviors, including `MarioCameraRig`; hashes of its shared `main.cpp` and registration source matched before and after the query. No game scripts were linked into the editor target.
- Commands used included `cmake --build --preset cleanup-debug --parallel 4 --target main`, `cmake -S examples/mario -B bin/mario-catalog ...`, `cmake --build bin/mario-catalog --parallel 4 --target mario_example`, and direct `--editor-catalog` subprocess checks. Builds stayed under `bin/`.
- `HierarchyPanel` renders ordered nested object/component rows, clips visible rows, and routes context actions through structural commands. `InspectorPanel` edits every listed built-in field group through property commands, includes Add Component, and routes Script selection to Behaviors. `BehaviorsPanel` attaches/removes registered behavior types; `BehaviorFieldsPanel` edits recursive typed and observed JSON values while keeping missing declared fields as drafts. Typed reference controls retain and label dangling/wrong-kind values and reject invalid new assignments.
- `BehaviorCatalogQuery` now owns the ProcessService query path and is integrated into the app's explicit Refresh Catalog action. Queries are permitted only for declared `common-runner-v1`; identity fields include canonical project, profile, target, executable, binary metadata and root CMake input metadata. UI refresh runs the configured executable directly with `--editor-catalog`, uses separate stdout/stderr, enforces cumulative 4 MiB and ten-second limits, and never mutates the scene from catalog data. Full Build & Run sequencing and persisted catalog-cache behavior remain in later US5 tasks.
- T038 validation passed 9/9: `platformator_scene_script_roundtrip_test`, `platformator_object_reference_contract_test`, `platformator_scene_io_contract_test`, `platformator_asset_root_contract_test`, `platformator_package_smoke`, and the four editor suites. The package smoke passed in 413.72 seconds, compiling, installing, relocating, and running public consumers including the installed Mario example.
- The script roundtrip fixture now adds a behavior containing only its serialized `type` and verifies nonzero constructor defaults for omitted fields remain intact at runtime load. The document authoring fixture covers both Box and Circle Collider fields. Both focused tests were rebuilt and passed after those additions.
- T039's field-by-field findings and story boundaries are tabulated in `specs/011-add-imgui-editor/parity.md`. These checks do not claim integrated preview/Scene highlighting (T049–T051) or complete history integration (T055–T059).

Commands used for T038:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_scene_script_roundtrip_test platformator_object_reference_contract_test platformator_scene_io_contract_test platformator_asset_root_contract_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(document|catalog|workspace|startup)_test$|^platformator_(scene_script_roundtrip_test|object_reference_contract_test|scene_io_contract_test|asset_root_contract_test|package_smoke)$'
cmake --build --preset editor-debug --parallel 4 --target platformator_scene_script_roundtrip_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_scene_script_roundtrip_test$'
cmake --build --preset editor-debug --parallel 4 --target platformator_editor_document_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_document_test$'
```

## T017–T025: US1 document lifecycle

- The authoritative document suite verifies lossless top-level-array parsing, zero-valued IDs, unfamiliar fields, legacy nested camera aliases, and same-directory atomic saves. A new runtime compatibility case saves a default-camera scene through the editor serializer, loads it in `platformator::Runtime`, saves it through runtime scene I/O, and compares known camera semantics; the editor reopen still retains unfamiliar fields that Runtime does not own.
- Workspace cases cover New/Open/Save/Save As/Validate, explicit startup scene precedence, missing last-scene recovery, picker cancellation, Save/Discard/Cancel, failed replacement save, malformed candidates, and destination preservation. Validate reports field paths, blocks Run for engine-rejected values, preserves imported/editable JSON, and distinguishes structural failure. Additional cases check camera alias conflicts, rigid-body mass/friction/restitution, collider dimensions/offsets/masks, and project-keyed preference recovery/round-trip.
- The app now wires startup restoration, native Open/Save dialogs, manual-path fallback, dirty-state prompts, overwrite/Save As decisions, validation feedback, project-keyed settings/layout persistence, and the default-camera New action. `--help` succeeds before Runtime creation; `--project-root /tmp` exits nonzero with the expected project-root error.
- `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(document|workspace|startup)_test$'` passed 3/3 (0.26 s total). `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_scene_io_contract_test$'` passed 1/1 (0.10 s). The editor document suite includes the explicit runtime load/save compatibility check. `git diff --check` passed.
- Native SDL portal dialogs, a user-driven full editor session, legacy Qt comparison, and desktop layout/focus review were not exercised in the headless test environment. These remain visual/provider checks, not passing acceptance claims.

Commands used:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor_document_test platformator_editor_workspace_test platformator_editor
cmake --build --preset editor-debug --parallel 4 --target platformator_scene_io_contract_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(document|workspace|startup)_test$'
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_scene_io_contract_test$'
bin/editor-debug/platformator_editor --help
bin/editor-debug/platformator_editor --project-root /tmp
git diff --check
```

## US3 Scene preview and viewport checkpoint (T040–T050)

- `platformator_editor_preview_test` passed with in-memory SDL readback. It checks document draw order and alpha overlap, source-rectangle cropping and horizontal flip pixels, parent translation with local scale/rotation, inactive sprites, static animation frame selection, missing-resource error caching and refresh, flattened Sprite-only proxies, and a Runtime without a gameplay camera. The preview test also checks target reuse/resize/zero extent, refuses to replace an active target, restores target/logical-presentation/viewport/clip/scale/color/blend/color-scale state, and verifies authored behavior and animation state are not simulated or changed. No image files were retained.
- The linked `viewport_cases.cpp` checks off-origin/letterboxed image mapping with independent fixture coordinates, unequal 2x/3x framebuffer ratios, pointer-anchored zoom, pan, fit bounds, zero extent, deterministic overlapping pick cycling, missing/inactive pick eligibility, parent-local position and rotated collider-offset conversion, equal-value commit, and cancellation restoring both original values and originally missing keys while publishing changes.
- `platformator_editor_startup_test` now renders one headless Scene panel frame through the same preview, transform, overlay, ImGui and SDL renderer path as the app. The unchanged scene data and orderly ImGui-before-preview-before-Runtime destruction passed under the dummy video/software renderer.
- In the ordinary `editor-debug` build, preview/startup, render geometry, transform hierarchy, runtime-step, and runtime-recreation checks passed. The probe-dependent render-culling readback test skipped as designed, and the counter contract was not applicable with `PLATFORMATOR_ENABLE_WORK_PROBES=OFF`.
- The repository's `cleanup-work` preset configured with `PLATFORMATOR_ENABLE_WORK_PROBES=ON` and `PLATFORMATOR_SANITIZER=none`. Its render-culling geometry, render-culling readback/order, render-culling contract, transform hierarchy, runtime-step, and runtime-recreation tests passed 6/6. The actual readback test used SDL's dummy video and software renderer and completed in 9.51 seconds; its image/order comparisons remained in memory.
- These checks do not include physical-display DPI, GPU backend, sanitizer, or user-driven desktop focus/drag review. `viewport_cases.cpp` supplies non-unit x/y framebuffer ratios as math fixtures; the headless SDL window itself reports the software/dummy backend. Project/asset roots are fixed by the current launch session, so replacing the active root is not yet an available editor workflow.

Commands used:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor_preview_test platformator_editor_startup_test platformator_editor
ctest --test-dir bin/editor-debug -R '^(platformator_editor_preview_test|platformator_editor_startup_test)$' --output-on-failure
cmake --build --preset editor-debug --parallel 4 --target platformator_render_culling_geometry_test platformator_render_culling_contract_test platformator_render_culling_test platformator_transform_hierarchy_test platformator_runtime_step_test platformator_runtime_recreation_test
cmake --preset cleanup-work
cmake --build --preset cleanup-work --parallel 4 --target platformator_render_culling_geometry_test platformator_render_culling_contract_test platformator_render_culling_test platformator_transform_hierarchy_test platformator_runtime_step_test platformator_runtime_recreation_test
ctest --test-dir bin/cleanup-work -R '^(platformator_render_culling_geometry_test|platformator_render_culling_contract_test|platformator_render_culling_test|platformator_transform_hierarchy_test|platformator_runtime_step_test|platformator_runtime_recreation_test)$' --output-on-failure
```

## US4 undo, redo, and saved-state checkpoint (T052–T059)

- Added and registered `platformator_editor_history_test`. It applies a mixed sequence of property, viewport-position gesture, object, component, behavior, numeric-reference and deletion edits; undo restores the exact starting JSON and redo replays the final state. It checks before/after selection restoration and valid selection repair after deletion.
- Gesture cases verify continuous updates become one history entry, separate gestures remain separate, no-op commit creates no entry, and canceling a gesture that materialized a previously missing key removes that key. A deliberately stale property precondition rejects undo without changing the document or history cursor.
- Saved-token cases cover save-mid-history, undo/redo across the saved state, redo-branch discard, an unreachable saved token staying dirty, save completion marking the current token, and document replacement clearing the old history branch.
- Budget cases use an injected 256-byte history limit to verify oldest-entry pruning and retention of one newest oversized command. The untimed affected-payload observation adds a 1 MiB unrelated JSON field, edits a small property, and verifies that retained property history stays below 1 KiB. Property commands retain only before/after field values and address data; structural commands retain only their affected payload subtree. Scene preview receives changed object IDs from the command observer. This is a bounded storage/work-path observation, not an allocator profile or timing benchmark.
- `DocumentWorkspace` now follows history tokens when bound: successful save marks the current token clean, undo after save becomes dirty, and successful New/Open resets history for the adopted document. The app connects selection capture/restoration, Toolbar Undo/Redo, Ctrl+Z/Ctrl+Y, title dirty state, Scene gesture resolution before save/history navigation/replacement, and visible retained-history/budget status.
- Build and CTest commands:

  ```sh
  cmake --build --preset editor-debug --parallel 4 --target platformator_editor platformator_editor_history_test platformator_editor_workspace_test platformator_editor_preview_test platformator_editor_startup_test
  ctest --test-dir bin/editor-debug -L editor --output-on-failure
  ```

- All seven registered editor suites passed (process, document, catalog, workspace, startup, preview and history; 2.47 seconds total). The headless startup/preview suites use SDL's dummy video/software renderer. Native mouse interaction, physical display scaling, and desktop visual inspection were not performed. No benchmark/timing campaign was run.

## US5 project, assets, and owned game pipeline checkpoint (T060–T071)

- Run preferences cover configure/build presets, target, executable, protocol declaration, debug/production, window size/mode, aspect ratio, start-paused and debug draw categories. Only a fresh recognized shared-runner repository or Mario project receives compatible defaults; existing saved declarations and custom projects retain their explicit state. Build remains enabled for `unavailable`; automatic Run requires `common-runner-v1`.
- Project asset browsing requests one bounded immediate-directory scan when a row expands. The scan service caps outstanding tasks, result publications, entries, files and directories, skips symlinks, and workers only return paths. Asset fields accept typed strings, validated drag/drop, and a native filtered dialog; all assignments use the same property-command and asset-root/type validation path.
- Source/CMake identity is refreshed explicitly before Build/Build & Run and by periodic asynchronous scans. Scans exclude generated/vendor trees, include CMake presets and source/header inputs, use bounded entries/publications, and invalidate catalog state when inputs change. No source tree is walked in the frame loop.
- `GamePipeline` snapshots project/asset roots, saved scene JSON/path, build settings, executable, run flags and identity. It owns configure, build, separate-stream catalog query, availability check, launch and Stop. A post-build query fingerprints the produced executable; failures, changed inputs, missing behavior registrations and false declarations do not launch a prior executable or retry. Console command/status/failure data is retained with a 1 MiB cap; catalog stdout/stderr retain their independent 4 MiB and ten-second limits.
- `platformator_editor_process_test` covers the full state sequence, immutable requests, explicit saved scene/asset-root argv, build-only protocol unavailability, build failure, missing behaviors, malformed false declarations, identity invalidation, bounded output and Stop. The custom-entry fixture compares its `main.cpp` byte-for-byte after Build-only and confirms no catalog query/launch. The owned process-group fixture verifies a grandchild is gone after Stop.
- The optional live mode in the same test executable ran the actual shared runner and Mario through `GamePipeline`: configure, build, query the resulting binary, launch the explicit saved scene/root, reach `Running`, Stop and return to `Idle`. The shared runner catalog reported schema version 1 with zero behaviors. Mario reported six registered behavior schemas. The root run used `bin/debug/main`, `assets/scenes/default.scene`, and `assets`; Mario used `bin/mario-catalog/mario_example`, `examples/mario/level1.scene`, and the default source asset root `examples/mario/assets`. Both used SDL dummy video/audio with the software renderer. An initial headless-only runner attempt could not load textures without a video renderer; the dummy video/software invocation passed. A direct Mario smoke launch also passed using CMake's copied `bin/mario-catalog/assets` runtime root.
- Live pipeline CLI snapshots were: root configure `cmake --preset debug`, build `cmake --build --preset debug --target main`; Mario configure `cmake -S examples/mario -B bin/mario-catalog -DCMAKE_BUILD_TYPE=Debug`, build `cmake --build bin/mario-catalog --target mario_example`; query `<executable> --editor-catalog`; launch `<executable> [run flags] --scene <saved scene> --asset-root <selected root>`. Stop was issued through `GamePipeline::stop()` after the operation reached `Running`.
- Focused process/document/workspace suites passed 3/3 after asset-dialog and source-scan changes. The final editor CTest group passed 7/7 (process, document, catalog, workspace, startup, preview and history; 2.54 seconds). The live integration command also passed both actual targets.
- Desktop portal behavior, mouse-driven drag/drop, and visual layout were not exercised. Build/query/run/Stop behavior is covered by the live pipeline and fixtures; no benchmark or timing campaign was run.

Commands used:

```sh
cmake --build --preset debug --parallel 4 --target main
cmake --build bin/mario-catalog --parallel 4 --target mario_example
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software bin/editor-debug/platformator_editor_process_test --live-integrations /home/user/Projects/Platformator
cmake --build --preset editor-debug --parallel 4 --target platformator_editor platformator_editor_process_test platformator_editor_document_test platformator_editor_catalog_test platformator_editor_workspace_test platformator_editor_startup_test platformator_editor_preview_test platformator_editor_history_test
ctest --test-dir bin/editor-debug -L editor --output-on-failure
```

## US6 workspace and desktop review

- Added stable `panel.*` window IDs, a checked-in public-docking seed, View visibility controls, project-keyed layout recovery/reset, dark style from an unscaled base, custom vector toolbar marks, action descriptors and context-aware shortcut routing. The editor requests SDL high-pixel-density mode; the default runtime `WindowSettings::highPixelDensity` remains false. Style scaling resets from the base and factors the current SDL logical/pixel ratio.
- Inspector and behavior fields use aligned label/value rows; Inspector components collapse by stable component ID. Scene framing keeps the legacy mapping (`F` Frame All, `Shift+F` Frame Selected). Scene gestures cancel when the Scene window hides, the app loses focus, the transform has zero size, or a capture/modal invalidates the gesture.
- `platformator_editor_workspace_test` covers stable panel IDs, default visibility, docking-seed read/recovery, corrupt/offscreen layout, reset, action/shortcut descriptors, text/modal/gesture/app/Scene focus capture, repeated 100%/150% style application, default high-density behavior, and unchanged document revisions/history across layout/scale operations.
- Focused workspace/history/preview CTest passed 3/3. The complete native editor CTest group passed 7/7 in 2.57 seconds. `platformator_editor --help` printed the documented CLI. A short SDL dummy-video/audio/software-renderer startup remained running until the external three-second timeout and printed `Renderer Used: software`; that checked initialization only and is not visual acceptance evidence.
- Real-desktop acceptance could not be performed: `xdpyinfo` reported `unable to open display ":0.0"`, and `xdotool getdisplaygeometry` could not create an X display connection. FR-015 visual color/spacing/icon observations, docking/tab interaction, native dialogs, focus behavior, 100%/150% display inspection and actual monitor scaling are therefore **unverified**. No dummy-driver result is counted for those checks. T079 and the desktop acceptance portion of SC-006 remain open for a session with accessible X11/Wayland desktop and dialog provider.

| FR-015 visual criterion | Implementation and automated evidence | Real desktop observation |
| --- | --- | --- |
| Neutral dark surfaces and restrained blue selection/focus | Explicit base palette and focus/navigation colors in `editor/src/style.cpp` | Unverified; display connection unavailable |
| Panel separation, compact spacing and readable labels | Dark panel borders, compact spacing, aligned Inspector/behavior field rows | Unverified; visual contrast and legibility require display review |
| Icons, labels, tooltips and component headers | Original vector toolbar icons, visible action labels/tooltips, collapsible component sections | Unverified; visual alignment requires display review |
| Default dock/tab layout and reachable overflow | Stable panel IDs, checked-in docking seed, dockspace, scrollable panel windows, reset and recovery tests | Unverified; docking/tab drag and overflow interaction require display review |
| 100%/150% and actual monitor scaling | Repeatable base-style tests and SDL window/pixel ratio scaling | Automated style test passed; actual font/display rendering unverified |
| Focus, dialogs, gestures and UI-only scene isolation | Shortcut-capture matrix, focus/minimize/zero-size cancellation, no document/history changes in UI-only tests | Automated routing passed; actual keyboard focus and OS dialogs unverified |

## T083–T085: guarded pilot implementation and pre-capture validation

- Added an optional `platformator_editor_probe` target and `benchmark/capture_editor.py`. One `--session` journal owns both phases, immutable source/harness/probe/workload/host identity, one monotonic and wall-clock deadline, and cumulative bytes across the journal, raw files, diagnostics, and monitored subprocess output. Failed, exhausted, identity-mismatched, or already-attempted phases cannot be resumed; the driver does not delete partial evidence or create replacement sessions.
- The probe exposes compact resource and timing modes. The resource mode observes four fixed scene shapes, Sprite texture-reference counts, scene JSON size, retained property history, a bounded project scan, a long-output child through `ProcessService`/`BoundedConsole`, resident memory and allocation totals. Timing defaults are 3 repetitions, 5 warmup interactions and 30 measured interactions for each of the four shapes. A render comparison is supported only as a monitored subprocess when matching inputs are supplied; no matching render baseline/candidate pair was frozen here, so no renderer campaign is claimed.
- Registered `editor/tests/capture_budget_test.py` as `platformator_editor_capture_budget_test` using CMake's discovered Python 3.12.14 interpreter; it uses only standard-library unittest. The suite covers phase continuation, persistent limits, malformed/failed/exhausted sessions, identity changes and a failing child process. It does not launch a campaign.
- `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(document|history|catalog|process|preview|startup|workspace|capture_budget)_test|platformator_(scene_io_contract|scene_script_roundtrip|object_reference_contract|asset_root_contract|transform_hierarchy|runtime_step|runtime_recreation)_test)$'` passed 15/15 in 3.82 s. This included all seven native editor suites plus the registered Python test and seven runtime/reference contract suites.
- With work probes enabled, `ctest --test-dir bin/cleanup-work --output-on-failure -R '^(platformator_render_culling_geometry_test|platformator_render_culling_contract_test|platformator_render_culling_test|platformator_transform_hierarchy_test|platformator_runtime_step_test|platformator_runtime_recreation_test)$'` passed 6/6 in 9.96 s. Render readback stayed in memory; no images or per-frame identity lists were retained.
- The first `editor-asan` CTest run ended 7/7 tests with LeakSanitizer's fatal ptrace limitation (including children spawned by process/catalog fixtures). Re-running the same focused AddressSanitizer/UndefinedBehaviorSanitizer suites once with `ASAN_OPTIONS=detect_leaks=0` passed 7/7 in 3.58 s. Address/UB checks passed; LeakSanitizer validation remains unavailable on this host.
- Pre-capture validation did not start the resource or timing mode. The desktop display remains unavailable as recorded under US6, and PySide6/PySide2 are unavailable, so real desktop/legacy checks are not represented as passing.

Commands used:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor_process_test platformator_editor_document_test platformator_editor_catalog_test platformator_editor_workspace_test platformator_editor_startup_test platformator_editor_preview_test platformator_editor_history_test platformator_scene_io_contract_test platformator_scene_script_roundtrip_test platformator_object_reference_contract_test platformator_asset_root_contract_test platformator_transform_hierarchy_test platformator_runtime_step_test platformator_runtime_recreation_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(document|history|catalog|process|preview|startup|workspace|capture_budget)_test|platformator_(scene_io_contract|scene_script_roundtrip|object_reference_contract|asset_root_contract|transform_hierarchy|runtime_step|runtime_recreation)_test)$'
cmake --build --preset cleanup-work --parallel 4 --target platformator_render_culling_geometry_test platformator_render_culling_contract_test platformator_render_culling_test platformator_transform_hierarchy_test platformator_runtime_step_test platformator_runtime_recreation_test
ctest --test-dir bin/cleanup-work --output-on-failure -R '^(platformator_render_culling_geometry_test|platformator_render_culling_contract_test|platformator_render_culling_test|platformator_transform_hierarchy_test|platformator_runtime_step_test|platformator_runtime_recreation_test)$'
cmake --preset editor-asan
cmake --build --preset editor-asan --parallel 4 --target platformator_editor_process_test platformator_editor_document_test platformator_editor_catalog_test platformator_editor_workspace_test platformator_editor_startup_test platformator_editor_preview_test platformator_editor_history_test
ctest --test-dir bin/editor-asan --output-on-failure -R '^platformator_editor_(document|history|catalog|process|preview|startup|workspace)_test$'
env ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure -R '^platformator_editor_(document|history|catalog|process|preview|startup|workspace)_test$'
```

Commands:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor platformator_editor_process_test platformator_editor_document_test platformator_editor_catalog_test platformator_editor_startup_test platformator_editor_preview_test platformator_editor_history_test platformator_editor_workspace_test
ctest --test-dir bin/editor-debug -L editor --output-on-failure
bin/editor-debug/platformator_editor --help
timeout 3s env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software bin/editor-debug/platformator_editor --project-root . --no-restore --asset-root assets
xdpyinfo
xdotool getdisplaygeometry
```

## T086: runtime-only and installed-package compatibility

- The existing `debug` preset is editor-off, runner/examples/tests/install-on. Rebuilt `platformator_engine`, `main`, and `mario_example` with `cmake --build --preset debug --parallel 4 --target platformator_engine main mario_example`.
- The default runner passed a five-step headless smoke with the existing default scene and explicit asset root. Mario passed a five-step headless smoke with `examples/mario/level1.scene` and `examples/mario/assets`; both selected SDL's software renderer and exited successfully.
- `nm -C bin/debug/libplatformator.a` reported no `ImGui` or `platformator_editor` symbols. The relocated installed CMake exports contain no editor/ImGui link targets, include paths, or dependency references. The installed public camera header contains only the editor-preview friend forward declaration needed for preview access; no editor header or ImGui API is included by runtime consumers.
- `ctest --test-dir bin/cleanup-debug --output-on-failure -R '^platformator_package_smoke$'` passed in 324.98 s. `tests/package_smoke.cmake` built and installed the engine, relocated the prefix, built/reran installed and relocated consumers, and built/reran the installed Mario example. All generated build products remained beneath `bin/cleanup-debug/package-smoke/`.

Commands used:

```sh
cmake --build --preset debug --parallel 4 --target platformator_engine main mario_example
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software bin/debug/main --smoke-steps 5 --scene /home/user/Projects/Platformator/assets/scenes/default.scene --asset-root /home/user/Projects/Platformator/assets
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software bin/debug/mario_example --smoke-steps 5 --scene /home/user/Projects/Platformator/examples/mario/level1.scene --asset-root /home/user/Projects/Platformator/examples/mario/assets
ctest --test-dir bin/cleanup-debug --output-on-failure -R '^platformator_package_smoke$'
```

## T087: mixed workflow and runtime/parity fixture

The automated mixed workflow and field matrix map to the observed checks below. These checks support the native behavior, but do not substitute for the separate real-desktop sequence in T079.

| Parity row | Executed checks | Observed result | Desktop/legacy limitation |
| --- | --- | --- | --- |
| Scene lifecycle and validation | `platformator_editor_document_test`, `platformator_editor_workspace_test`, `platformator_scene_io_contract_test` | Unknown scene data and supported camera semantics survive editor serialization and Runtime load/save; cancellation, invalid input and failed replacement/save protection pass. | Native dialog and full user-driven lifecycle not exercised. |
| Hierarchy and selection | document/workspace/history suites | Nested create/duplicate/delete, stable IDs/order, selection repair and numeric-reference policy pass. | Mouse-driven hierarchy/Scene synchronization not exercised. |
| Components and Inspector fields | `platformator_editor_document_test`, `platformator_editor_history_test` | GameObject, box/circle collider, rigid-body, Sprite, Animator, Camera, Audio, Script and recursive/reference field mutations are covered; unfamiliar values remain preserved. | Visual alignment and actual field focus remain open under T079. |
| Behaviors and typed references | `platformator_editor_catalog_test`, document/history suites, live pipeline fixture | Exported schemas and registered behavior fields validate; missing/dangling/wrong-kind cases remain repairable. Shared runner/Mario catalogs were queried from the built executables. | Legacy Qt catalog comparison unavailable. |
| Scene view and gestures | `platformator_editor_preview_test`, viewport/history cases | In-memory render/geometry, parent-local movement, overlap picking, pan/zoom, gesture commit/cancel and saved-camera isolation pass. | Physical-display rendering, mouse gestures and focus-loss review unavailable. |
| Undo/redo and dirty state | `platformator_editor_history_test`, `platformator_editor_workspace_test` | Mixed edits restore exact document state; gesture coalescing, branch discard, save token and UI-only no-history behavior pass. | Actual keyboard/mouse routing still needs desktop review. |
| Assets and references | document/workspace/project suites | Root containment, case/path preservation, wrong-type/missing assignment, scan limits and text/drag/dialog request validation pass in service tests. | Interactive drag/drop and native portal dialogs unavailable. |
| Configure/build/run/stop | live `platformator_editor_process_test --live-integrations` | Actual shared `main` and `mario_example` configure/build/catalog/launch/Stop flows passed with explicit scene and asset roots. | Real separate-window observation unavailable. |
| Workspace/actions/preferences | `platformator_editor_workspace_test` | Stable IDs, default layout seed/recovery/reset, shortcut descriptors, text/modal/focus capture and repeatable 100%/150% scale logic pass. | Visual review, docking, native dialogs, and physical monitor scaling remain unverified. |
| Runtime independence | T086 editor-off build and package smoke | Runner/library/Mario build and installed/relocated consumers pass without editor/ImGui linkage. | None for the headless package contract. |
| Large-scene/resource evidence | T083 probe build and identity freeze | Workload and session contract are implemented and frozen. No resource/timing capture was started before these gates. | Render comparison has no matched baseline; editor-model probe does not claim renderer cost. |

Built-in field rows from `parity.md` were exercised through `platformator_editor_document_test` and the history suite:

| Field row | Observed check |
| --- | --- |
| GameObject | Name/tag/active/position/scale/rotation targeted property mutations and history passed. |
| BoxCollider | Dimensions, offset, trigger, collision group/mask and undo/redo passed. |
| CircleCollider | Radius, offset, trigger, collision group/mask and undo/redo passed. |
| Rigidbody | Velocity/force, mass, angular velocity/torque, friction/restitution, body type and gravity passed. |
| Sprite | Texture reference, flip, dimensions, source-rectangle enable and x/y/w/h edits passed. |
| Animator | Current frame, playback speed, playing state and animation reference edits passed; preview remains static. |
| Camera | Width/height edits and legacy nested camera preservation passed. |
| Audio | File reference, gain, loop count and autoplay data edits passed; preview does not autoplay. |
| Script and recursive fields | Behavior add/remove, scalar/nested/list/null values and typed object/component references passed. |
| Unknown component/field | Unfamiliar payload survived unrelated edits and round trips; unsupported entries remain identifiable. |

The executed live command was:

```sh
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software bin/editor-debug/platformator_editor_process_test --live-integrations /home/user/Projects/Platformator
```

It completed with `live shared-runner and Mario configure/build/catalog/launch/Stop paths passed`. The legacy Python/Qt editor could not be started because PySide6/PySide2 are unavailable. The X display connection and native desktop dialog provider are also unavailable; those rows remain unverified, not passing. `platformator_editor_document_test` supplies the built-in scene/runtime semantic round trip; no claim is made that the unavailable legacy editor opened the same fixture.

### Final pilot-preflight hardening

A pre-capture review found that phase identity comparison did not yet enforce the pre-frozen `BASELINE.md` hashes or make identity drift terminal. The driver now checks the exact frozen source/harness/workload/probe/host identity and fixed journal path before resources begin; timing identity drift marks that same journal failed. No capture had started when this was corrected. The harness hash was refreshed to `32458500e03a6369f906cc79b7bd3698110bf62605632864dbfeeaec9c72fa96`; source, workload and Release probe hashes remain the values in `BASELINE.md`.

The standard-library suite now passes 11/11 directly, `ctest --test-dir bin/editor-debug -L editor --output-on-failure` passes all seven native suites plus the Python budget suite (8/8), and a real baseline/session identity preflight passed. This harness-only change did not affect the already-passing runtime/render or ASan/UBSan suites; those remain valid for their source/build identities. No resource or timing phase was run during this hardening.

## T088: guarded resource pilot

Executed `python3 benchmark/capture_editor.py --mode resources --cycles 20 --session specs/011-add-imgui-editor/evidence/editor-pilot.session.json`. The phase completed all 20 cycles successfully in 1.78 seconds. The journal accounts for 22,150 output bytes against the shared 50 MiB limit; no budget or validation failure occurred. Resource timing remains untimed. The compact raw rows are retained at `evidence/editor-pilot.session.capture/resources.raw.json`, and the persistent journal is `evidence/editor-pilot.session.json`.

The four workload rows were stable across cycles: tiny 10 objects/10 texture references/2,671 serialized bytes; 1,000 objects/1,000 references/270,781 bytes; 10,000 objects/10,000 references/2,727,781 bytes; and the 128-deep chain with 128 objects/one reference/19,483 bytes. Retained property history was 287 bytes per workload in every cycle. The bounded scan completed 32 files in three directories without truncation. Each owned process emitted 1,200,000 bytes; the console retained its 1 MiB bound and reported truncation, as expected.

Resident memory was 38,276 KiB in cycle 0; cycles 1–19 ranged from 44,376 to 44,872 KiB (44,784 KiB median), ending at 44,872 KiB. Per-cycle allocation counters stabilized after the first cycle: 1,321,978–1,321,987 calls and 70,607,594–70,608,492 bytes for cycles 1–19, versus 1,321,995 calls/70,608,876 bytes in cycle 0. These process-level observations show a startup step and small later RSS drift; they do not prove allocator peak, GPU texture residency, or absence of leaks. T088 completed without failure; the subsequent timing phase and aggregate accounting are recorded under T089–T090.

## T089–T090: timing capture and analysis

After successful T088, continued the same journal with `python3 benchmark/capture_editor.py --mode timing --session specs/011-add-imgui-editor/evidence/editor-pilot.session.json`. The timing phase completed with the same Release probe/source/harness/workload/host identities. It recorded 360 samples: 30 measured selection-plus-property-edit interactions in each of three repetitions for each of the four workloads, after five warmups per repetition. Undo remains outside the timed interval. The raw samples are retained at `evidence/editor-pilot.session.capture/timing.raw.json`.

| Workload | Median | p95 (nearest rank) | Maximum | Median range across repetitions |
| --- | ---: | ---: | ---: | ---: |
| 10 objects | 1.193 µs | 2.244 µs | 2.936 µs | 1.192–1.202 µs |
| 1,000 objects | 126.981 µs | 158.230 µs | 233.021 µs | 119.146–127.612 µs |
| 10,000 objects | 1.644 ms | 4.323 ms | 4.895 ms | 1.626–1.769 ms |
| 128-deep parent chain | 33.444 µs | 38.733 µs | 51.157 µs | 33.013–33.518 µs |

The 10,000-object workload has a clear long tail: its p95 is about 2.6 times its median and its maximum about 3.0 times its median. The other shapes also have occasional outliers, though their absolute costs are lower. These are measurements of the editor document/selection/command model only, not a rendered frame, full UI event, mouse drag, idle loop or end-to-end user interaction. The timing process does not separately record cold initialization; its samples follow the stated warmups. No legacy GUI baseline is available because PySide is unavailable, and no matched render binaries/backend were frozen, so neither a legacy speedup nor renderer comparison is claimed. This model path is measured synchronously; no worker-count comparison was run and no parallel speedup is claimed.

The completed shared session accounts for 46.08 seconds and 51,352 bytes against the original 600-second/50-MiB budget. Both phases completed without failure or retry. Timing samples report the measured operation boundary; cold application startup, idle cost and per-interaction memory are not measured by this probe. Resource memory/allocation observations are reported under T088 and are not substituted for those missing measurements. At the time of T090 analysis, no source, dependency, build-configuration or harness changes had occurred after capture. T092 and the subsequent convergence work change the candidate source identity; this pilot is now a historical observation and does not certify the converged candidate. Any new capture requires explicit authorization. Missing legacy/render/idle/desktop evidence remains explicitly limited as described above.

## T092: affected-subtree preview reconciliation

Replaced local-change reconciliation's full document-object scan and repeated descendant ancestry checks with traversal rooted at unique changed IDs. Overlapping changed roots are removed by checking their ancestor chain, disjoint roots remain separate, and an explicit stack walks only the affected JSON subtrees. The ancestor transform is resolved once per retained root and carried down the walk. Descendant proxy transforms are published even when the descendant's own cached JSON is unchanged, while unchanged asset state remains cached. Structural/document replacement still uses the full rebuild path. The draw loop remains O(renderable proxies) per Scene frame; its `EFFICIENCY:` comment now states that cost and defers a view-aware index until matched mostly-offscreen measurements justify it.

Debug-only counters and untimed assertions cover a changed leaf, changed ancestor, duplicate/overlapping roots, and disjoint roots. `cmake --build --preset editor-debug --parallel 2 --target platformator_editor_preview_test` succeeded, and `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_preview_test$'` passed. The fixture confirms a leaf visits/updates one proxy, the 19-object ancestor subtree updates its 16 sprites, duplicate roots visit once, and two disjoint roots visit only their combined 29 objects/24 sprites despite more than 60 total document objects. No performance capture was run for this correction.

## T093–T101: convergence implementation status

T093 adds project-folder validation using the existing `CMakeLists.txt` and `assets/` convention, active project/asset roots throughout the editor session, a project-context settings action, Save/Discard/Cancel handling, scan cancellation/join, dialog-service invalidation, and safe-frame Runtime/preview/ImGui recreation with restoration on replacement failure. Workspace tests cover root metadata replacement while preserving an edited document and undo history, plus rejection of an unavailable root. Project-scan tests cover cancel/join, clearing old publications, and reuse. The root-switch UI transition, stale native-provider callback, and unsaved Save/Discard/Cancel sequence have not been driven end to end; T093 remains open.

T094 now uses the stable `panel.scene` identity and migrates legacy `[Window][Scene]` preferences without modifying scene data. The startup suite loads the actual seed, renders actual ImGui windows, checks intended dock IDs, reloads the seed as Reset Layout at 150% scale, and verifies the migrated Scene window docks after real frames. T094 is complete.

T095 skips hidden panel submission, balances `Begin`/`End` only for submitted windows, and cancels hidden Scene gestures. Actual-frame checks cover Hierarchy, Scene, Inspector, Behaviors, Project, Console, Run, Validation, and Settings as hidden and reopened windows. T095 is complete.

T096 wires File/Open Scene, Ctrl+O, and the primary toolbar action through the same request; exposes current roots and loaded scene; reports opening status and cancellation; and opens a manual path prompt directly when the native provider fails. A reproduced SDL error showed that filters were passed as globs (`*.scene`) even though SDL expects extension lists; scene/save and asset filters now use valid extension-list patterns, with regression coverage. The manual path popup now closes via both Cancel and Escape using one `CloseCurrentPopup` path; a headless ImGui frame test sends Escape and confirms the popup stack is cleared. The workspace retains its old document on invalid adoption, and service/workspace tests accept a path with spaces and Unicode. Native-provider selection/cancel/failure and the whole menu/toolbar/shortcut/unsaved flow have not been exercised together; T096 remains open.

The Mario Run screen exposed a separate false positive: `examples/mario/level1.scene` contains 24 objects and 92 components, including Animator and Audio components with intentionally empty asset references. The engine serializers skip loading for empty Sprite, Animator, and Audio paths, matching the legacy editor's validation behavior. Validation now permits an empty string while still blocking a missing/non-string field and any non-empty unresolved path. The focused workspace validation test checks all three cases and loads the actual Mario scene with `examples/mario/assets`; it passes without a Run-blocking issue. The existing Mario executable also passed a five-step software-renderer smoke with that scene and asset root. This verifies the scene/runtime path, but does not replace a full desktop Build & Run interaction check.

T097 separates menus from the responsive toolbar and keeps Open Scene, Save, Build, Build and Run, and Stop visible. The actual toolbar component is rendered and bounds-checked at 1280x800, 720x500, and 480x320 at 150% scale; synthetic SDL mouse events activate Open Scene at all three settings. T097 is complete.

T098 routes F/Shift+F framing in the Scene frame and maps Shift+F5 to Stop independently of the normal idle-operation shortcut guard. Dispatcher checks cover Stop, framing, text/modal ownership, focus loss and gestures. The live application operation-state route and physical key delivery have not been driven through a full editor session, so T098 remains open.

T099 has broader actual-frame coverage for seeded docking, visibility, toolbar bounds/activation, and Scene preview composition, in addition to workspace, action, dialog-service and root-model tests. The full production application frame, native dialogs, root replacement decisions, busy-operation cancellation and desktop edit/undo/save/run sequence remain unverified. The host's desktop display and legacy PySide editor are unavailable; T099 remains open.

T100's probe implementation now adopts each workload through `DocumentWorkspace`, builds live preview proxies and an SDL render target through `Runtime`/`EditorPreview`, records unique texture wrappers and their live reference counts, representative edit/undo history, and post-release counts, and drives the selected `main` game through the owned `GamePipeline` to verified Run and Stop. It declares one warmup cycle and checkpoints each of the 20 measured resource cycles to a guarded progress file; the capture driver validates live and released resource counts and preserves partial checkpoints on failure. The historical baseline and journal remain untouched. The separate candidate identity freeze in `evidence/BASELINE-converged.md` was invalidated by the subsequent GUI cleanup; no new Release build or capture was started, so T100 remains open pending a fresh identity freeze and explicit capture authorization.

Previously prepared resource-capture command (not executed; refresh the source/build identity freeze before using it):

```sh
python3 benchmark/capture_editor.py --mode resources --cycles 20 \
  --probe bin/editor-release/platformator_editor_probe \
  --build-label editor-release --expected-configuration Release \
  --baseline specs/011-add-imgui-editor/evidence/BASELINE-converged.md \
  --session specs/011-add-imgui-editor/evidence/editor-pilot-converged.session.json \
  --max-output-mib 50 --max-seconds 600
```

T101 remains open. The timing mode still measures document-model edits and does not record cold application startup, warmed idle/full GUI frames, or production selection/edit/drag events. No timing capture was started. Do not infer GUI responsiveness or leak acceptance from the historical model-only observations.

After the focused validation above, toolbar cleanup removed the redundant New/More controls and moved Validate Scene into Run; hierarchy cleanup added distinct object/component icons, deeper component indentation, and collapsed component-only branches initially. The editor and startup-test targets compile, but the test suites and desktop visual checks were not rerun after these UI changes. The prior size/activation results therefore describe the preceding toolbar version. The Release probe identity freeze was marked invalid; no benchmark build or capture was started.

Focused validation for this implementation:

```sh
cmake --build --preset editor-debug --parallel 2 --target platformator_editor platformator_editor_workspace_test platformator_editor_startup_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(workspace|startup)_test$'
```

The build succeeded and both focused CTest suites passed (2/2); after adding the wider visibility and toolbar activation assertions, `platformator_editor_startup_test` passed again. No benchmark capture or diagnostic image was produced.
