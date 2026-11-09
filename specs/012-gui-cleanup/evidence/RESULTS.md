# Current implementation and acceptance ledger

**Review date:** 2026-11-08
**Source revision:** `b45bdff9` with an uncommitted worktree. The evidence below describes the current Linux source/build state; no claim is made that the shared worktree is a clean revision.
**Scope:** This ledger records delivered work and open evidence for FR-001–052 and SC-001–017. It does not mark the feature accepted while platform, desktop, performance, or integrated regression work remains open.

## Requirement ledger

“Mapped tasks checked” means the implementation/test tasks in the existing task-to-requirement map are checked. It does not replace the story walkthrough, convergence tasks, or integrated acceptance gates.

| Requirement | Mapped implementation and focused test tasks | Open mapped tasks | Current state | Evidence |
| --- | --- | --- | --- | --- |
| FR-001 | T002, T019, T021, T023, T028, T029 | None | Mapped tasks checked | [US1.md](US1.md), [US3.md](US3.md) |
| FR-002 | T019, T020, T021, T022, T025, T029 | None | Mapped tasks checked | [US1.md](US1.md), [US3.md](US3.md) |
| FR-003 | T020, T024, T029, T100 | None | Mapped tasks checked | [US1.md](US1.md), [US3.md](US3.md) |
| FR-004 | T020, T024, T025, T027, T029, T075, T137 | None | Mapped tasks checked | [US1.md](US1.md), [US3.md](US3.md) |
| FR-005 | T009, T014, T026, T027, T029, T051, T052, T074, T137 | T052 | Partial; mapped tasks open | [US1.md](US1.md), [US3.md](US3.md) |
| FR-006 | T002, T007, T008, T009, T010, T011, T012, T013, T014, T017, T023, T026, T027, T029, T051, T052, T074, T109, T113, T134, T135 | T052 | Partial; mapped tasks open | [US1.md](US1.md), [US3.md](US3.md) |
| FR-007 | T030, T032, T034, T035, T036, T038, T041, T043 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-008 | T030, T031, T032, T034, T035, T036, T037, T040, T043 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-009 | T031, T033, T039, T040, T043, T060, T140 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-010 | T030, T032, T033, T039, T041, T043 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-011 | T030, T038, T039, T042, T043 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-012 | T030, T032, T033, T034, T035, T036, T037, T038, T040, T041, T042, T043 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-013 | T007, T008, T011, T012, T017, T030, T031, T040, T041, T042, T043, T078, T132 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-014 | T031, T033, T042, T043, T078 | None | Mapped tasks checked | [US2.md](US2.md), [US4.md](US4.md) |
| FR-015 | T044, T046, T047, T048, T052 | T052 | Partial; mapped tasks open | [US3.md](US3.md) |
| FR-016 | T045, T046, T049, T050, T052 | T052 | Partial; mapped tasks open | [US3.md](US3.md) |
| FR-017 | T012, T017, T044, T051, T052, T134, T135, T139 | T052 | Partial; mapped tasks open | [US3.md](US3.md) |
| FR-018 | T004, T053, T056, T060, T065, T068, T142, T146 | T142, T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-019 | T053, T057, T058, T059, T060, T061, T065, T068, T098, T140, T146 | T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-020 | T013, T055, T061, T062, T068, T142, T146 | T142, T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-021 | T033, T047, T053, T063, T068, T146 | T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-022 | T016, T064, T065, T068, T146 | T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-023 | T013, T021, T039, T047, T053, T055, T058, T060, T068, T142, T146 | T142, T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-024 | T056, T068, T142, T146 | T142, T146 | Partial; mapped tasks open | [US4.md](US4.md) |
| FR-025 | T070, T073, T076, T079 | T079 | Partial; mapped tasks open | [US5.md](US5.md) |
| FR-026 | T069, T072, T073, T079 | T079 | Partial; mapped tasks open | [US5.md](US5.md) |
| FR-027 | T009, T014, T069, T070, T071, T072, T073, T074, T075, T079 | T079 | Partial; mapped tasks open | [US5.md](US5.md) |
| FR-028 | T016, T070, T077, T079 | T079 | Partial; mapped tasks open | [US5.md](US5.md) |
| FR-029 | T070, T071, T073, T079, T100 | T079 | Partial; mapped tasks open | [US5.md](US5.md) |
| FR-030 | T070, T078, T079, T112 | T079 | Partial; mapped tasks open | [US5.md](US5.md) |
| FR-031 | T094, T106, T107, T145 | T107, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-032 | T041, T095, T106, T107, T121 | T107 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-033 | T016, T048, T064, T092, T100, T106, T107 | T107 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-034 | T007, T016, T092, T096, T097, T098, T099, T107, T145 | T107, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-035 | T016, T092, T101, T107, T145 | T107, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-036 | T093, T097, T100, T102, T103, T104, T105, T106, T107, T115, T129, T139, T145 | T107, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-037 | T015, T034, T035, T036, T080, T081, T082, T083, T084, T085, T086, T087, T088, T090, T091, T103, T104, T105, T107, T122, T144, T145 | T088, T090, T091, T107, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-038 | T089, T090, T091, T144, T145 | T089, T090, T091, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-039 | T015, T050, T080, T081, T082, T083, T084, T085, T088, T091, T145 | T088, T091, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-040 | T015, T017, T051, T080, T081, T082, T083, T084, T085, T086, T087, T088, T091, T132, T144, T145 | T088, T091, T145 | Partial; mapped tasks open | [US6.md](US6.md), [US7.md](US7.md) |
| FR-041 | T109, T110, T115, T116 | None | Mapped tasks checked | [US8.md](US8.md) |
| FR-042 | T108, T111, T112, T113, T115, T116 | None | Mapped tasks checked | [US8.md](US8.md) |
| FR-043 | T016, T108, T112, T114, T115, T116 | None | Mapped tasks checked | [US8.md](US8.md) |
| FR-044 | T007, T008, T010, T011, T012, T016, T051, T117, T119, T123, T130, T132, T133, T135, T139 | T133 | Partial; mapped tasks open | [US9.md](US9.md) |
| FR-045 | T086, T087, T118, T120, T121, T122, T123, T124, T126, T129, T130, T132, T133 | T133 | Partial; mapped tasks open | [US9.md](US9.md) |
| FR-046 | T118, T120, T123, T124, T125, T126, T127, T128, T129, T130, T132, T133 | T133 | Partial; mapped tasks open | [US9.md](US9.md) |
| FR-047 | T118, T131, T133 | T133 | Partial; mapped tasks open | [US9.md](US9.md) |
| FR-048 | T134, T135, T136, T137, T138, T139, T140, T141 | None | Mapped tasks checked | [US10.md](US10.md) |
| FR-049 | T001, T003, T006, T018, T020, T022, T028, T029, T052, T068, T090, T091, T107, T116, T133, T141, T143 | T052, T090, T091, T107, T133 | Partial; mapped tasks open | [REGRESSIONS.md](REGRESSIONS.md), [US1.md](US1.md), [US3.md](US3.md), [US6.md](US6.md), [US7.md](US7.md), [US9.md](US9.md) |
| FR-050 | T002, T003, T008, T018, T030, T031, T037, T043, T044, T045, T052, T053, T068, T079, T090, T091, T107, T116, T118, T133, T141, T143 | T052, T079, T090, T091, T107, T133 | Partial; mapped tasks open | [REGRESSIONS.md](REGRESSIONS.md), [US1.md](US1.md), [US3.md](US3.md), [US5.md](US5.md), [US6.md](US6.md), [US7.md](US7.md), [US9.md](US9.md) |
| FR-051 | T001, T004, T005, T006, T054, T066, T067, T068, T146, T147, T149 | T146, T147 | Partial; mapped tasks open | [BASELINE.md](BASELINE.md), [GUI-PILOT.md](GUI-PILOT.md) (no matched capture retained) |
| FR-052 | T001, T005, T018, T029, T043, T052, T068, T079, T091, T107, T116, T122, T133, T141, T142, T143, T144, T145, T148, T149 | T052, T079, T091, T107, T133, T142, T145 | Partial; mapped tasks open | [README.md](../../../README.md), [editor/README.md](../../../editor/README.md), [parity.md](../../../specs/011-add-imgui-editor/parity.md), [GUI_AUDIT.md](../../../docs/GUI_AUDIT.md), [quickstart.md](../quickstart.md) |

