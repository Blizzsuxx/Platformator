# US4 Responsive large-scene work evidence

**Status:** In progress; this is not a US4 acceptance record.

## Focused headless verification

- Source baseline: `b45bdff923aed012f96e2de1b5554541679636f1`; working tree contains changes.
- Host: Linux `6.12.112-1-MANJARO x86_64`; reused Debug build `bin/editor-debug` with `PLATFORMATOR_EDITOR_WORK_COUNTERS=ON`.
- Build commands: `cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test platformator_editor_preview_test -j2` and `cmake --build bin/editor-debug --target platformator_editor_workspace_test -j2` — passed.
- Test commands: `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_workspace_test|platformator_editor_preview_test)$'` — 2/2 passed; `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_workspace_test$'` — passed again after hierarchy connector coverage (2026-11-06).
- `git diff --check` — passed.
- Scene preview now updates local authoring descriptors with local proxy state. It visits descendants for inherited position changes and updates only the changed object for local geometry changes; hierarchy cache consumes typed label deltas and connector work follows visible rows and their ancestors.
- Regressions use 10,000 unrelated objects plus a depth-128 sprite chain, verify inherited positions, local-only geometry, one-row rename refresh, no label refresh for pose edits, clipped scrolling, and visible connector candidates. Work counts distinguish authoring descriptor visits from proxy visits, visible rows from connector submissions, label string capacity growth, hierarchy scratch capacity growth, and authoring descriptor insertions. After warmup, local subtree edits reuse descriptor traversal scratch and insert no descriptor nodes.

## Remaining acceptance work

- Allocation counts are not yet measured independently; the work-bound tests alone do not prove allocator bounds.
- Project listing rename/pose behavior and all relevant visible-folder/asset workloads still need a registered large presentation suite.
- No matched GUI capture, actual desktop walkthrough, or cross-platform acceptance was run.

## Focused resource and reuse continuation (2026-11-06)

- Built `platformator_editor_resources_test`, `platformator_editor_preview_test`, `platformator_editor_workspace_test` and `platformator_editor` in `bin/editor-debug`.
- `ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_resources_test|platformator_editor_preview_test|platformator_editor_workspace_test)$' -j2` passed 3/3 after the async startup and explicit Refresh regressions were added.
- Follow-up focused groups passed Diagnostics/Console/Document/History/Workspace/Preview/Resources (7/7), BatchCommand/InspectorInteraction/Changes/Tabs (4/4), and Startup/Catalog/AssetWatchIntegration/Changes (4/4). `git diff --check` passed.
- Coverage now includes bounded image header admission and staged decode, inclusive working-set arithmetic, unsupported-format fallback, cache entry/byte/pin/LRU/negative-result/generation behavior, last-good Scene replacement, retained animation references, stale worker rejection, shared dependency decode, depth-128 descriptors, local-only proxy changes, static render-target reuse, ordered offscreen rejection, and watcher-driven Scene alias repair.
- ASan/UBSan identified and resolved a test fixture lifetime defect. The work-counter-enabled preview executable then passed with `ASAN_OPTIONS=abort_on_error=1:detect_leaks=0` and `UBSAN_OPTIONS=print_stacktrace=1`.
- No GUI capture, matched performance comparison, native desktop walkthrough, or cross-platform lane was run. Those acceptance items remain open.

## Focused preview continuation (2026-11-06)

- Rebuilt `platformator_editor` and `platformator_editor_preview_test` in `bin/editor-debug`; the focused preview CTest passed 1/1 and `git diff --check` passed.
- Preview proxy equality now stores only the render-relevant Sprite, Animator and Collider inputs. Collider-only edits refresh placeholder/shape geometry even when sprite data is unchanged.
- The Scene target is reused when its renderer, target size and view are unchanged; selection/hover overlays stay outside that target. Dirty view renders retain document order and conservatively reject sprites whose rotated bounds are wholly outside the view. Counter regressions verify zero repeated capture/submission for a static view and offscreen rejection after a view change.
- These remain focused software-renderer checks. Allocation budgets, preflight/decode/LRU acceptance, matched GUI captures, and native desktop evidence remain open.
# Focused continuation (2026-11-06)

