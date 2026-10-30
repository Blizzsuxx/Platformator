# Editor implementation baseline

## Repository state

- Starting revision: `c6e3dee36a7116c89d1eadba966cf8bdeba4ea60`.
- Starting worktree: only `specs/011-add-imgui-editor/` was untracked; existing source was otherwise clean. Preserve the feature specification and this pre-existing worktree state.
- No native `editor/` implementation or `vendored/imgui-1.92.9b/` checkout exists at baseline.
- Baseline collection did not configure/build a target or run a benchmark campaign.

## Reusable parity references

The legacy entry point is `ui/src/platformator_ui/main_window.py`. Relevant legacy modules and tests are:

- Scene lifecycle/round-trip: `ui/src/platformator_ui/scene/serializer.py`, `ui/tests/test_scene_roundtrip.py`, `ui/tests/test_main_window_startup.py`.
- Mutations and hierarchy: `ui/src/platformator_ui/scene/mutations.py`, `ui/src/platformator_ui/widgets/scene_tree.py`, `ui/tests/test_scene_mutations.py`, `ui/tests/test_scene_tree.py`, `ui/tests/test_main_window_selection.py`.
- Inspector and fields: `ui/src/platformator_ui/widgets/inspector.py`, `ui/src/platformator_ui/scene/models.py`, `ui/tests/test_inspector.py`.
- Behavior discovery: `ui/src/platformator_ui/services/behavior_catalog.py`, `ui/tests/test_behavior_catalog.py`.
- Scene view: `ui/src/platformator_ui/widgets/scene_canvas.py`, `ui/tests/test_scene_canvas.py`.
- History, assets and execution: `ui/src/platformator_ui/services/undo_stack.py`, `ui/src/platformator_ui/services/asset_paths.py`, `ui/src/platformator_ui/engine/run_controller.py`, `ui/tests/test_asset_path_rules.py`, `ui/tests/test_engine_runner.py`, `ui/tests/test_run_settings.py`.
- Engine references: `tests/scene_io_contract_test.cpp`, `tests/scene_script_roundtrip_test.cpp`, `tests/object_reference_contract_test.cpp`, `tests/asset_root_contract_test.cpp`, `tests/transform_hierarchy_test.cpp`, `tests/runtime_step_test.cpp`, `tests/runtime_recreation_test.cpp`, and the render culling/geometry/contract tests registered in the root `CMakeLists.txt`.

The current engine serializers in `include/platformator/` and `src/` are authoritative for runtime constraints and serialized forms; legacy behavior is a workflow reference, not authority where it conflicts with those contracts.

## Legacy environment

Python 3.14.7 is present. PySide6, PySide2 and pytest are not importable in the current environment, so the Qt desktop editor and its UI tests are unavailable here unless the environment changes. This is an environment observation, not a test result.

## Focused workload selection

Reserve one small matched baseline/candidate subset for the later pilot:

1. Selection and a single property edit at tiny, 1,000-object and 10,000-object scene sizes.
2. One deep-parent transform/selection case.
3. One visible-output rendering control with a mostly-offscreen versus all-visible comparison, only if a genuine rendering comparison is needed.
4. Bounded long-output draining as a non-timing responsiveness/resource observation.

Use legacy-equivalent interactions only where the current Python editor exposes the same operation and report any timing-boundary difference. Since the legacy GUI environment is unavailable at baseline, do not claim a legacy measurement; retain this selection for reassessment before capture. Any later rendering capture must use `benchmark/capture_linear_render.py` with unchanged pilot defaults. The focused pilot must remain within 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB and 600 seconds, with compact reports and no retained full-frame images or per-frame identity lists.

## Frozen editor pilot

