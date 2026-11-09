# Feature Specification: GUI Cleanup

**Feature Branch**: `branch-without-bvh` (existing checkout; feature directory is independent)

**Created**: 2026-11-05

**Status**: Draft — validated for planning

**Input**: User description: "address all the findings in the audit - GUI cleanup"

**Source**: [Native GUI audit](../../docs/GUI_AUDIT.md), including its asset file-change findings and filesystem notification recommendations. The audit describes the reviewed starting point, not completed implementation or current performance measurements.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Edit every scene object accurately (Priority: P1)

An author selects and positions sprites, invisible collision areas, cameras, and empty objects through consistent Scene, Hierarchy, and Inspector views.

**Why this priority**: Invisible or incorrectly framed authoring objects prevent reliable level editing.

**Independent Test**: Open a fixture containing every supported object/component representation, legacy camera dimensions, nested objects, and overlapping targets. Select, frame, transform, save, undo, and reopen it.

**Acceptance Scenarios**:

1. **Given** collider-only objects, empty objects, cameras, and sprites with missing textures, **When** viewing and picking the Scene, **Then** each has an appropriate outline or placeholder, can be selected without first using Hierarchy, and can be framed individually or with a selection.
2. **Given** overlapping selectable objects/components, **When** repeatedly picking or choosing their handles, **Then** eligible targets remain deterministically reachable and all panels agree on object/component selection.
3. **Given** canonical or supported legacy camera dimensions, **When** opening, editing, resizing, and framing the Scene, **Then** camera boundaries and Inspector values agree with the saved scene and game interpretation.
4. **Given** a selected object, **When** choosing Move, Rotate, or Scale by labeled controls or W/E/R, **Then** the chosen mode is visibly active and only its intended handles perform transformations; typing in another field does not switch tools.
5. **Given** a nested object or collider offset, **When** transforming with configured grid/increment snapping, **Then** the correct local values change and one Undo reverses the completed interaction.
6. **Given** an off-center pointer position, **When** wheel-zooming or panning, **Then** the point under the pointer remains anchored during zoom and coordinate mapping remains correct after resize or monitor-scale changes.

---

### User Story 2 - See asset changes without interrupting editing (Priority: P1)

An author adds, modifies, renames, or deletes assets using an external tool and sees accurate browser, reference, and preview updates while continuing work.

**Why this priority**: External asset creation is a routine part of the edit-test cycle; stale listings and unnecessary reloads undermine it.

**Independent Test**: Keep a populated folder and scene open while making controlled filesystem changes, including startup overlap, partial writes, replacement, notification loss, and project switching.

**Acceptance Scenarios**:

1. **Given** an already-open asset folder, **When** adding a file, creating a nested folder, or moving a populated directory into the asset root, **Then** its contents become discoverable automatically without restarting or requiring another filesystem change.
2. **Given** unrelated assets already displayed, **When** adding an unused image elsewhere, **Then** affected listings update while unrelated thumbnails, Scene resources, scene history, and valid selection remain intact.
3. **Given** a referenced texture or animation, **When** modifying it or replacing it by an atomic file replacement, **Then** affected previews and reference validity update; a notified same-size, same-timestamp write is not ignored.
4. **Given** an asset being written in multiple steps, **When** it is temporarily unreadable or incomplete, **Then** the last usable preview remains available, failure status is visible, and bounded recovery adopts the completed file without an endless retry loop.
5. **Given** changes during initial discovery, notification loss, exhausted monitoring capacity, or a tree exceeding global scan limits, **When** monitoring reconciles, **Then** no successful baseline silently hides the changes; visible-folder refresh and manual Refresh remain usable and incomplete coverage is clearly reported.
6. **Given** pending events from the current project, **When** switching projects or closing the editor, **Then** old events cannot alter the replacement workspace or access released resources.
7. **Given** a selected or referenced asset, **When** it is renamed or deleted externally, **Then** existing serialized references are preserved as unresolved until explicitly repaired and selection is retained only if its target still exists.
8. **Given** contained, broken, escaping, or cyclic asset links, **When** browsing, assigning, and monitoring them, **Then** contained targets remain usable and update correctly, broken links show unresolved status, and escaping/cyclic links cannot expose out-of-root content or cause endless discovery.

---

### User Story 3 - Diagnose problems and trust undo and saved state (Priority: P1)

An author corrects validation errors, reads build output, and experiments with property values without losing work or repeatedly undoing one intended edit.

**Why this priority**: Accurate feedback and reversible changes are necessary for safe authoring.

**Independent Test**: Open invalid but editable content, repair it, delete a dependency, enter numeric values, trigger a compiler error, and use validation, Console, save conflict, and history controls.

**Acceptance Scenarios**:

1. **Given** a displayed validation issue, **When** correcting the field or restoring its asset, **Then** the stale issue disappears automatically; introducing a new issue makes it visible, and Validate/F6 provides an explicit refresh.
2. **Given** a validation result, **When** activating it, **Then** the offending object/component is selected and its field is revealed; Run remains blocked by unresolved blocking issues.
3. **Given** a numeric value entry or continuous adjustment, **When** committing, cancelling, undoing, and redoing it, **Then** one completed interaction is one history entry, cancelled/unchanged drafts produce none, and saved-state tracking remains correct.
4. **Given** compiler or game output, **When** selecting/copying, searching, filtering severity, collapsing repeated messages, following output, clearing it, or opening a source location, **Then** each action works on the retained output; display and retention limits are explained and retained earlier text is accessible.
5. **Given** an externally changed scene with unsaved edits, **When** a change is detected or a save is attempted, **Then** the editor preserves the authored work and offers explicit conflict resolution rather than overwriting or reloading silently.

---

### User Story 4 - Work responsively with large scenes and asset libraries (Priority: P1)

An author leaves the editor open, browses large images, and navigates large or deeply nested scenes without unnecessary ongoing compute or unbounded memory growth.

**Why this priority**: The audit identifies repeated idle work, blocking asset loads, and retention risks in common workflows.

**Independent Test**: Compare the audited behavior and candidate on the same machine using a small scene, a 10,000-object scene, a 128-level hierarchy, and a bounded library of large images; measure the actual displayed application during idle, interaction, and asset refresh.

**Acceptance Scenarios**:

1. **Given** an unchanged static Scene, **When** leaving the editor idle or minimizing/unfocusing it, **Then** unchanged Scene content is reused and background work is reduced while incoming changes, process output, and user actions still receive service.
2. **Given** large image and animation assets, **When** first browsing and revisiting folders, **Then** decoding/loading does not block unrelated input, retained previews stay within a documented memory budget, and evicted entries reload correctly.
3. **Given** a large expanded Hierarchy or folder, **When** scrolling or editing one item, **Then** hidden unchanged rows and unrelated labels/resources do not undergo repeated rebuilding solely because another frame is displayed.
4. **Given** an unchanged selected object with asset references, **When** remaining idle, **Then** the editor avoids repeatedly checking the same filesystem facts; changes still invalidate the displayed validity and assignments are checked when committed.
5. **Given** Run Settings text entry or a local property edit, **When** typing or committing, **Then** durable preference writes and expensive refreshes are consolidated; unrelated descendants and resources remain untouched unless the property's effect requires them.
6. **Given** objects outside the Scene view, **When** navigating a large scene, **Then** avoidable offscreen work is reduced without changing overlap order, visibility, picking, or authored results.

---

### User Story 5 - Find and organize content efficiently (Priority: P2)

An author searches for objects/assets, organizes hierarchy relationships, edits multiple items, and assigns compatible references through familiar direct actions.

**Why this priority**: Repeated menu searches and manual JSON edits slow routine level authoring.

**Independent Test**: Complete search, reparent/reorder, multi-edit, and reference-assignment workflows in a scene with duplicate names, overlapping objects, and a large asset tree.

**Acceptance Scenarios**:

1. **Given** many objects, assets, components, behaviors, and reference targets, **When** using their search/filter controls, **Then** matching entries remain identifiable by type and location and can be selected without losing unrelated authored data.
2. **Given** a hierarchy, **When** dragging objects to a new parent or sibling position, **Then** the operation preserves intended world placement, prevents cycles, updates applicable draw order, and supports Undo/Redo.
3. **Given** a range or additive multi-selection, **When** transforming or editing common properties, **Then** all eligible selected items change consistently, mixed values are distinguished, and one Undo reverses the group operation.
4. **Given** a background object, **When** hiding or locking it for editing, **Then** its Scene visibility/pickability changes without modifying runtime Active state or dirtying the scene.
5. **Given** a compatible selected component, **When** double-clicking an asset, choosing Assign, entering/pasting its path, browsing, or dragging it onto a field, **Then** the intended assignment succeeds; an incompatible or escaping path explains the failure and preserves the old value.
6. **Given** a Hierarchy selection, **When** using F2, Delete, or Ctrl+D, **Then** rename, delete, or duplicate applies to the intended selection with normal input capture and reversible history.

---

### User Story 6 - Set up and customize a clear workspace (Priority: P2)

An author opens a project, chooses build/run settings, arranges panels across monitors, and completes common tasks using clear controls or the keyboard.

**Why this priority**: The editor should explain its state without requiring knowledge of build internals.

**Independent Test**: Launch from an unrelated directory, open a project, discover build choices, run its game, arrange/detach panels, change display scale, restart, and repeat primary actions using keyboard navigation.

