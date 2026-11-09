# US9 evidence

## T120 bounded protocol foundation

Added the opt-in editor-session message schema and bounded framing helpers in `src/editorprotocol.h` and `src/editorprotocol.cpp`. Frames use a 32-bit little-endian length; the stream decoder accepts partial/coalesced reads, checks the direction-specific limit before copying the payload, and fails on invalid JSON, wrong envelope fields, and EOF inside a frame. Request/event envelopes carry v1, a bounded nonce, positive generation and sequence, a type, and an object body; events also require a runtime serial.

Catalog capability parsing preserves the current schema-v1 behavior catalog, treats missing session capabilities as unsupported, validates fixed 640×360/≤15 Hz frame capability, and rejects `input_forwarding=true`. Bounded queues enforce 64 messages/1 MiB, with optional events capped below the 16 KiB reliable-control reserve. Sequence and one-inspection-page gates reject replay and concurrent inspection pages.

The protocol source is excluded from ordinary engine builds while `PLATFORMATOR_ENABLE_EDITOR_SESSION` is OFF (the option defaults OFF); its focused test compiles the protocol implementation directly so runtime-only consumers do not acquire session code by default.

Command: `cmake --build build --target platformator_editor_protocol_test -j8 && ctest --test-dir build -R '^platformator_editor_protocol_test$' --output-on-failure && git diff --check`

Outcome: build passed, protocol test passed 1/1, and `git diff --check` passed. Tests cover partial/coalesced framing, oversized and malformed/truncated frames, request-size rejection, event identity, capability absence/validation, count/byte queue limits, reserved control capacity, replay rejection, and the single-page gate. No session child runtime or Windows/macOS verification was performed.

## T117 tab identity and isolation regression

The focused `platformator_editor_tabs_test` passes in the Linux Debug build. It covers two external scene paths, independent selection/view/expansion state, tab-scoped external-file acknowledgements, canonical-path reactivation, dirty close cancellation, and late dialog results after a same-path tab is closed and reopened. Async dialog results remain qualified by the originating `DocumentIdentity`.

Command: `cmake --build bin/editor-debug --target platformator_editor_tabs_test -j2 && ctest --test-dir bin/editor-debug -R '^platformator_editor_tabs_test$' --output-on-failure`

The T117 regressions and T119 tab lifecycle implementation are recorded below. Play-session behavior and native desktop walkthroughs remain open under later tasks.

## T119 tab lifecycle implementation

The app now renders scene tabs with dirty and external-change markers, opens scenes into separate tabs, restores per-tab view/expansion state, switches the active workspace/history/selection/preview binding, supports Save All with partial-failure reporting, and prompts before closing a dirty tab. Save As cancellation leaves an untitled tab open. Async file-dialog results remain keyed to their originating document identity. `DocumentManager::save_all()` and the related tab regressions pass in the Linux Debug build; the editor executable also builds.

Commands: `cmake --build bin/editor-debug --target platformator_editor platformator_editor_tabs_test platformator_editor_workspace_test -j2`; `ctest --test-dir bin/editor-debug -R '^(platformator_editor_tabs_test|platformator_editor_workspace_test)$' --output-on-failure` (2/2 passed).

No interactive tab-bar walkthrough or Windows/macOS verification was performed in this pass. Play-session routing is still open under T123–T133.

## T192 immutable Run target and protected close regression

`DocumentManager::capture_active_scene_launch_snapshot()` now captures the active tab's `DocumentIdentity`, canonical source path, and authored scene payload by value. `editorapp.cpp` places that identity and snapshot in the asynchronous `PipelineRequest`; switching tabs or editing after capture cannot retarget an in-flight Run. Untitled tabs cannot produce a saved-scene launch snapshot.

The tab regression also covers two dirty scenes, canonical-path reactivation, close Save/Discard/Cancel, Save All stopping at a conflict or untitled scene, and a late dialog result from a closed tab not targeting a reopened tab at the same path.

