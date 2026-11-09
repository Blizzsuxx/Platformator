# Contract: Reusable Objects, Sprite Regions, and Animation Assets

**Requirements**: FR-005–006, FR-013–014, FR-017, FR-030, FR-041–044, FR-048, FR-050–052.

## Component copy and reset

Copy retains declared supported values, not object/component identity or the component kind discriminator. Paste requires compatible target type, previews mixed/incompatible state, preserves destination fields not covered by the copied declared field set, and commits one transaction. Unknown fields are not silently removed. Reset built-in supported values uses engine-authoritative defaults; script Reset removes supported serialized overrides where omitted fields retain constructor defaults. Never instantiate behaviors or invent zero/empty defaults to author a reset. IDs, relationships and unknown payload remain unchanged. A group reset/paste is all-or-nothing with one Undo.

## Template v1 format

Use editor-only `.pttemplate` JSON; the game scene format remains the ordinary object array. The envelope includes:

```json
{
  "format": "platformator.object-template",
  "version": 1,
  "name": "Enemy Assembly",
  "root": {"id": 10, "components": [], "children": []},
  "reference_bindings": [
    {"owner_id": 10, "component_id": 11, "path": "/behaviors/0/target", "target_kind": "GameObject", "scope": "internal"}
  ]
}
```

The root example is abbreviated, not a valid complete fixture. Actual root records preserve all required scene fields/unknown data. Binding paths are validated document-relative JSON pointers at declared property sites; owner/component identities must exist and expected types match. A binding cannot target identity/type discriminators or arbitrarily reinterpret another numeric field. Template IDs are source-local; validate uniqueness before instantiation. Unknown envelope keys survive editing and unsupported major versions are rejected with an explanation.

Collect bindings from authoritative compiled field descriptors; author-declared reference sites can cover otherwise opaque data. If reference semantics are unavailable, the UI requests declarations or explains that self-contained remapping cannot be guaranteed; it never guesses that every integer is a reference. Keep opaque values lossless. External bindings additionally store source-scene identity and original target kind/ID. Preserve an external target without prompting only when instantiating in the same scene identity. In another scene, require explicit destination assignment before committing instantiation, even if the original numeric ID resolves to a same-kind object there. Missing/wrong-kind targets require repair/assignment; integer/type coincidence never proves the intended external identity.

Instantiation allocates all object/component IDs first, builds old→new mapping, updates only declared internal bindings, and inserts the resulting subtree in one structural transaction. No source instance changes. No automatic instance propagation, nested prefab variants, or runtime template loader is introduced.

## Duplication policy

The project default remains `PreserveExistingTargets` for compatibility until explicitly changed. The alternative `RemapInternalTargets` uses the same validated typed-binding map; references outside the copied set retain their existing targets. Show the active policy in duplication UI/settings and explain that preserved internal references point to originals. A multi-root duplicate uses one combined mapping so references among selected copied roots remap consistently; nonselected descendants included by the copied subtree are part of that mapping. Unknown/unsupported reference sites require declarations for an assured remap and otherwise remain opaque with explicit status. Undo/Redo uses the retained payload and map results; it never allocates different IDs on redo.

## Source rectangles and animation authoring

Sprite region editing writes existing `textureFilePath`, `sourceRectEnabled`, `sourceRect{x,y,w,h}`, width/height and flip fields as applicable. The region tool displays source-image pixels and dimensions, allows drag/numeric bounds editing, and commits a completed rectangle change as one gesture/transaction. Coordinates are finite, sizes positive, and enabled regions fit their verified source image; missing/unavailable dimensions preserve the old accepted value and show pending/unresolved state. Region previews do not silently resize the authored sprite. A standalone sheet can be inspected, but saving a region targets an explicit Sprite component or animation frame; no new game asset format is necessary.

Animation documents use existing `.animset` structure: clip name, FPS, loop, width/height and ordered frames with `textureWrapperFilePath`, `hasSourceRect`, optional source rectangle and `duration`. Preserve unknown members and missing/explicit distinctions on load/save. Positive frame duration overrides clip FPS; zero duration uses the engine's `1/FPS` fallback. Reject nonfinite/negative duration and invalid FPS for fallback frames; playback and scrubbing use the same shared duration calculation as the engine.

Add/remove/reorder frame, timing, loop, region and compatible texture assignment are undoable asset transactions. Preview playback/scrubbing and selected frame are view state, not dirty edits. Clip draft rendering uses prepared thumbnails/source pixels and does not instantiate behaviors/audio or simulate a game. Save uses the native atomic/conflict writer and marks only that asset document saved. Closing/switching project protects unsaved asset tabs as well as scene tabs.

A `.animset` asset document is a lossless JSON object; it must not use scene `parse_document`, which expects an array. Preview description/image limits produce explicit preview status and never silently truncate saved asset data. Asset save notifications coalesce by current content version; dependent previews update once, and the writing tab's own event cannot overwrite newer unsaved edits.

## Acceptance boundary

Test component copy/reset with unknown fields and constructor-default omission; self/descendant/component/cross-root/external/opaque reference sites; identity collision and metadata-unavailable cases; cross-scene same-kind ID collision and mandatory external assignment; default-policy migration; exact redo IDs and single Undo; template format/version/unknown-data preservation; sprite region bounds and group gestures; variable duration and FPS fallback; frame ordering/loop/scrub/save/reopen; external write conflict; dirty asset tabs; and normal game consumption of saved `.scene`/`.animset` data. A template or animation preview alone does not establish game-format correctness.
