# Performance Evidence Summary (`020-sprite-tint-raycast`)

This compact report records the exploratory benchmark and microbenchmark measurements for feature `020-sprite-tint-raycast` (`T022`) under the repository benchmark artifact policy (`AGENTS.md`): 3 independent repetitions, 5 warmup iterations/frames, 30 measured iterations/frames, binaries kept under `build/`, and no retained per-frame traces or full-frame images.

## 1. Build Identities & Environment

- **OS / Driver**: Linux (`SDL_RENDER_DRIVER=software`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`)
- **Frozen Pre-Feature Baseline (`build/render-order-baseline/platformator_linear_render_probe`)**:
  - Configuration: `Release`, `sanitizer: none`, `work_probes: false`, `debug_tools: false`
  - `source_sha256`: `ae1f6003dc402c7243fb91d9cb91e161c86b9b376a73b3572fbe8881759c021e`
  - `fixture_sha256`: `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`
  - `harness_sha256`: `c1345c9ec9de4e08d1942b904cd293d1d930b73fa384bd49990001a048f1d62a`
- **Candidate Timing Build (Linear Render Pilot `bench_pilot_020`)**:
  - Configuration: `Release`, `sanitizer: none`, `work_probes: false`, `debug_tools: false`
  - `source_sha256`: `f7790e9bc812e46c0393efcf197c7affca0bb97d2504954ee9c89565ffd08aa5`
  - `fixture_sha256`: `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`
  - `harness_sha256`: `9ca485bd7d18341309d9a581354b8901d156392f0eb1050a9f5aef0ca2d0cedc`
- **Candidate Raycast & Sprite Timing/Validation Builds (`Grid + AABB` refactor, Sections 3–4)**:
  - Configuration: `Release`, `sanitizer: none`, `debug_tools: false`, `work_probes`: `false` (`build/render-order-timing`) and `true` (`build/render-order-validation`)
  - `source_sha256`: `24612ec650b8c0441a40b81ce8f9c007767fb348d818f96c78c28a3b028d7938`
  - `fixture_sha256`: `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`
  - `harness_sha256`: `9ca485bd7d18341309d9a581354b8901d156392f0eb1050a9f5aef0ca2d0cedc`

## 2. Linear Render Exploratory Pilot (`capture_linear_render.py`)

Command executed (output retained under `build/test/bench_pilot_020`, `1.1 MiB` total within the 50 MiB / 600 s pilot budget):

```sh
SDL_RENDER_DRIVER=software SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
python3 engine/benchmarks/capture_linear_render.py \
  --comparison render-order-culling \
  --focused-cases authored-static,sparse-camera-pan,dense-z-edit \
  --baseline-probe "$PWD/build/render-order-baseline/platformator_linear_render_probe" \
  --candidate-validation-probe "$PWD/build/render-order-validation/platformator_linear_render_probe" \
  --candidate-probe "$PWD/build/render-order-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/engine/benchmarks/assets" \
  --workers 1 --multiworker-count 4 --seed 0 \
  --output "$PWD/build/test/bench_pilot_020"
```

Parameters: 3 repetitions, 5 warmup frames, 30 measured frames, software backend `vsync=1`, `320x240` output.

| Case | Arm | Slice | Selection Median / p95 (ms) | Order Prep Mean (ms) | Render Mean (ms) | Paired Mean / Median / p95 (ms) | Step Mean (ms) | Per-run `render_ms` (ms) |
|---|---|---|---:|---:|---:|---:|---:|---|
| `authored-static` | `baseline-indexed` | `combined` | unavailable | unavailable | 16.6468 | 16.6644 / 16.6669 / 16.6751 | 0.0058 | `[16.6419, 16.6494, 16.6490]` |
| `authored-static` | `candidate-cached-linear` | `combined` | 0.0007 / 0.0011 | 0.0004 | 16.6268 | 16.6396 / 16.6669 / 16.6713 | 0.0040 | `[16.6273, 16.6212, 16.6319]` |
| `sparse-camera-pan` | `baseline-indexed` | `combined` | unavailable | unavailable | 16.6476 | 16.6642 / 16.6669 / 16.6685 | 0.0067 | `[16.6465, 16.6474, 16.6489]` |
| `sparse-camera-pan` | `candidate-cached-linear` | `combined` | 0.0497 / 0.0790 | 0.0014 | 16.6325 | 16.6412 / 16.6671 / 16.6698 | 0.0040 | `[16.6310, 16.6372, 16.6292]` |
| `dense-z-edit` | `candidate-rebuild-w1` | `combined` | 0.0006 / 0.0010 | 0.3326 | 16.6336 | 16.6456 / 16.6669 / 16.6720 | 0.0045 | `[16.6269, 16.6383, 16.6355]` |
| `dense-z-edit` | `candidate-forced-sort-w1` | `combined` | 0.0006 / 0.0011 | 0.6954 | 16.6291 | 16.6433 / 16.6668 / 16.6695 | 0.0052 | `[16.6301, 16.6280, 16.6293]` |
| `dense-z-edit` | `candidate-repair-w1` | `combined` | 0.0006 / 0.0012 | 0.0065 | 16.6265 | 16.6403 / 16.6668 / 16.6687 | 0.0049 | `[16.6295, 16.6161, 16.6339]` |
| `dense-z-edit` | `candidate-rebuild-w4` | `combined` | 0.0006 / 0.0012 | 0.3417 | 16.6308 | 16.6447 / 16.6667 / 16.6704 | 0.0050 | `[16.6321, 16.6299, 16.6304]` |

- All 30/30 frames passed oracle validation in `authored-static` and `sparse-camera-pan`, and 30/30 frames passed inter-arm parity + oracle checks in `dense-z-edit`.
- Default untinted sprites show no regression relative to `baseline-indexed` (Note: `baseline-indexed` vs `candidate-cached-linear` compares the frozen pre-feature binary with current `cached-linear`, and SDL software `vsync=1` paces `SDL_RenderPresent` to ~16.63–16.65 ms/frame).

## 3. Raycast `Grid + AABB` vs Contiguous Scan (`perf_020_timing` / `perf_020_validation`)

Measured on deterministic small (`8x8 = 64` colliders, spacing `256`, alternating 32x32 box & R=16 circle) and large sparse (`40x50 = 2,000` colliders, spacing `256`) scenes, 320-ray batch per iteration, 5 warmup + 30 measured batches across 3 independent processes (`Release`, `work_probes=false` for timings; `work_probes=true` for exact `raycastCandidateVisits`). `Runtime::raycast` traverses occupied `Grid` cells via 2D DDA and queries each cell's `AABB::forEachOverlappingAABB` (`SegmentedIntervalList` + X/Y projections) followed by ray-AABB slab pruning before narrow-phase intersection.

| Workload | Colliders | Rays/Batch | Method | Candidate Visits / Batch | Hits / Batch | Rep 1 Mean / Median (ms) | Rep 2 Mean / Median (ms) | Rep 3 Mean / Median (ms) | 3-Rep Mean of Means (ms) |
|---|---:|---:|---|---:|---:|---:|---:|---:|---:|
| `small_64_ray512` | 64 | 320 | `Grid + AABB` (`Runtime::raycast`) | 169 | 82 | 0.0705 / 0.0636 | 0.0795 / 0.0717 | 0.0817 / 0.0702 | 0.0772 |
| `small_64_ray512` | 64 | 320 | Contiguous scan (all colliders) | 20,480 | 82 | 0.0983 / 0.0841 | 0.0937 / 0.0884 | 0.0874 / 0.0849 | 0.0931 |
| `large_2000_ray1024` | 2,000 | 320 | `Grid + AABB` (`Runtime::raycast`) | 284 | 143 | 0.1466 / 0.1447 | 0.1698 / 0.1516 | 0.1803 / 0.1565 | 0.1656 |
| `large_2000_ray1024` | 2,000 | 320 | Contiguous scan (all colliders) | 640,000 | 143 | 2.4554 / 2.4049 | 2.5470 / 2.4598 | 2.6662 / 2.5272 | 2.5562 |
| `large_2000_ray12800` | 2,000 | 320 | `Grid + AABB` (`Runtime::raycast`) | 572 | 299 | 0.3959 / 0.3912 | 0.4454 / 0.4042 | 0.4389 / 0.3949 | 0.4267 |
| `large_2000_ray12800` | 2,000 | 320 | Contiguous scan (all colliders) | 640,000 | 299 | 2.5010 / 2.4736 | 2.7859 / 2.6960 | 2.7115 / 2.5064 | 2.6661 |

- Both `Grid + AABB` and contiguous scan returned identical hit counts on all workloads (`82`, `143`, `299`).
- On `large_2000_ray1024`, `Grid + AABB` reduced narrow-phase candidate checks from `640,000` to `284` (`~2,253x` reduction) and cut 320-ray batch wall time from `2.5562 ms` mean to `0.1656 ms` mean (`~15.4x` faster). On world-spanning rays (`large_2000_ray12800`), candidate checks dropped `~1,119x` (`640,000` to `572`) and batch time dropped from `2.6661 ms` to `0.4267 ms` (`~6.2x` faster).

## 4. Default White vs Modulated RGBA Sprite Draws (Direct Target, Un-vsynced)

`capture_linear_render.py` exercises only untinted default `(255, 255, 255, 255)` sprites under `vsync=1`. To isolate `submitSpriteRender`'s `(255,255,255,255)` zero-SDL-call fast path vs modulated non-default `(r,g,b,a)` draws (which query, set, and restore SDL texture color & alpha modulation per draw) without `SDL_RenderPresent` 60 Hz pacing, `perf_020_timing` rendered `64` and `2,000` `1x1` visible sprites (shared `assets/wall.png` texture) via `Camera::render` onto a `320x240 RGBA8888` offscreen target texture (`SDL_SetRenderVSync(renderer, 0)`), 5 warmup + 30 measured frames, 3 repetitions:

| Workload | Sprites | Color Mode | Rep 1 Mean / Median (ms) | Rep 2 Mean / Median (ms) | Rep 3 Mean / Median (ms) | 3-Rep Mean of Means (ms) |
|---|---:|---|---:|---:|---:|---:|
| `visible_64_default` | 64 | Default white `(255,255,255,255)` | 0.0075 / 0.0075 | 0.0084 / 0.0085 | 0.0091 / 0.0084 | 0.0083 |
| `visible_64_modulated` | 64 | Varied non-default `(r,g,b,a)` | 0.0189 / 0.0187 | 0.0187 / 0.0187 | 0.0183 / 0.0184 | 0.0186 |
| `visible_2000_default` | 2,000 | Default white `(255,255,255,255)` | 0.2019 / 0.2019 | 0.2099 / 0.2034 | 0.2037 / 0.1972 | 0.2052 |
| `visible_2000_modulated` | 2,000 | Varied non-default `(r,g,b,a)` | 0.6866 / 0.6442 | 0.6425 / 0.6399 | 0.6297 / 0.6312 | 0.6529 |

## 5. Limitations

1. **Software / Dummy Driver Scope**: All timings were measured with `SDL_RENDER_DRIVER=software` and `SDL_VIDEODRIVER=dummy`. They capture CPU-side dispatch and SDL software rasterization only, not hardware GPU driver/present performance.
2. **Exploratory Sample Size**: 3 short repetitions (`30` measured frames/batches each) screen order-of-magnitude behavior and are not a full multi-profile acceptance campaign.
3. **Hidden Window Full-Frame Metric Omitted**: In `perf_020_probe`, `Runtime::simulateAndRenderFrame` with `windowSettings.hidden = true` and dummy video driver took `~0.0068 ms` regardless of sprite count (64 vs 2,000), indicating hidden dummy-window frame skip; therefore, un-vsynced `Camera::render` onto an explicit `320x240 RGBA8888` render target texture was used for Section 4 above.
4. **`capture_linear_render.py` Untinted Baseline**: `capture_linear_render.py` (`linear_render_probe`) does not toggle per-sprite RGBA tinting, so `baseline-indexed` vs `candidate-cached-linear` compares untinted renders across revisions (`~16.65 ms` vs `~16.63 ms` under software `vsync=1`).
