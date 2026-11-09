# GUI Cleanup Baseline

Captured 2026-11-05 before implementation changes.

## Source and build identity

- Branch: `branch-without-bvh`
- Source revision: `b45bdff923aed012f96e2de1b5554541679636f1`
- The worktree had no unstaged tracked changes. The GUI audit and feature design artifacts were already staged as additions; this report preserves that staging state.
- Reusable build: `bin/editor-debug`, CMake Debug, editor enabled, `/usr/bin/c++`; vendored SDL 3.4.18, SDL_image 3.4.8, SDL_mixer 3.2.4 and SDL_ttf 3.2.2.
- Existing executable: `bin/editor-debug/platformator_editor`, ELF x86-64, Build ID `d5726744f75241d4d5d4d7a06214ee039d43a63c`, SHA-256 `e2ea4ff489c5a59b43064243fc15702208efda64979667d455a780fe5a06d5d1`, 66,386,840 bytes, mtime `2026-10-31 00:38:47 +0100`.
- The executable dynamically loads SDL libraries from `bin/editor-debug`. Its binary timestamp predates this capture; this record does not assert that the executable was built from the captured source revision.
- Editor CTest lanes configure `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and `SDL_RENDER_DRIVER=software`. This identifies the test renderer configuration, not physical-GPU behavior.

## Existing audit result

The repository audit records 6/8 editor suites passing in 2.12 seconds on its prior run. The audit used the following commands:

```sh
cmake --build --preset editor-debug --parallel 4 --target platformator_editor platformator_editor_document_test platformator_editor_catalog_test platformator_editor_workspace_test platformator_editor_startup_test platformator_editor_preview_test platformator_editor_history_test platformator_editor_process_test
ctest --test-dir bin/editor-debug -L editor --output-on-failure --timeout 60 -j 4
```

That is a historical audit observation, not a rerun for this feature. Six C++ suites passed and the capture-budget suite passed. Two C++ suites failed:

- `platformator_editor_document_test`, `editor/tests/document_test.cpp:435`: hierarchy ordering/ownership assertion contains a hardcoded full-row sequence. The audit diagnosed a fixture/implementation contract disagreement; later assertions were not reached.
- `platformator_editor_preview_test`, `editor/tests/viewport_cases.cpp:59`: projection assertion expects a letterboxed `360x180` logical image and `720x540` target, while the implementation fills the `360x250` panel and computes `720x750`. The audit diagnosed a projection-contract disagreement; later assertions were not reached.

These remain open US1 acceptance work. Existing audit notes also say `editor_probe.cpp` measures selection plus a property command rather than the actual GUI frame path, so its results are not GUI performance evidence.

## Capture status and evidence limits

- No tests, benchmark captures, or GUI probe were run for this baseline capture.
- No renderer output, frame images, source archives, copied builds, or per-frame identity lists were retained.
- Physical GPU behavior, Windows/macOS, native dialogs, multi-window, accessibility, and interactive idle/input performance remain unverified.
- The feature's actual-GUI baseline remains open until the behavior-neutral probe is implemented and its source/build/workload identity is frozen.

## Behavior-neutral GUI probe identity

The tick seam and probe were built before application behavior or performance cleanup. The application and probe share the same event, panel, preview, draw, and present callback. The probe isolates preference/temp paths and reports active frame time separately from the cycle including the existing one-millisecond wait. It also prints the runtime SDL video and renderer names when actually run.

- Source revision: `b45bdff923aed012f96e2de1b5554541679636f1` plus working-tree changes. Aggregate SHA-256 over `sha256sum editor/src/editorapp.h editor/src/editorapp.cpp editor/benchmark/gui_probe.cpp editor/CMakeLists.txt`: `2f75ffdf0da0d7b7a8f42965f8c5f7a010dc05ff971b632dfce17afa5fb05fc3`.
- Build: `bin/editor-debug/platformator_editor_gui_probe`, Debug, `/usr/bin/c++`; ELF Build ID `5220690bbd5b5463a87c2d80b1db50724e3b571d`, SHA-256 `cc9cdb026a60809bce2950c6611493f8a089065f5292e569cbd556ad0eeb8ddc`.
- Frozen workload: static `assets/scenes/stacking.scene` (10 objects, SHA-256 `ff97204972864ba7a53f3bd8d3d68d326b050d89de6b23f96fc6a1a86758bdb4`), project root `.`, asset root `assets/`, five warmup frames and 30 measured frames. The three referenced checked-in textures have SHA-256 values `14c9b88be3a9f6b27c61404e438ae6afe5bdb4367bc1c34d67dce7580fe9eac3`, `1f503e1f0b65a643f93fd5a3c60a2181e3cc8fd0a23e6d2d8be6b38235fd915a`, and `ce6b61beb2bc795d9939a368fdcae31c106229ec65f270d77346d7bda9363ebf` for black square, black circle, and red circle respectively.
- Invocation to use for a future guarded capture: `bin/editor-debug/platformator_editor_gui_probe --project-root "$PWD" --asset-root "$PWD/assets" --scene "$PWD/assets/scenes/stacking.scene" --frames 35 --warmup-frames 5 --no-restore`.
- Existing unaffected audit cases for context: catalog, workspace, startup, history, process, and capture-budget suites passed on the audited run. These are correctness observations, not comparable GUI timing cases.
- The probe was compiled but not launched; there are no raw timing samples and no observed renderer identity yet. No benchmark campaign or historical feature-011 campaign was run. The intended renderer identity must be captured and matched when the guarded baseline/candidate comparison is performed.

## Follow-up build note

The frozen probe executable was relinked during subsequent foundation builds after the baseline identity was recorded. Its T006 Build ID and checksum describe the original frozen setup artifact only; no post-change probe identity or timing samples were recorded. The original capture remains an identity record, not a performance result, and no historical campaign was restarted.
