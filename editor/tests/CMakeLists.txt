function(platformator_editor_add_test name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
    target_link_libraries(${name} PRIVATE platformator_editor_core Platformator::platformator)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "platformator;editor"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
endfunction()

add_executable(platformator_editor_process_fixture process_fixture.cpp)
target_compile_features(platformator_editor_process_fixture PRIVATE cxx_std_20)

add_executable(platformator_editor_invalid_catalog_fixture process_fixture.cpp)
target_compile_features(platformator_editor_invalid_catalog_fixture PRIVATE cxx_std_20)
target_compile_definitions(platformator_editor_invalid_catalog_fixture PRIVATE
    PLATFORMATOR_EDITOR_INVALID_CATALOG=1)

add_executable(platformator_editor_catalog_process_fixture catalog_process_fixture.cpp)
target_compile_features(platformator_editor_catalog_process_fixture PRIVATE cxx_std_20)

platformator_editor_add_test(platformator_editor_process_test process_test.cpp)
target_compile_definitions(platformator_editor_process_test PRIVATE
    PLATFORMATOR_EDITOR_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_process_fixture>"
    PLATFORMATOR_EDITOR_INVALID_CATALOG_FIXTURE="$<TARGET_FILE:platformator_editor_invalid_catalog_fixture>")
add_dependencies(platformator_editor_process_test
    platformator_editor_process_fixture platformator_editor_invalid_catalog_fixture)

platformator_editor_add_test(platformator_editor_document_test document_test.cpp)

platformator_editor_add_test(platformator_editor_catalog_test catalog_test.cpp catalog_fixture.cpp)
target_compile_definitions(platformator_editor_catalog_test PRIVATE
    PLATFORMATOR_EDITOR_CATALOG_PROCESS_FIXTURE="$<TARGET_FILE:platformator_editor_catalog_process_fixture>")
add_dependencies(platformator_editor_catalog_test platformator_editor_catalog_process_fixture)

platformator_editor_add_test(platformator_editor_workspace_test
    workspace_test.cpp
    async_request_cases.cpp
    project_test.cpp
    editcommand_test.cpp
    workspace_lifecycle_test.cpp)

platformator_editor_add_test(platformator_editor_startup_test startup_test.cpp)
target_include_directories(platformator_editor_startup_test PRIVATE
    "${PROJECT_SOURCE_DIR}/vendored/imgui-1.92.9b")
target_link_libraries(platformator_editor_startup_test PRIVATE platformator_imgui)

platformator_editor_add_test(platformator_editor_preview_test preview_test.cpp viewport_cases.cpp)
target_compile_definitions(platformator_editor_preview_test PRIVATE
    PLATFORMATOR_EDITOR_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

platformator_editor_add_test(platformator_editor_history_test history_test.cpp)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME platformator_editor_capture_budget_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/capture_budget_test.py")
set_tests_properties(platformator_editor_capture_budget_test PROPERTIES
    LABELS "platformator;editor;benchmark"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
