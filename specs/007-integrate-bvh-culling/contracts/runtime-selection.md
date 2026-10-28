# Runtime Selection Contract

Status: planned API for feature 007. Existing behavior remains the default. Settings are fixed at runtime construction; there is no live switching, automatic selection or scene migration.

## SDK

Add installed `platformator/spatialsettings.h`, following the existing global RuntimeOptions convention:

```cpp
enum class BroadPhaseMode { Segmented, DynamicBvh };
enum class RenderCullingMode { None, Linear, DynamicBvh, Segmented };
enum class BvhMaintenance { NativePartial, FullRebuild };
enum class BvhQueryExecution { Serial, Parallel };

struct SpatialSettings {
    BroadPhaseMode broadPhase = BroadPhaseMode::Segmented;
    RenderCullingMode renderCulling = RenderCullingMode::None;
    BvhMaintenance bvhMaintenance = BvhMaintenance::NativePartial;
    float bvhPadding = 0.0f;
    BvhQueryExecution bvhQueryExecution = BvhQueryExecution::Parallel;
};
```

Append `SpatialSettings spatialSettings{};` **after** RuntimeOptions.smokeSteps, preserving existing positional aggregate initializers. Include the new header from runtimeoptions.h. Validate enum values and finite nonnegative padding even for embedded consumers. Runtime construction copies settings; later edits to the caller's value do not alter the runtime.

```cpp
RuntimeOptions options;
options.spatialSettings.broadPhase = BroadPhaseMode::DynamicBvh;
options.spatialSettings.renderCulling = RenderCullingMode::Segmented;
platformator::Runtime runtime(options);
```

All 2 x 4 combinations are valid. Physics and visibility contexts have separate state. Inactive BVH controls are accepted and recorded but cause no tree work. A headless runtime may request any render mode; its effective render status is disabled/headless, not an executed visibility benchmark.

Collider gains `bool getIsRegisteredInBroadPhase() const`. Keep existing public Grid-named APIs and explicit geometry preparation methods source compatible. `getIsRegisteredInGrid()` and cell-range diagnostics retain their literal meaning: BVH registration is not Grid registration, and ranges remain invalid unless explicitly calculated. Queue-status getters report the common selected-backend queue. Document the distinction in the installed header and SDK usage guide. No binary ABI guarantee is introduced; rebuild the SDK and consumers together. Public projection types keep their collider-facing constructors and getters.

Box2D remains a private source dependency packed into the engine archive, with no new client package requirement, exposed Box2D types or whole-physics-world integration. Ownership/current-runtime and SDL lifetime contracts remain unchanged.

## CLI mapping

The shared runtime parser, stock runner and benchmark runner's separate parser accept identical tokens:

| Option | Values | Default |
| --- | --- | --- |
| `--broad-phase` | `segmented`, `bvh` | `segmented` |
| `--render-culling` | `none`, `linear`, `bvh`, `segmented` | `none` |
| `--bvh-maintenance` | `native-partial`, `full-rebuild` | `native-partial` |
| `--bvh-padding` | Finite nonnegative world-unit float | `0` |
| `--bvh-query-execution` | `serial`, `parallel` | `parallel` |

Missing/unknown values, invalid enum conversions and invalid padding fail before running with a clear option-specific error and nonzero exit. Existing flags retain their meaning; particularly `--render` creates actual SDL rendering, whereas visibility replay does not. Existing duplicate-option policy is preserved; record the final resolved settings. Selection flags do not imply render activation.

NativePartial uses marked inserts/moves and one partial maintenance pass per changed published batch after initial full construction. FullRebuild applies the same updates followed by full maintenance. Padding controls envelopes, not collision geometry; exact postfilter and retained-pair reconciliation remain required. Parallel applies only to independent read-only collider queries after exclusive writes complete, through existing oneTBB worker control; single-camera queries and SDL calls are sequential.

## Unsupported input and fallback

- Physics BVH batches preflight finite ordered bounds and padded extents below the pinned Box2D default 100,000-unit limit. A failure identifies backend, endpoint and reason, aborts the run and makes the capture ineligible. No automatic switch or omitted collider is allowed.
- Rendering conservatively submits unsupported individual sprites via an ordered uncullable path. Invalid camera/non-default unsupported SDL view state bypasses culling for that frame. Report reasons and counts; do not label a bypass as a successful pure indexed frame. Apply equivalent conservative input handling to all culling modes.
- A failed mandatory query, allocation or lifecycle operation propagates as a run failure after safe completion of C/worker boundaries. Do not silently continue with incomplete results.
- Headless/no-camera behavior remains an early return. Pending render mutations are reconciled before a later supported rendered frame.

## Acceptance

Exercise default legacy callers, all eight selections through SDK and both parser paths, invalid tokens/padding, large unsupported physics bounds, render fallback reporting and source/installed relocated consumers. Validate that only selected contexts allocate/advance. Settings and effective status must appear in every benchmark manifest.
