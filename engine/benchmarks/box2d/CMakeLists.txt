cmake_minimum_required(VERSION 3.22)
project(PlatformatorBox2DBenchmarks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(CheckIPOSupported)

find_package(TBB REQUIRED)

set(BOX2D_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../build/box2d-deps" CACHE PATH "Optional local cache for Box2D source trees")

# --- Box2D v2.4.2 (classic C++ AoS, single-threaded — Unity Physics2D baseline) ---
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(EXISTS "${BOX2D_DEPS_DIR}/box2d-2.4.2/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_BOX2D_V2 "${BOX2D_DEPS_DIR}/box2d-2.4.2")
endif()
FetchContent_Declare(
    box2d_v2
    URL https://github.com/erincatto/box2d/archive/refs/tags/v2.4.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(box2d_v2)
if(NOT box2d_v2_POPULATED)
    FetchContent_Populate(box2d_v2)
endif()

# Build Box2D v2.4.2 directly as a named static target `box2d_v2` so its target
# name never collides with Box2D v3.1.1's `box2d` target.
file(GLOB_RECURSE BOX2D_V2_SOURCES CONFIGURE_DEPENDS "${box2d_v2_SOURCE_DIR}/src/*.cpp")
add_library(box2d_v2 STATIC ${BOX2D_V2_SOURCES})
target_include_directories(box2d_v2
    PUBLIC "${box2d_v2_SOURCE_DIR}/include"
    PRIVATE "${box2d_v2_SOURCE_DIR}/src"
)

add_executable(box2d_v2_benchmark_runner bench_box2d_v2.cpp)
target_link_libraries(box2d_v2_benchmark_runner PRIVATE box2d_v2)

# --- Box2D v3.1.1 (modern C11 SoA + SIMD + graph-colored multithreaded solver) ---
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
set(BOX2D_DISABLE_SIMD OFF CACHE BOOL "" FORCE)
option(PLATFORMATOR_BOX2D_V3_AVX2 "Enable AVX2 in Box2D v3.1.1 (default OFF matches baseline x86-64 SSE2)" OFF)
set(BOX2D_AVX2 ${PLATFORMATOR_BOX2D_V3_AVX2} CACHE BOOL "" FORCE)

if(EXISTS "${BOX2D_DEPS_DIR}/box2d-3.1.1/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_BOX2D_V3 "${BOX2D_DEPS_DIR}/box2d-3.1.1")
endif()
FetchContent_Declare(
    box2d_v3
    URL https://github.com/erincatto/box2d/archive/refs/tags/v3.1.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(box2d_v3)

add_executable(box2d_v3_benchmark_runner bench_box2d_v3.cpp)
target_link_libraries(box2d_v3_benchmark_runner PRIVATE box2d TBB::tbb)

# Match Platformator's Release IPO/LTO setting when supported.
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_property(TARGET box2d_v2 box2d_v2_benchmark_runner box2d box2d_v3_benchmark_runner
        PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
