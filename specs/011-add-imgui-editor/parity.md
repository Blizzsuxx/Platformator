# Workflow and Field Parity Inventory

This inventory turns FR-018 into implementation/acceptance coverage. Existing tests are behavioral references, not a claim that Python internals or defects must be reproduced. Proposed native test names are defined in [quickstart.md](quickstart.md).

## Workflow mapping

| Workflow and feature requirements | Existing source/tests | Native acceptance |
| --- | --- | --- |
| New/default camera, Open/Save/Save As, startup/last scene, validation, unsaved protection (FR-003–004,008) | `ui/src/platformator_ui/main_window.py`, `scene/serializer.py`; `test_scene_roundtrip.py`, `test_main_window_startup.py` | Native document/workspace suites cover default camera, unknown data, cancellation/failure, startup precedence/missing restore, Validate and unsaved protection; runtime scene-I/O accepts editor output with matching camera semantics. The editor lifecycle controls are wired; native dialogs and full desktop interaction remain unverified in this headless environment. |
| Root/child create, duplicate/delete, nested/component selection (FR-005) | `scene/mutations.py`, `widgets/scene_tree.py`; `test_scene_mutations.py`, `test_scene_tree.py`, `test_main_window_selection.py` | Document/history: unique IDs, insertion/order/root offset, numeric-reference duplication policy, selection repair on delete/undo/document replacement |
| Component palette and Inspector (FR-006,008) | `widgets/component_palette.py`, `widgets/inspector.py`, `scene/models.py`; `test_inspector.py` | Document/workspace: full field table below, valid/invalid commits, compatible add/remove, per-component selection, unknown preservation |
| Behavior catalog/nested/list/reference fields (FR-007–008) | `services/behavior_catalog.py`, Inspector; `test_behavior_catalog.py`, `test_inspector.py` | Catalog/document: executable-registered names, no construction, field kinds, observed custom JSON, dangling and wrong-kind refs, absent fields/default retention, stale data, factory-only schema |
| 2D canvas/picking/navigation/drag (FR-009–010) | `widgets/scene_canvas.py`; `test_scene_canvas.py` | Preview/history/workspace: engine appearance/order, overlap cycling, component handles, translated parents, pan/zoom/frame, local offset drag, input capture, no simulation |
| Undo/redo/dirty state (FR-011) | `services/undo_stack.py`, main window; `test_main_window_selection.py`, `test_main_window_shortcuts.py` | History: every mutation type, separate versus continuous gestures, cancellation, redo branch, saved token/pruning, valid restored selection |
| Asset browsing/path rules (FR-012) | `widgets/asset_browser.py`, `services/asset_paths.py`; `test_asset_path_rules.py`, `test_inspector.py` | Document/workspace: case, separator normalization of known fields only, containment/symlink escape, missing files, type checks, drag/drop/typed/dialog assignment |
| Configure/build/run/stop/settings (FR-013–014) | `engine/cmake_runner.py`, `engine/run_controller.py`, `services/run_settings.py`; `test_engine_runner.py`, `test_run_settings.py` | Process/catalog/workspace: selected target/profile/program, correct current CLI, saved-scene/root snapshot, build/catalog failure prevents launch, grandchildren stopped, bounded output, responsive cancellation |
| Menus/shortcuts/panels/preferences (FR-015–017) | main window and run settings dialog; `test_main_window_shortcuts.py`, `test_editor_smoke.py` | Workspace + desktop review: complete actions, dark layout/style, fields, tooltips, restore/reset, 100%/150%, native dialog and focus behavior |
| Runtime/editor independence (FR-001–002, FR-020) | Root CMake/install package, existing public consumers | Editor-OFF configure/build/install consumer and headless run; editor-ON builds with no legacy GUI packages or network; Dear ImGui absent from engine exports |
| Responsive large scenes/resources (FR-019) | Whole-scene copy baseline in main window/history; existing render driver | Focused interaction pilot and allocation/work probes; targeted changes, no per-frame full serialization, 20-cycle resource checks; no automatic expanded campaign |

