# Platformator

Platformator can be used in three common ways:

1. Run the stock desktop runner and load a scene.
2. Run benchmark scenarios and collect performance summaries.
3. Consume Platformator as a library in your own host.

The Mario example in [examples/mario](examples/mario) demonstrates both source-tree consumption and installed-package consumption.

## Quick Start: Build And Run

Platformator uses CMake presets.

Build debug runner:

```bash
cmake --build --preset debug
```

Run the default scene:

```bash
./bin/debug/main
```

Run a specific scene:

```bash
./bin/debug/main --scene assets/scenes/mario.scene
```

Build benchmark runner (instrumentation enabled):

```bash
cmake --build --preset benchmark-debug --target platformator_benchmark_runner
```

Run a benchmark scenario:

```bash
./bin/benchmark-debug/platformator_benchmark_runner --scenario broad_phase
```

## Build Presets And Output Paths

Main presets from [CMakePresets.json](CMakePresets.json):

- `debug` -> build output in `bin/debug`, install prefix `dist/debug`
- `benchmark-debug` -> build output in `bin/benchmark-debug`, install prefix `dist/benchmark-debug`
- `benchmark-release` -> build output in `bin/benchmark-release`, install prefix `dist/benchmark-release`
- `production` -> build output in `bin/production`, install prefix `dist/production`

The stock runner binary is `main` and is built when `PLATFORMATOR_BUILD_RUNNER=ON`.
The benchmark binary is `platformator_benchmark_runner` and is built when both `PLATFORMATOR_BUILD_RUNNER=ON` and `PLATFORMATOR_ENABLE_BENCHMARKS=ON`.

## Stock Runner CLI

The stock runner entrypoint is implemented in [src/main.cpp](src/main.cpp) and delegates to [src/runner.cpp](src/runner.cpp).

Usage pattern:

```bash
./bin/debug/main [scene_path]
./bin/debug/main --scene path/to.scene
```

Supported runtime arguments:

- `--scene <path>`
- `--window-width <positive-int>`
- `--window-height <positive-int>`
- `--fullscreen`
- `--maximized`
- `--keep-aspect-ratio`
- `--debug-draw <value>`
    - accepted values: `all`, `default`, `none`
    - or comma-separated categories: `colliders`, `collision-points` (alias `points`), `collision-normals` (alias `normals`), `collisions`, `grid-cells` (alias `gridcells`)
- `--start-paused`

Examples:

```bash
./bin/debug/main --scene assets/scenes/mario.scene --window-width 1920 --window-height 1080 --keep-aspect-ratio

./bin/debug/main --scene assets/scenes/mario.scene --debug-draw colliders,collisions,grid-cells --start-paused
```

## Benchmark Runner CLI

Benchmark runner source is [src/benchmark_main.cpp](src/benchmark_main.cpp).

Usage:

```bash
./bin/benchmark-debug/platformator_benchmark_runner [scene] [--scene path | --scenario broad_phase|narrow_phase|rigid_body_container] [--warmup-frames N] [--measure-frames N] [--dt seconds] [--box-count N] [--circle-count N] [--broad-count N] [--narrow-count N] [--csv-output path] [--render]
```

Supported benchmark arguments:

- `--scene <path>`
- `--scenario <broad_phase|narrow_phase|rigid_body_container>`
- `--warmup-frames <non-negative-int>`
- `--measure-frames <positive-int>`
- `--dt <positive-float-seconds>`
- `--box-count <non-negative-int>` (rigid body container)
- `--circle-count <non-negative-int>` (rigid body container)
- `--broad-count <positive-int>` (broad phase lane movers)
- `--narrow-count <positive-int>` (narrow phase element density)
- `--csv-output <path>` (alias `--csv`)
- `--render` (visual run instead of headless frame stepping)

Notes:

- `--scene` and `--scenario` are mutually exclusive.
- For `rigid_body_container`, at least one of `--box-count` or `--circle-count` must be greater than zero.
- CSV output writes a single summary file with rows for metadata, scope timings, and counters.

Examples:

