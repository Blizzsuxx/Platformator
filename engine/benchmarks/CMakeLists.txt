cmake_minimum_required(VERSION 3.16)
if(NOT TARGET Platformator::platformator)
    project(PlatformatorBenchmarks LANGUAGES C CXX)
    option(BUILD_TESTING "Standalone component build option" OFF)
    option(PLATFORMATOR_BUILD_EDITOR "Standalone component build option" OFF)
    option(PLATFORMATOR_BUILD_BENCHMARKS "Standalone component build option" ON)
    option(PLATFORMATOR_INSTALL "Standalone component build option" OFF)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    enable_testing()
    add_subdirectory(../.. platformator)
    return()
endif()

foreach(probe platformator_benchmark_runner platformator_transform_setter_probe platformator_linear_render_probe)
    if(probe STREQUAL "platformator_benchmark_runner")
        set(source "${PLATFORMATOR_ENGINE_DIR}/src/benchmark/benchmark_main.cpp")
    elseif(probe STREQUAL "platformator_transform_setter_probe")
        set(source transform_setter_probe.cpp)
    else()
        set(source linear_render_probe.cpp)
    endif()
    add_executable(${probe} ${source})
    target_link_libraries(${probe} PRIVATE Platformator::platformator)
    target_include_directories(${probe} PRIVATE "${PLATFORMATOR_ENGINE_DIR}/src" "${PLATFORMATOR_ENGINE_DIR}/tests" "${PLATFORMATOR_ENGINE_BINARY_DIR}/generated")
endforeach()
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets")
set(PLATFORMATOR_BUILD_BENCHMARK_TESTS ON)
if(PLATFORMATOR_BUILD_BENCHMARK_TESTS)
    enable_testing()
    include("${PROJECT_SOURCE_DIR}/cmake/PlatformatorTests.cmake")
    platformator_add_test(platformator_benchmark_contract_test tests/benchmark_contract_test.cpp INTERNAL LABELS benchmark)
    set_tests_properties(platformator_benchmark_contract_test PROPERTIES SKIP_RETURN_CODE 77)
    if(PLATFORMATOR_ENABLE_BENCHMARKS)
        platformator_add_test(platformator_benchmark_render_culling_cli_test
            tests/benchmark_render_culling_cli_test.cpp INTERNAL LABELS benchmark rendering)
    endif()
    if(PLATFORMATOR_ENABLE_BENCHMARKS AND PLATFORMATOR_ENABLE_WORK_PROBES)
        add_test(NAME platformator_linear_render_allocation_observer_test
            COMMAND platformator_linear_render_probe --allocation-observer-self-test)
        set_tests_properties(platformator_linear_render_allocation_observer_test
            PROPERTIES LABELS "platformator;benchmark;allocation")
        add_test(NAME platformator_linear_render_z_oracle_test
            COMMAND platformator_linear_render_probe --z-edit-oracle-self-test)
        set_tests_properties(platformator_linear_render_z_oracle_test
            PROPERTIES LABELS "platformator;benchmark;rendering")
    endif()
    if(PLATFORMATOR_ENABLE_BENCHMARKS)
        add_executable(platformator_focused_cleanup_probe ${CMAKE_CURRENT_SOURCE_DIR}/focused_cleanup_probe.cpp)
        target_link_libraries(platformator_focused_cleanup_probe PRIVATE Platformator::platformator)
        target_include_directories(platformator_focused_cleanup_probe PRIVATE
            "${PLATFORMATOR_ENGINE_DIR}/src" "${PLATFORMATOR_ENGINE_DIR}/tests" "${PLATFORMATOR_ENGINE_BINARY_DIR}/generated")
        add_executable(platformator_runtime_cleanup_probe ${CMAKE_CURRENT_SOURCE_DIR}/runtime_cleanup_probe.cpp)
        target_link_libraries(platformator_runtime_cleanup_probe PRIVATE Platformator::platformator)
        target_include_directories(platformator_runtime_cleanup_probe PRIVATE
            "${PLATFORMATOR_ENGINE_DIR}/src" "${PLATFORMATOR_ENGINE_DIR}/tests" "${PLATFORMATOR_ENGINE_BINARY_DIR}/generated")
        add_executable(platformator_dead_state_pair_churn_probe ${CMAKE_CURRENT_SOURCE_DIR}/dead_state_pair_churn_probe.cpp)
        target_link_libraries(platformator_dead_state_pair_churn_probe PRIVATE Platformator::platformator)
        target_include_directories(platformator_dead_state_pair_churn_probe PRIVATE
            "${PLATFORMATOR_ENGINE_DIR}/src" "${PLATFORMATOR_ENGINE_DIR}/tests" "${PLATFORMATOR_ENGINE_BINARY_DIR}/generated")
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
        -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
    set_tests_properties(platformator_benchmark_sweep_test PROPERTIES LABELS "platformator;benchmark")
endif()
