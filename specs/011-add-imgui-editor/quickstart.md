# Validation Guide: Dear ImGui Editor

**Status**: Implementation commands and acceptance guide. Native tests cover headless workspace, action routing and scaling logic; the desktop-only checks below remain independent evidence.

## Prerequisites and setup

Use the existing Linux C++20/CMake/Ninja and SDL system prerequisites. The checkout must contain the recorded bundled Dear ImGui revision. No Qt/PySide/Python editor package or network access is required for a normal editor build. Existing repository development tests may require Python 3; legacy parity tests additionally require the optional legacy development environment.

Run commands from the repository root. Keep all builds in `bin/` and all timing reports free of build products. Dear ImGui is vendored at `vendored/imgui-1.92.9b`; no network fetch is needed.

```sh
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4 --target platformator_editor
bin/editor-debug/platformator_editor --project-root . --no-restore --asset-root assets
```

Expected: dark docked Hierarchy/Scene/Inspector/Project/Console, starter camera, usable menu/toolbar, and no gameplay callback/physics/autoplay. Behavior fields appear in the Inspector; there is no separate Behaviors panel. For an editor-only build without development-test Python discovery, configure with `-DPLATFORMATOR_BUILD_TESTS=OFF`.

Use Project Settings to select configure/build preset `debug` or `production`, game target (`main` for stock scenes, `mario_example` for Mario), executable path, and matching asset root. Build output paths derive from that chosen build, not the editor's own executable. The generated Mario output asset root contains both copied engine/example assets; use that root with the example scene.

## Automated contract suites

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor_document_test platformator_editor_history_test platformator_editor_catalog_test platformator_editor_process_test platformator_editor_preview_test platformator_editor_startup_test platformator_editor_workspace_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(document|history|catalog|process|preview|startup|workspace)_test$'
```

Expected:

- Document: lossless array/unknown-field roundtrip, structural validation, stable IDs/order/reference semantics, field constraints, paths with spaces/Unicode, atomic-save failure, import repair and cancellation.
- History: all mutation families, missing versus materialized keys, saved token/redo branch, continuous versus separate gestures, Escape/focus loss, selection repair, bounded history/pruning.
- Catalog: macro/private/zero-field/container/reference traits, factory-only registration, no constructors/Runtime/SDL during export, deterministic/schema-checked output, observed versus declared fields, stale identity and missing-default behavior.
- Process: real lightweight fixture processes rather than long builds, argv/cwd fidelity, merged/large output, stage failure, no stale executable launch, catalog timeout/size limit, repeated Stop, grandchild cleanup/reaping, close/cancel, bad executable.
- Preview: corresponding in-memory images and submission order, geometry/active/source/flip/animator cases, framebuffer mapping, complete renderer-state restoration, no main-camera mutation or simulation, missing resources, zero-size and repeated teardown.
- Workspace: preference/default-layout state, startup precedence, dialog-result generation/lifetime, action availability, focus routing, 100%/150% style scaling. Real OS/dialog/docking operation still needs desktop review.

The capture driver and standard-library Python test are implemented and registered with the discovered Python 3 interpreter as `platformator_editor_capture_budget_test`. The test uses unittest only, not pytest/Qt. Run it with the native suites and again before either capture phase:

```sh
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_capture_budget_test$'
```

Expected: cumulative time/size limits, phase continuation, matching identities, stopped/failed sessions and refusal to reset budgets are checked with small fixture processes and reduced test budgets. This is a mandatory additional gate; the seven-native-suite command above does not run it.

Reuse the existing test registration's dummy video/audio and software renderer for appropriate tests. Do not use a dummy-driver pass as evidence for real fonts, portals, monitor DPI or docking. Run selected runtime tests after building their targets:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_scene_io_contract_test platformator_scene_script_roundtrip_test platformator_object_reference_contract_test platformator_asset_root_contract_test platformator_transform_hierarchy_test platformator_runtime_step_test platformator_runtime_recreation_test
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_(scene_io_contract|scene_script_roundtrip|object_reference_contract|asset_root_contract|transform_hierarchy|runtime_step|runtime_recreation)_test$'
```

Build/run the existing render-culling/geometry/contract tests in their supported work-probe configuration when required; do not repeat the previously documented all-target work-probe dependency failure. Validate normal Camera/SDLWindow output after extracting the shared draw helper. Run applicable one-/multiworker fixtures; no editor physics parallelism claim follows from these tests.

## Native game metadata and launch

After configuring/building the selected game target with the updated headers:

```sh
bin/debug/main --editor-catalog
bin/debug/mario_example --editor-catalog
```

