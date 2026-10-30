# Feature Specification: Dear ImGui Game Engine Editor

**Feature Branch**: `branch-without-bvh` (existing checkout; feature directory is independent)

**Created**: 2026-10-30

**Status**: Draft — validated for planning

**Input**: User description: "add Dear ImGui to the project and use it for the GUI of the game engine - it should serve similar function to the existing python version. The Dear ImGui version should strive to look similar to Unity in useability, looks and style."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Open and save existing scenes (Priority: P1)

A developer launches the new editor without installing the legacy editor, opens an existing scene, and saves changes without converting project content.

**Why this priority**: A replacement must protect existing work and remain compatible with game projects.

**Independent Test**: Open and save representative existing scenes, including unfamiliar fields; compare their saved meaning. Create/reopen a starter scene and exercise cancellation and write failure.

**Acceptance Scenarios**:

1. **Given** an existing valid scene and asset root, **When** it is opened and saved without edits, **Then** object/component identities, hierarchy/order, references, supported properties, and unrecognized serialized data retain their meaning; existing tools can consume the saved scene where its contents are supported.
2. **Given** a fresh session, **When** New Scene is selected, **Then** a usable default camera is created and the scene can be saved and reopened.
3. **Given** unsaved edits, **When** New, Open, or Exit is requested, **Then** Save/Discard/Cancel choices protect the work; failed or cancelled saves do not proceed as though successful.
4. **Given** malformed content or an unwritable destination, **When** Open or Save fails, **Then** the error identifies the cause and location, and the current document and previously saved content remain intact.

---

### User Story 2 - Edit objects, components, and behaviors (Priority: P1)

A developer uses Hierarchy and Inspector to create objects, configure components and game behaviors, and assign references with the practical coverage of the current Python editor.

**Why this priority**: These authoring workflows make the GUI useful beyond viewing scenes.

**Independent Test**: With all supported component types and representative behavior fields, create a child, duplicate/delete subtrees, edit properties/references, add/remove components and behaviors, and save/reopen. Compare outcomes with established workflows.

**Acceptance Scenarios**:

1. **Given** nested objects, **When** an object or component is selected, **Then** Hierarchy, Inspector, and Scene agree on the selection; root/child creation, subtree duplication, and deletion maintain valid unique identities and selection.
2. **Given** a selected object, **When** name, tag, active state, position, rotation, scale, or supported component fields change, **Then** all views reflect the change without modifying unrelated data.
3. **Given** a selected object, **When** supported components or available behaviors are added/removed, **Then** valid changes persist on save/reopen and conflicting components are rejected with an explanation.
4. **Given** behavior fields containing scalar, nested, list, asset, and typed object/component references, **When** inspected or edited, **Then** types and identities are retained; unsupported data remains preserved and visibly distinguishable from editable fields.
5. **Given** a deleted reference target or invalid property value, **When** the field is inspected or committed, **Then** the problem is visible without silently binding another target or accepting a value rejected by the engine.

---

### User Story 3 - Navigate and manipulate a scene visually (Priority: P1)

A developer uses a central 2D Scene view to inspect scene appearance, select objects, move them, and adjust collider offsets while keeping the Inspector and Hierarchy synchronized.

**Why this priority**: Visual editing is central to both the existing GUI and the requested Unity-inspired workflow.

**Independent Test**: Use overlapping sprites, parented objects, camera bounds, and both collider types. Pan/zoom/frame, pick overlapping targets, and drag objects/collider offsets; verify saved coordinates and visible results.

**Acceptance Scenarios**:

1. **Given** transformed sprites, **When** Scene is displayed, resized, panned, or zoomed, **Then** dimensions, source rectangles, flips, rotation, hierarchy translation, and relative drawing order agree with engine rendering; editor overlays are distinct.
2. **Given** overlapping objects/components, **When** selecting in Scene, **Then** picking is deterministic, underlying selectable targets remain reachable, and Hierarchy/Inspector show the matching selection.
3. **Given** a parented object or selected collider, **When** its position or offset handle is dragged, **Then** the intended local property changes correctly at every zoom level without changing unrelated properties.
4. **Given** missing assets, inactive objects, no sprites, or no game camera, **When** editing the scene, **Then** objects remain discoverable/selectable and applicable camera/collider outlines remain usable; resource failures do not prevent unrelated edits.
5. **Given** text input or another panel owns an interaction, **When** typing or dragging there, **Then** Scene does not also move objects, pan, or invoke unrelated shortcuts.