Command:

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_tabs_test platformator_editor_process_test -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_tabs_test|platformator_editor_process_test)$' --output-on-failure
cmake --build bin/editor-debug --target platformator_editor_tabs_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_tabs_test$' --output-on-failure
```

Outcome: both builds passed; the first CTest run passed 2/2 and the extended tab close/launch regression passed 1/1. Native tab-bar walkthrough remains unavailable, and Windows/macOS verification was skipped as requested.

## Optional protocol and stock-runner catalog integration (T193/T236)

Added a runtime-only `playsession_test.cpp` that invokes the shared runner's exclusive `--editor-catalog` query and verifies the schema-v1 envelope, absence of Runtime/SDL initialization, and compile-time capability behavior. With `PLATFORMATOR_ENABLE_EDITOR_SESSION=OFF`, the stock runner omits `editor_capabilities`; with it ON, the catalog passes the v1 capability parser and advertises no play control, inspection, frame stream, live reload, or input forwarding until those handlers exist. The editor catalog test also validates its existing project/source/binary identity and side-effect-free query behavior in both configurations. The protocol suite covers framing including oversized serialized plans rejected by the 64 KiB request envelope, queues, reliable-control reserve, sequence replay, capability fallback, and the single-page gate. A play-session child/runtime is not implemented or claimed here.

Default-off commands:

```text
cmake --build build --target platformator_editor_playsession_test platformator_editor_catalog_test platformator_editor_protocol_test -j8
ctest --test-dir build -R '^platformator_editor_(playsession|catalog|protocol)_test$' --output-on-failure
git diff --check
```

Opt-in commands:

```text
cmake -S . -B build/editor-session-on -DPLATFORMATOR_BUILD_EDITOR=ON -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_RUNNER=ON -DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON -DPLATFORMATOR_ENABLE_WORK_PROBES=OFF
cmake --build build/editor-session-on --target platformator_editor_catalog_test platformator_editor_protocol_test platformator_editor_playsession_test -j8
ctest --test-dir build/editor-session-on -R '^platformator_editor_(playsession|catalog|protocol)_test$' --output-on-failure
```

Outcome: the default-off configuration passed 3/3; the opt-in Linux build compiled `src/editorprotocol.cpp` into the engine and passed 3/3. The catalog query remained Runtime/SDL-free in both cases. No child session, Windows/macOS build, or native desktop verification is claimed; those remain under T194–T198 and platform-specific tasks.

Development builds launched from Editor Run Settings now pass `-DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON`, including configure-preset builds. Production builds explicitly pass OFF so switching an existing build directory cannot retain an opt-in session flag. The editor and startup test rebuilt successfully; `platformator_editor_startup_test` passed 1/1. T122 remains open for the dependent native source-selection/install lane in T088, which has unverified Windows/macOS adapters.

The real shared runner target was also built in both configurations and queried directly with `--editor-catalog`. Default-off emitted schema v1 without `editor_capabilities`; opt-in emitted `platformator-editor-session-v1`, with `play_control=false` and `input_forwarding=false`. The stock `main` target links the engine, without `platformator_editor_core` or ImGui.

```text
cmake --build build --target platformator_editor platformator_editor_startup_test main -j8
ctest --test-dir build -R '^platformator_editor_startup_test$' --output-on-failure
./build/main --editor-catalog
cmake --build build/editor-session-on --target main -j8
./build/editor-session-on/main --editor-catalog
git diff --check
```

The two direct catalog queries parsed as JSON and their option-gated capability assertions passed. T122 remains open for the dependent native source-selection/install lane in T088, which has unverified Windows/macOS adapters.

## Linux parent-side PlaySession controller progress (T123/T194/T237)

Added `editor/src/playsession.h` and `editor/src/playsession.cpp` as a parent-side controller over the bounded native-channel interface. It tracks the required configuration/build/catalog/start/run/pause/stop lifecycle, immutable document/saved-file/pipeline launch identity, generated nonce and monotonic play generation, sequence-checked events, partial nonblocking command writes, bounded event delivery, pause/resume acknowledgements, exactly-once Step completions, capability gates, and stop/reap/cancel/unmap ordering. Optional frame-capture requests preserve eight command slots and 16 KiB for reliable controls. Malformed, stale, replayed, or out-of-order events enter a visible Stopping state; channel I/O is cancelled and the mapping is released during owner cleanup after child reap.

`editor/tests/playsession_test.cpp` now exercises fake partial channels, write backpressure and recovery, pause/step/resume sequencing, duplicate Step/event rejection, stale nonce and replay rejection, queue reserve, fixed mapping admission/cleanup, EOF, bounded coalesced event processing, and runtime-only side-effect-free catalog negotiation. This is the parent controller only: no child session entry, game runtime pause hook, frame-slot publication/readback, Game panel, or process-launch integration is claimed. T123 remains unchecked because its T122/T087 prerequisites are incomplete; T118 remains open for slot/readback/child-process cases.

Commands and outcome:

```text
cmake --build bin/editor-debug --target platformator_editor_playsession_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_playsession_test$'
cmake -S . -B bin/editor-debug -DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON
cmake --build bin/editor-debug --target platformator_editor platformator_editor_playsession_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_playsession_test|platformator_editor_hierarchy_move_test)$'
git diff --check && git diff --cached --check
```

The focused PlaySession and Hierarchy regressions passed 2/2. The default-off isolated PlaySession test build and the opt-in Linux editor/core build both compiled. No child runtime, Windows/macOS build, or native desktop walkthrough was run.

## Failure-path stop/reap ordering (Linux, 2026-11-07)

Protocol failures no longer cancel private command/event channels while the owned child can still be alive. A failed session stops polling further protocol traffic, then waits for the owned process stop/reap path to finish. Owner cleanup cancels both channels and closes the shared mapping in `finish_stop_after_reap()`. The Game panel observes the retired play generation afterward and destroys its renderer-local frame texture. The stale nonce and replay regression asserts that I/O remains open before owner cleanup and is cancelled by post-reap cleanup.

The PlaySession regression passed under AddressSanitizer/UBSan, and the full RelWithDebInfo build/CTest suite completed afterward. This implements Linux/headless teardown ordering; Windows handle closure and a native Game panel shutdown walkthrough remain open.

## Linux child-side session entry progress (T124)

Added `src/editorsession.h` and `src/editorsession.cpp`, compiled only when `PLATFORMATOR_ENABLE_EDITOR_SESSION=ON`. The runner recognizes `--editor-session-v1` with the fixed child descriptors 3/4/5. It constructs the child service before `Runtime`; on POSIX the service validates distinct nonblocking pipe endpoints, access directions, the exact 1,843,200-byte shared mapping, and close-on-exec flags before mapping memory or allowing scene execution. Invalid descriptors fail before SDL/Runtime startup. After the runner creates its single Runtime, a nonblocking owner-thread hook services at most 256 KiB/64 requests per pump. `hello` returns capability and Ready events on the independent event pipe; Stop is sequenced, and protocol bytes never use stdout/stderr.

The opt-in fake-channel/pipe tests verify CLOEXEC, exact mapping admission, invalid-map rejection before Runtime construction, capability/Ready handshake sequencing, Stop completion, and that the Runtime owner hook runs between frames without creating a second Runtime. This does not implement pause/step, frame capture, parent process launch/reap wiring, or a Windows handle-list child adapter. T124 remains unchecked pending process integration and the unverified native adapter lane.

Commands and outcome:

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_playsession_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_playsession_test|platformator_editor_hierarchy_move_test)$'
c++ -std=c++20 -DPLATFORMATOR_ENABLE_EDITOR_SESSION=0 -Iinclude -Isrc -fsyntax-only src/runtimeoptions.cpp
git diff --check && git diff --cached --check
```

