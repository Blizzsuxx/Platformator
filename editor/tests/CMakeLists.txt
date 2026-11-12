function(platformator_editor_add_test name)
    add_executable(${name} ${ARGN})
    target_sources(${name} PRIVATE gui_fixtures.cpp)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${PROJECT_BINARY_DIR}/editor/generated"
        "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
    target_link_libraries(${name} PRIVATE platformator_editor_core Platformator::platformator)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "platformator;editor;editor-headless"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
endfunction()

function(platformator_editor_add_test_if_sources name)
    set(sources_exist TRUE)
    foreach(source IN LISTS ARGN)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
            set(sources_exist FALSE)
        endif()
    endforeach()
    if(sources_exist)
        platformator_editor_add_test(${name} ${ARGN})
    endif()
endfunction()

function(platformator_editor_add_desktop_test name)
    add_executable(${name} ${ARGN})
    target_sources(${name} PRIVATE gui_fixtures.cpp)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
    target_link_libraries(${name} PRIVATE platformator_editor_core Platformator::platformator)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "platformator;editor;editor-desktop")
endfunction()

add_executable(platformator_editor_process_fixture process_fixture.cpp)
target_compile_features(platformator_editor_process_fixture PRIVATE cxx_std_20)

add_executable(platformator_editor_invalid_catalog_fixture process_fixture.cpp)
target_compile_features(platformator_editor_invalid_catalog_fixture PRIVATE cxx_std_20)
target_compile_definitions(platformator_editor_invalid_catalog_fixture PRIVATE
    PLATFORMATOR_EDITOR_INVALID_CATALOG=1)

add_executable(platformator_editor_catalog_process_fixture catalog_process_fixture.cpp)
target_compile_features(platformator_editor_catalog_process_fixture PRIVATE cxx_std_20)

if(NOT WIN32)
    platformator_editor_add_test_if_sources(platformator_editor_process_test process_test.cpp)
    target_compile_definitions(platformator_editor_process_test PRIVATE
        PLATFORMATOR_EDITOR_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_process_fixture>"
        PLATFORMATOR_EDITOR_INVALID_CATALOG_FIXTURE="$<TARGET_FILE:platformator_editor_invalid_catalog_fixture>")
    add_dependencies(platformator_editor_process_test
        platformator_editor_process_fixture platformator_editor_invalid_catalog_fixture)
