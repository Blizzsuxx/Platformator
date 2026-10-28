if(NOT DEFINED RUNNER OR NOT DEFINED OUTPUT_DIR OR NOT DEFINED PYTHON OR NOT DEFINED ROOT)
    message(FATAL_ERROR "RUNNER, OUTPUT_DIR, PYTHON and ROOT are required")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_fixture label csv)
    execute_process(COMMAND "${RUNNER}" ${ARGN} --warmup-frames 1 --measure-frames 2 --workers 1
        --capture-mode validate --render --csv-output "${csv}"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} failed (${result}): ${stdout}${stderr}")
    endif()
    set(${label}_MANIFEST "${csv}" PARENT_SCOPE)
endfunction()

function(validate_schema4 label csv submitted visible)
    execute_process(COMMAND "${PYTHON}" -c
        "import sys; sys.path.insert(0, r'${ROOT}/benchmark'); from spatial_results import read_schema4_capture; c=read_schema4_capture(r'${csv}'); m=c['manifest']; assert m['schema_version']==4 and m['input']['domain']=='engine' and m['complete_capture'] is True and m['sample_counts']['step']==2 and m['sample_counts']['render']==2; assert m['input']['case_sha256']; assert m['initialization']['total_ns'] >= 0; rows=[r for r in c['raw_samples'] if r['sample_kind']=='render']; assert all(int(r['submitted_sprite_count'])==${submitted} for r in rows); assert all(r['visible_candidate_count']=='' if ${visible} < 0 else int(r['visible_candidate_count'])==${visible} for r in rows)"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} schema-4 capture was rejected: ${stdout}${stderr}")
    endif()
endfunction()

set(SPRITE_CAPTURE "${OUTPUT_DIR}/sprite.csv")
run_fixture(sprite "${SPRITE_CAPTURE}" --scenario sprite_visibility --sprite-count 1000
    --visible-percent 1 --changed-percent 10 --spatial-variant animation)
validate_schema4(sprite "${SPRITE_CAPTURE}" 1000 -1)
set(SPRITE_MANIFEST "${SPRITE_CAPTURE}")
string(REPLACE ".csv" ".manifest.json" SPRITE_MANIFEST "${SPRITE_MANIFEST}")
file(READ "${SPRITE_MANIFEST}" sprite_json)
foreach(expected IN ITEMS "\"visible_count\": 10" "\"changed_count\": 100" "\"sprite_count\": 1000"
                         "\"renderer_signature\"" "\"backend\"" "\"output_size\"")
    string(FIND "${sprite_json}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Sprite visibility fixture did not record ${expected}")
    endif()
endforeach()

set(COMBINED_CAPTURE "${OUTPUT_DIR}/combined.csv")
run_fixture(combined "${COMBINED_CAPTURE}" --scenario spatial_combined --sprite-count 1000
    --visible-percent 1 --changed-percent 10 --spatial-variant moving --broad-phase bvh --render-culling segmented)
validate_schema4(combined "${COMBINED_CAPTURE}" 10 10)
set(COMBINED_MANIFEST "${COMBINED_CAPTURE}")
string(REPLACE ".csv" ".manifest.json" COMBINED_MANIFEST "${COMBINED_MANIFEST}")
file(READ "${COMBINED_MANIFEST}" combined_json)
foreach(expected IN ITEMS "\"combined_collider_count\": 128" "\"visible_count\": 10" "\"changed_count\": 100")
    string(FIND "${combined_json}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Combined fixture did not record ${expected}")
    endif()
endforeach()

set(HEADLESS_CAPTURE "${OUTPUT_DIR}/headless.csv")
execute_process(COMMAND "${RUNNER}" --scenario broad_phase --broad-count 16 --warmup-frames 1
    --measure-frames 2 --workers 1 --capture-mode validate --csv-output "${HEADLESS_CAPTURE}"
    RESULT_VARIABLE headless_result OUTPUT_VARIABLE headless_stdout ERROR_VARIABLE headless_stderr)
if(NOT headless_result EQUAL 0)
    message(FATAL_ERROR "Headless schema-4 fixture failed (${headless_result}): ${headless_stdout}${headless_stderr}")
endif()
execute_process(COMMAND "${PYTHON}" -c
    "import sys; sys.path.insert(0, r'${ROOT}/benchmark'); from spatial_results import read_schema4_capture; c=read_schema4_capture(r'${HEADLESS_CAPTURE}'); assert c['manifest']['sample_counts']['render']==0; assert not any(r['sample_kind']=='render' for r in c['raw_samples'])"
    RESULT_VARIABLE headless_parse OUTPUT_VARIABLE headless_parse_stdout ERROR_VARIABLE headless_parse_stderr)
if(NOT headless_parse EQUAL 0)
    message(FATAL_ERROR "Headless engine capture manufactured render rows: ${headless_parse_stdout}${headless_parse_stderr}")
endif()