The opt-in editor/runner build succeeded; focused PlaySession child/parent and Hierarchy regressions passed 2/2; the default-off runtime-options parser syntax check passed. Windows/macOS verification and a real parent-launched child-process walkthrough were not run.

## Linux parent process and fixed-step control progress (T123/T125)

The current parent `PlaySession::start_process` now sends the initial hello on the private command channel, launches through `ProcessService` with separate bounded stdout/stderr logs, polls the protocol nonblockingly, and retains the mapping until child reap. Stop first sends a sequenced protocol request and escalates to owned-process termination after a two-second grace period. The integration fixture launches the test executable as a child, verifies capability/Ready sequencing, receives the Stop acknowledgement, checks independent logs, and confirms cleanup follows reap.

The opt-in child loop now services Pause, Resume and Step on the Runtime owner thread in Debug and Release. Pause/Resume acknowledgement follows the state change at the frame boundary; Step invokes one existing `FIXED_TIME_STEP` and emits one completion after the simulation step. The owner-loop fixture checks the request/ack sequence and confirms Resume does not replay paused wall-clock debt. `scriptregistration.cpp` advertises `play_control=true` only in the opt-in build where these handlers are compiled.

Linux Debug and Release commands:

```text
cmake --build bin/editor-debug --target platformator_editor_playsession_test platformator_editor_nativeprocess_test platformator_editor_hierarchy_move_test platformator_runtime_step_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_playsession_test|platformator_editor_nativeprocess_test|platformator_editor_hierarchy_move_test|platformator_runtime_step_test)$'
cmake -S . -B bin/editor-release -DPLATFORMATOR_BUILD_EDITOR=ON -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON
cmake --build bin/editor-release --target platformator_editor_playsession_test platformator_editor -j2
ctest --test-dir bin/editor-release --output-on-failure -R '^platformator_editor_playsession_test$'
git diff --check && git diff --cached --check
```

