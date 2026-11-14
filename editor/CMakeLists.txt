if(NOT PLATFORMATOR_BUILD_EDITOR)
    message(FATAL_ERROR "The editor subdirectory requires PLATFORMATOR_BUILD_EDITOR=ON.")
endif()

option(PLATFORMATOR_EDITOR_WORK_COUNTERS "Enable editor work counters for focused correctness and performance probes" OFF)

include(CheckSymbolExists)
check_symbol_exists(posix_spawn_file_actions_addchdir "spawn.h"
    PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
if(NOT PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(posix_spawn_file_actions_addchdir_np "spawn.h"
        PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    unset(CMAKE_REQUIRED_DEFINITIONS)
endif()

set(PLATFORMATOR_IMGUI_DIR "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")

set(PLATFORMATOR_EDITOR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PLATFORMATOR_EDITOR_GENERATED_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/default-layout.ini")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/default-layout.ini"
    PLATFORMATOR_DEFAULT_LAYOUT_CONTENT)
set(PLATFORMATOR_DEFAULT_LAYOUT_DELIMITER ")PADL01\"")
string(FIND "${PLATFORMATOR_DEFAULT_LAYOUT_CONTENT}" "${PLATFORMATOR_DEFAULT_LAYOUT_DELIMITER}"
    PLATFORMATOR_DEFAULT_LAYOUT_DELIMITER_POSITION)
if(NOT PLATFORMATOR_DEFAULT_LAYOUT_DELIMITER_POSITION EQUAL -1)
    message(FATAL_ERROR "The editor default layout contains its reserved C++ raw-string delimiter.")
endif()
configure_file(src/defaultlayout.h.in "${PLATFORMATOR_EDITOR_GENERATED_DIR}/defaultlayout.h" @ONLY)

if(WIN32)
    set(PLATFORMATOR_PROJECT_WATCH_SOURCE src/platform/watch_windows.cpp)
    set(PLATFORMATOR_SESSION_CHANNEL_SOURCE src/platform/sessionchannel_windows.cpp)
    set(PLATFORMATOR_NATIVE_PROCESS_SOURCE src/process_windows.cpp)
    set(PLATFORMATOR_NATIVE_FILE_SOURCE src/platform/file_windows.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    set(PLATFORMATOR_PROJECT_WATCH_SOURCE src/platform/watch_macos.mm)
    set(PLATFORMATOR_SESSION_CHANNEL_SOURCE src/platform/sessionchannel_posix.cpp)
    set(PLATFORMATOR_NATIVE_PROCESS_SOURCE src/process_posix.cpp)
    set(PLATFORMATOR_NATIVE_FILE_SOURCE src/platform/file_posix.cpp)
else()
    set(PLATFORMATOR_PROJECT_WATCH_SOURCE src/platform/watch_linux.cpp)
    set(PLATFORMATOR_SESSION_CHANNEL_SOURCE src/platform/sessionchannel_posix.cpp)
    set(PLATFORMATOR_NATIVE_PROCESS_SOURCE src/process_posix.cpp)
    set(PLATFORMATOR_NATIVE_FILE_SOURCE src/platform/file_posix.cpp)
endif()

add_library(platformator_imgui STATIC
    "${PLATFORMATOR_IMGUI_DIR}/imgui.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/imgui_draw.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/imgui_tables.cpp"
    "${PLATFORMATOR_IMGUI_DIR}/imgui_widgets.cpp"
    src/backends/imgui_impl_sdl3_editor.cpp
    "${PLATFORMATOR_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
)
target_compile_features(platformator_imgui PRIVATE cxx_std_20)
target_include_directories(platformator_imgui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PLATFORMATOR_IMGUI_DIR}"
    "${PLATFORMATOR_IMGUI_DIR}/backends"
)
target_link_libraries(platformator_imgui PRIVATE SDL3::SDL3)

add_library(platformator_editor_core STATIC
    src/animationeditor.cpp
    src/accessibility.cpp
    src/assetdocument.cpp
    src/actions.cpp
    src/authoringindex.cpp
    src/camerainterpretation.cpp
    src/componentoperations.cpp
    src/document.cpp
    src/documentmanager.cpp
    src/documentio.cpp
    src/diagnostics.cpp
    src/editorresources.cpp
    src/editcommands.cpp
    src/editorapp.cpp
    src/filedialogs.cpp
    src/imagepreflight.cpp
    src/process.cpp
    ${PLATFORMATOR_NATIVE_PROCESS_SOURCE}
    ${PLATFORMATOR_NATIVE_FILE_SOURCE}
    ${PLATFORMATOR_SESSION_CHANNEL_SOURCE}
    src/preferences.cpp
    src/project.cpp
    src/projectchanges.cpp
    src/projectsession.cpp
    src/resourceindex.cpp
    src/layersettings.cpp
    src/sourcescanner.cpp
    src/audiopreview.cpp
    src/templates.cpp
    ${PLATFORMATOR_PROJECT_WATCH_SOURCE}
    src/behaviorcatalog.cpp
    src/preview.cpp
    src/style.cpp
    src/thumbnailcache.cpp
    src/toolbar.cpp
    src/uitextures.cpp
    src/windowhost.cpp
    src/viewport.cpp
    src/panels/console.cpp
    src/panels/behaviorfields.cpp
    src/panels/animation.cpp
    src/panels/hierarchy.cpp
    src/panels/inspector.cpp
    src/panels/project.cpp
    src/panels/referencefields.cpp
    src/panels/projectsettings.cpp
    src/panels/spriteeditor.cpp
    src/selection.cpp
    src/validation.cpp
    src/workspace.cpp
)
target_compile_features(platformator_editor_core PRIVATE cxx_std_20)
target_include_directories(platformator_editor_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PLATFORMATOR_EDITOR_GENERATED_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated"
    "${PLATFORMATOR_IMGUI_DIR}"
    "${PLATFORMATOR_IMGUI_DIR}/backends"
)
target_compile_definitions(platformator_editor_core PRIVATE
    PLATFORMATOR_ENABLE_EDITOR_SESSION=$<BOOL:${PLATFORMATOR_ENABLE_EDITOR_SESSION}>
    PLATFORMATOR_EDITOR_RESOURCE_RELATIVE_PATH="../${CMAKE_INSTALL_DATADIR}/platformator/editor/default-layout.ini")
if(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    target_compile_definitions(platformator_editor_core PUBLIC PLATFORMATOR_EDITOR_WORK_COUNTERS=1)
endif()
target_link_libraries(platformator_editor_core PRIVATE
    platformator_imgui
    Platformator::platformator
    nlohmann_json::nlohmann_json
    TBB::tbb
)
if(PLATFORMATOR_ENABLE_EDITOR_SESSION)
    target_sources(platformator_editor_core PRIVATE
        src/playsession.cpp
        src/panels/game.cpp)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS1 REQUIRED IMPORTED_TARGET dbus-1)
    target_sources(platformator_editor_core PRIVATE src/platform/accessibility_linux.cpp)
    target_link_libraries(platformator_editor_core PRIVATE PkgConfig::DBUS1)
elseif(APPLE)
    target_sources(platformator_editor_core PRIVATE src/platform/accessibility_macos.mm)
    target_link_libraries(platformator_editor_core PRIVATE "-framework AppKit")
elseif(WIN32)
    target_sources(platformator_editor_core PRIVATE src/platform/accessibility_windows.cpp)
    target_link_libraries(platformator_editor_core PRIVATE Comctl32 OleAut32 Uiautomationcore)
endif()
if(APPLE)
    target_link_libraries(platformator_editor_core PRIVATE "-framework CoreServices")
endif()
if(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    target_compile_definitions(platformator_editor_core PRIVATE
        PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR=1)
elseif(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    target_compile_definitions(platformator_editor_core PRIVATE
        _GNU_SOURCE=1
        PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP=1)
endif()

add_executable(platformator_editor src/main.cpp)
target_compile_features(platformator_editor PRIVATE cxx_std_20)
target_link_libraries(platformator_editor PRIVATE platformator_editor_core)

add_executable(platformator_editor_probe benchmark/editor_probe.cpp)
target_compile_features(platformator_editor_probe PRIVATE cxx_std_20)
target_include_directories(platformator_editor_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(platformator_editor_probe PRIVATE platformator_editor_core Platformator::platformator)
target_compile_definitions(platformator_editor_probe PRIVATE
    PLATFORMATOR_EDITOR_PROBE_CONFIGURATION="$<CONFIG>"
    PLATFORMATOR_EDITOR_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

add_executable(platformator_editor_gui_probe benchmark/gui_probe.cpp)
target_compile_features(platformator_editor_gui_probe PRIVATE cxx_std_20)
target_include_directories(platformator_editor_gui_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(platformator_editor_gui_probe PRIVATE platformator_editor_core Platformator::platformator)
target_compile_definitions(platformator_editor_gui_probe PRIVATE
    PLATFORMATOR_EDITOR_PROBE_CONFIGURATION="$<CONFIG>"
    PLATFORMATOR_EDITOR_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

if(DEFINED PLATFORMATOR_INSTALL_RPATH)
    set_target_properties(platformator_editor PROPERTIES
        INSTALL_RPATH "${PLATFORMATOR_INSTALL_RPATH}"
    )
endif()
