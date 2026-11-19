cmake_minimum_required(VERSION 3.16)

if(TARGET Platformator::platformator)
    return()
endif()

project(Platformator VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PLATFORMATOR_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PLATFORMATOR_IS_TOP_LEVEL ON)
endif()

# User-facing choices: component selection, tests, and benchmark products.
# Runtime features and observation hooks follow the selected build purpose.
option(BUILD_TESTING "Build engine/editor correctness tests" OFF)
option(PLATFORMATOR_BUILD_BENCHMARKS "Build engine/editor benchmark tools" OFF)
set(platformator_default_editor OFF)
if(PLATFORMATOR_IS_TOP_LEVEL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(platformator_default_editor ON)
endif()
option(PLATFORMATOR_BUILD_EDITOR "Build the desktop editor" ${platformator_default_editor})
option(PLATFORMATOR_INSTALL "Enable SDK/editor installation rules" ${PLATFORMATOR_IS_TOP_LEVEL})
mark_as_advanced(PLATFORMATOR_INSTALL)

# Retire old cache controls: changing build purpose must not retain old hooks.
foreach(old_option PLATFORMATOR_ENABLE_DEBUG_TOOLS PLATFORMATOR_ENABLE_BENCHMARKS
        PLATFORMATOR_ENABLE_WORK_PROBES PLATFORMATOR_BUILD_RUNNER PLATFORMATOR_BUILD_TESTS
        PLATFORMATOR_BUILD_EDITOR_TESTS PLATFORMATOR_BUILD_EDITOR_PROBES
        PLATFORMATOR_BUILD_EXAMPLES PLATFORMATOR_ENABLE_EDITOR_SESSION PLATFORMATOR_EDITOR_WORK_COUNTERS)
    unset(${old_option} CACHE)
endforeach()
set(PLATFORMATOR_ENABLE_DEBUG_TOOLS OFF)
if(CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
    set(PLATFORMATOR_ENABLE_DEBUG_TOOLS ON)
endif()
set(PLATFORMATOR_ENABLE_BENCHMARKS ${PLATFORMATOR_BUILD_BENCHMARKS})
set(PLATFORMATOR_BUILD_TESTS ${BUILD_TESTING})
set(PLATFORMATOR_ENABLE_WORK_PROBES ${BUILD_TESTING})
set(PLATFORMATOR_BUILD_EDITOR_TESTS ${BUILD_TESTING})
set(PLATFORMATOR_EDITOR_WORK_COUNTERS ${BUILD_TESTING})
set(PLATFORMATOR_BUILD_EDITOR_PROBES ${PLATFORMATOR_BUILD_BENCHMARKS})
set(PLATFORMATOR_ENABLE_EDITOR_SESSION ON)
set(PLATFORMATOR_BUILD_RUNNER ${PLATFORMATOR_IS_TOP_LEVEL})

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(cmake/PlatformatorSanitizers.cmake)
include(cmake/PlatformatorDependencies.cmake)

if(APPLE)
    set(PLATFORMATOR_INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
    set(PLATFORMATOR_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

set(PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
foreach(platformator_runtime_target SDL3-shared SDL3_image-shared SDL3_mixer-shared tbb)
    if(TARGET ${platformator_runtime_target})
        list(APPEND PLATFORMATOR_RUNTIME_LIBRARY_TARGETS ${platformator_runtime_target})
    endif()
endforeach()

set(PLATFORMATOR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PLATFORMATOR_ENGINE_DIR "${PLATFORMATOR_ROOT_DIR}/engine")
set(PLATFORMATOR_ENGINE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/engine")
if(PLATFORMATOR_BUILD_TESTS OR PLATFORMATOR_BUILD_EDITOR_TESTS OR PLATFORMATOR_ENABLE_BENCHMARKS)
    enable_testing()
endif()
add_subdirectory(engine)

if(PLATFORMATOR_BUILD_EDITOR)
    if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$")
        message(FATAL_ERROR "PLATFORMATOR_BUILD_EDITOR is supported on Linux; Windows is experimental and unverified.")
    endif()
    add_subdirectory(editor)
    if(PLATFORMATOR_BUILD_EDITOR_TESTS)
        add_subdirectory(editor/tests)
    endif()
    if(PLATFORMATOR_INSTALL)
        include(cmake/PlatformatorEditorInstall.cmake)
        platformator_install_editor()
    endif()
endif()