## Success criteria

| Criteria | Current evidence | State |
| --- | --- | --- |
| SC-001–002 | Headless authoring, selection, transform, history, Inspector, hierarchy, template, and animation regressions cover the implemented transactions. The US1/US5 native pointer walkthroughs remain unverified. | Partial; T052/T079 and native desktop acceptance remain open. |
| SC-003–005 | Linux watcher/resource integration exercises native notifications, same-metadata changes, root recovery, source/assets shared watch budget, and scoped preview invalidation. Windows/macOS adapter verification is skipped; current monitoring acceptance is not cross-platform. | Partial; T162/T205 remain open. |
| SC-006 | Diagnostics tests cover current/stale revisions, F6 refresh and Run gating; numeric draft tests cover one Undo per committed edit. | Partial; the native F6-to-Run walkthrough remains open under T052. |
| SC-007–009 | No matched GUI performance capture was run. The historical baseline executable is not retained for a reproducible comparison; [GUI-PILOT.md](GUI-PILOT.md) records the unmet status. | Unmeasured; leave targets unmet under T146/T147/T176/T219/T262. |
| SC-010 | Linux package smoke installs and relocates editor-off/session-off consumers; POSIX process/session tests cover owned foreground children. | Partial; Windows/macOS verification and adapters remain open. |
| SC-011 | Deterministic scale/DPI and toolbar bounds have headless coverage. Host-registry, detached SDL renderer-host, cross-host movement, and semantic snapshot/action tests are implemented. No physical monitor transition, two-host workflow, or screen-reader walkthrough was performed. | Partial; T107/T145 remain open. |
| SC-012 | Search, assignment, discovery, moves, diagnostics and semantic actions have headless coverage. | Partial; independent native US5/US6 walkthroughs remain open. |
| SC-013 | Template/component tests cover typed references, cross-scene target collision, atomic save, instantiation, Copy/Paste/Reset and exact Undo/Redo. | Implementation/test lane checked; native walkthrough is not claimed. |
| SC-014 | Session tests cover separate child Runtime, one-step pause/resume, immutable launch identity, frame backpressure, stale handles and source invalidation. Linux failure teardown now waits for child reap before cancelling channels and releasing the mapping; Game texture retirement follows generation invalidation. T132 implementation is complete; native verification is skipped. | Partial; compatible/external UI walkthrough remains open under T133/T194/T237, with native Windows/macOS verification under T091/T145. |
| SC-015 | Sprite/animation implementation and round-trip/game-consumption cases are recorded in US10 evidence. | Implementation/test lane checked; visual desktop walkthrough is not claimed. |
| SC-016 | Before T259, the integrated Linux Debug and ASan/UBSan CTest suites each accounted for 70 tests: 69 passed and the configured benchmark contract test skipped. The D-Bus AT-SPI provider test and package smoke both passed. The ASan/UBSan suite completed in 597.03 seconds, including the package smoke in 482.01 seconds; LeakSanitizer was disabled under the container limitation. The standalone Linux install-validation script also passed install/relocation and editor-OFF/session-OFF SDK consumer checks. The T259-focused Debug suite passed 3/3. | Partial; T142 and T198/T241 acceptance remain open. T143 startup evidence is checked; unavailable platform and desktop acceptance remain open. |
| SC-017 | Capture tools have bounded pilot guards and focused guard tests exist. No comparative performance campaign was run. | Guard implementation present; comparative evidence remains open. |

