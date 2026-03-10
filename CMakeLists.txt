cmake_minimum_required(VERSION 3.16)

project(Platformator LANGUAGES CXX)

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


file(GLOB_RECURSE PLATFORMATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/*.c"
)

add_executable(main ${PLATFORMATOR_SOURCES})

target_include_directories(main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored"
)

# target_link_libraries(main PRIVATE
#     SDL3::SDL3
#     SDL3_mixer::SDL3_mixer
#     SDL3_image::SDL3_image
#     SDL3_ttf::SDL3_ttf
#     eigen3::eigen
# )