The hierarchy presentation cache task T062 is complete. Structure/document generations gate topology rebuilds; typed Label deltas refresh only affected names; pose-only deltas advance the cached value revision without refreshing labels. Connector endpoints are cached with the row topology, and each draw derives connector candidates from visible rows and their ancestors. The registered workspace lifecycle cases exercise label locality, zero additional label work after rename settles or after pose edits, deep hierarchies, 10,000-row scrolling, visible-row limits, connector-parent visit limits, and no scratch capacity growth.

Command run:

```text
cmake --build bin/editor-debug --target platformator_editor_workspace_test platformator_editor_resources_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_(workspace|resources)_test' --output-on-failure
```

Outcome: 2/2 focused tests passed. T057–T061, T068 and broader US4 acceptance remained open at this checkpoint; no GUI performance comparison was run.

## Bounded image decoder continuation

T057 is complete for the current implementation. Preflight rejects oversized encoded inputs, dimensions, pixel counts and unsupported formats before decoding. PNG, BMP, JPEG and first-frame GIF decoding now use the vendored stb callback path with a per-decode allocator that charges allocations (including allocator headers), tracks peak use and refuses allocations beyond the remaining inclusive 128 MiB budget after encoded input staging. The decoded RGBA buffer is wrapped by an SDL surface without a second full pixel copy, and its allocator lifetime remains attached until the surface and borrowed pixels are released. SVG and unknown formats retain the explanatory icon fallback.

The vendored stb change is gated by PLATFORMATOR_BOUNDED_DECODER; it restores only the memory helper required to compile the fork's GIF entry point in this private capped translation unit. SDL_image's regular build path does not enable that helper.

Focused verification commands:

    cmake --build bin/editor-debug --target platformator_editor_resources_test platformator_editor -j2
    ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_resources_test$'
    git diff --check

Outcome: build passed and resources test passed 1/1, including valid PNG, BMP and GIF decode checks, the inclusive arithmetic boundary, dimension and encoded-size rejection, and unsupported-format fallback. No matched GUI capture or native desktop walkthrough was run.

## Bounded preparation and cache admission continuation

T058 is complete for the current single-renderer Linux editor. Thumbnail requests are versioned by per-entry revision and cache generation, decoded one at a time on oneTBB, queued in visible draw order, and capped at 64 outstanding paths including the running and completed result. Repeated demand for one version is deduplicated; invalidation removes queued stale work and in-flight stale results cannot publish. Worker surfaces remain immutable until owner-thread upload. Project animation parsing remains asynchronous, caps descriptions at 1 MiB and frames at 512, retains the last complete preview on failed refresh, and does not modify authored animation data.

The 256-entry cache admission now counts pending and negative entries as well as uploaded textures, and evicts only unpinned least-recently-used entries. Pin release runs after ImGui render submission and SDL present, so each requested texture remains pinned through presentation. A reserved output allowance and upload-time admission keep the primary renderer texture bytes plus the completed/uploading CPU surface within 64 MiB. Accounting for multiple renderer replicas remains open with T059/T098.

Focused verification: platformator_editor_resources_test was rebuilt and passed 1/1. Its cache cases now saturate the 64 outstanding path budget, check duplicate demand, load beyond the 256-entry boundary, verify frame pins and LRU eviction, and exercise negative-version repair and stale-generation teardown.

## US4 focused regression checkpoint

T068 is complete for the Linux headless regression lane. The resource, preview, workspace, project change, asset watcher, and capture guard/schema suites are registered in editor/tests/CMakeLists.txt. The work-counter-enabled Debug build was rebuilt after the bounded decoder and cache changes. Static preview reuse, local and unrelated resource behavior, thumbnail eviction/pins/teardown, 10,000-object hierarchy work and depth-128 sprite descriptor coverage are exercised by the selected suites.

    cmake --build bin/editor-debug --target platformator_editor platformator_editor_resources_test platformator_editor_preview_test platformator_editor_workspace_test platformator_editor_changes_test platformator_editor_asset_watch_integration_test -j2
    ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_(workspace|preview|resources|changes|asset_watch_integration|capture_budget|gui_capture_budget|gui_capture)_test$' -j2
    git diff --cached --check
    git diff --check

Outcome: build passed; all 8 focused tests passed; both diff checks passed. No matched GUI capture, benchmark comparison, physical desktop walkthrough, or cross-platform verification was run. Those benefits and acceptance claims remain open.

## Prepared Scene and Project previews (2026-11-06)

