# Current integrated regression checks

## Accessibility owner-thread wake regression (2026-11-08)

Added a shared accessibility wake callback and wired it to the editor's SDL wake event. A validated native action invokes the callback after leaving the snapshot mutex, so provider threads do not touch SDL/Runtime state and an idle owner loop still processes queued work. The logical regression verifies the wake callback is invoked on successful enqueue.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_accessibility_test platformator_editor_accessibility_linux_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_accessibility_test$' --output-on-failure
dbus-run-session -- bin/editor-debug/platformator_editor_accessibility_linux_test
```

The Linux build and logical test passed (1/1); the private-session Linux provider passed with local Unix-socket access. At initial wake-queue verification, Debug SHA-256 was: logical test `9c48eeae49b2f44f700649b2f7bb9f81a910c5b5b315f41b3e628cd090ef10be`; Linux provider `f2f64a0fb09c8459aaa78b77d61cc064e52f1cf0e4a6880ba5a8bf8f79efde4c`; editor `11c9f4494d966a6981064a325602f6da1caabc489704822ce36a89c6871f244c`. No Windows/macOS compile or screen-reader verification is included.

## Accessibility wake queue order and failure isolation (2026-11-08)

Strengthened the logical regression to query the pending-action queue from inside the wake callback. This verifies the callback runs after the snapshot mutex is released and after the action is queued. Added a throwing-callback case proving wake failure is contained and the queued action still dispatches on the owner thread. The Debug and ASan/UBSan accessibility tests each passed 1/1 after rebuilding.

```text
cmake --build bin/editor-debug --target platformator_editor_accessibility_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_accessibility_test$' --output-on-failure
cmake --build bin/editor-asan --target platformator_editor_accessibility_test --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_accessibility_test$' --output-on-failure
```

Current logical test SHA-256: Debug `fed091ad88c60b4ef1728bce64e398652ee30e420d7d5a72e0c6ed270b7e8b06`; ASan/UBSan `6e6258e2f5842b81b70d0608ea4b89df9fac2069ebed3e16e8d303d80b3ca744`. Windows/macOS provider sources were not compiled or run.

## Integrated Linux Debug and sanitizer regressions (2026-11-08)

The full Linux Debug CTest suite passed: 69 passed, and `platformator_benchmark_contract_test` skipped by configuration (70 tests accounted for). The updated accessibility wake path, Linux AT-SPI bridge, project watcher integration, Scene preview, resources, workspace/startup and all other registered targets completed successfully.

```text
ctest --test-dir bin/editor-debug --output-on-failure --timeout 120
cmake --build bin/editor-asan --target platformator_editor platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor_asset_watch_integration_test platformator_editor_preview_test platformator_editor_resources_test --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^(platformator_editor_(accessibility|asset_watch_integration|preview|resources)_test)$' --output-on-failure --timeout 120
ASAN_OPTIONS=detect_leaks=0 dbus-run-session -- bin/editor-asan/platformator_editor_accessibility_linux_test
```

The focused ASan/UBSan set passed 4/4; the private-session Linux AT-SPI provider also passed. LeakSanitizer remains disabled per the existing container limitation. Debug SHA-256: editor `11c9f4494d966a6981064a325602f6da1caabc489704822ce36a89c6871f244c`; accessibility test `9c48eeae49b2f44f700649b2f7bb9f81a910c5b5b315f41b3e628cd090ef10be`; Linux provider test `f2f64a0fb09c8459aaa78b77d61cc064e52f1cf0e4a6880ba5a8bf8f79efde4c`; watcher integration test `b9ed9265bbcc99ee3fd4a088cef348e6a82a18976fa7332af73c24bc067618e`. ASan/UBSan SHA-256: editor `d305380556d7b5e59a50394f861f4bfc9e7925dc0a78f1d0f7bde125516dc47e`; accessibility test `3313e5ab1f64a2060c13c291bd7c258485afccce00bc854e3874c704401d7eec`; Linux provider test `9bc5652db1fe85d6de6091507010452a6f14dc4d62c0618321a55a3d999ff680`; watcher integration test `410f9bb822f33cb308fad05e7fefcf6bbf872f07946659adc741bc3f53720cc7`.

## Linux Debug build attempt (2026-11-07)

Configuration: existing `bin/editor-debug`, Debug, `PLATFORMATOR_ENABLE_EDITOR_SESSION=ON`, `PLATFORMATOR_ENABLE_WORK_PROBES=OFF`.

Command:

```text
cmake --build bin/editor-debug --parallel 4
```

The build stopped while compiling `platformator_render_culling_index_test`:

```text
tests/render_culling_index_test.cpp:188:49: error: ‘spatialHashForTesting’ is not a member of ‘platformator_detail::RenderCullingState’
tests/render_culling_index_test.cpp:211:11: error: ‘class platformator_detail::RenderCullingState’ has no member named ‘forceQueryStampForTesting’
```

Both hooks are declared in `src/renderculling.h` and defined in `src/renderculling.cpp` only under `#if PLATFORMATOR_ENABLE_WORK_PROBES`; the editor Debug preset configured that feature OFF, while still registering the index test target. The editor and several changed targets built before the failing target, but the full build and full editor CTest run were not completed. This is a test-configuration mismatch, not a passing result. A later full-suite attempt must use a work-probe-enabled correctness build or gate this test behind that option; do not infer broader regressions from this compile failure.

Focused Linux Debug regressions completed separately during this implementation:

- Hierarchy selection/Delete regression: 1/1 passed.
- US3 diagnostics, Console, Inspector drafts, history/document/save, and process gate set: 6/6 passed.
- US5 Hierarchy, discovery, history, Preview/Viewport, Inspector/reference, template, and selection set: 7/7 passed.
- Linux asset-watch integration: 1/1 passed (recorded in [US2](US2.md)).
- Release PlaySession fixture with session support enabled: 1/1 passed (recorded in [US9](US9.md)).

No Windows/macOS verification or native desktop walkthrough was performed. No benchmark was run.

## Linux Debug correctness suite after preset alignment (2026-11-07)

`editor-debug` now enables `PLATFORMATOR_ENABLE_WORK_PROBES=ON`, which is required by its registered test-only RenderCullingState hooks. `editor-release` and benchmark presets keep probes disabled. After reconfiguring the preset, the complete build succeeded. The initial CTest run exposed a stale Workspace test expectation: the test expected seven panel IDs and `panel.settings`, while the current workspace has eight IDs, five visible by default, and hidden Game/Run/Validation panels. The assertion was updated to the current workspace contract.

```text
cmake --preset editor-debug
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug -L '^editor$' --output-on-failure --timeout 60 -j 4
```

Outcome: the complete Linux editor-labeled suite passed 34/34. This is headless/editor-label evidence; it does not satisfy native desktop, Windows/macOS, installation, sanitizer, or comparative performance acceptance. No benchmark was run.

## Final Linux Debug integrated suite (2026-11-07)

After enabling work probes in the Debug preset and correcting two stale test assumptions, the complete Debug build and all-label CTest run passed. The runtime-step regression now accepts either one fresh visibility test or a zero-check cache hit on repeated paused frames; both still require exactly one prepared view and one submitted active sprite. The workspace lifecycle test expects the current eight default panels, with Game/Run/Validation hidden.

```text
cmake --preset editor-debug -DPLATFORMATOR_INSTALL=ON
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug --output-on-failure --timeout 240 -j 4
```

