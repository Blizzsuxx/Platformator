# Cross-Engine 2D Physics Comparison: Platformator vs. Godot 4.7 vs. Unity 6.3 vs. Box2D (v2.4.2 & v3.1.1)

## Hardware, Build Provenance & Methodology

- **CPU**: AMD Ryzen 5 7535HS (6 physical cores / 12 threads, pinned via `taskset -c 0,2,4,6,8,10`)
- **OS & Compiler**: Manjaro Linux (`x86_64`), GNU GCC `16.2.1` (`-O3 -DNDEBUG`, IPO/LTO enabled for Platformator, Box2D v2.4.2, and Box2D v3.1.1)
- **Step Configuration**: Fixed `dt = 1/120 s`, `120` warmup steps, `600` measured steps, headless, sleeping enabled, zero per-body script loops.
  - **Platformator**: Full engine `complete_step` (includes `GameObject` transform sync, scripts/events, animation, and deferred commits), `10` solver iterations, `gravity = (0, 98.1)`, 5 reps from `engine/benchmarks/results/thesis-20261121/raw/`.
  - **Godot 4.7.2 (`GodotPhysics2D`, `1T`)**: Both `Node2D` SceneTree mode and direct `PhysicsServer2D` (`--server-only`) mode, `10` solver iterations, `gravity = (0, 98.1)`, 3 reps from `engine/benchmarks/results/godot-comparison-20261122/`.
  - **Unity `6000.3.22f1` (Unity 6.3 `Physics2D`, Linux64 Release Player, `-batchmode -nographics`)**: `GameObject` + `Rigidbody2D` + `BoxCollider2D`/`CircleCollider2D`, `Physics2D.simulationMode = Script`, `Physics2D.Simulate(1/120)` timed via `Stopwatch`, `10` velocity / `3` position iterations, `0.1` meter unit scale (`gravity = 9.81 m/s²`), discrete collision, tested in both default single-threaded (`1T`) and Job System multithreaded (`6T`, `useMultithreading = true`, `useConsistencySorting = true`, `JobWorkerCount = 5` + main thread) modes, 3 reps. *(Note: the 420 s sweep budget completed cleanly through `N = 10,000` for Unity `1T` and `N = 5,000` for Unity `6T` on `ideal_world`, where Unity `1T` had already reached `26.98 ms/step`.)*
  - **Box2D v2.4.2 (`1T`)**: Classic C++ AoS library (underlying 2D core wrapped by Unity `Physics2D`), `10` velocity / `3` position iterations, discrete step, `0.1` meter unit scale (`gravity = 9.81 m/s²`), 3 reps.
  - **Box2D v3.1.1 (`w=1` & `w=6` via oneTBB)**: Modern C11 SoA + SSE2 SIMD + graph-colored TGS Soft solver, `subStepCount = 4` (default), discrete step, `0.1` meter unit scale (`gravity = 9.81 m/s²`), 3 reps.

### Reproduction Commands

```bash
# 1. Build standalone Box2D v2.4.2 & v3.1.1 runners
cmake -S engine/benchmarks/box2d -B build/box2d-bench -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/box2d-bench -j6

# 2. Build standalone Unity 6.3 Linux64 Release Player
/home/user/Unity/Hub/Editor/6000.3.22f1/Editor/Unity -batchmode -nographics -quit \
  -projectPath engine/benchmarks/unity -executeMethod BuildBench.BuildLinux64

# 3. Run Box2D + Godot merge, then Unity merge, then plots
python3 engine/benchmarks/compare_box2d.py \
  --box2d-build-dir build/box2d-bench \
  --platformator-raw engine/benchmarks/results/thesis-20261121/raw \
  --godot-json engine/benchmarks/results/godot-comparison-20261122/godot_vs_platformator.json \
  --output-dir engine/benchmarks/results/cross-engine-20261122

python3 engine/benchmarks/compare_unity.py \
  --unity-player build/unity-bench/UnityBench.x86_64 \
  --cross-json engine/benchmarks/results/cross-engine-20261122/cross_engine_comparison.json
```

## Mean Step Time Results (ms)

### 1. `broad_phase` (Coherent Moving Lanes)

