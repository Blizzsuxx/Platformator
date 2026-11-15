# Phase 0 Research: Runtime Template Instantiation

**Feature**: `017-runtime-template-instantiation` | **Date**: 2026-11-15

## Summary of Codebase Findings & Decisions

All technical unknowns were resolved directly against the Platformator runtime and editor codebase (`include/platformator/`, `src/`, `editor/src/`).

---

### Decision 1: Prototype Representation & Ownership (`Template : public Asset`)
- **Decision**: Model a loaded `.pttemplate` as `class Template : public Asset`, owning `std::vector<std::unique_ptr<GameObject, ObjectDeleter>> objects` (with `objects[0]` as the prototype root), plus `Asset::Reference<T>` vectors keeping pre-warmed media assets alive.
- **Rationale**:
  - Platformator's `GameObject` and `Component` already initialize with `ObjectLifecycle::Staged` and empty `ObjectHandle` ([baseobject.h](../../../include/platformator/baseobject.h)), exempt from runtime mutation locks ([gameobject.cpp](../../../src/gameobject.cpp), [scriptcomponent.cpp](../../../src/scriptcomponent.cpp)) and invisible to `ObjectRegistry`, physics, rendering, and `Scene::saveScene`.
  - `GameObject::~GameObject()` deletes only `components` (not `children`), and `GameObject` is non-copyable/non-movable with `private` constructor/destructor. Storing `std::unique_ptr<GameObject, ObjectDeleter>` in a `std::vector` keeps pointers stable (`parent`, `children`, `Component::gameObject`), preserves `GameObject`'s strict private construction/destruction invariants via `friend class Template`, and provides automatic RAII cleanup (`~Template() = default;`) with zero custom allocators.
- **Alternatives Considered**:
  - *Re-parsing `.pttemplate` JSON on every spawn*: Simpler initially, but causes JSON parse/file overhead per spawn and pushes media discovery to spawn time instead of owning an in-memory unregistered prototype.
  - *`std::vector<GameObject>` or `std::list<GameObject>`*: `std::vector<GameObject>` requires move semantics and invalidates internal raw pointers on reallocation; `std::list<GameObject>` fails to compile with `private` `GameObject` ctors without public exposure or a custom friend allocator, and still allocates per node with extra link overhead.

---

### Decision 2: Component & Behavior Cloning Without Boilerplate
- **Decision**: Add `virtual Component *clone() const = 0` to `Component` for the 8 built-in component classes (`Animator`, `Audio`, `BoxCollider`, `CircleCollider`, `Camera`, `Rigidbody`, `ScriptComponent`, `Sprite`), and generate `Behavior *clone() const override` directly inside the `SERIALIZABLE_SCRIPT(...)` macro ([scriptregistration.h](../../../include/platformator/scriptregistration.h)).
- **Rationale**:
  - Component cloning copies authored fields and asset references while leaving runtime registration state (`id`, `handle`, `lifecycle`, `systemsRegistered`, `reconcileQueued`, window/physics indexes) at fresh defaults.
  - `SERIALIZABLE_SCRIPT` already generates factory construction, metadata inspection, serialization, and reference resolution for every registered behavior. Adding `return new Type(*this);` inside the macro requires zero per-script user code. `ScriptComponent::clone()` resets `scriptComponent = nullptr` and `gameObject = nullptr` on each cloned behavior before calling `clonedScript->addBehavior(...)` so `addBehavior`'s ownership invariant holds cleanly.
  - If copying throws midway while cloning a prototype subtree, a `unique_ptr`/guard cleans up all unregistered cloned `GameObject`s before `GameManager::registerGameObjectSubtree` touches live state (`FR-011`).
- **Alternatives Considered**:
  - *JSON serialize/deserialize per spawn*: Extra heap/JSON churn per spawned enemy/bullet.
  - *Manual `clone()` in every user script*: Breaks Unity-like ergonomics and invites silent omissions.

---

### Decision 3: Internal vs. External `ObjectReference` Remapping Order
- **Decision**:
  1. Clone the prototype hierarchy cleanly into unregistered `Staged` objects, building `unordered_map<int, BaseObject *> protoIdToClone` as each prototype node and component (`proto->getId()`) is cloned.
  2. Attach optional parent or set transform overrides; then register the cloned subtree via `GameManager::registerGameObjectSubtree(cloneRoot)` inside an active `assertCanMutate()` + `flushIfIdle()` boundary so every clone node/component immediately gets a unique live scene ID and `ObjectHandle`.
  3. Run a minimal `CloneReferenceResolver` over cloned `Behavior`s (`resolveReferences`) **before** `start()` executes:
     - IDs present in `protoIdToClone` (`internal` bindings) resolve to the cloned target (`set(mappedTarget)` — updating **both** `handle` and `objectId` in `ObjectReference<T>`). Wrong target kinds fail with an exception and trigger atomic cleanup before registration commits (or build/remap handles before publishing). Wait: even simpler, validate internal target kinds while still unregistered/before committing OR register only after checking `protoIdToClone` kinds so failures never leak partial live objects!
     - IDs **not** in `protoIdToClone` (`external` cross-scene bindings, since `.pttemplate` guarantees `allIds` disjointness) are cleared via `reference.clear()` (`FR-009`), never silently matched against scene objects.
