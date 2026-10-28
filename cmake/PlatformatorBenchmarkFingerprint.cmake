function(platformator_compute_benchmark_source_fingerprint root output_hash output_inputs)
    set(input_patterns
        "${root}/src/*"
        "${root}/include/platformator/*"
        "${root}/cmake/*"
        "${root}/benchmark/*.py"
        "${root}/benchmark/*.cpp"
        "${root}/benchmark/*.h"
        "${root}/benchmark/tests/*.py"
        "${root}/benchmark/fixtures/*"
        "${root}/tests/*.cpp"
        "${root}/tests/*.h"
        "${root}/tests/*.py"
        "${root}/tests/*.cmake"
        "${root}/tests/fixtures/*"
        "${root}/vendored/box2d-3.2.0/src/*"
        "${root}/vendored/box2d-3.2.0/include/*")
    if(CMAKE_SCRIPT_MODE_FILE)
        file(GLOB_RECURSE inputs ${input_patterns})
    else()
        file(GLOB_RECURSE inputs CONFIGURE_DEPENDS ${input_patterns})
    endif()
    list(APPEND inputs
        "${root}/CMakeLists.txt"
        "${root}/vendored/SDL3_mixer-3.2.4/src/decoder_wav.c"
        "${root}/vendored/box2d-3.2.0/CMakeLists.txt"
        "${root}/vendored/box2d-3.2.0/UPSTREAM.sha256")
    list(REMOVE_DUPLICATES inputs)
    list(SORT inputs)
    set(source_hashes "")
    foreach(source_file IN LISTS inputs)
        if(EXISTS "${source_file}" AND NOT IS_DIRECTORY "${source_file}")
            file(SHA256 "${source_file}" content_hash)
            file(RELATIVE_PATH source_name "${root}" "${source_file}")
            string(APPEND source_hashes "${source_name}:${content_hash}\n")
        endif()
    endforeach()
    string(SHA256 fingerprint "${source_hashes}")
    set(${output_hash} "${fingerprint}" PARENT_SCOPE)
    set(${output_inputs} "${inputs}" PARENT_SCOPE)
endfunction()