| `N` | Godot `Node2D` | Godot `Server2D` | Unity `1T` | Unity `6T` | Platformator `w=1` | Platformator `w=6` | Box2D `v2.4.2` | Box2D `v3.1` (`w=1`) | Box2D `v3.1` (`w=6`) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 50 | 0.020 | 0.013 | 0.009 | 0.022 | 0.009 | 0.011 | 0.019 | 0.006 | 0.010 |
| 100 | 0.042 | 0.024 | 0.021 | 0.035 | 0.022 | 0.023 | 0.043 | 0.011 | 0.015 |
| 200 | 0.082 | 0.042 | 0.046 | 0.053 | 0.040 | 0.037 | 0.105 | 0.021 | 0.021 |
| 500 | 0.198 | 0.111 | 0.118 | 0.128 | 0.122 | 0.085 | 0.283 | 0.052 | 0.040 |
| 1,000 | 0.423 | 0.225 | 0.254 | 0.225 | 0.304 | 0.139 | 0.602 | 0.113 | 0.061 |
| 2,000 | 1.179 | 0.446 | 0.515 | 0.424 | 0.988 | 0.264 | 1.298 | 0.226 | 0.121 |
| 4,000 | 3.019 | 0.965 | 1.168 | 0.888 | 3.590 | **0.857** | 2.689 | 0.471 | 0.244 |

### 2. `rigid_body_container` (Dense Single Island)

| `N` | Godot `Node2D` | Godot `Server2D` | Unity `1T` | Unity `6T` | Platformator `w=1` | Platformator `w=6` | Box2D `v2.4.2` | Box2D `v3.1` (`w=1`) | Box2D `v3.1` (`w=6`) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 50 | 0.098 | 0.083 | 0.026 | 0.045 | 0.015 | 0.014 | 0.028 | 0.014 | 0.018 |
| 100 | 0.153 | 0.127 | 0.067 | 0.081 | 0.039 | 0.038 | 0.063 | 0.033 | 0.037 |
| 200 | 0.283 | 0.241 | 0.175 | 0.174 | 0.093 | 0.088 | 0.143 | 0.080 | 0.073 |
| 500 | 0.830 | 0.683 | 0.556 | 0.466 | 0.347 | 0.303 | 0.417 | 0.261 | 0.146 |
| 1,000 | 2.024 | 1.528 | 1.264 | 1.126 | 0.955 | 0.732 | 0.992 | 0.547 | 0.248 |
| 1,500 | 3.672 | 2.326 | 2.303 | 1.947 | 1.471 | 0.941 | 1.470 | 0.933 | 0.357 |
| 2,000 | 5.460 | 3.441 | 2.985 | 2.514 | 2.265 | **1.199** | 2.199 | 1.438 | 0.517 |

### 3. `ideal_world` (Disjoint 20-Body Chambers)

| `N` (dynamic) | Godot `Node2D` | Godot `Server2D` | Unity `1T` | Unity `6T` | Platformator `w=1` | Platformator `w=6` | Box2D `v2.4.2` | Box2D `v3.1` (`w=1`) | Box2D `v3.1` (`w=6`) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 200 | 0.236 | 0.192 | 0.194 | 0.173 | 0.132 | 0.120 | 0.131 | 0.072 | 0.085 |
| 500 | 0.520 | 0.383 | 0.499 | 0.396 | 0.340 | 0.245 | 0.344 | 0.187 | 0.131 |
| 1,000 | 1.042 | 0.710 | 1.140 | 0.771 | 0.735 | 0.411 | 0.690 | 0.395 | 0.216 |
| 2,000 | 3.181 | 1.607 | 2.585 | 2.456 | 2.102 | 0.892 | 1.431 | 0.865 | 0.337 |
| 5,000 | 9.998 | 5.722 | 11.028 | 7.811 | 7.594 | **2.828** | 4.578 | 2.805 | 0.724 |
| 10,000 | 23.603 | 15.638 | 26.981 | *budget* | 20.337 | **8.551** | 11.472 | 7.905 | 1.704 |
| 15,000 | 35.483 | 24.177 | *budget* | *budget* | 35.895 | **16.042** | 21.149 | 13.692 | 3.247 |
| 20,000 | *timeout* | 31.834 | *budget* | *budget* | 49.170 | **25.434** | 29.597 | 20.098 | 4.862 |

## Generated Plots (300 DPI)

- **English**: `plots/en/01_broad_phase_cross_engine.png` .. `plots/en/04_summary_overview_cross_engine.png`
- **Serbian**: `plots/sr/01_broad_phase_cross_engine.png` .. `plots/sr/04_summary_overview_cross_engine.png`