Outcome: all 69 registered tests passed; `platformator_benchmark_contract_test` returned the configured skip code (77), so CTest reported it skipped. After the editor install work, the package install/relocation smoke passed in the aggregate run in 6.69 seconds using its already-built fixture trees; an isolated run from the partially built state also completed successfully in 116.69 seconds. The complete editor label passed 34/34. No benchmark was run. The headless Hierarchy regression passes, but no live GUI display was available to verify physical pointer behavior. Windows/macOS and native desktop walkthroughs remain unverified.

## Integrated RelWithDebInfo build and all-label suite (2026-11-07)

The current shared `build/` tree is configured as `RelWithDebInfo`, with the editor enabled, session support disabled, and work probes enabled. At source revision `b45bdff923aed012f96e2de1b5554541679636f1` (dirty worktree), the full build and all-label CTest run completed after the latest Linux implementation changes:

```text
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Outcome: build succeeded; 68 tests passed and `platformator_benchmark_contract_test` skipped with its configured skip code (77), for 69 tests accounted for. The package smoke was included. This run is integrated Linux regression evidence, but it does not substitute for the separate lifetime sanitizer lane or native desktop/platform checks. No benchmark was run.

After the bounded-queue test adjustments and PlaySession failure teardown ordering change, the build and all-label suite were rerun from the same `build/` tree:

```text
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

Outcome: 68 passed and the benchmark contract test skipped (77), with all 69 tests accounted for. The ASan/UBSan build was rebuilt after the PlaySession change; its selected 64-test engine/editor lane again passed 64/64 with LeakSanitizer disabled for the ptrace limitation described below.

## Linux AddressSanitizer/UBSan regression lane (2026-11-07)

`bin/editor-asan` is a Debug build configured with `PLATFORMATOR_SANITIZER=address-undefined`, editor sessions enabled, and editor work counters enabled. Its full build completed. The first CTest attempt hit the environment's LeakSanitizer fatal error in child fixtures (`LeakSanitizer does not work under ptrace`), so leak detection could not be used here. With only leak detection disabled, AddressSanitizer and UBSan remained enabled:

```text
cmake --build bin/editor-asan --parallel 8
cmake --build bin/editor-asan --target platformator_editor_workspace_test platformator_editor_asset_watch_integration_test --parallel 8
env ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 180 -j 1 -E 'package_smoke|benchmark|gui_capture'
```

The initial sanitizer pass exposed two test assumptions about draining bounded owner queues in a fixed number of frames. The tests now continue until the queued paths drain, failing if a frame makes no progress. The focused rerun passed 2/2. A later parallel-four sanitizer run once hit the watch integration's three-second event deadline under concurrent load; the watcher test passed serially, and the complete selected engine/editor lane then passed 64/64 with `-j 1`. Package installation and benchmark/capture tests were excluded from this sanitizer command; package smoke has separate non-sanitized integrated evidence above. Leak checking and native desktop/platform acceptance remain unverified.

## Final toolbar and integrated regression rerun (2026-11-07)

The responsive toolbar now places project/scene identity and plain-text document/operation state on a separate fitted row. The application semantic snapshot publishes both values as named text nodes with current bounds and full paths. The startup harness checks layout bounds, Open Scene clickability and semantic values at 100%, 150%, and 200% scales, including narrow windows and long project, scene, and status labels.

The first all-label RelWithDebInfo run after this change found a test-fixture timing issue in `platformator_editor_gui_capture_test`: its child-output preservation case allowed only 50 ms for a newly spawned Python process to flush its first line. Under parallel suite load the process had not started before the test budget expired. The fixture now allows one second while its child still sleeps for ten seconds; the owned-child termination remains the tested behavior. The focused capture contract test then passed, and the full rerun completed with 68 passed and the benchmark-contract test skipped (77):

```text
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure --timeout 120 -j 4
```

The matching ASan/UBSan build completed and the selected serial lane passed 64/64 after the toolbar change. LeakSanitizer remained disabled because it aborts under ptrace in this environment:

```text
cmake --build bin/editor-asan --parallel 8
env ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 180 -j 1 -E 'package_smoke|benchmark|gui_capture'
```

No GUI performance capture was run. Windows/macOS verification and physical desktop walkthroughs remain outstanding.

The exact source/build identities for this rerun are:

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1 (dirty worktree)
Code/test/build-input manifest SHA-256: 8e417589057d488d6b1729f4c16d7eaa297ad8b378254a5bb5ff597995606bd6 (6,251 C/C++/Objective-C/Python/CMake source files and CMakeLists/CMakePresets files outside specs, docs, build, and bin)
RelWithDebInfo: Ninja, GCC 16.2.1, editor ON, session OFF, work probes ON
build/platformator_editor SHA-256: 9aa3a4037c6137fa68f9b5487a2d5adcbc821658d02fb06575da42a89c2a083e
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, work probes ON
bin/editor-asan/platformator_editor SHA-256: b2f01ecbd481f6eab9855e138c7f559188df34728ca1cb0c7ab0a48582ae19aa
```

## Integrated rerun after Console and Scene semantic accessibility changes (2026-11-07)

The Console publishes its visible virtualized rows and current status, filter, copy, clear, repeat-collapse and follow controls as semantic nodes. Owner-thread actions reject stale row IDs after output revisions. The Scene publishes canvas bounds, current selection, active tool, zoom, snap increments and coordinate space with owner-thread actions for tools and framing. The focused Console and viewport/preview tests passed in RelWithDebInfo and ASan/UBSan builds.

```text
cmake --build build -j8
ctest --test-dir build --output-on-failure --timeout 180 -j8
cmake --build bin/editor-asan -j8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 180 -j1 -E 'package_smoke|benchmark|gui_capture'
```

Outcome: RelWithDebInfo build succeeded; CTest accounted for 70 tests, with 69 passed and the configured benchmark-contract test skipped (77). The selected serial AddressSanitizer/UBSan lane passed 65/65. LeakSanitizer remains disabled because it cannot run under ptrace in this environment. No benchmark or native desktop/platform verification was run.

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1 (dirty worktree)
Code/test/build-input manifest SHA-256: 3c97bbec859536d7776e1174f1d27b8c394151f3001e909c96303e304ac3e151 (6,254 C/C++/Objective-C/Python/CMake source files and CMakeLists/CMakePresets files outside specs, docs, build, and bin)
RelWithDebInfo: Ninja, GCC 16.2.1, editor ON, session OFF, work probes ON
build/platformator_editor SHA-256: a35871305ae2a39a10f3d4dea8fcd23f16586fe34fcb13ba05ef52aba39275e3
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, work probes ON
bin/editor-asan/platformator_editor SHA-256: 78ab90c94e46c69bf5cb2245761d4e95fcf6262c14ef0dc27dd59cf2e4cadd4e
```

## Integrated Release and sanitizer rerun after source-region semantics and POSIX recovery preservation (2026-11-07)

The Animation asset editor now publishes validated semantic pixel fields for the selected frame's source region. POSIX atomic replacement now retains a same-directory recovery link to the old file until the new directory entry is durable; a directory-flush warning identifies the retained prior content. Windows replacement now keeps its `ReplaceFileW` backup through the first directory durability attempt and reports when recovery content remains. Only the Linux/POSIX path was compiled and exercised in this environment.

Focused native-save and animation tests passed 2/2. The complete Release build succeeded with editor work counters and engine work probes enabled for the correctness tests. Full CTest accounted for 70 registered tests: 68 passed, `platformator_benchmark_contract_test` skipped as configured, and `platformator_package_smoke` timed out at its 180-second limit during the source-tree consumer build. The timeout was not retried; the earlier editor-off/session-off package smoke remains historical evidence and is not a current rerun pass. The selected serial ASan/UBSan lane passed 65/65 with LeakSanitizer disabled due the ptrace limitation.

