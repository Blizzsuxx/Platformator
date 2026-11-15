cmake_minimum_required(VERSION 3.16)
project(PlatformatorCrystalCitadelExample LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLATFORMATOR_CRYSTAL_CITADEL_USE_INSTALLED
    "Link against an installed Platformator package instead of the source tree." OFF)

if(PLATFORMATOR_CRYSTAL_CITADEL_USE_INSTALLED)
    find_package(Platformator CONFIG REQUIRED)
elseif(NOT TARGET Platformator::platformator)
    set(PLATFORMATOR_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "Path to the Platformator source tree.")
    set(PLATFORMATOR_BUILD_RUNNER OFF CACHE BOOL "" FORCE)
    set(PLATFORMATOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PLATFORMATOR_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PLATFORMATOR_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/platformator")
endif()

add_executable(crystal_citadel
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_boss.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_camera_rig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_collectible.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_crate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_game.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_jump_pad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_moving_platform.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_orbit_shield.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_patrol_enemy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_player.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_portal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_projectile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_spawner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_splitting_slime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_timed_effect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/citadel_turret.cpp"
)

target_include_directories(crystal_citadel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors"
)

target_link_libraries(crystal_citadel PRIVATE Platformator::platformator)

if(PLATFORMATOR_CRYSTAL_CITADEL_USE_INSTALLED)
    set(PLATFORMATOR_CRYSTAL_CITADEL_ENGINE_ASSETS_SOURCE_DIR "${Platformator_ASSET_DIR}")
elseif(DEFINED PLATFORMATOR_SOURCE_DIR)
    set(PLATFORMATOR_CRYSTAL_CITADEL_ENGINE_ASSETS_SOURCE_DIR "${PLATFORMATOR_SOURCE_DIR}/assets")
else()
    set(PLATFORMATOR_CRYSTAL_CITADEL_ENGINE_ASSETS_SOURCE_DIR "${Platformator_SOURCE_DIR}/assets")
endif()

foreach(fallback textures/missing.png audio/missing.wav animations/missing.animset)
    if(NOT EXISTS "${PLATFORMATOR_CRYSTAL_CITADEL_ENGINE_ASSETS_SOURCE_DIR}/${fallback}")
        message(FATAL_ERROR "Crystal Citadel requires the packaged engine fallback: ${fallback}")
    endif()
endforeach()

set(PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_ASSETS_DIR "$<TARGET_FILE_DIR:crystal_citadel>/assets")
add_custom_command(TARGET crystal_citadel POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PLATFORMATOR_CRYSTAL_CITADEL_ENGINE_ASSETS_SOURCE_DIR}"
        "${PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_ASSETS_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_ASSETS_DIR}"
    VERBATIM
)

if(NOT DEFINED PLATFORMATOR_INSTALL_RPATH)
    if(APPLE)
        set(PLATFORMATOR_INSTALL_RPATH "@loader_path/../lib")
    elseif(UNIX)
        set(PLATFORMATOR_INSTALL_RPATH "$ORIGIN/../lib")
    endif()
endif()

if(DEFINED PLATFORMATOR_INSTALL_RPATH)
    set_target_properties(crystal_citadel PROPERTIES
        INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
    )
endif()

set(PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_LIBRARY_TARGETS)
foreach(platformator_runtime_target SDL3-shared SDL3_image-shared SDL3_mixer-shared SDL3_ttf-shared tbb)
    if(TARGET ${platformator_runtime_target})
        list(APPEND PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_LIBRARY_TARGETS ${platformator_runtime_target})
    endif()
endforeach()

install(TARGETS crystal_citadel
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

if(PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_LIBRARY_TARGETS)
    install(TARGETS ${PLATFORMATOR_CRYSTAL_CITADEL_RUNTIME_LIBRARY_TARGETS}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()

install(DIRECTORY "${PLATFORMATOR_CRYSTAL_CITADEL_ENGINE_ASSETS_SOURCE_DIR}/"
    DESTINATION assets PATTERN "test_runtime" EXCLUDE
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
    DESTINATION assets
)
