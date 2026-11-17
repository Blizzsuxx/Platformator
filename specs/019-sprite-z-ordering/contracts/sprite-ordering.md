# Contract: Runtime Sprite Ordering, Culling and Persistence

## Public interface

Add to the existing public `Sprite` component:

```cpp
std::int32_t getZOrder() const;
void setZOrder(std::int32_t zOrder);
```

Include `<cstdint>` in the public header. The default is zero for every constructor, including default construction. Existing constructor overloads and callers remain valid. No manual invalidation call or new runtime/window configuration is required. Cloning copies Z Order as authored data; it does not copy registration precedence.

The setter is owner-thread mutation under the engine's established public lifecycle contract, like other Sprite setters. It does not add cross-thread mutation support. An identical assignment is a no-op. A change committed before render preparation affects that frame; intermediate assignments before preparation are coalesced. The typed setter needs no redundant range check or exception wrapper.

## Render culling policy

Resolve the existing process-local `PLATFORMATOR_RENDER_CULLING_MODE` selector at window construction. Culling remains enabled by default through `WindowSettings::enableRenderCulling`.

| Selector with culling enabled and a live renderer | Result |
|---|---|
| Unset, empty, or `cached-linear` | Cached-linear visibility. A view change scans cached bounds. Clean fixed views reuse membership. Grid-specific storage and work are zero. |
| `indexed` | Explicit render-owned sparse grid. An eligible camera pan queries local cells and conservative exceptions. |
| `current-linear` | Direct linear classification during traversal of the prepared draw order. |
| Unknown, nonempty value | Existing startup error, before sprite registration. |

The window validates the selector at construction even when culling is disabled. With a valid selector, `--no-render-culling` or `enableRenderCulling=false` selects submit-all. Headless or renderer-free execution creates no selection, index or draw-order storage. A custom SDL mapping bypasses culling for that frame. An uncertifiable query takes the existing conservative traversal and applies the normal per-sprite classifier only where eligible. Both paths preserve compositing and resume configured selection on a later eligible frame. Strategy selection has no asset field, public enum or automatic threshold.

## Scene drawing behavior

The total key is `(signed zOrder, registrationSequence)`, ascending. Higher Z Order therefore appears in front for overlapping sprites. The tie sequence is assigned on actual registration, independent of address, file ID, dense slot, texture, worker scheduling or culling traversal.

- Moving the last dense slot into a removal hole does not change the survivor's sequence.
- View exit/reentry, texture availability, animation and z changes away from/back to a tie retain a surviving registration's sequence.
- Actual removal/re-registration renews precedence. Current activation reconciliation includes such removal/re-registration; activity changes do not silently promise a permanently stable key.
- Asset load/instantiation establishes registration precedence through deterministic serialized traversal. Runtime registration history is not saved. Distinct authored Z Order values express persistent explicit precedence across reloads.
- Culling modes and conservative fallback agree on overlapping visible compositing. They may submit different safely rejected offscreen candidates.
- Existing individual `Camera::render`/`renderWithView` calls remain immediate caller-ordered draws. Window scene ordering does not buffer or reorder arbitrary user submissions.

Existing transparency, source-region, transform, debug-overlay and presentation behavior remains. A Z Order edit alone does not dirty geometry, bounds, spatial cells or visibility.

## Asset representation

Add an ordinary Sprite component member alongside its existing fields:

```json
"zOrder": -10
```

On read, an omitted member means zero, including when the destination was previously nonzero. On write, emit the current integer. No scene envelope, asset version bump, migration step, sequence member or parent-order inheritance is introduced.

Accepted present values are JSON integers in [-2147483648, 2147483647], including representable nonnegative unsigned encodings. Reject null, boolean, floating-point encoding (including `1.0`), string/container values and integers outside the domain before narrowing. Errors use established deserialization/scene-preparation failures; invalid candidate scenes do not replace the live scene. The setter itself accepts an already representable `std::int32_t`.

Scene/template save/load, runtime template instantiation, clone, duplicate, component settings copy/paste, and template overrides retain the authored value. Old assets continue to load without a field. Initial all-zero stacking follows prior registration traversal; stable survivor order after dense removals deliberately replaces the old incidental storage-rank order.

## Performance/lifetime guarantees

On a warmed, valid candidate subset, changing a rejected sprite's Z Order does zero draw-order preparation, key comparisons, bounds refreshes, migrations or visibility queries. If a later camera move or conservative fallback admits that sprite, its first prepared frame uses the latest authored value. A sprite retained conservatively counts as a candidate. A prepared all-sprite set treats every registered slot as a candidate.

With unchanged retained keys, a camera query that returns the same members in a different traversal order reuses the prepared draw list. Exchanging one member for another changes membership even when the count stays constant. Preparing a changed indexed selection may sort once by draw key. Sorting the selection by storage rank is unnecessary. Registering or removing a sprite invalidates cached rank identities before they are reused. After warmup at sufficient capacity, preparation adds zero recurring temporary allocations.

Headless/renderer-free execution stores the authored property but allocates/maintains no ordering state. Registration/removal invalidates cached rank entries before retirement. Successful scene replacement/clear and shutdown release prior-scene ordering storage at existing commit boundaries; failed scene preparation preserves the current state.

## Deliberate compatibility choices

`getZOrder`/`setZOrder` follow Platformator naming rather than introducing Unity-style property syntax. The signed-32 range and lack of named sorting layers are deliberate scope decisions. No Unity-specific tie behavior is promised. See [research.md](../research.md) for the checked upstream reference and alternatives.
