# C++ review and cleanup walkthrough

Written 24 October 2026 against the tree at `1f3ffa2` (`SDL fix`), with the startup-only reference-resolution and assets-only path follow-ups described below. This is a reading guide to the work performed during the review, cleanup, benchmark capture, and SDL follow-up. It explains the behavior changes and maps them to files so you can read the implementation without reconstructing the chat.

The engine already had a library target, public headers, components, scripts, serialization, rendering, audio, animation, a spatial grid, collision manifolds, an impulse solver, sleeping, and benchmark infrastructure before this work. Those systems are your existing engine; this cleanup repaired and reorganized them. It did not introduce a new ECS or replace the physics architecture.

## How to use this document

Read sections 1–5 first to understand object lifetime and scene operations. Sections 6–10 explain resource ownership, transforms, physics, and the simulation loop. Sections 11–15 cover using the library, validation, performance, and dependencies. The final inventory identifies the smaller changes, tests, deleted forwarding headers, and recorded artifacts.

Jump to:

- [Ownership](#1-one-owner-for-objects-hierarchy-links-describe-relationships), [IDs/handles](#2-file-ids-and-runtime-handles-have-separate-jobs), [lifecycle](#3-lifecycle-phases-starts-and-deferred-destruction).
- [Scene transitions](#4-scene-loading-is-prepare-then-replace), [resolvers and saving](#5-explicit-reference-binding-and-safer-scene-io).
- [Audio/Sprite/Animator/resources](#6-resource-ownership-audio-sprite-animator-and-backends), [transforms](#7-hierarchy-transforms-update-affected-objects).
- [Grid/colliders](#8-broad-phase-and-collider-changes-reconcile-desired-state), [physics](#9-contacts-parallel-support-accounting-geometry-and-sleeping), [step clock](#10-one-explicit-simulation-pipeline-separate-interactive-clock).
- [Paths/startup](#11-asset-paths-startup-headless-operation-and-command-line), [SDK/build](#12-sdkpublic-headers-cmake-presets-and-ci), [tests](#13-tests-what-was-added-versus-repaired).
- [Performance](#14-benchmark-changes-and-what-performance-actually-means), [SDL and unresolved findings](#15-sdl-follow-up-leak-investigations-and-current-limits), [file inventory](#17-file-by-file-inventory), [exact diff commands](#18-looking-up-an-exact-diff).

Useful implementation reading order:

1. [Public Runtime API](../include/platformator/runtime.h).
2. [BaseObject and Asset](../include/platformator/baseobject.h), [ObjectHandle](../include/platformator/objecthandle.h), and [ObjectRegistry](../src/objectregistry.cpp).
3. [ObjectReference](../include/platformator/objectreference.h).
4. [GameManager declarations](../src/gamemanager.h), then `runSimulationStepWithCustomCallback`, `commitMutations`, `reconcilePending`, and `drainRetirements` in [gamemanager.cpp](../src/gamemanager.cpp).
5. [SceneBuildContext](../src/scenebuilder.cpp).
6. [GameObject](../src/gameobject.cpp) and [ScriptComponent](../src/scriptcomponent.cpp).
7. [PhysicsManager](../src/physicsmanager.cpp), [physicscontacts.cpp](../src/physicscontacts.cpp), [physicsbodyindexes.cpp](../src/physicsbodyindexes.cpp), and [physicsgeometry.cpp](../src/physicsgeometry.cpp).

For just the API changes, use [CPP_API_MIGRATION.md](CPP_API_MIGRATION.md). For historical commands/results, use [validation.md](../specs/001-cpp-engine-cleanup/validation.md). This walkthrough does not replace their execution evidence.

## Change boundaries and attribution

| Point in history | Meaning |
| --- | --- |
| `e01526b` | Original reviewed implementation and pre-cleanup performance reference. |
| `9b5a5f6`, `4d5bc02` | Review/planning/tooling and baseline records before the main implementation. These are not additional engine implementations. |
| `7906959` (`clanker fix`) | Main C++ cleanup, tests, SDK fixes, benchmark tooling and corrected reference. |
| `9c4f3f6` (`vendored updated`) | Your replacement of the vendored SDL libraries. The upstream library changes are not changes authored by this review. |
| `0b9c3e4` (`logs`) | Integration paths, truthful runtime dependency metadata, update testing, benchmark captures and diagnostics. |
| `1f3ffa2` (`SDL fix`) | Restoration of the local WAV decoder arithmetic patch and its successful sanitizer follow-up. |
| `162d3ec` (`removed referencesPrepared`) | Follow-up unifying behavior reference binding immediately before first start. |
| Current assets-only follow-up | Removed containing-file-relative loading and directory-aware resolvers; see §11 and the follow-up inventory. |

On 24 October, you chose a simpler initialization model: all behaviors resolve references immediately before their first start. The separate scene-loading resolution pass and `referencesPrepared` flag were removed. Missing behavior references now fail after scene commit, and inactive behaviors defer that failure until activation. The original 95-task records and captured benchmarks predate this amendment. You then chose assets-only resource paths: every nonempty asset path starts with assets/, and containing-scene/animation directory context is no longer supported.

The detailed behavioral explanation below covers the main cleanup plus those follow-ups. Formatting and public-include normalization account for some large-looking diffs; a changed file does not automatically imply a rewritten algorithm.

## 1. One owner for objects; hierarchy links describe relationships

**Before:** registered objects were tracked in a flat collection, but destruction could still follow parent/child pointers in an order that inspected already-deleted children. Creating a child before its parent exposed this during shutdown.

**After:** the runtime owns registered objects in one flat collection with stable heap addresses. Parent/child links are borrowed relationships. Retirement detaches relationships before allocations are released. Destroying a subtree retires each descendant once, independent of allocation order. Destructors release storage instead of using user callbacks as part of recursive neighbor deletion.

This is deliberately not a conversion of every pointer into `shared_ptr`. Internal pointers are useful while the engine controls their lifetime. The ownership rule, rather than a smart-pointer spelling alone, is what makes those pointers safe.

**Files:** `src/gamemanager.{h,cpp}`, `include/platformator/gameobject.h`, `src/gameobject.cpp`.

## 2. File IDs and runtime handles have separate jobs

### File IDs

`BaseObject::id` identifies an object/component in serialized scene data. IDs are nonnegative, including zero, and unique across the scene's objects and components. Newly created objects receive IDs through the manager; loaded IDs advance that scene's allocation counter. Duplicate/negative IDs and exhausted ID space produce errors at creation/load boundaries.

The old process-wide atomic ID generator inside `BaseObject` was removed. An ID is not proof that an object is still alive, and the same saved ID can legitimately appear in another scene. Registered IDs cannot be changed underneath the lookup map. Copying `BaseObject` is disabled so a copy cannot silently duplicate identity.

### Handles

`ObjectHandle` contains a registry slot, generation, and runtime serial:

```cpp
platformator::ObjectHandle handle; // slot + generation + runtimeSerial
```

The slot gives indexed access. The generation distinguishes successive objects using the same slot. The runtime serial distinguishes registries across runtime destruction/recreation. Serial assignment uses an atomic counter once per registry, not once per frame. Free slots are reused; a slot whose generation has exhausted its range is not recycled.

Registry entries also record concrete object kind and publication state. Resolving a `Collider` can accept a box or circle; resolving a `BoxCollider` must not return a circle. Normal lookups cannot expose staged scene objects or retired objects.

For example:

```text
Enemy A: slot 7, generation 1, runtime 1
A is destroyed; slot generation advances.
Enemy B: slot 7, generation 2, runtime 1
An old reference to A does not resolve to B.
```

An assigned handle is not necessarily a live handle: `if (handle)` only checks its explicit invalid-slot representation. Registry resolution checks liveness. Handles are identifiers, not owners; holding one does not prevent deletion. Handles are not saved in JSON.

### ObjectReference

`ObjectReference<T>` no longer caches a potentially dangling object pointer. It stores a handle plus an optional file ID for the unbound/loading state. `get()` performs typed registry resolution. No active runtime, retired objects, old generations, and a different runtime all yield an empty reference.

```cpp
ObjectReference<GameObject> target(enemy);

// Later, at the point where an optional target is needed:
if (auto *object = target.get())
    object->getPosition();
```

An unbound file ID does not resolve merely by being read: explicit binding is required. Once bound, expiration does not fall back to an ID lookup and accidentally select an object from a replacement scene. Dereference operators assert that a target is live; they do not make unconditionally dereferencing an optional reference valid.

Saving an expired bound reference emits null. Saving an unbound non-null reference is an error, as is saving a live reference to an object outside the graph being saved. Existing valid integer reference JSON remains supported.

**Cost:** handle storage, registry storage, and constant-time validation when a persistent reference is resolved. Internal physics/dispatch loops retain borrowed pointers where their lifetime is controlled; they were not converted into repeated handle lookups.

**Files:** `baseobject.h`, `objecthandle.h`, `objectreference.h`, `src/objectregistry.{h,cpp}`, `src/referencehelpers.cpp`, `src/referenceserialization.h`.

## 3. Lifecycle, phases, starts, and deferred destruction

### An object can be retired before its memory is freed

`ObjectLifecycle` names the states:

| State | Meaning |
| --- | --- |
| `Staged` | Constructed/prepared, not published for ordinary lookups. |
| `Live` | Published for runtime use. |
| `PendingRetire` | Logically destroyed; ordinary handles stop resolving, allocation retained. |
| `DestroyCallbacks` | Required destruction callbacks run before associated storage is freed. |
| `Unregistered` | Final unregister/reclamation transition. |
| `Freed` | Identity has been erased, immediately before allocation release. |

Not every object takes the full path: failed staging can reclaim an unpublished object directly. `Freed` does not let callers read a deleted object; it is a pre-deletion bookkeeping state. `isRetired()` relies on the enum ordering from `PendingRetire` onward.

### The manager tracks what operation is executing

The manager has phases for idle operation, starting behaviors, the custom step hook, fixed update, physics, collision dispatch, update, animation, late update, mutation commit, destruction callbacks, rendering, saving and shutdown. `PhaseGuard` restores the preceding phase on scope exit, including exceptions.

Structural mutation is checked at its shared entry boundaries: it must run on the owning main thread and in an allowed phase. Workers do not acquire general permission to mutate the scene. Nested simulation steps are rejected instead of recursively running the engine from cleanup.

### Stable dispatch and indexed queues

Previously, a callback could grow or swap-remove the collection currently being iterated. That caused invalid iterators, skipped objects, or surviving scripts updating twice.

The manager now separates requested state from system membership. It uses reusable start, reconciliation, and retirement buffers with queued indexes/bits so the same item is not repeatedly enqueued. Dense dispatch vectors change at reconciliation boundaries. Disabling/retiring an item suppresses later eligible callbacks immediately; physical removal happens safely afterward. Off/on before the barrier reconciles the final desired state.

Every attached behavior enters a pending list. The global script queue selects components with pending work. Startup iterates each queued component's pending vector in insertion order, including eligible additions made during startup. Each entry gets one resolve/start attempt. After processing the script, the attempted prefix transfers together into its runnable behaviors vector, including entries whose initialization errors were logged. Eligible additions during startup can start in the same pass; additions from later phases wait for the next startup pass, even on an already active ScriptComponent. Ordinary callbacks iterate the runnable list without a per-behavior started flag. Inactive creation does not queue startup; activation queues only pending work. Reactivation does not restart runnable behaviors. Retiring a ScriptComponent suppresses its remaining behaviors during the current dispatch.

There are just two owning vectors: pendingBehaviors and behaviors (runnable entries whose initialization was attempted). getBehaviors exposes runnable entries; getPendingBehaviors exposes the pending vector; `getBehavior<T>()` searches both. Serialization, owner updates and component destruction inspect both lists. There is no separate runnableBehaviors vector. Each script component has one index for deduplication and O(1) cancellation from the global script startup queue.

Resolve/start exceptions are logged per behavior so later entries can initialize. Failed entries remain owned and become runnable; only normal teardown invokes onDestroy and deletes them. Startup clears pending once after transferring attempted entries. If the script becomes inactive or retired during startup, only the processed prefix transfers, preserving its unattempted suffix. Behavior has no pending membership index. ScriptComponent retains one index for O(1) cancellation from the global script queue. Cancellation marks a queue entry empty so callbacks can append/cancel without moving the drain position. Eligible additions during startup join the current pass.

### Destruction callback rules

All required `onDestroy` callbacks in a retirement batch complete before the batch's component/object allocations are freed. Attached components remain readable. A removed old component can remain allocated even though its slot has already been detached or replaced.

`onDestroy` is read-only with respect to structural mutation: creating/destroying, attaching/removing, reparenting, loading/clearing, or saving there throws. Retirement is terminal and duplicate destruction requests are idempotent. Callback exceptions are remembered while required cleanup proceeds; they are reported afterward.

Collision retirement follows the same lifetime rule: an eligible surviving recipient can receive its final exit while the retired other collider is still allocated. Both-retired pairs do not receive callbacks. Retired/inactive recipients do not receive ordinary callbacks.

**Cost:** indexes and bookkeeping at changes. No whole-scene lifetime sweep or copy of every active list was added per callback phase. Detailed lifecycle labels partly support diagnostics, but immediate retirement and delayed freeing enforce release-build behavior.

**Files:** `src/gamemanager.{h,cpp}`, `src/gameobject.cpp`, `src/scriptcomponent.cpp`, `src/animator.cpp`, `src/colliderpair.cpp`, associated public headers.

## 4. Scene loading is prepare-then-replace

The original failure you reported was scene operations from scripts. The implementation now has a specific request/commit model.

```cpp
// During an ordinary behavior callback:
getRuntime().requestLoadScene("scenes/next.scene");
// The callback can return normally before the current scene is retired.
```

The path above is illustrative; use an actual scene path from your project.

1. A load/clear request occupies one pending slot. Last request wins.
2. At the end-of-step barrier, the manager latches that slot once.
3. Only the selected load path is opened and prepared.
4. `SceneBuildContext` constructs a private flat graph, validates IDs/components/behavior data, and loads component resources. Behavior reference fields remain serialized IDs/paths; their resolver does not execute here.
5. If preparation fails, staging is reclaimed and the old scene/path remains committed.
6. On success, the old scene retires, required exits/destruction callbacks finish, and the prepared objects/ID map are published.
7. On the following eligible step, each new behavior resolves references against the published scene immediately before its first start. Inactive behaviors wait until activation.

Requests produced by retirement exits during commit belong to a later step; commit does not recursively consume requests forever. Superseded paths are not loaded unnecessarily.

`Runtime::loadScene` replaces the scene rather than appending. At idle, load/clear can complete synchronously; during supported callbacks the operation is deferred. The explicit request methods make that intention clear.

`clearScene()` leaves a genuinely empty scene and clears the current save path, with no camera. A successfully loaded camera-less scene gets a fallback camera. `Scene::loadScene()` returns borrowed roots owned by the runtime, not a second ownership tree.

Deferred load errors surface at the host's step/run boundary. Catch around the step, not just around the request inside a script:

```cpp
try
{
    runtime.simulateFrame(1.0 / 120.0);
}
catch (const std::exception &error)
{
    // Report the failure at your host boundary.
}
```

Preparation failure preserves the old scene; it does not undo arbitrary script-side changes already made earlier in that step or promise rollback after irreversible commit work. Behavior reference errors are startup errors after commit: the old scene is gone, earlier behaviors may already have started, and the replacement remains committed. The host can clear/load another scene or replace the failed behavior for a fresh attempt; simply advancing another step does not retry it.

**Files:** `src/scenebuilder.{h,cpp}`, `src/gamemanager.cpp`, `src/runtime.cpp`, `src/scene.cpp`, `src/gameobjectserializer.cpp`.

## 5. Explicit reference binding and safer scene I/O

### Resolver migration

Behavior deserialization reads stored values; reference binding receives an explicit context:

```cpp
void resolveReferences(platformator::ReferenceResolver &resolver) override
{
    resolver.resolve(target);
    resolver.resolve(texture);
}
```

Registration macros propagate this resolver. Handwritten resolvers must adopt the signature. Every behavior, whether loaded or created in code, receives the active resolver immediately before its first start. All asset references use explicit assets/ paths beneath the configured root. Non-null behavior asset paths remain required. A shared resolver is created once for the startup batch. Directory-aware adapters and resolver directory state have been removed; resource requests go directly to the common asset loader for one path resolution. The staging resolver remains internal to component resource construction and its acquisition ledger.

Deserializing behavior fields stores IDs/paths rather than resolving against the old active scene. Unknown behavior names or malformed serialized fields reject preparation. Missing non-null targets, incorrect object/component subtypes, missing behavior assets, and exceptions from a handwritten resolver fail at startup before that behavior starts. Initialization is not retried on reactivation, including after a logged failure. Resolve/start exceptions are logged per behavior and the startup pass continues. Attempted entries become runnable, including failures, and remain owned until normal teardown. A resolver exception skips start for that attempt. Initialization is not retried automatically and arbitrary user-side effects are not rolled back. There is no `referencesPrepared` flag or alternate loaded-behavior initialization path.

### Resource rollback

Staging records acquired component resources. Failed preparation releases those acquisitions and removes newly inserted unused resources while preserving preexisting resources still used by the live scene. It does not sweep every cache to guess what should be deleted.

### Saving

Saving requires an idle runtime. Serialization first validates that references belong to the graph being saved, then writes an exclusively created sibling temporary file, checks writes and close, and renames it over the destination. Failure preserves the prior destination where the operation has not committed and cleans up temporary state. The runtime updates the current scene path only after success. A newly loaded behavior with an unresolved non-null object ID cannot serialize that reference as bound; start it or explicitly bind the reference before saving. Inactive behavior references can therefore keep a save invalid until resolved.

This is safe file replacement, not a claim of crash/power-loss durability through `fsync` or identical behavior on every untested filesystem/platform.

Ownerless JSON construction of GameObject graphs is rejected. Creation/loading goes through a runtime so ownership, registration, and reference binding agree.

**Files:** public `referenceresolver.h`, `behavior.h`, `scriptregistration.h`; `src/referenceresolver.{h,cpp}`, `src/referencehelpers.cpp`, `src/referenceserialization.h`, `src/scenecomponentio.h`, scene/component serializers and `src/scene.cpp`.

## 6. Resource ownership: Audio, Sprite, Animator, and backends

### Audio

`AudioWrapper *audioWrapper` is a shared resource managed by the engine's existing manual reference count. `Audio` owns one playback track:

```cpp
std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)>
    track{nullptr, MIX_DestroyTrack};
```

The deleter tells `unique_ptr` to call SDL's destroy function rather than `delete`. Track creation is consolidated in the common constructor; `.get()` borrows the pointer for SDL calls. Member destruction releases the track even if later construction throws. It introduces no separate smart-pointer allocation or shared reference-count control block; this function-pointer deleter typically stores an extra pointer.

The constructor starts `audioWrapper` as null, then binds through `setAudio`. Previously, initializing the member to the incoming pointer made the setter's equality check skip both reference acquisition and binding. Replacement now retains the incoming resource, asks SDL to bind it, and releases the old resource after success. A failed bind releases the attempted acquisition and reports the error.

### Sprite

The texture-taking constructor similarly starts its member empty before calling the setter. This fixes a skipped retain when the setter sees what looks like an already-installed texture. Width/height derivation and the existing rendering model remain.

### Animator

The active animation clip is retained, the old clip is released after acquiring its replacement, and destruction releases the current clip. `play()` uses the common clip setter instead of bypassing its ownership rules.

Clip-end callbacks now live in stable individually owned callable storage. Dispatch latches the callback count and indexes that storage: adding callbacks cannot relocate the callable currently executing, and additions are not unexpectedly executed in the same batch. Eligibility checks stop further callback/frame advancement after retirement or disabling. This trades an allocation when registering each callback for stable execution without copying the callback collection every frame.

### AssetReference and caches

AssetReference copy/move/set/release behavior follows runtime-owned resource lifetime. Assets record their owning runtime serial. Debug assertions diagnose references that outlive that runtime; assets do not have the same generational safety as ObjectReference, and callers must keep asset references within runtime lifetime.

Resource caches and SDL backend acquisitions use explicit RAII ownership. Shutdown destroys components and cached resources before their SDL backends. These changes preserve the manual asset-retain model rather than introducing atomic shared ownership throughout the engine.

Several animation/audio/texture implementation files changed only through include normalization or formatting; they were not all rewritten resource systems. Animation serializers additionally route resource loading through the supplied resolver.

**Files:** public `assetreference.h`, `audio.h`, `animator.h`; `src/audio.cpp`, `src/sprite.cpp`, `src/animator.cpp`, resource serializers, `src/gamemanager.{h,cpp}`, `src/sdlwindow.{h,cpp}`.

## 7. Hierarchy transforms update affected objects

World-position reads previously relied on ancestor traversal, and physics could leave a collider-only child stale when its parent moved. Parallel movement also risked reading parent state while another worker changed it.

The manager now maintains cached world translation, dirty-transform queues, hierarchy order/depth metadata, and traversal stamps. Hierarchy order is rebuilt after structural hierarchy changes; dirty subtrees are published parent-first, visiting affected descendants once. Immediate public position reads still synchronize the needed state; callers do not manually repair colliders after parent motion.

Physics workers integrate their own body's local transform and collect changed objects in thread-local storage. Main-thread publication merges those changes in hierarchy order before subsequent consumers. Pair processing and rendering use synchronized caches. Camera rendering reads cached sprite/camera positions instead of repeatedly walking ancestry.

Reparenting validates ownership/cycles and preserves world translation. This is not a new general-purpose scene-graph transform system promising inherited rotation/scale semantics beyond the implemented contract.

The audit also removed duplicate parent-position work in `GameObject::setPosition` by reusing the computed local value.

**Files:** `include/platformator/gameobject.h`, `src/gameobject.cpp`, `src/gamemanager.cpp`, `src/rigidbody.cpp`, `src/collider.cpp`, `src/camera.cpp`.

## 8. Broad phase and collider changes reconcile desired state

The spatial grid and segmented interval/sweep infrastructure remain your existing design. The cleanup changes how updates reach them.

Old ADD/SYNC/REMOVE flags could contradict one another after off/on or filter changes. Colliders now track current/desired membership plus geometry, filtering, and response/body dirty state. One indexed queued entry reconciles the final state at a barrier. Retirement retains the allocation until queued grid/pair work no longer uses it.

Filter, trigger, and body changes invalidate affected bindings/cells/adjacency rather than treating only physical movement as important. Re-enabling unchanged overlaps can restore them, and temporary binding removal/reinsertion combines net witness changes so a continuously permitted contact does not manufacture exit/enter events.

Idle work avoids unnecessary grid/TLS setup. No whole-scene "repair all colliders" loop was introduced to make these cases pass.

**Files:** public `collider.h`; `src/collider.cpp`, `src/grid.{h,cpp}`, `src/gridcell.h`, `src/colliderpair.{h,cpp}`, `src/physicsmanager.{h,cpp}`. Some adjacent AABB/projection files only changed public includes/formatting; see the inventory.

## 9. Contacts, parallel support accounting, geometry, and sleeping

### Pair-local work; serial shared-body updates

Previously, parallel pair processing could increment/decrement the same Rigidbody's support count through Collision methods. Pair tasks are not independent if they write shared body state.

Workers now produce pair-owned manifold/support results. Canonical pair identities use object handles rather than allocation-address or task-completion ordering. The main thread reduces old/new support transitions onto affected bodies, applying net deltas and waking a body when its last support disappears. Collision lifetime methods no longer hide shared Rigidbody counter mutations.

### Persistent contact/solver/event collections

The engine maintains separate indexed collections for confirmed contacts and solver-eligible contacts. Membership uses insertion and swap-removal; solver membership is canonically ordered when changed. Resolution/debug consumers read the collision directly through solver pairs, without a second cached projection. Body-state changes refresh affected solver adjacency; script changes do not visit physical adjacency.

Enter and Exit are generated from confirmed-contact transitions after geometry workers finish, independently of scripts. Unchanged stationary/sleeping contacts generate no recurring event. Explicit pair removal queues Exit before erasure; pair destructors only free contact memory. Support and event transitions remain correct through filtering, body replacement, deactivation and retirement. Each delivery batch orders Exit before Enter and canonical endpoints; recipient eligibility is checked at delivery.

An early corrected benchmark exposed an unnecessary full sort of the general contact-membership collection whenever anything changed. At 133,007 contacts this was expensive and its ordering had no consumer. That sort was removed; solver and observed-event lists retain their own required canonical ordering. Incomplete preliminary captures were excluded, not mixed into accepted results.

### Geometry and manifold fixes

- Circle/box uses closest-point geometry, including outside corners, inside cases and deterministic ties. The old approximation could accept a circle outside a box corner.
- Circle/circle work uses its exact geometry; collider offsets orient normals consistently.
- Box axes use fixed-size storage instead of allocating temporary normal vectors per pair.
- SAT face selection is stable enough for persistent feature/contact identity.
- Two-contact box manifolds use a coupled normal solve rather than treating both normal constraints as wholly independent; solver friction/block data is retained where useful.
- A benchmark validation case exposed a tiny separating gap rounded to touching by single-precision SAT. Clipping produced zero points, but the old acceptance still published contact/support. A zero-point clipped candidate is now rejected through normal reconciliation. A specific two-box regression preserves the input.

### Input validation and sleeping

Positive finite scale/shape dimensions, positive finite dynamic mass, nonnegative finite friction, finite restitution in [0,1], and finite transform inputs are enforced at supported setters/construction/load boundaries. Static/kinematic zero inverse mass remains intentional. These checks are not repeated for every pair in the solver.

Support transitions, wake handling, stable contacts and coordinated sleeping of connected bodies restore the original tall-stack/wide-pile behavior without relaxing their limits. The last-support-removal wake test was restored against the current API.

Awake dynamic, kinematic, and sleep-eligible body collections have maintained membership indexes. Changes update affected bodies; integration avoids traversing irrelevant sleeping/static bodies solely to skip them. This is not a claim that all physics work is O(number of awake bodies): contact maintenance and actual constraints still cost work.

**Files:** `src/physicsmanager.{h,cpp}`, new `src/physicscontacts.cpp`, `src/physicsgeometry.cpp`, `src/physicsbodyindexes.cpp`, `src/collision.cpp`, `src/colliderpair.{h,cpp}`, `src/rigidbody.cpp`, shape implementations, `src/inputvalidation.h` and related public headers.

## 10. One explicit simulation pipeline; separate interactive clock

The step implementation has one ordered path:

```text
start eligible behaviors
custom step hook
fixedUpdate
physics (transform publication, collision/solver/movement work)
collision callbacks
update
animation
lateUpdate
transient-audio cleanup + mutation commit/reconciliation/retirement
end complete-step timing
optional rendering in the rendered wrapper
```

If ordinary work throws, the manager still attempts required cleanup, restores phase state, then reports the first error. Wrapper methods no longer implement independent competing lifecycle pipelines. The legacy custom-callback entry points execute their hook in the documented step location, despite older parameter naming suggesting rendering.

`simulateFrame(dt)` means exactly one step using the caller's finite positive dt. It is not an accumulator wrapper.

Interactive `run()` uses `SimulationClock`: 1/120-second fixed steps, maximum eight catch-up steps per render iteration, fractional remainder retained, excess whole ticks counted as dropped. Pause clears accumulation; manual advance while paused performs one tick. Resume does not inject the paused elapsed interval as catch-up. Rendered wrappers stop on quit rather than claiming an attempted step executed.

**Files:** `src/simulationclock.h`, `src/constants.h`, `src/gamemanager.cpp`, `src/runtime.cpp`, `src/sdlwindow.cpp`, `tests/runtime_step_test.cpp`.

## 11. Asset paths, startup, headless operation, and command line

`RuntimeOptions::assetRoot` and `--asset-root` establish one startup-normalized root. An explicit relative root is resolved against startup cwd. Defaults check executable/assets and executable-parent/assets. Every nonempty asset path uses the literal assets/ prefix. Paths preserve filename case and normalize backslashes to forward slashes. Bare paths, absolute asset filenames, containing-file-relative paths, parent traversal and symlink escapes reject. For example, assets/textures/Player.PNG always maps to <assetRoot>/textures/Player.PNG, even when the configured root is named Data rather than assets. There is no scene/animation directory context or implicit prefix insertion.

The previous upward/demo-directory searching heuristics were removed. Missing required scene resources reject preparation. Supported optional loads diagnose and use intentional packaged fallbacks. Changing cwd later does not redefine the root. A scene path itself can be absolute or startup-relative.

Headless/physics-only configuration skips audio and video device acquisition. It does not promise that audio/texture resources requiring those backends can still be loaded. Desktop initialization uses SDL3's actual success conventions and exceptions to report required startup failures. Acquisition and teardown are exception-safe.

The runner accepts a positive `--smoke-steps N` and exits after finite explicit ticks. CLI boundaries turn startup/load/save/configuration errors into a useful nonzero process status. The stock runner and Mario are exercised from unrelated working directories.

Fallback `assets/audio/missing.wav` and `assets/animations/missing.animset` were added; the animation uses the existing missing texture. Core installation includes fallbacks without depending on a Mario build.

**Files:** public `runtimeoptions.h`; `src/runtimeoptions.cpp`, `src/runtime.cpp`, `src/runner.cpp`, `src/pathmanager.{h,cpp}`, `src/runtimeaccess.{h,cpp}`, `src/sdlwindow.{h,cpp}`, fallback assets and Mario CMake.

## 12. SDK/public headers, CMake, presets, and CI

The existing library packaging was repaired rather than introduced from scratch.

- Consumers link the exported `Platformator::platformator` target and include `<platformator/...>`.
- Public headers compile individually, catching missing transitive standard includes such as the original BaseObject issue.
- Engine source includes were normalized to public headers. Tiny duplicate private forwarding headers and unused generated detail forwarders were removed; real private algorithm headers remain private.
- The package config uses supported dependency exports/find_dependency rather than maintaining a handwritten list of machine-specific imported library files.
- Core-only, source add_subdirectory, installed, relocated, and Mario consumers were checked from outside the repository. RPATH/configuration and asset-copy rules were adjusted for those cases.
- Vendor tests/examples are disabled for ordinary engine builds. This removes unbuilt dependency tests from project CTest runs; it does not hide engine failures. Stale vendor test-registration handling supports reused build directories.
- Project tests use one helper for target setup, labels, and dummy-driver environment. Internal algorithm tests opt into private include access; public-host tests do not need it.
- Presets separate normal Debug, ASan/UBSan, TSan, work probes and Release benchmark configurations. Sanitizer flags instrument dependencies as needed; TSan uses instrumented oneTBB. TBB warnings remain visible, with its warnings-as-errors disabled for GCC's sanitizer warning rather than suppressing runtime races.
- Public-header check files use `file(GENERATE)` so unchanged checks do not get rewritten and force needless recompilation on configure.
- `.github/workflows/cpp.yml` adds normal/sanitizer/work/benchmark-build and package jobs plus focused physics TSan validation. Adding the workflow is not evidence that every remote runner/platform has passed it.

The unfilled constitution template was not adopted as project policy. Skill/workflow installation files are tooling, not runtime dependencies.

## 13. Tests: what was added versus repaired

The existing lightweight test approach remains. New tests exercise public contracts and independent work/correctness oracles; no external test framework was introduced.

| Test | What it establishes |
| --- | --- |
| `runtime_foundation_test.cpp` | Stable dispatch, retirement, ownership order, identity, resource retention and base runtime behavior. |
| `scene_transition_contract_test.cpp` | Idle/deferred load and clear, last-wins requests, errors, starts and retirement-time requests. |
| `scene_transition_stress_test.cpp` | At least 1,000 transitions across supported callback families, expired references and callback traces. |
| `callback_mutation_test.cpp` | Script growth, self-removal/replacement, destroy-another, activation and once-per-step behavior. |
| `destruction_contract_test.cpp` | Readable attached state, callback ordering/errors and forbidden teardown mutation. |
| `object_reference_contract_test.cpp` | Type checking, unbound/bound/expired states, slot/runtime reuse and reference serialization. |
| `scene_io_contract_test.cpp` | Invalid IDs/graphs, failed component preparation, post-commit behavior-reference failures and cleanup, save failure and preservation. |
| `transform_hierarchy_test.cpp` | Parent/child motion, collider-only descendants, reparenting and automatic synchronization. |
| `physics_support_test.cpp` | Canonical serial/worker support and event behavior, including stationary contacts and invalidation. |
| `physics_geometry_test.cpp` | Independent shape/parameter checks and the empty-clipped-manifold regression. |
| `runtime_step_test.cpp` | Step order, deterministic clock partitions, pause/manual/catch-up and invalid dt. |
| `runtime_work_test.cpp` | Counted warmed-idle C++ allocations and affected transform/body work, in a dedicated instrumented build. |
| `asset_root_contract_test.cpp` | Root selection, filename case, required/fallback resources, canonical serialization, rejection of unsupported paths/symlink escapes and nested-animation assets. |
| `runner_failure_test.cpp` | Startup/headless/CLI failure and finite-run behavior. |
| `benchmark_contract_test.cpp` | Complete-step includes destruction/cleanup, separate scopes, metadata and reserved storage. |
| `package_smoke.cmake`, `consumer/` | Independent source/installed/relocated SDK and Mario consumption. |
| `benchmark/tests/test_sweep_scaling.py` | Schema validation, parameter propagation, independent repetitions and invalid-output rejection. |

Existing tests were repaired too. Deletion helpers now capture IDs while objects are alive instead of dereferencing freed pointers to check success. The collision-exit expectation remains meaningful. Roundtrip/resolver assertions were migrated to the explicit API and invalid-ID policy. The parent-transform regression no longer manually calls a repair that masks the engine bug. Tall-stack/wide-pile and last-support-removal checks remain real physics requirements. Existing runtime-recreation coverage was reused; its source was not rewritten merely to add a new file.

`tests/test_support.h` centralizes fixtures/traces; `tests/runtime_work_probe.h` and compiled-out engine hooks expose narrowly scoped work counters. A passing allocation oracle is not a universal claim that arbitrary gameplay makes no allocations.

## 14. Benchmark changes and what performance actually means

### Measurement changes

The previous internal frame timer omitted post-step collider/grid work and deferred destruction. New schema-v3 `complete_step` includes the final retirement/transient cleanup. Rendering has its own scope; physics, cleanup, scene-transition and lower-level scopes have documented nesting. Nested times must not all be added as if disjoint.

Sample storage is reserved before measurement. Once-per-run manifests record source commit/dirty state/fingerprint, compiler/flags, build/instrumentation, machine/OS, linked dependencies, scenario/seed/workers/dt/warmup/step/repetition settings and timing boundaries. CLI worker control is configured once per process. Quit stops samples for work that did not happen.

Python benchmark tooling—not the GUI—now validates the schema and required metrics, propagates seed/workers/repetitions, preserves raw per-run CSV/manifests, and reports run-to-run spread. Missing metrics no longer silently become zero. `capture_cleanup.py` orchestrates the full matrix, CPU affinity, source snapshot/checksums, validation and timing archives.

Expensive physical invariants run in separate labeled validation processes. Each accepted full matrix has 180 timing processes (five repetitions for 36 conditions) and 36 validation processes. Each timing process uses 120 warmup and 600 measured steps, dt=1/120. Corrected serial/multiworker final states agree within declared tolerances; IDs/support/sleep/features match exactly. This establishes the tested fixtures, not bitwise determinism for every possible game on every CPU.

### Before cleanup versus after cleanup

The following is historical context, not a controlled speedup claim. Original numbers are medians of five outer-host-step means; cleaned numbers are means of five complete-step means. Physics, setup, lifetime work and timing boundaries changed.

| Largest workload | Original 1 worker, ms | Cleaned 1 worker, ms | Original 6 workers, ms | Cleaned 6 workers, ms |
| --- | ---: | ---: | ---: | ---: |
| Broad lanes, 1,000 | 0.563 | 0.608 | 0.216 | 0.208 |
| Dense broad boxes, 300 | 2.654 | 2.653 | 2.549 | 2.479 |
| Narrow elements, 800 actual objects | 50.389 | 34.114 | 17.808 | 17.365 |
| Rigid container, 1,000 bodies + walls | 1.745 | 1.513 | 1.260 | 1.167 |
| Static churn, 1,000 replacements | 2.727 | 2.735 | 1.138 | 1.401 |
| Moving churn, 1,000 replacements | 3.358 | 2.845 | 1.323 | 1.437 |

Narrow/rigid single-worker captures look better; broad phase is similar; heavy churn carries extra lifetime/reference/reconciliation work and its six-worker numbers are higher. The churn difference was investigated: required affected-object cleanup is a significant cost, without a whole-scene repair fallback. No universal speedup percentage is justified. Original captures also had invalid support counts and divergent final states, so they did not consistently simulate equivalent physical work.

The very dense narrow case still exceeds an 8.33 ms/120 Hz budget with six workers. These are headless stress results, not hardware rendering/audio throughput measurements. Godot monitor timings remain separate because equivalent timing boundaries were not established.

### Old SDL versus updated SDL on the cleaned engine

Largest-case historical changes ranged from roughly -3.2% to +5.9%. A supplementary interleaved runtime-library control investigated static churn: old 2.838 ± 0.208 ms, updated 2.888 ± 0.153 ms, about +1.76% with overlapping ranges. Host activity/frequency/temperature were not fully controlled; no clearly isolated library slowdown was established. All 36 updated physics snapshots agreed with the cleaned reference.

Reports and complete raw records:

- [Original baseline](../benchmark/baselines/pre-cleanup-e01526b/RESULTS.md).
- [Corrected cleanup reference](../benchmark/results/cpp-cleanup/RESULTS.md).
- [SDL update reference](../benchmark/results/sdl-update-9c4f3f6/RESULTS.md).

The WAV patch was restored after the updated reference. Those archives retain their actual source fingerprints; they were not rewritten to pretend they measured the later patch.

## 15. SDL follow-up, leak investigations, and current limits

Your vendor import changed SDL 3.4.2 → 3.4.18, mixer 3.2.0 → 3.2.4, image 3.4.0 → 3.4.8, and ttf 3.1.0 → 3.2.2. Integration edits updated CMake paths and the benchmark source-archive/fingerprint input. Benchmark dependency metadata now calls the linked libraries' version functions rather than repeating stale version literals. The upstream library implementation changes are yours to import, not this review's authored cleanup.

### WAV local patch

The earlier mixer 3.2.0 local fix was overwritten by the new unpatched mixer import. Valid negative 24-bit PCM samples reproduced signed-left-shift undefined behavior in `FetchPCM24LE`:

```cpp
// Original high-byte term:
(Sint32)(Sint8)x[2] << 16

// Restored local term:
(Sint32)(Sint8)x[2] * 65536
```

65536 is 2^16, placing the signed high byte above the lower two bytes. Multiplication fits in Sint32 for every possible signed high byte, while C does not define left-shifting a negative signed value. The updated file marks this as a local alteration. It adds no allocation, branch, or per-frame work. Decode performance was not separately benchmarked.

After restoration, the strict non-package ASan/UBSan suite passed 21 executed tests with two intentional skips and leak detection disabled. See [the full follow-up log](sdl-update-evidence/wav-fix-asan-tests.txt.gz).

### Findings that remain unresolved

- Leak-enabled suites reported 8,200 bytes/164 allocations across 14 processes through FluidSynth module initialization. Standalone SDL/mixer init/quit reproduced 200 bytes/four allocations per cycle without engine code. No engine-owned leak path was reported in those scans; this is not a universal proof of leak freedom. No leak suppression or removal of engine MIDI support was used to make the gate green.
- Physics-focused TSan tests passed with instrumented TBB. Broader desktop tests still reported an SDL_mixer output-format data race and system DBus lock-order warnings. The latter are diagnostics to investigate, not proof of an actual normal-build deadlock or a newly introduced regression.
- LeakSanitizer cannot run under this environment's sandbox/ptrace conditions. Running the local tests outside the sandbox allowed real scans; automatic review approved those executions without a user prompt.
- Linux/GCC, dummy/software drivers and package consumers were exercised. Other operating systems and hardware renderer/audio throughput remain unverified.

### Standalone upstream reproduction

After you cloned upstream under `TMP/SDL_mixer`, the investigation used upstream's unchanged `test/testaudiodecoder.c`, system SDL 3.4.16 and UBSan-only builds of development commit `ccb4670fe5bd6da3c9acb9132f77062fdd39425d` and release 3.2.4. Locally generated 24-bit WAV files containing -1 samples failed; matching +1 and zero controls passed on both versions.

`TMP/wav-repro/` held a README, three WAV inputs, six result logs, configure/build records, linked-library output, results JSON, checksums, a source export, two build directories, a tar.gz evidence bundle and later `negative-24bit.zip` for GitHub attachment. The upstream clone's sources were not edited. These were temporary local artifacts; `TMP/` is no longer present in the tree inspected for this guide. No issue/comment/PR was submitted by the agent.

For current recorded dependency findings, use [SDL_UPDATE_VALIDATION.md](SDL_UPDATE_VALIDATION.md). Statements in older benchmark archives about then-blocked leak scans or the then-unpatched WAV decoder describe their capture time, not the latest follow-up.

## 16. What to keep in mind while reading the code

- Persistent script references use handles; ordinary short-lived borrowed pointers are still intentional.
- Retirement, publication and physical deletion are different operations. Their distinction is required for callback mutation safety; some additional enum detail is primarily diagnostic.
- Boundary validation is intentional. The cleanup did not add null checks to every solver operation or a global validity pass each frame.
- Indexed queues/membership metadata make common idle and affected-object work cheap, but increase bookkeeping and make some headers larger. The added code is not all necessary every frame.
- Resource references remain bound to runtime lifetime. The debug serial check is a diagnostic precondition, not an asset handle system.
- New guarantees have costs. Particularly heavy churn should be profiled against the corrected reference rather than by removing required invalidation/callback work.
- The Python GUI did not receive a broad review or rewrite. Python changes here concern benchmark orchestration and validation.
- The task list has 95 completed work items, many of which are tests, integration, documentation or acceptance—not 95 new runtime subsystems.

## 17. File-by-file inventory

The inventory below covers the first-party changes from the reviewed commit through the local WAV follow-up. Rows distinguish behavioral changes from include/formatting maintenance. Recorded run files and tooling installations are grouped separately because they are artifacts, not individual algorithms. Deleted private forwarding headers are listed explicitly; public counterparts remain.

Status: **A** = added; **M** = modified; **D** = deleted. Paths compare `e01526b` to `1f3ffa2` and do not imply every change was authored in one operation.

### Build, entry documentation and example integration

| Status | File | Change |
| --- | --- | --- |
| M | [.github/copilot-instructions.md](../.github/copilot-instructions.md) | Updated repository guidance to match lifecycle, public API, performance and validation contracts; not runtime code. |
| A | [.github/workflows/cpp.yml](../.github/workflows/cpp.yml) | Added C++ configuration/test matrix, SDK smoke coverage and focused instrumented physics TSan job (§12). |
| M | [.gitignore](../.gitignore) | Ignored local build/editor/Python/environment artifacts so generated files do not become source changes. |
| M | [CMakeLists.txt](../CMakeLists.txt) | Integrated dependency-test controls, sanitizer/work/header modules, public library exports, tests/assets/Mario and benchmark fingerprinting; later updated the four SDL paths (§§11–15). |
| M | [CMakePresets.json](../CMakePresets.json) | Added separate cleanup Debug, ASan/UBSan, TSan, work-probe and Release benchmark configurations (§12). |
| A | [CODEBASE_REVIEW.md](../CODEBASE_REVIEW.md) | Original review and reproduced failure evidence at e01526b; historical findings, not current failure claims. |
| M | [README.md](../README.md) | Updated build/use instructions and links to migration, evidence and SDL follow-up. |
| M | [cmake/PlatformatorConfig.cmake.in](../cmake/PlatformatorConfig.cmake.in) | Replaced brittle handwritten imported dependency locations with exported package dependency discovery (§12). |
| A | [cmake/PlatformatorPublicHeaders.cmake](../cmake/PlatformatorPublicHeaders.cmake) | Added one-translation-unit-per-public-header compilation using timestamp-preserving generation (§12). |
| A | [cmake/PlatformatorSanitizers.cmake](../cmake/PlatformatorSanitizers.cmake) | Centralized supported instrumentation configuration and dependency/TBB handling (§12). |
| A | [cmake/PlatformatorTests.cmake](../cmake/PlatformatorTests.cmake) | Centralized project test creation, labels, dummy-driver environment and optional internal includes (§12). |
| A | [cmake/VendorPatches.md](../cmake/VendorPatches.md) | Recorded the local WAV arithmetic patch and TBB compiler-warning policy; tracked overwrite/restoration after the update (§15). |
| M | [examples/mario/CMakeLists.txt](../examples/mario/CMakeLists.txt) | Fixed independent source/installed example dependency discovery, asset packaging and installed-prefix/relocation behavior (§§11–12). The example main program was already present; it was not rewritten in this diff. |

### Public API headers

| Status | File | Change |
| --- | --- | --- |
| M | [include/platformator/animator.h](../include/platformator/animator.h) | Stable owned clip-end callback storage and associated declarations; formatting (§6). |
| M | [include/platformator/assetreference.h](../include/platformator/assetreference.h) | Explicit resolver binding, retained-resource copy/move/set behavior and owning-runtime diagnostics (§§5–6). |
| M | [include/platformator/audio.h](../include/platformator/audio.h) | Changed the playback track member to unique_ptr with SDL custom deleter; public include/formatting cleanup (§6). |
| M | [include/platformator/baseobject.h](../include/platformator/baseobject.h) | Separated file ID from registry handle/lifecycle, disabled identity copying, restricted ID edits, and added asset runtime ownership serial and required includes (§§2–3,6). |
| M | [include/platformator/behavior.h](../include/platformator/behavior.h) | Added the explicit resolver signature; later removed reference-preparation/started flags. Removed the behavior startup flags and membership indices; global startup membership belongs to ScriptComponent (§§3,5). |
| M | [include/platformator/boxcollider.h](../include/platformator/boxcollider.h) | Exposed fixed-size box axes and replaced dynamic normal-vector storage (§9). |
| M | [include/platformator/collider.h](../include/platformator/collider.h) | Added cached position, desired grid membership, dirty reasons, indexed queue membership and response invalidation; removed contradictory queued-operation flags (§§7–9). |
| M | [include/platformator/collision.h](../include/platformator/collision.h) | Added cached two-contact normal matrices, block-solve flag and solver friction; related support behavior changes are implemented in Collision/PhysicsManager source (§9). |
| M | [include/platformator/gameobject.h](../include/platformator/gameobject.h) | Added lifecycle/system reconciliation metadata, hierarchy/world-position caches, dirty/index state and controlled-manager access (§§1,3,7). |
| A | [include/platformator/objecthandle.h](../include/platformator/objecthandle.h) | Added slot/generation/runtime identity and supported typed-object-kind definitions (§2). |
| M | [include/platformator/objectreference.h](../include/platformator/objectreference.h) | Replaced cached raw-pointer identity with checked handle access, explicit binding and scene-aware serialization (§2). |
| A | [include/platformator/referenceresolver.h](../include/platformator/referenceresolver.h) | Added the public context interface used to bind object and asset references (§5). |
| M | [include/platformator/rigidbody.h](../include/platformator/rigidbody.h) | Added affected-body membership/support/sleep bookkeeping and physics friendship; updated solver-facing declarations (§9). |
| M | [include/platformator/runtime.h](../include/platformator/runtime.h) | Exposed scene request, checked identity and asset-root APIs; kept the public host boundary for stepping/scene errors (§§2,4,10–11). |
| M | [include/platformator/runtimeoptions.h](../include/platformator/runtimeoptions.h) | Added asset-root and finite smoke-run configuration (§11). |
| M | [include/platformator/scene.h](../include/platformator/scene.h) | Aligned scene/path/loading declarations with runtime ownership and validated save behavior (§§4–5). |
| M | [include/platformator/scriptcomponent.h](../include/platformator/scriptcomponent.h) | Added manager access, dispatch eligibility and destruction bookkeeping; the follow-up uses only pending/runnable owning lists and a global script startup queue (§3). |
| M | [include/platformator/scriptregistration.h](../include/platformator/scriptregistration.h) | Forwarded explicit resolvers through registered fields and containers; validated missing required references with field context (§5). |

### Engine implementation and private helpers

| Status | File | Change |
| --- | --- | --- |
| M | [src/aabb.h](../src/aabb.h) | Normalized public includes and formatting around the existing broad-phase/helper algorithms; no independent algorithm rewrite in this file. |
| M | [src/animationclip.cpp](../src/animationclip.cpp) | Public-include normalization and formatting; existing AnimationFrame copy/move texture ownership remains (§6). |
| M | [src/animationclipserializer.cpp](../src/animationclipserializer.cpp) | Added resolver-aware frame/clip deserialization and resource acquisition; retained active-runtime adapters (§§5–6). |
| M | [src/animator.cpp](../src/animator.cpp) | Retained/released active clips, unified clip assignment, made callback storage/dispatch mutation-safe and stopped ineligible advancement (§6). |
| M | [src/animatorserializer.cpp](../src/animatorserializer.cpp) | Resolved serialized clip resources through the explicit supplied context (§5). |
| M | [src/audio.cpp](../src/audio.cpp) | Consolidated track construction, fixed skipped initial retain/bind, added exception-safe track ownership and retain-before-release replacement (§6). |
| M | [src/audioserializer.cpp](../src/audioserializer.cpp) | Routed audio resource deserialization through explicit resolver context (§5). |
| M | [src/audiowrapper.cpp](../src/audiowrapper.cpp) | Public-include normalization and formatting; these files do not themselves implement a new reference-counting system (§6). |
| M | [src/audiowrapperserializer.cpp](../src/audiowrapperserializer.cpp) | Switched to public header includes; no new wrapper JSON format. |
| M | [src/behavior.cpp](../src/behavior.cpp) | Normalized includes to public declarations; main lifecycle/factory behavior changes are in the manager, builder and headers. |
| M | [src/behaviorfactoryregistry.cpp](../src/behaviorfactoryregistry.cpp) | Normalized includes to public declarations; main lifecycle/factory behavior changes are in the manager, builder and headers. |
| M | [src/benchmark.cpp](../src/benchmark.cpp) | Implemented complete-step/nested scopes, reserved samples, schema-v3 output/manifests and later actual runtime SDL version reporting (§14). |
| M | [src/benchmark.h](../src/benchmark.h) | Declared corrected scopes/counters/recorder controls and compiled-out instrumentation interface (§14). |
| M | [src/benchmark_main.cpp](../src/benchmark_main.cpp) | Added worker/seed control, validation fixtures and full snapshots, workload metadata, and correct quit/sample handling; substantial formatting too (§14). |
| A | [src/benchmarkbuildconfig.h.in](../src/benchmarkbuildconfig.h.in) | Added generated benchmark build/source identity metadata (§14). |
| M | [src/boundingradiousprojection.cpp](../src/boundingradiousprojection.cpp) | Normalized public collider includes and formatting; existing projection comparisons and interval structure are preserved. |
| M | [src/boundingradiousprojectionproxy.cpp](../src/boundingradiousprojectionproxy.cpp) | Normalized public collider includes and formatting; existing projection comparisons and interval structure are preserved. |
| M | [src/boundingradiusprojectionaxis.cpp](../src/boundingradiusprojectionaxis.cpp) | Normalized public collider includes and formatting; existing projection comparisons and interval structure are preserved. |
| M | [src/boxcollider.cpp](../src/boxcollider.cpp) | Validated box dimensions and used fixed-size axes in geometry preparation (§9). |
| M | [src/boxcolliderserializer.cpp](../src/boxcolliderserializer.cpp) | Replaced deleted private includes with public collider/body and shared JSON helper declarations; setters enforce the new numeric boundaries (§§5,9). |
| M | [src/camera.cpp](../src/camera.cpp) | Used synchronized cached camera/sprite positions for rendering, avoiding repeated ancestor-position queries; formatting (§7). |
| M | [src/cameraserializer.cpp](../src/cameraserializer.cpp) | Public include and JSON formatting maintenance; no separate camera scene-format redesign. |
| M | [src/circlecollider.cpp](../src/circlecollider.cpp) | Validated radius and integrated cached-position geometry preparation (§§7,9). |
| M | [src/circlecolliderserializer.cpp](../src/circlecolliderserializer.cpp) | Replaced deleted private includes with public collider/body and shared JSON helper declarations; setters enforce the new numeric boundaries (§§5,9). |
| M | [src/collider.cpp](../src/collider.cpp) | Implemented dirty-reason/desired-state queuing, cached synchronization and response/filter invalidation (§§7–9). |
| M | [src/colliderpair.cpp](../src/colliderpair.cpp) | Added stable canonical identity and persistent pair membership/support/event state; retirement respects allocation lifetime (§§3,9). |
| M | [src/colliderpair.h](../src/colliderpair.h) | Added stable canonical identity and persistent pair membership/support/event state; retirement respects allocation lifetime (§§3,9). |
| M | [src/collision.cpp](../src/collision.cpp) | Removed implicit shared Rigidbody support writes from collision lifecycle; retained pair-local support/manifold state (§9). |
| M | [src/component.cpp](../src/component.cpp) | Normalized includes to public declarations; main lifecycle/factory behavior changes are in the manager, builder and headers. |
| M | [src/constants.h](../src/constants.h) | Declared the 1/120 fixed timestep and eight-step interactive catch-up cap (§10). |
| M | [src/debugdraw.h](../src/debugdraw.h) | Public include normalization and formatting, including compiled-out no-op definitions; no new debug rendering subsystem. |
| M | [src/gamemanager.cpp](../src/gamemanager.cpp) | Integrated flat ownership, phase/thread boundaries, stable dispatch, indexed lifecycle/transform queues, transactional scene commit, identity/resources, final step pipeline and shutdown (§§1–11). |
| M | [src/gamemanager.h](../src/gamemanager.h) | Integrated flat ownership, phase/thread boundaries, stable dispatch, indexed lifecycle/transform queues, transactional scene commit, identity/resources, final step pipeline and shutdown (§§1–11). |
| M | [src/gameobject.cpp](../src/gameobject.cpp) | Reworked registration/destruction/component mutations/reparenting and dirty transform propagation; retained borrowed getters (§§1,3,7). |
| M | [src/gameobjectserializer.cpp](../src/gameobjectserializer.cpp) | Serialized the owned hierarchy and restricted ownerless deserialization; staging now owns graph construction (§§4–5). |
| M | [src/grid.cpp](../src/grid.cpp) | Reconciled desired collider state, affected bindings/pairs and incremental witness/event work; skipped unnecessary idle setup (§8). |
| M | [src/grid.h](../src/grid.h) | Reconciled desired collider state, affected bindings/pairs and incremental witness/event work; skipped unnecessary idle setup (§8). |
| M | [src/gridcell.h](../src/gridcell.h) | Normalized public includes and formatting around the existing broad-phase/helper algorithms; no independent algorithm rewrite in this file. |
| M | [src/helpers.h](../src/helpers.h) | Normalized public includes; the scheduling follow-up delegates typed behavior lookup to ScriptComponent so it searches pending and runnable entries. |
| A | [src/inputvalidation.h](../src/inputvalidation.h) | Added shared finite/positive/nonnegative numeric boundary helpers (§9). |
| M | [src/localsortarray.h](../src/localsortarray.h) | Normalized public includes and formatting around the existing broad-phase/helper algorithms; no independent algorithm rewrite in this file. |
| A | [src/objectregistry.cpp](../src/objectregistry.cpp) | Added reusable typed slots, runtime serial, generations, staged/live visibility and invalidation (§2). |
| A | [src/objectregistry.h](../src/objectregistry.h) | Added reusable typed slots, runtime serial, generations, staged/live visibility and invalidation (§2). |
| M | [src/pathmanager.cpp](../src/pathmanager.cpp) | Replaced heuristic upward/demo search with startup-rooted, case-preserving asset resolution; later restricted all asset inputs to assets/ paths (§11). |
| M | [src/pathmanager.h](../src/pathmanager.h) | Replaced heuristic upward/demo search with startup-rooted, case-preserving asset resolution; later restricted all asset inputs to assets/ paths (§11). |
| A | [src/physicsbodyindexes.cpp](../src/physicsbodyindexes.cpp) | Added affected-only queued membership for awake dynamics, kinematics and sleep-eligible bodies (§9). |
| A | [src/physicscontacts.cpp](../src/physicscontacts.cpp) | Physical contact/solver indexes, support-delta reduction and transition-only event generation; removed observer tracking and duplicate solver projection (§9). |
| A | [src/physicsgeometry.cpp](../src/physicsgeometry.cpp) | Separated exact circle/box and circle/circle manifold generation and deterministic geometric decisions (§9). |
| M | [src/physicsmanager.cpp](../src/physicsmanager.cpp) | Integrated pair-local parallel results, synchronized movement, deterministic solver/events, coupled box solve, sleeping and indexed body/contact work (§§7–10). |
| M | [src/physicsmanager.h](../src/physicsmanager.h) | Integrated pair-local parallel results, synchronized movement, deterministic solver/events, coupled box solve, sleeping and indexed body/contact work (§§7–10). |
| M | [src/referencehelpers.cpp](../src/referencehelpers.cpp) | Added scene-graph serialization scope and typed active reference validation (§§2,5). |
| A | [src/referenceresolver.cpp](../src/referenceresolver.cpp) | Implemented active-runtime resolution; the later assets-only amendment removed directory adapters/context and forwards required loads directly to the common loader (§5). |
| A | [src/referenceresolver.h](../src/referenceresolver.h) | Implemented active-runtime resolution; the later assets-only amendment removed directory adapters/context and forwards required loads directly to the common loader (§5). |
| A | [src/referenceserialization.h](../src/referenceserialization.h) | Declared scoped membership validation for the graph being saved (§5). |
| M | [src/rigidbody.cpp](../src/rigidbody.cpp) | Validated body inputs, updated affected-body membership/support/wake transitions, and integrated local movement under coordinated publication (§§7,9). |
| M | [src/rigidbodyserializer.cpp](../src/rigidbodyserializer.cpp) | Replaced deleted private includes with public collider/body and shared JSON helper declarations; setters enforce the new numeric boundaries (§§5,9). |
| M | [src/runner.cpp](../src/runner.cpp) | Reported startup/runtime failures at the CLI boundary with nonzero status (§11). |
| M | [src/runtime.cpp](../src/runtime.cpp) | Implemented the public scene request/error boundary, configured roots, unified stepping/finite runs and ordered runtime ownership (§§4–5,10–11). |
| M | [src/runtimeaccess.cpp](../src/runtimeaccess.cpp) | Aligned internal access helpers with runtime-owned state and explicit resource requirements; removed obsolete ownerless loading routes (§§5,11). |
| M | [src/runtimeaccess.h](../src/runtimeaccess.h) | Aligned internal access helpers with runtime-owned state and explicit resource requirements; removed obsolete ownerless loading routes (§§5,11). |
| M | [src/runtimeoptions.cpp](../src/runtimeoptions.cpp) | Parsed asset-root and positive smoke-step options and normalized option failure handling (§11). |
| M | [src/scene.cpp](../src/scene.cpp) | Delegated graph loading to the runtime, returned borrowed roots and implemented idle validated temporary-file save/replace (§§4–5). |
| A | [src/scenebuilder.cpp](../src/scenebuilder.cpp) | Added staged graph validation/construction, private handles and component-resource rollback before publication; behavior binding now occurs only at startup (§§4–5). |
| A | [src/scenebuilder.h](../src/scenebuilder.h) | Added staged graph validation/construction, private handles and component-resource rollback before publication; behavior binding now occurs only at startup (§§4–5). |
| A | [src/scenecomponentio.h](../src/scenecomponentio.h) | Declared component/resource deserialization overloads that accept an explicit resolver (§5). |
| M | [src/scriptcomponent.cpp](../src/scriptcomponent.cpp) | Owns pending/runnable behaviors, dispatches runnable entries directly and exposes both collections for lookup/serialization/teardown (§3). |
| M | [src/scriptcomponentserializer.cpp](../src/scriptcomponentserializer.cpp) | Adapted behavior-field deserialization to the explicit resolver; serialization includes both pending and runnable entries after the scheduling cleanup (§§3,5). |
| M | [src/sdlwindow.cpp](../src/sdlwindow.cpp) | Made backend acquisition/headless startup/teardown ownership explicit, kept transient audio cleanup safe and allowed empty-scene rendering (§§6,10–11). |
| M | [src/sdlwindow.h](../src/sdlwindow.h) | Made backend acquisition/headless startup/teardown ownership explicit, kept transient audio cleanup safe and allowed empty-scene rendering (§§6,10–11). |
| A | [src/simulationclock.h](../src/simulationclock.h) | Added deterministic accumulator, pause/manual-step, catch-up limit/remainder and dropped-tick accounting (§10). |
| M | [src/sprite.cpp](../src/sprite.cpp) | Fixed initial texture ownership by starting empty before binding through the retaining setter (§6). |
| M | [src/spriteserializer.cpp](../src/spriteserializer.cpp) | Loaded texture references through the supplied staged/active resolver (§5). |
| M | [src/texturewrapper.cpp](../src/texturewrapper.cpp) | Public-include normalization and formatting; these files do not themselves implement a new reference-counting system (§6). |
| M | [src/texturewrapperserializer.cpp](../src/texturewrapperserializer.cpp) | Switched to public header includes; no new wrapper JSON format. |
| A | [src/workprobe.h](../src/workprobe.h) | Added narrow test-only affected-work hooks compiled out of normal performance builds (§13). |

### Removed private forwarding headers

| Status | File | Change |
| --- | --- | --- |
| D | `src/animationclip.h` | Removed tiny private forwarding header; [include/platformator/animationclip.h](../include/platformator/animationclip.h) remains the public declaration (§12). |
| D | `src/animator.h` | Removed tiny private forwarding header; [include/platformator/animator.h](../include/platformator/animator.h) remains the public declaration (§12). |
| D | `src/assetreference.h` | Removed tiny private forwarding header; [include/platformator/assetreference.h](../include/platformator/assetreference.h) remains the public declaration (§12). |
| D | `src/audio.h` | Removed tiny private forwarding header; [include/platformator/audio.h](../include/platformator/audio.h) remains the public declaration (§12). |
| D | `src/audiowrapper.h` | Removed tiny private forwarding header; [include/platformator/audiowrapper.h](../include/platformator/audiowrapper.h) remains the public declaration (§12). |
| D | `src/baseobject.h` | Removed tiny private forwarding header; [include/platformator/baseobject.h](../include/platformator/baseobject.h) remains the public declaration (§12). |
| D | `src/behavior.h` | Removed tiny private forwarding header; [include/platformator/behavior.h](../include/platformator/behavior.h) remains the public declaration (§12). |
| D | `src/boxcollider.h` | Removed tiny private forwarding header; [include/platformator/boxcollider.h](../include/platformator/boxcollider.h) remains the public declaration (§12). |
| D | `src/camera.h` | Removed tiny private forwarding header; [include/platformator/camera.h](../include/platformator/camera.h) remains the public declaration (§12). |
| D | `src/circlecollider.h` | Removed tiny private forwarding header; [include/platformator/circlecollider.h](../include/platformator/circlecollider.h) remains the public declaration (§12). |
| D | `src/collider.h` | Removed tiny private forwarding header; [include/platformator/collider.h](../include/platformator/collider.h) remains the public declaration (§12). |
| D | `src/debugsettings.h` | Removed tiny private forwarding header; [include/platformator/debugsettings.h](../include/platformator/debugsettings.h) remains the public declaration (§12). |
| D | `src/gameobject.h` | Removed tiny private forwarding header; [include/platformator/gameobject.h](../include/platformator/gameobject.h) remains the public declaration (§12). |
| D | `src/gridtypes.h` | Removed tiny private forwarding header; [include/platformator/gridtypes.h](../include/platformator/gridtypes.h) remains the public declaration (§12). |
| D | `src/jsonhelpers.h` | Removed duplicate private forwarding header; source serializers include the shared public scriptregistration JSON helpers directly (§12). |
| D | `src/objectreference.h` | Removed tiny private forwarding header; [include/platformator/objectreference.h](../include/platformator/objectreference.h) remains the public declaration (§12). |
| D | `src/rigidbody.h` | Removed tiny private forwarding header; [include/platformator/rigidbody.h](../include/platformator/rigidbody.h) remains the public declaration (§12). |
| D | `src/runtimeoptions.h` | Removed tiny private forwarding header; [include/platformator/runtimeoptions.h](../include/platformator/runtimeoptions.h) remains the public declaration (§12). |
| D | `src/scene.h` | Removed tiny private forwarding header; [include/platformator/scene.h](../include/platformator/scene.h) remains the public declaration (§12). |
| D | `src/scriptcomponent.h` | Removed tiny private forwarding header; [include/platformator/scriptcomponent.h](../include/platformator/scriptcomponent.h) remains the public declaration (§12). |
| D | `src/sprite.h` | Removed tiny private forwarding header; [include/platformator/sprite.h](../include/platformator/sprite.h) remains the public declaration (§12). |
| D | `src/texturewrapper.h` | Removed tiny private forwarding header; [include/platformator/texturewrapper.h](../include/platformator/texturewrapper.h) remains the public declaration (§12). |
| D | `src/windowsettings.h` | Removed tiny private forwarding header; [include/platformator/windowsettings.h](../include/platformator/windowsettings.h) remains the public declaration (§12). |

### Tests and independent consumer

| Status | File | Change |
| --- | --- | --- |
| A | [tests/asset_root_contract_test.cpp](../tests/asset_root_contract_test.cpp) | Asset root/path/required-resource contracts, unsupported-path rejection, symlink containment and canonical serialization (§13). |
| A | [tests/benchmark_contract_test.cpp](../tests/benchmark_contract_test.cpp) | Complete-step, cleanup, metadata and sample-storage contracts (§13). |
| A | [tests/callback_mutation_test.cpp](../tests/callback_mutation_test.cpp) | Stable callback creation/removal/replacement and activation traces (§13). |
| M | [tests/collider_pair_cleanup_test.cpp](../tests/collider_pair_cleanup_test.cpp) | Existing pair cleanup coverage migrated to public includes and formatting; not a replacement physics oracle (§13). |
| A | [tests/consumer/CMakeLists.txt](../tests/consumer/CMakeLists.txt) | Added an independent public-header-only SDK consumer used by package smoke (§12). |
| A | [tests/consumer/main.cpp](../tests/consumer/main.cpp) | Added an independent public-header-only SDK consumer used by package smoke (§12). |
| A | [tests/destruction_contract_test.cpp](../tests/destruction_contract_test.cpp) | Readable teardown, once-only callbacks and prohibited structural mutations (§13). |
| A | [tests/object_reference_contract_test.cpp](../tests/object_reference_contract_test.cpp) | Typed references, expiration, slot/runtime reuse and serialization (§13). |
| A | [tests/package_smoke.cmake](../tests/package_smoke.cmake) | Built/installed/relocated the SDK and source/installed Mario consumers outside the source cwd (§12). |
| A | [tests/physics_geometry_test.cpp](../tests/physics_geometry_test.cpp) | Independent exact-shape, numeric-boundary and empty-manifold regression checks (§13). |
| A | [tests/physics_support_test.cpp](../tests/physics_support_test.cpp) | Support transitions, filters/body changes, stationary events and worker equivalence (§13). |
| M | [tests/queued_deletion_safety_test.cpp](../tests/queued_deletion_safety_test.cpp) | Repaired helper use-after-free by capturing IDs while alive; preserved eligible-exit assertion and updated includes (§13). |
| M | [tests/regression_tests.cpp](../tests/regression_tests.cpp) | Repaired stale-pointer helpers, removed manual transform repair, restored support wake coverage, and migrated intentional scene/path expectations; retained stack/pile limits (§13). |
| A | [tests/runner_failure_test.cpp](../tests/runner_failure_test.cpp) | Process/CLI failures, headless configuration and finite smoke runs (§13). |
| A | [tests/runtime_foundation_test.cpp](../tests/runtime_foundation_test.cpp) | Shared lifetime/identity/resource foundation contracts (§13). |
| A | [tests/runtime_step_test.cpp](../tests/runtime_step_test.cpp) | Step order, clock partitions, pause/manual/catch-up and invalid dt (§13). |
| A | [tests/runtime_work_probe.h](../tests/runtime_work_probe.h) | Test-facing work/allocation instrumentation declarations (§13). |
| A | [tests/runtime_work_test.cpp](../tests/runtime_work_test.cpp) | Dedicated warmed-idle allocation and affected-subtree/body work oracles (§13). |
| A | [tests/scene_io_contract_test.cpp](../tests/scene_io_contract_test.cpp) | Scene graph/component-resource rollback, startup reference failures after commit, ID validation and safe-save failure cases (§13). |
| M | [tests/scene_script_roundtrip_test.cpp](../tests/scene_script_roundtrip_test.cpp) | Explicit resolver migration, invalid-ID/type policy and scene/script serialization cases (§13). |
| A | [tests/scene_transition_contract_test.cpp](../tests/scene_transition_contract_test.cpp) | Load/clear request timing, last-wins and deferred error semantics (§13). |
| A | [tests/scene_transition_stress_test.cpp](../tests/scene_transition_stress_test.cpp) | At least 1,000 callback-triggered transitions and reference/callback traces (§13). |
| A | [tests/test_support.h](../tests/test_support.h) | Reusable public-runtime fixtures, temporary inputs and trace helpers (§13). |
| A | [tests/transform_hierarchy_test.cpp](../tests/transform_hierarchy_test.cpp) | Parent-only and simultaneous parent/child movement, reparenting and collider sync (§13). |

### Benchmark tools, fallback assets and focused documentation

| Status | File | Change |
| --- | --- | --- |
| A | [assets/animations/missing.animset](../assets/animations/missing.animset) | Added an intentional core fallback animation referring to the existing missing texture (§11). |
| A | [assets/audio/missing.wav](../assets/audio/missing.wav) | Added a core fallback sound independent of example assets (§11). |
| A | [benchmark/README.md](../benchmark/README.md) | Documented complete-step/schema-v3 measurement, independent runs, workload interpretation and historical/Godot comparison limits (§14). |
| A | [benchmark/capture_cleanup.py](../benchmark/capture_cleanup.py) | Added pinned fresh-process validation/timing orchestration, snapshot comparison, source/raw-result archives and checksums; updated the mixer source path after the vendor import (§14). |
| M | [benchmark/plot_scaling.py](../benchmark/plot_scaling.py) | Required recognized schema/metrics instead of plotting missing measurements as zero (§14). |
| M | [benchmark/sweep_scaling.py](../benchmark/sweep_scaling.py) | Propagated worker/seed/repetition settings, preserved raw samples/manifests and aggregated independent-process spread with stricter parsing (§14). |
| A | [benchmark/tests/test_sweep_scaling.py](../benchmark/tests/test_sweep_scaling.py) | Added benchmark parser/orchestration contract tests; these test tooling, not the GUI (§13). |
| A | [docs/CPP_API_MIGRATION.md](../docs/CPP_API_MIGRATION.md) | Concise consumer migration guide for scene requests, references/resolvers, destruction, stepping, paths and benchmarks. |
| A | [docs/SDL_UPDATE_VALIDATION.md](../docs/SDL_UPDATE_VALIDATION.md) | Recorded updated-dependency compatibility, benchmark comparison, leaks/TSan findings and later successful WAV-fix verification (§15). |

### Specification and execution records

| Status | File | Change |
| --- | --- | --- |
| A | [specs/001-cpp-engine-cleanup/contracts/library-benchmarks.md](../specs/001-cpp-engine-cleanup/contracts/library-benchmarks.md) | Public library and benchmark output/timing contract; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/contracts/physics-step.md](../specs/001-cpp-engine-cleanup/contracts/physics-step.md) | Physics, dirty-state, solver/event and step contract; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/contracts/runtime-lifecycle.md](../specs/001-cpp-engine-cleanup/contracts/runtime-lifecycle.md) | Mutation, identity, scene transition and ownership contract; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/data-model.md](../specs/001-cpp-engine-cleanup/data-model.md) | Design entities, states and relationships; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/plan.md](../specs/001-cpp-engine-cleanup/plan.md) | Implementation structure and verification approach; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/quickstart.md](../specs/001-cpp-engine-cleanup/quickstart.md) | Build/test/package/benchmark reproduction commands; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/research.md](../specs/001-cpp-engine-cleanup/research.md) | Design decisions and alternatives evaluated during planning; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/spec.md](../specs/001-cpp-engine-cleanup/spec.md) | Feature requirements and acceptance criteria; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/tasks.md](../specs/001-cpp-engine-cleanup/tasks.md) | 95 dependency-ordered implementation, test and integration tasks; planning/evidence rather than runtime code. |
| A | [specs/001-cpp-engine-cleanup/validation.md](../specs/001-cpp-engine-cleanup/validation.md) | Historical red/green execution evidence and links to later follow-ups; planning/evidence rather than runtime code. |

