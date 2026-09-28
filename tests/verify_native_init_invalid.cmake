if(NOT DEFINED BUDO OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "BUDO and TEST_ROOT are required")
endif()

foreach(arguments
        "--language;rust"
        "--template;gpu"
        "--language;c;--template;unknown"
        "--language;c;--language;c"
        "--language;c;extra")
    file(REMOVE_RECURSE "${TEST_ROOT}")
    execute_process(
        COMMAND "${BUDO}" init "${TEST_ROOT}" ${arguments}
        RESULT_VARIABLE result
        OUTPUT_QUIET ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "Invalid init options unexpectedly succeeded: ${arguments}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(WRITE "${TEST_ROOT}/main.js" "console.log('managed');\n")
execute_process(
    COMMAND "${BUDO}" init "${TEST_ROOT}" --language c
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "Native init unexpectedly accepted a managed project")
endif()
if(EXISTS "${TEST_ROOT}/main.c" OR EXISTS "${TEST_ROOT}/budo-native.json")
    message(FATAL_ERROR "Rejected mixed-project init wrote native files")
endif()
