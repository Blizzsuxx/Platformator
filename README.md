# Platformator

A C++20 2D game engine using SDL3, Eigen and oneTBB, with component-based objects, script behaviors, scene serialization and parallel collision detection. The C++ runtime can be embedded or run through the desktop runner. The native scene editor builds by default on Linux and can be disabled; Linux is the only exercised editor platform, and its native desktop, accessibility and performance acceptance remains in progress.

## Build and test

Use a C++20 compiler, CMake 3.21+ with presets, Ninja, and SDL system development prerequisites. Dependencies are bundled. Python 3 is used by benchmark tooling/tests; the Python GUI is unnecessary.

The default preset builds an optimized Release engine and editor with debug tools disabled:

```sh
cmake --preset default
cmake --build --preset default --parallel 3
```

Launch the editor with `build/default/platformator_editor`. For Debug, replace
`default` with `debug` in both commands and the executable path.

Tests and benchmarks have separate build directories:

```sh
cmake --preset test
cmake --build --preset test --parallel 4
ctest --preset test

cmake --preset benchmark
cmake --build --preset benchmark --parallel 4
ctest --preset benchmark
```

The test profile enables engine/editor tests and their observation counters.
The benchmark profile uses Release without those counters. The test preset
excludes the heavyweight SDK installation/relocation smoke test; run it explicitly
with `ctest --test-dir build/test -R platformator_package_smoke --output-on-failure`.

The generic scene runner is an explicit target:

```sh
cmake --build --preset default --target main --parallel 4
build/default/main --scene /path/to/game.scene --asset-root /path/to/assets
```

Build structure and standalone component commands are documented in [BUILD_LAYOUT.md](docs/BUILD_LAYOUT.md). The default root build includes the engine and editor; tests and benchmarks are opt-in.

Headless scenes must use physics-compatible resources; texture/audio asset loading requires the corresponding desktop backend. Tests own their fixture assets under `engine/tests/fixtures/assets`; nonempty asset requests must resolve successfully and do not automatically fall back. Examples build as standalone projects; see [Mario](examples/mario/CMakeLists.txt) (e.g. `cmake -S examples/mario -B build/mario && cmake --build build/mario`). Desktop options include `--window-width`, `--window-height`, `--fullscreen`, `--maximized`, `--keep-aspect-ratio`, `--debug-draw` and `--start-paused`.

Game pause (F5 in debug builds, or the editor's Pause control) freezes game audio along with simulation. Playback positions and fades resume from where they stopped; sounds requested while paused wait for resume. Single-stepping keeps audio suspended. Individually paused sounds retain their state, and editor asset previews remain independent.

## Embed or install

```cmake
add_subdirectory(path/to/Platformator platformator)
target_link_libraries(my_game PRIVATE Platformator::platformator)
```

The default build enables installation: `cmake --install build/default --prefix /path/to/sdk`. For an engine-only SDK, see the standalone engine commands in [BUILD_LAYOUT.md](docs/BUILD_LAYOUT.md). Consumers use `find_package(Platformator CONFIG REQUIRED)` with that prefix in `CMAKE_PREFIX_PATH`, then the same target. Public headers are under `engine/include/platformator`; consumers do not need private source paths or hand-written dependency link lists. The installed SDK and example support prefix relocation.

On Linux, configure and build the `default` preset, then install to add `bin/platformator_editor` plus its SDK runtime libraries, editor resources and license notices. The default docking layout is compiled into the editor. See [editor installation instructions](editor/README.md#build-and-launch). A relocated CLI launch is covered; a live installed GUI launch and Windows/macOS packaging remain unverified.

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
cmake --preset benchmark
cmake --build --preset benchmark --target platformator_benchmark_runner --parallel 4
python3 engine/benchmarks/sweep_scaling.py --runner build/benchmark/platformator_benchmark_runner \
  --scenario broad_phase --counts 100 --workers 1 --seed 0 --warmup-frames 5 \
  --measure-frames 30 --repetitions 3 --raw-output-dir /tmp/broad-runs --output /tmp/broad.csv
```

[Benchmark documentation](engine/benchmarks/README.md) explains schema v3, complete-step boundaries, independent repetitions, validation instrumentation and historical-result limitations. Use focused captures within the current size/time limits; preserve the original [pre-cleanup reference](engine/benchmarks/baselines/pre-cleanup-e01526b/RESULTS.md) as a historical observation.