## Integrated Linux verification before T259

The integrated Linux results below predate the T259 animation-save change:

- US3/US5 diagnostics and authoring batch: 11/11 passed.
- Native file, native process and PlaySession batch: 3/3 passed.
- Template, component operations, Workspace and Resources batch: 4/4 passed.
- PlaySession rerun after adding Game-panel generation/nonce rejection: 1/1 passed.
- An earlier editor-off/session-off package smoke passed 1/1. The first cold pass took 325.13 seconds; its rerun against the existing build cache took 5.97 seconds. A later full-suite invocation timed out on this check; the final-source rerun below supersedes that failure for current Linux status.
- An earlier Release build succeeded with work probes and editor counters enabled for correctness checks. Full CTest accounted for 70 tests: 68 passed, the benchmark contract test skipped (77), and `platformator_package_smoke` timed out at 180 seconds during its source-tree consumer build. The timeout was not retried at that time; the final-source Debug and ASan/UBSan runs below subsequently passed package smoke. See [REGRESSIONS.md](REGRESSIONS.md).
- Animation source-region accessibility and POSIX replacement recovery tests passed in the focused Release run. The selected serial Debug AddressSanitizer/UBSan lane passed 65/65 with `ASAN_OPTIONS=detect_leaks=0`. The ptrace-related LeakSanitizer limitation and exclusions are recorded in [REGRESSIONS.md](REGRESSIONS.md).
- After that integrated run, Run Settings semantic choices and toggles were made actionable for the owner-thread provider. Release and ASan/UBSan editor/test targets rebuilt, and `platformator_editor_discovery_test` passed 1/1 in each. At that point the full suite had not been rerun; the final-source rerun is recorded below. Native accessibility-reader and platform checks remain open.
- A subsequent Linux AT-SPI fix aligned numeric `Value.SetCurrentValue` actions with the advertised `set_value` contract and added finite/range validation. The logical accessibility and Linux provider lifecycle suites passed 2/2 in Release and 2/2 in ASan/UBSan. The lifecycle suite used its graceful no-registry path; no screen-reader session was verified.
- A focused semantic-input change added AT-SPI Text/EditableText routing, Console search replacement, and bounded Run Settings dimensions. The Animation, Console, discovery, logical accessibility and Linux provider suites passed 5/5 in Release and ASan/UBSan. A private fake registry exercised provider D-Bus methods; no desktop registry or reader was available at that point. The final-source full suites are recorded below. Build identities and commands are in [REGRESSIONS.md](REGRESSIONS.md).
- The Linux Project watcher integration now verifies one native owner for nested, disjoint, and equal source/asset roots, including path-specific availability/freshness and asset-root removal/recreation. The workspace and asset watcher suites passed 2/2 in Release and 2/2 in ASan/UBSan. Native Windows/macOS watcher verification remains open; see [US2.md](US2.md) and [REGRESSIONS.md](REGRESSIONS.md).
- PlaySession now has Linux crash/reap and project-switch identity regressions, plus an assertion for the external native-input guidance. The PlaySession suite passed 1/1 in Release and ASan/UBSan. Windows/macOS channel adapters and visual walkthroughs remain open.
- Before the T259 animation-save change, the integrated Linux Debug and ASan/UBSan full CTest reruns each passed 69/70 tests; `platformator_benchmark_contract_test` skipped by configuration. The Linux AT-SPI provider test, package smoke, and standalone install/relocation lane passed. The ASan/UBSan CTest run took 597.03 seconds; its package smoke took 482.01 seconds under the test's 600-second timeout. Current build identities and commands are in [REGRESSIONS.md](REGRESSIONS.md).

