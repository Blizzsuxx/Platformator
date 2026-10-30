# Research: Dear ImGui Game Engine Editor

**Date**: 2026-10-30. Repository inspection and official upstream documentation; no new dependency downloaded, implementation built, or benchmark run during planning.

## 1. Bundled dependency and backend

**Decision**: Vendor Dear ImGui `v1.92.9b-docking` under `vendored/imgui-1.92.9b/`. Preserve its MIT license, upstream version and provenance, resolve the tag to a full commit and record source hashes when acquiring it. Compile core/draw/tables/widgets plus `imgui_impl_sdl3.cpp` and `imgui_impl_sdlrenderer3.cpp` into an editor-private target. Do not track upstream build outputs. Do not fetch during configure or silently follow a moving branch.

**Rationale**: The repository already bundles dependencies. The [tagged release](https://github.com/ocornut/imgui/releases/tag/v1.92.9b-docking) and [pinned example](https://raw.githubusercontent.com/ocornut/imgui/v1.92.9b-docking/examples/example_sdl3_sdlrenderer3/main.cpp) provide an SDL3 integration. The [pinned renderer header](https://raw.githubusercontent.com/ocornut/imgui/v1.92.9b-docking/backends/imgui_impl_sdlrenderer3.h) accepts SDL textures and supports dynamic fonts, but does not support multiple OS viewports. In-window docking satisfies the specification.

**Alternatives considered**: Qt retention alone does not meet the requested GUI; Python bindings retain a language bridge; OpenGL/Vulkan or SDL_GPU migration adds an unrelated renderer rewrite. A moving submodule or configure-time fetch weakens offline/reproducible setup. No claim that the selected version is the latest or measured fastest is needed.

## 2. Engine preview without executing the document

**Decision**: Keep one editor-owned Runtime. Parse authoring JSON in the editor and materialize only root `GameObject` + `Sprite` proxies with document-computed world translations, local scale/rotation, and explicit authoring draw ranks. Resolve current animator frame as static preview data when applicable; do not create Animator, Script, Audio, Rigidbody, or Collider components. Camera/collider/grid handles are editor overlays. No `Runtime::loadScene`, `run`, or `simulateFrame` call is permitted in editing.

**Rationale**: [runtime.h](../../include/platformator/runtime.h) permits only one active Runtime; its renderer owns engine textures. [scenebuilder.cpp](../../src/scenebuilder.cpp) and script serialization construct actual components/behaviors when loading scenes. [gamemanager.cpp](../../src/gamemanager.cpp) starts pending behaviors during simulation; paused frame APIs still do more than render. Unknown game behaviors must remain editable without being linked into the editor.

**Decision**: A narrow `EditorPreviewAccess` adapter compiled with editor code synchronizes proxy transforms at the owner-thread boundary, then draws an explicit ordered sprite list using a shared view-based engine helper extracted from [camera.cpp](../../src/camera.cpp). It targets a reusable SDL texture and never presents or changes the main camera. Save/restore renderer target, target-specific mapping and draw state on success/failure. Default game render continues using the same helper and existing culling instrumentation.

**Rationale**: Geometry is already centralized around `SpriteRenderView` in [spriterendergeometry.h](../../src/spriterendergeometry.h). Target rendering is excluded by `hasDefaultRenderMapping` in [sdlwindow.cpp](../../src/sdlwindow.cpp); explicitly disable index/cache culling for preview instead of poisoning game-camera keys. SDL documents [per-target viewport/clip/scale/logical state](https://wiki.libsdl.org/SDL3/SDL_SetRenderTarget). The initial proxy path remains linear; report its cost rather than adding an index without evidence.

**Alternatives considered**: A second Runtime is forbidden. Full scene load starts forbidden construction paths and cannot preserve unknown data. Drawing with a new editor renderer duplicates engine geometry. Direct writes to private published positions bypass invariants. Registering an editor camera as main mutates gameplay state. Sprite proxies reuse engine resource management and field interpretation with a small private seam.

**Limit**: Empty PhysicsManager and desktop audio services are still initialized by Runtime; no authored body/audio or gameplay callback is instantiated/run. Suppressing backend initialization is not required by the spec and is not hidden in this feature.

## 3. Lossless scene data and undo

**Decision**: Store the top-level JSON array as authoritative data, with object/component ID-to-location indexes and structural generations. Known field adapters validate engine constraints; unknown keys stay untouched. Saving serializes this document, never preview runtime objects. Use same-directory temporary write, successful flush/close, and atomic replacement; only successful replacement advances the saved marker.

**Rationale**: [legacy models](../../ui/src/platformator_ui/scene/models.py) allow extra fields; [serializer.py](../../ui/src/platformator_ui/scene/serializer.py) preserves unknown components and normalizes known assets. Runtime objects cannot retain every unfamiliar serialized payload. ID-based addressing avoids retaining pointers across array mutations.

**Decision**: Targeted property commands and affected-subtree structural commands, explicit begin/update/commit/cancel gestures, monotonic history-state identities, and a saved-state token. Keep selection/panel navigation out of scene history. Rebuild derived topology/location indexes on structural changes only. History retains a 64 MiB payload budget plus the newest individual oversized command so the latest committed edit remains undoable; prune oldest completed commands, expose history truncation, and invalidate unreachable saved tokens.

**Rationale**: Current [snapshot history](../../ui/src/platformator_ui/services/undo_stack.py) and [main window](../../ui/src/platformator_ui/main_window.py) repeatedly copy/serialize whole scenes and merge by control/object key, which can merge separate gestures. An explicit gesture boundary fixes both cost and behavior.

**Alternatives considered**: Full snapshots for every change violate the feature's hot-path requirement. Runtime-first editing loses unknown data. A generic command bus/event-sourcing framework is unnecessary. Copy only a structural subtree when that is the actual changed unit.

**Compatibility decision**: Preserve legacy subtree duplication's new object/component IDs, root-name uniquification, root local offset (+32,+32), and placement after the original. Serialized reference integers remain unchanged, including references to originals inside the duplicated subtree. Do not guess which arbitrary numbers are references. Document this Unity deviation and test self/internal/external/component references explicitly. Automatic internal remapping would be a separate deliberate behavior change.

## 4. Behavior availability and typed field metadata

**Decision**: Extend existing `SERIALIZABLE_SCRIPT` macros with lazy descriptor providers generated from member identifiers/types in class scope. Add editor-independent field traits and registry enumeration. Keep two-argument factory registration compatible; factory-only registrations are available types with unavailable schema. Export a versioned catalog through the selected runner's early `--editor-catalog` path before constructing Runtime or invoking any factory.

**Rationale**: [scriptregistration.h](../../include/platformator/scriptregistration.h) already receives serialized fields and has class-member access. [registry](../../src/behaviorfactoryregistry.h) currently stores factories only. Standard and engine field types can be described with unevaluated member types without running game constructors. [runner.cpp](../../src/runner.cpp) is the common main entry used by the stock runner and Mario.

**Decision**: Query the exact successfully built, declared-compatible game executable, associate the result with project/profile/target/binary/source-input identity, and distinguish current, stale, unavailable, registered-without-schema, and unregistered scene types. A change to selected target, profile, source input, protocol declaration, or build identity invalidates current status. Refresh/Build verifies again before launch. The no-source-rewrite baseline is stock `main`, `mario_example`, and other shared argv-runner targets using existing macros: rebuilding supplies support. Custom entry points remain editable/buildable but require explicit export integration and a compatible protocol declaration before query/automatic Run; do not probe undeclared entries or rewrite their source.

**Default-value boundary**: Metadata reports member type, not C++ constructor defaults. Add Behavior writes only `{ "type": "Name" }`; absent fields show uncommitted drafts and retain engine defaults until explicitly edited. Existing `WITH_DEFAULT` deserialization supplies runtime defaults. Samples inform custom JSON display shape only; they never become authoritative schema/defaults or delete unknown fields. Factory-only custom deserializers may require explicit user-supplied fields and are validated by that game at launch. No behavior constructor runs for catalog generation.

**Alternatives considered**: Legacy source regex discovery cannot prove a type was linked, misses complex declarations, and deletes stale fields during synchronization. Linking arbitrary game scripts into the editor couples builds and permits side effects. Construct-and-serialize for defaults can invoke game code and fail without a Runtime. Broad C++ reflection/plugin loading is unnecessary.

## 5. File dialogs and child-process ownership

**Decision**: Use SDL's native open/save/folder dialogs. Copy callback-owned strings immediately into an application-owned completion queue; main-thread consumption checks request and document/project generations before applying results. Callback context outlives the request/application session through shared lifetime state; it never captures raw panel/document pointers.

**Rationale**: [SDL dialog documentation](https://wiki.libsdl.org/SDL3/SDL_ShowOpenFileDialog) confirms asynchronous return, possible non-main-thread callbacks, caller-owned filter lifetime, cancellation/error distinctions, and portal/event-pump requirements on Linux. Existing SDL is sufficient; no extra dialog library.

**Decision**: Linux `posix_spawnp`, argv vectors, explicit cwd file action where supported, new process group, merged nonblocking stdout/stderr, and `waitpid(..., WNOHANG)` in the UI loop. Detect cwd-spawn capability; a fallback resolves executable/argv/environment in the parent, then uses only async-signal-safe fork-child chdir/dup/close/execve/error-write/_exit operations, never shell execution or allocation after fork. Stop clears queued stages, sends group TERM, polls, escalates to KILL after 2 seconds, and reaps owned children before closing the session. Bound per-frame drain work and retained Console bytes.

**Rationale**: Existing [run controller](../../ui/src/platformator_ui/engine/run_controller.py) has a sequential pipeline and merged output, but killing only a direct process can leave build children. Native polling keeps input/drawing independent of child completion without another worker pool. Consult bundled/system POSIX declarations for platform capability rather than assuming a GNU extension exists everywhere.

**Alternatives considered**: `system()` and shell-concatenated paths lose argv fidelity/ownership. Blocking pipe reads or `wait()` freeze GUI. A custom process thread pool is unnecessary. Windows process support is deferred explicitly.

## 6. Workspace, input, and DPI

**Decision**: Public `DockSpaceOverViewport`, a checked-in default layout seed, stable panel IDs, and separate project preference/layout files. Avoid the internal DockBuilder API. Use compact aligned fields, collapsible components, dark neutral surfaces, restrained blue selection, labeled/tooltipped simple vector icons, and a center Scene. No new raster/font dependency is required.

**Rationale**: [official docking documentation](https://github.com/ocornut/imgui/wiki/Docking) distinguishes supported user docking from internal builder APIs. [Unity interface reference](https://docs.unity.cn/Documentation/Manual/UsingTheEditor.html) establishes panel roles; this is inspiration, not branding or full Unity parity.

**Decision**: Feed every SDL event to the platform backend. Capture flags plus focused-field/active-gesture/modal state decide whether editor commands/Scene receive input. Rebuild style from an unscaled base on scale changes (never cumulatively scale it); use the pinned font backend and effective display/user scale. Add opt-in `WindowSettings::highPixelDensity=false`; only editor opts in. Translate window coordinates to Scene framebuffer pixels using actual pixel/window ratios. Test 100%/150% style and real display scaling separately.

**Alternatives considered**: Styling demo windows is not an editor design. OS multi-viewports exceed backend capability. Hand-waving automatic DPI scaling risks blurry/misaligned picking. Keeping existing F/Shift+F framing minimizes migration cost; document the Unity shortcut deviation.

## 7. Focused verification and measurement

**Decision**: Test new semantics where failures matter: lossless roundtrip/atomic failure, history/gesture boundaries, reference/identity repair, metadata without construction/SDL, process cancellation/grandchildren, preview image/order/state/no-simulation, and preference generations. Reuse targeted engine tests and optional legacy fixtures. Desktop inspection remains necessary for style/dialog/focus/scale.

**Decision**: Plan a small sequential pilot of equivalent select/edit/drag/navigation workloads on tiny, 1,000- and 10,000-object scenes, selected deep/large-visible controls and a long-output case. Instrument work/allocations separately from timing. One-/multiworker engine correctness covers shared code; preview timings do not claim multicore scaling. Record latency distributions per process, startup versus warm costs, RSS/history/target storage, full editor frame cost, source/build/backend/workload identity, and independent raw samples.

**Rationale**: Migration does not guarantee speedup. Use rendering driver defaults for genuine render comparisons; a future editor interaction driver must enforce equivalent 600-second/50-MiB guards across the entire pilot, not per case. No source/build bundles, executable copies, full-frame images, or per-frame identity lists in normal reports. Tests can compare images in memory; specific failures alone justify bounded diagnostic files.

**Outcome**: All initial design unknowns are resolved. Binary/source hashes, host-specific timing results, and real-desktop behavior are implementation validation work, not invented research findings.

## Analysis remediation: final-revision and budget gates

**Decision**: Register the standard-library Python budget/preflight tests as `platformator_editor_capture_budget_test` in CTest and run them before probes. Guard resource and timing modes with the same persistent capture session, aggregate 600-second/50-MiB limits, identity checks and fail-closed phase transitions. Resource completion is the only allowed normal continuation into timing; failed/exhausted sessions cannot resume automatically.

**Decision**: If post-capture investigation changes code/dependencies/build configuration or harness, invalidate affected results, reopen matching story checks and final validation tasks, and rerun relevant correctness checks on rebuilt final code. Existing captures stay historical, not proof for the changed candidate. Further resource/timing capture requires explicit authorization; affected acceptance remains open meanwhile. Documentation-only changes do not require unrelated tests.

**Rationale**: These close the analysis findings without weakening the constitution or benchmark policy. A documented skip or stale measurement cannot certify the final candidate.