The implementation pilot uses one immutable workload inventory and one journal path: `specs/011-add-imgui-editor/evidence/editor-pilot.session.json`. The driver's sibling `editor-pilot.session.capture/` contains only compact raw JSON output and bounded diagnostics. The resource and timing invocations share the journal's original monotonic/wall-clock deadline, 50 MiB allowance, source/build/harness/workload identity, and output accounting. A failed or exhausted phase cannot be resumed or replaced automatically.

The timed model interaction is selection plus one `position.x` property edit against four scene shapes: 10 objects, 1,000 objects, 10,000 objects, and a 128-object deep-parent chain. The sample interval contains selection and the edit command; Undo is run immediately afterward and outside the sample to keep document/history state bounded. There are 3 repetitions, 5 warmup interactions and 30 measured interactions per shape. This compares the same editor-model operation at each population; it does not claim legacy UI timing or measure mouse/drag latency.

Each of the 20 untimed resource cycles observes the same four shapes, serialized scene bytes, Sprite texture-reference count, retained property-history bytes, one bounded 32-file source scan, one owned child that emits 1,200,000 bytes through `ProcessService` into the 1 MiB `BoundedConsole`, process/scan completion, resident memory, and allocation calls/bytes. These are probe observations, not texture-GPU residency measurements or allocator peak proofs. The generated long output is consumed in memory and not retained as a file.

The probe does not render. If a genuine visible-output comparison is available, it is an optional `benchmark/capture_linear_render.py` compact pilot invoked as a monitored subprocess by `capture_editor.py`. It retains that tool's default pilot parameters; the outer editor session supplies only a smaller remaining time/byte ceiling so the combined capture cannot exceed its original budget. The renderer mapping reported by the render proof is part of that comparison's identity. No renderer/backend result is inferred from SDL's dummy/software startup. Legacy Qt/PySide is unavailable in this host, so no legacy interaction samples are claimed.

Before T088, record the built probe's exact Release/compiler identity, source and harness SHA-256 values, probe binary SHA-256, workload SHA-256, host/CPU identity, and the selected SDL renderer identity if a render pilot is used. Build products stay under `bin/editor-release/`. The model workload has no graphics backend (`not-applicable`); any render control must record one concrete SDL backend and matching source/build identities in its own report. No report directory contains a build tree or executable.

### Pre-capture identity freeze

These identities were resolved from the current source and one Release probe build before any resource/timing invocation. The Python session journal rechecks them when each phase starts. Any relevant source, build, harness or workload change requires a new correctness review and invalidates this freeze; do not replace a failed session automatically.

| Identity | Frozen value |
| --- | --- |
| Source SHA-256 | `dae89bc59e905558562426a9a2e2a236462ab0a0a3de227e5673c7a3447fd30e` |
| Harness SHA-256 | `32458500e03a6369f906cc79b7bd3698110bf62605632864dbfeeaec9c72fa96` |
| Workload SHA-256 | `93804135b792dac5d9622a6864de177a0e28104a151cda56f84b1303732952ee` |
| Probe build | `bin/editor-release/platformator_editor_probe`, Release, GNU 16.2.1, C++20 |
| Probe binary SHA-256 | `302022e95fd6b4dd03fe6353f756970cd60766165ce726b64b51bcdbbfb635c3` |
| Host | Linux x86_64, kernel `6.18.55-1-MANJARO`, AMD Ryzen 5 7535HS with Radeon Graphics, CPUs 0–11 |
| Editor model backend | Not applicable; the interaction probe has no SDL renderer |
| Rendering backend | Not frozen: no matched baseline/candidate linear-render probe and runner identities have been selected. No renderer result is implied. |
| Session journal | `specs/011-add-imgui-editor/evidence/editor-pilot.session.json` (single session; not yet created) |
| Aggregate capture limit | 600 seconds and 50 MiB total across resource and timing phases |

The legacy Python editor remains unavailable because PySide6/PySide2 are not installed. Therefore there is no legacy interaction baseline. A visible-output renderer comparison remains unmeasured unless a compatible, identity-matched pair is available; any such comparison must use the monitored default pilot path described above.