The package smoke confirms `PLATFORMATOR_BUILD_EDITOR=OFF` and `PLATFORMATOR_ENABLE_EDITOR_SESSION=OFF`, installs and relocates public consumers, runs them from an unrelated directory, and exercises a headless Runtime step plus the Mario smoke. `ldd` resolves runtime libraries from the relocated prefix. `readelf -d` has no ImGui, editor, or accessibility-provider dependency. See [RUNTIME.md](RUNTIME.md).

## T259 animation source conflict regression (2026-11-08)

The animation save/reload lifecycle change rebuilt `platformator_editor`, `platformator_editor_tabs_test`, `platformator_editor_workspace_test`, and `platformator_editor_assetdocument_test` in the existing `bin/editor-debug` tree. The focused affected suite passed 3/3. This targeted rerun covers the changed source-conflict paths; it does not supersede the prior full-suite results for all targets.

```text
cmake --build bin/editor-debug --parallel 4 --target platformator_editor_tabs_test platformator_editor_workspace_test platformator_editor_assetdocument_test platformator_editor
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software ctest --test-dir bin/editor-debug -R 'platformator_editor_(tabs|workspace|assetdocument)_test' --output-on-failure
```

The changed cases cover observed-version overwrite conflicts, stale source and Save As destination rejection, failed/cancelled save and reload preservation, identity-scoped reload, and history retention. No platform desktop check or performance capture was run.

## T263 source cleanup audit (2026-11-08)

