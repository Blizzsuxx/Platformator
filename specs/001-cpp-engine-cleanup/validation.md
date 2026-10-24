# C++ cleanup execution evidence

Latest dependency-update rerun and follow-up: [SDL update validation](../../docs/SDL_UPDATE_VALIDATION.md). The local WAV fix has been restored; strict ASan/UBSan now passes all 21 executed non-package tests, with two intentional skips and leak detection disabled. Existing leak/TSan findings remain unresolved. The dependency-update benchmark captures and older cleanup acceptance below are historical evidence for their recorded source/dependencies.

## Logged startup errors and batch promotion (24 October 2026)

At the user's request, initialization errors are logged per behavior, and failed initialization no longer destroys behaviors or propagates startup exceptions to the host. Every attempted entry becomes runnable and remains owned until ordinary teardown. Initialization is never automatically retried. A resolver exception skips start for that attempt; ordinary callbacks can still run on the partially initialized behavior. Ordinary callback and engine exceptions retain their existing propagation policy.

Pending behaviors are vectors again. Startup iterates by index so eligible callback additions can join the pass, then transfers the processed prefix together into runnable behaviors. Fully processed scripts clear pending once. When a script becomes inactive or retired during startup, only its attempted prefix transfers; the unattempted suffix stays owned. No deque, per-entry pop/removal, behavior membership index, startup state flag, or snapshot is needed.

Updated mutation, scene-I/O and asset-root contracts check retained/runnable behavior after resolver/start failures, non-standard exceptions, no initialization retries, normal exactly-once teardown, and interruption/reactivation of a partially processed script. Diagnostics include behavior type, owner name and exception text; stderr logging remains enabled independently of debug logging. [Captured diagnostics](evidence/logged-start-diagnostics.txt).

- Normal build, including Mario/public headers, passed. Non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 5.62 s. [Log](evidence/logged-start-debug-tests.txt).
- ASan/UBSan build passed. With `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 12.84 s. [Log](evidence/logged-start-asan-tests.txt).
- Dedicated work/allocation oracle: **1 passed**, 0.04 s. [Log](evidence/logged-start-work-tests.txt).

Package, leak, TSan and benchmark runs were not repeated. Earlier sections below describe historical policies and are superseded by this section.

## Previous direct startup drain, without a snapshot (24 October 2026)

At the user's request, removed the behavior snapshot and relaxed startup deferral. Startup drains each queued script's pending behaviors directly in FIFO order. Eligible additions during startup can initialize in that pass, including additions to current, later queued, and already processed scripts. Additions from ordinary callback phases still wait for the next startup pass. Per-behavior failure handling and retirement gates remain in place.

Pending behaviors now use a deque for constant-time front removal/back insertion; initialized behaviors retain their dispatch vector. Behavior has no pending index or startup state. ScriptComponent retains one queue membership index. Cancellation marks the script queue slot empty without moving other entries during callback mutation. There is no startup snapshot, per-script startup cursor, or additional owning list. The pending getter's public return type is now `const std::deque<Behavior *> &`.

The mutation test covers immediate startup additions to current/later/already processed scripts, queue growth, and exactly-once initialization; existing cases retain cancellation/reactivation, replacement, failed initialization, serialization, and later-phase deferral coverage.

- Normal build, including Mario/public headers, passed. Non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 5.60 s. [Log](evidence/direct-start-debug-tests.txt).
- ASan/UBSan build passed. With `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 12.25 s. [Log](evidence/direct-start-asan-tests.txt).
- Dedicated work/allocation oracle: **1 passed**, 0.03 s. [Log](evidence/direct-start-work-tests.txt).

Package, leak, TSan and benchmark runs were not repeated. The snapshot-based section below is historical and superseded by this section.

## Previous script startup queue follow-up (24 October 2026)

This follow-up supersedes the queue representation described in the previous section. The global producer queue now holds ScriptComponent pointers, with one deduplication/cancellation index per component. Behavior retains only its owner-pending-list index. Each component still owns exactly two collections: pending and initialized behaviors.

Before callbacks, queued components' pending pointers are captured into the existing reusable, non-owning behavior batch. This freezes additions to both current and later queued scripts without adding a per-script cursor or third owning list. Startup groups behaviors by script queue order. Per-behavior failure handling and promotion are unchanged. Empty startup steps return immediately; snapshot construction only visits queued work, and its capacity is reused.

The mutation test now checks grouped script startup order and deduplication; its existing cases cover callback additions to current/later scripts, activation, replacement, and individual failures.

