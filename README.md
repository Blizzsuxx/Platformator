# Platformator

A C++20 2D game engine using SDL3, Eigen and oneTBB, with component-based objects, script behaviors, scene serialization and parallel collision detection. The C++ runtime can be embedded or run through the desktop runner. The native scene editor is optional; Linux is the only exercised editor platform, and its native desktop, accessibility and performance acceptance remains in progress.

## Build and test

Use a C++20 compiler, CMake 3.21+ with presets, Ninja, and SDL system development prerequisites. Dependencies are bundled. Python 3 is used by benchmark tooling/tests; the Python GUI is unnecessary.

```sh
cmake --preset cleanup-debug
cmake --build --preset cleanup-debug --parallel 4
ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke
```

Work-probe oracle tests report `Skipped` in this configuration; build the `cleanup-work` preset to exercise them.

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

On Linux, configure the editor preset with `-DPLATFORMATOR_INSTALL=ON`, build, and install to add `bin/platformator_editor` plus its SDK runtime libraries, editor resources and license notices. The default docking layout is compiled into the editor. See [editor installation instructions](editor/README.md#build-and-launch). A relocated CLI launch is covered; a live installed GUI launch and Windows/macOS packaging remain unverified.

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

The optional [native editor](editor/README.md) uses SDL3 and Dear ImGui, edits shared `.scene` files, and remains outside runtime-only builds. See the [GUI cleanup quickstart](specs/012-gui-cleanup/quickstart.md) for implemented checks, desktop-review steps and open acceptance items.

The editor's current Linux workflow includes `W/E/R` Scene tools, Hierarchy rename/delete/duplicate, `F6` validation and `Ctrl+,` preferences. It stops only the foreground process tree it launched; embedded Game view is bounded, read-only and does not forward gameplay input. Detached SDL window/renderer hosts and cross-host panel movement have Linux headless coverage; physical two-host, monitor-scale and screen-reader walkthroughs remain open. Windows/macOS adapter compilation and verification, plus matched GUI performance evidence, remain open; see [editor limitations and controls](editor/README.md).

## Benchmarks

```sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --target platformator_benchmark_runner --parallel 4
python3 benchmark/sweep_scaling.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100 --workers 1 --seed 0 --warmup-frames 5 \
  --measure-frames 30 --repetitions 3 --raw-output-dir /tmp/broad-runs --output /tmp/broad.csv
```

[Benchmark documentation](benchmark/README.md) explains schema v3, complete-step boundaries, independent repetitions, validation instrumentation and historical-result limitations. Use focused captures within the current size/time limits; preserve the original [pre-cleanup reference](benchmark/baselines/pre-cleanup-e01526b/RESULTS.md) as a historical observation.