**Acceptance Scenarios**:

1. **Given** no valid startup project, **When** launching the editor, **Then** a welcome/open-project flow explains how to proceed; cancelling or choosing an invalid folder preserves a usable workspace.
2. **Given** a game project, **When** configuring Build/Run, **Then** available presets and targets are discoverable, an executable can be browsed for, missing behavior metadata has an actionable explanation, and advanced integration settings are separated from routine choices.
3. **Given** an active workspace, **When** opening or changing scenes, selecting tools, or starting/stopping work, **Then** project/scene identity, dirty state, selected mode, and operation state remain visible; Ctrl+, opens settings and an action reference lists supported shortcuts.
4. **Given** multiple monitors and supported display scales, **When** detaching/docking/hiding panels, moving between monitors, resizing, restarting, or resetting layout, **Then** panels remain usable, text/icons/focus are legible, and saved arrangements recover from unavailable monitors.
5. **Given** primary authoring actions, **When** completing them through keyboard focus or inspecting control names with supported desktop assistive tools, **Then** controls have meaningful names and visible focus; essential state is understandable without color alone and verified accessibility limits are documented.

---

### User Story 7 - Use the editor on supported desktop platforms (Priority: P2)

An author installs and uses the same project on Linux, Windows, or macOS without a source checkout or the legacy Python editor.

**Why this priority**: Platform support and self-contained distribution are explicit gaps in the audit.

**Independent Test**: On each supported desktop platform, install into a fresh location, launch from another directory, open/save a Unicode-and-spaces project, use dialogs and notifications, build/run/stop its game, and restart.

**Acceptance Scenarios**:

1. **Given** a packaged editor, **When** launching without its original source/build tree, **Then** layouts, icons, dependencies, and required resources are available and native dialogs work.
2. **Given** a project whose paths include spaces, Unicode, and platform-specific path conventions, **When** opening/saving/assigning assets and building/running, **Then** identities and references retain their meaning and errors preserve existing data.
3. **Given** an owned build or game with child processes, **When** stopping, cancelling, failing, or exiting, **Then** owned work and pending launches stop while unrelated processes remain untouched.
4. **Given** a save conflict, write failure, or interrupted save, **When** saving scenes or preferences, **Then** previous valid content remains recoverable, conflicts are reported, and the editor does not falsely mark unsaved work saved.

---

### User Story 8 - Reuse objects and component settings safely (Priority: P3)

An author copies/reset components and saves reusable object assemblies without accidentally binding new copies to original internal objects.

**Why this priority**: Reusable authoring reduces repetitive setup after basic editing is dependable.

**Independent Test**: Copy component settings, reset a component, save/instantiate a nested object template with internal/external references, and duplicate it using each reference policy.

**Acceptance Scenarios**:

1. **Given** compatible components, **When** copying/pasting values or resetting supported properties, **Then** values change through undoable commands while identities and unrelated data are retained.
2. **Given** a nested object assembly, **When** saving it as a reusable template and instantiating it repeatedly, **Then** each instance receives unique identities, retains its configured children/components, and resolves internal references to its own members.
3. **Given** a subtree referencing itself, descendants, and external objects, **When** duplicating with the chosen internal-reference policy, **Then** the UI explains the policy, internal references follow that policy, external references retain their targets, and Undo/Redo preserves the result.

---

### User Story 9 - Work across scenes and inspect play sessions (Priority: P3)

An author keeps several scene documents open, plays the active scene in a Game view, and pauses/steps/inspects a running game without mixing runtime state with authored changes.

**Why this priority**: These audit recommendations complete a more convenient edit-test workflow and require separate lifecycle acceptance.

**Independent Test**: Open two scenes with independent unsaved changes, build/run one, view game output inside the editor, pause/step and inspect runtime objects, then stop and close documents with Save/Discard/Cancel.

**Acceptance Scenarios**:

1. **Given** several scene tabs, **When** switching, editing, saving, undoing, or closing a tab, **Then** each document retains its own selection, view, history, and saved state; Run identifies the intended active scene.
2. **Given** a compatible game target, **When** starting play, **Then** a Game view displays the running game, failures/capability limits are explained, and an external game-window option remains available.
3. **Given** a running compatible game, **When** pausing and stepping, **Then** pause freezes simulation while the editor stays responsive and each Step advances exactly one declared simulation step.
4. **Given** a live object, **When** inspecting its runtime properties and values, **Then** runtime information is clearly distinguished from authored content; stopping restores authoring context without silently saving runtime changes.
5. **Given** edited game code or assets during a play session, **When** applying a supported live update or restarting through the editor, **Then** capability/status is explicit, authored work is preserved, and unsupported updates offer a controlled rebuild/restart path.