Outcome: the four focused Debug tests passed 4/4; the opt-in Release PlaySession test passed 1/1. Release compilation emitted a GCC `-Wstringop-overflow` diagnostic in the existing bounded decoder vector insert; compilation and tests completed. This is Linux headless evidence only. No Windows/macOS adapter or desktop verification is claimed. T125 is complete; T123/T124 and T237 remain open for the unimplemented cross-platform channel adapter and remaining Game/frame integration.

## Linux bounded frame-slot transport (T126)

Session Runtime options now create a hidden 640×360 child window with high-density scaling disabled. The child validates the renderer output size and only reads pixels when `set_capture.visible` is true, at a minimum 66,667-microsecond interval. It converts the pre-present SDL readback to RGBA32 and copies exactly 360 rows of 2,560 bytes into one of two 921,600-byte slots. It skips readback when both slots are occupied or optional event capacity is near its reserved control limit. The parent validates dimensions, stride, format, slot and monotonic frame identity, copies exactly one slot into owned pixels, and only then queues release. A matching child `frame_released` event retires the parent's slot identity.

The PlaySession test includes an actual hidden SDL dummy/software renderer run. It verifies 640×360 hidden output, no capture before visibility, RGBA8 pixels in the shared mapping, release acknowledgement and Stop. A separate parent fixture checks full-slot copying, rejection of release before copy, stale slot retirement, and exact release request identity.

Commands and outcome:

```text
cmake --build bin/editor-debug --target platformator_editor_playsession_test platformator_editor_nativeprocess_test platformator_editor_hierarchy_move_test platformator_runtime_step_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_playsession_test|platformator_editor_nativeprocess_test|platformator_editor_hierarchy_move_test|platformator_runtime_step_test)$'
cmake --build bin/editor-release --target platformator_editor_playsession_test platformator_editor -j2
ctest --test-dir bin/editor-release --output-on-failure -R '^platformator_editor_playsession_test$'
git diff --check && git diff --cached --check
```

The focused Debug set passed 4/4 and Release PlaySession passed 1/1; after tightening parent release to require a successful copy, the PlaySession suite passed again in both Debug and Release. This is Linux dummy/software renderer evidence, with no Windows/macOS verification or interactive editor Game panel claim. T126 is complete; T129/T237 remain open for editor texture upload and Game controls.

## Conditional session build and generated inspection accessors (T122, T127)

`PLATFORMATOR_ENABLE_EDITOR_SESSION` defaults OFF. The engine source list excludes `editorprotocol.cpp` and `editorsession.cpp` while OFF; the editor adds `playsession.cpp` only while enabled. Development runner builds pass the option ON, while production builds force it OFF. Session protocol sources contain no ImGui or native accessibility dependency, and SDK installation exports only the runtime engine target. The opt-in `bin/editor-debug` configuration compiled the shared runner and play-session test without game-source edits. `bin/debug` was configured with both editor and session OFF, built `platformator_engine`, and installed the package to `/tmp/platformator-session-off-install`; the package exports no ImGui, editor or accessibility artifacts. The first install attempt followed an engine-only build and exposed oneTBB install rules for optional `tbbmalloc`, `tbbmalloc_proxy` and `tbbbind_2_5` targets; after building those packaged dependency targets, the install completed.

The `SERIALIZABLE_SCRIPT` macros now register a type-checked, on-demand read-only provider generated from explicitly declared members. It handles primitive values, bounded strings and sequences, optionals, enums, vectors/rectangles, object references and asset references. Each field is capped at 1,024 UTF-8 bytes of string data and 32 total sequence items; the full request is capped at 64 fields and a 64 KiB aggregate output estimate (defaults are lower). Object references return file identity plus runtime serial/slot/generation summaries; asset references return a bounded path and runtime serial. Unsupported/custom fields are returned as unavailable, and providers never call behavior serialization or retain object pointers. Factory-only behaviors have no provider and remain unavailable.

The catalog fixture verifies declared values, reference summaries, UTF-8 and sequence truncation, field/output byte caps, empty/factory-only providers, and that opaque custom `to_json` is never invoked:

```text
cmake --build bin/editor-debug --target platformator_editor_catalog_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_catalog_test$' --output-on-failure
cmake -S . -B bin/debug -DPLATFORMATOR_BUILD_EDITOR=OFF -DPLATFORMATOR_ENABLE_EDITOR_SESSION=OFF -DPLATFORMATOR_BUILD_TESTS=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_INSTALL=ON
cmake --build bin/debug --target platformator_engine -j2
cmake --build bin/debug --target tbbmalloc tbbmalloc_proxy tbbbind_2_5 -j2
cmake --install bin/debug --prefix /tmp/platformator-session-off-install
```

