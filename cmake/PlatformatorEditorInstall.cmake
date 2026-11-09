function(platformator_install_editor)
    if(NOT TARGET platformator_editor)
        message(FATAL_ERROR "platformator_install_editor() requires PLATFORMATOR_BUILD_EDITOR=ON")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The editor install layout is currently validated only on Linux")
    endif()

    # SDL and oneTBB shared dependencies are installed into CMAKE_INSTALL_LIBDIR
    # by their upstream CMake install rules. The editor's $ORIGIN/../lib RPATH
    # keeps that dependency lookup independent of the build and source trees.
    install(TARGETS platformator_editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/editor/resources/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/platformator/editor")
    install(FILES "${PROJECT_SOURCE_DIR}/cmake/PlatformatorEditorThirdPartyNotices.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/platformator-editor")

    set(editor_license_directory "${CMAKE_INSTALL_DATADIR}/doc/platformator-editor/licenses")
    install(FILES "${PROJECT_SOURCE_DIR}/vendored/SDL3-3.4.18/LICENSE.txt"
        DESTINATION "${editor_license_directory}" RENAME SDL3-LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/vendored/SDL3_image-3.4.8/LICENSE.txt"
        DESTINATION "${editor_license_directory}" RENAME SDL3-image-LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/vendored/SDL3_mixer-3.2.4/LICENSE.txt"
        DESTINATION "${editor_license_directory}" RENAME SDL3-mixer-LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/vendored/SDL3_ttf-3.2.2/LICENSE.txt"
        DESTINATION "${editor_license_directory}" RENAME SDL3-ttf-LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b/LICENSE.txt"
        DESTINATION "${editor_license_directory}" RENAME Dear-ImGui-LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/vendored/oneTBB-2022.3.0/LICENSE.txt"
        DESTINATION "${editor_license_directory}" RENAME oneTBB-LICENSE.txt)
    install(FILES
        "${PROJECT_SOURCE_DIR}/vendored/eigen-5.0.1/LICENSE"
        "${PROJECT_SOURCE_DIR}/vendored/eigen-5.0.1/COPYING.README"
        "${PROJECT_SOURCE_DIR}/vendored/eigen-5.0.1/COPYING.MPL2"
        "${PROJECT_SOURCE_DIR}/vendored/eigen-5.0.1/COPYING.MINPACK"
        "${PROJECT_SOURCE_DIR}/vendored/eigen-5.0.1/COPYING.BSD"
        "${PROJECT_SOURCE_DIR}/vendored/eigen-5.0.1/COPYING.APACHE"
        DESTINATION "${editor_license_directory}")
endfunction()