---

### User Story 4 - Undo edits and trust saved state (Priority: P1)

A developer experiments safely, reverses changes, and reliably knows whether current work is saved.

**Why this priority**: Iterative editing requires reliable history across every authoring surface.

**Independent Test**: Make mixed object/component/behavior/reference/drag edits, undo to the start, and redo to the end. Save midway and branch history; verify selection and saved-state indicators.

**Acceptance Scenarios**:

1. **Given** any supported scene edit, **When** Undo/Redo is invoked, **Then** document values, structure, relevant selection, and visible state return to their expected before/after states.
2. **Given** a continuous drag or numeric adjustment, **When** completed, **Then** one Undo reverses that interaction; separately completed interactions stay separately undoable.
3. **Given** a saved document, **When** edits are undone to the saved state, **Then** the unsaved indicator clears; a new edit after Undo discards the obsolete redo branch.
4. **Given** selection, focus, or layout changes without scene edits, **When** performed, **Then** they create no scene history entry or unsaved-scene indication.

---

### User Story 5 - Find assets and build and run the game (Priority: P2)

A developer browses assets, assigns compatible references, discovers behaviors, and configures/builds/runs/stops the game with progress and diagnostics inside the editor.

**Why this priority**: These existing workflows complete the edit-test cycle, including games with their own compiled behaviors.

**Independent Test**: Use an asset tree and game-specific behavior target. Assign texture/audio/animation and scene references; build/run/stop; repeat with deliberate configure/build/launch failures.

**Acceptance Scenarios**:

1. **Given** an asset root, **When** assigning a file by browsing, typing, or drag/drop, **Then** compatible fields store case-preserving `assets/...` references; incompatible or out-of-root assignments explain the issue without overwriting a valid value.
2. **Given** available project behavior definitions and serialized behaviors, **When** opening the behavior list or Inspector, **Then** available types/fields can be selected and edited without losing unfamiliar data or falsely presenting unavailable behavior as runnable.
3. **Given** valid run settings, **When** Build or Build & Run is requested, **Then** configure/build progress and game output appear in Console, the chosen game target receives the intended scene/asset root, and controls show active status.
4. **Given** an unsaved or modified scene, **When** Build & Run is requested, **Then** the intended scene must be saved successfully or execution cancelled; an obsolete saved version is never launched silently.
5. **Given** active work, **When** Stop is requested or a stage fails, **Then** queued launch work is cancelled, owned work stops, the outcome remains visible, and the editor remains usable without losing authored edits.
6. **Given** changed run settings, **When** the editor restarts, **Then** build profile, target/program, output dimensions, fullscreen/maximized/aspect choices, and applicable debug overlays/start-paused options are restored and applied.

---

### User Story 6 - Use a familiar, configurable workspace (Priority: P2)

A developer works in a coherent dark workspace inspired by Unity and arranges panels without losing scene data.

**Why this priority**: Similarity in usability, appearance, and style is an explicit user requirement, beyond applying a dark background.

**Independent Test**: Inspect first launch; complete selection-edit-run through labeled controls; move/hide panels, restart, and reset layout at normal and increased display scaling.

**Acceptance Scenarios**:

1. **Given** first launch, **When** the editor opens, **Then** Hierarchy is left, Scene center, Inspector right, Project/Assets and Console below, and a top toolbar presents clearly labeled/tooltipped editing and build/run controls.
2. **Given** selection, **When** using Inspector, **Then** object properties and collapsible component sections have consistent label/field alignment, a clear Add Component action, selection accent, and visible validation feedback.
3. **Given** a docked workspace, **When** panels are moved/resized/tabbed/hidden/reopened, **Then** the arrangement survives restart; Reset Layout restores usable defaults without changing scene data/history.
4. **Given** normal or increased scaling, **When** controls are used, **Then** text/icons, keyboard focus, selection, and controls remain legible and operable, with overflow reachable by scrolling/resizing.

### Edge Cases

