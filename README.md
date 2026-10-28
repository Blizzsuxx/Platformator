# Platformator

A C++20 2D game engine using SDL3, Eigen and oneTBB, with component-based objects, script behaviors, scene serialization and parallel collision detection. The C++ runtime can be embedded or run through the desktop runner. The Python editor is optional.

## Build and test

Use a C++20/C17 toolchain, CMake 3.22+ with presets, Ninja, and SDL system development prerequisites. Dependencies are bundled. Python 3 is used by benchmark tooling/tests; the Python GUI is unnecessary.

The engine includes a private [Box2D 3.2.0 dynamic-tree subset](vendored/box2d-3.2.0/README.platformator.md), used by opt-in BVH collision and visibility indexes. The installed SDK exposes no Box2D types and consumers need no separate Box2D dependency. Rebuild the SDK and client translation units together after changing spatial settings or upgrading the engine; no binary ABI guarantee is made.

```sh
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke
```

The standalone runner is `bin/cleanup-debug/main`. Supply a scene and its asset root explicitly:

```sh
bin/cleanup-debug/main --scene /path/to/game.scene --asset-root /path/to/assets
printf '[]\n' > /tmp/platformator-empty.scene
bin/cleanup-debug/main --headless --scene /tmp/platformator-empty.scene --smoke-steps 10 --asset-root assets
```

Headless scenes must use physics-compatible resources; texture/audio asset loading requires the corresponding desktop backend. The core assets include example placeholder files; nonempty asset requests must resolve successfully and do not automatically fall back. Build the example with `-DPLATFORMATOR_BUILD_EXAMPLES=ON`; see [Mario](examples/mario/CMakeLists.txt). Desktop options include `--window-width`, `--window-height`, `--fullscreen`, `--maximized`, `--keep-aspect-ratio`, `--debug-draw` and `--start-paused`.

## Embed or install

```cmake
add_subdirectory(path/to/Platformator platformator)
target_link_libraries(my_game PRIVATE Platformator::platformator)
```

Or configure `-DPLATFORMATOR_INSTALL=ON`, build, and `cmake --install bin/cleanup-debug --prefix /path/to/sdk`. Consumers use `find_package(Platformator CONFIG REQUIRED)` with that prefix in `CMAKE_PREFIX_PATH`, then the same target. Public headers are under `include/platformator`; consumers do not need private source paths or hand-written dependency link lists. The installed SDK and example support prefix relocation.

```cpp
#include <platformator/runtime.h>

RuntimeOptions options;
options.windowSettings.headless = true;
platformator::Runtime runtime(options);
runtime.simulateFrame(1.0 / 120.0);
```

One Runtime may be active at a time. It owns objects/components/assets; returned raw pointers are borrowed. Use typed `ObjectReference<T>` for references that must survive lifecycle changes. Scripts request scene replacement/clear; commit occurs after callbacks and graph/component preparation succeeds before the old scene retires. Every behavior resolves its references immediately before its first `start()`, including scene-loaded behaviors; startup errors leave the replacement scene committed. Behaviors enter ordinary dispatch after one initialization attempt; initialization exceptions are logged per behavior, without destroying the behavior or retrying initialization. All asset references use case-preserving `assets/...` paths beneath the configured asset root; scene/animation-relative and absolute asset paths are unsupported. `onDestroy` can read attached components but cannot perform structural edits. Interactive running uses fixed 1/120 steps; explicit stepping executes one caller-specified tick.

Read [C++ API migration](docs/CPP_API_MIGRATION.md) for lifecycle, error, resolver, lifetime and asset-root contracts. [Validation guide](specs/001-cpp-engine-cleanup/quickstart.md) contains sanitizer, work-counter and package commands; [recorded evidence](specs/001-cpp-engine-cleanup/validation.md) distinguishes verified behavior and host limitations. Linux/GCC is exercised; other SDL platforms remain unverified.

The latest [SDL update validation](docs/SDL_UPDATE_VALIDATION.md) records current dependency compatibility, benchmark comparisons and outstanding sanitizer findings.

## Benchmarks

```sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --target platformator_benchmark_runner --parallel 4
python3 benchmark/sweep_scaling.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100,500,1000 --workers 1 --repetitions 5 --output /tmp/broad.csv
```

[Benchmark documentation](benchmark/README.md) explains schema v2, complete-step boundaries, independent repetitions, validation instrumentation and historical-result limitations. Preserve the original [pre-cleanup reference](benchmark/baselines/pre-cleanup-e01526b/RESULTS.md).

### Spatial index selection

`RuntimeOptions::spatialSettings` is copied when a runtime is constructed and remains fixed for that runtime. Defaults preserve segmented collision broad phase and direct sprite submission. All eight combinations are supported:

| Setting | Choices | Default |
| --- | --- | --- |
| Collision broad phase | `segmented`, `bvh` | `segmented` |
| Sprite visibility | `none`, `linear`, `bvh`, `segmented` | `none` |
| BVH maintenance | `native-partial`, `full-rebuild` | `native-partial` |
| BVH padding | finite nonnegative world units | `0` |
| BVH query execution | `serial`, `parallel` | `parallel` |

The CLI accepts the same choices through `--broad-phase`, `--render-culling`, `--bvh-maintenance`, `--bvh-padding`, and `--bvh-query-execution`. Selection alone does not enable drawing; `--render` creates an actual SDL renderer. Render culling is inactive in headless or camera-less frames. Unsupported individual sprites are submitted in draw order; unsupported camera/view state uses a reported submit-all fallback. Grid diagnostics remain Grid-specific and are unavailable when the BVH is selected. See the [runtime selection contract](specs/007-integrate-bvh-culling/contracts/runtime-selection.md) and [spatial benchmark guide](benchmark/README.md) for schema versions, limitations and retained evidence.
