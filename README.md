# Platformator

Platformator can be consumed as a library in two supported host shapes:

1. Use the stock runner and provide a scene path.
2. Create your own `main` and drive `platformator::Runtime` directly.

The Mario example in [examples/mario](examples/mario) demonstrates both source-tree consumption and installed-package consumption.

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