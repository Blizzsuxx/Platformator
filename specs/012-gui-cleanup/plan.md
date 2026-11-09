# Implementation Plan: GUI Cleanup

**Branch**: `branch-without-bvh` (actual Git checkout) | **Date**: 2026-11-05 | **Spec**: [spec.md](spec.md)

**Feature directory**: `specs/012-gui-cleanup`. `setup-plan.sh` reports feature identifier `012-gui-cleanup`; it did not create/switch the Git branch. `.specify/feature.json` selects this feature.

## Summary

Complete all audit findings through ten independently testable user stories: accurate all-object Scene editing, reliable targeted external-change refresh, diagnostics/history, bounded responsive GUI work, search/group organization, configurable accessible workspace, three desktop platforms, reusable object/asset authoring, scene tabs, and separate-process Game viewing/control/inspection.

Preserve the lossless authoring document and existing runtime semantics. Refactor the 1,995-line application loop into explicit application/project/document/window ownership before adding cross-cutting features. Use one project-scoped resource/change service, one active authoring Runtime, stable document tabs, and atomic targeted command batches. Add native notification/file/process/accessibility adapters and explicit SDL host windows. Compatible game sessions remain separate processes with bounded optional presentation/control channels. No new game-object framework, general reflection system, arbitrary code hot reload, or prefab propagation system is introduced.

This command produces design artifacts only. Proposed sources, flags, test targets, protocol modes, and capture commands below require implementation; no new application behavior or measured improvement is claimed here.

## Technical Context

**Language/Version**: C++20 for engine/editor; Objective-C++ for macOS platform providers; existing Python 3 standard-library tooling for compact capture/validation. Root CMake currently declares 3.16; supported build lanes use CMake 3.28+ with Ninja, preserving runtime-only consumer compatibility unless a concrete dependency requires a documented change.

**Primary Dependencies**: Pinned SDL 3.4.18, SDL_image 3.4.8, SDL_mixer/ttf, Dear ImGui 1.92.9b, Eigen 5.0.1, oneTBB 2022.3.0, nlohmann JSON. Native inotify/Win32/FSEvents; UI Automation/NSAccessibility/AT-SPI accessibility (Linux editor-only libdbus-1). Reuse vendored image metadata/decoder code with checked allocation limits; no new graphics API or CPU worker pool. Changes remain outside engine exports except additive opt-in runner/session integration and narrow internal resource/render access.

**Storage**: Existing lossless `.scene` arrays and `.animset` objects; editor-only versioned `.pttemplate` envelopes; version-2 workspace preferences with migration from version 1; per-tab saved identities and targeted history. Same-directory staged atomic writes with optimistic external-conflict checks. No database or network service.

**Testing**: Existing seven native editor suites + capture-budget suite and affected engine tests; new focused document-batch/tab, watcher/recovery, resource-budget, window-host/accessibility, template/animation, native platform, and play-session contracts. Deterministic injected notifications plus small real filesystem cases; dummy/software checks plus real desktop checks. Add a GUI probe that runs the actual application frame path; old selection-only timings remain labeled as such.

**Target Platform**: Planned validated lanes: Ubuntu 24.04 x86_64/GCC 13+ with X11 and Wayland, Windows 11 x64/MSVC 2022 v143, macOS 14 arm64/Xcode 15+/AppleClang 15+. These are acceptance lanes, not measured support claims or dependency-wide minimums. macOS arm64 oneTBB support is community-provided; actual project verification is mandatory.

**Project Type**: Optional native desktop authoring tool beside a reusable game runtime/library. Editor defaults OFF; install/exported engine consumers do not depend on ImGui, AT-SPI, or editor headers.

**Performance Goals**: Settled local asset/validation updates within the spec's existing three-second refresh bound; no repeated static Scene submission; zero unrelated reloads for an unused asset addition; bounded cache/queue/decode memory; lower comparable idle CPU and no interaction regressions beyond measured variability. Use 10,000-object and depth-128 correctness fixtures without automatically expanding the benchmark matrix.

**Constraints**: One Runtime per process; SDL/ImGui/Runtime/document mutations on the owner thread; each SDL texture stays with its creating renderer. Authoring does not run scripts/physics/autoplay. Unknown data, typed references, cancellation, fixed-step behavior and saved-state semantics survive. Benchmark defaults remain 3 repetitions, 5 warmups, 30 measured frames, 50 MiB and 600 seconds, stop on failure/budget; no retained frame dumps or copied builds.

