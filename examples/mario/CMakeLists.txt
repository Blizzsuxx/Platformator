if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(PlatformatorMarioExample LANGUAGES CXX C)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

option(PLATFORMATOR_MARIO_USE_INSTALLED "Link against an installed Platformator package instead of the source tree." OFF)

if(PLATFORMATOR_MARIO_USE_INSTALLED)
    find_package(Platformator CONFIG REQUIRED)
elseif(NOT TARGET Platformator::platformator)
    set(PLATFORMATOR_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "Path to the Platformator source tree.")
    set(PLATFORMATOR_BUILD_RUNNER OFF CACHE BOOL "" FORCE)
    set(PLATFORMATOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(PLATFORMATOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PLATFORMATOR_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PLATFORMATOR_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/platformator")
endif()

add_executable(mario_example
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mario_camera_rig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mario_coin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mario_game.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mario_goal_flag.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mario_patrol_enemy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mario_player.cpp"
)

target_include_directories(mario_example PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(mario_example PRIVATE Platformator::platformator)

if(PLATFORMATOR_MARIO_USE_INSTALLED)
    set(PLATFORMATOR_MARIO_ENGINE_ASSETS_SOURCE_DIR "${Platformator_ASSET_DIR}")
elseif(DEFINED PLATFORMATOR_SOURCE_DIR)
    set(PLATFORMATOR_MARIO_ENGINE_ASSETS_SOURCE_DIR "${PLATFORMATOR_SOURCE_DIR}/assets")
else()
    set(PLATFORMATOR_MARIO_ENGINE_ASSETS_SOURCE_DIR "${Platformator_SOURCE_DIR}/assets")
endif()
foreach(fallback textures/missing.png audio/missing.wav animations/missing.animset)
    if(NOT EXISTS "${PLATFORMATOR_MARIO_ENGINE_ASSETS_SOURCE_DIR}/${fallback}")
        message(FATAL_ERROR "Mario requires the packaged engine fallback: ${fallback}")
    endif()
endforeach()
set(PLATFORMATOR_MARIO_RUNTIME_ASSETS_DIR "$<TARGET_FILE_DIR:mario_example>/assets")
add_custom_command(TARGET mario_example POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PLATFORMATOR_MARIO_ENGINE_ASSETS_SOURCE_DIR}" "${PLATFORMATOR_MARIO_RUNTIME_ASSETS_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${PLATFORMATOR_MARIO_RUNTIME_ASSETS_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PLATFORMATOR_MARIO_RUNTIME_ASSETS_DIR}/scenes"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/level1.scene" "${PLATFORMATOR_MARIO_RUNTIME_ASSETS_DIR}/scenes/mario_example.scene"
    VERBATIM)
install(DIRECTORY "${PLATFORMATOR_MARIO_ENGINE_ASSETS_SOURCE_DIR}/" DESTINATION assets PATTERN "test_runtime" EXCLUDE)

if(NOT DEFINED PLATFORMATOR_INSTALL_RPATH)
    if(APPLE)
        set(PLATFORMATOR_INSTALL_RPATH "@loader_path/../lib")
    elseif(UNIX)
        set(PLATFORMATOR_INSTALL_RPATH "$ORIGIN/../lib")
    endif()
endif()

if(DEFINED PLATFORMATOR_INSTALL_RPATH)
    set_target_properties(mario_example PROPERTIES
        INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
    )
endif()

set(PLATFORMATOR_MARIO_RUNTIME_LIBRARY_TARGETS)
foreach(platformator_runtime_target SDL3-shared SDL3_image-shared SDL3_mixer-shared SDL3_ttf-shared tbb)
    if(TARGET ${platformator_runtime_target})
        list(APPEND PLATFORMATOR_MARIO_RUNTIME_LIBRARY_TARGETS ${platformator_runtime_target})
    endif()
endforeach()

install(TARGETS mario_example
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

if(PLATFORMATOR_MARIO_RUNTIME_LIBRARY_TARGETS)
    install(TARGETS ${PLATFORMATOR_MARIO_RUNTIME_LIBRARY_TARGETS}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
    DESTINATION assets
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/level1.scene"
    DESTINATION assets/scenes
    RENAME mario_example.scene
)