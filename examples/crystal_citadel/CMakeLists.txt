cmake_minimum_required(VERSION 3.21)
project(crystal_citadel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PLATFORMATOR_ENGINE_SOURCE_DIR "/home/user/Projects/Platformator" CACHE PATH
    "Fallback path to local Platformator source tree when Platformator CONFIG package is not installed")

if(NOT TARGET Platformator::platformator)
    find_package(Platformator CONFIG QUIET)
    if(NOT Platformator_FOUND)
        if(EXISTS "${PLATFORMATOR_ENGINE_SOURCE_DIR}/CMakeLists.txt")
            add_subdirectory("${PLATFORMATOR_ENGINE_SOURCE_DIR}"
                             "${CMAKE_CURRENT_BINARY_DIR}/platformator"
                             EXCLUDE_FROM_ALL)
        else()
            message(FATAL_ERROR
                "Platformator package not found and fallback source directory "
                "'${PLATFORMATOR_ENGINE_SOURCE_DIR}' does not contain CMakeLists.txt. "
                "Pass -DPlatformator_DIR=<prefix> or -DPLATFORMATOR_ENGINE_SOURCE_DIR=<repo>.")
        endif()
    endif()
endif()

file(GLOB_RECURSE PROJECT_BEHAVIOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/behaviors/*.hpp"
)

add_executable(crystal_citadel
    main.cpp
    ${PROJECT_BEHAVIOR_SOURCES}
)

target_include_directories(crystal_citadel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(crystal_citadel PRIVATE Platformator::platformator)

if(WIN32)
    add_custom_command(TARGET crystal_citadel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:crystal_citadel>
            $<TARGET_FILE_DIR:crystal_citadel>
        COMMAND_EXPAND_LISTS
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET crystal_citadel POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:crystal_citadel>/assets"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_FILE_DIR:crystal_citadel>/assets/behaviors"
        VERBATIM
    )
endif()

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

install(TARGETS crystal_citadel
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

set(PLATFORMATOR_PROJECT_RUNTIME_LIBRARY_TARGETS)
foreach(platformator_runtime_target SDL3-shared SDL3_image-shared SDL3_mixer-shared SDL3_ttf-shared tbb)
    if(TARGET ${platformator_runtime_target})
        get_target_property(platformator_runtime_imported ${platformator_runtime_target} IMPORTED)
        if(NOT platformator_runtime_imported)
            list(APPEND PLATFORMATOR_PROJECT_RUNTIME_LIBRARY_TARGETS ${platformator_runtime_target})
        endif()
    endif()
endforeach()

if(PLATFORMATOR_PROJECT_RUNTIME_LIBRARY_TARGETS)
    install(TARGETS ${PLATFORMATOR_PROJECT_RUNTIME_LIBRARY_TARGETS}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION assets
        PATTERN "behaviors" EXCLUDE
    )
endif()