### Assets-only follow-up (24 October 2026)

The current follow-up simplifies the common loader rather than adding another runtime subsystem:

- `src/pathmanager.{h,cpp}` rejects unsupported asset spellings at the boundary and maps assets/ paths beneath the configured root. Removed the scene-directory parameter, filename-location fallback decisions, and the unused general canonicalizePath method. Symlink containment and case preservation remain.
- `src/referenceresolver.{h,cpp}` removes ResourceDirectoryResolver and ActiveReferenceResolver directory state. Required texture/audio/clip loads forward directly to the common manager loader, avoiding duplicate normalization/filesystem resolution.
- `src/gamemanager.cpp` removes animation-relative adapters and the startup resolver's directory argument. `src/scenebuilder.cpp` forwards component asset paths directly while retaining its acquisition ledger.
- `tests/asset_root_contract_test.cpp` checks explicit paths for components, behaviors and nested clips, serialization/case, cwd independence, optional fallbacks and rejection of bare/absolute/relative/traversing/symlink-escaping paths. Regression/foundation fixtures now separate filesystem locations used to write files from logical assets paths used to load them.
- The checked-in engine and Mario scene/animation files already used assets/ paths, so their content did not need rewriting.
- `ui/src/platformator_ui/services/asset_paths.py` removes legacy migration, automatic prefix insertion and lowercasing. The scene serializer no longer passes a containing-directory context. Service exports and asset-browser/inspector/canvas callers use the single canonicalizer. Imported invalid fields remain visible rather than silently repaired. Focused path/roundtrip tests were updated; this is not a general GUI rewrite.
- README, migration guide, lifecycle/library contracts, feature requirements and this walkthrough document the new rule. The older plan/research/tasks are explicitly annotated where their original directory-aware policy is superseded.

