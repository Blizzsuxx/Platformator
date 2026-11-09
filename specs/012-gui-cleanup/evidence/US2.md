# US2 Asset change handling — progress evidence

**Status:** In progress; this is not a US2 acceptance record.

## Build and checks

- Source baseline: `b45bdff923aed012f96e2de1b5554541679636f1`; working tree is modified.
- Host: Linux 6.18.55-1-MANJARO x86_64. CMake build: `bin/editor-debug`, Debug, editor enabled; `PLATFORMATOR_EDITOR_WORK_COUNTERS=ON` for this focused verification.
- Build command: `cmake --build bin/editor-debug --target platformator_editor_core platformator_editor_workspace_test platformator_editor_asset_watch_integration_test platformator_editor_preview_test platformator_editor_document_test -j2` — passed.
- Focused command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(asset_watch_integration|changes|document|preview|workspace)_test$'` — 5/5 passed on 2026-11-06.
- Linux watcher coverage includes add/rename, populated moved directories, subtree rebasing, root replacement/rearm, symlink containment, fan-out, stale request rejection and fallback checks.
- The native asset watcher integration case covers add, same-size/restored-timestamp edit, partial write and completion, rename, atomic replacement, delete, moved-in subtrees, contained aliases, out-of-root links, root replacement, and watcher discovery cap cleanup. Its event waits are bounded by three seconds.
- Enabled work-counter checks cover zero repeated ResourceIndex resolution for an unchanged displayed reference, cached hierarchy labels across idle frames, changed-record transactions, scene decode/upload/submission/capture, document writes, and Project panel invalidation.
- Project scan coverage includes content changes with restored size and timestamp, and the configured source hashing byte cap.
- Preview coverage includes worker-prepared texture adoption, stale-version rejection, owner-thread cache replacement, last-good rendering after failed decode, and preparation state transitions.

## Remaining acceptance work

- Windows `ReadDirectoryChangesW` and macOS FSEvents sources exist but were not compiled or run on native hosts.
- The cached Project browser and referenced Scene have not yet been driven together through a real application frame during watcher changes. These tests do not prove that unrelated reloads, authoring history, and valid UI selection remain unchanged across the full application flow.
- Recovery scheduling still relies on the app loop; a native I/O wakeup is not implemented.
- Source scans now hash configured source contents under a 256 MiB scan cap, with a regression for same-size writes whose timestamp is restored. Detected source changes or uncertain scans disclose that the next explicit Build uses `--clean-first`; an actual desktop Build/Run walkthrough has not been run.
- Animation JSON preparation is asynchronous, but newly introduced frame textures can still be loaded by the Runtime resolver during owner-thread clip adoption. Bounded image preflight and decode memory admission belong to US4 and are not implemented here.
- No actual desktop walkthrough or cross-platform acceptance was run. No benchmark or capture campaign was run.

## Focused convergence follow-up (2026-11-06)

- Reused the existing `bin/editor-debug` Debug build. Build command: `cmake --build bin/editor-debug --target platformator_editor_core platformator_editor_workspace_test platformator_editor_changes_test platformator_editor_asset_watch_integration_test platformator_editor_preview_test -j2` — passed; final incremental invocation reported no work to do.
- Test command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(workspace|changes|asset_watch_integration|preview)_test$' -j2` — 4/4 passed.
- Added coverage for complete inventories containing empty directories and contained aliases; healthy reconciliation now emits path-scoped add/change/remove deltas, while startup and uncertainty recovery keep conservative subtree invalidation. Integration coverage checks missed empty-folder changes and an unused asset without referenced-consumer refresh.
- Alias coverage checks cached child consumers after canonical parent rename/removal, target recreation, broken alias repair, alias-folder removal/recreation and existing containment/cycle cases. Preview coverage verifies Refresh Assets retries an exhausted referenced clip after its frame texture is repaired while leaving an unrelated Scene resource version unchanged.
- The Project button-to-preview retry dispatch and full application-frame watcher path were not exercised by these headless cases; T204 remains open until that integration is covered.
- The first focused integration run exposed a stale alias after canonical-directory recreation. The cache now retains its last-known canonical link until a resolved retarget replaces it; the focused suites passed after that correction.
- This is a Linux headless checkpoint, not US2 acceptance. Windows/macOS watcher builds and lifetime tests, full application-frame watcher walkthroughs, and physical desktop evidence remain open; no benchmark was run.

## Focused convergence follow-up T247 (2026-11-06)

