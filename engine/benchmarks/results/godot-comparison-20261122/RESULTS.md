# Platformator vs. Godot 4 (`GodotPhysics2D`) Benchmark Comparison

## Hardware, Software & Reproducibility

- **CPU**: AMD Ryzen 5 7535HS (6 physical cores / 12 threads, pinned via `taskset -c 0,2,4,6,8,10`)
- **OS**: Manjaro Linux (`x86_64`)
- **Platformator**: Release (`-O3 -DNDEBUG`, IPO/LTO), GCC `16.2.1`, baseline data from `engine/benchmarks/results/thesis-20261121/raw/` (5 reps × 600 measured steps after 120 warmup, `dt = 1/120 s`, `seed = 0`)
- **Godot**: `/usr/bin/godot` `4.7.2.stable.arch_linux` (`GodotPhysics2D`, single-threaded), 3 reps × 600 measured steps after 120 warmup, `--headless --fixed-fps 120`, swept in **both** standard `Node2D` SceneTree mode and direct `PhysicsServer2D` (`--server-only`) mode.
  - *Binary provenance note*: `/usr/bin/godot` is the Arch system editor binary (`OS.is_debug_build() == true`, stripped `-O2/-O3`) because export templates were not installed locally. A direct smoke check against the pre-exported `engine/benchmarks/godot/RigidBodyContainerRelease.x86_64` (Godot `4.6.3.stable.official` release export) confirmed `/usr/bin/godot` 4.7.2 was ~5% faster (`4.189 ms` vs `4.414 ms` on the legacy scene).
- **Godot Harness**: `engine/benchmarks/godot/comparison/` (`project.godot`, `bench.tscn`, `bench.gd`)
  - Exact parameter parity with Platformator: `physics_ticks_per_second = 120`, `gravity = (0, 98.1)`, `solver_iterations = 10`, `linear_damp = 0`, `angular_damp = 0`, `sleep_threshold_linear = 5.0`, `sleep_threshold_angular = 0.05`, `time_before_sleep = 0.25`.
  - Shared `Shape2D` resources, zero visual nodes, zero per-tick GDScript loops, true per-tick wall timing via `Time.get_ticks_usec()`.

### Reproduction Commands

```bash
# Re-run full Godot 4 sweep (both Node2D and PhysicsServer2D) and regenerate plots
python3 engine/benchmarks/compare_godot.py \
  --platformator-raw engine/benchmarks/results/thesis-20261121/raw \
  --output-dir engine/benchmarks/results/godot-comparison-20261122

# Regenerate plots only from saved JSON
python3 engine/benchmarks/plot_godot_comparison.py \
  --input engine/benchmarks/results/godot-comparison-20261122/godot_vs_platformator.json \
  --output-dir engine/benchmarks/results/godot-comparison-20261122/plots
```

## Side-by-Side Mean Step Time (ms)

### 1. `broad_phase` (Coherent Moving Lanes)

| `N` | Godot 4 `Node2D` (ms) | Godot 4 `PhysicsServer2D` (ms) | Platformator `w=1` (ms) | Platformator `w=6` (ms) | `Node / P6` | `Server / P6` |
|---:|---:|---:|---:|---:|---:|---:|
| 50 | 0.020 | 0.013 | 0.009 | 0.011 | 1.76× | 1.10× |
| 100 | 0.042 | 0.024 | 0.022 | 0.023 | 1.87× | 1.07× |
| 200 | 0.082 | 0.042 | 0.040 | 0.037 | 2.20× | 1.14× |
| 500 | 0.198 | 0.111 | 0.122 | 0.085 | 2.34× | 1.30× |
| 1,000 | 0.423 | 0.225 | 0.304 | 0.139 | 3.04× | 1.61× |
| 2,000 | 1.179 | 0.446 | 0.988 | 0.264 | **4.47×** | **1.69×** |
| 4,000 | 3.019 | 0.965 | 3.590 | 0.857 | **3.52×** | **1.13×** |

### 2. `rigid_body_container` (Dense Single Island)

| `N` | Godot 4 `Node2D` (ms) | Godot 4 `PhysicsServer2D` (ms) | Platformator `w=1` (ms) | Platformator `w=6` (ms) | `Node / P6` | `Server / P6` |
|---:|---:|---:|---:|---:|---:|---:|
| 50 | 0.098 | 0.083 | 0.015 | 0.014 | 6.90× | 5.86× |
| 100 | 0.153 | 0.127 | 0.039 | 0.038 | 4.00× | 3.30× |
| 200 | 0.283 | 0.241 | 0.093 | 0.088 | 3.23× | 2.74× |
| 500 | 0.830 | 0.683 | 0.347 | 0.303 | 2.74× | 2.25× |
| 1,000 | 2.024 | 1.528 | 0.955 | 0.732 | 2.77× | 2.09× |
| 1,500 | 3.672 | 2.326 | 1.471 | 0.941 | 3.90× | 2.47× |
| 2,000 | 5.460 | 3.441 | 2.265 | 1.199 | **4.56×** | **2.87×** |

### 3. `ideal_world` (Disjoint 20-Body Chambers)

| `N` (dynamic) | Godot 4 `Node2D` (ms) | Godot 4 `PhysicsServer2D` (ms) | Platformator `w=1` (ms) | Platformator `w=6` (ms) | `Node / P6` | `Server / P6` |
|---:|---:|---:|---:|---:|---:|---:|
| 200 | 0.236 | 0.192 | 0.132 | 0.120 | 1.97× | 1.60× |
| 500 | 0.520 | 0.383 | 0.340 | 0.245 | 2.12× | 1.56× |
| 1,000 | 1.042 | 0.710 | 0.735 | 0.411 | 2.54× | 1.73× |
| 2,000 | 3.181 | 1.607 | 2.102 | 0.892 | 3.56× | 1.80× |
| 5,000 | 9.998 | 5.722 | 7.594 | 2.828 | **3.54×** | **2.02×** |
| 10,000 | 23.603 | 15.638 | 20.337 | 8.551 | **2.76×** | **1.83×** |
| 15,000 | 35.483 | 24.177 | 35.895 | 16.042 | **2.21×** | **1.51×** |
| 20,000 | *timeout (>42 ms/step)* | 31.834 | 49.170 | 25.434 | — | **1.25×** |

## Generated Plots (300 DPI)

- **English**: `plots/en/01_broad_phase_vs_godot.png` .. `plots/en/04_summary_overview_vs_godot.png`
- **Serbian**: `plots/sr/01_broad_phase_vs_godot.png` .. `plots/sr/04_summary_overview_vs_godot.png`
