# US7 implementation evidence

## POSIX native file identity and atomic writes (2026-11-06)

T080 and T082 are complete for the POSIX adapter. `file_posix.cpp` uses platform-specific nanosecond timestamp fields (`st_mtim`/`st_ctim` on Linux and `st_mtimespec`/`st_ctimespec` on macOS), create-new same-directory staging files, destination permission preservation, staged-file flush, a late observed-identity check, atomic replacement or create-if-absent linking, and directory-flush durability reporting. The API distinguishes a pre-commit failure, an observed conflict, a completed write, and a committed write whose directory durability could not be confirmed.

`nativefile_test.cpp` exercises native identity for missing and present files; Unicode directory/file names containing spaces; preserved POSIX permission bits; injected staging, file-flush, replacement, and directory-flush failures; a competing edit after staging; and a create-if-absent race. Pre-commit failures preserve the destination and remove staging files. A late conflict preserves the external edit. A directory-flush failure reports a warning with `destinationReplaced=true` and retains the committed content. Internal test hooks make these failure windows deterministic.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_nativefile_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_nativefile_test$' --output-on-failure
git diff --check
```

Outcome: the Linux native file suite passed 1/1 after the final adapter change, and `git diff --check` passed. The macOS timestamp branch is implemented but was not built or run on a macOS host. This evidence does not cover Windows, installed/relocated launches, process ownership, IPC, packaging, or the remaining US7 platform lanes; T083–T091 remain open.

## POSIX inherited session channels (T086 complete)

Added a nonblocking local command pipe, event pipe, and bounded shared mapping in `sessionchannel_posix.cpp`. Parent copies of the child ends are close-on-exec and reserved above fd 5; spawn actions map the ordered endpoints to child fds 3/4/5. Shared memory is created with `shm_open` and immediately unlinked. The child marks 3/4/5 close-on-exec before it can start a later game process. The neutral interface documents pair ownership and the caller's drain-before-unmap boundary; channel cancel/close are idempotent and distinguish cancellation from normal closure.

`nativeprocess_test.cpp` now verifies invalid mapping sizes, unique safe source descriptors, close-on-exec flags, unlinked shared-memory identity, spawn mapping and bidirectional pipe traffic, shared-memory visibility, EOF/EPIPE behavior, idempotent cancellation/close/unmap, and that descriptors 3/4/5 are absent after a subsequent exec.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_nativeprocess_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_nativeprocess_test$' --output-on-failure
git diff --check
```

Outcome: the Linux native process/channel suite passed 1/1. macOS was not built or run. At the time this section was recorded, T087's Windows adapter and T088 source selection were still open; their later implementation status is recorded below. Cross-platform native verification remains open; no Windows or macOS result is claimed.

## POSIX session launch descriptor collision (2026-11-07)

When a PlaySession has inherited channels, POSIX process output pipes are now moved above reserved child descriptors 3/4/5. This keeps the `fork` fallback from closing freshly mapped session channels while it closes its stdout/stderr pipe descriptors. The native process regression temporarily closes parent stdin/stdout/stderr, forces the fallback path, creates channels and launches a child with separate log pipes; the child still exchanges a command, event and mapped frame data, while stdout and stderr remain separate. The standard descriptors are restored by an RAII guard even when the case fails.

```text
cmake --build bin/editor-release --target platformator_editor_nativeprocess_test platformator_editor_process_fixture platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^(platformator_editor_nativeprocess_test|platformator_editor_playsession_test)$'
cmake --build bin/editor-asan --target platformator_editor_nativeprocess_test platformator_editor_process_fixture platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^(platformator_editor_nativeprocess_test|platformator_editor_playsession_test)$'
git diff --check && git diff --cached --check
```