- Empty/deeply nested scenes, duplicate/invalid identities, unknown components/behaviors, and unresolved references must not silently lose data. Invalid structural input may be rejected while retaining the current document.
- An editor-only missing-resource marker must remain visibly unresolved, never substitute saved game content, and never count as successful runtime loading.
- Delete/undo, document replacement, and repeated selection changes must repair selections/reference displays without accessing retired objects.
- Escape during a drag restores its starting value without a history entry; cancelled dialogs or invalid edits leave coherent state.
- Hiding/closing Scene cancels active gestures; zero-sized/minimized viewports do not cause invalid coordinates or saved-camera mutations.
- A failed build must not launch an old executable as though it were the requested result; failed launch/cancellation releases busy state.
- Paths containing spaces/non-ASCII characters must work across open/save, references, and build/run.
- Corrupt/missing workspace preferences fall back to usable defaults; inaccessible panel positions remain recoverable.
- Large hierarchies/asset trees and long build output must not produce unbounded retained-state growth or prevent cancellation while external work runs.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Add Dear ImGui as a reproducibly identified project dependency and provide a standalone desktop editor using it for the GUI. Building/launching through the documented repository workflow MUST require neither the legacy Python/Qt editor nor manually downloading the GUI dependency.
- **FR-002**: Editor support MUST be optional for runtime/library consumers. Existing standalone, headless, example, and installed-consumer workflows MUST work without linking/initializing editor facilities. The legacy editor MUST remain available during this feature; removal is outside scope.
- **FR-003**: Support New, Open, Save, Save As, validation, explicit startup scene, and restoring the last successfully opened scene. Preserve established scene format, hierarchy/order, identities, references, and unfamiliar serialized data unless explicitly removing their owner. No conversion to an editor-only scene format is required.
- **FR-004**: Track saved/unsaved state and offer Save/Discard/Cancel before replacement/exit. Failed/cancelled open/save MUST preserve current work and previously saved content, reporting destination/cause.
- **FR-005**: Provide nested Hierarchy display, object/component selection, root/child creation, subtree duplication, and deletion with synchronized valid selection. Created/duplicated identities MUST be unique. Duplication MUST preserve established reference semantics without silently targeting unrelated objects.
- **FR-006**: Inspector MUST edit object name/tag/active/position/rotation/scale and all currently supported editable fields of box/circle colliders, rigid bodies, sprites, animators, cameras, audio, and scripts. Component add/remove MUST respect engine eligibility and collider conflicts.
- **FR-007**: Support discovery/attachment/removal/editing of available project behaviors, including existing scalar, nested, list, asset, and typed object/component-reference workflows. Preserve unknown data through unrelated edits; distinguish missing/unsupported behavior/fields from available ones. Baseline parity MUST NOT require rewriting game code for the stock `main`, `mario_example`, and other targets using the shared `platformator::run(argc, argv, defaultScene)` entry and existing serialization macros; rebuilding with updated headers is sufficient. Custom entry points remain editable/buildable, but automatic catalog verification and Run require an explicit compatible catalog integration; the editor MUST NOT modify game source to add it or claim such targets are automatically compatible.
- **FR-008**: Validate committed authoring changes against established engine value/reference constraints with field-specific feedback. Retain recoverable imported unresolved values for correction and identify unsupported structural content before run. Do not silently change engine physics, lifecycle, or required-asset contracts.
- **FR-009**: Provide an engine-rendered 2D Scene view with selection highlights, grid, camera/collider outlines, and object-position/collider-offset handles. Sprite appearance and transform interpretation MUST follow engine semantics; navigation MUST be independent of saved cameras. Preview MUST NOT start game behaviors, physics, or automatic audio playback.
- **FR-010**: Support pan/zoom, frame-scene/selection, deterministic picking with access to overlapping targets, and correct local edits for parented objects. Inspector/Scene MUST agree after commits; input capture MUST prevent unintended interactions in other panels or the game.
- **FR-011**: Provide undo/redo for every supported scene mutation, including object/component/behavior/property/reference/viewport edits. Continuous gestures MUST be single transactions; cancellation restores the starting state; separate gestures remain separate. Restore document/valid selection and follow US4 saved-state/redo-branch behavior.
- **FR-012**: Provide Project/Assets browsing beneath the selected asset root and compatible assignment through browsing, text, and drag/drop. Preserve case and existing `assets/...` identity/containment rules; unresolved resources remain visible/repairable without substituting saved content.
- **FR-013**: Provide persistent project/run settings and Build, Build & Run, and Stop with configure/build/launch status/output in Console. Preserve debug/production and existing window/debug options and allow the correct game-specific target/program. One active pipeline owns its work, rejects duplicate launches, cancels queued work on failure/Stop, and leaves the GUI responsive during external work.
- **FR-014**: Build & Run MUST execute the intended successfully saved scene with explicit asset root through the established runtime entry contract. Report unavailable behaviors rather than omitting them. Execution/failure/Stop MUST preserve authoring data, selection/history, and editor usability.
- **FR-015**: Implement US6 layout/presentation: dockable/resizable/tabbed panels, menus/top toolbar, neutral dark surfaces, clear panel separation, restrained selection accents, consistent spacing, compact aligned fields, collapsible component headers, and readable icons with labels/tooltips. Unstyled demo windows alone do not meet the requested direction.
- **FR-016**: Persist layout/preferences separately from scenes; supply panel visibility controls, Reset Layout, and invalid/offscreen-preference recovery. Support normal/increased scaling, reachable overflow, and visible keyboard focus. Docking within the main application window is required; detached OS windows are optional.
- **FR-017**: Expose File/Edit/View/Run actions and discoverable shortcuts. Preserve existing new/open/save/save-as/undo/redo, Build (`Ctrl+B`), Run (`F5`), Stop (`Shift+F5`), frame-scene (`F`), frame-selection (`Shift+F`), and zoom actions on the initial platform. Scene validation is automatic before Build & Run; do not expose a separate Validate Scene command or Editor Settings panel. Document deliberate Unity deviations; global shortcuts MUST respect text entry.
- **FR-018**: During planning, establish a workflow parity inventory from the current editor/tests, mapping every included workflow to acceptance checks. Verify representative scenes with standalone runtime and legacy editor. Parity means equivalent authoring outcomes, not copying legacy internals/defects.
- **FR-019**: Evaluate representative large-scene selection, property editing, dragging, hierarchy/asset browsing, idle operation, and build-output handling. Avoid full-document copying/serialization merely to draw the UI or track one field/drag. Record responsiveness, recurring allocations/retained memory, and complete editor/game costs separately; investigate regressions without assuming migration guarantees speedups. UI mutation and engine worker boundaries MUST remain safe.
- **FR-020**: Document dependency identity/license notices, setup/launch, migration/compatibility, supported platform, shortcuts, limitations, and Scene preview versus game execution. Versions, build integration, ownership, and scheduling strategies belong to planning; focused validation MUST follow compact artifact and time/size-budget policy.