Outcome: `platformator_editor_catalog_test` passed 1/1 on Linux. At that point `runtime_inspection` remained false pending T128; the child capability is enabled in the T128 evidence below. Game UI controls remain under T129.

## Linux bounded child runtime inspection (T128)

The child now accepts `list_objects` and `inspect_object` only when runtime inspection is advertised. Requests carry `runtime_serial`; GameObject inspection additionally requires a `game_object` handle with slot and generation. The child resolves that tuple through the active Runtime registry before calling getters and responds `stale` for foreign Runtime identities or retired/reused handles.

Hierarchy pages contain bounded handle/file-ID/name/active/parent/depth/sibling summaries. The child retains only copied JSON summaries, capped at 4,096 objects, 256 levels and 128 UTF-8 bytes per name. Continuations carry a monotonic snapshot revision and exact next cursor; before returning another page, the child rebuilds and compares the bounded summary so a changed hierarchy returns `stale` instead of mixing revisions. Page size is at most 64 rows. Selected snapshots include bounded GameObject transforms, child handles and built-in component values. Script component fields use only generated providers with bounded output; factory-only schemas and custom fields remain unavailable. No behavior serializer or full-scene dump is called.

The opt-in Linux child fixture creates nested objects, changes the hierarchy between pages, verifies stale continuation rejection and a fresh page, inspects built-in and generated script values, destroys the inspected object and confirms its old handle is rejected. The catalog and hierarchy click regressions also pass:

```text
cmake --build bin/editor-debug --target platformator_editor_catalog_test platformator_editor_playsession_test platformator_editor_hierarchy_move_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_catalog_test|platformator_editor_playsession_test|platformator_editor_hierarchy_move_test)$' --output-on-failure
git diff --check
```

Outcome: all three focused Linux tests passed. The opt-in runner now advertises `runtime_inspection=true` because the child handlers are available. Game panel controls/image upload remain open under T129; Windows/macOS verification was skipped.

## Linux PlaySession failure and ownership regressions (T118)

Moved the owned-process handshake fixture into `editor/tests/session_process_fixture.cpp` so the controller test launches a separate executable over the inherited channels and mapping. The session suite also checks malformed/truncated/oversized protocol data, capability absence and rejection, bounded queues and reliable reserve, replay/stale events, EOF, partial writes and recovery, exactly one accepted fixed Step completion, hidden zero-readback, complete frame-slot copying/release, stale object handles, and child-side full-slot backpressure. The backpressure case snapshots each published slot and verifies the bytes remain unchanged until stop; no third frame is announced while both slots remain owned. The hierarchy inspection case writes an authored scene once before the session and verifies its bytes remain identical after runtime inspection, object destruction and Stop.

```text
cmake --build bin/editor-debug --target platformator_editor_playsession_test platformator_editor_session_process_fixture -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_playsession_test$' --output-on-failure
git diff --check
```

Outcome: the opt-in Linux headless PlaySession suite passed 1/1, including separate-process launch/reap and the hidden dummy-renderer slot checks. Windows/macOS verification was skipped; no native desktop Game UI walkthrough was performed.

## Session-off controller test linkage (2026-11-07)

The session-off PlaySession target previously compiled `playsession.cpp` directly without linking the editor core that provides `ProcessService` and `BoundedConsole`, so the test failed at link time. The session-off test still compiles the controller source directly and now also links the editor core; the session-enabled path continues to use the core's single controller object. The test compiles only the protocol implementation it needs for its session-off fixture, and neither path adds editor dependencies to the exported engine target.

```text
cmake --build build --target platformator_editor_nativefile_test platformator_editor_nativeprocess_test platformator_editor_playsession_test -j2
ctest --test-dir build -R '^(platformator_editor_nativefile_test|platformator_editor_nativeprocess_test|platformator_editor_playsession_test)$' --output-on-failure
```

Outcome: all three Linux headless tests passed 3/3. This fixes the session-off test lane and does not complete Game panel integration or native Windows/macOS session adapters; T123/T124/T133/T237 remain open.

## Linux embedded Game panel and restart integration (2026-11-07)

The opt-in Build & Run path now carries the immutable saved-scene identity through configure/build, finalizes the produced executable identity before catalog validation, then launches only when the built runner advertises the required session and frame-stream capabilities. The Game panel uses a texture created on the editor renderer, fits the fixed 640×360 image without changing its aspect ratio, requests capture only while visible, and copies/releases each shared slot before returning it to the child. Pause, Resume, one-step, Stop, runtime hierarchy paging and object snapshots remain read-only with respect to authored scene data/history. Semantic nodes expose the frame dimensions, current controls, bounded runtime objects, status and Build & Restart.

