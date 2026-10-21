# Data Model: C++ Engine Cleanup

This describes internal ownership/state and the small public identity types. It does not introduce an ECS, persistent database, generic event bus, or per-frame reflection framework. See [contracts/runtime-lifecycle.md](contracts/runtime-lifecycle.md) for observable behavior.

## Runtime and active scene

`Runtime` owns the core managers, a slot registry, a resource store, and one `ActiveScene`. It has a monotonically assigned process-local `runtimeSerial`, execution phase, pending scene request, reusable mutation buffers, and fixed-step run settings. Single-active-runtime remains an explicit supported constraint.

`ActiveScene` owns flat, stable-address GameObject allocations. Each owner has a dense storage index for swap-pop removal. Parent/child links are borrowed relationships. The scene contains the current successful scene path and an active file-ID-to-handle map. Clearing leaves an empty scene and no current save path; loading publishes a new map/path only on success. Programmatically created objects join this active scene with a new unused file ID.

`ExecutionPhase` distinguishes Idle, Starting, StepHook, FixedUpdate, Physics, CollisionDispatch, Update, Animation, LateUpdate, MutationCommit, DestructionCallbacks, Rendering, and Shutdown. One scoped phase guard restores state even if a callback throws. Phase changes are per stage, not per object. Structural mutation is main-thread-only; worker tasks have explicitly bounded data access.

An implementation may keep this ownership in GameManager rather than add another public class. `ActiveScene` is a concrete ownership record, not a new scene-service hierarchy.

## Object, component, and behavior lifecycle

| State | Meaning | Permitted transition |
| --- | --- | --- |
| Staged | Owned by an uncommitted scene or a new-object batch; not in active systems | Live or reclaimed on failure |
| Live | Owned, externally addressable, and eligible subject to activation/start state | PendingRetire |
| PendingRetire | Logically destroyed; cannot receive new ordinary callbacks; memory retained | DestroyCallbacks |
| DestroyCallbacks | onDestroy runs once while required component allocations remain readable | Unregistered |
| Unregistered | Systems/events/physics references have retired and hierarchy links detached | Freed |
| Freed | Registry generation invalidated; allocation released | No resurrection |

Components have the same lifecycle, an owning object, component-kind tag, current/pending registration state, and existing system indices. Component replacement transfers the old allocation into the retirement buffer and attaches the new allocation to the slot; it does not overwrite/free the old allocation while callbacks or pair data refer to it. Removing the executing script suppresses later behaviors in that retired script after the current callback returns.

Behaviors have a stable heap address, owning ScriptComponent, `started`/pending-start state, and a destruction-delivered flag. Start eligibility is queued when an unstarted behavior is active; duplicate enqueues use an index/bit. Inactive creation does not enqueue start work. A reused start buffer is swapped at step start, not copied; newly queued entries go to the next-step buffer. A behavior disabled before its first start is removed from pending-start work by its stored index and remains unstarted; activation enqueues it once. Inactive pending behaviors are not polled each step. Started behaviors do not start again after reactivation.

Immediate slot removal/replacement is not undone for destruction callbacks: getComponent sees only currently attached slots. Retired allocations are retained for existing engine users, not re-exposed through an obsolete slot.

`onDestroy` is an explicit runtime callback, never an incidental effect of C++ member destruction. It permits reading the object's component graph and releasing resources. Structural runtime mutation from onDestroy is outside this feature's supported contract and fails at the centralized public mutation boundary. Destructors do not call user callbacks or access freed hierarchy neighbors.

## Runtime identities and typed references

`ObjectHandle` contains a slot index, slot generation, and runtimeSerial. Invalid is represented explicitly. Slot generation increments on invalidation/reuse; exhausted generation slots are not recycled. Runtime serial assignment occurs once at construction; there is no per-frame global ID repair.

A registry slot stores a borrowed BaseObject pointer, generation, concrete object kind, and live/staged/retiring status. A free-slot vector supplies constant-time reuse. Concrete kinds distinguish GameObject, Animator, Audio, Camera, BoxCollider, CircleCollider, Rigidbody, ScriptComponent, and Sprite. Traits match a requested base Collider to either shape and concrete shape references only to that exact shape. Type validation occurs on bind, before a checked cast.

