# Contract: Layer Settings & `.scene` / Physics Layer Serialization

**Feature**: `016-final-editor-cleanup`
**Covers**: `FR-011`, `FR-012`, `FR-013`, `FR-014`

## 1. Per-Project Layer Settings File (`.platformator/layers.json`)

Schema (version `1`):

```json
{
  "version": 1,
  "layers": [
    "Default",
    "Player",
    "Enemy",
    "Ground",
    ""
  ],
  "collisionMatrix": [
    4294967295,
    4294967293,
    4294967295
  ]
}
```

### Invariants
1. **`layers` length**: Always normalized to 32 entries (`0..31`) in memory; serialized array with <32 elements zero/empty-pads missing slots.
2. **Slot `0`**: Always `"Default"`. If loaded file sets `layers[0]` to anything else, loader normalizes slot `0` back to `"Default"`.
3. **Name uniqueness & trimming**: Non-empty layer names (`1..31`) are trimmed of leading/trailing whitespace and must be unique (case-insensitive check recommended in UI validation; exact duplicate rejected on save/edit).
4. **`collisionMatrix` symmetry**: Always 32 unsigned 32-bit integers (`uint32_t`). Bit `j` (`0..31`) of `collisionMatrix[i]` indicates whether layer `i` collides with layer `j`.
   - Missing file or missing entries default to `0xFFFFFFFFu` (all bits set — 100% compatible with legacy scenes).
   - Setting `(i, j, enabled)` atomically sets bit `j` of row `i` AND bit `i` of row `j`.
   - Deserializing non-symmetric rows normalizes `(i, j)` consistently across both rows (`matrix[i]` and `matrix[j]`).

## 2. GameObject Serialization (`.scene` & `.pttemplate`)

Every serialized `GameObject` record gains:

```json
{
  "id": 101,
  "name": "Player",
  "tag": "player",
  "layer": 1,
  ...
}
```

- **Backward Compatibility**: If `"layer"` is absent in an existing `.scene` or `.pttemplate` file, both `SceneBuildContext::buildObject` (engine runtime) and editor scene/template loaders default `layer` to `0`.
- **Bounds Check**: Valid range `0..31`. Values `< 0` or `> 31` in hand-edited JSON produce a validation error in editor (`SceneValidator`) and clamp/fail safely during runtime load without UB.
- **Standalone / Play Execution**: `Runtime` loads `.platformator/layers.json` from the project root / parent of `assetRoot` (if present) during initialization before `PhysicsManager` steps simulation frames.

## 3. Runtime Collision Filtering Semantics

- For any overlapping collider pair `(A, B)`, collision contact generation and resolution proceed **only** if:
  `((matrix[A.layer & 31] >> (B.layer & 31)) & 1u) != 0`.
- Layer filtering applies identically to SOLID colliders and TRIGGER colliders (both skip `COLLISION_ENTER`/`STAY`/resolution when the layer pair bit is `0`).
- Changing `GameObject::setLayer(newLayer)` dynamically at runtime clears disabled contacts cleanly without leaving stale penetration or permanent stuck `COLLISION_ENTER` state, while preserving `Grid` cell witness bookkeeping invariants (`tbb::parallel_for` narrow-phase safety).
- Sprite draw order and Z/sort behavior remain strictly independent of `GameObject::layer`.
