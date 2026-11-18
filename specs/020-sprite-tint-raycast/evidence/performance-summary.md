# Performance Evidence Summary (`020-sprite-tint-raycast`)

This compact report records the scoped exploratory benchmarks and microbenchmarks for feature `020-sprite-tint-raycast` (`T022`, `T034`) under the repository benchmark artifact policy (`AGENTS.md`): 3 independent repetitions, 5 warmup iterations/frames/steps, 30 measured iterations/frames/steps, build artifacts kept under `build/`, and compact raw timing sample files retained alongside this report without retaining per-frame sprite identity lists, full-frame images, or binaries.

## 1. Retained Artifacts & Build Identities

- **Retained Raw Timing Samples & Metadata**:
  - [linear-render-pilot-samples.json](linear-render-pilot-samples.json) (`~309 KiB`): build identities, commands, parity results, case aggregates, and raw per-frame timing samples from `capture_linear_render.py`.
  - [raycast-tint-raw-samples.csv](raycast-tint-raw-samples.csv) (`~58 KiB`, 900 measured rows): 3-repetition x 30-iteration raw timings for `Grid + AABB` vs contiguous scan raycasting and un-vsynced default vs modulated `Sprite` draws.
  - [complete-step-sync-samples.csv](complete-step-sync-samples.csv) (`~18 KiB`, 360 measured step rows): 3-repetition x 30-step complete-step timings for `500` moving colliders with grid-edge churn comparing `no_override_baseline` vs `afterPhysicsSync_320_rays` across `1` and `4` workers.
