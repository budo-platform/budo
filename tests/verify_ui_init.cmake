# `budo init --template ui`: the project gets the budo-ui library under ui/,
# identical to examples/budo-ui (budo.d.ts references one level shallower),
# and a starter main.js that runs against it (checked with node when found).
if(NOT DEFINED BUDO OR NOT DEFINED TEST_ROOT OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "BUDO, TEST_ROOT, and SOURCE_DIR are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(WRITE "${TEST_ROOT}.stdin" "")
execute_process(
    COMMAND "${BUDO}" init "${TEST_ROOT}" --template ui
    INPUT_FILE "${TEST_ROOT}.stdin"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "UI init failed (${result}):\n${output}\n${error}")
endif()
foreach(path main.js app.json budo.d.ts budo-llm.md jsconfig.json ui/budo-ui.js ui/README.md)
    if(NOT EXISTS "${TEST_ROOT}/${path}")
        message(FATAL_ERROR "UI init did not create ${path}")
    endif()
endforeach()

file(READ "${TEST_ROOT}/main.js" generated)
file(READ "${SOURCE_DIR}/src/core/templates/ui/main.js" expected)
if(NOT generated STREQUAL expected)
    message(FATAL_ERROR "main.js differs from src/core/templates/ui/main.js")
endif()

set(library_dir "${SOURCE_DIR}/examples/budo-ui")
file(GLOB_RECURSE library RELATIVE "${library_dir}" "${library_dir}/lib/*.js")
list(APPEND library budo-ui.js README.md)
foreach(path IN LISTS library)
    if(NOT EXISTS "${TEST_ROOT}/ui/${path}")
        message(FATAL_ERROR "UI init did not create ui/${path} (rebuild budo after adding library files)")
    endif()
    file(READ "${library_dir}/${path}" expected)
    string(REGEX REPLACE "(^|\n)/// <reference path=\"\\.\\./" "\\1/// <reference path=\"" expected "${expected}")
    file(READ "${TEST_ROOT}/ui/${path}" generated)
    if(NOT generated STREQUAL expected)
        message(FATAL_ERROR "ui/${path} differs from examples/budo-ui/${path} (rebuild budo)")
    endif()
endforeach()
file(GLOB_RECURSE generated_library RELATIVE "${TEST_ROOT}/ui" "${TEST_ROOT}/ui/*")
list(LENGTH library expected_count)
list(LENGTH generated_library generated_count)
if(NOT expected_count EQUAL generated_count)
    message(FATAL_ERROR "ui/ has ${generated_count} files, examples/budo-ui has ${expected_count}:\n${generated_library}")
endif()

if(DEFINED NODE AND NODE)
    execute_process(
        COMMAND "${NODE}" "${SOURCE_DIR}/tests/ui_template_test.mjs" "${TEST_ROOT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "The UI starter failed under node (${result}):\n${output}\n${error}")
    endif()
    message(STATUS "${output}")
endif()
