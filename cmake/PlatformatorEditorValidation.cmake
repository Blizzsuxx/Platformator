cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This validation script currently covers Linux only; Windows/macOS lanes require their native verification hosts.")
endif()

if(NOT DEFINED PLATFORMATOR_SOURCE_DIR)
    get_filename_component(PLATFORMATOR_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
endif()
if(NOT DEFINED PLATFORMATOR_VALIDATION_BUILD_DIR)
    set(PLATFORMATOR_VALIDATION_BUILD_DIR "${PLATFORMATOR_SOURCE_DIR}/bin/editor-validation")
endif()
if(NOT DEFINED PLATFORMATOR_VALIDATION_JOBS)
    set(PLATFORMATOR_VALIDATION_JOBS 4)
endif()

get_filename_component(PLATFORMATOR_SOURCE_DIR "${PLATFORMATOR_SOURCE_DIR}" REALPATH)
cmake_path(ABSOLUTE_PATH PLATFORMATOR_VALIDATION_BUILD_DIR NORMALIZE
           OUTPUT_VARIABLE PLATFORMATOR_VALIDATION_BUILD_DIR)
string(FIND "${PLATFORMATOR_VALIDATION_BUILD_DIR}/" "${PLATFORMATOR_SOURCE_DIR}/bin/" build_prefix_index)
if(NOT build_prefix_index EQUAL 0)
    message(FATAL_ERROR "Keep validation builds under ${PLATFORMATOR_SOURCE_DIR}/bin/.")
endif()
if(NOT IS_DIRECTORY "${PLATFORMATOR_SOURCE_DIR}" OR
   NOT EXISTS "${PLATFORMATOR_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PLATFORMATOR_SOURCE_DIR is not a Platformator checkout.")
endif()
if(NOT PLATFORMATOR_VALIDATION_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "PLATFORMATOR_VALIDATION_JOBS must be a positive integer.")
endif()

string(TIMESTAMP validation_stamp "%Y%m%d%H%M%S")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef validation_nonce)
string(APPEND validation_stamp "-${validation_nonce}")
set(stage_dir "${PLATFORMATOR_VALIDATION_BUILD_DIR}/stage-${validation_stamp}")
set(relocated_dir "${PLATFORMATOR_VALIDATION_BUILD_DIR}/relocated-${validation_stamp}")
set(work_dir "${PLATFORMATOR_VALIDATION_BUILD_DIR}/unrelated-working-directory")
file(MAKE_DIRECTORY "${PLATFORMATOR_VALIDATION_BUILD_DIR}" "${work_dir}")

