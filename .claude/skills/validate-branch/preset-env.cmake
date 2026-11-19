# Print the environment of a configure preset as KEY=VALUE lines, one per line, following
# "inherits" (parents first, so a child overrides its parents).
#
# Used by validate-branch.sh: `cmake --preset` and `ctest --preset` apply the environment of a
# preset (ASAN_OPTIONS, UBSAN_OPTIONS), but running the test binary directly does not. CMake is
# used to read the JSON so that no other tool (python, jq) is needed on Windows, Linux or macOS.
#
# Usage: cmake -DPRESETS_FILE=CMakePresets.json -DPRESET=asan -P preset-env.cmake

cmake_minimum_required(VERSION 3.26)

if(NOT PRESETS_FILE OR NOT PRESET)
    message(FATAL_ERROR "Usage: cmake -DPRESETS_FILE=<file> -DPRESET=<name> -P preset-env.cmake")
endif()

file(READ "${PRESETS_FILE}" presets_json)
string(JSON preset_count LENGTH "${presets_json}" configurePresets)

# Environment collected so far, in a cache so that it survives the recursion scopes.
set(collected_keys "" CACHE INTERNAL "")

# Store the JSON object of the named configure preset in <out_var>.
function(find_preset name out_var)
    math(EXPR last "${preset_count} - 1")
    foreach(i RANGE ${last})
        string(JSON candidate GET "${presets_json}" configurePresets ${i})
        string(JSON candidate_name GET "${candidate}" name)
        if(candidate_name STREQUAL name)
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Configure preset '${name}' not found in ${PRESETS_FILE}")
endfunction()

# Apply the environment of a preset, after the environment of the presets it inherits from.
function(apply_preset name)
    find_preset("${name}" preset_json)

    # "inherits" is a string or an array of strings.
    string(JSON inherits_type ERROR_VARIABLE inherits_error TYPE "${preset_json}" inherits)
    if(NOT inherits_error)
        if(inherits_type STREQUAL "STRING")
            string(JSON parent GET "${preset_json}" inherits)
            apply_preset("${parent}")
        elseif(inherits_type STREQUAL "ARRAY")
            string(JSON parent_count LENGTH "${preset_json}" inherits)
            if(parent_count GREATER 0)
                math(EXPR last "${parent_count} - 1")
                foreach(i RANGE ${last})
                    string(JSON parent GET "${preset_json}" inherits ${i})
                    apply_preset("${parent}")
                endforeach()
            endif()
        endif()
    endif()

    string(JSON env_type ERROR_VARIABLE env_error TYPE "${preset_json}" environment)
    if(NOT env_error AND env_type STREQUAL "OBJECT")
        string(JSON env_json GET "${preset_json}" environment)
        string(JSON env_count LENGTH "${env_json}")
        if(env_count GREATER 0)
            math(EXPR last "${env_count} - 1")
            foreach(i RANGE ${last})
                string(JSON key MEMBER "${env_json}" ${i})
                string(JSON value_type TYPE "${env_json}" "${key}")
                if(NOT value_type STREQUAL "STRING")
                    # A JSON null means "unset this variable", which cannot be applied by
                    # exporting a value. Fail instead of running in a different environment.
                    message(FATAL_ERROR
                        "Environment '${key}' of preset '${name}' is a ${value_type}, not a string")
                endif()
                string(JSON value GET "${env_json}" "${key}")
                if(value MATCHES "\\$")
                    # $env{}, $penv{} and other preset macros are not expanded here.
                    message(FATAL_ERROR
                        "Environment '${key}' of preset '${name}' uses a preset macro: ${value}")
                endif()
                set(env_value_${key} "${value}" CACHE INTERNAL "")
                list(APPEND collected_keys "${key}")
                list(REMOVE_DUPLICATES collected_keys)
                set(collected_keys "${collected_keys}" CACHE INTERNAL "")
            endforeach()
        endif()
    endif()
endfunction()

apply_preset("${PRESET}")

# Printed through the cmake executable so the lines go to stdout, not stderr.
foreach(key IN LISTS collected_keys)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${key}=${env_value_${key}}")
endforeach()
