# Feature Specification: Runtime Template Instantiation

**Feature Branch**: `017-runtime-template-instantiation`  
**Created**: 2026-11-15  
**Status**: Draft  
**Input**: User description: "/speckit-specify create a specification of adding templates to the platformator - as we previously discussed"

## Clarifications

### Session 2026-11-15

- Q: How should runtime instantiation behave when a saved `.pttemplate` contains an external reference (a link to an object or component outside the template subtree) authored in another scene? → A: Clear cross-scene external references to unset while still remapping all internal template references.
- Q: When spawning a template programmatically with an optional parent object or custom coordinates, how should the root object's initial placement behave? → A: Preserve template authored local position and rotation by default (and when only parent is given); when an explicit position/rotation is passed, place the spawned root in world space even if parented.
- Q: When a scene loads a behavior whose assigned template reference points to a missing or malformed `.pttemplate` file, when should that error take effect? → A: Fail fast when resolving references during scene load (consistent with existing media asset references), while empty/unset template fields remain valid unless instantiated.
- Q: Should a behavior inside a saved `.pttemplate` be allowed to hold a direct or mutual cyclic reference back to itself or another loaded template? → A: Yes; support self-referential and cyclic template asset links via lazy/path-backed resolution on spawn so loading never recurses infinitely or creates reference-retention cycles.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Reference and Spawn Saved Templates in Gameplay Scripts (Priority: P1)

A game developer creates a reusable entity (such as a projectile, coin, enemy, or multi-part hazard) as a saved object template asset, attaches a reference to that template on a gameplay behavior script (for example, on a player weapon, spawner, or level manager), and spawns live copies of it programmatically during play. Calling spawn creates a brand-new active object hierarchy registered in the running scene (optionally placed at a chosen world position, rotation, or under a chosen parent object), with all its visual, audio, animation, physics, collider, and script behaviors immediately ready and active. Meanwhile, the underlying template remains an inert blueprint: until instantiated, it does not appear in the scene, render on screen, trigger collisions, run scripts, or load redundant copies of textures, audio clips, or animation clips on every spawn.

**Why this priority**: Programmatic spawning of reusable templates (bullets, enemies, pickups, effects) is essential to almost every 2D game; without runtime template references and instantiation, developers must hand-construct complex component hierarchies in code on every spawn or keep hidden inactive copies cluttering every scene.

**Independent Test**: Can be fully tested by saving a `.pttemplate` asset containing a visual sprite, collider, rigidbody, and behavior script, assigning that template to a spawner behavior field in a test scene, triggering three spawns at different coordinates during play, and verifying that three independent live objects appear, simulate, render, and trigger startup callbacks while zero phantom prototype objects exist in the scene before or after spawning.

**Acceptance Scenarios**:

1. **Given** a gameplay behavior declares a template asset reference field and points to a valid saved template asset, **When** the scene loads and before any spawn is requested, **Then** the template blueprint and its referenced media assets (textures, audio, animation clips) are loaded once in an inert, unregistered state with zero entries added to live scene object lists, rendering queues, physics simulation, or script callbacks.
2. **Given** an active gameplay scene where a behavior holds a valid template reference, **When** the script invokes instantiation (with default placement, a specified position/rotation, or an optional parent object), **Then** a fresh copy of the template's root object, child hierarchy, components, and authored property values is registered into the live scene and begins simulating immediately.
3. **Given** multiple copies are spawned from the same template in the same or subsequent frames, **When** each copy moves, collides, or is destroyed independently, **Then** mutating or destroying one spawned copy leaves all other spawned copies and the underlying template blueprint completely unaffected, and saving/querying the live scene reflects only the registered live instances.

---

### User Story 2 - Remap Internal Object and Component References per Spawned Instance (Priority: P1)

A developer builds a composite template whose behaviors reference other objects or components inside the same template — for example, a turret referencing its child muzzle object, an enemy script referencing its own animator, audio source, camera, collider, or rigidbody, or a parent-child pair where sibling scripts point to each other. When that template is spawned once or many times at runtime, every internal object and component reference on each spawned instance automatically resolves to that instance's own newly created objects and components rather than pointing back to the inert blueprint or aliasing an earlier spawned copy.