The process, native process and PlaySession suites passed 3/3 in Release and 3/3 in ASan/UBSan. Release test SHA-256: `5315aef44cdf5a5824309d87c5a84d57f188d67471aff6437cfc3f82661fe5f4` (`process`), `27117ca0565fd9f2e218406876a41a2c7f4a1a7a263d8ca98d8d11a5da2ca82c` (`nativeprocess`), `cfe010a1d53e22df13c02f1b403569668f5cc9b966e7d84373b50db98767911b` (`playsession`). ASan/UBSan test SHA-256: `3e28a899e7fd9349b0e946c2c3c90a4c58b36c2238587aa1ac1b4f73edffd7cd` (`process`), `ab7bb8aa9661661d7af92f826461b348a3c45db2cb778957039bdf01784930a0` (`nativeprocess`), `62556ff91bc3d6706e49185428e4fcc8f3c2ef65e8f4df9ddb1f915609cba793` (`playsession`). This is Linux evidence only; Windows channel implementation and native Windows/macOS verification remain open under T184/T227.

## Windows native process implementation (T081/T084)

Drafted `process_windows.cpp` with strict UTF-8-to-wide argv conversion and Windows CRT quoting, a `STARTUPINFOEXW` handle whitelist for standard I/O, suspended process creation, Job Object assignment before resume, kill-on-close ownership, and termination/reaping on setup failure. Pipeline path arguments now use UTF-8 encoding so Unicode executable, scene, asset, and project paths reach the adapter losslessly. Added Windows-only setup-failure hooks, a Windows fixture branch, and `nativeprocess_windows_test.cpp` cases for Unicode/space argv and working directories, unlisted inheritable-handle exclusion, failed whitelist setup, failed Job assignment without resume, descendant cleanup, unrelated-process survival, and bounded Stop.

The Windows process adapter and Windows-only tests implement wide argv construction, explicit inherited-handle whitelisting, suspended process creation, Job assignment before resume, and cleanup after setup failure. The task to provide those sources and cases is complete. This Linux host has `clang-cl` but no Windows SDK headers or MinGW toolchain, so the adapter and tests were not compiled or run; no Windows result is claimed. Native verification remains open under T091/T145.

## POSIX process ownership and shutdown (T085 complete; T081 coverage complete)

Added `nativeprocess_test.cpp` coverage for foreground grandchild group ownership and cleanup, survival of an unrelated process running the same fixture executable, and an intentionally detached `setsid` child. The detached child is always held by a scoped cleanup guard and explicitly killed by the test. The POSIX adapter now exposes its containment limit, and the running-game Console/status reports that daemonized descendants escape process-group ownership and need explicit target lifecycle registration. The existing process suite additionally checks the two-second SIGTERM-to-SIGKILL escalation bound and argv containing spaces and Unicode.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_process_test platformator_editor_nativeprocess_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_(nativeprocess|process)_test$' --output-on-failure
git diff --check
```

Outcome: both Linux process suites passed 2/2 after the ownership disclosure assertion. T085's POSIX adapter retains configure-detected spawn/chdir actions, nonblocking group signaling, direct-child reaping, and the tested two-second SIGTERM-to-SIGKILL bound; its run status now discloses daemon escape and lifecycle registration requirements. T081's Windows Job assignment and inherited-handle cases are authored in the Windows-specific suite but not built or run on this Linux host; no Windows result is claimed. The channel implementation status was updated later in this evidence file.

## Native file adapters and injected failure coverage (2026-11-07)

The POSIX and Windows adapters implement handle/native identity, same-directory staged writes and flush, optimistic initial/late conflict checks, atomic create/replace, prior-content recovery, metadata preservation, and committed durability warnings. `nativefile_test.cpp` includes injected staging, flush, replacement, late conflict, create-race, and Unicode-path cases, with Windows attribute checks behind the Windows build guard.

```text
cmake --build bin/editor-release --target platformator_editor_nativefile_test -j2
ctest --test-dir bin/editor-release -R '^platformator_editor_nativefile_test$' --output-on-failure
```

Outcome: the POSIX native file suite passed 1/1 on Linux Release. The Windows adapter and its guarded test cases were not compiled or run because Windows verification was explicitly skipped; no Windows result is claimed. The native implementation and Linux test tasks T083/T182/T225 are complete, while platform integration and verification remain under T088/T091/T228.

## Linux watcher asynchronous startup and shared-budget slice (T162/T205 partial)

The Linux watcher now returns from `start()` after scheduling discovery on its worker thread. Discovery is cancellable and retains the existing 4,096-watch, 100,000-entry, and five-second bounds. After installing the initial tree it emits `RootAvailable` followed by `Overflow`, forcing reconciliation for changes that overlapped discovery. A bounded discovery failure reports `NotificationsUnavailable`; parent-root watching remains available for recovery. Stop joins the worker before callback state is released, and the reported watch count is atomic. `ProjectSession` passes one shared `ProjectWatchBudget` to its asset and source watchers. Linux, Windows, and macOS watcher sources now reserve and release against that budget, including Windows root rearm and macOS stream rearm.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_asset_watch_integration_test$' --output-on-failure
git diff --check
```

