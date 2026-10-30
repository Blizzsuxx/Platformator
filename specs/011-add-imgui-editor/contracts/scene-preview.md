# Contract: Render-Only Scene Preview

## Interface boundary

The editor owns an internal `EditorPreviewAccess` adapter. Its frame operation accepts the active Runtime, an explicit ordered list of current proxy Sprites, a value-only `SpriteRenderView`, and a target texture allocated by that Runtime's renderer. It performs owner-thread transform publication and shared sprite drawing, then returns without presenting. Do not expose editor types/ImGui through installed engine headers.

Refactor only the view-based geometry/submission currently in `Camera::renderWithView`/`prepareSpriteGeometry` into a shared internal helper. Normal Camera/SDLWindow rendering delegates to it while retaining existing culling, submission counters, ordering and clear/present semantics. A narrow private friendship allows the editor adapter to call transform synchronization; it is not a general public arbitrary-phase or lifecycle API.

## Preconditions and exclusions

- One live editor Runtime, invoked on its owning thread, with a positive finite view mapping and valid target extent; zero extent suspends drawing rather than constructing invalid scale values.
- The Runtime contains only editor-created flattened root GameObjects and Sprite components. It is never loaded from the authoritative authoring file. No Script, Audio, Rigidbody, Collider, or Animator component enters that Runtime.
- No `simulateFrame`, `simulateAndRenderFrame`, `run`, script resolve/start/update, physics integration, audio update/autoplay, or user pre-render callback is invoked. Empty engine services/backend initialization does not mean gameplay ran.
- Editor preview rendering never changes `mainCamera`, saved camera JSON, physics queues, or normal renderer culling-cache identity. It performs no hidden scene save/reload.

## Document conversion

Compute world translation by parent-first accumulation matching current translation-only parenting. Copy each object's local scale/rotation and active rule; do not inherit parent scale/rotation or impose a new active-in-hierarchy rule. Apply scaled sprite dimensions exactly as `Sprite` does. Carry source rectangle/flip and selected animator-frame data consistently with current engine serialization and preview semantics. Do not advance frames merely because an animator is marked playing.

Maintain explicit authoring draw ranks validated against equivalent runtime-loaded scene fixtures. Do not use unordered-map iteration or assume swap-removal of SDLWindow's registration vector preserves the document order. Submit the explicit ordered proxy list; removal invalidates ordered borrowed pointers before destroying objects. Selection/picking uses the same resolved geometry/order, with overlays for inactive/missing-resource objects.

Only geometry/texture/eligibility changes reconcile proxies. Ancestor edits update affected descendants; repeated value notifications in one frame can coalesce. Structural/root/document changes can rebuild derived order/proxies when justified, with costs recorded. Missing-asset errors are cached until relevant change/explicit reload; release obsolete cache entries with their document/root generation.

Read static AnimationClip data through retained engine asset references; use frame source/texture and clip dimensions following existing animator rules. Invalid frame index or failed asset loading produces a labeled editor marker/validation issue, never replacement game content. Known unresolved sprites are skipped by engine sprite submission; their editor handles stay selectable.

## Renderer state and pixels

1. Observe current target and its viewport/clip/scale/logical-presentation state plus renderer draw color/blend state.
2. Bind the preview target, establish explicit target-local mapping, clear it, and draw sprites through the shared engine helper. `view.cullingEligible=false`; do not reuse indexed/cached camera visibility for this nondefault target.
3. Restore both the preview target's modified mapping and the previously selected target/mapping, color/blend state on success and failure. SDL mapping state is per target, so restoring target selection alone is insufficient if an existing target was temporarily modified.
4. Return the reusable texture for ImGui composition. EditorApp clears/composes the main output and calls `SDL_RenderPresent` once. Preview never clears/presents the main UI output or destroys/replaces a target still referenced by the current ImGui draw data.

Target size uses Scene image pixel extent, not full-window size. Convert ImGui logical mouse positions through the Scene image rectangle and actual SDL window/pixel ratios; zoom/style scale alone is not the framebuffer ratio. Picking and dragging invert the exact view mapping. Camera pan/zoom never writes game camera properties. Test off-origin image rectangles, unequal target/window extents, resize, 150% style, and real high-density ratios independently.

## Lifetime and failure

Runtime owns proxy GameObjects/assets and SDL window/renderer. Preview bridge owns its mapping, ordered pointers, retained clip references and target texture. Destroy references/texture before Runtime. ImGui renderer backend shutdown also precedes Runtime destruction. Changing asset roots releases the old bridge/backend session and recreates the Runtime at a frame boundary, because asset caches are root-specific; preserve document/history and restore UI preferences.

A failed individual resource does not prevent unrelated scene editing. An invalid Runtime/renderer/target aborts preview with a visible error and preserves the document. Do not catch all internal invariant failures and pretend a valid frame was produced.

## Required proofs

- Compare corresponding images in memory and ordered submissions for valid runtime/reference versus preview fixtures: parent translation, local scale/rotation, source rectangles/flips, alpha overlap, active rules, animator frame, scene order, and output mapping.
- Prove preview construction excludes authored behavior/audio/body/collider creation; rendering repeatedly leaves behavior/physics/audio counters and main-camera identity unchanged.
- Verify complete renderer-state restoration for nondefault targets/viewports/clip/scale/logical presentation, including failed resource/target paths.
- Exercise missing assets, deleted selections, no camera/sprites, zero-size/hide/minimize, root replacement, 20 recreation cycles, and references released before renderer teardown under supported sanitizers.
- Run existing render-culling/geometry/contract and runtime-step/recreation tests for the shared helper change. No normal retained frame images/identity lists; failure diagnostics alone may use a small explicit cap.
