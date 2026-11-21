# Platformator Thesis Benchmark Results (2026-11-21, Post-Fix Refresh)

## Hardware & Build Provenance

- **Host CPU**: AMD Ryzen 5 7535HS with Radeon Graphics (6 physical cores / 12 threads, pinned via `taskset -c 0,2,4,6,8,10` to 6 dedicated physical cores)
- **OS / Kernel**: Manjaro Linux (`6.12.112-1-MANJARO`, x86_64)
- **Compiler & Flags**: GNU GCC `16.2.1`, Release (`-O3 -DNDEBUG`, IPO/LTO enabled, sanitizers/debug-tools/work-probes disabled)
- **Dependencies**: SDL `3.4.18`, SDL_image `3.4.8`, SDL_mixer `3.2.4`, Eigen `5.0.1`, nlohmann/json `3.12.0`, oneTBB `2022.3.0`
- **Methodology**: Schema v3 (`complete_step` + all subscopes), headless, fixed `dt = 1/120 s`, `120` warmup steps, `600` measured steps, `5` independent process repetitions per data point, `seed = 0`. Separate `--validate-invariants` verification passed on all scenarios before timing sweeps. Plots use `--merge-deferred-grid` to combine in-step `broad_phase` + end-of-step `deferred_grid_flush` into `Broad Phase (grid)` and subtract it from `Cleanup (excl. grid)` so nested grid updates are not double-counted. Includes post-audit fixes: lazy `activeCellBounds`, `MAX_RECYCLED_GRID_CELLS = 4096`, cached cell-pointer lookup on migration, `GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD = 512`, and deduplicated root body movement syncs.

## Summary of Physics Scaling Sweeps (`w=1` vs `w=6`)

| # | Scenario | Sweep Range | `w=1` Max Mean / p95 (ms) | `w=6` Max Mean / p95 (ms) | Speedup (`w6`) | Empirical Slope $\alpha$ (`w1` / `w6`) | Dominant Bottleneck / Behavior |
|---|---|---|---:|---:|---:|---:|---|
| 1 | `broad_phase` | `N = 50..4000` | `2.604 / 4.117` | `0.594 / 0.879` | **4.38×** | `1.41 / 0.94` | `Broad Phase (grid)` (`0.476 ms` @ w6); textbook `O(N)` with 6 workers |
| 2 | `broad_phase_stress` | `N = 25..400` | `0.731 / 0.875` | `0.799 / 1.256` | `0.91×` | `1.81 / 1.89` | `Broad Phase (grid)` (`0.716 ms` @ w1); worst-case single-cell teleport `O(N^2)` |
| 3 | `narrow_phase` | `N = 10..200` (`4N` colliders) | `22.244 / 27.130` | `7.860 / 10.399` | **2.83×** | `2.44 / 2.58` | `narrow_phase` (`5.680 ms` @ w6) + `Broad Phase (grid)` (`2.118 ms`); `O(N^2)` pairs + cache pressure; stays under 120 Hz mean at `w=6` |
| 4 | `rigid_body_container` | `N = 50..2000` | `2.009 / 3.186` | `1.190 / 1.787` | **1.69×** | `1.36 / 1.11` | `resolve_collisions` (`0.684 ms` @ w6) + `Broad Phase (grid)` (`0.226 ms`); ordered-batch parallel solver |
| 5 | `add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.817 / 1.004` | `0.706 / 0.893` | **1.16×** | `0.92 / 0.93` | `step_hook` (`0.434 ms`) + `Broad Phase (grid)` (`0.148 ms` @ w6) + `retirement` (`0.065 ms`); linear `O(m)` |
| 6 | `moving_add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.922 / 1.290` | `0.747 / 0.959` | **1.23×** | `0.47 / 0.51`* | Affine `c_0 + c_1 * m`; converges directly to static churn as `m -> 1000` |
| 7 | `ideal_world` (distributed chambers) | `N = 200..20000` dynamic (`+40..4000` walls) | `46.307 / 77.403` | `26.859 / 41.422` | **1.72×** (up to **3.09×** @ 5k) | `1.38 / 1.39` | Balanced `resolve_collisions` + `Broad Phase (grid)` + `narrow_phase`; mean **~10k dynamic (12k total) @ 120 Hz** (`8.42 ms`, p95 `14.39 ms`), **~15k dynamic (18k total) @ 60 Hz** (`14.80 ms`, p95 `23.37 ms`); strict p95 budget supports ~7–8k @ 120 Hz and ~11–12k @ 60 Hz |

*\*Note on `moving_add_and_remove`: The 1,000 continuously moving steady bodies contribute an `O(1)` baseline (~0.14 ms @ w6, ~0.21 ms @ w1) that flattens the log-log slope at small `m`; incremental cost per churned body remains linear `O(m)`.*

## Render Order & Culling Pilot (`cached-linear`)

Compact pilot (`3` repetitions, `5` warmup, `30` measured frames, SDL `software` renderer, `320x240`, 100% oracle & 4-arm pixel parity verified):

| Case | Arm / Slice | Visible Selection Median (ms) | Draw-Order Prepare Mean (ms) | Notes |
|---|---|---:|---:|---|
| `authored-static` (64 visible) | `cached-linear` | `0.0006` | `0.0004` | Near-zero steady-state overhead |
| `sparse-camera-pan` (10,000 sparse) | `cached-linear` | `0.0530` | `0.0018` | ~53 µs culling across 10k sprites |
| `dense-z-edit` (10,000 mixed-Z) | `forced-sort-w1` (edit) | `0.0006` | `0.7061` | Full sort every frame |
| `dense-z-edit` (10,000 mixed-Z) | `rebuild-w1` (clean / edit) | `0.0006` | `0.0004 / 0.7183` | Skips clean frames, rebuilds on edit |
| `dense-z-edit` (10,000 mixed-Z) | `repair-w1` (clean / edit) | `0.0006` | `0.0005 / 0.0129` | **~56× faster** than full rebuild on edit frames |

## Generated 300-DPI Plots

- **English**: `plots/en/01_broad_phase.png` .. `plots/en/07_ideal_world.png`, `plots/en/08_render_culling.png`
- **Serbian**: `plots/sr/01_broad_phase.png` .. `plots/sr/07_ideal_world.png`, `plots/sr/08_render_culling.png`