- Normal rebuild, including Mario/public headers, passed. Non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 5.79 s. [Log](evidence/script-start-queue-debug-tests.txt).
- ASan/UBSan rebuild passed. With `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 13.35 s. [Log](evidence/script-start-queue-asan-tests.txt).

- Dedicated work/allocation oracle: **1 passed**, 0.02 s. [Log](evidence/script-start-queue-work-tests.txt).

Package, leak, TSan and benchmark runs were not repeated for this follow-up; previous results remain historical evidence.

## Previous global pending behavior queue and two-list ownership (24 October 2026)

At the user's request, kept Behavior free of a started flag and restored the original global behavior startup queue. ScriptComponent has only two owning lists: pendingBehaviors and initialized behaviors. There is no separate runnable/ownership list or per-script startup queue. getBehaviors returns initialized entries, getPendingBehaviors returns pending entries, and getBehavior<T> searches both. Serialization, owner updates and normal component teardown inspect both lists.

The global producer/drain buffers swap before callbacks. New additions wait until the next eligible batch even on an already active component. Only successful resolve/start moves a pending entry into ordinary dispatch. Inactive unattempted entries remain pending without per-frame polling; activation queues only remaining pending work. Per-behavior catches finish the batch and report its first error afterward. Failed initialization removes the entry, invokes read-only onDestroy and deletes it, so its borrowed pointers become invalid and it cannot retry or receive later callbacks.

Two Behavior indices are membership metadata only: one for O(1) swap-removal from the owner's pending vector and one for O(1) cancellation/deduplication in the global queue. No startup cursor, initialized-prefix count or per-frame started check is used.

Extended mutation contracts verify global cross-script startup order, additions to current/later active components during startup and fixed update, cancellation during a startup batch with later activation, mixed resolver/start failures followed by healthy entries, no retries after reactivation/additions, and exactly-once failure cleanup without later double destruction. Existing scene serialization/reference, retirement and physics tests remain enabled.

- Normal rebuild including Mario/public headers passed. Full non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 5.51 s. [Log](evidence/pending-behavior-debug-tests.txt).
- ASan/UBSan rebuild passed. Outside the sandbox with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, full non-package CTest: **21 passed, 0 failed, 2 intentional skips**, 11.88 s. [Log](evidence/pending-behavior-asan-tests.txt).
- Dedicated work/allocation target rebuilt and its oracle passed: **1 passed**, 0.02 s. [Log](evidence/pending-behavior-work-tests.txt).

These are correctness/work checks, not performance timings. No SDK relocation, TSan/leak scan or benchmark capture was repeated for this scheduling change. Historical automatic-retry and per-behavior started-state evidence below is superseded by the current contract.

## Assets-only resource paths (24 October 2026)

At the user's request, simplified resource loading to one assets/... namespace. All nonempty asset paths need the literal assets/ prefix; filename case is preserved and backslashes normalize to forward slashes. Bare/scene-relative/animation-relative paths, absolute asset filenames, parent traversal and symlink escapes reject even for optional loads. Valid missing optional assets retain their deliberate fallbacks. Host asset-root configuration and scene-opening paths still accept their documented filesystem paths.

Removed ResourceDirectoryResolver, active-resolver directory state, containing-file fallback decisions, and duplicate path resolution in staged/active resource loading. Behavior startup remains the single binding location. Scene/animation files checked into the engine and Mario already used assets/ paths; no bulk data rewriting was needed. Test fixtures now separate filesystem write locations from logical loading paths. The editor's small shared path helper/callers now apply the same syntax and preserve case instead of migrating relative fields or silently adding prefixes.

New/updated contract checks cover mixed-case paths, explicit asset roots not named assets, changed cwd, nested animation frame assets, behavior startup, canonical serialized paths, valid optional fallbacks, rejected unsupported spellings and symlinks, old-scene preservation for invalid component paths, and post-commit failure for invalid behavior paths.

- Normal Debug rebuild passed, including Mario/public headers. Full non-package suite: **21 passed, 0 failed, 2 intentional skips**, 5.44 s. [Log](evidence/assets-only-debug-tests.txt.gz).
- Rebuilt ASan/UBSan; outside the sandbox, `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir bin/cleanup-asan --output-on-failure -E package_smoke`: **21 passed, 0 failed, 2 intentional skips**, 12.03 s. [Log](evidence/assets-only-asan-tests.txt.gz).
- Full `tests/package_smoke.cmake`: core-only Release build/install, source/installed/relocated public consumers, installed SDK Mario and installed/relocated Mario from unrelated cwd all passed. [Log](evidence/assets-only-package.txt.gz).
- Focused editor asset-path and scene-roundtrip tests passed. Broader path/roundtrip/inspector/canvas run: **6 passed, 2 failed**. Both failures are existing ctrl-drag grid-snap assertions, reproduced with unchanged HEAD UI sources in a separate temporary snapshot (**2 failed, 2 deselected**). No snapping behavior was changed. [Current log](evidence/assets-only-editor-tests.txt.gz), [unchanged-source control](evidence/assets-only-editor-before.txt.gz).

No TSan/leak scan or benchmark timing was repeated for this loading-policy change. Existing dependency leak/desktop TSan findings remain unresolved. Original directory-aware design/tasks and prior captures remain historical evidence, superseded by the current contracts rather than silently relabeled as current behavior.

## Startup-only behavior reference resolution (24 October 2026)

At the user's request, removed `Behavior::referencesPrepared` and the behavior-resolution pass from scene preparation. Loaded and programmatic behaviors now resolve against the published scene immediately before first start. The active resolver is shared for the startup batch, retains the committed scene directory for relative behavior assets, and enforces required resources. Inactive behaviors defer resolution until activation; successful initialization does not repeat on reactivation. A resolution error propagates before that behavior's start, leaves the replacement committed, and remains eligible for retry; it does not restore the old scene.

Updated scene transition/reference/I/O and asset-root tests check resolution/start order, no load-time resolution, loaded/programmatic parity, activation/reactivation, wrong-kind/missing targets, missing required behavior assets, startup failure cleanup and scene-relative assets after cwd changes. Preparation failures for malformed graphs, unknown behavior types and invalid component data still preserve the current scene.

- `cmake --build bin/cleanup-debug --parallel 3`: passed, including Mario and independent public headers. `ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke`: **21 passed, 0 failed, 2 intentional skips**, 5.52 s. [Complete log](evidence/startup-only-debug-tests.txt.gz).
- `cmake --build bin/cleanup-asan --parallel 3`: passed. Outside the sandbox, `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir bin/cleanup-asan --output-on-failure -E package_smoke`: **21 passed, 0 failed, 2 intentional skips**, 11.82 s. [Complete log](evidence/startup-only-asan-tests.txt.gz).

The work/benchmark-only tests intentionally skip in these configurations. Leak detection, full package relocation, TSan and Release timing were not rerun for this startup-only change. Existing leak/desktop TSan findings remain unresolved; recorded benchmark fingerprints/results describe the earlier implementation. Historical staged-behavior-binding evidence below records the original contract and is superseded by this amendment.

## Initial baseline (T001)

- Starting commit: `4d5bc02ce03e4119fdc4a764e993d4d4b7dad35b` (`logs`); initial working tree clean.
- Linux x86-64, AMD Ryzen 5 7535HS; GCC 16.2.1, CMake 4.4.3, Ninja 1.13.2.
- Fresh configure: `cmake -S . -B bin/cleanup-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_BUILD_EXAMPLES=ON`.
- Build: `cmake --build bin/cleanup-debug --parallel 4` succeeded, including Mario.
- Tests: `ctest --test-dir bin/cleanup-debug -R '^platformator_' --output-on-failure`: **3/5 executables passed**. Regression failures: tall sleeping stack and wide sleeping pile. Queued-deletion test fails its expected-exit assertion. Roundtrip, runtime recreation, and collider-pair cleanup passed.
- Execution logs: `/tmp/platformator-implementation/baseline-{configure,build,tests}.log`. This is a fresh run; existing review findings have not been relabeled as passes.
- Preserve the [pre-cleanup performance reference](../../benchmark/baselines/pre-cleanup-e01526b/RESULTS.md): 180 Release runs, original partial-frame and full public-step boundaries, raw samples and flagged physics failures. Future comparisons must use new output directories.

Task checkboxes record implemented and checked work only. Expected red tests and known engine failures remain visible until their owning changes fix them.

## Setup gate (T002–T005)

- Normal rebuild succeeded, including every public header as its own translation unit and Mario. CTest inventory is now exactly five project tests, also when reconfiguring the previously polluted build tree. Obsolete generated vendor CTest files are removed at configure time.
- Common test registration provides project/lifecycle/scene/physics labels and dummy SDL environment. Vendor tests/examples are disabled.
- Sanitizer presets configure successfully for normal, Address/UBSan, and ThreadSanitizer. ASan/UBSan flags instrument engine, tests, and dependencies; TSan configures instrumented TBB with `TBB_SANITIZE=thread` (compiler support check passed). Actual TSan execution remains a later physics gate.
- Fresh ASan/UBSan build succeeded. Runtime-recreation test passed. Repaired queued-deletion helpers now reach the real missing-exit assertion without a helper memory-access error. `ASAN_OPTIONS=detect_leaks=0` was used for memory-access checks, consistent with the review host's leak-checking restriction; this does not certify leaks.
- Normal project suite remains 3/5 passing; sleeping/deletion assertions are retained. The queued-exit diagnostic now states the assertion it checks. Logs: `/tmp/platformator-implementation/setup-*` and `asan-*`.

## Foundation red checks (T006)

`platformator_runtime_foundation_test` uses public headers and covers no-runtime references, growth/retirement dispatch, both hierarchy allocation orders, typed identities/reuse, and resource retention. Before changing runtime behavior, `no_runtime`, `resources`, and `identity` cases each fail the intended contract: inactive-runtime resolve throws, Animator does not retain its clip, and component IDs incorrectly bind as GameObjects. Logs: `/tmp/platformator-implementation/kernel-red-*.log`. The complete ASan/UBSan regression run also reaches only the existing tall-stack/wide-pile failures, with the ID-based helpers active.

## Foundation gate (T007–T017)

- Normal build, Mario, and standalone public headers pass after the explicit `resolveReferences(ReferenceResolver&)` migration.
- `ctest --test-dir bin/cleanup-debug -R 'platformator_(runtime_foundation|runtime_recreation)_test' --output-on-failure`: 2/2 executables passed. Eight foundation scenarios cover no-runtime access, script growth, logical retirement, both hierarchy orders/readable teardown, typed/reused/expired identities, clip retention, inactive/reenabled starts, and recovery after a throwing callback.
- Equivalent ASan/UBSan build and focused CTest run with `ASAN_OPTIONS=detect_leaks=0`: 2/2 passed; no memory-access/UB diagnostics. Logs: `/tmp/platformator-implementation/kernel-{normal,asan}.log`.
- One indexed registry owns lifetime/type metadata; file-ID lookup remains separate. Registration arrays stay stable during callbacks. Queued component reconciliation is deduplicated by an embedded bit, starts cancel by stored index, and buffers retain capacity. Destructors release storage; the retirement drain delivers callbacks while attached components remain allocated. Runtime shutdown performs resource release while its access context/backends are still alive.
- Per-dispatch eligibility includes started state and immediate retirement/disable suppression. Off/on restores callback eligibility at the barrier without forcing removal/re-add. No scene scans or list snapshots were introduced for lifecycle safety.
- Remaining scene replacement/staging/I/O and physics acceptance scenarios are not claimed as passed by this gate.

### Additional resource regression

A cached-audio-pointer constructor omitted its retain and allocated mixer tracks redundantly. The new audio-owner/asset-reference case reproduced a heap-use-after-free under ASan (`audio-red.log`). Audio now retains before replacing its asset and owns one track through a concrete unique_ptr deleter. Expanded foundation suite (nine cases) plus runtime recreation pass normally and under ASan/UBSan (`kernel-normal.log`, `kernel-asan.log`). The WAV fixture is created beneath the CTest working directory; the current heuristic asset-root behavior remains scheduled for US5.

## US1 red checks (T018–T020)

Public-only contract/stress hosts are registered before implementation. The build fails at missing `requestLoadScene`/`requestClearScene` as expected (`scene-red-build.log`). Contract cases cover synchronous/deferred replacement, selected-path-only loading, repeated scene IDs, preparation failure, empty clear/render/save-path behavior, and observable host-boundary errors. The stress host specifies 1,000 replacements distributed across start/fixed/update/late/enter/stay/exit/animation callbacks; it changes overlaps through public position setters to exercise stay/exit request delivery independently of the later stationary-contact fix.

## US2 red checks (T028–T030)

Both new public-only mutation/destruction hosts compile and are registered with lifecycle/mutation labels. Callback mutation cases already pass through the foundation queues. The destruction exception fixture fails with a segmentation fault normally; ASan identifies a heap-use-after-free in `GameManager::fixedUpdateScriptComponents`: the templated attachment helper deletes the newly attached replacement when the old script's onDestroy throws. Logs: `/tmp/platformator-implementation/us2-red-tests.log` and `us2-red-asan.log`. This discriminates exception-safe ownership transfer from ordinary replacement behavior. Existing sleeping regressions remain enabled.

## Scene replacement and mutation gates (T021–T037)

- Normal full build includes Mario and isolated public-header compilation. Full normal CTest: **9/10 passed**; only the original physics executable's tall-stack/wide-pile sleeping assertions remain failing. Scene roundtrip now explicitly accepts zero IDs and rejects duplicate IDs/unknown behaviors transactionally; no compatibility assertion silently accepts invalid graphs.
- ASan/UBSan initially identified incomplete core/audio test fixtures when examples were disabled. Fixtures now copy their required data independently of Mario; the expanded US3 rerun below passes, using `ASAN_OPTIONS=detect_leaks=0`. Logs: `/tmp/platformator-implementation/us2-{build,tests,asan-build,asan-tests}.log`. Leak detection is not certified.
- Scene stress performs 1,000 replacements across eight callback families. Contract tests additionally prove next-step loaded starts, ordinary retirement cleanup after failed preparation, retained other-collider readability, and exit-generated requests waiting for the following latch. Clear leaves no camera/save path and supports empty rendering. Pending requests are discarded on shutdown.
- Staging owns a flat graph, private unpublished handles/local IDs, and an acquisition ledger. Explicit resolver binding never substitutes the active scene. Only the selected pending path is prepared. Failed preparation leaves committed ownership/maps/path intact, while already requested ordinary retirements still drain. Typed caches own assets with unique_ptr; build acquisitions protect shared resources and reclaim unused entries individually. Deeper injected resource/save failures are covered by the following US3 gate.
- Callback tests cover 100 additions, victim/survivor ordering, self replacement across seven phases, self removal, immediate slots, behavior suppression, activation/toggle coalescing, and animation callback-list growth/clip change/retirement without snapshots. Destruction tests cover both hierarchy allocation orders, explicit retirement/shutdown, cycle rejection/world translation, readable attachments, replacement/absent slots, duplicate destroy, nine rejected structural operations per teardown, and completing cleanup after a throwing onDestroy.
- The discriminating ownership failure is fixed by transferring the replacement to its owner before the idle barrier can invoke callbacks. Templates validate the mutation boundary before construction; rejected teardown attachment cannot run constructors. Object identities cannot be copied. The legacy deletion regression now requires the one survivor exit before allocation release and no duplicate next frame, matching the new contract.

## US3 red checks (T038–T040)

Public reference/I/O hosts compile and both fail their intended contract (`us3-red-build.log`, `us3-red-tests.log`). An unbound file ID incorrectly serializes as a live reference. A failed save to a missing parent only prints a diagnostic and returns success. Earlier test stages already exercise malformed IDs/graphs, required references and 20 staged resource/resolver failures; no staged object publishes and retained old resources survive. Full save-failure assertions remain red until observable atomic I/O lands.

## US3 gate (T041–T046), including final US1/US2 sanitizer rerun

- Full normal build passes, including Mario and each public header. Normal suite **11/12 passed**; only the existing sleeping-physics executable remains red. Expanded ASan/UBSan suite **11/11 passed**, with no memory/UB diagnostics (`us3-{build,tests,asan-build,asan-tests}.log`, `ASAN_OPTIONS=detect_leaks=0`). This includes all scene/lifecycle/reference tests and the old roundtrip/queued-deletion/pair-cleanup/recreation hosts.
- New reference host checks unbound IDs, explicit binding, wrong kinds/shapes, pending retirement, actual generation reuse, repeated scene IDs, runtime recreation, integer/null JSON, and saved-subgraph foreign references. Non-null unbound IDs cannot serialize. Expired handles serialize as null; scene save scopes validate live references against the actual graph. These checks run only while serializing, not in pair loops.
- I/O host rejects negative/duplicate IDs, object/component collisions, unknown behaviors, nested missing/wrong-kind references, invalid scale and malformed deserialize. Twenty injected handwritten resolver failures acquire new/shared textures before throwing; no staging handles publish, destructors run without onDestroy, and retained old textures remain readable. Error diagnostics include scene/object/behavior/field context. INT_MAX graph load succeeds with an explicit camera, then automatic creation rejects without publishing; clear resets the allocator.
- Save serializes while idle, reserves an exclusive sibling temporary file, checks complete write and close, then renames. Missing parent, temporary-name-too-long, and destination-directory replacement failures preserve prior destinations, remove temporary state, and keep the current successful path. Callback saves reject. Saving forbids structural edits from handwritten serializers.
- UBSan exposed a preexisting unsafe shape downcast in the legacy Ball fixture. getComponent<BoxCollider/CircleCollider> now checks its concrete shape; the v1 fixture explicitly asserts a circle and absence of a box. Registered identities cannot be copied or renumbered. Valid legacy v1/v2 integer JSON still roundtrips. Handwritten resolvers use ReferenceResolver; Scene::loadScene returns borrowed runtime-owned roots, and direct ownerless GameObject JSON construction rejects.

## US4 red checks (T047–T052)

Five oracle hosts are authored/registered before physics changes. Clock host cannot compile because SimulationClock does not exist (`us4-red-build.log`). The three geometry/transform/support hosts compile separately and fail the intended assertions (`us4-red-tests.log`): parent-only motion misses the collider-only child, the frozen 128-contact fixture fails to balance a filtered support, and the outside circle/box corner falsely collides. Work instrumentation is opt-in and its host skips with CTest code 77 unless PLATFORMATOR_ENABLE_WORK_PROBES=ON; the instrumented host checks affected-subtree counts, unobserved/static/sleeping work and warmed allocations separately from timing runs.

## US4 implementation checkpoint (T053–T065; final T066 matrix in progress)

- Normal full build, Mario and isolated public headers pass. Normal CTest **16 passed, one work oracle skipped intentionally** (`us4-full-build.log`, `us4-tests.log`). The coupled two-point normal solver, stable SAT face selection, explicit support transitions and coordinated sleep timing restore the original tall-stack/wide-pile limits. Last-support-removal wake coverage is restored without obsolete constants or relaxed assertions.
- Transform/collider-only descendants synchronize automatically; the legacy parent/child test runs a normal step instead of calling prepareSync manually. Workers integrate only local translations/rotations, then main-thread parent-first propagation publishes affected caches. Hierarchy order changes only after structure edits; queued roots coalesce and traversal stamps suppress repeated descendants.
- Collider state uses desired membership plus indexed geometry/filter/response dirtiness. Filter reinsertion touches affected cells; canonical net witness deltas avoid contradictory off/on operations. Pair-owned results/stable slots separate parallel manifolds from serial support reduction. Persistent canonical contact/solver/subscribed-contact indexes avoid stationary geometry work and unobserved stay traversal; body membership indexes skip sleeping/static force/movement/sleep loops.
- Exact circle/box closest-point and circle/circle geometry, deterministic containment/features, offset normals and fixed box axes replace allocated per-pair normal vectors. Supported numeric inputs validate at public/load boundaries; solver impulses and worker integration use internal established invariants.
- The opt-in cleanup-work build passes: warmed idle C++ new/new[] allocations = 0; idle hierarchy/reconciliation/geometry/stay/body counters = 0 with 1,000 static and 1,000 sleeping bodies; a 20-child dirty root visits exactly 21 transforms and performs no hierarchy rebuild (`work-final-tests.log`). Work probes are disabled in ordinary builds and timings.
- Interactive SimulationClock uses steady-clock fixed 1/120 steps, cap eight, fractional remainder, aggregate dropped steps, pause reset and exactly-one manual advance. Explicit host steps retain caller dt/hook ordering. Clock partitions/cap/invalid dt/quit tests pass.
- Properly instrumented GCC16/TBB TSan worker tests pass **2/2** with no race reports (`us4-tsan-final-tests.log`). Initial desktop tests exposed an SDL audio-thread race before physics ran. Physics-only startup was moved forward from T073 to skip video/audio acquisition, and those physics hosts now explicitly request it; no race suppression was added. GCC's atomic_thread_fence modeling warning remains visible.
- ASan/UBSan physics/scene/lifecycle hosts pass, but the monolithic old regression host still exposes three heuristic asset-root assertions scheduled for US5. Its audio fixture also exposed a real signed-left-shift UB in the vendored 24-bit WAV decoder; the minimal arithmetic fix is recorded in cmake/VendorPatches.md. T066 remains open until the updated full matrix is rerun after root migration. Leak detection remains excluded on this host.

## US5 red checks (T067–T070)

Asset-root, runner-failure and public-only consumer/package fixtures are authored and registered. The normal build fails at missing RuntimeOptions.assetRoot/smokeSteps and Runtime.getAssetRoot (`us5-red-build.log`). Package smoke configures a core-only Release install, builds a consumer using only Platformator::platformator, relocates the entire prefix, and launches installed/newly configured consumers from an unrelated cwd. Backend-isolation fixes moved forward for TSan remain active; root/export/CLI work is not claimed complete.

## US5 gate (T071–T079), final root migration and SDK consumption

- Normal focused roots/runner/physics/I/O suite passes 7/7 (`roots-tests.log`). After final sequential archive rebuilding, all normal non-package executables pass, with only the opt-in work oracle skipped (`us5-normal-tests.log`). Full ASan/UBSan core matrix passes **20 passed / one intentional work skip** using detect_leaks=0 and UBSAN_OPTIONS=halt_on_error=1 (`us5-asan-final-tests.log`), including all original regression cases. No remaining sanitizer diagnostic from the valid 24-bit audio fixture.
- Runtime owns a normalized asset root and startup base. Case is preserved, logical assets paths stay beneath that root, and supported scene/animation-relative legacy paths normalize through explicit resolver context. No upward/demo-folder search remains. Required resources fail; optional missing texture/audio/animation use one diagnosed packaged fallback. Empty physics-only runtimes acquire no video/audio devices, and startup failures do not publish a Runtime. SDL bool checks and constructor cleanup are corrected.
- RuntimeOptions/CLI expose assetRoot, --asset-root, --headless and positive --smoke-steps; finite hosts exit without changing explicit step dt. CLI errors report nonzero. A duplicate private RuntimeOptions definition was replaced, then source/test/example includes were normalized; 23 unused public-header shims and unused generated detail-forwarder generation were removed after full builds/public-header checks.
- SDK smoke passes **core-only Release build/install; installed and prefix-relocated public-only consumers; a separate source add_subdirectory consumer; installed-SDK Mario source smoke; and installed Mario after a second prefix relocation**, all from an unrelated cwd (`package-smoke-final.log`). Core resources are copied/installed independently of Mario. The consumer asserts a real fallback resource exists at the resolved default root. The relocation fixture caught and fixed a trailing-separator bug in executable-parent resolution. SDK .cmake files contain no source/build root paths.
- Package targets now use official bundled dependency exports/find_dependency and an exported Platformator::platformator with C++20 requirements, represented JSON/header dependencies and relative runtime search paths. Mario deliberately copies engine fallbacks plus its own resources and uses the package's asset-directory variable.
- Final TSan/work and benchmark-off Release symbol checks remain part of the final matrix; these are not speed measurements. Historical benchmark files remain untouched.

## US6 red checks (T080–T082)

The benchmark contract host is registered with an intentional skip when instrumentation is disabled. In a benchmark-enabled configuration its independent syntax check fails at missing CompleteStep/Cleanup/SceneTransition scope and recorder configure/sample/output APIs (`benchmark-cpp-red.log`). Python standard-library tests fail because legacy/incomplete CSV and missing manifests are accepted, and the sweep lacks repeated-run arguments/API (`benchmark-python-red.log`). Tests precede schema/timing implementation.


## US6 implementation and final core gates (T083–T087, T089–T092)

- Complete-step recording now ends after transient audio, scene/mutation reconciliation and actual destruction/free. Cleanup and scene-transition scopes nest within it; rendering records independently afterward. Buffers reserve before measurement; recorder/destructor output cannot pollute timed steps. `benchmarkbuildconfig.h.in` deliberately keeps benchmark metadata separate from ordinary build configuration to avoid rebuilding the entire engine on metadata changes.
- Schema-v2 summary/raw CSV and manifests include all scopes/counters, source fingerprint/commit/dirty state, compiler/flags, configuration/instrumentation, dependencies, machine, workload geometry/counts, seed/workers/dt/warmup/measurement/repetitions, capture completeness and invariant status. Worker control is established once before Runtime creation. Headless scenarios no longer create a camera: the population oracle explicitly adds one only in rendering mode. This corrected an initially failing oracle expectation rather than adding an unnecessary object to the benchmark.
- C++ benchmark contract passed before the final build, then the full final Release suite passed: **22 passed, one intentional work-probe skip** (23 non-package registrations). Its contract covers complete-step/Cleanup during onDestroy and actual Behavior destruction, scene clear scope, reserved capacity, emitted schema/raw metadata, separate render samples, and no fake samples on quit. Six standard-library Python tests pass: reject historical/incomplete/failed data, preserve five independent runs/population, propagate workers/seed, retain odd rigid counts, reject process failure/too few repetitions, and require complete performance schema in plotting.
- The sweep retains raw files and reports mean/median/stdev/min/max across independent run means. Explicit per-step validation recomputes supports from contact normals/configuration and checks manifolds, filters, finite state and population. Validation snapshots permit exact IDs/support/sleep/features plus declared float tolerances. These runs are labeled `timing_eligible=false`; ordinary timings have only boundary checks outside step clocks. The plotting loader rejects validation captures and historical partial-frame data.
- Final normal build (including Mario and independent public headers) passes. `ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke`: **21 passed, two intentional instrumentation skips**, 6.34 seconds. Full final ASan/UBSan build and equivalent CTest command: **21 passed, two intentional instrumentation skips**, 15.63 seconds, with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Both include 1,000 scene transitions across eight callback families and all original geometry/sleep regressions. Leak detection remains unverified.
- Final Release benchmark-enabled CTest command: **22 passed, one intentional work-probe skip**, 4.99 seconds. Timings here are test durations under build contention, not benchmark performance claims. Logs are retained with final evidence below. Benchmark, work-counter and sanitizer configurations remain separate.
- README, API migration, coding guidance and focused C++ CI now describe actual contracts and commands. CI instruments bundled TBB for TSan, keeps dependency tests off, runs package relocation and dedicated work/benchmark configurations, and visibly fails unsupported/racy sanitizer execution instead of suppressing it. No Python GUI cleanup/framework is required.


## Final physics/package/work gate and hot-path audit (T066, T093)

After formatting, the audit removed a duplicate parent world-position traversal in `GameObject::setPosition`; the already computed local position is now reused. Normal and ASan/UBSan full non-package suites passed again (21 passed, two intentional instrumentation skips each). Instrumented TBB/engine TSan transform/support/geometry tests passed again **3/3**, including the frozen 128-support fixture and five six-worker comparisons to one worker: exact support/contact/event traces and physical state within 1e-5. No runtime suppressions were added. This certifies those physics-only hosts, not desktop SDL audio threads.

The final work host passed again: one warmed idle step has zero counted C++ new/new[] allocations and zero transform/hierarchy/reconciliation/geometry/stay/force/movement/sleep work with 1,000 static plus 1,000 sleeping bodies. A dirty root with 20 children visits exactly 21 transforms with no hierarchy rebuild; flat creation/retirement does no hierarchy rebuild or terminal-body integration. This allocator hook does not claim every native/SDL allocation is intercepted.

Final source/core-only installed/relocated consumer and installed-SDK Mario smoke passed again after the audit change. Mario also built in the normal tree; public headers compile independently. The core-only Release archive has no BenchmarkRecorder or work-probe symbols (`nm -C ... | rg 'platformator_test::work|BenchmarkRecorder::'` produced no matches). This verifies disabled instrumentation compiles out. Original historical benchmark SHA256SUMS verification passed unchanged.

The audit retained dense ownership/dispatch arrays, indexed free/retirement/start/reconciliation queues, canonical pair-local results, serial support reduction, subscribed stays/body membership and reusable buffers. Feasible performance fixes were implemented: skip idle grid/TLS and flat hierarchy work, avoid sleeping/static/terminal integration, avoid per-pair normal vectors, retain solver friction/block matrices, and reuse the computed local transform. No deliberate per-frame safety scan or allocation fallback was introduced requiring an EFFICIENCY exception. Changed geometry/contact membership still performs the actual incremental work needed to maintain deterministic state. Corrected Release performance acceptance remains separate below.

Reproducible logs: [normal](evidence/audit-normal-tests.txt), [ASan/UBSan](evidence/audit-asan-tests.txt), [TSan](evidence/audit-tsan-tests.txt), [work](evidence/audit-work-tests.txt), [package](evidence/audit-package.txt), [Release/benchmark contract](evidence/final-release-tests.txt), [Python contracts](evidence/final-python-tests.txt), [historical checksums](evidence/historical-checksums.txt).


### Additional correctness defect found by benchmark validation

The first large invariant preflight rejected the 500-body rigid container: `Container Box 148 / Container Box 168` had a unit normal but **zero contact points**. Isolating the input produced two 28×28 boxes at (-431.325165, 138.851532), rotation -0.81292206, and (-450.082489, 161.533493), rotation -2.28055906. A double-precision SAT calculation shows a roughly 3e-5 separating gap; single-precision projections rounded it to touching. The clipping stage correctly returned no points, but SAT still published a Collision/support contribution.

A focused public-runtime regression reproduced the empty-manifold assertion before the fix ([red test](evidence/manifold-red-tests.txt)). SAT now accepts box contact only when clipping produces an actual manifold; zero-point candidates clear through the normal pair-result/support/exit reconciliation. This is a geometry acceptance condition, not a blanket defensive scan or a fabricated fallback point. The focused case, complete Release suite and previously failing 500-body/720-step oracle pass ([green suite](evidence/manifold-green-tests.txt), [container oracle](evidence/manifold-green.txt)). The diagnostic is preserved in [input evidence](evidence/manifold-input.txt). Initial preflight output is [recorded separately](evidence/invariant-red.txt); it is not accepted benchmark data.

The post-fix full normal and ASan/UBSan suites pass again, including the new regression, with the same two expected instrumentation skips. Complete invariant and repeated timing captures are rerun against the final source fingerprint. Public-header translation units now use CMake file(GENERATE) so unchanged checks retain timestamps across reconfiguration instead of forcing needless recompilation.

## Requirement-to-evidence map

| Requirements | Implementation and discriminating evidence |
| --- | --- |
| FR-001–FR-003 | Shared step/barrier pipeline, prepared replacement and latched last-wins requests; scene transition contract and 1,000-cycle eight-family stress hosts |
| FR-004–FR-006 | Indexed start/reconcile/retirement queues, immediate dispatch suppression, all destruction callbacks before frees; callback mutation/destruction/foundation and original queued-deletion hosts |
| FR-007–FR-009 | Runtime/generation/type handles, explicit staged resolver and file-ID validation, transactional preparation and checked temporary save; reference, scene I/O and roundtrip hosts including repeated failure cleanup |
| FR-010–FR-011 | Cached parent-first publication after local worker integration, pair-owned manifolds and serial support reduction; transform/support hosts normally and with instrumented TBB/TSan |
| FR-012 | Desired membership plus dirty state, canonical contact/subscription indexes; exact support/filter/trigger/body replacement/SAT reference-flip/retirement event checks |
| FR-013 | Exact circle geometry, valid clipped box manifolds, coupled normal solve and coordinated sleeping; corner/rounded-gap geometry regression, unchanged tall-stack/wide-pile/last-support tests |
| FR-014 | SimulationClock and explicit one-step host dt; time partition, catch-up, pause/manual and rendered quit tests |
| FR-015 | Runtime-owned normalized case-preserving root, required/optional resource contracts and final-user retention; asset-root/startup/resource-retention/failed-staging tests |
| FR-016 | Exported public target/dependencies, independent headers, core fallbacks and standalone Mario packaging; source/SDK/install/two relocations from unrelated cwd |
| FR-017 | ID-based lifetime-safe helpers, project-only registration, dependency test disabling and sanitizer instrumentation; full normal/ASan suites without missing vendor executables |
| FR-018 | Complete-step/destructor/render scope tests, strict schema/raw/manifest parsing, six tooling tests and corrected invariant/performance reference below |
| FR-019 | README, C++ API migration, feature contracts, benchmark README, repository coding guidance and C++ CI |
| PR-001–PR-003, PR-006 | Concrete ownership/phase structures, boundary checks, indexed affected-item queues, pair-local workers and canonical serial reduction; implementation audit and lifecycle/TSan evidence |
| PR-004–PR-005, PR-007 | Dedicated zero-allocation/affected-work oracle; feasible hot-path fixes implemented, correctness oracles deliberately outside timing; see audit and corrected reference |
| SC-001–SC-004 | Full normal/ASan suites, dedicated instrumentation configurations, 1,000 cycles and original assertions retained; new large-workload defect receives a failing-then-passing geometry regression |
| SC-005 | Exact 128-support/event traces and 1e-5 frozen worker state; full workload snapshot tolerances declared in benchmark invariants.json |
| SC-006 | Source/installed/relocated consumers, installed Mario and observable startup/I/O failures |
| SC-007–SC-008 | Complete-step destruction inclusion, preserved raw independent captures, explicit historical scope limitations, work counters and audit; no universal cross-engine or buggy-baseline speed claim |

Host scope: Linux x86-64/GCC 16.2.1 is validated. ASan checks memory access and UBSan; `detect_leaks=0` leaves leak detection unverified. TSan acceptance covers physics-only worker contracts with instrumented TBB, not SDL desktop audio threads (the earlier desktop dependency race remains a limitation). Windows/macOS and CI execution itself have not been exercised locally. Constitution remains the original unratified template; no governance policy is inferred from it.


## Corrected benchmark invariant acceptance

`python3 benchmark/capture_cleanup.py --mode validate --runner bin/cleanup-benchmark/platformator_benchmark_runner --output benchmark/results/cpp-cleanup` completed successfully after the rounded-gap fix. All **18 workload sizes × two worker limits = 36 fresh processes** ran 120 warmup plus 600 measured steps with expensive validation after every step. Headless population, finite body/transform state, valid bounded contact manifolds, filters and independently reduced exact support counts pass. Final serial/six-worker snapshots agree: IDs/supports/sleep/contact features exact; position/velocity within 1e-4, rotation/angular velocity/contact normal within 1e-5, separations within 1e-4. These are predeclared capture-script tolerances, not adjusted after seeing results.

The largest narrow case has 800 objects and 133,007 final accepted contacts. Final rigid-body support totals for 100/500/1,000 bodies are 80/351/620, with 118/704/1,297 final contacts. Static and moving churn each preserve 1,000 live bodies at every checked step. Validation durations are explicitly ineligible for speed claims. [invariants.json](../../benchmark/results/cpp-cleanup/invariants.json) contains all cases/tolerances, and compressed validation archives preserve full raw CSV/manifests/body/contact snapshots/logs. Source fingerprint: `fe2dc8c9f5510658c9eea33b4019d439f6659d17dc29b557013dc283608cecc1`, checked against both the binary metadata and actual source inputs.

Post-fix final non-package runs: normal **21 passed/two instrumentation skips**, 5.80 s; ASan/UBSan **21 passed/two skips**, 12.41 s; benchmark-enabled Release **22 passed/one work skip**, 3.60 s. TSan physics **3/3**, 6.27 s; dedicated work **1/1**, 0.03 s. Source/SDK/relocated/Mario acceptance passed again. Final logs are [normal](evidence/acceptance-normal-tests.txt), [ASan/UBSan](evidence/acceptance-asan-tests.txt), [Release](evidence/acceptance-release-tests.txt), [TSan](evidence/acceptance-tsan-tests.txt), [work](evidence/acceptance-work-tests.txt), [package](evidence/acceptance-package.txt), [tooling](evidence/acceptance-python-tests.txt), [symbol inspection](evidence/acceptance-symbols.txt), [unchanged-header no-op build](evidence/header-noop.txt) and [invariant run order](evidence/acceptance-invariants.txt).


### Contact membership sorting removed after preliminary timing

The initial corrected-source timing attempt exposed an avoidable whole-contact-list sort in narrow phase whenever any membership changed. This collection feeds diagnostics/counts, while solver and subscribed-event dispatch already have separate canonical lists. At 133,007 contacts it caused expensive unused ordering. The capture was deliberately stopped before job 28 and excluded from the reference; no partial run or build-contended number is promoted to a baseline.

Contact membership now retains O(1) indexed insert/swap-removal without sorting; only the solver and observed-event lists canonicalize when their membership changes. The helper returns membership change to those consumers instead of keeping an unused contact-order dirty flag. Public behavior/physics ordering remains defined by the canonical solver/events, and validation sorts full snapshots outside timing. This implements the feasible efficient alternative rather than leaving an EFFICIENCY exception. [The earlier corrected invariant report](evidence/pre-contact-order-invariants.json) is preserved as pre-optimization evidence; the final capture below uses a fresh fingerprint and full invariant rerun.


## Final invariant gate after contact-order optimization

Final source fingerprint: `9fad5294d11a084a705051a0877aedf84a56e14f38a28eecfae815068581af93`. The full 36-process/18-case oracle passed again, with the same exact identity/support/sleep/features and declared floating-point tolerances. [Final invariants](../../benchmark/results/cpp-cleanup/invariants.json) and compressed validation archives now correspond to this fingerprint; the earlier `fe2dc8...` report is explicitly pre-optimization evidence.

Normal **21 passed/two intentional skips**, 5.79 s; ASan/UBSan **21 passed/two skips**, 12.44 s; TSan physics **3/3**, 6.44 s; dedicated work **1/1**, 0.03 s. The benchmark-enabled full Release suite passed **22 tests with one intentional work skip**. Core-only Release/package relocation/Mario passed again, and core-only Release symbol inspection still finds no recorder/work-probe symbols. These repeat checks are warranted by the contact-membership change; no physical assertion/tolerance was weakened.

Final logs: [normal](evidence/contact-order-normal-tests.txt), [ASan/UBSan](evidence/contact-order-asan-tests.txt), [TSan](evidence/contact-order-tsan-tests.txt), [work](evidence/contact-order-work-tests.txt), [Release](evidence/contact-order-release-tests.txt), [package](evidence/contact-order-package.txt), [invariant order](evidence/contact-order-invariants.txt). The 1,000-cycle stress host is included in the full normal/ASan/Release suites. Correctness-only timings and earlier build-contended/preliminary timing attempts remain ineligible for speed comparisons.


## Final corrected timing reference and completion

The final `capture_cleanup.py --mode time` completed **180/180 fresh pinned Release processes**, 120 warmup + 600 measured steps per process, one/six workers, seed 0, five independent repetitions per workload size, shuffled job order seed 1. Timing ran 2026-10-22 09:09:01–09:14:21 UTC after builds/tests finished. [RESULTS.md](../../benchmark/results/cpp-cleanup/RESULTS.md) records all reproduction/settings/noise/caveats; [summary.csv](../../benchmark/results/cpp-cleanup/summary.csv) contains all 36 aggregates. Largest-case complete-step means in ms (one/six workers): broad 0.608/0.208; dense broad 2.653/2.479; narrow 34.114/17.365; rigid container 1.513/1.167; static churn 2.735/1.401; moving churn 2.845/1.437. Independent run spread is preserved, not replaced with a single favorable run.

All raw timing files were reparsed after capture: 180 unique process records, five unique repetition indexes per case, complete schema/build provenance, requested 600 raw steps, raw means and every aggregate mean/median/stddev/min/max match. Twelve validation archives retain the 36 separate expensive oracle runs. Extracted source fingerprint matches the final accepted fingerprint and all timing manifests. Archive/source/report checksums pass; original pre-cleanup checksums still pass unchanged. [Verification](evidence/reference-verification.txt) and [final checksums](evidence/reference-final-checksums.txt) are retained. The parser additionally rejects missing source/compiler/build provenance; **seven** tooling tests pass, including that new failure case ([tooling](evidence/final-tooling-tests.txt)).

Historical six-worker static churn is above its earlier run spread; the report explicitly records this and reviews the affected-object lifecycle/identity/reconciliation/free cost. Other historical scope/physics differences prevent a controlled optimization ratio. The corrected reference is the acceptance anchor; no data was rerun selectively to hide an unfavorable value, and interrupted preflight attempts are excluded. Complete-step cleanup/destruction, scalar scope boundaries and Godot non-equivalence remain explicit.

Leak detection was retried with `ASAN_OPTIONS=detect_leaks=1` on foundation/destruction/scene-I/O/1,000-transition hosts. All four stop at LeakSanitizer's fatal `LeakSanitizer does not work under ptrace` host error; they are **not leak-check passes** ([host evidence](evidence/leak-host-check.txt)). Their ASan memory-access/UBSan acceptance remains the separately recorded detect_leaks=0 suite. TSan remains physics-only with instrumented TBB, with desktop SDL audio safety and other OSes unverified.

The README's headless CLI smoke was checked with an explicit empty physics-compatible scene; the default desktop scene loads textures and correctly requires a renderer. No engine fallback hides this backend requirement. The documented corrected command passes. No extension hooks file exists. The constitution template and unrelated staged state were preserved; no commit/staging/publishing was performed.


## LeakSanitizer access resolved (follow-up)

An approved run outside the execution sandbox successfully runs LeakSanitizer. The prior ptrace error is a sandbox limitation, not a permanent host inability. Command: `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir bin/cleanup-asan -R '^platformator_runtime_foundation_test$' --output-on-failure`, executed with elevated sandbox permissions.

The foundation host now fails on an actual LeakSanitizer report: **2,000 bytes in 40 allocations**. Stacks include SDL_mixer initialization/dynamic FluidSynth module loading. Attribution and the remaining leak-enabled matrix are outstanding; do not classify all findings as engine leaks or dependency-only leaks without investigation. No suppressions or code changes were applied. [Full leak report](evidence/leak-unsandboxed-foundation.txt).


## Full LeakSanitizer run and dependency attribution

Ran the entire existing ASan/UBSan non-package suite **outside the sandbox** with `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir bin/cleanup-asan --output-on-failure -E package_smoke`. Result: **7 passed, 14 failed with leak reports, 2 intentional instrumentation skips** (23 CTest registrations), 13.42 seconds. Package smoke builds separate unsanitized SDK/consumers and is intentionally excluded from this leak claim. No engine/build/CI settings or suppressions were changed.

All reported allocation stacks go through `LoadModule_fluidsynth → FLUIDSYNTH_init → MIX_Init`; none is an engine-owned allocation stack. Across the 14 failing processes LeakSanitizer reports **8,200 bytes / 164 allocations**. The physics-only transform/support/geometry hosts pass without leak reports. No additional ASan memory-access or UBSan diagnostics were observed. This does not turn the full leak-enabled suite into a pass.

An independent [minimal probe](evidence/leak_mixer_probe.cpp) links only the same instrumented bundled SDL3/SDL3_mixer shared libraries, with no Platformator library, objects, components, scenes, window, mixer device, or audio tracks. It calls `SDL_Init(0); MIX_Init(); MIX_Quit(); SDL_Quit();` repeatedly:

| Initialization cycles | Exit | Reported leak |
| --- | ---: | --- |
| 0 | 0 | None |
| 1 | 1 | 200 bytes / 4 allocations (96, 48, 48, 8 bytes) |
| 10 | 1 | 2,000 bytes / 40 allocations |

The same per-cycle signature reproduces without engine code, isolating the observed findings to the FluidSynth dynamic dependency initialization/unloading path on this host. This is stronger evidence than simply assuming every external stack is harmless; the underlying responsible module still needs further investigation/fixing. No suppression or removal of MIDI support was applied. No engine-owned allocation leak was detected by this run; that is not a universal guarantee of leak freedom.

Reproduce the control from repo root:

```sh
c++ -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I vendored/SDL3-3.4.2/include -I vendored/SDL3_mixer-3.2.0/include \
  specs/001-cpp-engine-cleanup/evidence/leak_mixer_probe.cpp \
  -L bin/cleanup-asan -Wl,-rpath,"$PWD/bin/cleanup-asan" \
  -lSDL3_mixer -lSDL3 -o /tmp/platformator-mixer-leak-probe
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/platformator-mixer-leak-probe 1
```

Run outside the sandbox; repeat with 0 and 10. [Structured per-test summary](evidence/leak-full-summary.json), [compressed full CTest output](evidence/leak-full-suite.txt.gz), [zero-cycle output](evidence/leak-mixer-zero.txt), [one-cycle output](evidence/leak-mixer-one.txt), [ten-cycle output](evidence/leak-mixer-ten.txt). These supersede the earlier statement that leak execution is entirely unavailable: sandboxed execution fails under ptrace, while approved outside-sandbox execution works and exposes the dependency findings above. Historical benchmark files and their captured source remain unchanged.
