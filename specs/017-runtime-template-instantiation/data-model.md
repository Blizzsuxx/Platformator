# Phase 1 Data Model: Runtime Template Instantiation

**Feature**: `017-runtime-template-instantiation` | **Date**: 2026-11-15

---

## Entities & Ownership Boundaries

### 1. `Template` (`include/platformator/template.h`, inherits `Asset`)
Represents a cached, read-only runtime blueprint loaded once from a `.pttemplate` file.

- **Ownership**: Owned by `GameManager::templateCache` (`unordered_map<std::string, std::unique_ptr<Template>>`) with alias cache (`templateAliases`), refcounted via `Asset::referenceCount` (`freeFromRuntime()` delegates to `GameManager::freeTemplate(this)`).
- **Fields**:
  - `std::string name`: Template display name from envelope `"name"`.
  - `std::vector<std::unique_ptr<GameObject, ObjectDeleter>> objects`: Prototype object subtree (`objects[0]` is root; children follow in build order). All remain `ObjectLifecycle::Staged` with `handle == {}`, carrying prototype-local `id` values from `.pttemplate`.
  - Retained dependencies via `Asset::Reference<...>`:
    - `std::vector<Asset::Reference<TextureWrapper>> retainedTextures`
    - `std::vector<Asset::Reference<AudioWrapper>> retainedAudio`
    - `std::vector<Asset::Reference<AnimationClip>> retainedClips`
    - `std::vector<Asset::Reference<Template>> retainedNestedTemplates` (acyclic only; cyclic refs remain path-backed on prototype behaviors so `referenceCount` never cycles)
- **Validation Rules**:
  - Reject non-object envelope, missing/wrong `"format"` (`"platformator.object-template"`), `"version" != 1`, missing `"root"` object, duplicate IDs or duplicate component slots inside `"root"`, or invalid JSON fields/asset dependencies on load (throws before caching, leaving caches untouched).
  - Unset/empty paths on a behavior's `AssetReference<Template>` succeed during scene load; non-empty missing/malformed paths fail fast during `resolve`/`loadTemplate`.

---

### 2. `AssetReference<Template>` (`TemplateAssetRef`)
Field type declared in gameplay behavior scripts (`FIELD(TemplateAssetRef, projectileTemplate)` or `AssetReference<Template>`).

- **Serialization**:
  - JSON representation matches all `AssetReference<T>` types: string relative path (`"assets/templates/Coin.pttemplate"`) or `null` when unset.
  - Serialized and deserialized as `asset_reference` (`target_kind = "Template"`) in runtime metadata and editor `sourceScanner` / `behaviorCatalog`.
- **Runtime Resolution State Transitions**:
  - `Unset` (`filePath == nullopt`, `object == nullptr`): valid at load time; calling `instantiate()` on unset/null throws `std::logic_error` / `std::invalid_argument` clearly (`FR-011`).
  - `PathOnly` (deserialized or cyclic prototype ref: `filePath.has_value()`, `object == nullptr`): resolves during `ScriptComponent` start / prototype build (`resolve()`), failing fast if file is missing/malformed.
  - `Bound` (`object != nullptr`, `runtimeSerial == activeRuntimeSerial`): `Asset::addReference()` held; calling `instantiate()` clones and registers directly in `O(subtree)` without disk reads.

---

### 3. `GameObject` & `Component` Clone Relationship
- **Prototype Subtree -> Spawned Instance Mapping**:
  - Prototype nodes stay permanently `Staged` (`!getIsRegisteredInGameManager()`, `!handle`), invisible to all live systems.
  - Cloning constructs new `GameObject`s (`id = -1`, `handle = {}`, `lifecycle = Staged`) and clones components via `Component::clone()`, building `std::unordered_map<int, BaseObject *> protoIdToClone` (mapping every prototype `GameObject` and `Component` local ID present).
  - Clone parent-child links match prototype hierarchy; root placement obeys Q2:
    - `instantiate()` / `instantiate(GameObject *parent)`: keeps prototype root `localPosition` and `rotation` relative to `parent` (or world root when `parent == nullptr`).
    - `instantiate(const Eigen::Vector2f &worldPosition, std::optional<float> worldRotation = std::nullopt, GameObject *parent = nullptr)`: attaches under `parent` if given, then places root at `worldPosition` (and `worldRotation` or prototype root rotation) in world space while child nodes keep parent-relative local transforms.

---

### 4. Reference Remapping State (`internal` vs `external`)
During `CloneReferenceResolver` traversal of cloned `Behavior` `ObjectReference<T>` fields:
- If `objectId` is unset -> stays unset (`null`).
- If `objectId` is present in `protoIdToClone`:
  - Verify target matches `ObjectKind` (including component subtype checks); mismatch throws and aborts before registration completes cleanly (`FR-011`).
  - Bind cloned field via `reference.set(typedCloneTarget)` **after** `registerGameObjectSubtree` assigns live IDs and `ObjectHandle`s — setting **both** live `handle` and new unique `objectId`.
- If `objectId` is **not** in `protoIdToClone` (`external` cross-scene binding):
  - Call `reference.clear()` (`handle = {}`, `objectId = std::nullopt`) cleanly (`FR-009`).

---

### 5. Scene Spawning Asset Retention Scope
- When a `Template` spawns an instance whose components (`Audio`, `Animator`, `Sprite`) or behaviors depend on cached `TextureWrapper` / `AudioWrapper` / `AnimationClip` assets, the live spawned scene copies also retain their asset dependencies in `GameManager`'s active scene asset ledger (the same way `SceneBuildContext` commits scene-level asset acquisitions into `GameManager` on scene load) so spawned objects remain safe even if the originating `Template` is later released.
- Unloading the scene (`freeScene` / `clearScene` / `shutdown`) drops scene-retained refs + unused `Template` entries deterministically (`SC-005`).