Python test paths above are beneath `ui/tests/`. Engine reference tests include scene I/O/script roundtrip, object-reference/asset-root contracts, runtime step/recreation, transform hierarchy, render culling/geometry/contract, and installed-package smoke. New tests must exercise new semantics; existing passing tests alone are insufficient.

## Built-in field coverage

Read actual current serializers/public setters alongside `ui/src/platformator_ui/scene/models.py`; the engine is authoritative if UI defaults or validation are stale. Changes through every supported field go through the same command path and must survive undo/redo and save/reopen.

| Owner | Required editable values |
| --- | --- |
| GameObject | name, tag, active, local position x/y, local scale x/y, rotation |
| BoxCollider | width, height, offset x/y, trigger, collisionGroup, collisionMask |
| CircleCollider | radius, offset x/y, trigger, collisionGroup, collisionMask |
| Rigidbody | velocity x/y, force x/y, mass, angularVelocity, torque, friction, restitution, bodyType, gravity |
| Sprite | textureFilePath, flip, width, height, sourceRectEnabled, sourceRect x/y/w/h when present |
| Animator | currentFrameIndex, playbackSpeed, playing, animationClipFilePath; preview remains static |
| Camera | width/height; recognize legacy nested camera dimensions without silently destroying unfamiliar payload |
| Audio | filePath, gain, loopCount, autoPlay; no authoring autoplay |
| Script | behavior list, add/remove behavior, all known declared serialized fields and safely editable existing extras |
| Recursive fields | booleans, known enums, signed/unsigned integers, finite numbers, strings, vectors/rectangles, optional/null, nested JSON objects/arrays, typed asset and object/component references |
| Unknown component/field | preserve payload; show type/identity and raw/safe recursive data, clearly mark unsupported editing/runtime availability |

IDs and component kind discriminators are identity information, not free-form property inputs. No generic integer-to-reference inference. Null/missing optional data must remain distinct from explicitly provided zero/empty values. Newly materialized fields may use validated editor defaults; never rewrite unrelated imported fields to fill all defaults.

## Deliberate departures and compatibility decisions

1. The authoring document remains lossless rather than mirroring a live runtime. No scripts or automatic sound run in editing; missing/unknown game data remains visible.
2. Replace whole-document snapshots with targeted history. Separate completed drags must no longer merge merely because they use the same object key.
3. Replace source-regex behavior availability with a queried compiled registry. Current Python field synchronization can delete fields absent from inferred templates; the new editor explicitly never does so automatically.
4. A new macro behavior initially stores only its type and inherits omitted C++ defaults on game load; missing-field drafts are not saved until explicitly edited. Do not claim inferred/neutral UI values reproduce constructor defaults.
5. Duplicate references keep existing numeric targets, including originals inside the copied subtree. This preserves legacy behavior and differs from an automatically self-contained Unity-style duplicate. New IDs, root offset/name, sibling placement, and this policy all need direct tests because existing mutation tests do not cover every duplication case.
6. Run uses a separate game window; no embedded Game tab/hot reload/live inspection. Use explicit `--scene` and `--asset-root`, correcting stale legacy positional-launch assumptions.
7. F frames all and Shift+F frames selection, retaining the legacy mapping. Label these rather than claiming exact Unity shortcut parity.
8. Preferences are newly versioned project-keyed files, not a migration of machine-specific Qt settings. Existing scene content is the compatibility boundary; Qt stays usable during migration.
9. Automatic Run compatibility is explicitly limited to declared catalog-compatible entries. Stock `main`, `mario_example`, and other shared-runner/macro targets need only a rebuild. Custom entries without catalog support remain editable/buildable; they are not automatically probed/launched or rewritten. Include positive shared-runner and negative undeclared-custom-entry fixtures.
10. The Run action owns a separate game process/window. The editor has no embedded Game tab, hot reload, or live runtime inspection.
11. One scene document and one current selection are supported per editor window. Multi-scene tabs, multi-selection, and detached OS panel windows are deferred; user docking and tabbing remain within the main application window.
12. The editor keeps scene content and project-keyed preferences in separate files. Existing machine-specific Qt settings are not migrated, so the Qt tool can remain available as a separate migration aid without sharing preference state.