- **OS / Driver**: Linux (`SDL_RENDER_DRIVER=software`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`)
- **Frozen Pre-Feature Baseline (`build/render-order-baseline/platformator_linear_render_probe`)**:
  - Configuration: `Release`, `sanitizer: none`, `work_probes: false`, `debug_tools: false`
  - `source_sha256`: `ae1f6003dc402c7243fb91d9cb91e161c86b9b376a73b3572fbe8881759c021e`
  - `fixture_sha256`: `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`
  - `harness_sha256`: `c1345c9ec9de4e08d1942b904cd293d1d930b73fa384bd49990001a048f1d62a`
- **Final Candidate Timing & Validation Builds (`build/render-order-timing` & `build/render-order-validation`)**:
  - Configuration: `Release`, `sanitizer: none`, `debug_tools: false`, `work_probes`: `false` (`build/render-order-timing`) and `true` (`build/render-order-validation`)
  - `source_sha256`: `2d2d231fc8e1809ffc5439d59b7f2cf619850ef18f70f0a6aa873c866e195fdc`
  - `fixture_sha256`: `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`
  - `harness_sha256`: `9ca485bd7d18341309d9a581354b8901d156392f0eb1050a9f5aef0ca2d0cedc`
- **Historical Earlier Pilot Runs (Superseded / No Retained Final-Tree Raw Data)**:
  - Prior pre-convergence linear-render pilot (`build/test/bench_pilot_020`, `source_sha256: f7790e9bc812e46c0393efcf197c7affca0bb97d2504954ee9c89565ffd08aa5`) and hidden-window full-frame dummy-driver observation (`~0.0068 ms` regardless of 64 vs 2,000 sprites) are historical pre-convergence observations superseded by the post-convergence measurements below.

## 2. Linear Render Exploratory Pilot (`capture_linear_render.py`)

Command executed (pilot directory `build/test/bench_pilot_020_final`, `1.2 MiB` total within the 50 MiB / 600 s pilot budget; compact samples retained in [linear-render-pilot-samples.json](linear-render-pilot-samples.json)):

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
  --output "$PWD/build/test/bench_pilot_020_final"
```

Parameters: 3 repetitions, 5 warmup frames, 30 measured frames, software backend `vsync=1`, `320x240` output.

| Case | Arm | Slice | Selection Median / p95 (ms) | Order Prep Mean (ms) | Render Mean (ms) | Paired Mean / Median / p95 (ms) | Step Mean (ms) | Per-run `render_ms` Mean (ms) |
|---|---|---|---:|---:|---:|---:|---:|---|
| `authored-static` | `baseline-indexed` | `combined` | unavailable | unavailable | 16.6437 | 16.6632 / 16.6667 / 16.7105 | 0.0067 | `[16.6399, 16.6452, 16.6459]` |
| `authored-static` | `candidate-cached-linear` | `combined` | 0.0007 / 0.0011 | 0.0004 | 16.6201 | 16.6378 / 16.6668 / 16.7907 | 0.0059 | `[16.6175, 16.6303, 16.6125]` |
| `sparse-camera-pan` | `baseline-indexed` | `combined` | unavailable | unavailable | 16.6412 | 16.6639 / 16.6667 / 16.6786 | 0.0089 | `[16.6379, 16.6409, 16.6446]` |
| `sparse-camera-pan` | `candidate-cached-linear` | `combined` | 0.0704 / 0.1127 | 0.0026 | 16.6204 | 16.6400 / 16.6667 / 16.6701 | 0.0087 | `[16.6189, 16.6178, 16.6245]` |
| `dense-z-edit` | `candidate-rebuild-w1` | `combined` | 0.0007 / 0.0012 | 0.3697 | 16.6016 | 16.6234 / 16.6667 / 16.9114 | 0.0076 | `[16.6190, 16.5618, 16.6242]` |
| `dense-z-edit` | `candidate-forced-sort-w1` | `combined` | 0.0008 / 0.0013 | 0.6958 | 16.6098 | 16.6284 / 16.6666 / 16.7119 | 0.0064 | `[16.6210, 16.6274, 16.5810]` |
| `dense-z-edit` | `candidate-repair-w1` | `combined` | 0.0008 / 0.0010 | 0.0082 | 16.6099 | 16.6284 / 16.6668 / 16.9220 | 0.0068 | `[16.6240, 16.6262, 16.5794]` |
| `dense-z-edit` | `candidate-rebuild-w4` | `combined` | 0.0007 / 0.0012 | 0.3668 | 16.6092 | 16.6277 / 16.6666 / 17.4100 | 0.0070 | `[16.6168, 16.6220, 16.5888]` |

- All 30/30 frames passed oracle validation in `authored-static` and `sparse-camera-pan`, and 30/30 frames passed inter-arm parity + oracle checks in `dense-z-edit` (workers: 1 for single-worker arms and 4 for `candidate-rebuild-w4`).

## 3. Raycast `Grid + AABB` vs Contiguous Scan (`raycast-tint-raw-samples.csv`)

Measured via temporary Release probe built against `build/render-order-timing/libplatformator.a` (`work_probes=false`, `PERF_COUNT_ALLOCS` disabled) and `build/render-order-validation/libplatformator.a` (`work_probes=true`, `PERF_COUNT_ALLOCS=1`) using `--mode raycast-tint --rep {1,2,3}` (3 independent processes, 5 warmup + 30 measured 320-ray batches per workload; scenes: `8x8 = 64` and `40x50 = 2,000` alternating `32x32` boxes and `R=16` circles at spacing `256`).

| Workload | Colliders | Method | Candidates / Batch | Allocs / 30 Batches | Hits / Batch | Rep 1 Mean / Med (ms) | Rep 2 Mean / Med (ms) | Rep 3 Mean / Med (ms) | Overall Mean / Med (ms) |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| `small_64_ray512` | 64 | `Grid + AABB` (`Runtime::raycast`) | 169 | 0 | 82 | 0.1178 / 0.1150 | 0.1152 / 0.1160 | 0.1080 / 0.1208 | 0.1137 / 0.1153 |
| `small_64_ray512` | 64 | Contiguous scan (all colliders) | 20,480 | 0 | 82 | 0.1487 / 0.1541 | 0.1476 / 0.1419 | 0.1200 / 0.1141 | 0.1388 / 0.1369 |
| `large_2000_ray1024` | 2,000 | `Grid + AABB` (`Runtime::raycast`) | 284 | 0 | 143 | 0.2359 / 0.2196 | 0.2674 / 0.2378 | 0.2235 / 0.1852 | 0.2423 / 0.2299 |
| `large_2000_ray1024` | 2,000 | Contiguous scan (all colliders) | 640,000 | 0 | 143 | 3.7340 / 3.6196 | 3.1990 / 3.1876 | 3.2624 / 3.1613 | 3.3984 / 3.3332 |
| `large_2000_ray12800` | 2,000 | `Grid + AABB` (`Runtime::raycast`) | 572 | 0 | 299 | 0.9416 / 0.9255 | 0.6038 / 0.5891 | 0.6937 / 0.6862 | 0.7464 / 0.6924 |
| `large_2000_ray12800` | 2,000 | Contiguous scan (all colliders) | 640,000 | 0 | 299 | 3.6806 / 3.6073 | 3.4619 / 3.3774 | 3.5315 / 3.5061 | 3.5580 / 3.5026 |

- `Grid + AABB` (`Runtime::raycast`) recorded `0` heap allocations on every batch, matched exact contiguous-scan hit counts (`82`, `143`, `299`), reduced candidate checks `~2,253x` (`640,000` -> `284`) on `large_2000_ray1024` (`~14.0x` lower mean latency), and reduced candidate checks `~1,119x` (`640,000` -> `572`) on world-spanning `large_2000_ray12800` (`~4.8x` lower mean latency).

## 4. Direct Target Sprite Draws: Default White vs Modulated RGBA (`raycast-tint-raw-samples.csv`)

Measured un-vsynced (`SDL_SetRenderVSync(renderer, 0)`) via `Camera::render` onto a `320x240 RGBA8888` offscreen target texture (`assets/wall.png` shared across `64` and `2,000` `1x1` sprites; 5 warmup + 30 measured frames, 3 independent processes):

| Workload | Sprites | Color Mode | Rep 1 Mean / Med (ms) | Rep 2 Mean / Med (ms) | Rep 3 Mean / Med (ms) | Overall Mean / Med (ms) |
|---|---:|---|---:|---:|---:|---:|
| `visible_64_default` | 64 | Default white `(255,255,255,255)` (fast path) | 0.0075 / 0.0066 | 0.0058 / 0.0052 | 0.0072 / 0.0064 | 0.0068 / 0.0061 |
| `visible_64_modulated` | 64 | Varied non-default `(r,g,b,a)` (save+restore) | 0.0223 / 0.0213 | 0.0220 / 0.0219 | 0.0262 / 0.0261 | 0.0235 / 0.0221 |
| `visible_2000_default` | 2,000 | Default white `(255,255,255,255)` (fast path) | 0.3064 / 0.2968 | 0.3036 / 0.2987 | 0.3022 / 0.2768 | 0.3041 / 0.2955 |
| `visible_2000_modulated` | 2,000 | Varied non-default `(r,g,b,a)` (save+restore) | 0.9032 / 0.8970 | 0.8666 / 0.8863 | 0.8803 / 0.8870 | 0.8834 / 0.8891 |

## 5. Complete-Step Synchronized Raycast & `Behavior::afterPhysicsSync` Comparison (`complete-step-sync-samples.csv`)

Focused, budgeted complete-step benchmark using `runtime.simulateFrame(1.0 / 60.0)` with `tbb::global_control` worker caps (`1` and `4` workers), 5 warmup steps + 30 measured steps across 3 independent processes (`--mode complete-step --rep {1,2,3}`):
- **Scene & churn workload**: `500` dynamic moving colliders (`10x50` alternating `32x32` `BoxCollider` and `R=16` `CircleCollider`), each anchored directly on `200`-unit `GridCell` boundaries `(200*(c+1), 200*(r+1))`. Every `fixedUpdate`, all `500` `GridChurnMoverBehavior` components flip position `(-24, -24) <-> (+24, +24)`, forcing `500` cross-cell `Grid` removals/insertions, transform syncs, published geometry updates, and end-of-step `commitMutations()` on every single step.
- **Variants**:
  - `no_override_baseline`: All 500 `GridChurnMoverBehavior` components leave `Behavior::afterPhysicsSync` at its default no-op implementation.
  - `afterPhysicsSync_320_rays`: Same 500 moving colliders + 1 observer `Behavior` whose `afterPhysicsSync(double)` casts `320` synchronized `Runtime::raycast` queries (`maxDistance = 2048.0f`) spanning the churning grid rows and asserts `cachedWorldPosition == getPosition()` on every hit (`mismatches = 0`).

| Variant | Workers | Sync Candidates / Step | Hits / Step | Ray Heap Allocs | Parity Mismatches | Rep 1 Mean / Med (ms) | Rep 2 Mean / Med (ms) | Rep 3 Mean / Med (ms) | Overall Mean / Med / p95 (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `no_override_baseline` | 1 | — | — | — | — | 0.5151 / 0.4954 | 0.6926 / 0.6516 | 0.6374 / 0.5965 | 0.6150 / 0.5737 / 0.8604 |
| `afterPhysicsSync_320_rays` | 1 | 280 | 232 | 0 | 0 | 0.8224 / 0.8119 | 0.8618 / 0.7920 | 0.7841 / 0.7623 | 0.8228 / 0.7806 / 1.0952 |
| `no_override_baseline` | 4 | — | — | — | — | 0.3525 / 0.3449 | 0.2959 / 0.2942 | 0.2902 / 0.2842 | 0.3129 / 0.3061 / 0.3831 |
| `afterPhysicsSync_320_rays` | 4 | 280 | 232 | 0 | 0 | 0.5144 / 0.4928 | 0.5856 / 0.5794 | 0.5008 / 0.4710 | 0.5336 / 0.5218 / 0.6871 |

- **Key observations**:
  - Scaling `no_override_baseline` under heavy 500-collider grid-edge churn from `1` worker to `4` workers reduced complete-step mean time from `0.6150 ms` to `0.3129 ms` (`~1.97x` parallel speedup).
  - Running `320` synchronized `raycast` queries inside `Behavior::afterPhysicsSync` added `~0.2078 ms` at 1 worker and `~0.2207 ms` at 4 workers (serial observer loop), keeping total step time (physics churn + sync + `commitMutations()` + 320 raycasts) at `0.5336 ms` mean (`0.6871 ms` p95) with `280` candidate visits/step, `232` hits/step, `0` raycast heap allocations, and `0` physics-transform sync mismatches.

## 6. Limitations

1. **Software / Dummy Driver Scope**: All timings ran under `SDL_RENDER_DRIVER=software`, `SDL_VIDEODRIVER=dummy`, and `SDL_AUDIODRIVER=dummy`. They capture CPU-side engine/Physics/SDL software execution rather than hardware GPU present timings.
2. **Exploratory Pilot Budget**: Per `AGENTS.md`, measurements use 3 independent repetitions (`5` warmup + `30` measured samples each) rather than an exhaustive full-campaign sweep.
3. **Serial Observer Ray Loop in Complete-Step Probe**: Inside `afterPhysicsSync_320_rays`, the 320 `Runtime::raycast` calls executed sequentially inside the observer's `afterPhysicsSync` callback while internal `Phase::Physics` broad-phase/sync work scaled across `1` vs `4` oneTBB workers; caller-side `tbb::parallel_for` ray batching was not exercised in that complete-step probe.
4. **Software VSync Pacing in `capture_linear_render.py`**: `capture_linear_render.py` exercises untinted `(255, 255, 255, 255)` sprites under software `vsync=1` (~16.6 ms present cadence); un-vsynced per-draw modulation cost is isolated in Section 4 (`visible_*` direct-target draws).
