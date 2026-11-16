# Build layout

All generated build trees belong under `build/<profile>/`. Installed SDKs and
games retain their conventional `<prefix>/bin` executable directory. Historical
reports retain the paths used when their measurements were captured. CMake build
trees embed absolute paths: regenerate an old `bin/` build using its preset
instead of moving the tree. Existing saved editor executable overrides are
preserved; update those explicitly if they refer to an old build directory.

The root project assembles the engine and, on Linux, the editor. The engine
library never depends on the editor. The editor links the engine because its
scene previews and resource management execute runtime code; building game
projects separately is another use of the engine SDK, not a reason to remove
that link.

| Component | Build owner | Resources |
| --- | --- | --- |
| Engine SDK | root `CMakeLists.txt` | None required or installed |
| Editor | `editor/CMakeLists.txt` | `editor/resources` |
| Engine tests | `tests/CMakeLists.txt` | `tests/fixtures/assets` |
| Benchmark tools and correctness tests | `benchmark/CMakeLists.txt` | `benchmark/assets` |
| Editor performance/GUI probes | `editor/benchmark/CMakeLists.txt` | opt-in via `PLATFORMATOR_BUILD_EDITOR_PROBES` |
| Games | each `examples/*/CMakeLists.txt` | each game's own assets |

Tests retain the former demonstration scenes as regression fixtures. Their WAV
fixture is local too; tests no longer copy audio out of Mario at configure time.
Benchmark rendering owns its PNG fixtures and the two scene profiles used by its capture driver. Neither set is an SDK asset
bundle. The root `assets/` directory is no longer needed.

## Root and SDK builds

```sh
cmake -S . -B build/default
cmake --build build/default --parallel 3
cmake --preset library
cmake --build --preset library --parallel 3
cmake --install build/library --prefix /path/to/sdk
```

A new default root build has tests, benchmarks, game examples and the generic
runner disabled. Linux editor builds enable the editor play-session protocol by
default. Existing cache choices are preserved; use a fresh build directory or
pass options explicitly when changing profiles. The editor remains Linux-only
under the current supported-platform policy.

Consumers use `find_package(Platformator CONFIG REQUIRED)` and link
`Platformator::platformator`. Adding the source checkout as a subdirectory defaults
to the library alone and does not enable editor/test/example products or change
the parent's runtime output directory. There is no `Platformator_ASSET_DIR` in
the SDK: a game provides its own asset root.

`PLATFORMATOR_BUILD_RUNNER=ON` builds the generic `main` scene host. Pass
`--scene /path/to/scene --asset-root /path/to/assets`; it no longer selects a
repository demo scene automatically. `PLATFORMATOR_BUILD_EXAMPLES=ON` adds both
games, with separate executable/asset directories to avoid collisions. Aggregate installs put
them under `examples/mario` and `examples/crystal_citadel`; standalone installs
keep each game's usual `bin`, `lib`, and `assets` layout.

## Standalone components

Use a distinct binary directory for each source entry point; CMake caches cannot
be switched between root, editor, tests and benchmark source directories.

```sh
cmake -S editor -B build/editor
cmake --build build/editor --parallel 3
cmake -S tests -B build/tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests --parallel 3
ctest --test-dir build/tests --output-on-failure -E package_smoke
cmake -S benchmark -B build/benchmarks -DCMAKE_BUILD_TYPE=Release
cmake --build build/benchmarks --parallel 3
ctest --test-dir build/benchmarks --output-on-failure
cmake -S examples/mario -B build/mario
cmake --build build/mario --parallel 3
cmake -S examples/crystal_citadel -B build/citadel
cmake --build build/citadel --parallel 3
```

Standalone benchmark builds default their own correctness tests on. Set
`PLATFORMATOR_BUILD_BENCHMARK_TESTS=OFF` for tools alone. Engine tests, editor tests (`PLATFORMATOR_BUILD_EDITOR_TESTS`), and Python
benchmark tests are independent; ordinary engine/editor builds do not require
Python. Instrumentation (`PLATFORMATOR_ENABLE_BENCHMARKS`) enables benchmark
products independently of `PLATFORMATOR_BUILD_RUNNER`.

The `tests` preset offers the root-based engine-test build as an alternative.
The existing debug, sanitizer and benchmark presets explicitly retain their
validation settings. Benchmark captures still follow `AGENTS.md`; building
benchmark tools does not authorize a timing campaign.

## Validation of this restructuring

On 2026-11-16, with the root `assets/` directory absent:

- Full engine/editor build and public-header compilation succeeded. Shared editor
  test helpers now compile once instead of once per test executable.
- Non-package CTest: **66 passed, 6 skipped, 0 failed** (33.92 seconds). Five
  skips require work-probe instrumentation; one requires the Linux accessibility
  environment. Two counter-only assertions were corrected to honor disabled
  editor counters; their functional assertions still execute.
- Standalone benchmark build: **4/4 correctness suites passed**, including
  **52 Python tests**. No performance capture or full timing campaign ran.
- Fresh configuration/target graphs checked for SDK-only, standalone editor,
  standalone tests, both standalone games, and aggregate examples.
- Installed SDK has no asset directory. After relocating it, an installed
  public-header consumer ran from `/tmp` without assets.
- Both games compiled against that relocated SDK and completed five-step dummy
  video/audio smoke runs from `/tmp`. The generic runner completed three steps
  with an explicitly supplied external scene and asset root.
- The relocated installed editor's `--help` runs successfully.

The heavyweight `package_smoke` CTest was excluded in favor of the explicit SDK
installation/relocation/consumer/game checks above. Standalone editor and test
entry points were configuration-checked; their code was built through the root
validation build. Physical desktop interaction and other operating systems were
not tested by this restructuring.