The Run Settings embedded-view checkbox is opt-in; the unchecked path retains the external game window and its native input/resolution. Unsupported session capabilities remain visible in the Game panel with external-window guidance. Closing a scene that launched the live session asks whether to stop it first or keep it running from the saved launch snapshot. Source changes stop a session built from stale source; asset changes are reported because this runner does not advertise live asset reload. Build & Restart routes back through the normal current-scene validation/save gates and stops the prior child first.

The new Linux fixture covers the full embedded pipeline through capability query, child handshake and owned Stop/reap. A software-renderer Game panel test checks visibility-gated capture, texture upload, exact slot release, semantic Pause and restart actions, status after a session ends, and mapping cleanup before texture teardown. The Hierarchy regression additionally now requires a physical SDL Delete key-down edge, so ImGui key state alone cannot trigger object deletion.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_playsession_test platformator_editor_hierarchy_move_test platformator_editor_process_test -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_playsession_test|platformator_editor_hierarchy_move_test|platformator_editor_process_test)$' --output-on-failure
git diff --check && git diff --cached --check
```

Outcome: the focused Linux headless set passed 3/3. An X11 walkthrough was unavailable (`xdpyinfo` could not open `:0.0`), and Windows/macOS verification was skipped. The Game-panel implementation is present, but the remaining session/channel platform dependencies, native walkthrough and complete stale/restart/close acceptance keep T123/T124/T129–T133 and T237–T239 open.

## Release PlaySession fixture (2026-11-07)

Configured a separate Linux Release build with editor session support enabled and debug tools disabled, then built the application and session test:

```text
cmake -S . -B bin/editor-release-session -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_BUILD_EDITOR=ON -DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF -DPLATFORMATOR_SANITIZER=none
cmake --build bin/editor-release-session --target platformator_editor platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-release-session -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
```

Outcome: Release application and test built, and the PlaySession suite passed 1/1. Together with the earlier Debug fixture, this exercises pause/step acknowledgements, frame-slot ownership/backpressure, inspection handles and no-authored-mutation behavior in both configurations. Native Game panel and external-window walkthroughs remain unavailable without a display; no Windows/macOS verification was performed, so T133 remains open.

## Embedded source-staleness stop regression (2026-11-07)

Extended the integrated `GamePipeline` fixture to launch a live embedded child, change its project identity while Running, and require the invalidation path to stop/reap the process, finish session-channel cleanup, return to Idle, and reject the stale launch. The Game panel contract also drives semantic Pause → one Step → Resume through matching session acknowledgements. This validates project-switch invalidation and logical controls; it does not synthesize a native watcher event through the full application loop, verify asset-reload capability integration, or replace the game-panel stale/restart walkthrough. T131/T196/T239 and final T133 acceptance remain open.

```text
cmake --build bin/editor-debug --target platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
cmake --build bin/editor-release-session --target platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-release-session -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
git diff --check
```

Outcome: both session-enabled configurations built and passed 1/1; `git diff --check` passed. Windows/macOS verification was skipped as requested.

## Generated inspection fallback and recreated handles (T195/T238)

Extended the child inspection fixture with an unregistered custom behavior and a replacement GameObject. The selected snapshot exposes typed fields only through the generated read-only provider; the opaque behavior is explicitly marked provider-unavailable without calling serialization. After destroying the selected object and creating a replacement, the replacement has a distinct `(slot, generation)` incarnation and the prior handle remains rejected. The fixture also compares the authored scene bytes before and after inspection to prove the inspection path does not save or mutate the authored scene. The existing owner-loop fixture verifies one fixed Step, matching acknowledgements, Resume without paused-clock catch-up, and responsive servicing; these checks ran in both session-enabled Debug and Release builds.

```text
cmake --build bin/editor-debug --target platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-debug -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
cmake --build bin/editor-release-session --target platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-release-session -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
```

Outcome: both builds succeeded and the session suite passed 1/1 in each configuration. Native Windows/macOS verification was skipped as requested.

## Linux PlaySession failure and input-fallback coverage (T194/T237, 2026-11-07)

The PlaySession integration now launches a child that terminates by signal and verifies visible failure state, completed reap, retained immutable launch identity until cleanup, and channel/identity release only after reap. The embedded Game's semantic frame help is asserted to direct native gameplay input to the external game window. The running pipeline identity regression now changes project roots, representing project-switch invalidation and requiring the owned child to stop and reap. Existing cases cover EOF, stale nonce/generation, hidden capture, two-slot backpressure/no overwrite, stop/close cleanup, read-only inspection, and the 640×360 RGBA8 mapping.

```text
cmake --build bin/editor-release --target platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^platformator_editor_playsession_test$'
cmake --build bin/editor-asan --target platformator_editor_playsession_test platformator_editor_session_process_fixture --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^platformator_editor_playsession_test$'
```

Both Linux headless runs passed 1/1. At this point the native Windows/macOS session-channel adapters and visual Game/external-window walkthroughs remained open; see the later Windows channel source status update. The adapter implementation tasks are now checked, while platform acceptance remains open under T091/T132/T133/T145/T185/T228.

```text
bin/editor-release/platformator_editor_playsession_test SHA-256: 07e37e8e9a0310e050007c7b74515320ca1875b0f30ca4cc005a360b470ad56d
bin/editor-release/platformator_editor_session_process_fixture SHA-256: 9c39ef5a596fd797d21541c4763c77682404a453af23511332325196d2f29b6d
bin/editor-asan/platformator_editor_playsession_test SHA-256: b131f1bb69d1f7faef7794a3c98b0019f0c9dfa7b63a702c051b164910bc0df6
bin/editor-asan/platformator_editor_session_process_fixture SHA-256: d4fe71edf8914a75da2b9e3e85086899d2bee22f1bb7d8a91970f5b7ee1bf882
```

## Asset and source stale handling (T131/T196/T239)

The app now marks an active embedded session stale for every project asset change. If a runner advertises live asset reload, the Game status explicitly explains that this editor has no safe reload transaction path and offers Build & Restart; an unsupported runner gets the existing capability-specific explanation. This closes the silent-ignore path without claiming that changed assets were applied. The app and session fixture rebuilt in Linux Debug and Release, and the PlaySession test passed in both configurations. A real versioned live-reload transaction is not implemented, so all assets use the explicit restart path. Native event-loop and visual walkthrough evidence remains under T133.

The asset status copy is owned by `GamePanel::asset_change_status_message()` and tested for both capability states; semantic status remains visible and Build & Restart stays actionable. The stock runner advertises `live_asset_reload=false`, so it does not claim a capability for which no request/transaction path exists. Source identity changes invalidate the active embedded pipeline and reap the stale child; the Run path still passes through the saved-scene and current-validation gates instead of automatically rebuilding after a watcher event.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_playsession_test platformator_editor_process_test platformator_editor_asset_watch_integration_test --parallel 4
ctest --test-dir bin/editor-debug -R '^(platformator_editor_playsession_test|platformator_editor_process_test|platformator_editor_asset_watch_integration_test)$' --output-on-failure --timeout 60
cmake --build bin/editor-release-session --target platformator_editor platformator_editor_playsession_test --parallel 4
ctest --test-dir bin/editor-release-session -R '^platformator_editor_playsession_test$' --output-on-failure --timeout 60
```