T060/T061 implementation is complete for the current single-renderer editor. Scene image/animation inputs are prepared asynchronously through ResourceIndex, decoded bytes and working sets are bounded before SDL upload, proxy texture and Scene target allocation caps are enforced, and source-rectangle rendering scales against the original image dimensions. Static Scene frames reuse their cached target and do not synchronously decode Runtime files. Project tile drawing requests prepared thumbnails/animation descriptions, retains last-good content during refresh, pins textures through `ImGui::Render` and `present`, and now displays explicit “Preparing preview,” “Updating preview,” “Preview unavailable,” or “No preview frames” labels with path and failure detail on hover. Project file and animation parsing remain in bounded workers; the visible grid stays virtualized and authored references are not changed by preview failure.

Focused command:

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_resources_test platformator_editor_preview_test -j2
ctest --test-dir bin/editor-debug -R '^(platformator_editor_resources_test|platformator_editor_preview_test)$' --output-on-failure
```

Outcome: editor build passed and CTest passed 2/2. Resource tests cover decoder admission, thumbnail bounds/negative caching/frame pins/LRU, and stale worker rejection. Preview tests cover prepared Scene texture/clip adoption, retained proxy and target caps, original source geometry, last-good content, and static target reuse. T059 remains open only for retained accounting across renderer replicas, which depends on the still-unimplemented multi-host path; Windows/macOS verification was skipped as requested. No rendering benchmark was run.

## Reference validity cache continuation

T063 is also complete. `ResourceIndex` caches displayed resolution by the project-owned entry, logical path, requested `ResourceKind`, logical content version, and uncertainty epoch. Watcher invalidation and bounded fallback checks advance the affected version; authoritative assignment validation still performs a current resolution. The existing asset watcher integration test asserts unchanged-frame queries do not increase `ValidityQueries`, then detects content change and disappearance through bounded fallback invalidation.

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test -j2
ctest --test-dir bin/editor-debug -R 'platformator_editor_asset_watch_integration_test' --output-on-failure
```

Outcome: 1/1 passed. This verifies the cache and invalidation path; it does not complete prepared Scene/Project previews, cross-platform watching, or full acceptance.

## Preference persistence continuation

T064 is complete for the current Linux implementation. Settings and docking layout writes are independently dirty-tracked; unchanged sections are not rewritten. Transient preference changes use the existing 500 ms debounce, while explicit durable actions and shutdown still flush pending state and report section write/durability outcomes. Window title assignment remains change-gated by the cached title string.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_workspace_test$'
git diff --cached --check
git diff --check
```

Outcome: build passed, workspace test passed 1/1, and both diff checks passed. Added regressions prove settings-only writes preserve layout bytes and layout-only writes preserve settings bytes. These preference tests do not exercise the app's minimized/unfocused event loop.

## Event scheduling continuation

T065 is complete for the Linux application loop. SDL input and watcher notifications wake the loop directly; completed project scans, validation jobs, Scene resource preparation, and Project-panel thumbnail/animation preparation push the registered wake event. The loop sleeps to the earliest external-file, source-scan, recovery, deferred-save, change-batch, or visible animation deadline. Child process and catalog work retain an 8 ms owner-thread polling cap. Presentation wakes at most every 16 ms while focused and every 100 ms while minimized or unfocused, with earlier deadlines taking precedence.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test platformator_editor_changes_test platformator_editor_preview_test platformator_editor_resources_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_workspace_test|platformator_editor_changes_test|platformator_editor_preview_test|platformator_editor_resources_test|platformator_editor_asset_watch_integration_test)$' -j2
git diff --cached --check
git diff --check
```

Outcome: build passed, focused tests passed 5/5, and diff checks passed. Scheduler tests cover earliest/overdue deadlines, bounded polling, and the watcher quiet/maximum batch deadlines. No native desktop presentation or latency capture was run.

The application wake event is now coalesced with an atomic gate so concurrent watcher/scan/preview completions do not enqueue an unbounded run of duplicate SDL events. Event consumption and SDL queue failure release the gate. `cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test -j2` passed; `ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_workspace_test$'` passed 1/1 with queued, duplicate, consumed and failed-insertion gate cases. Multi-host scheduling and renderer-local cache replicas remain tied to the open host tasks.

## GUI probe and bounded capture harness

