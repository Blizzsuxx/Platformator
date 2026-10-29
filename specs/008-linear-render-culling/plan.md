# Implementation Plan: Linear Rendering Culling

**Branch**: `branch-without-bvh` | **Date**: 2026-10-28 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/008-linear-render-culling/spec.md`

The setup script reports feature identifier `008-linear-render-culling` in its `BRANCH` field because it resolves `.specify/feature.json`; the actual Git branch remains `branch-without-bvh`.

## Summary

Address audit P03 with one camera/output snapshot per rendered frame and a linear, ordered sprite filter. Compute the existing destination rectangle once and reuse it for conservative visibility and SDL submission. Begin without persistent sprite bounds, visibility lists, dirty notifications, or physics changes. Keep the common zero-rotation path cheap and preserve all supported drawing semantics.

The only new runtime configuration is `WindowSettings::enableRenderCulling`, default true, with `--no-render-culling` adapters. Keep the public four-argument `Camera::render` method unchanged. A private prepared-view path is shared with `SDLWindow`. Validate actual images and order separately from timing, then compare pre-feature, disabled, and enabled runtimes through a focused rendering probe using the existing recorder.

## Technical Context

**Language/Version**: C++20 runtime and tests; Python 3 standard-library capture driver.

**Primary Dependencies**: Existing SDL 3.4.18 / SDL_image 3.4.8 / SDL_ttf 3.2.2 / SDL_mixer 3.2.4, Eigen 5.0.1, oneTBB 2022.3.0, and nlohmann JSON. No new dependency or vendored edits.

**Storage**: No persisted runtime state. Existing schema-v3 timing summaries, raw samples, manifests, and separate validation images/traces/resource observations; retain captures outside tracked source and link their hashes/location from a concise evidence report.

**Testing**: Existing CTest/C++ executable conventions, new focused geometry and rendered-integration tests, work-probe assertions, ASan/UBSan, existing public-header/consumer and benchmark-contract checks, focused Python capture-contract checks.

**Target Platform**: Existing SDL desktop targets; first reproducible measurements on available Linux host. Deterministic SDL software output for mandatory image validation; hardware-renderer runs when available, reported separately.

**Project Type**: Native engine library with desktop runner, examples, tests, and benchmark tools.

**Performance Goals**: O(N) visibility/drawing preparation, O(1) additional runtime storage, no culling allocation or headless work; reduced complete render time beyond observed variation in a representative mostly-offscreen workload. All-visible and rotated-sprite overhead must be measured. No invented percentage speedup target.

**Constraints**: Preserve ordering, visible pixels, immediate existing transform semantics, source rectangles/flips, debug overlays, public scene format, and collision/physics behavior. Keep added state on the render stack; no backend abstraction, persistent index, cache, new worker pool, or per-sprite telemetry in ordinary timing/production builds.

**Scale/Scope**: Tiny through 10,000-sprite controlled fixtures; one main camera and current submission order. Full-window default renderer mapping is culled; custom renderer mappings conservatively use submit-all. Baseline commit: `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75`.

## Constitution Check

*Gate evaluated before research and after design. Both pass; no exceptions requested.*

| Principle | Before research | After design |
|-----------|-----------------|--------------|
| I. Performance/scalability | Compare linear scan, caching, and indexed alternatives; inspect actual hot path. | One prepared view, one traversal, zero-angle fast path, no allocations; measure real render/step impact and large dynamic scenes. |
| II. Multicore | Evaluate parallel visibility independently of ordered drawing. | Sequential owning-thread pass chosen: parallel classification adds storage, scheduling, and a serial merge/submission boundary. Check one and physical-core worker configurations without changing existing oneTBB physics. Revisit only if profiling shows material classification cost. |
| III. Collision architecture | No broad-phase change is authorized. | No collider/index/pair-state dependencies or modifications. Collision paper-specific implementation analysis is not required for this rendering-only change. |
| IV. KISS | Avoid reinstating experiment infrastructure. | One boolean, transient view, private geometry helper, existing order, no dirty hooks or persisted visibility. Focused probe reuses recorder and shared test fixture. |
| V. Developer experience | Existing scenes/components must work automatically. | Default-enabled window culling; explicit opt-out; no manual visibility API; existing direct Camera calls preserve submit-all behavior. C++ consumers rebuild for appended settings field. No Unity behavior claim or migration to a different camera model. |
| VI. Evidence | Record current-branch baseline before engine edits. | Common harness across pre-feature/disabled/enabled builds; actual images/traces before independent timing; retained raw data and source/renderer/worker provenance. No speedup claim at planning time. |
| VII. Correctness/ownership | Read existing render/lifecycle boundaries. | Immutable view and borrowed Sprite access only inside current render phase; no pointer retention. Conservative fallback for unclassifiable geometry and custom renderer state; lifecycle and numerical-edge coverage. |

## Project Structure

### Documentation (this feature)

```text
specs/008-linear-render-culling/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── checklists/requirements.md
└── contracts/
    ├── rendering.md
    └── validation-evidence.md
