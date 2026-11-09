# Quickstart: GUI Cleanup Validation

This guide describes the implemented Linux headless checks and the remaining native desktop acceptance work for [plan.md](plan.md). The editor test targets, GUI probe and bounded capture driver exist. A Linux editor install target and embedded default layout are now available; detached SDL window/renderer hosts and cross-host panel movement have Linux headless coverage. Physical two-host workflows, full installed GUI launch, Windows/macOS verification and comparative GUI performance remain open. See the current story evidence in [evidence](evidence/). The audit's historical 6/8 result is not a current passing claim.

## Prerequisites and isolation

Use the proposed lanes in [desktop.md](contracts/desktop.md): Ubuntu 24.04/GCC 13+ (X11 and Wayland), Windows 11/MSVC 2022 x64, macOS 14/Xcode 15+ arm64; CMake 3.28+, Ninja, vendored dependencies and Python 3. Linux accessibility additionally needs libdbus-1 development/runtime support. Run Windows commands from the compiler developer environment and use `python` where `python3` is unavailable. Native adapters and physical platform behavior require actual corresponding machines, not a cross-compile or SDL dummy driver alone.

Run commands from the repository root. Keep builds/fixtures under `bin/` or `build/`, and test preferences/dialog outputs in a separate disposable test directory, never the user's normal editor preferences. The GUI probe injects an isolated environment through the application frame driver. Do not change the user's HOME or copy the repository/build into evidence.

## 1. Establish the current baseline

```sh
git rev-parse HEAD
git status --short
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4
ctest --test-dir bin/editor-debug -L '^editor$' --output-on-failure --timeout 60 -j 4
```

Record exact revision/dirty source identity, compiler/configuration and renderer/driver. The 2026-11-05 audit recorded failures in `platformator_editor_document_test` and `platformator_editor_preview_test`; later fixes and focused results are recorded in [US1](evidence/US1.md). Do not treat that historical 6/8 run as a current passing claim. Run the current affected suites through their final assertions and retain any current failure details in [REGRESSIONS.md](evidence/REGRESSIONS.md) before performance capture; see [editor-workspace.md](contracts/editor-workspace.md).

Do not launch old feature-011 acceptance/resource/timing campaigns. Baseline GUI timing needs a behavior-neutral frame-harness integration pinned separately from performance changes and matched workload/build identities; the selection-only probe is not GUI timing.

## 2. Build and run implemented contract suites

```sh
cmake --preset editor-debug
cmake --build --preset editor-debug --parallel 4 --target platformator_editor platformator_editor_hierarchy_move_test platformator_editor_tabs_test platformator_editor_changes_test platformator_editor_resources_test platformator_editor_template_test platformator_editor_animation_test platformator_editor_playsession_test platformator_editor_asset_watch_integration_test
ctest --test-dir bin/editor-debug -R 'platformator_editor_(hierarchy_move|tabs|changes|resources|template|animation|playsession|asset_watch_integration)_test' --output-on-failure --timeout 60
```

The focused `hierarchy_move` regression verifies that clicking every visible Hierarchy object preserves the scene and history, and that an ImGui Delete state or a coincident SDL mouse-button event cannot delete it. The currently registered suites and their source contracts are listed in [`editor/tests/CMakeLists.txt`](../../editor/tests/CMakeLists.txt). Current evidence and platform limitations are recorded in [US1](evidence/US1.md), [US2](evidence/US2.md), [US5](evidence/US5.md), and [US9](evidence/US9.md). Headless provider doubles do not complete physical desktop or assistive-technology acceptance.

The `editor-debug` preset enables test-only affected-work counters needed by correctness regressions; do not use its timings for performance comparisons. `editor-release` keeps those counters disabled.

## 3. Authoring and real desktop walkthroughs

Launch the implemented editor:

```sh
bin/editor-debug/platformator_editor --project-root . --asset-root assets --no-restore
```

Use small checked-in/generated fixtures: overlapping sprites, camera-only and legacy-camera objects, collider-only triggers, empty markers, missing textures, mixed components/references, a 10,000-object scene and a depth-128 hierarchy. Keep generated fixture assets under `bin/gui-validation/`; retain only compact identities/outcomes in evidence.