```text
cmake -S . -B bin/editor-release -DPLATFORMATOR_ENABLE_WORK_PROBES=ON -DPLATFORMATOR_EDITOR_WORK_COUNTERS=ON
cmake --build bin/editor-release -j8
ctest --test-dir bin/editor-release --output-on-failure --timeout 180 -j2
cmake --build bin/editor-asan -j8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 180 -j1 -E 'package_smoke|benchmark|gui_capture'
```

Exact identities for the current dirty worktree:

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1
Code/test/build-input manifest SHA-256: 9faa312cbf8b533ed185c4806053692be3b29bb557d1b9bba782f6b134a3c8e7 (6,254 source/build inputs outside specs, docs, build and bin)
Release: Ninja, GCC 16.2.1, editor ON, session ON, engine work probes ON, editor work counters ON
bin/editor-release/platformator_editor SHA-256: 5c62e23ec1e8a2d63ad0e07c9285bcc4d2b47bfa1bb5b8d670ecbe92ab84f271
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, engine work probes ON, editor work counters ON
bin/editor-asan/platformator_editor SHA-256: fb99cdc4ce210f40d1c362896b038d824e781d8a4a69ddd0cee7a672b60dc601
```

No GUI performance capture or native desktop/platform walkthrough was run. T231 remains open for native accessibility providers and screen-reader evidence; T225/T227 remain open for Windows adapter verification; package smoke, desktop, Windows/macOS, and performance acceptance remain open under T143/T145/T198/T241.

## Run Settings accessibility actions focused Release check (2026-11-07)

After the integrated Release/ASan run above, Run Settings semantic nodes gained owner-thread activation for discovered configure/build presets, built target/executable selection, and catalog protocol choices. Interactive preference checkboxes now publish `toggle` actions, respect Production and active-operation disabled states, and reject stale project choices after discovery or project-root replacement. The discovery regression covers these contracts and snapshot parent validity.

```text
cmake --build bin/editor-release --target platformator_editor_discovery_test platformator_editor --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 60 -R '^platformator_editor_discovery_test$'
cmake --build bin/editor-asan --target platformator_editor_discovery_test platformator_editor --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 60 -R '^platformator_editor_discovery_test$'
```

Outcome: editor and focused test built in both configurations; 1/1 test passed in Release and 1/1 passed in ASan/UBSan. The full suite and sanitizer lane were not rerun after this focused change. Current artifact identities:

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1 (dirty worktree)
Release: Ninja, GCC 16.2.1, editor ON, session ON, work probes ON, editor work counters ON
bin/editor-release/platformator_editor SHA-256: ea57a8954981670cee09756e95550cb453efccd4ee084ba18601a8f6229e507e
bin/editor-release/platformator_editor_discovery_test SHA-256: 28a8389b8f2d6ade6d04abf0d5ab99a89406b4e087dafd47ea7b694041cbedf8
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, work probes ON, editor work counters ON
bin/editor-asan/platformator_editor SHA-256: 3a74c6e2a878f2a6ae7282fe7d097b21b2a0f0495048bb4e5931a5668e287ded
bin/editor-asan/platformator_editor_discovery_test SHA-256: a5cbf7e934dca04cabc0a2a5754d790b41155171d6be28b74a847520d8f0caa1
```

This is logical/headless accessibility evidence only. The Linux AT-SPI reader walkthrough and Windows/macOS providers and verification remain open.

## Linux AT-SPI numeric value action routing (2026-11-07)

The provider now enqueues the exact `set_value` action advertised by numeric semantic controls. It validates finite values against the published range and uses round-trip-safe double formatting before dispatch. The logical regression rejects the hyphenated spelling and verifies owner-thread action delivery.

```text
cmake --build bin/editor-release --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 60 -R '^platformator_editor_accessibility(_linux)?_test$'
cmake --build bin/editor-asan --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 60 -R '^platformator_editor_accessibility(_linux)?_test$'
```

Outcome: Release passed 2/2 and ASan/UBSan passed 2/2. The Linux provider lifecycle test passed its graceful unavailable-registry path; these runs did not exercise a live AT-SPI reader.

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1 (dirty worktree)
Release: Ninja, GCC 16.2.1, editor ON, session ON, work probes ON, editor work counters ON
bin/editor-release/platformator_editor SHA-256: ba26c5e3aad52b6029e8efdf380d420c890df583f67e396807fdc54952a5a0a6
bin/editor-release/platformator_editor_accessibility_test SHA-256: f95da3269763c7f87665bc10652c44e929dafa7b9953656735e3e0570eb08adf
bin/editor-release/platformator_editor_accessibility_linux_test SHA-256: d5a45c49781621849933a9a37964518b3b73a98db17e4658d3eea02294ed28a1
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, work probes ON, editor work counters ON
bin/editor-asan/platformator_editor SHA-256: c18b7eb611a07ce26ece808d1310c72716e1808a81e0b777d3dfb7bdbc4651f5
bin/editor-asan/platformator_editor_accessibility_test SHA-256: 483be34d6e8f27b9fe797d30bca64b7833ac6117e061bea557e64cad48d92a0e
bin/editor-asan/platformator_editor_accessibility_linux_test SHA-256: d39a43cccb92bf5817b49217a602e1825e11909b00f9f9ac5431be9c2e76f9c7
```

## Editable text and semantic input checks (2026-11-07)

Linux AT-SPI Text reads semantic text fields by UTF-8 character offsets. EditableText queues capped whole-value replacements as the existing `set_value` owner-thread action. Console search and Run Settings Game dimensions now accept that action with their own visible field validation. Release and ASan/UBSan runs each passed the Animation, logical accessibility, Linux provider lifecycle, Console, and discovery suites (5/5).

```text
cmake --build bin/editor-release --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor_console_test platformator_editor_discovery_test platformator_editor_animation_test platformator_editor --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 60 -R '^platformator_editor_(accessibility(_linux)?|console|discovery|animation)_test$'
cmake --build bin/editor-asan --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor_console_test platformator_editor_discovery_test platformator_editor_animation_test platformator_editor --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 60 -R '^platformator_editor_(accessibility(_linux)?|console|discovery|animation)_test$'
```

The Linux provider test starts a private D-Bus session with a fake AT-SPI registry. It exercises D-Bus Text/EditableText/Value methods and dispatch without needing a desktop, but it does not replace a walkthrough with the desktop accessibility registry or a screen reader.

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1 (dirty worktree)
Release: Ninja, GCC 16.2.1, editor ON, session ON, work probes ON, editor work counters ON
bin/editor-release/platformator_editor SHA-256: eb3954fc310bde6fc16177382e5001696d728e456bd6cceede3435d8db3d9187
bin/editor-release/platformator_editor_accessibility_test SHA-256: 2ae768e331e570a04dd314cf79282589a135ece392ea2d8618e9b4cdb3b752ea
bin/editor-release/platformator_editor_accessibility_linux_test SHA-256: 052a523169f9e77709ffbff57314d541b13a3ecc4d85d1bf65bf7b014414daee
bin/editor-release/platformator_editor_console_test SHA-256: 7a028008cc48296ec3b32ffdfba6fb91d70c0b5a53937f7e6f7b507007698319
bin/editor-release/platformator_editor_discovery_test SHA-256: fdc8f66b771f7f337051baeb48f49f8041c0f2c1915a2f5bf3a9975ec06590b1
bin/editor-release/platformator_editor_animation_test SHA-256: 1f8598ee3dba741ec9dff70915aa71e3834558c3c6d5e81fee38fd1f557fb9ac
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, work probes ON, editor work counters ON
bin/editor-asan/platformator_editor SHA-256: bfe2cc6d5cdd8b279d1e41cf9b59002d82f21dc694ae1a7b0a56deead0db8e37
bin/editor-asan/platformator_editor_accessibility_test SHA-256: d869ce070cf480c714ad9db9944d5371e8855d04b73a9e1426004747b7e7a931
bin/editor-asan/platformator_editor_accessibility_linux_test SHA-256: 63f1d0ea010e394abb66f90e6d95773cf9281903b851b01540a4001146df35d7
bin/editor-asan/platformator_editor_console_test SHA-256: b9e6970858a42dbac8f1361a3c941fdaee1e5bf09e292fcc1b0126ee21fed0f4
bin/editor-asan/platformator_editor_discovery_test SHA-256: 7be167c1d9bba392776045cdf923e714ff45507317b25dd3018088d8edf2eee3
bin/editor-asan/platformator_editor_animation_test SHA-256: 6b6852b2401caf131f8e277ee0b73b37cb8079fac20c95ddad7999d3a2c5edf5
```

