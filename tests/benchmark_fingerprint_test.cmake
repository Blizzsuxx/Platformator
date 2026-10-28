if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED DISPOSABLE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT and DISPOSABLE_ROOT are required")
endif()
include("${SOURCE_ROOT}/cmake/PlatformatorBenchmarkFingerprint.cmake")

file(REMOVE_RECURSE "${DISPOSABLE_ROOT}")
file(MAKE_DIRECTORY
    "${DISPOSABLE_ROOT}/src"
    "${DISPOSABLE_ROOT}/include/platformator"
    "${DISPOSABLE_ROOT}/cmake"
    "${DISPOSABLE_ROOT}/benchmark"
    "${DISPOSABLE_ROOT}/tests")
file(WRITE "${DISPOSABLE_ROOT}/CMakeLists.txt" "project(fingerprint-fixture)\n")
file(WRITE "${DISPOSABLE_ROOT}/src/runtime.cpp" "int runtime_fixture;\n")
file(WRITE "${DISPOSABLE_ROOT}/include/platformator/runtime.h" "int runtime_header_fixture;\n")
file(WRITE "${DISPOSABLE_ROOT}/benchmark/spatial_fixtures.cpp" "int spatial_fixture;\n")
file(WRITE "${DISPOSABLE_ROOT}/benchmark/spatial_fixtures.h" "int spatial_fixture_header;\n")
file(WRITE "${DISPOSABLE_ROOT}/benchmark/spatial_resource_hooks.cpp" "int resource_hook_fixture;\n")

platformator_compute_benchmark_source_fingerprint("${DISPOSABLE_ROOT}" first inputs)
foreach(required IN ITEMS "${DISPOSABLE_ROOT}/benchmark/spatial_fixtures.cpp"
                          "${DISPOSABLE_ROOT}/benchmark/spatial_fixtures.h"
                          "${DISPOSABLE_ROOT}/benchmark/spatial_resource_hooks.cpp")
    list(FIND inputs "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Compiled benchmark fingerprint omitted ${required}")
    endif()
endforeach()

file(WRITE "${DISPOSABLE_ROOT}/benchmark/spatial_fixtures.cpp" "int changed_fixture;\n")
platformator_compute_benchmark_source_fingerprint("${DISPOSABLE_ROOT}" after_fixture inputs)
if(first STREQUAL after_fixture)
    message(FATAL_ERROR "Benchmark fixture edits must change the source fingerprint")
endif()

file(WRITE "${DISPOSABLE_ROOT}/benchmark/spatial_fixtures.h" "int changed_fixture_header;\n")
platformator_compute_benchmark_source_fingerprint("${DISPOSABLE_ROOT}" after_header inputs)
if(after_fixture STREQUAL after_header)
    message(FATAL_ERROR "Benchmark fixture header edits must change the source fingerprint")
endif()

file(WRITE "${DISPOSABLE_ROOT}/benchmark/spatial_resource_hooks.cpp" "int changed_resource_hook;\n")
platformator_compute_benchmark_source_fingerprint("${DISPOSABLE_ROOT}" after_hook inputs)
if(after_header STREQUAL after_hook)
    message(FATAL_ERROR "Resource hook edits must change the source fingerprint")
endif()