T066 and T067 are implemented. The GUI probe runs the real app loop with isolated preferences and has `idle` and synthetic Scene-panel hover workloads. It records cold preparation, event-to-present latency, active frame time, wait-inclusive frame time, and process idle CPU for the default 3 × 30 measured samples after five warmups. `capture_gui.py` compares one selected workload for baseline/candidate executables, records identities and raw samples, refuses reused output directories, stops on failure, and caps the cumulative output at 50 MiB and 600 seconds. It does not retain frame images or source/build trees.

```text
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy bin/editor-debug/platformator_editor_gui_probe --frames 4 --warmup-frames 0 --workload hover --no-restore
cmake --build bin/editor-debug --target platformator_editor_gui_probe platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_gui_capture_test|platformator_editor_gui_capture_budget_test|platformator_editor_capture_budget_test|platformator_editor_workspace_test)$' -j2
```

Outcome: the 4-frame probe smoke run emitted cold, hover-latency, frame, and idle CPU records. The matched-capture guard tests passed 3/3 and the workspace probe tests passed; the combined CTest selection passed 4/4. No baseline/candidate capture was run. Actual matched GUI measurements remain at the later acceptance gate.

### Convergence guard coverage follow-up

Expanded `editor/tests/gui_capture_test.py` to verify source/harness/probe/workload identity hashing and to force a capture failure after the first child starts. The failure regression confirms partial stdout and a failed compact report are retained and the driver does not retry. Existing cases cover schema/workload validation, frame and event samples, pilot defaults, fresh output paths, child termination, and the capture budget. `ctest --test-dir bin/editor-debug -R '^(platformator_editor_gui_capture_test|platformator_editor_gui_capture_budget_test)$' --output-on-failure` passed 2/2. No capture campaign was run; the repository's compact 3/5/30, 50 MiB, 600 second limits remain unchanged.

## Visible reference demand continuation T215 (2026-11-06)

Inspector asset fields now register frame-scoped ResourceIndex consumers for valid, missing, and wrong-kind logical paths. Visible demand is refreshed during the active UI frame and retired when a field leaves the active selection or its document tab closes. These consumers use the existing logical alias and uncertainty indexing, so fallback can surface Audio/behavior references even when there is no Scene preview proxy. Assignment, save, and Run validation continue to use fresh authoritative path checks.

```text
cmake --build bin/editor-debug --target platformator_editor_inspector_interaction_test platformator_editor_asset_watch_integration_test platformator_editor -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_inspector_interaction_test|platformator_editor_asset_watch_integration_test)$' -j1
```

Outcome: build passed and focused tests passed 2/2. The Inspector interaction regression draws an Audio reference, verifies the demand appears, detects silent delete/restore through bounded fallback, asserts zero repeated healthy-frame validity queries, and retires demand after selection change. The watcher integration regression verifies round-robin progress across 520 reference-only Audio paths and document teardown. This is Linux headless coverage; native Windows/macOS and physical desktop checks were skipped.

## Explicit referenced-resource Retry continuation T204 (2026-11-06)

The Project panel's Refresh Assets action now has a covered end-to-end retry path for an exhausted referenced Scene resource. After the watched Scene texture is deleted and its prepared request reaches Failed, the integration fixture restores the file without consuming another watcher event, invokes the same preview retry service used by the app's Refresh action, and confirms the Scene adopts the restored pixels. Exactly one texture upload occurs, the unrelated resource version remains unchanged, and authored content/history inputs stay intact. Existing preview tests also cover a failed animation clip whose missing frame texture is repaired.

```text
cmake --build bin/editor-debug --target platformator_editor_asset_watch_integration_test platformator_editor_preview_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_preview_test|platformator_editor_asset_watch_integration_test)$' -j1
```

Outcome: build passed and focused tests passed 2/2. This is Linux headless verification; native desktop UI and Windows/macOS lanes were skipped.

## Bounded image admission/cache acceptance T170 (2026-11-06)

The existing single-renderer resource path enforces format/header admission before decode, the inclusive encoded-plus-decoded working-set bound, oneTBB preparation with owner-thread texture upload, bounded pending/completed work, a 256-entry/64-MiB thumbnail cache, frame pins, negative results and LRU reload. Scene preview adds its 512-frame/1-MiB clip input, 256-MiB proxy texture and 64-MiB render-target caps. Unsupported/unbounded formats report the icon fallback reason. T059's cross-renderer replica accounting remains open with the not-yet-implemented host system.

