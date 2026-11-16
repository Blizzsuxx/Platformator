cmake_minimum_required(VERSION 3.20)
project(PlatformatorBaseline LANGUAGES C CXX)

get_filename_component(PLATFORMATOR_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PLATFORMATOR_BUILD_BENCHMARKS ON CACHE BOOL "" FORCE)
set(PLATFORMATOR_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(PLATFORMATOR_INSTALL OFF CACHE BOOL "" FORCE)
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${PLATFORMATOR_SOURCE}" engine)

get_target_property(baseline_engine_binary_dir platformator_engine BINARY_DIR)

add_executable(platformator_baseline_capture capture.cpp)
target_include_directories(platformator_baseline_capture PRIVATE
    "${PLATFORMATOR_SOURCE}/engine/src" "${baseline_engine_binary_dir}/generated")
target_link_libraries(platformator_baseline_capture PRIVATE Platformator::platformator)
