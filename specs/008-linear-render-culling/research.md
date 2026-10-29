# Research: Linear Rendering Culling

**Date**: 2026-10-28
**Feature**: [spec.md](spec.md)
**Source inspected**: `branch-without-bvh`, commit `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75`, with feature documentation untracked. No engine implementation changes or performance runs were made during planning.

## Questions resolved

Research addressed geometry parity, integration ownership, opt-out compatibility, caching, parallel execution, and the smallest adequate validation path. Two read-only research agents inspected rendering/SDL geometry and existing measurement/test facilities. All design questions are resolved below; expected performance remains unmeasured on this branch.

## 1. Filter inside the existing submission path

**Decision**: Prepare an immutable camera/output snapshot once per window render. Iterate `SDLWindow::spriteComponents` in its current order. Construct each sprite's existing destination rectangle once, reject clearly invisible rectangles, then use the existing texture drawing call for retained sprites. Do not construct a visible-sprite vector or sort.

**Rationale**: The audit's P03 finding identifies both repeated camera scale computation and offscreen submissions. `src/sdlwindow.cpp:229` already owns the ordered loop and early exits. `src/camera.cpp:30` owns destination geometry. Sharing that geometry avoids doing transforms twice and allows all-visible comparisons to reveal the filtering cost.

**Alternatives considered**:

- A separate prepass followed by existing `Camera::render`: duplicates geometry work or needs a retained prepared-geometry buffer.
- Bounds/visibility caches: reduce unchanged-frame arithmetic but introduce mutation hooks, membership tracking, and invalidation. Not justified before measurement on this branch.
- BVH or segmented spatial indexes: deliberately outside scope and add maintenance costs already investigated in the archived experiment.
- SDL's own clipping: retains every CPU submission; it is the disabled reference behavior.

## 2. Use actual output-space geometry

**Decision**: Derive bounds from the same `SDL_FRect` and angle used for drawing. Camera mapping scales X and Y independently, then SDL rotates the destination rectangle around its center. Use an unrotated fast path for zero angle and a conservative rotated rectangle for other angles.

For a destination rectangle `(x, y, w, h)`, the center is `(x + w/2, y + h/2)`. In real arithmetic its rotated enclosing half-extents are:

- `ex = abs(cos(angle)) * abs(w)/2 + abs(sin(angle)) * abs(h)/2`
- `ey = abs(sin(angle)) * abs(w)/2 + abs(cos(angle)) * abs(h)/2`

**Initial numerical choice**: For rotated sprites, compute the four transformed screen corners using SDL-compatible float angle conversion and rotation arithmetic, then take their min/max. Expand finite extrema outward with `nextafter` and a one-output-pixel rasterization guard. The one-pixel guard is a conservative policy to validate against actual backend pixels, not a claimed universal arithmetic-error theorem; it may retain a thin band of invisible edge sprites. Matching SDL's staged float corner operations and outward rounding establish the geometry enclosure. The guard is part of the conservative bounds, not an inset into the view. Use strict outside comparisons so contact with a boundary remains included. The numerical allowance must be documented and exercised with fractional/edge fixtures on the tested backend. Unknown/non-finite/overflowing geometry is retained. Negative or degenerate dimensions initially use the reference submission path rather than introduce changed handling.

The initial zero-angle path uses the already prepared rectangle with the same outward expansion and no trigonometry. Read the rotation once. Match the degree angle actually passed to SDL; do not initially substitute GameObject's cached sine/cosine, which came through a different float conversion path. A later analytic half-extents optimization needs the independent corner oracle and benchmark evidence before replacing this conservative first version.

**Rationale**: World-space bounds followed by camera scaling describe a different transform order when camera scales differ. Sprite width/height already include GameObject scale (`src/sprite.cpp:91`). Flips and source rectangles do not enlarge the destination quad. The camera position is the current view origin; do not change camera semantics or infer collider bounds.

**Primary local evidence**: `src/camera.cpp:30`, `src/gameobject.cpp:33`, `src/gameobject.cpp:108`, and read-only vendored SDL 3.4.18 `src/render/SDL_render.c:4237` (rotation entry), `:4284` (default center), `:4314` (degree conversion and float trig), plus `include/SDL3/SDL_render.h` (drawing contract). No vendored modifications are planned.

**Alternatives considered**: A circle or angle-independent square avoids trigonometry but retains more invisible elongated sprites. An analytic rectangle is cheaper than four corners but needs a careful float error allowance. Cached GameObject trigonometry can differ at boundary pixels. Choose correctness and the common zero-angle fast path first; measure rotated workloads explicitly.

## 3. Keep configuration and APIs narrow

**Decision**: Append `bool enableRenderCulling = true` to `WindowSettings`. Expose `--no-render-culling` through the ordinary runtime parser and benchmark parser; the focused probe also accepts this flag. These are adapters to the same configuration value. No runtime toggle, scene serialization field, backend enum, or singleton is added.

Keep the existing public four-argument `Camera::render` declaration and its direct submit-all semantics. Add a private, shared `renderWithView` path and camera-view preparation, accessible to `SDLWindow` as a friend. The window prepares the view once; the public method prepares a one-call uncullled view and delegates. This avoids an incompatible public method signature or per-sprite global configuration lookup.

