# Platformator Thesis Benchmark Results (2026-11-27, Post-Broadphase & Solver Optimization Refresh)

## Hardware & Build Provenance

- **Host CPU**: AMD Ryzen 5 7535HS with Radeon Graphics (6 physical cores / 12 threads, pinned via `taskset -c 0,2,4,6,8,10` to 6 dedicated physical cores)
- **OS / Kernel**: Manjaro Linux (`6.12.117-1-MANJARO`, x86_64)
- **Compiler & Flags**: GNU GCC `16.2.1`, Release (`-O3 -DNDEBUG`, IPO/LTO enabled, sanitizers/debug-tools/work-probes disabled)
- **Dependencies**: SDL `3.4.18`, SDL_image `3.4.8`, SDL_mixer `3.2.4`, Eigen `5.0.1`, nlohmann/json `3.12.0`, oneTBB `2022.3.0`
- **Methodology**: Schema v3 (`complete_step` + all subscopes), headless, fixed `dt = 1/120 s`, `120` warmup steps, `600` measured steps, `5` independent process repetitions per data point, `seed = 0`. Separate `--validate-invariants` verification passed on all scenarios with 100% bit-for-bit `w=1 == w=6` snapshot parity before timing sweeps. Includes all prior optimizations plus:
  - Cache-line-aligned SoA `positions[]` + `isMaxima[]` key arrays in `LocalSortArray`, compact 24 B `BoundingRadiusProjectionProxy`, and `O(1)` chunk removal via `chunkListIndex`.
  - Direction-aware `AABB::repair()` with unchanged-endpoint skipping.
  - Single-pass `addBulkEmpty()` + `tryClearAllRemoved()` + `tbb::parallel_invoke` across X/Y axes for dense cell batches.
  - Pointer-free 64-bit `PairDelta::sortKey` in `Grid::applyCollectedPairDeltas()`.
  - Parallel `prepareSolverConstraint()`, deferred deterministic `applyWarmStart()`, and thread-local contiguous per-island constraint gathering in `solveIslands()`.
  - Non-template RAII `SlabPool` (`objectpool.cpp`), intrusive `noexcept` `ObjectRegistry` free list, and move-safe `FlatHashSet` / `FlatIdMap`.

## Summary of Physics Scaling Sweeps (`w=1` vs `w=6`)

| # | Scenario | Sweep Range | `w=1` Max Mean / p95 (ms) | `w=6` Max Mean / p95 (ms) | Speedup (`w6`) | Empirical Slope $\alpha$ (`w1` / `w6`) | Dominant Bottleneck / Behavior |
|---|---|---|---:|---:|---:|---:|---|
| 1 | `broad_phase` | `N = 50..4000` | `2.513 / 3.654` | `0.545 / 0.869` | **4.61×** | `1.33 / 0.90` | `Broad Phase (grid)` (`0.426 ms` @ w6); sub-linear/linear scaling with 6 workers |
| 2 | `broad_phase_stress` | `N = 25..400` | `0.275 / 0.395` | `0.298 / 0.387` | `0.92×` | `1.53 / 1.50` | `Broad Phase (grid)` (`0.280 ms` @ w6); ~2.2× faster than pre-opt via bulk build & clear |
| 3 | `narrow_phase` | `N = 10..200` (`4N` colliders) | `21.563 / 26.018` | `5.741 / 7.537` | **3.76×** | `2.39 / 2.27` | `narrow_phase` (`4.798 ms` @ w6) + `Broad Phase (grid)` (`0.889 ms`); well under 120 Hz mean & p95 |
| 4 | `rigid_body_container` | `N = 50..2000` | `1.848 / 2.931` | `0.965 / 1.409` | **1.92×** | `1.32 / 1.09` | `resolve_collisions` (`0.546 ms` @ w6) + `Broad Phase (grid)` (`0.172 ms`); sub-millisecond mean |
| 5 | `add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.789 / 1.120` | `0.617 / 0.798` | **1.28×** | `0.87 / 0.91` | `step_hook` (`0.382 ms`) + `Broad Phase (grid)` (`0.126 ms` @ w6) + `retirement` (`0.056 ms`); linear `O(m)` |
| 6 | `moving_add_and_remove` (`steady=1000`) | `m = 10..1000` | `0.807 / 1.165` | `0.647 / 0.874` | **1.25×** | `0.40 / 0.48`* | Affine `c_0 + c_1 * m`; converges to static churn as `m -> 1000` |
| 7 | `ideal_world` (distributed chambers) | `N = 200..20000` dynamic (`+40..4000` walls) | `39.274 / 62.780` | `13.445 / 19.132` | **2.92×** | `1.33 / 1.24` | `resolve_collisions` (`5.362 ms`) + `narrow_phase` (`3.070 ms`) + `Broad Phase (grid)` (`2.962 ms`); **15k dynamic (18k total) @ 120 Hz mean** (`7.958 ms`, p95 `11.500 ms`), **10k dynamic @ 120 Hz strict p95** (`4.520 / 6.412 ms`), **20k dynamic (24k total) @ 60 Hz mean** (`13.445 ms`, p95 `19.132 ms`) |

## Regenerating 300-DPI PNG Plots

Once `matplotlib` is installed in Python (e.g. `sudo pacman -S python-matplotlib`), run:

```sh
bash engine/benchmarks/results/thesis-20261127/render_plots.sh
```