**Scale/Scope**: All FR-001–052 and SC-001–017. Multiple independent scene/asset tabs; ordered multi-selection; up to eight native editor hosts as an initial resource guard; one active authoring preview and one owned play session. Full scope includes P3 stories, sequenced later rather than deferred.

## Constitution Check

Initial gates were assessed against the specification and current sources before research; resolved decisions were rechecked after Phase 1 design. No principle exception is requested.

| Principle | Initial gate | Post-design gate |
| --- | --- | --- |
| I. Performance/scalability | PASS: recurring full-tree work, blocking decode, recursive copies and unbounded caches identified | PASS: category/path invalidation, static image reuse, local descriptors, visible-row work, bounded queues/decodes/replicas; explicit O(N) structure/dirty-view costs and measured culling alternative |
| II. Multicore/oneTBB | PASS: identify affinity and independent preparation work | PASS: oneTBB for bounded scan/decode/immutable validation preparation; serial owner-thread UI/SDL/runtime commits; native blocking I/O integration only, not a competing compute pool; one-/multiworker throughput comparisons when relevant |
| III. Collision foundations | PASS: no broad-phase/solver redesign | PASS: preserve physics implementation; Step uses the existing fixed-step path. Any later collision redesign requires its own paper/invariant review and scope justification |
| IV. KISS/ownership | PASS: preserve existing lossless documents and command service | PASS: six explicit owners, typed batches and small resource indexes; native adapters satisfy required platform/accessibility behavior; no generic plug-in host, new graphics stack, or live runtime document mirror |
| V. Unity-inspired UX | PASS: audit/spec state familiar workflows and intentional boundaries | PASS: tool modes, search, group editing, component operations and diagnostics; explicit hosts instead of unsupported native tab dragging; translation-only hierarchy and existing shortcut/reference defaults documented |
| VI. Evidence | PASS: current 6/8 audit result is incomplete evidence | PASS: independent oracles, actual GUI-loop harness, compact controlled captures, platform evidence ledger; no unsupported speedup or passing-platform claims |
| VII. Correctness/lifetimes | PASS: unknown data, resource/async generations and owned processes mandatory | PASS: tab/project/session/renderer identities, atomic command rollback, safe phase reload, native owned-tree cancellation with explicit foreground POSIX membership/registered service boundary, staged writes, bounded malformed-input handling, teardown barriers and failure tests |

### Owners, ordering, and complexity

- `EditorApplication` owns SDL event scheduling, main authoring Runtime, native hosts, and one `ProjectSession`. `ProjectSession` owns change/resource/source/catalog/process services and `DocumentManager`. Persistent HostLayoutId and a separate docking blob survive restart; native host/renderer generations do not. Each heap-stable tab owns its document/history/selection/view; window hosts borrow stable IDs and never own scene truth. `PlaySession` owns child process/channels/mapping and is bound to an immutable launch-tab snapshot.
- Main-frame order: service native events and completions → reject stale identities → apply bounded change/resource batches → commit document actions and latest validation → reconcile affected authoring metadata/proxies → draw dirty visible hosts/Scene/Game images → present → release frame pins → wait until input or the earliest animation/process/recovery deadline. Never sleep inside a callback or busy-loop with a 1 ms delay. Process output and native callbacks wake the event loop.
- Dialog results and property targets carry `{projectGeneration, tabId, documentGeneration}`; resource results add directory request/revision or path/uncertainty version and host/renderer generation; play events add session/runtime serial. Changing the active tab alone does not invalidate another tab's dialogs, but closing/replacing it does. Catalog results use project/source/binary identity, not selection state.
- Ordinary properties retain only before/after field values; group transactions cost O(changed targets). Reparent costs O(moved subtree + structure-index repair), preserves engine world translation, and prevents cycles. Structure rebuilds remain O(N) on structural changes; rename/pose/reference changes do not rebuild topology. One property in a parent may require a descendant pose pass; a label/behavior change does not.
- Source/asset preparation uses existing oneTBB scheduling with one active image decode and bounded scan slots. Native I/O waits run outside compute tasks because indefinite waits would starve the scheduler. Commit sorted stable batches on the owner thread so completion order does not change document/render ordering. No worker receives mutable DOM pointers, SDL textures, ImGui state, or live Runtime objects.
- Frame pins protect submitted textures; stage safe resource replacements before swapping all dependent bindings. The main Runtime and other renderers never exchange SDL_Texture pointers. CPU pixels/semantic snapshots cross boundaries as immutable bounded values. Authoring proxy textures 256 MiB, main Scene target 64 MiB, thumbnail 64 MiB, inclusive decode working set 128 MiB (≤64 MiB decoded + ≤64 MiB scratch/input staging), Scene transfer 64 MiB, and Game transfer 1.76 MiB caps are separate accounting domains.
- Teardown: mark generation obsolete → stop owned play/pipeline and invalidate dialogs → stop/join native I/O and cancel/join bounded preparation → release tab/proxy references, semantic providers and per-host UI resources → shut down each ImGui backend/context → destroy secondary renderers/windows → destroy main preview/Runtime last. No callbacks retain raw pointers past their owner's shutdown.
- Renderer dirty-view submission initially uses a linear conservative bounds pass ordered by document order. An added spatial index is contingent on demonstrated dirty-view cost and correctness checks; the retained loop gets the constitution's local `EFFICIENCY:` rationale/cost/follow-up. Static frames do not run this pass. Record CPU/GPU readback costs for detached Scene/Game views separately.