Host asset-root configuration and scene-opening paths retain their filesystem semantics. Empty optional fields still mean no asset. Invalid asset spelling is an error even for an optional loader; an optional valid assets/ path that names a missing file can still select the intentional fallback.

### Dependency import and local vendor edits

| Path / group | Change |
| --- | --- |
| `vendored/SDL3-3.4.2/` → `vendored/SDL3-3.4.18/` | Your upstream SDL replacement; CMake integration adjusted afterward. |
| `vendored/SDL3_mixer-3.2.0/` → `vendored/SDL3_mixer-3.2.4/` | Your mixer replacement; temporarily removed the previous local decoder patch. |
| `vendored/SDL3_image-3.4.0/` → `vendored/SDL3_image-3.4.8/` | Your upstream image-library replacement. |
| `vendored/SDL3_ttf-3.1.0/` → `vendored/SDL3_ttf-3.2.2/` | Your upstream font-library replacement. |
| [Current mixer decoder](../vendored/SDL3_mixer-3.2.4/src/decoder_wav.c) | Restored the signed-high-byte multiplication patch; previously applied to 3.2.0 during cleanup (§15). |

The many upstream files inside the replacement directories belong to those upstream releases. This guide does not attribute their implementation changes to the review.

### Captured results and diagnostic artifacts

