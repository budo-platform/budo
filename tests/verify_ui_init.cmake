# `budo init --template ui`: the project gets the budo-ui library under ui/,
# byte-identical to examples/budo-ui/ui,
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

set(library_dir "${SOURCE_DIR}/examples/budo-ui/ui")
file(GLOB_RECURSE library LIST_DIRECTORIES false RELATIVE "${library_dir}" "${library_dir}/*")
foreach(path IN LISTS library)
    if(NOT EXISTS "${TEST_ROOT}/ui/${path}")
        message(FATAL_ERROR "UI init did not create ui/${path} (rebuild budo after adding library files)")
    endif()
    file(SHA256 "${library_dir}/${path}" expected)
    file(SHA256 "${TEST_ROOT}/ui/${path}" generated)
    if(NOT generated STREQUAL expected)
        message(FATAL_ERROR "ui/${path} differs from examples/budo-ui/ui/${path} (rebuild budo)")
    endif()
endforeach()
file(GLOB_RECURSE generated_library LIST_DIRECTORIES false RELATIVE "${TEST_ROOT}/ui" "${TEST_ROOT}/ui/*")
if(NOT "${generated_library}" STREQUAL "${library}")
    message(FATAL_ERROR "ui/ file set differs from examples/budo-ui/ui:\n${generated_library}")
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