function(run_checked label)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result STREQUAL "0")
        string(LENGTH "${stdout}" stdout_length)
        if(stdout_length GREATER 12000)
            math(EXPR stdout_start "${stdout_length} - 12000")
            string(SUBSTRING "${stdout}" ${stdout_start} -1 stdout)
        endif()
        message(FATAL_ERROR "${label} failed (${result}); final build output follows.\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    message(STATUS "${label}: passed")
endfunction()

run_checked("Linux editor configure"
    "${CMAKE_COMMAND}" -S "${PLATFORMATOR_SOURCE_DIR}" -B "${PLATFORMATOR_VALIDATION_BUILD_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Debug
    "-DCMAKE_INSTALL_PREFIX=${stage_dir}"
    -DCMAKE_INSTALL_LIBDIR=lib
    -DPLATFORMATOR_BUILD_EDITOR=ON
    -DPLATFORMATOR_BUILD_TESTS=ON
    -DPLATFORMATOR_BUILD_EXAMPLES=OFF
    -DPLATFORMATOR_INSTALL=ON
    -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=ON
    -DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON
    -DPLATFORMATOR_ENABLE_BENCHMARKS=OFF
    -DPLATFORMATOR_ENABLE_WORK_PROBES=ON
    -DPLATFORMATOR_EDITOR_WORK_COUNTERS=ON
    -DPLATFORMATOR_SANITIZER=none)
run_checked("Linux editor build"
    "${CMAKE_COMMAND}" --build "${PLATFORMATOR_VALIDATION_BUILD_DIR}"
    --parallel "${PLATFORMATOR_VALIDATION_JOBS}")
find_program(PLATFORMATOR_CTEST_EXECUTABLE ctest REQUIRED)
execute_process(
    COMMAND "${PLATFORMATOR_CTEST_EXECUTABLE}" --test-dir "${PLATFORMATOR_VALIDATION_BUILD_DIR}"
        --output-on-failure
        -R "^platformator_editor_(accessibility|asset_watch_integration|nativefile|nativeprocess|playsession|startup|workspace)_test$"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error)
if(NOT test_result STREQUAL "0")
    message(FATAL_ERROR "Linux focused native/editor suites failed (${test_result}).\n${test_output}\n${test_error}")
endif()
message(STATUS "Linux focused native/editor suites: ${test_output}")
run_checked("Linux editor install"
    "${CMAKE_COMMAND}" --install "${PLATFORMATOR_VALIDATION_BUILD_DIR}" --prefix "${stage_dir}")

set(installed_editor "${stage_dir}/bin/platformator_editor")
if(NOT EXISTS "${installed_editor}")
    message(FATAL_ERROR "The install did not produce ${installed_editor}.")
endif()
file(RENAME "${stage_dir}" "${relocated_dir}" RESULT relocation_result)
if(NOT relocation_result STREQUAL "0")
    message(FATAL_ERROR "Could not relocate the staged editor: ${relocation_result}")
endif()
set(relocated_editor "${relocated_dir}/bin/platformator_editor")
run_checked("Relocated editor launch from unrelated cwd"
    "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
    "${relocated_editor}" --help)

find_program(LINUX_LDD_EXECUTABLE ldd REQUIRED)
execute_process(COMMAND "${LINUX_LDD_EXECUTABLE}" "${relocated_editor}"
    RESULT_VARIABLE ldd_result OUTPUT_VARIABLE ldd_output ERROR_VARIABLE ldd_error)
if(NOT ldd_result STREQUAL "0" OR ldd_output MATCHES "not found")
    message(FATAL_ERROR "Relocated editor dependencies are unresolved.\n${ldd_output}\n${ldd_error}")
endif()
set(runtime_private_library_path "${relocated_dir}/bin/../lib/")
set(normalized_private_library_path "${relocated_dir}/lib/")
string(REPLACE "${runtime_private_library_path}" "${normalized_private_library_path}"
    normalized_ldd_output "${ldd_output}")
string(FIND "${normalized_ldd_output}" "=> ${normalized_private_library_path}libSDL3"
    private_sdl_dependency_index)
string(FIND "${normalized_ldd_output}" "=> ${normalized_private_library_path}libtbb"
    private_tbb_dependency_index)
if(private_sdl_dependency_index EQUAL -1 OR private_tbb_dependency_index EQUAL -1)
    message(FATAL_ERROR "The relocated editor did not resolve its private SDL/oneTBB libraries from its own prefix.\n${ldd_output}")
endif()
string(REPLACE "${PLATFORMATOR_SOURCE_DIR}/bin/" "" source_dependency_check_output "${ldd_output}")
string(FIND "${source_dependency_check_output}" "${PLATFORMATOR_SOURCE_DIR}/"
    source_dependency_index)
string(FIND "${ldd_output}" "${PLATFORMATOR_VALIDATION_BUILD_DIR}/CMakeFiles/" build_dependency_index)
if(NOT source_dependency_index EQUAL -1 OR NOT build_dependency_index EQUAL -1)
    message(FATAL_ERROR "The relocated editor still resolves a dependency from its source/build tree.\n${ldd_output}")
endif()

run_checked("Linux editor-off/session-off SDK consumer validation"
    "${CMAKE_COMMAND}"
    "-DPLATFORMATOR_SOURCE_DIR=${PLATFORMATOR_SOURCE_DIR}"
    "-DPLATFORMATOR_BINARY_DIR=${PLATFORMATOR_VALIDATION_BUILD_DIR}/runtime-consumer-validation"
    -P "${PLATFORMATOR_SOURCE_DIR}/tests/package_smoke.cmake")

message(STATUS "LINUX_EDITOR_INSTALL_VALIDATION: PASS")
message(STATUS "Relocated prefix: ${relocated_dir}")