### Workflow Parity Boundary

| Existing workflow | Required outcome | Acceptance coverage |
| --- | --- | --- |
| Scene lifecycle/validation | Default camera, open/save/save-as, startup/last scene, errors, unsaved protection | US1; FR-003–004, FR-008 |
| Hierarchy/selection | Nested objects, root/child creation, subtree duplicate/delete, object/component selection | US2; FR-005 |
| Inspector/component palette | Existing object/component fields and supported add/remove actions | US2; FR-006, FR-008 |
| Behaviors/references | Available types, supported field editors, asset/typed reference assignment | US2, US5; FR-007–008, FR-012 |
| Scene canvas | Sprites/outlines, picking, navigation/framing, object/offset dragging | US3; FR-009–010 |
| Undo/redo/dirty state | Reversible edits, gesture merging, valid selection, saved-state tracking | US4; FR-011 |
| Assets | Folder browsing, compatible assignments, established path rules | US5; FR-012 |
| Configure/build/run/stop | Correct target/scene/root/options, output, failure/cancellation | US5; FR-013–014 |
| Workspace | Panels, actions/shortcuts, preferences/layout reset, coherent dark styling | US6; FR-015–017 |

### Key Entities *(include if feature involves data)*

- **Authoring document**: Scene game data, identities, hierarchy/order, references, source location, and saved-state marker; separate from workspace and running-game state.
- **Selection**: Selected object and optional component shared across panels and repaired on removal/document replacement.
- **Edit transaction**: Reversible action with before/after values and relevant selection; one continuous gesture is one transaction.
- **Scene preview**: Editable content with independent navigation/overlays, without game simulation.
- **Project context**: Scene/asset locations, available behaviors, game target/program, and build/run choices.
- **Build/run operation**: Owned configure/build/launch sequence with explicit scene/root, status, output, and cancellation.
- **Workspace preferences**: Layout, visibility, scaling, and user choices stored separately from scenes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every included parity-inventory workflow has a demonstrated passing acceptance check; no required workflow is a placeholder or requires the legacy editor to finish.
- **SC-002**: Valid compatibility fixtures round-trip with zero unintended changes to identities, references, hierarchy/order, values, or unfamiliar data. Authored supported scenes load in standalone games; cross-editor checks show equivalent saved meaning.
- **SC-003**: All supported mutations in the mixed-edit acceptance sequence undo/redo to the expected document/selection. Continuous gestures produce one history action, cancelled gestures none, and returning to saved state clears the unsaved indicator.
- **SC-004**: Overlap, parent translation, sprite source/flip/rotation, and both collider fixtures show zero observed selection/coordinate discrepancies or unintended saved-camera changes through navigation, resize, and editing.
- **SC-005**: Successful run, save-before-run cancellation, configure/build/launch failure, and Stop preserve scene data and usable controls. Twenty consecutive open/edit/run/stop cycles produce no stale selection, duplicate operation, or continuing retained-state growth after warmup.
- **SC-006**: Fresh launch exposes all required panels/actions in the documented layout. Hide/reopen/rearrange/restart/reset checks pass at 100% and 150% display scaling without permanently unreachable controls. Visual review records compliance with every FR-015 style requirement and deliberate workspace deviations.
- **SC-007**: A developer builds/launches from documented checkout without the legacy editor installed or manual dependency download; runtime-only/headless/installed-consumer checks remain independently usable.
- **SC-008**: Focused measurements report interaction responsiveness, idle cost, and memory on declared representative scene sizes, with comparable legacy workflows where available. Regressions/unavailable comparisons are explained; speedup claims are limited to supported measurements and retained samples. Accepted evidence must identify the final candidate source/dependency/build/harness revision; changes invalidating that evidence reopen affected verification, and superseded captures do not certify the changed candidate.

