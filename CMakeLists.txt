cmake_minimum_required(VERSION 3.16)

project(Platformator VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PLATFORMATOR_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PLATFORMATOR_IS_TOP_LEVEL ON)
endif()

option(PLATFORMATOR_ENABLE_DEBUG_TOOLS "Enable debug-only logging, overlays, and frame advance controls." ON)
option(PLATFORMATOR_BUILD_RUNNER "Build the stock Platformator desktop runner." ${PLATFORMATOR_IS_TOP_LEVEL})
option(PLATFORMATOR_BUILD_EXAMPLES "Build standalone Platformator example projects." ${PLATFORMATOR_IS_TOP_LEVEL})
option(PLATFORMATOR_BUILD_TESTS "Build Platformator test executables." ${PLATFORMATOR_IS_TOP_LEVEL})
option(PLATFORMATOR_INSTALL "Install Platformator library headers, assets, and package files." ${PLATFORMATOR_IS_TOP_LEVEL})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# find_package(PkgConfig REQUIRED)

# pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
# pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
# pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
# pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
# pkg_check_modules(EIGEN3 REQUIRED IMPORTED_TARGET eigen3)

add_subdirectory(vendored/SDL3-3.4.2 EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL3_mixer-3.2.0 EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL3_image-3.4.0 EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL3_ttf-3.1.0 EXCLUDE_FROM_ALL)
add_subdirectory(vendored/eigen-5.0.1 EXCLUDE_FROM_ALL)
add_subdirectory(vendored/oneTBB-2022.3.0 EXCLUDE_FROM_ALL)

add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/json"
)

file(GLOB_RECURSE PLATFORMATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/buildconfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/buildconfig.h"
    @ONLY
)

file(GLOB_RECURSE PLATFORMATOR_HEADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

foreach(platformator_header IN LISTS PLATFORMATOR_HEADER_SOURCES)
    file(RELATIVE_PATH platformator_header_relative "${CMAKE_CURRENT_SOURCE_DIR}/src" "${platformator_header}")
    get_filename_component(platformator_header_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/platformator/detail/${platformator_header_relative}" DIRECTORY)
    file(RELATIVE_PATH platformator_header_include_path "${platformator_header_output_dir}" "${platformator_header}")
    file(MAKE_DIRECTORY "${platformator_header_output_dir}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/platformator/detail/${platformator_header_relative}"
        "#pragma once\n\n#include \"${platformator_header_include_path}\"\n"
    )
endforeach()

set(PLATFORMATOR_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM PLATFORMATOR_SOURCES "${PLATFORMATOR_MAIN_SOURCE}")

add_library(platformator_engine STATIC ${PLATFORMATOR_SOURCES})
add_library(Platformator::platformator ALIAS platformator_engine)

set_target_properties(platformator_engine PROPERTIES
    OUTPUT_NAME platformator
)

target_include_directories(platformator_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(platformator_engine PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
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
    set(PLATFORMATOR_INSTALL_RPATH "$ORIGIN/../lib")
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

if(PLATFORMATOR_BUILD_EXAMPLES)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/mario")
endif()

if(PLATFORMATOR_BUILD_TESTS)
    enable_testing()

    add_executable(platformator_regression_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression_tests.cpp"
    )
    target_link_libraries(platformator_regression_tests PRIVATE Platformator::platformator)

    add_executable(platformator_scene_script_roundtrip_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_script_roundtrip_test.cpp"
    )
    target_link_libraries(platformator_scene_script_roundtrip_test PRIVATE Platformator::platformator)

    add_executable(platformator_queued_deletion_safety_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/queued_deletion_safety_test.cpp"
    )
    target_link_libraries(platformator_queued_deletion_safety_test PRIVATE Platformator::platformator)

    add_executable(platformator_runtime_recreation_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_recreation_test.cpp"
    )
    target_link_libraries(platformator_runtime_recreation_test PRIVATE Platformator::platformator)

    foreach(platformator_test_target
        platformator_regression_tests
        platformator_scene_script_roundtrip_test
        platformator_queued_deletion_safety_test
        platformator_runtime_recreation_test
    )
        target_include_directories(${platformator_test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
    endforeach()

    add_test(
        NAME platformator_regression_tests
        COMMAND platformator_regression_tests
    )

    add_test(
        NAME platformator_scene_script_roundtrip_test
        COMMAND platformator_scene_script_roundtrip_test
    )

    add_test(
        NAME platformator_queued_deletion_safety_test
        COMMAND platformator_queued_deletion_safety_test
    )

    add_test(
        NAME platformator_runtime_recreation_test
        COMMAND platformator_runtime_recreation_test
    )
endif()

if(PLATFORMATOR_INSTALL)
    install(TARGETS platformator_engine
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(PLATFORMATOR_BUILD_RUNNER)
        install(TARGETS main
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
        install(TARGETS ${PLATFORMATOR_RUNTIME_LIBRARY_TARGETS}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION assets
        PATTERN "test_runtime" EXCLUDE
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/platformator/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/platformator"
        FILES_MATCHING PATTERN "*.h"
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/platformator/detail"
        FILES_MATCHING PATTERN "*.h"
    )

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/buildconfig.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/platformator/detail"
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/SDL3-3.4.2/include/SDL3"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/SDL3_image-3.4.0/include/SDL3_image"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/SDL3_mixer-3.2.0/include/SDL3_mixer"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/SDL3_ttf-3.1.0/include/SDL3_ttf"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/eigen-5.0.1/Eigen"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendored/eigen-5.0.1/unsupported/Eigen")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/eigen-5.0.1/unsupported/Eigen"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/unsupported"
        )
    endif()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendored/oneTBB-2022.3.0/include/oneapi"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/vendored/json/json.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    set(PLATFORMATOR_PACKAGE_CONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Platformator")
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformatorConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfig.cmake"
        INSTALL_DESTINATION "${PLATFORMATOR_PACKAGE_CONFIG_DIR}"
        NO_SET_AND_CHECK_MACRO
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfigVersion.cmake"
        DESTINATION "${PLATFORMATOR_PACKAGE_CONFIG_DIR}"
    )
endif()
