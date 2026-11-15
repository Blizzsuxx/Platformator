# Quickstart Validation Guide: Runtime Template Instantiation

**Feature**: `017-runtime-template-instantiation` | **Date**: 2026-11-15

All implementation paths reference [data-model.md](./data-model.md) and [contracts/runtime-template-api.md](./contracts/runtime-template-api.md).

---

## Prerequisites
Build both the runtime library/tests (`debug` preset) and the editor library/tests (`editor-debug` preset):
```bash
cmake --build --preset debug --parallel 4
cmake --build --preset editor-debug --parallel 4
```

---

## Validation Scenarios

### 1. Inert Prototype Isolation & Multi-Spawn Identity (`SC-001`, `SC-002`, `SC-003`)
- **Setup**: Load a scene containing a spawner object holding a `.pttemplate` with a parent node, child hitbox (`BoxCollider`), `Sprite`, `Audio`, `Animator`, `Rigidbody`, and `ScriptComponent` with internal object/component references.
- **Check before spawning**:
  - Active `Runtime::getAllGameObjects()` contains only scene objects (`0` prototype objects/components).
  - Saving scene output excludes the dormant prototype cleanly.
- **Check after spawning 3+ times** (default, parented, and explicit world `(x, y)` + `rotation`):
  - Each spawned copy has distinct scene IDs and live handles across all nodes and components.
  - Each clone's internal `ObjectReference<GameObject>` / `ObjectReference<Camera/Collider/Audio/...>` resolves strictly to its own clone's objects/components (`ref.get()` != `nullptr`, `ref.getReferencedId()` matches clone ID).
  - Mutating or destroying Spawn #1 leaves Spawn #2, Spawn #3, and subsequent Spawn #4 completely intact.

### 2. Mid-Step / Callback Spawning & Rollback (`FR-010`, `FR-011`, `SC-003`)
- **Trigger inside `update()` and collision callbacks**: Spawn 100 instances across gameplay ticks and collision events cleanly without corrupting active body/object/pair iteration; verify every spawned script receives `start()` once with internal references already bound.
- **Negative checks**:
  - Calling `instantiate(unsetRef)` throws cleanly without modifying live scene state.
  - Loading a scene with a missing or corrupted `.pttemplate` path fails fast (`FR-011`).

### 3. External Reference Clearing & Cyclic Templates (`FR-003`, `FR-009`, `SC-005`)
- **Cross-scene external binding**: Load a `.pttemplate` containing an `external` binding (matching a coincidental object ID present in the destination test scene) -> verify the spawned instance's external field resolves to cleared/unset (`!ref`), while its `internal` bindings remain intact.
- **Self-referential / cyclic template (`Slime -> Slime` and `A -> B -> A`)**: Load without infinite recursion -> spawn cleanly -> verify cloned `Slime` receives valid bound `Template*` -> reload/unload scene repeatedly and verify zero leaked prototype objects or retained asset refcounts (`SC-005`).

### 4. Editor Authoring Round-Trip (`SC-004`)
- Run editor tests (`ctest --test-dir bin/editor-debug --output-on-failure -L editor`):
  - Source scanner emits `"asset_reference"` (`"target": "Template"`) for `TemplateAssetRef` / `AssetReference<Template>`.
  - `validate_asset_reference_assignment` accepts `.pttemplate`, rejects non-template files, and diagnostics catch broken paths.
  - Export/duplicate/apply/revert preserve template reference asset paths cleanly as `asset_reference` rather than scene ID bindings.