1. Select/frame every category and a component; verify matching Hierarchy/Inspector state. Use tool buttons/W/E/R, snap and pointer zoom, parented moves/collider offsets/rectangle sizing, group edits, reparent/reorder and Undo/Redo. Confirm world placement, local serialization and unchanged unrelated payloads. Test direct text, Escape/cancel, modal capture and rapid separate interactions.
2. Open two scenes and an animation asset tab, edit each, switch/restart/close with Save/Discard/Cancel and deliberate external conflicts. Check independent histories/saved state and correct intended-tab Run. Switch projects with a dialog/resource load pending.
3. Search every palette/list/reference picker; F2/Delete/Ctrl+D; hide/lock a background without changing runtime Active or scene dirty state. Assign assets by double-click/Assign, typed path, drag and picker; check ambiguity/type/root failure feedback and explicit reference repair.
4. Fix then reintroduce validation errors and dependencies. Wait no more than the specified three-second settled-change bound, click each diagnostic to reveal its field, use F6, and confirm Run cannot use old successful validation. Select/copy/filter/collapse/Clear Console, reach retained earlier output, open a compiler source location and inspect truncation status.
5. Detach two hosts using explicit controls, move panels among them, redock/tab/hide, close one, restore/reset and remove a monitor. At 100/150/200% scale test narrow/long-label overflow, text/IME, clipboard, keyboard shortcuts and cursor focus. On Wayland verify native creation/redocking without assuming absolute positioning.
6. Use keyboard-only welcome/open-project, tools, hierarchy, Inspector, asset assignment, validation and Run. Inspect roles/names/values/actions with UIA tools on Windows, Accessibility Inspector/VoiceOver on macOS and AT-SPI/Orca on Linux. Record exact successful primary flows and concrete unsupported custom-control limitations.

Each walkthrough records fixture/build/platform, expected/observed result, pass/failure/limitation and relevant requirement IDs. A text report is sufficient; ordinary visual/pixel checks do not retain screenshots by default.

## 4. External asset and recovery walkthrough

Keep an already-loaded folder open. Add an ordinary image, create a nested folder, and move a populated directory under the root; each must appear without another unrelated change. Modify/rewrite a referenced texture, replace it by temp-file rename, and do a notified write whose size/timestamp are restored. Rename/delete a selected asset; ensure reference repair is explicit and unaffected selection/resources/history survive.

During a deliberately slow write, verify last-good display and bounded retry. Inject startup overlap, queue overflow combined with same-metadata changes, out-of-order listing completion, watch exhaustion, global scan truncation, permissions and root replacement. Coverage must become visibly degraded and manual/visible-folder fallback remain available. Check contained/broken/escaping/cyclic symlinks and old events after project switch. Use targeted counters to verify unused asset addition causes zero unrelated Scene/thumbnail reloads. Details and limits: [asset-changes.md](contracts/asset-changes.md) and [rendering-resources.md](contracts/rendering-resources.md).

## 5. Templates, asset authoring, and Game sessions

Copy/reset supported component settings while preserving identities/unknown fields. Save/instantiate a nested `.pttemplate` with self, descendant, component and external bindings; test each duplicate policy, unavailable-schema status and cross-scene external-ID collision requiring explicit assignment. Every completed operation is one Undo and redo retains identical allocated IDs.

Edit Sprite source rectangles visually/numerically, then an `.animset` with mixed explicit-duration/FPS-fallback frames. Scrub/play, save/reopen, load through the ordinary game consumer, and compare frame regions/order/timing/loop. Oversize preview limits must not truncate saved authoring content.

Configure the opt-in shared-runner session for a development build:

```sh
cmake -S . -B bin/editor-session-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPLATFORMATOR_BUILD_EDITOR=ON -DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON \
  -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_INSTALL=OFF
cmake --build bin/editor-session-debug --target platformator_editor platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-session-debug -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
```

Verify optional catalog capabilities and launch through the GUI. Confirm the bounded Game image, Pause works independently of debug-only hotkeys in a Release session build and freezes step counter, each Step advances exactly one current 1/120-second simulation step, and Resume carries no paused debt. Inspect built-in live object values, retire/recreate an object, and verify handle rejection/no authored dirty changes. Hide Game view and assert zero captures; fill both slots and confirm skipped capture rather than blocked simulation. Crash/Stop/switch project with pending messages and confirm cleanup. Repeat with a capability-absent target and external-window fallback. Embedded v1 image/control/inspection does not claim arbitrary gameplay-input forwarding; external mode remains the native-input path.

Source changes invalidate and stop the stale embedded session. Source or asset changes show an explained stale status and offer Build & Restart; the action still passes through the normal saved-scene and current-validation checks. No live asset or arbitrary code reload transaction is implemented, so a runner's live-reload claim does not cause the editor to apply changed files.

