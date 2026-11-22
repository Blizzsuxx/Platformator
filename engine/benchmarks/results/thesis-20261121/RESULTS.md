# Platformator Thesis Benchmark Results (2026-11-22, Final Pre-Defense Refresh)

## Hardware & Build Provenance

- **Host CPU**: AMD Ryzen 5 7535HS with Radeon Graphics (6 physical cores / 12 threads, pinned via `taskset -c 0,2,4,6,8,10` to 6 dedicated physical cores)
- **OS / Kernel**: Manjaro Linux (`6.12.112-1-MANJARO`, x86_64)
- **Compiler & Flags**: GNU GCC `16.2.1`, Release (`-O3 -DNDEBUG`, IPO/LTO enabled, sanitizers/debug-tools/work-probes disabled)
- **Dependencies**: SDL `3.4.18`, SDL_image `3.4.8`, SDL_mixer `3.2.4`, Eigen `5.0.1`, nlohmann/json `3.12.0`, oneTBB `2022.3.0`
- **Methodology**: Schema v3 (`complete_step` + all subscopes), headless, fixed `dt = 1/120 s`, `120` warmup steps, `600` measured steps, `5` independent process repetitions per data point, `seed = 0`. Separate `--validate-invariants` verification passed on all scenarios before timing sweeps. Plots use `--merge-deferred-grid` to combine in-step `broad_phase` + end-of-step `deferred_grid_flush` into `Broad Phase (grid)` and subtract it from `Cleanup (excl. grid)` so nested grid updates are not double-counted. Includes all optimizations: lazy `activeCellBounds`, `MAX_RECYCLED_GRID_CELLS = 4096`, cached cell-pointer lookup on migration, `GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD = 512`, deduplicated root body movement syncs, flat open-addressing hash sets (`FlatHashSet` / `FlatPointerSet`), pointer-stable pool-backed `FlatCellMap` & `FlatCandidatePairPool`, parallel order-preserving narrow-phase pair queueing, deterministic `applyMovement`, and sparse per-phase script update lists.

## Summary of Physics Scaling Sweeps (`w=1` vs `w=6`)

| # | Scenario | Sweep Range | `w=1` Max Mean / p95 (ms) | `w=6` Max Mean / p95 (ms) | Speedup (`w6`) | Empirical Slope $\alpha$ (`w1` / `w6`) | Dominant Bottleneck / Behavior |
|---|---|---|---:|---:|---:|---:|---|
| 1 | `broad_phase` | `N = 50..4000` | `3.590 / 5.039` | `0.857 / 1.375` | **4.19×** | `1.50 / 1.00` | `Broad Phase (grid)` (`0.708 ms` @ w6); exact textbook `O(N)` (`α = 1.00`) with 6 workers |
| 2 | `broad_phase_stress` | `N = 25..400` | `0.636 / 0.831` | `0.617 / 0.758` | `1.03×` | `1.86 / 1.81` | `Broad Phase (grid)` (`0.599 ms` @ w6); worst-case single-cell teleport `O(N^2)` |
| 3 | `narrow_phase` | `N = 10..200` (`4N` colliders) | `22.177 / 27.257` | `6.790 / 9.276` | **3.27×** | `2.39 / 2.37` | `narrow_phase` (`5.615 ms` @ w6) + `Broad Phase (grid)` (`1.112 ms`); `O(N^2)` pairs; well under 120 Hz mean at `w=6` |
| 4 | `rigid_body_container` | `N = 50..2000` | `2.266 / 3.747` | `1.199 / 1.829` | **1.89×** | `1.38 / 1.14` | `resolve_collisions` (`0.708 ms` @ w6) + `Broad Phase (grid)` (`0.211 ms`); ordered-batch parallel solver |
| 5 | `add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.966 / 1.375` | `0.755 / 1.037` | **1.28×** | `0.95 / 0.92` | `step_hook` (`0.456 ms`) + `Broad Phase (grid)` (`0.164 ms` @ w6) + `retirement` (`0.067 ms`); linear `O(m)` |
| 6 | `moving_add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.933 / 1.274` | `0.712 / 0.961` | **1.31×** | `0.46 / 0.51`* | Affine `c_0 + c_1 * m`; converges directly to static churn as `m -> 1000` |
| 7 | `ideal_world` (distributed chambers) | `N = 200..20000` dynamic (`+40..4000` walls) | `49.170 / 77.496` | `25.434 / 39.020` | **1.93×** (up to **2.69×** @ 5k) | `1.41 / 1.39` | Balanced `resolve_collisions` + `Broad Phase (grid)` + `narrow_phase`; mean **~10k dynamic (12k total) @ 120 Hz** (`8.55 ms`, p95 `14.32 ms`), **~15k dynamic (18k total) @ 60 Hz** (`16.04 ms`, p95 `24.61 ms`); strict p95 budget supports ~7–8k @ 120 Hz and ~11–12k @ 60 Hz |

*\*Note on `moving_add_and_remove`: The 1,000 continuously moving steady bodies contribute an `O(1)` baseline (~0.15 ms @ w6, ~0.21 ms @ w1) that flattens the log-log slope at small `m`; incremental cost per churned body remains linear `O(m)`.*

## Render Order & Culling Pilot (`cached-linear`)

Compact pilot (`3` repetitions, `5` warmup, `30` measured frames, SDL `software` renderer, `320x240`, 100% oracle & 4-arm pixel parity verified):

| Case | Arm / Slice | Visible Selection Median (ms) | Draw-Order Prepare Mean (ms) | Notes |
|---|---|---:|---:|---|
| `authored-static` (64 visible) | `cached-linear` | `0.0006` | `0.0004` | Near-zero steady-state overhead |
| `sparse-camera-pan` (10,000 sparse) | `cached-linear` | `0.0583` | `0.0020` | ~58 µs culling across 10k sprites |
| `dense-z-edit` (10,000 mixed-Z) | `forced-sort-w1` (edit) | `0.0006` | `0.7171` | Full sort every frame |
| `dense-z-edit` (10,000 mixed-Z) | `rebuild-w1` (clean / edit) | `0.0006` | `0.0004 / 0.7033` | Skips clean frames, rebuilds on edit |
| `dense-z-edit` (10,000 mixed-Z) | `repair-w1` (clean / edit) | `0.0006` | `0.0004 / 0.0138` | **~51× faster** than full rebuild on edit frames |

## Generated 300-DPI Plots

- **English**: `plots/en/01_broad_phase.png` .. `plots/en/07_ideal_world.png`, `plots/en/08_render_culling.png`
- **Serbian**: `plots/sr/01_broad_phase.png` .. `plots/sr/07_ideal_world.png`, `plots/sr/08_render_culling.png`
