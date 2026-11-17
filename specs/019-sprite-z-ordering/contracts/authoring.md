# Contract: Z Order Authoring and Preview

## Inspector and commands

Sprite inspection exposes **Z Order**, default **0**, using the existing integer field control with the runtime's signed-32 bounds. The property participates in semantic/accessibility field discovery, search, normal selection and supported multi-selection editing, undo/redo, template override indication/revert and document dirty state.

Use ordinary field path `zOrder` and the existing Sprite document-change category. A malformed present value is an affected-object validation error that blocks invalid run/asset use through current mechanisms. A rejected editor input leaves the committed document unchanged. Missing legacy values are valid and inspect as zero; merely opening an asset does not require rewriting it.

Adding the field to the Sprite component default record also includes it in existing generic component settings copy/paste. Template field propagation is dynamic and includes `zOrder` without a special field allowlist or new template metadata. Saving/reopening, duplicating, runtime instantiation and existing override/revert flows retain the committed value.

## Preview

Preview and runtime use the same ordering direction and registration-lifetime tie rule. A committed edit appears in the next preview prepared through existing document synchronization, including while paused. Ordering-only edits dirty image/draw order without reloading textures or clips or refreshing spatial geometry.

Preview's manual camera submissions use its cached visual-ID order. Document traversal and hierarchy order remain independently available. Equal-value live proxy registrations retain precedence through ordinary movement/view/texture changes and unrelated deletions. A newly registered/reactivated proxy takes new precedence. Reopening the asset rebuilds ties deterministically from serialized traversal; gameplay registration history is not authored data.

Do not add z-dependent bounds, inherited parent depth, per-camera order, automatic Y sorting, or a new preview spatial index. Existing camera/hidden/locked/asset-error behavior remains.

## Overlap selection

Sprite-bearing pick targets follow cached visual order, traversed in reverse by the existing click-cycle logic so the highest visible sprite is considered first among overlapping sprite-bearing objects. Apply existing hidden/locked filters. Preserve non-sprite authoring target slots and the existing within-object collider/camera/sprite priorities. Repeated-click cycling continues; changing candidate order naturally resets its sequence under the current cycle contract.

Use the already prepared visual IDs to order only the sprite-bearing subsequence of pickable objects. Do not sort on every frame, discard camera/collider-only objects, add a permanent pick-order map, or change hierarchy/gizmo/marquee-selection policy.

## Required validation

Extend existing Inspector, preview/viewport, diagnostics, template and component-copy tests. Verify a saved negative value, both accepted extremes, omitted legacy defaults, invalid type/range errors, edit/undo/redo, overrides/revert, duplicated components, and front-to-back overlap picking. Check that an order-only edit performs no asset refresh and repeated unchanged preview frames do no ordering preparation. Pixel/order checks stay in memory; retain detailed diagnostics only for a specific bounded failure.