---

### User Story 10 - Author sprites and animations visually (Priority: P3)

An author configures sprite-sheet regions and animation sequences with visual feedback rather than manually editing asset files.

**Why this priority**: The audit identifies missing asset-authoring workflows beyond the existing thumbnail previews.

**Independent Test**: Select a sprite sheet, define regions, construct/edit an animation sequence, scrub/play it, save/reopen, and load the result in a game.

**Acceptance Scenarios**:

1. **Given** a sprite sheet, **When** visually selecting or numerically editing frame regions, **Then** bounds and dimensions are clear and invalid/out-of-image regions are explained before commit.
2. **Given** animation frames, **When** adding/removing/reordering frames or editing timing/loop options and scrubbing/playing a preview, **Then** the preview reflects the saved sequence and authored edits support Undo/Redo.
3. **Given** a saved sprite/animation asset, **When** reopening it or using it in a scene/game, **Then** compatible asset semantics are retained and dependent previews update without unrelated reloads.

### Edge Cases

- Empty and 10,000-object scenes, a 128-level hierarchy, offscreen populations, missing/inactive sprites, collider-only objects, multiple cameras, and overlapping components must remain navigable without silently losing data.
- Camera data with conflicting canonical and legacy dimensions must surface the conflict; supported legacy-only values must not silently become default dimensions.
- Reparenting into a descendant, mixed incompatible multi-selections, duplicate names, deletion of selected/reference targets, and out-of-root references must be handled explicitly.
- Startup filesystem changes, newly moved populated directories, root replacement/removal, partial writes, atomic saves, unchanged metadata, permission changes, event overflow, monitoring exhaustion, and unavailable/network filesystems require visible, bounded recovery.
- Symlinks within the asset root may be used only when their resolved targets stay inside that root; escaping links and cycles must not be followed. Broken links remain identifiable as unresolved entries.
- Closing or replacing a project with pending dialogs, scans, resource loads, detached windows, or game processes must not let old results reach new documents or released resources.
- Monitor removal, high-DPI changes, narrow windows, long labels/output, keyboard-only use, and unavailable desktop dialog providers must retain reachable controls and understandable status.
- Huge image dimensions, invalid animation descriptions, rapidly changing assets, and browsing more assets than fit the preview budget must not cause unbounded memory, UI blocking, or endless retries.
- Unsaved tabs, external edits, failed writes, cancellation, runtime termination, unsupported live-update capability, and unsaved asset documents must preserve authored work and clearly identify the operation outcome.

## Requirements *(mandatory)*

### Functional Requirements

#### Scene accuracy and editing

- **FR-001**: The editor MUST show selectable authoring representations and frameable bounds for all scene objects, including cameras, collider-only objects, empty objects, inactive objects, and objects with missing assets. Component selection MUST remain distinguishable from owner selection.
- **FR-002**: Scene navigation, overlays, camera boundaries, and Inspector MUST interpret supported canonical and legacy camera dimensions consistently with saved game data. The Scene MUST fill its available editing area without stretching world geometry, show the game camera boundary separately, and preserve correct independent display-coordinate ratios. This defines the intended replacement for the audited letterboxing disagreement.
- **FR-003**: The editor MUST provide labeled Move/Rotate/Scale controls, visible active-tool state, W/E/R shortcuts, clear rotation units, and local/world axis choices where applicable; field/popup/gesture input capture MUST prevent unrelated actions.
- **FR-004**: Authors MUST be able to configure grid spacing and position/rotation/scale increments, use Ctrl-drag position snapping including collider offsets, pan, frame selections/all objects, and zoom around the pointer. A rectangle tool MUST support visual sprite/collider sizing.
- **FR-005**: Numeric field entry, continuous adjustment, and viewport gestures MUST each commit as one reversible interaction; cancelled, unchanged, or invalid drafts MUST preserve the previous accepted value without unrelated history entries.
- **FR-006**: Every supported scene/asset mutation MUST preserve appropriate identity, history, saved-state, and selection semantics through Undo/Redo and save/reopen. Unknown serialized data and existing engine asset-path/reference rules MUST remain preserved.

#### Asset changes and monitoring