## Workspace scope and acceptance

| Area | Native/automated coverage | Desktop-only evidence |
| --- | --- | --- |
| Stable panels and default layout | `default_panel_visibility`, layout seed, project-keyed preference round trip and offscreen/corrupt layout recovery in `platformator_editor_workspace_test` | Dock/tab placement, hide/reopen, restart and Reset Layout remain a real desktop check |
| Actions and focus routing | `editor_action_descriptors` plus capture-state checks for text, modal, gesture, app focus and Scene focus | Mouse focus, native dialog behavior and typed text must be reviewed in the desktop sequence |
| Style scale and scene isolation | Repeated 100%/150% unscaled-base style application; layout/scale operations preserve document revisions and edit history | Fonts, monitor scale changes, window movement/minimize/resize and actual visible overflow remain desktop checks |
| Unity-inspired workspace | Functional panel roles, actions, static Scene preview and external Run | Visual color/spacing/icon review is recorded per FR-015; no pixel-exact Unity match is claimed |

## Completion evidence

For each row, record fixture/command, expected outcome, automated or desktop check, observed outcome, and limitation. No row can be completed using a mock panel alone. Capture visual-review observations in a compact report; ordinary image comparisons stay in memory. Tests for validation failure must verify preserved document/history and destination content, not just the presence of an error string.

### US2 authoring checkpoint

| Coverage | Fixture or command | Observed result | Remaining dependency |
| --- | --- | --- | --- |
| GameObject fields | `run_document_authoring_cases` in `editor/tests/document_test.cpp`, `platformator_editor_history_test` | Name, tag, active, position, scale and rotation accepted as targeted property commands; mixed history and viewport movement undo/redo restore exact values. | Live project-root switching remains open in T049. |
| Box and Circle Collider | `run_document_authoring_cases` | Shape, width/height or radius, offset, trigger, collision group and mask mutations passed. | UI range/feedback review remains part of later workspace work. |
| Rigidbody | `run_document_authoring_cases` | Velocity, force, mass, angular velocity, torque, friction, restitution, body type and gravity mutations passed. | Undo/history integration is T055–T059. |
| Sprite | `run_document_authoring_cases` | Texture path, flip, dimensions, source rectangle toggle and x/y/w/h mutations passed. | Asset browsing/drag-and-drop integration remains T061–T071. |
| Animator and Audio | `run_document_authoring_cases` | Frame, speed, playing, animation path, audio path, gain, loop count and autoplay data mutations passed; preview remains outside this story. | Static Scene preview remains T040–T051. |
| Script and recursive behavior fields | `run_document_authoring_cases`, `platformator_editor_catalog_test`, `platformator_editor_history_test` | Behavior values preserve missing/null/empty distinctions and unfamiliar data; behavior insertion/removal and typed reference edits undo/redo with saved-state tracking. | Selection-driven Scene highlight and root-switch workflow retain the T049 limitation. |
| References and assets | `run_typed_reference_cases` | Object/component IDs resolve by target kind; dangling and wrong-kind scene references, invalid/missing/wrong-extension assets, traversal and symlink escapes are distinguished; invalid assignments reject while nullable null is accepted. | Asset browser workflows remain T061–T071. |
| Hierarchy and structural edits | `run_document_authoring_cases` | Row order/name refresh, subtree create/duplicate/delete, compatible component/behavior edits, selection repair and exact numeric reference preservation passed. | Scene selection/highlighting integration remains T049–T051. |
| Save/reopen and runtime compatibility | `run_runtime_compatibility_cases`, `platformator_scene_script_roundtrip_test` | Editor-authored camera data loaded/saved in Runtime. A type-only macro behavior retained constructor values for fields omitted from JSON. | Full workflow parity still requires T087. |