The example executable location follows the root CMake runtime output directory shown; verify the selected build's actual output. Each command must emit one schema-versioned JSON document and exit without opening SDL or constructing behaviors. The stock runner may have no registered types. The Mario query must contain linked Mario types. These shared-runner baseline targets gain support by rebuild without game-source edits. Project settings default known bundled entries to `catalogProtocol=common-runner-v1`; other entries default to `unavailable` until explicitly declared compatible. An undeclared custom-main fixture must remain editable/buildable with unchanged source, but automatic query/Run must be disabled without executing it. A falsely declared protocol fixture must fail query verification without starting gameplay. Do not infer registration from source text or rewrite the game to add support.

For runtime cross-check, save a built-in-only fixture with valid resources from the new editor and launch:

```sh
bin/debug/main --scene /absolute/path/to/editor-fixture.scene --asset-root /absolute/path/to/fixture-assets
```

Also open/save that fixture with the legacy editor, if available, and compare semantic data rather than formatting. An unavailable legacy test environment must be recorded as unverified, not a passed cross-editor result. Runtime/JSON comparisons and existing fixtures remain required.

## Desktop acceptance sequence

1. Launch fresh. Confirm Hierarchy left, Scene center, Inspector right, and Project/Assets and Console below. Confirm there is no Behaviors tab and behavior fields remain inside the Inspector. Confirm actual Scene docking, neutral dark surfaces, compact spacing, aligned fields, collapsible component headers, vector toolbar icons/tooltips, Add Component, separate menus/toolbar and readable status against [contracts/editor.md](contracts/editor.md). This is a visual review, not a pixel-exact Unity comparison.
2. Open an existing scene separately from changing the project: test File/Open Scene, the Open Scene toolbar control and Ctrl+O. Exercise native picker selection, cancellation, and provider failure followed by manual entry of a path containing spaces and Unicode. Confirm the active project/asset roots do not change, invalid paths report the offending path, and the previous document/history remain usable. With no authored scene loaded, confirm the Open Scene and New Scene entry is visible.
3. Use File/Open Project Folder on a second fixture containing `CMakeLists.txt` and `assets/`. Test Save, Discard and Cancel with dirty work; reject a missing/invalid root; verify active roots and project preferences update only after adoption; confirm the authored document, selection and undo history survive, old scans/dialog callbacks cannot update the new context, and build/catalog/asset paths use the active roots. Attempt replacement during build, catalog query and Run to confirm it is locked.
4. Inspect toolbar control bounds and activation at 1280x800, a smaller window, and 100%/150% UI scale. Confirm Open Scene, Save, Build, Build and Run, and Stop remain visible; New, Undo/Redo, and project-folder selection remain reachable through File/Edit/Run; long status text does not push actions offscreen and remains available from its tooltip or Validation/Console. Reset Layout at normal and 150% scale, then verify the actual Scene window attaches to the seeded Scene node.
5. Open a fixture with every supported component, unknown data and behavior references. Select objects/components from Hierarchy and Scene; verify Inspector synchronization. Add a child, duplicate a subtree, delete/undo it, and inspect numeric-reference duplication behavior.
6. Edit every field group in [parity.md](parity.md), including nested/list/null fields. Assign valid/invalid typed references and assets through text, dialog and drag/drop. Check wrong-kind, missing and root-escape feedback without silent saved-data replacement.
7. Pan/zoom/frame; cycle overlapping selections; drag a parented object and collider offset. Verify exact one-gesture Undo, separate gesture history and Escape restoration. Type ordinary letters in fields without triggering scene framing/shortcuts; test focus loss and hidden Scene gesture cancellation.
8. Save, make more edits, undo to saved state and redo. Cancel New/Open/Exit and failed save-before-run. Verify previous destination contents and current edits survive failures. Change the file externally and verify overwrite/Save As handling.
9. Select the correct game target/root/options, Build & Run, observe separate game window/output, then Stop. Induce configure/build/catalog/launch errors; verify no stale launch, usable controls and preserved document/history. Closing during work must offer Stop/Cancel.
10. Hide/move/tab panels through View and drag docking tabs, restart, restore/reset; verify scene dirty/history is unaffected. Test 100% and `--ui-scale 1.5`, then move between displays with different system scale factors. Check off-origin Scene rectangles and minimize/zero-size. Confirm `F` frames all, `Shift+F` frames selection, `Ctrl++`/`Ctrl+-`, scene File/Edit shortcuts, `Ctrl+B`, `F5` and `Shift+F5`; typing in a text field must not invoke them.
11. Run the 20-cycle resource mode below while observing object/texture/history/task/process counts and warmed retained memory. Wait for owned tasks/processes to finish before comparing. Distinguish allocator high-water capacity from continuing live-state growth. Do not run the benchmark probe directly without its monitor.

