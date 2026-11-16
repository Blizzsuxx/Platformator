add_executable(platformator_editor_probe editor_probe.cpp)
target_compile_features(platformator_editor_probe PRIVATE cxx_std_20)
target_include_directories(platformator_editor_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(platformator_editor_probe PRIVATE platformator_editor_core Platformator::platformator)
target_compile_definitions(platformator_editor_probe PRIVATE
    PLATFORMATOR_EDITOR_PROBE_CONFIGURATION="$<CONFIG>"
    PLATFORMATOR_EDITOR_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

add_executable(platformator_editor_gui_probe gui_probe.cpp)
target_compile_features(platformator_editor_gui_probe PRIVATE cxx_std_20)
target_include_directories(platformator_editor_gui_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(platformator_editor_gui_probe PRIVATE platformator_editor_core Platformator::platformator)
target_compile_definitions(platformator_editor_gui_probe PRIVATE
    PLATFORMATOR_EDITOR_PROBE_CONFIGURATION="$<CONFIG>"
    PLATFORMATOR_EDITOR_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
