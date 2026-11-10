function(platformator_add_test name source)
    cmake_parse_arguments(TEST "INTERNAL;PROBE" "" "LABELS" ${ARGN})
    add_executable(${name} "${PROJECT_SOURCE_DIR}/${source}")
    target_link_libraries(${name} PRIVATE Platformator::platformator)
    if(TEST_INTERNAL)
        target_include_directories(${name} PRIVATE
            "${PROJECT_SOURCE_DIR}/src" "${PROJECT_BINARY_DIR}/generated")
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "platformator;${TEST_LABELS}"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
    if(TEST_PROBE)
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
        set_property(TEST ${name} APPEND PROPERTY LABELS probe)
    endif()
endfunction()
