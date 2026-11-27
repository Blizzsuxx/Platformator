cmake_minimum_required(VERSION 3.22)
project(Box2DUpstreamReference LANGUAGES C CXX)

find_package(Git REQUIRED)
set(BOX2D_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../build/box2d-v3.2.0-src" CACHE PATH
    "Path to the Box2D v3.2.0 git checkout")
set(box2d_commit 178495ba35695aefe9c321e569d2277f06db3b90)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${BOX2D_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE checkout_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT checkout_commit STREQUAL box2d_commit)
    message(FATAL_ERROR "BOX2D_SOURCE_DIR must point to a git checkout of Box2D v3.2.0 (${box2d_commit})")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${BOX2D_SOURCE_DIR}" diff --quiet HEAD
    RESULT_VARIABLE checkout_changed)
if(NOT checkout_changed EQUAL 0)
    message(FATAL_ERROR "The Box2D checkout has tracked modifications")
endif()

set(BOX2D_VALIDATE OFF)
set(BOX2D_PROFILE OFF)
set(BOX2D_DISABLE_SIMD OFF)
set(BOX2D_DOUBLE_PRECISION OFF)
set(BOX2D_SANITIZE OFF)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${BOX2D_SOURCE_DIR}" box2d EXCLUDE_FROM_ALL)
add_subdirectory("${BOX2D_SOURCE_DIR}/shared" upstream-shared EXCLUDE_FROM_ALL)
add_subdirectory("${BOX2D_SOURCE_DIR}/benchmark" upstream-benchmark EXCLUDE_FROM_ALL)

add_executable(box2d_upstream_reference_runner box2d_runner.cpp)
target_compile_features(box2d_upstream_reference_runner PRIVATE cxx_std_20)
target_include_directories(box2d_upstream_reference_runner PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../../vendored/json")
target_compile_definitions(box2d_upstream_reference_runner PRIVATE
    BOX2D_REFERENCE_COMMIT="${box2d_commit}"
    BOX2D_REFERENCE_CONFIGURATION="$<CONFIG>"
    BOX2D_REFERENCE_COMPILER="${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
target_link_libraries(box2d_upstream_reference_runner PRIVATE box2d shared)