Outcome: the Linux watcher integration suite passed 1/1, including asynchronous readiness, watch-cap failure, two watcher instances sharing a three-watch budget, immediate cancellation, zero remaining watches, and no callbacks after stop. The shared budget source changes for Windows and macOS were not compiled or run. T162 remains open for their native lifecycle/root/overflow checks; T205 remains open for native platform verification and end-to-end degraded-status acceptance. No Windows or macOS result is claimed.

## Native process ownership test coverage T081 (2026-11-07)

The Linux native process suite covers foreground grandchild ownership and reaping, survival of an unrelated same-executable process, the documented and safely cleaned `setsid` escape, and inherited session-channel restrictions. The Windows-specific fixture and `nativeprocess_windows_test.cpp` also contain failure-first cases for inheritable-handle exclusion, failed handle-list setup, failed Job assignment before resume, descendant cleanup, and unrelated-process survival. The Windows cases are registered only on Windows and were not built or run here, as requested; this closes the task to add the cases while leaving Windows execution unverified under T084/T091.

```text
cmake --build build --target platformator_editor_nativeprocess_test platformator_editor_process_fixture -j8
ctest --test-dir build -R '^platformator_editor_nativeprocess_test$' --output-on-failure
```

Outcome: the Linux native process suite passed 1/1 on 2026-11-07. No Windows or macOS verification was performed.

## Linux editor install and embedded default layout (2026-11-07)

The editor now compiles `editor/resources/default-layout.ini` into a generated header and uses that embedded string at startup, removing the runtime dependency on the checkout path. Linux installation adds the editor executable, optional editor resources, and third-party notices/licenses. SDL and oneTBB runtime libraries use the SDK install rules and the editor's `$ORIGIN/../lib` RUNPATH.

```text
cmake --preset editor-debug -DPLATFORMATOR_INSTALL=ON
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_startup_test$' --output-on-failure
cmake --install bin/editor-debug --prefix /tmp/platformator-editor-install-20261107
cmake -E chdir /tmp/platformator-editor-elsewhere-20261107 /tmp/platformator-editor-install-20261107/bin/platformator_editor --help
ldd /tmp/platformator-editor-install-20261107/bin/platformator_editor
```

Outcome: the startup fixture passed 1/1 and verifies that the compiled layout equals the checked-in seed. Installation completed after building the full default target set; the first targeted build had omitted oneTBB allocator targets, so its install attempt correctly failed until those install dependencies were built. From an unrelated working directory, the installed executable printed help successfully, and `ldd` resolved SDL3 and oneTBB from the install prefix via `$ORIGIN/../lib`. The installed `share/platformator/editor/default-layout.ini` and license notices are present. No live window was launched because this host has no accessible X display. Windows/macOS packaging and native launch checks remain open; this is Linux packaging evidence, not full T089/T091 acceptance.

## Linux executable-relative optional layout lookup (T089 partial, 2026-11-07)

The editor now resolves the optional installed layout relative to `SDL_GetBasePath()` using the configured `../${CMAKE_INSTALL_DATADIR}/platformator/editor/default-layout.ini` path. Lookup canonicalizes the resource, constrains it to the install prefix, and rejects files above 1 MiB; a missing, unreadable, empty, out-of-prefix, or oversized resource falls back to the compiled layout. This keeps installed launch independent of the source checkout while allowing a bounded layout override.

