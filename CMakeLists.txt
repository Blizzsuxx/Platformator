cmake_minimum_required(VERSION 3.16)

project(Platformator LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLATFORMATOR_ENABLE_DEBUG_TOOLS "Enable debug-only logging, overlays, and frame advance controls." ON)

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

add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/json"
)

file(GLOB_RECURSE PLATFORMATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/buildconfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/buildconfig.h"
    @ONLY
)

set(PLATFORMATOR_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM PLATFORMATOR_SOURCES "${PLATFORMATOR_MAIN_SOURCE}")

add_library(platformator_engine STATIC ${PLATFORMATOR_SOURCES})

target_include_directories(platformator_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
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

add_executable(main "${PLATFORMATOR_MAIN_SOURCE}")
target_link_libraries(main PRIVATE platformator_engine)

add_executable(mario_example
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_camera_rig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_coin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_game.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_goal_flag.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_patrol_enemy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/mario_player.cpp"
)
target_link_libraries(mario_example PRIVATE platformator_engine)

if(APPLE)
    set(PLATFORMATOR_INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
    set(PLATFORMATOR_INSTALL_RPATH "$ORIGIN/../lib")
endif()

if(DEFINED PLATFORMATOR_INSTALL_RPATH)
    set_target_properties(main mario_example PROPERTIES
        INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
    )
endif()

set(PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
foreach(platformator_runtime_target SDL3-shared SDL3_image-shared SDL3_mixer-shared SDL3_ttf-shared tbb)
    if(TARGET ${platformator_runtime_target})
        list(APPEND PLATFORMATOR_RUNTIME_LIBRARY_TARGETS ${platformator_runtime_target})
    endif()
endforeach()

install(TARGETS main mario_example
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

if(PLATFORMATOR_RUNTIME_LIBRARY_TARGETS)
    install(TARGETS ${PLATFORMATOR_RUNTIME_LIBRARY_TARGETS}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
    DESTINATION assets
    PATTERN "test_runtime" EXCLUDE
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/examples/mario/level1.scene"
    DESTINATION assets/scenes
    RENAME mario_example.scene
)

enable_testing()

add_executable(platformator_regression_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression_tests.cpp"
)

target_link_libraries(platformator_regression_tests PRIVATE platformator_engine)

add_executable(platformator_scene_script_roundtrip_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_script_roundtrip_test.cpp"
)

target_link_libraries(platformator_scene_script_roundtrip_test PRIVATE platformator_engine)

add_executable(platformator_queued_deletion_safety_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/queued_deletion_safety_test.cpp"
)

target_link_libraries(platformator_queued_deletion_safety_test PRIVATE platformator_engine)

add_test(
    NAME platformator_regression_tests
    COMMAND platformator_regression_tests
)

add_test(
    NAME platformator_scene_script_roundtrip_test
    COMMAND platformator_scene_script_roundtrip_test
)

add_test(
    NAME platformator_queued_deletion_safety_test
    COMMAND platformator_queued_deletion_safety_test
)