```

`tasks.md` is generated by the later tasks command, not this planning phase.

### Source Code (planned changes)

```text
include/platformator/
├── windowsettings.h             # append default-true opt-out setting
└── camera.h                     # preserve public method; private prepared-view methods/friend
src/
├── camera.cpp                   # prepare mapping once; shared geometry/draw path
├── spriterendergeometry.h        # internal value types and conservative bounds helper
├── sdlwindow.cpp                # prepare once and traverse existing ordered sprites
├── runtimeoptions.cpp           # normal CLI adapter
└── benchmark_main.cpp           # existing benchmark CLI adapter and mode metadata
tests/
├── render_culling_fixture.h      # feature-specific deterministic scene/trajectory fixture
├── render_culling_geometry_test.cpp
├── render_culling_test.cpp       # actual frame-image, lifecycle, mode, and trace tests
├── runtime_work_probe.h          # counters/trace/readback seam, work-probes only
└── consumer/main.cpp             # new setting public-consumer exercise as appropriate
benchmark/
├── linear_render_probe.cpp      # focused scenario driver, existing BenchmarkRecorder output
├── capture_linear_render.py     # bounded case matrix, comparisons, provenance
├── tests/test_linear_render.py  # meaningful capture rejection/provenance tests
└── README.md                    # focused reproduction and output scope documentation
CMakeLists.txt                   # targets/test registration and probe fingerprint inputs
```

**Structure Decision**: Use existing public/private split. The runtime addition is one internal stateless helper and small integrations in current camera/window code. Test/probe code owns all fixture trajectories, trace buffers, image capture, and reporting. Do not modify transform setters, GameManager synchronization, physics, or Sprite membership to support culling.

## Phase 0: Research Outcome

[research.md](research.md) resolves geometry order and rounding, API compatibility, custom renderer state, cache/parallel alternatives, and measurement gaps. Findings rely on current local primary source. No new library is needed. No unresolved clarification remains.

## Phase 1: Design

### Render flow and hot paths

1. Preserve existing renderer-null and no-camera exits, clear/presentation behavior, and debug overlay preparation.
2. `SDLWindow` asks the camera to prepare an immutable `SpriteRenderView` with camera world-position values, the existing float scale factors, output dimensions, and effective culling eligibility. When the option is disabled, skip renderer-state eligibility queries.
3. For enabled mode, check renderer target/viewport/scale/logical presentation and current output dimensions once. If the mapping differs from the engine's default full-output mapping or a query fails, set effective culling false for the frame. Do not alter consumer SDL state.
4. Traverse `spriteComponents` exactly as today, preserving active filtering and relative order. Call private `Camera::renderWithView` for each eligible sprite.
5. Build the exact existing destination `SDL_FRect` once. If effective culling is enabled, compute conservative output bounds and reject only strict outside cases. Draw retained sprites with the same angle, texture, source rectangle, center, and flip.
6. Render debug overlays and present as today. Work-probe builds may record actual submission order and read pixels immediately before presentation; those seams compile out of production and timing builds.

The public `Camera::render(Sprite*, SDL_Renderer*, int, int)` remains a direct submit-all draw. It creates an uncullled one-call view and uses the same geometry/draw implementation, avoiding duplicate formulas. Private preparation and drawing methods accept/return an internal detail type declared in the camera header and defined in the internal geometry header; `SDLWindow` friendship is limited to using these methods.

### Geometry and numerical contract

The renderer first applies independent camera X/Y scales, then rotates in output coordinates. Bounds must follow that order. The zero-angle path uses the destination rectangle directly. The initial rotated path obtains the same degree-derived float angle as SDL, transforms four corners, and forms an enclosing rectangle. Expand extrema outward and retain a one-pixel output guard for rasterization edge uncertainty. Retain boundary contact; do not shrink the view. Invalid output, non-finite intermediate results, negative/degenerate sizes, or uncertain classification use submit-all behavior.

A double-precision corner oracle and actual output-image comparisons independently validate rejection. Include near-quadrant and fractional-pixel angles, highly unequal output scales, extremely elongated sprites, large positions, edge-adjacent representable float values, source rectangles, flips, and supported scales. Do not use the cached GameObject radians-based trig until parity and benefit are established.

### Ownership, lifetime, and complexity

`SDLWindow` retains its existing sprite membership and camera ownership rules. The view is a value snapshot for one render call; per-sprite geometry is local and no Sprite pointer survives its ordinary submission. All classification/drawing occurs on the owning thread after existing simulation/structural reconciliation. No new worker mutation, reclamation, callbacks in production, or synchronization exists.

Frame setup is O(1); traversal/preparation is O(N), with O(V) draw calls for V retained sprites. Additional runtime retained memory and temporary stack use are O(1). With no cache, unchanged scenes still take O(N); this is a deliberate tradeoff against invalidation and cross-system notifications, to be checked in static and dynamic cases. Headless simulation has zero view preparation and no culling metadata updates. Rotation incurs extra math only when enabled; default zero-angle sprites take the fast path.

### Configuration and compatibility

See [contracts/rendering.md](contracts/rendering.md). Append `WindowSettings::enableRenderCulling = true`; `--no-render-culling` sets false in both existing CLI entry points and the focused probe. No runtime toggling or serialized data changes. A custom viewport, target, scale, or logical presentation automatically falls back per frame. No new exception or silent correction is introduced for existing geometry.

### Validation and performance design

See [contracts/validation-evidence.md](contracts/validation-evidence.md) for exact cases, capture acceptance, and baseline provenance. First build the feature-specific common fixture/probe against the pre-feature source and retain reference images/traces/captures. Then implement geometry/filtering, prove image/order/lifecycle correctness, and compare candidate modes. The baseline harness uses a capability check for the appended field and accepts reference mode only on the old source.

The recorder remains schema v3 with the same eight scopes and eleven counters. Feature metadata is additive and namespaced; frame submission counts/order and heap/resource probes are captured in separate validation evidence. Extend fingerprint input coverage to the new probe/shared fixture; record source and common-harness hashes on both builds. Keep the generic benchmark sweep scenario list unchanged; reuse its strict CSV parser in the focused driver.

Evaluate complete render, complete_step separately, run-to-run spread, p95/p99 and observed worst frames. A bounded, non-Cartesian set covers tiny/large populations, visible fraction, skewed layout, unchanged frames, continuous sprite boundary crossings, camera-only motion, churn, and rotated sprites. One-worker results are supplemented with selected physical-core-worker cases and existing physics/headless controls. Allocation checks separate culling-owned work from SDL/driver behavior. Performance beyond measurement variability must be investigated; do not solve regressions by silently changing geometry or dropping cases.

### Phase 1 gate recheck

At design review, all seven constitution gates were covered by the proposed design; that review did not report measured performance. The final implementation, bounded evidence, and acceptance status are recorded under [Implementation status](#implementation-status-2026-10-29) and in `evidence/` below.

## Complexity Tracking

No constitution violations or exemptions. The additional configuration value exists solely for benchmark/reference rendering and troubleshooting; it is owned by existing window settings. The transient view removes repeated camera divisions; the stateless helper centralizes geometry. Every trace, capture hook, and fixture is test/benchmark-only. Persistent caching and parallel classification remain alternatives requiring evidence, not planned infrastructure.

## Implementation status (2026-10-29)

The planned setting, prepared view, stateless conservative geometry helper, existing-order window traversal, custom SDL mapping fallback, work-probe observation seam, shared fixture, schema-v3 probe, capture driver, and consumer documentation are implemented. The realized numerical guard is `1 + 8 × float_epsilon × maximum_absolute_intermediate_coordinate` output pixels, with `nextafter` expansion; if the rounding allowance reaches one pixel or an input is otherwise uncertain, the sprite is submitted. Actual renderer and VSync identity is queried for focused probe proofs and timing manifests. Renderer controls through the pre-existing stock runner use the SDL software-driver environment, but its manifest does not report a queried renderer name.

The final correctness, resource, and timing evidence covers the complete bounded matrix in [evidence/coverage.md](evidence/coverage.md): 17 workloads, matched proof/resource captures, selected six-worker cases, and 315 timed processes. Sparse gains exceed observed variation; visible, headless, tail, and stock engine-control costs were investigated. SC-005 is accepted for the tested SDL software renderer and workload bounds; hardware rendering and GPU elapsed time remain unverified.