- **FR-007**: The editor MUST use filesystem change notifications as the primary trigger for automatic asset/source refresh on supported local filesystems, with bounded asynchronous discovery/reconciliation and polling fallback when notifications are unavailable or unreliable. Manual Refresh MUST remain available.
- **FR-008**: Automatic discovery MUST cover additions, modifications, removals, renames, new subdirectories, populated directories moved into the root, and root replacement. Initial discovery and monitoring MUST account for changes occurring during startup without requiring a later unrelated change.
- **FR-009**: Detected changes MUST refresh affected listings, thumbnail/animation previews, Scene dependencies, reference validity, and validation results while preserving unaffected resources, valid selection, scene content, and undo history.
- **FR-010**: A notified write or replacement MUST invalidate the affected asset even if size and modification time are unchanged. Metadata comparisons MUST NOT be presented as proof that contents are identical.
- **FR-011**: Rapid writes MUST be consolidated with a maximum postponement bound. Temporarily incomplete/unreadable assets MUST retain a last usable preview where available, expose current status, and receive bounded retry/reconciliation without silently accepting invalid content.
- **FR-012**: Lost events, scan truncation, monitoring/resource exhaustion, permission errors, and unavailable roots MUST produce visible coverage status and bounded recovery. Global coverage failure MUST NOT disable refresh of reachable visible folders or referenced assets. Limits MUST NOT silently increase.
- **FR-013**: Project replacement and shutdown MUST stop obsolete monitoring/loading work and reject stale notifications/results. External asset renames MUST NOT silently rewrite authored references; the editor MUST offer explicit repair of unresolved references.
- **FR-014**: Browsing, monitoring, and assignment MUST consistently support contained symlink assets, identify broken links, and reject escaping links/cycles under the existing root-containment policy.

#### Diagnostics and output

- **FR-015**: Validation MUST refresh after committed scene edits and relevant external asset changes, provide Validate/F6, identify blocking issues accurately, and let an author select/reveal the offending object, component, and field. Run MUST validate the current intended scene before launch.
- **FR-016**: Console MUST provide selectable/copyable text, Copy All retained output, search, severity filters, collapsed repeats, Clear, optional auto-follow, and actionable source locations where supplied by diagnostics. Retention/display truncation MUST be explained and all retained text MUST be accessible.
- **FR-017**: External scene changes and failed/conflicting saves MUST preserve authored work and previous valid content; Save/Discard/Cancel and explicit overwrite/save-elsewhere choices MUST accurately control continuation. Notifications MUST NOT silently reload a dirty scene.

#### Resource use and responsiveness

- **FR-018**: The editor MUST reuse unchanged static Scene content, pace idle work, and reduce unnecessary activity when minimized/unfocused while continuing to service changes, dialogs, owned processes, and visible animated previews.
- **FR-019**: Asset browsing MUST avoid blocking unrelated interaction during image/animation preparation and MUST enforce documented preview memory and per-update work budgets. Unused previews MUST be reclaimable and reload correctly when revisited.
- **FR-020**: Hierarchy/asset presentation MUST avoid rebuilding unchanged hidden entries, labels, and connector geometry each frame; local edits MUST invalidate only affected presentation and required dependencies.
- **FR-021**: Asset-reference display MUST reuse unchanged validity information and refresh it after relevant changes; authoritative assignment/save/run validation MUST remain intact.
- **FR-022**: Preference changes MUST be consolidated and persisted at a completed editing boundary or bounded deferred save, with final flush on orderly shutdown; typing each character MUST NOT cause independent durable writes of unchanged preference/layout files.
- **FR-023**: Retained preview state MUST grow with represented objects and their own authoring/rendering inputs, without multiplying descendant content across ancestors; a property change without descendant effects MUST leave descendant previews unchanged.
- **FR-024**: Scene rendering/overlay work MUST avoid unnecessary offscreen work where a simpler correct bounded approach is available, preserving document draw order, overlap picking, parent transforms, resize, and asset-refresh semantics. Planning MUST justify any remaining full population work against measured representative cases.

#### Discovery and organization

- **FR-025**: Hierarchy, Project/Assets, Add Component, Add Behavior, and scene-reference choices MUST support search/filtering with enough type/location context to distinguish duplicate names.
- **FR-026**: Hierarchy MUST support undoable reparenting and sibling reordering, prevent cycles, preserve world placement by default on reparent, and update applicable drawing order consistently with saved document order.
- **FR-027**: Authors MUST be able to use additive/range multi-selection and edit/transform common eligible properties. Mixed values and incompatible operations MUST be explicit, and group operations MUST form one reversible interaction.
- **FR-028**: Authors MUST be able to hide objects or disable their pickability for editing without changing runtime Active state or dirtying scene content; these choices MUST persist as workspace preferences.
- **FR-029**: Hierarchy MUST expose F2 rename, Delete, and Ctrl+D duplication with input capture and selection behavior consistent with the corresponding visible commands.
- **FR-030**: Compatible asset assignment MUST support double-click/Assign, inline typed/pasted paths, drag/drop, and browsing. Fields MUST offer full-path display/copy and unresolved-path correction. Rejected assignments MUST preserve accepted values and explain expected type/root constraints.

