cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED PLATFORMATOR_ROOT_DIR)
    project(PlatformatorEngine LANGUAGES C CXX)
    option(PLATFORMATOR_BUILD_EDITOR "Build editor alongside the standalone engine" OFF)
    set(engine_default_install OFF)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(engine_default_install ON)
    endif()
    option(PLATFORMATOR_INSTALL "Install engine SDK" ${engine_default_install})
    if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    add_subdirectory(.. platformator)
    return()
endif()

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

include("${PLATFORMATOR_ROOT_DIR}/cmake/PlatformatorBenchmarkConfig.cmake")

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

if(PLATFORMATOR_BUILD_RUNNER)
    add_executable(main EXCLUDE_FROM_ALL "${PLATFORMATOR_MAIN_SOURCE}")
    target_link_libraries(main PRIVATE Platformator::platformator)

    if(DEFINED PLATFORMATOR_INSTALL_RPATH)
        set_target_properties(main PROPERTIES
            INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
        )
    endif()

endif()


if(PLATFORMATOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(PLATFORMATOR_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(PLATFORMATOR_INSTALL)
    install(TARGETS platformator_engine nlohmann_json EXPORT PlatformatorTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT PlatformatorTargets NAMESPACE Platformator::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Platformator")
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
    install(FILES "${PLATFORMATOR_ROOT_DIR}/vendored/json/json.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    set(PLATFORMATOR_PACKAGE_CONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Platformator")
    configure_package_config_file("${PLATFORMATOR_ROOT_DIR}/cmake/PlatformatorConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfig.cmake" INSTALL_DESTINATION "${PLATFORMATOR_PACKAGE_CONFIG_DIR}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/PlatformatorConfigVersion.cmake"
        DESTINATION "${PLATFORMATOR_PACKAGE_CONFIG_DIR}")
endif()