Outcome: Linux Debug passed all three suites (3/3); Release PlaySession passed (1/1). Coverage includes absent/claimed reload status, asset watcher events, source-identity stale stop/reap, protocol EOF/replay failures, build failure and immutable tab launch identity in the cited focused suites. The notification-to-status path is compiled; the native editor event-loop walkthrough remains open under T133. Live reload is intentionally unsupported and no apply command is sent. Windows/macOS verification was skipped as requested.

T123 and T124 implementation tasks are complete on the Linux native channel path. The state machine, immutable launch identity, nonce/sequence validation, bounded control/event queues, one child Runtime, owner-thread command servicing, descriptor validation, separate stdout/stderr capture and non-mutating source-scene behavior are covered by the PlaySession suite. At the time of this entry the Windows session-channel adapter was unimplemented; see the later Windows channel source status update. T132/T133 remain open for lifecycle verification and desktop walkthrough acceptance.

## Session ownership and channel shutdown recheck (2026-11-07)

Removed an unused second PlaySession ownership slot from `ProjectSession`; `GamePipeline` is the sole owner of child-session lifetime. This avoids project-root replacement appearing to own an independent child while the pipeline separately stops and reaps it. `GamePanel::consume_event()` now also checks the current session generation and nonce before applying an event, so a stale error or frame notification cannot affect a replacement session. Linux Debug rebuilt the Workspace, asset watcher, PlaySession and editor targets; CTest passed 3/3 before the stale-event guard and the focused PlaySession suite passed 1/1 after it. Coverage includes injected stale generation/nonce messages, inherited endpoint validation, process-tree cleanup, session EOF/replay handling, frame-slot release, and stop/reap before channel cleanup. The renderer teardown order and native compatible/external walkthrough remain unverified on a desktop host. The Windows adapter was added later but was not compiled or run; T132/T133 remain open. Windows/macOS verification was skipped as requested.

