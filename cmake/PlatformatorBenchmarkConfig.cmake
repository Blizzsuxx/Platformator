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
        "${PLATFORMATOR_ROOT_DIR}/cmake/*")
    list(APPEND platformator_fingerprint_inputs "${PLATFORMATOR_ROOT_DIR}/CMakeLists.txt"
        "${PLATFORMATOR_ROOT_DIR}/CMakePresets.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt"
        "${PLATFORMATOR_ROOT_DIR}/vendored/SDL3_mixer-3.2.4/src/decoder_wav.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/linear_render_probe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/capture_linear_render.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/capture_budget.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/sweep_scaling.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/transform_setter_probe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/capture_transform_setter.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/tests/test_linear_render.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_culling_fixture.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_work_probe.h")
    file(GLOB_RECURSE platformator_loaded_asset_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/assets/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/assets/*")
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
            file(RELATIVE_PATH source_name "${PLATFORMATOR_ROOT_DIR}" "${source_file}")
            string(APPEND platformator_source_hashes "${source_name}:${source_hash}\n")
            file(APPEND "${platformator_source_hash_manifest}" "${source_name}:${source_hash}\n")
        endif()
    endforeach()
    string(SHA256 PLATFORMATOR_BENCH_SOURCE_SHA256 "${platformator_source_hashes}")
    file(APPEND "${platformator_source_hash_manifest}"
        "aggregate:${PLATFORMATOR_BENCH_SOURCE_SHA256}\n")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_culling_fixture.h" PLATFORMATOR_LINEAR_FIXTURE_SHA256)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/linear_render_probe.cpp" platformator_probe_sha)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/capture_linear_render.py" platformator_capture_driver_sha)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_work_probe.h" platformator_work_probe_sha)
    string(SHA256 PLATFORMATOR_LINEAR_HARNESS_SHA256
        "fixture:${PLATFORMATOR_LINEAR_FIXTURE_SHA256}\nprobe:${platformator_probe_sha}\ndriver:${platformator_capture_driver_sha}\nwork:${platformator_work_probe_sha}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${platformator_fingerprint_inputs})
    string(TOUPPER "${CMAKE_BUILD_TYPE}" platformator_config_upper)
    set(PLATFORMATOR_BENCH_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${platformator_config_upper}}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark/benchmarkbuildconfig.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/benchmarkbuildconfig.h" @ONLY)
endif()