The application lifecycle modules were reviewed alongside this documentation reconciliation. `EditorPreview::refresh_assets()` has no remaining declaration or call; preview invalidation is consumer-scoped. `ProjectPanel::refresh()` remains only on the explicit Refresh Assets button and semantic action, where it clears cached listings/resources and requests the documented retry path. It is not used as a whole-project fallback for ordinary watcher events. `ProjectSession` owns the project/document/resource/watcher lifecycle, while `GamePipeline` remains the sole child `PlaySession` owner. The app's `workspace`, `selection`, and history pointers are active-tab aliases rebound together by `bind_active_document`, not separate owners. `assetformats.h` centralizes asset presentation/reference classification; Project source-input filtering is a distinct CMake/source allowlist. Editor-session command branches are capability-gated paths for supported runners, so this audit found no dead protocol branch to remove. T142 remains open for its full dependency and acceptance chain; this focused review found no additional safe code deletion.

The linked current docs now describe detached SDL hosts and cross-host movement as implemented with Linux headless coverage, and physical host walkthroughs as open. Windows UI Automation and macOS Accessibility provider sources are present but native compilation/verification and reader walkthroughs remain open. The dated 2026-11-05 GUI audit findings and the 2026-11-05 baseline observations remain labeled as historical.

The integrated Linux Debug and ASan/UBSan suites above predate T259. T259 received a focused Debug rebuild and 3/3 affected tests passed; an integrated full-suite rerun after T259 was not performed. Scoped `git diff --check` passed for the current changes. LeakSanitizer and unavailable platform/desktop checks are not claimed.

## Open work and unavailable acceptance

The task ledger currently has **29 unchecked tasks**: T052 T079 T088 T089 T090 T091 T107 T133 T142 T145 T146 T147 T162 T176 T185 T186 T187 T188 T198 T205 T219 T228 T229 T230 T231 T241 T260 T261 T262. T260/T261 are explicitly user-excluded native/manual checks; T262 remains open because there is no retained matched GUI baseline/candidate capture.

The previous all-label runs recorded package-smoke timeout and sandbox D-Bus setup failures; those two suites now pass in the final-source Debug and ASan/UBSan full runs. Each full run accounted for 70 tests: 69 passed and the configured benchmark contract test skipped. The standalone editor validation script also passed its six focused suites and install/relocated SDK consumer checks. LeakSanitizer remains disabled in this container. Earlier failures remain documented as historical attempts in [REGRESSIONS.md](REGRESSIONS.md); see its final-source entries for current status and build identities.

After an earlier all-label run, executable-relative optional-layout startup cases passed 1/1 in the ASan/UBSan build with LeakSanitizer disabled. The Ubuntu Linux validation script then passed end to end: configure/build, six focused suites (6/6, including asset-watcher integration), install and relocated launch, private SDL/oneTBB resolution, and editor-OFF/session-OFF SDK consumer validation. Two initial validator path assumptions were corrected; the run details and final relocated prefix are recorded in [US7.md](US7.md). The GitHub workflow itself has not run, and Windows/macOS lanes remain open. The final-source full Debug and ASan/UBSan reruns are recorded above; they do not close cross-platform or desktop acceptance.

The Run Settings change queues a bounded CMake File API codemodel query only on explicit target refresh. The workspace, startup, and discovery tests passed 3/3 in both Debug and ASan/UBSan after the change, with LeakSanitizer disabled. Live onboarding and Run Settings walkthroughs remain open under T186/T229; the final-source full suite rerun is recorded above.

The later panel-level refresh regression passed the discovery suite 1/1 in Debug and 1/1 in ASan/UBSan after checking that automatic project selection creates no query and explicit refresh creates one and reports it to the semantic UI.

The main outstanding work is:

- Windows file/process/session-channel implementation sources and focused cases are authored, but Windows/macOS compilation or verification and platform packaging/CI remain open (T088–T091, T185/T228). Host-registry and detached-renderer implementation tasks T104/T105 are checked; native walkthroughs remain open. Windows/macOS verification was explicitly skipped by the user.
- The portable host registry, SDL window/context ownership, routed focus/IME input, renderer-local replicas, docking blobs, and host failure tests are implemented and covered by Linux headless tests. Physical two-host, monitor-transition and input walkthroughs remain open (T107/T187/T230).
- Remaining semantic panel/reveal coverage and native-provider verification plus two-host/accessibility walkthroughs remain open (T107/T188/T231). A private fake D-Bus registry exercises Linux AT-SPI Text/EditableText/Value methods; no desktop registry/reader walkthrough was available. Windows UI Automation and macOS Accessibility provider sources are authored but have not been compiled or verified on their native platforms.
- The US3 and US5 native UI walkthroughs (T052/T079). No display is available here: `DISPLAY=:0.0` is unreachable and Xvfb/Weston are not installed.
- T132 implementation is complete: Linux headless PlaySession protocol, crash/reap, slot, EOF, stale-generation, project-switch, and renderer-retirement checks pass; Windows adapter source and fixture are authored but were not compiled or run. Native Windows/macOS verification remains under T091/T145, and the actual embedded/compatible/external walkthrough remains open under T133. T183/T184/T226/T227 implementation and fixture-authoring tasks are checked; no Windows test result is claimed.
- Final T142 cleanup and dependent acceptance records (T142/T198/T241); T143 Linux startup coverage is checked. The integrated Linux Debug and ASan/UBSan suites before T259 passed 69/70 tests with the configured benchmark skip. T142's remaining cleanup audit and dependency chain, plus platform and desktop evidence, remain open.
- A reproducible matched GUI baseline/candidate comparison (T146/T147/T176/T219/T262). The historical baseline executable is not retained; no benchmark was started and no performance improvement is claimed.
- Physical monitor-transition, IME, and native keyboard/scale walkthroughs (T107/T145); the portable T199/T242 implementation and headless evidence are complete.

## Ownership, parallelism and constitution review

- **Ownership:** `GamePipeline` is the sole owner of child `PlaySession` state. The unused second session slot was removed from `ProjectSession`. `ProjectSession` owns project roots, documents, resource index, and watcher lifecycle. `GamePanel` owns only renderer-local frame texture state. Accessibility snapshots are immutable shared objects; actions are validated and dispatched on the editor owner thread.
- **Watcher work:** Linux uses one cancellable native worker for nested, disjoint, and equal source/asset roots, with one project-shared 4,096-directory budget. Discovery is entry/deadline bounded and off the UI thread; callback queues are bounded and publish path-scoped degraded/overflow status. T205 remains open for native Windows/macOS validation, which this run could not perform.
- **CPU parallelism:** Existing oneTBB scheduling is used for bounded project scans and editor preparation where work is independent. UI/document commits remain owner-thread operations. Native watcher threads wait on OS notification APIs and are not a competing CPU worker pool. No new compute pool or runtime parallelism claim was added. The remaining watcher-thread composition and multiworker/performance tradeoffs are unmeasured.
- **Simplicity and integrity:** Removing duplicate PlaySession ownership narrows lifecycle authority. The Linux Game panel rejects messages whose nonce or play generation does not match the current session. Protocol failure teardown now waits for child reap before channel cancellation and mapping release. The unused all-resource `EditorPreview::refresh_assets()` path is removed; manual retry targets failed referenced resources. Project search and icon selection share a single extension classifier. Broader T142 cleanup and native session/process confirmation remain open.
- **Evidence policy:** No performance result is promoted without a reproducible matched baseline. Output/time guards remain enabled; the full benchmark matrix was not run.

This ledger closes the documentation review task only. It does not claim the GUI cleanup feature or any unavailable platform lane is complete.

## Converged code corrections (2026-11-09)

T264–T267 are complete after focused Linux Debug implementation and regression coverage. Run continuations retain the initiating document identity and Stop cancels an unstarted continuation. Animation save gates now cover all runtime-required root fields and runtime-float dimensions. Scene and animation Save As resolve canonical destinations and refuse a destination already owned by another open tab, including symlink aliases and delayed overwrite callbacks. Recovery validation now follows project/uncertainty revisions so persistent dirty coverage does not cancel and resubmit the same full snapshot each frame.

The four affected headless suites passed 4/4 after rebuilding the editor and test targets. Exact commands, build identity, and test executable hashes are recorded in [REGRESSIONS.md](REGRESSIONS.md). The current task ledger has 238 checked and 29 unchecked tasks. Windows/macOS/native desktop and manual walkthrough exclusions remain open; the matched GUI comparison remains unmet because the behavior-neutral baseline executable is unavailable. No benchmark was run.
