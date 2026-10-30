# Implementation Plan: Dear ImGui Game Engine Editor

**Branch**: `branch-without-bvh` (actual Git checkout) | **Date**: 2026-10-30 | **Spec**: [spec.md](spec.md)

**Feature directory**: `specs/011-add-imgui-editor`. The setup script reports the feature-derived identifier `011-add-imgui-editor`; no Git branch was created or switched. `.specify/feature.json` selects this directory.

## Summary

Build an optional native desktop editor with the existing Python editor's authoring coverage and a cohesive Unity-inspired workspace. Bundle Dear ImGui `v1.92.9b-docking`, use its SDL3/SDL_Renderer backends, and keep it out of engine link/export dependencies. Retain the Python editor.

The authoritative scene is a lossless JSON document with stable ID lookup and targeted reversible edits. One editor-owned Runtime holds only renderable proxy objects; an internal render-only bridge shares engine sprite submission without loading authored scripts, physics, or audio components. The editor owns event pumping and presentation. Build & Run launches the selected game in a separate process. Additive script metadata and an early runner catalog command describe that executable's available behaviors without constructing them.

This plan completes design only. Proposed sources, build targets, CLI options, and tests below do not exist yet; [quickstart.md](quickstart.md) describes validation after implementation.

## Technical Context

**Language/Version**: C++20; existing CMake 3.21+ preset workflow and Ninja. Python remains optional legacy tooling and existing development-test tooling.

**Primary Dependencies**: Bundled Dear ImGui `v1.92.9b-docking`; existing SDL 3.4.18, SDL_image/mixer/ttf, Eigen, oneTBB, and nlohmann JSON. SDL supplies asynchronous file dialogs; no Qt, Python bindings, new graphics backend, or extra process/thread framework.

**Storage**: Existing top-level-array `.scene` JSON, preserved unknown fields; atomic replacement on save. Workspace/project settings and ImGui layout under SDL preference directory, keyed by canonical project path; versioned catalog cache keyed by selected executable/build identity. No database or network service.

**Testing**: Six native CTest document/history/catalog/process/preview/workspace suites, plus the Python standard-library budget/preflight suite registered as `platformator_editor_capture_budget_test`; existing runtime/reference/scene/render/package tests; optional legacy pytest parity checks. The budget suite must pass before resource or timing capture. GUI style/layout, desktop dialogs, DPI, and focus need explicit real-desktop checks beyond dummy-driver tests.

**Target Platform**: Linux first. POSIX process groups/nonblocking pipes; detect the working-directory spawn API at configure time. Other desktops retain runtime support, but the new editor is unsupported there until its process implementation is supplied.

**Project Type**: Optional desktop authoring application beside a reusable engine library.

**Performance Goals**: Remove recurring whole-document snapshot/serialization from drawing and individual property edits; stable warmed storage should not grow on repeated selection/run cycles. Measure comparable small/large interactions before making speedup claims; no invented frame-rate or speedup threshold.

**Constraints**: One Runtime per process; SDL textures belong to its renderer; owner-thread UI/resource/mutation; no simulation/startup/autoplay during preview; unknown scene content survives editing. Editor defaults OFF and never enters SDK exports. Benchmark budget defaults remain 3 repetitions, 5 warmups, 30 measured frames, 50 MiB and 600 seconds with stop-on-failure; no expanded campaigns.

**Scale/Scope**: One open scene and single selection; nine parity groups in [parity.md](parity.md). Exercise a tiny fixture, 1,000-object hierarchy, and 10,000-object sparse sprite/asset cases in a focused screening set, not a Cartesian product. Include deep parenting, large visible output, structural churn, and 20 open/edit/run/stop cycles in correctness/resource checks.

## Constitution Check

The initial gate was assessed from the feature and existing interfaces before research; the final column rechecks the resolved design. No principle exception is requested.