## Asset-watch owner queue under ASan/UBSan (2026-11-07)

An ASan/UBSan run exposed that the Project-frame watcher integration helper asserted all paths drained in one owner call. `ProjectPanel` intentionally limits commits to 256 paths or 2 ms and retains the remainder. The helper now sends empty continuation batches, asserts each frame's count matches callbacks and stays within the 256-path cap, requires progress, and verifies the queue drains. The independent 600-path test already checks full bounded continuation and counter accounting.

```text
cmake --build bin/editor-release --target platformator_editor_asset_watch_integration_test --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^platformator_editor_asset_watch_integration_test$'
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^platformator_editor_asset_watch_integration_test$'
```

Release passed 1/1; ASan/UBSan passed 1/1. The first sanitizer failure was diagnosed as the test's one-frame assumption; after correcting its continuation handling, the focused sanitizer run passed.

```text
HEAD: b45bdff923aed012f96e2de1b5554541679636f1 (dirty worktree)
Release: Ninja, GCC 16.2.1, editor ON, session ON, work probes ON, editor work counters ON
bin/editor-release/platformator_editor_asset_watch_integration_test SHA-256: ebae4ede4c25ffc11a6a271d666d028354cf10b21ae3bab8f73b2ee5c5347cfa
ASan/UBSan: Ninja, GCC 16.2.1, Debug, editor ON, session ON, address-undefined, work probes ON, editor work counters ON
bin/editor-asan/platformator_editor_asset_watch_integration_test SHA-256: 8be39424d946cd48cc0afc918783fc6ff311e8f6fc1a5394691d5dea27789810
```

## Shared Linux watcher owner recovery (2026-11-07)

The Linux ProjectSession now starts one inotify service/worker for nested, disjoint, and equal source/asset roots. An included nested/equal asset subtree bypasses source-only directory exclusions; a disjoint asset root is watched as an additional tree under the same owner. Path-specific root, overflow and unavailable notifications preserve independent coverage and source freshness. Integration tests verify one owner, asset delivery, source-only revisions, and asset-root removal/recreation for the nested/disjoint cases. An initial assertion expected an internal `RootAvailable` control event in the public batch; the corrected regression checks the public coverage state instead. Native Windows/macOS verification and full degraded-status acceptance remain open under T162/T205.

```text
cmake --build bin/editor-release --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test platformator_editor --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^(platformator_editor_asset_watch_integration_test|platformator_editor_workspace_test)$'
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^(platformator_editor_asset_watch_integration_test|platformator_editor_workspace_test)$'
git diff --check
```

Release and ASan/UBSan workspace plus watcher suites passed 2/2 each. A transient assertion race was fixed by sampling source revision after the source-file notifications settle. Release integration SHA-256: `8986b6105d754e54ecc801c77148d46cfd8da425bb1b9a667806ec5b21c10e9d`; ASan/UBSan integration SHA-256: `96dfbfd84265ec8b85322e48a867c7889cceb7f8d4f0b7d59dab2ef8086f77ca`; Release workspace SHA-256: `d0cda9bcc168b240f270d8f314e3af057979a842bd999015c222148ba7693877`; ASan/UBSan workspace SHA-256: `5153b39238eef43dfda9e7e0f3c4c879436211dca1a88aba77e51782ff9f0e17`. This is Linux host evidence only; Windows/macOS integration remains open under T162/T205.

## Equal project and asset roots (2026-11-07)

The Linux shared watcher path now also handles identical project and asset roots. Previously that configuration started separate source and asset watchers, while source-path filtering classified every source file as an asset. The shared path includes the configured tree, routes source file changes to source freshness, and routes non-source asset changes to ProjectChanges. The focused regression verifies one watcher owner, both routes, and no source revision change for a non-source asset update.

```text
cmake --build bin/editor-release --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^(platformator_editor_asset_watch_integration_test|platformator_editor_workspace_test)$'
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^(platformator_editor_asset_watch_integration_test|platformator_editor_workspace_test)$'
git diff --check && git diff --cached --check
```

Release passed 2/2; ASan/UBSan passed 2/2. Release asset-watch SHA-256: `037a6ef75ed466373fd1766bbde22fa787ba5f8a1794f1d6b4779847b9dc0ad5`; ASan/UBSan asset-watch SHA-256: `936eb0b823edd09684f36dca0f9cf0d6b345325f484928bd01bab6c191bd2eb0`; Release workspace SHA-256: `5cf0e56bddd5e46206c020348107e3b94cd8245c7ec89f596e5a29ceb8d56667`; ASan/UBSan workspace SHA-256: `88b7aad05ce3766c235c18a4425445dd357c267c99a94433dcda4edf9d323bd8`. This supplements the nested/disjoint watcher results above; native Windows/macOS verification remains open.

## PlaySession failure, teardown and fallback regressions (2026-11-07)

Added a SIGKILL child fixture and verified PlaySession reports the unexpected exit, reaps the child before cancelling channels or dropping launch identity, then returns to Idle. An initial attempt placed the crash mode in the general process fixture, while the PlaySession test launches the session fixture; moving the mode to that fixture resolved the test failure. The panel regression now checks its semantic guidance directs gameplay input to the external window. The active pipeline test changes the project identity while Running and verifies stop/reap plus stale launch rejection. Existing tests cover EOF, stale nonce/generation, two-slot backpressure, visibility gating, slot release, and explicit stop cleanup. Linux `EditorSessionService::capture_frame` enforces the 66,667 μs minimum interval (≤15 Hz).

```text
cmake --build bin/editor-release --target platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^platformator_editor_playsession_test$'
cmake --build bin/editor-asan --target platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^platformator_editor_playsession_test$'
git diff --check
```

Release and ASan/UBSan passed 1/1 each. Release PlaySession test SHA-256: `07e37e8e9a0310e050007c7b74515320ca1875b0f30ca4cc005a360b470ad56d`; ASan/UBSan test SHA-256: `b131f1bb69d1f7faef7794a3c98b0019f0c9dfa7b63a702c051b164910bc0df6`. Native Windows/macOS verification remains open under T091/T145; the Game/external-window walkthrough remains open under T133/T184/T227. T132 implementation is now checked separately in `US9.md`.

## POSIX session launch descriptor collision (2026-11-07)

