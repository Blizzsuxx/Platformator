# Platformator Thesis Benchmark Results (2026-11-21)

## Hardware & Build Provenance

- **Host CPU**: AMD Ryzen 5 7535HS with Radeon Graphics (6 physical cores / 12 threads, pinned via `taskset -c 0,2,4,6,8,10` to 6 dedicated physical cores)
- **OS / Kernel**: Manjaro Linux (`6.12.112-1-MANJARO`, x86_64)
- **Compiler & Flags**: GNU GCC `16.2.1`, Release (`-O3 -DNDEBUG`, IPO/LTO enabled, sanitizers/debug-tools/work-probes disabled)
- **Dependencies**: SDL `3.4.18`, SDL_image `3.4.8`, SDL_mixer `3.2.4`, Eigen `5.0.1`, nlohmann/json `3.12.0`, oneTBB `2022.3.0`
- **Methodology**: Schema v3 (`complete_step` + all 11 timing scopes including `step_hook`, `retirement`, `deferred_grid_flush`), headless, fixed `dt = 1/120 s`, `120` warmup steps, `600` measured steps, `5` independent process repetitions per data point, `seed = 0`. Separate `--validate-invariants` verification passed on all 8 physics scenarios before timing sweeps.

## Summary of Physics Scaling Sweeps (`w=1` vs `w=6`)

| # | Scenario | Sweep Range | `w=1` Max Mean / p95 (ms) | `w=6` Max Mean / p95 (ms) | Speedup (`w6`) | Empirical Slope $\alpha$ (`w1` / `w6`) | Dominant Bottleneck / Behavior |
|---|---|---|---:|---:|---:|---:|---|
| 1 | `broad_phase` | `N = 50..4000` | `2.243 / 3.745` | `0.722 / 1.293` | **3.11×** | `1.27 / 0.98` | `deferred_grid_flush` (`0.607 ms` @ w6); textbook `O(N)` with 6 workers |
| 2 | `broad_phase_stress` | `N = 25..400` | `0.758 / 1.028` | `0.751 / 1.201` | `1.01×` | `1.81 / 1.80` | `broad_phase` (`0.732 ms`); worst-case single-cell teleport `O(N^2)`, no cross-cell parallelism |
| 3 | `narrow_phase` | `N = 10..200` (`4N` colliders) | `21.991 / 27.373` | `7.244 / 10.158` | **3.04×** | `2.42 / 2.49` | `narrow_phase` (`5.195 ms` @ w6); `O(N^2)` pairs + cache pressure; stays under 120 Hz budget at `w=6` |
| 4 | `rigid_body_container` | `N = 50..2000` | `2.069 / 3.170` | `1.199 / 1.784` | **1.73×** | `1.34 / 1.11` | `resolve_collisions` (`0.672 ms` @ w6) + `deferred_grid_flush` (`0.233 ms`); ordered-batch parallel solver |
| 5 | `add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.865 / 1.200` | `0.691 / 0.943` | **1.25×** | `0.91 / 0.92` | `step_hook` (`0.418 ms`) + `deferred_grid_flush` (`0.152 ms` @ w6) + `retirement` (`0.062 ms`); linear `O(m)` |
| 6 | `moving_add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.892 / 1.243` | `0.702 / 0.973` | **1.27×** | `0.47 / 0.53`* | Affine `c_0 + c_1 * m`; converges directly to static churn as `m -> 1000` |
| 7 | `persistent_contacts` (`2` scripted) | `N = 50..5000` pairs | `0.223 / 0.323` | `0.233 / 0.339` | `0.96×` | `1.08 / 1.08` | Linear `O(N)` event/contact bookkeeping (`0.045 µs` per active pair) |
| 8 | `ideal_world` (distributed chambers) | `N = 200..20000` dynamic (`+40..4000` walls) | `42.873 / 62.617` | `24.691 / 35.547` | **1.74×** (up to **2.63×** @ 5k) | `1.32 / 1.39` | Balanced `resolve_collisions` + `deferred_grid_flush` + `narrow_phase`; **~10k dynamic (12k total) @ 120 Hz**, **~15k dynamic (18k total) @ 60 Hz** (`w=6`) |

*\*Note on `moving_add_and_remove`: The 1,000 continuously moving steady bodies contribute an `O(1)` baseline (~0.15 ms @ w6, ~0.21 ms @ w1) that flattens the log-log slope at small `m`; incremental cost per churned body remains linear `O(m)`.*

## Render Order & Culling Pilot (`cached-linear`)

Compact pilot (`3` repetitions, `5` warmup, `30` measured frames, SDL `software` renderer, `320x240`, 100% oracle & 4-arm pixel parity verified):

| Case | Arm / Slice | Visible Selection Median (ms) | Draw-Order Prepare Mean (ms) | Notes |
|---|---|---:|---:|---|
| `authored-static` (64 visible) | `cached-linear` | `0.0004` | `0.0002` | Near-zero steady-state overhead |
| `sparse-camera-pan` (10,000 sparse) | `cached-linear` | `0.0446` | `0.0014` | Sub-50 µs culling across 10k sprites |
| `dense-z-edit` (10,000 mixed-Z) | `forced-sort-w1` (edit) | `0.0005` | `0.6862` | Full sort every frame |
| `dense-z-edit` (10,000 mixed-Z) | `rebuild-w1` (clean / edit) | `0.0002` | `0.0002 / 0.6599` | Skips clean frames, rebuilds on edit |
| `dense-z-edit` (10,000 mixed-Z) | `repair-w1` (clean / edit) | `0.0004` | `0.0003 / 0.0098` | **~67× faster** than full rebuild on edit frames |

## Generated 300-DPI Plots

- **English**: `plots/en/01_broad_phase.png` .. `plots/en/08_ideal_world.png`, `plots/en/09_render_culling.png`
- **Serbian**: `plots/sr/01_broad_phase.png` .. `plots/sr/08_ideal_world.png`, `plots/sr/09_render_culling.png`