#### Workspace and setup

- **FR-031**: Launch without a usable project MUST present a recoverable welcome/open-project workflow; failed/cancelled project selection MUST preserve the current usable context.
- **FR-032**: Run setup MUST offer discoverable project build presets/targets and executable browsing, explain unavailable behavior metadata/capabilities, and provide an obvious build/refresh action. Compiled behavior availability MUST remain authoritative; advanced protocol choices MUST not be prerequisites for routine stock-project use.
- **FR-033**: The workspace MUST display active project/scene identity, dirty state, selected tool, and operation status, provide Ctrl+, for settings, and expose an action/shortcut reference consistent with implemented commands.
- **FR-034**: Panels MUST support detaching into desktop windows, redocking/tabbing, hide/reopen, restart restoration, and Reset Layout across monitor removal and display-scale changes without changing scene/history data.
- **FR-035**: An in-app UI scale setting MUST keep controls, icons, text, and hit targets usable at 100%, 150%, and 200%, including long labels and narrow layouts. Overflow MUST remain reachable.
- **FR-036**: Primary authoring controls MUST support meaningful accessible names, visible keyboard focus/activation, and state cues beyond color. Platform verification MUST include keyboard-only primary flows and desktop assistive-tool checks with concrete documented limitations.

#### Desktop compatibility and distribution

- **FR-037**: The editor MUST support Linux, Windows, and macOS for install/launch, scene/asset authoring, preferences, dialogs, notifications, and build/run/stop workflows, with platform-specific acceptance evidence.
- **FR-038**: Distributed editors MUST include required resources and dependencies and work from an arbitrary invocation directory without their original source/build tree or Python editor installation.
- **FR-039**: Cross-platform paths with Unicode, spaces, platform executable conventions, and project build configurations MUST work without losing asset identity, altering case semantics, or misinterpreting process arguments.
- **FR-040**: Process cancellation, failure, and exit MUST stop owned work and descendants and cancel queued launches, preserving unrelated processes and the current document. Scene/preference writes MUST preserve conflict detection and recoverable previous valid content on supported platforms.

#### Reusable and extended authoring

- **FR-041**: Authors MUST be able to reset supported component values and copy/paste compatible settings without replacing identities, destroying unknown fields, or bypassing undo history.
- **FR-042**: Authors MUST be able to save and instantiate reusable subtree templates containing children, components, and values with unique instance identities and internal-reference remapping. External references MUST remain explicit and be validated in the destination scene.
- **FR-043**: Subtree duplication MUST expose an explicit policy to retain existing numeric targets or remap references inside the copied subtree, explain the selected policy, preserve external targets, and remain reversible. Existing projects MUST retain their current duplication default until the author changes it.
- **FR-044**: Scene tabs MUST maintain independent documents, selection, view, undo history, and dirty state. Save/close/run MUST identify their target document and protect each unsaved tab.
- **FR-045**: Compatible games MUST be viewable in an editor Game view with an external-window option. Capability limits, build/query failures, and termination MUST be visible without losing authored scenes.
- **FR-046**: Compatible play sessions MUST offer Pause, Resume, and one-simulation-step advancement plus read-only live object/property inspection. Runtime values MUST be visibly distinct from authored values and MUST NOT be automatically persisted into scene content.
- **FR-047**: The edit-test workflow MUST support controlled application of compatible live asset/code changes where advertised and an explicit rebuild/restart path for unsupported changes, preserving authoring state. General arbitrary-code hot reload is not required; unsupported capability MUST be explained rather than implied.
- **FR-048**: Authors MUST be able to define sprite-sheet frame regions visually/numerically, edit animation frame order/timing/loop settings, scrub/play previews, and save/reopen compatible assets with Undo/Redo and dependent preview refresh.

#### Verification and compatibility

- **FR-049**: The audited hierarchy-row and Scene-projection failures MUST be resolved against explicit intended behavior and meaningful acceptance checks. Passing a changed assertion alone MUST NOT establish correctness; failed suites MUST be rerun through previously unreached checks and regressions diagnosed.
- **FR-050**: Verification MUST cover ordinary asset add/edit/delete/rename/replace, initial discovery overlap, partial writes, same-metadata notifications, overflow/exhaustion, bounded fallback, and stale project events, as well as current document/history/resource contracts.
- **FR-051**: Performance verification MUST exercise the displayed GUI/event loop, actual panels, Scene refresh/rendering, first asset browsing, and idle/background activity. Selection/property-command timings alone MUST NOT be represented as GUI performance. Comparisons MUST retain compact identities, commands, raw timing samples, and explicit limitations under the repository's artifact/time policy.
- **FR-052**: Documentation, parity inventory, shortcuts, platform support, and desktop/assistive-tool verification status MUST reflect delivered behavior. Existing runtime-only use, engine scene semantics, no-simulation authoring mode, and existing correct build/run cancellation and data preservation MUST remain supported.

