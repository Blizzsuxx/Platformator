if(NOT PLATFORMATOR_BUILD_EDITOR)
    message(FATAL_ERROR "The editor subdirectory requires PLATFORMATOR_BUILD_EDITOR=ON.")
endif()

include(CheckSymbolExists)
check_symbol_exists(posix_spawn_file_actions_addchdir "spawn.h"
    PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
if(NOT PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(posix_spawn_file_actions_addchdir_np "spawn.h"
        PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    unset(CMAKE_REQUIRED_DEFINITIONS)
endif()

set(PLATFORMATOR_IMGUI_DIR "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")

add_library(platformator_imgui STATIC
    "${PLATFORMATOR_IMGUI_DIR}/imgui.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/imgui_draw.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/imgui_tables.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/imgui_widgets.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
)
target_compile_features(platformator_imgui PRIVATE cxx_std_20)
target_include_directories(platformator_imgui PRIVATE
    "${PLATFORMATOR_IMGUI_DIR}"
    "${PLATFORMATOR_IMGUI_DIR}/backends"
)
target_link_libraries(platformator_imgui PRIVATE SDL3::SDL3)

add_library(platformator_editor_core STATIC
    src/actions.cpp
    src/document.cpp
    src/documentio.cpp
    src/editcommands.cpp
    src/editorapp.cpp
    src/filedialogs.cpp
    src/process.cpp
    src/process_posix.cpp
    src/preferences.cpp
    src/project.cpp
    src/behaviorcatalog.cpp
    src/preview.cpp
    src/style.cpp
    src/toolbar.cpp
    src/viewport.cpp
    src/panels/console.cpp
    src/panels/behaviors.cpp
    src/panels/behaviorfields.cpp
    src/panels/hierarchy.cpp
    src/panels/inspector.cpp
    src/panels/project.cpp
    src/panels/referencefields.cpp
    src/panels/runsettings.cpp
    src/selection.cpp
    src/validation.cpp
    src/workspace.cpp
)
target_compile_features(platformator_editor_core PRIVATE cxx_std_20)
target_include_directories(platformator_editor_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated"
    "${PLATFORMATOR_IMGUI_DIR}"
    "${PLATFORMATOR_IMGUI_DIR}/backends"
)
target_compile_definitions(platformator_editor_core PRIVATE
    PLATFORMATOR_EDITOR_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
target_link_libraries(platformator_editor_core PRIVATE
    platformator_imgui
    Platformator::platformator
    nlohmann_json::nlohmann_json
)
if(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    target_compile_definitions(platformator_editor_core PRIVATE
        PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR=1)
elseif(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    target_compile_definitions(platformator_editor_core PRIVATE
        _GNU_SOURCE=1
        PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP=1)
endif()

add_executable(platformator_editor src/main.cpp)
target_compile_features(platformator_editor PRIVATE cxx_std_20)
target_link_libraries(platformator_editor PRIVATE platformator_editor_core)

add_executable(platformator_editor_probe benchmark/editor_probe.cpp)
target_compile_features(platformator_editor_probe PRIVATE cxx_std_20)
target_include_directories(platformator_editor_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(platformator_editor_probe PRIVATE platformator_editor_core Platformator::platformator)
target_compile_definitions(platformator_editor_probe PRIVATE
    PLATFORMATOR_EDITOR_PROBE_CONFIGURATION="$<CONFIG>"
    PLATFORMATOR_EDITOR_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

if(DEFINED PLATFORMATOR_INSTALL_RPATH)
    set_target_properties(platformator_editor PROPERTIES
        INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
    )
endif()