`ObjectReference<T>` holds an unbound serialized file ID or a bound handle, never pointer-derived identity. `get` validates the active runtime serial and indexed slot lifetime, returning an optional pointer. `operator->`/dereference require a valid result; debug assertions expose programmer mistakes. The registry and references are not synchronized for arbitrary background-thread use. Required references are bound once and borrowed within engine phases where the lifecycle barrier guarantees validity.

During scene preparation, reserved slots belong to staging and are not published in active ID/system registries. A build resolver can bind/read those staged allocations explicitly; normal ObjectReference get, active ID lookup, and public live registries cannot expose them until publication. Deserialization/reference-preparation code uses the explicit resolver to inspect staging, not an implicit current-runtime lookup. Failed preparation invalidates all reserved handles before owned data is reclaimed. Cross-scene/runtime stale references cannot rebind just because a file ID or storage address repeats.

File IDs stay integer JSON fields. The chosen validation policy accepts nonnegative IDs, including zero, unique across all objects and components in a scene. Detect exhaustion/overflow during staging/creation. Loaded-ID assignment is private to building/deserialization; users cannot change registered IDs without the registry. Automatic IDs advance past reserved loaded IDs. A slot handle is never written to existing JSON fields.

For a bound ObjectReference serialized into the active scene, the serializer translates a still-live same-scene handle to its target's file ID. An expired reference serializes as null; a live reference outside the scene being saved is a save error. Unbound IDs are allowed during staging only. An internal cached file ID may be retained for serialization, but its target validity comes from the handle, never a cached pointer.

## Structural work and synchronization

Use a small number of typed vectors: objects/components awaiting registration reconciliation, retired owners, starts, changed transforms, and physics exits. Each producer sets one queued bit/index; repeated requests update desired state without appending another entry. Iteration and owner-retirement buffers retain capacity across steps.

- Object/component creation during callbacks allocates once, returns a borrowed pointer, and stages registration for the barrier. It is visible through its owner, but cannot join an in-progress callback list.
- Retirement is terminal and idempotent. A queued handle prevents accidentally targeting a recycled allocation.
- Activation changes desired state and enqueue reconciliation. Disabling suppresses subsequent ordinary callbacks immediately; re-enabling takes effect at the boundary. Existing registered objects toggled off/on before commit retain final membership without unnecessary grid removal/re-add.
- Detach/replace a component slot as specified by the API, but retain the old allocation until every relevant queue and pair has released it.
- The commit stage handles work that collision-exit callbacks generate through a separate producer buffer. Newly created systems become eligible next step; newly retired existing systems are drained before free. It does not recursively run callbacks through simulation.

The common no-work case checks empty queues and returns. There is no registry sweep to discover pending work.

## Scene request and staged scene

`SceneRequest` is an operation (Load/Clear), optional path, and sequence value for diagnostics. There is one pending slot. It contains a path, not a fully loaded scene, so superseded requests do not trigger redundant loads. The barrier latches it once; requests produced during commit wait for the next step.

`SceneBuildContext` owns parsed records, flat staged object/component ownership, staged file-ID map, reserved handles, and acquired resource references. An explicit `ReferenceResolver` resolves object IDs/types and asset paths within this context. It is passed to behavior reference preparation; JSON extraction itself does not resolve through Runtime::current.

Preparation performs schema/value/uniqueness/type/behavior validation, builds the complete graph, prepares resources, and resolves references. Staging may share preexisting resource-cache entries and insert new entries, retaining each acquired resource through owned references. On failure it releases those references; newly inserted entries with no remaining users are removed, while preexisting shared entries and their live owners remain intact. Track creation/acquisition in the build context, not by scanning the whole cache. Any exception destroys staging with the old scene untouched. On success the commit performs only established ownership/registration transitions. Any user lifecycle callback exception is reported after best-effort mandatory cleanup; arbitrary user callback state does not receive a general rollback guarantee. The strong old-scene-preserved guarantee applies to preparation/validation/resource errors before commit.

## Transforms and hierarchy

Each GameObject retains local translation, local rotation and scale, parent/child links, cached world translation for synchronized systems, and hierarchy/dirty bookkeeping. This feature preserves translation-only parent inheritance; rotation/scale/active state do not silently start inheriting.

Main-thread public position reads preserve immediate effective position after setters. Worker/render hot loops read synchronized cached world positions. Dirty roots are indexed/coalesced, with a traversal stamp to avoid processing an affected descendant twice. A flat hierarchy order/depth cache is updated only for hierarchy structural changes. World caches and collider geometry are updated parent-first before collision queries and before rendering as needed. Reparenting preserves world translation and dirties the affected subtree.

