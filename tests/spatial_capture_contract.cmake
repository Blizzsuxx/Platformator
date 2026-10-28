if(NOT DEFINED RUNNER OR NOT DEFINED TRACE OR NOT DEFINED OUTPUT_DIR OR NOT DEFINED PYTHON OR NOT DEFINED ROOT)
    message(FATAL_ERROR "RUNNER, TRACE, OUTPUT_DIR, PYTHON and ROOT are required")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(CAPTURE "${OUTPUT_DIR}/run.csv")
execute_process(COMMAND "${RUNNER}" --spatial-trace "${TRACE}" --broad-phase bvh
    --capture-mode performance --workers 1 --csv-output "${CAPTURE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Replay capture failed (${result}): ${stdout}${stderr}")
endif()
execute_process(COMMAND "${PYTHON}" -c
    "import sys; sys.path.insert(0, r'${ROOT}/benchmark'); from spatial_results import read_schema4_capture; b=read_schema4_capture(r'${CAPTURE}'); assert b['manifest']['timing_eligible'] is True; assert b['manifest']['input']['case_sha256']; assert b['manifest']['run']['measured_steps'] == 1"
    RESULT_VARIABLE parse_result OUTPUT_VARIABLE parse_stdout ERROR_VARIABLE parse_stderr)
if(NOT parse_result EQUAL 0)
    message(FATAL_ERROR "Schema-4 reader rejected replay capture: ${parse_stdout}${parse_stderr}")
endif()

set(VALIDATION_CAPTURE "${OUTPUT_DIR}/validation.csv")
execute_process(COMMAND "${RUNNER}" --spatial-trace "${TRACE}" --broad-phase segmented
    --validate-invariants --workers 1 --csv-output "${VALIDATION_CAPTURE}"
    RESULT_VARIABLE validation_result OUTPUT_VARIABLE validation_stdout ERROR_VARIABLE validation_stderr)
if(NOT validation_result EQUAL 0)
    message(FATAL_ERROR "Validation replay capture failed (${validation_result}): ${validation_stdout}${validation_stderr}")
endif()
execute_process(COMMAND "${PYTHON}" -c
    "import json, pathlib; p=pathlib.Path(r'${VALIDATION_CAPTURE}').with_suffix('.manifest.json'); m=json.loads(p.read_text()); assert m['capture_mode'] == 'validate'; assert m['timing_eligible'] is False; assert m['validation']['status'] == 'passed'"
    RESULT_VARIABLE validation_parse_result OUTPUT_VARIABLE validation_parse_stdout ERROR_VARIABLE validation_parse_stderr)
if(NOT validation_parse_result EQUAL 0)
    message(FATAL_ERROR "Legacy validation alias was not recorded correctly: ${validation_parse_stdout}${validation_parse_stderr}")
endif()

execute_process(COMMAND "${RUNNER}" --spatial-trace "${TRACE}" --workers 1
    --csv-output "${OUTPUT_DIR}" RESULT_VARIABLE failure_result
    OUTPUT_VARIABLE failure_stdout ERROR_VARIABLE failure_stderr)
if(failure_result EQUAL 0 OR NOT "${failure_stdout}${failure_stderr}" MATCHES "Spatial replay output")
    message(FATAL_ERROR "Capture writer failure must be fatal and named: ${failure_stdout}${failure_stderr}")
endif()
