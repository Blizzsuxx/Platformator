# Data Model: Linear Rendering Culling

## Window configuration

**Owner**: Existing `WindowSettings`, copied into `SDLWindow` at runtime construction.

**Added field**: `bool enableRenderCulling = true`, appended after existing fields. False selects existing submit-all semantics for the window's sprite loop. It is not serialized into scenes and has no runtime mutation API. Both CLI opt-outs map to this field; no second mode state is retained.

## SpriteRenderView

**Owner/lifetime**: Immutable value created by camera preparation for one window render. Internal definition in `src/spriterendergeometry.h`, forward-declared for private Camera methods.

**Fields**: Camera world X/Y values, float scale X/Y from the existing camera/output mapping, output width/height, and an effective-culling boolean. Store values rather than references. Mapping validity/custom SDL state can disable filtering for this frame without changing the configuration value.

**Relationships**: One view is reused by all eligible sprites in a window render. The unchanged public Camera drawing method creates one uncullled view for its individual call. No view is stored on Sprite, GameObject, camera, or window between frames.

**Validation**: Culling requires positive finite mapping/output and the default full-output SDL state. Existing drawing behavior still applies when classification is unavailable. Camera/output state is snapshotted after early exits and rebuilt on every rendered frame; resize/replacement cannot leave stale state.

## Per-sprite drawing geometry

**Owner/lifetime**: Stack values inside `Camera::renderWithView`.

**Fields**: Existing destination `SDL_FRect`, the actual degree rotation, and optional conservative min/max X/Y bounds. Texture/source rectangle and flip remain borrowed existing inputs to SDL. No texture, object, component, or pointer is copied into a long-lived record.

**Rules**:

- Width/height include the existing GameObject scale; position comes from the current synchronized world position used by drawing.
- Prepare the same rectangle once for classification and drawing.
- Zero rotation uses the rectangle; rotated bounds enclose all transformed destination corners in output space, expand with `nextafter`, and add a `1 + 8 × float_epsilon × maximum_absolute_intermediate_coordinate` output-pixel guard. If the rounding allowance is at least one pixel or cannot be computed safely, the input is unclassifiable and retained.
- A reject is allowed only when these bounds are strictly disjoint from `[0, outputWidth] × [0, outputHeight]`.
- Unclassifiable inputs are retained for existing submission handling.

## Renderable membership and order

**Owner**: Existing `SDLWindow::spriteComponents` and its existing registration/index rules.

No new entity or cache is introduced. Filtering takes a subsequence of the current active sprite sequence. Insertions, swap-removal, reactivation, and replacement can change that authoritative sequence exactly as before; culling does not stabilize or sort it.

## Validation-only observations

**Owner**: Work-probe build support and the test/probe fixture, all on the owning thread.

**Fields**: View/eligibility/visibility-check/submission counters, ordered submitted sprite identities, and a scoped pre-presentation readback observer. Frame traces use pre-reserved storage. Fixture slot IDs provide comparison identities across independently created runtimes; raw object addresses are not cross-run identifiers.

**Lifetime**: Reset/attach before a validation frame, consume after it, and detach with RAII before runtime destruction. The observer only reads output; it does not mutate the runtime. Observation code is compiled out when work probes are disabled. No production callback member is added.

## Benchmark case and evidence

**Owner**: Shared feature fixture and focused capture driver, not runtime state.

A case records profile, population, seed, fixed dt, output/camera dimensions, texture geometry, rotation/layout distribution, continuous trajectory parameters, warmup/measurement steps, and worker count. A capture records pre-feature/reference/linear mode, actual renderer, source and harness hashes, schema-v3 outputs, and validation evidence identity. Validation observations and raw images live separately from timing samples.

## State transitions

- Start rendered frame → prepare current view → inspect each current eligible sprite → reject or submit → discard view.
- Renderer/camera absent → preserve existing early exit; no view exists.
- Custom renderer mapping/query failure → uncullled prepared view for that frame.
- Headless simulation → none of these culling states or transitions occur.
- Scene/camera/sprite mutation → existing engine state changes; next render reads current values with no additional invalidation transition.
