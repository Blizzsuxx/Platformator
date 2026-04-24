cmake_minimum_required(VERSION 3.16)

project(Platformator LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

file(GLOB_RECURSE PLATFORMATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

set(PLATFORMATOR_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM PLATFORMATOR_SOURCES "${PLATFORMATOR_MAIN_SOURCE}")

add_library(platformator_engine STATIC ${PLATFORMATOR_SOURCES})

target_include_directories(platformator_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(platformator_engine PUBLIC
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    Eigen3::Eigen
    TBB::tbb
)

add_executable(main "${PLATFORMATOR_MAIN_SOURCE}")
target_link_libraries(main PRIVATE platformator_engine)

add_executable(mario_example
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_camera_rig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_coin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_game.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_goal_flag.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_patrol_enemy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_player.cpp"
)
target_link_libraries(mario_example PRIVATE platformator_engine)

enable_testing()

add_executable(platformator_regression_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression_tests.cpp"
)

target_link_libraries(platformator_regression_tests PRIVATE platformator_engine)

add_test(
    NAME platformator_regression_tests
    COMMAND platformator_regression_tests
)
