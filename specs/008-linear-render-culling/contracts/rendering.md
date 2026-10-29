# Rendering and Configuration Contract

## Public configuration

Append to `WindowSettings`:

```cpp
bool enableRenderCulling = true;
```

`RuntimeOptions::windowSettings` is the sole configuration path. The value is copied during runtime creation. No later setter, per-sprite flag, visibility callback, or scene serialization field is introduced.

Example consumer selection:

```cpp
RuntimeOptions options;
options.windowSettings.enableRenderCulling = false;
platformator::Runtime runtime(options);
```

Existing aggregate initializations that omit the appended field keep the default. C++ consumers rebuild against the changed settings definition; stable binary layout across header revisions is not promised.

## CLI adapter

`--no-render-culling` is a flag with no value. The ordinary runtime parser, existing benchmark runner, and focused rendering probe set the same field false when present. Omission means enabled for window rendering. Repeating the flag is idempotent; existing unknown-argument handling remains in force. `--headless` retains its meaning and performs no visibility work regardless of this setting.

The existing benchmark manifest includes an additive `linear_render_culling` metadata object recording requested mode and effective eligibility policy. Do not rename scopes, raw columns, or schema version to add a boolean configuration value. The focused probe records the fuller provenance contract below.

## Drawing behavior

For normal registered-window-sprite rendering:

- Existing active filtering and current submission order remain authoritative.
- A frame view is prepared once from the current main camera and output dimensions.
- A sprite is skipped only if finite conservative bounds of its actual destination geometry are strictly outside the view. Boundary contact is retained.
- Bounds follow camera mapping and output-space rotation, include outward numerical/raster allowance, and are independent of colliders.
- Retained sprites use unchanged texture/source rectangle, destination rectangle, angle, center, and flip arguments. Drawing geometry is computed once.
- Invalid or uncertain geometry follows the reference submission path. This does not add rendering support for previously unsupported input or promise a different SDL error result.
- Debug overlays, clear/present, and no-camera/no-renderer paths retain existing behavior.

## Existing Camera method

The public signature remains:

```cpp
void Camera::render(Sprite *sprite, SDL_Renderer *renderer,
                    int outputWidth, int outputHeight);
```

Direct calls preserve submit-all semantics, including use with custom SDL render targets. This is a low-level draw entry point; the window's opt-out setting controls its own registered sprite traversal. Internally, private view preparation and `renderWithView` share the geometry and submission code. No new public bounds or prepared-draw API is added.

## Custom renderer state

Because consumers can obtain the renderer, enabled window culling must verify default mapping once per frame using SDL's existing renderer-state queries. A custom render target, explicitly set viewport, non-identity render scale, enabled logical presentation, unexpected output dimensions, or failed required query selects submit-all for that frame. Do not mutate/reset the state to make it eligible. A smaller clip rectangle may retain extra candidates but cannot make an otherwise outside sprite visible.

Test both directions: default mapping permits culling; a shifted viewport/scale/target/logical mapping retains reference behavior. Clearing custom state restores eligibility on the next frame without a notification. Disabled mode skips these eligibility checks entirely.

## Internal geometry helper

The internal helper consumes already computed destination geometry, angle, and immutable view. It neither reads/mutates engine objects nor calls a draw function. The ordinary path allocates no memory. Its result is only a conservative reject decision; it is not exposed as public visibility state or collision data.

Zero rotation avoids trigonometry. The rotated implementation uses transformed destination corners with SDL-compatible angle conversion. Bounds expand each minimum/maximum outward with `nextafter`; the raster/error guard is `1 output pixel + 8 × float_epsilon × maximum_absolute_intermediate_coordinate`. If the rounding term is non-finite or at least one output pixel, classification is unclassifiable and the sprite is submitted. The independent oracle and image fixtures are acceptance gates for this numerical policy. Any future cheaper formula must preserve these gates.
