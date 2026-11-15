# Runtime & Editor Contracts: Runtime Template Instantiation

**Feature**: `017-runtime-template-instantiation` | **Date**: 2026-11-15

---

## 1. Public C++ Runtime API (`include/platformator/`)

### 1.1 `Template` Class (`include/platformator/template.h`)
```cpp
class Template : public Asset
{
  public:
    ~Template() override;

    const std::string &getName() const;

    // Default / parented spawn preserving prototype-authored local transform:
    GameObject *instantiate(GameObject *parent = nullptr) const;

    // Explicit world-space placement spawn (keeps child local transforms):
    GameObject *instantiate(const Eigen::Vector2f &worldPosition,
                            float worldRotation,
                            GameObject *parent = nullptr) const;
    GameObject *instantiate(const Eigen::Vector2f &worldPosition,
                            GameObject *parent = nullptr) const;
};
```
**Postconditions & Error Contract**:
- Returned `GameObject*` is registered live in the active `GameManager` (`getIsRegisteredInGameManager() == true`), with fresh unique scene IDs across all cloned objects and components.
- Calling outside a valid active mutation context or with a `parent` belonging to another runtime or retired/unregistered throws `std::logic_error` / `std::invalid_argument` (KISS boundary check; no speculative null recovery on internal helpers).
- Internal `ObjectReference<T>` fields across cloned behaviors already hold both their live `ObjectHandle` and new scene `id` **before** queued `start()` executes; cross-scene `external` references are cleared (`bool(ref) == false`).

### 1.2 `AssetReference<Template>` (`include/platformator/assetreference.h`)
```cpp
namespace platformator
{
using TemplateAssetRef = AssetReference<Template>;
}
```
- Scripts declare `TemplateAssetRef myPrefab;` inside `SERIALIZABLE_SCRIPT(...)`.
- Convenience forwarding or calling `myPrefab->instantiate(...)` (or `Instantiate(myPrefab, ...)` if added cleanly? Wait — keep it KISS: `Template::instantiate(...)` via `AssetReference<Template>::operator->()`, e.g., `coinTemplate->instantiate(pos)`! Wait: what if `myPrefab` is unset (`get() == nullptr`)? In debug `assert(object)` or boundary `instantiate(const AssetReference<Template> &...)` helper? Providing a tiny free function or `platformator::instantiate(const AssetReference<Template> &, ...)` that checks `!ref.get()` cleanly at the public boundary satisfies **FR-011** ("reject instantiating an unset reference cleanly with a clear error rather than crashing") without crashing with a null deref!).
  - Public helper functions in `namespace platformator` (or `Template` header):
    - `GameObject *instantiate(const AssetReference<Template> &tmpl, GameObject *parent = nullptr);`
    - `GameObject *instantiate(const AssetReference<Template> &tmpl, const Eigen::Vector2f &worldPosition, GameObject *parent = nullptr);`
    - `GameObject *instantiate(const AssetReference<Template> &tmpl, const Eigen::Vector2f &worldPosition, float worldRotation, GameObject *parent = nullptr);`
  - Boundary rule (`FR-011`): Throws `std::invalid_argument("Cannot instantiate an unset template reference")` when `tmpl.get() == nullptr` (and resolves path cleanly first if `tmpl.hasFilePath() && !tmpl.get()`, failing fast on missing/corrupt files).
  - Direct call `tmpl->instantiate(...)` remains available when caller already checked `if (tmpl)`.

### 1.3 `Runtime` & `ReferenceResolver` (`runtime.h`, `referenceresolver.h`, `scriptmetadata.h`)
- `Runtime::loadTemplate(const std::string &filePath) const -> Template *`
- `ReferenceResolver::loadTemplate(const std::string &path) -> Template *`
- `AssetTargetName<Template>::value = "Template"` in `scriptmetadata.h`

---

## 2. Serialization & Behavior Metadata Contract

### JSON Schema (`.scene` and `.pttemplate` behavior fields)
```json
{
  "type": "SpawnerBehavior",
  "enemyTemplate": "assets/templates/Goomba.pttemplate"
}
```
Unset:
```json
{
  "type": "SpawnerBehavior",
  "enemyTemplate": null
}
```

### Compiled Catalog & Source-Scanned Field Descriptor
```json
{
  "name": "enemyTemplate",
  "kind": "asset_reference",
  "target": "Template",
  "nullable": true
}
```
Also emitted in runtime `ScriptFieldTraits` / inspection metadata:
- `target_kind = "Template"`
- `kind = "asset_reference"`

**Source Scanner Type Mappings (`sourcescanner.cpp`)**:
- Recognized aliases in C++ headers:
  - `TemplateAssetRef` / `platformator::TemplateAssetRef`
  - `TemplateRef` / `platformator::TemplateRef` (optional alias if used)
  - `AssetReference<Template>` / `AssetRef<Template>`

---

## 3. Editor Asset & Diagnostic Contract
- `ResourceKind::Template` resolves `.pttemplate` files inside `assets/`.
- `asset_reference_extension_matches("Template", path)` returns `true` iff `editor_asset_format(path).kind == EditorAssetKind::Template` (`.pttemplate`).
- Inspector renders a `.pttemplate`-filtered asset picker (drag-and-drop from Project panel, file dialog filter `"*.pttemplate"`, clear to `null`).
- Diagnostics report missing/unreadable `.pttemplate` paths on the owning component/behavior cleanly without confusing them with `object_reference` ID bindings.
