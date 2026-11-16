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

set(platformator_default_debug_tools ON)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
    set(platformator_default_debug_tools OFF)
endif()
option(PLATFORMATOR_ENABLE_DEBUG_TOOLS "Enable debug-only logging, overlays, and frame advance controls." ${platformator_default_debug_tools})
option(PLATFORMATOR_ENABLE_BENCHMARKS "Enable compile-time benchmark instrumentation and runtime summaries." OFF)
option(PLATFORMATOR_ENABLE_WORK_PROBES "Enable test-only affected-work counters (never for performance runs)." OFF)
option(PLATFORMATOR_BUILD_RUNNER "Build the generic scene runner." OFF)
option(PLATFORMATOR_BUILD_TESTS "Build Platformator test executables." OFF)
option(PLATFORMATOR_INSTALL "Install Platformator library headers and package files." ${PLATFORMATOR_IS_TOP_LEVEL})
set(platformator_default_editor OFF)
if(PLATFORMATOR_IS_TOP_LEVEL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(platformator_default_editor ON)
endif()
option(PLATFORMATOR_BUILD_EDITOR "Build the Dear ImGui desktop editor." ${platformator_default_editor})
option(PLATFORMATOR_BUILD_EDITOR_TESTS "Build editor correctness tests." ${PLATFORMATOR_BUILD_TESTS})
option(PLATFORMATOR_BUILD_EDITOR_PROBES "Build editor performance/GUI probes." OFF)
option(PLATFORMATOR_BUILD_EXAMPLES "Build standalone game examples." OFF)
option(PLATFORMATOR_ENABLE_EDITOR_SESSION "Compile the opt-in editor play-session protocol." ${PLATFORMATOR_BUILD_EDITOR})

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(cmake/PlatformatorSanitizers.cmake)
# Only engine tests belong to an ordinary Platformator CTest run.
foreach(dependency_test_option TBB_TEST TBB_EXAMPLES EIGEN_BUILD_TESTING
        SDL_TESTS SDL_TEST_LIBRARY SDL_EXAMPLES SDLMIXER_TESTS SDLMIXER_EXAMPLES SDLIMAGE_TESTS)
    set(${dependency_test_option} OFF CACHE BOOL "Disabled in Platformator builds" FORCE)
endforeach()
# CMake leaves old CTest files behind when vendor testing is switched off in an
# existing build tree. Its parent traversal would still discover those tests.
file(GLOB_RECURSE platformator_stale_vendor_tests
    "${CMAKE_CURRENT_BINARY_DIR}/vendored/*/CTestTestfile.cmake")
if(platformator_stale_vendor_tests)
    file(REMOVE ${platformator_stale_vendor_tests})
endif()

# Use dependency-owned export/install rules for offline SDK consumption.
foreach(install_option SDL_INSTALL SDLIMAGE_INSTALL SDLMIXER_INSTALL SDLTTF_INSTALL TBB_INSTALL EIGEN_BUILD_CMAKE_PACKAGE)
    set(${install_option} ${PLATFORMATOR_INSTALL} CACHE BOOL "Follow Platformator SDK installation" FORCE)
endforeach()
foreach(relocatable_option SDL_RELOCATABLE SDLIMAGE_RELOCATABLE SDLMIXER_RELOCATABLE SDLTTF_RELOCATABLE)
    set(${relocatable_option} ON CACHE BOOL "Build relocatable bundled dependencies" FORCE)
endforeach()
set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF)
set(platformator_dependency_exclusion)
if(NOT PLATFORMATOR_INSTALL)
    set(platformator_dependency_exclusion EXCLUDE_FROM_ALL)
endif()
add_subdirectory(vendored/SDL3-3.4.18 ${platformator_dependency_exclusion})
add_subdirectory(vendored/SDL3_mixer-3.2.4 ${platformator_dependency_exclusion})
add_subdirectory(vendored/SDL3_image-3.4.8 ${platformator_dependency_exclusion})
add_subdirectory(vendored/SDL3_ttf-3.2.2 ${platformator_dependency_exclusion})
add_subdirectory(vendored/eigen-5.0.1 ${platformator_dependency_exclusion})
add_subdirectory(vendored/oneTBB-2022.3.0 ${platformator_dependency_exclusion})

add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/json>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