- ResourceIndex now fans a parent-directory event out through retained intermediate symlink-hop paths, including after the parent is missing. Project listings and cached presentations apply the same ancestor match and refresh/rebind only affected logical aliases.
- Scene preview retains the logical frame paths from prepared animation documents so frame consumers stay attached to the alias chain. At owner commit, the engine binds each replacement clip to an already-prepared current frame wrapper and rebinds the serialized texture request alias.
- Build command: `cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test platformator_editor_preview_test -j2` — passed.
- Test command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(asset_watch_integration|workspace|preview)_test)$'` — 3/3 passed (2026-11-06).
- Regressions cover ResourceIndex parent rename/removal/recreation/replacement, stale request rejection and old-target retirement; real Project frames verify last-good previews, refreshed nested listings, current pixels, valid selection/history preservation and unrelated-resource stability; the owner-thread Scene dispatch verifies parent rename/recreation, broken status, sprite/clip/frame pixels and repeated replacement within three seconds.
- This remains Linux headless evidence. Windows/macOS compilation and native watcher lifetime checks, physical desktop/assistive walkthroughs, and full application-frame watcher acceptance remain open. No benchmark or capture was run.

## Focused continuation (2026-11-06)

- Watcher scheduling and replay checks: `cmake --build bin/editor-debug --target platformator_editor_changes_test platformator_editor_workspace_test platformator_editor_asset_watch_integration_test -j2` — passed; `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_changes_test|platformator_editor_workspace_test|platformator_editor_asset_watch_integration_test)$' -j2` — 3/3 passed.
- Source review confirmed watch-before-snapshot ordering, sequence-based replay for events observed during a snapshot, latest-token rejection, full-scan limits of 50,000 files/100,000 entries, targeted listing limits of 10,000 files/2,000 directories/20,000 entries, and rejection of incomplete/truncated results.
- Recovery scheduling uses one outstanding root inventory scan, at most two pending Project listings, 256 inbox events or a 2 ms inbox-processing slice per frame, 512 fallback path stats per two-second interval, a 200 ms quiet / one-second maximum event batch, 60-second consistency scans, and retries capped at 30 seconds.
- The Windows and macOS watcher implementations are present and selected by CMake, but neither was compiled or run on this Linux host. Native and physical desktop acceptance remains open; no benchmark was run.
- Staged resource reload continuation: `cmake --build bin/editor-debug --target platformator_editor_preview_test platformator_editor_asset_watch_integration_test platformator_editor_document_test -j2` — passed; the matching focused CTest filter — 3/3 passed. Coverage includes stale version rejection, last-good asset retention, prepared texture/animation adoption at the owner boundary, dependency refresh, and old-wrapper identity-safe retirement.

## Application-frame watcher continuation T163/T206 (2026-11-06)

- Linux host: `Linux 6.12.112-1-MANJARO`; source revision: `b45bdff923aed012f96e2de1b5554541679636f1` with the working tree changes above it. Reused `bin/editor-debug` (Debug, `PLATFORMATOR_EDITOR_WORK_COUNTERS=ON`).
- Build commands: `cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_asset_watch_integration_test -j2` and, after expanding the integration scenario, `cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test -j2` — passed.
- Focused command: `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_workspace_test|platformator_editor_asset_watch_integration_test)$' -j1` — 2/2 passed on 2026-11-06.
- The real Project frame path now queues coalesced paths and commits at most 256 paths or 2 ms per call, retaining remaining paths for subsequent frames. `OwnerAssetChangePaths` counts committed path invalidations. A 600-path regression verifies the cap, continuation and complete counter accounting.
- Native integration drives ProjectSession notifications through `ProjectPanel::apply_changes` and the same `apply_project_resource_change` seam used by `editorapp.cpp`, with a referenced Scene texture. It covers a same-size/restored-timestamp atomic replacement, unrelated addition, partial write completion with restored timestamp, deletion with last-good Scene pixels retained, and repair. Each change settles within three seconds; enabled counters prove one upload for the referenced replacement/edit/repair, zero for unrelated addition/deletion, while authored JSON/revisions and valid selection remain unchanged.
- Existing focused integration checks cover startup/recovery inventory overlap, rename and subtree moves, overflow/caps, bounded fallback, alias recovery, root replacement, stale generation rejection and watcher stop/drain.
- This is Linux headless evidence. Windows/macOS adapter compilation and native lifetime verification remain open under T162/T205; no desktop walkthrough or benchmark was run.

## Animation frame alias dependency continuation (2026-11-07)

Project animation preview adoption now records each prepared frame texture as a presentation dependency, including frames outside the currently displayed animation frame. This lets a canonical texture change stale every dependent alias animation preview without requiring its tile/frame to be visible. The canonical-alias regression now waits for an actually prepared frame before asserting invalidation and uses an explicit zero-duration value with the FPS fallback; the previous fixture omitted the required numeric duration and failed before testing alias behavior.

```text
cmake --build build --target platformator_editor_startup_test platformator_editor_workspace_test platformator_editor_resources_test platformator_editor_preview_test platformator_editor -j8
ctest --test-dir build -R '^(platformator_editor_startup_test|platformator_editor_workspace_test|platformator_editor_resources_test|platformator_editor_preview_test)$' --output-on-failure
git diff --check
```

Outcome: build succeeded and all four focused tests passed. This does not close the larger US2 native-platform or physical desktop acceptance.

