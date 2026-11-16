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
| Engine SDK | `engine/CMakeLists.txt` | None required or installed |
| Editor | `editor/CMakeLists.txt` | `editor/resources` |
| Engine tests | `engine/tests/CMakeLists.txt` | `engine/tests/fixtures/assets` |
| Benchmark tools and correctness tests | `engine/benchmarks/CMakeLists.txt` | `engine/benchmarks/assets` |
| Editor performance/GUI probes | `editor/benchmarks/CMakeLists.txt` | selected by the `benchmark` profile |
| Games | each `examples/*/CMakeLists.txt` | each game's own assets |

Tests retain the former demonstration scenes as regression fixtures. Their WAV
fixture is local too; tests no longer copy audio out of Mario at configure time.
Benchmark rendering owns its PNG fixtures and the two scene profiles used by its capture driver. Neither set is an SDK asset
bundle. The root `assets/` directory is no longer needed.

The root `CMakeLists.txt` configures shared dependencies and selects components;
`engine/CMakeLists.txt` owns the library, runner and SDK installation. The editor
continues to link the engine for previews. Public include spellings such as
`<platformator/runtime.h>` and the exported CMake target are unchanged.

The component layout is:

```text
CMakeLists.txt             # workspace composition
engine/
  CMakeLists.txt           # library, runner, SDK
  include/platformator/    # public API
  src/
  tests/
  benchmarks/
editor/
  CMakeLists.txt
  src/
  tests/
  benchmarks/
  resources/
examples/
cmake/
vendored/
```

## Daily builds

| Preset | Mode | Builds |
| --- | --- | --- |
| `default` | Release | Engine and editor |
| `debug` | Debug | Engine and editor, with debug tools |
| `test` | Debug | Engine, editor and correctness tests, with observation counters |
| `benchmark` | Release | Engine/editor benchmark tools and their correctness checks, without observation counters |

```sh
cmake --preset default
cmake --build --preset default --parallel 4
build/default/platformator_editor
```

Replace `default` with `debug` for debugging. No `-DCMAKE_BUILD_TYPE` argument is
needed. Each profile explicitly resets its component, test, sanitizer and install
settings so that old cache values do not change its purpose. The desktop editor
is currently Linux-only, so these editor presets require Linux.

```sh
cmake --preset test
cmake --build --preset test --parallel 4
ctest --preset test
cmake --preset benchmark
cmake --build --preset benchmark --parallel 4
ctest --preset benchmark
```

`ctest --preset test` excludes `platformator_package_smoke`, which builds and
relocates a separate SDK and consumers. Run that expensive check explicitly with
`ctest --test-dir build/test -R platformator_package_smoke --output-on-failure`.
Benchmark correctness checks are small; building tools does not start a timing
campaign. Captures follow the limits in `AGENTS.md`.

The generic runner is an explicit target, excluded from the ordinary editor build:

```sh
cmake --build --preset default --target main --parallel 4
build/default/main --scene /path/to/game.scene --asset-root /path/to/assets
```

Games build separately with the standalone commands below and keep their own
executable/asset directories. Root installation installs the SDK/editor;
games have their own standalone installation workflows.

## Standalone components and SDK

Use a distinct build directory for each source entry point. CMake caches cannot
be switched between root, editor, tests and benchmark source directories.

```sh
cmake -S engine -B build/sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdk --parallel 4
cmake --install build/sdk --prefix /path/to/sdk

cmake -S editor -B build/editor -DCMAKE_BUILD_TYPE=Release
cmake --build build/editor --parallel 4

cmake -S engine/tests -B build/engine-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/engine-tests --parallel 4
ctest --test-dir build/engine-tests --output-on-failure -E package_smoke

cmake -S engine/benchmarks -B build/engine-benchmarks -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine-benchmarks --parallel 4
ctest --test-dir build/engine-benchmarks --output-on-failure

cmake -S examples/mario -B build/mario -DCMAKE_BUILD_TYPE=Release
cmake --build build/mario --parallel 4
cmake -S examples/crystal_citadel -B build/citadel -DCMAKE_BUILD_TYPE=Release
cmake --build build/citadel --parallel 4
```

Consumers use `find_package(Platformator CONFIG REQUIRED)` and link
`Platformator::platformator`. Adding the source checkout as a subdirectory defaults
to the engine alone and does not change the parent's output directory. Games
supply their own asset root; the SDK installs no assets.

The remaining public selectors are `BUILD_TESTING`,
`PLATFORMATOR_BUILD_EDITOR`, and `PLATFORMATOR_BUILD_BENCHMARKS`.
Tests enable their observation hooks together. Debug tools follow Debug or
RelWithDebInfo. Editor-session support is always available to compatible runner
projects, including Release builds. Installation and sanitizers remain advanced
settings rather than additional daily presets. For example:

```sh
cmake --preset test -B build/asan -DPLATFORMATOR_SANITIZER=address-undefined
cmake --build build/asan --parallel 4
ctest --test-dir build/asan --output-on-failure -E package_smoke
```

The old `cleanup-*`, `editor-*`, `production`, `library`, `tests` and sanitizer
preset names have been retired. Reconfigure using the new profile directories;
old build trees and historical reports are not moved or rewritten. Saved custom
editor Build & Run overrides are preserved; update overrides that name retired
presets or old executable paths.

## Validation of this restructuring

The earlier build decoupling was validated on 2026-11-16, with the root `assets/` directory absent:

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

## Component-directory move validation

After moving runtime sources, public headers, tests and benchmarks under
`engine/`, and editor probes/capture drivers under `editor/benchmarks/`:

- Full C++ build and public-header checks succeed; non-package CTest reports
  **66 passed, 6 skipped, 0 failed**. The skips remain the five work-probe
  tests and the restricted D-Bus accessibility check.
- Standalone engine benchmarks build and pass all **4 correctness suites**.
  Engine/editor Python tooling passes **76 tests** in total.
- Standalone engine, engine tests, editor and benchmark configurations succeed.
- SDK installation and a separately built installed consumer run without assets.
  Both game examples build against that SDK and pass five-step dummy-backend
  smoke runs from `/tmp`; the generic runner passes an external-scene smoke run.
- No benchmark performance campaign or physical desktop acceptance was run.

At the time of that move, root presets and executable names remained unchanged. Standalone source entry
points are now `engine/`, `engine/tests/`, `engine/benchmarks/`, and `editor/`.