file(GLOB_RECURSE PLATFORMATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(NOT PLATFORMATOR_ENABLE_BENCHMARKS)
    list(REMOVE_ITEM PLATFORMATOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark.cpp")
endif()
if(NOT PLATFORMATOR_ENABLE_EDITOR_SESSION)
    list(REMOVE_ITEM PLATFORMATOR_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editorprotocol.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editorsession.cpp")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/buildconfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/buildconfig.h"
    @ONLY
)

include(cmake/PlatformatorBenchmarkConfig.cmake)

set(PLATFORMATOR_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
set(PLATFORMATOR_BENCHMARK_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark_main.cpp")
list(REMOVE_ITEM PLATFORMATOR_SOURCES "${PLATFORMATOR_MAIN_SOURCE}" "${PLATFORMATOR_BENCHMARK_MAIN_SOURCE}")

add_library(platformator_engine STATIC ${PLATFORMATOR_SOURCES})
add_library(Platformator::platformator ALIAS platformator_engine)
target_compile_features(platformator_engine PUBLIC cxx_std_20)
set_target_properties(platformator_engine PROPERTIES EXPORT_NAME platformator)
set_target_properties(nlohmann_json PROPERTIES EXPORT_NAME json)
target_compile_definitions(platformator_engine PUBLIC
    PLATFORMATOR_ENABLE_WORK_PROBES=$<BOOL:${PLATFORMATOR_ENABLE_WORK_PROBES}>
    PLATFORMATOR_ENABLE_EDITOR_SESSION=$<BOOL:${PLATFORMATOR_ENABLE_EDITOR_SESSION}>)

set_target_properties(platformator_engine PROPERTIES
    OUTPUT_NAME platformator
)

target_include_directories(platformator_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(platformator_engine PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)

target_link_libraries(platformator_engine PUBLIC
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    Eigen3::Eigen
    TBB::tbb
    nlohmann_json::nlohmann_json
)

if(APPLE)
    set(PLATFORMATOR_INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
    set(PLATFORMATOR_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

if(PLATFORMATOR_BUILD_EDITOR)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "PLATFORMATOR_BUILD_EDITOR is currently supported only on Linux.")
    endif()
    add_subdirectory(editor)
endif()

if(PLATFORMATOR_BUILD_RUNNER)
    add_executable(main "${PLATFORMATOR_MAIN_SOURCE}")
    target_link_libraries(main PRIVATE Platformator::platformator)

    if(DEFINED PLATFORMATOR_INSTALL_RPATH)
        set_target_properties(main PROPERTIES
            INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
        )
    endif()

endif()


set(PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
foreach(platformator_runtime_target SDL3-shared SDL3_image-shared SDL3_mixer-shared SDL3_ttf-shared tbb)
    if(TARGET ${platformator_runtime_target})
        list(APPEND PLATFORMATOR_RUNTIME_LIBRARY_TARGETS ${platformator_runtime_target})
    endif()
endforeach()

if(PLATFORMATOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(PLATFORMATOR_BUILD_EDITOR AND PLATFORMATOR_BUILD_EDITOR_TESTS)
    enable_testing()
    add_subdirectory(editor/tests)
endif()
if(PLATFORMATOR_ENABLE_BENCHMARKS)
    add_subdirectory(benchmark)
endif()
if(PLATFORMATOR_BUILD_EXAMPLES)
    add_subdirectory(examples/mario)
    add_subdirectory(examples/crystal_citadel)
endif()

if(PLATFORMATOR_INSTALL)
    install(TARGETS platformator_engine nlohmann_json EXPORT PlatformatorTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT PlatformatorTargets NAMESPACE Platformator::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Platformator")
    if(PLATFORMATOR_BUILD_RUNNER)
        install(TARGETS main RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(PLATFORMATOR_BUILD_EDITOR AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(cmake/PlatformatorEditorInstall.cmake)
        platformator_install_editor()
    endif()
    # A shared dependency may depend on another library in this same directory.
    foreach(runtime_target IN LISTS PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
        if(UNIX AND NOT APPLE)
            set_target_properties(${runtime_target} PROPERTIES INSTALL_RPATH "$ORIGIN")
        elseif(APPLE)
            set_target_properties(${runtime_target} PROPERTIES INSTALL_RPATH "@loader_path")
        endif()
    endforeach()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/platformator/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/platformator" FILES_MATCHING PATTERN "*.h")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/vendored/json/json.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    set(PLATFORMATOR_PACKAGE_CONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Platformator")
    configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformatorConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfig.cmake" INSTALL_DESTINATION "${PLATFORMATOR_PACKAGE_CONFIG_DIR}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfigVersion.cmake"
        DESTINATION "${PLATFORMATOR_PACKAGE_CONFIG_DIR}")
endif()
