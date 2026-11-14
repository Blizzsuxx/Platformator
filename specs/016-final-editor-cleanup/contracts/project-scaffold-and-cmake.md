# Contract: Project Scaffolding & Generated `CMakeLists.txt`

**Feature**: `016-final-editor-cleanup`
**Covers**: `FR-001`, `FR-002`, `FR-003`, `FR-004`

## 1. Scaffolded New Project Structure

Creating a new project `<name>` under `<parent>` creates:

```text
<parent>/<name>/
├── CMakeLists.txt
├── main.cpp
├── .platformator/
│   └── layers.json
└── assets/
    ├── behaviors/
    ├── templates/
    ├── audio/
    └── scenes/
        └── main.scene
```

- `main.cpp` includes `"platformator/runner.h"` and calls `return platformator::run(argc, args, "assets/scenes/main.scene");`.
- `assets/scenes/main.scene` is a valid empty/starter scene JSON array (e.g. containing a starter Camera GameObject on `"layer": 0`) that passes `SceneValidator` cleanly on first open.
- `.platformator/layers.json` initializes with slot `0: "Default"` and full `0xFFFFFFFF` symmetric collision matrix.

## 2. Generated `CMakeLists.txt` Contract

The generated `CMakeLists.txt` (for both **New Project...** and **"Generate CMakeLists.txt"** when opening an existing project without `CMakeLists.txt`) MUST:

1. Declare `cmake_minimum_required(VERSION 3.20)` and `project(<SanitizedProjectName> LANGUAGES CXX)` with C++20 (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`).
2. Locate Platformator using installed package first, falling back automatically to the local engine source tree:
   ```cmake
   set(PLATFORMATOR_ENGINE_SOURCE_DIR "<detected_platformator_source_path>" CACHE PATH
       "Fallback path to local Platformator source tree when Platformator CONFIG package is not installed")

   find_package(Platformator CONFIG QUIET)
   if(NOT Platformator_FOUND)
       if(EXISTS "${PLATFORMATOR_ENGINE_SOURCE_DIR}/CMakeLists.txt")
           add_subdirectory("${PLATFORMATOR_ENGINE_SOURCE_DIR}"
                            "${CMAKE_CURRENT_BINARY_DIR}/platformator"
                            EXCLUDE_FROM_ALL)
       else()
           message(FATAL_ERROR
               "Platformator package not found and fallback source directory "
               "'${PLATFORMATOR_ENGINE_SOURCE_DIR}' does not contain CMakeLists.txt. "
               "Pass -DPlatformator_DIR=<prefix> or -DPLATFORMATOR_ENGINE_SOURCE_DIR=<repo>.")
       endif()
   endif()
   ```
3. Automatically discover project sources (`main.cpp` plus `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` under `assets/behaviors/` for `*.cpp` and `*.h`), avoiding hardcoding individual behavior filenames.
4. Link `Platformator::platformator` (matching `examples/mario/CMakeLists.txt` alias/target conventions) and ensure runtime asset path & shared-library deployment work cleanly out of the project build output directory.

## 3. Existing Project Without `CMakeLists.txt` Handling

- `validate_project_folder(root)` classifies a directory containing `assets/` or `.scene` files without `CMakeLists.txt` as `missingCmake = true` (valid asset project needing build configuration) rather than blocking project open.
- Opening such a project opens its workspace normally and displays an explicit **"Generate CMakeLists.txt"** banner/action in Project Validation & Project Settings (`Build & Run`), plus prompts cleanly if the user triggers Build/Play without a `CMakeLists.txt`.
- **"Generate CMakeLists.txt"** refuses to silently overwrite an already-existing `CMakeLists.txt` without explicit overwrite confirmation.