## 6. Linux editor install and runtime-only independence

The Linux install rules place the editor in `bin/`, SDL and oneTBB dependencies in `lib/`, the default layout in the executable as compiled data, and optional resources under `share/platformator/editor/`. The executable uses `$ORIGIN/../lib`. The installed `--help` command has been launched from an unrelated working directory; a live installed GUI launch, native dialogs, two hosts and Windows/macOS packaging remain open.

```sh
cmake --preset editor-debug -DPLATFORMATOR_INSTALL=ON
cmake --build bin/editor-debug --parallel 4
cmake --install bin/editor-debug --prefix /path/to/platformator-editor
(cd /tmp && /path/to/platformator-editor/bin/platformator_editor --help)
```

See [US7 evidence](evidence/US7.md) for the concrete Linux prefix and result. Keep the runtime-only consumer check separate from editor validation; it confirms that editor/session dependencies remain optional.

Build the runtime without editor/session integration and verify public consumers independently:

```sh
cmake -S . -B bin/gui-runtime-only -G Ninja -DPLATFORMATOR_BUILD_EDITOR=OFF -DPLATFORMATOR_ENABLE_EDITOR_SESSION=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_INSTALL=ON
cmake --build bin/gui-runtime-only --parallel 4
ctest --test-dir bin/gui-runtime-only -R 'platformator_(package_smoke|asset_root_contract_test|scene_io_contract_test|runtime_step_test)' --output-on-failure --timeout 60
```

Expected: installed engine consumers do not require ImGui, editor/session UI code or native accessibility libraries. Native step/normal runner behavior remains correct with session support OFF.

## 7. Focused GUI evidence, after correctness

`platformator_editor_gui_probe` is built from the same editor core used by the application and supports `idle` and `hover` workloads. Extracting a probe for a baseline must preserve baseline behavior and identities. Measure source/build/OS/CPU/renderer parameters; use equivalent baseline/candidate workloads and timing boundaries. Keep cold preparation separate from warmed interaction and SDL software results do not establish hardware-GPU timings. If an equivalent baseline cannot be reproduced, leave comparative criteria unmet.

The bounded `benchmark/capture_gui.py` driver compares one workload across baseline and candidate probes. It defaults to 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB output and 600 seconds:

```sh
python3 benchmark/capture_gui.py \
  --baseline-probe bin/gui-baseline/platformator_editor_gui_probe \
  --baseline-source-root /path/to/pinned/baseline/source --baseline-label baseline \
  --candidate-probe bin/editor-release/platformator_editor_gui_probe \
  --candidate-source-root "$PWD" --candidate-label candidate \
  --workload hover --project-root "$PWD" --asset-root "$PWD/assets" \
  --output specs/012-gui-cleanup/evidence/gui-pilot
```

Before running, confirm equivalent baseline/candidate source and build identities and that the output path is fresh. Do not reconstruct or claim verification of deleted historical binaries or raw samples. Idle CPU uses a separate bounded two-second wall interval per repetition after settling, with real waits and incoming-event service; do not reinterpret 30 command samples as idle GUI frames. Keep event-to-present input latency, active frame time, wait-inclusive frame time and idle process CPU as separate metrics. Report raw per-repetition samples and variation without overclaiming a three-repetition pilot.

Do not automatically execute all scenarios, all sizes, all workers, or old lifecycle campaigns. For independent scan/decode scaling, choose one targeted case and compare one worker with an available small multiworker count only when the change warrants it. Renderer-specific comparisons must use `benchmark/capture_linear_render.py` with its default exploratory guards; if invoked from a GUI capture, share the remaining session bytes/deadline.

Default session cap is cumulative 50 MiB/600 seconds including child captures, driver/probe logs and samples. Budget/failure stops the process tree, retains partial compact output and prevents automatic retry or replacement sessions. Retain reports, workload/build identities, commands and raw timing samples. Keep builds outside report folders and never retain per-frame object lists, full frames, source archives, executables, libraries or build copies. Shared-memory Game pixels and correctness image checks remain transient memory. Historical missing raw artifacts stay historical observations, not reverified evidence.

## Evidence ledger and completion

For each FR/SC record: exact current source/build identity, fixture/command, platform/backend/worker configuration, expected and observed outcome, pass/failure, and limitation. Keep native desktop and performance evidence distinct from deterministic headless contracts. Review all 52 FRs and 17 success criteria; P3 work remains required. An unavailable platform, unmeasured timing benefit, partial test run, or failed budget remains outstanding and prevents claiming that acceptance complete.