## Linux watcher startup recovery with a missing root (2026-11-07)

The Linux inotify watcher now accepts a temporarily missing root when its parent exists, watches that parent, reports `RootUnavailable`, and installs recursive child watches when the root is recreated. A newly registered Linux regression verifies the parent-only watch count before recovery, the restored root and child notifications afterward, and complete watch-budget release on Stop.

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_asset_watch_integration_test$' --output-on-failure
git diff --check && git diff --cached --check
```

Outcome: the Linux native asset-watch integration suite passed 1/1. Windows/macOS adapters and native verification remain open under T162; this change does not claim cross-platform watcher acceptance.

## Shared Linux source and asset watcher owner (2026-11-07)

Linux now uses one inotify descriptor and worker for project-source plus asset monitoring, whether the configured asset root is nested, disjoint, or the same as the project root. Nested/equal roots are included under the source filter; disjoint roots are added as a second tree. All use the shared 4,096-watch budget. Root availability, notification failures and overflow are routed by path so one disjoint root does not incorrectly mark the other unavailable or advance source freshness. Integration cases check one owner, independent source revisions and asset delivery, and removal/recreation of nested/disjoint roots; equal roots verify source and non-source asset routing.

```text
cmake --build bin/editor-release --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test platformator_editor --parallel 8
ctest --test-dir bin/editor-release --output-on-failure --timeout 120 -R '^(platformator_editor_asset_watch_integration_test|platformator_editor_workspace_test)$'
cmake --build bin/editor-asan --target platformator_editor_asset_watch_integration_test platformator_editor_workspace_test --parallel 8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/editor-asan --output-on-failure --timeout 120 -R '^(platformator_editor_asset_watch_integration_test|platformator_editor_workspace_test)$'
```

Final focused runs passed 2/2 in Release and 2/2 in ASan/UBSan. The restoration assertions check `ProjectCoverageState` because `RootAvailable` is consumed as an internal transition rather than returned as a batch event. Source freshness/status ignores asset-only notifications, including a disjoint asset-root recovery; equal roots now keep source files freshness-relevant while routing non-source assets. Windows/macOS still use their existing adapters and were not verified; T205 remains open.

```text
bin/editor-release/platformator_editor_asset_watch_integration_test SHA-256: 8986b6105d754e54ecc801c77148d46cfd8da425bb1b9a667806ec5b21c10e9d
bin/editor-asan/platformator_editor_asset_watch_integration_test SHA-256: 96dfbfd84265ec8b85322e48a867c7889cceb7f8d4f0b7d59dab2ef8086f77ca
bin/editor-release/platformator_editor_workspace_test SHA-256: d0cda9bcc168b240f270d8f314e3af057979a842bd999015c222148ba7693877
bin/editor-asan/platformator_editor_workspace_test SHA-256: 5153b39238eef43dfda9e7e0f3c4c879436211dca1a88aba77e51782ff9f0e17
```

The same one-worker arrangement now covers disjoint source and asset roots as well as nested roots. Recovery checks confirm an asset-root replacement does not change source freshness. This remains Linux headless evidence; native Windows/macOS compilation and lifetime verification are still open.

## Nested source and asset watcher integration (2026-11-07)

`ProjectSession` now uses one native watcher when the asset root is nested beneath the source project on every selected backend. Source freshness and asset invalidation remain routed independently. Linux retains its existing one-owner path for disjoint roots through inotify's additional-root support; Windows/macOS disjoint roots still use separate services because those adapters do not yet expose additional-root monitoring. The nested-owner regression now applies to all platforms, though only Linux was built and run here.

```text
cmake --build bin/editor-validation --target platformator_editor_workspace_test platformator_editor_asset_watch_integration_test platformator_editor_startup_test --parallel 4
ctest --test-dir bin/editor-validation -R '^platformator_editor_(workspace|asset_watch_integration|startup)_test$' --output-on-failure --timeout 180
git diff --check
```

The focused Linux Debug suites passed 3/3. The asset-watch suite had two runs fail its renamed-subtree child-event assertion. A later 3-run repeat passed, a plain 10-run repeat failed on its ninth invocation, then trace-enabled 10-run and 20-run repeats and a final clean single run passed. The regression now compares active watch count across the rename and prints a bounded recent-event snapshot on child-event timeout. A fresh 20-run repeat passed; the earlier intermittent failure remains open rather than being treated as fixed. No Windows/macOS compile or run was available; T205 remains open.

## Bounded owner-queue continuation recheck (2026-11-07)

The Linux Release and ASan/UBSan asset-watch integration suite passed 1/1 in each configuration. ASan exposed an assertion in the real Project frame helper that assumed all affected paths drain in the first owner call, although the queue intentionally yields after its 2 ms budget. The helper now drains with empty continuation frames, checks every frame commits no more than 256 paths, requires forward progress, and verifies the queue reaches zero. The existing 600-path unit regression independently checks cap and counter accounting.