Session-enabled POSIX launches now reserve output-pipe descriptors above fd 5 before spawning. This protects the fork fallback's fd 3/4/5 session mappings from the subsequent stdio-pipe close loop. The native process regression closes parent fd 0/1/2 before creating session channels and launching with separate output capture, then verifies bidirectional channel traffic, shared mapping writes, clean child exit, and separate stdout/stderr capture. The focused tests passed in Release and ASan/UBSan.

```text
cmake --build bin/editor-release --target platformator_editor_nativeprocess_test platformator_editor_process_fixture platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^(platformator_editor_nativeprocess_test|platformator_editor_playsession_test)$'
cmake --build bin/editor-asan --target platformator_editor_nativeprocess_test platformator_editor_process_fixture platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^(platformator_editor_nativeprocess_test|platformator_editor_playsession_test)$'
git diff --check && git diff --cached --check
```

Release and ASan/UBSan each passed the process, native process and PlaySession suites 3/3. The descriptor-collision regression forces the POSIX `fork` fallback. Release SHA-256: `5315aef44cdf5a5824309d87c5a84d57f188d67471aff6437cfc3f82661fe5f4` (`process`), `27117ca0565fd9f2e218406876a41a2c7f4a1a7a263d8ca98d8d11a5da2ca82c` (`nativeprocess`), `cfe010a1d53e22df13c02f1b403569668f5cc9b966e7d84373b50db98767911b` (`playsession`). ASan/UBSan SHA-256: `3e28a899e7fd9349b0e946c2c3c90a4c58b36c2238587aa1ac1b4f73edffd7cd` (`process`), `ab7bb8aa9661661d7af92f826461b348a3c45db2cb778957039bdf01784930a0` (`nativeprocess`), `62556ff91bc3d6706e49185428e4fcc8f3c2ef65e8f4df9ddb1f915609cba793` (`playsession`). No Windows/macOS verification was performed; the Windows session-channel adapter remains incomplete under T184/T227.
## Linux integrated Debug and lifetime sanitizer rerun (2026-11-07)

The editor Debug tree rebuilt successfully from the current worktree. CTest accounted for all 70 registered tests: 68 passed, `platformator_benchmark_contract_test` was skipped by its configured condition, and `platformator_editor_accessibility_linux_test` failed before provider startup because `dbus-run-session` could not create `/run/user/1000/dbus-1` (read-only filesystem) or bind its temporary socket (`Operation not permitted`). Direct invocation with the inherited stale `DBUS_SESSION_BUS_ADDRESS` failed to connect to that denied socket; `env -u DBUS_SESSION_BUS_ADDRESS` passed the provider's graceful unavailable-registry path. The fake-registry Text/EditableText/Value dispatch path could not run here. The other 69 tests, including native file/process, PlaySession, workspace, preview, reference, resource, fixed-step, render, hierarchy, discovery and package-smoke coverage, completed successfully.

The selected ASan/UBSan lifetime set rebuilt and passed 7/7: native process, logical accessibility, window host, PlaySession, workspace, preview, and asset watcher integration. LeakSanitizer stayed disabled for the documented container ptrace limitation; this run does not include the Linux D-Bus provider test or claim native reader interoperability.

```text
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug --output-on-failure --timeout 180
bin/editor-debug/platformator_editor_accessibility_linux_test
cmake --build bin/editor-asan --target platformator_editor_nativeprocess_test platformator_editor_playsession_test platformator_editor_asset_watch_integration_test platformator_editor_windowhost_test platformator_editor_workspace_test platformator_editor_preview_test platformator_editor_accessibility_test --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R 'platformator_editor_(nativeprocess|playsession|asset_watch_integration|windowhost|workspace|preview|accessibility)_test' --output-on-failure --timeout 120
```

Source revision: `b45bdff923aed012f96e2de1b5554541679636f1` plus the current tracked and untracked worktree changes. Linux Debug editor SHA-256: `b21f3980de4cd4452c5650f4f00c8555c1938f99bf9566395bb6547cbc331db4`; native process test: `646d78d9be2f3e6624c7a3ea82b3665383d0ebcced6372df399d098a8b3b83a2`; PlaySession test: `9f97a36941152d6b50da5d64bc50c4fc890ee5569b72731e23befad6cc0f8730`. This verifies current Linux code but does not complete the native desktop, D-Bus reader, Windows/macOS, or matched performance criteria; T143/T198/T241 remain open.

ASan/UBSan target SHA-256: native process `94d4683199c9e0bdc9b800a0f6d00f50a201d1f5f0f216ce66b8a62ab1f031fa`, PlaySession `5c1f59fbd20f0b1f40c8927c715d7a67e2e228d1a8543a508791477277bd4f77`, Workspace `1c8968ff29c92b2c197db2030a1ac7aef75e78228f1d77f5bed213d0de2ba19f`, Preview `f36cf72ed0ea34c678f6056e610df5fc16783de7174b3a7057d7ff344df7bcd2`, asset watcher `e7e5a3552bd32515aa9a8f0d46eb71f2c7c1458aef535ea661f14bb7c43ad1d8`.

## Linux all-label rerun after File API and install-validation changes (2026-11-07)

The validation build uses source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus the current uncommitted worktree, CMake 4.4.4, GCC 16.2.1, Debug, `PLATFORMATOR_BUILD_EDITOR=ON`, `PLATFORMATOR_ENABLE_EDITOR_SESSION=ON`, and work probes/editor counters enabled. The editor executable SHA-256 is `876559271dd2ec9b2724950100ce1fb12f681bb8b238847883a49a39149064d5`.

```text
cmake --build bin/editor-validation --parallel 4
ctest --test-dir bin/editor-validation --output-on-failure --timeout 180
```

CTest accounted for all 70 registered tests: 67 passed, the configured benchmark contract test was skipped, and two failed. `platformator_package_smoke` timed out at 180.10 seconds while its fresh editor-OFF/session-OFF consumer tree was building the core SDK. Separately, the standalone `cmake -P cmake/PlatformatorEditorValidation.cmake` lane passed its editor-OFF/session-OFF consumer validation using its own persistent `runtime-consumer-validation` build tree, so the timeout is specific to CTest's cold-build limit; no retry was made. `platformator_editor_accessibility_linux_test` failed before provider startup because the sandbox denied creation of `/run/user/1000/dbus-1` (read-only filesystem) and its temporary D-Bus socket (`Operation not permitted`). No provider assertions ran.

The other 67 tests completed, including updated File API discovery, asset watcher, workspace, startup, process, session, scene, resource, history and rendering tests. The standalone Linux editor install/relocation validation passed its six focused suites (6/6), `ldd` dependency checks and relocated SDK smoke. This entry supersedes earlier all-label counts for this build identity only; full-suite acceptance remains open under T143/T198/T241.

The timed-out package test was then run by itself with a 600-second cap against its partially built tree:

```text
ctest --test-dir bin/editor-validation -R '^platformator_package_smoke$' --output-on-failure --timeout 600
```

It passed 1/1 in 343.32 seconds, completing the core install, source and relocated SDK consumers, and Mario installed/source smoke checks. This completes the package test assertions but does not change the earlier all-label result: its explicit 180-second limit still timed out the cold build. The D-Bus test remains unexecuted because the sandbox cannot bind the private bus socket.

## Nested source/asset watcher routing continuation (2026-11-07)

`ProjectSession` now uses one watcher for a nested asset root on all selected platforms; the same callback publishes source freshness and separately routes asset changes. This removes duplicate recursive monitoring for the common nested layout on Windows/macOS. Linux keeps its existing single-owner behavior for both nested and disjoint roots. Non-Linux disjoint roots remain on separate services because the current Windows/macOS adapters do not support additional roots.