### Audit Coverage

Every audit finding and recommendation is within this feature; priorities sequence delivery rather than exempting later stories. Platform API selection and detailed notification mechanics remain planning decisions informed by the audit's recommended approach.

| Audit area | Requirements | Primary stories |
| --- | --- | --- |
| Non-sprite Scene representations, picking, framing, legacy cameras | FR-001–FR-002, FR-049 | 1 |
| W/E/R, snapping, pointer zoom, axes, rotation units, rectangle sizing | FR-003–FR-005 | 1 |
| Numeric history, stale/actionable validation, external scene conflicts | FR-005–FR-006, FR-015, FR-017 | 3 |
| Notifications, startup race, same-metadata writes, rename/partial-write behavior | FR-007–FR-011, FR-013 | 2 |
| Scan limits, overflow/watch exhaustion, fallback/manual refresh, symlinks | FR-012–FR-014, FR-050 | 2 |
| Whole-preview invalidation, repeated path checks, source polling | FR-007, FR-009, FR-018, FR-021 | 2, 4 |
| Idle redraws, synchronous/unbounded thumbnails, hierarchy passes, offscreen work | FR-018–FR-020, FR-024 | 4 |
| Durable writes per keystroke, recursive preview copies, unrelated descendant visits | FR-022–FR-023 | 4 |
| Python asset activation/typed paths, Console copying, Validate/settings shortcuts, scene title | FR-015–FR-016, FR-030, FR-033 | 3, 5, 6 |
| Search, Hierarchy shortcuts/reparent/reorder, multi-edit, visibility/pick locks | FR-025–FR-029 | 5 |
| Project/run onboarding, authoritative catalogs, detached windows, UI scale/accessibility | FR-031–FR-036 | 6 |
| Windows/macOS, platform I/O/processes, Unicode/build choices, packaging/resources | FR-037–FR-040 | 7 |
| Component copy/reset, subtree templates, explicit duplicate-reference policy | FR-041–FR-043 | 8 |
| Scene tabs, embedded play, pause/step, live inspection/update capabilities | FR-044–FR-047 | 9 |
| Sprite-sheet/animation authoring beyond existing asset thumbnails | FR-048 | 10 |
| Failing tests, true GUI performance evidence, desktop gaps, stale documentation | FR-049–FR-052 | All |

### Key Entities *(include if feature involves data)*