- **Rationale**:
  - Updating `ObjectReference<T>` (`objectId` + `handle`) ensures `getReferencedId()`, `get()`, and `Scene::saveScene()` stay consistent for cloned instances.
  - Registering inside `registerGameObjectSubtree` before `flushIfIdle()` reuses Platformator's existing deferred `queueReconciliation` / `queueScriptStart` machinery, making spawns during `update()` and collision callbacks completely safe (`FR-010`).
- **Alternatives Considered**:
  - *Two-pass lookup tables storing string JSON pointers from `reference_bindings`*: Unnecessary complexity — `SERIALIZABLE_SCRIPT` already iterates every `ObjectReference<T>` member directly in C++, and `.pttemplate` `allIds` guarantees every internal target ID is inside `protoIdToClone`.

---

### Decision 4: Preloading Dependencies & Cycle-Safe Nested Templates
- **Decision**: Reuse `GameManager`'s existing `ReferenceResolver` loading pipeline during `GameManager::loadTemplate(filePath, ...)` while tracking active canonical template paths in a recursion stack (`std::vector<std::string>` / `unordered_set<std::string>`):
  - Non-cyclic dependencies (`TextureWrapper`, `AudioWrapper`, `AnimationClip`, non-cyclic nested `Template`) load eagerly and are retained via `Asset::Reference<T>` on the `Template` (plus components' own refs) for zero-disk-I/O spawning.
  - If a nested `AssetReference<Template>` targets a canonical path currently on the active load stack (self-reference or cycle `A -> B -> A`), validate that the file exists/parses without recursing infinitely and leave the prototype's `AssetReference<Template>` path-backed (`object == nullptr`, `filePath` preserved). When a clone spawns and enters the live scene, normal resolution binds `filePath` to the already-cached live `Template*` cleanly — zero infinite recursion and zero shutdown refcount cycles (`FR-003`, `FR-004`).
- **Alternatives Considered**:
  - *Keeping strong `AssetReference<Template>` cycles in cache*: Causes `referenceCount > 0` leaks at scene unload unless a mark-sweep GC runs on caches; path-backed cyclic prototype fields avoid the problem with ~5 lines of code.

---

### Decision 5: Editor Asset Integration (Minimal Extension of Existing Pipelines)
- **Decision**: Add `Template` as a standard `asset_reference` target alongside `Texture`, `Audio`, and `AnimationClip`:
  - Runtime: `AssetTargetName<Template> = "Template"` in [scriptmetadata.h](../../../include/platformator/scriptmetadata.h) and `using TemplateAssetRef = AssetReference<Template>;` in [assetreference.h](../../../include/platformator/assetreference.h).
  - Editor:
    - [sourcescanner.cpp](../../../editor/src/sourcescanner.cpp): Recognize `TemplateAssetRef` / `TemplateRef` / `AssetReference<Template>` as `asset_reference` with `targetType = "Template"` (already strips template args for `AssetRef<T>`, easily matching `TemplateAssetRef` and `AssetReference<Template>`!).
    - [resourceindex.h](../../../editor/src/resourceindex.h) & `resourceindex.cpp`: Add `ResourceKind::Template`.
    - [assetformats.h](../../../editor/src/assetformats.h), [filedialogs.cpp](../../../editor/src/filedialogs.cpp), [referencefields.cpp](../../../editor/src/panels/referencefields.cpp): Map `"Template"` -> `ResourceKind::Template` / `EditorAssetKind::Template` (`.pttemplate`).
  - Existing `validation.cpp`, `inspector.cpp`, `templates.cpp`, and `editcommands.cpp` already handle `asset_reference` generically by file path (`assets/...`), so overrides, apply, revert, duplicate, and missing-asset diagnostics work automatically.

---

### Decision 6: Parallelism & Performance Assessment (Constitution §I, §II, §IV, §VI)
- **Workload & Hot Path**: Prototype parsing occurs once per unique `.pttemplate` file per scene load (cached by canonical path). `instantiate()` executes per runtime spawn (e.g., bullets, pickups, enemies, particle bursts) on the main simulation thread (`assertCanMutate()`), where structural registration and `ScriptComponent` attachment are inherently serialized with `ObjectRegistry` and `GameManager::gameObjects`.
- **Parallelism Decision**: **Sequential main-thread execution**. Justified by `GameManager`'s single-writer structural mutation boundary (Constitution §II explicitly permits sequential execution where structural invariants require serialization). No new mutexes, worker queues, or cross-thread synchronization are introduced.
- **Complexity & Scaling**: Clones in `O(nodes + components + behaviors)` per spawn with zero file I/O and zero JSON parsing after initial template load; scales linearly with subtree size up to hundreds of spawns per session without frame-time I/O hitches.