```bash
# Broad phase scaling experiment (25 movers)
./bin/benchmark-release/platformator_benchmark_runner --scenario broad_phase --broad-count 25 --warmup-frames 120 --measure-frames 600

# Narrow phase scaling experiment (12 elements per axis/band set)
./bin/benchmark-release/platformator_benchmark_runner --scenario narrow_phase --narrow-count 12 --warmup-frames 120 --measure-frames 600

# Rigid body container scenario with custom body counts
./bin/benchmark-release/platformator_benchmark_runner --scenario rigid_body_container --box-count 400 --circle-count 400

# Export benchmark summary to CSV
./bin/benchmark-release/platformator_benchmark_runner --scenario broad_phase --broad-count 128 --csv-output benchmark_broad_128.csv
```

## Consume From Source

Add Platformator as a subdirectory and link against `Platformator::platformator`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyGame LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PLATFORMATOR_BUILD_RUNNER OFF CACHE BOOL "" FORCE)
set(PLATFORMATOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLATFORMATOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PLATFORMATOR_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(path/to/Platformator)

add_executable(my_game
    src/main.cpp
    src/my_player_behavior.cpp
)

target_link_libraries(my_game PRIVATE Platformator::platformator)
target_include_directories(my_game PRIVATE src)
```

## Consume As An Installed Package

First install Platformator from its own source tree:

```bash
cmake --build --preset debug --target install
```

Then consume it from another project with `find_package`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyGame LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Platformator CONFIG REQUIRED)

add_executable(my_game
    src/main.cpp
    src/my_player_behavior.cpp
)

target_link_libraries(my_game PRIVATE Platformator::platformator)
target_include_directories(my_game PRIVATE src)
```

Configure your consumer with `CMAKE_PREFIX_PATH` pointing at the Platformator install prefix, for example `dist/debug`.

## Host Path 1: Stock Runner

Use the helper entrypoint when you want Platformator to parse runtime options and run a scene for you:

```cpp
#include "platformator/runner.h"

int main(int argc, char *args[])
{
    return platformator::run(argc, args, "assets/scenes/default.scene");
}
```

This is the simplest host path and matches [src/main.cpp](src/main.cpp).

## Host Path 2: Custom Runtime

Create your own host when you want to control startup, scene loading, object creation, or the frame loop setup:

```cpp
#include "platformator/runtime.h"
#include "platformator/runtimeoptions.h"

int main()
{
    RuntimeOptions options;
    options.sceneFilePath = "assets/scenes/default.scene";

    platformator::Runtime runtime(options);
    runtime.loadScene(options.sceneFilePath);
    runtime.run();
    return 0;
}
```

You can also skip `loadScene` and populate the scene manually with `createGameObject()` before calling `run()`.

## Writing Behaviors

Behavior authors should include only the public Platformator headers they actually use. The common starting point is:

```cpp
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"
```

Add component headers as needed, for example `platformator/rigidbody.h`, `platformator/sprite.h`, or `platformator/audio.h`.

Example behavior skeleton:

```cpp
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

class MyBehavior : public Behavior
{
public:
    void start() override
    {
        getGameObject()->setName("Player");
    }

    SERIALIZABLE_SCRIPT(MyBehavior);
};
```

## Mario Example

The Mario example is a separate consumer project under [examples/mario](examples/mario):

- Source-tree consumer: configure the example directly and let it add Platformator as a subdirectory.
- Installed-package consumer: configure with `-DPLATFORMATOR_MARIO_USE_INSTALLED=ON -DCMAKE_PREFIX_PATH=/path/to/Platformator/dist/debug`.

Installed-package example command:

```bash
cmake -S examples/mario -B build/mario \
  -DPLATFORMATOR_MARIO_USE_INSTALLED=ON \
  -DCMAKE_PREFIX_PATH="$PWD/dist/debug"

cmake --build build/mario --target mario_example
```

## Useful CMake Options

Important options from [CMakeLists.txt](CMakeLists.txt):

- `PLATFORMATOR_ENABLE_DEBUG_TOOLS` (default ON)
- `PLATFORMATOR_ENABLE_BENCHMARKS` (default OFF)
- `PLATFORMATOR_BUILD_RUNNER`
- `PLATFORMATOR_BUILD_EXAMPLES`
- `PLATFORMATOR_BUILD_TESTS`
- `PLATFORMATOR_INSTALL`