**Why this priority**: Composite templates commonly wire internal object and component references together in the editor; if spawned instances still pointed to blueprint IDs or shared references across clones, any template more complex than a single self-contained object would silently break or corrupt sibling instances at runtime.

**Independent Test**: Can be tested by creating a template with a parent object, a child object, multiple typed components (such as animator, collider, audio, sprite, and script), and behavior reference fields targeting both the child object and sibling components; spawning two instances at runtime; and verifying that each instance's script references resolve cleanly to its own distinct child object and components.

**Acceptance Scenarios**:

1. **Given** a saved template containing internal object references and internal component references (including generic and subtype component references), **When** the template is instantiated at runtime, **Then** every spawned object and component receives a unique live scene identity and every internal reference on the spawned behaviors resolves to the corresponding cloned object or component in that same spawned hierarchy.
2. **Given** two instances spawned from the same composite template, **When** Instance A accesses or mutates an object or component through its behavior references, **Then** only Instance A's corresponding child object or component is affected and Instance B remains untouched.

---

### User Story 3 - Assign and Inspect Template Reference Fields in the Editor (Priority: P2)

While editing a scene or another object in the editor, a developer adds a template reference field to a behavior script and immediately sees it in the Inspector with an asset picker/drop target filtered to object template assets (`.pttemplate`). They can drag a `.pttemplate` file from the project assets browser or pick it from the field selector, clear it back to unset, save the scene or template containing that reference, and rely on project validation to warn them if the assigned template file is missing or invalid before running the game.

**Why this priority**: Runtime template instantiation is only ergonomic in a visual workflow if behavior template fields are discoverable by the editor, assignable like other assets in the Inspector, persisted cleanly in scene/template files, and validated against missing asset paths.

**Independent Test**: Can be tested by declaring a template reference field in a behavior header, opening a scene in the editor without building first, assigning a `.pttemplate` asset via the Inspector field control, saving and reloading the scene to confirm persistence, and verifying that removing the `.pttemplate` file surfaces a clear validation diagnostic rather than silent failure.

**Acceptance Scenarios**:

1. **Given** an unbuilt or built project with a behavior script declaring a template reference field, **When** a developer inspects an object carrying that behavior, **Then** the Inspector renders a template asset picker showing the currently linked `.pttemplate` path (or `None`), accepts drag-and-drop of `.pttemplate` assets, and allows selection or clearing.
2. **Given** a template reference field assigned in a scene or in another object template, **When** the user saves, closes, and reopens the document (or saves the object itself as a template), **Then** the template asset path persists accurately across round-trips.
3. **Given** a template reference field points to a nonexistent or corrupted template file, **When** project diagnostics run, **Then** the editor flags the broken reference clearly on that object/component without blocking unrelated editing.

---

### User Story 4 - Safe Spawning During Simulation, External Reference Handling, and Resource Cleanup (Priority: P2)

A developer spawns templates inside regular frame updates, fixed physics/step updates, collision or trigger callbacks, or destruction handlers, and occasionally spawns templates whose original editor source held external scene references from another level. Spawning mid-step integrates new instances cleanly without disrupting active update loops, collision pair tracking, or rendering; external references from unrelated scenes never accidentally bind to coincidental object IDs in the destination scene; and unloading scenes or exiting cleans up all dormant template prototypes and retained media resources cleanly.

**Why this priority**: Spawning projectiles on input, loot on enemy defeat, or explosions on collision contact are primary real-world triggers for instantiation, so mid-step registration safety, deterministic cross-scene reference behavior, and leak-free cleanup are essential for engine stability.

**Independent Test**: Can be tested by instantiating a template inside a collision callback while a simulation step is active, instantiating a template that carried an external binding from another scene into a scene where a matching numeric ID happens to exist, and loading/unloading a scene repeatedly under memory/lifecycle checks to verify clean resource release.

**Acceptance Scenarios**:

1. **Given** an active simulation step (including during a behavior update or collision callback), **When** a script instantiates a template, **Then** the newly created hierarchy registers safely without invalidating ongoing iteration, triggers startup lifecycle hooks appropriately, and participates in subsequent simulation, collision detection, and rendering without glitches.
2. **Given** a template authored with an external reference from another scene, **When** that template is instantiated at runtime in a scene that happens to contain an object or component with the same raw numeric ID, **Then** that external reference is left safely unset rather than silently binding to an unrelated object in the destination scene.
3. **Given** a loaded template prototype holding retained textures, audio clips, or animation clips, **When** no live objects or active template references continue referencing it (on scene unload or runtime shutdown), **Then** the prototype hierarchy and its retained media asset references are released cleanly with no leaked objects or dangling handles.

---

### Edge Cases

- What happens when a script attempts to instantiate an unset/empty template reference or a template asset path is missing or malformed? A non-empty template reference pointing to a missing or malformed `.pttemplate` file fails fast during scene/reference loading (matching other asset references), while an unset/empty template reference loads cleanly and rejects instantiation with a clear error rather than crashing or registering a partial hierarchy.
- What happens if a behavior inside a template itself holds a template reference field (for example, an enemy spawner template referencing a bullet template, or a splitting enemy referencing itself or forming a mutual template cycle)? Loading preserves nested template asset paths without eagerly spawning children, validates acyclic dependencies without infinite recursion, and resolves self-referential or cyclic template references lazily when a live instance spawns so zero reference-retention cycles remain on unload or shutdown.
- What happens when a template prototype contains parent-child transforms (non-zero child local positions/rotations/scales) and the caller supplies a custom spawn position, rotation, or parent? Without explicit coordinates (including when only a parent is specified), the spawned root preserves its template-authored local transform relative to the parent (or world root). When explicit position/rotation coordinates are supplied, the spawned root is placed in world space even if parented, while all child objects preserve their authored parent-relative local transforms, scales, and rotations.
- What happens if cloning or registering partway through instantiating a multi-object template fails (for example, missing asset or invalid state)? No partially constructed objects, components, or orphaned handles remain registered in the live scene.
- What happens when the user copies, duplicates, applies overrides, or reverts an object in the editor that carries a template reference field on one of its behaviors? Template reference paths are preserved as authored asset references alongside textures, audio, and animation clips without confusing them for scene object IDs.
- What happens when non-reference plain configuration fields vs runtime-only unserialized script members exist on a behavior inside a template? Authored serialized fields and asset references are preserved verbatim on every spawned copy; the prototype itself never executes startup or update callbacks, so spawned copies initialize fresh runtime execution when registered.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST provide a first-class reusable template asset type that gameplay behavior scripts can declare as a serialized reference field, inspectable and assignable by asset path just like existing media asset references.
- **FR-002**: Loading a template asset MUST construct an inert, unregistered prototype hierarchy representing the template's root object, child hierarchy, components, and authored behavior configuration without registering any prototype objects or components into live scene object lists, physics systems, collision broad-phase, rendering queues, or script callback schedules.
- **FR-003**: Loading a template asset MUST pre-retain referenced non-cyclic media assets (textures, audio clips, animation clips, and non-cyclic nested templates) on the prototype so repeated runtime instantiation does not re-read files or re-decode media from disk per spawn, while supporting self-referential and cyclic template-to-template links without infinite load recursion or resource retention cycles (resolving cyclic links when instantiated).
- **FR-004**: Unregistered prototype hierarchies owned by template assets MUST clean up all prototype root objects, child objects, components, attached behaviors, and retained asset references automatically when the template asset is unloaded or destroyed, without leaking memory, leaving cyclic refcount leaks, or dangling runtime handles.
- **FR-005**: System MUST support runtime instantiation from a loaded template reference (preserving template-authored local transforms by default and when parented alone, or placing the spawned root at explicit world coordinates and rotation even when an optional parent object is provided), returning a live reference to the newly registered root object.
- **FR-006**: Instantiating a template MUST produce a deep, independent clone of the prototype's object hierarchy, all attached components (including visual, audio, animation, camera, physics, collider, and script components), and all attached behaviors, isolating each spawned copy from both the prototype and all other spawned copies.
- **FR-007**: Each spawned object and component MUST receive fresh, unique runtime scene identities and live registration handles rather than reusing the prototype's local template IDs.
- **FR-008**: Instantiating a template MUST remap all internal object and component references declared within the template (both generic and subtype component references) so every spawned reference points to the corresponding cloned object or component within that newly spawned instance and carries its new live identity and handle.
- **FR-009**: Instantiating a template MUST NOT silently bind unresolved cross-scene external references baked into a `.pttemplate` file to coincidental matching numeric IDs in the active runtime scene; unresolved external bindings MUST be cleared to unset on spawned instances.
- **FR-010**: Newly instantiated hierarchies MUST support safe registration both during idle/initialization phases and mid-step during behavior updates or collision callbacks, executing normal script startup callbacks cleanly after internal references have been remapped.
- **FR-011**: System MUST fail fast with a clear error when resolving a non-empty template reference whose `.pttemplate` file is missing or malformed during scene or template loading, allow unset/empty template references to load cleanly until instantiated, reject instantiating an unset reference, and leave zero partial objects or components registered in the scene on any failure.
- **FR-012**: Behavior field discovery and script metadata generation MUST recognize template reference fields as a distinct asset-reference field kind so scenes and templates serialize, load, validate, copy, and propagate them accurately.
- **FR-013**: The editor Inspector and project diagnostics MUST support template reference fields by rendering a `.pttemplate`-filtered asset picker with drag-and-drop and clear controls, persisting the relative asset path, and reporting missing or invalid template asset targets.
- **FR-014**: Object template export, override tracking, revert, apply, and scene-to-template propagation in the editor MUST treat template reference fields as authored asset paths rather than scene object ID bindings.