```text
cmake --build bin/editor-debug --target platformator_editor_resources_test platformator_editor_preview_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_resources_test|platformator_editor_preview_test)$' -j1
```

Outcome: build passed and focused tests passed 2/2. Tests exercise oversized-header rejection before decode, inclusive 128-MiB admission, negative-cache repair, queue caps, stale worker rejection, frame-pin/LRU eviction, source geometry and Scene allocation limits. This is Linux headless verification.

## Work-probe coverage continuation T217 (2026-11-06)

`editor/src/editorwork.h` now documents the unit counted by each probe. The compile-time-only counters include batch targets resolved, patches committed, ordered path comparisons, and separate settings/layout atomic-write attempts. Batch path comparisons are recorded in the ordered-path comparator used by property batch validation and gesture preparation. Existing probes still cover image decode/texture upload, Scene submission/capture/cache reuse, visible Project tiles, hierarchy visited/visible rows and connector work. `PLATFORMATOR_EDITOR_WORK_COUNTERS` remains OFF by default; the current focused Debug build has it enabled for correctness oracles, and no timed benchmark was run.

The batch command suite now applies 512 disjoint property edits and checks exact targets/patches, one retained history command, unchanged document-copy count, and a bounded path-comparison count. The counter assertion requires nonzero comparisons above the batch size and caps work at 64 comparisons per edit; a missing instrumentation path or quadratic all-pairs validator fails. Workspace tests attribute settings-only and layout-only writes independently. The existing preview/resource suites continue to exercise cold and refreshed thumbnail/Scene decoding and owner-thread uploads; workspace presentation coverage checks visited hierarchy rows against the large fixture.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_batchcommand_test platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_batchcommand_test|platformator_editor_workspace_test)$' -j1
cmake --build bin/editor-debug --target platformator_editor_resources_test platformator_editor_preview_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_resources_test|platformator_editor_preview_test)$' -j1
git diff --check
```

Outcome: both focused CTest selections passed (2/2 each), builds passed, and `git diff --check` passed. This is Linux headless coverage; timed comparisons and Windows/macOS verification remain outside this task.

## Bounded image pipeline continuation T213 (2026-11-06)

T213 is complete for the current single-renderer editor. Header and allocation admission enforce the encoded, dimension, pixel, and inclusive decode-working-set limits. oneTBB workers prepare bounded thumbnail/Scene resources; SDL texture upload and pin release stay on the owner thread. Thumbnail queue/cache entries, completed bytes, dimensions, clip description size/frame count, Scene proxy texture bytes, and Scene target bytes are capped. Failed versions are negatively cached and repaired through explicit refresh; LRU eviction, stale-worker rejection, last-good preview retention, and original source geometry are covered by the focused resource and preview tests. Unbounded formats retain the explained icon fallback. T059's multi-renderer replica accounting remains open with the multi-host implementation.

```text
cmake --build bin/editor-debug --target platformator_editor_resources_test platformator_editor_preview_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_resources_test|platformator_editor_preview_test)$' -j1
```

Outcome: build passed and tests passed 2/2. This is Linux headless verification; Windows/macOS native rendering was skipped as requested.

## Preference persistence consolidation T216 (2026-11-06)

The editor and regression suite now share `WorkspacePreferencePersistence`, which compares settings and docking layout independently and advances each saved snapshot only after that section succeeds. The editor retains its 500 ms quiet-period debounce for snap changes, viewport changes, and Run Settings edits; immediate durable actions and orderly shutdown still call the same dirty-section flush. A failed settings flush leaves that snapshot dirty so a later explicit or shutdown flush can retry it. Last-workspace writes remain identity-gated, and the title remains assignment-gated by its cached value.

The workspace lifecycle test applies repeated frame-like snap DragFloat, per-tab wheel zoom, Run Settings typing, and continuous viewport pan updates. It advances the same deferred timer and invokes the persistence coordinator used by the app against the real atomic preference writer. Each interaction burst produces exactly one settings write and zero layout writes; unchanged frames produce no write. An injected settings-path failure is retried successfully, and the follow-up no-op flush does not write again.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_workspace_test$'
git diff --check
```

Outcome: Linux build passed, workspace test passed 1/1, and `git diff --check` passed. The interaction checks exercise the shared preference state/debounce/persistence path headlessly; they do not certify native GUI input or Windows/macOS behavior.

## Event-driven static Scene scheduling continuation T169/T212 (2026-11-07)