else()
    platformator_editor_add_test_if_sources(platformator_editor_nativeprocess_windows_test
        nativeprocess_windows_test.cpp)
    target_compile_definitions(platformator_editor_nativeprocess_windows_test PRIVATE
        PLATFORMATOR_EDITOR_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_process_fixture>")
    add_dependencies(platformator_editor_nativeprocess_windows_test platformator_editor_process_fixture)
endif()

platformator_editor_add_test_if_sources(platformator_editor_nativefile_test nativefile_test.cpp)

if(NOT WIN32)
    platformator_editor_add_test_if_sources(platformator_editor_nativeprocess_test nativeprocess_test.cpp)
    target_compile_definitions(platformator_editor_nativeprocess_test PRIVATE
        PLATFORMATOR_EDITOR_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_process_fixture>")
    add_dependencies(platformator_editor_nativeprocess_test platformator_editor_process_fixture)
endif()

platformator_editor_add_test_if_sources(platformator_editor_document_test document_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_assetdocument_test assetdocument_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_animation_test animation_test.cpp)
target_link_libraries(platformator_editor_animation_test PRIVATE platformator_imgui)
platformator_editor_add_test_if_sources(platformator_editor_numericdraft_interaction_test
    numericdraft_interaction_test.cpp)
target_link_libraries(platformator_editor_numericdraft_interaction_test PRIVATE platformator_imgui)
platformator_editor_add_test_if_sources(platformator_editor_accessibility_test accessibility_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_windowhost_test windowhost_test.cpp)
target_link_libraries(platformator_editor_windowhost_test PRIVATE platformator_imgui)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PLATFORMATOR_TEST_DBUS REQUIRED IMPORTED_TARGET dbus-1)
    add_executable(platformator_editor_accessibility_linux_test accessibility_linux_test.cpp)
    target_sources(platformator_editor_accessibility_linux_test PRIVATE gui_fixtures.cpp)
    target_compile_features(platformator_editor_accessibility_linux_test PRIVATE cxx_std_20)
    target_include_directories(platformator_editor_accessibility_linux_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
    target_link_libraries(platformator_editor_accessibility_linux_test PRIVATE
        platformator_editor_core Platformator::platformator PkgConfig::PLATFORMATOR_TEST_DBUS)
    find_program(PLATFORMATOR_DBUS_RUN_SESSION dbus-run-session)
    if(PLATFORMATOR_DBUS_RUN_SESSION)
        add_test(NAME platformator_editor_accessibility_linux_test
            COMMAND "${PLATFORMATOR_DBUS_RUN_SESSION}" --
                $<TARGET_FILE:platformator_editor_accessibility_linux_test>)
    else()
        add_test(NAME platformator_editor_accessibility_linux_test
            COMMAND $<TARGET_FILE:platformator_editor_accessibility_linux_test>)
    endif()
    set_tests_properties(platformator_editor_accessibility_linux_test PROPERTIES
        LABELS "platformator;editor;editor-headless;linux")
endif()
platformator_editor_add_test_if_sources(platformator_editor_sprite_region_test sprite_region_test.cpp)

platformator_editor_add_test_if_sources(platformator_editor_catalog_test catalog_test.cpp catalog_fixture.cpp)
target_include_directories(platformator_editor_catalog_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(platformator_editor_catalog_test PRIVATE
    PLATFORMATOR_EDITOR_CATALOG_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_catalog_process_fixture>")

platformator_editor_add_test_if_sources(platformator_editor_protocol_test editorprotocol_test.cpp)
target_include_directories(platformator_editor_protocol_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_sources(platformator_editor_protocol_test PRIVATE "${PROJECT_SOURCE_DIR}/src/editorprotocol.cpp")
target_link_libraries(platformator_editor_protocol_test PRIVATE nlohmann_json::nlohmann_json)
add_executable(platformator_editor_playsession_test playsession_test.cpp)
target_compile_features(platformator_editor_playsession_test PRIVATE cxx_std_20)
target_include_directories(platformator_editor_playsession_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
if(PLATFORMATOR_ENABLE_EDITOR_SESSION)
    target_link_libraries(platformator_editor_playsession_test PRIVATE
        platformator_editor_core platformator_imgui Platformator::platformator nlohmann_json::nlohmann_json)
else()
    target_sources(platformator_editor_playsession_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/playsession.cpp"
        "${PROJECT_SOURCE_DIR}/src/editorprotocol.cpp")
    target_link_libraries(platformator_editor_playsession_test PRIVATE
        platformator_editor_core platformator_imgui Platformator::platformator nlohmann_json::nlohmann_json)
endif()
add_test(NAME platformator_editor_playsession_test COMMAND platformator_editor_playsession_test)
set_tests_properties(platformator_editor_playsession_test PROPERTIES
    LABELS "platformator;editor;editor-headless"
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
if(NOT WIN32 AND PLATFORMATOR_ENABLE_EDITOR_SESSION)
    add_executable(platformator_editor_session_process_fixture session_process_fixture.cpp)
    target_compile_features(platformator_editor_session_process_fixture PRIVATE cxx_std_20)
    target_include_directories(platformator_editor_session_process_fixture PRIVATE
        "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(platformator_editor_session_process_fixture PRIVATE
        Platformator::platformator nlohmann_json::nlohmann_json)
    target_compile_definitions(platformator_editor_playsession_test PRIVATE
        PLATFORMATOR_EDITOR_SESSION_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_session_process_fixture>")
    add_dependencies(platformator_editor_playsession_test
        platformator_editor_session_process_fixture)
endif()
add_dependencies(platformator_editor_catalog_test platformator_editor_catalog_process_fixture)

platformator_editor_add_test_if_sources(platformator_editor_workspace_test
    workspace_test.cpp
    async_request_cases.cpp
    project_test.cpp
    editcommand_test.cpp
    workspace_lifecycle_test.cpp)

platformator_editor_add_test_if_sources(platformator_editor_startup_test startup_test.cpp)
target_include_directories(platformator_editor_startup_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
target_link_libraries(platformator_editor_startup_test PRIVATE platformator_imgui)

platformator_editor_add_test_if_sources(platformator_editor_preview_test preview_test.cpp viewport_cases.cpp)
target_compile_definitions(platformator_editor_preview_test PRIVATE
    PLATFORMATOR_EDITOR_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
platformator_editor_add_test_if_sources(platformator_editor_resources_test resources_test.cpp)

platformator_editor_add_test_if_sources(platformator_editor_history_test history_test.cpp)

platformator_editor_add_test_if_sources(platformator_editor_session_context_test
    session_context_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_tabs_test tabs_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_batchcommand_test
    batchcommand_cases.cpp)
platformator_editor_add_test_if_sources(platformator_editor_component_operations_test
    component_operations_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_template_test templates_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_authoringindex_test
    authoringindex_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_changes_test changes_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_asset_watch_integration_test
    asset_watch_integration_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_console_test console_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_diagnostics_test diagnostics_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_inspector_interaction_test
    inspector_interaction_test.cpp)
target_link_libraries(platformator_editor_inspector_interaction_test PRIVATE platformator_imgui)
platformator_editor_add_test_if_sources(platformator_editor_selection_test selection_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_hierarchy_move_test hierarchy_move_test.cpp)
target_link_libraries(platformator_editor_hierarchy_move_test PRIVATE platformator_imgui)
platformator_editor_add_test_if_sources(platformator_editor_discovery_test discovery_test.cpp)
platformator_editor_add_test_if_sources(platformator_editor_project_watch_test project_watch_test.cpp)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME platformator_editor_capture_budget_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/capture_budget_test.py")
set_tests_properties(platformator_editor_capture_budget_test PROPERTIES
    LABELS "platformator;editor;benchmark"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

add_test(NAME platformator_editor_gui_capture_budget_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gui_capture_budget_test.py")
set_tests_properties(platformator_editor_gui_capture_budget_test PROPERTIES
    LABELS "platformator;editor;benchmark"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

add_test(NAME platformator_editor_gui_capture_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gui_capture_test.py")
set_tests_properties(platformator_editor_gui_capture_test PROPERTIES
    LABELS "platformator;editor;benchmark"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