| Directory / files | Purpose |
| --- | --- |
| [Pre-cleanup baseline](../benchmark/baselines/pre-cleanup-e01526b/) | Original 180-run capture, source snapshot, raw-run archive, manifests, state comparison, summary and checksum/verification records. Original physical failures remain flagged. |
| [Corrected cleanup reference](../benchmark/results/cpp-cleanup/) | 180 timing runs and 36 validation runs; reports, aggregate CSVs, capture/invariant JSON, source snapshot and checksums. Per-scenario archives retain raw per-run data. |
| [SDL update reference](../benchmark/results/sdl-update-9c4f3f6/) | Equivalent new-library matrix, old/new comparison CSV, supplemental paired-churn raw runs/summary, manifests, source snapshot and checksums. |
| [Cleanup evidence](../specs/001-cpp-engine-cleanup/evidence/) | Red/green geometry and invariant logs, work/package/sanitizer evidence, standalone mixer leak probe source/results and full-suite leak summary. |
| [SDL update evidence](sdl-update-evidence/) | Compressed configure/build/test/package/sanitizer/probe logs; structured test summary; standalone mixer init/quit probe; comparison/verification drivers and outputs. |
| [WAV-fix follow-up log](sdl-update-evidence/wav-fix-asan-tests.txt.gz) | 21 passed, zero failed, two intentionally skipped after local patch restoration, leaks disabled. |
| Temporary `TMP/wav-repro/` | Local upstream decoder reproduction artifacts described in §15, including the upload ZIP. No longer present at documentation time. |