| Principle | Initial gate | Post-design assessment |
| --- | --- | --- |
| I. Performance/scalability | PASS: identify editor hot paths and current snapshot costs | PASS: ID lookup, affected-property/subtree transactions, cached visible hierarchy rows, changed-only preview reconciliation; bounded output and asset scans; measured complete costs and initialization separately |
| II. oneTBB/multicore | PASS: assess UI affinity versus independent work | PASS: SDL/ImGui/document/preview operations sequential on owner thread; nonblocking process polling avoids worker waits; oneTBB task group for directory/source-identity scans publishes immutable results. No competing worker pool; no worker touches JSON/SDL/ImGui |
| III. Collision architecture | PASS: no broad-phase redesign required | PASS: preview has no colliders/bodies and never simulates. Preserve collision implementation/tests; the paper is not used to justify unrelated changes |
| IV. KISS | PASS: prefer existing backend, JSON, build workflow | PASS: each cache has an owner/generation; no second scene serializer from runtime objects, generic editor framework, plugin host, new renderer backend, or snapshot per keystroke. Extra metadata is limited to serialized fields |
| V. Unity-inspired UX | PASS: explicit workspace/interaction criteria | PASS: official panel-role reference; neutral dark palette, docked layout, component Inspector, focused inputs. External Run and existing frame shortcuts are documented deviations; duplication preserves legacy numeric references |
| VI. Evidence | PASS: baseline and bounded validation required | PASS: meaningful new contracts plus existing regressions; comparable legacy/current workloads and raw samples, recorded limitations, compact artifacts only. No result is claimed during planning |
| VII. Correctness/lifetimes | PASS: existing runtime contracts authoritative | PASS: lossless document, atomic save, ID-based commands, proxy-only runtime, target-state restoration, callback generations, owned process groups, stale catalog states, worker completion before teardown |

### Ownership, complexity, and scheduling boundaries

- `EditorApp` owns one Runtime, ImGui context, document/session, preview bridge, process service, and scan task group. Shut down child operations and asynchronous requests; invalidate callbacks and join owned tasks; release preview textures/asset references and ImGui backends while SDL is alive; then destroy Runtime.
- Property commands copy only the changed value. Structural commands retain the affected subtree/position. ID/location maps and flat row/order indexes rebuild only on structural changes; that O(N) work is deliberate and separately measured. Frame drawing does not serialize/copy the whole document. Numeric/drag updates reconcile affected render proxies; ancestor changes cost O(affected subtree).
- Preview submission is initially linear O(renderable proxies), with no new spatial index. Reuse geometry/submission and bypass camera-specific culling caches for a nondefault target. This bounds implementation scope and preserves correctness; measure all-visible/offscreen costs before considering another index. Add a local `EFFICIENCY:` explanation at that loop with its cost, reason, and measured-work-triggered follow-up.
- Render to one reusable target sized to actual Scene pixel extent; recreate only on size changes. Clear/repopulate document-owned caches on document/root generation changes. Cache missing-asset results until explicit refresh or root/file-generation change; do not retry failed decoding every frame.
- GUI, serialization, history, texture loading and renderer calls remain sequential because of dependencies/thread affinity. OneTBB scans only filesystem/project identities and publishes immutable batches checked against request generation. Cancel/join on context destruction. Narrow tasks and visible-directory demand avoid enqueueing a task per file or stealing excessive game resources.
- Child process stdout/stderr are polled with byte/line work limits per frame. Retain a 1 MiB Console ring and a 4 MiB maximum catalog payload, each with explicit truncation/error handling. These are resource bounds, not measured performance claims. Process groups allow cancellation of configure/build children without blocking the event loop.
- Reuse existing one-/multiworker runtime tests for the shared-render-helper change and catalog-free normal execution. Preview itself has no parallel physics work. Do not call a worker-count change an editor speedup; runtime changes require their own matching comparisons.
- Automatic catalog/Run support is declared per target. Stock `main`, `mario_example`, and other shared-runner targets need a rebuild, not source edits. Custom entries without declared compatible catalog support remain editable/buildable but are not probed or automatically launched; opting into the protocol is outside the no-source-rewrite guarantee. Cover both supported and unsupported entries in catalog/process tests.
- Resource observations and timings use one guarded capture session: `capture_editor.py --mode resources --cycles 20 --session PATH`, followed only on success by `--mode timing --session PATH`. Persist cumulative wall-clock/byte use, immutable workload/build identities and phase status; the same 600-second/50-MiB budget covers both phases and rendering subprocesses. A failed/exhausted session cannot resume or be replaced automatically.
- A source, dependency, configuration, or harness change after validation invalidates affected evidence. Reopen matching story checks and T085–T088 as applicable, rerun relevant correctness checks on the rebuilt final revision, and invalidate superseded timing conclusions. T089 may run again only with explicit authorization; until required final-revision evidence exists, leave the affected acceptance criteria open. Documentation-only edits do not trigger unrelated retesting.

## Project Structure

### Documentation (this feature)

```text
specs/011-add-imgui-editor/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── parity.md
├── quickstart.md
├── checklists/requirements.md
└── contracts/
    ├── editor.md
    ├── scene-preview.md
    └── behavior-catalog.md
```