Commands and combined outcomes are recorded in `evidence/RESULTS.md`. The US2 panel paths build and the editor document/catalog/workspace/startup suites plus scene, script-roundtrip, object-reference, asset-root and package-smoke checks passed. Integrated preview remains covered by US3; history behavior is recorded below. Real desktop visual review is separate from these headless tests.

### US3 Scene and canvas checkpoint

| Coverage | Fixture or command | Observed result | Remaining dependency |
| --- | --- | --- | --- |
| Preview composition and draw order | `platformator_editor_preview_test` | Flattened Sprite proxies follow document sprite order; alpha overlap changes the in-memory center pixel when the front object is disabled. Source-rectangle and flip pixels matched fixture colors. | Physical renderer backends and desktop visual review remain unverified. |
| Hierarchy transforms and active rules | `platformator_editor_preview_test`, `viewport_cases.cpp` | Parent translations accumulate; local scale and rotation remain local; inactive and missing-resource objects remain available to overlays and picking while inactive sprites do not submit. | Gesture history grouping is completed in US4. |
| Static assets and lifetime | `platformator_editor_preview_test` | Selected animator frame is drawn without playback; missing-resource errors stay cached until explicit refresh; resizing waits until the old target is not active; preview teardown releases proxies/resources before Runtime. | Physical display DPI and long-session desktop review remain separate. |
| View mapping and picking | `viewport_cases.cpp` | Off-origin image bounds, letterboxing, independent 2x/3x output ratios, zoom anchor, pan, frame bounds and repeated topmost cycling passed from fixture math. | Real monitor scaling is not represented by dummy SDL. |
| Object/collider handles and cancellation | `viewport_cases.cpp`, `platformator_editor_history_test` | World drags convert to parent-local position; collider offsets invert object rotation; equal-value commit yields no command; cancellation restores existing and missing values; committed viewport movement is one undoable transaction. | Mouse-driven desktop focus/capture checks remain unverified. |
| App composition and no-camera path | `platformator_editor_startup_test`, `platformator_editor_preview_test` | The Scene panel rendered one headless ImGui frame with canvas overlays; the no-camera preview fixture kept Runtime camera state empty and authored data unchanged; ImGui shut down before preview/Runtime. | Current launch keeps project and asset roots fixed; no live root-switch workflow exists yet. |
| Engine renderer compatibility | `cleanup-work` render-culling geometry/readback/contract plus transform/runtime lifecycle checks | 6/6 passed with work probes enabled and SDL dummy/software; normal Camera and SDLWindow rendering contracts remained intact. | No sanitizer or physical GPU backend run was made. |

The US3 pixel comparisons stayed in memory; no screenshots or frame dumps were retained. Full command/configuration and the ordinary no-probe skip are recorded in `evidence/RESULTS.md`. The shared edit history path is completed in US4; project-root switching and physical desktop review are outside the current headless checkpoint.

### US4 undo, redo, and saved-state checkpoint

| Coverage | Fixture or command | Observed result | Remaining limitation |
| --- | --- | --- | --- |
| Mixed property and structural history | `platformator_editor_history_test` | Property, object, component, behavior, numeric reference, and viewport-position edits each undo and redo; undo to the saved start restores exact JSON. | Real panel input remains a separate desktop review. |
| Selection restoration | `platformator_editor_history_test` | Object creation restores its after-selection on redo; undo restores the before-selection; redo of deletion applies valid selection repair. | T049 root/project switching is still incomplete. |
| Gesture lifecycle | `platformator_editor_history_test`, `viewport_cases.cpp` | Continuous movement creates one entry; separate gestures remain separate; no-op commit creates none; canceling a newly materialized missing key removes it exactly. | Mouse-driven focus-loss behavior was not exercised on a desktop. |
| Saved tokens and branches | `platformator_editor_history_test` | Undo/redo crosses the saved token with correct clean/dirty state; a new edit after Undo removes redo and makes the discarded saved token unreachable; successful save marks the current token clean; replacement clears prior history. | None for the headless command contract. |
| Budget and affected payload | `platformator_editor_history_test` | Small injected budget prunes oldest commands while retaining one oversized newest command. Editing a property in a scene containing 1 MiB of unrelated JSON retained under 1 KiB of history payload. | This is an untimed storage observation, not an allocator profile or performance benchmark. |
| App integration | `platformator_editor`, all seven editor CTest suites | Toolbar and keyboard Undo/Redo route through the same service; save/replacement resolve Scene gestures; title dirty state and visible history-limit text read from history status. Editor suites passed 7/7. | Dummy/software renderer only; no native desktop interaction or visual validation. |