```text
cmake --build bin/editor-validation --target platformator_editor_workspace_test platformator_editor_asset_watch_integration_test platformator_editor_startup_test --parallel 4
ctest --test-dir bin/editor-validation -R '^platformator_editor_(workspace|asset_watch_integration|startup)_test$' --output-on-failure --timeout 180
```

The current Linux Debug run passed all three focused suites. The asset watcher integration suite had two failures at the renamed-subtree child-event assertion during earlier runs. A later `--repeat until-fail:3` check passed, a plain `--repeat until-fail:10` check failed on its ninth invocation, then trace-enabled 10-run and 20-run repeats and a final clean single run passed. The failure output showed seven active watches and no event below the renamed child; it did not include the watch-path map. The regression now asserts that active watch count is unchanged by rename and prints up to twelve recent events on failure. The latest 20-run check passed. The intermittent failure remains recorded as unresolved. Windows/macOS source selection, compilation, watcher lifetime and event behavior remain unverified; T162/T205 stay open.

## Shared editor asset-format classification (2026-11-07)

Moved Project presentation, template classification, and typed asset-reference extension interpretation into `editor/src/assetformats.h`. The shared mapping preserves the distinction between audio formats shown as audio in Project (`.aiff`, `.aif`, `.m4a` included) and the narrower set assignable to Audio fields. Animation frame texture compatibility now uses the same Texture mapping; regressions accept QOI, classify `.pttemplate`, and reject unsupported DDS and wrong-kind AIFF references.

```text
cmake --build bin/editor-validation --target platformator_editor_workspace_test platformator_editor_document_test --parallel 4
ctest --test-dir bin/editor-validation -R '^platformator_editor_(workspace|document)_test$' --output-on-failure --timeout 180
git diff --check
```

The Linux Debug build succeeded and the focused Project/document suites passed 2/2. This was a behavior-preserving ownership cleanup; it did not invalidate performance observations. T142 remains open because its dependency chain and remaining cleanup/acceptance work are not complete.

The follow-up Project/document/animation/tab-format check passed in Debug:

```text
cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_document_test platformator_editor_animation_test platformator_editor_tabs_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_(workspace|document|animation|tabs)_test$' --output-on-failure -j4
```

All four focused suites passed (4/4). The tab regression also opens uppercase `.ANIMSET` through the shared case-insensitive animation mapping. A source scan now finds the image/audio and animation/template extension mappings in one editor header rather than animation-specific or Project-specific checks.

## ImGui reference-search state ownership (2026-11-07)

Removed the process-wide reference-search buffer map from `referencefields.cpp`. Search bytes now live in packed words in the current ImGui window's `ImGuiStorage`, scoped by the field ID and current host context. This prevents two detached ImGui contexts with matching widget IDs from sharing filter text and avoids clearing one host's buffers when another reaches the old global cap.

```text
cmake --build bin/editor-validation --target platformator_editor_inspector_interaction_test platformator_editor_workspace_test platformator_editor_document_test --parallel 4
ctest --test-dir bin/editor-validation -R '^platformator_editor_(inspector_interaction|workspace|document)_test$' --output-on-failure --timeout 180
git diff --check
```

The Linux Debug build succeeded and the focused Inspector, Project/workspace, and document suites passed 3/3. T142 remains open pending its wider cleanup audit and dependent acceptance gates.

## Linux watcher discovery queue bound (2026-11-07)