```text
cmake --build build --target platformator_editor_workspace_test platformator_editor_asset_watch_integration_test platformator_editor_playsession_test -j2
ctest --test-dir build --output-on-failure -R '^platformator_editor_(workspace|asset_watch_integration|playsession)_test$'
```

At the time of this check T132/T133 remained open; the later T132 lifecycle status below closes the implementation item, while native verification remains under T091/T145 and the desktop walkthrough remains under T133.

## Game panel and external-input fallback integration (T129/T130)

The Game panel uses a renderer-owned streaming texture, requests capture only while visible, copies/releases the fixed frame slots before upload, preserves the 640×360 aspect ratio, and exposes distinct Pause/Resume/Step/Stop/inspection semantic actions. Runtime inspection is read-only and session scoped. Run Settings selects embedded mode; external mode keeps the configured native window size and normal game input, while embedded v1 rejects input forwarding and tells users to use the external window for gameplay input. Closing the launch tab offers Stop or keep-running against the immutable saved launch snapshot.

The existing PlaySession integration regression exercises visible/hidden capture, frame upload/release, semantic controls, read-only inspection and stale restart status in both Debug and Release. Protocol tests reject input-forwarding capability claims, and tab/process regressions cover immutable launch identity, close decisions, stop/reap and external process behavior. Linux headless suites passed as recorded above. The interactive Run Settings and source-tab-close walkthrough remains open under T133; detached hosts and renderer replicas remain separately open under T187.

## Windows channel source status update (2026-11-07)

The earlier notes that the Windows session-channel adapter was absent are superseded. `sessionchannel_windows.cpp` and a Windows-only inherited-channel fixture are now authored, and `editor/CMakeLists.txt` selects the platform-native process/file/channel sources. The Linux Debug tree rebuilt after this selection change; native process and PlaySession suites passed 2/2, and the selected ASan/UBSan lifetime set passed 7/7. The Windows adapter and fixture were not compiled or run because Windows verification was skipped. T184/T227 implementation and fixture-authoring tasks are checked; native verification remains under T091/T145, and desktop walkthrough acceptance remains under T133/T185/T228.

## T132 lifecycle implementation status (2026-11-07)

The owner lifecycle implementation is complete: `GamePipeline` owns the single `PlaySession`; stop waits for the owned process to reap before cancelling command/event I/O and closing the shared mapping; `GamePanel` retires renderer-local frame textures on play-generation replacement and before renderer destruction. Nonce/generation checks reject stale session messages, and project replacement stops active project work before replacing roots. POSIX channels close unused descriptors, while the Windows process/channel sources whitelist child handles and close the parent's child-end copies. Linux Debug/Release PlaySession and native-process evidence above covers EOF, replay/stale identity, frame-slot release, stop/reap, process-tree cleanup and renderer retirement.

T132 is checked as implementation work. The Windows adapter and fixture have not been compiled or run, as requested; native Windows/macOS shutdown verification remains open under T091/T145. The actual embedded/compatible/external UI walkthrough remains open under T133.

## Fresh Linux Release session and startup regression (2026-11-08)

After CMake reconfigured the existing Release tree for the updated local validation lane, rebuilt and ran the current process, PlaySession, Workspace, Startup, and SessionContext suites. All five passed. This adds Release coverage for the session fixtures alongside the existing Debug full-suite result. The Game/external-input user walkthrough still requires a visible desktop and remains open under T133.

```text
cmake --build bin/editor-release --target platformator_editor_process_test platformator_editor_playsession_test platformator_editor_workspace_test platformator_editor_startup_test platformator_editor_session_context_test --parallel 4
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^platformator_editor_(process|playsession|workspace|startup|session_context)_test$'
```

Outcome: 5/5 passed on Linux Release. Source revision `b45bdff923aed012f96e2de1b5554541679636f1` plus current worktree changes. SHA-256: process `5315aef44cdf5a5824309d87c5a84d57f188d67471aff6437cfc3f82661fe5f4`; PlaySession `2ed8718a6bab466985be930d1a494da63c10f4cca4adc862eddf831cc75fb826`; Workspace `010db8d7c070ad713d883d94a647f4a554e02d7b2eb229852867a6973de8d966`; Startup `26bf4d0f96c2fd2cee2dfe7aa323650725a0eb0c74fb8ea6ecb56aa212309f08`; SessionContext `b3d8031af42c56ec0ea39d68cc55ae1e09371cfbe8a84eb1cd68ce30b1673dd9`.