## Assumptions

- This invocation creates a specification. Dependency acquisition and implementation follow planning/tasks; this document does not claim completed integration.
- Dear ImGui and a native C++ editor are product constraints from the request and discussion. Follow the repository's bundled-dependency convention; exact revision/backend/build wiring are planning choices. Other designs remain open.
- Linux is the first verified desktop platform, matching the currently verified runtime environment. Windows/macOS delivery and detached OS-window docking are follow-ups; avoid unnecessary portability obstacles.
- Baseline sources are the [editor shell](../../ui/src/platformator_ui/main_window.py), [Scene canvas](../../ui/src/platformator_ui/widgets/scene_canvas.py), [Inspector](../../ui/src/platformator_ui/widgets/inspector.py), [models](../../ui/src/platformator_ui/scene/models.py), and [editor tests](../../ui/tests). The [UI README](../../ui/README.md) is not a complete parity inventory. Engine public contracts govern where the legacy editor is outdated.
- Initial execution parity is Build & Run in the game window plus Stop and existing runner debug options. Embedded Game view, live runtime property editing, automatic code reload, and new pause/step controls are outside scope. Run labeling must make this execution model clear; authoring Scene remains embedded and engine-rendered.
- Automatic Build & Run compatibility covers shared-runner targets, including the repository's stock runner and Mario example. Project settings declare catalog protocol support; known bundled targets default to the supported protocol, other targets require explicit declaration. A custom entry without that support can still be edited and built; automatic Run is disabled with an integration explanation, without probing its executable or changing its source.
- One scene and single-selection editing satisfy initial parity. Multi-object editing, new reparent/reorder tools, full rotation/scale gizmos, prefabs, animation timeline authoring, visual scripting, new asset formats, and arbitrary editor plugins are follow-ups.
- Unity similarity means recognizable panel roles, selection/Inspector workflow, component presentation, and cohesive styling adapted to this 2D engine. It does not require branding or full Unity functionality. The [official Unity interface reference](https://docs.unity.cn/Documentation/Manual/UsingTheEditor.html) establishes panel roles; FR-015 defines this project's visual direction rather than claiming an exact Unity theme match. Checked 2026-10-30.
- The [official Dear ImGui docking reference](https://github.com/ocornut/imgui/wiki/Docking) documents maintained docking support. Exact revision/backend capabilities belong to planning; in-window docking does not imply detached-window support. Checked 2026-10-30.
- Preserve single-active-runtime, reference lifetime, mutation, fixed-step, asset, and drawing-order contracts. Preview/game lifetimes must be explicit without unrelated physics/culling/lifecycle rewrites. Planning must assess main-thread UI work versus safe background/worker work under the constitution.
- Validation is a focused pilot under existing budgets, retaining compact reports, identities, commands, and raw samples. Use targeted correctness tests; retain detailed diagnostics/images only for a specific failure with a small explicit bound. Do not restart historical campaigns or treat missing historical raw artifacts as verifiable.