`startup_test.cpp` covers the installed-prefix path with spaces, optional override precedence, embedded fallback when the file is absent, traversal rejection, absolute-path rejection, and the size cap.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_startup_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_startup_test$' --output-on-failure
```

Outcome: the Linux Debug editor and startup test built; the focused startup test passed 1/1. This does not verify installed runtime launch with a custom override or Windows/macOS resource layouts and dependency staging; T089/T091/T185/T228 remain open.

The same current-source startup test passed under the existing Debug AddressSanitizer/UBSan build with LeakSanitizer disabled:

```text
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_startup_test$' --output-on-failure
```

Outcome: 1/1 passed. The test covers the bounded executable-relative optional resource lookup and fallback cases; this is not an installed-bundle or native-platform result.

## Windows inherited session channel implementation (T087/T184 implementation tasks checked; Windows verification skipped)

Added `sessionchannel_windows.cpp` with two local overlapped named pipes and an unnamed, size-bounded shared mapping. Child endpoint handles are explicitly inheritable and are passed through the existing `STARTUPINFOEXW` handle whitelist; parent copies close immediately after successful process creation. The pipe adapter keeps pending read/write storage alive, uses an internal fixed read buffer, reports nonblocking progress, and cancels/drains overlapped I/O before closing its event/pipe handles. Mapping close unmaps once and then closes its mapping handle. The Windows-only native process fixture now contains a round-trip case for command bytes, event bytes, mapped frame memory, child EOF, and repeated close/cancel/unmap; invalid mapping sizes fail before native allocation.

`editor/CMakeLists.txt` now selects the Windows process, file, watcher, and session channel sources by platform and retains the POSIX adapters on Linux/macOS. The root Linux-only editor guard remains because these platform adapters have not been compiled, and the macOS accessibility provider/desktop acceptance is still incomplete. The Windows test registration is in `editor/tests/CMakeLists.txt`, but this Linux host has no Windows SDK and Windows verification was explicitly skipped. No Windows build or execution result is claimed. T088/T091/T185/T228 remain open for the remaining integration and native acceptance; T183/T184/T226/T227 implementation and fixture-authoring tasks are checked without claiming Windows execution.

Linux source-selection regression:

```text
cmake --build bin/editor-debug --target platformator_editor_nativeprocess_test platformator_editor_playsession_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_(nativeprocess|playsession)_test' --output-on-failure
```

Outcome: Linux rebuilt against the platform-selected POSIX sources, and the focused native process and PlaySession tests passed 2/2. This validates only the Linux configuration; it does not validate the Windows adapter or Windows-only fixture.

## Linux editor validation workflow (T090 partial, 2026-11-07)

Added `.github/workflows/editor-desktop.yml` for Ubuntu 24.04 x86_64. It installs the Linux SDL development dependencies, enforces CMake 3.28+ and GCC 13+, then calls `cmake/PlatformatorEditorValidation.cmake` for the focused headless suites, install/relocation check, and editor-OFF/session-OFF consumer smoke. The GitHub-hosted workflow was not executed in this session. Windows and macOS jobs and native verification remain open; this does not complete T090 or claim those lanes.

The Linux validation script was run locally after adding the workflow:

```text
cmake -P cmake/PlatformatorEditorValidation.cmake
```

Two initial attempts exposed validation-script assumptions, then the checks were corrected. First, `ldd` reported the valid RUNPATH resolution as `bin/../lib`, while the script searched only the normalized `lib` spelling. After normalizing that spelling, the source-tree guard falsely matched the relocated prefix because both are under the required repository `bin/` build root. The guard now allows generated `bin/` paths while still rejecting source paths outside `bin/` and build-tree `CMakeFiles` dependencies.

Final outcome after adding the watcher integration suite: configure/build passed; the six focused native/editor suites passed 6/6 (native file, native process, PlaySession, workspace, startup, asset watcher); installation and relocated `--help` launch passed; `ldd` confirmed SDL and oneTBB from the relocated prefix; and editor-OFF/session-OFF SDK consumer validation passed. The latest relocated prefix was `bin/editor-validation/relocated-20261107211603-8067b964`. This validates the Linux lane only. The workflow itself was not run on GitHub, and Windows/macOS lanes remain explicitly unverified under T090/T091/T185/T228.

## Linux editor validation rerun after accessibility changes (2026-11-07)

Reran the locally available install lane after the per-host accessibility implementation:

```text
cmake -P cmake/PlatformatorEditorValidation.cmake
```

Configure and full build passed. The six focused suites passed 6/6: `platformator_editor_nativefile_test`, `platformator_editor_nativeprocess_test`, `platformator_editor_playsession_test`, `platformator_editor_workspace_test`, `platformator_editor_startup_test`, and `platformator_editor_asset_watch_integration_test`. Install, relocated launch from an unrelated working directory, private SDL/oneTBB dependency resolution, and editor-OFF/session-OFF SDK consumer validation passed. The relocated prefix is `bin/editor-validation/relocated-20261107230048-5e5da40d`.

Environment: Linux 6.12.112-1-MANJARO x86_64; CMake 4.4.4; GCC 16.2.1. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Build editor SHA-256 `eeaa8e29e6cf6b9847be8dde1412c0e815919859ee7ebcc22fe67921ab67984b`; relocated installed editor SHA-256 `914955fe0d4551327190b1bbb931e26ec97417f71b211f5f66b468eef8c6e756`. This Linux host has no accessible X display, so no live window/dialog or screen-reader walkthrough was performed. Windows/macOS lanes remain unverified; T091 and its dependent acceptance tasks stay open.

## Linux local install validation after watcher recovery (2026-11-07)

After adding Linux recovery for a missing or replaced immediate watcher parent, reran the local validation script. Configure/build, the six focused native/editor suites (6/6), install, relocated launch from an unrelated working directory, private SDL/oneTBB dependency resolution, and editor-OFF/session-OFF SDK consumer validation all passed.

```text
cmake -P cmake/PlatformatorEditorValidation.cmake
```

Environment: Linux x86_64; CMake 4.4.4; GCC 16.2.1. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Build editor SHA-256 `8f23e0127e8aa2dbb3a7c5645403738cc8c8ea3bb6d7a2364149b6a39f15f082`; relocated prefix `bin/editor-validation/relocated-20261107235247-6b12ccf2`; installed editor SHA-256 `933fee124c3240f99c30a71c13ddf5354acc4fb22f625d91108cdb5ed40586b8`. This is Linux headless install evidence only. No dialogs or desktop walkthrough were possible, Windows/macOS verification was skipped, and GitHub Actions was not run; T091 and dependent acceptance tasks remain open.

## Linux local install validation with logical accessibility (2026-11-08)

Added `platformator_editor_accessibility_test` to the local validation lane, then reran the complete Linux configure/build/focused-test/install/relocation/runtime-consumer script:

```text
cmake -P cmake/PlatformatorEditorValidation.cmake
```

Configure and full build passed. The seven focused suites passed 7/7: logical accessibility, native file, native process, PlaySession, workspace, startup, and asset watcher. Install and `--help` launch from an unrelated working directory passed. `ldd` resolved SDL3 and oneTBB from the relocated prefix, with no missing dependencies or source/build-tree dependency paths. Editor-OFF/session-OFF SDK consumer validation passed.

Relocated prefix: `bin/editor-validation/relocated-20261108005347-1b221a9c`. Build editor SHA-256: `eb6c98d0a48df7469ee2d2d34c15a6c44b84e5e9f0d5aad5647209a245bfe8e7`; relocated editor SHA-256: `3c8d1ce15360441cb89d0e821dffc141c43fe6880d46e6b708f6d27ff4d44e64`. This verifies the Linux headless install lane only. No live window/dialog or reader walkthrough was possible; Windows/macOS verification and GitHub Actions remain unrun, so T091 and T090 remain open.