### Key Entities

- **Template Asset**: An asset-backed blueprint loaded from a `.pttemplate` file that owns an inert, unregistered prototype object hierarchy, retained asset dependencies, and template reference-binding metadata (distinguishing internal bindings from external scene bindings).
- **Template Reference Field**: A behavior script property storing a link to a Template Asset by asset path in serialized documents and resolving to a loaded Template Asset at runtime.
- **Prototype Subtree**: The complete root-and-descendant hierarchy of unregistered objects, components, and behaviors owned exclusively by a loaded Template Asset, carrying template-local identities solely for cloning and internal reference remapping.
- **Spawned Instance**: A live, independent copy cloned from a Template Asset's prototype subtree and registered into the active scene runtime with fresh unique scene identities and remapped internal references.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Before instantiation occurs, 0 objects, components, colliders, render entries, or script callbacks from a loaded template asset appear in active scene counts, physics steps, rendered frames, or saved scene output.
- **SC-002**: 100% of internal object and typed component references across single-root and multi-child template hierarchies resolve to each spawned instance's own cloned targets across repeated spawns (verified across 10+ consecutive spawns in a scene).
- **SC-003**: Spawning 100 instances sequentially or during active update/collision steps completes with zero duplicate-ID failures, zero dangling-handle errors, zero missed startup callbacks, and zero cross-instance state leakage when any subset of clones is mutated or destroyed.
- **SC-004**: Developers can declare a template reference field in a behavior script, assign a `.pttemplate` asset in the editor Inspector without building first, save/reload the scene, and see the spawned object behave identically in play mode on the first run.
- **SC-005**: Unloading a scene or releasing all references to a loaded template asset frees 100% of prototype objects, components, behaviors, and prototype-retained asset references with zero leaked objects across repeated scene reloads.

## Assumptions

- Single-level template composition remains in effect for template authoring as defined in existing project specifications (saving a template from a scene object flattens nested template links into one template definition), while a behavior script inside a template may hold a reference field pointing to another `.pttemplate` asset — including itself or a mutual cycle — resolved cleanly without embedded child hierarchy cycles or retention leaks.
- When resolving nested template references inside a prototype on load, acyclic dependencies are preloaded cleanly while self-referential or cyclic template links remain path-backed on the dormant prototype and bind upon instance spawning.
- At runtime, instantiating a `.pttemplate` does not prompt for missing cross-scene external bindings; leaving cross-scene external bindings unset matches Unity's prefab instantiation model while preserving deterministic behavior across levels.
- Scene editing operations for objects carrying template reference fields build directly on existing template serialization and asset-reference handling in the editor.