History stores only property before/after values or the affected structural subtree. Property address resolution and Scene preview reconciliation use targeted addresses/changed object IDs; ordinary field edits do not serialize or copy the full scene. The history test compares retained payload size against a large unrelated field; no timing or expanded benchmark campaign was run.

### US5 project, asset, and execution checkpoint

| Coverage | Fixture or command | Observed result | Remaining limitation |
| --- | --- | --- | --- |
| Project/run settings | `workspace_lifecycle_test.cpp`, Run Settings panel | Fresh known shared-runner/Mario projects receive compatible defaults; saved declarations round-trip; arbitrary protocol strings recover to `unavailable`; custom projects remain undeclared. Build settings include configure/build preset or explicit direct build path, target, executable, profile, window, aspect, pause and debug overlays. | Roots are selected at editor launch and cannot be replaced live (T049). Native preferences paths are not user-driven in this headless check. |
| Expanded asset directories and source identity | `project_test.cpp`, `ProjectScanService` | Expanded directory scans return only immediate children; recursive source identity includes source, CMakeLists and CMakePresets inputs, changes when an input changes, excludes runtime scene assets, and publishes only within bounded task/result/file/entry limits. | Scan refresh is metadata-based and asynchronous; desktop responsiveness on very large projects was not visually reviewed. |
| Asset path assignment | `document_test.cpp`, `async_request_cases.cpp`, asset reference field | Typed path, drag payload and filtered dialog all route to the same command validation. Case, spaces, Unicode, separator normalization, root/symlink containment, missing files and expected types are checked; scene/history remains command-based. | SDL native portal dialogs and interactive drag/drop were not exercised on a desktop. |
| Configure/build/catalog/launch/Stop | `platformator_editor_process_test --live-integrations`; `process_test.cpp` fixture cases | The real shared runner and Mario each completed the exact `GamePipeline` sequence through Running, then Stop returned to Idle. Query output was schema v1; root exposed zero behaviors and Mario exposed six. Fixture cases cover malformed catalog failure, missing behavior rejection, build failure, immutable scene/root args, protocol limits, identity changes, cancellation and descendant cleanup. | Actual runs used SDL dummy video/software, with no display or gameplay interaction review. |
| Custom entry boundary | `test_custom_entry_boundary_and_false_protocol_declaration` | An undeclared custom-main fixture remained source-identical through Build-only, with no catalog query or launch. A falsely declared target emitted malformed catalog output and failed before launch. | The fixture models protocol behavior; it does not represent every custom CMake layout. |
| Save protection and separate game window | App run workflow plus workspace/process fixtures | Build & Run validates and saves the current document before queuing a fresh source scan and immutable pipeline snapshot. Failed/cancelled saves do not start the pipeline; active operations lock document mutations and Stop on close terminates the owned process. Launch passes separate explicit `--scene` and `--asset-root`; game uses its own window. | UI modal/save-dialog behavior and exit flow were not exercised with a human-driven desktop session. Embedded play and hot reload remain out of scope. |

The exact live configure/build commands, roots/settings, catalog observations, headless renderer constraint, test commands and results are in `evidence/RESULTS.md`. No game source was modified by catalog integration, and no timing campaign or generated frame artifact was retained.