## Project Structure

### Documentation (this feature)

```text
specs/012-gui-cleanup/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── checklists/requirements.md
└── contracts/
    ├── editor-workspace.md
    ├── asset-changes.md
    ├── rendering-resources.md
    ├── desktop.md
    ├── play-session.md
    └── reusable-assets.md
```

`tasks.md` belongs to `$speckit-tasks` and is intentionally not generated by this command.

### Source Code (existing files extended; new paths are proposed)

```text
editor/src/
├── editorapp.{h,cpp}, main.cpp          # composition + actual tick entry point
├── projectsession.{h,cpp}              # roots, services, teardown
├── documentmanager.{h,cpp}, assetdocument.{h,cpp}
├── document.*, workspace.*, selection.*, editcommands.*
├── authoringindex.*, preview.*, viewport.*
├── project.*, projectchanges.*, resourceindex.*, thumbnailcache.*
├── imagepreflight.*                    # checked vendored-codec bridge
├── diagnostics.*, actions.*, preferences.*, toolbar.*, style.*
├── windowhost.*, uitextures.*, accessibility.*
├── process.*, documentio.*, playsession.*
├── templates.*, animationeditor.*
├── platform/                           # watch, file, process, IPC, accessibility adapters
├── backends/                           # narrow attributed SDL3 multi-context adaptation
└── panels/                             # existing panels + Game/asset editor/tab UI
editor/resources/                       # embedded default layout, licensed resource metadata
editor/tests/                           # existing regression suites + focused contract suites
editor/benchmark/gui_probe.cpp          # actual EditorApplication frame path
include/platformator/                   # additive opt-in runner protocol declarations if public
src/runner.cpp, runtime.cpp, gamemanager.*, sdlwindow.*
src/editorsession.*, src/editorprotocol.* # no ImGui dependency, disabled in normal runtime builds
src/animationclipserializer.cpp          # shared duration interpretation/compatibility validation
benchmark/capture_gui.py                 # thin CaptureBudget orchestration, no new guard system
CMakeLists.txt, CMakePresets.json, editor/CMakeLists.txt, cmake/
```

**Structure Decision**: Keep UI/private adapters under `editor/`, native engine session functionality opt-in and GUI-independent, and shared serialization/geometry authoritative in existing engine code. CMake chooses native sources rather than compiling POSIX files on Windows. Do not migrate vendored source wholesale; narrowly adapted backend/codec files retain attribution and upstream provenance. Build artifacts stay under `bin/` or `build/`.

## Phase 0: Research decisions

[research.md](research.md) resolves ownership, notifications and recovery, bounded decode/reload, host rendering, accessibility, platform seams, template/animation formats, separate-process play, and actual-GUI evidence. Four research agents supplied source-backed decisions and alternatives. No design clarification remains. Starting safety caps are explicit and can change only through recorded evidence and maintained resource bounds; performance capture guards cannot be raised without user authorization.

## Phase 1: Design artifacts

- [data-model.md](data-model.md): stable identities, document/command models, change/resource state, native hosts, semantic nodes, templates/assets, and play-session states.
- [editor-workspace.md](contracts/editor-workspace.md): Scene mapping, input, group/history, diagnostics, document tabs and authoring behavior.
- [asset-changes.md](contracts/asset-changes.md): event batches, initialization/recovery, symlinks, fallback and visible coverage.
- [rendering-resources.md](contracts/rendering-resources.md): render keys, decode/caches, owner-thread reload and multi-renderer transfers.
- [desktop.md](contracts/desktop.md): process/file semantics, installation, native hosts/accessibility and support lanes.
- [play-session.md](contracts/play-session.md): capability negotiation, inherited channels, bounded frame transport, pause/step and read-only inspection.
- [reusable-assets.md](contracts/reusable-assets.md): component reset/copy, template/reference policy and existing sprite/animation formats.
- [quickstart.md](quickstart.md): current baseline commands, proposed post-implementation checks, real desktop walkthroughs and guarded focused captures.