Recursive inotify discovery now caps its pending directory paths at the same 4,096-directory limit as native watches. A very wide tree stops discovery and reports unavailable coverage instead of retaining an unbounded fan-out before attempting child watches. The existing wide-tree, cancellation, cleanup, and shared-budget integration cases passed.

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_asset_watch_integration_test$' --output-on-failure
git diff --check -- editor/src/platform/watch_linux.cpp editor/tests/asset_watch_integration_test.cpp specs/012-gui-cleanup/evidence/REGRESSIONS.md
```

Linux 6.12.112-1-MANJARO; source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. The focused test passed 1/1, including a 4,097-directory fan-out that exercises the new queue limit; executable SHA-256: `6645c9ed91b34cdd576d49bf77801ec85275bcf49c6c1b3d33d195dbb532a0c3`. This does not complete T205: Windows/macOS compilation, native lifetime checks, and cross-platform watcher ownership remain unverified.

## Linux complete regression rerun (2026-11-07)

After rebuilding the watcher test with the queue-bound regression, the complete Linux Debug CTest suite passed 69/69 runnable tests; the benchmark contract test returned its configured skip code. This rerun supersedes the earlier D-Bus setup failure and cold package-smoke timeout for this build. The AT-SPI provider test passed under `dbus-run-session` when its required local Unix-socket bind was allowed; package smoke passed in 7.51 seconds from the populated consumer tree. The watcher integration test also passed under ASan/UBSan after the queue change.

```text
ctest --test-dir bin/editor-debug --output-on-failure --timeout 600
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_asset_watch_integration_test$' --output-on-failure --timeout 120
```

Debug CTest accounted for 70 tests: 69 passed and `platformator_benchmark_contract_test` was skipped. The sanitizer watcher test passed 1/1; LeakSanitizer remained disabled per the existing container limitation. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Debug editor SHA-256: `76e7a23c7ec88a994560166e3cbda9753ae9ddbc062f9b0febb07157080e18a2`; Linux accessibility test: `f47b0d4ddf3641cd69963d7059925df624998c784b6ebbf4b9644d168d9c2c34`; Debug asset watcher test: `6645c9ed91b34cdd576d49bf77801ec85275bcf49c6c1b3d33d195dbb532a0c3`; ASan/UBSan asset watcher test: `0365678c9d71cb9f01222343ef1b7c1ba02e509303ef6df884dc0549ccf6b777`.

## Final Linux Debug regression after accessibility and format changes (2026-11-07)

Rebuilt the full Debug tree after the bounded semantic-action, watcher queue, and shared asset-format changes. CTest again accounted for 70 tests: 69 passed and the benchmark contract test was skipped by its configured return code. This run includes the Linux AT-SPI provider, package smoke, watcher integration, uppercase `.ANIMSET` tabs, shared template classification, and the animation QOI/DDS checks.

```text
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug --output-on-failure --timeout 600
```

Focused ASan/UBSan tests also passed after the watcher and accessibility action changes: asset watcher integration 1/1, logical accessibility 1/1, and the Linux private-session AT-SPI provider passed. LeakSanitizer was disabled under the documented container limitation. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Debug editor SHA-256: `210894523f1ccf992b27c9f5910b2e9d9b96161bc957a4241d521544942d524f`; Debug Linux accessibility provider test: `e93c68b0ec955e914bb3a9f401a5eab2dc409101b06b26e1964802fc49b25992`; Debug watcher test: `e651c303377d8bbba47a38bfdd8101d0fa6e03ad8e6480c01ffb1bb1463eaaa4`; Debug animation test: `06a22a9dc4dabb1f1ad58e69f3d35b0c41e3fa2e249ee68e6f424a1cff03c808`; Debug tabs test: `66d3eef30881e64f6c0187f951cd848a0a79ee985af2e9e464024f79c9516649`.

## Multi-host accessibility regression (2026-11-07)

The logical model test verifies snapshot partitioning by panel assignment, detached window roots, parent and focus-path integrity, incarnation-specific publication, validated action ownership, and queue drain on retirement. The Linux AT-SPI private-session test publishes a second host, enumerates its named window and control through the application root, invokes its action, and confirms the old object path becomes unavailable immediately after retirement.

```text
cmake --build bin/editor-debug --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor -j4
ctest --test-dir bin/editor-debug -R '^platformator_editor_accessibility_test$' --output-on-failure
dbus-run-session -- bin/editor-debug/platformator_editor_accessibility_linux_test
git diff --check -- editor/src/accessibility.cpp editor/src/accessibility.h editor/src/editorapp.cpp editor/src/platform/accessibility_linux.cpp editor/tests/accessibility_test.cpp editor/tests/accessibility_linux_test.cpp specs/012-gui-cleanup/evidence/US6.md specs/012-gui-cleanup/evidence/REGRESSIONS.md
```

Outcome: Debug and ASan/UBSan builds succeeded; both focused tests passed in each configuration. The provider test required permission to bind a temporary local D-Bus socket; LeakSanitizer was disabled per the existing container limitation. Debug SHA-256: logical test `90299c6baa61c722c16ff278fc195865f18e3b05c959680e749006d97be5bac9`; Linux provider test `79bb6841423aa0c50521c87345630ae3242a442e7f7b9b20e218c71a5e7633f0`. ASan/UBSan SHA-256: logical test `078bfbc5c27ebff87798db245419108c6ae462e27bbc04b5b3581fa2fe78e9e8`; Linux provider test `bbcada4a704fcca3ce12b80307485855e4a870dbd6bd5f56c6325fc609e5c6f3`. Windows/macOS providers and a real reader walkthrough are not covered.

## Full Linux Debug regression after per-host accessibility (2026-11-07)

Rebuilt the full Debug tree after adding per-host accessibility publication and Linux provider routing. The full CTest run accounted for 70 tests: 69 passed and the configured benchmark contract test skipped. This includes package smoke, scene/render/runtime suites, asset watching, logical accessibility, and the D-Bus AT-SPI detached-window action/retirement regression. Focused ASan/UBSan coverage for the logical and Linux provider accessibility tests passed 2/2; LeakSanitizer was disabled under the existing container limitation.

```text
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug --output-on-failure --timeout 600
cmake --build bin/editor-asan --target platformator_editor_accessibility_test platformator_editor_accessibility_linux_test platformator_editor -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_accessibility_test$' --output-on-failure --timeout 120
ASAN_OPTIONS=detect_leaks=0 dbus-run-session -- bin/editor-asan/platformator_editor_accessibility_linux_test
```

Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Debug editor SHA-256: `59bd35aa9e86f1fd813a07e601506f4aaffa358e50ab109b64d8976d39f2d35b`; ASan/UBSan editor SHA-256: `022dbad02a0d44c3fece7fb4d163ccef7e14a8b336a4b5012e677ac6f8250d89`. This is Linux headless/provider evidence; the live desktop walkthrough and Windows/macOS lanes remain open.

## Final-source Linux Debug and ASan/UBSan regression suites (2026-11-07)

After the final move-only semantic publication change, rebuilt Debug and ASan/UBSan and reran the full test suite in both configurations. Each run accounted for 70 tests: 69 passed and `platformator_benchmark_contract_test` returned its configured skip. No benchmark campaign was run. The ASan/UBSan run completed in 597.03 seconds; its clean SDK package smoke took 482.01 seconds and completed within the per-test 600-second timeout. LeakSanitizer was disabled due to the existing container limitation.

```text
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug --output-on-failure --timeout 600
cmake --build bin/editor-asan --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 600
```

Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Debug editor SHA-256: `e004c6806757a186e80d87a30d2f2b6e151c94d23fb487384f23178f099eb524`; ASan/UBSan editor SHA-256: `3bc63eff43606c1638870ea159f03142e3b31cf788c2379f9d37fbd4caad26c7`. This completes the Linux full-suite rerun, not the dependent cleanup or native desktop/platform acceptance tasks.

## Linux watcher routing for nested project and asset roots (2026-11-07)

When the project root and nested asset root shared an inotify descriptor, the nested root's parent handler consumed direct project-root edits. The Linux adapter now forwards unmatched parent-directory events only when that directory also belongs to a watched tree; external rename sources remain excluded. It watches a recovery ancestor, supports a missing or replaced immediate parent, drains stale `IN_IGNORED` events before rearming, and requests reconciliation after recovery. The watcher integration regression checks direct `CMakeLists.txt` delivery and source freshness through `ProjectSession`, missing-root/parent recovery, parent replacement, source and nested-asset degraded status during project-root loss, recovery status after restoration, moved subtrees, and asset routing.

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R platformator_editor_asset_watch_integration_test --output-on-failure --timeout 120
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test platformator_editor -j2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R platformator_editor_asset_watch_integration_test --output-on-failure --timeout 120
git diff --check -- editor/src/platform/watch_linux.cpp editor/tests/asset_watch_integration_test.cpp specs/012-gui-cleanup/evidence/REGRESSIONS.md
```

Outcome: Debug and ASan/UBSan watcher integration each passed 1/1; the editor executable linked in both configurations. LeakSanitizer remained disabled under the documented container limitation. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Linux adapter SHA-256: `e46f8534b5a3a4b3d1b2241146f51a64a2aef87071d5a8d3f5688150c669988c`; test source SHA-256: `a56b502cebdfe12a3c60254ed54c18edebfdbab285aaaeb21f81952c8ff07462`; Debug editor/test SHA-256: `91ff7d3e55a3b1545a4374c25ea2edb64d9ef4eea0f9ebda85d88e2c6a0c1a66` / `b9ed9265bbcc99ee3fd4a088cef348e6a82a18976fa7332af73c24bc067618e5`; ASan/UBSan editor/test SHA-256: `c3d5598411fd3e09392b3ba282dea0ad01c1dc6361307be3d98b3f30d6bca441` / `5dde2604bca403c398420b200ad68819f18fa2ea41ed7bd75ab1cb110f4a01bc`. Native Windows/macOS watcher behavior and desktop acceptance remain unverified; the corresponding tasks stay open.

## Final Linux Debug regression after ProjectSession root recovery (2026-11-08)

Rebuilt the Debug tree and ran the complete CTest suite after adding the ProjectSession source/asset degraded-status recovery regression. The final run accounted for 70 tests: 69 passed and `platformator_benchmark_contract_test` returned its configured skip. The AT-SPI private-session test passed when CTest was run with permission to bind its temporary local D-Bus socket. One preceding sandboxed full run failed only that D-Bus setup; another full run also hit the known intermittent renamed-subtree child-event assertion. The focused watcher suite then passed 20 consecutive runs, and the final full run passed it. The watcher failure was not reproduced or attributed to a specific lost event; it remains a rare unresolved risk rather than a claimed fix. The focused ASan/UBSan watcher integration test had passed after the same ProjectSession regression was added; LeakSanitizer remains disabled under the documented container limitation.

```text
cmake --build bin/editor-debug --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_asset_watch_integration_test$' --repeat until-fail:20 --output-on-failure --timeout 120
dbus-run-session -- bin/editor-debug/platformator_editor_accessibility_linux_test
ctest --test-dir bin/editor-debug --output-on-failure --timeout 600
```

Linux `6.12.112-1-MANJARO x86_64`; source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Debug editor SHA-256: `91ff7d3e55a3b1545a4374c25ea2edb64d9ef4eea0f9ebda85d88e2c6a0c1a66`; watcher integration test SHA-256: `b9ed9265bbcc99ee3fd4a088cef348e6a82a18976fa7332af73c24bc067618e5`; Linux accessibility provider test SHA-256: `79bb6841423aa0c50521c87345630ae3242a442e7f7b9b20e218c71a5e7633f0`; Linux watcher adapter SHA-256: `e46f8534b5a3a4b3d1b2241146f51a64a2aef87071d5a8d3f5688150c669988c`; watcher test source SHA-256: `a56b502cebdfe12a3c60254ed54c18edebfdbab285aaaeb21f81952c8ff07462`. The intermittent renamed-subtree report remains open for diagnosis; native Windows/macOS watcher lanes and desktop acceptance remain unverified.