`tasks.md` is intentionally left to `$speckit-tasks`.

### Source Code (planned paths, repository root)

```text
vendored/imgui-1.92.9b/        # upstream core/backends/docs/license; no build products
editor/
├── CMakeLists.txt
├── README.md
├── resources/default-layout.ini
├── src/
│   ├── main.cpp, editorapp.{h,cpp}
│   ├── document.{h,cpp}, editcommands.{h,cpp}, selection.{h,cpp}
│   ├── project.{h,cpp}, behaviorcatalog.{h,cpp}, preferences.{h,cpp}
│   ├── preview.{h,cpp}, viewport.{h,cpp}, filedialogs.{h,cpp}
│   ├── process.{h,cpp}, process_posix.cpp
│   └── panels/               # hierarchy, inspector, project, behaviors, console
└── tests/                    # document, history, catalog, process, preview, workspace
include/platformator/
├── scriptregistration.h     # additive lazy field descriptors, existing macros preserved
├── scriptmetadata.h         # editor-independent descriptor types/traits
└── windowsettings.h         # opt-in highPixelDensity=false
src/
├── camera.cpp               # delegate to shared view-based sprite draw helper
├── spriterendergeometry.h   # shared draw/projection support; no ImGui dependency
├── gamemanager.h            # narrow internal preview-access friendship
├── behaviorfactoryregistry.{h,cpp}, scriptregistration.cpp
├── runner.cpp               # early --editor-catalog before Runtime creation
└── sdlwindow.cpp            # opt-in high-density creation; default rendering preserved
CMakeLists.txt, CMakePresets.json, docs/CPP_API_MIGRATION.md
```

**Structure Decision**: Keep editor source outside the recursive `src/` engine glob. `PLATFORMATOR_BUILD_EDITOR=OFF` by default. Build `platformator_imgui` and `platformator_editor_core` as private, nonexported targets and `platformator_editor` as the executable. Compile an internal preview-access adapter in the editor, with engine private includes; the SDK exposes neither ImGui nor editor headers. Add `editor-debug`, `editor-release`, and `editor-asan` presets under `bin/`; use an explicit editor target and focused tests. Vendor provenance records tag, resolved full revision and source/license hashes at acquisition; builds require no network.

## Phase 0: Research decisions

[research.md](research.md) resolves dependency/backend, preview/lifetimes, lossless data/undo, compiled behavior metadata, process/dialogs, UI/scaling, and measurement design. Research agents inspected the existing engine/editor and official upstream sources. No unresolved design clarification remains.

## Phase 1: Design and validation artifacts

- [data-model.md](data-model.md): authoritative data, validation, indexes, commands, resources, and state transitions.
- [contracts/editor.md](contracts/editor.md): build/CLI, workspace, document lifecycle, input, history, preferences, and process semantics.
- [contracts/scene-preview.md](contracts/scene-preview.md): render-only bridge, pixel mapping, proxies, ordering, state restoration, teardown.
- [contracts/behavior-catalog.md](contracts/behavior-catalog.md): lazy type metadata and executable query compatibility.
- [parity.md](parity.md): source/test-backed workflow and field coverage, including intentional departures.
- [quickstart.md](quickstart.md): post-implementation build, automated/manual validation, and bounded measurement guide.

## Delivery sequence for task generation

1. Dependency pin, optional targets/presets, editor shell and ownership; keep runtime/package checks passing.
2. Lossless document/indexes, validation, atomic save, selection and command history with headless tests.
3. Lazy script metadata, early runner export and catalog states, independently tested before UI consumption.
4. Render-only preview adapter and shared draw helper, with no-simulation, renderer-state and equivalence tests.
5. Complete panels/field editors, references, gestures, input routing, native dialogs, layout/DPI/preferences.
6. Asynchronous build/catalog/run pipeline, project settings, cancellation and recovery.
7. Cross-editor/runtime fixtures, desktop UX review, focused resource/timing pilot, documentation and migration checks.

Each slice has independently testable contracts. A shell/demo is not feature completion; all parity rows and SC-001–008 remain required.

## Complexity Tracking

No constitution violations need an exception. The raw document preserves information a live-runtime serializer cannot retain; proxy rendering protects edit-mode lifetimes; lazy metadata distinguishes compiled behavior types without linking game code into the editor. Each is a bounded responsibility with specific tests, not a general plugin/reflection architecture. Linear preview submission and O(N) structural index rebuilds are explicit costs subject to measurement.
