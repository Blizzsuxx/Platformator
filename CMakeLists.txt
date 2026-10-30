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
option(PLATFORMATOR_ENABLE_BENCHMARKS "Enable compile-time benchmark instrumentation and runtime summaries." OFF)
option(PLATFORMATOR_ENABLE_WORK_PROBES "Enable test-only affected-work counters (never for performance runs)." OFF)
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

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/buildconfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/buildconfig.h"
    @ONLY
)

if(PLATFORMATOR_ENABLE_BENCHMARKS)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE PLATFORMATOR_BENCH_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE platformator_git_changes ERROR_QUIET)
    set(PLATFORMATOR_BENCH_DIRTY 0)
    if(platformator_git_changes)
        set(PLATFORMATOR_BENCH_DIRTY 1)
    endif()
    file(GLOB_RECURSE platformator_fingerprint_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*" "${CMAKE_CURRENT_SOURCE_DIR}/include/platformator/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*")
    list(APPEND platformator_fingerprint_inputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendored/SDL3_mixer-3.2.4/src/decoder_wav.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/linear_render_probe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/capture_linear_render.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/sweep_scaling.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/tests/test_linear_render.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_culling_fixture.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_work_probe.h")
    file(GLOB_RECURSE platformator_loaded_asset_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
    list(APPEND platformator_fingerprint_inputs ${platformator_loaded_asset_inputs})
    set(platformator_unique_fingerprint_inputs "")
    foreach(source_file IN LISTS platformator_fingerprint_inputs)
        list(FIND platformator_unique_fingerprint_inputs "${source_file}" duplicate_index)
        if(NOT duplicate_index EQUAL -1)
            message(FATAL_ERROR "Duplicate benchmark fingerprint input: ${source_file}")
        endif()
        list(APPEND platformator_unique_fingerprint_inputs "${source_file}")
    endforeach()
    set(platformator_fingerprint_inputs "${platformator_unique_fingerprint_inputs}")
    list(SORT platformator_fingerprint_inputs)
    set(platformator_source_hashes "")
    set(platformator_source_hash_manifest "${CMAKE_CURRENT_BINARY_DIR}/generated/platformator-source-hashes.manifest")
    file(WRITE "${platformator_source_hash_manifest}" "")
    foreach(source_file IN LISTS platformator_fingerprint_inputs)
        if(NOT IS_DIRECTORY "${source_file}")
            file(SHA256 "${source_file}" source_hash)
            file(RELATIVE_PATH source_name "${CMAKE_CURRENT_SOURCE_DIR}" "${source_file}")
            string(APPEND platformator_source_hashes "${source_name}:${source_hash}\n")
            file(APPEND "${platformator_source_hash_manifest}" "${source_name}:${source_hash}\n")
        endif()
    endforeach()
    string(SHA256 PLATFORMATOR_BENCH_SOURCE_SHA256 "${platformator_source_hashes}")
    file(APPEND "${platformator_source_hash_manifest}"
        "aggregate:${PLATFORMATOR_BENCH_SOURCE_SHA256}\n")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_culling_fixture.h" PLATFORMATOR_LINEAR_FIXTURE_SHA256)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/linear_render_probe.cpp" platformator_probe_sha)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/capture_linear_render.py" platformator_capture_driver_sha)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_work_probe.h" platformator_work_probe_sha)
    string(SHA256 PLATFORMATOR_LINEAR_HARNESS_SHA256
        "fixture:${PLATFORMATOR_LINEAR_FIXTURE_SHA256}\nprobe:${platformator_probe_sha}\ndriver:${platformator_capture_driver_sha}\nwork:${platformator_work_probe_sha}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${platformator_fingerprint_inputs})
    string(TOUPPER "${CMAKE_BUILD_TYPE}" platformator_config_upper)
    set(PLATFORMATOR_BENCH_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${platformator_config_upper}}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/benchmarkbuildconfig.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/benchmarkbuildconfig.h" @ONLY)
endif()

set(PLATFORMATOR_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
set(PLATFORMATOR_BENCHMARK_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark_main.cpp")
list(REMOVE_ITEM PLATFORMATOR_SOURCES "${PLATFORMATOR_MAIN_SOURCE}" "${PLATFORMATOR_BENCHMARK_MAIN_SOURCE}")

add_library(platformator_engine STATIC ${PLATFORMATOR_SOURCES})
add_library(Platformator::platformator ALIAS platformator_engine)
target_compile_features(platformator_engine PUBLIC cxx_std_20)
set_target_properties(platformator_engine PROPERTIES EXPORT_NAME platformator)
set_target_properties(nlohmann_json PROPERTIES EXPORT_NAME json)
target_compile_definitions(platformator_engine PUBLIC PLATFORMATOR_ENABLE_WORK_PROBES=$<BOOL:${PLATFORMATOR_ENABLE_WORK_PROBES}>)

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

if(PLATFORMATOR_BUILD_RUNNER)
    add_executable(main "${PLATFORMATOR_MAIN_SOURCE}")
    target_link_libraries(main PRIVATE Platformator::platformator)

    if(DEFINED PLATFORMATOR_INSTALL_RPATH)
        set_target_properties(main PROPERTIES
            INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
        )
    endif()

    if(PLATFORMATOR_ENABLE_BENCHMARKS)
        add_executable(platformator_benchmark_runner "${PLATFORMATOR_BENCHMARK_MAIN_SOURCE}")
        target_link_libraries(platformator_benchmark_runner PRIVATE Platformator::platformator)
        target_include_directories(platformator_benchmark_runner PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
        )

        if(DEFINED PLATFORMATOR_INSTALL_RPATH)
            set_target_properties(platformator_benchmark_runner PROPERTIES
                INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
            )
        endif()
    endif()
endif()

if(PLATFORMATOR_ENABLE_BENCHMARKS)
    add_executable(platformator_linear_render_probe benchmark/linear_render_probe.cpp)
    target_link_libraries(platformator_linear_render_probe PRIVATE Platformator::platformator)
    target_include_directories(platformator_linear_render_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
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

# Source runners and core-only tests have deliberate engine resources.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets" PATTERN "test_runtime" EXCLUDE)
if(PLATFORMATOR_BUILD_TESTS)
    # The roundtrip audio fixture is independent of building Mario.
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/assets/audio/jump.wav" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets/audio")
    enable_testing()
    include(cmake/PlatformatorTests.cmake)
    platformator_add_test(platformator_regression_tests tests/regression_tests.cpp INTERNAL LABELS physics)
    platformator_add_test(platformator_scene_script_roundtrip_test tests/scene_script_roundtrip_test.cpp INTERNAL LABELS scene)
    platformator_add_test(platformator_queued_deletion_safety_test tests/queued_deletion_safety_test.cpp INTERNAL LABELS lifecycle physics)
    platformator_add_test(platformator_runtime_recreation_test tests/runtime_recreation_test.cpp INTERNAL LABELS lifecycle)
    platformator_add_test(platformator_collider_pair_cleanup_test tests/collider_pair_cleanup_test.cpp INTERNAL LABELS physics)
    platformator_add_test(platformator_runtime_foundation_test tests/runtime_foundation_test.cpp INTERNAL LABELS lifecycle reference)
    platformator_add_test(platformator_scene_transition_contract_test tests/scene_transition_contract_test.cpp LABELS lifecycle scene)
    platformator_add_test(platformator_scene_transition_stress_test tests/scene_transition_stress_test.cpp LABELS lifecycle scene)
    platformator_add_test(platformator_callback_mutation_test tests/callback_mutation_test.cpp LABELS lifecycle mutation)
    platformator_add_test(platformator_destruction_contract_test tests/destruction_contract_test.cpp LABELS lifecycle mutation)
    platformator_add_test(platformator_object_reference_contract_test tests/object_reference_contract_test.cpp LABELS scene reference)
    platformator_add_test(platformator_scene_io_contract_test tests/scene_io_contract_test.cpp LABELS scene reference)
    platformator_add_test(platformator_transform_hierarchy_test tests/transform_hierarchy_test.cpp LABELS physics transform)
    platformator_add_test(platformator_physics_support_test tests/physics_support_test.cpp LABELS physics determinism)
    platformator_add_test(platformator_contact_events_test tests/contact_events_test.cpp INTERNAL LABELS physics determinism)
    platformator_add_test(platformator_physics_bookkeeping_test tests/physics_bookkeeping_test.cpp INTERNAL LABELS physics determinism)
    platformator_add_test(platformator_membership_exception_test tests/membership_exception_test.cpp INTERNAL LABELS physics)
    platformator_add_test(platformator_asset_alias_exception_test tests/asset_alias_exception_test.cpp INTERNAL LABELS asset)
    platformator_add_test(platformator_asset_runtime_exception_test tests/asset_runtime_exception_test.cpp INTERNAL LABELS asset scene)
    platformator_add_test(platformator_physics_geometry_test tests/physics_geometry_test.cpp LABELS physics geometry)
    platformator_add_test(platformator_runtime_step_test tests/runtime_step_test.cpp INTERNAL LABELS lifecycle clock)
    platformator_add_test(platformator_runtime_work_test tests/runtime_work_test.cpp INTERNAL LABELS physics work)
    set_tests_properties(platformator_runtime_work_test PROPERTIES SKIP_RETURN_CODE 77)
    platformator_add_test(platformator_render_culling_geometry_test tests/render_culling_geometry_test.cpp INTERNAL LABELS rendering geometry)
    platformator_add_test(platformator_render_culling_test tests/render_culling_test.cpp INTERNAL LABELS rendering)
    set_tests_properties(platformator_render_culling_test PROPERTIES SKIP_RETURN_CODE 77)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/renderculling.cpp")
        platformator_add_test(platformator_render_culling_worker_test tests/render_culling_worker_test.cpp INTERNAL LABELS rendering physics workers)
        set_tests_properties(platformator_render_culling_worker_test PROPERTIES SKIP_RETURN_CODE 77)
        platformator_add_test(platformator_render_culling_trajectory_test tests/render_culling_trajectory_test.cpp INTERNAL LABELS rendering determinism)
        platformator_add_test(platformator_render_culling_contract_test tests/render_culling_contract_test.cpp INTERNAL LABELS rendering package)
        platformator_add_test(platformator_render_culling_index_test tests/render_culling_index_test.cpp INTERNAL LABELS rendering index)
    endif()
    platformator_add_test(platformator_asset_root_contract_test tests/asset_root_contract_test.cpp LABELS package asset)
    platformator_add_test(platformator_runner_failure_test tests/runner_failure_test.cpp LABELS package runner)
    add_test(NAME platformator_runner_startup_failure COMMAND platformator_runner_failure_test startup)
    add_test(NAME platformator_physics_only_startup COMMAND platformator_runner_failure_test physics)
    set_tests_properties(platformator_runner_startup_failure platformator_physics_only_startup PROPERTIES LABELS "platformator;package;runner")
    add_test(NAME platformator_package_smoke COMMAND "${CMAKE_COMMAND}"
        "-DPLATFORMATOR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DPLATFORMATOR_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_smoke.cmake")
    set_tests_properties(platformator_package_smoke PROPERTIES LABELS "platformator;package;install")
    platformator_add_test(platformator_benchmark_contract_test tests/benchmark_contract_test.cpp INTERNAL LABELS benchmark)
    set_tests_properties(platformator_benchmark_contract_test PROPERTIES SKIP_RETURN_CODE 77)
    if(PLATFORMATOR_ENABLE_BENCHMARKS AND PLATFORMATOR_BUILD_RUNNER)
        platformator_add_test(platformator_benchmark_render_culling_cli_test
            tests/benchmark_render_culling_cli_test.cpp INTERNAL LABELS benchmark rendering)
    endif()
    if(PLATFORMATOR_ENABLE_BENCHMARKS AND PLATFORMATOR_ENABLE_WORK_PROBES)
        add_test(NAME platformator_linear_render_allocation_observer_test
            COMMAND platformator_linear_render_probe --allocation-observer-self-test)
        set_tests_properties(platformator_linear_render_allocation_observer_test
            PROPERTIES LABELS "platformator;benchmark;allocation")
    endif()
    if(PLATFORMATOR_ENABLE_BENCHMARKS)
        add_executable(platformator_focused_cleanup_probe benchmark/focused_cleanup_probe.cpp)
        target_link_libraries(platformator_focused_cleanup_probe PRIVATE Platformator::platformator)
        target_include_directories(platformator_focused_cleanup_probe PRIVATE
            "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_BINARY_DIR}/generated")
        add_executable(platformator_runtime_cleanup_probe benchmark/runtime_cleanup_probe.cpp)
        target_link_libraries(platformator_runtime_cleanup_probe PRIVATE Platformator::platformator)
        target_include_directories(platformator_runtime_cleanup_probe PRIVATE
            "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_BINARY_DIR}/generated")
        add_executable(platformator_dead_state_pair_churn_probe benchmark/dead_state_pair_churn_probe.cpp)
        target_link_libraries(platformator_dead_state_pair_churn_probe PRIVATE Platformator::platformator)
        target_include_directories(platformator_dead_state_pair_churn_probe PRIVATE
            "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_BINARY_DIR}/generated")
    endif()
    if(TARGET platformator_benchmark_runner)
        add_test(NAME platformator_benchmark_persistent_zero_warmup
            COMMAND platformator_benchmark_runner --scenario persistent_contacts --contact-pair-count 10
                --scripted-endpoints 0 --workers 1 --warmup-frames 0 --measure-frames 2 --validate-invariants)
        set_tests_properties(platformator_benchmark_persistent_zero_warmup PROPERTIES
            LABELS "platformator;benchmark"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME platformator_benchmark_sweep_test COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/tests" -v)
    set_tests_properties(platformator_benchmark_sweep_test PROPERTIES LABELS "platformator;benchmark")
    include(cmake/PlatformatorPublicHeaders.cmake)
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
    # A shared dependency may depend on another library in this same directory.
    foreach(runtime_target IN LISTS PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
        if(UNIX AND NOT APPLE)
            set_target_properties(${runtime_target} PROPERTIES INSTALL_RPATH "$ORIGIN")
        elseif(APPLE)
            set_target_properties(${runtime_target} PROPERTIES INSTALL_RPATH "@loader_path")
        endif()
    endforeach()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION assets PATTERN "test_runtime" EXCLUDE)
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