These records are snapshots of the builds actually measured. They were not regenerated during writing of this guide, and older failures/limitations were not overwritten with newer outcomes.

### Workflow installation files

The history also contains `.agents/skills/speckit-*/SKILL.md` and `.specify/` integration manifests, shell scripts, templates, workflow definitions and the placeholder constitution. They support the explicitly invoked planning/task/implementation workflow. They are not C++ runtime changes or dependencies. The placeholder constitution remains unratified. The project-specific generated design is listed individually above; stock workflow installation files are not presented as hand-authored engine work.

### This guide

[CPP_CLEANUP_WALKTHROUGH.md](CPP_CLEANUP_WALKTHROUGH.md) is the new document created for your request. It changes no runtime behavior. The file inventory was checked against all 172 non-vendor, non-bulk-artifact paths in the historical diff; grouped artifacts/tool installation/vendor imports are accounted for separately.

## 18. Looking up an exact diff

From the repository root, use these read-only commands to pair a section with the actual patch:

```sh
# Main cleanup relative to the reviewed engine:
git diff e01526b 7906959 -- include/platformator/baseobject.h
git diff e01526b 7906959 -- src/gamemanager.cpp

# Ignore whitespace when a file looks larger than its behavioral change:
git diff -w e01526b 7906959 -- src/animator.cpp

# Follow-up integration and the local WAV fix:
git show 0b9c3e4 -- CMakeLists.txt src/benchmark.cpp
git show 1f3ffa2 -- vendored/SDL3_mixer-3.2.4/src/decoder_wav.c

# Assets-only simplification relative to the startup-only implementation:
git diff 162d3ec -- src/pathmanager.cpp src/referenceresolver.h src/scenebuilder.cpp
```

The initial walkthrough was documentation-only. The later startup-only initialization amendment changes the runtime and its tests; its current verification is recorded in validation.md. Historical benchmark archives remain unchanged. No commit or upstream submission was performed by the agent.

### Contact callback migration

The Stay callback family has been removed intentionally. Rebuild the library and consumers together, remove obsolete overrides/calls, and use existing update callbacks for recurring gameplay work. Enter/Exit signatures and borrowed callback-duration payloads remain unchanged. Attaching/replacing a script during a contact does not replay Enter; an eligible late recipient can receive the real Exit. No scene serialization migration is required. Schema-3 event counters count generated pairs including cleanup, not script notifications.