The Scene target reuses its rendered image while renderer, target, view, dimensions, hidden-object set, and invalidation state match. Selection and transform overlays remain separate in the viewport. Dirty captures retain document order and reject only conservatively bounded fully offscreen sprites; the `EFFICIENCY:` comment records why the O(N) ordered pass remains. The outer event loop no longer imposes a 16-ms foreground redraw or 100-ms background redraw. It waits until SDL input, a coalesced completion event, a bounded process-poll interval, or the earliest maintenance/animation deadline. Existing deadlines continue to service external file checks, source scans, watcher recovery, preference flush, resource batches, validation wakeups, and Project animation.

Existing preview tests cover zero additional Scene submissions on a static cached render, cache invalidation, hidden-object changes, offscreen culling, and ordered overlapping sprite/collider/camera picking. The workspace lifecycle suite checks earliest/overdue deadlines, bounded polling for services without completion events, coalesced wake insertion/consumption/failure, and the long event-driven maximum wait when no maintenance deadline exists.

```text
cmake --build build --target platformator_editor_workspace_test platformator_editor_preview_test platformator_editor -j8
ctest --test-dir build -R '^(platformator_editor_workspace_test|platformator_editor_preview_test)$' --output-on-failure
git diff --check
```

Outcome: build succeeded, focused tests passed 2/2, and the diff check passed. This is Linux headless coverage. No native desktop latency walkthrough or Windows/macOS verification was run.

## Thumbnail peak-pixel accounting continuation (2026-11-07)

Thumbnail replacement admission now includes the prior cached version while reserving space for the prepared output and incoming renderer texture. During upload, statistics account for both the prepared pixels and temporary texture; after publication they account for the new retained texture, any still-live prior version, and the prepared surface until each is released. Frame pins continue to clear only after the application presents the frame.

Added a resource regression that prepares 256 unique 256×256 images, checks every uploaded texture stays at or below 256 pixels per side, and verifies near-cap retained pixel accounting never exceeds 64 MiB.

```text
cmake --build build --target platformator_editor_resources_test platformator_editor -j8
ctest --test-dir build -R '^platformator_editor_resources_test$' --output-on-failure
git diff --check
```

Outcome: the Linux resource suite passed 1/1. The current application has one renderer and clears Project textures when that renderer changes. Cross-renderer replica accounting remains open under T059/T098 until renderer-local host textures are implemented; no Windows/macOS verification was run.

## Thumbnail renderer-thread enforcement T059 partial (2026-11-07)

`ThumbnailCache` now binds its renderer-facing API to the constructing thread. Worker-thread calls to `request`, `find`, `source_to_thumbnail_scale`, `poll_uploads`, `clear`, and `end_frame` are rejected before touching SDL textures; the cache exposes an owner-thread-violation counter for regression diagnosis. The resource test calls upload/request from a worker and confirms no texture is replaced or returned, while the current frame pin remains valid.

```text
cmake --build bin/editor-debug --target platformator_editor_resources_test platformator_editor -j2
ctest --test-dir bin/editor-debug -R '^platformator_editor_resources_test$' --output-on-failure
```

Outcome: build passed and the Linux resource suite passed 1/1. At that checkpoint, T059 stayed open for byte accounting across renderer-local replicas, which depends on T098 host texture support.

## Renderer-local thumbnail replicas complete T059 (2026-11-07)

Thumbnail entries now retain one bounded prepared pixel surface plus a texture replica per renderer. The 64 MiB accounting includes all retained surfaces and replicas; pending uploads reserve their incoming bytes before allocation, and LRU eviction releases every replica and its source pixels together. `request`, `find`, scale lookup, upload, clear, frame release, and renderer release are owner-thread checked. `release_renderer` drops that renderer's textures and budget before its SDL renderer is destroyed. A regression creates two hidden SDL renderers, confirms each gets a distinct texture, verifies the added replica increases accounted bytes, then releases one renderer and checks its texture and bytes are removed. Existing frame pins still clear only after frame presentation.

```text
cmake --build build --target platformator_editor_resources_test -j2
ctest --test-dir build -R '^platformator_editor_resources_test$' --output-on-failure
```

Outcome: build succeeded and the focused Linux resource suite passed 1/1. The application currently creates one renderer; this test verifies the cache contract independently of the still-open multi-host integration in T098/T230. No Windows/macOS verification was run.