Movement workers update their own local translation and rotation and append changed objects to thread-local buffers; they never read or write another object's live ancestor state. A controlled merge then propagates changed world translations and collider dirtiness. Main-thread queries between edits may calculate current ancestry; repeated per-pair worker queries must use the synchronized cache. If retaining an unsynchronized ancestry fallback is found to dominate a real path, evaluate demand/eager cache alternatives and apply the mandated EFFICIENCY policy rather than adding a full-scene refresh.

## Collider membership, pairs, and events

A collider has desired/current membership, geometry/filter/body/trigger dirty bits, occupied-cell range/bindings, adjacency index, and one queued reconciliation entry. Its retirement queue keeps the allocation live through cell/pair operations and exit delivery.

A collider pair has a canonical key from runtime identities, stable pair storage/index, witness count, old/new pair-local manifold/support flags keyed to canonical A/B collider identity, and active-contact membership. Stable result slots let workers write independently. Nonzero support transitions are reduced into body counters on the main thread. Collision destructors do not change body state.

Keep dirty broad-phase work and persistent active contacts separate. Candidate changes trigger geometry work; stay delivery uses the chosen active-contact/awake contract rather than requiring unrelated movement to requeue the pair. Pair membership transitions produce enter/exit once. Exit records retain the retired collider allocation until eligible survivor callbacks have run. No event stores a pointer to an already freed Collision or collider.

Filter changes locally remove/reinsert affected cell bindings, combining witness deltas so an allowed contact is not spuriously destroyed. Reconciliation compares final desired/current state, fixing off/on churn without contradictory ADD/REMOVE operations racing on one collider.

Bodies maintain indexed awake-dynamic, kinematic, and sleep-eligible membership. Changes in impulses, support, activation, type, or external motion update only affected entries. The serial solver processes the persistent contact list in deterministic order; changed membership is canonicalized outside unchanged hot traversal. Sleeping refinements must satisfy existing physical assertions without relaxed tolerances hiding residual solver problems.

## Resources and asset root

Runtime normalizes one configured root at startup. A logical `assets/...` path maps beneath it while preserving disk filename case; explicit scene paths and scene-relative behavior are defined in the public contract. Fallback assets are deliberately packaged, not inferred from presence of unrelated folders.

Resource cache entries own SDL resources. Scene staging, Sprite/Audio/AnimationFrame/Animator, and explicit public asset references retain resources consistently; replacing an animator clip releases the old clip after acquiring the new one. Unreferenced release is explicit and occurs while backend resources are alive. External asset references are scoped to the owning runtime; debug assertions/tests expose violations of that documented precondition. Runtime shutdown destroys components/resources before SDL backends. A physics-only runtime skips video and audio device acquisition.

## Frame clock and benchmark records

Interactive run settings hold fixed dt (default 1/120), accumulated remainder, maximum catch-up steps (default eight), pause state, and aggregate dropped-step count. Explicit `simulateFrame(dt)` runs exactly one caller-defined finite-positive step; it does not alter a wall-time accumulator.

Benchmark configuration fixes workload, seed, workers, dt, warmup, measured steps, and repetitions. Reusable samples contain schema version, complete-step duration, named subscopes, render duration when requested, and counters. A run manifest records source/build/machine metadata once per run. Expensive invariant checks are explicit validation instrumentation, separate from uninstrumented performance runs.

## Intended complexity

| Operation | Intended work |
| --- | --- |
| Request object/component mutation | Amortized O(1), one entry per changed entity |
| Resolve bound handle | O(1) direct slot access with runtime/generation checks |
| Commit registrations | O(changed entities), plus affected hierarchy/physics relationships |
| Destroy subtree | O(its objects/components/adjacent pairs), each retired once |
| Refresh transforms | O(affected subtrees), each reached descendant once per drain |
| Change collision filter | Existing broad-phase work in that collider's occupied cells and adjacency |
| Reduce support changes | O(nonzero support transitions) |
| Integrate bodies / advance sleep timers | O(awake/kinematic/eligible bodies), not all sleeping/static objects |
| Deliver collision callbacks | O(eligible active contacts/events), required observable behavior |
| Load/save scene | O(serialized scene/resources), cold-path allocation permitted |
| Unchanged-step safety overhead | Empty-queue checks and necessary per-dispatch lifecycle state; no whole-scene audit |
