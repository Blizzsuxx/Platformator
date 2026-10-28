if(NOT DEFINED RUNNER OR NOT DEFINED TRACE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "RUNNER, TRACE and OUTPUT_DIR are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(expect_rejection label expected)
    execute_process(COMMAND "${RUNNER}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(result EQUAL 0)
        message(FATAL_ERROR "${label}: runner unexpectedly succeeded")
    endif()
    string(CONCAT output "${stdout}" "${stderr}")
    string(FIND "${output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${label}: expected '${expected}', got '${output}'")
    endif()
endfunction()

expect_rejection("render is prohibited for replay" "cannot be combined with --render"
    --spatial-trace "${TRACE}" --render)
expect_rejection("scenario is exclusive with replay" "cannot be combined with a scene or --scenario"
    --spatial-trace "${TRACE}" --scenario broad_phase)
expect_rejection("scene is exclusive with replay" "cannot be combined with a scene or --scenario"
    --spatial-trace "${TRACE}" --scene fixture.scene)
expect_rejection("header warmup is authoritative" "contradicts the spatial trace header"
    --spatial-trace "${TRACE}" --warmup-frames 4 --csv-output "${OUTPUT_DIR}/unused.csv")
expect_rejection("header measurement count is authoritative" "contradicts the spatial trace header"
    --spatial-trace "${TRACE}" --measure-frames 4 --csv-output "${OUTPUT_DIR}/unused.csv")
expect_rejection("header dt is authoritative" "contradicts the spatial trace header"
    --spatial-trace "${TRACE}" --dt 0.25 --csv-output "${OUTPUT_DIR}/unused.csv")
expect_rejection("header seed is authoritative" "contradicts the spatial trace header"
    --spatial-trace "${TRACE}" --seed 17 --csv-output "${OUTPUT_DIR}/unused.csv")
expect_rejection("capture mode conflicts with legacy alias" "conflicts with --capture-mode performance"
    --spatial-trace "${TRACE}" --capture-mode performance --validate-invariants)
expect_rejection("actual sprite fixture requires rendering" "require --render"
    --scenario sprite_visibility --sprite-count 100 --visible-percent 10)
expect_rejection("spatial percentages are bounded" "between 0 and 100"
    --scenario sprite_visibility --sprite-count 100 --visible-percent 101 --render)