Record each parity row's result and any unsupported environment. Linux desktop verification is the initial delivery gate; other OS behavior is not assumed from successful compilation.

Record a specific observation for each FR-015 criterion: surface palette, restrained selection/focus color, panel separation, compact spacing, readable labels, icons/tooltips, dock/tab behavior, Inspector alignment/headers, and reachable overflow. Record the two UI scales and the actual monitor scale factor separately. Mark any unavailable desktop provider as unverified; dummy-driver tests are not substitutes.

## Runtime-only and installed-consumer independence

```sh
cmake --preset cleanup-debug -DPLATFORMATOR_BUILD_EDITOR=OFF
cmake --build --preset cleanup-debug --parallel 4 --target main
```

Use the repository's existing package-smoke command/test for the install/relocation consumer after building required dependencies. Inspect exported targets: no editor/ImGui dependency or include path. A runtime-only build should not compile GUI sources. Run the existing small headless fixture with explicit scene/root; verify catalog additions do not change ordinary argument parsing or gameplay startup.

## Sanitizers

Build the focused editor tests under the planned `editor-asan` preset and run them once with the same fixtures. Respect the host's documented LeakSanitizer/ptrace limitation; if leak detection cannot run, state that leak validation is unavailable and distinguish it from address/UB checks. Use a supported TSan setup for callback/task publication only if available; do not infer a clean audio/desktop TSan baseline from headless checks. Stop and diagnose failures instead of rebuilding/retrying automatically.

## Focused measurement, only after correctness passes

Add a compact interaction capture/probe as implementation work; do not turn this guide into a broad acceptance campaign. The planned CLI below becomes runnable only after T083 implements it. Both phases share one persistent session with the original wall-clock deadline, cumulative output accounting, immutable source/build/workload identities and a 600-second/50-MiB total budget. Timing defaults are 3 independent repetitions, 5 warmup and 30 measured frames. Pass the native, budget/preflight and applicable desktop checks before capture; freeze pilot inputs in T084.

```sh
python3 benchmark/capture_editor.py --mode resources --cycles 20 --session specs/011-add-imgui-editor/evidence/editor-pilot.session.json
```

Only after resource success, continue the same session without resetting its budget:

```sh
python3 benchmark/capture_editor.py --mode timing --session specs/011-add-imgui-editor/evidence/editor-pilot.session.json
```

The resource phase is an untimed correctness/allocation observation, but its process lifetime/output are still monitored. A failed, exhausted, mismatched or already completed session cannot resume. Stop and preserve the partial result; do not create a replacement session, retry, increase limits or collect timings after resource failure automatically.

The implemented model probe times selection plus one `position.x` property edit on tiny, 1,000-object and 10,000-object scenes plus one 128-deep parent chain; Undo is immediately applied outside the timing interval. Continuous drag correctness is covered by the Scene/history suites, but mouse/drag latency is not part of this probe. Long output is a bounded untimed resource observation. A mostly-offscreen versus all-visible render control is available only through a genuinely matched `capture_linear_render.py` comparison; do not infer or claim that comparison when matching binaries/backend identities are unavailable. Choose a focused subset rather than every combination. Use current legacy equivalent interactions where available; identify differences in rendering/timing boundaries. Report cold initialization separately, the actual timed operation boundary, interaction latency distribution, affected work, allocation/retained-state observations, and worker configuration.

For actual rendering comparisons use `benchmark/capture_linear_render.py` with its unchanged defaults and matched workloads. If an editor-specific interaction driver is used, it must enforce equivalent guards itself. Keep instrumentation out of timing builds. Retain a small report, workload/source/build/backend identities, commands, independent raw timing/resource samples; no builds, libraries, executables, source copies, full images or per-frame identity traces in reports. Stop on first budget/failure, retain the partial report, and diagnose; do not retry or expand automatically.

## Final-revision acceptance

T090 may investigate/simplify/revert implementation, but any resulting source/dependency/build-configuration/harness change invalidates affected proof. Reopen matching story checks and T085–T089 as applicable; rebuild and rerun relevant correctness checks against final code. Existing captures remain historical for their exact identities. Another resource/timing capture requires explicit authorization and cannot automatically restart a failed session. Until required final-revision evidence exists, leave the affected acceptance criteria and feature completion open. T091 must verify these identities/gates; documentation-only edits do not require unrelated tests.