- **Workspace**: Project/asset roots, open documents, active scene, panel arrangements, display scale, editor-only visibility/picking choices, and run settings.
- **Authoring Document**: Scene or supported asset content, stable identities/references, current selection/view, independent undo history, saved baseline, and external-change/conflict state.
- **Selection and Interaction**: Ordered selected objects/components, common or mixed fields, active tool, coordinate mode, snapping settings, and one reversible editing transaction.
- **Asset Entry and Dependency**: Root-relative path, resolved containment, type, existence/validity, last usable preview, and scene/animation/reference consumers affected by change.
- **Change Batch and Coverage State**: Project ownership, affected paths/renames, pending reconciliation, completeness/failure status, and bounded recovery state.
- **Reusable Template**: Saved object assembly with components/children, internal reference relationships, external references, and per-instance identities.
- **Play Session**: Target document/game, build and capability status, owned execution, Game view, pause/step state, and runtime-only inspection values.
- **Diagnostic**: Severity, message, related document/object/component/field or source location, repeat count, and retained/displayed state.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every object/component category in the authoring fixture can be discovered, picked, framed, transformed, undone/redone, and saved/reopened with correct identity, dimensions, and reference semantics, including legacy cameras and collider-only objects.
- **SC-002**: Each completed numeric, viewport, group, component, hierarchy, and supported asset-edit interaction requires exactly one Undo; cancelled/unchanged interactions add zero entries.
- **SC-003**: On a supported local filesystem under normal load, new/changed assets appear within the existing three-second polling interval after the write settles, including the startup-overlap case. This is a project target derived from the audited polling interval, not a measured improvement claim.
- **SC-004**: Adding one unused asset refreshes the relevant folder with zero unrelated Scene-resource reloads, zero unrelated thumbnail reloads, zero scene-history changes, and no loss of an existing valid asset selection.
- **SC-005**: All monitoring failure cases in Story 2 produce visible incomplete-coverage status and preserve manual/visible-folder refresh; switching projects yields zero applied changes from the previous project.
- **SC-006**: Every committed blocking validation change is reflected within the same three-second refresh bound used for settled asset changes; explicit Validate reports current results and activating each issue reveals its authoring target. No stale validation result permits an invalid Run.
- **SC-007**: In focused equivalent trials, an unchanged static editor consumes less idle CPU time than the audited build beyond observed run-to-run variation; minimized/unfocused operation avoids continued full-rate authoring work while detecting new changes and remaining responsive.
- **SC-008**: Repeatedly browsing assets exceeding the declared preview budget keeps retained preview memory within that budget after pending work settles, and returning to an evicted asset restores a correct preview. Cold-browse input latency and large-scene interaction latency do not regress beyond measured variability from the audited baseline.
- **SC-009**: The 10,000-object and 128-level authoring fixtures complete the primary selection/edit/undo/frame workflows correctly without unrelated subtree retention growth; measured GUI results include panel/preview/event handling and report average and tail interaction latency.
- **SC-010**: Linux, Windows, and macOS each pass the installed-editor workflow in Story 7 from an arbitrary directory with Unicode/spaces paths; saves preserve previous valid data on injected failure and Stop leaves zero owned descendants running.
- **SC-011**: At 100%, 150%, and 200% scale, primary authoring flows remain operable with keyboard-only navigation, named controls, visible focus, reachable overflow, and monitor detach/restore. Platform assistive-tool checks and concrete limitations are recorded.
- **SC-012**: Users can complete search, reparent/reorder, multi-edit, asset assignment, and diagnostic navigation without manually editing scene files or knowing build protocol identifiers; every scenario in Stories 5 and 6 has a recorded successful walkthrough.
- **SC-013**: Repeated template instantiation and each duplication policy produce unique identities and exactly the declared internal/external reference targets; one Undo reverses each operation without disturbing the source assembly.
- **SC-014**: Two unsaved scenes retain independent selection/history/saved state across tab switches and play; Pause freezes simulation, each Step advances one simulation step, and stopping play saves zero runtime-only changes automatically.
- **SC-015**: An authored sprite region and multi-frame animation survive save/reopen and game consumption with matching regions, order, timing, and loop behavior; dependent previews update without unrelated reloads.
- **SC-016**: The two audited failing suites and all affected previously passing suites complete successfully after behavior-aligned verification, and every coverage row above has identifiable acceptance evidence or an explicit unresolved failure preventing feature completion.
- **SC-017**: Each performance experiment stays within the repository's default output/time limits and stops on budget/failure; evidence retains compact reports/identities/commands/raw timing samples and avoids full campaigns or bulky artifacts without explicit authorization.

## Assumptions

- "All findings" includes the audit's larger recommended workflows as required P3 scope. No story is deferred merely because it has lower priority. The feature is the native editor cleanup; the existing Python editor is a behavioral reference and need not be rewritten.
- Desktop scope is Linux, Windows, and macOS. The implementation plan must identify exact supported OS versions, build configurations, and physical test environments using project/dependency constraints, and report unavailable validation honestly.
- Scene navigation uses the current full-panel editing view with a separate game-camera boundary, preserving world aspect and coordinate correctness. Prior letterboxing test expectations must be aligned to this explicit choice rather than treated as a requirement to restore letterboxing.
- Existing duplication reference behavior remains the initial project default; users can explicitly choose remapping. Reparenting preserves world placement by default. Contained symlinks are supported consistently while root escape remains forbidden.
- Reusable templates cover stored subtree reuse and safe instantiation; automatic propagation, nested prefab variants, and full Unity prefab compatibility are outside this feature. The audit recommends templates before that larger system.
- Live inspection is read-only for this feature. Compatible live updates are capability-based, with controlled rebuild/restart for unsupported code changes; an arbitrary-code hot-reload system and runtime-to-authoring value application are outside scope.
- Sprite/animation authoring targets the engine's supported region and animation asset semantics. It does not require a new general asset format or unrelated game-engine functionality.
- Existing scene formats, behavior catalog authority, data preservation, asset-root containment, runtime-only independence, and external-window Run remain compatibility boundaries. Dependencies include the current engine's runtime, process, resource, and serialization capabilities; planning may extend integration where the required Game view and platform workflows need it.
- File notifications are advisory; initial discovery and bounded reconciliation/fallback remain necessary. Recommended platform backends, queue ownership, debounce values, memory budgets, and parallel/sequential choices belong in the plan, informed by the audit and project constitution.
- Performance targets beyond the existing three-second asset polling basis use measured comparative outcomes and bounded resource behavior rather than invented universal frame-time or memory constants. Measurements must obey the compact benchmark policy; broad historical campaigns are not authorized by this specification.
- The audit's 6/8 test result and potential startup race are starting evidence, not proof that every reported risk has been reproduced. Planning/implementation must confirm causes and record actual platform/GUI measurements before claiming fixes or speedups.