## Linux local install lane and integrated regression after wake-callback changes (2026-11-08)

The local CMake validation lane now includes the logical accessibility test. The validation script completed its Linux build/install checks, relocated arbitrary-working-directory launch, dependency inspection, and editor-OFF/session-OFF SDK consumer check. A subsequent integrated Debug CTest run passed every runnable test, including the accessibility wake-callback ordering/exception regression and Linux AT-SPI provider test. `platformator_benchmark_contract_test` returned its configured skip; no benchmark campaign was run.

```text
cmake -P cmake/PlatformatorEditorValidation.cmake
ctest --test-dir bin/editor-debug --output-on-failure --timeout 120
git diff --check HEAD
```

CTest accounted for 70 tests: 69 passed and one configured skip. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. Debug editor SHA-256: `11c9f4494d966a6981064a325602f6da1caabc489704822ce36a89c6871f244c`. This Linux headless result does not provide a live GUI walkthrough, Windows/macOS verification, GitHub Actions execution, or comparative performance evidence; those acceptance items remain open.

## Shared watcher owner across source and asset roots (2026-11-08)

`ProjectSession` now starts one native watcher for both source and asset monitoring. A nested asset root is included in the project-root watch; a disjoint asset root is passed as an additional root. The callback still updates source freshness and asset invalidation separately. The Windows adapter now owns up to eight roots with one worker and shared watch budget; the macOS adapter registers deduplicated roots and parent recovery paths in one FSEvents stream. Equal-root and disjoint-root owner integration cases are no longer Linux-only, so they will exercise the selected backend when native lanes are available.

Linux verification:

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test platformator_editor --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_asset_watch_integration_test$' --repeat until-fail:20 --output-on-failure --timeout 120
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test platformator_editor --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan -R '^platformator_editor_asset_watch_integration_test$' --output-on-failure --timeout 120
ctest --test-dir bin/editor-debug --output-on-failure --timeout 600
git diff --check HEAD
```

The watcher integration passed 20/20 Debug repetitions (72.80 seconds), then passed 1/1 under ASan/UBSan with LeakSanitizer disabled. The complete Debug suite accounted for 70 tests: 69 passed and the benchmark contract test returned its configured skip. The Linux accessibility provider test required a private-session D-Bus socket; the full run passed after running outside the sandbox. `git diff --check HEAD` passed. The watcher regression covers the shared owner for equal and disjoint roots, routing an asset edit without changing source freshness, direct project-root CMake edits while nested assets are watched, and Linux root loss/recovery. Linux 6.12.112-1-MANJARO x86_64; source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus worktree changes. `projectsession.cpp` SHA-256 `be05853b817b2a88396d57e188e4844659735568e77091fdb464e935ac64a019`; `projectsession.h` `93f105728243caf18907220329e59ca5d65ac8d4f4b35a72b57a1b909aab16cd`; Windows adapter `ad6498e372c8ec5617e00042faebb6042df9fb3a60bdf77b0fb50c3579926892`; macOS adapter `e482d0bb829bd57fcc4cab9cc25f32984879ec2cf2a4986dc0989b8ba629c9e5`; integration source `7eb45c6a7a7fa051b8583e8ba3d842ebd7fb4286a7387565134baf7815ea9509`; Debug test executable `848311c220756fa32d4b5ae4c6d161e6e721946a0836adf9ac4aa282a6873ccd`; ASan/UBSan test executable `bf5d99cd70e09a6b8c4ccdfd0f3906b0b9b407bd94ae7d034194337c772cdac3`. Windows/macOS sources remain uncompiled and unrun; no native support is claimed and T162/T205 remain open.

## Superseded-state cleanup audit continuation (2026-11-08)

The scoped source scan found the extension map centralized in `editor/src/assetformats.h`; Project listing, preview, animation, tabs, and typed-reference paths consume that mapping. The reference-search text map was already moved into per-ImGui-context storage. The remaining refresh controls in `editor/src/editorapp.cpp` and `editor/src/panels/project.cpp` are explicit user actions for source identity, catalog discovery, validation, or asset reconciliation; the scan found no superseded app-global refresh path in the four T142 target files. The remaining static storage found there is the bootstrap directory collision counter and an immutable empty hidden-object set. No additional deletion was justified by this audit. T142 remains open because its dependent story/platform acceptance is incomplete; this scan is not a completion claim.

The full Debug regression suite above also supplies current headless acceptance evidence for T143: every runnable engine, editor, reference, asset, scene, fixed-step, render, and install test completed, with the benchmark contract test skipped by configuration. The focused ASan/UBSan watcher suite covers the lifetime-sensitive watcher changes. T143 is complete for the current Linux headless source/build; desktop, Windows, and macOS acceptance remains open under T145/T198/T241.

## Converged Run, animation schema, Save As ownership, and recovery fixes (2026-11-09)

Implemented T264–T267. The Linux Debug build compiled and linked `platformator_editor` plus the four affected headless suites. The Run wait now carries project/tab/document identity, newer Run intents supersede prior tickets, tab/project replacement retires the wait, and Stop cancels before the end-of-frame continuation can launch. Animation Save checks the complete Runtime root schema and float dimensions while preserving malformed drafts. Scene and animation Save As resolve and write through canonical destinations, rejecting other open owners before writing; the manager rechecks ownership when delayed overwrite results arrive. Root recovery validation is scheduled once per project/uncertainty epoch, allowing a pending result to publish while coverage remains dirty.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_startup_test platformator_editor_diagnostics_test platformator_editor_tabs_test platformator_editor_animation_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_(startup|diagnostics|tabs|animation)_test$' --output-on-failure --timeout 60
git diff --check
```

Outcome: all four focused headless tests passed. Startup covers tab switch, close/reopen, project replacement, Stop after readiness, newer Run supersession, and selected document identity. Diagnostics covers unchanged `rootDirty` epochs across repeated frames, publication of the original recovery request, and a new request after overflow advances uncertainty. Tabs covers dirty Scene/animation owners, symlink aliases, late owner arrival, own-source Save As, and stale callbacks after close/reopen. Animation covers missing/wrong-kind root fields, overflow/underflow and negative dimensions, draft/disk/history preservation, and save/reopen plus ordinary Runtime loading of a valid Add Frame asset.

Build identity: HEAD `b45bdff923aed012f96e2de1b5554541679636f1` with a dirty worktree; Ninja, Debug, GCC 16.2.1, editor ON, session ON, work probes ON, CMake 4.4.4. SHA-256: editor `96ef86776b16947f964308bdccd507475b28a8322d7a0e9f6b54e06266d2c099`; startup `fac1134120ea99a813d63d1ee1ddcad1d08541fb485c23f95ee26e8fed512d21`; diagnostics `dbc37b36e957bfd7a2639077f6f776577ae5e24698c71575faa8a99f1ea66db1`; tabs `2ccebfd9ba1eb7722a4270698bd8294945e932d48fd98f4845d7d8a71aaed3bb`; animation `6e87ea538886aa8ffc061a6f372222a4592bda8484f305fa18953708b73f4d35`.

This was a focused rerun, not the complete editor suite. It does not certify native desktop behavior, Windows/macOS builds, or manual walkthroughs. No GUI benchmark was run because the retained behavior-neutral baseline executable is unavailable; comparative performance criteria remain unmet.
