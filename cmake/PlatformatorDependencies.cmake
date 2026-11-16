# Only engine tests belong to an ordinary Platformator CTest run.
foreach(dependency_test_option TBB_TEST TBB_EXAMPLES EIGEN_BUILD_TESTING
        SDL_TESTS SDL_TEST_LIBRARY SDL_EXAMPLES SDLMIXER_TESTS SDLMIXER_EXAMPLES SDLIMAGE_TESTS)
    set(${dependency_test_option} OFF CACHE BOOL "Disabled in Platformator builds" FORCE)
endforeach()
# CMake leaves old CTest files behind when vendor testing is switched off in an
# existing build tree. Its parent traversal would still discover those tests.
file(GLOB_RECURSE platformator_stale_vendor_tests
    "${CMAKE_CURRENT_BINARY_DIR}/vendored/*/CTestTestfile.cmake")
if(platformator_stale_vendor_tests)
    file(REMOVE ${platformator_stale_vendor_tests})
endif()

# Use dependency-owned export/install rules for offline SDK consumption.
foreach(install_option SDL_INSTALL SDLIMAGE_INSTALL SDLMIXER_INSTALL SDLTTF_INSTALL TBB_INSTALL EIGEN_BUILD_CMAKE_PACKAGE)
    set(${install_option} ${PLATFORMATOR_INSTALL} CACHE BOOL "Follow Platformator SDK installation" FORCE)
endforeach()
foreach(relocatable_option SDL_RELOCATABLE SDLIMAGE_RELOCATABLE SDLMIXER_RELOCATABLE SDLTTF_RELOCATABLE)
    set(${relocatable_option} ON CACHE BOOL "Build relocatable bundled dependencies" FORCE)
endforeach()
set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF)
set(platformator_dependency_exclusion)
if(NOT PLATFORMATOR_INSTALL)
    set(platformator_dependency_exclusion EXCLUDE_FROM_ALL)
endif()
add_subdirectory(vendored/SDL3-3.4.18 ${platformator_dependency_exclusion})
add_subdirectory(vendored/SDL3_mixer-3.2.4 ${platformator_dependency_exclusion})
add_subdirectory(vendored/SDL3_image-3.4.8 ${platformator_dependency_exclusion})
add_subdirectory(vendored/SDL3_ttf-3.2.2 ${platformator_dependency_exclusion})
add_subdirectory(vendored/eigen-5.0.1 ${platformator_dependency_exclusion})
add_subdirectory(vendored/oneTBB-2022.3.0 ${platformator_dependency_exclusion})
# Eigen's dependent option hides BUILD_TESTING in the shared cache. Restore our
# public selector after configuring vendors; their test switches stay off.
set(BUILD_TESTING ${PLATFORMATOR_BUILD_TESTS} CACHE BOOL
    "Build engine/editor correctness tests" FORCE)


add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/json>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