**Rationale**: `RuntimeOptions` already contains `WindowSettings`, which is passed through `GameManager` into `SDLWindow`. The default applies to normal registered-window-sprite rendering, while direct low-level camera calls remain compatible. Appending a defaulted field preserves existing aggregate initializations; consumers must rebuild against the changed C++ headers.

## 4. Treat renderer state conservatively

**Decision**: Prepare the view only after renderer/camera early exits. Once per enabled frame, verify that drawing uses the engine's full window output, identity renderer scale, and disabled logical presentation, with no custom viewport or render target. Also verify output dimensions agree with the dimensions used for camera mapping. On a query failure or nonstandard state, use submit-all for that frame. A clip rectangle alone can only shrink visible output and does not require tighter culling.

**Rationale**: The engine currently configures none of these custom transforms, but `Runtime::getRenderer()` exposes SDL to consumers. A cheap frame-level fallback preserves existing custom rendering without inventing support for all combinations. Disabling culling must skip these eligibility queries.

**Alternatives considered**: Implementing arbitrary SDL target, viewport, logical presentation, and scale mapping expands this feature substantially. Silently assuming default state could hide pixels for existing consumers.

## 5. Sequential filtering; no persistent derived state

**Decision**: Start with sequential filtering and drawing on the owning render thread. Use only an immutable stack view and per-sprite local geometry: O(N) time, O(1) extra retained storage, no culling allocations, no work when rendering is not invoked.

**Rationale**: Drawing is ordered and main-thread constrained. Parallel visibility needs an extra results buffer and synchronization before serial submission, and still visits every sprite. The initial arithmetic is small, especially for zero rotation. None of that overhead has a demonstrated benefit here. Existing physics oneTBB work is untouched.

**Alternatives considered**: oneTBB chunked classification plus an ordered serial pass is a feasible future alternative if a large rotated workload proves classification dominates complete rendering. Dirty bounds and unchanged-result reuse are also possible, but cross-system invalidation is an unmeasured cost. The benchmark matrix checks 1, 1,000, and 10,000 sprites, zero/mixed rotations, and one versus available physical-core worker counts; no parallel speedup is claimed.

## 6. Use a focused rendering probe, not a replacement benchmark framework

**Decision**: Add `benchmark/linear_render_probe.cpp` and a small `benchmark/capture_linear_render.py` driver. Use existing `BenchmarkRecorder` schema-v3 scope/raw/manifest output and `sweep_scaling.parse_benchmark_csv` validation. Leave the generic sweep's scenario list and numeric schema unchanged. Add namespaced manifest metadata for the culling configuration, renderer/output, trajectory, fixture parameters, and separate validation evidence.

**Rationale**: The current runner can render physics fixtures, but it has no controlled visibility distribution. Its stress fixture changes phase centers, which does not answer the user's continuous-motion question. A small collider-free rendering fixture isolates that question and exercises the real Runtime → SDLWindow → Camera path. Existing engine scenarios still provide complete-step and headless controls. Focused probes already exist under `benchmark/`.

**Fixture ownership**: A single feature-specific fixture under `tests/render_culling_fixture.h` supplies deterministic sprite layouts/continuous updates to both the integration test and probe. It owns only fixture objects and logical slot IDs. It is not a runtime subsystem. Texture generation/loading and setup are outside measured frames.

**Measurements**: Keep eight scopes and eleven counters unchanged. Obtain submission counts/order in a separate work-probe validation capture, never by a new counter/timer on every production draw. The probe uses scalar fixture updates and the existing engine scopes. Metadata links timing cases to validated fixture parameters. Add the focused probe and shared fixture contents to provenance (CMake source fingerprint inputs and recorded harness hashes), since they are outside the current runtime-source glob.

## 7. Establish a common baseline and correctness evidence

**Decision**: Capture pre-feature runtime `ad9b4f3` using the same rendering fixture and recorder with submit-all semantics, then feature disabled and feature enabled. Use a read-only baseline source snapshot or isolated worktree, with common harness/build integration applied identically. A small templated capability check in the probe can recognize the new settings field; the old runtime supports reference mode only. Record the common harness patch/hash and actual source hashes; never pretend an altered binary has the original embedded fingerprint.

Use deterministic software-render image comparisons and work-probe submission traces before timing. Add a test-only observation point immediately before presentation, after ordinary sprite/debug rendering, for image readback. It is compiled out unless work probes are enabled. Keep trace buffers/readback allocations outside timing captures. No persistent observer or callable hook exists in production.

**Rationale**: Candidate-disabled alone cannot reveal common-path overhead. Actual images and traces are needed to check geometry and order, not merely a duplicate bounds implementation. A double-precision transformed-corner oracle supplies an independent geometry check. Existing test setup provides dummy video/audio and software rendering (`tests/test_support.h`); there is currently no image test.

**Acceptance of performance evidence**: Use five independently started processes per case/mode/worker count, 120 warmup and 600 measured steps at 1/120 second initially, shuffled execution, fixed seed, recorded affinity and renderer. Extend sampling if the first results are too noisy to distinguish costs. Report per-run means, spread, p95/p99, and worst observed frames. Complete render includes preparation, filtering, SDL drawing/flush/present; complete_step stays separate. Validate matching workloads before comparing. Publish software-only limitations and do not infer hardware GPU throughput from CPU render timing.