## Delivery sequence and requirement coverage

These are implementation workstreams for task generation, not generated tasks. Each must yield independently reviewable behavior with tests and documentation. P3 scope remains required.

| Order | Deliverable | Required coverage | Depends on |
| --- | --- | --- | --- |
| 1 | Preserve baseline identities; fix hierarchy/full-panel regression contracts; extract app tick and stable project/tab ownership | FR-002, FR-006, FR-044, FR-049, FR-051–052 | Existing editor; no behavior-changing performance baseline substitution |
| 2 | All-object metadata, tool modes/snapping/zoom/rectangle sizing, numeric drafts/batch history and correct selection | FR-001–006 | 1 |
| 3 | Native change service, source revision, resource dependency index, initial/recovery handshake and symlink policy | FR-007–014, FR-050 | 1; native interfaces developed with 6 |
| 4 | Bounded CPU thumbnail preparation and versioned reload, static rendering/idle pacing, visible-row caches, deferred preference flush and validity cache | FR-009, FR-018–024, FR-051 | 2–3 |
| 5 | Current validation and selectable diagnostic Console; search, hierarchy moves/multi-edit, asset assignment/repair and reference policy | FR-015–017, FR-025–030, FR-043 | 1–4 |
| 6 | Windows/macOS file/process/notification/IPC seams, installed resources/dependencies and platform lanes | FR-037–040 | 1; can proceed alongside 2–5 |
| 7 | Guided setup, labels/shortcuts/UI scale, native host windows/backend ownership, semantic accessibility providers | FR-031–036 | 1, 4–6 |
| 8 | Component copy/reset, versioned subtree templates, source-region and animation asset documents | FR-041–043, FR-048 | 1–5 |
| 9 | Catalog capabilities, opt-in child session, Game image, pause/step, handle-safe read-only inspection and explicit restart flow | FR-044–047 | 1, 4, 6–7 |
| 10 | Full requirement/desktop evidence, focused performance comparison, package/runtime-only checks and updated parity/support documentation | FR-049–052; SC-001–017 | Every affected workstream |

Development run configuration enables a proposed `PLATFORMATOR_ENABLE_EDITOR_SESSION` option only for compatible runners; normal SDK/runtime builds keep it OFF. The session option supplies remote pause/step independently of debug hotkeys, including Release sessions; embedded v1 remains viewing/control/inspection with input_forwarding=false. Stock shared-runner targets gain capabilities after rebuilding, without game-source edits. A custom runner lacking session support keeps external Run; POSIX owned toolchains/runners must remain foreground process-group members or provide explicit detached-service ownership integration; source inference never enables capabilities.

### Validation and evidence gate

Use independent document ordering/coordinate/pixel/reference oracles and direct counters for zero unrelated work. Both previously failed suites must reach completion. Real desktop tests cover each support lane, IME/focus across hosts, resource loss, monitor removal, assistive inspection and clean relocated installs. Runtime-only build/install/consumer tests prove editor dependencies have not leaked.

Actual GUI performance needs a behavior-equivalent baseline harness on a pinned baseline revision/build and the candidate. Separate harness integration from performance changes; do not call an altered workload or a known-failing rendering path a matched baseline. Record known baseline defects and choose unaffected comparable measurement cases. Focused GUI captures share a single 50 MiB/600-second session budget with any rendering subprocess; `capture_linear_render.py` handles rendering comparisons. No automatic 011 resource/timing sequence or historical full matrix. Rebuild/change invalidates affected evidence; investigate a failure before any further capture.

Platform access or timing evidence unavailable during implementation remains an unmet acceptance item, never a passed gate. Planning gates pass because ownership/decisions and verification obligations are concrete; they do not certify delivered behavior.

## Complexity Tracking

No constitution violation is accepted. Required complexity has a narrow purpose: a change index avoids unrelated rescans, per-tab identity prevents cross-document actions, batch commands provide atomic group undo, per-renderer replicas permit SDL window isolation, native semantic providers satisfy accessibility, and inherited session channels preserve the single-Runtime boundary. The native I/O dispatcher exists solely for OS waits and never competes with oneTBB CPU work. Larger graphics migration, generic plug-in frameworks, arbitrary code reload, and prefab propagation are rejected alternatives outside scope.
